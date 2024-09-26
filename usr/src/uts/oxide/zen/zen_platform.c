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
 * Provides convenient accessors for ops vectors and so forth from the Oxide
 * platform definition.  Generally, client code in the Oxide architecture is
 * expected to invoke a wrapper function which calls these, as opposed to
 * accessing the ops vectors directly.  Thus, these are used only in the
 * implementation of those wrappers.
 *
 * Ideally, none of these would be necessary, and we'd simply plumb these
 * through as arguments where needed (or we'd just pass the `zen_platform_t`
 * around and dereference from there), but empirically this has been seen to be
 * a bit unwieldy.
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/null.h>
#include <sys/platform_detect.h>
#include <sys/io/zen/platform_impl.h>

const zen_platform_consts_t *
oxide_zen_platform_consts(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	ASSERT3P(platform, !=, NULL);
	return (&platform->zp_consts);
}

const zen_ccx_ops_t *
oxide_zen_ccx_ops(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	const zen_ccx_ops_t *ccx_ops;

	ASSERT3P(platform, !=, NULL);
	ccx_ops = platform->zp_ccx_ops;
	ASSERT3P(ccx_ops, !=, NULL);

	return (ccx_ops);
}

const zen_fabric_ops_t *
oxide_zen_fabric_ops(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	const zen_fabric_ops_t *fabric_ops;

	ASSERT3P(platform, !=, NULL);
	fabric_ops = platform->zp_fabric_ops;
	ASSERT3P(fabric_ops, !=, NULL);

	return (fabric_ops);
}

const zen_hack_ops_t *
oxide_zen_hack_ops(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	const zen_hack_ops_t *hack_ops;

	ASSERT3P(platform, !=, NULL);
	hack_ops = platform->zp_hack_ops;
	ASSERT3P(hack_ops, !=, NULL);

	return (hack_ops);
}

const zen_ras_ops_t *
oxide_zen_ras_ops(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	const zen_ras_ops_t *ras_ops;

	ASSERT3P(platform, !=, NULL);
	ras_ops = platform->zp_ras_ops;
	ASSERT3P(ras_ops, !=, NULL);

	return (ras_ops);
}

const zen_smu_ops_t *
oxide_zen_smu_ops(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	const zen_smu_ops_t *smu_ops;

	ASSERT3P(platform, !=, NULL);
	smu_ops = platform->zp_smu_ops;
	ASSERT3P(smu_ops, !=, NULL);

	return (smu_ops);
}
