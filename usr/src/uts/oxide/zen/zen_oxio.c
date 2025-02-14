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
 * This file exists to perform translations between the OXIO data engine format
 * and the corresponding data across all platforms. Currently a few
 * Milan-specific items are in a Milan-specific OXIO file as they rely on
 * Milan-specific data structures or PCIe constants.
 */

#include <sys/stdbool.h>
#include <sys/pcie.h>
#include <sys/io/zen/hotplug.h>
#include <sys/io/zen/oxio.h>
#include <sys/io/zen/mpio_impl.h>
#include <sys/io/zen/platform_impl.h>

typedef struct oxio_lane_map {
	oxio_tile_t		lm_tile;
	uint32_t		lm_dxio;
	uint8_t			lm_nlanes;
	bool			lm_rev;
} oxio_lane_map_t;

static const oxio_lane_map_t sp3_lane_map[] = { {
	.lm_tile = OXIO_TILE_G0,
	.lm_dxio = 0x10,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_P0,
	.lm_dxio = 0x2a,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_P1,
	.lm_dxio = 0x3a,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_G1,
	.lm_dxio = 0x00,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_G3,
	.lm_dxio = 0x72,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_P3,
	.lm_dxio = 0x5a,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_P2,
	.lm_dxio = 0x4a,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_G2,
	.lm_dxio = 0x82,
	.lm_nlanes = 0x10,
	.lm_rev = false
} };

static const oxio_lane_map_t sp5_lane_map[] = { {
	.lm_tile = OXIO_TILE_G0,
	.lm_dxio = 0x60,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_P0,
	.lm_dxio = 0x00,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_P1,
	.lm_dxio = 0x20,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_G1,
	.lm_dxio = 0x40,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_G3,
	.lm_dxio = 0x50,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_P3,
	.lm_dxio = 0x10,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_P2,
	.lm_dxio = 0x30,
	.lm_nlanes = 0x10,
	.lm_rev = true
}, {
	.lm_tile = OXIO_TILE_G2,
	.lm_dxio = 0x70,
	.lm_nlanes = 0x10,
	.lm_rev = false
}, {
	/*
	 * Note, there is a single instance that covers the 8 bonus lanes in
	 * Turin, while Genoa has two 4 lane instances. As there is not hotplug
	 * supported on these and we don't need the firmware's notion of a tile
	 * ID, we keep them split to make it easier to support both Genoa and
	 * Turin. If we ever encounter an SP5 based system with an x8 bonus lane
	 * (which would be Turin only), this split can be revisited.
	 */
	.lm_tile = OXIO_TILE_P4,
	.lm_dxio = 0x80,
	.lm_nlanes = 4,
	.lm_rev = false
}, {
	.lm_tile = OXIO_TILE_P5,
	.lm_dxio = 0x84,
	.lm_nlanes = 4,
	.lm_rev = false
} };

/*
 * This can become static if we manage to determine how to merge the
 * Milan-specific DXIO logic back into here.
 */
void
oxio_eng_to_lanes(const oxio_engine_t *oxio, uint8_t *startp, uint8_t *endp,
    bool *revp)
{
	const uint32_t sock = oxide_board_data->obd_cpuinfo.obc_socket;
	const oxio_lane_map_t *map = NULL, *lane_maps;
	size_t nmaps;
	uint8_t start, end;
	bool eng_rev, rev;

	switch (sock) {
	case X86_SOCKET_SP3:
		lane_maps = sp3_lane_map;
		nmaps = ARRAY_SIZE(sp3_lane_map);
		break;
	case X86_SOCKET_SP5:
		lane_maps = sp5_lane_map;
		nmaps = ARRAY_SIZE(sp5_lane_map);
		break;
	default:
		panic("Unsupported platform socket: 0x%x", sock);
	}

	for (size_t i = 0; i < nmaps; i++) {
		if (lane_maps[i].lm_tile == oxio->oe_tile) {
			map = &lane_maps[i];
			break;
		}
	}

	if (map == NULL) {
		panic("%s: invalid PCIe tile specified: 0x%x", oxio->oe_name,
		    oxio->oe_tile);
	}

	if (oxio->oe_lane >= map->lm_nlanes ||
	    oxio->oe_nlanes > map->lm_nlanes || oxio->oe_nlanes == 0 ||
	    oxio->oe_lane + oxio->oe_nlanes > map->lm_nlanes) {
		panic("%s: invalid lane configuration: [0x%x, 0x%x]",
		    oxio->oe_name, oxio->oe_lane, oxio->oe_lane +
		    oxio->oe_nlanes - 1);
	}

	/*
	 * When the core is reversed, schematic lane 0 maps to the end. For
	 * example, in SP3, P2 has its core reversed. If you had a device in P2
	 * lanes [3:0] (like M.2 East on Gimlet), then that needs to map to the
	 * DXIO lanes [15:12].
	 */
	if (!map->lm_rev) {
		start = map->lm_dxio + oxio->oe_lane;
		end = start + oxio->oe_nlanes - 1;

	} else {
		start = map->lm_dxio + map->lm_nlanes - oxio->oe_lane -
		    oxio->oe_nlanes;
		end = start + oxio->oe_nlanes - 1;
	}

	VERIFY3U(end, >=, start);
	eng_rev = (oxio->oe_flags & OXIO_ENGINE_F_REVERSE) != 0;
	rev = (map->lm_rev && !eng_rev) || (!map->lm_rev && eng_rev);

	if (startp != NULL)
		*startp = start;
	if (endp != NULL)
		*endp = end;
	if (revp != NULL)
		*revp = rev;
}

/*
 * Translate the OXIO GPIO expander type to one that is understood by AMD
 * firmware. AMD uses the same values for both traditional SMU hotplug and MPIO
 * UBM information.
 */
zen_hotplug_fw_i2c_expander_type_t
oxio_gpio_expander_to_fw(oxio_i2c_gpio_expander_type_t type)
{
	switch (type) {
	case OXIO_I2C_GPIO_EXP_T_PCA9539:
		return (ZEN_HP_FW_I2C_EXP_PCA9539);
	case OXIO_I2C_GPIO_EXP_T_PCA9535:
		return (ZEN_HP_FW_I2C_EXP_PCA9535);
	case OXIO_I2C_GPIO_EXP_T_PCA9506:
		return (ZEN_HP_FW_I2C_EXP_PCA9506);
	default:
		panic("unmappable OXIO i2c GPIO expander type: 0x%x", type);
	}
}

void
oxio_eng_to_ask(const oxio_engine_t *oxio, zen_mpio_ask_port_t *ask)
{
	oxio_speed_t max = oxide_zen_platform_consts()->zpc_pcie_max_speed;
	uint8_t start;
	bool rev;

	if (oxio->oe_type != OXIO_ENGINE_T_PCIE) {
		panic("%s: invalid engine type: 0x%x", oxio->oe_name,
		    oxio->oe_type);
	}

	oxio_eng_to_lanes(oxio, &start, NULL, &rev);
	ask->zma_link.zml_lane_start = start;
	ask->zma_link.zml_num_lanes = oxio->oe_nlanes;
	ask->zma_link.zml_reversed = rev;
	ask->zma_link.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE;

	/*
	 * Like with DXIO we don't actually need to support the different GPIO
	 * reset groups and given everyone our token GPIO ID since we will
	 * always just deassert PERST in a group.
	 */
	ask->zma_link.zml_gpio_id = ZEN_DXIO_FW_GROUP_UNUSED;

	/*
	 * We always indicate that the port is present. When we add support for
	 * traditional hotplug on MPIO-based platforms, then we will need to
	 * fill in the hotplug type here.
	 */
	ask->zma_link.zml_attrs.zmla_port_present = 1;

	/*
	 * The only other parameters we set right now are the speed related
	 * parameters. The rest are left at the default zeroed values to
	 * basically let the system more or less figure it out. Note, the OXIO
	 * enumeration for the speed definitions is purposefully kept in sync
	 * with both the DXIO and MPIO definitions.
	 */
	if (oxio->oe_tuning.ot_hw_limit > max) {
		cmn_err(CE_WARN, "%s: requested hardware limit speed (0x%x) "
		    "is greater than the maximum the hardware can support "
		    "(0x%x): using OXIO_SPEED_GEN_MAX instead", oxio->oe_name,
		    oxio->oe_tuning.ot_hw_limit, max);

		ask->zma_link.zml_attrs.zmla_max_link_speed_cap =
		    OXIO_SPEED_GEN_MAX;
	} else {
		ask->zma_link.zml_attrs.zmla_max_link_speed_cap =
		    oxio->oe_tuning.ot_hw_limit;
	}

	if (oxio->oe_tuning.ot_hw_target > max) {
		cmn_err(CE_WARN, "%s: requested hardware target speed (0x%x) "
		    "is greater than the maximum the hardware can support "
		    "(0x%x): using OXIO_SPEED_GEN_MAX instead", oxio->oe_name,
		    oxio->oe_tuning.ot_hw_target, max);

		ask->zma_link.zml_attrs.zmla_target_link_speed =
		    OXIO_SPEED_GEN_MAX;
	} else {
		ask->zma_link.zml_attrs.zmla_target_link_speed =
		    oxio->oe_tuning.ot_hw_target;
	}
}

/*
 * Transform UBM-based data that we've received and OXIO engine data into the
 * ask.
 */
void
oxio_ubm_to_ask(zen_ubm_hfc_t *hfc, const zen_mpio_ubm_dfc_descr_t *dfc,
    uint32_t dfcno, zen_mpio_ask_port_t *ask)
{
	const oxio_engine_t *oxio = hfc->zuh_oxio;
	oxio_engine_t synth;
	uint8_t eng_start, eng_end, start, eng_nlanes;
	bool rev;

	VERIFY3U(oxio->oe_type, ==, OXIO_ENGINE_T_UBM);

	/*
	 * First make sure that the set of lane information that's present in
	 * the UBM descriptor actually fits within the OXIO engine that we have.
	 * If not, then we should consider this all suspect.
	 */
	oxio_eng_to_lanes(oxio, &eng_start, &eng_end, &rev);
	eng_nlanes = eng_end - eng_start + 1;

	if (dfc->zmudd_lane_width == 0 || dfc->zmudd_lane_width > eng_nlanes) {
		panic("%s: engine has 0x%x lanes, but DFC has invalid lane "
		    "width: 0x%x", oxio->oe_name, eng_nlanes,
		    dfc->zmudd_lane_width);
	}

	if (dfc->zmudd_lane_start >= eng_nlanes ||
	    dfc->zmudd_lane_start + dfc->zmudd_lane_width - 1 >= eng_nlanes) {
		panic("%s: DFC %u wants lanes [%u, %u], but that is more than "
		    "the engine has available", oxio->oe_name, dfcno,
		    dfc->zmudd_lane_start, dfc->zmudd_lane_start +
		    dfc->zmudd_lane_width - 1);
	}

	/*
	 * Now that we know this will fit, we toss this into a faked up oxio
	 * engine so that way we can use the lane translation logic to properly
	 * handle reversals.
	 */
	bcopy(oxio, &synth, sizeof (synth));
	synth.oe_name = "synth";
	synth.oe_lane += dfc->zmudd_lane_start;
	synth.oe_nlanes = dfc->zmudd_lane_width;

	oxio_eng_to_lanes(&synth, &start, NULL, &rev);
	ask->zma_link.zml_lane_start = start;
	ask->zma_link.zml_num_lanes = dfc->zmudd_lane_width;

	/*
	 * Now that we've cemented that in the ask, snapshot that in our DFC
	 * information and assign the slot as well.
	 */
	hfc->zuh_dfcs[dfcno].zud_ask = ask;
	hfc->zuh_dfcs[dfcno].zud_slot = oxio->oe_slot + dfcno;

	/*
	 * The default behavior for an empty UBM slot is to assume it will
	 * become a PCIe slot by default.
	 */
	switch (dfc->zmudd_data.zmudt_type) {
		case ZEN_MPIO_UBM_DFC_TYPE_QUAD_PCI:
			ask->zma_link.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE;
			ask->zma_link.zml_attrs.zmla_port_present = 1;
			ask->zma_link.zml_reversed = rev;
			break;
		case ZEN_MPIO_UBM_DFC_TYPE_SATA_SAS:
			ask->zma_link.zml_attrs.zmla_port_present = 1;
			ask->zma_link.zml_ctlr_type = ZEN_MPIO_ASK_LINK_SATA;
			break;
		case ZEN_MPIO_UBM_DFC_TYPE_EMPTY:
			ask->zma_link.zml_ctlr_type = ZEN_MPIO_ASK_LINK_PCIE;
			ask->zma_link.zml_attrs.zmla_port_present = 0;
			break;
		default:
			panic("encountered unsupported UBM DFC type from "
			    "firmware: 0x%x", dfc->zmudd_data.zmudt_type);
	}

	/*
	 * See oxio_eng_to_ask() above on GPIO selection.
	 */
	ask->zma_link.zml_gpio_id = ZEN_DXIO_FW_GROUP_UNUSED;

	/*
	 * Finally, fill in the required UBM information.
	 */
	ask->zma_link.zml_attrs.zmla_link_hp_type = ZEN_MPIO_HOTPLUG_T_UBM;
	ask->zma_link.zml_attrs.zmla_hfc_idx = hfc->zuh_num;
	ask->zma_link.zml_attrs.zmla_dfc_idx = dfcno;
}

void
oxio_eng_to_ubm(const oxio_engine_t *oxio, zen_mpio_ubm_hfc_port_t *ubm)
{
	const oxio_ubm_gpio_t *gpio = &oxio->oe_hp_ubm.ohu_gpio;

	if (oxio->oe_type != OXIO_ENGINE_T_UBM) {
		panic("%s: invalid engine type: 0x%x", oxio->oe_name,
		    oxio->oe_type);
	}

	ubm->zmuhp_node_type = ZEN_MPIO_I2C_NODE_TYPE_UBM;
	ubm->zmuhp_expander.zmie_addr = gpio->oug_addr;
	ubm->zmuhp_expander.zmie_type =
	    oxio_gpio_expander_to_fw(gpio->oug_type);
	ubm->zmuhp_expander.zmie_clear_intrs = 0;

	oxio_eng_to_lanes(oxio, &ubm->zmuhp_start_lane, NULL, NULL);

	/*
	 * The MPIO firmware wants an absolute bit index where as we use a byte
	 * and bit offset combination to make it easier to map to a schematic.
	 */
	ubm->zmuhp_ubm_device.zmud_bp_type_bitno = gpio->oug_ifdet_byte * NBBY +
	    gpio->oug_ifdet_bit;
	ubm->zmuhp_ubm_device.zmud_i2c_reset_bitno = gpio->oug_reset_byte *
	    NBBY + gpio->oug_reset_bit;
	ubm->zmuhp_ubm_device.zmud_slot_num = oxio->oe_slot;

	for (size_t i = 0; i < ZEN_MPIO_I2C_SWITCH_DEPTH; i++) {
		zen_mpio_i2c_switch_t *dst = &ubm->zmuhp_i2c_switch[i];
		const oxio_i2c_switch_t *src = &oxio->oe_hp_ubm.ohu_switch[i];

		switch (src->ois_type) {
		case OXIO_I2C_SWITCH_T_NONE:
			/*
			 * To represent that there is no switch present, we
			 * leave all fields as zeros and hope that the MPIO
			 * firmware mostly figures out the right thing.
			 */
			VERIFY0(src->ois_addr);
			VERIFY0(src->ois_select);
			continue;
		case OXIO_I2C_SWITCH_T_9545:
			dst->zmis_type = ZEN_HP_FW_I2C_SW_9545;
			break;
		case OXIO_I2C_SWITCH_T_9546_48:
			dst->zmis_type = ZEN_HP_FW_I2C_SW_9546_48;
			break;
		default:
			panic("%s: encountered invalid I2C switch type 0x%x in "
			    "UBM switch[%zu]", oxio->oe_name, src->ois_type,
			    i);
		}

		/*
		 * The address is a 7-bit I2C address.
		 */
		VERIFY0(bitx8(src->ois_addr, 7, 7));
		dst->zmis_addr = src->ois_addr;
		VERIFY0(bitx8(src->ois_select, 7, 4));
		dst->zmis_select = src->ois_select;
	}
}

/*
 * Fill in common information about the port that comes from the engine itself.
 * This includes:
 *
 *  - The port's slot number
 *  - Hotplug status
 *
 * We don't end up doing anything with slot features here as only PCIe
 * ExpressModule based hotplug actually translates into slot features being set
 * that we can control at this time, though in theory UBM could likely advertise
 * out-of-band presence. Unfortunately we don't know if you can get to the UBM
 * power disable capability through the PCIe slot registers, but in practice
 * there are no platforms that we need to worry about that with.
 */
static void
oxio_port_info_fill(zen_pcie_port_t *port)
{
	const oxio_engine_t *oxio = port->zpp_oxio;
	zen_pcie_core_t *core = port->zpp_core;
	zen_fabric_t *fabric = core->zpc_ioms->zio_iodie->zi_soc->zs_fabric;

	/*
	 * UBM based devices have the slot on the DFC itself. The OXIO
	 * information only has the base slot.
	 */
	if (oxio->oe_type == OXIO_ENGINE_T_UBM) {
		port->zpp_hp_slotno = port->zpp_dfc->zud_slot;
	} else {
		port->zpp_hp_slotno = oxio->oe_slot;
	}

	/*
	 * Determine what hotplug flags we need to set. If we have either UBM or
	 * PCIe hotplug, then we need to set hotplug on the port and core. If we
	 * have traditional hoptlug present, then we must flag that on the
	 * fabric.
	 */
	if (oxio->oe_type != OXIO_ENGINE_T_UBM &&
	    (oxio->oe_type != OXIO_ENGINE_T_PCIE ||
	    oxio->oe_hp_type == OXIO_HOTPLUG_T_NONE)) {
		return;
	}

	port->zpp_flags |= ZEN_PCIE_PORT_F_HOTPLUG;
	core->zpc_flags |= ZEN_PCIE_CORE_F_HAS_HOTPLUG;

	if (oxio->oe_type == OXIO_ENGINE_T_PCIE &&
	    oxio->oe_hp_type != OXIO_HOTPLUG_T_NONE) {
		fabric->zf_flags |= ZEN_FABRIC_F_TRAD_HOTPLUG;
	}
}

/*
 * Determine the OXIO engine that corresponds to this DXIO firmware information.
 * We do this by basically translating lanes until we find a match. Because
 * there is a 1:1 ratio here, there isn't much that we need to do.
 */
void
oxio_dxio_to_eng(zen_pcie_port_t *port)
{
	const zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;
	const zen_dxio_fw_engine_t *en = port->zpp_dxio_engine;

	for (size_t i = 0; i < iodie->zi_nengines; i++) {
		uint8_t start, end;
		const oxio_engine_t *oxio = &iodie->zi_engines[i];

		oxio_eng_to_lanes(oxio, &start, &end, NULL);
		if (start == en->zde_start_lane && end == en->zde_end_lane) {
			port->zpp_oxio = oxio;
			oxio_port_info_fill(port);
			return;
		}
	}

	panic("failed to map PCIe port to OXIO engine!");
}

/*
 * We have an ASK that corresponds to UBM. Walk our UBM mapping data to get it
 * back to an OXIO engine, HFC, and DFC.
 */
static void
oxio_mpio_to_eng_ubm(zen_pcie_port_t *port)
{
	const zen_mpio_ask_port_t *ask = port->zpp_ask_port;
	zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;
	zen_fabric_t *fabric = iodie->zi_soc->zs_fabric;
	zen_ubm_config_t *ubm = &fabric->zf_ubm;
	const uint32_t ubm_idx = zen_mpio_ubm_idx(iodie);

	for (uint32_t i = 0; i < ubm->zuc_die_nports[ubm_idx]; i++) {
		zen_ubm_hfc_t *hfc;
		uint32_t hfcno = ubm->zuc_die_idx[ubm_idx] + i;
		hfc = &ubm->zuc_hfc[hfcno];

		for (uint32_t dfc = 0; dfc < hfc->zuh_ndfcs; dfc++) {
			if (hfc->zuh_dfcs[dfc].zud_ask == ask) {
				port->zpp_oxio = hfc->zuh_oxio;
				port->zpp_hfc = hfc;
				port->zpp_dfc = &hfc->zuh_dfcs[dfc];
				return;
			}
		}
	}

	panic("failed to map UBM port to OXIO engine!");
}

/*
 * Given a PCIe port with an MPIO ASK mapped, determine the corresponding OXIO
 * engine that led to this. This is a bit more involved than our DXIO version as
 * a single engine map end up mapped to a UBM device, whose lanes will be a
 * subset of the ones here. If the ASK has a UBM hotplug type, then we must go
 * through and walk all the UBM DFCs looking for a match. Otherwise we look for
 * an exact lane match ala DXIO.
 */
void
oxio_mpio_to_eng(zen_pcie_port_t *port)
{
	const zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;
	const zen_mpio_ask_port_t *ask = port->zpp_ask_port;
	uint8_t eng_start, eng_end;

	if (ask->zma_link.zml_attrs.zmla_link_hp_type ==
	    ZEN_MPIO_HOTPLUG_T_UBM) {
		oxio_mpio_to_eng_ubm(port);
		oxio_port_info_fill(port);
		return;
	}

	eng_start = ask->zma_link.zml_lane_start;
	eng_end = ask->zma_link.zml_lane_start + ask->zma_link.zml_num_lanes -
	    1;
	for (size_t i = 0; i < iodie->zi_nengines; i++) {
		uint8_t start, end;
		const oxio_engine_t *oxio = &iodie->zi_engines[i];

		if (oxio->oe_type != OXIO_ENGINE_T_PCIE)
			continue;

		oxio_eng_to_lanes(oxio, &start, &end, NULL);
		if (start == eng_start && end == eng_end) {
			port->zpp_oxio = oxio;
			oxio_port_info_fill(port);
			return;
		}
	}

	panic("failed to map PCIe port to OXIO engine!");
}

/*
 * We've been given a speed to set as the logical limit on a PCIe bridge.
 * Validate that this is valid for the platform and return the corresponding
 * value that makes sense for PCIe, generally in the context of the Link Control
 * register target speed register.
 */
uint16_t
oxio_loglim_to_pcie(const oxio_engine_t *oxio)
{
	oxio_speed_t max = oxide_zen_platform_consts()->zpc_pcie_max_speed;
	oxio_speed_t limit = oxio->oe_tuning.ot_log_limit;

	if (limit == OXIO_SPEED_GEN_MAX) {
		limit = max;
	}

	if (limit > max) {
		cmn_err(CE_WARN, "%s: requested logical limit speed (0x%x) "
		    "is greater than the maximum the hardware can support "
		    "(0x%x): using OXIO_SPEED_GEN_MAX instead", oxio->oe_name,
		    oxio->oe_tuning.ot_hw_limit, max);
		limit = max;
	}

	switch (limit) {
	case OXIO_SPEED_GEN_1:
		return (PCIE_LINKCTL2_TARGET_SPEED_2_5);
	case OXIO_SPEED_GEN_2:
		return (PCIE_LINKCTL2_TARGET_SPEED_5);
	case OXIO_SPEED_GEN_3:
		return (PCIE_LINKCTL2_TARGET_SPEED_8);
	case OXIO_SPEED_GEN_4:
		return (PCIE_LINKCTL2_TARGET_SPEED_16);
	case OXIO_SPEED_GEN_5:
		return (PCIE_LINKCTL2_TARGET_SPEED_32);
	default:
		panic("%s: unmappable OXIO logical limit speed: 0x%x",
		    oxio->oe_name, limit);
	}
}

/*
 * Both the SMU and MPIO use a 5-bit index to determine the meaning of an i2c
 * switch in the system, with values defined in the range [0, 16]. This table
 * maps the index values in the oxio_i2c_switch_t to corresponding values
 * expected by SMU/MPIO firmware.
 */
static const oxio_i2c_switch_t oxio_i2c_switch_map[17] = {
	[0] = { OXIO_I2C_SWITCH_T_9545, 0x70, 0x0 },
	[1] = { OXIO_I2C_SWITCH_T_9545, 0x70, 0x1 },
	[2] = { OXIO_I2C_SWITCH_T_9545, 0x70, 0x2 },
	[3] = { OXIO_I2C_SWITCH_T_9545, 0x70, 0x3 },
	[4] = { OXIO_I2C_SWITCH_T_9545, 0x71, 0x0 },
	[5] = { OXIO_I2C_SWITCH_T_9545, 0x71, 0x0 },
	[6] = { OXIO_I2C_SWITCH_T_9545, 0x71, 0x0 },
	[7] = { OXIO_I2C_SWITCH_T_NONE, 0x00, 0x0 },
	[8] = { OXIO_I2C_SWITCH_T_9545, 0x71, 0x3 },
	[9] = { OXIO_I2C_SWITCH_T_9545, 0x72, 0x0 },
	[10] = { OXIO_I2C_SWITCH_T_9545, 0x72, 0x1 },
	[11] = { OXIO_I2C_SWITCH_T_9545, 0x72, 0x2 },
	[12] = { OXIO_I2C_SWITCH_T_9545, 0x72, 0x3 },
	[13] = { OXIO_I2C_SWITCH_T_9545, 0x73, 0x0 },
	[14] = { OXIO_I2C_SWITCH_T_9545, 0x73, 0x1 },
	[15] = { OXIO_I2C_SWITCH_T_9545, 0x73, 0x2 },
	[16] = { OXIO_I2C_SWITCH_T_9545, 0x73, 0x3 },
};

uint8_t
oxio_switch_to_fw(const oxio_i2c_switch_t *i2c)
{
	for (size_t i = 0; i < ARRAY_SIZE(oxio_i2c_switch_map); i++) {
		const oxio_i2c_switch_t *comp = &oxio_i2c_switch_map[i];
		if (i2c->ois_type == comp->ois_type &&
		    i2c->ois_addr == comp->ois_addr &&
		    i2c->ois_select == comp->ois_select) {
			return (i);
		}
	}

	panic("encountered unmappable i2c switch config: type/addr/select: "
	    "0x%x/0x%x/0x%x", i2c->ois_type, i2c->ois_addr, i2c->ois_select);
}

typedef struct oxio_pcie_fw_map {
	oxio_pcie_slot_cap_t		ops_oxio;
	zen_hotplug_fw_expa_bits_t	ops_expa;
	zen_hotplug_fw_expb_bits_t	ops_expb;
} oxio_pcie_fw_map_t;

static const oxio_pcie_fw_map_t oxio_pcie_cap_map[] = {
	{ OXIO_PCIE_CAP_OOB_PRSNT, ZEN_HP_FW_EXPA_PRSNT, ZEN_HP_FW_EXPB_PRSNT },
	{ OXIO_PCIE_CAP_PWREN, ZEN_HP_FW_EXPA_PWREN, ZEN_HP_FW_EXPB_PWREN },
	{ OXIO_PCIE_CAP_PWRFLT, ZEN_HP_FW_EXPA_PWRFLT, ZEN_HP_FW_EXPB_PWRFLT },
	{ OXIO_PCIE_CAP_ATTNLED, ZEN_HP_FW_EXPA_ATTNLED,
	    ZEN_HP_FW_EXPB_ATTNLED },
	{ OXIO_PCIE_CAP_PWRLED, ZEN_HP_FW_EXPA_PWRLED, ZEN_HP_FW_EXPB_PWRLED },
	{ OXIO_PCIE_CAP_EMIL, ZEN_HP_FW_EXPA_EMIL, ZEN_HP_FW_EXPB_EMIL },
	{ OXIO_PCIE_CAP_EMILS, ZEN_HP_FW_EXPA_EMILS, ZEN_HP_FW_EXPB_EMILS },
	{ OXIO_PCIE_CAP_ATTNSW, ZEN_HP_FW_EXPA_ATTNSW, ZEN_HP_FW_EXPB_ATTNSW },
};

/*
 * Translate the corresponding capabilities format to one that is used by the
 * SMU/MPIO firmware.
 *
 * Note that Enterprise SSD based devices have a mask that doesn't correspond to
 * standard functions and instead is related to things like DualPortEn# and
 * IfDet#. There are no features that are allowed to be set by Enterprise SSD
 * devices, therefore we ensure that this is set to 0.
 */
uint8_t
oxio_pcie_cap_to_mask(const oxio_engine_t *oxio)
{
	const oxio_pcie_slot_cap_t cap = oxio->oe_hp_trad.ohp_cap;
	uint8_t mask = 0;

	VERIFY3U(oxio->oe_type, ==, OXIO_ENGINE_T_PCIE);
	if (oxio->oe_hp_type == OXIO_HOTPLUG_T_ENTSSD) {
		VERIFY0(oxio->oe_hp_trad.ohp_cap);
		return (mask);
	}

	for (size_t i = 0; i < ARRAY_SIZE(oxio_pcie_cap_map); i++) {
		if ((cap & oxio_pcie_cap_map[i].ops_oxio) != 0)
			continue;

		if (oxio->oe_hp_type == OXIO_HOTPLUG_T_EXP_A) {
			mask |= oxio_pcie_cap_map[i].ops_expa;
		} else {
			ASSERT3U(oxio->oe_hp_type, ==, OXIO_HOTPLUG_T_EXP_B);
			mask |= oxio_pcie_cap_map[i].ops_expb;
		}
	}

	return (mask);
}
