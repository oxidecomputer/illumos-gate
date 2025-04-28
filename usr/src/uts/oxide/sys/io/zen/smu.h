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

/*
 * Type, structure, and function definitions for interacting with the System
 * Management Unit, or SMU.
 */

#ifndef	_ZEN_SMU_H
#define	_ZEN_SMU_H

#include <sys/types.h>
#include <sys/stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMU RPC Operation Codes. Note, these are tied to firmware and therefore may
 * not be portable beyond Milan, Genoa, and Turin processors.  However, we have
 * verified that these match the supported SMU firmware running on on those
 * three microarchitectures.
 */
#define	ZEN_SMU_OP_TEST			0x01
#define	ZEN_SMU_OP_GET_VERSION		0x02
#define	ZEN_SMU_OP_GET_VERSION_MAJOR(x)		bitx32(x, 23, 16)
#define	ZEN_SMU_OP_GET_VERSION_MINOR(x)		bitx32(x, 15, 8)
#define	ZEN_SMU_OP_GET_VERSION_PATCH(x)		bitx32(x, 7, 0)
#define	ZEN_SMU_OP_ENABLE_FEATURE	0x03
#define	ZEN_SMU_OP_HAVE_AN_ADDRESS	0x05
#define	ZEN_SMU_OP_GET_BRAND_STRING	0x0d
#define	ZEN_SMU_OP_TX_PP_TABLE		0x10
#define	ZEN_SMU_OP_ENABLE_HSMP_INT	0x41

typedef enum zen_smu_rpc_res {
	/*
	 * These are analogues of the response codes defined by firmware.
	 */
	ZEN_SMU_RPC_OK,
	ZEN_SMU_RPC_EBUSY,
	ZEN_SMU_RPC_EPREREQ,
	ZEN_SMU_RPC_EUNKNOWN,
	ZEN_SMU_RPC_ERROR,
	/*
	 * The SMU RPC timed out.
	 */
	ZEN_SMU_RPC_ETIMEOUT,
	/*
	 * Firmware on the SMU returned some other, possibly new, RPC error that
	 * we don't explicitly handle.
	 */
	ZEN_SMU_RPC_EOTHER,
} zen_smu_rpc_res_t;

/*
 * SMU RPC response codes defined by firmware that may appear in the response
 * register.
 */
#define	ZEN_SMU_RPC_FW_RESP_OK			0x01
#define	ZEN_SMU_RPC_FW_RESP_REJ_BUSY		0xfc
#define	ZEN_SMU_RPC_FW_RESP_REJ_PREREQ		0xfd
#define	ZEN_SMU_RPC_FW_RESP_UNKNOWN_CMD		0xfe
#define	ZEN_SMU_RPC_FW_RESP_FAILED		0xff

/*
 * The base of the SMU SMN register space.  This is common across Genoa and
 * Turin.
 */
#define	ZEN_SMU_SMN_REG_BASE		0x3b10000

/*
 * SMN addresses to reach the SMU for RPCs.  There is only ever one SMU per
 * node, so unit numbers aren't meaningful.  All registers have a single
 * instance only.
 */
AMDZEN_MAKE_SMN_REG_FN(zen_smu_smn_reg, SMU_RPC,
    ZEN_SMU_SMN_REG_BASE, 0xfffff000, 1, 0);

#ifdef __cplusplus
}
#endif

#endif	/* _ZEN_SMU_H */
