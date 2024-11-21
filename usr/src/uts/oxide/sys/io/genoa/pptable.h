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

#ifndef _SYS_IO_GENOA_PPTABLE_H
#define	_SYS_IO_GENOA_PPTABLE_H

/*
 * Defines Genoa-specific Power and Performance table (PPTable) structures.
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
typedef struct genoa_pptable_default_limits {
	uint32_t	gppd_tdp; /* W */
	uint32_t	gppd_ppt; /* W */
	uint32_t	gppd_tdc; /* A */
	uint32_t	gppd_edc; /* A */
	uint32_t	gppd_tjmax; /* 'C */
} genoa_pptable_default_limits_t;

/*
 * Platform specific limits.
 */
typedef struct genoa_pptable_platform_limits {
	uint32_t	gppp_tdp; /* W */
	uint32_t	gppp_ppt; /* W */
	uint32_t	gppp_tdc; /* A */
	uint32_t	gppp_edc; /* A */
} genoa_pptable_platform_limits_t;

/*
 * Fan override table. The first element controls whether the other values are
 * used. We leave this all at zero.
 */
typedef struct genoa_pptable_fan {
	uint8_t		gppf_override; /* bool */
	uint8_t		gppf_hyst;
	uint8_t		gppf_temp_low;
	uint8_t		gppf_temp_med;
	uint8_t		gppf_temp_high;
	uint8_t		gppf_temp_crit;
	uint8_t		gppf_pwm_low;
	uint8_t		gppf_pwm_med;
	uint8_t		gppf_pwm_high;
	uint8_t		gppf_pwm_freq;
	uint8_t		gppf_polarity;
	uint8_t		gppf_rsvd;
} genoa_pptable_fan_t;

/*
 * Misc. debug options.
 */
typedef struct genoa_pptable_debug {
	int32_t		gppd_core_dldo_margin;
	int32_t		gppd_vddcr_cpu_margin; /* mV */
	int32_t		gppd_vddcr_soc_margin; /* mV */
	int32_t		gppd_vddio_margin; /* mV */
	uint8_t		gppd_cc1_dis; /* bool */
	uint8_t		tppd_detctl;
	uint8_t		gppd_ccx_dci_mode; /* 1: async */
	uint8_t		gppd_apb_dis; /* bool */
	/*
	 * 0 - High Performance (default)
	 * 1 - Efficiency Mode
	 * 2 - Maximum IO Performance Mode
	 */
	uint8_t		tppd_eff_mode_policy;
	/*
	 * 0 - Enable PCIe speed controller
	 * 1 - Limit to Gen4
	 * 2 - Limit to Gen5
	 */
	uint8_t		tppd_pcie_spdctrl;
	uint8_t		gppd_spare[2]; /* per-NBIO_QUADRANT; bool */
} genoa_pptable_debug_t;

/*
 * DF Cstate configuration.
 */
typedef struct genoa_pptable_df_cstate {
	uint8_t		gppdc_override;
	uint8_t		gppdc_clk_pwrdn;
	uint8_t		gppdc_refresh_en;
	uint8_t		gppdc_gmi_pwrdn;
	uint8_t		gppdc_gop_pwrdn;
	uint8_t		gppdc_rsvd[3];
} genoa_pptable_df_cstate_t;

/*
 * xGMI configuration.
 */
typedef struct genoa_pptable_xgmi {
	uint8_t		gppx_max_width_en;
	uint8_t		gppx_max_width;
	uint8_t		gppx_force_width_en;
	uint8_t		gppx_force_width;
} genoa_pptable_xgmi_t;

/*
 * GFXCLK / GFXOFF related parameters.
 */
typedef struct genoa_pptable_gfx {
	uint8_t		gppg_clksrc;
	uint8_t		gppg_msmurstr_dis;
	uint8_t		gppg_apcc_en;
	uint8_t		gppg_rsvd1;
	uint16_t	gppg_vinit; /* mV */
	uint16_t	gppg_finit; /* MHz */
	uint16_t	gppg_fentry; /* MHz */
	uint16_t	gppg_fidle; /* MHz */
	uint16_t	gppg_clkslew;
	uint16_t	gppg_clkfmin; /* MHz */
	uint16_t	gppg_vmin; /* mV */
	uint16_t	gppg_vmax; /* mV */
	uint16_t	gppg_clkfforce; /* MHz */
	uint16_t	gppg_clkfmax; /* MHz */
	uint8_t		gppg_dldo_bypass;
	uint8_t		gppg_rsvd2[3];
} genoa_pptable_gfx_t;

/*
 * Telemetry and Calibration.
 */
typedef struct genoa_pptable_telemetry {
	uint8_t		gppt_i3c_sdahold[4];
	uint8_t		gppt_guard_band;
	uint8_t		tppt_svi3_speed;
	uint8_t		tppt_rsvd1[2];
	uint16_t	tppt_pcc_limit; /* A */
	uint8_t		tppt_i3c_pphcnt;
	uint8_t		tppt_i3c_speed;
	uint16_t	tppt_rsvd2[2];
} genoa_pptable_telemetry_t;

/*
 * Overclocking.
 */
typedef struct genoa_pptable_overclock {
	uint8_t		gppo_oc_dis; /* bool */
	uint8_t		gppo_oc_max_vid;
	uint16_t	gppo_oc_max_freq; /* MHz */
} genoa_pptable_overclock_t;

/*
 * Clock frequency forcing
 */
typedef struct genoa_pptable_cff {
	uint16_t	gppc_cclk_freq; /* MHz; 0: don't force */
	uint16_t	gppc_fmax_override; /* MHz; 0: don't override */
	uint8_t		tppc_apbdis_dfps; /* dfps index to set when apbdis */
	uint8_t		tppc_dffo_dis;
	uint8_t		tppc_rsvd1;
	uint8_t		tppc_dfsbypass_dis;
} genoa_pptable_cff_t;

/*
 * HTF Overrides
 */
typedef struct genoa_pptable_htf_overrides {
	uint16_t	gpph_htf_temp_max; /* 'C; 0 means use fused value */
	uint16_t	gpph_htf_freq_max; /* MHz; 0 means use fused value */
	uint16_t	gpph_mtf_temp_max; /* 'C; 0 means use fused value */
	uint16_t	gpph_mtf_freq_max; /* MHz; 0 means use fused value */
} genoa_pptable_htf_overrides_t;

/*
 * Various CPPC settings.
 */
typedef struct genoa_pptable_cppc {
	uint8_t		gppc_override; /* bool */
	uint8_t		gppc_epp; /* 0...100 */
	uint8_t		gppc_perf_max; /* 0...100 */
	uint8_t		gppc_perf_min; /* 0...100 */
	/* APICID mapping */
	uint16_t	gppc_thr_map_count;
	uint8_t		gppc_rsvd[2];
	uint16_t	gppc_thr_map[512];
} genoa_pptable_cppc_t;

/*
 * Power and Performance Table.
 * This is the version of the table that was introduced prior to Genoa SMU
 * firmware version 4.71.111. It is valid across Genoa PI firmware versions
 * 1.0.0.8 to 1.0.0.d.
 */
typedef struct genoa_pptable_v71_111 {
	genoa_pptable_default_limits_t		gpp_default_limits;
	genoa_pptable_platform_limits_t		gpp_platform_limits;
	genoa_pptable_fan_t			gpp_fan;
	genoa_pptable_debug_t			gpp_debug;
	genoa_pptable_df_cstate_t		gpp_df_cstate;
	genoa_pptable_xgmi_t			gpp_xgmi;
	genoa_pptable_gfx_t			gpp_gfx;
	genoa_pptable_telemetry_t		gpp_telemetry;
	genoa_pptable_overclock_t		gpp_overclock;
	genoa_pptable_cff_t			gpp_cff;
	genoa_pptable_htf_overrides_t		gpp_htf_overrides;
	genoa_pptable_cppc_t			gpp_cppc;

	uint16_t	gpp_vddcr_cpu_force; /* mV; 0: don't force */
	uint16_t	gpp_vddcr_soc_force; /* mV; 0: don't force */
	uint16_t	gpp_vddcr_io_force; /* mV; 0: don't force */
	uint8_t		gpp_rsvd1[2];
	uint32_t	gpp_min_s0i3_sleep; /* ms */
	uint32_t	gpp_wlan;
	uint8_t		gpp_df_pstate_range_en;
	uint8_t		gpp_df_pstate_range_min;
	uint8_t		gpp_df_pstate_range_max;
	uint8_t		gpp_df_pstate_range_spare;


	uint32_t	gpp_rsvd2[28];
} genoa_pptable_v71_111_t;

CTASSERT(sizeof (genoa_pptable_v71_111_t) == 0x520);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_default_limits) == 0x0);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_platform_limits) == 0x14);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_fan) == 0x24);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_debug) == 0x30);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_df_cstate) == 0x48);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_xgmi) == 0x50);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_gfx) == 0x54);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_telemetry) == 0x70);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_overclock) == 0x80);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_cff) == 0x84);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_htf_overrides) == 0x8c);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_cppc) == 0x94);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_vddcr_cpu_force) == 0x49c);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_vddcr_soc_force) == 0x49e);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_vddcr_io_force) == 0x4a0);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_rsvd1) == 0x4a2);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_min_s0i3_sleep) == 0x4a4);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_wlan) == 0x4a8);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_df_pstate_range_en) == 0x4ac);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_df_pstate_range_min) == 0x4ad);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_df_pstate_range_max) == 0x4ae);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_df_pstate_range_spare) == 0x4af);
CTASSERT(offsetof(genoa_pptable_v71_111_t, gpp_rsvd2) == 0x4b0);

#pragma pack()  /* pragma pack(4) */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_PPTABLE_H */
