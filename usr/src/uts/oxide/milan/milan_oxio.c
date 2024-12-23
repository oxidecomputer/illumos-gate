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
 * These OXIO routines are for the OXIO subsystem implementations and can be
 * removed from this header once we finish implementing hotplug logic and
 * determining how we want to deal with Milan-specific PCIe values in the DXIO
 * structures. We leave them out of a header file to discourage additional
 * proliferation of their use.
 */
extern void oxio_eng_to_lanes(const oxio_engine_t *, uint8_t *, uint8_t *,
    bool *);
extern smu_exp_type_t oxio_gpio_to_smu(oxio_i2c_gpio_type_t);


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
 * The remaining routines in this file are related to hotplug. AS such, when we
 * add traditional hotplug support beyond Milan and move the hotplug structures
 * out of a Milan-specific directory, this logic will all move to zen_oxio.c.
 */

/*
 * The SMU uses a 5-bit index to determine the meaning of an i2c switch in the
 * system. There are values defined in the range [0, 16]. This table encodes the
 * corresponding values in the oxio_i2c_switch_t to the SMU version.
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

static uint8_t
oxio_switch_to_smu(const oxio_i2c_switch_t *i2c)
{
	for (size_t i = 0; i < ARRAY_SIZE(oxio_i2c_switch_map); i++) {
		const oxio_i2c_switch_t *comp = &oxio_i2c_switch_map[i];
		if (i2c->ois_type == comp->ois_type &&
		    i2c->ois_addr == comp->ois_addr &&
		    i2c->ois_select == comp->ois_select) {
			return (i);
		}
	}

	panic("encountered unmappable i2c i2c configuration: "
	    "type/address/select: 0x%x/0x%x/0x%x", i2c->ois_type,
	    i2c->ois_addr, i2c->ois_select);
}

typedef struct {
	oxio_pcie_slot_cap_t	ops_oxio;
	smu_expa_bits_t		ops_expa;
	smu_expb_bits_t		ops_expb;
} oxio_pcie_smu_map_t;

static const oxio_pcie_smu_map_t oxio_pcie_cap_map[] = {
	{ OXIO_PCIE_CAP_OOB_PRSNT, SMU_EXPA_PRSNT, SMU_EXPB_PRSNT },
	{ OXIO_PCIE_CAP_PWREN, SMU_EXPA_PWREN, SMU_EXPB_PWREN },
	{ OXIO_PCIE_CAP_PWRFLT, SMU_EXPA_PWRFLT, SMU_EXPB_PWRFLT },
	{ OXIO_PCIE_CAP_ATTNLED, SMU_EXPA_ATTNLED, SMU_EXPB_ATTNLED },
	{ OXIO_PCIE_CAP_PWRLED, SMU_EXPA_PWRLED, SMU_EXPB_PWRLED },
	{ OXIO_PCIE_CAP_EMIL, SMU_EXPA_EMIL, SMU_EXPB_EMIL },
	{ OXIO_PCIE_CAP_EMILS, SMU_EXPA_EMILS, SMU_EXPB_EMILS },
	{ OXIO_PCIE_CAP_ATTNSW, SMU_EXPA_ATTNSW, SMU_EXPB_ATTNSW },
};

/*
 * Translate the corresponding capabilities format to one that is used by the
 * SMU. Note, that Enterprise SSD based devices have a mask that doesn't
 * correspond to standard functions and instead is related to things like
 * DualPortEn# and IfDet#. There are no features that are allowed to be set by
 * Enterprise SSD devices, therefore we ensure that this is set to 0.
 */
static uint8_t
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

/*
 * We have been given an engine that supports PCIe hotplug that we need to
 * transform into a form that the SMU can consume.
 */
void
oxio_port_to_smu_hp(const zen_pcie_port_t *port, smu_hotplug_table_t *smu)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	const oxio_engine_t *oxio = port->zpp_oxio;
	const zen_pcie_core_t *core = port->zpp_core;
	uint8_t slot = port->zpp_hp_slotno;
	smu_hotplug_map_t *map = &smu->smt_map[slot];
	smu_hotplug_function_t *func = &smu->smt_func[slot];
	smu_hotplug_reset_t *reset = &smu->smt_reset[slot];
	const oxio_trad_gpio_t *gpio;

	VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0);
	VERIFY((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) != 0);
	VERIFY0(port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN);

	/*
	 * Version 3 has a slightly differet data layout than the current
	 * supported version 2 format. While the reset descriptor is the same
	 * and the field meanings are generally the same, the actual order of
	 * the fields changed slightly in the map structue. The function
	 * descriptor added a new field.
	 */
	if (consts->zpc_hp_vers != ZEN_HP_VERS_2) {
		panic("cannot translate OXIO engine to unsupported SMU "
		    "hotplug version %u", consts->zpc_hp_vers);
	}

	switch (oxio->oe_hp_type) {
	case OXIO_HOTPLUG_T_EXP_A:
		map->shm_format = ZEN_HP_EXPRESS_MODULE_A;
		break;
	case OXIO_HOTPLUG_T_EXP_B:
		map->shm_format = ZEN_HP_EXPRESS_MODULE_B;
		break;
	case OXIO_HOTPLUG_T_ENTSSD:
		map->shm_format = ZEN_HP_ENTERPRISE_SSD;
		break;
	default:
		panic("cannot map unsupported hotplug type 0x%x on %s",
		    oxio->oe_hp_type, oxio->oe_name);
	}
	map->shm_active = 1;

	map->shm_apu = 0;
	map->shm_die_id = core->zpc_ioms->zio_iodie->zi_soc->zs_num;
	map->shm_port_id = port->zpp_portno;
	map->shm_tile_id = ops->zfo_tile_smu_hp_id(oxio);
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
	func->shf_i2c_dtype = oxio_gpio_to_smu(gpio->otg_exp_type);
	func->shf_i2c_bus = oxio_switch_to_smu(&gpio->otg_switch);
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
	reset->shr_i2c_dtype = oxio_gpio_to_smu(gpio->otg_exp_type);
	reset->shr_i2c_bus = oxio_switch_to_smu(&gpio->otg_switch);

}
