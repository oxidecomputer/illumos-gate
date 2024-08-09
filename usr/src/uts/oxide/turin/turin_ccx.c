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

/*
 * This file implements a collection of routines used to initialize various
 * aspects of a CPU core specific to Turin processors.
 */

#include <sys/boot_physmem.h>
#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/x86_archext.h>

#include <sys/io/zen/ccx_impl.h>


#define	TURIN_PHYSADDR_4G_64K_HOLE	0x100000000UL
#define	TURIN_PHYSADDR_4G_64K_HOLE_END	0x100010000UL


void
turin_ccx_init(void)
{
	cmn_err(CE_WARN, "Turin CCX initialization not yet implemented");
}

void
turin_ccx_physmem_init(void)
{
	/*
	 * For reasons unknown, the first 64 KiB after 4 GiB cannot be used as
	 * RAM. Attempting to read from those pages will return all 1s and all
	 * writes are ignored.
	 */
	if (MSR_AMD_TOM2_MASK(rdmsr(MSR_AMD_TOM2)) >=
	    TURIN_PHYSADDR_4G_64K_HOLE_END) {
		eb_physmem_reserve_range(TURIN_PHYSADDR_4G_64K_HOLE,
		    TURIN_PHYSADDR_4G_64K_HOLE_END - TURIN_PHYSADDR_4G_64K_HOLE,
		    EBPR_NOT_RAM);
	}
}
