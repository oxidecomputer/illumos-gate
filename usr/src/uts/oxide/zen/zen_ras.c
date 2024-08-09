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
 * Provide generic code for RAS enablement.  This provides a level of
 * indirection that allows us to manipulate RAS from common code without a
 * direct dependency on any specific microarchitecture.
 */

#include <sys/cmn_err.h>
#include <sys/cpuvar.h>

#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/ras.h>


void
zen_ras_init(void)
{
	const zen_ras_ops_t *rops = oxide_zen_ras_ops();

	if (rops->zro_ras_init == NULL) {
		cmn_err(CE_WARN, "cpu%d: skipping RAS initialization",
		    CPU->cpu_id);
		return;
	}

	(rops->zro_ras_init)();
}
