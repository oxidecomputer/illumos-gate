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

#ifndef _SYS_IO_ZEN_HOTPLUG_IMPL_H
#define	_SYS_IO_ZEN_HOTPLUG_IMPL_H

/*
 * Centralised definitions for the hotplug scheme in use.
 */

#include <sys/types.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ZEN_HP_PRESENCE_DETECT	= 0,
	ZEN_HP_EXPRESS_MODULE_A,
	ZEN_HP_ENTERPRISE_SSD,
	ZEN_HP_EXPRESS_MODULE_B,
	/*
	 * This value must not be sent to DXIO/MPIO. It's an internal value to
	 * us. The other values are actually meaningful values to the firmware
	 * and currently consistent across platforms.
	 */
	ZEN_HP_INVALID = INT32_MAX
} zen_hotplug_type_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_HOTPLUG_IMPL_H */
