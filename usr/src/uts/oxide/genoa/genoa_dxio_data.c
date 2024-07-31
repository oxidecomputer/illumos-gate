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

/*
 * This file contains platform-specific data blobs that are required for
 * MPIO.
 *
 * The following table has the general mapping of logical ports and engines to
 * the corresponding lanes and other properties. This is currently valid for all
 * SP5 systems and the ports are ordered based on how hardware and the SMN
 * expect them.
 *
 * PORT	REV	PHYS	DXIO	1P BUS	2P BUS
 * P0	0	0x00	0x00	??	0x60,0xe0
 * G0	1	0x60	0x60	??	0x60,0x30
 * P1	0	0x20	0x20	??	0x40,0xc0
 * G1	1	0x40	0x40	??	0x40,0xc0
 * P2	1	0x30	0x30	??	0x00,0x80
 * G2	0	0x70	0x70	??	0x00,0x80
 * P3	1	0x10	0x10	??	0x20,0xa0
 * G3	0	0x50	0x50	??	0x20,0xa0
 * P4	0	0x84
 *
 * A core reversal is where the actual lanes are swapped in a way that might not
 * be expected here. Let's try and draw this out here. In the general case, the
 * physical lanes of a group which in the pin list are phrased as PORT[15:0],
 * e.g. G0_0N/P, G0_1N/P, ..., G0_15N/P. The following images first show the
 * normal mapping and then follow up with the reversed mapping.
 *
 *    +------+        +------+
 *    | Phys |        | dxio |		Therefore, in this case, a device that
 *    |  0   |        |  0   |		uses a set number of lanes, say the
 *    |  1   |        |  1   |		physical [3:0] uses the dxio [3:0].
 *    |  2   |        |  2   |		This is always the case regardless of
 *    |  3   |        |  3   |		whether or not the device is performing
 *    |  4   |        |  4   |		lane reversals or not.
 *    |  5   |        |  5   |
 *    |  6   |        |  6   |
 *    |  7   |------->|  7   |
 *    |  8   |        |  8   |
 *    |  9   |        |  9   |
 *    | 10   |        | 10   |
 *    | 11   |        | 11   |
 *    | 12   |        | 12   |
 *    | 13   |        | 13   |
 *    | 14   |        | 14   |
 *    | 15   |        | 15   |
 *    +------+        +------+
 *
 * However, when the core is reversed we instead see something like:
 *
 *    +------+        +------+
 *    | Phys |        | dxio |
 *    |  0   |        | 15   |		In the core reversal case we see that a
 *    |  1   |        | 14   |		device that would use physical lanes
 *    |  2   |        | 13   |		[3:0] is instead actually using [15:12].
 *    |  3   |        | 12   |		An important caveat here is that any
 *    |  4   |        | 11   |		device in this world must initially set
 *    |  5   |        | 10   |		the `zmlc_reverse` field in its DXIO
 *    |  6   |        |  9   |		configuration as the core itself is
 *    |  7   |------->|  8   |		reversed.
 *    |  8   |        |  7   |
 *    |  9   |        |  6   |		If instead, the device has actually
 *    | 10   |        |  5   |		reversed its lanes, then we do not need
 *    | 11   |        |  4   |		to set 'zmlc_reverse' as it cancels out.
 *    | 12   |        |  3   |
 *    | 13   |        |  2   |		Regardless, it's important to note the
 *    | 14   |        |  1   |		DXIO lane numbering is different here.
 *    | 15   |        |  0   |
 *    +------+        +------+
 *
 * There are broadly speaking two different types of data that we provide and
 * fill out:
 *
 * 1. Information that's used to program the various DXIO engines. This is
 *    basically responsible for conveying the type of ports (e.g. PCIe, SATA,
 *    etc.) and mapping those to various lanes. Eventually this'll then be
 *    mapped to a specific instance and bridge by the SMU and DXIO firmware.
 *
 * 2. We need to fill out a table that describes which ports are hotplug capable
 *    and how to find all of the i2c information that maps to this. An important
 *    caveat with this approach is that we assume that the DXIO firmware will
 *    map things to the same slot deterministically, given the same DXIO
 *    configuration. XXX should we move towards an interface where hp is
 *    specified in terms of lanes and then bridge/tile are filled in? XXX Or
 *    perhaps it's better for us to combine these.
 */

#include <sys/stddef.h>
#include <sys/debug.h>
#include <sys/pcie.h>
#include <sys/sysmacros.h>
#include <sys/io/genoa/mpio_impl.h>

#if 0
CTASSERT(sizeof (zen_mpio_link_cap_t) == 0x8);
CTASSERT(sizeof (zen_mpio_config_base_t) == 0x18);
CTASSERT(sizeof (zen_mpio_config_net_t) == 0x18);
CTASSERT(sizeof (zen_mpio_config_pcie_t) == 0x18);
CTASSERT(sizeof (zen_mpio_config_t) == 0x18);
CTASSERT(sizeof (zen_mpio_engine_t) == 0x28);
CTASSERT(offsetof(zen_mpio_engine_t, zme_config) == 0x8);
CTASSERT(sizeof (zen_mpio_platform_t) == 0x10);
#endif
CTASSERT(sizeof (zen_mpio_ask_t) < MMU_PAGESIZE);


CTASSERT(offsetof(genoa_pptable_t, ppt_plat_tdp_lim) == 0x14);
CTASSERT(offsetof(genoa_pptable_t, ppt_fan_override) == 0x24);
CTASSERT(offsetof(genoa_pptable_t, ppt_core_dldo_margin) == 0x30);
CTASSERT(offsetof(genoa_pptable_t, ppt_df_override) == 0x48);
CTASSERT(offsetof(genoa_pptable_t, ppt_xgmi_max_width_en) == 0x50);
CTASSERT(offsetof(genoa_pptable_t, ppt_i3c_sda_hold_tm) == 0x70);
CTASSERT(offsetof(genoa_pptable_t, ppt_oc_dis) == 0x80);
CTASSERT(offsetof(genoa_pptable_t, ppt_force_cclk_freq) == 0x84);
CTASSERT(offsetof(genoa_pptable_t, ppt_htf_temp_max) == 0x8c);
CTASSERT(offsetof(genoa_pptable_t, ppt_cppc_override) == 0x94);
CTASSERT(offsetof(genoa_pptable_t, ppt_cppc_thr_apicid_size) == 0x98);
CTASSERT(offsetof(genoa_pptable_t, ppt_cppc_thr_map) == 0x9c);
CTASSERT(offsetof(genoa_pptable_t, ppt_vddcr_cpu_volt_force) == 0x49c);
CTASSERT(offsetof(genoa_pptable_t, ppt_reserved) == 0x4b0);
CTASSERT(sizeof (genoa_pptable_t) == 0x520);

CTASSERT(sizeof (smu_hotplug_map_t) == 4);
CTASSERT(sizeof (smu_hotplug_function_t) == 4);
CTASSERT(sizeof (smu_hotplug_reset_t) == 4);
CTASSERT(sizeof (smu_hotplug_table_t) == 0x780);

#define	SP5_PHY_OFFSET_P0	0
#define	SP5_PHY_OFFSET_G0	96
#define	SP5_PHY_OFFSET_P1	32
#define	SP5_PHY_OFFSET_G1	64
#define	SP5_PHY_OFFSET_P2	48
#define	SP5_PHY_OFFSET_G2	112
#define	SP5_PHY_OFFSET_P3	16
#define	SP5_PHY_OFFSET_G3	80

/* XXX TODO: ASPM, seems to be set in config space */
/* XXX TODO: Target link speed? */
/* XXX TODO: Slot nums */
const zen_mpio_port_conf_t ruby_mpio_pcie_s0[] = {
    { /* P0, riser 0 */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 0,
		.zml_num_lanes = 8,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P0, riser 1 */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 8,
		.zml_num_lanes = 8,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P1 */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 32,
		.zml_num_lanes = 16,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P2 */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 48,
		.zml_num_lanes = 16,
		.zml_reversed = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P3 */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 16,
		.zml_num_lanes = 16,
		.zml_reversed = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P4 */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 128,
		.zml_num_lanes = 4,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_GEN4,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_GEN3,
		},
	    },
	},
    },
    { /* P5 M.2 (1/2) */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 132,
		.zml_num_lanes = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_GEN4,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_GEN3,
		},
	    },
	},
    },
    { /* P5 M.2 (2/2) */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 133,
		.zml_num_lanes = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_GEN4,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_GEN3,
		},
	    },
	},
    },
    { /* P5 NIC */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = 135,
		.zml_num_lanes = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_GEN4,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_GEN3,
		},
	    },
	},
    },
};

const size_t RUBY_MPIO_PCIE_S0_LEN = ARRAY_SIZE(ruby_mpio_pcie_s0);

/*
 * Ethanol-X hotplug data.
 */
const smu_hotplug_entry_t ruby_hotplug_ents[] = {
	/* NVMe Port 0 */
	{
	    .se_slotno = 8,
	    .se_map = {
		.shm_format = SMU_HP_ENTERPRISE_SSD,
		.shm_active = 1,
		/*
		 * XXX They claim this is Die ID 0, though it's on P1, roll with
		 * our gut.
		 */
		.shm_apu = 1,
		.shm_die_id = 1,
		.shm_port_id = 0,
		.shm_tile_id = SMU_TILE_P0,
		.shm_bridge = 0
	    },
	    .se_func = {
		.shf_i2c_bit = 1,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 8,
		.shf_i2c_dtype = 1,
		.shf_i2c_bus = 1,
		.shf_mask = 0
	    },
	},
	/* NVMe Port 1 */
	{
	    .se_slotno = 9,
	    .se_map = {
		.shm_format = SMU_HP_ENTERPRISE_SSD,
		.shm_active = 1,
		/*
		 * XXX They claim this is Die ID 0, though it's on P1, roll with
		 * our gut.
		 */
		.shm_apu = 1,
		.shm_die_id = 1,
		.shm_port_id = 1,
		.shm_tile_id = SMU_TILE_P0,
		.shm_bridge = 1
	    },
	    .se_func = {
		.shf_i2c_bit = 1,
		.shf_i2c_byte = 1,
		.shf_i2c_daddr = 8,
		.shf_i2c_dtype = 1,
		.shf_i2c_bus = 1,
		.shf_mask = 0
	    },
	},
	/* NVMe Port 2 */
	{
	    .se_slotno = 10,
	    .se_map = {
		.shm_format = SMU_HP_ENTERPRISE_SSD,
		.shm_active = 1,
		/*
		 * XXX They claim this is Die ID 0, though it's on P1, roll with
		 * our gut.
		 */
		.shm_apu = 1,
		.shm_die_id = 1,
		.shm_port_id = 2,
		.shm_tile_id = SMU_TILE_P0,
		.shm_bridge = 2
	    },
	    .se_func = {
		.shf_i2c_bit = 1,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 9,
		.shf_i2c_dtype = 1,
		.shf_i2c_bus = 1,
		.shf_mask = 0
	    },
	},
	/* NVMe Port 3 */
	{
	    .se_slotno = 11,
	    .se_map = {
		.shm_format = SMU_HP_ENTERPRISE_SSD,
		.shm_active = 1,
		/*
		 * XXX They claim this is Die ID 0, though it's on P1, roll with
		 * our gut.
		 */
		.shm_apu = 1,
		.shm_die_id = 1,
		.shm_port_id = 3,
		.shm_tile_id = SMU_TILE_P0,
		.shm_bridge = 3
	    },
	    .se_func = {
		.shf_i2c_bit = 1,
		.shf_i2c_byte = 1,
		.shf_i2c_daddr = 9,
		.shf_i2c_dtype = 1,
		.shf_i2c_bus = 1,
		.shf_mask = 0
	    },
	},
	/* PCIe x16 Slot 4 */
	{
	    .se_slotno = 4,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_active = 1,
		/*
		 * XXX Other sources suggest this should be apu/die 1, but it's
		 * P0
		 */
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0,
		.shm_tile_id = SMU_TILE_P2,
		.shm_bridge = 0
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 3,
		.shf_i2c_dtype = 1,
		.shf_i2c_bus = 7,
		.shf_mask = 0
	    },
	},
	{ .se_slotno = SMU_HOTPLUG_ENT_LAST }
};

/*
 * PCIe slot capabilities that determine what features the slot actually
 * supports.
 */
const uint32_t ruby_pcie_slot_cap_entssd =
    PCIE_SLOTCAP_HP_SURPRISE |
    PCIE_SLOTCAP_HP_CAPABLE |
    PCIE_SLOTCAP_NO_CMD_COMP_SUPP;

const uint32_t ruby_pcie_slot_cap_express =
    PCIE_SLOTCAP_ATTN_BUTTON |
    PCIE_SLOTCAP_POWER_CONTROLLER |
    PCIE_SLOTCAP_ATTN_INDICATOR |
    PCIE_SLOTCAP_PWR_INDICATOR |
    PCIE_SLOTCAP_HP_SURPRISE |
    PCIE_SLOTCAP_HP_CAPABLE |
    PCIE_SLOTCAP_EMI_LOCK_PRESENT;
