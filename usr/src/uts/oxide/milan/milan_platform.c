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


#include <sys/io/zen/platform.h>
#include <sys/io/milan/platform_impl.h>

#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

static const zen_ccx_ops_t milan_ccx_ops = {
	.zco_physmem_init = zen_ccx_physmem_init,
};

static const zen_fabric_ops_t milan_fabric_ops = {};

zen_platform_t milan_platform = {
	.zp_ccx_ops = &milan_ccx_ops,
	.zp_fabric_ops = &milan_fabric_ops,
};
