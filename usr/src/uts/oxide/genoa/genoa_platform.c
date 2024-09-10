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

#include <sys/amdzen/df.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/genoa/ccx_impl.h>
#include <sys/io/genoa/mpio.h>
#include <sys/io/genoa/smu.h>
#include <sys/io/zen/mpio.h>

/*
 * Genoa has up to 12 CCDs per IODIE.
 */
#define	GENOA_MAX_CCDS_PER_IODIE	12

/*
 * Genoa has up to 8 cores per CCX.
 */
#define	GENOA_MAX_CORES_PER_CCX		8

static const zen_apob_ops_t genoa_apob_ops = {
};

static const zen_ccx_ops_t genoa_ccx_ops = {
	.zco_init = genoa_ccx_init,
};

static const zen_fabric_ops_t genoa_fabric_ops = {
	.zfo_get_dxio_fw_version = zen_mpio_get_fw_version,
	.zfo_report_dxio_fw_version = zen_mpio_report_fw_version,
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
	},
	.zp_apob_ops = &genoa_apob_ops,
	.zp_ccx_ops = &genoa_ccx_ops,
	.zp_fabric_ops = &genoa_fabric_ops,
	.zp_hack_ops = &genoa_hack_ops,
	.zp_ras_ops = &genoa_ras_ops,
};
