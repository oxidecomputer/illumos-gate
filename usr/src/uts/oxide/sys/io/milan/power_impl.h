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

#ifndef _SYS_IO_MILAN_POWER_IMPL_H
#define	_SYS_IO_MILAN_POWER_IMPL_H

/*
 * Power performance table (PPT) definition.
 */

#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The structure of the Power Performance Table (PPT) passed to the SMU.  Its
 * structure depends on the version of the SMU.  For most (and perhaps all?)
 * values, 0 denotes that the fused value should be used.
 */
typedef struct smu_ppt {
	uint32_t sppt_tdp;		/* Thermal Design Power (watts) */
	uint32_t sppt_ppt;		/* Package Power Tracking (watts) */
	uint32_t sppt_tdc;		/* Thermal Design Current (amps) */
	uint32_t sppt_edc;		/* Engineering Design Current (amps) */
	uint32_t sppt_tj_max;		/* maximum thermal junction (deg C) */
	uint32_t sppt_tdp_platlimit;	/* platform-limited TDP */
	uint32_t sppt_ppt_platlimit;	/* platform-limited PPT */
	uint32_t sppt_tdc_platlimit;	/* platform-limited TDC */
	uint32_t sppt_edc_platlimit;	/* platform-limited EDC */

	/*
	 * The fan table is described in 55483, Sec. 3.6.1.5.2.
	 */
	uint8_t sppt_fan_override;
	uint8_t sppt_fan_hysteresis;
	uint8_t sppt_fan_templow;
	uint8_t sppt_fan_tempmed;
	uint8_t sppt_fan_temphigh;
	uint8_t sppt_fan_tempcrit;
	uint8_t sppt_fan_pwmlow;
	uint8_t sppt_fan_pwmmed;
	uint8_t sppt_fan_pwmhigh;
	uint8_t sppt_fan_pwmfreq;
	uint8_t sppt_fan_polarity;
	uint8_t sppt_fan_pad;

	int32_t sppt_dldo_psm_margin;	/* margin on dLDO PSM */
	int32_t sppt_vddcr_cpu_margin;	/* margin on VDDCR CPU (mV) */
	int32_t sppt_vddcr_soc_margin;	/* margin on VDDCR SoC (mV) */
	uint8_t sppt_cc1_disable;	/* disable CC 1 */
	uint8_t sppt_determinism_en;	/* determinism enable */
	uint8_t sppt_determinism_perc;	/* determinism percentage */
	uint8_t sppt_ccx_vdci_async;	/* enable async mode */
	uint8_t sppt_apb_disable;	/* disable Algorithmic Perf. Boost */
	uint8_t sppt_effopt_mode;	/* enable efficiency optimized mode */
	uint8_t sppt_mgmtmode_override;	/* override fused power mgmt mode */
	uint8_t sppt_mgmtmode;		/* power management mode */
	uint8_t sppt_pcie_esm_mode[4];	/* PCIe ESM mode, per NBIO */

	/*
	 * Data fabric CState enables
	 */
	uint8_t sppt_df_override;
	uint8_t sppt_df_clkpwrdown;
	uint8_t sppt_df_selfrefrn;
	uint8_t sppt_df_gmipwrdn;
	uint8_t sppt_df_goppwrdn;
	uint8_t sppt_df_pad[3];

	/*
	 * xGMI
	 */
	uint8_t sppt_xgmi_maxw_en;	/* enable xGMI max link width */
	uint8_t sppt_xgmi_maxw;		/* xGMI max link width */
	uint8_t sppt_xgmi_forcew_en;	/* enable xGMI force link width */
	uint8_t sppt_xgmi_forcew;	/* xGMI force link width */

	/*
	 * Telemetry (Family 17h)
	 */
	uint32_t sppt_telem_cpu_full;
	int32_t sppt_telem_cpu_offs;
	uint32_t sppt_telem_soc_full;
	int32_t sppt_telem_soc_offs;

	/*
	 * Overclocking
	 */
	uint8_t sppt_oc_disable;	/* disable overclocking */
	uint8_t sppt_oc_maxvoltage;	/* maximum overclock voltage (mV) */
	uint16_t sppt_oc_maxfreq;	/* maximum overclock frequency (MHz) */

	uint16_t sppt_force_cclk_freq;	/* forced core clock (MHz) */
	uint16_t sppt_fmax_override;	/* override fabric clock (MHz) */
	uint8_t sppt_apbdis_dfpstate;	/* DF PState when APB disabled */
	uint8_t sppt_dffo_disable;	/* disable DF frequency optimizer */
	uint8_t sppt_dflo_disable;	/* disable DF latency optimizer */
	uint8_t sppt_pad;

	uint16_t sppt_ht_fmax_temp;
	uint16_t sppt_ht_fmax_freq;
	uint16_t sppt_mt_fmax_temp;
	uint16_t sppt_mt_fmax_freq;

	/*
	 * Collaborative Processor Performance Control (CPPC)
	 */
	uint8_t sppt_cppc_override;	/* override CPPC settings */
	uint8_t sppt_cppc_epp;		/* Energy Performance Pref. (0-100) */
	uint8_t sppt_cppc_maxperf;	/* max perf limit when dis. (0-100) */
	uint8_t sppt_cppc_minperf;	/* min perf limit when dis. (0-100) */

	/*
	 * CPPC APIC settings
	 */
	uint16_t sppt_cppc_apicmap_size; /* size thread-to-local APIC map */
	uint8_t sppt_cppc_apicmap_pad[2];
	uint16_t sppt_cppc_apicmap[256]; /* map of thread-to-local APIC */

	/*
	 * Cats and dogs
	 */
	uint16_t sppt_cpu_voltage;	/* forced VDDCR CPU voltage (mV) */
	uint16_t sppt_soc_voltage;	/* forced VDDCR SoC voltage (mV) */
	uint16_t sppt_cstate_boost;	/* CState boost threshold */
	uint16_t sppt_fmax_override_all; /* all cores FCLK override (MHz) */
	uint8_t sppt_max_did_delta;	/* ?? */
	uint8_t sppt_cca_enabled;	/* ?? */
	uint8_t sppt_pad1[2];
	uint32_t sppt_l3_threshold_ceil; /* L3 bandwidth mgmt related? */
	uint32_t sppt_pad2[29];
} smu_ppt_t;

typedef struct milan_power {
	smu_ppt_t		*mpwr_ppt;
	uint64_t		mpwr_pa;
	uint32_t		mpwr_alloc_len;
} milan_power_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_POWER_IMPL_H */
