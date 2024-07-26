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


#include <sys/io/zen/ccx.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>


/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	GENOA_IOMS_PER_IODIE	4

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

static const zen_ccx_ops_t genoa_ccx_ops = {
	.zco_physmem_init = zen_ccx_physmem_init,
	.zco_mmio_init = zen_ccx_mmio_init,
};

static const zen_fabric_ops_t genoa_fabric_ops = {
	.zfo_topo_init = zen_fabric_topo_init_common,
};

zen_platform_t genoa_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_4,
		.zpc_ioms_per_iodie = GENOA_IOMS_PER_IODIE,
		.zpc_df_first_iom_id = GENOA_DF_FIRST_IOM_ID,
		.zpc_df_first_ios_id = GENOA_DF_FIRST_IOS_ID,
	},
	.zp_ccx_ops = &genoa_ccx_ops,
	.zp_fabric_ops = &genoa_fabric_ops,
};
