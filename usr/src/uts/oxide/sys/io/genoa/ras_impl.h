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

#ifndef	_SYS_IO_GENOA_RAS_IMPL_H
#define	_SYS_IO_GENOA_RAS_IMPL_H

/*
 * Genoa-specific declarations for RAS initialization.
 */

#include <sys/io/genoa/ras.h>
#include <sys/io/zen/ras_impl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const zen_ras_init_data_t genoa_ras_init_data;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IO_GENOA_RAS_IMPL_H */
