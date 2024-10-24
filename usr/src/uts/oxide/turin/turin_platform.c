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
 * Provides the definition of the microarchitecture-specific platform for Turin
 * and dense Turin.
 *
 * These are operation vectors and the main platform struct that provide common
 * code in the Oxide architecture indirect access to microarchitecture-specific
 * functionality and constants.
 */

#include <sys/sysmacros.h>

#include <sys/amdzen/df.h>
#include <sys/io/zen/platform.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/turin/fabric_impl.h>
#include <sys/io/turin/hacks.h>
#include <sys/io/turin/pcie_impl.h>
#include <sys/io/turin/ccx_impl.h>
#include <sys/io/turin/mpio.h>
#include <sys/io/turin/smu_impl.h>
#include <sys/io/zen/mpio.h>
#include <sys/io/zen/ras.h>

/*
 * The turin_pcie_dbg.c is file is included here so that we have access to the
 * constant initialisers for the core and port debug registers declared within,
 * and can compute their size. The alternative would be to link it into the
 * final object but we would lose the ability to declare all of the platform
 * data as const below.
 */
#include "turin_pcie_dbg.c"

/*
 * Classic Turin has up to 16 CCDs per I/O die, whereas
 * Dense Turin only has up to 12 CCDs per I/O die.
 */
#define	CLASSIC_TURIN_MAX_CCDS_PER_IODIE	16
#define	DENSE_TURIN_MAX_CCDS_PER_IODIE		12

/*
 * Classic Turin has up to 8 cores per CCX. whereas
 * Dense Turin has up to 16 cores per CCX.
 */
#define	CLASSIC_TURIN_MAX_CORES_PER_CCX		8
#define	DENSE_TURIN_MAX_CORES_PER_CCX		16

/*
 * The APOB may provide up to 51 holes in the memory map on both Classic and
 * Dense Turin.
 */
#define	TURIN_MAX_APOB_MEM_MAP_HOLES	51


static const zen_ccx_ops_t turin_ccx_ops = {
	.zco_physmem_init = turin_ccx_physmem_init,

	/*
	 * Turin doesn't read DPM weights from the SMU nor do we need to
	 * explicitly zero them as we do for Genoa.
	 */
	.zco_get_dpm_weights = zen_fabric_thread_get_dpm_weights_noop,

	.zco_thread_feature_init = turin_thread_feature_init,
	.zco_thread_uc_init = turin_thread_uc_init,
	.zco_core_ls_init = turin_core_ls_init,
	.zco_core_ic_init = turin_core_ic_init,
	.zco_core_dc_init = turin_core_dc_init,
	.zco_core_tw_init = turin_core_tw_init,
	.zco_core_de_init = zen_ccx_init_noop,
	.zco_core_fp_init = zen_ccx_init_noop,
	.zco_core_l2_init = turin_core_l2_init,
	.zco_ccx_l3_init = zen_ccx_init_noop,
	.zco_core_undoc_init = turin_core_undoc_init,
};

static const zen_fabric_ops_t turin_fabric_ops = {
	.zfo_get_dxio_fw_version = zen_mpio_get_fw_version,
	.zfo_report_dxio_fw_version = zen_mpio_report_fw_version,

	.zfo_ioms_init = turin_fabric_ioms_init,

	.zfo_init_tom = turin_fabric_init_tom,
	.zfo_disable_vga = turin_fabric_disable_vga,
	.zfo_iohc_pci_ids = zen_null_fabric_iohc_pci_ids, /* skip for turin */
	.zfo_pcie_refclk = turin_fabric_pcie_refclk,
	.zfo_pci_crs_to = turin_fabric_set_pci_to,
	.zfo_iohc_features = turin_fabric_iohc_features,
	.zfo_iohc_bus_num = turin_fabric_iohc_bus_num,
	.zfo_iohc_fch_link = turin_fabric_iohc_fch_link,
	.zfo_iohc_arbitration = turin_fabric_iohc_arbitration,
	.zfo_nbif_arbitration = turin_fabric_nbif_arbitration,
	.zfo_sdp_control = zen_null_fabric_sdp_control, /* skip for turin */
	.zfo_nbif_syshub_dma = turin_fabric_nbif_syshub_dma,
	.zfo_ioapic = turin_fabric_ioapic,
	.zfo_nbif_dev_straps = turin_fabric_nbif_dev_straps,
	.zfo_nbif_bridges = zen_null_fabric_nbif_bridges, /* skip for turin */
	.zfo_pcie = turin_fabric_pcie,
	.zfo_pcie_port_unhide_bridges = turin_fabric_unhide_bridges,
	.zfo_pcie_port_hide_bridges = turin_fabric_hide_bridges,
	.zfo_init_pcie_straps = turin_fabric_init_pcie_straps,
	.zfo_init_smn_port_state = turin_fabric_init_smn_port_state,
	.zfo_init_pcie_core = turin_fabric_init_pcie_core,
	.zfo_init_bridges = turin_fabric_init_bridges,

	.zfo_iohc_enable_nmi = turin_iohc_enable_nmi,
	.zfo_iohc_nmi_eoi = turin_iohc_nmi_eoi,

	.zfo_ioms_n_pcie_cores = turin_ioms_n_pcie_cores,
	.zfo_pcie_core_n_ports = turin_pcie_core_n_ports,
	.zfo_pcie_core_info = turin_pcie_core_info,
	.zfo_pcie_port_info = turin_pcie_port_info,
	.zfo_pcie_core_reg = turin_pcie_core_reg,
	.zfo_pcie_port_reg = turin_pcie_port_reg,
};

static const zen_hack_ops_t turin_hack_ops = {
	.zho_check_furtive_reset = zen_null_check_furtive_reset,
	.zho_cgpll_set_ssc = zen_null_cgpll_set_ssc,
	.zho_hack_gpio = turin_hack_gpio,
	.zho_fabric_hack_bridges = turin_fabric_hack_bridges,
};

static const zen_ras_ops_t turin_ras_ops = {
	.zro_ras_init = zen_null_ras_init,
};

static const zen_smu_ops_t turin_smu_ops = {
	.zsmuo_early_features_init = turin_smu_early_features_init,
	.zsmuo_features_init = turin_smu_features_init,
};

const zen_platform_t turin_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4D2,
		.zpc_chiprev = X86_CHIPREV_AMD_TURIN_A0 |
		    X86_CHIPREV_AMD_TURIN_B0 | X86_CHIPREV_AMD_TURIN_B1 |
		    X86_CHIPREV_AMD_TURIN_C0 | X86_CHIPREV_AMD_TURIN_C1,
		.zpc_max_apob_mem_map_holes = TURIN_MAX_APOB_MEM_MAP_HOLES,
		.zpc_max_cfgmap = DF_MAX_CFGMAP_TURIN,
		.zpc_max_iorr = DF_MAX_IO_RULES_TURIN,
		.zpc_max_mmiorr = DF_MAX_MMIO_RULES_TURIN,
		.zpc_ccds_per_iodie = CLASSIC_TURIN_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = CLASSIC_TURIN_MAX_CORES_PER_CCX,
		.zpc_smu_smn_addrs = {
			.zssa_req = D_TURIN_SMU_RPC_REQ,
			.zssa_resp = D_TURIN_SMU_RPC_RESP,
			.zssa_arg0 = D_TURIN_SMU_RPC_ARG0,
			.zssa_arg1 = D_TURIN_SMU_RPC_ARG1,
			.zssa_arg2 = D_TURIN_SMU_RPC_ARG2,
			.zssa_arg3 = D_TURIN_SMU_RPC_ARG3,
			.zssa_arg4 = D_TURIN_SMU_RPC_ARG4,
			.zssa_arg5 = D_TURIN_SMU_RPC_ARG5,
		},
		.zpc_mpio_smn_addrs = {
			.zmsa_arg0 = D_TURIN_MPIO_RPC_ARG0,
			.zmsa_arg1 = D_TURIN_MPIO_RPC_ARG1,
			.zmsa_arg2 = D_TURIN_MPIO_RPC_ARG2,
			.zmsa_arg3 = D_TURIN_MPIO_RPC_ARG3,
			.zmsa_arg4 = D_TURIN_MPIO_RPC_ARG4,
			.zmsa_arg5 = D_TURIN_MPIO_RPC_ARG5,
			.zmsa_resp = D_TURIN_MPIO_RPC_RESP,
			.zmsa_doorbell = D_TURIN_MPIO_RPC_DOORBELL,
		},
		.zpc_nnbif = TURIN_NBIO_MAX_NBIF,
		.zpc_nbif_nfunc = turin_nbif_nfunc,
		.zpc_nbif_data = turin_nbif_data,
		.zpc_pcie_core0_unitid = TURIN_PCIE_CORE0_UNITID,
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = turin_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(turin_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = turin_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(turin_pcie_port_dbg_regs),
#endif
	},
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
	.zp_smu_ops = &turin_smu_ops,
};

const zen_platform_t dense_turin_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4D2,
		.zpc_chiprev = X86_CHIPREV_AMD_DENSE_TURIN_A0 |
		    X86_CHIPREV_AMD_DENSE_TURIN_B0 |
		    X86_CHIPREV_AMD_DENSE_TURIN_B1,
		.zpc_max_apob_mem_map_holes = TURIN_MAX_APOB_MEM_MAP_HOLES,
		.zpc_max_cfgmap = DF_MAX_CFGMAP_TURIN,
		.zpc_max_iorr = DF_MAX_IO_RULES_TURIN,
		.zpc_max_mmiorr = DF_MAX_MMIO_RULES_TURIN,
		.zpc_ccds_per_iodie = DENSE_TURIN_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = DENSE_TURIN_MAX_CORES_PER_CCX,
		.zpc_smu_smn_addrs = {
			.zssa_req = D_TURIN_SMU_RPC_REQ,
			.zssa_resp = D_TURIN_SMU_RPC_RESP,
			.zssa_arg0 = D_TURIN_SMU_RPC_ARG0,
			.zssa_arg1 = D_TURIN_SMU_RPC_ARG1,
			.zssa_arg2 = D_TURIN_SMU_RPC_ARG2,
			.zssa_arg3 = D_TURIN_SMU_RPC_ARG3,
			.zssa_arg4 = D_TURIN_SMU_RPC_ARG4,
			.zssa_arg5 = D_TURIN_SMU_RPC_ARG5,
		},
		.zpc_mpio_smn_addrs = {
			.zmsa_arg0 = D_TURIN_MPIO_RPC_ARG0,
			.zmsa_arg1 = D_TURIN_MPIO_RPC_ARG1,
			.zmsa_arg2 = D_TURIN_MPIO_RPC_ARG2,
			.zmsa_arg3 = D_TURIN_MPIO_RPC_ARG3,
			.zmsa_arg4 = D_TURIN_MPIO_RPC_ARG4,
			.zmsa_arg5 = D_TURIN_MPIO_RPC_ARG5,
			.zmsa_resp = D_TURIN_MPIO_RPC_RESP,
			.zmsa_doorbell = D_TURIN_MPIO_RPC_DOORBELL,
		},
		.zpc_nnbif = TURIN_NBIO_MAX_NBIF,
		.zpc_nbif_nfunc = turin_nbif_nfunc,
		.zpc_nbif_data = turin_nbif_data,
		.zpc_pcie_core0_unitid = TURIN_PCIE_CORE0_UNITID,
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = turin_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(turin_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = turin_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(turin_pcie_port_dbg_regs),
#endif
	},
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
	.zp_smu_ops = &turin_smu_ops,
};
