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
 * Type, structure, and function definitions for for interacting with MPIO,
 * the post-Milan AMD Zen "MicroProcessor for IO", which is the component that
 * handles things like driving the DXIO crossbar to train PCIe lanes, etc.
 */

#ifndef	_ZEN_MPIO_IMPL_H
#define	_ZEN_MPIO_IMPL_H

#include <sys/types.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/mpio.h>

/*
 * MPIO RPC result codes.  These incorporate both the responses returned from
 * MPIO, as well as codes we have defined for e.g. RPC call failures.
 */
typedef enum zen_mpio_rpc_res {
	/*
	 * These are analogues of firmware errors.
	 */
	ZEN_MPIO_RPC_OK,
	ZEN_MPIO_RPC_EBUSY,
	ZEN_MPIO_RPC_EPREREQ,
	ZEN_MPIO_RPC_EUNKNOWN,
	ZEN_MPIO_RPC_ERROR,
	/*
	 * MPIO never became ready to receive an RPC.
	 */
	ZEN_MPIO_RPC_ENOTREADY,
	/*
	 * The RPC itself timed out.
	 */
	ZEN_MPIO_RPC_ETIMEOUT,
	/*
	 * Firmware on MPIO returned some other, possibly new, RPC error that we
	 * don't explicitly handle.
	 */
	ZEN_MPIO_RPC_EOTHER,
} zen_mpio_rpc_res_t;

/*
 * MPIO message codes.  These are specific to firmware revision 3.
 */
#define	ZEN_MPIO_OP_GET_VERSION		0x00

/*
 * MPIO RPC response codes defined by firmware that may appear in the response
 * register.
 */
#define	ZEN_MPIO_RPC_FW_RESP_OK			0x01
#define	ZEN_MPIO_RPC_FW_RESP_REJ_BUSY		0xfc
#define	ZEN_MPIO_RPC_FW_RESP_REJ_PREREQ		0xfd
#define	ZEN_MPIO_RPC_FW_RESP_UNKNOWN_CMD	0xfe
#define	ZEN_MPIO_RPC_FW_RESP_FAILED		0xff

/*
 * The "ready" bit in the response register is set when MPIO is done processing
 * a command.
 */
#define	ZEN_MPIO_RPC_FW_RESP_READY		(1U << 31)

/*
 * The arguments, request, and response for an RPC sent to MPIO.  Note that the
 * response field holds the raw response from firmware and is kept for debugging
 * and error reporting, and not generally used by callers, which instead examine
 * a zen_mpio_rpc_res_t.
 */
typedef struct zen_mpio_rpc {
	uint32_t		zmr_req;
	uint32_t		zmr_resp;
	uint32_t		zmr_args[6];
} zen_mpio_rpc_t;

/*
 * Synchronously calls the given MPIO RPC.  Returns the RPC status.  Overwrites
 * rpc->zmr_args with data returned by the RPC on success; zmr_args is
 * unmodified if the RPC fails.
 */
extern zen_mpio_rpc_res_t zen_mpio_rpc(zen_iodie_t *iodie, zen_mpio_rpc_t *rpc);

/*
 * The base of the MPIO SMN register space.  This is common across Genoa and
 * Turin.  Note that Milan does not use MPIO.
 */
#define	ZEN_MPIO_SMN_REG_BASE	0x0c910000U

/*
 * Defines a function for accessing MPIO registers.
 */
AMDZEN_MAKE_SMN_REG_FN(zen_mpio_smn_reg, MPIO_RPC,
    ZEN_MPIO_SMN_REG_BASE, 0xfffff000U, 1, 0);

#endif	/* _ZEN_MPIO_IMPL_H */
