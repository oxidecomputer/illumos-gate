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
 */
#define	ZEN_MAX_CCDS_PER_IODIE		16
#define	ZEN_MAX_CCXS_PER_CCD		1
#define	ZEN_MAX_CORES_PER_CCX		16
#define	ZEN_MAX_THREADS_PER_CORE	2

/*
 * All Zen SoCs supported on the Oxide platform have at most 8 CCMs.
 */
#define	ZEN_MAX_CCMS_PER_IODIE		8

/*
 * All Zen SoCs supported on the Oxide platform have the same Instance ID for
 * the first CCM.
 */
#define	ZEN_DF_FIRST_CCM_ID		0x10

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

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_IMPL_H */
