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
#include <sys/io/turin/pcie_impl.h>
#include <sys/io/turin/ccx_impl.h>
#include <sys/io/turin/mpio.h>
#include <sys/io/turin/smu.h>
#include <sys/io/zen/mpio.h>

/*
 * The turin_pcie_dbg.c is file is included here so that we have access to the
 * constant initialisers for the core and port debug registers declared within,
 * and can compute their size. The alternative would be to link it into the
 * final object but we would lose the ability to declare all of the platform
 * data as const below.
 */
#include "turin_pcie_dbg.c"

/*
 * Classic Turin has up to 16 CCDs per IODIE, whereas
 * Dense Turin only has up to 12 CCDs per IODIE.
 */
#define	CLASSIC_TURIN_MAX_CCDS_PER_IODIE	16
#define	DENSE_TURIN_MAX_CCDS_PER_IODIE		12

/*
 * Classic Turin has up to 8 cores per CCX.
 */
#define	CLASSIC_TURIN_MAX_CORES_PER_CCX		8
/*
 * Whereas Dense Turin has up to 16 cores per CCX.
 */
#define	DENSE_TURIN_MAX_CORES_PER_CCX		16


static const zen_apob_ops_t turin_apob_ops = {
};

static const zen_ccx_ops_t turin_ccx_ops = {
	.zco_physmem_init = turin_ccx_physmem_init,
	.zco_init = turin_ccx_init,
};

static const zen_fabric_ops_t turin_fabric_ops = {
	.zfo_get_dxio_fw_version = zen_mpio_get_fw_version,
	.zfo_report_dxio_fw_version = zen_mpio_report_fw_version,

	.zfo_fabric_init = turin_fabric_init,

	.zfo_pcie_core_reg = turin_pcie_core_reg,
	.zfo_pcie_port_reg = turin_pcie_port_reg,
};

static const zen_hack_ops_t turin_hack_ops = {
	.zho_check_furtive_reset = zen_null_check_furtive_reset,
	.zho_cgpll_set_ssc = zen_null_cgpll_set_ssc,
};

static const zen_ras_ops_t turin_ras_ops = {
};

const zen_platform_t turin_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4D2,
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
#ifdef DEBUG
		.zpc_pcie_core_dbg_regs = turin_pcie_core_dbg_regs,
		.zpc_pcie_core_dbg_nregs = ARRAY_SIZE(turin_pcie_core_dbg_regs),
		.zpc_pcie_port_dbg_regs = turin_pcie_port_dbg_regs,
		.zpc_pcie_port_dbg_nregs = ARRAY_SIZE(turin_pcie_port_dbg_regs),
#endif
	},
	.zp_apob_ops = &turin_apob_ops,
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
};

const zen_platform_t dense_turin_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4D2,
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
	},
	.zp_apob_ops = &turin_apob_ops,
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
};
