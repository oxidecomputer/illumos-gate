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
#include <sys/platform_detect.h>
#include <sys/x86_archext.h>

#include <sys/amdzen/ccd.h>
#include <sys/amdzen/ccx.h>

#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>


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

/*
 * In this context, "thread" == AP.  SMT may or may not be enabled (by HW, FW,
 * or our own controls).  That may affect the number of threads per core, but
 * doesn't otherwise change anything here.
 *
 * This function is one-way; once a thread has been enabled, we are told that
 * we must never clear this bit.  What happens if we do, I do not know.  If the
 * thread was already booted, this function does nothing and returns false;
 * otherwise it returns true and the AP will be started.  There is no way to
 * fail; we don't construct a zen_thread_t for hardware that doesn't exist, so
 * it's always possible to perform this operation if what we are handed points
 * to genuine data.
 *
 * See MP boot theory in os/mp_startup.c
 */
bool
zen_ccx_start_thread(const zen_thread_t *thread)
{
	const x86_uarchrev_t uarch = oxide_board_data->obd_cpuinfo.obc_uarchrev;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	zen_core_t *core = thread->zt_core;
	zen_ccx_t *ccx = core->zc_ccx;
	zen_ccd_t *ccd = ccx->zcx_ccd;
	smn_reg_t reg;
	uint8_t thr_ccd_idx;
	uint32_t en;

	VERIFY3U(CPU->cpu_id, ==, 0);

	/*
	 * The CCX spacing is based upon the total possible physical cores and
	 * threads in each CCX.
	 */
	thr_ccd_idx = ccx->zcx_logical_cxno;
	thr_ccd_idx *= consts->zpc_cores_per_ccx;
	thr_ccd_idx *= ZEN_MAX_THREADS_PER_CORE;
	thr_ccd_idx += core->zc_logical_coreno * core->zc_nthreads;
	thr_ccd_idx += thread->zt_threadno;

	VERIFY3U(thr_ccd_idx, <, ZEN_MAX_CCXS_PER_CCD *
	    consts->zpc_cores_per_ccx * ZEN_MAX_THREADS_PER_CORE);

	/*
	 * SMU::PWR::THREAD_ENABLE moved to L3::L3SOC::CcxThreadEnable0 in Zen5
	 * but the register layout is the same, hence we can use the same
	 * SMUPWR_THREAD_EN_{GET,SET}_T macros.
	 */
	switch (uarchrev_uarch(uarch)) {
	case X86_UARCH_AMD_ZEN3:
	case X86_UARCH_AMD_ZEN4:
		reg = SMUPWR_THREAD_EN(ccd->zcd_physical_dieno);
		break;
	case X86_UARCH_AMD_ZEN5:
		reg = L3SOC_THREAD_EN(ccd->zcd_physical_dieno);
		break;
	default:
		panic("Unsupported uarchrev 0x%x", uarch);
	}

	en = zen_ccd_read(ccd, reg);
	if (SMUPWR_THREAD_EN_GET_T(en, thr_ccd_idx) != 0)
		return (false);

	en = SMUPWR_THREAD_EN_SET_T(en, thr_ccd_idx);
	zen_ccd_write(ccd, reg, en);

	return (true);
}

apicid_t
zen_thread_apicid(const zen_thread_t *thread)
{
	return (thread->zt_apicid);
}
