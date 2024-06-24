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

#ifndef _SYS_IO_ZEN_CCX_IMPL_H
#define	_SYS_IO_ZEN_CCX_IMPL_H

/*
 * Structure and register definitions for the resources contained on the
 * core-complex dies (CCDs), including the core complexes (CCXs) themselves
 * and the cores and constituent compute threads they contain.
 */

#include <sys/apic.h>
#include <sys/bitext.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum Zen cores/thread parameters for Milan.  Naples and Rome each have
 * up to 4 cores per CCX and 2 CCXs per CCD; Naples always has 1 CCD per
 * IO die as they were colocated.  Supporting Rome or other old processor
 * packages requires generalizing these parameters.  CCX == L3.
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
#define	ZEN_MAX_CCDS_PER_IODIE		8
#define	ZEN_MAX_CCXS_PER_CCD		1
#define	ZEN_MAX_CORES_PER_CCX		8
#define	ZEN_MAX_THREADS_PER_CORE	2

struct zen_thread {
	uint8_t		zt_threadno;
	apicid_t	zt_apicid;
	zen_core_t	*zt_core;
};

struct zen_core {
	uint8_t		zc_logical_coreno;
	uint8_t		zc_physical_coreno;
	uint8_t		zc_nthreads;
	zen_thread_t	zc_threads[ZEN_MAX_THREADS_PER_CORE];
	zen_ccx_t	*zc_ccx;
};

struct zen_ccx {
	uint8_t		zcx_logical_cxno;
	uint8_t		zcx_physical_cxno;
	uint8_t		zcx_ncores;
	zen_core_t	zcx_cores[ZEN_MAX_CORES_PER_CCX];
	zen_ccd_t	*zcx_ccd;
};

struct zen_ccd {
	uint8_t		zcd_logical_dieno;
	uint8_t		zcd_physical_dieno;
	uint8_t		zcd_ccm_fabric_id;
	uint8_t		zcd_ccm_comp_id;
	uint8_t		zcd_nccxs;
	zen_ccx_t	zcd_ccxs[ZEN_MAX_CCXS_PER_CCD];
	zen_iodie_t	*zcd_iodie;
};

extern size_t zen_fabric_thread_get_brandstr(const zen_thread_t *,
    char *, size_t);
extern void zen_fabric_thread_get_dpm_weights(const zen_thread_t *,
    const uint64_t **, uint32_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_IMPL_H */
