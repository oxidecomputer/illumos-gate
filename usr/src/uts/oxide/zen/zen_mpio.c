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
 * Utilities for interacting with MPIO, the post-Milan AMD Zen "MicroProcessor
 * for IO", which is the component that handles things like driving the DXIO
 * crossbar to train PCIe lanes and so forth.
 *
 * In the Milan and earlier world, we interacted with DXIO by sending RPCs to
 * the SMU.  In the payload of the RPC, we set command bits indicating that the
 * RPC was really meant for DXIO, but the implementation beyond that was
 * opaque: conceptually, the SMU was responsible for directing these to
 * whatever that in turn actually implemented the intent of the RPC.  The
 * result of the RPC, as read from the SMU, thus held two statuses: one for the
 * DXIO operation, and the other generically from the SMU.
 *
 * In Genoa and later, the responsibility for interfacing with DXIO shifted
 * from the SMU to a new component, MPIO.  Instead of piggybacking operations
 * for DXIO on top of SMU RPCs, instead we send RPCs directly to MPIO, and
 * read the results directly.
 *
 * The structure of RPCs thus changes slightly.  There are six arguments one
 * may provide; these are written to argument registers on MPIO.  One then
 * strobes a doorbell register, and spins reading from a status register until
 * a completion bit is set.  That register also contains the single status
 * value for the RPC sent.  On completion, the argument registers are read to
 * retrieve data in response to the RPC.  All register reads and writes are
 * done via SMN.
 *
 * Digging into the mechanism a bit, SMN is really a network of AXI4 buses.
 * Writes to the MPIO argument registers are thus AXI bursts that latch 32-bit
 * values into registers on the MPIO microprocessor.  Writing the doorbell
 * register causes MPIO to examine those and perform the specified operation;
 * MPIO will write whatever data the operation specified to the argument
 * registers and then set the status and completion bit in the request register
 * for transfer back to the host CPU.
 */

#include <sys/types.h>
#include <sys/stdbool.h>

#include <sys/io/zen/mpio_impl.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>

#define	ZEN_MPIO_RPC_ARG0(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_arg0, 0)
#define	ZEN_MPIO_RPC_ARG1(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_arg1, 0)
#define	ZEN_MPIO_RPC_ARG2(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_arg2, 0)
#define	ZEN_MPIO_RPC_ARG3(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_arg3, 0)
#define	ZEN_MPIO_RPC_ARG4(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_arg4, 0)
#define	ZEN_MPIO_RPC_ARG5(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_arg5, 0)
#define	ZEN_MPIO_RPC_RESP(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_resp, 0)
#define	ZEN_MPIO_RPC_DOORBELL(ZPCS) \
    zen_mpio_smn_reg(0, (ZPCS)->zpc_mpio_smn_addrs.zmsa_doorbell, 0)

/*
 * Translates the raw MPIO RPC response code from firmware to our internal
 * result code.
 */
static zen_mpio_rpc_res_t
zen_mpio_rpc_resp_to_res(const zen_mpio_rpc_t *rpc)
{
	/*
	 * For RPCs that did not time out, only the low 8 bits of the response
	 * is significant.  In particular, we mask off the READY bit here.
	 */
	switch (rpc->zmr_resp & 0xFF) {
	case ZEN_MPIO_RPC_FW_RESP_OK:		return (ZEN_MPIO_RPC_OK);
	case ZEN_MPIO_RPC_FW_RESP_REJ_BUSY:	return (ZEN_MPIO_RPC_EBUSY);
	case ZEN_MPIO_RPC_FW_RESP_REJ_PREREQ:	return (ZEN_MPIO_RPC_EPREREQ);
	case ZEN_MPIO_RPC_FW_RESP_UNKNOWN_CMD:	return (ZEN_MPIO_RPC_EUNKNOWN);
	case ZEN_MPIO_RPC_FW_RESP_FAILED:	return (ZEN_MPIO_RPC_ERROR);
	default:
		cmn_err(CE_WARN, "Unknown MPIO RPC response (0x%x)",
		    rpc->zmr_resp);
		return (ZEN_MPIO_RPC_EOTHER);
	}
}

/*
 * Return a printable string naming MPIO errors.
 */
const char *
zen_mpio_rpc_res_str(const zen_mpio_rpc_res_t res)
{
	switch (res) {
	case ZEN_MPIO_RPC_OK:		return ("ZEN_MPIO_RPC_OK");
	case ZEN_MPIO_RPC_EBUSY:	return ("ZEN_MPIO_RPC_EBUSY");
	case ZEN_MPIO_RPC_EPREREQ:	return ("ZEN_MPIO_RPC_EPREREQ");
	case ZEN_MPIO_RPC_EUNKNOWN:	return ("ZEN_MPIO_RPC_EUNKNOWN");
	case ZEN_MPIO_RPC_ERROR:	return ("ZEN_MPIO_RPC_ERROR");
	case ZEN_MPIO_RPC_ENOTREADY:	return ("ZEN_MPIO_RPC_ENOTREADY");
	case ZEN_MPIO_RPC_ETIMEOUT:	return ("ZEN_MPIO_RPC_ETIMEOUT");
	case ZEN_MPIO_RPC_EOTHER:	return ("ZEN_MPIO_RPC_EOTHER");
	default:
		panic("Unknown MPIO RPC result code: 0x%x", res);
		break;
	}
}

/*
 * This is an arbitrarily chosen constant to prevent unbounded looping when
 * reading the RPC response register: this is the maximum number of times we'll
 * spin waiting for the READY bit to be set.  We use this because we make MPIO
 * RPCs early enough in boot that that we still don't quite have timers.
 *
 * Empirically, this number takes enough time on every system that we've tried
 * that it should account for any reasonable amount of time required by any RPC.
 */
#define	RPC_READY_MAX_SPIN	(1U << 20)

zen_mpio_rpc_res_t
zen_mpio_rpc(zen_iodie_t *iodie, zen_mpio_rpc_t *rpc)
{
	const zen_platform_consts_t *zpcs = oxide_zen_platform_consts();
	uint32_t resp, req, doorbell;
	zen_mpio_rpc_res_t res;

	req = rpc->zmr_req & 0xFFF;
	ASSERT3U(rpc->zmr_req, ==, req);

	mutex_enter(&iodie->zi_mpio_lock);

	/* Wait until the MPIO engine is ready to receive an RPC. */
	resp = 0;
	for (uint_t k = 0;
	    (resp & ZEN_MPIO_RPC_FW_RESP_READY) == 0 && k < RPC_READY_MAX_SPIN;
	    k++) {
		resp = zen_iodie_read(iodie, ZEN_MPIO_RPC_RESP(zpcs));
	}
	rpc->zmr_resp = resp;

	if ((resp & ZEN_MPIO_RPC_FW_RESP_READY) == 0) {
		mutex_exit(&iodie->zi_mpio_lock);
		cmn_err(CE_WARN, "MPIO failed to become ready for RPC "
		    "(request: 0x%x, MPIO response: 0x%x)", req, resp);
		return (ZEN_MPIO_RPC_ENOTREADY);
	}

	/* Write arguments. */
	zen_iodie_write(iodie, ZEN_MPIO_RPC_ARG0(zpcs), rpc->zmr_args[0]);
	zen_iodie_write(iodie, ZEN_MPIO_RPC_ARG1(zpcs), rpc->zmr_args[1]);
	zen_iodie_write(iodie, ZEN_MPIO_RPC_ARG2(zpcs), rpc->zmr_args[2]);
	zen_iodie_write(iodie, ZEN_MPIO_RPC_ARG3(zpcs), rpc->zmr_args[3]);
	zen_iodie_write(iodie, ZEN_MPIO_RPC_ARG4(zpcs), rpc->zmr_args[4]);
	zen_iodie_write(iodie, ZEN_MPIO_RPC_ARG5(zpcs), rpc->zmr_args[5]);

	/* The request number is written to the response register. */
	zen_iodie_write(iodie, ZEN_MPIO_RPC_RESP(zpcs), req << 8);

	/* Ring the doorbell. */
	doorbell = UINT32_MAX;
	zen_iodie_write(iodie, ZEN_MPIO_RPC_DOORBELL(zpcs), doorbell);

	/* Wait for completion. */
	resp = 0;
	for (uint_t k = 0;
	    (resp & ZEN_MPIO_RPC_FW_RESP_READY) == 0 && k < RPC_READY_MAX_SPIN;
	    k++) {
		resp = zen_iodie_read(iodie, ZEN_MPIO_RPC_RESP(zpcs));
	}
	rpc->zmr_resp = resp;

	/* Check for timeout. */
	if ((resp & ZEN_MPIO_RPC_FW_RESP_READY) == 0) {
		mutex_exit(&iodie->zi_mpio_lock);
		cmn_err(CE_WARN,
		    "MPIO RPC timed out and failed to complete "
		    "(request: 0x%x, MPIO response: 0x%x)", req, resp);
		return (ZEN_MPIO_RPC_ETIMEOUT);
	}

	/* Check firmware result for error. */
	res = zen_mpio_rpc_resp_to_res(rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		mutex_exit(&iodie->zi_mpio_lock);
		cmn_err(CE_WARN,
		    "MPIO RPC failed (request: 0x%x: %s, MPIO response: 0x%x)",
		    req, zen_mpio_rpc_res_str(res), resp);
		return (res);
	}

	/* The RPC was successful; read response. */
	rpc->zmr_args[0] = zen_iodie_read(iodie, ZEN_MPIO_RPC_ARG0(zpcs));
	rpc->zmr_args[1] = zen_iodie_read(iodie, ZEN_MPIO_RPC_ARG1(zpcs));
	rpc->zmr_args[2] = zen_iodie_read(iodie, ZEN_MPIO_RPC_ARG2(zpcs));
	rpc->zmr_args[3] = zen_iodie_read(iodie, ZEN_MPIO_RPC_ARG3(zpcs));
	rpc->zmr_args[4] = zen_iodie_read(iodie, ZEN_MPIO_RPC_ARG4(zpcs));
	rpc->zmr_args[5] = zen_iodie_read(iodie, ZEN_MPIO_RPC_ARG5(zpcs));

	mutex_exit(&iodie->zi_mpio_lock);

	return (ZEN_MPIO_RPC_OK);
}

/*
 * Retrieves and reports the MPIO firmware's version.
 */
bool
zen_mpio_get_fw_version(zen_iodie_t *iodie)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;
	uint32_t v;

	rpc.zmr_req = ZEN_MPIO_OP_GET_VERSION;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO Get Version RPC Failed: %s "
		    "(MPIO response 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}
	v = rpc.zmr_args[0];
	iodie->zi_ndxio_fw = 4;
	iodie->zi_dxio_fw[0] = bitx32(v, 31, 24);
	iodie->zi_dxio_fw[1] = bitx32(v, 23, 16);
	iodie->zi_dxio_fw[2] = bitx32(v, 15, 8);
	iodie->zi_dxio_fw[3] = bitx32(v, 7, 0);


	return (true);
}

void
zen_mpio_report_fw_version(const zen_iodie_t *iodie)
{
	cmn_err(CE_CONT,
	    "?MPIO Firmware Version: 0x%02x.0x%02x.0x%02x.0x%02x\n",
	    iodie->zi_dxio_fw[0], iodie->zi_dxio_fw[1], iodie->zi_dxio_fw[2],
	    iodie->zi_dxio_fw[3]);
}
