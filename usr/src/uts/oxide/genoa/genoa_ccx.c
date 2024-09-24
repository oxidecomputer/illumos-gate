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
 * aspects of a CPU core specific to Genoa processors.
 */

#include <sys/cmn_err.h>
#include <sys/x86_archext.h>

#include <sys/amdzen/ccx.h>

#include <sys/io/genoa/ccx_impl.h>
#include <sys/io/zen/ccx_impl.h>


void
genoa_thread_feature_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_CPUID_7_FEATURES);

	/* Advertise AVX512 support. */
	v = AMD_CPUID_7_FEATURES_U_ZEN4_SET_AVX512VL(v, 1);
	v = AMD_CPUID_7_FEATURES_U_ZEN4_SET_AVX512BW(v, 1);
	v = AMD_CPUID_7_FEATURES_U_ZEN4_SET_AVC512CD(v, 1);
	v = AMD_CPUID_7_FEATURES_U_ZEN4_SET_AVX512_IFMA(v, 1);
	v = AMD_CPUID_7_FEATURES_U_ZEN4_SET_AVX512DQ(v, 1);
	v = AMD_CPUID_7_FEATURES_U_ZEN4_SET_AVX512F(v, 1);

	v = AMD_CPUID_7_FEATURES_SET_RTM(v, 0);
	v = AMD_CPUID_7_FEATURES_SET_ERMS(v, 1);
	v = AMD_CPUID_7_FEATURES_SET_HLE(v, 0);

	/*
	 * While the RDSEED instruction does exist on these processors and can
	 * work, it is not actually implemented by the Zen4 core.  Instead, one
	 * must configure an MMIO aperture for the PSP and then a separate MSR
	 * to allow the core to access it, through which the instruction
	 * operates; without this, it always returns 0 with CF clear.  As we
	 * don't currently have the infrastructure to set this up, we want to
	 * inform software that the instruction doesn't work to encourage it to
	 * obtain entropy by other means.
	 */
	v = AMD_CPUID_7_FEATURES_SET_RDSEED(v, 0);

	wrmsr_and_test(MSR_AMD_CPUID_7_FEATURES, v);

	v = rdmsr(MSR_AMD_FEATURE_EXT_ID);
	if (zen_ccx_set_undoc_fields) {
		/* Possible policy option: IBS. */
		v = AMD_FEATURE_EXT_ID_SET_UNKNOWN_IBS_31(v, 0);
		v = AMD_FEATURE_EXT_ID_SET_UNKNOWN_22(v, 0);
	}
	wrmsr_and_test(MSR_AMD_FEATURE_EXT_ID, v);

	v = rdmsr(MSR_AMD_FEATURE_EXT2_EAX);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_FEATURE_EXT2_EAX_U_ZEN4_SET_UNKNOWN_4(v, 0);
	}
	wrmsr_and_test(MSR_AMD_FEATURE_EXT2_EAX, v);

	v = rdmsr(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0);
	v = AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0_SET_FSRM(v, 1);
	wrmsr_and_test(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0, v);

	v = rdmsr(MSR_AMD_OSVW_ID_LENGTH);
	v = AMD_OSVW_ID_LENGTH_SET_LENGTH(v, 5);
	wrmsr_and_test(MSR_AMD_OSVW_ID_LENGTH, v);

	v = rdmsr(MSR_AMD_OSVW_STATUS);
	v = AMD_OSVW_STATUS_SET_STATUS_BITS(v, 0);
	wrmsr_and_test(MSR_AMD_OSVW_STATUS, v);

	v = rdmsr(MSR_AMD_SVM_REV_FEAT_ID);
	v = AMD_SVM_REV_FEAT_ID_U_ZEN4_SET_X2AVIC(v, 1);
	v = AMD_SVM_REV_FEAT_ID_SET_AVIC(v, 1);
	wrmsr_and_test(MSR_AMD_SVM_REV_FEAT_ID, v);
}

void
genoa_thread_uc_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_MCODE_CTL);
	v = AMD_MCODE_CTL_SET_REP_STOS_ST_THRESH(v,
	    AMD_MCODE_CTL_ST_THRESH_32M);
	v = AMD_MCODE_CTL_SET_REP_MOVS_ST_THRESH(v,
	    AMD_MCODE_CTL_ST_THRESH_32M);
	v = AMD_MCODE_CTL_SET_REP_STRING_ST_DIS(v, 0);
	wrmsr_and_test(MSR_AMD_MCODE_CTL, v);
}

void
genoa_core_ls_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_LS_CFG);
	v = AMD_LS_CFG_SET_SPEC_LOCK_MAP_DIS(v, 0);
	v = AMD_LS_CFG_U_ZEN4_SET_DIS_SPEC_WC_REQ(v, 0);
	v = AMD_LS_CFG_SET_TEMP_LOCK_CONT_THRESH(v, 1);
	v = AMD_LS_CFG_SET_ALLOW_NULL_SEL_BASE_LIMIT_UPD(v, 1);
	/* Possible policy option: Streaming Stores. */
	v = AMD_LS_CFG_SET_DIS_STREAM_ST(v, 0);

	wrmsr_and_test(MSR_AMD_LS_CFG, v);

	v = rdmsr(MSR_AMD_LS_CFG3);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_SET_UNKNOWN_60(v, 1);
		v = AMD_LS_CFG3_SET_UNKNOWN_56(v, 1);
	}
	v = AMD_LS_CFG3_U_ZEN4_SET_UNKNOWN_33_SPEC(v, 1);
	v = AMD_LS_CFG3_SET_DIS_SPEC_WC_NON_STRM_LD(v, 1);
	/* Possible policy option: Speculation (Balanced). */
	v = AMD_LS_CFG3_SET_EN_SPEC_ST_FILL(v, 1);
	wrmsr_and_test(MSR_AMD_LS_CFG3, v);

	v = rdmsr(MSR_AMD_LS_CFG4);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG4_U_ZEN4_SET_UNKNOWN_38(v, 0);
	}
	wrmsr_and_test(MSR_AMD_LS_CFG4, v);
}

void
genoa_core_ic_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_IC_CFG);
	/* Possible policy option: Opcache. */
	v = AMD_IC_CFG_SET_OPCACHE_DIS(v, 0);
	wrmsr_and_test(MSR_AMD_IC_CFG, v);
}

void
genoa_core_dc_init(void)
{
	uint64_t v;

	/* Possible policy option: Prefetch. */
	v = rdmsr(MSR_AMD_DC_CFG);
	v = AMD_DC_CFG_SET_EN_BURST_PFS(v, 1);
	v = AMD_DC_CFG_SET_NUM_MABS_RSVD_HW_PF_L2(v, 3);
	v = AMD_DC_CFG_SET_DIS_REGION_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_STRIDE_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_DIS_STREAM_HW_PF(v, 0);
	v = AMD_DC_CFG_SET_EN_PF_HIST_STREAM_HIT(v, 1);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_DC_CFG_U_ZEN4_SET_UNKNOWN_59_PF(v, 0);
		v = AMD_DC_CFG_U_ZEN4_SET_UNKNOWN_12_PF(v, 0);
	}
	wrmsr_and_test(MSR_AMD_DC_CFG, v);

	v = rdmsr(MSR_AMD_DC_CFG2);
	v = AMD_DC_CFG2_SET_DIS_SCB_NTA_L1(v, 1);
	v = AMD_DC_CFG2_SET_DIS_DMB_STORE_LOCK(v, 0);
	wrmsr_and_test(MSR_AMD_DC_CFG2, v);
}

void
genoa_core_fp_init(void)
{
	uint64_t v;
	x86_uarchrev_t uarchrev = cpuid_getuarchrev(CPU);

	v = rdmsr(MSR_AMD_FP_CFG);
	/* Zen4 Ax */
	if (zen_ccx_set_undoc_fields &&
	    !uarchrev_at_least(uarchrev, X86_UARCHREV_AMD_ZEN4_B0)) {
		v = AMD_FP_CFG_F_GENOA_SET_UNKNOWN_52(v, 1);
	}
	/* Zen4 A0, Zen4 B0 */
	if (zen_ccx_set_undoc_fields &&
	    uarchrev_matches(uarchrev, X86_UARCHREV_AMD_ZEN4_A0 |
	    X86_UARCHREV_AMD_ZEN4_B0)) {
		v = AMD_FP_CFG_F_GENOA_SET_UNKNOWN_43(v, 1);
	}
	wrmsr_and_test(MSR_AMD_FP_CFG, v);
}

void
genoa_core_l2_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_L2_CFG);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_L2_CFG_SET_UNKNOWN_52(v, 1);
	}
	v = AMD_L2_CFG_SET_DIS_HWA(v, 0);
	v = AMD_L2_CFG_SET_DIS_L2_PF_LOW_ARB_PRIORITY(v, 1);
	v = AMD_L2_CFG_SET_EXPLICIT_TAG_L3_PROBE_LOOKUP(v, 1);
	wrmsr_and_test(MSR_AMD_L2_CFG, v);

	v = rdmsr(MSR_AMD_CH_L2_CFG1);
	v = AMD_CH_L2_CFG1_SET_EN_WCB_CONTEXT_DELAY(v, 1);
	if (zen_ccx_set_undoc_fields) {
		/* Likely corresponds to CBB_LS_TIMEOUT_VALUE = 64. */
		v = AMD_CH_L2_CFG1_U_ZEN4_SET_UNKNOWN_46_45(v, 1);
		/* Likely corresponds to CBB_PROBE_TIMEOUT_VALUE = 160. */
		v = AMD_CH_L2_CFG1_U_ZEN4_SET_UNKNOWN_44(v, 1);
	}
	v = AMD_CH_L2_CFG1_SET_EN_MIB_TOKEN_DELAY(v, 1);
	v = AMD_CH_L2_CFG1_SET_EN_MIB_THROTTLING(v, 1);

	if (zen_ccx_set_undoc_fields) {
		/* Possible policy option: Speculation (Balanced). */
		v = AMD_CH_L2_CFG1_U_ZEN4_SET_UNKNOWN_30_SPEC(v, 0);
	}

	wrmsr_and_test(MSR_AMD_CH_L2_CFG1, v);

	v = rdmsr(MSR_AMD_CH_L2_AA_CFG);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_DEMAND(v, AMD_CH_L2_AA_CFG_SCALE_MUL4);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_MISS_L3(v, AMD_CH_L2_AA_CFG_SCALE_MUL4);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_MISS_L3_BW(v,
	    AMD_CH_L2_AA_CFG_SCALE_MUL4);
	v = AMD_CH_L2_AA_CFG_SET_SCALE_REMOTE(v, AMD_CH_L2_AA_CFG_SCALE_MUL4);
	wrmsr_and_test(MSR_AMD_CH_L2_AA_CFG, v);

	v = rdmsr(MSR_AMD_CH_L2_PF_CFG);
	if (zen_ccx_set_undoc_fields) {
		/* Possible policy option: Prefetch. */
		v = AMD_CH_L2_PF_CFG_U_ZEN4_SET_UNKNOWN_22_PF(v, 1);
	}
	v = AMD_CH_L2_PF_CFG_SET_EN_UP_DOWN_PF(v, 1);
	v = AMD_CH_L2_PF_CFG_SET_EN_STREAM_PF(v, 1);
	wrmsr_and_test(MSR_AMD_CH_L2_PF_CFG, v);
}

void
genoa_ccx_l3_init(void)
{
	uint64_t v;

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
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_WR_SIZED_COMP_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_VIC_BLK_COMP_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_WR_SIZED_ZERO_EN(v, 1);
	v = AMD_CH_L3_XI_CFG0_SET_SDP_REQ_VIC_BLK_ZERO_EN(v, 1);
	wrmsr_and_test(MSR_AMD_CH_L3_XI_CFG0, v);
}

void
genoa_core_undoc_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_BP_CFG);
	v = AMD_BP_CFG_U_ZEN4_SET_DIS_STAT_COND_BP(v, 1);
	wrmsr_and_test(MSR_AMD_BP_CFG, v);

	v = rdmsr(MSR_AMD_UNKNOWN_C001_10EC);
	/* Possible policy option: Speculation (Balanced). */
	v = AMD_UNKNOWN_C001_10EC_SET_UNKNOWN_0_SPEC(v, 0);
	wrmsr_and_test(MSR_AMD_UNKNOWN_C001_10EC, v);
}
