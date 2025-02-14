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
 * Oxide DXIO Ethanol-X Declarations
 *
 * There are two sets of declarations. One for each socket, referred to as 's0'
 * and 's1' (we don't use p0/p1 to avoid confusion with the PCIe tiles).
 */

#include <sys/io/zen/oxio.h>
#include <sys/sysmacros.h>

const oxio_engine_t oxio_ethanolx_s0[] = { {
	.oe_name = "Slot 1 x16 (P0)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x1,
	.oe_flags = OXIO_ENGINE_F_REVERSE
}, {
	.oe_name = "Slot 3 x16 (P1)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P1,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x3,
	.oe_flags = OXIO_ENGINE_F_REVERSE
}, {
	.oe_name = "Slot 4 x16 (P2)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P2,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x4,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.otg_addr = 0x23,
			.otg_byte = 0
		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_PWRLED | OXIO_PCIE_CAP_EMIL |
		    OXIO_PCIE_CAP_EMILS | OXIO_PCIE_CAP_ATTNSW
	}

}, {
	.oe_name = "Slot 2 x16 (P2)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_tile = OXIO_TILE_P1,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x2,
	.oe_flags = OXIO_ENGINE_F_REVERSE
} };

/*
 * Socket 1 contains an 8 PHY SATA engine on P1, 4 x4 NVMe devices on P0. The
 * other lanes are unused. The hotplug support for the NVMe devices is driven by
 * an MG9088. The values for the bit and byte selects have been cargo-culted and
 * are somewhat suspect and are left here to help exercise things. In
 * particular, we've always seen a value of 0x1 for the 'I2CGpioBitSelector' in
 * AMD sources, but that is supposed to be masked out, so we change it to 0.
 * Similarly, we've never seen a function mask set, so for now we don't set it.
 */
const oxio_engine_t oxio_ethanolx_s1[] = { {
	.oe_name = "NVMe 0",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_ENTSSD,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x8,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_switch = {
				.ois_type = OXIO_I2C_SWITCH_T_9545,
				.ois_addr = 0x70,
				.ois_select = 1
			},
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.otg_addr = 0x20,
			.otg_byte = 0,
			.otg_bit = 0
		},
		.ohp_cap = 0

	}
}, {
	.oe_name = "NVMe 1",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_ENTSSD,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 4,
	.oe_nlanes = 4,
	.oe_slot = 0x9,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_switch = {
				.ois_type = OXIO_I2C_SWITCH_T_9545,
				.ois_addr = 0x70,
				.ois_select = 1
			},
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.otg_addr = 0x20,
			.otg_byte = 1,
			.otg_bit = 0
		},
		.ohp_cap = 0
	}
}, {
	.oe_name = "NVMe 2",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_ENTSSD,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 8,
	.oe_nlanes = 4,
	.oe_slot = 0xa,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_switch = {
				.ois_type = OXIO_I2C_SWITCH_T_9545,
				.ois_addr = 0x70,
				.ois_select = 1
			},
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.otg_addr = 0x21,
			.otg_byte = 0,
			.otg_bit = 0
		},
		.ohp_cap = 0
	}
}, {
	.oe_name = "NVMe 3",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_ENTSSD,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 12,
	.oe_nlanes = 4,
	.oe_slot = 0xb,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_switch = {
				.ois_type = OXIO_I2C_SWITCH_T_9545,
				.ois_addr = 0x70,
				.ois_select = 1
			},
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9535,
			.otg_addr = 0x21,
			.otg_byte = 1,
			.otg_bit = 0
		},
		.ohp_cap = 0
	}
} };

const size_t oxio_ethanolx_s0_nengines = ARRAY_SIZE(oxio_ethanolx_s0);
const size_t oxio_ethanolx_s1_nengines = ARRAY_SIZE(oxio_ethanolx_s1);
