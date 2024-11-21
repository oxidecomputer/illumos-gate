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

#ifndef _SYS_IO_MILAN_PPTABLE_H
#define	_SYS_IO_MILAN_PPTABLE_H

/*
 * Defines Milan-specific Power and Performance table (PPTable) structures.
 * These are SMU firmware specific.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These next structures assume standard x86 ILP32 alignment.
 */
#pragma pack(4)

/*
 * Default limits in the system.
 */
typedef struct milan_pptable_default_limits {
	uint32_t	mppd_tdp; /* W */
	uint32_t	mppd_ppt; /* W */
	uint32_t	mppd_tdc; /* A */
	uint32_t	mppd_edc; /* A */
	uint32_t	mppd_tjmax; /* 'C */
} milan_pptable_default_limits_t;

/*
 * Platform specific limits.
 */
typedef struct milan_pptable_platform_limits {
	uint32_t	mppp_tdp; /* W */
	uint32_t	mppp_ppt; /* W */
	uint32_t	mppp_tdc; /* A */
	uint32_t	mppp_edc; /* A */
} milan_pptable_platform_limits_t;

/*
 * Fan override table. The first element controls whether the other values are
 * used. We leave this all at zero.
 */
typedef struct milan_pptable_fan {
	uint8_t		mppf_override; /* bool */
	uint8_t		mppf_hyst;
	uint8_t		mppf_temp_low;
	uint8_t		mppf_temp_med;
	uint8_t		mppf_temp_high;
	uint8_t		mppf_temp_crit;
	uint8_t		mppf_pwm_low;
	uint8_t		mppf_pwm_med;
	uint8_t		mppf_pwm_high;
	uint8_t		mppf_pwm_freq;
	uint8_t		mppf_polarity;
	uint8_t		mppf_rsvd;
} milan_pptable_fan_t;

/*
 * Misc. debug options.
 */
typedef struct milan_pptable_debug {
	int32_t		mppd_core_dldo_margin;
	int32_t		mppd_vddcr_cpu_margin; /* mV */
	int32_t		mppd_vddcr_soc_margin; /* mV */
	uint8_t		mppd_cc1_dis; /* bool */
	uint8_t		mppd_detpct_en; /* bool */
	uint8_t		mppd_detpct; /* percent */
	uint8_t		mppd_ccx_dci_mode; /* 1: async */
	uint8_t		mppd_apb_dis; /* bool */
	uint8_t		mppd_eff_mode_en; /* bool */
	uint8_t		mppd_pwr_mgmt_override; /* bool */
	/*
	 * 0: telemetry
	 * 1: per part
	 * 2: force 100% determinism
	 * 3: default 100% determinism
	 * 4: default 0% determinism
	 */
	uint8_t		mppd_pwr_mgmt;
	uint8_t		mppd_esm[4]; /* per-NBIO_QUADRANT; bool */
} milan_pptable_debug_t;

/*
 * DF Cstate configuration.
 */
typedef struct milan_pptable_df_cstate {
	uint8_t		mppdc_override;
	uint8_t		mppdc_clk_pwrdn;
	uint8_t		mppdc_refresh_en;
	uint8_t		mppdc_gmi_pwrdn;
	uint8_t		mppdc_gop_pwrdn;
	uint8_t		mppdc_rsvd[2];
} milan_pptable_df_cstate_t;

/*
 * xGMI configuration.
 */
typedef struct milan_pptable_xgmi {
	uint8_t		mppx_max_width_en;
	uint8_t		mppx_max_width; /* 0...1 */
	uint8_t		mppx_min_width_en;
	uint8_t		mppx_min_width;
	uint8_t		mppx_force_width_en;
	uint8_t		mppx_force_width; /* 0...2 */
	uint8_t		mppx_rsvd[2];
} milan_pptable_xgmi_t;

/*
 * Telemetry and Calibration:
 * VDD compensation for voltage drop due to high current.
 */
typedef struct milan_pptable_telemetry {
	uint32_t	mppt_vddcr_cpu_full_scale; /* A */
	int32_t		mppt_vddcr_cpu_offset; /* A */
	uint32_t	mppt_vddcr_soc_full_scale; /* A */
	int32_t		mppt_vddcr_soc_offset; /* A */
} milan_pptable_telemetry_t;

/*
 * Overclocking.
 */
typedef struct milan_pptable_overclock {
	uint8_t		mppo_oc_dis; /* bool */
	uint8_t		mppo_oc_min_vid;
	uint16_t	mppo_oc_max_freq; /* MHz */
} milan_pptable_overclock_t;

/*
 * Clock frequency forcing
 */
typedef struct milan_pptable_cff {
	uint16_t	mppc_cclk_freq; /* MHz; 0: don't force */
	uint16_t	mppc_fmax_override; /* MHz; 0: don't override */
	uint8_t		mppc_apbdis_dfps; /* dfps index to set when apbdis */
	uint8_t		mppc_dfps_freqo_dis; /* bool */
	uint8_t		mppc_dfps_lato_dis; /* bool */
	uint8_t		mppc_cclk_rsvd[1];
} milan_pptable_cff_t;

/*
 * HTF Overrides
 */
typedef struct milan_pptable_htf_overrides {
	uint16_t	mpph_htf_temp_max; /* 'C; 0 means use fused value */
	uint16_t	mpph_htf_freq_max; /* MHz; 0 means use fused value */
	uint16_t	mpph_mtf_temp_max; /* 'C; 0 means use fused value */
	uint16_t	mpph_mtf_freq_max; /* MHz; 0 means use fused value */
} milan_pptable_htf_overrides_t;

/*
 * Various Collaborative Processor Performance Control (CPPC) settings.
 */
typedef struct milan_pptable_cppc {
	uint8_t		mppc_override; /* bool */
	uint8_t		mppc_epp; /* 0...100 */
	uint8_t		mppc_perf_max; /* 0...100 */
	uint8_t		mppc_perf_min; /* 0...100 */
	/* APICID mapping */
	uint16_t	mppc_thr_map_count;
	uint8_t		mppc_rsvd[2];
	uint16_t	mppc_thr_map[256];
} milan_pptable_cppc_t;

/*
 * Power and Performance Table.
 * This is the version of the table that was introduced in Milan SMU firmware
 * version 45.65.0. It is valid across Milan PI firmware versions 1.0.0.2 to
 * 1.0.0.e.
 */
typedef struct milan_pptable_v45_65 {
	milan_pptable_default_limits_t		mpp_default_limits;
	milan_pptable_platform_limits_t		mpp_platform_limits;
	milan_pptable_fan_t			mpp_fan;
	milan_pptable_debug_t			mpp_debug;
	milan_pptable_df_cstate_t		mpp_df_cstate;
	uint8_t					mpp_ccr_en;
	milan_pptable_xgmi_t			mpp_xgmi;
	milan_pptable_telemetry_t		mpp_telemetry;
	milan_pptable_overclock_t		mpp_overclock;
	milan_pptable_cff_t			mpp_cff;
	milan_pptable_htf_overrides_t		mpp_htf_overrides;
	milan_pptable_cppc_t			mpp_cppc;

	uint16_t	mpp_vddcr_cpu_force; /* mV; 0: don't force */
	uint16_t	mpp_vddcr_soc_force; /* mV; 0: don't force */
	uint16_t	mpp_cstate_boost_override; /* 0: no override */
	uint16_t	mpp_global_fmax_override; /* MHz; 0: no override */
	uint8_t		mpp_max_did_override; /* 0: no override */
	uint8_t		mpp_cca_en; /* bool */
	uint8_t		mpp_rsvd1[2];
	uint32_t	mpp_l3credit_ceil;

	uint32_t	mpp_rsvd2[28];
} milan_pptable_v45_65_t;

CTASSERT(sizeof (milan_pptable_v45_65_t) == 0x304);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_default_limits) == 0x0);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_platform_limits) == 0x14);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_fan) == 0x24);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_debug) == 0x30);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_df_cstate) == 0x48);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_ccr_en) == 0x4f);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_xgmi) == 0x50);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_telemetry) == 0x58);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_overclock) == 0x68);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_cff) == 0x6c);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_htf_overrides) == 0x74);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_cppc) == 0x7c);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_vddcr_cpu_force) == 0x284);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_vddcr_soc_force) == 0x286);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_cstate_boost_override) == 0x288);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_global_fmax_override) == 0x28a);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_max_did_override) == 0x28c);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_cca_en) == 0x28d);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_rsvd1) == 0x28e);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_l3credit_ceil) == 0x290);
CTASSERT(offsetof(milan_pptable_v45_65_t, mpp_rsvd2) == 0x294);

#pragma pack()  /* pragma pack(4) */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_PPTABLE_H */
