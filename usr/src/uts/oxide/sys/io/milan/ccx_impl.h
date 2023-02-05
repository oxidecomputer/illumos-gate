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

#ifndef _SYS_IO_MILAN_CCX_IMPL_H
#define	_SYS_IO_MILAN_CCX_IMPL_H

/*
 * Structure and register definitions for the resources contained on the
 * core-complex dies (CCDs), including the core complexes (CCXs) themselves and
 * the cores and constituent compute threads they contain.
 */

#include <sys/apic.h>
#include <sys/bitext.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/io/milan/ccx.h>
#include <sys/io/milan/fabric.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum Zen cores/thread parameters for Milan.  Naples and Rome each have
 * up to 4 cores per CCX and 2 CCXs per CCD; Naples always has 1 CCD per
 * IO die as they were colocated.  Supporting Rome or other old processor
 * packages requires generalising these parameters.  CCX == L3.
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
#define	MILAN_MAX_CCDS_PER_IODIE	8
#define	MILAN_MAX_CCXS_PER_CCD		1
#define	MILAN_MAX_CORES_PER_CCX		8
#define	MILAN_MAX_THREADS_PER_CORE	2

struct milan_thread {
	uint8_t			mt_threadno;
	apicid_t		mt_apicid;
	milan_core_t		*mt_core;
};

struct milan_core {
	uint8_t			mc_logical_coreno;
	uint8_t			mc_physical_coreno;
	uint8_t			mc_nthreads;
	milan_thread_t		mc_threads[MILAN_MAX_THREADS_PER_CORE];
	milan_ccx_t		*mc_ccx;
};

struct milan_ccx {
	uint8_t			mcx_logical_cxno;
	uint8_t			mcx_physical_cxno;
	uint8_t			mcx_ncores;
	milan_core_t		mcx_cores[MILAN_MAX_CORES_PER_CCX];
	milan_ccd_t		*mcx_ccd;
};

struct milan_ccd {
	uint8_t			mcd_logical_dieno;
	uint8_t			mcd_physical_dieno;
	uint8_t			mcd_ccm_fabric_id;
	uint8_t			mcd_ccm_comp_id;
	uint8_t			mcd_nccxs;
	milan_ccx_t		mcd_ccxs[MILAN_MAX_CCXS_PER_CCD];
	milan_iodie_t		*mcd_iodie;
};

extern size_t milan_fabric_thread_get_brandstr(const milan_thread_t *,
    char *, size_t);
extern void milan_fabric_thread_get_dpm_weights(const milan_thread_t *,
    const uint64_t **, uint32_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_CCX_IMPL_H */
