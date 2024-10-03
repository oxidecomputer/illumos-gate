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
#include <sys/io/zen/dxio_data.h>
#include <sys/io/zen/mpio_impl.h>

CTASSERT(sizeof (zen_mpio_ask_t) < MMU_PAGESIZE);


#define	SP5_PHY_OFFSET_P0	0
#define	SP5_PHY_OFFSET_G0	96
#define	SP5_PHY_OFFSET_P1	32
#define	SP5_PHY_OFFSET_G1	64
#define	SP5_PHY_OFFSET_P2	48
#define	SP5_PHY_OFFSET_G2	112
#define	SP5_PHY_OFFSET_P3	16
#define	SP5_PHY_OFFSET_G3	80

/* BEGIN CSTYLED */
const zen_mpio_port_conf_t ruby_mpio_pcie_s0[] = {
    { /* P1, "Ruby Riser 1" */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = SP5_PHY_OFFSET_P1,
		.zml_num_lanes = 16,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P0_1, "Ruby Riser 2" */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = SP5_PHY_OFFSET_P0 + 8,
		.zml_num_lanes = 8,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P3, "Ruby Riser 3" */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = SP5_PHY_OFFSET_P3,
		.zml_num_lanes = 16,
		.zml_reversed = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P2, "OCP 3.0" */
	.zmpc_ask = {
	    .zma_link = {
		.zml_lane_start = SP5_PHY_OFFSET_P2,
		.zml_num_lanes = 16,
		.zml_reversed = 1,
		.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE,
		.zml_gpio_id = 1,
		.zml_attrs = {
		    .zmla_port_present = 1,
		    .zmla_max_link_speed_cap = ZEN_MPIO_LINK_SPEED_MAX,
		    .zmla_target_link_speed = ZEN_MPIO_LINK_SPEED_MAX,
		},
	    },
	},
    },
    { /* P4, M.2 x4 */
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
    { /* P5, M.2 (1/2) */
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
    { /* P5, M.2 (2/2) */
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
    { /* P5, NIC */
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
/* END CSTYLED */

size_t RUBY_MPIO_PCIE_S0_LEN = ARRAY_SIZE(ruby_mpio_pcie_s0);

/* BEGIN CSTYLED */
const zen_mpio_ubm_hfc_port_t ruby_mpio_hfc_ports_full_nvme[] = {
    { /* P0 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x21,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 0,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 0,
	    .zmud_i2c_reset_bitno = 1,
	    .zmud_slot_num = 0x10,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x71,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G0 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 96,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 0,
	    .zmud_i2c_reset_bitno = 1,
	    .zmud_slot_num = 0x14,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G1 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 64,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 2,
	    .zmud_i2c_reset_bitno = 3,
	    .zmud_slot_num = 0x18,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 1,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G2 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 112,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 4,
	    .zmud_i2c_reset_bitno = 5,
	    .zmud_slot_num = 0x1c,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 2,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* P3 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x21,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 16,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 2,
	    .zmud_i2c_reset_bitno = 3,
	    .zmud_slot_num = 0x20,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x71,
		.zmis_select = 1,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G3 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 80,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 6,
	    .zmud_i2c_reset_bitno = 7,
	    .zmud_slot_num = 0x24,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 3,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
};

const zen_mpio_ubm_hfc_port_t ruby_mpio_hfc_ports[] = {
    { /* P0 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x21,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 0,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 0,
	    .zmud_i2c_reset_bitno = 1,
	    .zmud_slot_num = 0x10,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x71,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G0 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 96,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 0,
	    .zmud_i2c_reset_bitno = 1,
	    .zmud_slot_num = 0x14,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G1 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 64,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 2,
	    .zmud_i2c_reset_bitno = 3,
	    .zmud_slot_num = 0x18,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 1,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G2 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 112,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 4,
	    .zmud_i2c_reset_bitno = 5,
	    .zmud_slot_num = 0x1c,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 2,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G3 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 80,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 6,
	    .zmud_i2c_reset_bitno = 7,
	    .zmud_slot_num = 0x20,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 3,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
};

const zen_mpio_ubm_hfc_port_t ruby_mpio_hfc_ports_standard_nvme[] = {
    { /* G0 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 96,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 0,
	    .zmud_i2c_reset_bitno = 1,
	    .zmud_slot_num = 0x10,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G1 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 64,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 2,
	    .zmud_i2c_reset_bitno = 3,
	    .zmud_slot_num = 0x14,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 1,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G2 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 112,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 4,
	    .zmud_i2c_reset_bitno = 5,
	    .zmud_slot_num = 0x18,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 2,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G3 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 80,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 6,
	    .zmud_i2c_reset_bitno = 7,
	    .zmud_slot_num = 0x1c,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 3,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
};

const zen_mpio_ubm_hfc_port_t ruby_mpio_hfc_ports_full_sata[] = {
    { /* P0 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x21,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 0,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 0,
	    .zmud_i2c_reset_bitno = 1,
	    .zmud_slot_num = 0,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x71,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G3 0-7 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x20,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 80,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 6,
	    .zmud_i2c_reset_bitno = 7,
	    .zmud_slot_num = 0,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x70,
		.zmis_select = 3,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
    { /* G3 8-15 */
	.zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM,
	.zmuhp_expander = {
	    .zmie_addr = 0x21,
	    .zmie_type = SMU_I2C_PCA9535,
	    .zmie_clear_intrs = 0,
	},
	.zmuhp_start_lane = 88,
	.zmuhp_ubm_device = {
	    .zmud_bp_type_bitno = 4,
	    .zmud_i2c_reset_bitno = 5,
	    .zmud_slot_num = 0,
	},
	.zmuhp_i2c_switch = {
	    {
		.zmis_addr = 0x72,
		.zmis_select = 0,
		.zmis_type = SMU_GPIO_SW_9546_48,
	    },
	    {
		.zmis_addr = 0x71,
		.zmis_select = 2,
		.zmis_type = SMU_GPIO_SW_9545,
	    },
	},
    },
};
/* END CSTYLED */

const size_t RUBY_MPIO_UBM_HFC_DESCR_NPORTS = ARRAY_SIZE(ruby_mpio_hfc_ports);

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
