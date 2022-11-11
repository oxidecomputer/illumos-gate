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
#include <sys/reboot.h>
#include <sys/sysmacros.h>
#include <sys/boot_physmem.h>
#include <sys/boot_debug.h>
#include <sys/kernel_ipcc.h>

extern int bootrd_debug, prom_debug;
extern boolean_t kbm_debug;

/*
 * A lookup table that maps a board model and revision to various data
 * required for populating boot properties. First match wins. If the revision is
 * set to UINT32_MAX then the entry applies to all revisions. if the model is
 * set to NULL, then the entry applies to all models.
 */
typedef struct {
	const char	*bl_model;
	uint32_t	bl_rev;
	const char	*bl_descr;
	uint16_t	bl_bsu_slota;	/* Slot corresponding to BSU A */
	uint16_t	bl_bsu_slotb;	/* Slot corresponding to BSU B */
} board_lookup_t;

static const board_lookup_t board_lookup[] = {
	{ "913-0000019",	UINT32_MAX,	"Oxide,Gimlet",  17, 18, },
	{ NULL,			UINT32_MAX,	"Oxide,Unknown", 17, 18, },
};

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

	DBG_MSG("setprop %.*s (nlen %lx vlen %lx)\n", (int)nlen, name,
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
		DBG_MSG("New page (%lx > %lx)\n", size, bt_props_avail);
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

void
eb_create_properties(uint64_t ramdisk_paddr, size_t ramdisk_len)
{
	uint64_t spstatus, spstartup, ramdisk_start, ramdisk_end;
	ipcc_ident_t ident;
	uint8_t bsu;
	int err;

	if ((err = kernel_ipcc_status(&spstatus, &spstartup)) != 0) {
		bop_panic("Could not retrieve status registers from SP (%d)",
		    err);
	}

	boothowto |= RB_VERBOSE;

	/*
	 * krtld will ignore RB_DEBUGENTER when not accompanied by RB_KMDB.
	 * Setting IPCC_STARTUP_KMDB_BOOT will set both, regardless of the
	 * status of IPCC_STARTUP_KMDB.
	 */
	if ((spstartup & IPCC_STARTUP_KMDB_BOOT) != 0)
		boothowto |= RB_KMDB | RB_DEBUGENTER;
	else if ((spstartup & IPCC_STARTUP_KMDB) != 0)
		boothowto |= RB_KMDB;

	if ((spstartup & IPCC_STARTUP_KBM) != 0)
		kbm_debug = B_TRUE;

	if ((spstartup & IPCC_STARTUP_BOOTRD) != 0)
		bootrd_debug = 1;

	if ((spstartup & IPCC_STARTUP_PROM) != 0)
		prom_debug = 1;

	/*
	 * XXX Although the oxide arch does not use it, preferring to set flags
	 * in boothowto directly, the "bootargs" property is required to exist
	 * otherwise krtld objects.
	 */
	bt_set_prop_str(BTPROP_NAME_BOOTARGS, "");

	/* XXX not yet implemented - phase2 via IPCC */
	if ((spstartup & IPCC_STARTUP_RECOVERY) != 0)
		bt_set_prop_u8(BTPROP_NAME_BOOT_RECOVERY, 1);

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

	const board_lookup_t *board = NULL;
	for (uint_t i = 0; i < ARRAY_SIZE(board_lookup); i++) {
		const board_lookup_t *b = &board_lookup[i];

		if (b->bl_model != NULL &&
		    strcmp(b->bl_model, (char *)ident.ii_model) != 0) {
			continue;
		}
		if (b->bl_rev != UINT32_MAX && b->bl_rev != ident.ii_rev)
			continue;

		board = b;
		break;
	}
	if (board == NULL) {
		bop_panic("Could not find model %s/%u in lookup table",
		    ident.ii_model, ident.ii_rev);
	}

	bt_set_prop_str(BTPROP_NAME_MFG, board->bl_descr);
	bt_set_prop_u8(BTPROP_NAME_BSU_SLOTA, board->bl_bsu_slota);
	bt_set_prop_u8(BTPROP_NAME_BSU_SLOTB, board->bl_bsu_slotb);

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
	bt_set_prop_u64(BTPROP_NAME_APOB_ADDRESS, apob_addr);

	bt_set_prop_str(BTPROP_NAME_FSTYPE, "ufs");
	bt_set_prop_str(BTPROP_NAME_WHOAMI,
	    "/platform/oxide/kernel/amd64/unix");
	bt_set_prop_str(BTPROP_NAME_IMPL_ARCH, "oxide");

	/*
	 * If this parameter was provided by the loader then we assume that
	 * we are using the unified boot strategy. Otherwise we use some
	 * hardcoded defaults for the expected location of the ramdisk.
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

		/*
		 * This property is checked in boot_image_locate(), called from
		 * main().
		 */
		bt_set_prop_str(BTPROP_NAME_BOOT_IMAGE_OPS, "misc/boot_image");
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
