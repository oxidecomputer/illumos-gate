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
 *
 * -----------
 * UBM Hotplug
 * -----------
 *
 * In addition to the traditional SMU based hotplug (e.g. ExpressModule,
 * Enterprise SSD, etc.), MPIO adds support for the SFF-TA-1005 Universal
 * Backplane Module (UBM) based hotplug. UBM consists of a series of 'Host
 * Facing Connectors' (HFCs) which are basically root ports on the AMD SoC and
 * 'Downstream Facing Connectors' (DFCs) which are basically U.2 (SFF-8639)
 * style connectors or something entirely different.
 *
 * A UBM based system has a series of UBM controllers that may embed static
 * EEPROMs and optional control interfaces. These EEPROMs allow a system to
 * dynamically discover the configuration of the downstream connectors and
 * allows for even changing the PHY type at run-time between PCIe and SATA. This
 * information is all transited over I2C.
 *
 * When dealing with a UBM system, we have to ask MPIO to enumerate all of the
 * HFC and DFC information over I2C for us. Based on this information, we
 * transform it into data in the initial ASK. There is a small wrinkle here.
 * There is an instance of MPIO in each I/O die, which is why we have to have a
 * per-I/O die ASK. However, like with traditional hotplug, only socket 0 is
 * actually connected to the I2C bus. This means that we must specifically send
 * the I2C enumeration and DFC information request RPCs to I/O die 0's MPIO
 * instance, but come back and put the actual ASK information in each I/O die's
 * corresponding buffer. This is because the actual underlying SoC's DXIO
 * crossbar can only be manipulated by the local MPIO service.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/ddi_subrdefs.h>
#include <sys/platform_detect.h>

#include <sys/io/zen/hacks.h>
#include <sys/io/zen/mpio_impl.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/hotplug.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/ruby_dxio_data.h>
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
 * Note that this has evolved over time during the development process: early
 * on, we started with a sufficiently high number that the timeout was
 * effectively infinite, but not useful; as we got further and implemented
 * simple RPCs that completed quickly, we used a lower timeout, but as training
 * and related processes started to actually work, we found it was too small.
 * What we have now seems about right, but it is not a number derived from a
 * serious mathematical analysis of average case times, or anything similar.
 */
#define	RPC_READY_MAX_SPIN	(1U << 24)

/*
 * Similarly, this constant is the maximum number of times that we will spin
 * waiting for MPIO to indicate that it is ready to begin processing some
 * asynchronous operation (such as a posted operation, or DMA transfer).  That
 * is, this is the number of times we will invoke the status retrieval RPC and
 * test its response to see whether MPIO is ready to being a new (async)
 * operation; this is distinct from whether MPIO is in a position to receive
 * another RPC, as indicated by the READY bit being set in the RPC response
 * register.
 *
 * This value was chosen arbitrarily and has never been adjusted, but probably
 * could be smaller.
 */
#define	RPC_MAX_WAIT_READY	(1U << 30)

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
		cmn_err(CE_WARN, "MPIO Get Version RPC Failed: %s (MPIO: 0x%x)",
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

bool
zen_mpio_rpc_get_status(zen_iodie_t *iodie, zen_mpio_status_t *status)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_GET_STATUS;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO Get Status Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}
	CTASSERT(sizeof (rpc.zmr_args) == sizeof (*status));
	bcopy(rpc.zmr_args, status, sizeof (*status));

	return (true);
}

static bool
zen_mpio_wait_ready(zen_iodie_t *iodie)
{
	zen_mpio_status_t status = { 0 };

	for (uint_t k = 0; k < RPC_MAX_WAIT_READY; k++) {
		if (!zen_mpio_rpc_get_status(iodie, &status)) {
			cmn_err(CE_WARN, "MPIO wait ready RPC failed");
			return (false);
		}
		if (status.zms_cmd_stat == 0)
			return (true);
	}
	cmn_err(CE_WARN, "MPIO wait ready timed out, cmd status: 0x%x",
	    status.zms_cmd_stat);

	return (false);
}

/*
 * Note this is specific to UBM, which is only used on development boards during
 * software bringup. Note, the UBM RPCs only truly having meaning on the primary
 * socket as the I2C interface is only allowed to be connected there. We require
 * that this RPC be sent only to that instance of MPIO.
 */
static bool
zen_mpio_rpc_ubm_enumerate_i2c(zen_iodie_t *iodie)
{
	zen_ubm_config_t *conf;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	VERIFY3P(iodie, !=, NULL);
	VERIFY0(iodie->zi_soc->zs_num);

	conf = &iodie->zi_soc->zs_fabric->zf_ubm;
	ASSERT3P(conf->zuc_hfc_ports, !=, NULL);
	VERIFY3U(conf->zuc_hfc_ports_pa, !=, 0);
	VERIFY3U(conf->zuc_hfc_ports_pa, <, 0xFFFFFFFFU);

	/*
	 * Sadly, this RPC can only accept 32-bits worth of a
	 * physical address.  Thus, the data is artificially
	 * constrained to be in the first 4GiB of address space
	 * by DMA attributes.
	 */
	rpc.zmr_args[0] = (uint32_t)conf->zuc_hfc_ports_pa;
	rpc.zmr_args[1] = conf->zuc_hfc_nports;
	rpc.zmr_req = ZEN_MPIO_OP_ENUMERATE_I2C;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO I2C Enumerate RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_rpc_ubm_get_i2c_device(zen_iodie_t *iodie, uint32_t hfc, uint32_t dfc,
    zen_mpio_ubm_dfc_descr_t *descr)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_args[0] = hfc;
	rpc.zmr_args[1] = dfc;
	rpc.zmr_args[2] = 0;  /* Only used for OCP, which we don't handle. */
	rpc.zmr_req = ZEN_MPIO_OP_GET_I2C_DEV;
	res = zen_mpio_rpc(iodie, &rpc);
	/*
	 * This oddly-different method of testing for success mirrors AGESA,
	 * which appears to allow non-zero return values for this RPC.
	 */
	if (res != ZEN_MPIO_RPC_OK && (rpc.zmr_resp & 0xFF) != 0) {
		return (false);
	}
	CTASSERT(sizeof (*descr) <=
	    (sizeof (rpc.zmr_args) - sizeof (rpc.zmr_args[0])));
	bcopy(&rpc.zmr_args[1], descr, sizeof (*descr));

	return (true);
}

/*
 * Address here is a 7-bit I2C address (8 bits with the R/W bit).
 */
bool
zen_mpio_rpc_set_i2c_switch_addr(zen_iodie_t *iodie, uint8_t i2addr)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;
	uint32_t addr = i2addr * 0x100;

	rpc.zmr_req = ZEN_MPIO_OP_SET_HP_I2C_SW_ADDR;
	rpc.zmr_args[0] = addr;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO Set i2c address RPC Failed: %s "
		    "(addr: 0x%x, MPIO 0x%x)",
		    zen_mpio_rpc_res_str(res), addr, rpc.zmr_resp);
		return (false);
	}

	return (true);
}

/*
 * Do MPIO global configuration initialization.  Unlike earlier systems that did
 * this via DXIO and discrete RPCs, MPIO takes a single global configuration
 * parameter in an RPC.
 *
 * The specific values we use here are taken from AMD's recommendations.
 * TODO: Add clock gating back in.
 */
static bool
zen_mpio_init_global_config(zen_iodie_t *iodie)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_global_config_t *args;
	zen_mpio_rpc_res_t res;
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();

	VERIFY3P(fops->zfo_set_mpio_global_config, !=, NULL);
	rpc.zmr_req = ZEN_MPIO_OP_SET_GLOBAL_CONFIG;
	args = (zen_mpio_global_config_t *)rpc.zmr_args;
	fops->zfo_set_mpio_global_config(args);
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO set global config RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_send_ext_attrs(zen_iodie_t *iodie, void *arg)
{
	zen_mpio_config_t *conf;
	zen_mpio_xfer_ext_attrs_args_t *args;
	zen_mpio_xfer_ext_attrs_resp_t *resp;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	ASSERT3P(iodie, !=, NULL);
	conf = &iodie->zi_mpio_conf;
	ASSERT(conf->zmc_ext_attrs != NULL);
	ASSERT3U(conf->zmc_ext_attrs_pa, !=, 0);

	rpc.zmr_req = ZEN_MPIO_OP_XFER_EXT_ATTRS;
	args = (zen_mpio_xfer_ext_attrs_args_t *)rpc.zmr_args;
	args->zmxeaa_paddr_hi = conf->zmc_ext_attrs_pa >> 32;
	args->zmxeaa_paddr_lo = conf->zmc_ext_attrs_pa & 0xFFFFFFFFU;
	VERIFY0(conf->zmc_ext_attrs_len % 4);
	args->zmxeaa_nwords = conf->zmc_ext_attrs_len / 4;
	res = zen_mpio_rpc(iodie, &rpc);
	resp = (zen_mpio_xfer_ext_attrs_resp_t *)rpc.zmr_args;
	if (res != ZEN_MPIO_RPC_OK ||
	    resp->zxear_res != ZEN_MPIO_FW_EXT_ATTR_XFER_RES_OK) {
		cmn_err(CE_WARN,
		    "MPIO transfer ext attrs RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

uint32_t
zen_mpio_ubm_idx(const zen_iodie_t *iodie)
{
	return (iodie->zi_soc->zs_num * ZEN_FABRIC_MAX_DIES_PER_SOC +
	    iodie->zi_num);
}

static void
zen_mpio_ubm_hfc_init(zen_iodie_t *iodie, zen_ubm_hfc_t *hfc)
{
	zen_mpio_config_t *conf = &iodie->zi_mpio_conf;
	uint32_t dfcno = 0;
	zen_mpio_ubm_dfc_descr_t dfc;

	/*
	 * The number of DFCs changes for each HFC, and is discovered when
	 * requesting I2C information for the first DFC.
	 */
	do {
		zen_mpio_ask_port_t *ask;

		VERIFY3U(conf->zmc_ask_nports, <, ZEN_MPIO_ASK_MAX_PORTS);
		if (!zen_mpio_rpc_ubm_get_i2c_device(iodie, hfc->zuh_num, dfcno,
		    &dfc)) {
			cmn_err(CE_PANIC, "%s: failed to get DFC information "
			    "for DFC %u", hfc->zuh_oxio->oe_name, dfcno);
		}
		if (dfcno == 0)
			hfc->zuh_ndfcs = dfc.zmudd_ndfcs;
		if (hfc->zuh_ndfcs == 0)
			return;

		ask = &conf->zmc_ask->zma_ports[conf->zmc_ask_nports++];
		oxio_ubm_to_ask(hfc, &dfc, dfcno, ask);

		++dfcno;
	} while (dfcno < hfc->zuh_ndfcs);
}

bool
zen_mpio_send_hotplug_table(zen_iodie_t *iodie, uint64_t paddr)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_SEND_HP_CFG_TBL;
	rpc.zmr_args[0] = bitx64(paddr, 31, 0);
	rpc.zmr_args[1] = bitx64(paddr, 63, 32);
	rpc.zmr_args[2] = sizeof (zen_mpio_hotplug_table_t);
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO TX Hotplug Table Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

bool
zen_mpio_rpc_hotplug_flags(zen_iodie_t *iodie, uint32_t flags)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_SET_HP_FLAGS;
	rpc.zmr_args[0] = flags;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO Set Hotplug Flags failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

bool
zen_mpio_rpc_start_hotplug(zen_iodie_t *iodie, uint32_t flags)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_HOTPLUG_EN;
	rpc.zmr_args[0] = flags;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO Start Hotplug Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

/*
 * This is the per-I/O die callback to transform the generated UBM data into the
 * corresponding form for our ASK.
 */
static int
zen_mpio_init_ubm_iodie(zen_iodie_t *iodie, void *arg)
{
	zen_fabric_t *fabric = iodie->zi_soc->zs_fabric;
	zen_ubm_config_t *ubm = &fabric->zf_ubm;
	const uint32_t ubm_idx = zen_mpio_ubm_idx(iodie);

	for (uint32_t i = 0; i < ubm->zuc_die_nports[ubm_idx]; i++) {
		zen_ubm_hfc_t *hfc;
		uint32_t hfcno = ubm->zuc_die_idx[ubm_idx] + i;
		hfc = &ubm->zuc_hfc[hfcno];
		ASSERT3U(hfcno, ==, hfc->zuh_num);

		zen_mpio_ubm_hfc_init(iodie, hfc);
	}

	return (0);
}

/*
 * We need to transform the UBM data that we've gathered and perform initial
 * enumeration. This is a little nuanced. While DFCs PCIe and SATA lanes may be
 * connected to both processors in a dual socket system, the I2C network is only
 * ever connected to processor zero, like in traditional hotplug. As such, we
 * have to ask the MPIO instance on I/O die 0 to perform all of the RPCs, but
 * then translate the results back into each socket's ASK as the ASK is per-I/O
 * die.
 */
static bool
zen_mpio_init_ubm(zen_fabric_t *fabric)
{
	zen_iodie_t *iodie;

	if ((fabric->zf_flags & ZEN_FABRIC_F_UBM_HOTPLUG) == 0) {
		return (true);
	}

	iodie = &fabric->zf_socs[0].zs_iodies[0];
	if (!zen_mpio_rpc_ubm_enumerate_i2c(iodie)) {
		return (false);
	}

	if (zen_fabric_walk_iodie(fabric, zen_mpio_init_ubm_iodie, NULL) != 0) {
		return (false);
	}

	return (true);
}

static bool
zen_mpio_send_ask(zen_iodie_t *iodie)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_config_t *conf;
	zen_mpio_xfer_ask_args_t *args;
	zen_mpio_xfer_ask_resp_t *resp;
	zen_mpio_rpc_res_t res;

	ASSERT3P(iodie, !=, NULL);
	conf = &iodie->zi_mpio_conf;
	ASSERT3P(conf, !=, NULL);
	ASSERT3P(conf->zmc_ask, !=, NULL);
	ASSERT3U(conf->zmc_ask_pa, !=, 0);

	if (!zen_mpio_wait_ready(iodie)) {
		cmn_err(CE_WARN, "MPIO wait for ready to send ASK failed");
		return (false);
	}

	rpc.zmr_req = ZEN_MPIO_OP_XFER_ASK;
	args = (zen_mpio_xfer_ask_args_t *)rpc.zmr_args;
	args->zmxaa_paddr_hi = conf->zmc_ask_pa >> 32;
	args->zmxaa_paddr_lo = conf->zmc_ask_pa & 0xFFFFFFFFU;
	args->zmxaa_link_count = conf->zmc_ask_nports;
	/*
	 * Transfer the ASK from RAM to MPIO via DMA.  We are asking MPIO to
	 * look at the links we have "selected" by inclusion in the ASK.  AGESA
	 * sets this unconditionally.
	 */
	args->zmxaa_links = ZEN_MPIO_LINK_SELECTED;
	args->zmxaa_dir = ZEN_MPIO_XFER_FROM_RAM;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO transfer ASK RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}
	resp = (zen_mpio_xfer_ask_resp_t *)rpc.zmr_args;
	if (resp->zmxar_res != ZEN_MPIO_FW_ASK_XFER_RES_OK) {
		cmn_err(CE_WARN, "ASK rejected by MPIO: MPIO Resp: 0x%x",
		    rpc.zmr_args[0]);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_recv_ask(zen_iodie_t *iodie)
{
	zen_mpio_config_t *conf;
	zen_mpio_xfer_ask_args_t *args;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	ASSERT3P(iodie, !=, NULL);
	conf = &iodie->zi_mpio_conf;
	ASSERT3P(conf, !=, NULL);
	ASSERT3P(conf->zmc_ask, !=, NULL);
	ASSERT3U(conf->zmc_ask_pa, !=, 0);

	if (!zen_mpio_wait_ready(iodie)) {
		cmn_err(CE_WARN, "MPIO wait for ready to receive ASK failed");
		return (false);
	}

	rpc.zmr_req = ZEN_MPIO_OP_GET_ASK_RESULT;
	args = (zen_mpio_xfer_ask_args_t *)rpc.zmr_args;
	args->zmxaa_paddr_hi = conf->zmc_ask_pa >> 32;
	args->zmxaa_paddr_lo = conf->zmc_ask_pa & 0xFFFFFFFFU;
	/*
	 * Retrieve a copy of the ASK from MPIO; here, we ask MPIO to send us
	 * information about all links that it knows about (e.g., from previous
	 * ASKs that we sent it).  AGESA sets this unconditionally.
	 */
	args->zmxaa_links = ZEN_MPIO_LINK_ALL;
	args->zmxaa_dir = ZEN_MPIO_XFER_TO_RAM;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO recveive ASK RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_setup_link_post_map(zen_iodie_t *iodie)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_link_setup_args_t *args;
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_POSTED_SETUP_LINK;
	args = (zen_mpio_link_setup_args_t *)rpc.zmr_args;
	args->zmlsa_map = 1;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO setup link RPC failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_setup_link_post_config_reconfig(zen_iodie_t *iodie)
{
	zen_mpio_link_setup_args_t *args;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_POSTED_SETUP_LINK;
	args = (zen_mpio_link_setup_args_t *)rpc.zmr_args;
	args->zmlsa_configure = 1;
	args->zmlsa_reconfigure = 1;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO setup link RPC failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_setup_link_post_perst_req(zen_iodie_t *iodie)
{
	zen_mpio_link_setup_args_t *args;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_POSTED_SETUP_LINK;
	args = (zen_mpio_link_setup_args_t *)rpc.zmr_args;
	args->zmlsa_perst_req = 1;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO setup link RPC failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static bool
zen_mpio_setup_link_train_enumerate(zen_iodie_t *iodie)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_link_setup_args_t *args;
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_POSTED_SETUP_LINK;
	args = (zen_mpio_link_setup_args_t *)rpc.zmr_args;
	args->zmlsa_training = 1;
	args->zmlsa_enumerate = 1;
	args->zmlsa_early = 0;  /* We do not early train. */
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO setup link train/enum failed: "
		    "%s (MPIO: 0x%x)", zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static int
zen_mpio_send_data(zen_iodie_t *iodie, void *arg __unused)
{
	if (!zen_mpio_send_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO send ASK failed");
		return (1);
	}

	return (0);
}

/*
 * Depending on the platform and fused secure state of the processor, we may not
 * be able to access the PCIe core and port registers via the normal SMN
 * routines and instead must proxy through MPIO.
 */

static bool
zen_mpio_read_pcie_reg(zen_iodie_t *iodie, const smn_reg_t reg, uint32_t *val)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	VERIFY(SMN_REG_UNIT(reg) == SMN_UNIT_PCIE_CORE ||
	    SMN_REG_UNIT(reg) == SMN_UNIT_PCIE_PORT);
	VERIFY(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	VERIFY(SMN_REG_SIZE_IS_VALID(reg));

	rpc.zmr_req = ZEN_MPIO_OP_RDWR_PCIE_PROXY;
	rpc.zmr_args[0] = SMN_REG_ADDR_BASE(reg);

	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO PCIe reg 0x%x read failed: %s "
		    "(MPIO: 0x%x)", SMN_REG_ADDR(reg),
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}
	*val = (rpc.zmr_args[0] >> (SMN_REG_ADDR_OFF(reg) << 3)) &
	    SMN_REG_SIZE_MASK(reg);

	return (true);
}

static bool
zen_mpio_write_pcie_reg(zen_iodie_t *iodie, const smn_reg_t reg,
    uint32_t val)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);

	VERIFY(SMN_REG_UNIT(reg) == SMN_UNIT_PCIE_CORE ||
	    SMN_REG_UNIT(reg) == SMN_UNIT_PCIE_PORT);
	VERIFY(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	VERIFY(SMN_REG_SIZE_IS_VALID(reg));
	VERIFY(SMN_REG_VALUE_FITS(reg, val));

	rpc.zmr_req = ZEN_MPIO_OP_RDWR_PCIE_PROXY;
	rpc.zmr_args[0] = SMN_REG_ADDR_BASE(reg);
	rpc.zmr_args[1] = SMN_REG_SIZE_MASK(reg) << (addr_off << 3);
	rpc.zmr_args[2] = val << (addr_off << 3);

	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO PCIe reg 0x%x write failed: %s "
		    "(MPIO: 0x%x)", SMN_REG_ADDR(reg),
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

uint32_t
zen_mpio_pcie_core_read(zen_pcie_core_t *pc, const smn_reg_t reg)
{
	zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;
	uint32_t val;

	ASSERT3U(SMN_REG_UNIT(reg), ==, SMN_UNIT_PCIE_CORE);
	VERIFY(zen_mpio_read_pcie_reg(iodie, reg, &val));

	return (val);
}

void
zen_mpio_pcie_core_write(zen_pcie_core_t *pc, const smn_reg_t reg,
    const uint32_t val)
{
	zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;

	ASSERT3U(SMN_REG_UNIT(reg), ==, SMN_UNIT_PCIE_CORE);
	VERIFY(zen_mpio_write_pcie_reg(iodie, reg, val));
}

uint32_t
zen_mpio_pcie_port_read(zen_pcie_port_t *port, const smn_reg_t reg)
{
	zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;
	uint32_t val;

	ASSERT3U(SMN_REG_UNIT(reg), ==, SMN_UNIT_PCIE_PORT);
	VERIFY(zen_mpio_read_pcie_reg(iodie, reg, &val));

	return (val);
}

void
zen_mpio_pcie_port_write(zen_pcie_port_t *port, const smn_reg_t reg,
    const uint32_t val)
{
	zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;

	ASSERT3U(SMN_REG_UNIT(reg), ==, SMN_UNIT_PCIE_PORT);
	VERIFY(zen_mpio_write_pcie_reg(iodie, reg, val));
}

bool
zen_mpio_write_pcie_strap(zen_pcie_core_t *pc,
    const uint32_t addr, const uint32_t data)
{
	zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_PCIE_WRITE_STRAP;
	rpc.zmr_args[0] = addr;
	rpc.zmr_args[1] = data;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "writing strap (addr 0x%x data 0x%x) failed: "
		    " %s (MPIO 0x%x)", addr, data, zen_mpio_rpc_res_str(res),
		    rpc.zmr_resp);
		return (false);
	}

	return (true);
}

/*
 * Transform all of the per-socket OXIO data into the appropriate form for the
 * MPIO subsystem. We will place all standard devices into the ASK first, while
 * assembling UBM related devices into the UBM data if required.
 */
static int
zen_mpio_init_data(zen_iodie_t *iodie, void *arg)
{
	zen_mpio_config_t *conf = &iodie->zi_mpio_conf;
	zen_fabric_t *fabric = iodie->zi_soc->zs_fabric;
	zen_ubm_config_t *ubm = &fabric->zf_ubm;
	const uint32_t ubm_idx = zen_mpio_ubm_idx(iodie);
	ddi_dma_attr_t attr;
	pfn_t pfn;
	bool has_ubm;

	if (iodie->zi_nengines == 0)
		return (0);

	/*
	 * Always create the DMA region for the ASK and the extra attributes. If
	 * we encounter UBM data, then we'll create it on demand.
	 */
	zen_fabric_dma_attr(&attr);
	conf->zmc_ask_alloc_len = MMU_PAGESIZE;
	conf->zmc_ask = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->zmc_ask, MMU_PAGESIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zmc_ask);
	conf->zmc_ask_pa = mmu_ptob((uint64_t)pfn);

	conf->zmc_ext_attrs_alloc_len = MMU_PAGESIZE;
	conf->zmc_ext_attrs = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE,
	    1);
	bzero(conf->zmc_ext_attrs, MMU_PAGESIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zmc_ext_attrs);
	conf->zmc_ext_attrs_pa = mmu_ptob((uint64_t)pfn);


	/*
	 * Walk each engine and determine whether we should append it to the ASK
	 * now (PCIe) or if we need to allocate and map it to UBM.
	 */
	for (size_t i = 0; i < iodie->zi_nengines; i++) {
		const oxio_engine_t *oxio = &iodie->zi_engines[i];

		if (oxio->oe_type == OXIO_ENGINE_T_PCIE) {
			oxio_eng_to_ask(oxio,
			    &conf->zmc_ask->zma_ports[conf->zmc_ask_nports]);
			conf->zmc_ask_nports++;
		} else if (oxio->oe_type == OXIO_ENGINE_T_UBM) {
			has_ubm = true;
		} else {
			panic("%s: encountered invalid OXIO engine type 0x%x",
			    oxio->oe_name, oxio->oe_type);
		}

	}

	if (!has_ubm) {
		return (0);
	}

	if (ubm->zuc_hfc_ports == NULL) {
		/*
		 * Note that we explicitly set attr.dma_attr_addr_hi
		 * here to emphasize that RPC to DMA zmc_ubm_hfc_ports
		 * to MPIO requires that a 32-bit address (the RPC only
		 * accepts a single uint32_t for the DMA address).
		 */
		zen_fabric_dma_attr(&attr);
		attr.dma_attr_addr_hi = UINT32_MAX;
		ubm->zuc_hfc_ports_alloc_len = MMU_PAGESIZE;
		ubm->zuc_hfc_ports = contig_alloc(MMU_PAGESIZE,
		    &attr, MMU_PAGESIZE, 1);
		bzero(ubm->zuc_hfc_ports, MMU_PAGESIZE);
		pfn = hat_getpfnum(kas.a_hat, (caddr_t)ubm->zuc_hfc_ports);
		ubm->zuc_hfc_ports_pa = mmu_ptob((uint64_t)pfn);
		fabric->zf_flags |= ZEN_FABRIC_F_UBM_HOTPLUG;
	}

	/*
	 * Snapshot the starting HFC number for this I/O die.
	 */
	ubm->zuc_die_idx[ubm_idx] = ubm->zuc_hfc_nports;

	for (size_t i = 0; i < iodie->zi_nengines; i++) {
		const oxio_engine_t *oxio = &iodie->zi_engines[i];

		if (oxio->oe_type != OXIO_ENGINE_T_UBM) {
			continue;
		}

		VERIFY3U(ubm->zuc_hfc_nports, <, ZEN_MAX_UBM_HFC);
		ubm->zuc_hfc[ubm->zuc_hfc_nports].zuh_oxio = oxio;
		ubm->zuc_hfc[ubm->zuc_hfc_nports].zuh_num = ubm->zuc_hfc_nports;
		ubm->zuc_hfc[ubm->zuc_hfc_nports].zuh_hfc =
		    &ubm->zuc_hfc_ports[ubm->zuc_hfc_nports];

		oxio_eng_to_ubm(oxio, &ubm->zuc_hfc_ports[ubm->zuc_hfc_nports]);
		ubm->zuc_hfc_nports++;
		ubm->zuc_die_nports[ubm_idx]++;
	}

	return (0);
}

/*
 * Given all of the engines on an I/O die, try and map each one to a
 * corresponding IOMS and bridge. We only care about an engine if it is a PCIe
 * engine. Note, because each I/O die is processed independently, this only
 * operates on a single I/O die. As part of this we map this back to the
 * corresponding OXIO engine information and fill in common information.
 */
static bool
zen_mpio_map_engines(zen_fabric_t *fabric, zen_iodie_t *iodie)
{
	bool ret = true;
	zen_mpio_config_t *conf = &iodie->zi_mpio_conf;

	for (uint32_t i = 0; i < conf->zmc_ask_nports; i++) {
		zen_mpio_ask_port_t *ap = &conf->zmc_ask->zma_ports[i];
		zen_mpio_link_t *lp = &ap->zma_link;
		zen_pcie_core_t *pc;
		zen_pcie_port_t *port;
		uint32_t start_lane, end_lane;
		uint8_t portno;

		if (lp->zml_ctlr_type != ZEN_MPIO_ASK_LINK_PCIE)
			continue;

		start_lane = lp->zml_lane_start;
		end_lane = start_lane + lp->zml_num_lanes - 1;

		pc = zen_fabric_find_pcie_core_by_lanes(iodie,
		    start_lane, end_lane);
		if (pc == NULL) {
			cmn_err(CE_WARN,
			    "failed to map engine %u [%u, %u] to a PCIe core",
			    i, start_lane, end_lane);
			ret = false;
			continue;
		}

		portno = ap->zma_status.zmils_port;
		if (portno >= pc->zpc_nports) {
			cmn_err(CE_WARN,
			    "failed to map engine %u [%u, %u] to a PCIe port: "
			    "found nports %u, but mapped to port %u",
			    i, start_lane, end_lane,
			    pc->zpc_nports, portno);
			ret = false;
			continue;
		}

		port = &pc->zpc_ports[portno];
		if (port->zpp_ask_port != NULL) {
			zen_mpio_link_t *l = &port->zpp_ask_port->zma_link;
			cmn_err(CE_WARN, "engine %u [%u, %u] mapped to "
			    "port %u, which already has an engine [%u, %u]",
			    i, start_lane, end_lane, pc->zpc_nports,
			    l->zml_lane_start,
			    l->zml_lane_start + l->zml_num_lanes - 1);
			ret = false;
			continue;
		}

		port->zpp_flags |= ZEN_PCIE_PORT_F_MAPPED;
		port->zpp_ask_port = ap;
		pc->zpc_flags |= ZEN_PCIE_CORE_F_USED;

		/*
		 * Now that we've found the port and the MPIO engine, map it
		 * back to the original OXIO engine that spawned this. This will
		 * also take care of any HFC / DFC mapping that needs to occur.
		 */
		oxio_mpio_to_eng(port);
	}

	return (ret);
}

static int
zen_mpio_init_mapping(zen_iodie_t *iodie, void *arg)
{
	zen_fabric_t *fabric = iodie->zi_soc->zs_fabric;

	if (!zen_mpio_setup_link_post_map(iodie) || !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO map failed");
		return (1);
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_MAPPED, iodie->zi_node_id);

	if (!zen_mpio_map_engines(iodie->zi_soc->zs_fabric, iodie)) {
		cmn_err(CE_WARN, "Socket %u failed to map all DXIO engines "
		    "to devices.  PCIe will not function",
		    iodie->zi_soc->zs_num);
		return (1);
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_MAPPED_POST, iodie->zi_node_id);

	return (0);
}

static int
zen_mpio_more_conf(zen_iodie_t *iodie, void *arg __unused)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	zen_fabric_t *fabric = iodie->zi_soc->zs_fabric;

	(void) zen_fabric_walk_pcie_core(iodie->zi_soc->zs_fabric,
	    zen_fabric_pcie_core_op, fops->zfo_init_pcie_straps);
	cmn_err(CE_CONT, "?Socket %u MPIO: Wrote PCIe straps\n",
	    iodie->zi_soc->zs_num);

	(void) zen_fabric_walk_pcie_port(iodie->zi_soc->zs_fabric,
	    zen_fabric_pcie_port_op, fops->zfo_init_pcie_port);
	cmn_err(CE_CONT, "?Socket %u MPIO: Init PCIe port registers\n",
	    iodie->zi_soc->zs_num);

	if (!zen_mpio_setup_link_post_config_reconfig(iodie) ||
	    !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO config/reconfig failed");
		return (1);
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_CONFIGURED, iodie->zi_node_id);

	(void) zen_fabric_walk_pcie_port(iodie->zi_soc->zs_fabric,
	    zen_fabric_pcie_port_op, fops->zfo_init_pcie_port_after_reconfig);
	cmn_err(CE_CONT,
	    "?Socket %u MPIO: Init PCIe port registers post reconfig\n",
	    iodie->zi_soc->zs_num);

	zen_pcie_populate_dbg(fabric, ZPCS_SM_CONFIGURED_POST,
	    iodie->zi_node_id);

	if (!zen_mpio_setup_link_post_perst_req(iodie) ||
	    !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO PERST request failed");
		return (1);
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_PERST, iodie->zi_node_id);

	if (iodie->zi_node_id == 0) {
		for (size_t i = 0;
		    i < oxide_board_data->obd_perst_gpios_len;
		    i++) {
			zen_hack_gpio(ZHGOP_SET,
			    oxide_board_data->obd_perst_gpios[i]);
		}
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_PERST_POST, iodie->zi_node_id);

	if (!zen_mpio_setup_link_train_enumerate(iodie) ||
	    !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO train and enumerate request failed");
		return (1);
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_DONE, iodie->zi_node_id);

	return (0);
}

/*
 * MPIO-level PCIe initialization: training links and mapping bridges and so on.
 */
void
zen_mpio_pcie_init(zen_fabric_t *fabric)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();

	zen_fabric_walk_pcie_port(fabric, zen_fabric_pcie_port_op,
	    fops->zfo_pcie_port_unhide_bridge);

	if (zen_fabric_walk_iodie(fabric, zen_mpio_init_data, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO ASK Initialization failed");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, zen_fabric_iodie_op,
	    zen_mpio_init_global_config) != 0) {
		cmn_err(CE_WARN, "MPIO Initialization failed: lasciate ogni "
		    "speranza voi che pcie");
		return;
	}

	if (!zen_mpio_init_ubm(fabric)) {
		cmn_err(CE_WARN, "MPIO UBM Initialization failed");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, zen_mpio_send_data, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO Initialization failed: failed to load "
		    "data into mpio");
		return;
	}

	zen_pcie_populate_dbg(fabric, ZPCS_SM_START, ZEN_IODIE_MATCH_ANY);

	if (zen_fabric_walk_iodie(fabric, zen_mpio_init_mapping, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO Initialize mapping failed");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, zen_mpio_more_conf, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO Initialization failed: failed to do yet "
		    "more configuration");
		return;
	}

	cmn_err(CE_CONT, "?MPIO initialization completed successfully\n");

	/*
	 * Now that training is complete, hide all PCIe bridges that do not
	 * have an attached device and are not hotplug capable.
	 */
	zen_fabric_walk_pcie_port(fabric, zen_fabric_pcie_port_op,
	    fops->zfo_pcie_port_hide_bridge);
}

bool
zen_mpio_pcie_port_is_trained(const zen_pcie_port_t *port)
{
	zen_mpio_ict_link_status_t *lp = &port->zpp_ask_port->zma_status;
	return (lp->zmils_state == ZEN_MPIO_LINK_STATE_TRAINED);
}

/*
 * We have been given a zen_pcie_port_t for a port that supports PCIe hotplug.
 * The zen_pcie_port_t contains a pointer to the Oxide-generic OXIO engine data
 * needed to configure PCIe hotplug for the port.  This function translates that
 * into the internal format expected by MPIO.
 *
 * Note that there is some unfortunate duplication in the pre-MPIO, SMU-centric
 * code used for Milan.  Here, the structures sent to MPIO are almost exactly
 * the same as the structures sent to the SMU; the mapping structure is slightly
 * different, function is the same as far as the bits we fill in, and reset is
 * exactly the same.  We should find some better way to combine these to
 * eliminate duplication wherever we can.
 */
static void
zen_mpio_oxio_to_port_hp(const zen_pcie_port_t *port,
    zen_mpio_hotplug_table_t *hp)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	const oxio_engine_t *oxio = port->zpp_oxio;
	const zen_pcie_core_t *core = port->zpp_core;
	uint8_t slot = port->zpp_slotno;
	zen_mpio_hotplug_map_t *map = &hp->zmht_map[slot];
	zen_mpio_hotplug_function_t *func = &hp->zmht_func[slot];
	zen_mpio_hotplug_reset_t *reset = &hp->zmht_reset[slot];
	const oxio_trad_gpio_t *gpio;

	VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0);
	VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) != 0);
	VERIFY0(port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN);

	switch (oxio->oe_hp_type) {
	case OXIO_HOTPLUG_T_EXP_A:
		map->zmhm_format = ZEN_HP_FW_EXPRESS_MODULE_A;
		break;
	case OXIO_HOTPLUG_T_EXP_B:
		map->zmhm_format = ZEN_HP_FW_EXPRESS_MODULE_B;
		break;
	case OXIO_HOTPLUG_T_ENTSSD:
		map->zmhm_format = ZEN_HP_FW_ENTERPRISE_SSD;
		break;
	default:
		panic("cannot map unsupported hotplug type 0x%x on %s",
		    oxio->oe_hp_type, oxio->oe_name);
	}
	map->zmhm_active = 1;

	map->zmhm_apu = 0;
	map->zmhm_die_id = core->zpc_ioms->zio_iodie->zi_soc->zs_num;
	map->zmhm_port_id = port->zpp_portno;
	map->zmhm_tile_id = ops->zfo_tile_fw_hp_id(oxio);
	map->zmhm_bridge = consts->zpc_pcie_core_max_ports * core->zpc_coreno +
	    port->zpp_portno;

	gpio = &oxio->oe_hp_trad.ohp_dev;
	VERIFY3U(gpio->otg_byte, <, 8);
	VERIFY3U(gpio->otg_bit, <, 8);
	func->zmhf_i2c_bit = gpio->otg_bit;
	func->zmhf_i2c_byte = gpio->otg_byte;

	/*
	 * The SMU only accepts a 5-bit address and assumes that the upper two
	 * bits are fixed based upon the device type. The most significant bit
	 * cannot be used. For the various supported PCA devices, the upper two
	 * bits must be 0b01 (7-bit 0x20).
	 */
	VERIFY0(bitx8(gpio->otg_addr, 7, 7));
	VERIFY3U(bitx8(gpio->otg_addr, 6, 5), ==, 1);
	func->zmhf_i2c_daddr = bitx8(gpio->otg_addr, 4, 0);
	func->zmhf_i2c_dtype = oxio_gpio_expander_to_fw(gpio->otg_exp_type);
	func->zmhf_i2c_bus = oxio_switch_to_fw(&gpio->otg_switch);
	func->zmhf_mask = oxio_pcie_cap_to_mask(oxio);

	if ((oxio->oe_hp_flags & OXIO_HP_F_RESET_VALID) == 0) {
		map->zmhm_rst_valid = 0;
		return;
	}

	map->zmhm_rst_valid = 1;
	gpio = &oxio->oe_hp_trad.ohp_reset;
	VERIFY3U(gpio->otg_byte, <, 8);
	VERIFY3U(gpio->otg_bit, <, 8);
	reset->zmhr_i2c_gpio_byte = gpio->otg_byte;
	reset->zmhr_i2c_reset = 1 << gpio->otg_bit;
	VERIFY0(bitx8(gpio->otg_addr, 7, 7));
	VERIFY3U(bitx8(gpio->otg_addr, 6, 5), ==, 1);
	reset->zmhr_i2c_daddr = bitx8(gpio->otg_addr, 4, 0);
	reset->zmhr_i2c_dtype = oxio_gpio_expander_to_fw(gpio->otg_exp_type);
	reset->zmhr_i2c_bus = oxio_switch_to_fw(&gpio->otg_switch);
}

void
zen_mpio_hotplug_port_data_init(zen_pcie_port_t *port, zen_hotplug_table_t *arg)
{
	ASSERT3U(port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG, !=, 0);
	zen_mpio_oxio_to_port_hp(port, (zen_mpio_hotplug_table_t *)arg);
}

bool
zen_mpio_init_hotplug_fw(zen_iodie_t *iodie)
{
	/*
	 * These represent the addresses that we need to program in MPIO.
	 * Strictly speaking, the lower 8-bits represents the addresses that the
	 * firmware seems to expect. The upper byte is a bit more of a mystery;
	 * however, it does correspond to the expected values that AMD roughly
	 * documents for 5-bit bus segment value which is the zmhf_i2c_bus
	 * member of the zen_mpio_hotplug_function_t.
	 */
	const uint32_t i2c_addrs[4] = { 0x70, 0x171, 0x272, 0x373 };

	for (uint_t i = 0; i < ARRAY_SIZE(i2c_addrs); i++) {
		if (!zen_mpio_rpc_set_i2c_switch_addr(iodie, i2c_addrs[i])) {
			return (false);
		}
	}

	return (zen_mpio_send_hotplug_table(iodie,
	    iodie->zi_soc->zs_fabric->zf_hp_pa));
}

bool
zen_mpio_null_set_hotplug_flags(zen_iodie_t *iodie)
{
	return (true);
}
