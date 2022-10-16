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

#ifndef _ZEN_GPIO_IMPL_H
#define	_ZEN_GPIO_IMPL_H

/*
 * Implementation details of the Zen GPIO driver.
 */

#include <sys/types.h>
#include <sys/gpio/zen_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char *zg_name;
	const char *zg_signal;
	const char *zg_pin;
	uint32_t zg_id;
	zen_gpio_cap_t zg_cap;
	zen_gpio_pad_type_t zg_pad;
	zen_gpio_voltage_t zg_voltage;
} zen_gpio_pindata_t;

extern const zen_gpio_pindata_t zen_gpio_sp3_data[];
extern const zen_gpio_pindata_t zen_gpio_sp5_data[];

extern const size_t zen_gpio_sp3_nents;
extern const size_t zen_gpio_sp5_nents;

#ifdef __cplusplus
}
#endif

#endif /* _ZEN_GPIO_IMPL_H */
