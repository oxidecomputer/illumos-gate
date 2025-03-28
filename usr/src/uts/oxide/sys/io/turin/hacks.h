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

#ifndef _SYS_IO_TURIN_HACKS_H
#define	_SYS_IO_TURIN_HACKS_H

/*
 * Hacks that we have added for particular quirks in Turin.
 */

#include <sys/types.h>
#include <sys/stdbool.h>

#include <sys/io/zen/hacks.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool turin_cgpll_set_ssc(bool);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_HACKS_H */
