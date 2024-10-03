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
 * Provides the definition of the microarchitecture-specific platform for Milan.
 *
 * These are operation vectors and the main platform struct that provide common
 * code in the Oxide architecture indirect access to microarchitecture-specific
 * functionality and constants.
 */

#include <sys/sysmacros.h>

#include <sys/amdzen/ccd.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/fch/smi.h>

#include <sys/io/zen/platform.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/milan/dxio.h>
#include <sys/io/milan/fabric_impl.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/io/milan/ioapic.h>
#include <sys/io/milan/iohc.h>
#include <sys/io/milan/iommu.h>
#include <sys/io/milan/smu_impl.h>
#include <sys/io/milan/ras.h>
#include <sys/io/milan/hacks.h>

/*
 * The milan_pcie_dbg.c is file is included here so that we have access to the
 * constant initialisers for the core and port debug registers declared within,
 * and can compute their size. The alternative would be to link it into the
 * final object but we would lose the ability to declare all of the platform
 * data as const below.
 */
#include "milan_pcie_dbg.c"

/*
 * Milan has up to 8 CCDs per I/O die.
 */
#define	MILAN_MAX_CCDS_PER_IODIE	8

/*
 * Milan has up to 8 cores per CCX.
 */
#define	MILAN_MAX_CORES_PER_CCX		8

/*
 * The APOB may provide up to 18 holes in the memory map on Milan.
 */
#define	MILAN_MAX_APOB_MEM_MAP_HOLES	18


static const zen_ccx_ops_t milan_ccx_ops = {
	.zco_get_dpm_weights = milan_fabric_thread_get_dpm_weights,

	.zco_thread_feature_init = milan_thread_feature_init,
	.zco_thread_uc_init = milan_thread_uc_init,
	.zco_core_ls_init = milan_core_ls_init,
	.zco_core_ic_init = milan_core_ic_init,
	.zco_core_dc_init = milan_core_dc_init,
	.zco_core_tw_init = zen_ccx_init_noop,
	.zco_core_de_init = milan_core_de_init,
	.zco_core_fp_init = zen_ccx_init_noop,
	.zco_core_l2_init = milan_core_l2_init,
	.zco_ccx_l3_init = milan_ccx_l3_init,
	.zco_core_undoc_init = milan_core_undoc_init,
};

static const zen_fabric_ops_t milan_fabric_ops = {
	.zfo_init_tom = milan_fabric_init_tom,
	.zfo_disable_vga = milan_fabric_disable_vga,
	.zfo_iohc_pci_ids = milan_fabric_iohc_pci_ids,
	.zfo_pcie_refclk = milan_fabric_pcie_refclk,
	.zfo_pci_crs_to = milan_fabric_set_pci_to,
	.zfo_iohc_features = milan_fabric_iohc_features,
	.zfo_iohc_bus_num = milan_fabric_iohc_bus_num,
	.zfo_iohc_fch_link = milan_fabric_iohc_fch_link,
	.zfo_iohc_arbitration = milan_fabric_iohc_arbitration,
	.zfo_nbif_arbitration = milan_fabric_nbif_arbitration,
	.zfo_sdp_control = milan_fabric_sdp_control,
	.zfo_nbif_syshub_dma = milan_fabric_nbif_syshub_dma,
	.zfo_ioapic = milan_fabric_ioapic,
	.zfo_nbif_dev_straps = milan_fabric_nbif_dev_straps,
	.zfo_nbif_bridges = milan_fabric_nbif_bridges,
	.zfo_pcie = milan_fabric_pcie,

	.zfo_iohc_enable_nmi = milan_iohc_enable_nmi,
	.zfo_iohc_nmi_eoi = milan_iohc_nmi_eoi,

	.zfo_topo_init = milan_fabric_topo_init,
	.zfo_soc_init = milan_fabric_soc_init,
	.zfo_iodie_init = milan_fabric_iodie_init,
	.zfo_smu_misc_init = milan_fabric_smu_misc_init,
	.zfo_ioms_init = milan_fabric_ioms_init,
	.zfo_ioms_pcie_init = milan_fabric_ioms_pcie_init,

	.zfo_get_dxio_fw_version = milan_get_dxio_fw_version,
	.zfo_report_dxio_fw_version = milan_report_dxio_fw_version,

	.zfo_ioms_n_pcie_cores = milan_ioms_n_pcie_cores,
	.zfo_pcie_core_n_ports = milan_pcie_core_n_ports,
	.zfo_pcie_core_info = milan_pcie_core_info,
	.zfo_pcie_port_info = milan_pcie_port_info,
	.zfo_pcie_core_reg = milan_pcie_core_reg,
	.zfo_pcie_port_reg = milan_pcie_port_reg,
	.zfo_pcie_dbg_signal = milan_pcie_dbg_signal,
};

static const zen_hack_ops_t milan_hack_ops = {
	.zho_check_furtive_reset = milan_check_furtive_reset,
	.zho_cgpll_set_ssc = milan_cgpll_set_ssc,
	.zho_hack_gpio = milan_hack_gpio,
};

static const zen_ras_ops_t milan_ras_ops = {
	.zro_ras_init = milan_ras_init,
};

const zen_platform_t milan_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_3,
		.zpc_chiprev = X86_CHIPREV_AMD_MILAN_B0 |
		    X86_CHIPREV_AMD_MILAN_B1,
		.zpc_max_apob_mem_map_holes = MILAN_MAX_APOB_MEM_MAP_HOLES,
		.zpc_max_cfgmap = DF_MAX_CFGMAP,
		.zpc_max_iorr = DF_MAX_IO_RULES,
		.zpc_max_mmiorr = DF_MAX_MMIO_RULES,
		.zpc_ccds_per_iodie = MILAN_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = MILAN_MAX_CORES_PER_CCX,
		.zpc_smu_smn_addrs = {
			.zssa_req = D_MILAN_SMU_RPC_REQ,
			.zssa_resp = D_MILAN_SMU_RPC_RESP,
			.zssa_arg0 = D_MILAN_SMU_RPC_ARG0,
			.zssa_arg1 = D_MILAN_SMU_RPC_ARG1,
			.zssa_arg2 = D_MILAN_SMU_RPC_ARG2,
			.zssa_arg3 = D_MILAN_SMU_RPC_ARG3,
			.zssa_arg4 = D_MILAN_SMU_RPC_ARG4,
			.zssa_arg5 = D_MILAN_SMU_RPC_ARG5,
		},
		.zpc_nnbif = MILAN_IOMS_MAX_NBIF,
		.zpc_nbif_nfunc = milan_nbif_nfunc,
		.zpc_nbif_data = milan_nbif_data,
		.zpc_pcie_core0_unitid = MILAN_PCIE_CORE0_UNITID,
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = milan_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(milan_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = milan_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(milan_pcie_port_dbg_regs),
#endif
	},
	.zp_ccx_ops = &milan_ccx_ops,
	.zp_fabric_ops = &milan_fabric_ops,
	.zp_hack_ops = &milan_hack_ops,
	.zp_ras_ops = &milan_ras_ops,
};
