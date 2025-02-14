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

/*
 * Oxide DXIO Ruby Declaration
 */

#include <sys/io/zen/oxio.h>
#include <sys/sysmacros.h>

const oxio_engine_t oxio_ruby[] = { {
	.oe_name = "Ruby Riser 1",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P1,
	.oe_lane = 0,
	.oe_nlanes = 16
}, {
	.oe_name = "Ruby Riser 2",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 8,
	.oe_nlanes = 8
}, {
	.oe_name = "Ruby Riser 3",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P3,
	.oe_lane = 0,
	.oe_nlanes = 16
},  {
	.oe_name = "OCP 3.0",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P2,
	.oe_lane = 0,
	.oe_nlanes = 16
}, {
	.oe_name = "M.2 0 (x4)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P4,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_tuning = {
		.ot_hw_limit = OXIO_SPEED_GEN_4,
		.ot_hw_target = OXIO_SPEED_GEN_3
	}
}, {
	.oe_name = "M.2 1 (x1)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P5,
	.oe_lane = 0,
	.oe_nlanes = 1,
	.oe_tuning = {
		.ot_hw_limit = OXIO_SPEED_GEN_4,
		.ot_hw_target = OXIO_SPEED_GEN_3
	}
}, {
	.oe_name = "M.2 2 (x1)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P5,
	.oe_lane = 1,
	.oe_nlanes = 1,
	.oe_tuning = {
		.ot_hw_limit = OXIO_SPEED_GEN_4,
		.ot_hw_target = OXIO_SPEED_GEN_3
	}
}, {
	.oe_name = "NIC",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P5,
	.oe_lane = 3,
	.oe_nlanes = 1,
	.oe_tuning = {
		.ot_hw_limit = OXIO_SPEED_GEN_4,
		.ot_hw_target = OXIO_SPEED_GEN_3
	}

}, {
	.oe_name = "UBM P0",
	.oe_type = OXIO_ENGINE_T_UBM,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x10,
	.oe_hp_ubm = {
		.ohu_switch = { {
			.ois_type = OXIO_I2C_SWITCH_T_9546_48,
			.ois_addr = 0x72,
			.ois_select = 0
		}, {
			.ois_type = OXIO_I2C_SWITCH_T_9545,
			.ois_addr = 0x71,
			.ois_select = 0
		} },
		.ohu_gpio = {
			.oug_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.oug_addr = 0x21,
			.oug_ifdet_byte = 0,
			.oug_ifdet_bit = 0,
			.oug_reset_byte = 0,
			.oug_reset_bit = 1
		}
	}
}, {
	.oe_name = "UBM G0",
	.oe_type = OXIO_ENGINE_T_UBM,
	.oe_tile = OXIO_TILE_G0,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x14,
	.oe_hp_ubm = {
		.ohu_switch = { {
			.ois_type = OXIO_I2C_SWITCH_T_9546_48,
			.ois_addr = 0x72,
			.ois_select = 0
		}, {
			.ois_type = OXIO_I2C_SWITCH_T_9545,
			.ois_addr = 0x70,
			.ois_select = 0
		} },
		.ohu_gpio = {
			.oug_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.oug_addr = 0x20,
			.oug_ifdet_byte = 0,
			.oug_ifdet_bit = 0,
			.oug_reset_byte = 0,
			.oug_reset_bit = 1
		}
	}
}, {
	.oe_name = "UBM G1",
	.oe_type = OXIO_ENGINE_T_UBM,
	.oe_tile = OXIO_TILE_G1,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x18,
	.oe_hp_ubm = {
		.ohu_switch = { {
			.ois_type = OXIO_I2C_SWITCH_T_9546_48,
			.ois_addr = 0x72,
			.ois_select = 0
		}, {
			.ois_type = OXIO_I2C_SWITCH_T_9545,
			.ois_addr = 0x70,
			.ois_select = 1
		} },
		.ohu_gpio = {
			.oug_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.oug_addr = 0x20,
			.oug_ifdet_byte = 0,
			.oug_ifdet_bit = 2,
			.oug_reset_byte = 0,
			.oug_reset_bit = 3
		}
	}
}, {
	.oe_name = "UBM G2",
	.oe_type = OXIO_ENGINE_T_UBM,
	.oe_tile = OXIO_TILE_G2,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x1c,
	.oe_hp_ubm = {
		.ohu_switch = { {
			.ois_type = OXIO_I2C_SWITCH_T_9546_48,
			.ois_addr = 0x72,
			.ois_select = 0
		}, {
			.ois_type = OXIO_I2C_SWITCH_T_9545,
			.ois_addr = 0x70,
			.ois_select = 2
		} },
		.ohu_gpio = {
			.oug_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.oug_addr = 0x20,
			.oug_ifdet_byte = 0,
			.oug_ifdet_bit = 4,
			.oug_reset_byte = 0,
			.oug_reset_bit = 5
		}
	}
}, {
	.oe_name = "UBM G3",
	.oe_type = OXIO_ENGINE_T_UBM,
	.oe_tile = OXIO_TILE_G3,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x20,
	.oe_hp_ubm = {
		.ohu_switch = { {
			.ois_type = OXIO_I2C_SWITCH_T_9546_48,
			.ois_addr = 0x72,
			.ois_select = 0
		}, {
			.ois_type = OXIO_I2C_SWITCH_T_9545,
			.ois_addr = 0x70,
			.ois_select = 3
		} },
		.ohu_gpio = {
			.oug_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.oug_addr = 0x20,
			.oug_ifdet_byte = 0,
			.oug_ifdet_bit = 6,
			.oug_reset_byte = 0,
			.oug_reset_bit = 7
		}
	}
} };

const size_t oxio_ruby_nengines = ARRAY_SIZE(oxio_ruby);
