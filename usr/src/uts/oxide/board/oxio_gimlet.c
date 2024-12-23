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
 * Oxide DXIO Engines for Gimlet. These are organized as follows:
 *
 *  o x16 NIC
 *  o 2x x4 M.2
 *  o 10x x4 U.2
 *  o Sidecar
 *
 * A couple of notes on this:
 *
 *   o We do not want to constrain the link speed for any devices at this time.
 *   o The reversible setting comes from firmware information. It seems that G0,
 *     G1, P2, and P3 are considered reversed (this is zdlc_reverse), polarity
 *     reversals are elsewhere.
 *
 * The following table covers core information around a PCIe device, the port
 * it's on, the physical lanes and corresponding dxio lanes. The notes have the
 * following meanings:
 *
 *   o rev - lanes reversed. That is instead of device lane 0 being connected to
 *           SP3 logical lane 0, the opposite is true.
 *   o cr - indicates that the core internally has reversed the port.
 *   o tx - tx polarity swapped. In each lane N/P has been switched. PCIe should
 *          detect this automatically. It is here just for record purposes.
 *   o rx - rx polarity swapped. In each lane N/P has been switched. PCIe should
 *          detect this automatically. It is here just for record purposes.
 *
 * An important note on reversals. The underlying DXIO data will have the
 * 'zdlc_reverse' field set if one of rev or cr are set; however, if both of
 * these are set, then we do not set 'zdlc_reverse'. This is managed
 * transparently by the OXIO translation code. This should only indicate what
 * has been drawn on the schematic. Physical and DXIO lane translations are left
 * here for historical information.
 *
 * DEVICE	PORT	XP	PHYS		DXIO		NOTES
 * NIC		P1	0-15	0x20-0x2f	0x3a-0x49	-
 * M.2 0 (A)	P2	0-3	0x50-0x53	0x56-0x59	cr
 * M.2 1 (B)	P3	0-3	0x70-0x73	0x66-0x69	cr
 * U.2 0 (A)	G0	12-15	0x1c-0x1f	0x10-0x13	rev, tx, cr
 * U.2 1 (B)	G0	8-11	0x18-0x1b	0x14-0x17	rev, tx, cr
 * U.2 2 (C)	G0	4-7	0x14-0x17	0x18-0x1b	rev, tx, cr
 * U.2 3 (D)	G0	0-3	0x10-0x13	0x1c-0x1f	rev, tx, cr
 * U.2 4 (E)	G2	12-15	0x4c-0x4f	0x8e-0x91	rev, tx
 * U.2 5 (F)	G2	8-11	0x48-0x4b	0x8a-0x8d	rev, tx
 * U.2 6 (G)	G2	4-7	0x44-0x47	0x86-0x89	rev, tx
 * U.2 7 (H)	G3	8-11	0x68-0x6b	0x7a-0x7d	rev, tx
 * U.2 8 (I)	G3	4-7	0x64-0x67	0x76-0x79	rev, tx
 * U.2 9 (J)	G3	0-3	0x60-0x63	0x72-0x75	rev, tx
 * Sidecar	P0	0-3	0x00-0x03	0x2a-0x2d	-
 *
 * Entries in this table follow the same order as the table above. That is first
 * the NIC, then M.2 devices, SSDs, and finally the switch. We label slots
 * starting at 0. Physical slots 0-9 are the U.2 devices. The remaining slots go
 * from there. With that in mind, the following table is used to indicate which
 * i2c devices everything is on.
 *
 * DEVICE	PORT	TYPE	I2C/BYTE	TYPE	RESET/BYTE-bit	SLOT
 * NIC		P1	9535	0x25/0		9535	0x26/0-5	0x10
 * M.2 0 (A)	P2	9535	0x24/0		9535	0x26/0-7	0x11
 * M.2 1 (B)	P3	9535	0x24/1		9535	0x26/0-6	0x12
 * U.2 0 (A)	G0	9506	0x20/0		9535	0x22/0-7	0x00
 * U.2 1 (B)	G0	9506	0x20/2		9535	0x22/0-6	0x01
 * U.2 2 (C)	G0	9506	0x20/4		9535	0x22/0-5	0x02
 * U.2 3 (D)	G0	9506	0x20/1		9535	0x22/0-4	0x03
 * U.2 4 (E)	G2	9506	0x20/3		9535	0x22/0-3	0x04
 * U.2 5 (F)	G2	9506	0x21/0		9535	0x22/0-2	0x05
 * U.2 6 (G)	G2	9506	0x21/2		9535	0x22/0-1	0x06
 * U.2 7 (H)	G3	9506	0x21/4		9535	0x22/0-0	0x07
 * U.2 8 (I)	G3	9506	0x21/1		9535	0x22/1-7	0x08
 * U.2 9 (J)	G3	9506	0x21/3		9535	0x22/1-6	0x09
 * Sidecar	P0	9535	0x25/1		9535	0x26/0-4	0x13
 *
 */

#include <sys/io/zen/oxio.h>
#include <sys/sysmacros.h>

const oxio_engine_t oxio_gimlet[] = { {
	.oe_name = "T6",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P1,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x10,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x25,
			.otg_byte = 0
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x26,
			.otg_byte = 0,
			.otg_bit = 5

		},
		/*
		 * The T6 GPIO for physical presence is strapped low because
		 * this device is always on the board. However, we still want to
		 * make sure that this is visible this way to the operating
		 * system.
		 */
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT
	}
}, {
	.oe_name = "M.2 East",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P2,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x11,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x24,
			.otg_byte = 0
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x26,
			.otg_byte = 0,
			.otg_bit = 7

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "M.2 West",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P3,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x12,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x24,
			.otg_byte = 1
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x26,
			.otg_byte = 0,
			.otg_bit = 6

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N0 (A)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G0,
	.oe_lane = 12,
	.oe_nlanes = 4,
	.oe_slot = 0x0,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 0
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 7

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N1 (B)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G0,
	.oe_lane = 8,
	.oe_nlanes = 4,
	.oe_slot = 0x1,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 2
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 6

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N2 (C)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G0,
	.oe_lane = 4,
	.oe_nlanes = 4,
	.oe_slot = 0x2,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 4
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 5

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N3 (D)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G0,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x3,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 1
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 4

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N4 (E)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G2,
	.oe_lane = 12,
	.oe_nlanes = 4,
	.oe_slot = 0x4,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 3
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 3

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N5 (F)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G2,
	.oe_lane = 8,
	.oe_nlanes = 4,
	.oe_slot = 0x5,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 0
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 2

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N6 (G)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G2,
	.oe_lane = 4,
	.oe_nlanes = 4,
	.oe_slot = 0x6,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 2
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 1

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N7 (H)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G3,
	.oe_lane = 8,
	.oe_nlanes = 4,
	.oe_slot = 0x7,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 4
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 0,
			.otg_bit = 0

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N8 (I)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G3,
	.oe_lane = 4,
	.oe_nlanes = 4,
	.oe_slot = 0x8,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 1
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 1,
			.otg_bit = 7

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N9 (J)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G3,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x9,
	.oe_flags = OXIO_ENGINE_F_REVERSE,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 3
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x22,
			.otg_byte = 1,
			.otg_bit = 6

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "Backplane x4 (Sidecar)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x13,
	.oe_hp_flags = OXIO_HP_F_RESET_VALID,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x25,
			.otg_byte = 1
		},
		.ohp_reset = {
			.otg_exp_type = OXIO_I2C_GPIO_T_PCA9535,
			.otg_addr = 0x26,
			.otg_byte = 0,
			.otg_bit = 4

		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWRFLT
	}
} };

const size_t oxio_gimlet_nengines = ARRAY_SIZE(oxio_gimlet);
