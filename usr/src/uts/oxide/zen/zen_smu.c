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
 * Utilities for interacting with the System Management Unit, or SMU.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/cmn_err.h>

#include <sys/io/zen/smu_impl.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>

#define	ZEN_SMU_RPC_REQ(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_req, 0)
#define	ZEN_SMU_RPC_RESP(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_resp, 0)
#define	ZEN_SMU_RPC_ARG0(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_arg0, 0)
#define	ZEN_SMU_RPC_ARG1(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_arg1, 0)
#define	ZEN_SMU_RPC_ARG2(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_arg2, 0)
#define	ZEN_SMU_RPC_ARG3(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_arg3, 0)
#define	ZEN_SMU_RPC_ARG4(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_arg4, 0)
#define	ZEN_SMU_RPC_ARG5(ZPCS) \
    zen_smu_smn_reg(0, (ZPCS)->zpc_smu_smn_addrs.zssa_arg5, 0)

/*
 * Translates the raw SMU RPC response code from firmware to our internal
 * result code.
 */
static zen_smu_rpc_res_t
zen_smu_rpc_resp_to_res(const zen_smu_rpc_t *rpc)
{
	switch (rpc->zsr_resp) {
	case ZEN_SMU_RPC_FW_RESP_OK:		return (ZEN_SMU_RPC_OK);
	case ZEN_SMU_RPC_FW_RESP_REJ_BUSY:	return (ZEN_SMU_RPC_EBUSY);
	case ZEN_SMU_RPC_FW_RESP_REJ_PREREQ:	return (ZEN_SMU_RPC_EPREREQ);
	case ZEN_SMU_RPC_FW_RESP_UNKNOWN_CMD:	return (ZEN_SMU_RPC_EUNKNOWN);
	case ZEN_SMU_RPC_FW_RESP_FAILED:	return (ZEN_SMU_RPC_ERROR);
	default:
		cmn_err(CE_WARN, "Unknown SMU RPC response (0x%x)",
		    rpc->zsr_resp);
		return (ZEN_SMU_RPC_EOTHER);
	}
}

/*
 * Return a printable string naming SMU RPC errors.
 */
const char *
zen_smu_rpc_res_str(const zen_smu_rpc_res_t res)
{
	switch (res) {
	case ZEN_SMU_RPC_OK:		return ("ZEN_SMU_RPC_OK");
	case ZEN_SMU_RPC_EBUSY:		return ("ZEN_SMU_RPC_EBUSY");
	case ZEN_SMU_RPC_EPREREQ:	return ("ZEN_SMU_RPC_EPREREQ");
	case ZEN_SMU_RPC_EUNKNOWN:	return ("ZEN_SMU_RPC_EUNKNOWN");
	case ZEN_SMU_RPC_ERROR:		return ("ZEN_SMU_RPC_ERROR");
	case ZEN_SMU_RPC_ETIMEOUT:	return ("ZEN_SMU_RPC_ETIMEOUT");
	case ZEN_SMU_RPC_EOTHER:	return ("ZEN_SMU_RPC_EOTHER");
	default:
		panic("Unknown SMU result code: 0x%x", res);
		break;
	}
}

/*
 * This is an arbitrarily chosen constant to prevent unbounded looping when
 * reading the RPC response register: this is the maximum number of times we'll
 * spin waiting for the response to change from RPC_NOTDONE.  We use this
 * because we make SMU RPCs early enough in boot that that we still don't quite
 * have timers.
 *
 * Empirically, this number takes enough time on every system that we've tried
 * that it should account for any reasonable amount of time required by any RPC.
 */
#define	RPC_DONE_MAX_SPIN	(1U << 20)

/*
 * This is a pseudo-response code.  We write this to the result register before
 * issuing a SMU RPC; epirically, we have observed firmware won't overwrite this
 * value until the RPC is done, allowing us to detect completion.
 */
#define	RPC_NOTDONE		0x00

zen_smu_rpc_res_t
zen_smu_rpc(zen_iodie_t *iodie, zen_smu_rpc_t *rpc)
{
	const zen_platform_consts_t *zpcs = oxide_zen_platform_consts();
	zen_smu_rpc_res_t res;
	uint32_t resp;

	mutex_enter(&iodie->zi_smu_lock);

	/*
	 * Write a sentinel value to the RPC response register.  When the value
	 * read from the register changes from this value, the RPC is complete.
	 */
	zen_iodie_write(iodie, ZEN_SMU_RPC_RESP(zpcs), RPC_NOTDONE);

	/* Write arguments. */
	zen_iodie_write(iodie, ZEN_SMU_RPC_ARG0(zpcs), rpc->zsr_args[0]);
	zen_iodie_write(iodie, ZEN_SMU_RPC_ARG1(zpcs), rpc->zsr_args[1]);
	zen_iodie_write(iodie, ZEN_SMU_RPC_ARG2(zpcs), rpc->zsr_args[2]);
	zen_iodie_write(iodie, ZEN_SMU_RPC_ARG3(zpcs), rpc->zsr_args[3]);
	zen_iodie_write(iodie, ZEN_SMU_RPC_ARG4(zpcs), rpc->zsr_args[4]);
	zen_iodie_write(iodie, ZEN_SMU_RPC_ARG5(zpcs), rpc->zsr_args[5]);

	/*
	 * Write the request to the request register.  This initiates the
	 * processing of the RPC on the SMU.
	 */
	zen_iodie_write(iodie, ZEN_SMU_RPC_REQ(zpcs), rpc->zsr_req);

	/* Poll the response register for completion. */
	resp = RPC_NOTDONE;
	for (uint_t k = 0; resp == RPC_NOTDONE && k < RPC_DONE_MAX_SPIN; k++)
		resp = zen_iodie_read(iodie, ZEN_SMU_RPC_RESP(zpcs));
	rpc->zsr_resp = resp;

	/* Check for timeout. */
	if (resp == RPC_NOTDONE) {
		mutex_exit(&iodie->zi_smu_lock);
		cmn_err(CE_WARN,
		    "SMU RPC timed out and failed to complete "
		    "(request: 0x%x, MPIO response: 0x%x)", rpc->zsr_req, resp);
		return (ZEN_SMU_RPC_ETIMEOUT);
	}

	/* Check the firmware result for error. */
	res = zen_smu_rpc_resp_to_res(rpc);
	if (res != ZEN_SMU_RPC_OK) {
		mutex_exit(&iodie->zi_smu_lock);
		cmn_err(CE_WARN,
		    "SMU RPC failed (request: 0x%x: %s, SMU response: 0x%x)",
		    rpc->zsr_req, zen_smu_rpc_res_str(res), resp);
		return (res);
	}

	/* The RPC was successful; read response. */
	rpc->zsr_args[0] = zen_iodie_read(iodie, ZEN_SMU_RPC_ARG0(zpcs));
	rpc->zsr_args[1] = zen_iodie_read(iodie, ZEN_SMU_RPC_ARG1(zpcs));
	rpc->zsr_args[2] = zen_iodie_read(iodie, ZEN_SMU_RPC_ARG2(zpcs));
	rpc->zsr_args[3] = zen_iodie_read(iodie, ZEN_SMU_RPC_ARG3(zpcs));
	rpc->zsr_args[4] = zen_iodie_read(iodie, ZEN_SMU_RPC_ARG4(zpcs));
	rpc->zsr_args[5] = zen_iodie_read(iodie, ZEN_SMU_RPC_ARG5(zpcs));

	mutex_exit(&iodie->zi_smu_lock);

	return (ZEN_SMU_RPC_OK);
}

/*
 * Retrieves the firmware version on the SMU associated with the given IO die.
 */
bool
zen_smu_get_fw_version(zen_iodie_t *iodie)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	rpc.zsr_req = ZEN_SMU_OP_GET_VERSION;
	res = zen_smu_rpc(iodie, &rpc);
	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN,
		    "Failed to retrieve SMU firmware version: %s "
		    "(SMU response 0x%x)",
		    zen_smu_rpc_res_str(res), rpc.zsr_resp);
		return (false);
	}

	iodie->zi_smu_fw[0] = ZEN_SMU_OP_GET_VERSION_MAJOR(rpc.zsr_args[0]);
	iodie->zi_smu_fw[1] = ZEN_SMU_OP_GET_VERSION_MINOR(rpc.zsr_args[0]);
	iodie->zi_smu_fw[2] = ZEN_SMU_OP_GET_VERSION_PATCH(rpc.zsr_args[0]);

	return (true);
}

/*
 * Reports the SMU firmware version for the given IO die.  This must follow a
 * call to zen_smu_get_fw_version, above.
 */
void
zen_smu_report_fw_version(const zen_iodie_t *iodie)
{
	zen_soc_t *soc = iodie->zi_soc;

	cmn_err(CE_CONT, "?Socket %u SMU Version: %u.%u.%u\n",
	    soc->zs_num, iodie->zi_smu_fw[0], iodie->zi_smu_fw[1],
	    iodie->zi_smu_fw[2]);
}

/*
 * Returns true if the firmware version running on the SMU for the given IO die
 * is greater than or equal to the given major, minor, and patch versions.
 */
bool
zen_smu_version_at_least(const zen_iodie_t *iodie,
    const uint8_t major, const uint8_t minor, const uint8_t patch)
{
	return (iodie->zi_smu_fw[0] > major ||
	    (iodie->zi_smu_fw[0] == major && iodie->zi_smu_fw[1] > minor) ||
	    (iodie->zi_smu_fw[0] == major && iodie->zi_smu_fw[1] == minor &&
	    iodie->zi_smu_fw[2] >= patch));
}

/*
 * buf and len semantics here match those of snprintf
 */
bool
zen_smu_get_brand_string(zen_iodie_t *iodie, char *buf, size_t len)
{
	zen_smu_rpc_t rpc = { 0 };

	ASSERT(len > 0);

	len = MIN(len - 1, CPUID_BRANDSTR_STRLEN);
	rpc.zsr_req = ZEN_SMU_OP_GET_BRAND_STRING;

	/*
	 * Read the brand string by repeatedly calling the SMU, retrieving
	 * "chunks" of the string that are packed into 32-bit integers, and
	 * copying those into `buf`; the argument to the SMU RPC is the index of
	 * the 4-byte chunk we want to read.  Note that the last chunk, as
	 * counted by the `len` argument, may have fewer than the 4 bytes
	 * required for a 32-bit value, so we take care to handle it specially.
	 */
	for (size_t off = 0; off < len; off += sizeof (uint32_t)) {
		zen_smu_rpc_res_t res;
		size_t chunk_size, chunkno;

		chunkno = off / sizeof (uint32_t);
		ASSERT3U(chunkno, <=, UINT32_MAX);
		rpc.zsr_args[0] = (uint32_t)chunkno;
		res = zen_smu_rpc(iodie, &rpc);
		if (res != ZEN_SMU_RPC_OK) {
			cmn_err(CE_WARN, "SMU Read Brand String Failed: %s "
			    "(offset %lu, SMU 0x%x)", zen_smu_rpc_res_str(res),
			    off, rpc.zsr_resp);
			return (false);
		}
		/*
		 * The `MIN` here accounts for the possibility that the last
		 * chunk may have fewer than `sizeof (uint32_t)` bytes.
		 */
		chunk_size = MIN(sizeof (uint32_t), len - off);
		bcopy(&rpc.zsr_args[0], buf + off, chunk_size);
	}
	buf[len] = '\0';

	return (true);
}

bool
zen_null_smu_features_init(zen_iodie_t *iodie __unused)
{
	return (true);
}

bool
zen_smu_early_features_init(zen_iodie_t *iodie)
{
	const zen_smu_ops_t *ops = oxide_zen_smu_ops();
	VERIFY3P(ops->zsmuo_early_features_init, !=, NULL);
	return (ops->zsmuo_early_features_init(iodie));
}

bool
zen_smu_features_init(zen_iodie_t *iodie)
{
	const zen_smu_ops_t *ops = oxide_zen_smu_ops();
	VERIFY3P(ops->zsmuo_features_init, !=, NULL);
	return (ops->zsmuo_features_init(iodie));
}
