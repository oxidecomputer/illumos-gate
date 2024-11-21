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

#ifndef _SYS_IO_TURIN_PPTABLE_H
#define	_SYS_IO_TURIN_PPTABLE_H

/*
 * Defines Turin-specific Power and Performance table (PPTable) structures.
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
typedef struct turin_pptable_default_limits {
	uint32_t	tppd_tdp; /* W */
	uint32_t	tppd_ppt; /* W */
	uint32_t	tppd_tdc; /* A */
	uint32_t	tppd_edc; /* A */
	uint32_t	tppd_tjmax; /* 'C */
} turin_pptable_default_limits_t;

/*
 * Platform specific limits.
 */
typedef struct turin_pptable_platform_limits {
	uint32_t	tppp_tdp; /* W */
	uint32_t	tppp_ppt; /* W */
	uint32_t	tppp_tdc; /* A */
	uint32_t	tppp_edc; /* A */
} turin_pptable_platform_limits_t;

/*
 * Misc. debug options.
 */
typedef struct turin_pptable_debug {
	int32_t		tppd_core_dldo_margin;
	int32_t		tppd_vddcr_cpu_margin; /* mV */
	int32_t		tppd_vddcr_soc_margin; /* mV */
	int32_t		tppd_vddio_margin; /* mV */
	uint8_t		tppd_cc1_dis; /* bool */
	uint8_t		tppd_detctl;
	uint8_t		tppd_ccx_dci_mode; /* 1: async */
	uint8_t		tppd_apb_dis; /* bool */
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
	uint8_t		tppd_thrtl_mode;
	uint8_t		tppd_rsvd;
} turin_pptable_debug_t;

/*
 * DF Cstate configuration.
 */
typedef struct turin_pptable_df_cstate {
	uint8_t		tppdc_override;
	uint8_t		tppdc_clk_pwrdn;
	uint8_t		tppdc_refresh_en;
	uint8_t		tppdc_gmi_pwrdn;
	uint8_t		tppdc_gop_pwrdn;
	uint8_t		tppdc_rsvd[3];
} turin_pptable_df_cstate_t;

/*
 * xGMI configuration.
 */
typedef struct turin_pptable_xgmi {
	uint8_t		tppx_max_width_en;
	uint8_t		tppx_max_width; /* 0...1 */
	uint8_t		tppx_force_width_en;
	uint8_t		tppx_force_width; /* 0...2 */
} turin_pptable_xgmi_t;

/*
 * Telemetry and Calibration:
 * VDD compensation for voltage drop due to high current.
 */
typedef struct turin_pptable_telemetry {
	uint8_t		tppt_guard_band;
	uint8_t		tppt_svi3_speed;
	uint16_t	tppt_pcc_limit; /* A */
	uint8_t		tppt_i3c_pphcnt;
	uint8_t		tppt_i3c_speed;
	uint8_t		tppt_i3c_sdahold[4];
} turin_pptable_telemetry_t;

/*
 * DRAM Post Package Repair Configuration
 */
typedef struct turin_pptable_dram {
	uint8_t		tppd_ppr_cfginit; /* 0: in-band, 1: out-of-band */
	uint8_t		tppd_rsvd;
} turin_pptable_dram_t;

/*
 * Overclocking.
 */
typedef struct turin_pptable_overclock {
	uint8_t		tppo_oc_dis; /* bool */
	uint8_t		tppo_oc_max_vid;
	uint16_t	tppo_oc_max_freq; /* MHz */
} turin_pptable_overclock_t;

/*
 * Clock frequency forcing
 */
typedef struct turin_pptable_cff {
	uint16_t	tppc_cclk_freq; /* MHz; 0: don't force */
	uint16_t	tppc_fmax_override; /* MHz; 0: don't override */
	uint8_t		tppc_apbdis_dfps; /* dfps index to set when apbdis */
	uint8_t		tppc_dffo_dis;
	uint16_t	tppc_cpu_voltage; /* mV; 0: don't force */
	uint16_t	tppc_soc_voltage; /* mV; 0: don't force */
	uint16_t	tppc_io_voltage; /* mV; 0: don't force */
} turin_pptable_cff_t;

/*
 * Power and Performance Table.
 * This is the version of the table that was introduced in Turin SMU firmware
 * version 94.91.0 and Dense Turin SMU firmware version 99.91.0. It is valid
 * across Turin PI firmware versions 0.0.9.0 to 1.0.0.2.
 */
typedef struct turin_pptable_v94_91 {
	turin_pptable_default_limits_t		tpp_default_limits;
	turin_pptable_platform_limits_t		tpp_platform_limits;
	turin_pptable_debug_t			tpp_debug;
	turin_pptable_df_cstate_t		tpp_df_cstate;
	turin_pptable_xgmi_t			tpp_xgmi;
	turin_pptable_telemetry_t		tpp_telemetry;
	turin_pptable_dram_t			tpp_dram;
	turin_pptable_overclock_t		tpp_overclock;
	turin_pptable_cff_t			tpp_cff;

	uint8_t					tpp_df_pstate_range_en;
	uint8_t					tpp_df_pstate_range_min;
	uint8_t					tpp_df_pstate_range_max;
	uint8_t					tpp_df_pstate_range_spare;
	uint8_t					tpp_xgmi_pstate_range_en;
	uint8_t					tpp_xgmi_pstate_range_min;
	uint8_t					tpp_xgmi_pstate_range_max;
	uint8_t					tpp_xgmi_pstate_range_spare;
	uint8_t					tpp_xgmi_min_width;
	uint8_t					tpp_rsvd1[3];

	uint32_t				tpp_rsvd2[8];
} turin_pptable_v94_91_t;

CTASSERT(sizeof (turin_pptable_v94_91_t) == 0x90);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_default_limits) == 0x0);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_platform_limits) == 0x14);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_debug) == 0x24);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_df_cstate) == 0x3c);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_xgmi) == 0x44);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_telemetry) == 0x48);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_dram) == 0x52);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_overclock) == 0x54);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_cff) == 0x58);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_df_pstate_range_en) == 0x64);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_df_pstate_range_min) == 0x65);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_df_pstate_range_max) == 0x66);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_df_pstate_range_spare) == 0x67);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_xgmi_pstate_range_en) == 0x68);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_xgmi_pstate_range_min) == 0x69);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_xgmi_pstate_range_max) == 0x6a);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_xgmi_pstate_range_spare) == 0x6b);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_xgmi_min_width) == 0x6c);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_rsvd1) == 0x6d);
CTASSERT(offsetof(turin_pptable_v94_91_t, tpp_rsvd2) == 0x70);

#pragma pack()  /* pragma pack(4) */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_PPTABLE_H */
