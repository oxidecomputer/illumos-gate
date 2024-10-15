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
#include <sys/io/genoa/hacks.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/io/genoa/ccx_impl.h>
#include <sys/io/genoa/mpio.h>
#include <sys/io/genoa/smu.h>
#include <sys/io/zen/mpio.h>
#include <sys/io/zen/ras.h>

/*
 * The genoa_pcie_dbg.c is file is included here so that we have access to the
 * constant initialisers for the core and port debug registers declared within,
 * and can compute their size. The alternative would be to link it into the
 * final object but we would lose the ability to declare all of the platform
 * data as const below.
 */
#include "genoa_pcie_dbg.c"

/*
 * Genoa has up to 12 CCDs per I/O die.
 */
#define	GENOA_MAX_CCDS_PER_IODIE	12

/*
 * Genoa has up to 8 cores per CCX.
 */
#define	GENOA_MAX_CORES_PER_CCX		8

/*
 * The APOB may provide up to 49 holes in the memory map on Genoa.
 */
#define	GENOA_MAX_APOB_MEM_MAP_HOLES	49


static const zen_ccx_ops_t genoa_ccx_ops = {
	.zco_get_dpm_weights = genoa_fabric_thread_get_dpm_weights,

	.zco_thread_feature_init = genoa_thread_feature_init,
	.zco_thread_uc_init = genoa_thread_uc_init,
	.zco_core_ls_init = genoa_core_ls_init,
	.zco_core_ic_init = genoa_core_ic_init,
	.zco_core_dc_init = genoa_core_dc_init,
	.zco_core_tw_init = zen_ccx_init_noop,
	.zco_core_de_init = zen_ccx_init_noop,
	.zco_core_fp_init = genoa_core_fp_init,
	.zco_core_l2_init = genoa_core_l2_init,
	.zco_ccx_l3_init = genoa_ccx_l3_init,
	.zco_core_undoc_init = genoa_core_undoc_init,
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
	.zfo_nbif_dev_straps = genoa_fabric_nbif_dev_straps,
	.zfo_nbif_bridges = zen_null_fabric_nbif_bridges, /* skip for genoa */
	.zfo_pcie = genoa_fabric_pcie,
	.zfo_pcie_port_unhide_bridges = genoa_fabric_unhide_bridges,
	.zfo_init_pcie_straps = genoa_fabric_init_pcie_straps,
	.zfo_init_smn_port_state = genoa_fabric_init_smn_port_state,
	.zfo_init_pcie_core = genoa_fabric_init_pcie_core,
	.zfo_init_bridges = genoa_fabric_init_bridges,

	.zfo_iohc_enable_nmi = genoa_iohc_enable_nmi,
	.zfo_iohc_nmi_eoi = genoa_iohc_nmi_eoi,

	.zfo_ioms_n_pcie_cores = genoa_ioms_n_pcie_cores,
	.zfo_pcie_core_n_ports = genoa_pcie_core_n_ports,
	.zfo_pcie_core_info = genoa_pcie_core_info,
	.zfo_pcie_port_info = genoa_pcie_port_info,
	.zfo_pcie_core_reg = genoa_pcie_core_reg,
	.zfo_pcie_port_reg = genoa_pcie_port_reg,
};

static const zen_hack_ops_t genoa_hack_ops = {
	.zho_check_furtive_reset = zen_null_check_furtive_reset,
	.zho_cgpll_set_ssc = zen_null_cgpll_set_ssc,
	.zho_hack_gpio = genoa_hack_gpio,
};

static const zen_ras_ops_t genoa_ras_ops = {
	.zro_ras_init = zen_null_ras_init,
};

const zen_platform_t genoa_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4,
		.zpc_chiprev = X86_CHIPREV_AMD_GENOA_A0 |
		    X86_CHIPREV_AMD_GENOA_A1 | X86_CHIPREV_AMD_GENOA_AB |
		    X86_CHIPREV_AMD_GENOA_B0 | X86_CHIPREV_AMD_GENOA_B1 |
		    X86_CHIPREV_AMD_GENOA_B2,
		.zpc_max_apob_mem_map_holes = GENOA_MAX_APOB_MEM_MAP_HOLES,
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
		.zpc_pcie_core0_unitid = GENOA_PCIE_CORE0_UNITID,
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = genoa_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(genoa_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = genoa_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(genoa_pcie_port_dbg_regs),
#endif
	},
	.zp_ccx_ops = &genoa_ccx_ops,
	.zp_fabric_ops = &genoa_fabric_ops,
	.zp_hack_ops = &genoa_hack_ops,
	.zp_ras_ops = &genoa_ras_ops,
};
