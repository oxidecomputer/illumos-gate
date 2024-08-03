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
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>

#include <sys/io/milan/fabric_impl.h>


/*
 * XXX: To keep milan working, we still make use of the existing milan_* ccx &
 * fabric routines. But this should all be migrated to the common Zen routines
 * (with any Milan-specific bits kept as needed). Some are already done, like
 * milan_ccx_{physmem,mmio}_init => zen_ccx_{physmem,mmio}_init which had no
 * Milan-specific code and no other dependencies.
 */
extern void milan_fabric_topo_init(void);


/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	MILAN_IOMS_PER_IODIE	4

/*
 * Per the PPR, the following defines the InstanceID of the first IOMS on Milan.
 */
#define	MILAN_DF_FIRST_IOMS_ID	24


static const zen_ccx_ops_t milan_ccx_ops = {
	.zco_physmem_init = zen_ccx_physmem_init,
	.zco_mmio_init = zen_ccx_mmio_init,
	.zco_start_thread = milan_ccx_start_thread,
};

static const zen_fabric_ops_t milan_fabric_ops = {
	.zfo_topo_init = milan_fabric_topo_init,
	.zfo_enable_nmi = milan_fabric_enable_nmi,
	.zfo_nmi_eoi = milan_fabric_nmi_eoi,
};

zen_platform_t milan_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_3,
		.zpc_ioms_per_iodie = MILAN_IOMS_PER_IODIE,
		/*
		 * Milan has a single IOMS component hence these are the same.
		 */
		.zpc_df_first_iom_id = MILAN_DF_FIRST_IOMS_ID,
		.zpc_df_first_ios_id = MILAN_DF_FIRST_IOMS_ID,
	},
	.zp_ccx_ops = &milan_ccx_ops,
	.zp_fabric_ops = &milan_fabric_ops,
};
