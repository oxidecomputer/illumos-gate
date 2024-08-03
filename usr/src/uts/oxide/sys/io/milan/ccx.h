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

#ifndef _SYS_IO_MILAN_CCX_H
#define	_SYS_IO_MILAN_CCX_H

/*
 * Structure and register definitions for the resources contained on the
 * core-complex dies (CCDs), including the core complexes (CCXs) themselves and
 * the cores and constituent compute threads they contain.
 */

#include <sys/apic.h>
#include <sys/bitext.h>
#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/ccd.h>
#include <sys/io/zen/ccx.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */

extern bool milan_ccx_start_thread(const zen_thread_t *);
extern void milan_ccx_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_CCX_H */
