/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2022 Oxide Computer Co
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/boot_data.h>
#include <sys/apic_common.h>
#include <sys/modctl.h>
#include <sys/x86_archext.h>
#include <sys/sysmacros.h>
#include <sys/boot_physmem.h>
#include <sys/boot_debug.h>
#include <sys/kernel_ipcc.h>

extern int bootrd_debug;
extern boolean_t kbm_debug;

/*
 * Some of these properties in the fallback set could also potentially be
 * defined as part of the machine architecture. More generally, there will be
 * some minimal collection of non-discoverable machine state that we must
 * either define or obtain from outside, which in the absence of a good way to
 * do that is mocked up here.
 *
 * OXIDE_UNIFIED_BOOT should be defined to use the phase1+phase2 zpool image
 * loaded from persistent storage or a network device.  If this is not defined,
 * we will boot using the UFS phase1 ramdisk as the rootfs.  XXX This can
 * probably be figured out at runtime.
 */

#define	OXIDE_UNIFIED_BOOT

#ifdef	OXIDE_UNIFIED_BOOT
static const bt_prop_t boot_image_ops_prop = {
	.btp_next = NULL,
	.btp_name = BTPROP_NAME_BOOT_IMAGE_OPS,
	.btp_vlen = sizeof ("misc/boot_image"),
	.btp_value = "misc/boot_image",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

#else

static const bt_prop_t fstype_prop = {
	.btp_next = NULL,
	.btp_name = BTPROP_NAME_FSTYPE,
	.btp_vlen = sizeof ("ufs"),
	.btp_value = "ufs",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};
#endif

static const bt_prop_t whoami_prop = {
#ifdef	OXIDE_UNIFIED_BOOT
	.btp_next = &boot_image_ops_prop,
#else
	.btp_next = &fstype_prop,
#endif
	.btp_name = "whoami",
	.btp_vlen = sizeof ("/platform/oxide/kernel/amd64/unix"),
	.btp_value = "/platform/oxide/kernel/amd64/unix",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

static const bt_prop_t impl_arch_prop = {
	.btp_next = &whoami_prop,
	.btp_name = BTPROP_NAME_IMPL_ARCH,
	.btp_vlen = sizeof ("oxide"),
	.btp_value = "oxide",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

static const bt_prop_t mfg_name_prop = {
	.btp_next = &impl_arch_prop,
	.btp_name = BTPROP_NAME_MFG,
	.btp_vlen = sizeof ("Oxide,Gimlet"),
	.btp_value = "Oxide,Gimlet",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

static const bt_prop_t bootargs_prop = {
	.btp_next = &mfg_name_prop,
	.btp_name = BTPROP_NAME_BOOTARGS,
	.btp_vlen = sizeof ("-kv"),
	.btp_value = "-kv",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

const bt_prop_t * const bt_fallback_props = &bootargs_prop;

const bt_prop_t *bt_props;
static uint8_t *bt_props_mem;
static size_t bt_props_avail;

#define	BTP_ALIGN(p) (((p) + 0xf) & ~0xf)
#define	BTP_ALIGNP(p) ((uint8_t *)BTP_ALIGN((uintptr_t)(p)))

void
bt_set_prop(uint32_t flags, const char *name, size_t nlen, const void *value,
    size_t vlen)
{
	bt_prop_t *btp;
	uint8_t *omem;
	size_t size;

	DBG_MSG("setprop %.*s\n", (int)nlen, name);

	size = sizeof (bt_prop_t) + nlen + 1;
	if (vlen > 0)
		size += BTP_ALIGN(vlen);
	size = BTP_ALIGN(size);

	/* If we are out of space in the current page, allocate a new one. */
	if (size > bt_props_avail) {
		bt_props_mem = (uint8_t *)eb_alloc_page();
		bt_props_avail = MMU_PAGESIZE;
	}

	omem = bt_props_mem;

	/*
	 * Use the space pointed to by bt_props_mem for the new bt_prop_t
	 * followed by the property name and a terminating nul byte, some
	 * padding to ensure that the value is aligned, and then the value
	 * itself.
	 */
	btp = (bt_prop_t *)bt_props_mem;
	bt_props_mem += sizeof (bt_prop_t);

	btp->btp_typeflags = flags;

	btp->btp_name = (char *)bt_props_mem;
	bcopy(name, btp->btp_name, nlen);
	btp->btp_name[nlen] = '\0';
	bt_props_mem += nlen + 1;

	btp->btp_vlen = vlen;
	if (vlen > 0) {
		/* Align for the value */
		btp->btp_value = bt_props_mem = BTP_ALIGNP(bt_props_mem);
		bcopy(value, bt_props_mem, vlen);
		bt_props_mem += vlen;
	}

	/* Align the pointer ready for the next property */
	bt_props_mem = BTP_ALIGNP(bt_props_mem);
	bt_props_avail -= (bt_props_mem - omem);

	btp->btp_next = bt_props;
	bt_props = btp;
}

static void
bt_set_prop_u8(const char *name, uint8_t value)
{
	uint32_t val = value;

	bt_set_prop(DDI_PROP_TYPE_INT, name, strlen(name),
	    (void *)&val, sizeof (val));
}

static void
bt_set_prop_u32(const char *name, uint32_t value)
{
	bt_set_prop(DDI_PROP_TYPE_INT, name, strlen(name),
	    (void *)&value, sizeof (value));
}

static void
bt_set_prop_u64(const char *name, uint64_t value)
{
	bt_set_prop(DDI_PROP_TYPE_INT64, name, strlen(name),
	    (void *)&value, sizeof (value));
}

static void
bt_set_prop_str(const char *name, const char *value)
{
	bt_set_prop(DDI_PROP_TYPE_STRING,
	    name, strlen(name), value, strlen(value));
}

void
eb_create_properties(uint64_t ramdisk_paddr, size_t ramdisk_len)
{
	uint64_t spstatus, ramdisk_start, ramdisk_end;
	const char *bootargs;
	ipcc_ident_t ident;
	uint8_t bsu;

	if (kernel_ipcc_status(&spstatus) != 0)
		bop_panic("Could not retrieve status value from SP");

	/*
	 * XXXBOOT - temporary use of SP status register bits to set
	 *		various debugging options.
	 */
	if (spstatus & IPCC_STATUS_DEBUG_KMDB)
		bootargs = "-kdv";
	else
		bootargs = "-kv";

	bt_set_prop_str(BTPROP_NAME_BOOTARGS, bootargs);

	if (spstatus & IPCC_STATUS_DEBUG_KBM) {
		bt_set_prop_u8("kbm_debug", 1);
		kbm_debug = B_TRUE;
	}

	if (spstatus & IPCC_STATUS_DEBUG_BOOTRD) {
		bt_set_prop_u8("bootrd_debug", 1);
		bootrd_debug = 1;
	}

	// XXX IPCC - set flag to dump boot properties to console
	bt_set_prop_u8("prom_debug", 1);

#ifdef noyyet
	// Awaiting RFD determinations
	if (spstatus & IPCC_STATUS_STARTED)
		kernel_ipcc_ackstart();
#endif

	if (kernel_ipcc_bsu(&bsu) == 0)
		bt_set_prop_u8(BTPROP_NAME_BSU, bsu);

	if (kernel_ipcc_ident(&ident) == 0) {
		bt_set_prop(DDI_PROP_TYPE_STRING,
		    BTPROP_NAME_BOARD_IDENT, sizeof (BTPROP_NAME_BOARD_IDENT),
		    ident.ii_serial, sizeof (ident.ii_serial));

		// XXX - adjust once format of model and revision is known
		bt_set_prop_u8(BTPROP_NAME_BOARD_MODEL, ident.ii_model);
		bt_set_prop_u8(BTPROP_NAME_BOARD_REVISION, ident.ii_rev);
	} else {
		bt_set_prop_str(BTPROP_NAME_BOARD_IDENT, "NO-SP-IDENT");
	}

	/*
	 * The APOB address and reset vector are stored in, or computed
	 * trivially from, data in the BHD.  See the discussion in AMD pub.
	 * 57299 sec. 4.1.5 table 17, and sec. 4.2 especially steps 2 and 4e.
	 * The APOB address can be set (by the SP and/or at image creation
	 * time) to almost anything in the bottom 2 GiB that doesn't conflict
	 * with other uses of memory; see the discussion in vm/kboot_mmu.c.
	 */
	const uint64_t apob_addr = 0x4000000UL;
	const uint32_t reset_vector = 0x7ffefff0U;

	bt_set_prop_u32(BTPROP_NAME_RESET_VECTOR, reset_vector);
	// XXX IPCC - also had DDI_PROP_NOTPROM, do we need that?
	bt_set_prop_u64(BTPROP_NAME_APOB_ADDRESS, apob_addr);

	/*
	 * XXX(cross): the conditional is a hack for transition.
	 * In steady-state, we'll call this unconditionally.
	 */
	if (ramdisk_paddr != 0) {
		ramdisk_start = ramdisk_paddr;
		ramdisk_end = ramdisk_start + ramdisk_len;

		/*
		 * Validate that the ramdisk lies completely
		 * within the 48-bit physical address space.
		 *
		 * The check against the length accounts for
		 * modular arithmetic in the cyclic subgroup.
		 */
		const uint64_t PHYS_LIMIT = (1ULL << 48) - 1;
		if (ramdisk_start > PHYS_LIMIT ||
		    ramdisk_end > PHYS_LIMIT ||
		    ramdisk_len > PHYS_LIMIT ||
		    ramdisk_start >= ramdisk_end) {
			bop_panic(
			    "Ramdisk parameter problem start=0x%lx end=0x%lx",
			    ramdisk_start, ramdisk_end);
		}
	} else {
		/* Default to the usual values used with nanobl-rs */
		ramdisk_start = 0x101000000UL;
		ramdisk_end = 0x105c00000UL;
	}

	bt_set_prop_u64(BTPROP_NAME_RAMDISK_START, ramdisk_start);
	bt_set_prop_u64(BTPROP_NAME_RAMDISK_END, ramdisk_end);
}

extern void
eb_set_tunables(void)
{
	/*
	 * We always want to enter the debugger if present or panic otherwise.
	 */
	nmi_action = NMI_ACTION_KMDB;
}

extern void
genunix_set_tunables(void)
{
	/*
	 * XXX Temporary for bringup: don't automatically unload modules.
	 */
	moddebug |= MODDEBUG_NOAUTOUNLOAD;

	/*
	 * We don't support running in a virtual environment.
	 */
	enable_platform_detection = 0;
}
