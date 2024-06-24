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

#ifndef _SYS_IO_ZEN_FIRMWARE_IMPL_H
#define	_SYS_IO_ZEN_FIRMWARE_IMPL_H

/*
 * Definitions for getting to the DXIO Engine configuration data format.
 */

#include <sys/param.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	DXIO_PORT_NOT_PRESENT	0
#define	DXIO_PORT_PRESENT	1

typedef enum zen_link_speed {
	ZEN_LINK_SPEED_MAX	= 0,
	ZEN_LINK_SPEDD_GEN1,
	ZEN_LINK_SPEED_GEN2,
	ZEN_LINK_SPEED_GEN3,
	ZEN_LINK_SPEED_GEN4
} zen_dxio_link_speed_t;

typedef enum zen_hotplug_type {
	ZEN_HOTPLUG_T_DISABLED	= 0,
	ZEN_HOTPLUG_T_BASIC,
	ZEN_HOTPLUG_T_EXPRESS_MODULE,
	ZEN_HOTPLUG_T_ENHANCED,
	ZEN_HOTPLUG_T_INBOARD,
	ZEN_HOTPLUG_T_ENT_SSD
} zen_hotplug_type_t;

typedef enum zen_fw_hotplug_type {
	ZEN_FW_HP_PRESENCE_DETECT	= 0,
	ZEN_FW_HP_EXPRESS_MODULE_A,
	ZEN_FW_HP_ENTERPRISE_SSD,
	ZEN_FW_HP_EXPRESS_MODULE_B,
	/*
	 * This value must not be sent to the SMU. It's an internal value to us.
	 * The other values are actually meaningful.
	 */
	ZEN_FIRMWARE_HP_INVALID = INT32_MAX
} zen_fw_hotplug_type_t;



#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FIRMWARE_IMPL_H */
