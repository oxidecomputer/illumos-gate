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

#include <sys/amdzen/df.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/turin/ccx_impl.h>

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
	},
	.zp_apob_ops = &turin_apob_ops,
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
};
