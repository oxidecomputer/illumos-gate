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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_AMDZEN_CCX_H
#define	_SYS_AMDZEN_CCX_H

/*
 * AMD-specific MSRs that are generally not architectural.  For architectural
 * and common MSR definitions, see x86_archext.h and controlregs.h.  There is
 * presently no analogue for Intel-specific MSRs because there are no consumers.
 */

#include <sys/bitext.h>
#include <sys/debug.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register Naming
 *
 * This is patterned after the DF register nomenclature conventions.  If a
 * register name and location is universal -- that is, if it always either
 * exists with the same name and address or not at all -- its name is
 * MSR_AMD_reg.  Registers that have different names/location depending on
 * processor family are named MSR_AMD_F_family_reg.  If the name/location is
 * specific to revision(s) of that family, the name expands to
 * MSR_AMD_F_family_rev_reg.  Similar conventions apply to uarch-specific
 * registers: MSR_AMD_U_uarch_reg, MSR_AMD_U_uarch_rev_reg.  Remember that a
 * package of revision R may contain cores of microarchitectural revision S,
 * where R != S.
 *
 * Unlike the DF, we don't have many, if any, cases where a register with
 * substantially the same name and/or contents is at different locations on
 * different processor or microarchitecture revisions.  If the register exists
 * at all, it's typically in the same place.  The same is not quite true of its
 * contents, however.
 *
 * Note that many of these registers are poorly documented, and some are
 * entirely undocumented.  In addition, each processor family has its own
 * separate documentation; there is no common documentation for
 * non-architectural MSRs even when they are common to a microarchitecture or
 * cpuid family.  This makes it impractical to factor these definitions
 * effectively, and it is likely that for many years each time support is
 * introduced for a new processor there will be changes required to the names
 * and field definitions of existing registers to better reflect what is and is
 * not shared.
 *
 * Field Naming
 *
 * Fields are named in the same way: if the field is either at the same place or
 * absent across all registers of the same name, it is accessed via macros
 * AMD_reg_GET_field and AMD_reg_SET_field.  If the location of the field
 * differs across revisions but its function is the same, we use
 * AMD_reg_{F,U}_{family,uarch}_{GET,SET}_field or
 * AMD_reg_{F,U}_{family,uarch}_{rev}_{GET,SET}_field.  Yes, these names can and
 * do get unwieldy very quickly; there is no simple way to avoid this.  In most
 * cases a field will show up or disappear over time rather than moving around,
 * and occasionally a field with a different name and purpose will take its
 * place.  If this were more common, we might want (as we are more likely to
 * want for the DF) a way beyond just convention to express the presence or
 * absence of a particular field name (and the location in the register to which
 * it is bound); as it is, we don't have this.
 *
 * Registers that cannot be written do not have SET macros; registers that
 * cannot be read may or may not have GET macros, as there may still be
 * situations in which one wishes to extract a field from an integer value that
 * has been set up by other software rather than read from hardware.
 *
 * Registers that have only one useful field don't have separate field getters
 * and setters; they have macros that are named for the register itself.
 * Similarly, a register that has multiple fields, one of which has the same
 * name as the register, will have a getter and setter for that field which does
 * not replicate the field name.  While this is not quite as obvious as one
 * might like, cutting down on the still ridiculous amount of typing makes it
 * worthwhile.
 *
 * Constants
 *
 * Often there are useful names to be given to values that can be stored in
 * multibyte fields.  Collections of constants that are used in multiple fields
 * (or even multiple registers) can be reused by choosing appropriate names.
 * Conventions here should follow those for fields: the constant name is
 * appended to the register and field name, and if a particular constant applies
 * only to some subset of processors its name should reflect that.  Thus it is
 * possible to end up with a register, field, and constant that are all
 * increasingly specific; for example, one might have MSR_AMD_FOO that is
 * universal and contains AMD_FOO_U_ZEN3_BAR which in turn can take values
 * AMD_FOO_U_ZEN3_BAR_F_MILAN_BAZ only on Milan processors and
 * AMD_FOO_U_ZEN3_BAR_F_CEZANNE_QUUX only on Cezanne.  Again, this is rarely the
 * case and most actual constants will be straightforward.
 *
 * Register numbers and constants are consts, not macros, so they can be picked
 * up by CTF and potentially code generators for both C and other languages that
 * may in future consume it.  There's no immediately obvious way to define
 * fields that can be used in this manner, however.
 */

#define	DECL_REG(_rn, _addr)	\
    static const uint32_t MSR_AMD_ ## _rn = (_addr)

#define	DECL_FIELD_R(_rn, _fn, _upper, _lower)	\
    static inline uint64_t AMD_ ## _rn ## _GET_ ## _fn(uint64_t v)	\
{									\
	return (bitx64(v, (_upper), (_lower)));				\
}

#define	DECL_FIELD_W(_rn, _fn, _upper, _lower)	\
    static inline uint64_t AMD_ ## _rn ## _SET_ ## _fn(uint64_t v,	\
    uint64_t fv) \
{									\
	return (bitset64(v, (_upper), (_lower), fv));			\
}

#define	DECL_FIELD_RW(_rn, _fn, _upper, _lower) \
	DECL_FIELD_R(_rn, _fn, (_upper), (_lower))	\
	DECL_FIELD_W(_rn, _fn, (_upper), (_lower))

#define	DECL_FIELD_SINGLETON_R(_rn, _upper, _lower)	\
    static inline uint64_t AMD_ ## _rn ## _ ## GET(uint64_t v)	\
{									\
	return (bitx64(v, (_upper), (_lower)));				\
}

#define	DECL_FIELD_SINGLETON_W(_rn, _upper, _lower)	\
    static inline uint64_t AMD_ ## _rn ## _ ## SET(uint64_t v, uint64_t fv) \
{									\
	return (bitset64(v, (_upper), (_lower), fv));			\
}

#define	DECL_FIELD_SINGLETON_RW(_rn, _upper, _lower) \
	DECL_FIELD_SINGLETON_R(_rn, (_upper), (_lower))	\
	DECL_FIELD_SINGLETON_W(_rn, (_upper), (_lower))

#define	DECL_CONST(_rn, _fn, _cn, _v)			\
    static const uint64_t AMD_ ## _rn ## _ ## _fn ## _ ## _cn = _v ## UL

/*
 * Core::X86::Msr::PrefetchControl.  Turns on or off the 2 L2 and 3 L1 prefetch
 * engines for all threads of a core.
 */
DECL_REG(PREFETCH_CONTROL, 0xC0000108);
DECL_FIELD_RW(PREFETCH_CONTROL, UPDOWN, 5, 5);
DECL_FIELD_RW(PREFETCH_CONTROL, L2STREAM, 3, 3);
DECL_FIELD_RW(PREFETCH_CONTROL, L1REGION, 2, 2);
DECL_FIELD_RW(PREFETCH_CONTROL, L1STRIDE, 1, 1);
DECL_FIELD_RW(PREFETCH_CONTROL, L1STREAM, 0, 0);

/*
 * Core::X86::Msr::MmioCfgBaseAddr.  Configures the location and size of the
 * PCIe ECAM region, and applies to all threads of a core.
 */
DECL_REG(MMIO_CFG_BASE_ADDR, 0xC0010058);
DECL_FIELD_RW(MMIO_CFG_BASE_ADDR, ADDR, 51, 20);
DECL_CONST(MMIO_CFG_BASE_ADDR, ADDR, SHIFT, 20);
DECL_FIELD_RW(MMIO_CFG_BASE_ADDR, BUS_RANGE, 5, 2);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 1, 0);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 2, 1);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 4, 2);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 8, 3);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 16, 4);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 32, 5);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 64, 6);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 128, 7);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, 256, 8);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_2SEG, 9);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_4SEG, 10);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_8SEG, 11);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_16SEG, 12);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_32SEG, 13);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_64SEG, 14);
DECL_CONST(MMIO_CFG_BASE_ADDR, BUS_RANGE, U_ZEN4_128SEG, 15);
DECL_FIELD_RW(MMIO_CFG_BASE_ADDR, EN, 0, 0);

/*
 * Core::X86::Msr::PStateDef.  Each of these per-processor registers defines one
 * of 8 PStates.  These registers may also be accessed via SMN.
 */
DECL_REG(PSTATE_DEF0, 0xC0010064);
DECL_REG(PSTATE_DEF1, 0xC0010065);
DECL_REG(PSTATE_DEF2, 0xC0010066);
DECL_REG(PSTATE_DEF3, 0xC0010067);
DECL_REG(PSTATE_DEF4, 0xC0010068);
DECL_REG(PSTATE_DEF5, 0xC0010069);
DECL_REG(PSTATE_DEF6, 0xC001006A);
DECL_REG(PSTATE_DEF7, 0xC001006B);
DECL_FIELD_RW(PSTATE_DEF, PSTATE_EN, 63, 63);
DECL_FIELD_RW(PSTATE_DEF, IDD_DIV, 31, 30);
DECL_FIELD_RW(PSTATE_DEF, IDD_VALUE, 29, 22);
DECL_FIELD_RW(PSTATE_DEF, CPU_VID, 21, 14);
DECL_FIELD_RW(PSTATE_DEF, CPU_DFS_ID, 13, 8);
DECL_CONST(PSTATE_DEF, CPU_DFS_ID, OFF, 0);
DECL_CONST(PSTATE_DEF, CPU_DFS_ID, MULTIPLIER, 8);
DECL_FIELD_RW(PSTATE_DEF, CPU_FID, 7, 0);
DECL_CONST(PSTATE_DEF, CPU_FID, DIVISOR, 25);

/*
 * Core::X86::Msr::OSVW_ID_Length.
 */
DECL_REG(OSVW_ID_LENGTH, 0xC0010140);
DECL_FIELD_RW(OSVW_ID_LENGTH, LENGTH, 15, 0);

/*
 * Core::X86::Msr::OSVW_Status.
 */
DECL_REG(OSVW_STATUS, 0xC0010141);
DECL_FIELD_RW(OSVW_STATUS, STATUS_BITS, 15, 0);

/*
 * Core::X86::Msr::CSTATE_POLICY, also accessible via SMN as
 * L3::SCFCTP::PMC_CPR[1:0].  Does what its name suggests; CStates, also as the
 * name suggests, apply to the core, not individual threads.
 */
DECL_REG(CSTATE_POLICY, 0xC0010294);
DECL_FIELD_RW(CSTATE_POLICY, CLT_EN, 62, 62);
DECL_FIELD_RW(CSTATE_POLICY, CIT_FASTSAMPLE, 61, 61);
DECL_FIELD_RW(CSTATE_POLICY, CIT_EN, 60, 60);
DECL_FIELD_RW(CSTATE_POLICY, IRM_MAXDEPTH, 59, 56);
DECL_FIELD_RW(CSTATE_POLICY, IRM_THRESHOLD, 55, 52);
DECL_FIELD_RW(CSTATE_POLICY, IRM_BURSTEN, 51, 49);
DECL_FIELD_RW(CSTATE_POLICY, IRM_DECRRATE, 48, 44);
DECL_FIELD_RW(CSTATE_POLICY, CFSM_MISPREDACT, 43, 42);
DECL_CONST(CSTATE_POLICY, CFSM_MISPREDACT, RESET, 0);
DECL_CONST(CSTATE_POLICY, CFSM_MISPREDACT, DEC1, 1);
DECL_CONST(CSTATE_POLICY, CFSM_MISPREDACT, DEC2, 2);
DECL_CONST(CSTATE_POLICY, CFSM_MISPREDACT, DEC3, 3);
DECL_FIELD_RW(CSTATE_POLICY, CFSM_THRESHOLD, 41, 39);
DECL_CONST(CSTATE_POLICY, CFSM_THRESHOLD, DIS_MON, 0);
DECL_FIELD_RW(CSTATE_POLICY, CFSM_DURATION, 38, 32);
DECL_FIELD_RW(CSTATE_POLICY, C1E_EN, 29, 29);
DECL_FIELD_RW(CSTATE_POLICY, C1E_TMRLEN, 28, 24);
DECL_FIELD_RW(CSTATE_POLICY, C1E_TMRSEL, 23, 22);
DECL_FIELD_RW(CSTATE_POLICY, CFOH_TMRSEL, 21, 21);
DECL_FIELD_RW(CSTATE_POLICY, CFOH_TMRLEN, 20, 14);
DECL_FIELD_RW(CSTATE_POLICY, HYST_TMRLEN, 13, 9);
DECL_FIELD_RW(CSTATE_POLICY, HYST_TMRSEL, 8, 7);
DECL_CONST(CSTATE_POLICY, HYST_TMRSEL, DIS, 0);
DECL_CONST(CSTATE_POLICY, HYST_TMRSEL, MUL128, 2);
DECL_CONST(CSTATE_POLICY, HYST_TMRSEL, MUL512, 3);
DECL_FIELD_RW(CSTATE_POLICY, CC1_TMRLEN, 6, 2);
DECL_FIELD_RW(CSTATE_POLICY, CC1_TMRSEL, 1, 0);
DECL_CONST(CSTATE_POLICY, CC1_TMRSEL, DIS, 0);
DECL_CONST(CSTATE_POLICY, CC1_TMRSEL, MUL250, 2);
DECL_CONST(CSTATE_POLICY, CC1_TMRSEL, MUL1000, 3);

/*
 * Core::X86::Msr::CSTATE_CONFIG, also accessible via SMN as
 * L3::SCFCTP::PMC_CCR[2:0].  Does what its name suggests.
 */
DECL_REG(CSTATE_CFG, 0xC0010296);
DECL_FIELD_RW(CSTATE_CFG_U_ZEN4, CCR2_CC1E_EN, 55, 55);
DECL_FIELD_RW(CSTATE_CFG, CCR2_CFOHTMR_LEN, 54, 48);
DECL_FIELD_RW(CSTATE_CFG, CCR1_CFOHTMR_LEN, 46, 40);
DECL_FIELD_RW(CSTATE_CFG, CCR0_CFOHTMR_LEN, 38, 32);
DECL_FIELD_RW(CSTATE_CFG, CCR2_CC6EN, 22, 22);
DECL_FIELD_RW(CSTATE_CFG, CCR2_CC1DFSID, 21, 16);
DECL_FIELD_RW(CSTATE_CFG, CCR1_CC6EN, 14, 14);
DECL_FIELD_RW(CSTATE_CFG, CCR1_CC1DFSID, 13, 8);
DECL_FIELD_RW(CSTATE_CFG, CCR0_CC6EN, 6, 6);
DECL_FIELD_RW(CSTATE_CFG, CCR0_CC1DFSID, 5, 0);

/*
 * Core::X86::Msr::MCODE_CTL.  Allows configuring aspects of microcode
 * behaviour for each thread; only used fields are declared here.  See PPRs for
 * additional fields.
 */
DECL_REG(MCODE_CTL, 0xC0011000);
DECL_FIELD_RW(MCODE_CTL, REP_STOS_ST_THRESH, 31, 28);
DECL_FIELD_RW(MCODE_CTL, REP_MOVS_ST_THRESH, 27, 24);
DECL_CONST(MCODE_CTL, ST_THRESH, 4M, 0);
DECL_CONST(MCODE_CTL, ST_THRESH, 8M, 1);
DECL_CONST(MCODE_CTL, ST_THRESH, 16M, 2);
DECL_CONST(MCODE_CTL, ST_THRESH, 32M, 3);
DECL_CONST(MCODE_CTL, ST_THRESH, 64M, 4);
DECL_CONST(MCODE_CTL, ST_THRESH, 128M, 5);
DECL_CONST(MCODE_CTL, ST_THRESH, 256M, 6);
DECL_CONST(MCODE_CTL, ST_THRESH, 512M, 7);
DECL_CONST(MCODE_CTL, ST_THRESH, 16K, 8);
DECL_CONST(MCODE_CTL, ST_THRESH, 32K, 9);
DECL_CONST(MCODE_CTL, ST_THRESH, 64K, 10);
DECL_CONST(MCODE_CTL, ST_THRESH, 128K, 11);
DECL_CONST(MCODE_CTL, ST_THRESH, 256K, 12);
DECL_CONST(MCODE_CTL, ST_THRESH, 512K, 13);
DECL_CONST(MCODE_CTL, ST_THRESH, 1M, 14);
DECL_CONST(MCODE_CTL, ST_THRESH, 2M, 15);
DECL_FIELD_RW(MCODE_CTL, REP_STRING_ST_DIS, 15, 15);

/*
 * Core::X86::Msr::CPUID_7_Features.  This register's contents are supposed to
 * control the corresponding contents of %eax and %ebx for CPUID leaf 7.  Some
 * of the bits are reserved, however, both in AMD's and Intel's documentation,
 * which means it's extremely unlikely any machine-independent code would ever
 * inspect them and gives rise to the hypothesis that some of these bits may
 * also control whether a given feature is enabled.  In general, bits are
 * meaningful only on microarchitectures that can otherwise support the features
 * to which they refer.
 */
DECL_REG(CPUID_7_FEATURES, 0xC0011002);
DECL_FIELD_R(CPUID_7_FEATURES_U_ZEN4, AVX512VL, 31, 31);
DECL_FIELD_R(CPUID_7_FEATURES_U_ZEN4, AVX512BW, 30, 30);
DECL_FIELD_RW(CPUID_7_FEATURES, SHA, 29, 29);
DECL_FIELD_R(CPUID_7_FEATURES_U_ZEN4, AVC512CD, 28, 28);
DECL_FIELD_RW(CPUID_7_FEATURES, CLWB, 24, 24);
DECL_FIELD_RW(CPUID_7_FEATURES, CLFSHOPT, 23, 23);
DECL_FIELD_R(CPUID_7_FEATURES_U_ZEN4, AVX512_IFMA, 21, 21);
DECL_FIELD_RW(CPUID_7_FEATURES, SMAP, 20, 20);
DECL_FIELD_RW(CPUID_7_FEATURES, ADX, 19, 19);
DECL_FIELD_RW(CPUID_7_FEATURES, RDSEED, 18, 18);
DECL_FIELD_R(CPUID_7_FEATURES_U_ZEN4, AVX512DQ, 17, 17);
DECL_FIELD_R(CPUID_7_FEATURES_U_ZEN4, AVX512F, 16, 16);
DECL_FIELD_RW(CPUID_7_FEATURES, PQE, 15, 15);
DECL_FIELD_RW(CPUID_7_FEATURES, PQM, 12, 12);
/* Undocumented */
DECL_FIELD_RW(CPUID_7_FEATURES, RTM, 11, 11);
DECL_FIELD_RW(CPUID_7_FEATURES, INVPCID, 10, 10);
/* Undocumented but known to exist on Zen3, documented on Zen4 */
DECL_FIELD_RW(CPUID_7_FEATURES, ERMS, 9, 9);
DECL_FIELD_RW(CPUID_7_FEATURES, BMI2, 8, 8);
DECL_FIELD_RW(CPUID_7_FEATURES, SMEP, 7, 7);
DECL_FIELD_RW(CPUID_7_FEATURES, AVX2, 5, 5);
/* Undocumented but known to exist on Zen3, documented on Zen4 */
DECL_FIELD_RW(CPUID_7_FEATURES, HLE, 4, 4);
DECL_FIELD_RW(CPUID_7_FEATURES, BMI1, 3, 3);
DECL_FIELD_RW(CPUID_7_FEATURES, FSGSBASE, 0, 0);

/*
 * Core::X86::Msr::LS_CFG.  Load-store unit configuration parameters.  This
 * register is semi-documented only on Zen3, but it seems it still exists on
 * at least some Zen4 processors.  Only used fields are declared.
 */
DECL_REG(LS_CFG, 0xC0011020);
DECL_FIELD_RW(LS_CFG, SPEC_LOCK_MAP_DIS, 54, 54);
DECL_FIELD_RW(LS_CFG_U_ZEN4, DIS_SPEC_WC_REQ, 53, 53);
DECL_FIELD_RW(LS_CFG, TEMP_LOCK_CONT_THRESH, 52, 50);
DECL_FIELD_RW(LS_CFG, ALLOW_NULL_SEL_BASE_LIMIT_UPD, 46, 46);
/* Renamed from SbexNonPgMaxMa2FrcTlbMissIfMa1TlbMiss */
DECL_FIELD_RW(LS_CFG, SBEX_MISALIGNED_TLBMISS_MA1_FRC_MA2, 43, 43);
DECL_FIELD_RW(LS_CFG, DIS_STREAM_ST, 28, 28);

/*
 * Core::X86::Msr::IC_CFG.  Presumably controls the I-cache; this register may
 * have been documented long ago but is no longer documented for Zen3 or Zen4
 * although it appears to exist on both.  The purpose of most of its contents is
 * completely unknown and cannot be derived even from context, except for
 * OPCACHE_DIS, which is associated with a very lightly-documented PCD on both
 * Zen3 and Zen4, and DIS_SPEC_TLB_RLD which was described for family 0x15
 * models 0x7X in public documentation.  Only used fields are declared.
 */
DECL_REG(IC_CFG, 0xC0011021);
DECL_FIELD_RW(IC_CFG, UNKNOWN_53, 53, 53);
DECL_FIELD_RW(IC_CFG, UNKNOWN_52, 52, 52);
DECL_FIELD_RW(IC_CFG, UNKNOWN_51, 51, 51);
DECL_FIELD_RW(IC_CFG, UNKNOWN_50, 50, 50);
DECL_FIELD_RW(IC_CFG, UNKNOWN_48, 48, 48);
DECL_FIELD_RW(IC_CFG, DIS_SPEC_TLB_RLD, 9, 9);
DECL_FIELD_RW(IC_CFG, UNKNOWN_8, 8, 8);
DECL_FIELD_RW(IC_CFG, UNKNOWN_7, 7, 7);
DECL_FIELD_RW(IC_CFG, OPCACHE_DIS, 5, 5);

/*
 * Core::X86::Msr::DC_CFG.  Controls miscellaneous D-cache parameters.  Known to
 * exist on at least Zen2-Zen4.  Only used fields are declared; all pertain to
 * knobs for disabling parts of the prefetcher.
 */
DECL_REG(DC_CFG, 0xC0011022);
DECL_FIELD_RW(DC_CFG, EN_BURST_PFS, 63, 63);
DECL_FIELD_RW(DC_CFG, NUM_MABS_RSVD_HW_PF_L2, 58, 57);
DECL_FIELD_RW(DC_CFG, EN_PF_HIST_STREAM_HIT, 55, 55);
DECL_FIELD_RW(DC_CFG, DIS_REGION_HW_PF, 18, 18);
DECL_FIELD_RW(DC_CFG, DIS_STRIDE_HW_PF, 17, 17);
DECL_FIELD_RW(DC_CFG, DIS_STREAM_HW_PF, 16, 16);
DECL_FIELD_RW(DC_CFG, DIS_PF_HW_FOR_SW_PF, 15, 15);
DECL_FIELD_RW(DC_CFG, DIS_HW_PF, 13, 13);

/*
 * Core::X86::Msr::TW_CFG.  Controls miscellaneous table-walker parameters.
 * Known to exist on at least Zen2-Zen4 although it is no longer documented in
 * Zen4.  The only field we use is declared here but others exist; see PPRs.
 */
DECL_REG(TW_CFG, 0xC0011023);
DECL_FIELD_RW(TW_CFG, COMBINE_CR0_CD, 49, 49);

/*
 * Core::X86::Msr::FP_CFG. Undocumented.
 */
DECL_REG(FP_CFG, 0xC0011028);
DECL_FIELD_RW(FP_CFG, UNKNOWN_52, 52, 52);
DECL_FIELD_RW(FP_CFG, UNKNOWN_43, 43, 43);

/*
 * Core::X86::Msr::DE_CFG.  Controls the instruction-decode unit.  This register
 * is completely undocumented except for:
 *
 * - a public AMD whitepaper in which the behaviour of bit 1 is described and
 *   informally promoted to architectural status,
 * - a lightly-documented workaround using bit 14 for a Carrizo bug that may or
 *   may not still exist, and
 * - documentation for a CBS option that strongly implies bit 31 disables idling
 *   a (thread?) after executing a pause instruction, maybe only on Zen4.
 *
 * This register is therefore known to exist on all Zen processors; we define
 * only those fields we use, and know precious little about them.
 */
DECL_REG(DE_CFG, 0xC0011029);
DECL_FIELD_RW(DE_CFG, UNKNOWN_60, 60, 60);
DECL_FIELD_RW(DE_CFG, UNKNOWN_59, 59, 59);
DECL_FIELD_RW(DE_CFG, UNKNOWN_48, 48, 48);
DECL_FIELD_RW(DE_CFG, UNKNOWN_33, 33, 33);
DECL_FIELD_RW(DE_CFG, UNKNOWN_32, 32, 32);
DECL_FIELD_RW(DE_CFG_U_ZEN4, DIS_PAUSE_IDLE, 31, 31);
DECL_FIELD_RW(DE_CFG, UNKNOWN_28, 28, 28);
DECL_FIELD_RW(DE_CFG, REDIRECT_FOR_RETURN_DIS, 14, 14);
DECL_FIELD_RW(DE_CFG, LFENCE_DISPATCH, 1, 1);

/*
 * Core::X86::Msr::L2_CFG.  L2 cache configuration; this register has had
 * different names in the past, including CU_CFG (Combined Unit) prior to Zen1;
 * it's unknown whether it had a similar function.  This particular register
 * appears to be Zen2-Zen4 only, though it is no longer documented on Zen4.
 * Only used fields are declared here; see PPRs.
 */
DECL_REG(L2_CFG, 0xC001102A);
DECL_FIELD_RW(L2_CFG, UNKNOWN_52, 52, 52);
DECL_FIELD_RW(L2_CFG, DIS_HWA, 16, 16);
DECL_FIELD_RW(L2_CFG, DIS_L2_PF_LOW_ARB_PRIORITY, 15, 15);
DECL_FIELD_RW(L2_CFG, EXPLICIT_TAG_L3_PROBE_LOOKUP, 7, 7);

/*
 * Core::X86::Msr::ChL2PfCfg.  Additional L2 prefetch configuration bits.  Known
 * to exist at least on Zen2-Zen4; only used fields are declared here.
 */
DECL_REG(CH_L2_PF_CFG, 0xC001102B);
DECL_FIELD_RW(CH_L2_PF_CFG, EN_UP_DOWN_PF, 2, 2);
DECL_FIELD_RW(CH_L2_PF_CFG, EN_STREAM_PF, 0, 0);

/*
 * Undocumented register, known to be present on Zen2 and Zen3 processors.  May
 * be gone on Zen4.
 */
DECL_REG(UNKNOWN_C001_102C, 0xC001102C);
DECL_FIELD_RW(UNKNOWN_C001_102C, UNKNOWN_58, 58, 58);

/*
 * Core::X86::Msr::LS_CFG2.  Additional load-store unit configuration.  This
 * register is known to exist on at least Zen2 and Zen3, but is no longer
 * documented for Zen4 and may no longer exist there.  Only used fields are
 * declared here.
 */
DECL_REG(LS_CFG2, 0xC001102D);
DECL_FIELD_RW(LS_CFG2, DIS_ST_PIPE_COMP_BYP, 57, 57);
DECL_FIELD_RW(LS_CFG2, LIVE_LOCK_WIDGET_CNT_SEL, 52, 51);
DECL_FIELD_RW(LS_CFG2, DIS_FAST_TPR_OPT, 47, 47);
DECL_FIELD_RW(LS_CFG2, HW_PF_ST_PIPE_PRIO_SEL, 36, 35);

/*
 * This register is undocumented; in Zen4 it may be named BP_CFG, presumably
 * containing bits to control the branch predictor.  It also exists, or at least
 * some register at this address exists, on Zen2 and Zen3.
 */
DECL_REG(BP_CFG, 0xC001102E);
DECL_FIELD_RW(BP_CFG_U_ZEN4, DIS_STAT_COND_BP, 35, 35);
DECL_FIELD_RW(BP_CFG, UNKNOWN_14, 14, 14);
DECL_FIELD_RW(BP_CFG, UNKNOWN_6, 6, 6);
DECL_FIELD_RW(BP_CFG, UNKNOWN_5, 5, 5);
DECL_FIELD_RW(BP_CFG, UNKNOWN_4_2, 4, 2);
DECL_FIELD_RW(BP_CFG, UNKNOWN_1, 1, 1);

/*
 * Core::X86::Msr::ChL2RangeLock0.  Along with a few others, this register
 * controls the ability to lock a range of physical addresses, in 256-byte
 * blocks, into the L2 cache.  This is known to exist on Zen3; its presence
 * elsewhere is unknown.
 */
DECL_REG(CH_L2_RANGE_LOCK0, 0xC001102F);
DECL_FIELD_RW(CH_L2_RANGE_LOCK0, BASE_ADDR, 63, 20);
DECL_FIELD_RW(CH_L2_RANGE_LOCK0, LIMIT, 19, 4);
DECL_FIELD_RW(CH_L2_RANGE_LOCK0, LOCK, 1, 1);
DECL_FIELD_RW(CH_L2_RANGE_LOCK0, EN, 0, 0);

/*
 * Core::X86::Msr::DPM_CFG.  Per-core dynamic power management configuration
 * bits.  Known to be present on at least Zen2-Zen4 processors; the rest of the
 * register is believed to be reserved.
 */
DECL_REG(DPM_CFG, 0xC0011074);
DECL_FIELD_RW(DPM_CFG, CFG_LOCKED, 63, 63);
DECL_FIELD_RW(DPM_CFG, MSR_ACCESS_DIS, 62, 62);
DECL_FIELD_RW(DPM_CFG, CONTINUE_ACCUM, 61, 61);
DECL_FIELD_RW(DPM_CFG, JITTER_EN, 60, 60);

/*
 * Core::X86::Msr::DPM_WAC_ACC_INDEX.  This is an index register that along with
 * the following data counterparts provides indirect access to two collections
 * of 20-22 weight chains pertaining to DPM.  It exists on at least Zen2-Zen4
 * though the set of valid addresses this register can accept varies by
 * microarchitecture and possibly processor family.
 */
DECL_REG(DPM_WAC_ACC_INDEX, 0xC0011076);
/* AMD name: WeightChainIndex, shortened for redundancy. */
DECL_FIELD_SINGLETON_RW(DPM_WAC_ACC_INDEX, 4, 0);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_FP0_ACC_FP1_FP0, 0);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_FP1_ACC_FP3_FP2, 1);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_FP2_ACC_L21_L20, 2);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_FP3_ACC_RSV_IC0, 3);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_L20_ACC_RSV_BP0, 4);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_L21_ACC_DE1_DE0, 5);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_IC0_ACC_DE3_DE2, 6);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_BP0_ACC_SC1_SC0, 7);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_DE0_ACC_SC3_SC2, 8);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_DE1_ACC_LS1_LS0, 9);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_DE2_ACC_LS3_LS2, 0xA);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_DE3_ACC_RSC_CPLB0, 0xB);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_SC0, 0xC);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_SC1, 0xD);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_SC2, 0xE);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_SC3, 0xF);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_LS0, 0x10);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_LS1, 0x11);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_LS2, 0x12);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_LS3, 0x13);
DECL_CONST(DPM_WAC_ACC_INDEX, WEIGHT_CHAIN, WCC_CPLB0, 0x14);
DECL_CONST(DPM_WAC_ACC_INDEX_U_ZEN3, WEIGHT_CHAIN, WCC_UNKNOWN_21, 0x15);
DECL_CONST(DPM_WAC_ACC_INDEX_U_ZEN3, WEIGHT_CHAIN, WCC_UNKNOWN_22, 0x16);

/*
 * Core::X86::Msr::DPM_WAC_DATA, sometimes given as WCC_DATA.  This data
 * register accesses per-chain DPM weights.
 */
DECL_REG(DPM_WAC_DATA, 0xC0011077);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT7, 63, 56);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT6, 55, 48);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT5, 47, 40);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT4, 39, 32);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT3, 31, 24);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT2, 23, 16);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT1, 15, 8);
DECL_FIELD_RW(DPM_WAC_DATA, WEIGHT0, 7, 0);

/*
 * Core::X86::Msr::DPM_ACC_DATA.  This data register accesses per-chain DPM
 * accumulator data.
 */
DECL_REG(DPM_ACC_DATA, 0xC0011078);
DECL_FIELD_RW(DPM_ACC_DATA, ACC_HI, 63, 32);
DECL_FIELD_RW(DPM_ACC_DATA, ACC_LO, 31, 0);

/*
 * Core::X86::Msr::ChL3Cfg0.  L3 cache controller configuration bits.  As one
 * would expect, all cores sharing a CCX share a single instance of this
 * register and subsequent L3-related registers.  Known to exist on at least
 * Zen2-Zen4.  Only used fields are declared here.
 */
DECL_REG(CH_L3_CFG0, 0xC0011092);
DECL_FIELD_RW(CH_L3_CFG0_U_ZEN3_B1, RANGE_LOCK_EN, 8, 8);
DECL_FIELD_RW(CH_L3_CFG0_U_ZEN3_B1, REPORT_SHARED_VIC, 2, 2);
DECL_FIELD_RW(CH_L3_CFG0, REPORT_RESPONSIBLE_VIC, 0, 0);

/*
 * Core::X86::Msr::ChL3Cfg1.  Additional L3 cache controller configuration.
 * This register is undocumented on Zen2 but known to exist there, and
 * documented for Zen3/4.  Only used fields are declared here.
 */
DECL_REG(CH_L3_CFG1, 0xC0011093);
DECL_FIELD_RW(CH_L3_CFG1, SDR_USE_L3_HIT_FOR_WASTED, 23, 23);
DECL_FIELD_RW(CH_L3_CFG1, SDR_IF_DIS, 22, 22);
DECL_FIELD_RW(CH_L3_CFG1, SDR_DYN_BURST_LIMIT, 21, 21);
DECL_FIELD_RW(CH_L3_CFG1, SDR_BURST_LIMIT, 20, 19);
DECL_CONST(CH_L3_CFG1, SDR_BURST_LIMIT, UNLIMITED, 0);
DECL_CONST(CH_L3_CFG1, SDR_BURST_LIMIT, 4_IN_16, 1);
DECL_CONST(CH_L3_CFG1, SDR_BURST_LIMIT, 2_IN_16, 2);
DECL_CONST(CH_L3_CFG1, SDR_BURST_LIMIT, 1_IN_16, 3);
DECL_FIELD_RW(CH_L3_CFG1, SDR_DYN_SUP_NEAR, 18, 18);
DECL_FIELD_RW(CH_L3_CFG1, SDR_SUP_NEAR, 17, 17);
DECL_FIELD_RW(CH_L3_CFG1, SDR_DYN_LS_WASTE_THRESH, 16, 16);
DECL_FIELD_RW(CH_L3_CFG1, SDR_DYN_LS_RATE_THRESH, 14, 14);
DECL_FIELD_RW(CH_L3_CFG1, SDR_LS_WASTE_THRESH, 12, 10);
DECL_FIELD_RW(CH_L3_CFG1, SDR_IF_WASTE_THRESH, 9, 7);
DECL_FIELD_RW(CH_L3_CFG1, SDR_LS_RATE_THRESH, 6, 4);
DECL_FIELD_RW(CH_L3_CFG1, SDR_IF_RATE_THRESH, 3, 1);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 47, 0);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 63, 1);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 95, 2);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 127, 3);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 191, 4);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 255, 5);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 383, 6);
DECL_CONST(CH_L3_CFG1, SDR_THRESH, 511, 7);
DECL_FIELD_RW(CH_L3_CFG1, SPEC_DRAM_RD_DIS, 0, 0);

/*
 * Core::X86::Msr::ChL3RangeLockBaseAddr.  This and several subsequent registers
 * controls the ability to lock a range of physical memory into the L3 cache.
 * This is believed to exist only on Zen3 and Zen4 processors.  Also accessible
 * via SMN as L3::L3CRB::ChL3RangeLockBaseAddr{Hi,Lo}.  See also CH_L3_CFG0 for
 * the enable bit; this may not be available on Zen3 prior to rev B1.
 */
DECL_REG(CH_L3_RANGE_LOCK_BASE_ADDR, 0xC0011095);
DECL_FIELD_SINGLETON_RW(CH_L3_RANGE_LOCK_BASE_ADDR, 51, 12);

/*
 * Core::X86::Msr::ChL3RangeLockMaxAddr.  Upper limit to go with the base
 * address in the previous register.  Similarly accessible via SMN as
 * L3::L3CRB::ChL3RangeLockMaxAddr{Hi,Lo}.
 */
DECL_REG(CH_L3_RANGE_LOCK_MAX_ADDR, 0xC0011096);
DECL_FIELD_SINGLETON_RW(CH_L3_RANGE_LOCK_MAX_ADDR, 51, 12);

/*
 * Core::X86::Msr::ChL3XiCfg0.  Whatever the XI complex interface is, this
 * register configures it!  Also accessible via SMN as L3::L3CRB::ChXiCfg0.
 * Believed to exist on Zen3 and Zen4 only.  Only used fields are declared here.
 */
DECL_REG(CH_L3_XI_CFG0, 0xC0011097);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDP_REQ_WR_SIZED_COMP_EN, 20, 20);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDP_REQ_VIC_BLK_COMP_EN, 19, 19);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDP_REQ_WR_SIZED_ZERO_EN, 18, 18);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDP_REQ_VIC_BLK_ZERO_EN, 17, 17);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDR_HIT_SPEC_FEEDBACK_EN, 12, 12);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 11, 9);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 95, 0);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 127, 1);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 191, 2);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 255, 3);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 383, 4);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 511, 5);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 767, 6);
DECL_CONST(CH_L3_XI_CFG0, SDR_REQ_BUSY_THRESH, 1023, 7);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDR_WASTE_THRESH, 8, 6);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDR_RATE_THRESH, 5, 3);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 47, 0);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 63, 1);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 95, 2);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 127, 3);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 191, 4);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 255, 5);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 383, 6);
DECL_CONST(CH_L3_XI_CFG0, SDR_THRESH, 511, 7);
DECL_FIELD_RW(CH_L3_XI_CFG0, SDR_SAMP_INTERVAL, 2, 1);
DECL_CONST(CH_L3_XI_CFG0, SDR_SAMP_INTERVAL, DIS, 0);
DECL_CONST(CH_L3_XI_CFG0, SDR_SAMP_INTERVAL, 1K, 1);
DECL_CONST(CH_L3_XI_CFG0, SDR_SAMP_INTERVAL, 4K, 2);
DECL_CONST(CH_L3_XI_CFG0, SDR_SAMP_INTERVAL, 16K, 3);

/*
 * Core::X86::Msr::ChL3RangeLockWayMask.  Additional control of the L3
 * range-locking mechanism, also Zen3/Zen4 only and per-CCX.
 */
DECL_REG(CH_L3_RANGE_LOCK_WAY_MASK, 0xC001109A);
DECL_FIELD_SINGLETON_RW(CH_L3_RANGE_LOCK_WAY_MASK, 15, 0);

/*
 * Core::X86::Msr::PSP_ADDR.  BAR for a PSP mailbox interface we don't use.
 * This exists at least on Zen2-Zen4 but is undocumented prior to Zen3.  Each
 * thread has its own instance of this BAR.  The size of the only field is
 * documented as 48 bits on Zen4 but may or may not be larger given the
 * availability of 52-bit PA space.
 */
DECL_REG(PSP_ADDR, 0xC00110A2);
DECL_FIELD_SINGLETON_RW(PSP_ADDR, 47, 0);

/*
 * Core::X86::Msr::FeatureExtID.  Another writable cpuid register!  This one
 * controls the extended feature extensions %ebx value (lower half) and a few
 * bits of the long mode information leaf (upper half).  Once again, we do not
 * know whether this has any effects beyond simply what is reported by cpuid.
 * Exists on at least Zen2-Zen4, configurable per-thread.
 */
DECL_REG(FEATURE_EXT_ID, 0xC00110DC);
DECL_FIELD_RW(FEATURE_EXT_ID, GUEST_PHYS_ADDR_SIZE, 55, 48);
DECL_FIELD_RW(FEATURE_EXT_ID, LIN_ADDR_SIZE, 47, 40);
DECL_FIELD_RW(FEATURE_EXT_ID, PHYS_ADDR_SIZE, 39, 32);
/* Undocumented, exact purpose unknown; believed related to IBS in some way. */
DECL_FIELD_RW(FEATURE_EXT_ID, UNKNOWN_IBS_31, 31, 31);
DECL_FIELD_RW(FEATURE_EXT_ID, PSFD, 28, 28);
DECL_FIELD_RW(FEATURE_EXT_ID, CPPC, 27, 27);
DECL_FIELD_RW(FEATURE_EXT_ID, SSBD, 24, 24);
DECL_FIELD_RW(FEATURE_EXT_ID, PPIN, 23, 23);
/* Undocumented, purpose unknown. */
DECL_FIELD_RW(FEATURE_EXT_ID, UNKNOWN_22, 22, 22);
DECL_FIELD_RW(FEATURE_EXT_ID, TLB_FLUSH_NESTED, 21, 21);
DECL_FIELD_RW(FEATURE_EXT_ID, EFER_LMSLE_UNSUPPORTED, 20, 20);
DECL_FIELD_RW(FEATURE_EXT_ID, IBRS_PROVIDES_SAME_MODE_PROTECTION, 19, 19);
DECL_FIELD_RW(FEATURE_EXT_ID, IBRS_PREFERRED, 18, 18);
DECL_FIELD_RW(FEATURE_EXT_ID, STIBP_ALWAYS_ON, 17, 17);
DECL_FIELD_RW(FEATURE_EXT_ID, IBRS_ALWAYS_ON, 16, 16);
DECL_FIELD_RW(FEATURE_EXT_ID, STIBP, 15, 15);
DECL_FIELD_RW(FEATURE_EXT_ID, IBRS, 14, 14);
DECL_FIELD_RW(FEATURE_EXT_ID, INT_WBINVD, 13, 13);
DECL_FIELD_RW(FEATURE_EXT_ID, IBPB, 12, 12);
/* This field is documented on Zen3, given as reserved on Zen4. */
DECL_FIELD_RW(FEATURE_EXT_ID, LBREXTN, 10, 10);
DECL_FIELD_RW(FEATURE_EXT_ID, WBNOINVD, 9, 9);
DECL_FIELD_RW(FEATURE_EXT_ID, MCOMMIT, 8, 8);
DECL_FIELD_RW(FEATURE_EXT_ID, MBE, 6, 6);
DECL_FIELD_RW(FEATURE_EXT_ID, RDPRU, 4, 4);
DECL_FIELD_RW(FEATURE_EXT_ID, INVLPGB, 3, 3);
DECL_FIELD_RW(FEATURE_EXT_ID, RSTR_FP_ERR_PTRS, 2, 2);
DECL_FIELD_RW(FEATURE_EXT_ID, INST_RET_CNT_MSR, 1, 1);
DECL_FIELD_RW(FEATURE_EXT_ID, CLZERO, 0, 0);

/*
 * Core::X86::Msr::SvmRevFeatId.  Controls feature bits in the %edx value
 * provided by the SVM revision and feature ID cpuid leaf (0x8000000A).  As with
 * other similar registers, it's not known whether these bits control anything
 * beyond that.  This exists on Zen3 and Zen4 (at least) and each thread has its
 * own instance.  Only used fields are declared here.
 */
DECL_REG(SVM_REV_FEAT_ID, 0xC00110DD);
/* XXX Determine whether this field needs to be modified. */
DECL_FIELD_RW(SVM_REV_FEAT_ID, AVIC, 13, 13);

/*
 * Core::X86::Msr::FeatureExt2Eax.  More cpuid control bits, standard disclaimer
 * applies, this one for leaf 0x80000021 %eax.  Known to exist on Zen3 and Zen4.
 * Only used fields are declared here.
 */
DECL_REG(FEATURE_EXT2_EAX, 0xC00110DE);
DECL_FIELD_RW(FEATURE_EXT2_EAX, NULL_SELECTOR_CLEARS_BASE, 6, 6);
/* Undocumented, purpose unknown. */
DECL_FIELD_RW(FEATURE_EXT2_EAX_U_ZEN3_B0, UNKNOWN_4, 4, 4);
DECL_FIELD_RW(FEATURE_EXT2_EAX_U_ZEN4, UNKNOWN_4, 4, 4);

/*
 * Core::X86::Msr::StructExtFeatIdEdx0Ecx0.  More cpuid control bits, standard
 * disclaimer applies.  This is for %ecx and %edx for leaf 7.  Known to exist on
 * at least Zen3 B0+ and Zen4.  Only used fields declared here.
 */
DECL_REG(STRUCT_EXT_FEAT_ID_EDX0_ECX0, 0xC00110DF);
DECL_FIELD_RW(STRUCT_EXT_FEAT_ID_EDX0_ECX0, FSRM, 36, 36);

/*
 * Core::X86::Msr::ChL2Cfg1.  More L2 cache controller configuration bits.  This
 * exists on Zen2-Zen4 and is per-core, but is undocumented on Zen4.  Only used
 * fields are declared here.
 */
DECL_REG(CH_L2_CFG1, 0xC00110E2);
DECL_FIELD_RW(CH_L2_CFG1_U_ZEN3_B0, EN_BUSLOCK_IFETCH, 55, 55);
DECL_FIELD_RW(CH_L2_CFG1, EN_WCB_CONTEXT_DELAY, 49, 49);
DECL_FIELD_RW(CH_L2_CFG1, CBB_LS_TIMEOUT_VALUE, 47, 45);
DECL_FIELD_RW(CH_L2_CFG1, CBB_PROBE_TIMEOUT_VALUE, 44, 42);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 32, 0);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 64, 1);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 96, 2);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 128, 3);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 160, 4);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 192, 5);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 224, 6);
DECL_CONST(CH_L2_CFG1, CBB_TIMEOUT_VALUE, 256, 7);
DECL_FIELD_RW(CH_L2_CFG1, CBB_EN_L2_MISS, 41, 41);
DECL_FIELD_RW(CH_L2_CFG1, CBB_BLOCK_LS_REQUESTS, 39, 39);
DECL_FIELD_RW(CH_L2_CFG1, CBB_BLOCK_PRB_SHR, 38, 38);
DECL_FIELD_RW(CH_L2_CFG1, CBB_BLOCK_PRB_MIG, 37, 37);
DECL_FIELD_RW(CH_L2_CFG1, CBB_BLOCK_PRB_INV, 36, 36);
DECL_FIELD_RW(CH_L2_CFG1, CBB_ALLOC_FOR_CHG_TO_X, 35, 35);
DECL_FIELD_RW(CH_L2_CFG1, CBB_ALLOC_FOR_EXCLUSIVE, 33, 33);
DECL_FIELD_RW(CH_L2_CFG1, CBB_MASTER_EN, 32, 32);
DECL_FIELD_RW(CH_L2_CFG1, EN_PROBE_INTERRUPT, 19, 19);
DECL_FIELD_RW(CH_L2_CFG1, EN_MIB_TOKEN_DELAY, 4, 4);
DECL_FIELD_RW(CH_L2_CFG1, EN_MIB_THROTTLING, 3, 3);

/*
 * Undocumented register, name and purpose unknown.  Known to exist on Zen2-Zen4
 * processors.
 */
DECL_REG(UNKNOWN_C001_10E3, 0xC00110E3);
DECL_FIELD_RW(UNKNOWN_C001_10E3_U_ZEN4, PAUSE_IDLE_CYC, 17, 16);
DECL_CONST(UNKNOWN_C001_10E3_U_ZEN4, PAUSE_IDLE_CYC, 16, 0);
DECL_CONST(UNKNOWN_C001_10E3_U_ZEN4, PAUSE_IDLE_CYC, 32, 1);
DECL_CONST(UNKNOWN_C001_10E3_U_ZEN4, PAUSE_IDLE_CYC, 64, 2);
DECL_CONST(UNKNOWN_C001_10E3_U_ZEN4, PAUSE_IDLE_CYC, 128, 3);
DECL_FIELD_RW(UNKNOWN_C001_10E3, UNKNOWN_3, 3, 3);
DECL_FIELD_RW(UNKNOWN_C001_10E3, UNKNOWN_1, 1, 1);

/* Undocumented register, name and purpose unknown.  Known to exist on Milan. */
DECL_REG(UNKNOWN_C001_10E4, 0xC00110E4);
DECL_FIELD_RW(UNKNOWN_C001_10E4, UNKNOWN_31_30, 31, 30);
DECL_FIELD_RW(UNKNOWN_C001_10E4, UNKNOWN_23, 23, 23);

/*
 * Core::X86::Msr::LS_CFG3.  Yet more load-store config bits.  Exists on Zen3
 * and Zen4 but undocumented on Zen4; only used fields declared here.
 */
DECL_REG(LS_CFG3, 0xC00110E5);
/* Undocumented fields, purpose(s) unknown. */
DECL_FIELD_RW(LS_CFG3, UNKNOWN_62, 62, 62);
DECL_FIELD_RW(LS_CFG3, UNKNOWN_60, 60, 60);
DECL_FIELD_RW(LS_CFG3, UNKNOWN_57, 57, 57);
DECL_FIELD_RW(LS_CFG3, UNKNOWN_56, 56, 56);
DECL_FIELD_RW(LS_CFG3, DIS_NC_FILLWITH_LTLI, 42, 42);
/*
 * As the name implies, disables speculatively fetching from WC regions when the
 * load is not part of a stream.  We are told that for this to work (and we do
 * want it to work), we must also ensure that DE_CFG[LFENCE_DISPATCH] is set.
 * Fortunately we do that unconditionally on every processor that has this
 * register.
 */
DECL_FIELD_RW(LS_CFG3, DIS_SPEC_WC_NON_STRM_LD, 30, 30);
DECL_FIELD_RW(LS_CFG3, EN_SPEC_ST_FILL, 26, 26);
DECL_FIELD_RW(LS_CFG3, DIS_FAST_LD_BARRIER, 19, 19);
DECL_FIELD_RW(LS_CFG3, DIS_MAB_FULL_SLEEP, 13, 13);
DECL_FIELD_RW(LS_CFG3, DVM_SYNC_ONLY_ON_TLBI, 8, 8);

/*
 * Core::X86::Msr::LS_CFG4.  Yet more load-store config bits.  Exists on Zen3
 * and Zen4 but undocumented on Zen4; only used fields declared here
 */
DECL_REG(LS_CFG4, 0xC00110E6);
DECL_FIELD_RW(LS_CFG4, DIS_LIVE_LOCK_CNT_FST_BUSLOCK, 12, 12);
DECL_FIELD_RW(LS_CFG4, LIVE_LOCK_DET_FORCE_SBEX, 0, 0);

/*
 * Core::X86::Msr::DC_CFG2.  More D-cache config bits.  This register exists on
 * Zen3 and Zen4 but is undocumented on Zen4; only used fields declared here.
 */
DECL_REG(DC_CFG2, 0xC00110E7);
DECL_FIELD_RW(DC_CFG2, DIS_SCB_NTA_L1, 7, 7);
DECL_FIELD_RW(DC_CFG2, DIS_DMB_STORE_LOCK, 3, 3);

/*
 * Core::X86::Msr::ChL2AaCfg.  L2 adaptive allocation configuration bits.
 * Present on Zen3 and Zen4 only and undocumented on Zen4; only used fields
 * declared here.
 */
DECL_REG(CH_L2_AA_CFG, 0xC00110E8);
DECL_FIELD_RW(CH_L2_AA_CFG, SCALE_DEMAND, 11, 10);
DECL_FIELD_RW(CH_L2_AA_CFG, SCALE_MISS_L3, 9, 8);
DECL_FIELD_RW(CH_L2_AA_CFG, SCALE_MISS_L3_BW, 7, 6);
DECL_FIELD_RW(CH_L2_AA_CFG, SCALE_REMOTE, 5, 4);
DECL_CONST(CH_L2_AA_CFG, SCALE, MUL1, 0);
DECL_CONST(CH_L2_AA_CFG, SCALE, MUL2, 1);
DECL_CONST(CH_L2_AA_CFG, SCALE, MUL3, 2);
DECL_CONST(CH_L2_AA_CFG, SCALE, MUL4, 3);

/*
 * Core::X86::Msr::ChL2AaPairCfg0.  L2 adaptive allocation configuration for
 * dueling pairs.  Present on Zen3 and Zen4 only and undocumented on Zen4.  Only
 * used fields are declared here.
 */
DECL_REG(CH_L2_AA_PAIR_CFG0, 0xC00110E9);
DECL_FIELD_RW(CH_L2_AA_PAIR_CFG0, SUPPRESS_DIFF_VICT, 24, 24);

/*
 * Core::X86::Msr::ChL2AaPairCfg1.  L2 adaptive allocation configuration for
 * dueling pairs.  This register is present and documented on Zen3 and one would
 * expect it to exist on Zen4 but it is undocumented there and may not exist.
 */
DECL_REG(CH_L2_AA_PAIR_CFG1, 0xC00110EA);
DECL_FIELD_RW(CH_L2_AA_PAIR_CFG1, NOT_UNUSED_PF_RRIP_LVL_B4_L1V, 23, 22);
DECL_FIELD_RW(CH_L2_AA_PAIR_CFG1, DEMAND_HIT_PF_RRIP, 7, 4);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_CCX_H */
