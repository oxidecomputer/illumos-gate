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

#ifndef _SYS_IO_ZEN_HOTPLUG_H
#define	_SYS_IO_ZEN_HOTPLUG_H

/*
 * This header contains constant definitions used for traditional (non-UBM) PCIe
 * hotplug by SMU and MPIO firmware.  These are the same across Milan, Genoa and
 * Turin.
 */

/*
 * Note, Express Module B is no longer supported starting in Genoa.
 */
typedef enum zen_hotplug_fw_type {
	ZEN_HP_FW_PRESENCE_DETECT	= 0,
	ZEN_HP_FW_EXPRESS_MODULE_A,
	ZEN_HP_FW_ENTERPRISE_SSD,
	ZEN_HP_FW_EXPRESS_MODULE_B,
} zen_hotplug_fw_type_t;

typedef enum zen_hotplug_fw_i2c_expander_type {
	ZEN_HP_FW_I2C_EXP_PCA9539 = 0,
	ZEN_HP_FW_I2C_EXP_PCA9535 = 1,
	ZEN_HP_FW_I2C_EXP_PCA9506 = 2
} zen_hotplug_fw_i2c_expander_type_t;

typedef enum zen_hotplug_fw_i2c_switch_type {
	ZEN_HP_FW_I2C_SW_9545 = 0,
	ZEN_HP_FW_I2C_SW_9546_48 = 1,
} zen_hotplug_fw_i2c_switch_type_t;

typedef enum zen_hotplug_fw_enta_bits {
	ZEN_HP_FW_EXPA_PRSNT		= 1 << 0,
	ZEN_HP_FW_EXPA_PWRFLT		= 1 << 1,
	ZEN_HP_FW_EXPA_ATTNSW		= 1 << 2,
	ZEN_HP_FW_EXPA_EMILS		= 1 << 3,
	ZEN_HP_FW_EXPA_PWREN		= 1 << 4,
	ZEN_HP_FW_EXPA_ATTNLED		= 1 << 5,
	ZEN_HP_FW_EXPA_PWRLED		= 1 << 6,
	ZEN_HP_FW_EXPA_EMIL		= 1 << 7
} zen_hotplug_fw_expa_bits_t;

typedef enum zen_hotplug_fw_entb_bits {
	ZEN_HP_FW_EXPB_ATTNLED		= 1 << 0,
	ZEN_HP_FW_EXPB_PWRLED		= 1 << 1,
	ZEN_HP_FW_EXPB_PWREN		= 1 << 2,
	ZEN_HP_FW_EXPB_ATTNSW		= 1 << 3,
	ZEN_HP_FW_EXPB_PRSNT		= 1 << 4,
	ZEN_HP_FW_EXPB_PWRFLT		= 1 << 5,
	ZEN_HP_FW_EXPB_EMILS		= 1 << 6,
	ZEN_HP_FW_EXPB_EMIL		= 1 << 7
} zen_hotplug_fw_expb_bits_t;

#endif /* _SYS_IO_ZEN_HOTPLUG_H */
