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
 * Oxide DXIO Engines for Cosmo. These are organized as follows:
 *
 *  o x16 NIC
 *  o 2x x4 M.2
 *  o 10x x4 U.2
 *  o Sidecar
 *  o MCIO connector
 *
 * A couple of notes on this:
 *
 *   o We do not want to constrain the link speed for most devices at this time.
 *     The exceptions are the T6 NIC when in manufacturing mode (see note
 *     below), and the backplane link to the sidecar.
 *   o The reversible setting comes from firmware information. It seems that G0,
 *     G1, P2, and P3 are considered reversed (this is zdlc_reverse), polarity
 *     reversals are elsewhere.
 *   o The MCIO connector is not currently included in the DXIO data.
 *
 * The following table covers core information around a PCIe device, the port
 * it's on, the physical lanes and corresponding dxio lanes. The notes have the
 * following meanings:
 *
 *   o rev - lanes reversed. That is instead of device lane 0 being connected to
 *           SP5 logical lane 0, the opposite is true.
 *   o cr - indicates that the core internally has reversed the port.
 *   o tx - tx polarity swapped. In each lane N/P has been switched. PCIe should
 *          detect this automatically. It is here just for record purposes. The
 *          numbers in parenthesis show which lanes are switched where it is
 *          not all of them.
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
 * DEVICE	PORT	XP	PHYS		NOTES
 * NIC		P1	0-15	0x20-0x2f	rx
 * M.2 E (A)	P2	0-3	0x30-0x33	cr, rx(0,1,2), tx(3)
 * M.2 W (B)	P3	0-3	0x10-0x13	cr, rx(1,2,3), tx(0)
 * U.2 0 (A)	G0	12-15	0x6c-0x6f	cr, rx
 * U.2 1 (B)	G0	8-11	0x68-0x6b	cr, rx
 * U.2 2 (C)	G0	4-7	0x64-0x67	cr, rx
 * U.2 3 (D)	G0	0-3	0x60-0x63	cr, rx
 * U.2 4 (E)	G2	8-11	0x78-0x7b	rx
 * U.2 5 (F)	G2	4-7	0x74-0x77	rx
 * U.2 6 (G)	G3	8-11	0x58-0x5b	rx
 * U.2 7 (H)	G3	4-7	0x54-0x57	rx
 * U.2 8 (I)	G3	0-3	0x50-0x53	rx
 * U.2 9 (J)	G2	0-3	0x70-0x73	rx
 * Sidecar	P0	0-3	0x00-0x03	rx(0,2), tx(1,2,3)
 * MCIO		G1	0-15	0x40-0x4f	cr
 *
 * Entries in this table follow the same order as the table above. That is first
 * the NIC, then M.2 devices, SSDs, and finally the switch. A slot ID of zero
 * is reserved in PCIe for on-board or root-complex-integrated slots and
 * carries various assumptions; we don't use it. Physical slots 0x20-0x29 are
 * the U.2 devices, the remaining slots start at 0x10. With that in mind, the
 * following table is used to indicate which i2c devices everything is on.
 * Unlike Gimlet, there are no I/O expanders for PERST which is controlled
 * entirely by the FPGA.
 *
 * DEVICE	PORT	TYPE	I2C/BYTE	SLOT
 * NIC		P1	9506	0x22/2		0x10
 * M.2 E (A)	P2	9506	0x22/0		0x11
 * M.2 W (B)	P3	9506	0x22/1		0x12
 * U.2 0 (A)	G0	9506	0x20/0		0x20
 * U.2 1 (B)	G0	9506	0x20/1		0x21
 * U.2 2 (C)	G0	9506	0x20/2		0x22
 * U.2 3 (D)	G0	9506	0x20/3		0x23
 * U.2 4 (E)	G2	9506	0x20/4		0x24
 * U.2 5 (F)	G2	9506	0x21/0		0x25
 * U.2 6 (G)	G3	9506	0x21/1		0x26
 * U.2 7 (H)	G3	9506	0x21/2		0x27
 * U.2 8 (I)	G3	9506	0x21/3		0x28
 * U.2 9 (J)	G2	9506	0x21/4		0x29
 * Sidecar	P0	9506	0x22/3		0x13
 * MCIO		G1				0x14
 */

#include <sys/io/zen/oxio.h>
#include <sys/sysmacros.h>

const oxio_engine_t oxio_cosmo[] = { {
	.oe_name = "T6",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P1,
	.oe_lane = 0,
	.oe_nlanes = 16,
	.oe_slot = 0x10,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x22,
			.otg_byte = 2
		},
		/*
		 * The T6 GPIO for physical presence is strapped low because
		 * this device is always on the board. However, we still want to
		 * make sure that this is visible this way to the operating
		 * system.
		 */
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT
	},
	.oe_tuning = {
		/*
		 * On Cosmo, we control whether the T6 enters manufacturing
		 * mode or mission mode (what one would normally expect) based
		 * on a GPIO. This GPIO is strapped on the board to enter
		 * manufacturing mode by default, which limits the device to
		 * PCIe Gen 2 operation.
		 *
		 * For various reasons, we have seen issues while trying to
		 * perform initial training to PCIe Gen 2. In particular, while
		 * this is successfully negotiated and we see the SoC enter a
		 * Recovery.Speed in the PCIe LTSSM, it fails to leave the
		 * subsequent Recovery.Config and then enters Compliance mode.
		 * We've observed that by limiting the bridge to PCIe Gen 1
		 * behavior, that we will always successfully train the link
		 * initially. This setting applies an initial constraint on the
		 * bridge that will be lifted by the t6init service when it
		 * transitions to mission mode via a pcieb driver ioctl.
		 */
		.ot_log_limit = OXIO_SPEED_GEN_1
	}
}, {
	.oe_name = "M.2 East",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P2,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x11,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x22,
			.otg_byte = 0
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
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x22,
			.otg_byte = 1,
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
	.oe_slot = 0x20,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 0,
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
	.oe_slot = 0x21,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 1
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
	.oe_slot = 0x22,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 2,
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
	.oe_slot = 0x23,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 3,
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
	.oe_lane = 8,
	.oe_nlanes = 4,
	.oe_slot = 0x24,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x20,
			.otg_byte = 4,
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
	.oe_lane = 4,
	.oe_nlanes = 4,
	.oe_slot = 0x25,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 0
		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N6 (G)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G3,
	.oe_lane = 8,
	.oe_nlanes = 4,
	.oe_slot = 0x26,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 1,
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
	.oe_lane = 4,
	.oe_nlanes = 4,
	.oe_slot = 0x27,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 2,
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
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x28,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 3,
		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "U.2 N9 (J)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_G2,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x29,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x21,
			.otg_byte = 4
		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWREN |
		    OXIO_PCIE_CAP_PWRFLT | OXIO_PCIE_CAP_ATTNLED |
		    OXIO_PCIE_CAP_EMILS
	}
}, {
	.oe_name = "Backplane (Switch)",
	.oe_type = OXIO_ENGINE_T_PCIE,
	.oe_hp_type = OXIO_HOTPLUG_T_EXP_A,
	.oe_tile = OXIO_TILE_P0,
	.oe_lane = 0,
	.oe_nlanes = 4,
	.oe_slot = 0x13,
	.oe_hp_flags = 0,
	.oe_hp_trad = {
		.ohp_dev = {
			.otg_exp_type = OXIO_I2C_GPIO_EXP_T_PCA9506,
			.otg_addr = 0x22,
			.otg_byte = 3
		},
		.ohp_cap = OXIO_PCIE_CAP_OOB_PRSNT | OXIO_PCIE_CAP_PWRFLT
	},
	.oe_tuning = {
		/*
		 * The backplane link is unlikely to have enough margin to
		 * operate at Gen5, and doesn't need to, so limit to Gen4.
		 */
		.ot_hw_limit = OXIO_SPEED_GEN_4
	}
} };

const size_t oxio_cosmo_nengines = ARRAY_SIZE(oxio_cosmo);
