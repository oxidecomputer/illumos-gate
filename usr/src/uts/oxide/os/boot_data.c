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
 * Copyright 2024 Oxide Computer Co
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/boot_data.h>
#include <sys/apic_common.h>
#include <sys/modctl.h>
#include <sys/x86_archext.h>
#include <sys/reboot.h>
#include <sys/sysmacros.h>
#include <sys/boot_image_ops.h>
#include <sys/boot_physmem.h>
#include <sys/boot_debug.h>
#include <sys/platform_detect.h>
#include <sys/kernel_ipcc.h>
#include <sys/smt.h>
#include <sys/time.h>

/*
 * Used by apix code that could be shared with other kernels.  Not tunable on
 * this kernel except by manual change to source code.
 */
nmi_action_t nmi_action = NMI_ACTION_UNSET;

extern int bootrd_debug, prom_debug;
extern boolean_t kbm_debug;

/*
 * Boot properties. We build a list of boot properties backed by boot pages -
 * allocated from eb_alloc_page() - that are used by the early boot process.
 * This is always done on the boot CPU so there is no locking of these
 * structures. Later in boot, these properties are subsumed into properties of
 * the root devtree node, before the mappings are torn down.
 *
 * bt_props is the head of a linked list of properties, bt_props_mem points to
 * the memory that should be used to store the next property, and bt_props_avail
 * is the number of available bytes to which bt_props_mem points.
 *
 * Properties are stored as a bt_prop_t, with the btp_name element pointing
 * to memory directly after it, into which the property name is placed along
 * with a terminating NUL. The value is placed after this, with padding to
 * ensure it is aligned to 16 bytes.
 *
 *        bt_prop_t
 *              btp_next
 *              btp_name  --------------
 *              btl_vlen                |
 *              btp_value --------------+---
 *              btp_typeflags           |   |
 *        name\0  <---------------------    |
 *        <padding>                         |
 *        value   <-------------------------
 *
 * XXX - The 16 byte alignment of property values is something that i86pc does,
 * presumably to ensure that whatever is put there ends up aligned.
 * It is probably not required here since at least the property accessor -
 * do_bsys_getprop() - does not appear to depend on the value being aligned.
 */
const bt_prop_t *bt_props;
static uint8_t *bt_props_mem;
static size_t bt_props_avail;

/* Round up the provided size to the next 16 byte alignment */
static inline size_t
btp_align_size(size_t size)
{
	size_t asize;

	asize = (size + 0xf) & ~0xf;
	ASSERT3U(asize, >=, size);
	return (asize);
}

/* Round up the provided pointer to the next 16 byte alignment */
static inline uint8_t *
btp_align_pointer(uint8_t *ptr)
{
	uintptr_t p = (uintptr_t)ptr;
	uintptr_t ap;

	ap = (p + 0xf) & ~0xf;
	ASSERT3P(ap, >=, p);
	return ((uint8_t *)ap);
}

void
bt_set_prop(uint32_t flags, const char *name, size_t nlen, const void *value,
    size_t vlen)
{
	bt_prop_t *btp;
	uint8_t *omem;
	size_t size;

#ifdef DEBUG
	/* do_bsys_nextprop() depends on unique property names */
	for (const bt_prop_t *b = bt_props; b != NULL; b = b->btp_next) {
		if (strcmp(b->btp_name, name) == 0)
			bop_panic("Duplicate boot property name '%s'", name);
	}
#endif

	EB_DBGMSG("setprop %.*s (nlen %lx vlen %lx)\n", (int)nlen, name,
	    nlen, vlen);

	size = sizeof (bt_prop_t) + nlen + 1;
	if (vlen > 0)
		size += btp_align_size(vlen);
	size = btp_align_size(size);

	/* If we are out of space in the current page, allocate a new one. */
	if (size > bt_props_avail) {
		if (size > MMU_PAGESIZE) {
			bop_panic("Boot property requires 0x%lx bytes "
			    "(> MMU_PAGESIZE)", size);
		}
		EB_DBGMSG("New page (%lx > %lx)\n", size, bt_props_avail);
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
		btp->btp_value = bt_props_mem = btp_align_pointer(bt_props_mem);
		bcopy(value, bt_props_mem, vlen);
		bt_props_mem += vlen;
	} else {
		btp->btp_value = NULL;
	}

	/* Align the pointer ready for the next property */
	bt_props_mem = btp_align_pointer(bt_props_mem);
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
	/*
	 * Even though there is a value length property, many consumers
	 * assume that string property values include a terminator.
	 */
	bt_set_prop(DDI_PROP_TYPE_STRING,
	    name, strlen(name), value, strlen(value) + 1);
}

static void
eb_create_common_properties(uint64_t ramdisk_paddr, size_t ramdisk_len,
    uint64_t spstartup)
{
	uint64_t ramdisk_start, ramdisk_end;

	/*
	 * krtld will ignore RB_DEBUGENTER when not accompanied by RB_KMDB.
	 * Setting IPCC_STARTUP_KMDB_BOOT will set both, regardless of the
	 * status of IPCC_STARTUP_KMDB.
	 */
	if ((spstartup & IPCC_STARTUP_KMDB_BOOT) != 0)
		boothowto |= RB_KMDB | RB_DEBUGENTER;
	else if ((spstartup & IPCC_STARTUP_KMDB) != 0)
		boothowto |= RB_KMDB;

	if ((spstartup & IPCC_STARTUP_VERBOSE) != 0)
		boothowto |= RB_VERBOSE;

	if ((spstartup & IPCC_STARTUP_KBM) != 0)
		kbm_debug = B_TRUE;

	if ((spstartup & IPCC_STARTUP_BOOTRD) != 0)
		bootrd_debug = 1;

	if ((spstartup & IPCC_STARTUP_PROM) != 0)
		prom_debug = 1;

	/*
	 * The APOB address and reset vector are stored in, or computed
	 * trivially from, data in the BHD.  See the discussion in AMD pub.
	 * 57299 sec. 4.1.5 table 17, and sec. 4.2 especially steps 2 and 4e.
	 * The APOB address can be set (by the SP and/or at image creation
	 * time) to almost anything in the bottom 2 GiB that doesn't conflict
	 * with other uses of memory; see the discussion in vm/kboot_mmu.c.
	 */
	const uint64_t apob_addr = APOB_ADDR;
	const uint32_t reset_vector = 0x7ffefff0U;

	bt_set_prop_str(BTPROP_NAME_MFG, oxide_board_data->obd_rootnexus);
	bt_set_prop_u32(BTPROP_NAME_RESET_VECTOR, reset_vector);
	bt_set_prop_u64(BTPROP_NAME_APOB_ADDRESS, apob_addr);

	bt_set_prop_str(BTPROP_NAME_FSTYPE, "ufs");
	bt_set_prop_str(BTPROP_NAME_WHOAMI,
	    "/platform/oxide/kernel/amd64/unix");
	bt_set_prop_str(BTPROP_NAME_IMPL_ARCH, "oxide");

	if (ramdisk_paddr == 0)
		bop_panic("Ramdisk parameters were not provided.");

	ramdisk_start = ramdisk_paddr;
	ramdisk_end = ramdisk_start + ramdisk_len;

	/*
	 * Validate that the ramdisk lies completely within the 48-bit physical
	 * address space. The check against the length accounts for modular
	 * arithmetic in the cyclic subgroup.
	 */
	const uint64_t PHYS_LIMIT = (1ULL << 48) - 1;
	if (ramdisk_start > PHYS_LIMIT || ramdisk_end > PHYS_LIMIT ||
	    ramdisk_len > PHYS_LIMIT || ramdisk_start >= ramdisk_end) {
		bop_panic("Ramdisk parameter problem start=0x%lx end=0x%lx",
		    ramdisk_start, ramdisk_end);
	}

	bt_set_prop_u64(BTPROP_NAME_RAMDISK_START, ramdisk_start);
	bt_set_prop_u64(BTPROP_NAME_RAMDISK_END, ramdisk_end);

	/*
	 * Although the oxide arch does not use it, preferring to set flags
	 * in boothowto directly, the "bootargs" property is required to exist
	 * to sate krtld.
	 */
	bt_set_prop_str(BTPROP_NAME_BOOTARGS, "");
}

static void
eb_fake_ipcc_properties(void)
{
	bt_set_prop_str(BTPROP_NAME_BOOT_SOURCE, "ramdisk");
	bt_set_prop_u8(BTPROP_NAME_BSU, 'A');

	bt_set_prop_str(BTPROP_NAME_BOARD_IDENT, "FAKE-IDENT");
	bt_set_prop_str(BTPROP_NAME_BOARD_MODEL, "FAKE-MODEL");
	bt_set_prop_u32(BTPROP_NAME_BOARD_REVISION, 0);
}

static void
eb_real_ipcc_properties(uint64_t spstatus, uint64_t spstartup)
{
	ipcc_ident_t ident;
	uint8_t bsu;
	int err;

	if ((spstatus & IPCC_STATUS_STARTED) != 0)
		kernel_ipcc_ackstart();

	/*
	 * Now that we have the initial status registers and have acknowledged
	 * any SP (re)start, enable polling the SP-to-Host interrupt line in
	 * case the SP task restarts while processing the following commands.
	 */
	kernel_ipcc_init(IPCC_INIT_ENABLE_INTERRUPT);

	if ((err = kernel_ipcc_bsu(&bsu)) != 0)
		bop_panic("Could not retrieve BSU from SP (%d)", err);

	bt_set_prop_u8(BTPROP_NAME_BSU, bsu);

	if ((err = kernel_ipcc_ident(&ident)) != 0)
		bop_panic("Could not retrieve ident from SP (%d)", err);

	bt_set_prop_str(BTPROP_NAME_BOARD_IDENT, (char *)ident.ii_serial);
	bt_set_prop_str(BTPROP_NAME_BOARD_MODEL, (char *)ident.ii_model);
	bt_set_prop_u32(BTPROP_NAME_BOARD_REVISION, ident.ii_rev);

	/*
	 * Set properties to configure how we will boot. This is controlled by
	 * flags in the SP's startup options register, and by the boot storage
	 * unit (BSU) communicated by the SP.
	 */

	if ((spstartup & IPCC_STARTUP_BOOT_RAMDISK) != 0) {
		/*
		 * This option selects booting using the provided ramdisk for
		 * the root filesystem, without loading a phase 2 image.
		 */
		bt_set_prop_str(BTPROP_NAME_BOOT_SOURCE, "ramdisk");
	} else {
		/*
		 * In this block, we are heading for new style boot,
		 * acquiring a phase 2 image from somewhere. Setting this
		 * property causes main() to try and load the kernel module
		 * set as the value, and use it to locate phase 2.
		 */
		bt_set_prop_str(BTPROP_NAME_BOOT_IMAGE_OPS, "misc/boot_image");

		if ((spstartup & IPCC_STARTUP_RECOVERY) != 0) {
			/*
			 * The SP has requested phase2 recovery - load via
			 * ipcc.
			 */
			bt_set_prop_str(BTPROP_NAME_BOOT_SOURCE, "sp");
		} else if ((spstartup & IPCC_STARTUP_BOOT_NET) != 0) {
			/*
			 * The SP has requested network boot.
			 */
			bt_set_prop_str(BTPROP_NAME_BOOT_SOURCE, "net");
		} else {
			/*
			 * No special options, request boot from the BSU
			 * provided by the SP.
			 */
			char bootdev[sizeof ("disk:") + 10];

			(void) snprintf(bootdev, sizeof (bootdev), "disk:%u",
			    (uint32_t)oxide_board_data->obd_bsu_slot[bsu ==
			    'A' ? 0 : 1]);

			bt_set_prop_str(BTPROP_NAME_BOOT_SOURCE, bootdev);
		}
	}
}

void
eb_create_properties(uint64_t ramdisk_paddr, size_t ramdisk_len)
{

	if (oxide_board_data->obd_ipccmode == IPCC_MODE_DISABLED) {
		eb_create_common_properties(ramdisk_paddr, ramdisk_len,
		    oxide_board_data->obd_startupopts);
		eb_fake_ipcc_properties();
	} else {
		uint64_t spstatus, spstartup;
		int err;

		if ((err = kernel_ipcc_status(&spstatus, &spstartup)) != 0) {
			bop_panic(
			    "Could not retrieve status registers from SP (%d)",
			    err);
		}
		eb_create_common_properties(ramdisk_paddr, ramdisk_len,
		    spstartup);
		eb_real_ipcc_properties(spstatus, spstartup);
	}
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
	 * We don't support running in a virtual environment.
	 */
	enable_platform_detection = 0;

	/*
	 * No time-of-day unit so tell the clock code not to bother.
	 */
	have_hw_tod = 0;

	/*
	 * KPTI is always on, as is use of PCID.
	 */
	kpti_enable = 1;
	x86_use_pcid = 1;

	/*
	 * We neither support nor have any need for monkeying with CPUID
	 * results.  Note that even if we had, we'd instead change the
	 * non-architectural MSRs that control what CPUID returns so that user
	 * software would get the same thing if it chose to invoke the
	 * instruction instead of getting the feature bits like it should.
	 * Nevertheless, we must clear these explicitly as common code does not.
	 */
	extern uint32_t cpuid_feature_ecx_include;
	extern uint32_t cpuid_feature_ecx_exclude;
	extern uint32_t cpuid_feature_edx_include;
	extern uint32_t cpuid_feature_edx_exclude;

	cpuid_feature_ecx_include = 0;
	cpuid_feature_ecx_exclude = 0;
	cpuid_feature_edx_include = 0;
	cpuid_feature_edx_exclude = 0;

	/*
	 * SMT is enabled unconditionally for now.  This could also be changed
	 * to a policy communicated by the SP if needed, or SMT could be
	 * disabled from userland.  Again, this is used by common code but has
	 * no default value there so we must clear it.
	 */
	smt_boot_disable = 0;
}

/*
 * This function is used only by genunix_is_loaded() below.  It has to be a
 * separate function because if we were to simply take the address of an extern
 * global, the compiler would optimise it away because that can "never" be NULL.
 * In reality, it can be NULL if the symbol is outside unix and krtld has not
 * yet processed the relocation against the symbol (in our case, always
 * something from genunix).  Until that relocation has been processed, the
 * address of that symbol will be 0.  Such symbols are "weakish": they aren't
 * declared weak because most code is supposed to assume the fiction that unix
 * and genunix are all one object, but some of our code needs to know the truth.
 */
static bool
weakish_is_null(const void *p)
{
	return (p == NULL);
}

extern bool
genunix_is_loaded(void)
{
	extern kmutex_t mod_lock;

	return (!weakish_is_null(&mod_lock));
}
