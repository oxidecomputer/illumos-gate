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

#include <sys/amdzen/ccd.h>

#include <sys/io/zen/ccx.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>
#include <sys/io/zen/smn.h>

#include <sys/io/turin/ccx_impl.h>

/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	TURIN_IOMS_PER_IODIE			8

/*
 * Classic Turin has up to 16 CCDs per IODIE.
 */
#define	CLASSIC_TURIN_MAX_CCDS_PER_IODIE	16
/*
 * Whereas Dense Turin only has up to 12 CCDs per IODIE.
 */
#define	DENSE_TURIN_MAX_CCDS_PER_IODIE		12

/*
 * Classic Turin has up to 8 cores per CCX.
 */
#define	CLASSIC_TURIN_MAX_CORES_PER_CCX		8
/*
 * Whereas Dense Turin has up to 16 cores per CCX.
 */
#define	DENSE_TURIN_MAX_CORES_PER_CCX		16

/*
 * Per the PPR, the following defines the InstanceID of the first Turin
 * IOM (IOMx_IOHUBx).
 */
#define	TURIN_DF_FIRST_IOM_ID	0x20

/*
 * Per the PPR, the following defines the InstanceID of the first Turin
 * IOS (IOHUBSx).
 */
#define	TURIN_DF_FIRST_IOS_ID	0x28


static const zen_apob_ops_t turin_apob_ops = {
};

static const zen_ccx_ops_t turin_ccx_ops = {
	.zco_physmem_init = turin_ccx_physmem_init,
	.zco_init = turin_ccx_init,
};

static const zen_fabric_ops_t turin_fabric_ops = {
};

static const zen_hack_ops_t turin_hack_ops = {
};

static const zen_ras_ops_t turin_ras_ops = {
};

static const zen_smn_ops_t turin_smn_ops = {
	.zso_core_reg_fn = {
		[SMN_UNIT_SCFCTP]	= amdzen_scfctp_smn_reg,
	},
	.zso_ccd_reg_fn = {
		[SMN_UNIT_SMUPWR]	= amdzen_smupwr_smn_reg,
		[SMN_UNIT_L3SOC]	= amdzen_l3soc_smn_reg,
	},
	.zso_ioms_reg_fn = {
	},
	.zso_iodie_reg_fn = {
	},
};

zen_platform_t turin_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4D2,
		.zpc_ioms_per_iodie = TURIN_IOMS_PER_IODIE,
		.zpc_ccds_per_iodie = CLASSIC_TURIN_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = CLASSIC_TURIN_MAX_CORES_PER_CCX,
		.zpc_df_first_iom_id = TURIN_DF_FIRST_IOM_ID,
		.zpc_df_first_ios_id = TURIN_DF_FIRST_IOS_ID,
	},
	.zp_apob_ops = &turin_apob_ops,
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
	.zp_smn_ops = &turin_smn_ops,
};

zen_platform_t dense_turin_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4D2,
		.zpc_ioms_per_iodie = TURIN_IOMS_PER_IODIE,
		.zpc_ccds_per_iodie = DENSE_TURIN_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = DENSE_TURIN_MAX_CORES_PER_CCX,
		.zpc_df_first_iom_id = TURIN_DF_FIRST_IOM_ID,
		.zpc_df_first_ios_id = TURIN_DF_FIRST_IOS_ID,
	},
	.zp_apob_ops = &turin_apob_ops,
	.zp_ccx_ops = &turin_ccx_ops,
	.zp_fabric_ops = &turin_fabric_ops,
	.zp_hack_ops = &turin_hack_ops,
	.zp_ras_ops = &turin_ras_ops,
	.zp_smn_ops = &turin_smn_ops,
};
