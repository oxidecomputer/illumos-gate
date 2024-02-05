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
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef _SYS_IO_GENOA_HACKS_H
#define	_SYS_IO_GENOA_HACKS_H

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/io/genoa/fabric.h>
#include <sys/kernel_ipcc.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool xxxhackymchackface;

typedef enum genoa_board_type {
	MBT_ANY,
	MBT_RUBY,
	MBT_COSMO,
} genoa_board_type_t;

/*
 * Here is a temporary rough heuristic for determining what board we're on.
 */
static inline genoa_board_type_t
genoa_board_type(void)
{
	return (ipcc_enable ? MBT_COSMO : MBT_RUBY);
}

extern bool genoa_fixup_i2c_clock(void);
extern bool genoa_cgpll_set_ssc(bool);
extern void genoa_shutdown_detect_init(void);
extern void genoa_check_furtive_reset(void);

/*
 * Used internally by genoa_hack_gpio().  Do not use outside this code.
 */
typedef enum genoa_hack_gpio_op {
	GHGOP_CONFIGURE,
	GHGOP_RESET,
	GHGOP_SET,
	GHGOP_TOGGLE
} genoa_hack_gpio_op_t;

extern void genoa_hack_gpio(genoa_hack_gpio_op_t, uint16_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_HACKS_H */
