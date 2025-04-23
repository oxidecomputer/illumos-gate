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
 * Perform Milan-specific OXIO translations.
 */

#include <sys/stdbool.h>
#include <sys/debug.h>
#include <sys/io/zen/oxio.h>
#include <sys/io/zen/dxio_impl.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/io/zen/platform_impl.h>

/*
 * Perform a translation between an Oxide DXIO engine to the Milan-specific DXIO
 * firmware structure.
 */
void
oxio_eng_to_dxio(const oxio_engine_t *oxio, zen_dxio_fw_engine_t *dxio)
{
	zen_dxio_fw_link_cap_t *cap;
	oxio_speed_t max = oxide_zen_platform_consts()->zpc_pcie_max_speed;
	bool rev;

	if (oxio->oe_type != OXIO_ENGINE_T_PCIE) {
		panic("%s: invalid engine type: 0x%x", oxio->oe_name,
		    oxio->oe_type);
	}

	/*
	 * While we set the type based on this, the hotplug member in this has
	 * always been left at 0 regardles of what type of hotplug is in use.
	 * Instead this is set in the capabilities section below.
	 */
	dxio->zde_type = ZEN_DXIO_FW_ENGINE_PCIE;

	oxio_eng_to_lanes(oxio, &dxio->zde_start_lane, &dxio->zde_end_lane,
	    &rev);

	/*
	 * The GPIO and reset groups are really internal things that come back
	 * to us while we're executing the DXIO state machine. As we either will
	 * deassert all of the built-in PERST signals at once or have per-device
	 * GPIOs, we leave this set to the unused group macro.
	 */
	dxio->zde_gpio_group = ZEN_DXIO_FW_GROUP_UNUSED;
	dxio->zde_reset_group = ZEN_DXIO_FW_GROUP_UNUSED;

	/*
	 * On server platforms we do not need to ever indicate that a kpnp reset
	 * is required and therefore we leave that at zero. This appears mostly
	 * used on some client platforms for items that are connected to the
	 * discrete chipset. Similarly, we do not need to indicate a search
	 * depth.
	 */
	dxio->zde_search_depth = 0;
	dxio->zde_kpnp_reset = 0;

	/*
	 * Because we are supporting PCIe devices we need to fill out the
	 * various portions of the capabilities section. The other portions are
	 * instead filled in when we ask for this structure back from the DXIO
	 * firmware.
	 */
	cap = &dxio->zde_config.zdc_pcie.zdcp_caps;

	/*
	 * Always indicate that this is present. We do not support any links
	 * that have early training so we can leave that set to zero. While
	 * Ethanol-X does have some early training in its APOB, we don't reuse
	 * lanes related to that. Finally, nothing should have compliance mode.
	 */
	cap->zdlc_present = ZEN_DXIO_PORT_PRESENT;
	cap->zdlc_early_train = 0;
	cap->zdlc_comp_mode = 0;

	/*
	 * Determine if something is reversed or not. If the core is reversed or
	 * the lanes are physically reversed we need to set this to true.
	 * However, if the core is reversed and we have reversed the lanes, than
	 * that cancels itself out.
	 *
	 * Because we're setting PCIe based engines, we can leave off all of the
	 * RX and TX polarity inversion. That should only apply to SATA.
	 */
	if (rev) {
		cap->zdlc_reverse = 1;
	}

	/*
	 * These next two options control some amount of power savings related
	 * features in the device and allow the firmware to turn off unused PCIe
	 * lanes.
	 */
	cap->zdlc_en_off_config = 1;
	cap->zdlc_off_unused = 1;

	/*
	 * The PCIe Gen 3 equalization search mode is always explicitly
	 * overwritten in the data we send to firmware. Our, potentially
	 * dubious, understanding is that this is related to the LC_CTL4
	 * equalization search mode.
	 */
	cap->zdlc_eq_mode = LC_CTL4_EQ_8GT_MODE_COEFF_PRESET;
	cap->zdlc_eq_override = 1;

	/*
	 * Come and set the appropriate hotplug mode for this.
	 */
	switch (oxio->oe_hp_type) {
	case OXIO_HOTPLUG_T_NONE:
		cap->zdlc_hp = ZEN_DXIO_FW_HOTPLUG_T_DISABLED;
		break;
	case OXIO_HOTPLUG_T_EXP_A:
	case OXIO_HOTPLUG_T_EXP_B:
		cap->zdlc_hp = ZEN_DXIO_FW_HOTPLUG_T_EXPRESS_MODULE;
		break;
	case OXIO_HOTPLUG_T_ENTSSD:
		cap->zdlc_hp = ZEN_DXIO_FW_HOTPLUG_T_ENT_SSD;
		break;
	default:
		panic("%s: invalid hotplug mode: 0x%x", oxio->oe_name,
		    oxio->oe_hp_type);
	}


	/*
	 * Check to see if we have any limits that we need to apply. In the DXIO
	 * firmware, the only thing that is supported is the hardware limit.
	 * There is no support for the hardware target. The logical limit will
	 * be applied later.
	 */

	if (oxio->oe_tuning.ot_hw_target != OXIO_SPEED_GEN_MAX) {
		panic("%s: invalid hardware target speed set: 0x%x",
		    oxio->oe_name, oxio->oe_tuning.ot_hw_target);
	}

	/*
	 * Currently the values for the oxio_speed_t match up with the AMD speed
	 * definitions.
	 */
	if (oxio->oe_tuning.ot_hw_limit > max) {
		cmn_err(CE_WARN, "%s: requested hardware limit speed (0x%x) "
		    "is greater than the maximum the hardware can support "
		    "(0x%x): using OXIO_SPEED_GEN_MAX instead", oxio->oe_name,
		    oxio->oe_tuning.ot_hw_limit, max);
		cap->zdlc_max_speed = OXIO_SPEED_GEN_MAX;
	} else {
		cap->zdlc_max_speed = oxio->oe_tuning.ot_hw_limit;
	}
}

/*
 * We have been given an engine that supports PCIe hotplug that we need to
 * transform into a form that the SMU can consume.
 */
void
oxio_port_to_smu_hp(const zen_pcie_port_t *port, smu_hotplug_table_t *smu)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	const oxio_engine_t *oxio = port->zpp_oxio;
	const zen_pcie_core_t *core = port->zpp_core;
	uint8_t slot = port->zpp_slotno;
	smu_hotplug_map_t *map = &smu->smt_map[slot];
	smu_hotplug_function_t *func = &smu->smt_func[slot];
	smu_hotplug_reset_t *reset = &smu->smt_reset[slot];
	const oxio_trad_gpio_t *gpio;

	VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0);
	VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) != 0);
	VERIFY0(port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN);

	switch (oxio->oe_hp_type) {
	case OXIO_HOTPLUG_T_EXP_A:
		map->shm_format = ZEN_HP_FW_EXPRESS_MODULE_A;
		break;
	case OXIO_HOTPLUG_T_EXP_B:
		map->shm_format = ZEN_HP_FW_EXPRESS_MODULE_B;
		break;
	case OXIO_HOTPLUG_T_ENTSSD:
		map->shm_format = ZEN_HP_FW_ENTERPRISE_SSD;
		break;
	default:
		panic("cannot map unsupported hotplug type 0x%x on %s",
		    oxio->oe_hp_type, oxio->oe_name);
	}
	map->shm_active = 1;

	map->shm_apu = 0;
	map->shm_die_id = core->zpc_ioms->zio_nbio->zn_iodie->zi_num;
	map->shm_port_id = port->zpp_portno;
	map->shm_tile_id = ops->zfo_tile_fw_hp_id(oxio);
	map->shm_bridge = core->zpc_coreno * MILAN_PCIE_CORE_MAX_PORTS +
	    port->zpp_portno;


	gpio = &oxio->oe_hp_trad.ohp_dev;
	VERIFY3U(gpio->otg_byte, <, 8);
	VERIFY3U(gpio->otg_bit, <, 8);
	func->shf_i2c_bit = gpio->otg_bit;
	func->shf_i2c_byte = gpio->otg_byte;

	/*
	 * The SMU only accepts a 5-bit address and assumes that the upper two
	 * bits are fixed based upon the device type. The most significant bit
	 * cannot be used. For the various supported PCA devices, the upper two
	 * bits must be 0b01 (7-bit 0x20).
	 */
	VERIFY0(bitx8(gpio->otg_addr, 7, 7));
	VERIFY3U(bitx8(gpio->otg_addr, 6, 5), ==, 1);
	func->shf_i2c_daddr = bitx8(gpio->otg_addr, 4, 0);
	func->shf_i2c_dtype = oxio_gpio_expander_to_fw(gpio->otg_exp_type);
	func->shf_i2c_bus = oxio_switch_to_fw(&gpio->otg_switch);
	func->shf_mask = oxio_pcie_cap_to_mask(oxio);

	if ((oxio->oe_hp_flags & OXIO_HP_F_RESET_VALID) == 0) {
		map->shm_rst_valid = 0;
		return;
	}

	map->shm_rst_valid = 1;
	gpio = &oxio->oe_hp_trad.ohp_reset;
	VERIFY3U(gpio->otg_byte, <, 8);
	VERIFY3U(gpio->otg_bit, <, 8);
	reset->shr_i2c_gpio_byte = gpio->otg_byte;
	reset->shr_i2c_reset = 1 << gpio->otg_bit;
	VERIFY0(bitx8(gpio->otg_addr, 7, 7));
	VERIFY3U(bitx8(gpio->otg_addr, 6, 5), ==, 1);
	reset->shr_i2c_daddr = bitx8(gpio->otg_addr, 4, 0);
	reset->shr_i2c_dtype = oxio_gpio_expander_to_fw(gpio->otg_exp_type);
	reset->shr_i2c_bus = oxio_switch_to_fw(&gpio->otg_switch);
}
