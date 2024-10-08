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

#ifndef _SYS_IO_ZEN_DXIO_IMPL_H
#define	_SYS_IO_ZEN_DXIO_IMPL_H

/*
 * Definitions for getting to the DXIO Engine configuration data format.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	SMU_HP_PRESENCE_DETECT	= 0,
	SMU_HP_EXPRESS_MODULE_A,
	SMU_HP_ENTERPRISE_SSD,
	SMU_HP_EXPRESS_MODULE_B,
	/*
	 * This value must not be sent to the SMU. It's an internal value to us.
	 * The other values are actually meaningful.
	 */
	SMU_HP_INVALID = INT32_MAX
} smu_hotplug_type_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_DXIO_IMPL_H */
