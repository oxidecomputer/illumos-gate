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

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_thread zen_thread_t;
typedef struct zen_core zen_core_t;
typedef struct zen_ccx zen_ccx_t;
typedef struct zen_ccd zen_ccd_t;

/* Walker callback function types */
typedef int (*zen_thread_cb_f)(zen_thread_t *, void *);

extern int zen_walk_thread(zen_thread_cb_f, void *);

extern zen_thread_t *zen_fabric_find_thread_by_cpuid(uint32_t);

extern apicid_t zen_thread_apicid(const zen_thread_t *);

typedef struct zen_ccx_ops {
	void	(*zco_physmem_init)(void);
	void	(*zco_mmio_init)(uint64_t, bool);
	bool	(*zco_start_thread)(const zen_thread_t *);
} zen_ccx_ops_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_H */
