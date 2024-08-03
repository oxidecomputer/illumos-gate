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

#include <sys/amdzen/df.h>

#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/ras.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_platform_consts {
	/*
	 * The specific DF revision supported by this platform.
	 * XXX: Determine dynamically?
	 */
	const df_rev_t		zpc_df_rev;
	/*
	 * The number of IOM/S per IO die.
	 */
	const uint8_t		zpc_ioms_per_iodie;
	/*
	 * The InstanceID of the first IOM.
	 */
	const uint8_t		zpc_df_first_iom_id;
	/*
	 * The InstanceID of the first IOS.
	 */
	const uint8_t		zpc_df_first_ios_id;
} zen_platform_consts_t;

typedef struct zen_platform {
	const zen_platform_consts_t	zp_consts;
	const zen_ccx_ops_t		*zp_ccx_ops;
	const zen_fabric_ops_t		*zp_fabric_ops;
	const zen_ras_ops_t		*zp_ras_ops;
} zen_platform_t;


extern const zen_platform_consts_t *oxide_zen_platform_consts(void);
extern const zen_ccx_ops_t *oxide_zen_ccx_ops(void);
extern const zen_fabric_ops_t *oxide_zen_fabric_ops(void);
extern const zen_ras_ops_t *oxide_zen_ras_ops(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PLATFORM_H */
