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

#include <sys/io/genoa/ccx_impl.h>


/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	GENOA_IOMS_PER_IODIE		4

/*
 * Genoa has up to 12 CCDs per IODIE.
 */
#define	GENOA_MAX_CCDS_PER_IODIE	12

/*
 * Genoa has up to 8 cores per CCX.
 */
#define	GENOA_MAX_CORES_PER_CCX		8

/*
 * Per the PPR, the following defines the InstanceID of the first Genoa
 * IOM (IOMx_IOHUBx).
 */
#define	GENOA_DF_FIRST_IOM_ID	0x20

/*
 * Per the PPR, the following defines the InstanceID of the first Genoa
 * IOS (IOHUBSx).
 */
#define	GENOA_DF_FIRST_IOS_ID	0x24

static const zen_apob_ops_t genoa_apob_ops = {
};

static const zen_ccx_ops_t genoa_ccx_ops = {
	.zco_physmem_init = zen_ccx_physmem_init_common,
	.zco_init = genoa_ccx_init,
};

static const zen_fabric_ops_t genoa_fabric_ops = {
};

static const zen_hack_ops_t genoa_hack_ops = {
};

static const zen_ras_ops_t genoa_ras_ops = {
};

static const zen_smn_ops_t genoa_smn_ops = {
	.zso_core_reg_fn = {
		[SMN_UNIT_SCFCTP]	= amdzen_scfctp_smn_reg,
	},
	.zso_ccd_reg_fn = {
		[SMN_UNIT_SMUPWR]	= amdzen_smupwr_smn_reg,
	},
	.zso_ioms_reg_fn = {
	},
	.zso_iodie_reg_fn = {
	},
};

zen_platform_t genoa_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4,
		.zpc_ioms_per_iodie = GENOA_IOMS_PER_IODIE,
		.zpc_ccds_per_iodie = GENOA_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = GENOA_MAX_CORES_PER_CCX,
		.zpc_df_first_iom_id = GENOA_DF_FIRST_IOM_ID,
		.zpc_df_first_ios_id = GENOA_DF_FIRST_IOS_ID,
	},
	.zp_apob_ops = &genoa_apob_ops,
	.zp_ccx_ops = &genoa_ccx_ops,
	.zp_fabric_ops = &genoa_fabric_ops,
	.zp_hack_ops = &genoa_hack_ops,
	.zp_ras_ops = &genoa_ras_ops,
	.zp_smn_ops = &genoa_smn_ops,
};
