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

#ifndef	_SYS_IO_ZEN_PLATFORM_IMPL_H
#define	_SYS_IO_ZEN_PLATFORM_IMPL_H

/*
 * This header file provides the top-level microarchitecture-specific ops vector
 * used to provide the microarchitecture-independent entrypoint for the wider
 * kernel. Consumers outside the generic zen code should not include this (or
 * any other _impl.h) header.
 */

#include <sys/amdzen/df.h>
#include <sys/io/zen/uarch.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_platform {
	const zen_platform_consts_t	zp_consts;
	const zen_ccx_ops_t		*zp_ccx_ops;
	const zen_fabric_ops_t		*zp_fabric_ops;
	const zen_hack_ops_t		*zp_hack_ops;
	const zen_ras_ops_t		*zp_ras_ops;
	const zen_smu_ops_t		*zp_smu_ops;
} zen_platform_t;

extern const zen_platform_consts_t *oxide_zen_platform_consts(void);
extern const zen_ccx_ops_t *oxide_zen_ccx_ops(void);
extern const zen_fabric_ops_t *oxide_zen_fabric_ops(void);
extern const zen_hack_ops_t *oxide_zen_hack_ops(void);
extern const zen_ras_ops_t *oxide_zen_ras_ops(void);
extern const zen_smu_ops_t *oxide_zen_smu_ops(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PLATFORM_IMPL_H */
