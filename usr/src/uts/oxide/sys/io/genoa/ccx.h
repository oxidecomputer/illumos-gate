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

#ifndef _SYS_IO_GENOA_CCX_H
#define	_SYS_IO_GENOA_CCX_H

/*
 * Structure and register definitions for the resources contained on the
 * core-complex dies (CCDs), including the core complexes (CCXs) themselves and
 * the cores and constituent compute threads they contain.
 */

#include <sys/apic.h>
#include <sys/bitext.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/ccd.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The following types are exposed to implementers but not consumers,
 * which are only allowed to use pointers to objects of those types as
 * opaque handles; hence the forward-declarations.  The actual definitions
 * are in the corresponding *_impl.h header files.
 */
struct genoa_thread;
struct genoa_core;
struct genoa_ccx;
struct genoa_ccd;

typedef struct genoa_thread genoa_thread_t;
typedef struct genoa_core genoa_core_t;
typedef struct genoa_ccx genoa_ccx_t;
typedef struct genoa_ccd genoa_ccd_t;

extern void genoa_ccx_mmio_init(uint64_t, bool);
extern void genoa_ccx_physmem_init(void);
extern bool genoa_ccx_start_thread(const genoa_thread_t *);
extern void genoa_ccx_init(void);

/* Walker callback function types */
typedef int (*genoa_thread_cb_f)(genoa_thread_t *, void *);
typedef int (*genoa_ccd_cb_f)(genoa_ccd_t *, void *);
typedef int (*genoa_ccx_cb_f)(genoa_ccx_t *, void *);
typedef int (*genoa_core_cb_f)(genoa_core_t *, void *);

extern int genoa_walk_thread(genoa_thread_cb_f, void *);

extern genoa_thread_t *genoa_fabric_find_thread_by_cpuid(uint32_t);
extern apicid_t genoa_thread_apicid(const genoa_thread_t *);

extern smn_reg_t genoa_core_reg(const genoa_core_t *const, const smn_reg_def_t);
extern smn_reg_t genoa_ccd_reg(const genoa_ccd_t *const, const smn_reg_def_t);
extern uint32_t genoa_ccd_read(genoa_ccd_t *, const smn_reg_t);
extern void genoa_ccd_write(genoa_ccd_t *, const smn_reg_t, const uint32_t);
extern uint32_t genoa_core_read(genoa_core_t *, const smn_reg_t);
extern void genoa_core_write(genoa_core_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_CCX_H */
