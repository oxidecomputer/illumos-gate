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

#ifndef _SYS_IO_ZEN_CCX_H
#define	_SYS_IO_ZEN_CCX_H

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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
struct zen_thread;
struct zen_core;
struct zen_ccx;
struct zen_ccd;

typedef struct zen_thread zen_thread_t;
typedef struct zen_core zen_core_t;
typedef struct zen_ccx zen_ccx_t;
typedef struct zen_ccd zen_ccd_t;

/*
 * Most walkers are hidden, but this one is called from
 * the apix code.
 */
typedef int (*zen_thread_cb_f)(zen_thread_t *, void *);

extern void zen_ccx_mmio_init(uint64_t, boolean_t);
extern void zen_ccx_physmem_init(void);
extern bool zen_ccx_start_thread(const zen_thread_t *);
extern void zen_ccx_init(void);

extern int zen_walk_thread(zen_thread_cb_f, void *);

extern zen_thread_t *zen_fabric_find_thread_by_cpuid(uint32_t);
extern apicid_t zen_thread_apicid(const zen_thread_t *);

extern smn_reg_t zen_core_reg(const zen_core_t *const, const smn_reg_def_t);
extern smn_reg_t zen_ccd_reg(const zen_ccd_t *const, const smn_reg_def_t);
extern uint32_t zen_ccd_read(zen_ccd_t *, const smn_reg_t);
extern void zen_ccd_write(zen_ccd_t *, const smn_reg_t, const uint32_t);
extern uint32_t zen_core_read(zen_core_t *, const smn_reg_t);
extern void zen_core_write(zen_core_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_H */
