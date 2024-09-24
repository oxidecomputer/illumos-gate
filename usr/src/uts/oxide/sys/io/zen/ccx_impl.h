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

#ifndef	_SYS_IO_ZEN_CCX_IMPL_H
#define	_SYS_IO_ZEN_CCX_IMPL_H

/*
 * This file includes constants, type definitions, and prototypes that are
 * microachitecture-independent and used in the CCX implementation.
 */

#include <sys/apic.h>
#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Current architectural limits -- these are the maximum across all Zen SoCs
 * supported on the Oxide platform.
 *
 * Namespaces
 *
 * Each CCD, CCX, and core shares two distinct integer namespaces with its
 * siblings: a compact logical one and a possibly sparse physical one.  These
 * names are unique among siblings but not across e.g. cousins.  Both names are
 * provided to us for each object by the DF and APOB, and which name is used
 * to compute a register or bit address varies from one register to the next.
 * Therefore we need, and keep, both of them.  The logical name should always
 * correspond to the index into the parent's array.
 *
 * Threads are different: each core has some number of threads which in current
 * implementations is either 1 or 2.  There is no separate physical thread
 * identifier as there is no way for some discontiguous subset of threads to
 * exist.  Therefore each thread has but a single logical identifier, also its
 * index within its parent core's array of them.  However, the thread also has
 * an APIC ID, which unlike the other identifiers is globally unique across the
 * entire fabric.  The APIC ID namespace is sparse when any of a thread's
 * containing entities is one of a collection of siblings whose number is not
 * a power of 2.
 *
 * One last note on APIC IDs: while we compute the APIC ID that is assigned to
 * each thread by firmware prior to boot, that ID can be changed by writing to
 * the thread's APIC ID MSR (or, in xAPIC mode which we never use, the
 * analogous MMIO register).  The one we compute and store here is the one
 * set by firmware before boot.
 */
#define	ZEN_MAX_CCDS_PER_IODIE		16
#define	ZEN_MAX_CCXS_PER_CCD		1
#define	ZEN_MAX_CORES_PER_CCX		16
#define	ZEN_MAX_THREADS_PER_CORE	2

extern const bool zen_ccx_set_undoc_fields;

struct zen_thread {
	/*
	 * The thread number of this hardware thread.  This is always a small
	 * integer, either 0 or 1, though designs with larger numbers of
	 * hardware threads per core have existed in the past: for instance,
	 * SPARC Niagra had 8 threads per core.
	 */
	uint8_t		zt_threadno;

	/*
	 * The APIC ID for this thread.  This is a globally unique
	 * identifier for this particular thread.
	 */
	apicid_t	zt_apicid;

	zen_core_t	*zt_core;
};

struct zen_core {
	/*
	 * The logical core identifier for this core within its CCX.  This is a
	 * sequential integer, starting from 0.
	 */
	uint8_t		zc_logical_coreno;

	/*
	 * The physical core identifier for this core.  This may be sparse.
	 */
	uint8_t		zc_physical_coreno;

	uint8_t		zc_nthreads;
	zen_thread_t	zc_threads[ZEN_MAX_THREADS_PER_CORE];

	zen_ccx_t	*zc_ccx;
};

struct zen_ccx {
	/*
	 * The logical identifier for this core-complex.  This is a
	 * sequential identifier, origin 0.
	 */
	uint8_t		zcx_logical_cxno;

	/*
	 * The physical identifier for this core-complex.  This may
	 * be sparse.
	 */
	uint8_t		zcx_physical_cxno;

	uint8_t		zcx_ncores;
	zen_core_t	zcx_cores[ZEN_MAX_CORES_PER_CCX];

	zen_ccd_t	*zcx_ccd;
};

struct zen_ccd {
	/*
	 * The logical die number for this CCD.  This is a sequential
	 * identifier; origin 0.
	 */
	uint8_t		zcd_logical_dieno;

	/*
	 * The physical die number for this CCD.  This may be sparse.
	 */
	uint8_t		zcd_physical_dieno;

	uint8_t		zcd_nccxs;
	zen_ccx_t	zcd_ccxs[ZEN_MAX_CCXS_PER_CCD];

	zen_iodie_t	*zcd_iodie;
};

typedef int (*zen_ccd_cb_f)(zen_ccd_t *, void *);
typedef int (*zen_ccx_cb_f)(zen_ccx_t *, void *);
typedef int (*zen_core_cb_f)(zen_core_t *, void *);

/*
 * A no-op callback for use when a particular CCX initialization hook is not
 * required for a given microarchitecture.
 */
extern void zen_ccx_init_noop(void);

static inline void
wrmsr_and_test(uint32_t msr, uint64_t v)
{
	wrmsr(msr, v);

#ifdef	DEBUG
	uint64_t rv = rdmsr(msr);

	if (rv != v) {
		cmn_err(CE_PANIC, "MSR 0x%x written with value 0x%lx "
		    "has value 0x%lx\n", msr, v, rv);
	}
#endif
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_IMPL_H */
