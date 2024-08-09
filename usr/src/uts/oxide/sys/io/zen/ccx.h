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

#ifndef	_SYS_IO_ZEN_CCX_H
#define	_SYS_IO_ZEN_CCX_H

#include <sys/apic.h>
#include <sys/types.h>
#include <sys/stdbool.h>

/*
 * Provides type definitions and prototypes for working with CCX from common
 * parts of the Oxide architecture code, without a direct dependency on any
 * particular microarchitecture.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The definition of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_thread zen_thread_t;
typedef struct zen_core zen_core_t;
typedef struct zen_ccx zen_ccx_t;
typedef struct zen_ccd zen_ccd_t;

/*
 * Initialize the current CPU's (hwthread) thread-, core-, and CCX-specific
 * registers.
 */
extern void zen_ccx_init(void);

/*
 * Apply any physical memory reservations common to all supported Zen
 * microarchitectures and any microarchitecture-specific reservations.
 */
extern void zen_ccx_physmem_init(void);

/*
 * Enable PCIe ECAM access at the given address.
 */
extern void zen_ccx_mmio_init(uint64_t, bool);

/*
 * Start a (non-BSP) CPU/hwthread aka AP.
 * Returns false if the thread was already booted.
 */
extern bool zen_ccx_start_thread(const zen_thread_t *);

/* Walker callback function types */
typedef int (*zen_thread_cb_f)(zen_thread_t *, void *);

extern int zen_walk_thread(zen_thread_cb_f, void *);

extern zen_thread_t *zen_fabric_find_thread_by_cpuid(uint32_t);

extern apicid_t zen_thread_apicid(const zen_thread_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_H */
