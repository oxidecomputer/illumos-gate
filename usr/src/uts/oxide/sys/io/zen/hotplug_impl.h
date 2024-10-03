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

typedef enum smu_exp_type {
	SMU_I2C_PCA9539 = 0,
	SMU_I2C_PCA9535 = 1,
	SMU_I2C_PCA9506 = 2
} smu_exp_type_t;

typedef enum smu_gpio_sw_type {
	SMU_GPIO_SW_9545 = 0,
	SMU_GPIO_SW_9546_48 = 1,
} smu_gpio_sw_type_t;

/*
 * XXX it may be nicer for us to define our own semantic set of bits here that
 * don't change based on version and then we change it.
 */
typedef enum smu_enta_bits {
	SMU_ENTA_PRSNT		= 1 << 0,
	SMU_ENTA_PWRFLT		= 1 << 1,
	SMU_ENTA_ATTNSW		= 1 << 2,
	SMU_ENTA_EMILS		= 1 << 3,
	SMU_ENTA_PWREN		= 1 << 4,
	SMU_ENTA_ATTNLED	= 1 << 5,
	SMU_ENTA_PWRLED		= 1 << 6,
	SMU_ENTA_EMIL		= 1 << 7
} smu_enta_bits_t;

typedef enum smu_entb_bits {
	SMU_ENTB_ATTNLED	= 1 << 0,
	SMU_ENTB_PWRLED		= 1 << 1,
	SMU_ENTB_PWREN		= 1 << 2,
	SMU_ENTB_ATTNSW		= 1 << 3,
	SMU_ENTB_PRSNT		= 1 << 4,
	SMU_ENTB_PWRFLT		= 1 << 5,
	SMU_ENTB_EMILS		= 1 << 6,
	SMU_ENTB_EMIL		= 1 << 7
} smu_entb_bits_t;

#define	SMU_I2C_DIRECT	0x7

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_HOTPLUG_IMPL_H */
