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

#ifndef _SYS_IO_ZEN_DXIO_DATA_H
#define	_SYS_IO_ZEN_DXIO_DATA_H

/*
 * Definitions used in DXIO data common across MPIO and DXIO via the SMU.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum smu_exp_type {
	SMU_I2C_PCA9539 = 0,
	SMU_I2C_PCA9535 = 1,
	SMU_I2C_PCA9506 = 2
} smu_exp_type_t;

typedef enum smu_gpio_sw_type {
	SMU_GPIO_SW_9545 = 0,
	SMU_GPIO_SW_9546_48 = 1,
} smu_gpio_sw_type_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_DXIO_DATA_H */
