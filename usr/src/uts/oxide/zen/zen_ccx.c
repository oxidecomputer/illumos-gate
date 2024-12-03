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
#include <sys/cmn_err.h>
#include <sys/platform_detect.h>
#include <sys/x86_archext.h>

#include <sys/amdzen/ccd.h>
#include <sys/amdzen/ccx.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>


#define	ZEN_CCX_INIT(ops, func)						\
	do {								\
		VERIFY3P((ops)->zco_ ## func ## _init, !=, NULL);	\
		((ops)->zco_ ## func ## _init)();			\
	} while (0)


/*
 * The early platform detect logic should prevent us from running on a
 * completely bogus CPU (e.g., Intel/non-AMD or a pre-Zen AMD CPU). However, we
 * still want to be conservative as there are still some differences even within
 * a supported processor family. As such, each Zen platform declares its own
 * supported chip rev/steppings we'll check against during CCX init.
 *
 * To ease future porting, we provide this chicken switch (as a static const
 * since we run before kmdb loads).
 */
static const bool zen_ccx_allow_unsupported_processor = false;

/*
 * Set the contents of undocumented registers to what we imagine they should be.
 * This chicken switch and the next exist mainly to debug total mysteries, but
 * it's also entirely possible that our sketchy information about what these
 * should hold is just wrong (for this machine, or entirely).
 */
static const bool zen_ccx_set_undoc_regs = true;

/*
 * Set the contents of undocumented fields in otherwise documented registers to
 * what we imagine they should be.
 */
const bool zen_ccx_set_undoc_fields = true;


static bool
zen_ccx_is_supported(void)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	return (chiprev_matches(chiprev, consts->zpc_chiprev));
}

static void
zen_core_dpm_init(void)
{
	const zen_ccx_ops_t *ccx_ops = oxide_zen_ccx_ops();
	const zen_thread_t *thread = CPU->cpu_m.mcpu_hwthread;
	const uint64_t *weights;
	uint32_t nweights;
	uint64_t cfg;

	VERIFY3P(ccx_ops->zco_get_dpm_weights, !=, NULL);
	(ccx_ops->zco_get_dpm_weights)(thread, &weights, &nweights);

	if (nweights == 0)
		return;

	VERIFY3P(weights, !=, NULL);

	cfg = rdmsr(MSR_AMD_DPM_CFG);
	cfg = AMD_DPM_CFG_SET_CFG_LOCKED(cfg, 0);
	wrmsr_and_test(MSR_AMD_DPM_CFG, cfg);

	for (uint32_t idx = 0; idx < nweights; idx++) {
		wrmsr_and_test(MSR_AMD_DPM_WAC_ACC_INDEX, idx);
		wrmsr_and_test(MSR_AMD_DPM_WAC_DATA, weights[idx]);
	}

	cfg = AMD_DPM_CFG_SET_CFG_LOCKED(cfg, 1);
	wrmsr_and_test(MSR_AMD_DPM_CFG, cfg);
}

static void
zen_core_tw_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_TW_CFG);
	v = AMD_TW_CFG_SET_COMBINE_CR0_CD(v, 1);
	wrmsr_and_test(MSR_AMD_TW_CFG, v);
}

void
zen_ccx_init(void)
{
	const zen_ccx_ops_t *ccx_ops = oxide_zen_ccx_ops();
	const zen_thread_t *thread = CPU->cpu_m.mcpu_hwthread;
	char str[CPUID_BRANDSTR_STRLEN + 1];

	if (!zen_ccx_is_supported()) {
		const char *vendor;
		uint_t family, model, step;

		vendor = cpuid_getvendorstr(CPU);
		family = cpuid_getfamily(CPU);
		model = cpuid_getmodel(CPU);
		step = cpuid_getstep(CPU);

		cmn_err(zen_ccx_allow_unsupported_processor ?
		    CE_WARN : CE_PANIC,
		    "cpu%d is unsupported: vendor %s family 0x%x model 0x%x "
		    "step 0x%x", CPU->cpu_id, vendor, family, model, step);
	}

	/*
	 * Set the MSRs that control the brand string so that subsequent cpuid
	 * passes can retrieve it.  We fetched it during earlyboot fabric
	 * initialisation.
	 */
	if (zen_fabric_thread_get_brandstr(thread, str, sizeof (str)) <=
	    CPUID_BRANDSTR_STRLEN && str[0] != '\0') {
		for (uint_t n = 0; n < sizeof (str) / sizeof (uint64_t); n++) {
			uint64_t sv = *(uint64_t *)&str[n * sizeof (uint64_t)];

			wrmsr(MSR_AMD_PROC_NAME_STRING0 + n, sv);
		}
	} else {
		cmn_err(CE_WARN, "cpu%d: invalid brand string", CPU->cpu_id);
	}

	/*
	 * We're called here from every thread, but the CCX doesn't have an
	 * instance of every functional unit for each thread.  As an
	 * optimisation, we set up what's shared only once.  One would imagine
	 * that the sensible way to go about that is to always perform the
	 * initialisation on the first thread that shares the functional unit,
	 * but other implementations do it only on the last.  It's possible that
	 * this is a bug, or that the internal process of starting a thread
	 * clobbers (some of?) the changes we might make to the shared register
	 * instances before doing so.  On the processors we support, doing this
	 * on the first sharing thread to start seems to have the intended
	 * result, so that's what we do.  Callbacks are named for their scope.
	 *
	 * Note there's both a table walker configuration callback that is
	 * follows the above pattern and invoked on just the first thread and a
	 * common table walker configuration routine that applies to all
	 * supported Zen processors, zen_core_tw_init().  The latter when called
	 * causes CR0.CD to be effectively set on both threads if either thread
	 * has it set; since by default, a thread1 that hasn't started yet has
	 * this bit set, setting it on thread0 will cause everything to grind to
	 * a near halt.  Since the TW config bit has no effect without SMT, we
	 * don't need to worry about setting it on thread0 if SMT is off.
	 */
	ZEN_CCX_INIT(ccx_ops, thread_feature);
	ZEN_CCX_INIT(ccx_ops, thread_uc);
	if (thread->zt_threadno == 1)
		zen_core_tw_init();
	if (thread->zt_threadno == 0) {
		ZEN_CCX_INIT(ccx_ops, core_ls);
		ZEN_CCX_INIT(ccx_ops, core_ic);
		ZEN_CCX_INIT(ccx_ops, core_dc);
		ZEN_CCX_INIT(ccx_ops, core_de);
		ZEN_CCX_INIT(ccx_ops, core_fp);
		ZEN_CCX_INIT(ccx_ops, core_l2);
		ZEN_CCX_INIT(ccx_ops, core_tw);
		if (thread->zt_core->zc_logical_coreno == 0)
			ZEN_CCX_INIT(ccx_ops, ccx_l3);
		if (zen_ccx_set_undoc_regs)
			ZEN_CCX_INIT(ccx_ops, core_undoc);
		zen_core_dpm_init();
	}
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

/*
 * A no-op callback for use when a particular CCX initialization hook is not
 * required for a given microarchitecture.
 */
void
zen_ccx_init_noop(void)
{
}
