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

#include <sys/apic.h>
#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Current architectural limits.
 *
 * XXX: This currently reflects Milan.  We know that this
 * expand in Genoa and Turin, but we're proceeding incrementally.
 */
#define	ZEN_MAX_CCDS_PER_IODIE		8
#define	ZEN_MAX_CCXS_PER_CCD		1
#define	ZEN_MAX_CORES_PER_CCX		8
#define	ZEN_MAX_THREADS_PER_CORE	2

/*
 * All Zen SoC supported on the Oxide platform have at most 8 CCMs.
 */
#define	ZEN_MAX_CCMS_PER_IODIE		8

/*
 * All Zen SoC supported on the Oxide platform have the same Instance ID for
 * the first CCM.
 */
#define	ZEN_DF_FIRST_CCM_ID		0x10

struct zen_thread {
	/*
	 * The thread number of this hardware thread.  This is always a
	 * small integer (currently either 0 or 1, though designs with
	 * larger numbers of hardware threads per core have existed in
	 * the past: for instance, SPARC Niagra had 8 threads per core).
	 */
	uint8_t		zt_threadno;

	/*
	 * The APIC ID for this thread.  This is a globally unique
	 * identifier for this particular thread.
	 */
	apicid_t	zt_apicid;

	/*
	 * A pointer to the core that this thread is a part of.
	 */
	zen_core_t	*zt_core;
};

struct zen_core {
	/*
	 * The logical core identifier for this core.  This is a sequential
	 * integer, starting from 0.
	 */
	uint8_t		zc_logical_coreno;

	/*
	 * The physical core identifier for this core.  This may be sparse.
	 */
	uint8_t		zc_physical_coreno;

	/*
	 * The number of hyperthreads for this core.
	 */
	uint8_t		zc_nthreads;

	/*
	 * The hyperthreads that are part of this core.
	 */
	zen_thread_t	zc_threads[ZEN_MAX_THREADS_PER_CORE];

	/*
	 * A pointer to the CCX that this core belongs to.
	 */
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

	/*
	 * The number of cores in this complex.
	 */
	uint8_t		zcx_ncores;

	/*
	 * The cores in this complex.
	 */
	zen_core_t	zcx_cores[ZEN_MAX_CORES_PER_CCX];

	/*
	 * A pointer to the CCD that this CCX is part of.
	 */
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

	/*
	 * The CCXs on this die.
	 */
	zen_ccx_t	zcd_ccxs[ZEN_MAX_CCXS_PER_CCD];

	/*
	 * The actual number of CCXes on this die, which may be different
	 * from the architectural maximum.
	 */
	uint8_t		zcd_nccxs;

	/*
	 * A pointer to the microarchitecturally specific IO die this
	 * CCD is attached to.
	 */
	zen_iodie_t	*zcd_iodie;
};

typedef int (*zen_ccd_cb_f)(zen_ccd_t *, void *);
typedef int (*zen_ccx_cb_f)(zen_ccx_t *, void *);
typedef int (*zen_core_cb_f)(zen_core_t *, void *);

extern void zen_ccx_physmem_init(void);
extern void zen_ccx_mmio_init(uint64_t, bool);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_IMPL_H */
