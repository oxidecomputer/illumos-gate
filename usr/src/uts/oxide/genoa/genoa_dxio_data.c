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
 * Copyright 2023 Oxide Computer Company
 */

/*
 * This file contains platform-specific data blobs that are required for the
 * DXIO engine.
 *
 * The following table has the general mapping of logical ports and engines to
 * the corresponding lanes and other properties. This is currently valid for all
 * SP3 systems and the ports are ordered based on how hardware and the SMN
 * expect them.
 *
 * PORT	REV	PHYS	DXIO	1P BUS	2P BUS
 * G0	1	0x10	0x10	0xc0	0x60,0x30
 * P0	0	0x00	0x2a	0xc0	0x60,0xe0
 * P1	0	0x20	0x3a	0x80	0x40,0xc0
 * G1	1	0x30	0x00	0x80	0x40,0xc0
 * G3	0	0x60	0x72	0x40	0x20,0xa0
 * P3	1	0x70	0x5a	0x40	0x20,0xa0
 * P2	1	0x50	0x4a	0x00	0x00,0x80
 * G2	0	0x40	0x82	0x00	0x00,0x80
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
 *    |  5   |        | 10   |		the `zdlc_reverse` field in its DXIO
 *    |  6   |        |  9   |		configuration as the core itself is
 *    |  7   |------->|  8   |		reversed.
 *    |  8   |        |  7   |
 *    |  9   |        |  6   |		If instead, the device has actually
 *    | 10   |        |  5   |		reversed its lanes, then we do not need
 *    | 11   |        |  4   |		to set 'zdlc_reverse' as it cancels out.
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
#include <sys/io/milan/dxio_impl.h>

CTASSERT(sizeof (zen_dxio_link_cap_t) == 0x8);
CTASSERT(sizeof (zen_dxio_config_base_t) == 0x18);
CTASSERT(sizeof (zen_dxio_config_net_t) == 0x18);
CTASSERT(sizeof (zen_dxio_config_pcie_t) == 0x18);
CTASSERT(sizeof (zen_dxio_config_t) == 0x18);
CTASSERT(sizeof (zen_dxio_engine_t) == 0x28);
CTASSERT(offsetof(zen_dxio_engine_t, zde_config) == 0x8);
CTASSERT(sizeof (zen_dxio_platform_t) == 0x10);

CTASSERT(offsetof(milan_pptable_t, ppt_plat_tdp_lim) == 0x14);
CTASSERT(offsetof(milan_pptable_t, ppt_fan_override) == 0x24);
CTASSERT(offsetof(milan_pptable_t, ppt_core_dldo_margin) == 0x30);
CTASSERT(offsetof(milan_pptable_t, ppt_df_override) == 0x48);
CTASSERT(offsetof(milan_pptable_t, ppt_xgmi_max_width_en) == 0x50);
CTASSERT(offsetof(milan_pptable_t, ppt_cpu_full_scale) == 0x58);
CTASSERT(offsetof(milan_pptable_t, ppt_oc_dis) == 0x68);
CTASSERT(offsetof(milan_pptable_t, ppt_cclk_freq) == 0x6c);
CTASSERT(offsetof(milan_pptable_t, ppt_htf_temp_max) == 0x74);
CTASSERT(offsetof(milan_pptable_t, ppt_ccp_override) == 0x7c);
CTASSERT(offsetof(milan_pptable_t, ppt_ccp_thr_apic_size) == 0x80);
CTASSERT(offsetof(milan_pptable_t, ppt_ccp_thr_map) == 0x84);
CTASSERT(offsetof(milan_pptable_t, ppt_vddcr_cpu_force) == 0x284);
CTASSERT(offsetof(milan_pptable_t, ppt_reserved) == 0x294);
CTASSERT(sizeof (milan_pptable_t) == 0x304);

CTASSERT(sizeof (smu_hotplug_map_t) == 4);
CTASSERT(sizeof (smu_hotplug_function_t) == 4);
CTASSERT(sizeof (smu_hotplug_reset_t) == 4);
CTASSERT(sizeof (smu_hotplug_table_t) == 0x480);

/*
 * This structure contains the data table which we use to define the engine data
 * for an AMD Ethanol-X platform. Each socket has its own chunk of data because
 * they are totally different.
 */
const zen_dxio_platform_t ethanolx_engine_s0 = {
    .zdp_type = DXIO_PLATFORM_EPYC, .zdp_nengines = 4, .zdp_engines = {
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x2a, .zde_end_lane = 0x39, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x3a, .zde_end_lane = 0x49, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x4a, .zde_end_lane = 0x59, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		/* No reversing here */
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x5a, .zde_end_lane = 0x69, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		/* No reversing here */
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	} }
};

/*
 * XXX There is a bunchof ancillary data for SATA by default. Trying to stay
 * laser focused on the objective and thus skipping it since we don't really
 * care about SATA.
 */
const zen_dxio_platform_t ethanolx_engine_s1 = {
    .zdp_type = DXIO_PLATFORM_EPYC, .zdp_nengines = 5, .zdp_engines = {
	{ .zde_type = DXIO_ENGINE_SATA, .zde_hp = 0,
	    .zde_start_lane = 0x3a, .zde_end_lane = 0x41, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_base = { .zdcb_chan_type = 0, .zdcb_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x2a, .zde_end_lane = 0x2d, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		/* This socket isn't reversing. Don't ask me why. */
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_ENT_SSD,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x2e, .zde_end_lane = 0x31, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		/* This socket isn't reversing. Don't ask me why. */
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_ENT_SSD,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x32, .zde_end_lane = 0x35, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		/* This socket isn't reversing. Don't ask me why. */
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_ENT_SSD,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	},
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x36, .zde_end_lane = 0x39, .zde_gpio_group = 1,
	    .zde_reset_group = 1, .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		/* This socket isn't reversing. Don't ask me why. */
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_ENT_SSD,
		/* XXX Next two always seems to be set */
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		/* XXX This pair is always overriden */
		.zdlc_eq_override = 1,
		.zdlc_eq_mode = 3,
		/* XXX Trust the gods */
		.zdlc_invert_rx_pol = 0x1,
	    } } }
	} }
};

/*
 * Ethanol-X hotplug data.
 */
const smu_hotplug_entry_t ethanolx_hotplug_ents[] = {
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
const uint32_t ethanolx_pcie_slot_cap_entssd =
    PCIE_SLOTCAP_HP_SURPRISE | PCIE_SLOTCAP_HP_CAPABLE |
    PCIE_SLOTCAP_NO_CMD_COMP_SUPP;
const uint32_t ethanolx_pcie_slot_cap_express =
    PCIE_SLOTCAP_ATTN_BUTTON | PCIE_SLOTCAP_POWER_CONTROLLER |
    PCIE_SLOTCAP_ATTN_INDICATOR | PCIE_SLOTCAP_PWR_INDICATOR |
    PCIE_SLOTCAP_HP_SURPRISE | PCIE_SLOTCAP_HP_CAPABLE |
    PCIE_SLOTCAP_EMI_LOCK_PRESENT;

/*
 * Engines for Gimlet. These are organized as follows:
 *
 *  o x16 NIC
 *  o 2x x4 M.2
 *  o 10x x4 U.2
 *  o Sidecar
 *
 * A couple of notes on this:
 *
 *   o We do not want to constrain the link speed for any devices at this time.
 *   o The GPIO and reset groups that we specify are our own internal indicators
 *     that it should be skipped as all this functionality is delivered by the
 *     expander network.
 *   o Lanes here are always based in terms of the dxio lanes and not the
 *     physical lanes that we see in a schematic or in hotplug.
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
 *   o tx - tx polarity swapped. In each lane N/P has been switched. The
 *          'zdlc_invert_tx_pol' bit must be set as a result.
 *   o rx - rx polarity swapped. In each lane N/P has been switched. The
 *          'zdlc_invert_rx_pol' bit must be set as a result.
 *
 * An important note on reversals. The value 'zdlc_reverse' must be set if one
 * of rev or cr are set; however, if both of these are set, then we do not set
 * 'zdlc_reverse'.
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
 * A few additional notes, it seems that the expectation is that we set the
 * default equalization override.
 */
const zen_dxio_platform_t gimlet_engine = {
    .zdp_type = DXIO_PLATFORM_EPYC, .zdp_nengines = 14, .zdp_engines = {
	/* NIC x16 */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x3a, .zde_end_lane = 0x49,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 0,
	    } } }
	},
	/* M.2 A */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x56, .zde_end_lane = 0x59,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 0,
	    } } }
	},
	/* M.2 B */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x66, .zde_end_lane = 0x69,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 0,
	    } } }
	},
	/* U.2 0 (A) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x10, .zde_end_lane = 0x13,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},

	/* U.2 1 (B) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x14, .zde_end_lane = 0x17,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 2 (C) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x18, .zde_end_lane = 0x1b,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 3 (D) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x1c, .zde_end_lane = 0x1f,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 4 (E) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x8e, .zde_end_lane = 0x91,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 5 (F) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x8a, .zde_end_lane = 0x8d,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 6 (G) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x86, .zde_end_lane = 0x89,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 7 (H) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x7a, .zde_end_lane = 0x7d,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 8 (I) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x76, .zde_end_lane = 0x79,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* U.2 9 (J) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x72, .zde_end_lane = 0x75,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 1,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 1,
	    } } }
	},
	/* Sidecar (x4) */
	{ .zde_type = DXIO_ENGINE_PCIE, .zde_hp = 0,
	    .zde_start_lane = 0x2a, .zde_end_lane = 0x2d,
	    .zde_gpio_group = DXIO_GROUP_UNUSED,
	    .zde_reset_group = DXIO_GROUP_UNUSED,
	    .zde_search_depth = 0, .zde_kpnp_reset = 0,
	    .zde_config = { .zdc_pcie = { .zdcp_caps = {
		.zdlc_present = DXIO_PORT_PRESENT,
		.zdlc_early_train = 0,
		.zdlc_comp_mode = 0,
		.zdlc_reverse = 0,
		.zdlc_max_speed = DXIO_LINK_SPEED_MAX,
		.zdlc_hp = DXIO_HOTPLUG_T_EXPRESS_MODULE,
		.zdlc_en_off_config = 1,
		.zdlc_off_unused = 1,
		.zdlc_eq_mode = 3,
		.zdlc_eq_override = 1,
		.zdlc_invert_rx_pol = 0,
		.zdlc_invert_tx_pol = 0,
	    } } }
	} }
};

/*
 * Entires in this table follow the same order as the table above. That is first
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
 */
const smu_hotplug_entry_t gimlet_hotplug_ents[] = {
	/* NIC */
	{
	    .se_slotno = 0x10,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x0,
		.shm_tile_id = SMU_TILE_P1,
		.shm_bridge = 0x0
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 0x5,
		.shf_i2c_dtype = SMU_I2C_PCA9535,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_ATTNLED |
		    SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x6,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 5,
	    }
	},
	/* M.2 0 (A) */
	{
	    .se_slotno = 0x11,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x2,
		.shm_tile_id = SMU_TILE_P2,
		.shm_bridge = 0x2
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 0x4,
		.shf_i2c_dtype = SMU_I2C_PCA9535,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_ATTNLED |
		    SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x6,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 7,
	    }
	},
	/* M.2 1 (B) */
	{
	    .se_slotno = 0x12,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x2,
		.shm_tile_id = SMU_TILE_P3,
		.shm_bridge = 0x2
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 1,
		.shf_i2c_daddr = 0x4,
		.shf_i2c_dtype = SMU_I2C_PCA9535,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_ATTNLED |
		    SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x6,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 6,
	    }
	},
	/* U.2 0 (A) */
	{
	    .se_slotno = 0x0,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x0,
		.shm_tile_id = SMU_TILE_G0,
		.shm_bridge = 0x0
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 0x0,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 7,
	    }
	},
	/* U.2 1 (B) */
	{
	    .se_slotno = 0x1,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x1,
		.shm_tile_id = SMU_TILE_G0,
		.shm_bridge = 0x1
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 2,
		.shf_i2c_daddr = 0x0,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 6,
	    }
	},
	/* U.2 2 (C) */
	{
	    .se_slotno = 0x2,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x2,
		.shm_tile_id = SMU_TILE_G0,
		.shm_bridge = 0x2
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 4,
		.shf_i2c_daddr = 0x0,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 5,
	    }
	},
	/* U.2 3 (D) */
	{
	    .se_slotno = 0x3,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x3,
		.shm_tile_id = SMU_TILE_G0,
		.shm_bridge = 0x3
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 1,
		.shf_i2c_daddr = 0x0,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 4,
	    }
	},
	/* U.2 4 (E) */
	{
	    .se_slotno = 0x4,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x3,
		.shm_tile_id = SMU_TILE_G2,
		.shm_bridge = 0x3
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 3,
		.shf_i2c_daddr = 0x0,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 3,
	    }
	},
	/* U.2 5 (F) */
	{
	    .se_slotno = 0x5,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x2,
		.shm_tile_id = SMU_TILE_G2,
		.shm_bridge = 0x2
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 0,
		.shf_i2c_daddr = 0x1,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 2,
	    }
	},
	/* U.2 6 (G) */
	{
	    .se_slotno = 0x6,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x1,
		.shm_tile_id = SMU_TILE_G2,
		.shm_bridge = 0x1
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 2,
		.shf_i2c_daddr = 0x1,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 1,
	    }
	},
	/* U.2 7 (H) */
	{
	    .se_slotno = 0x7,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x2,
		.shm_tile_id = SMU_TILE_G3,
		.shm_bridge = 0x2
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 4,
		.shf_i2c_daddr = 0x1,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 0,
	    }
	},
	/* U.2 8 (I) */
	{
	    .se_slotno = 0x8,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x1,
		.shm_tile_id = SMU_TILE_G3,
		.shm_bridge = 0x1
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 1,
		.shf_i2c_daddr = 0x1,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 1,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 7,
	    }
	},
	/* U.2 9 (J) */
	{
	    .se_slotno = 0x9,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x0,
		.shm_tile_id = SMU_TILE_G3,
		.shm_bridge = 0x0
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 3,
		.shf_i2c_daddr = 0x1,
		.shf_i2c_dtype = SMU_I2C_PCA9506,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_PWRLED | SMU_ENTA_EMIL
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 1,
		.shr_i2c_daddr = 0x2,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 6,
	    }
	},
	/* Sidecar */
	{
	    .se_slotno = 0x13,
	    .se_map = {
		.shm_format = SMU_HP_EXPRESS_MODULE_A,
		.shm_rst_valid = 1,
		.shm_active = 1,
		.shm_apu = 0,
		.shm_die_id = 0,
		.shm_port_id = 0x1,
		.shm_tile_id = SMU_TILE_P0,
		.shm_bridge = 0x1
	    },
	    .se_func = {
		.shf_i2c_bit = 0,
		.shf_i2c_byte = 1,
		.shf_i2c_daddr = 0x5,
		.shf_i2c_dtype = SMU_I2C_PCA9535,
		.shf_i2c_bus = SMU_I2C_DIRECT,
		.shf_mask = SMU_ENTA_ATTNSW | SMU_ENTA_ATTNLED |
		    SMU_ENTA_PWRLED | SMU_ENTA_EMIL | SMU_ENTA_PWREN
	    },
	    .se_reset = {
		.shr_i2c_gpio_byte = 0,
		.shr_i2c_daddr = 0x6,
		.shr_i2c_dtype = SMU_I2C_PCA9535,
		.shr_i2c_bus = SMU_I2C_DIRECT,
		.shr_i2c_reset = 1 << 4,
	    }
	},
	{ .se_slotno = SMU_HOTPLUG_ENT_LAST }
};
