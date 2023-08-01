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

#ifndef _SYS_IO_MILAN_CCX_H
#define	_SYS_IO_MILAN_CCX_H

/*
 * Structure and register definitions for the resources contained on the
 * core-complex dies (CCDs), including the core complexes (CCXs) themselves and
 * the cores and constituent compute threads they contain.
 */

#include <sys/apic.h>
#include <sys/bitext.h>
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
struct milan_thread;
struct milan_core;
struct milan_ccx;
struct milan_ccd;

typedef struct milan_thread milan_thread_t;
typedef struct milan_core milan_core_t;
typedef struct milan_ccx milan_ccx_t;
typedef struct milan_ccd milan_ccd_t;

extern void milan_ccx_mmio_init(uint64_t, boolean_t);
extern void milan_ccx_physmem_init(void);
extern boolean_t milan_ccx_start_thread(const milan_thread_t *);
extern void milan_ccx_init(void);

/* Walker callback function types */
typedef int (*milan_thread_cb_f)(milan_thread_t *, void *);
typedef int (*milan_ccd_cb_f)(milan_ccd_t *, void *);
typedef int (*milan_ccx_cb_f)(milan_ccx_t *, void *);
typedef int (*milan_core_cb_f)(milan_core_t *, void *);

extern int milan_walk_thread(milan_thread_cb_f, void *);

extern milan_thread_t *milan_fabric_find_thread_by_cpuid(uint32_t);
extern apicid_t milan_thread_apicid(const milan_thread_t *);

extern smn_reg_t milan_core_reg(const milan_core_t *const, const smn_reg_def_t);
extern smn_reg_t milan_ccd_reg(const milan_ccd_t *const, const smn_reg_def_t);
extern uint32_t milan_ccd_read(milan_ccd_t *, const smn_reg_t);
extern void milan_ccd_write(milan_ccd_t *, const smn_reg_t, const uint32_t);
extern uint32_t milan_core_read(milan_core_t *, const smn_reg_t);
extern void milan_core_write(milan_core_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_CCX_H */
