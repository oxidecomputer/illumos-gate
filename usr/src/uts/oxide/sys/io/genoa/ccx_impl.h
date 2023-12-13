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

#ifndef _SYS_IO_GENOA_CCX_IMPL_H
#define	_SYS_IO_GENOA_CCX_IMPL_H

/*
 * Structure and register definitions for the resources contained on
 * core-complex dies (CCDs), including the core complexes (CCXs) themselves
 * and the cores and constituent compute threads they contain.
 */

#include <sys/apic.h>
#include <sys/bitext.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/fabric.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum Zen cores/thread parameters for Genoa and Milan.  Naples and
 * Rome each have up to 4 cores per CCX and 2 CCXs per CCD; Naples always
 * has 1 CCD per IO die as they are colocated.
 *
 * Supporting Rome or other old processor packages requires generalizing
 * these parameters.  CCX == L3.
 *
 * Namespaces
 *
 * Each CCD, CCX, and core shares two distinct integer namespaces with its
 * siblings: a compact logical one and a possibly sparse physical one.  These
 * names are unique among siblings but not across e.g. cousins.  Both names are
 * provided to us for each object by the DF and APOB, and which name is used
 * to compute a register or bit address varies from one register to the next,
 * we must keep track of both of them.  Note that the logical name should always
 * correspond to the index into the parent's array.
 *
 * Threads are different: each core has some number of threads; in current
 * implementations this is either 1 or 2.  There is no separate physical thread
 * identifier as there is no way for some discontiguous subset of threads to
 * exist.  Therefore each thread only has a single logical identifier that is
 * also its index within its parent core's thread array.  However, the thread
 * also has an APIC ID, which unlike the other identifier,s is globally unique
 * across the entire fabric.  The APIC ID namespace is sparse when any of a
 * thread's containing entities is one of a set of siblings whose number of
 * elements is not a power of 2.
 *
 * One last note on APIC IDs: while we compute the APIC ID that is assigned to
 * each thread by firmware prior to boot, that ID can be changed by writing to
 * the thread's APIC ID register.  The one we compute and store here is the one
 * set by firmware before boot.
 */
#define	GENOA_MAX_CCDS_PER_IODIE	8
#define	GENOA_MAX_CCXS_PER_CCD		1
#define	GENOA_MAX_CORES_PER_CCX		8
#define	GENOA_MAX_THREADS_PER_CORE	2

struct genoa_thread {
	uint8_t			gt_threadno;
	apicid_t		gt_apicid;
	genoa_core_t		*gt_core;
};

struct genoa_core {
	uint8_t			gc_logical_coreno;
	uint8_t			gc_physical_coreno;
	uint8_t			gc_nthreads;
	genoa_thread_t		gc_threads[GENOA_MAX_THREADS_PER_CORE];
	genoa_ccx_t		*gc_ccx;
};

struct genoa_ccx {
	uint8_t			gcx_logical_cxno;
	uint8_t			gcx_physical_cxno;
	uint8_t			gcx_ncores;
	genoa_core_t		gcx_cores[GENOA_MAX_CORES_PER_CCX];
	genoa_ccd_t		*gcx_ccd;
};

struct genoa_ccd {
	uint8_t			gcd_logical_dieno;
	uint8_t			gcd_physical_dieno;
	uint8_t			gcd_ccm_fabric_id;
	uint8_t			gcd_ccm_comp_id;
	uint8_t			gcd_nccxs;
	genoa_ccx_t		gcd_ccxs[GENOA_MAX_CCXS_PER_CCD];
	genoa_iodie_t		*gcd_iodie;
};

extern size_t genoa_fabric_thread_get_brandstr(const genoa_thread_t *,
    char *, size_t);
extern void genoa_fabric_thread_get_dpm_weights(const genoa_thread_t *,
    const uint64_t **, uint32_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_CCX_IMPL_H */
