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
#include <sys/io/turin/pcie_impl.h>
#include <sys/io/turin/ccx_impl.h>
#include <sys/io/turin/mpio_impl.h>
#include <sys/io/turin/ras_impl.h>
#include <sys/io/turin/hacks.h>
#include <sys/io/turin/smu.h>
#include <sys/io/zen/mpio.h>

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
	 * Turin does not read weights from the SMU and set them explicitly.
	 * Instead, they seem to be set indirectly via enabling SMU features.
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

	.zfo_set_mpio_global_config = turin_set_mpio_global_config,
	.zfo_smu_pptable_init = turin_fabric_smu_pptable_init,
	.zfo_smu_pptable_post = turin_fabric_smu_pptable_post,
	.zfo_smu_misc_init = turin_smu_features_init,
	.zfo_nbio_init = turin_fabric_nbio_init,
	.zfo_ioms_init = turin_fabric_ioms_init,
	.zfo_misc_late_init = turin_fabric_misc_late_init,

	.zfo_physaddr_size = turin_fabric_physaddr_size,
	.zfo_init_tom = turin_fabric_init_tom,
	.zfo_disable_vga = turin_fabric_disable_vga,
	.zfo_iohc_pci_ids = zen_null_fabric_iohc_pci_ids, /* skip for turin */
	.zfo_pcie_refclk = turin_fabric_pcie_refclk,
	.zfo_pci_crs_to = turin_fabric_set_pci_to,
	.zfo_iohc_features = turin_fabric_iohc_features,
	.zfo_nbio_features = turin_fabric_nbio_features,
	.zfo_iohc_bus_num = turin_fabric_iohc_bus_num,
	.zfo_iohc_fch_link = turin_fabric_iohc_fch_link,
	.zfo_iohc_arbitration = turin_fabric_iohc_arbitration,
	.zfo_nbio_arbitration = turin_fabric_nbio_arbitration,
	.zfo_nbif_arbitration = turin_fabric_nbif_arbitration,
	.zfo_sdp_control = zen_null_fabric_sdp_control, /* skip for turin */
	.zfo_nbio_sdp_control = zen_null_fabric_nbio_sdp_control, /* skip */
	.zfo_nbif_syshub_dma = turin_fabric_nbif_syshub_dma,
	/*
	 * Unlike prior SoCs, the Turin family automatically enables clock
	 * gating for the IOHC and IOAPIC, but not the nBIFs. We need to enable
	 * clock gating for the nBIFs and we opt to do so for the others so a
	 * change in surrounding firmware doesn't change our desired settings.
	 */
	.zfo_iohc_clock_gating = turin_fabric_iohc_clock_gating,
	.zfo_nbio_clock_gating = turin_fabric_nbio_clock_gating,
	.zfo_nbif_clock_gating = turin_fabric_nbif_clock_gating,
	.zfo_ioapic_clock_gating = turin_fabric_ioapic_clock_gating,
	.zfo_ioapic = turin_fabric_ioapic,
	.zfo_nbif_init = turin_fabric_nbif_init,
	.zfo_nbif_dev_straps = turin_fabric_nbif_dev_straps,
	.zfo_nbif_bridges = zen_null_fabric_nbif_bridges, /* skip for turin */
	.zfo_ioms_nbio_num = turin_fabric_ioms_nbio_num,
	.zfo_pcie = turin_fabric_pcie,
	.zfo_pcie_port_is_trained = zen_mpio_pcie_port_is_trained,
	.zfo_pcie_port_unhide_bridge = turin_fabric_unhide_bridge,
	.zfo_pcie_port_hide_bridge = turin_fabric_hide_bridge,
	.zfo_init_pcie_port = turin_fabric_init_pcie_port,
	.zfo_init_pcie_port_after_reconfig =
	    turin_fabric_init_pcie_port_after_reconfig,
	.zfo_init_pcie_straps = turin_fabric_init_pcie_straps,
	.zfo_init_pcie_core = turin_fabric_init_pcie_core,
	.zfo_init_bridge = turin_fabric_init_bridge,
	.zfo_pcie_hotplug_port_data_init = zen_mpio_hotplug_port_data_init,
	.zfo_pcie_hotplug_fw_init = zen_mpio_init_hotplug_fw,
	.zfo_pcie_hotplug_core_init = turin_fabric_hotplug_core_init,
	.zfo_pcie_hotplug_port_init = turin_fabric_hotplug_port_init,
	.zfo_pcie_hotplug_port_unblock_training =
	    turin_fabric_hotplug_port_unblock_training,
	.zfo_pcie_hotplug_set_flags = zen_mpio_null_set_hotplug_flags,
	.zfo_pcie_hotplug_start = turin_fabric_hotplug_start,
	.zfo_iohc_disable_unused_pcie_bridges =
	    turin_fabric_ioms_iohc_disable_unused_pcie_bridges,

	.zfo_iohc_enable_nmi = turin_iohc_enable_nmi,
	.zfo_iohc_nmi_eoi = turin_iohc_nmi_eoi,

	.zfo_iohc_n_pcie_cores = turin_iohc_n_pcie_cores,
	.zfo_pcie_core_n_ports = turin_pcie_core_n_ports,
	.zfo_pcie_core_info = turin_pcie_core_info,
	.zfo_pcie_port_info = turin_pcie_port_info,
	.zfo_pcie_core_reg = turin_pcie_core_reg,
	.zfo_pcie_port_reg = turin_pcie_port_reg,
	.zfo_pcie_core_read = zen_mpio_pcie_core_read,
	.zfo_pcie_core_write = zen_mpio_pcie_core_write,
	.zfo_pcie_port_read = zen_mpio_pcie_port_read,
	.zfo_pcie_port_write = zen_mpio_pcie_port_write,
	.zfo_pcie_dbg_signal = turin_pcie_dbg_signal,

	.zfo_tile_fw_hp_id = turin_fabric_hotplug_tile_id,
};

static const zen_hack_ops_t turin_hack_ops = {
	.zho_check_furtive_reset = zen_null_check_furtive_reset,
	.zho_cgpll_set_ssc = turin_cgpll_set_ssc,
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
		.zpc_ras_init_data = &turin_ras_init_data,
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
			.zmsa_reg_base = ZEN_MPIO_SMN_REG_BASE,
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
		.zpc_pcie_int_ports = turin_pcie_int_ports,
		.zpc_pcie_core_dbg_regs = turin_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = &turin_pcie_core_dbg_nregs,
		.zpc_pcie_port_dbg_regs = turin_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = &turin_pcie_port_dbg_nregs,
		.zpc_pcie_core_max_ports = TURIN_PCIE_CORE_MAX_PORTS,
		.zpc_pcie_max_speed = OXIO_SPEED_GEN_5,
	},
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
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
		.zpc_ras_init_data = &turin_ras_init_data,
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
			.zmsa_reg_base = ZEN_MPIO_SMN_REG_BASE,
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
		.zpc_pcie_int_ports = turin_pcie_int_ports,
		.zpc_pcie_core_dbg_regs = turin_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = &turin_pcie_core_dbg_nregs,
		.zpc_pcie_port_dbg_regs = turin_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = &turin_pcie_port_dbg_nregs,
		.zpc_pcie_core_max_ports = TURIN_PCIE_CORE_MAX_PORTS,
		.zpc_pcie_max_speed = OXIO_SPEED_GEN_5,
	},
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
};
