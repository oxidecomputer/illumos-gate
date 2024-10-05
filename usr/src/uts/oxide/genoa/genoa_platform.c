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
 * Provides the definition of the microarchitecture-specific platform for Genoa.
 *
 * These are operation vectors and the main platform struct that provide common
 * code in the Oxide architecture indirect access to microarchitecture-specific
 * functionality and constants.
 */

#include <sys/sysmacros.h>

#include <sys/amdzen/df.h>
#include <sys/io/zen/platform.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/io/genoa/ccx_impl.h>
#include <sys/io/genoa/mpio.h>
#include <sys/io/genoa/smu.h>
#include <sys/io/zen/mpio.h>
#include <sys/io/zen/apob.h>

/*
 * The genoa_pcie_dbg.c is file is included here so that we have access to the
 * constant initialisers for the core and port debug registers declared within,
 * and can compute their size. The alternative would be to link it into the
 * final object but we would lose the ability to declare all of the platform
 * data as const below.
 */
#include "genoa_pcie_dbg.c"

/*
 * Genoa has up to 12 CCDs per IODIE.
 */
#define	GENOA_MAX_CCDS_PER_IODIE	12

/*
 * Genoa has up to 8 cores per CCX.
 */
#define	GENOA_MAX_CORES_PER_CCX		8

static const zen_apob_ops_t genoa_apob_ops = {
	.zao_reserve_phys = zen_null_apob_reserve_phys,
};

static const zen_ccx_ops_t genoa_ccx_ops = {
	.zco_init = genoa_ccx_init,
};

static const zen_fabric_ops_t genoa_fabric_ops = {
	.zfo_get_dxio_fw_version = zen_mpio_get_fw_version,
	.zfo_report_dxio_fw_version = zen_mpio_report_fw_version,

	.zfo_ioms_init = genoa_fabric_ioms_init,

	.zfo_init_tom = genoa_fabric_init_tom,
	.zfo_disable_vga = genoa_fabric_disable_vga,
	.zfo_iohc_pci_ids = zen_null_fabric_iohc_pci_ids, /* skip for genoa */
	.zfo_pcie_refclk = genoa_fabric_pcie_refclk,
	.zfo_pci_crs_to = genoa_fabric_set_pci_to,
	.zfo_iohc_features = genoa_fabric_iohc_features,
	.zfo_iohc_bus_num = genoa_fabric_iohc_bus_num,
	.zfo_iohc_fch_link = genoa_fabric_iohc_fch_link,
	.zfo_iohc_arbitration = genoa_fabric_iohc_arbitration,
	.zfo_nbif_arbitration = genoa_fabric_nbif_arbitration,
	.zfo_sdp_control = zen_null_fabric_sdp_control, /* skip for genoa */
	.zfo_nbif_syshub_dma = genoa_fabric_nbif_syshub_dma,
	.zfo_ioapic = genoa_fabric_ioapic,
	.zfo_pcie = genoa_fabric_pcie,

	.zfo_pcie_core_reg = genoa_pcie_core_reg,
	.zfo_pcie_port_reg = genoa_pcie_port_reg,
};

static const zen_hack_ops_t genoa_hack_ops = {
	.zho_check_furtive_reset = zen_null_check_furtive_reset,
	.zho_cgpll_set_ssc = zen_null_cgpll_set_ssc,
};

static const zen_ras_ops_t genoa_ras_ops = {
};

const zen_platform_t genoa_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4,
		.zpc_max_cfgmap = DF_MAX_CFGMAP,
		.zpc_max_iorr = DF_MAX_IO_RULES,
		.zpc_max_mmiorr = DF_MAX_MMIO_RULES,
		.zpc_ccds_per_iodie = GENOA_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = GENOA_MAX_CORES_PER_CCX,
		.zpc_smu_smn_addrs = {
			.zssa_req = D_GENOA_SMU_RPC_REQ,
			.zssa_resp = D_GENOA_SMU_RPC_RESP,
			.zssa_arg0 = D_GENOA_SMU_RPC_ARG0,
			.zssa_arg1 = D_GENOA_SMU_RPC_ARG1,
			.zssa_arg2 = D_GENOA_SMU_RPC_ARG2,
			.zssa_arg3 = D_GENOA_SMU_RPC_ARG3,
			.zssa_arg4 = D_GENOA_SMU_RPC_ARG4,
			.zssa_arg5 = D_GENOA_SMU_RPC_ARG5,
		},
		.zpc_mpio_smn_addrs = {
			.zmsa_arg0 = D_GENOA_MPIO_RPC_ARG0,
			.zmsa_arg1 = D_GENOA_MPIO_RPC_ARG1,
			.zmsa_arg2 = D_GENOA_MPIO_RPC_ARG2,
			.zmsa_arg3 = D_GENOA_MPIO_RPC_ARG3,
			.zmsa_arg4 = D_GENOA_MPIO_RPC_ARG4,
			.zmsa_arg5 = D_GENOA_MPIO_RPC_ARG5,
			.zmsa_resp = D_GENOA_MPIO_RPC_RESP,
			.zmsa_doorbell = D_GENOA_MPIO_RPC_DOORBELL,
		},
		.zpc_nnbif = GENOA_NBIO_MAX_NBIF,
		.zpc_nbif_nfunc = genoa_nbif_nfunc,
		.zpc_nbif_data = genoa_nbif_data,
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = genoa_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(genoa_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = genoa_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(genoa_pcie_port_dbg_regs),
#endif
	},
	.zp_apob_ops = &genoa_apob_ops,
	.zp_ccx_ops = &genoa_ccx_ops,
	.zp_fabric_ops = &genoa_fabric_ops,
	.zp_hack_ops = &genoa_hack_ops,
	.zp_ras_ops = &genoa_ras_ops,
};
