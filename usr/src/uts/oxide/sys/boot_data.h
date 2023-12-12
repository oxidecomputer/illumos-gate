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

#ifndef	_SYS_BOOT_DATA_H
#define	_SYS_BOOT_DATA_H

#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/ddipropdefs.h>
#include <sys/stdbool.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct bt_prop {
	const struct bt_prop *btp_next;
	char *btp_name;
	size_t btp_vlen;
	const void *btp_value;
	uint32_t btp_typeflags;
} bt_prop_t;

/*
 * These are all the required properties.  Some of them come from the SP
 * while others are fixed.
 */
#define	BTPROP_NAME_APOB_ADDRESS	"apob-address"
#define	BTPROP_NAME_BOARD_IDENT		"baseboard-identifier"
#define	BTPROP_NAME_BOARD_MODEL		"baseboard-model"
#define	BTPROP_NAME_BOARD_REVISION	"baseboard-revision"
#define	BTPROP_NAME_BOOTARGS		"bootargs"
#define	BTPROP_NAME_MFG			"mfg-name"
#define	BTPROP_NAME_IMPL_ARCH		"impl-arch-name"
#define	BTPROP_NAME_FSTYPE		"fstype"
#define	BTPROP_NAME_WHOAMI		"whoami"
#define	BTPROP_NAME_RESET_VECTOR	"reset-vector"
#define	BTPROP_NAME_BSU			"boot-storage-unit"
#define	BTPROP_NAME_BOOT_SOURCE		"boot-source"
#define	BTPROP_NAME_RAMDISK_START	"ramdisk_start"
#define	BTPROP_NAME_RAMDISK_END		"ramdisk_end"

extern const bt_prop_t *bt_props;
extern const bt_prop_t * const bt_fallback_props;

extern void bt_set_prop(uint32_t, const char *, size_t, const void *, size_t);
extern void eb_create_properties(uint64_t, size_t, uint64_t);
extern void eb_set_tunables(void);
extern void genunix_set_tunables(void);
extern bool genunix_is_loaded(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BOOT_DATA_H */
