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
 * Copyright 2022 Oxide Computer Company
 */

/*
 * AMD Zen family GPIO driver
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/stdbool.h>
#include <sys/x86_archext.h>
#include <sys/cpuvar.h>
#include <amdzen_client.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/gpio/kgpio_provider.h>

#include "amdzen_data.h"

typedef enum {
	/*
	 * Indicates that we should prefer to use SMN for accessing registers as
	 * opposed to MMIO.
	 */
	ZEN_GPIO_F_USE_SMN	= 1 << 0,
	/*
	 * Indicates that the platform has limited support for GPIOs that are
	 * I2C based. In particular this generally means:
	 *
	 *  o There is support for controlling the internal pulls.
	 *  o There is no support for controlling the output in a push-pull way
	 *    at all.
	 */
	ZEN_GPIO_F_I2C_NO_PP = 1 << 1
} zen_gpio_flags_t;

typedef struct zen_gpio {
	dev_info_t *zg_dip;
	zen_gpio_flags_t zg_flags;
	x86_processor_family_t zg_family;
	uint_t zg_dfno;
	size_t zg_ngpios;
	const zen_gpio_pindata_t *zg_pindata;
} zen_gpio_t;

static smn_reg_t
zen_gpio_pin_to_reg(zen_gpio_t *zg, const zen_gpio_pindata_t *pin)
{
	smn_reg_t reg;

	if ((pin->zg_cap & ZEN_GPIO_C_REMOTE) != 0) {
		uint32_t id = pin->zg_id;

		ASSERT3U(pin->zg_id, >=, 256);
		id -= 256;
		reg = FCH_RMTGPIO_GPIO_SMN(id);
	} else {
		reg = FCH_GPIO_GPIO_SMN(pin->zg_id);
	}

	return (reg);
}

static int
zen_gpio_read_reg(zen_gpio_t *zg, const zen_gpio_pindata_t *pin, uint32_t *out)
{
	int ret;

	if ((zg->zg_flags & ZEN_GPIO_F_USE_SMN) != 0) {
		const smn_reg_t reg = zen_gpio_pin_to_reg(zg, pin);
		ret = amdzen_c_smn_read(zg->zg_dfno, reg, out);
	} else {
		ret = ENOTSUP;
	}

	return (ret);
}

static int
zen_gpio_write_reg(zen_gpio_t *zg, const zen_gpio_pindata_t *pin, uint32_t val)
{
	int ret;

	if ((zg->zg_flags & ZEN_GPIO_F_USE_SMN) != 0) {
		const smn_reg_t reg = zen_gpio_pin_to_reg(zg, pin);
		ret = amdzen_c_smn_write(zg->zg_dfno, reg, val);
	} else {
		ret = ENOTSUP;
	}

	return (ret);
}

static int
zen_gpio_op_name2id(void *arg, const char *name, uint32_t *idp)
{
	zen_gpio_t *zg = arg;

	for (uint32_t i = 0; i < zg->zg_ngpios; i++) {
		if (strcmp(name, zg->zg_pindata[i].zg_name) == 0) {
			*idp = i;
			return (0);
		}
	}
	return (ENOENT);
}

/*
 * The driver is a synthesized value that we create based on the pin type.
 */
static void
zen_gpio_nvl_attr_fill_driver(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvlist_t *nvl, nvlist_t *meta)
{
	zen_gpio_driver_mode_t mode = ZEN_GPIO_DRIVER_UNKNOWN;
	uint_t npos = 0;
	uint32_t pos[2];

	switch (pin->zg_pad) {
	case ZEN_GPIO_PAD_TYPE_GPIO:
	case ZEN_GPIO_PAD_TYPE_SD:
		mode = pos[0] = ZEN_GPIO_DRIVER_PUSH_PULL;
		npos = 1;
		break;
	case ZEN_GPIO_PAD_TYPE_I2C:
		mode = pos[0] = ZEN_GPIO_DRIVER_OPEN_DRAIN;
		npos = 1;
		break;
	case ZEN_GPIO_PAD_TYPE_I3C:
		/*
		 * This varies based on pad settings and is potentially combined
		 * for multiple pins. For the moment, we leave this as something
		 * that we don't know.
		 */
		npos = 2;
		pos[0] = ZEN_GPIO_DRIVER_PUSH_PULL;
		pos[1] = ZEN_GPIO_DRIVER_OPEN_DRAIN;
		break;
	}

	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_OUTPUT_DRIVER, mode,
	    npos, pos, KGPIO_PROT_RO);
}

static void
zen_gpio_nvl_attr_fill_voltage(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvlist_t *nvl, nvlist_t *meta)
{
	zen_gpio_voltage_t volt = ZEN_GPIO_V_UNKNOWN;
	uint_t npos = 0;
	uint32_t pos[6];

	switch (pin->zg_pad) {
	case ZEN_GPIO_PAD_TYPE_GPIO:
	case ZEN_GPIO_PAD_TYPE_SD:
		volt = pin->zg_voltage;
		break;
	case ZEN_GPIO_PAD_TYPE_I2C:
	case ZEN_GPIO_PAD_TYPE_I3C:
		/*
		 * This varies based on pad settings and is potentially combined
		 * for multiple pins. For the moment, we leave this as something
		 * that we don't know.
		 */
		volt = ZEN_GPIO_V_UNKNOWN;
		break;
	}

	for (size_t i = 0; i < ARRAY_SIZE(pos); i++) {
		if (bitx32(pin->zg_voltage, i, i) != 0) {
			pos[npos] = 1 << i;
			npos++;
		}
	}

	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_VOLTAGE, volt, npos,
	    pos, KGPIO_PROT_RO);
}

/*
 * The configured drive strength generally holds for most pins. However, for I2C
 * it is entirely determined by the pad and therefore we punt for the moment.
 * I3C is more nuanced as well and therefore we basically just note it as
 * unknown. Otherwise, the valid values are based upon the voltage.
 */
static void
zen_gpio_nvl_attr_fill_strength(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvlist_t *nvl, nvlist_t *meta, uint32_t reg)
{
	bool v3p3, v1p8, v1p1;

	v3p3 = (pin->zg_voltage & (ZEN_GPIO_V_3P3_S5 | ZEN_GPIO_V_3P3_S0)) != 0;
	v1p8 = (pin->zg_voltage & (ZEN_GPIO_V_1P8_S5 | ZEN_GPIO_V_1P8_S0)) != 0;
	v1p1 = (pin->zg_voltage & ZEN_GPIO_V_1P1_S3) != 0;

	/*
	 * If a pin has an unknown voltage, then we can't really properly
	 * translate the drive strength. Treat that like I2C/I3C for the moment.
	 * Similarly, the controls are defined for a pin that is 1.1V capable,
	 * so if we find something there, note that. In general, those should
	 * only be true of I3C pins.
	 */
	if (pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C ||
	    pin->zg_pad == ZEN_GPIO_PAD_TYPE_I3C ||
	    pin->zg_voltage == ZEN_GPIO_V_UNKNOWN ||
	    v1p1 || (v3p3 && v1p8)) {
		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_DRIVE_STRENGTH,
		    ZEN_GPIO_DRIVE_UNKNOWN, 0, NULL, KGPIO_PROT_RO);
		return;
	}

	/*
	 * At this point we should only have pure 3.3V or 1.8V pins.
	 */
	if (v3p3) {
		uint32_t pos[2] = { ZEN_GPIO_DRIVE_40R, ZEN_GPIO_DRIVE_80R };
		zen_gpio_drive_strength_t str;

		switch (FCH_GPIO_GPIO_GET_DRVSTR_3P3(reg)) {
		case FCH_GPIO_GPIO_DRVSTR_3P3_40R:
			str = ZEN_GPIO_DRIVE_40R;
			break;
		case FCH_GPIO_GPIO_DRVSTR_3P3_80R:
			str = ZEN_GPIO_DRIVE_80R;
			break;
		default:
			str = ZEN_GPIO_DRIVE_UNKNOWN;
			break;
		}

		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_DRIVE_STRENGTH,
		    str, ARRAY_SIZE(pos), pos, KGPIO_PROT_RW);
	} else {
		uint32_t pos[3] = { ZEN_GPIO_DRIVE_40R, ZEN_GPIO_DRIVE_60R,
		    ZEN_GPIO_DRIVE_80R };
		zen_gpio_drive_strength_t str;

		switch (FCH_GPIO_GPIO_GET_DRVSTR_1P8(reg)) {
		case FCH_GPIO_GPIO_DRVSTR_1P8_60R:
			str = ZEN_GPIO_DRIVE_60R;
			break;
		case FCH_GPIO_GPIO_DRVSTR_1P8_80R:
			str = ZEN_GPIO_DRIVE_80R;
			break;
		case FCH_GPIO_GPIO_DRVSTR_1P8_40R:
			str = ZEN_GPIO_DRIVE_40R;
			break;
		default:
			str = ZEN_GPIO_DRIVE_UNKNOWN;
			break;
		}

		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_DRIVE_STRENGTH,
		    str, ARRAY_SIZE(pos), pos, KGPIO_PROT_RW);
	}
}

/*
 * The pull up settings here are a bit nuanced. In particular, we have the
 * following considerations:
 *
 * o In Zen 1-3, I2C pads do not support anything related to pulls, so this
 *   shows as always disabled.
 * o When remote GPIOs exist in Zen 2/3 systems, they do not support setting the
 *   internal pull up strength.
 * o The behvaior of whether pull up strength is supported varies based on the
 *   processor family.
 * o I3C pads may either be in an open-drain or in a push-pull configuration.
 *   The GPIOs for those don't indicate that they're reserved right now, unlike
 *   i2c.
 */
static void
zen_gpio_nvl_attr_fill_pull(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvlist_t *nvl, nvlist_t *meta, uint32_t reg)
{
	bool down, up, pstr;
	zen_gpio_pull_t pull;

	uint32_t pull_up_none[1] = { ZEN_GPIO_PULL_DISABLED };
	uint32_t pull_up_str[4] = { ZEN_GPIO_PULL_DISABLED, ZEN_GPIO_PULL_DOWN,
	    ZEN_GPIO_PULL_UP_4K, ZEN_GPIO_PULL_UP_8K };
	uint32_t pull_up_nostr[3] = { ZEN_GPIO_PULL_DISABLED,
	    ZEN_GPIO_PULL_DOWN, ZEN_GPIO_PULL_UP };

	pstr = (zg->zg_flags & ZEN_GPIO_F_I2C_NO_PP) != 0;
	down = FCH_GPIO_GPIO_GET_PD_EN(reg) != 0;
	up = FCH_GPIO_GPIO_GET_PU_EN(reg) != 0;

	/*
	 * For these systems where the I2C GPIOs are forced to be open-drain
	 * (according to the PPR), suggest that this is basically a forced
	 * disabled case.
	 */
	if (pstr && pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C) {
		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_PULL,
		    ZEN_GPIO_PULL_DISABLED, ARRAY_SIZE(pull_up_none),
		    pull_up_none, KGPIO_PROT_RO);
		return;
	}

	if (up && down) {
		pull = ZEN_GPIO_PULL_DOWN_UP;
	} else if (up) {
		pull = ZEN_GPIO_PULL_UP;
	} else if (down) {
		pull = ZEN_GPIO_PULL_DOWN;
	} else {
		pull = ZEN_GPIO_PULL_DISABLED;
	}

	if (pstr && (pin->zg_cap & ZEN_GPIO_C_REMOTE) == 0) {
		bool str = FCH_GPIO_GPIO_GET_PU_STR(reg);

		if (up && down) {
			if (str == FCH_GPIO_GPIO_PU_8K) {
				pull = ZEN_GPIO_PULL_DOWN_UP_8K;
			} else {
				pull = ZEN_GPIO_PULL_DOWN_UP_4K;
			}
		} else if (up) {
			if (str == FCH_GPIO_GPIO_PU_8K) {
				pull = ZEN_GPIO_PULL_UP_8K;
			} else {
				pull = ZEN_GPIO_PULL_UP_4K;
			}
		}
		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_PULL, pull,
		    ARRAY_SIZE(pull_up_str), pull_up_str, KGPIO_PROT_RW);
	} else {
		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_PULL, pull,
		    ARRAY_SIZE(pull_up_nostr), pull_up_nostr, KGPIO_PROT_RW);
	}
}

static void
zen_gpio_nvl_attr_fill_trigger(zen_gpio_t *zg, nvlist_t *nvl, nvlist_t *meta,
    uint32_t reg)
{
	zen_gpio_trigger_t trig;
	uint32_t level = FCH_GPIO_GPIO_GET_LEVEL(reg);
	uint32_t pos[5] = { ZEN_GPIO_TRIGGER_EDGE_HIGH,
	    ZEN_GPIO_TRIGGER_EDGE_LOW, ZEN_GPIO_TRIGGER_EDGE_BOTH,
	    ZEN_GPIO_TRIGGER_LEVEL_HIGH, ZEN_GPIO_TRIGGER_LEVEL_LOW };

	if (FCH_GPIO_GPIO_GET_TRIG(reg) == FCH_GPIO_GPIO_TRIG_EDGE) {
		switch (level) {
		case FCH_GPIO_GPIO_LEVEL_ACT_HIGH:
			trig = ZEN_GPIO_TRIGGER_EDGE_HIGH;
			break;
		case FCH_GPIO_GPIO_LEVEL_ACT_LOW:
			trig = ZEN_GPIO_TRIGGER_EDGE_LOW;
			break;
		case FCH_GPIO_GPIO_LEVEL_ACT_BOTH:
			trig = ZEN_GPIO_TRIGGER_EDGE_BOTH;
			break;
		default:
			trig = ZEN_GPIO_TRIGGER_UNKNOWN;
			break;
		}
	} else {
		switch (level) {
		case FCH_GPIO_GPIO_LEVEL_ACT_HIGH:
			trig = ZEN_GPIO_TRIGGER_LEVEL_HIGH;
			break;
		case FCH_GPIO_GPIO_LEVEL_ACT_LOW:
			trig = ZEN_GPIO_TRIGGER_LEVEL_LOW;
			break;
		default:
			trig = ZEN_GPIO_TRIGGER_UNKNOWN;
			break;
		}
	}

	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_TRIGGER_MODE, trig,
	    ARRAY_SIZE(pos), pos, KGPIO_PROT_RW);
}

static void
zen_gpio_nvl_attr_fill(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvlist_t *nvl, uint32_t reg)
{
	nvlist_t *meta = fnvlist_alloc();
	uint32_t input_pos[2] = { ZEN_GPIO_INPUT_LOW, ZEN_GPIO_INPUT_HIGH };
	uint32_t dbt_mode;
	zen_gpio_status_t stat;
	uint32_t dbt_mode_pos[4] = { ZEN_GPIO_DEBOUNCE_MODE_NONE,
	    ZEN_GPIO_DEBOUNCE_MODE_KEEP_LOW, ZEN_GPIO_DEBOUNCE_MODE_KEEP_HIGH,
	    ZEN_GPIO_DEBOUNCE_MODE_REMOVE };
	uint32_t dbt_unit_pos[4] = { ZEN_GPIO_DEBOUNCE_UNIT_2RTC,
	    ZEN_GPIO_DEBOUNCE_UNIT_8RTC, ZEN_GPIO_DEBOUNCE_UNIT_512RTC,
	    ZEN_GPIO_DEBOUNCE_UNIT_2048RTC };

	kgpio_nvl_attr_fill_str(nvl, meta, KGPIO_ATTR_NAME, pin->zg_name, 0,
	    NULL, KGPIO_PROT_RO);
	kgpio_nvl_attr_fill_str(nvl, meta, ZEN_GPIO_ATTR_PAD_NAME,
	    pin->zg_signal, 0, NULL, KGPIO_PROT_RO);
	kgpio_nvl_attr_fill_str(nvl, meta, ZEN_GPIO_ATTR_PIN, pin->zg_pin, 0,
	    NULL, KGPIO_PROT_RO);
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_PAD_TYPE, pin->zg_pad,
	    0, NULL, KGPIO_PROT_RO);
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_CAPS, pin->zg_cap, 0,
	    NULL, KGPIO_PROT_RO);

	/*
	 * Next, add information that depends on the type of pad.
	 */
	zen_gpio_nvl_attr_fill_driver(zg, pin, nvl, meta);
	zen_gpio_nvl_attr_fill_voltage(zg, pin, nvl, meta);
	zen_gpio_nvl_attr_fill_strength(zg, pin, nvl, meta, reg);

	/*
	 * Determine how to represent the output value. In particular, if this
	 * is an open-drain only pin then the only options we have are more
	 * limited and we represent this as just disabled or low. This only
	 * happens for I2C pad types.
	 */
	if ((zg->zg_flags & ZEN_GPIO_F_I2C_NO_PP) != 0 &&
	    pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C) {
		zen_gpio_output_t output;
		uint32_t output_pos[2] = { ZEN_GPIO_OUTPUT_DISABLED,
		    ZEN_GPIO_OUTPUT_LOW };

		if (FCH_GPIO_GPIO_GET_OUT_EN(reg) == 0) {
			output = ZEN_GPIO_OUTPUT_DISABLED;
		} else {
			output = ZEN_GPIO_OUTPUT_LOW;
		}

		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_OUTPUT, output,
		    ARRAY_SIZE(output_pos), output_pos, KGPIO_PROT_RW);
	} else {
		zen_gpio_output_t output;
		uint32_t output_pos[3] = { ZEN_GPIO_OUTPUT_DISABLED,
		    ZEN_GPIO_OUTPUT_LOW, ZEN_GPIO_OUTPUT_HIGH };

		if (FCH_GPIO_GPIO_GET_OUT_EN(reg) == 0) {
			output = ZEN_GPIO_OUTPUT_DISABLED;
		} else if (FCH_GPIO_GPIO_GET_OUTPUT(reg) != 0) {
			output = ZEN_GPIO_OUTPUT_HIGH;
		} else {
			output = ZEN_GPIO_OUTPUT_LOW;
		}
		kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_OUTPUT, output,
		    ARRAY_SIZE(output_pos), output_pos, KGPIO_PROT_RW);
	}

	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_INPUT,
	    FCH_GPIO_GPIO_GET_INPUT(reg), ARRAY_SIZE(input_pos), input_pos,
	    KGPIO_PROT_RO);

	/*
	 * Capture debounce and trigger information. Note, these are 1:1 mapped
	 * to the attributes right now.
	 */
	dbt_mode = FCH_GPIO_GPIO_GET_DBT_HIGH(reg) << 1;
	dbt_mode |= FCH_GPIO_GPIO_GET_DBT_LOW(reg);
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_DEBOUNCE_MODE,
	    dbt_mode, ARRAY_SIZE(dbt_mode_pos), dbt_mode_pos, KGPIO_PROT_RW);
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_DEBOUNCE_UNIT,
	    FCH_GPIO_GPIO_GET_DBT_CTL(reg), ARRAY_SIZE(dbt_unit_pos),
	    dbt_unit_pos, KGPIO_PROT_RW);
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_DEBOUNCE_COUNT,
	    FCH_GPIO_GPIO_GET_DBT_TMR(reg), 0, NULL, KGPIO_PROT_RW);

	zen_gpio_nvl_attr_fill_trigger(zg, nvl, meta, reg);

	stat = 0;
	if (FCH_GPIO_GPIO_GET_WAKE_STS(reg) != 0) {
		stat |= ZEN_GPIO_STATUS_WAKE;
	}
	if (FCH_GPIO_GPIO_GET_INT_STS(reg) != 0) {
		stat |= ZEN_GPIO_STATUS_INTR;
	}
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_STATUS, stat, 0, NULL,
	    KGPIO_PROT_RO);

	/*
	 * Fill attributes where the reading depends on the processor family
	 * and/or pin-type.
	 */
	zen_gpio_nvl_attr_fill_pull(zg, pin, nvl, meta, reg);

	/*
	 * Add the raw value for debugging purposes.
	 */
	kgpio_nvl_attr_fill_u32(nvl, meta, ZEN_GPIO_ATTR_RAW_REG, reg, 0, NULL,
	    KGPIO_PROT_RO);

	/*
	 * Now that we're done, finally add the metadata nvlist and free it.
	 */
	fnvlist_add_nvlist(nvl, KGPIO_ATTR_META, meta);
	fnvlist_free(meta);
}

static int
zen_gpio_op_attr_get(void *arg, uint32_t gpio_id, nvlist_t *nvl)
{
	int ret;
	uint32_t val;
	zen_gpio_t *zg = arg;
	const zen_gpio_pindata_t *pin;

	pin = &zg->zg_pindata[gpio_id];
	ret = zen_gpio_read_reg(zg, pin, &val);
	if (ret != 0) {
		return (ret);
	}
	zen_gpio_nvl_attr_fill(zg, pin, nvl, val);

	return (0);
}

typedef bool (*zen_gpio_attr_f)(zen_gpio_t *, const zen_gpio_pindata_t *,
    nvpair_t *, nvlist_t *, uint32_t *);

typedef struct {
	const char *zat_attr;
	zen_gpio_attr_f zat_proc;
} zen_gpio_attr_table_t;

static bool
zen_gpio_attr_set_ro(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	fnvlist_add_uint32(errs, nvpair_name(pair),
	    (uint32_t)KGPIO_ATTR_ERR_ATTR_RO);
	return (false);
}

static bool
zen_gpio_attr_set_output(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	switch (val) {
	case ZEN_GPIO_OUTPUT_DISABLED:
		*regp = FCH_GPIO_GPIO_SET_OUT_EN(*regp, 0);
		break;
	case ZEN_GPIO_OUTPUT_LOW:
		*regp = FCH_GPIO_GPIO_SET_OUT_EN(*regp, 1);
		*regp = FCH_GPIO_GPIO_SET_OUTPUT(*regp,
		    FCH_GPIO_GPIO_OUTPUT_LOW);
		break;
	case ZEN_GPIO_OUTPUT_HIGH:
		if ((zg->zg_flags & ZEN_GPIO_F_I2C_NO_PP) != 0 &&
		    pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C) {
			fnvlist_add_uint32(errs, nvpair_name(pair),
			    (uint32_t)KGPIO_ATTR_ERR_CANT_APPLY_VAL);
			return (false);
		}
		*regp = FCH_GPIO_GPIO_SET_OUT_EN(*regp, 1);
		*regp = FCH_GPIO_GPIO_SET_OUTPUT(*regp,
		    FCH_GPIO_GPIO_OUTPUT_HIGH);
		break;
	default:
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}

	return (true);
}

static bool
zen_gpio_attr_set_pull(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;
	bool pstr, i2c;

	pstr = (zg->zg_flags & ZEN_GPIO_F_I2C_NO_PP) != 0;
	i2c = pstr && pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C;
	if (i2c) {
		/*
		 * This property is read-only for i2c pads as all these fields
		 * are reserved. We fail fast up here to simplify the rest of
		 * the conditional code.
		 */
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_ATTR_RO);
		return (false);
	}

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	switch (val) {
	case ZEN_GPIO_PULL_DISABLED:
		*regp = FCH_GPIO_GPIO_SET_PD_EN(*regp, 0);
		*regp = FCH_GPIO_GPIO_SET_PU_EN(*regp, 0);
		break;
	case ZEN_GPIO_PULL_DOWN:
		*regp = FCH_GPIO_GPIO_SET_PD_EN(*regp, 1);
		*regp = FCH_GPIO_GPIO_SET_PU_EN(*regp, 0);
		break;
	case ZEN_GPIO_PULL_UP_4K:
		if (!pstr) {
			fnvlist_add_uint32(errs, nvpair_name(pair),
			    (uint32_t)KGPIO_ATTR_ERR_CANT_APPLY_VAL);
			return (false);
		}
		*regp = FCH_GPIO_GPIO_SET_PD_EN(*regp, 0);
		*regp = FCH_GPIO_GPIO_SET_PU_EN(*regp, 1);
		*regp = FCH_GPIO_GPIO_SET_PU_STR(*regp, FCH_GPIO_GPIO_PU_4K);
		break;
	case ZEN_GPIO_PULL_UP_8K:
		if (!pstr) {
			fnvlist_add_uint32(errs, nvpair_name(pair),
			    (uint32_t)KGPIO_ATTR_ERR_CANT_APPLY_VAL);
			return (false);
		}
		*regp = FCH_GPIO_GPIO_SET_PD_EN(*regp, 0);
		*regp = FCH_GPIO_GPIO_SET_PU_EN(*regp, 1);
		*regp = FCH_GPIO_GPIO_SET_PU_STR(*regp, FCH_GPIO_GPIO_PU_8K);
		break;
	case ZEN_GPIO_PULL_UP:
		if (pstr) {
			fnvlist_add_uint32(errs, nvpair_name(pair),
			    (uint32_t)KGPIO_ATTR_ERR_CANT_APPLY_VAL);
			return (false);
		}
		*regp = FCH_GPIO_GPIO_SET_PD_EN(*regp, 0);
		*regp = FCH_GPIO_GPIO_SET_PU_EN(*regp, 1);
		break;
	default:
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}

	return (true);
}

static bool
zen_gpio_attr_set_str(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;
	bool v3p3, v1p8, v1p1;

	v3p3 = (pin->zg_voltage & (ZEN_GPIO_V_3P3_S5 | ZEN_GPIO_V_3P3_S0)) != 0;
	v1p8 = (pin->zg_voltage & (ZEN_GPIO_V_1P8_S5 | ZEN_GPIO_V_1P8_S0)) != 0;
	v1p1 = (pin->zg_voltage & ZEN_GPIO_V_1P1_S3) != 0;

	/*
	 * See zen_gpio_nvl_attr_fill_strength(). This set of conditions are
	 * things that we can't know the valid set (or use pad controls that
	 * aren't a part of this). The drive strength is treated as read-only in
	 * that case.
	 */
	if (pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C ||
	    pin->zg_pad == ZEN_GPIO_PAD_TYPE_I3C ||
	    pin->zg_voltage == ZEN_GPIO_V_UNKNOWN ||
	    v1p1 || (v3p3 && v1p8)) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_ATTR_RO);
		return (false);
	}

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	switch (val) {
	case ZEN_GPIO_DRIVE_40R:
		if (v3p3) {
			*regp = FCH_GPIO_GPIO_SET_DRVSTR(*regp,
			    FCH_GPIO_GPIO_DRVSTR_3P3_40R);
		} else {
			*regp = FCH_GPIO_GPIO_SET_DRVSTR(*regp,
			    FCH_GPIO_GPIO_DRVSTR_1P8_40R);
		}
		break;
	case ZEN_GPIO_DRIVE_60R:
		if (v3p3) {
			fnvlist_add_uint32(errs, nvpair_name(pair),
			    (uint32_t)KGPIO_ATTR_ERR_CANT_APPLY_VAL);
			return (false);
		} else {
			*regp = FCH_GPIO_GPIO_SET_DRVSTR(*regp,
			    FCH_GPIO_GPIO_DRVSTR_1P8_60R);
		}
		break;
	case ZEN_GPIO_DRIVE_80R:
		if (v3p3) {
			*regp = FCH_GPIO_GPIO_SET_DRVSTR(*regp,
			    FCH_GPIO_GPIO_DRVSTR_3P3_80R);
		} else {
			*regp = FCH_GPIO_GPIO_SET_DRVSTR(*regp,
			    FCH_GPIO_GPIO_DRVSTR_1P8_80R);
		}
		break;
	default:
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}

	return (true);
}

static bool
zen_gpio_attr_set_dbt_mode(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	switch (val) {
	case ZEN_GPIO_DEBOUNCE_MODE_NONE:
	case ZEN_GPIO_DEBOUNCE_MODE_KEEP_LOW:
	case ZEN_GPIO_DEBOUNCE_MODE_KEEP_HIGH:
	case ZEN_GPIO_DEBOUNCE_MODE_REMOVE:
		*regp = FCH_GPIO_GPIO_SET_DBT_CTL(*regp, val);
		break;
	default:
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}

	return (true);
}

static bool
zen_gpio_attr_set_dbt_unit(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	switch (val) {
	case ZEN_GPIO_DEBOUNCE_UNIT_2RTC:
	case ZEN_GPIO_DEBOUNCE_UNIT_8RTC:
	case ZEN_GPIO_DEBOUNCE_UNIT_512RTC:
	case ZEN_GPIO_DEBOUNCE_UNIT_2048RTC:
		*regp = FCH_GPIO_GPIO_SET_DBT_LOW(*regp, (val & 1));
		*regp = FCH_GPIO_GPIO_SET_DBT_HIGH(*regp, (val & 1));
		break;
	default:
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}

	return (true);
}

static bool
zen_gpio_attr_set_dbt_count(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	/*
	 * The dbt count is a 4-bit value.
	 */
	if (val >= 0x10) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}

	*regp = FCH_GPIO_GPIO_SET_DBT_TMR(*regp, val);
	return (true);
}

static bool
zen_gpio_attr_set_trig(zen_gpio_t *zg, const zen_gpio_pindata_t *pin,
    nvpair_t *pair, nvlist_t *errs, uint32_t *regp)
{
	uint32_t val;

	if (nvpair_value_uint32(pair, &val) != 0) {
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_BAD_TYPE);
		return (false);
	}

	switch (val) {
	case ZEN_GPIO_TRIGGER_EDGE_HIGH:
		*regp = FCH_GPIO_GPIO_SET_LEVEL(*regp,
		    FCH_GPIO_GPIO_LEVEL_ACT_HIGH);
		*regp = FCH_GPIO_GPIO_SET_TRIG(*regp, FCH_GPIO_GPIO_TRIG_EDGE);
		break;
	case ZEN_GPIO_TRIGGER_EDGE_LOW:
		*regp = FCH_GPIO_GPIO_SET_LEVEL(*regp,
		    FCH_GPIO_GPIO_LEVEL_ACT_LOW);
		*regp = FCH_GPIO_GPIO_SET_TRIG(*regp, FCH_GPIO_GPIO_TRIG_EDGE);
		break;
	case ZEN_GPIO_TRIGGER_EDGE_BOTH:
		*regp = FCH_GPIO_GPIO_SET_LEVEL(*regp,
		    FCH_GPIO_GPIO_LEVEL_ACT_BOTH);
		*regp = FCH_GPIO_GPIO_SET_TRIG(*regp, FCH_GPIO_GPIO_TRIG_EDGE);
		break;
	case ZEN_GPIO_TRIGGER_LEVEL_HIGH:
		*regp = FCH_GPIO_GPIO_SET_LEVEL(*regp,
		    FCH_GPIO_GPIO_LEVEL_ACT_HIGH);
		*regp = FCH_GPIO_GPIO_SET_TRIG(*regp, FCH_GPIO_GPIO_TRIG_LEVEL);
		break;
	case ZEN_GPIO_TRIGGER_LEVEL_LOW:
		*regp = FCH_GPIO_GPIO_SET_LEVEL(*regp,
		    FCH_GPIO_GPIO_LEVEL_ACT_LOW);
		*regp = FCH_GPIO_GPIO_SET_TRIG(*regp, FCH_GPIO_GPIO_TRIG_LEVEL);
		break;
	default:
		fnvlist_add_uint32(errs, nvpair_name(pair),
		    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_VAL);
		return (false);
	}
	return (true);
}

static const zen_gpio_attr_table_t zen_gpio_attr_set[] = {
	{ ZEN_GPIO_ATTR_NAME, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_PAD_NAME, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_PAD_TYPE, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_PIN, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_CAPS, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_OUTPUT_DRIVER, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_INPUT, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_VOLTAGE, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_STATUS, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_RAW_REG, zen_gpio_attr_set_ro },
	{ ZEN_GPIO_ATTR_OUTPUT, zen_gpio_attr_set_output },
	{ ZEN_GPIO_ATTR_PULL, zen_gpio_attr_set_pull },
	{ ZEN_GPIO_ATTR_DRIVE_STRENGTH, zen_gpio_attr_set_str },
	{ ZEN_GPIO_ATTR_DEBOUNCE_MODE, zen_gpio_attr_set_dbt_mode },
	{ ZEN_GPIO_ATTR_DEBOUNCE_UNIT, zen_gpio_attr_set_dbt_unit },
	{ ZEN_GPIO_ATTR_DEBOUNCE_COUNT, zen_gpio_attr_set_dbt_count },
	{ ZEN_GPIO_ATTR_TRIGGER_MODE, zen_gpio_attr_set_trig }
};

static int
zen_gpio_op_attr_set(void *arg, uint32_t gpio_id, nvlist_t *nvl, nvlist_t *errs)
{
	zen_gpio_t *zg = arg;
	const zen_gpio_pindata_t *pin;
	uint32_t val;
	int ret;
	bool valid = true;

	pin = &zg->zg_pindata[gpio_id];
	ret = zen_gpio_read_reg(zg, pin, &val);
	if (ret != 0) {
		return (ret);
	}

	/*
	 * Now we need to walk each pair in the nvlist, see if it's something
	 * that we know, verify that the property is valid, that it is writable,
	 * and then construct a new value to write.
	 */
	for (nvpair_t *nvpair = nvlist_next_nvpair(nvl, NULL); nvpair != NULL;
	    nvpair = nvlist_next_nvpair(nvl, nvpair)) {
		const char *name = nvpair_name(nvpair);
		bool found = false;

		for (size_t i = 0; i < ARRAY_SIZE(zen_gpio_attr_set); i++) {
			if (strcmp(name, zen_gpio_attr_set[i].zat_attr) != 0) {
				continue;
			}

			found = true;
			if (!zen_gpio_attr_set[i].zat_proc(zg, pin, nvpair,
			    errs, &val)) {
				valid = false;
			}
			break;
		}

		if (!found) {
			fnvlist_add_uint32(errs, name,
			    (uint32_t)KGPIO_ATTR_ERR_UNKNOWN_ATTR);
			valid = false;
		}
	}

	if (!valid) {
		return (EINVAL);
	}

	return (zen_gpio_write_reg(zg, pin, val));
}

static int
zen_gpio_op_attr_cap(void *arg, uint32_t gpio_id, dpio_caps_t *caps)
{
	/*
	 * We don't support the interrupt yet, as such we only indicate read and
	 * write. All GPIOs currently support the same features. When we have to
	 * consider interrupt support, we need to look at both:
	 *
	 *  o Do we have an interrupt enabled in the FCH
	 *  o Do we have a GPIO that is capable of interrupt support
	 */
	*caps = DPIO_C_READ | DPIO_C_WRITE;

	return (0);
}

static int
zen_gpio_op_attr_dpio_input(void *arg, uint32_t gpio_id, dpio_input_t *input)
{
	int ret;
	uint32_t val;
	zen_gpio_t *zg = arg;

	ret = zen_gpio_read_reg(zg, &zg->zg_pindata[gpio_id], &val);
	if (ret != 0) {
		return (ret);
	}

	if (FCH_GPIO_GPIO_GET_INPUT(val) == FCH_GPIO_GPIO_OUTPUT_LOW) {
		*input = DPIO_INPUT_LOW;
	} else {
		*input = DPIO_INPUT_HIGH;
	}

	return (0);
}

static int
zen_gpio_op_attr_dpio_output_state(void *arg, uint32_t gpio_id,
    dpio_output_t *output)
{
	int ret;
	uint32_t val;
	zen_gpio_t *zg = arg;

	ret = zen_gpio_read_reg(zg, &zg->zg_pindata[gpio_id], &val);
	if (ret != 0) {
		return (ret);
	}

	if (FCH_GPIO_GPIO_GET_OUT_EN(val) == 0) {
		*output = DPIO_OUTPUT_DISABLE;
	} else if (FCH_GPIO_GPIO_GET_OUTPUT(val) != 0) {
		*output = DPIO_OUTPUT_HIGH;
	} else {
		*output = DPIO_OUTPUT_LOW;
	}

	return (0);
}

static int
zen_gpio_op_attr_dpio_output(void *arg, uint32_t gpio_id,
    dpio_output_t output)
{
	int ret;
	uint32_t val;
	zen_gpio_t *zg = arg;
	const zen_gpio_pindata_t *pin;

	pin = &zg->zg_pindata[gpio_id];

	/*
	 * We can't drive this set of i2c pins high, so error.
	 */
	if ((zg->zg_flags & ZEN_GPIO_F_I2C_NO_PP) != 0 &&
	    pin->zg_pad == ZEN_GPIO_PAD_TYPE_I2C &&
	    output == DPIO_OUTPUT_HIGH) {
		return (ENOTSUP);
	}

	ret = zen_gpio_read_reg(zg, &zg->zg_pindata[gpio_id], &val);
	if (ret != 0) {
		return (ret);
	}

	switch (output) {
	case DPIO_OUTPUT_LOW:
		val = FCH_GPIO_GPIO_SET_OUT_EN(val, 1);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val,
		    FCH_GPIO_GPIO_OUTPUT_LOW);
		break;
	case DPIO_OUTPUT_HIGH:
		val = FCH_GPIO_GPIO_SET_OUT_EN(val, 1);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val,
		    FCH_GPIO_GPIO_OUTPUT_HIGH);
		break;
	case DPIO_OUTPUT_DISABLE:
		val = FCH_GPIO_GPIO_SET_OUT_EN(val, 0);
		break;
	default:
		return (EINVAL);
	}

	return (zen_gpio_write_reg(zg, pin, val));
}

static const kgpio_ops_t zen_gpio_ops = {
	.kgo_name2id = zen_gpio_op_name2id,
	.kgo_get = zen_gpio_op_attr_get,
	.kgo_set = zen_gpio_op_attr_set,
	.kgo_cap = zen_gpio_op_attr_cap,
	.kgo_input = zen_gpio_op_attr_dpio_input,
	.kgo_output_state = zen_gpio_op_attr_dpio_output_state,
	.kgo_output = zen_gpio_op_attr_dpio_output
};

static bool
zen_gpio_identify(zen_gpio_t *zg)
{

	/*
	 * For the moment we always assume that we're on df 0. This will change
	 * once we're a child of huashan and we get our register properties and
	 * instances that way.
	 */
	zg->zg_dfno = 0;
	zg->zg_family = chiprev_family(cpuid_getchiprev(CPU));
	zg->zg_pindata = amdzen_data_pininfo(&zg->zg_ngpios);
	if (zg->zg_pindata == NULL) {
		dev_err(zg->zg_dip, CE_WARN, "!chiprev family 0x%x is not "
		    "supported: missing gpio data table", zg->zg_family);
		return (false);
	}

	/*
	 * As all currently supported systems support accessing GPIOs over the
	 * SMN, we flag that here for everything. If support for other systems
	 * is added, move the flag into the switch statement above.
	 */
	switch (zg->zg_family) {
	case X86_PF_AMD_ROME:
	case X86_PF_AMD_MILAN:
	case X86_PF_AMD_GENOA:
		zg->zg_flags |= ZEN_GPIO_F_USE_SMN;
		break;
	case X86_PF_AMD_NAPLES:
	case X86_PF_HYGON_DHYANA:
	case X86_PF_AMD_PINNACLE_RIDGE:
	case X86_PF_AMD_RAVEN_RIDGE:
	case X86_PF_AMD_PICASSO:
	case X86_PF_AMD_DALI:
	case X86_PF_AMD_RENOIR:
	case X86_PF_AMD_MATISSE:
	case X86_PF_AMD_VAN_GOGH:
	case X86_PF_AMD_MENDOCINO:
	case X86_PF_AMD_VERMEER:
	case X86_PF_AMD_REMBRANDT:
	case X86_PF_AMD_CEZANNE:
	case X86_PF_AMD_RAPHAEL:
		dev_err(zg->zg_dip, CE_WARN, "!chiprev family 0x%x is not "
		    "supported: no MMIO gpio support", zg->zg_family);
		return (false);
	default:
		dev_err(zg->zg_dip, CE_WARN, "!chiprev family 0x%x is not "
		    "supported: missing SMN vs. MMIO info", zg->zg_family);
		return (false);
	}

	/*
	 * Next go through and identify if this family supports the weird I2C
	 * mode where it's forced open-drain or not. These platforms also give
	 * you the ability to control the pull-up strength.
	 */
	switch (zg->zg_family) {
	case X86_PF_AMD_NAPLES:
	case X86_PF_HYGON_DHYANA:
	case X86_PF_AMD_PINNACLE_RIDGE:
	case X86_PF_AMD_RAVEN_RIDGE:
	case X86_PF_AMD_ROME:
	case X86_PF_AMD_MILAN:
		zg->zg_flags |= ZEN_GPIO_F_I2C_NO_PP;
		break;
	case X86_PF_AMD_PICASSO:
	case X86_PF_AMD_DALI:
	case X86_PF_AMD_RENOIR:
	case X86_PF_AMD_MATISSE:
	case X86_PF_AMD_VAN_GOGH:
	case X86_PF_AMD_MENDOCINO:
	case X86_PF_AMD_GENOA:
	case X86_PF_AMD_VERMEER:
	case X86_PF_AMD_REMBRANDT:
	case X86_PF_AMD_CEZANNE:
	case X86_PF_AMD_RAPHAEL:
		break;
	default:
		dev_err(zg->zg_dip, CE_WARN, "!chiprev family 0x%x is not "
		    "supported: missing i2c behavior", zg->zg_family);
		return (false);
	}

	return (true);
}

static void
zen_gpio_cleanup(zen_gpio_t *zg)
{
	kmem_free(zg, sizeof (zen_gpio_t));
}

static int
zen_gpio_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int ret;
	zen_gpio_t *zg;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	zg = kmem_zalloc(sizeof (zen_gpio_t), KM_SLEEP);
	zg->zg_dip = dip;

	if (!zen_gpio_identify(zg)) {
		goto err;
	}

	ret = kgpio_register(dip, &zen_gpio_ops, zg, zg->zg_ngpios);
	if (ret != 0) {
		dev_err(dip, CE_WARN, "failed to register with kgpio "
		    "interface: %d\n", ret);
		goto err;
	}

	ddi_set_driver_private(dip, zg);
	return (DDI_SUCCESS);

err:
	zen_gpio_cleanup(zg);
	return (DDI_FAILURE);
}

static int
zen_gpio_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int ret;
	zen_gpio_t *zg;

	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	zg = ddi_get_driver_private(dip);
	if (zg == NULL) {
		dev_err(dip, CE_WARN, "asked to detach instance with no state");
		return (DDI_FAILURE);
	}

	ASSERT3P(dip, ==, zg->zg_dip);

	ret = kgpio_unregister(zg->zg_dip);
	if (ret != 0) {
		dev_err(dip, CE_WARN, "failed to unregister from kgpio "
		    "framework: %d", ret);
		return (DDI_FAILURE);
	}

	zen_gpio_cleanup(zg);
	return (DDI_SUCCESS);
}

static struct dev_ops zen_gpio_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = zen_gpio_attach,
	.devo_detach = zen_gpio_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_needed,
};

static struct modldrv zen_gpio_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Zen GPIO Driver",
	.drv_dev_ops = &zen_gpio_dev_ops
};

static struct modlinkage zen_gpio_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &zen_gpio_modldrv, NULL }
};

int
_init(void)
{
	return (mod_install(&zen_gpio_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&zen_gpio_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&zen_gpio_modlinkage));
}
