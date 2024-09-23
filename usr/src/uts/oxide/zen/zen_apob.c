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
 * Abstract support for the APOB, so that code common across the Oxide
 * architecture can work with it, without a direct dependency on a
 * specific microarchitecture.
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/null.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/zen/apob.h>

/*
 * The APOB is set up by the PSP.  This invokes the microarchitecture-specific
 * code to walk through that and set up physical memory reservations for any
 * holes that it specifies.
 */
void
zen_apob_reserve_phys(void)
{
	const zen_apob_ops_t *apob_ops = oxide_zen_apob_ops();
	VERIFY3P(apob_ops->zao_reserve_phys, !=, NULL);
	(apob_ops->zao_reserve_phys)();
}

/*
 * A no-op for apob reservations for microarchitectures that have no special
 * handling needs.
 */
void
zen_null_apob_reserve_phys(void)
{
}
