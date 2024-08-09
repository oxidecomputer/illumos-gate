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

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/boot_physmem.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/ccx.h>

#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/physaddrs.h>


void
zen_ccx_init(void)
{
	const zen_ccx_ops_t *ccx_ops = oxide_zen_ccx_ops();
	VERIFY3P(ccx_ops->zco_init, !=, NULL);
	(ccx_ops->zco_init)();
}

void
zen_ccx_physmem_init(void)
{
	const zen_ccx_ops_t *ccx_ops = oxide_zen_ccx_ops();

	/*
	 * Due to undocumented, unspecified, and unknown bugs in the IOMMU
	 * (supposedly), there is a hole in RAM below 1 TiB.  It may or may not
	 * be usable as MMIO space but regardless we need to not treat it as
	 * RAM.
	 */
	eb_physmem_reserve_range(ZEN_PHYSADDR_IOMMU_HOLE,
	    ZEN_PHYSADDR_IOMMU_HOLE_END - ZEN_PHYSADDR_IOMMU_HOLE,
	    EBPR_NOT_RAM);

	/*
	 * Call microarchitecture-specific hook, if any.
	 */
	if (ccx_ops->zco_physmem_init != NULL)
		(ccx_ops->zco_physmem_init)();
}

void
zen_ccx_mmio_init(uint64_t pa, bool reserve)
{
	uint64_t val;

	val = AMD_MMIO_CFG_BASE_ADDR_SET_EN(0, 1);
	val = AMD_MMIO_CFG_BASE_ADDR_SET_BUS_RANGE(val,
	    AMD_MMIO_CFG_BASE_ADDR_BUS_RANGE_256);
	val = AMD_MMIO_CFG_BASE_ADDR_SET_ADDR(val,
	    pa >> AMD_MMIO_CFG_BASE_ADDR_ADDR_SHIFT);
	wrmsr(MSR_AMD_MMIO_CFG_BASE_ADDR, val);

	if (reserve) {
		eb_physmem_reserve_range(pa,
		    256UL << AMD_MMIO_CFG_BASE_ADDR_ADDR_SHIFT, EBPR_NOT_RAM);
	}
}

bool
zen_ccx_start_thread(const zen_thread_t *thread)
{
	const zen_ccx_ops_t *ccx_ops = oxide_zen_ccx_ops();
	VERIFY3P(ccx_ops->zco_start_thread, !=, NULL);
	return ((ccx_ops->zco_start_thread)(thread));
}

apicid_t
zen_thread_apicid(const zen_thread_t *thread)
{
	return (thread->zt_apicid);
}
