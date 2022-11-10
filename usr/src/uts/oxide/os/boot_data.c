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

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	USE_DISCOVERY_STUB

/*
 * This is a stub that will be replaced by communication from the SP very
 * early in boot.  The origins of these things vary:
 *
 * - The APOB address and reset vector are stored in, or computed trivially
 *   from, data in the BHD.  See the discussion in AMD pub. 57299 sec. 4.1.5
 *   table 17, and sec. 4.2 especially steps 2 and 4e.  The APOB address can be
 *   set (by the SP and/or at image creation time) to almost anything in the
 *   bottom 2 GiB that doesn't conflict with other uses of memory; see the
 *   discussion in vm/kboot_mmu.c.
 * - The board identifier comes from the FRUID ROM accessible only by the SP.
 * - The phase1 ramdisk can come from either the BHD if we have the PSP load
 *   it or directly from the SP if we have the loader decompress or otherwise
 *   manipulate the image in memory.  In either case, the SP has the authority
 *   to set this, either by setting the destination in the BHD or telling the
 *   loader where to put it.
 *
 * Some of these properties (and more especially those in the fallback set
 * below) could also potentially be defined as part of the machine architecture.
 * More generally, there will be some minimal collection of non-discoverable
 * machine state that we must either define or obtain from outside, which in
 * the absence of a good way to do that is mocked up here.
 */
static const uint64_t ASSUMED_APOB_ADDR = 0x4000000UL;
static const uint32_t ASSUMED_RESET_VECTOR = 0x7ffefff0U;
static const char FAKE_BOARD_IDENT[] = "FAKE-IDENT";

static uint64_t ramdisk_start_val = 0x101000000UL;
static uint64_t ramdisk_end_val = 0x105c00000UL;

static const bt_prop_t reset_vector_prop = {
	.btp_next = NULL,
	.btp_name = BTPROP_NAME_RESET_VECTOR,
	.btp_vlen = sizeof (uint32_t),
	.btp_value = &ASSUMED_RESET_VECTOR,
	.btp_typeflags = DDI_PROP_TYPE_INT
};

static const bt_prop_t ramdisk_end_prop = {
	.btp_next = &reset_vector_prop,
	.btp_name = "ramdisk_end",
	.btp_vlen = sizeof (uint64_t),
	.btp_value = &ramdisk_end_val,
	.btp_typeflags = DDI_PROP_TYPE_INT64
};

static const bt_prop_t ramdisk_start_prop = {
	.btp_next = &ramdisk_end_prop,
	.btp_name = "ramdisk_start",
	.btp_vlen = sizeof (uint64_t),
	.btp_value = &ramdisk_start_val,
	.btp_typeflags = DDI_PROP_TYPE_INT64
};

#define	WANT_KBM_DEBUG	0

#if WANT_KBM_DEBUG
static const uint32_t KBM_DEBUG_VAL = 1U;
static const bt_prop_t kbm_debug_prop = {
	.btp_next = &ramdisk_start_prop,
	.btp_name = "kbm_debug",
	.btp_vlen = sizeof (uint32_t),
	.btp_value = &KBM_DEBUG_VAL,
	.btp_typeflags = DDI_PROP_TYPE_INT
};
#endif

static const bt_prop_t board_ident_prop = {
#if WANT_KBM_DEBUG
	.btp_next = &kbm_debug_prop,
#else
	.btp_next = &ramdisk_start_prop,
#endif
	.btp_name = BTPROP_NAME_BOARD_IDENT,
	.btp_vlen = sizeof (FAKE_BOARD_IDENT),
	.btp_value = FAKE_BOARD_IDENT,
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

static const bt_prop_t apob_prop = {
	.btp_next = &board_ident_prop,
	.btp_name = BTPROP_NAME_APOB_ADDRESS,
	.btp_vlen = sizeof (uint64_t),
	.btp_value = &ASSUMED_APOB_ADDR,
	.btp_typeflags = DDI_PROP_TYPE_INT64 | DDI_PROP_NOTPROM
};

const bt_discovery_t bt_discovery_stub = {
	.btd_magic = BT_DISCOVERY_MAGIC,
	.btd_version = BT_DISCOVERY_VERSION(BT_DISCOVERY_MAJOR,
	    BT_DISCOVERY_MINOR),
	.btd_prop_list = &apob_prop
};

#endif	/* USE_DISCOVERY_STUB */

static const char *boot_image_enabled_value = "misc/boot_image";

/*
 * This one is not const, because we may modify its value in
 * ramdisk_set_tunables() below.
 */
static bt_prop_t boot_image_ops_prop = {
	.btp_next = NULL,
	.btp_name = BTPROP_NAME_BOOT_IMAGE_OPS,
	.btp_vlen = sizeof (""),
	.btp_value = "",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

static const bt_prop_t fstype_prop = {
	.btp_next = &boot_image_ops_prop,
	.btp_name = BTPROP_NAME_FSTYPE,
	.btp_vlen = sizeof ("ufs"),
	.btp_value = "ufs",
	.btp_typeflags = DDI_PROP_TYPE_STRING
};

static const bt_prop_t whoami_prop = {
	.btp_next = &fstype_prop,
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

void
ramdisk_set_tunables(uint64_t ramdisk_start, uint64_t ramdisk_end)
{
	ramdisk_start_val = ramdisk_start;
	ramdisk_end_val = ramdisk_end;

	/*
	 * If ramdisk parameters were provided by the loader, use the unified
	 * boot strategy:
	 */
	boot_image_ops_prop.btp_value = boot_image_enabled_value;
	boot_image_ops_prop.btp_vlen = strlen(boot_image_enabled_value) + 1;
}

#ifdef	__cplusplus
}
#endif
