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

#ifndef	_SYS_IO_ZEN_PLATFORM_H
#define	_SYS_IO_ZEN_PLATFORM_H

#include <sys/types.h>

#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_platform {
	const zen_ccx_ops_t	*zp_ccx_ops;
	const zen_fabric_ops_t	*zp_fabric_ops;

	zen_fabric_t		zp_fabric;
} zen_platform_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PLATFORM_H */
