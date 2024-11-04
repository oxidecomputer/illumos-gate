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
#include <sys/platform_detect.h>

#include <sys/io/zen/hacks.h>
#include <sys/io/zen/mpio_impl.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>

/*
 * These come from common code in the DDI.  They really ought to be in a header.
 */
extern void *contig_alloc(size_t, ddi_dma_attr_t *, uintptr_t, int);
extern void contig_free(void *, size_t);

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
#define	RPC_READY_MAX_SPIN	(1U << 24)

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
	ASSERT3U(sizeof (rpc.zmr_args), ==, sizeof (*status));
	bcopy(rpc.zmr_args, status, sizeof (*status));

	return (true);
}

static bool
zen_mpio_wait_ready(zen_iodie_t *iodie)
{
	const uint32_t RPC_MAX_WAIT_READY = 1U << 30;
	zen_mpio_status_t status = { 0 };

	for (int k = 0; k < RPC_MAX_WAIT_READY; k++) {
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

static bool
zen_mpio_rpc_enumerate_i2c(zen_iodie_t *iodie)
{
	zen_mpio_config_t *conf;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	ASSERT3P(iodie, !=, NULL);
	conf = &iodie->zi_mpio_conf;
	ASSERT(conf->zmc_ubm_hfc_ports != NULL);
	ASSERT3U(conf->zmc_ubm_hfc_ports_pa, !=, 0);
	ASSERT3U(conf->zmc_ubm_hfc_ports_pa, <, 0xFFFFFFFFU);

	/*
	 * Sadly, this RPC can only accept 32-bits worth of a
	 * physical address.  Thus, the data is artificially
	 * constrained to be in the first 4GiB of address space.
	 */
	rpc.zmr_args[0] = (uint32_t)conf->zmc_ubm_hfc_ports_pa;
	rpc.zmr_args[1] = conf->zmc_ubm_hfc_nports;
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
zen_mpio_rpc_get_i2c_device(zen_iodie_t *iodie, uint32_t hfc, uint32_t dfc,
    zen_mpio_ubm_dfc_descr_t *descr)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_args[0] = hfc;
	rpc.zmr_args[1] = dfc;
	rpc.zmr_req = ZEN_MPIO_OP_GET_I2C_DEV;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK && (rpc.zmr_resp & 0xFF) != 0) {
		return (false);
	}
	bcopy(&rpc.zmr_args[1], descr, sizeof (*descr));

	return (true);
}

bool
zen_mpio_rpc_set_i2c_switch_addr(zen_iodie_t *iodie, uint32_t addr)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

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
 * Do MPIO global configuration initialization.  Unlike earlier
 * systems that did this via DXIO and discrete RPCs, MPIO takes
 * a single global configuration parameter in an RPC.
 */
static bool
zen_mpio_init_global_config(zen_iodie_t *iodie)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_global_config_t *args;
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_SET_GLOBAL_CONFIG;
	args = (zen_mpio_global_config_t *)rpc.zmr_args;
	args->zmgc_skip_vet = 1;
	args->zmgc_save_restore_mode = 0;
	args->zmgc_use_phy_sram = 1;
	args->zmgc_valid_phy_firmware = 1;
	args->zmgc_en_pcie_noncomp_wa = 1;
	args->zmgc_pwr_mgmt_clk_gating = 1;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO set global config RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

static void
zen_mpio_ubm_hfc_init(zen_iodie_t *iodie, int i)
{
	zen_mpio_config_t *conf = &iodie->zi_mpio_conf;
	zen_mpio_ask_port_t *ask;
	zen_mpio_ubm_extra_t *extra;
	zen_mpio_ubm_dfc_descr_t d;
	zen_mpio_ubm_hfc_port_t *hfc_port;
	int j, ndfc;

	hfc_port = &conf->zmc_ubm_hfc_ports[i];
	for (j = 0, ndfc = 1; j < ndfc; j++) {
		if (conf->zmc_nports >= ZEN_MPIO_ASK_MAX_PORTS)
			return;
		if (!zen_mpio_rpc_get_i2c_device(iodie, i, j, &d)) {
			return;
		}
		if (j == 0)
			ndfc = d.zmudd_ndfcs;
		if (ndfc == 0)
			break;
		extra = &conf->zmc_ubm_extra[conf->zmc_ubm_extra_len++];
		extra->zmue_ubm = true;
		extra->zmue_npem_cap = 0xFFF;  /* XXX */
		extra->zmue_npem_en = 1;

		ask = &conf->zmc_ask->zma_ports[conf->zmc_nports++];
		ask->zma_link.zml_lane_start =
		    hfc_port->zmuhp_start_lane + d.zmudd_lane_start;
		ask->zma_link.zml_num_lanes = d.zmudd_lane_width;
		ask->zma_link.zml_gpio_id = 1;

		ask->zma_link.zml_attrs.zmla_link_hp_type =
		    ZEN_MPIO_HOTPLUG_T_UBM;
		ask->zma_link.zml_attrs.zmla_hfc_idx = i;
		ask->zma_link.zml_attrs.zmla_dfc_idx = j;
		ask->zma_link.zml_attrs.zmla_port_present = 1;

		switch (d.zmudd_data.zmudt_type) {
		case ZEN_MPIO_UBM_DFC_TYPE_QUAD_PCI:
			ask->zma_link.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE;
			extra->zmue_slot = d.zmudd_data.zmudt_slot;
			break;
		case ZEN_MPIO_UBM_DFC_TYPE_SATA_SAS:
			ask->zma_link.zml_ctlr_type = ZEN_MPIO_ASK_LINK_SATA;
			break;
		case ZEN_MPIO_UBM_DFC_TYPE_EMPTY:
			ask->zma_link.zml_attrs.zmla_port_present = 0;
			break;
		}

		cmn_err(CE_WARN, "dfc(%u,%u):", i, j);
		cmn_err(CE_CONT, " zmudd_hfcno = %u\n", d.zmudd_hfcno);
		cmn_err(CE_CONT, " zmudd_ndfcs = %hu\n", d.zmudd_ndfcs);
		cmn_err(CE_CONT, " zmudd_lane_start = %02x\n",
		    ask->zma_link.zml_lane_start);
		cmn_err(CE_CONT, " zmudd_lane_width = %02x\n",
		    d.zmudd_lane_width);
		cmn_err(CE_CONT, " zmudt_gen_speed = %02x\n",
		    d.zmudd_data.zmudt_gen_speed);
		cmn_err(CE_CONT, " zmudt_type = %02x\n",
		    d.zmudd_data.zmudt_type);
		cmn_err(CE_CONT, " zmudt_bifurcate_port = %x\n",
		    d.zmudd_data.zmudt_bifurcate_port);
		cmn_err(CE_CONT, " zmudt_zecondary_port = %x\n",
		    d.zmudd_data.zmudt_secondary_port);
		cmn_err(CE_CONT, " zmudt_ref_clk = %02x\n",
		    d.zmudd_data.zmudt_ref_clk);
		cmn_err(CE_CONT, " zmudt_pwr_dis = %02x\n",
		    d.zmudd_data.zmudt_pwr_dis);
		cmn_err(CE_CONT, " zmudt_has_perst = %x\n",
		    d.zmudd_data.zmudt_has_perst);
		cmn_err(CE_CONT, " zmudt_dual_port = %02x\n",
		    d.zmudd_data.zmudt_dual_port);
		cmn_err(CE_CONT, " zmudt_slot = %02x\n",
		    d.zmudd_data.zmudt_slot);
	}
}

static int
zen_mpio_init_ubm(zen_iodie_t *iodie, void *arg)
{
	zen_mpio_config_t *conf;

	if (!zen_mpio_rpc_enumerate_i2c(iodie)) {
		return (1);
	}
	conf = &iodie->zi_mpio_conf;
	for (int i = 0; i < conf->zmc_ubm_hfc_nports; i++) {
		zen_mpio_ubm_hfc_init(iodie, i);
	}

	return (0);
}

static bool
zen_mpio_send_ext_attrs(zen_iodie_t *iodie, void *arg)
{
	zen_mpio_config_t *conf;
	zen_mpio_xfer_ext_attrs_args_t *args;
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
	args->zmxeaa_nwords = conf->zmc_ext_attrs_len / 4;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO transfer ext attrs RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
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
	zen_mpio_rpc_res_t res;

	ASSERT3P(iodie, !=, NULL);
	conf = &iodie->zi_mpio_conf;
	ASSERT3P(conf, !=, NULL);
	ASSERT3P(conf->zmc_ask, !=, NULL);
	ASSERT3U(conf->zmc_ask_pa, !=, 0);

	cmn_err(CE_WARN, "send ask");
	if (!zen_mpio_wait_ready(iodie)) {
		cmn_err(CE_WARN, "MPIO wait for ready to send ASK failed");
		return (false);
	}

	rpc.zmr_req = ZEN_MPIO_OP_XFER_ASK;
	args = (zen_mpio_xfer_ask_args_t *)rpc.zmr_args;
	args->zmxaa_paddr_hi = conf->zmc_ask_pa >> 32;
	args->zmxaa_paddr_lo = conf->zmc_ask_pa & 0xFFFFFFFFU;
	args->zmxaa_link_count = conf->zmc_nports;
	args->zmxaa_links = ZEN_MPIO_LINK_SELECTED;
	args->zmxaa_dir = ZEN_MPIO_XFER_FROM_RAM;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN,
		    "MPIO transfer ASK RPC Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}
	if (rpc.zmr_args[0] != 1) {
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

	rpc.zmr_req = ZEN_MPIO_OP_POSTED | ZEN_MPIO_OP_SETUP_LINK;
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

	rpc.zmr_req = ZEN_MPIO_OP_POSTED | ZEN_MPIO_OP_SETUP_LINK;
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

	rpc.zmr_req = ZEN_MPIO_OP_POSTED | ZEN_MPIO_OP_SETUP_LINK;
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
zen_mpio_setup_link_train_enumerate(zen_iodie_t *iodie, bool early)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_link_setup_args_t *args;
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_POSTED | ZEN_MPIO_OP_SETUP_LINK;
	args = (zen_mpio_link_setup_args_t *)rpc.zmr_args;
	args->zmlsa_training = 1;
	args->zmlsa_enumerate = 1;
	args->zmlsa_early = early;
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

bool
zen_mpio_send_hotplug_table(zen_iodie_t *iodie, uint64_t paddr)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_SET_HP_CFG_TBL;
	rpc.zmr_args[0] = bitx64(paddr, 31, 0);
	rpc.zmr_args[1] = bitx64(paddr, 63, 32);
	rpc.zmr_args[2] = sizeof(zen_mpio_hotplug_table_t);
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
zen_mpio_rpc_start_hotplug(zen_iodie_t *iodie, bool one_based, uint32_t flags)
{
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_HOTPLUG_EN;
	if (one_based) {
		rpc.zmr_args[0] = 1;
	}
	rpc.zmr_args[0] |= flags;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "MPIO Start Hotplug Failed: %s (MPIO: 0x%x)",
		    zen_mpio_rpc_res_str(res), rpc.zmr_resp);
		return (false);
	}

	return (true);
}

bool
zen_mpio_write_pcie_strap(zen_pcie_core_t *pc,
    const uint32_t reg, const uint32_t data)
{
	zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;
	zen_mpio_rpc_t rpc = { 0 };
	zen_mpio_rpc_res_t res;

	rpc.zmr_req = ZEN_MPIO_OP_PCIE_WRITE_STRAP;
	rpc.zmr_args[0] = reg;
	rpc.zmr_args[1] = data;
	res = zen_mpio_rpc(iodie, &rpc);
	if (res != ZEN_MPIO_RPC_OK) {
		cmn_err(CE_WARN, "writing strap (reg 0x%x data 0x%x) failed: "
		    " %s (MPIO 0x%x)", reg, data, zen_mpio_rpc_res_str(res),
		    rpc.zmr_resp);
		return (false);
	}

	return (true);
}

/*
 * Here we need to assemble data for the system we're actually on.
 */
static int
zen_mpio_init_data(zen_iodie_t *iodie, void *arg)
{
	ddi_dma_attr_t attr;
	size_t size;
	pfn_t pfn;
	zen_mpio_config_t *conf = &iodie->zi_mpio_conf;
	const zen_mpio_port_conf_t *source_data = NULL;
	const zen_mpio_ubm_hfc_port_t *source_ubm_data = NULL;
	zen_mpio_ask_port_t *ask;
	int i;

	if (!oxide_board_is_ruby())
		panic("Currently only runs on Ruby.");
	if (iodie->zi_soc->zs_num != 0)
		return (0);

	source_data = ruby_mpio_pcie_s0;
	source_ubm_data = ruby_mpio_hfc_ports;
	conf->zmc_ubm_hfc_nports = RUBY_MPIO_UBM_HFC_DESCR_NPORTS;

	conf->zmc_nports = RUBY_MPIO_PCIE_S0_LEN;
	if (conf->zmc_nports > ZEN_MPIO_ASK_MAX_PORTS)
		conf->zmc_nports = ZEN_MPIO_ASK_MAX_PORTS;

	size = sizeof (zen_mpio_port_conf_t) * conf->zmc_nports;
	conf->zmc_port_conf = kmem_zalloc(size, KM_SLEEP);
	VERIFY3P(source_data, !=, NULL);
	bcopy(source_data, conf->zmc_port_conf, size);

	size = conf->zmc_nports * sizeof (zen_mpio_ask_port_t);
	VERIFY3U(size, <=, MMU_PAGESIZE);

	zen_fabric_dma_attr(&attr);

	conf->zmc_ubm_extra =
	    kmem_zalloc(ZEN_MPIO_ASK_MAX_PORTS * sizeof (zen_mpio_ubm_extra_t),
	    KM_SLEEP);
	conf->zmc_ubm_extra_len = 0;

	conf->zmc_ask_alloc_len = MMU_PAGESIZE;
	conf->zmc_ask = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->zmc_ask, MMU_PAGESIZE);
	ask = conf->zmc_ask->zma_ports;
	for (i = 0; i < conf->zmc_nports; i++)
		bcopy(&source_data[i].zmpc_ask, ask + i, sizeof (*ask));

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zmc_ask);
	conf->zmc_ask_pa = mmu_ptob((uint64_t)pfn);

	conf->zmc_ext_attrs_alloc_len = MMU_PAGESIZE;
	conf->zmc_ext_attrs =
	    contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->zmc_ext_attrs, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zmc_ext_attrs);
	conf->zmc_ext_attrs_pa = mmu_ptob((uint64_t)pfn);

	conf->zmc_ubm_hfc_ports_alloc_len = MMU_PAGESIZE;
	conf->zmc_ubm_hfc_ports =
	    contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->zmc_ubm_hfc_ports, MMU_PAGESIZE);

	VERIFY3P(source_ubm_data, !=, NULL);
	size = sizeof (zen_mpio_ubm_hfc_port_t) * conf->zmc_ubm_hfc_nports;
	bcopy(source_ubm_data, conf->zmc_ubm_hfc_ports, size);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->zmc_ubm_hfc_ports);
	conf->zmc_ubm_hfc_ports_pa = mmu_ptob((uint64_t)pfn);

	return (0);
}

/*
 * Given all of the engines on an I/O die, try and map each one to a
 * corresponding IOMS and bridge. We only care about an engine if it is a PCIe
 * engine. Note, because each I/O die is processed independently, this only
 * operates on a single I/O die.
 */
static bool
zen_mpio_map_engines(zen_fabric_t *fabric, zen_iodie_t *iodie)
{
	bool ret = true;
	zen_mpio_config_t *conf = &iodie->zi_mpio_conf;

	for (uint_t i = 0; i < conf->zmc_nports; i++) {
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
		port->zpp_ubm_extra = &conf->zmc_ubm_extra[i];
		pc->zpc_flags |= ZEN_PCIE_CORE_F_USED;
		if (lp->zml_attrs.zmla_link_hp_type !=
		    ZEN_MPIO_HOTPLUG_T_DISABLED) {
			pc->zpc_flags |= ZEN_PCIE_CORE_F_HAS_HOTPLUG;
		}
	}

	return (ret);
}

static int
zen_mpio_init_mapping(zen_iodie_t *iodie, void *arg)
{
	if (!zen_mpio_setup_link_post_map(iodie) || !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO reconcile map failed");
		return (1);
	}

	if (!zen_mpio_map_engines(iodie->zi_soc->zs_fabric, iodie)) {
		cmn_err(CE_WARN, "Socket %u failed to map all DXIO engines "
		    "to devices.  PCIe will not function",
		    iodie->zi_soc->zs_num);
		return (1);
	}

	return (0);
}

static int
zen_mpio_pcie_core_op(zen_pcie_core_t *port, void *arg)
{
	void (*callback)(zen_pcie_core_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(port);

	return (0);
}

static int
zen_mpio_iodie_op(zen_iodie_t *iodie, void *arg)
{
	void (*callback)(zen_iodie_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(iodie);

	return (0);
}

static int
zen_mpio_pcie_port_op(zen_pcie_port_t *port, void *arg)
{
	void (*callback)(zen_pcie_port_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(port);

	return (0);
}

static int
zen_mpio_more_conf(zen_iodie_t *iodie, void *arg __unused)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();

	(void) zen_fabric_walk_pcie_core(iodie->zi_soc->zs_fabric,
	    zen_mpio_pcie_core_op, fops->zfo_init_pcie_straps);
	cmn_err(CE_CONT, "?Socket %u MPIO: Wrote PCIe straps\n",
	    iodie->zi_soc->zs_num);

	(void) zen_fabric_walk_pcie_port(iodie->zi_soc->zs_fabric,
	    zen_mpio_pcie_port_op, fops->zfo_init_smn_port_state);
	cmn_err(CE_CONT, "?Socket %u MPIO: Wrote PCIe SMN registers\n",
	    iodie->zi_soc->zs_num);

	if (!zen_mpio_setup_link_post_config_reconfig(iodie) ||
	    !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO config/reconfig failed");
		return (1);
	}

	if (!zen_mpio_setup_link_post_perst_req(iodie) ||
	    !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO PERST request failed");
		return (1);
	}

	/* XXX: Ruby only? */
	if (oxide_board_is_ruby() &&
	    iodie->zi_node_id == 0) {
		zen_hack_gpio(ZHGOP_SET, 26);
		zen_hack_gpio(ZHGOP_SET, 266);
	}

	if (!zen_mpio_setup_link_train_enumerate(iodie, false) ||
	    !zen_mpio_recv_ask(iodie)) {
		cmn_err(CE_WARN, "MPIO train and enumerate request failed");
		return (1);
	}
#if 0
	/*
	 * This is set to 1 by default because we want 'latency behaviour' not
	 * 'improved latency'.
	 */
	if (!genoa_mpio_rpc_misc_rt_conf(iodie,
	    GENOA_MPIO_RT_SET_CONF_TX_FIFO_MODE, 1)) {
		return (1);
	}
#endif

	return (0);
}

/*
 * MPIO-level PCIe initialization: training links and mapping bridges and so on.
 */
void
zen_mpio_pcie_init(zen_fabric_t *fabric)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	const zen_hack_ops_t *hops = oxide_zen_hack_ops();

	zen_pcie_populate_dbg(fabric, ZPCS_PRE_DXIO_INIT, ZEN_IODIE_MATCH_ANY);

	if (zen_fabric_walk_iodie(fabric, zen_mpio_init_data, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO ASK Initialization failed");
		return;
	}

	zen_fabric_walk_pcie_port(fabric, zen_mpio_pcie_port_op,
	    fops->zfo_pcie_port_unhide_bridges);

	if (zen_fabric_walk_iodie(fabric, zen_mpio_iodie_op,
	    zen_mpio_init_global_config) != 0) {
		cmn_err(CE_WARN, "MPIO Initialization failed: lasciate ogni "
		    "speranza voi che pcie");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, zen_mpio_init_ubm, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO UBM enumeration failed");
		return;
	}

	if (zen_fabric_walk_iodie(fabric, zen_mpio_send_data, NULL) != 0) {
		cmn_err(CE_WARN, "MPIO Initialization failed: failed to load "
		    "data into mpio");
		return;
	}

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
	 * Now that training is complete, re-hide all bridges; we will
	 * selectively expose, given training and hotplug.
	 */
	if (fops->zfo_pcie_port_hide_bridges != NULL) {
		zen_fabric_walk_pcie_port(fabric, zen_mpio_pcie_port_op,
		    fops->zfo_pcie_port_hide_bridges);
	}

	/*
	 * Now that we have successfully trained devices, it's time to go
	 * through and set up the bridges so that way we can actual handle them
	 * aborting transactions and related.
	 */
	zen_fabric_walk_pcie_core(fabric, zen_mpio_pcie_core_op,
	    fops->zfo_init_pcie_core);
	zen_fabric_walk_pcie_port(fabric, zen_mpio_pcie_port_op,
	    fops->zfo_init_bridges);

	/*
	 * XXX This is a terrible hack. We should really fix pci_boot.c and we
	 * better before we go to market.
	 */
	if (hops->zho_fabric_hack_bridges != NULL)
		hops->zho_fabric_hack_bridges(fabric);

	/*
	 * At this point, go talk to the SMU to actually initialize our hotplug
	 * support.
	 */
	zen_pcie_populate_dbg(fabric, ZPCS_PRE_HOTPLUG, ZEN_IODIE_MATCH_ANY);
#if 0
	if (!zen_hotplug_init(fabric)) {
		cmn_err(CE_WARN, "SMUHP: initialization failed; PCIe hotplug "
		    "may not function properly");
	}
#endif
	zen_pcie_populate_dbg(fabric, ZPCS_POST_HOTPLUG, ZEN_IODIE_MATCH_ANY);
}
