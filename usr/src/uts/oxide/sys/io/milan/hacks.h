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
 * Copyright 2024 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_HACKS_H
#define	_SYS_IO_MILAN_HACKS_H

#include <sys/types.h>
#include <sys/stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool milan_fixup_i2c_clock(void);
extern bool milan_cgpll_set_ssc(bool);
extern void milan_check_furtive_reset(void);

/*
 * Used internally by milan_hack_gpio().  Do not use outside this code.
 */
typedef enum milan_hack_gpio_op {
	MHGOP_CONFIGURE,
	MHGOP_RESET,
	MHGOP_SET,
	MHGOP_TOGGLE
} milan_hack_gpio_op_t;

extern void milan_hack_gpio(milan_hack_gpio_op_t, uint16_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_HACKS_H */
