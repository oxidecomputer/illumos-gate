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
#include <milan/milan_apob.h>

/*
 * The milan_pcie_dbg.c is file is included here so that we have access to the
 * constant initialisers for the core and port debug registers declared within,
 * and can compute their size. The alternative would be to link it into the
 * final object but we would lose the ability to declare all of the platform
 * data as const below.
 */
#include "milan_pcie_dbg.c"

static const zen_apob_ops_t milan_apob_ops = {
	.zao_reserve_phys = milan_apob_reserve_phys,
};

static const zen_ccx_ops_t milan_ccx_ops = {
	.zco_init = milan_ccx_init,
	.zco_start_thread = milan_ccx_start_thread,
};

static const zen_fabric_ops_t milan_fabric_ops = {
	.zfo_fabric_init = milan_fabric_init,
	.zfo_enable_nmi = milan_fabric_enable_nmi,
	.zfo_nmi_eoi = milan_fabric_nmi_eoi,

	.zfo_topo_init = milan_fabric_topo_init,
	.zfo_soc_init = milan_fabric_soc_init,
	.zfo_ioms_init = milan_fabric_ioms_init,

	.zfo_get_dxio_fw_version = milan_get_dxio_fw_version,
	.zfo_report_dxio_fw_version = milan_report_dxio_fw_version,

	.zfo_pcie_core_reg = milan_pcie_core_reg,
	.zfo_pcie_port_reg = milan_pcie_port_reg,
	.zfo_pcie_dbg_signal = milan_pcie_dbg_signal,
};

static const zen_hack_ops_t milan_hack_ops = {
	.zho_check_furtive_reset = milan_check_furtive_reset,
	.zho_cgpll_set_ssc = milan_cgpll_set_ssc,
};

static const zen_ras_ops_t milan_ras_ops = {
	.zro_ras_init = milan_ras_init,
};

const zen_platform_t milan_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_3,
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
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = milan_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(milan_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = milan_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(milan_pcie_port_dbg_regs),
#endif
	},
	.zp_apob_ops = &milan_apob_ops,
	.zp_ccx_ops = &milan_ccx_ops,
	.zp_fabric_ops = &milan_fabric_ops,
	.zp_hack_ops = &milan_hack_ops,
	.zp_ras_ops = &milan_ras_ops,
};
