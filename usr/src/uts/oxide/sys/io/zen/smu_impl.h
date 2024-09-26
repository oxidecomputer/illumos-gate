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
 * Copyright 2024 Oxide Computer Company
 */

/*
 * Type, structure, and function definitions for for interacting with the System
 * Management Unit (SMU).
 */

#ifndef	_ZEN_SMU_IMPL_H
#define	_ZEN_SMU_IMPL_H

#include <sys/types.h>
#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/smu.h>

/*
 * SMU RPC Operation Codes. Note, these are tied to firmware and therefore may
 * not be portable beyond Milan, Genoa, and Turin processors.  However, we have
 * verified that these match the supported SMU firmware running on on those
 * three microarchitectures.
 */
#define	ZEN_SMU_OP_TEST			0x01
#define	ZEN_SMU_OP_GET_VERSION		0x02
#define	ZEN_SMU_OP_GET_VERSION_MAJOR(x)	bitx32(x, 23, 16)
#define	ZEN_SMU_OP_GET_VERSION_MINOR(x)	bitx32(x, 15, 8)
#define	ZEN_SMU_OP_GET_VERSION_PATCH(x)	bitx32(x, 7, 0)
#define	ZEN_SMU_OP_ENABLE_FEATURE	0x03
#define	ZEN_SMU_OP_GET_BRAND_STRING	0x0d

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
 * The arguments, request, and response for an RPC sent to the SMU.  Note that
 * the response field holds the raw response from firmware and is kept for
 * debugging and error reporting, and not generally used by callers, which
 * instead examine a zen_smu_rpc_res_t.
 */
typedef struct zen_smu_rpc {
	uint32_t			zsr_req;
	uint32_t			zsr_resp;
	uint32_t			zsr_args[6];
} zen_smu_rpc_t;

/*
 * Synchronously invoke an RPC on the SMU.  Returns the RPC status.
 * Overwrites rpc->zsr_args with data returned by the RPC on success; zsr_args
 * is unmodified if the RPC fails.
 */
extern zen_smu_rpc_res_t zen_smu_rpc(zen_iodie_t *, zen_smu_rpc_t *);

/*
 * Returns the string representation of a SMU RPC response.
 */
extern const char *zen_smu_rpc_res_str(const zen_smu_rpc_res_t);

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

#endif	/* _ZEN_SMU_IMPL_H */
