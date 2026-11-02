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
 * This file implements a collection of routines that can be used to initialize
 * various aspects of the Milan CPU cores.
 */

#include <sys/types.h>
#include <sys/amdzen/ccd.h>
#include <sys/amdzen/ccx.h>
#include <sys/amdzen/smn.h>
#include <sys/boot_physmem.h>
#include <sys/x86_archext.h>

#include <sys/io/milan/ccx_impl.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/smn.h>


void
milan_thread_feature_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	x86_uarchrev_t uarchrev = cpuid_getuarchrev(CPU);

	v = rdmsr(MSR_AMD_CPUID_7_FEATURES);
	v = AMD_CPUID_7_FEATURES_SET_RTM(v, 0);
	v = AMD_CPUID_7_FEATURES_SET_HLE(v, 0);

	/*
	 * While the RDSEED instruction does exist on these processors and can
	 * work, it is not actually implemented by the Zen3 core.  Instead, one
	 * must configure an MMIO aperture for the PSP and then a separate MSR
	 * to allow the core to access it, through which the instruction
	 * operates; without this, it always returns 0 with CF clear.  As we
	 * don't currently have the infrastructure to set this up, we want to
	 * inform software that the instruction doesn't work to encourage it to
	 * obtain entropy by other means.
	 */
	v = AMD_CPUID_7_FEATURES_SET_RDSEED(v, 0);
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_MILAN_B0))
		v = AMD_CPUID_7_FEATURES_SET_ERMS(v, 0);
	else
		v = AMD_CPUID_7_FEATURES_SET_ERMS(v, 1);

	wrmsr_and_test(MSR_AMD_CPUID_7_FEATURES, v);

	v = rdmsr(MSR_AMD_FEATURE_EXT_ID);
	if (zen_ccx_set_undoc_fields) {
		/* Possible policy option: IBS. */
		v = AMD_FEATURE_EXT_ID_SET_UNKNOWN_IBS_31(v, 0);
		v = AMD_FEATURE_EXT_ID_SET_UNKNOWN_22(v, 0);
	}

	wrmsr_and_test(MSR_AMD_FEATURE_EXT_ID, v);

	v = rdmsr(MSR_AMD_FEATURE_EXT2_EAX);
	v = AMD_FEATURE_EXT2_EAX_SET_NULL_SELECTOR_CLEARS_BASE(v, 1);
	if (zen_ccx_set_undoc_fields &&
	    (uarchrev_matches(uarchrev, X86_UARCHREV_AMD_ZEN3_B0) ||
	    chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0))) {
		v = AMD_FEATURE_EXT2_EAX_U_ZEN3_B0_SET_UNKNOWN_4(v, 0);
	}

	wrmsr_and_test(MSR_AMD_FEATURE_EXT2_EAX, v);

	if (uarchrev_at_least(uarchrev, X86_UARCHREV_AMD_ZEN3_B0)) {
		v = rdmsr(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0);
		v = AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0_SET_FSRM(v, 1);

		wrmsr_and_test(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0, v);
	}
}

void
milan_thread_uc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	/*
	 * The fields we modify in MCODE_CTL are reserved on A0.
	 */
	if (!chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0))
		return;

	v = rdmsr(MSR_AMD_MCODE_CTL);
	v = AMD_MCODE_CTL_SET_REP_STOS_ST_THRESH(v,
	    AMD_MCODE_CTL_ST_THRESH_32M);
	v = AMD_MCODE_CTL_SET_REP_MOVS_ST_THRESH(v,
	    AMD_MCODE_CTL_ST_THRESH_32M);

	wrmsr_and_test(MSR_AMD_MCODE_CTL, v);
}

void
milan_core_ls_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_LS_CFG);
	v = AMD_LS_CFG_SET_TEMP_LOCK_CONT_THRESH(v, 1);
	v = AMD_LS_CFG_SET_ALLOW_NULL_SEL_BASE_LIMIT_UPD(v, 1);
	v = AMD_LS_CFG_SET_SBEX_MISALIGNED_TLBMISS_MA1_FRC_MA2(v, 1);
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_MILAN_A0)) {
		v = AMD_LS_CFG_SET_SPEC_LOCK_MAP_DIS(v, 1);
	}
	/* Possible policy option: Streaming Stores. */
	v = AMD_LS_CFG_SET_DIS_STREAM_ST(v, 0);

	wrmsr_and_test(MSR_AMD_LS_CFG, v);

	v = rdmsr(MSR_AMD_LS_CFG2);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
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
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0) &&
	    zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_62(v, 0);
		v = AMD_LS_CFG3_SET_UNKNOWN_56(v, 0);
		v = AMD_LS_CFG3_SET_DIS_NC_FILLWITH_LTLI(v, 0);
		/* Possible policy option: Speculation (B0+ only). */
		v = AMD_LS_CFG3_SET_EN_SPEC_ST_FILL(v, 1);
		v = AMD_LS_CFG3_SET_DIS_FAST_LD_BARRIER(v, 0);
	} else if (zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_62(v, 1);
		v = AMD_LS_CFG3_SET_UNKNOWN_56(v, 1);
		v = AMD_LS_CFG3_SET_DIS_NC_FILLWITH_LTLI(v, 1);
		v = AMD_LS_CFG3_SET_EN_SPEC_ST_FILL(v, 0);
	}
	if (zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_60(v, 1);
		v = AMD_LS_CFG3_SET_UNKNOWN_57(v, 1);
	}
	v = AMD_LS_CFG3_SET_DIS_SPEC_WC_NON_STRM_LD(v, 1);
	v = AMD_LS_CFG3_SET_DIS_MAB_FULL_SLEEP(v, 1);
	v = AMD_LS_CFG3_SET_DVM_SYNC_ONLY_ON_TLBI(v, 1);

	wrmsr_and_test(MSR_AMD_LS_CFG3, v);

	if (!chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
		v = rdmsr(MSR_AMD_LS_CFG4);
		v = AMD_LS_CFG4_SET_DIS_LIVE_LOCK_CNT_FST_BUSLOCK(v, 1);
		v = AMD_LS_CFG4_SET_LIVE_LOCK_DET_FORCE_SBEX(v, 1);

		wrmsr_and_test(MSR_AMD_LS_CFG4, v);
	}
}

void
milan_core_ic_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_IC_CFG);
	if (zen_ccx_set_undoc_fields) {
		if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
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
	/* Possible policy option: Opcache. */
	v = AMD_IC_CFG_SET_OPCACHE_DIS(v, 0);

	wrmsr_and_test(MSR_AMD_IC_CFG, v);
}

void
milan_core_dc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	/* Possible policy option: Prefetch. */
	v = rdmsr(MSR_AMD_DC_CFG);
	v = AMD_DC_CFG_SET_DIS_REGION_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_STRIDE_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_STREAM_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_PF_HW_FOR_SW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_HW_PF(v, 0);

	wrmsr_and_test(MSR_AMD_DC_CFG, v);

	v = rdmsr(MSR_AMD_DC_CFG2);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
		v = AMD_DC_CFG2_SET_DIS_DMB_STORE_LOCK(v, 0);
	} else {
		v = AMD_DC_CFG2_SET_DIS_DMB_STORE_LOCK(v, 1);
	}
	v = AMD_DC_CFG2_SET_DIS_SCB_NTA_L1(v, 1);

	wrmsr_and_test(MSR_AMD_DC_CFG2, v);
}

void
milan_core_de_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_DE_CFG);
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_MILAN_B0) &&
	    zen_ccx_set_undoc_fields) {
		v = AMD_DE_CFG_SET_UNKNOWN_60(v, 0);
		v = AMD_DE_CFG_SET_UNKNOWN_59(v, 0);
	} else if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B1) &&
	    zen_ccx_set_undoc_fields) {
		v = AMD_DE_CFG_SET_UNKNOWN_48(v, 1);
	} else if (zen_ccx_set_undoc_fields) {
		/* Older than B0 */
		v = AMD_DE_CFG_SET_UNKNOWN_60(v, 1);
		v = AMD_DE_CFG_SET_UNKNOWN_59(v, 1);
	}
	if (zen_ccx_set_undoc_fields) {
		v = AMD_DE_CFG_SET_UNKNOWN_33(v, 1);
		v = AMD_DE_CFG_SET_UNKNOWN_32(v, 1);
		v = AMD_DE_CFG_SET_UNKNOWN_28(v, 1);
	}

	wrmsr_and_test(MSR_AMD_DE_CFG, v);
}

void
milan_core_l2_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	x86_uarchrev_t uarchrev = cpuid_getuarchrev(CPU);

	v = rdmsr(MSR_AMD_L2_CFG);
	v = AMD_L2_CFG_SET_DIS_HWA(v, 1);
	v = AMD_L2_CFG_SET_DIS_L2_PF_LOW_ARB_PRIORITY(v, 1);
	v = AMD_L2_CFG_SET_EXPLICIT_TAG_L3_PROBE_LOOKUP(v, 1);

	wrmsr_and_test(MSR_AMD_L2_CFG, v);

	/* Possible policy option: Prefetch. */
	v = rdmsr(MSR_AMD_CH_L2_PF_CFG);
	v = AMD_CH_L2_PF_CFG_SET_EN_UP_DOWN_PF(v, 1);
	v = AMD_CH_L2_PF_CFG_SET_EN_STREAM_PF(v, 1);

	wrmsr_and_test(MSR_AMD_CH_L2_PF_CFG, v);

	v = rdmsr(MSR_AMD_CH_L2_CFG1);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0) &&
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

void
milan_ccx_l3_init(void)
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
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
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

void
milan_core_undoc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
		v = rdmsr(MSR_AMD_UNKNOWN_C001_102C);
		v = AMD_UNKNOWN_C001_102C_SET_UNKNOWN_58(v, 1);

		wrmsr_and_test(MSR_AMD_UNKNOWN_C001_102C, v);
	}

	v = rdmsr(MSR_AMD_BP_CFG);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B0)) {
		v = AMD_BP_CFG_SET_UNKNOWN_14(v, 0);
		v = AMD_BP_CFG_SET_UNKNOWN_6(v, 1);
		v = AMD_BP_CFG_SET_UNKNOWN_1(v, 0);
	} else {
		v = AMD_BP_CFG_SET_UNKNOWN_14(v, 1);
		v = AMD_BP_CFG_SET_UNKNOWN_6(v, 0);
		v = AMD_BP_CFG_SET_UNKNOWN_1(v, 1);
	}
	/* Override B0 setting for UNKNOWN_5 */
	if (chiprev_matches(chiprev, X86_CHIPREV_AMD_MILAN_A0) ||
	    chiprev_at_least(chiprev, X86_CHIPREV_AMD_MILAN_B1)) {
		v = AMD_BP_CFG_SET_UNKNOWN_5(v, 1);
	}
	v = AMD_BP_CFG_SET_UNKNOWN_4_2(v, 0);

	wrmsr_and_test(MSR_AMD_BP_CFG, v);
}
