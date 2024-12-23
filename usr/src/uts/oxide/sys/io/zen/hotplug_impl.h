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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _SYS_IO_ZEN_HOTPLUG_IMPL_H
#define	_SYS_IO_ZEN_HOTPLUG_IMPL_H

/*
 * Centralized definitions for traditional (non-UBM) PCIe hotplug. This header
 * contains common constants that are synced with their values in SMU firmware
 * and are the same across Milan through Turin.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This enumeration describes the data layout of the traditional hotplug
 * descriptors that are sent to the SMU. As we don't support microarchitectures
 * prior to Milan and Milan supports version 2, we don't list version 1 here.
 */

typedef enum {
	ZEN_HP_VERS_2 = 2,
	ZEN_HP_VERS_3
} zen_hotplug_vers_t;

/*
 * Note, Express Module B is no longer supported starting in Genoa.
 */
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

typedef enum smu_i2c_sw_type {
	SMU_I2C_SW_9545 = 0,
	SMU_I2C_SW_9546_48 = 1,
} smu_i2c_sw_type_t;

typedef enum smu_enta_bits {
	SMU_EXPA_PRSNT		= 1 << 0,
	SMU_EXPA_PWRFLT		= 1 << 1,
	SMU_EXPA_ATTNSW		= 1 << 2,
	SMU_EXPA_EMILS		= 1 << 3,
	SMU_EXPA_PWREN		= 1 << 4,
	SMU_EXPA_ATTNLED	= 1 << 5,
	SMU_EXPA_PWRLED		= 1 << 6,
	SMU_EXPA_EMIL		= 1 << 7
} smu_expa_bits_t;

typedef enum smu_entb_bits {
	SMU_EXPB_ATTNLED	= 1 << 0,
	SMU_EXPB_PWRLED		= 1 << 1,
	SMU_EXPB_PWREN		= 1 << 2,
	SMU_EXPB_ATTNSW		= 1 << 3,
	SMU_EXPB_PRSNT		= 1 << 4,
	SMU_EXPB_PWRFLT		= 1 << 5,
	SMU_EXPB_EMILS		= 1 << 6,
	SMU_EXPB_EMIL		= 1 << 7
} smu_expb_bits_t;

#define	SMU_I2C_DIRECT	0x7

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_HOTPLUG_IMPL_H */
