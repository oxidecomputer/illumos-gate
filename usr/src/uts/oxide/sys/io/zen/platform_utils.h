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

#ifndef	_SYS_IO_ZEN_PLATFORM_UTILS_H
#define	_SYS_IO_ZEN_PLATFORM_UTILS_H

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/null.h>
#include <sys/platform_detect.h>

#include <sys/io/zen/platform.h>

#ifdef	__cplusplus
extern "C" {
#endif

static inline const zen_ccx_ops_t *
oxide_zen_ccx_ops(void)
{
	const zen_platform_t *platform = oxide_zen_platform();
	const zen_ccx_ops_t *ccx_ops;

	ASSERT3P(platform, !=, NULL);
	ccx_ops = platform->zp_ccx_ops;
	ASSERT3P(ccx_ops, !=, NULL);

	return (ccx_ops);
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PLATFORM_UTILS_H */
