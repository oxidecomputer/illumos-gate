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
 * Copyright 2023 Oxide Computer Company
 */

/*
 * This file contains routines that are be used to initialize various
 * aspects of Genoa CPU cores.
 */

#include <genoa/genoa_physaddrs.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/ccx_impl.h>
#include <sys/amdzen/ccx.h>
#include <sys/amdzen/smn.h>
#include <sys/boot_physmem.h>
#include <sys/x86_archext.h>
#include <sys/types.h>
#include <sys/stdbool.h>

/*
 * We run before kmdb loads, so these chicken switches are static consts.
 */
static const bool genoa_ccx_allow_unsupported_processor = false;

/*
 * Set the contents of undocumented registers to what we imagine they should be.
 * This chicken switch and the next exist mainly to debug total mysteries, but
 * it's also entirely possible that our sketchy information about what these
 * should hold is just wrong (for this machine, or entirely).
 */
static const bool genoa_ccx_set_undoc_regs = true;

/*
 * Set the contents of undocumented fields in otherwise documented registers to
 * what we imagine they should be.
 */
static const bool genoa_ccx_set_undoc_fields = true;

void
genoa_ccx_mmio_init(uint64_t pa, bool reserve)
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

void
genoa_ccx_physmem_init(void)
{
	/*
	 * Due to undocumented, unspecified, and unknown bugs in the IOMMU
	 * (supposedly), there is a hole in RAM below 1 TiB.  It may or may not
	 * be usable as MMIO space but regardless we need to not treat it as
	 * RAM.
	 */
	eb_physmem_reserve_range(GENOA_PHYSADDR_IOMMU_HOLE,
	    GENOA_PHYSADDR_IOMMU_HOLE_END - GENOA_PHYSADDR_IOMMU_HOLE,
	    EBPR_NOT_RAM);
}

smn_reg_t
genoa_core_reg(const genoa_core_t *const core, const smn_reg_def_t def)
{
	genoa_ccx_t *ccx = core->gc_ccx;
	genoa_ccd_t *ccd = ccx->gcx_ccd;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_SCFCTP:
		reg = amdzen_scfctp_smn_reg(ccd->gcd_physical_dieno,
		    ccx->gcx_physical_cxno, def, core->gc_physical_coreno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for core",
		    def.srd_unit);
	}

	return (reg);
}

smn_reg_t
genoa_ccd_reg(const genoa_ccd_t *const ccd, const smn_reg_def_t def)
{
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_SMUPWR:
		reg = amdzen_smupwr_smn_reg(ccd->gcd_physical_dieno, def, 0);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for CCD",
		    def.srd_unit);
	}

	return (reg);
}

uint32_t
genoa_ccd_read(genoa_ccd_t *ccd, const smn_reg_t reg)
{
	genoa_iodie_t *iodie = ccd->gcd_iodie;

	return (genoa_smn_read(iodie, reg));
}

void
genoa_ccd_write(genoa_ccd_t *ccd, const smn_reg_t reg, const uint32_t val)
{
	genoa_iodie_t *iodie = ccd->gcd_iodie;

	genoa_smn_write(iodie, reg, val);
}

uint32_t
genoa_ccx_read(genoa_ccx_t *ccx, const smn_reg_t reg)
{
	genoa_iodie_t *iodie = ccx->gcx_ccd->gcd_iodie;

	return (genoa_smn_read(iodie, reg));
}

void
genoa_ccx_write(genoa_ccx_t *ccx, const smn_reg_t reg, const uint32_t val)
{
	genoa_iodie_t *iodie = ccx->gcx_ccd->gcd_iodie;

	genoa_smn_write(iodie, reg, val);
}

uint32_t
genoa_core_read(genoa_core_t *core, const smn_reg_t reg)
{
	genoa_iodie_t *iodie = core->gc_ccx->gcx_ccd->gcd_iodie;

	return (genoa_smn_read(iodie, reg));
}

void
genoa_core_write(genoa_core_t *core, const smn_reg_t reg, const uint32_t val)
{
	genoa_iodie_t *iodie = core->gc_ccx->gcx_ccd->gcd_iodie;

	genoa_smn_write(iodie, reg, val);
}

/*
 * In this context, "thread" == AP.  SMT may or may not be enabled (by HW, FW,
 * or our own controls).  That may affect the number of threads per core, but
 * doesn't otherwise change anything here.
 *
 * This function is one-way; once a thread has been enabled, we are told that
 * we must never clear this bit.  What happens if we do, I do not know.  If the
 * thread was already booted, this function does nothing and returns B_FALSE;
 * otherwise it returns B_TRUE and the AP will be started.  There is no way to
 * fail; we don't construct a genoa_thread_t for hardware that doesn't exist, so
 * it's always possible to perform this operation if what we are handed points
 * to genuine data.
 *
 * See MP boot theory in os/mp_startup.c
 */
bool
genoa_ccx_start_thread(const genoa_thread_t *thread)
{
	genoa_core_t *core = thread->gt_core;
	genoa_ccx_t *ccx = core->gc_ccx;
	genoa_ccd_t *ccd = ccx->gcx_ccd;
	smn_reg_t reg;
	uint8_t thr_ccd_idx;
	uint32_t en;

	VERIFY3U(CPU->cpu_id, ==, 0);

	thr_ccd_idx = ccx->gcx_logical_cxno;
	thr_ccd_idx *= ccx->gcx_ncores;
	thr_ccd_idx += core->gc_logical_coreno;
	thr_ccd_idx *= core->gc_nthreads;
	thr_ccd_idx += thread->gt_threadno;

	VERIFY3U(thr_ccd_idx, <, GENOA_MAX_CCXS_PER_CCD *
	    GENOA_MAX_CORES_PER_CCX * GENOA_MAX_THREADS_PER_CORE);

	reg = genoa_ccd_reg(ccd, D_SMUPWR_THREAD_EN);
	en = genoa_ccd_read(ccd, reg);
	if (SMUPWR_THREAD_EN_GET_T(en, thr_ccd_idx) != 0)
		return (B_FALSE);

	en = SMUPWR_THREAD_EN_SET_T(en, thr_ccd_idx);
	genoa_ccd_write(ccd, reg, en);
	return (B_TRUE);
}

apicid_t
genoa_thread_apicid(const genoa_thread_t *thread)
{
	return (thread->gt_apicid);
}

bool
genoa_ccx_is_supported(void)
{
	x86_chiprev_t chiprev;

	if (genoa_ccx_allow_unsupported_processor)
		return (B_TRUE);

	chiprev = cpuid_getchiprev(CPU);
	return (chiprev_matches(chiprev, X86_CHIPREV_AMD_GENOA_ANY));
}

/*
 * This series of CCX subsystem initialisation routines is intended to
 * eventually be generalised out of Genoa to support arbitrary future
 * collections of processors.  Each sets up a particular functional unit within
 * the thread/core/core complex.  For reference, these are:
 *
 * LS: load-store, the gateway to the thread
 * IC: (L1) instruction cache
 * DC: (L1) data cache
 * TW: table walker (part of the MMU)
 * DE: instruction decode(/execute?)
 * L2, L3: caches
 * UC: microcode -- this is not microcode patch/upgrade
 *
 * Feature initialisation refers to setting up the internal registers that are
 * reflected into cpuid leaf values.
 *
 * All of these routines are infallible; we purposely avoid using on_trap() or
 * similar as we want to panic if any of these registers does not exist or
 * cannot be accessed.  Additionally, when building with DEBUG enabled, we will
 * panic if writing the bits we intend to change is ineffective.  None of these
 * outcomes should ever be possible on a supported processor; indeed,
 * understanding what to do here is a critical element of adding support for a
 * new processor family or revision.
 */

static inline void
wrmsr_and_test(uint32_t msr, uint64_t v)
{
	wrmsr(msr, v);

#ifdef	DEBUG
	uint64_t rv = rdmsr(msr);

	if (rv != v) {
		cmn_err(CE_PANIC, "MSR 0x%x written with value 0x%lx "
		    "has value 0x%lx\n", msr, v, rv);
	}
#endif
}

static void
genoa_thread_feature_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	x86_uarchrev_t uarchrev = cpuid_getuarchrev(CPU);

	v = rdmsr(MSR_AMD_CPUID_7_FEATURES);
	v = AMD_CPUID_7_FEATURES_SET_RTM(v, 0);
	v = AMD_CPUID_7_FEATURES_SET_HLE(v, 0);
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_GENOA_B0))
		v = AMD_CPUID_7_FEATURES_SET_ERMS(v, 0);
	else
		v = AMD_CPUID_7_FEATURES_SET_ERMS(v, 1);

	wrmsr_and_test(MSR_AMD_CPUID_7_FEATURES, v);

	v = rdmsr(MSR_AMD_FEATURE_EXT_ID);
	/*
	 * XXX Is IBS enable/disable an immutable boot-time policy?  If so, and
	 * if we want to allow controlling it, change this to reflect policy.
	 */
	if (genoa_ccx_set_undoc_fields) {
		v = AMD_FEATURE_EXT_ID_SET_UNKNOWN_IBS_31(v, 0);
		v = AMD_FEATURE_EXT_ID_SET_UNKNOWN_22(v, 0);
	}

	wrmsr_and_test(MSR_AMD_FEATURE_EXT_ID, v);

	v = rdmsr(MSR_AMD_FEATURE_EXT2_EAX);
	v = AMD_FEATURE_EXT2_EAX_SET_NULL_SELECTOR_CLEARS_BASE(v, 1);
	if (genoa_ccx_set_undoc_fields &&
	    (uarchrev_matches(uarchrev, X86_UARCHREV_AMD_ZEN3_B0) ||
	    chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0))) {
		v = AMD_FEATURE_EXT2_EAX_U_ZEN3_B0_SET_UNKNOWN_4(v, 0);
	}

	wrmsr_and_test(MSR_AMD_FEATURE_EXT2_EAX, v);

	if (uarchrev_at_least(uarchrev, X86_UARCHREV_AMD_ZEN3_B0)) {
		v = rdmsr(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0);
		v = AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0_SET_FSRM(v, 1);

		wrmsr_and_test(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0, v);
	}
}

static void
genoa_thread_uc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	/*
	 * The fields we modify in MCODE_CTL are reserved on A0.
	 */
	if (!chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0))
		return;

	v = rdmsr(MSR_AMD_MCODE_CTL);
	v = AMD_MCODE_CTL_SET_REP_STOS_ST_THRESH(v,
	    AMD_MCODE_CTL_ST_THRESH_32M);
	v = AMD_MCODE_CTL_SET_REP_MOVS_ST_THRESH(v,
	    AMD_MCODE_CTL_ST_THRESH_32M);

	wrmsr_and_test(MSR_AMD_MCODE_CTL, v);
}

static void
genoa_core_ls_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_LS_CFG);
	v = AMD_LS_CFG_SET_TEMP_LOCK_CONT_THRESH(v, 1);
	v = AMD_LS_CFG_SET_ALLOW_NULL_SEL_BASE_LIMIT_UPD(v, 1);
	v = AMD_LS_CFG_SET_SBEX_MISALIGNED_TLBMISS_MA1_FRC_MA2(v, 1);
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_GENOA_A0)) {
		v = AMD_LS_CFG_SET_SPEC_LOCK_MAP_DIS(v, 1);
	}
	/*
	 * XXX Possible boot-time or per-thread/guest policy option.
	 */
	v = AMD_LS_CFG_SET_DIS_STREAM_ST(v, 0);

	wrmsr_and_test(MSR_AMD_LS_CFG, v);

	v = rdmsr(MSR_AMD_LS_CFG2);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
		v = AMD_LS_CFG2_SET_DIS_ST_PIPE_COMP_BYP(v, 0);
		v = AMD_LS_CFG2_SET_DIS_FAST_TPR_OPT(v, 0);
		v = AMD_LS_CFG2_SET_HW_PF_ST_PIPE_PRIO_SEL(v, 3);
	} else {
		v = AMD_LS_CFG2_SET_DIS_ST_PIPE_COMP_BYP(v, 1);
		v = AMD_LS_CFG2_SET_DIS_FAST_TPR_OPT(v, 1);
		v = AMD_LS_CFG2_SET_HW_PF_ST_PIPE_PRIO_SEL(v, 1);
	}

	wrmsr_and_test(MSR_AMD_LS_CFG2, v);

	v = rdmsr(MSR_AMD_LS_CFG3);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0) &&
	    genoa_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_62(v, 0);
		v = AMD_LS_CFG3_SET_UNKNOWN_56(v, 0);
		v = AMD_LS_CFG3_SET_DIS_NC_FILLWITH_LTLI(v, 0);
		/* XXX Possible policy option on B0+ only. */
		v = AMD_LS_CFG3_SET_EN_SPEC_ST_FILL(v, 1);
		v = AMD_LS_CFG3_SET_DIS_FAST_LD_BARRIER(v, 0);
	} else if (genoa_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_62(v, 1);
		v = AMD_LS_CFG3_SET_UNKNOWN_56(v, 1);
		v = AMD_LS_CFG3_SET_DIS_NC_FILLWITH_LTLI(v, 1);
		v = AMD_LS_CFG3_SET_EN_SPEC_ST_FILL(v, 0);
	}
	if (genoa_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_60(v, 1);
		v = AMD_LS_CFG3_SET_UNKNOWN_57(v, 1);
	}
	v = AMD_LS_CFG3_SET_DIS_SPEC_WC_NON_STRM_LD(v, 1);
	v = AMD_LS_CFG3_SET_DIS_MAB_FULL_SLEEP(v, 1);
	v = AMD_LS_CFG3_SET_DVM_SYNC_ONLY_ON_TLBI(v, 1);

	wrmsr_and_test(MSR_AMD_LS_CFG3, v);

	if (!chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
		v = rdmsr(MSR_AMD_LS_CFG4);
		v = AMD_LS_CFG4_SET_DIS_LIVE_LOCK_CNT_FST_BUSLOCK(v, 1);
		v = AMD_LS_CFG4_SET_LIVE_LOCK_DET_FORCE_SBEX(v, 1);

		wrmsr_and_test(MSR_AMD_LS_CFG4, v);
	}
}

static void
genoa_core_ic_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_IC_CFG);
	if (genoa_ccx_set_undoc_fields) {
		if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
			v = AMD_IC_CFG_SET_UNKNOWN_48(v, 0);
		} else {
			v = AMD_IC_CFG_SET_UNKNOWN_48(v, 1);
			v = AMD_IC_CFG_SET_UNKNOWN_8(v, 1);
			v = AMD_IC_CFG_SET_UNKNOWN_7(v, 0);
		}
		v = AMD_IC_CFG_SET_UNKNOWN_53(v, 0);
		v = AMD_IC_CFG_SET_UNKNOWN_52(v, 1);
		v = AMD_IC_CFG_SET_UNKNOWN_51(v, 1);
		v = AMD_IC_CFG_SET_UNKNOWN_50(v, 0);
	}
	/* XXX Possible policy option. */
	v = AMD_IC_CFG_SET_OPCACHE_DIS(v, 0);

	wrmsr_and_test(MSR_AMD_IC_CFG, v);
}

static void
genoa_core_dc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	/* XXX All of the prefetch controls may become policy options. */
	v = rdmsr(MSR_AMD_DC_CFG);
	v = AMD_DC_CFG_SET_DIS_REGION_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_STRIDE_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_STREAM_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_PF_HW_FOR_SW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_HW_PF(v, 0);

	wrmsr_and_test(MSR_AMD_DC_CFG, v);

	v = rdmsr(MSR_AMD_DC_CFG2);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
		v = AMD_DC_CFG2_SET_DIS_DMB_STORE_LOCK(v, 0);
	} else {
		v = AMD_DC_CFG2_SET_DIS_DMB_STORE_LOCK(v, 1);
	}
	v = AMD_DC_CFG2_SET_DIS_SCB_NTA_L1(v, 1);

	wrmsr_and_test(MSR_AMD_DC_CFG2, v);
}

static void
genoa_core_tw_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_TW_CFG);
	v = AMD_TW_CFG_SET_COMBINE_CR0_CD(v, 1);

	wrmsr_and_test(MSR_AMD_TW_CFG, v);
}

static void
genoa_core_de_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_DE_CFG);
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_GENOA_B0) &&
	    genoa_ccx_set_undoc_fields) {
		v = AMD_DE_CFG_SET_UNKNOWN_60(v, 0);
		v = AMD_DE_CFG_SET_UNKNOWN_59(v, 0);
	} else if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B1) &&
	    genoa_ccx_set_undoc_fields) {
		v = AMD_DE_CFG_SET_UNKNOWN_48(v, 1);
	} else if (genoa_ccx_set_undoc_fields) {
		/* Older than B0 */
		v = AMD_DE_CFG_SET_UNKNOWN_60(v, 1);
		v = AMD_DE_CFG_SET_UNKNOWN_59(v, 1);
	}
	if (genoa_ccx_set_undoc_fields) {
		v = AMD_DE_CFG_SET_UNKNOWN_33(v, 1);
		v = AMD_DE_CFG_SET_UNKNOWN_32(v, 1);
		v = AMD_DE_CFG_SET_UNKNOWN_28(v, 1);
	}

	wrmsr_and_test(MSR_AMD_DE_CFG, v);
}

static void
genoa_core_l2_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	x86_uarchrev_t uarchrev = cpuid_getuarchrev(CPU);

	v = rdmsr(MSR_AMD_L2_CFG);
	v = AMD_L2_CFG_SET_DIS_HWA(v, 1);
	v = AMD_L2_CFG_SET_DIS_L2_PF_LOW_ARB_PRIORITY(v, 1);
	v = AMD_L2_CFG_SET_EXPLICIT_TAG_L3_PROBE_LOOKUP(v, 1);

	wrmsr_and_test(MSR_AMD_L2_CFG, v);

	/* XXX Prefetch policy options. */
	v = rdmsr(MSR_AMD_CH_L2_PF_CFG);
	v = AMD_CH_L2_PF_CFG_SET_EN_UP_DOWN_PF(v, 1);
	v = AMD_CH_L2_PF_CFG_SET_EN_STREAM_PF(v, 1);

	wrmsr_and_test(MSR_AMD_CH_L2_PF_CFG, v);

	v = rdmsr(MSR_AMD_CH_L2_CFG1);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0) &&
	    uarchrev_at_least(uarchrev, X86_UARCHREV_AMD_ZEN3_B0)) {
		v = AMD_CH_L2_CFG1_U_ZEN3_B0_SET_EN_BUSLOCK_IFETCH(v, 0);
	}
	v = AMD_CH_L2_CFG1_SET_EN_WCB_CONTEXT_DELAY(v, 1);
	v = AMD_CH_L2_CFG1_SET_CBB_MASTER_EN(v, 0);
	v = AMD_CH_L2_CFG1_SET_EN_PROBE_INTERRUPT(v, 1);
	v = AMD_CH_L2_CFG1_SET_EN_MIB_TOKEN_DELAY(v, 1);
	v = AMD_CH_L2_CFG1_SET_EN_MIB_THROTTLING(v, 1);

	wrmsr_and_test(MSR_AMD_CH_L2_CFG1, v);

	v = rdmsr(MSR_AMD_CH_L2_AA_CFG);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_DEMAND(v, AMD_CH_L2_AA_CFG_SCALE_MUL4);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_MISS_L3(v, AMD_CH_L2_AA_CFG_SCALE_MUL4);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_MISS_L3_BW(v,
	    AMD_CH_L2_AA_CFG_SCALE_MUL4);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_REMOTE(v, AMD_CH_L2_AA_CFG_SCALE_MUL4);

	wrmsr_and_test(MSR_AMD_CH_L2_AA_CFG, v);

	v = rdmsr(MSR_AMD_CH_L2_AA_PAIR_CFG0);
	v = AMD_CH_L2_AA_PAIR_CFG0_SET_SUPPRESS_DIFF_VICT(v, 1);

	wrmsr_and_test(MSR_AMD_CH_L2_AA_PAIR_CFG0, v);

	v = rdmsr(MSR_AMD_CH_L2_AA_PAIR_CFG1);
	v = AMD_CH_L2_AA_PAIR_CFG1_SET_DEMAND_HIT_PF_RRIP(v, 0);
	v = AMD_CH_L2_AA_PAIR_CFG1_SET_NOT_UNUSED_PF_RRIP_LVL_B4_L1V(v, 1);

	wrmsr_and_test(MSR_AMD_CH_L2_AA_PAIR_CFG1, v);
}

static void
genoa_ccx_l3_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	x86_uarchrev_t uarchrev = cpuid_getuarchrev(CPU);

	v = rdmsr(MSR_AMD_CH_L3_CFG0);
	if (uarchrev_at_least(uarchrev, X86_UARCHREV_AMD_ZEN3_B1)) {
		v = AMD_CH_L3_CFG0_U_ZEN3_B1_SET_REPORT_SHARED_VIC(v, 1);
	}
	v = AMD_CH_L3_CFG0_SET_REPORT_RESPONSIBLE_VIC(v, 1);

	wrmsr_and_test(MSR_AMD_CH_L3_CFG0, v);

	v = rdmsr(MSR_AMD_CH_L3_CFG1);
	v = AMD_CH_L3_CFG1_SET_SDR_USE_L3_HIT_FOR_WASTED(v, 0);
	v = AMD_CH_L3_CFG1_SET_SDR_IF_DIS(v, 1);
	v = AMD_CH_L3_CFG1_SET_SDR_BURST_LIMIT(v,
	    AMD_CH_L3_CFG1_SDR_BURST_LIMIT_2_IN_16);
	v = AMD_CH_L3_CFG1_SET_SDR_DYN_SUP_NEAR(v, 0);
	v = AMD_CH_L3_CFG1_SET_SDR_LS_WASTE_THRESH(v,
	    AMD_CH_L3_CFG1_SDR_THRESH_255);
	v = AMD_CH_L3_CFG1_SET_SDR_IF_WASTE_THRESH(v,
	    AMD_CH_L3_CFG1_SDR_THRESH_255);

	wrmsr_and_test(MSR_AMD_CH_L3_CFG1, v);

	v = rdmsr(MSR_AMD_CH_L3_XI_CFG0);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
		v = AMD_CH_L3_XI_CFG0_SET_SDR_REQ_BUSY_THRESH(v,
		    AMD_CH_L3_XI_CFG0_SDR_REQ_BUSY_THRESH_767);
	}
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_WR_SIZED_COMP_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_VIC_BLK_COMP_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_WR_SIZED_ZERO_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_VIC_BLK_ZERO_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDR_HIT_SPEC_FEEDBACK_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDR_WASTE_THRESH(v,
	    AMD_CH_L3_XI_CFG0_SDR_THRESH_191);
	v = AMD_CH_L3_XI_CFG0_SET_SDR_SAMP_INTERVAL(v,
	    AMD_CH_L3_XI_CFG0_SDR_SAMP_INTERVAL_16K);

	wrmsr_and_test(MSR_AMD_CH_L3_XI_CFG0, v);
}

static void
genoa_core_undoc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	if (!genoa_ccx_set_undoc_regs)
		return;

	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
		v = rdmsr(MSR_AMD_UNKNOWN_C001_102C);
		v = AMD_UNKNOWN_C001_102C_SET_UNKNOWN_58(v, 1);

		wrmsr_and_test(MSR_AMD_UNKNOWN_C001_102C, v);
	}

	v = rdmsr(MSR_AMD_BP_CFG);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B0)) {
		v = AMD_BP_CFG_SET_UNKNOWN_14(v, 0);
		v = AMD_BP_CFG_SET_UNKNOWN_6(v, 1);
		v = AMD_BP_CFG_SET_UNKNOWN_1(v, 0);
	} else {
		v = AMD_BP_CFG_SET_UNKNOWN_14(v, 1);
		v = AMD_BP_CFG_SET_UNKNOWN_6(v, 0);
		v = AMD_BP_CFG_SET_UNKNOWN_1(v, 1);
	}
	/* Override B0 setting for UNKNOWN_5 */
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_GENOA_A0) ||
	    chiprev_at_least(chiprev, X86_CHIPREV_AMD_GENOA_B1)) {
		v = AMD_BP_CFG_SET_UNKNOWN_5(v, 1);
	}
	v = AMD_BP_CFG_SET_UNKNOWN_4_2(v, 0);

	wrmsr_and_test(MSR_AMD_BP_CFG, v);
}

static void
genoa_core_dpm_init(void)
{
	const genoa_thread_t *thread = CPU->cpu_m.mcpu_hwthread;
	const uint64_t *weights;
	uint32_t nweights;
	uint64_t cfg;

	genoa_fabric_thread_get_dpm_weights(thread, &weights, &nweights);

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

void
genoa_ccx_init(void)
{
	const genoa_thread_t *thread = CPU->cpu_m.mcpu_hwthread;
	char str[CPUID_BRANDSTR_STRLEN + 1];

	/*
	 * First things first: it shouldn't be (and generally isn't) possible to
	 * get here on a completely bogus CPU; e.g., Intel or a pre-Zen part.
	 * But the remainder of this function, and our overall body of code,
	 * support only a limited subset of processors that exist.  Eventually
	 * this will include processors that are not Genoa, and at that time
	 * this set of checks will need to be factored out; even so, we also
	 * want to make sure we're on a supported revision.  A chicken switch is
	 * available to ease future porting work.
	 */

	if (!genoa_ccx_is_supported()) {
		uint_t vendor, family, model, step;

		vendor = cpuid_getvendor(CPU);
		family = cpuid_getfamily(CPU);
		model = cpuid_getmodel(CPU);
		step = cpuid_getstep(CPU);
		panic("cpu%d is unsupported: vendor 0x%x family 0x%x "
		    "model 0x%x step 0x%x\n", CPU->cpu_id,
		    vendor, family, model, step);
	}

	/*
	 * Set the MSRs that control the brand string so that subsequent cpuid
	 * passes can retrieve it.  We fetched it from the SMU during earlyboot
	 * fabric initialisation.
	 */
	if (genoa_fabric_thread_get_brandstr(thread, str, sizeof (str)) <=
	    CPUID_BRANDSTR_STRLEN && str[0] != '\0') {
		for (uint_t n = 0; n < sizeof (str) / sizeof (uint64_t); n++) {
			uint64_t sv = *(uint64_t *)&str[n * sizeof (uint64_t)];

			wrmsr(MSR_AMD_PROC_NAME_STRING0 + n, sv);
		}
	} else {
		cmn_err(CE_WARN, "cpu%d: SMU provided invalid brand string\n",
		    CPU->cpu_id);
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
	 * result, so that's what we do.  Functions are named for their scope.
	 * The exception to the rule is the table walker configuration, which
	 * causes CR0.CD to be effectively set on both threads if either thread
	 * has it set; since by default, a thread1 that hasn't started yet has
	 * this bit set, setting it on thread0 will cause everything to grind to
	 * a near halt.  Since the TW config bit has no effect without SMT, we
	 * don't need to worry about setting it on thread0 if SMT is off.
	 */
	genoa_thread_feature_init();
	genoa_thread_uc_init();
	if (thread->gt_threadno == 1) {
		genoa_core_tw_init();
	}
	if (thread->gt_threadno == 0) {
		genoa_core_ls_init();
		genoa_core_ic_init();
		genoa_core_dc_init();
		genoa_core_de_init();
		genoa_core_l2_init();
		if (thread->gt_core->gc_logical_coreno == 0)
			genoa_ccx_l3_init();
		genoa_core_undoc_init();
		genoa_core_dpm_init();
	}
}
