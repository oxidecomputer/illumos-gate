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

#ifndef	_SYS_IO_ZEN_HACKS_H
#define	_SYS_IO_ZEN_HACKS_H

#include <sys/types.h>
#include <sys/stdbool.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_hack_ops {
	void	(*zho_check_furtive_reset)(void);
	bool	(*zho_cgpll_set_ssc)(bool);
	void	(*zho_shutdown_detect_init)(void);
} zen_hack_ops_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_HACKS_H */
