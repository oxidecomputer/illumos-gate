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

#ifndef _SYS_IO_MILAN_HACKS_H
#define	_SYS_IO_MILAN_HACKS_H

#ifdef __cplusplus
extern "C" {
#endif

extern boolean_t milan_fixup_i2c_clock(void);
extern boolean_t milan_cgpll_set_ssc(boolean_t);
extern void milan_shutdown_detect_init(void);
extern void milan_check_furtive_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_HACKS_H */
