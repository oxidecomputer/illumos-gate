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
#include <sys/x86_archext.h>

#include <sys/amdzen/ccx.h>

#include <sys/io/turin/ccx_impl.h>
#include <sys/io/zen/ccx_impl.h>


#define	TURIN_PHYSADDR_4G_64K_HOLE	0x100000000UL
#define	TURIN_PHYSADDR_4G_64K_HOLE_END	0x100010000UL


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

void
turin_thread_feature_init(void)
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

	v = AMD_CPUID_7_FEATURES_SET_ERMS(v, 1);

	wrmsr_and_test(MSR_AMD_CPUID_7_FEATURES, v);

	v = rdmsr(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0);
	v = AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0_SET_FSRM(v, 1);
	wrmsr_and_test(MSR_AMD_STRUCT_EXT_FEAT_ID_EDX0_ECX0, v);

	v = rdmsr(MSR_AMD_SVM_REV_FEAT_ID);
	v = AMD_SVM_REV_FEAT_ID_U_ZEN4_SET_X2AVIC(v, 1);
	v = AMD_SVM_REV_FEAT_ID_SET_AVIC(v, 1);
	wrmsr_and_test(MSR_AMD_SVM_REV_FEAT_ID, v);
}

void
turin_thread_uc_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_MCODE_CTL);
	v = AMD_MCODE_CTL_SET_REP_STRING_ST_DIS(v, 0);
	wrmsr_and_test(MSR_AMD_MCODE_CTL, v);
}

void
turin_core_ls_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_LS_CFG);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG_F_TURIN_SET_UNKNOWN_62(v, 1);
	}

	v = AMD_LS_CFG_SET_TEMP_LOCK_CONT_THRESH(v, 1);
	v = AMD_LS_CFG_SET_ALLOW_NULL_SEL_BASE_LIMIT_UPD(v, 1);

	/* Possible policy option: Streaming Stores. */
	v = AMD_LS_CFG_SET_DIS_STREAM_ST(v, 0);

	wrmsr_and_test(MSR_AMD_LS_CFG, v);

	v = rdmsr(MSR_AMD_LS_CFG2);
	v = AMD_LS_CFG2_SET_HW_PF_ST_PIPE_PRIO_SEL(v, 1);
	if (zen_ccx_set_undoc_fields) {
		/* BRHD Ax, BRH Ax, BRH Bx */
		if (!chiprev_at_least(chiprev, X86_CHIPREV_AMD_DENSE_TURIN_B0)||
		    !chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_C0)) {
			v = AMD_LS_CFG2_F_TURIN_SET_UNKNOWN_56(v, 1);
		}
		v = AMD_LS_CFG2_F_TURIN_SET_UNKNOWN_34(v, 1);
	}
	wrmsr_and_test(MSR_AMD_LS_CFG2, v);

	v = rdmsr(MSR_AMD_LS_CFG3);
	/* Possible policy option: Speculation (Balanced). */
	v = AMD_LS_CFG3_SET_EN_SPEC_ST_FILL(v, 1);
	if (zen_ccx_set_undoc_fields) {
		v = AMD_LS_CFG3_U_ZEN5_SET_UNKNOWN_23_SPEC(v, 0);
	}
	wrmsr_and_test(MSR_AMD_LS_CFG3, v);

	if (zen_ccx_set_undoc_fields) {
		v = rdmsr(MSR_AMD_LS_CFG4);
		v = AMD_LS_CFG4_F_TURIN_SET_UNKNOWN_6(v, 1);
		wrmsr_and_test(MSR_AMD_LS_CFG4, v);
	}
}

void
turin_core_ic_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_IC_CFG);
	/* Possible policy option: Opcache. */
	v = AMD_IC_CFG_SET_OPCACHE_DIS(v, 0);
	wrmsr_and_test(MSR_AMD_IC_CFG, v);
}

void
turin_core_dc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	v = rdmsr(MSR_AMD_DC_PF_CFG_U_ZEN5);
	/* BRH Ax, BRH Bx */
	if (zen_ccx_set_undoc_fields &&
	    !chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_C0)) {
		v = AMD_DC_PF_CFG_U_ZEN5_F_TURIN_SET_UNKNOWN_32_30(v, 1);
	}

	/* Possible policy option: Prefetch. */
	v = AMD_DC_PF_CFG_U_ZEN5_SET_DIS_REGION_HW_PF(v, 0);
	v = AMD_DC_PF_CFG_U_ZEN5_SET_DIS_STRIDE_HW_PF(v, 0);
	v = AMD_DC_PF_CFG_U_ZEN5_SET_DIS_STREAM_HW_PF(v, 0);
	v = AMD_DC_PF_CFG_U_ZEN5_SET_EN_BURST_PFS_OR_PF_HIST_STREAM_HIT_12(v,
	    1);
	v = AMD_DC_PF_CFG_U_ZEN5_SET_EN_LOW_CONF_BURST_PFS(v, 0);
	v = AMD_DC_PF_CFG_U_ZEN5_SET_EN_BURST_PFS_OR_PF_HIST_STREAM_HIT_6(v, 1);

	wrmsr_and_test(MSR_AMD_DC_PF_CFG_U_ZEN5, v);
}

void
turin_core_tw_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	/* BRH Ax, BRH B0, BRHD Ax */
	if (!chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_B1) ||
	    !chiprev_at_least(chiprev, X86_CHIPREV_AMD_DENSE_TURIN_B0)) {
		v = rdmsr(MSR_AMD_TW_CFG);
		v = AMD_TW_CFG_U_ZEN5_SET_TLBI_BACK_TO_BACK_CNT_ALWAYS(v, 1);
		wrmsr_and_test(MSR_AMD_TW_CFG, v);
	}
}

void
turin_core_l2_init(void)
{
	uint64_t v;

	v = rdmsr(MSR_AMD_L2_CFG);
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
turin_core_undoc_init(void)
{
	uint64_t v;
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);

	/* BRH Bx, BRH Cx, BRHD */
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_B0) ||
	    chiprev_matches(chiprev, X86_CHIPREV_AMD_DENSE_TURIN_ANY)) {
		v = rdmsr(MSR_AMD_UNKNOWN_C001_10E9);
		v = AMD_UNKNOWN_C001_10E9_F_TURIN_SET_UNKNOWN_7_4(v, 0);
		v = AMD_UNKNOWN_C001_10E9_F_TURIN_SET_UNKNOWN_3_0(v, 0);
		wrmsr_and_test(MSR_AMD_UNKNOWN_C001_10E9, v);
	}

	/* BRH Bx, BRH Cx, BRHD */
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_B0) ||
	    chiprev_matches(chiprev, X86_CHIPREV_AMD_DENSE_TURIN_ANY)) {
		v = rdmsr(MSR_AMD_UNKNOWN_C001_10EA);
		v = AMD_UNKNOWN_C001_10EA_F_TURIN_SET_UNKNOWN_7_4(v, 3);
		v = AMD_UNKNOWN_C001_10EA_F_TURIN_SET_UNKNOWN_3_0(v, 2);
		wrmsr_and_test(MSR_AMD_UNKNOWN_C001_10EA, v);
	}

	/* BRH A0, BRH B0 */
	if (chiprev_matches(chiprev,
	    X86_CHIPREV_AMD_TURIN_A0 | X86_CHIPREV_AMD_TURIN_B0)) {
		v = rdmsr(MSR_AMD_UNKNOWN_C001_10EB);
		v = AMD_UNKNOWN_C001_10EB_F_TURIN_SET_UNKNOWN_18(v, 1);
		wrmsr_and_test(MSR_AMD_UNKNOWN_C001_10EB, v);
	}

	v = rdmsr(MSR_AMD_UNKNOWN_C001_10EC);

	/* BRH Bx, BRH Cx, BRHD */
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_B0) ||
	    chiprev_matches(chiprev, X86_CHIPREV_AMD_DENSE_TURIN_ANY)) {
		v = AMD_UNKNOWN_C001_10EC_F_TURIN_SET_UNKNOWN_9_5(v, 0x1f);
	}

	/* Possible policy option: Speculation (Balanced). */
	v = AMD_UNKNOWN_C001_10EC_SET_UNKNOWN_0_SPEC(v, 1);

	wrmsr_and_test(MSR_AMD_UNKNOWN_C001_10EC, v);
}
