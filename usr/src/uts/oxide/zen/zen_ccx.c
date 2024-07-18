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
 * aspects of a CPU core common across Zen family processors.
 */

#include <zen/physaddrs.h>
#include <sys/boot_physmem.h>


void
zen_ccx_physmem_init(void)
{
	/*
	 * Due to undocumented, unspecified, and unknown bugs in the IOMMU
	 * (supposedly), there is a hole in RAM below 1 TiB.  It may or may not
	 * be usable as MMIO space but regardless we need to not treat it as
	 * RAM.
	 */
	eb_physmem_reserve_range(ZEN_PHYSADDR_IOMMU_HOLE,
	    ZEN_PHYSADDR_IOMMU_HOLE_END - ZEN_PHYSADDR_IOMMU_HOLE,
	    EBPR_NOT_RAM);
}