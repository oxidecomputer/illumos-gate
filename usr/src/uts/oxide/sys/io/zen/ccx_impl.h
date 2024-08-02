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

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Current architectural limits
 */
#define	ZEN_MAX_CCXS_PER_CCD		1

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
	 * A pointer to the microarchitecturally specific data for
	 * this core.
	 */
	void		*zc_uarch_core;
};

struct zen_ccx {
	/*
	 * A pointer to the microarchitecturally specific data for this
	 * CCX.
	 */
	void		*zcx_uarch_ccx;
};

struct zen_ccd {
	/*
	 * The logical die number for this CCD.
	 */
	uint8_t		zcd_logical_dieno;

	/*
	 * The physical die number for this CCD.
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
};

extern void zen_ccx_physmem_init(void);
extern void zen_ccx_mmio_init(uint64_t, bool);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_CCX_IMPL_H */
