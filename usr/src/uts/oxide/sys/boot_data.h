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

#ifndef	_SYS_BOOT_DATA_H
#define	_SYS_BOOT_DATA_H

/*
 * Our entry point's argument is a pointer to a bt_discovery_t.  This is the
 * one and only thing that must be kept in sync with the bootloader; everything
 * else is either discoverable directly or contained here.  In principle we
 * could extend xboot_info, but there's little overlap between what it needs to
 * contain on i86pc and what we need.  Ultimately this all comes from the
 * service processor.
 *
 * We don't use struct boot_modules from intel/sys/bootinfo.h, nor do we share
 * struct bootprop with i86pc.  We'd like to, but those structures are designed
 * to be shared with 32-bit code, and necessarily assume that all pointers are
 * 32-bit and thus that all values passed to us by the bootloader are in
 * identity-mapped 32-bit space.  That's not correct for this architecture, so
 * the structures are not usable.  Additionally, even the pieces that could
 * have been usable have been defined with members of the wrong type; e.g.,
 * using uint_t for sizes instead of size_t and int for property type.  We
 * could continue the sins of the past, but that defeats the entire purpose of
 * building this machine: everything is 64-bit all the time, and where we must
 * write in C we insist upon the proper types.
 *
 * This is essentially a ruthlessly simplified take on both multiboot and the
 * xboot_info mechanism that i86pc uses, both rolled into a single simple
 * structure for boot-time discovery.  Instead of accepting arrays of memory
 * lists, boot modules, boot properties, environment files, hashes, etc.,
 * everything is a property.  We discover things we need at boot time from the
 * properties we are given.  This way we don't need to bother translating what
 * the loader gives us into properties ourselves; at the same time, we haven't
 * imposed much of a burden on the loader either, because it has to tell us
 * this stuff one way or another.  May as well have it tell us in the way that
 * results in the least code.  This takes the place of:
 *
 * boot_modules: The BMT_ROOTFS is given to us using the ramdisk_start and
 * ramdisk_end properties (which we need to supply elsewhere anyway).
 *
 * bootenv.rc: The contents of this can be set as properties.
 *
 * command line: -B arguments translate directly to properties.  Other command
 * line arguments may be passed as new properties.  XXX This is fine but we
 * need to define the mappings for any we wish to accept.
 *
 * boot_memlist: We don't ask nor expect the loader to supply memlists; instead
 * we require the loader to tell us where the APOB is and we obtain them from
 * there directly.
 *
 * hashes: These are supplied as ramdisk-hash and module-hash-%u.  They are
 * always required.  Hashes as separate modules aren't supported.
 *
 * fonts: Not supported on this architecture (no framebuffer).
 *
 * The main simplifying assumption here is therefore that the SP is responsible
 * for managing everything about our environment at boot time, and can
 * therefore assemble from whatever sources it wishes a single collection of
 * properties.  Those may come from an operator via the control plane, local
 * policy, AMD firmware, or any other source the SP sees fit to consider.  This
 * also means the SP is free to enforce whatever policy -- for security or
 * otherwise -- it wishes by filtering or validating these properties.  Where
 * necessary, the loader can manipulate them, which also means we may want to
 * add an HMAC here -- but the SP is also responsible for making sure the
 * loader is itself trustworthy so we probably shouldn't worry overly much
 * about a hostile loader tricking us by corrupting the SP's properties.
 *
 * Among these properties must be a pointer to the APOB, which we use (for now)
 * to discover DRAM, and the baseboard identifier.  If either is absent or of
 * the wrong type, (you guessed it!) we panic.  The identifier must be a byte
 * array, and the APOB must be a valid virtual address.  Thus, the loader is
 * required to have mapped all modules and the memory containing the APOB prior
 * to handing us control.  We don't otherwise assume anything about that memory
 * nor how it was mapped; we're free to unmap it, remap it, and reuse it as and
 * when needed so we don't care.
 *
 * Worth noting is that we include only things we can't otherwise discover for
 * ourselves.  If we can look at the pagetable or some collection of registers
 * to figure out where we are and what was done, we do that instead of having
 * the loader pass it to us.  This is more reliable, keeps the interface small
 * and simple so we're less likely to have touchy loader/kernel flag days, and
 * reduces duplication of code.  If we ever should need to make an incompatible
 * change, the major version must be incremented and a loader/kernel flag day
 * will result.  Compatible extensions (i.e., the addition of more members) is
 * indicated by incrementing the minor version.  Older kernels cannot make use
 * of this additional data but can still boot properly from newer loaders,
 * allowing a less risky two-stage transition.  Ideally, a kernel should be
 * willing to accept a small number of previous minor versions without the
 * extensions they represent; however, this is TBD as part of the larger
 * software upgrade strategy.  In general, we'll attempt to conform to the set
 * of expectations established for the system as a whole.
 */

#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/ddipropdefs.h>

/* XXXBOOT */
#define	USE_DISCOVERY_STUB

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct bt_prop {
	const struct bt_prop *btp_next;
	const char *btp_name;
	size_t btp_vlen;
	const void *btp_value;
	uint32_t btp_typeflags;
} bt_prop_t;

#define	BT_DISCOVERY_MAGIC	0x1DEC0C094608D15CUL
#define	BT_DISCOVERY_MAJOR	1UL
#define	BT_DISCOVERY_MINOR	0UL
#define	BT_DISCOVERY_VERSION(_major, _minor)	(((_major) << 32) | (_minor))

typedef struct bt_discovery {
	uint64_t btd_magic;
	uint64_t btd_version;
	const bt_prop_t *btd_prop_list;
} bt_discovery_t;

/*
 * These are all the required properties.  Some of them come from the SP
 * while others are fixed.
 */
#define	BTPROP_NAME_APOB_ADDRESS	"apob-address"
#define	BTPROP_NAME_BOARD_IDENT		"baseboard-identifier"
#define	BTPROP_NAME_BOOTARGS		"bootargs"
#define	BTPROP_NAME_MFG			"mfg-name"
#define	BTPROP_NAME_IMPL_ARCH		"impl-arch-name"
#define	BTPROP_NAME_FSTYPE		"fstype"
#define	BTPROP_NAME_RESET_VECTOR	"reset-vector"
#define	BTPROP_NAME_BOOT_IMAGE_OPS	"boot-image-ops"

#ifdef	USE_DISCOVERY_STUB
extern const bt_discovery_t bt_discovery_stub;
#endif
extern const bt_prop_t * const bt_fallback_props;

extern void eb_set_tunables(void);
extern void genunix_set_tunables(void);
extern void ramdisk_set_tunables(uint64_t, uint64_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BOOT_DATA_H */
