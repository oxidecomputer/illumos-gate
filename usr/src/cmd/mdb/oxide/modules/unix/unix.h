/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _UNIX_H
#define	_UNIX_H

#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/df.h>
#include <io/amdzen/amdzen_client.h>

/*
 * Shadow structures for bits that we care about in the debugger.
 * Must be kept in sync with the definitions in oxide/sys/platform_detect.h
 */
typedef struct {
	const uint32_t			zmsa_reg_base;
	const smn_reg_def_t		zmsa_arg0;
	const smn_reg_def_t		zmsa_arg1;
	const smn_reg_def_t		zmsa_arg2;
	const smn_reg_def_t		zmsa_arg3;
	const smn_reg_def_t		zmsa_arg4;
	const smn_reg_def_t		zmsa_arg5;
	const smn_reg_def_t		zmsa_resp;
	const smn_reg_def_t		zmsa_doorbell;
} mdb_zen_mpio_smn_addrs_t;

typedef struct {
	mdb_zen_mpio_smn_addrs_t	zpc_mpio_smn_addrs;
} mdb_zen_platform_consts_t;

typedef struct {
	mdb_zen_platform_consts_t	zp_consts;
} mdb_zen_platform_t;

typedef struct {
	x86_chiprev_t			obc_chiprev;
} mdb_oxide_board_cpuinfo_t;

typedef struct {
	mdb_oxide_board_cpuinfo_t	obd_cpuinfo;
	const mdb_zen_platform_t	*obd_zen_platform;
} mdb_oxide_board_data_t;

extern mdb_oxide_board_data_t *get_board_data(void);
extern boolean_t target_chiprev(x86_chiprev_t *);

#endif /* _UNIX_H */
