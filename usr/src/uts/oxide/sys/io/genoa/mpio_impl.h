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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _SYS_IO_GENOA_MPIO_IMPL_H
#define	_SYS_IO_GENOA_MPIO_IMPL_H

/*
 * Definitions for the MPIO (MicroProcessor Input Output) Engine
 * configuration data format.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MPIO_PORT_NOT_PRESENT	0
#define	MPIO_PORT_PRESENT	1

#define	MPIO_LINK_ALL 0
#define	MPIO_LINK_SELECTED 1

typedef enum zen_mpio_link_speed {
	ZEN_MPIO_LINK_SPEED_MAX	= 0,
	ZEN_MPIO_LINK_SPEED_GEN1,
	ZEN_MPIO_LINK_SPEED_GEN2,
	ZEN_MPIO_LINK_SPEED_GEN3,
	ZEN_MPIO_LINK_SPEED_GEN4,
	ZEN_MPIO_LINK_SPEED_GEN5,
} zen_mpio_link_speed_t;

typedef enum zen_mpio_hotplug_type {
	ZEN_MPIO_HOTPLUG_T_DISABLED	= 0,
	ZEN_MPIO_HOTPLUG_T_BASIC,
	ZEN_MPIO_HOTPLUG_T_EXPRESS_MODULE,
	ZEN_MPIO_HOTPLUG_T_ENHANCED,
	ZEN_MPIO_HOTPLUG_T_INBOARD,
	ZEN_MPIO_HOTPLUG_T_ENT_SSD,
	ZEN_MPIO_HOTPLUG_T_UBM,
	ZEN_MPIO_HOTPLUG_T_OCP,
} zen_mpio_hotplug_type_t;

/*
 * There are two different versions that we need to track. That over the overall
 * structure, which is at version 0 and then that of individual payloads, which
 * is version 1.
 */
#define	DXIO_ANCILLARY_VERSION		0
#define	DXIO_ANCILLARY_PAYLOAD_VERSION	1

typedef enum zen_mpio_anc_type {
	ZEN_MPIO_ANCILLARY_T_XGBE = 1,
	ZEN_MPIO_ANCILLARY_T_HIER = 2,
	ZEN_MPIO_ANCILLARY_T_OVERRIDE = 3,
	ZEN_MPIO_ANCILLARY_T_PSPP = 4,
	ZEN_MPIO_ANCILLARY_T_PHY_CONFIG = 5,
	ZEN_MPIO_ANCILLARY_T_PHY_VALUE = 6,
	ZEN_MPIO_ANCILLARY_T_PCIE_STRAP = 7,
} zen_mpio_anc_type_t;

/*
 * Structures defined here are expected to be packed by firmware.
 */
#pragma	pack(1)
typedef struct zen_mpio_global_config {
	/* uint32_t mpio_global_cfg_args[0]: General settings */
	uint32_t	zmgc_skip_vet:1;
	uint32_t	zmgc_ntb_hp_ival:1;
	uint32_t	zmgc_save_restore_mode:2;
	uint32_t	zmgc_exact_match_port_size:1;
	uint32_t	zmgc_skip_disable_link_on_fail:1;
	uint32_t	zmgc_use_phy_sram:1;
	uint32_t	zmgc_valid_phy_firmware:1;
	uint32_t	zmgc_enable_loopback_support:1;
	uint32_t	zmgc_stb_verbosity:2;
	uint32_t	zmgc_en_pcie_noncomp_wa:1;
	uint32_t	zmgc_active_slt_mode:1;
	uint32_t	zmgc_legacy_dev_boot_fail_wa:1;
	uint32_t	zmgc_deferred_msg_supt:1;
	uint32_t	zmgc_cxl_gpf_phase2_timeout:4;
	uint32_t	zmgc_run_xgmi_safe_recov_odt:1;
	uint32_t	zmgc_run_z_cal:1;
	uint32_t	zmgc_pad0:11;
	/* uint32_t mpio_global_cfg_args[1]: Power settings */
	uint32_t	zmgc_pwr_mgmt_clk_gating:1;
	uint32_t	zmgc_pwr_mgmt_static_pwr_gating:1;
	uint32_t	zmgc_pwr_mgmt_refclk_shutdown:1;
	uint32_t	zmgc_cbs_opts_en_pwr_mgmt:1;
	uint32_t	zmgc_pwr_mgmt_pma_pwr_gating:1;
	uint32_t	zmgc_pwr_mgmt_pma_clk_gating:1;
	uint32_t	zmgc_pad1:26;
	/* uint32_t mpio_global_cfg_args[2]: Link timeouts */
	uint16_t	zmgc_link_rcvr_det_poll_timeout_ms;
	uint16_t	zmgc_link_l0_poll_timeout_ms;
	/* uint32_t mpio_global_cfg_args[3]: Protocol settings */
	uint16_t	zmgc_link_reset_to_training_time_ms;
	uint16_t	zmgc_pcie_allow_completion_pass:1;
	uint16_t	zmgc_cbs_opts_allow_ptr_slip_ival:1;
	uint16_t	zmgc_link_dis_at_pwr_off_delay:4;
	uint16_t	zmgc_en_2spc_gen4:1;
	uint16_t	zmgc_pad2:9;
	/* uint32_t mpio_global_cfg_args[4]: Trap control */
	uint32_t	zmgc_dis_sbr_trap:1;
	uint32_t	zmgc_dis_lane_margining_trap:1;
	uint32_t	zmgc_pad3:30;
	/* uint32_t mpio_global_cfg_args[5]: Reserved */
	uint32_t	zmgc_resv;
} zen_mpio_global_config_t;

typedef struct zen_mpio_link_attr {
	/* uint32_t zmla[0]: BDF */
	uint32_t	zmla_dev_func;

	/* uint32_t zmla[1]: General */
	uint32_t	zmla_port_present:1;
	uint32_t	zmla_early_link_train:1;
	uint32_t	zmla_link_compl_mode:1;
	uint32_t	zmla_pad0:1;
	uint32_t	zmla_link_hp_type:4;

	/* Speed parameters */
	uint32_t	zmla_max_link_speed_cap:4;
	uint32_t	zmla_target_link_speed:4;

	/* PSP parameters */
	uint32_t	zmla_psp_mode:3;
	uint32_t	zmla_partner_dev_type:2;
	uint32_t	zmla_pad1:3;

	/* Control parameters */
	uint32_t	zmla_local_perst:1;
	uint32_t	zmla_bif_mode:1;
	uint32_t	zmla_is_master_pll:1;
	uint32_t	zmla_invert_rx_pol:1;
	uint32_t	zmla_invert_tx_pol:1;
	uint32_t	zmla_pad2:3;

	/* uint32_t zmla[2]: Gen3 and Gen4 search parameters */
	uint32_t	zmla_gen3_eq_search_mode:2;
	uint32_t	zmla_en_gen3_eq_search_mode:2;
	uint32_t	zmla_gen4_eq_search_mode:2;
	uint32_t	zmla_en_gen4_eq_search_mode:2;

	/* Gen5 and Gen6 search parameters */
	uint32_t	zmla_gen5_eq_search_mode:2;
	uint32_t	zmla_en_gen5_eq_search_mode:2;
	uint32_t	zmla_gen6_eq_search_mode:2;
	uint32_t	zmla_en_gen6_eq_search_mode:2;

	/* Tx/Rx parameters */
	uint32_t	zmla_demph_tx:2;
	uint32_t	zmla_en_demph_tx:1;
	uint32_t	zmla_tx_vetting:1;
	uint32_t	zmla_rx_vetting:1;
	uint32_t	zmla_pad3:3;

	/* ESM parameters */
	uint32_t	zmla_esm_speed:6;
	uint32_t	zmla_esm_mode:2;

	/* uint32_t zmla[3]: Bridge parameters */
	uint8_t		zmla_hfc_idx;
	uint8_t		zmla_dfc_idx;
	uint16_t	zmla_log_bridge_id:5;
	uint16_t	zmla_swing_mode:3;
	uint16_t	zmla_sris_skip_ival:3;
	uint16_t	zmla_pad4:5;

	/* uint32_t zmla[4]: Reserved */
	uint32_t	zmla_resv0;

	/* uint32_t zmla[5]: Reserved */
	uint32_t	zmla_resv1;
} zen_mpio_link_attr_t;

typedef struct zen_mpio_link {
	uint32_t	zml_lane_start:16;
	uint32_t	zml_num_lanes:6;
	uint32_t	zml_reversed:1;
	uint32_t	zml_status:5;
	uint32_t	zml_ctlr_type:4;
	uint32_t	zml_gpio_id:8;
	uint32_t	zml_chan_type:8;
	uint32_t	zml_anc_data_idx:16;

	zen_mpio_link_attr_t zml_attrs;
} zen_mpio_link_t;

typedef enum zen_mpio_link_state {
	ZEN_MPIO_LINK_STATE_FREE = 0,
	ZEN_MPIO_LINK_STATE_ALLOCATED,
	ZEN_MPIO_LINK_STATE_PROVISIONED,
	ZEN_MPIO_LINK_STATE_BIFURCATION_FAILED,
	ZEN_MPIO_LINK_STATE_RESET,
	ZEN_MPIO_LINK_STATE_UNTRAINED,
	ZEN_MPIO_LINK_STATE_TRAINED,
	ZEN_MPIO_LINK_STATE_FAILURE,
	ZEN_MPIO_LINK_STATE_TRAINING_FAILURE,
	ZEN_MPIO_LINK_STATE_TIMEOUT
} zen_mpio_link_state_t;

typedef struct zen_mpio_ict_link_status {
	uint32_t	zmils_state:4;
	uint32_t	zmils_speed:7;
	uint32_t	zmils_width:5;
	uint32_t	zmils_port:8;
	uint32_t	zmils_resv:8;
} zen_mpio_ict_link_status_t;

typedef struct zen_mpio_ask_port {
	zen_mpio_link_t	zma_link;
	zen_mpio_ict_link_status_t zma_status;
	uint32_t	zma_resv[4];
} zen_mpio_ask_port_t;

#define	ZEN_MPIO_ASK_MAX_PORTS	24
typedef struct zen_mpio_ask {
	zen_mpio_ask_port_t	zma_ports[ZEN_MPIO_ASK_MAX_PORTS];
} zen_mpio_ask_t;

typedef struct zen_mpio_port_conf {
	zen_mpio_ask_port_t	zmpc_ask;
} zen_mpio_port_conf_t;

typedef struct zen_mpio_ext_attrs {
	uint8_t		zmad_type;
	uint8_t		zmad_vers:4;
	uint8_t		zmad_rsvd0:4;
	uint8_t		zmad_nu32s;
	uint8_t		zmad_rsvd1;
} zen_mpio_ext_attrs_t;

typedef struct zen_mpio_xfer_ask_args {
	uint32_t	zmxaa_paddr_hi;
	uint32_t	zmxaa_paddr_lo;
	uint32_t	zmxaa_links:1;
	uint32_t	zmxaa_dir:1;
	uint32_t	zmxaa_resv0:30;
	uint32_t	zmxaa_link_start;
	uint32_t	zmxaa_link_count;
	uint32_t	zmxaa_resv1;
} zen_mpio_xfer_ask_args_t;

typedef struct zen_mpio_xfer_ask_resp {
	uint32_t	zmxar_res;
	uint32_t	zmxar_nbytes;
	uint32_t	zmxar_resv[4];
} zen_mpio_xfer_ask_resp_t;

typedef struct zen_mpio_xfer_ext_attrs_args {
	uint32_t	zmxeaa_paddr_hi;
	uint32_t	zmxeaa_paddr_lo;
	uint32_t	zmxeaa_nwords;
	uint32_t	zmxeaa_resv[3];
} zen_mpio_xfer_ext_attrs_args_t;

typedef struct zen_mpio_xfer_ext_attrs_resp {
	uint32_t	zxear_res;
	uint32_t	zxear_nbytes;
	uint32_t	zxear_resv[4];
} zen_mpio_xfer_ext_attrs_resp_t;

typedef struct zen_mpio_status {
	uint32_t	zms_cmd_stat;
	uint32_t	zms_cycle_stat;
	uint32_t	zms_fw_post_code;
	uint32_t	zms_fw_status;
	uint32_t	zms_resv[2];
} zen_mpio_status_t;

typedef struct zen_mpio_link_setup_args {
	uint32_t	zmlsa_map:1;
	uint32_t	zmlsa_configure:1;
	uint32_t	zmlsa_reconfigure:1;
	uint32_t	zmlsa_perst_req:1;
	uint32_t	zmlsa_training:1;
	uint32_t	zmlsa_enumerate:1;
	uint32_t	zmlsa_resv0:26;
	uint32_t	zmlsa_resv1[5];
} zen_mpio_link_setup_args_t;

typedef struct zen_mpio_link_setup_resp {
	uint32_t	zmlsr_result;
	uint32_t	zmlsr_map:1;
	uint32_t	zmlsr_configure:1;
	uint32_t	zmlsr_reconfigure:1;
	uint32_t	zmlsr_perst_req:1;
	uint32_t	zmlsr_training:1;
	uint32_t	zmlsr_enumerate:1;
	uint32_t	zmlsr_resv0:26;
	uint32_t	zmlsr_resv1[4];
} zen_mpio_link_setup_resp_t;

typedef struct zen_mpio_link_cap {
	uint32_t	zmlc_present:1;
	uint32_t	zmlc_early_train:1;
	uint32_t	zmlc_comp_mode:1;
	uint32_t	zmlc_reverse:1;
	uint32_t	zmlc_max_speed:3;
	uint32_t	zmlc_ep_status:1;
	uint32_t	zmlc_hotplug:3;
	uint32_t	zmlc_port_size:5;
	uint32_t	zmlc_max_trained_speed:3;
	uint32_t	zmlc_en_off_config:1;
	uint32_t	zmlc_turn_off_unused:1;
	uint32_t	zmlc_ntb_hotplug:1;
	uint32_t	zmlc_pspp_speed:2;
	uint32_t	zmlc_pspp_mode:3;
	uint32_t	zmlc_peer_type:2;
	uint32_t	zmlc_auto_change_ctrl:2;
	uint32_t	zmlc_primary_pll:1;
	uint32_t	zmlc_eq_search_mode:2;
	uint32_t	zmlc_eq_mode_override:1;
	uint32_t	zmlc_invert_rx_pol:1;
	uint32_t	zmlc_tx_vet:1;
	uint32_t	zmlc_rx_vet:1;
	uint32_t	zmlc_tx_deemph:2;
	uint32_t	zmlc_tx_deemph_override:1;
	uint32_t	zmlc_invert_tx_pol:1;
	uint32_t	zmlc_targ_speed:3;
	uint32_t	zmlc_skip_eq_gen3:1;
	uint32_t	zmlc_skip_eq_gen4:1;
	uint32_t	zmlc_rsvd:17;
} zen_mpio_link_cap_t;

/*
 * Note, this type is used for configuration descriptors involving SATA, USB,
 * GOP, GMI, and DP.
 */
typedef struct zen_mpio_config_base {
	uint8_t		zmcb_chan_type;
	uint8_t		zmcb_chan_descid;
	uint16_t	zmcb_anc_off;
	uint32_t	zmcb_bdf_num;
	zen_mpio_link_cap_t zmcb_caps;
	uint8_t		zmcb_mac_id;
	uint8_t		zmcb_mac_port_id;
	uint8_t		zmcb_start_lane;
	uint8_t		zmcb_end_lane;
	uint8_t		zmcb_pcs_id;
	uint8_t		zmcb_rsvd0[3];
} zen_mpio_config_base_t;

typedef struct zen_mpio_config_net {
	uint8_t		zmcn_chan_type;
	uint8_t		zmcn_rsvd0;
	uint16_t	zmcn_anc_off;
	uint32_t	zmcn_bdf_num;
	zen_mpio_link_cap_t zmcn_caps;
	uint8_t		zmcb_rsvd1[8];
} zen_mpio_config_net_t;

typedef struct zen_mpio_config_pcie {
	uint8_t		zmcp_chan_type;
	uint8_t		zmcp_chan_descid;
	uint16_t	zmcp_anc_off;
	uint32_t	zmcp_bdf_num;
	zen_mpio_link_cap_t zmcp_caps;
	uint8_t		zmcp_mac_id;
	uint8_t		zmcp_mac_port_id;
	uint8_t		zmcp_start_lane;
	uint8_t		zmcp_end_lane;
	uint8_t		zmcp_pcs_id;
	uint8_t		zmcp_link_train_state;
	uint8_t		zmcp_rsvd0[2];
} zen_mpio_config_pcie_t;

typedef union {
	zen_mpio_config_base_t	zmc_base;
	zen_mpio_config_net_t	zmc_net;
	zen_mpio_config_pcie_t	zmc_pcie;
} zen_mpio_config_t;

typedef enum zen_mpio_ask_link_type {
	ZEN_MPIO_ASK_LINK_PCIE	= 0x00,
	ZEN_MPIO_ASK_LINK_SATA	= 0x01,
	ZEN_MPIO_ASK_LINK_XGMI	= 0x02,
	ZEN_MPIO_ASK_LINK_GMI	= 0x03,
	ZEN_MPIO_ASK_LINK_ETH	= 0x04,
	ZEN_MPIO_ASK_LINK_USB	= 0x05,
} zen_mpio_ask_link_type_t;

/*
 * This macro should be a value like 0xff because this reset group is defined to
 * be an opaque token that is passed back to us. However, if we actually want to
 * do something with reset and get a chance to do something before the MPIO
 * engine begins training, that value will not work and experimentally the value
 * 0x1 (which is what Ethanol and others use, likely every other board too),
 * then it does. For the time being, use this for our internal things which
 * should go through GPIO expanders so we have a chance of being a fool of a
 * Took.
 */
#define	MPIO_GROUP_UNUSED	0x01
#define	MPIO_PLATFORM_EPYC	0x00

#pragma pack()	/* pragma pack(1) */

/*
 * These next structures are meant to assume standard x86 ILP32 alignment. These
 * structures are definitely Genoa and firmware revision specific. Hence we have
 * different packing requirements from the MPIO bits above.
 */
#pragma	pack(4)

/*
 * Power and Performance Table. XXX This varies depending on the
 * firmware version.  Be careful to ensure that the definition
 * here matches the version of firmware one uses.
 */
typedef struct genoa_pptable {
	/*
	 * Default limits in the system.
	 */
	uint32_t	ppt_tdp;		/* Milliwatts */
	uint32_t	ppt_ppt;		/* Milliwatts */
	uint32_t	ppt_tdc;		/* Amps */
	uint32_t	ppt_edc;		/* Amps */
	uint32_t	ppt_tjmax;		/* Deg C */
	/*
	 * Platform specific limits.
	 */
	uint32_t	ppt_plat_tdp_lim;	/* Milliwatts */
	uint32_t	ppt_plat_ppt_lim;	/* Milliwatts */
	uint32_t	ppt_plat_tdc_lim;	/* Amps */
	uint32_t	ppt_plat_edc_lim;	/* Amps */
	/*
	 * Table of values for driving fans.  Can probably be left zeroed.
	 */
	uint8_t		ppt_fan_override;	/* 1: use these, 0: defaults */
	uint8_t		ppt_fan_hyst;		/* Deg C */
	uint8_t		ppt_fan_temp_low;	/* Deg C */
	uint8_t		ppt_fan_temp_med;	/* Deg C */
	uint8_t		ppt_fan_temp_high;	/* Deg C */
	uint8_t		ppt_fan_temp_crit;	/* Deg C */
	uint8_t		ppt_fan_pwm_low;	/* 0 - 100 */
	uint8_t		ppt_fan_pwm_med;	/* 0 - 100 */
	uint8_t		ppt_fan_pwm_high;	/* 0 - 100 */
	uint8_t		ppt_fan_pwm_freq;	/* 0 = 25kHz, 1 = 100 Hz */
	uint8_t		ppt_fan_polarity;	/* 0 = neg, 1 = pos */
	uint8_t		ppt_fan_spare;

	/*
	 * Misc. debug options
	 */
	int32_t		ppt_core_dldo_margin;	/* PSM count */
	int32_t		ppt_vddcr_cpu_margin;	/* Millivolts */
	int32_t		ppt_vddcr_soc_margin;	/* Millivolts */
	int32_t		ppt_vddio_volt_margin;	/* Millivolts */
	uint8_t		ppt_cc1_dis;		/* CC1; 0=en, 1=dis */
	uint8_t		ppt_detism_en;		/* perf determinism; 1=en */
	uint8_t		ppt_ccx_vdci_mode;	/* 0=predictive, 1=async */
	uint8_t		ppt_apbdis;		/* 1=APBDIS, 0=mission mode */
	uint8_t		ppt_effiency_policy;	/* 0=high, 1=eff, 2=max io */
	uint8_t		ppt_pcie_speed_ctl;	/* 0=enable, 1=gen4, 2=gen5 */
	uint8_t		ppt_mdo_spare[2];

	/*
	 * DF Cstate configuration
	 */
	uint8_t		ppt_df_override;
	uint8_t		ppt_df_clk_pwrdn_en;
	uint8_t		ppt_df_self_refresh_en;
	uint8_t		ppt_df_gmi_pwrdn_en;
	uint8_t		ppt_df_gop_pwrdn_en;
	uint8_t		ppt_df_spare[3];

	/*
	 * xGMI configuration
	 */
	uint8_t		ppt_xgmi_max_width_en;
	uint8_t		ppt_xgmi_max_width;
	uint8_t		ppt_xgmi_force_width_en;
	uint8_t		ppt_xgmi_force_width;

	/*
	 * GFXCLK/GFXOFF configuration
	 */
	uint8_t		ppt_gfx_clk_src;		/* 0=PLL, 1=DFLL */
	uint8_t		ppt_gfx_msmu_sec_restore_dis;	/* 1=use PSP not MSMU */
	uint8_t		ppt_gfx_apcc_en;		/* 1=en */
	uint8_t		ppt_gfx_spare0;
	uint16_t	ppt_gfx_init_min_volt;		/* Millivolts */
	uint16_t	ppt_gfx_clk_init_freq;		/* MHz */
	uint16_t	ppt_gfx_clkoff_entry_freq;	/* MHz */
	uint16_t	ppt_gfx_clk_idle_freq;		/* MHz */
	uint16_t	ppt_gfx_clk_slew_rate;
	uint16_t	ppt_gfx_clk_fmin_override;	/* MHz */
	uint16_t	ppt_gfx_min_volt;		/* Millivolts */
	uint16_t	ppt_gfx_max_volt;		/* Millivolts */
	uint16_t	ppt_gfx_clk_force_freq;		/* MHz */
	uint16_t	ppt_gfx_clk_max_override_freq;	/* MHz */
	uint8_t		ppt_gfx_dldo_bypass;
	uint8_t		ppt_gfx_spare1[3];

	/*
	 * Telemetry and Calibration
	 */
	uint8_t		ppt_i3c_sda_hold_tm[4];
	uint8_t		ppt_telem_current_guard_band;	/* 0.125% */
	uint8_t		ppt_svi3_svc_speed;
	uint8_t		ppt_telem_spare0[2];
	uint16_t	ppt_pcc_limit;			/* Amps */
	uint8_t		ppt_i3c_scl_pp_high_cnt;
	uint8_t		ppt_i3c_sdr_speed;
	uint32_t	ppt_telem_spare1;

	/*
	 * Overclocking.
	 */
	uint8_t		ppt_oc_dis;
	uint8_t		ppt_oc_min_vid;
	uint16_t	ppt_oc_max_freq;

	/*
	 * Clock frequency forcing
	 */
	uint16_t	ppt_force_cclk_freq;	/* MHz */
	uint16_t	ppt_fmax_override;	/* MHz */
	uint8_t		ppt_apbdis_dfps;
	uint8_t		ppt_dfps_freqo_dis;
	uint8_t		ppt_cclk_spare;
	uint8_t		ppt_cclk_dfs_bypass_off;

	/*
	 * HTF Overrides
	 */
	uint16_t	ppt_htf_temp_max;	/* Deg C */
	uint16_t	ppt_htf_freq_max;	/* MHz */
	uint16_t	ppt_mtf_temp_max;	/* Deg C */
	uint16_t	ppt_mtf_freq_max;	/* MHz */

	/*
	 * CPPC Defaults
	 */
	uint8_t		ppt_cppc_override;
	uint8_t		ppt_cppc_epp;		/* 0-100 */
	uint8_t		ppt_cppc_perf_max;	/* 0-100 */
	uint8_t		ppt_cppc_perf_min;	/* 0-100 */

	uint16_t	ppt_cppc_thr_apicid_size;
	uint8_t		ppt_cppc_spare[2];
	uint16_t	ppt_cppc_thr_map[512];

	/*
	 * Other Values
	 */
	uint16_t	ppt_vddcr_cpu_volt_force;	/* mV */
	uint16_t	ppt_vddcr_soc_volt_force;	/* mV */
	uint16_t	ppt_vddio_volt_force;		/* mV */
	uint8_t		ppt_other_spare[2];

	uint32_t	ppt_min_s0_i3_sleep_tm;		/* mS */
	uint32_t	ppt_wlan_bdf;
	uint8_t		ppt_df_pstate_range_en;
	uint8_t		ppt_df_pstate_range_min;
	uint8_t		ppt_df_pstate_range_max;
	uint8_t		ppt_df_pstate_spare;

	uint32_t	ppt_reserved[28];
} genoa_pptable_t;

typedef enum smu_hotplug_type {
	SMU_HP_PRESENCE_DETECT	= 0,
	SMU_HP_EXPRESS_MODULE_A,
	SMU_HP_ENTERPRISE_SSD,
	SMU_HP_EXPRESS_MODULE_B,
	/*
	 * This value must not be sent to the SMU. It's an internal value to us.
	 * The other values are actually meaningful.
	 */
	SMU_HP_INVALID = INT32_MAX
} smu_hotplug_type_t;

typedef enum smu_pci_tileid {
	SMU_TILE_G0 = 0,
	SMU_TILE_P1,
	SMU_TILE_G3,
	SMU_TILE_P2,
	SMU_TILE_P0,
	SMU_TILE_G1,
	SMU_TILE_P3,
	SMU_TILE_G2
} smu_pci_tileid_t;

typedef enum smu_exp_type {
	SMU_I2C_PCA9539 = 0,
	SMU_I2C_PCA9535 = 1,
	SMU_I2C_PCA9506 = 2
} smu_exp_type_t;

typedef enum smu_gpio_sw_type {
	SMU_GPIO_SW_9545 = 0,
	SMU_GPIO_SW_9546_48 = 1,
} smu_gpio_sw_type_t;

/*
 * XXX it may be nicer for us to define our own semantic set of bits here that
 * don't change based on verison and then we change it.
 */
typedef enum smu_enta_bits {
	SMU_ENTA_PRSNT		= 1 << 0,
	SMU_ENTA_PWRFLT		= 1 << 1,
	SMU_ENTA_ATTNSW		= 1 << 2,
	SMU_ENTA_EMILS		= 1 << 3,
	SMU_ENTA_PWREN		= 1 << 4,
	SMU_ENTA_ATTNLED	= 1 << 5,
	SMU_ENTA_PWRLED		= 1 << 6,
	SMU_ENTA_EMIL		= 1 << 7
} smu_enta_bits_t;

typedef enum smu_entb_bits {
	SMU_ENTB_ATTNLED	= 1 << 0,
	SMU_ENTB_PWRLED		= 1 << 1,
	SMU_ENTB_PWREN		= 1 << 2,
	SMU_ENTB_ATTNSW		= 1 << 3,
	SMU_ENTB_PRSNT		= 1 << 4,
	SMU_ENTB_PWRFLT		= 1 << 5,
	SMU_ENTB_EMILS		= 1 << 6,
	SMU_ENTB_EMIL		= 1 << 7
} smu_entb_bits_t;

#define	SMU_I2C_DIRECT	0x7

/*
 * PCIe Hotplug mapping
 */
typedef struct smu_hotplug_map {
	uint32_t	shm_format:3;
	uint32_t	shm_rst_valid:1;
	uint32_t	shm_active:1;
	uint32_t	shm_apu:1;
	uint32_t	shm_die_id:1;
	uint32_t	shm_port_id:4;
	uint32_t	shm_tile_id:4;
	uint32_t	shm_bridge:5;
	uint32_t	shm_rsvd0:4;
	uint32_t	shm_alt_slot_no:6;
	uint32_t	shm_sec:1;
	uint32_t	shm_rsvsd1:1;
} smu_hotplug_map_t;

typedef struct smu_hotplug_function {
	uint32_t	shf_i2c_bit:3;
	uint32_t	shf_i2c_byte:3;
	uint32_t	shf_i2c_daddr:5;
	uint32_t	shf_i2c_dtype:2;
	uint32_t	shf_i2c_bus:5;
	uint32_t	shf_mask:8;
	uint32_t	shf_i2c_bus2:6;
} smu_hotplug_function_t;

typedef struct smu_hotplug_reset {
	uint32_t	shr_rsvd0:3;
	uint32_t	shr_i2c_gpio_byte:3;
	uint32_t	shr_i2c_daddr:5;
	uint32_t	shr_i2c_dtype:2;
	uint32_t	shr_i2c_bus:5;
	uint32_t	shr_i2c_reset:8;
	uint32_t	shr_rsvd1:6;
} smu_hotplug_reset_t;

typedef struct smu_hotplug_engine_data {
	uint8_t		shed_start_lane;
	uint8_t		shed_end_lane;
	uint8_t		shed_socket;
	uint8_t		shed_slot;
} smu_hotplot_engine_data_t;

#define	GENOA_HOTPLUG_MAX_PORTS	160

typedef struct smu_hotplug_table {
	smu_hotplug_map_t	smt_map[GENOA_HOTPLUG_MAX_PORTS];
	smu_hotplug_function_t	smt_func[GENOA_HOTPLUG_MAX_PORTS];
	smu_hotplug_reset_t	smt_reset[GENOA_HOTPLUG_MAX_PORTS];
} smu_hotplug_table_t;

typedef struct smu_hotplug_entry {
	uint_t			se_slotno;
	smu_hotplug_map_t	se_map;
	smu_hotplug_function_t	se_func;
	smu_hotplug_reset_t	se_reset;
} smu_hotplug_entry_t;

#define	SMU_HOTPLUG_ENT_LAST	UINT_MAX

#pragma	pack()	/* pragma pack(4) */

extern const zen_mpio_port_conf_t ruby_mpio_pcie_s0[];
extern const size_t RUBY_MPIO_PCIE_S0_LEN;
extern const smu_hotplug_entry_t ruby_hotplug_ents[];

extern const uint32_t ruby_pcie_slot_cap_entssd;
extern const uint32_t ruby_pcie_slot_cap_express;

extern const zen_mpio_ask_t cosmo_mpio_pcie_s0;
extern const smu_hotplug_entry_t cosmo_hotplug_ents[];

/*
 * MPIO message codes.  These are specific to firmware revision 3.
 */
#define	GENOA_MPIO_OP_GET_VERSION	0x00
#define	GENOA_MPIO_OP_GET_STATUS	0x01
#define	GENOA_MPIO_OP_SET_GLOBAL_CONFIG	0x02
#define	GENOA_MPIO_OP_GET_ASK_RESULT	0x03
#define	GENOA_MPIO_OP_SETUP_LINK	0x04
#define	GENOA_MPIO_OP_EN_CLK_GATING	0x05
#define	GENOA_MPIO_OP_RECOVER_ASK	0x06
#define	GENOA_MPIO_OP_XFER_ASK		0x07
#define	GENOA_MPIO_OP_XFER_EXT_ATTRS	0x08
#define	GENOA_MPIO_OP_PCIE_SET_SPEED	0x09
#define	GENOA_MPIO_OP_PCIE_INIT_ESM	0x0a
#define	GENOA_MPIO_OP_PCIE_RST_CTLR	0x0b
#define	GENOA_MPIO_OP_PCIE_WRITE_STRAP	0x0c
#define	GENOA_MPIO_OP_CXL_INIT		0x0d
#define	GENOA_MPIO_OP_GET_DELI_INFO	0x0e
#define	GENOA_MPIO_OP_ENUMERATE_I2C	0x10
#define	GENOA_MPIO_OP_GET_I2C_DEV	0x11
#define	GENOA_MPIO_OP_GET_I2C_DEV_CHG	0x12
#define	GENOA_MPIO_OP_SET_HP_CFG_TBL	0x13
#define	GENOA_MPIO_OP_LEGACY_HP_EN	0x14
#define	GENOA_MPIO_OP_LEGACY_HP_DIS	0x15
#define	GENOA_MPIO_OP_SET_HP_I2C_SW_ADDR 0x16

#define	GENOA_MPIO_OP_POSTED		(3 << 8)

#define	MPIO_XFER_TO_RAM	0
#define	MPIO_XFER_FROM_RAM	1

/*
 * MPIO RPC reply codes.
 *
 * While most of these codes are undocumented, most RPCs return
 * GENOA_MPIO_RPC_OK to indicate success.
 */

/*
 * MPIO RPC Response codes
 */
#define	GENOA_MPIO_RPC_NOTDONE	0x00
#define	GENOA_MPIO_RPC_OK	0x01
#define	GENOA_MPIO_RPC_EBUSY	0xfc
#define	GENOA_MPIO_RPC_EPREREQ	0xfd
#define	GENOA_MPIO_RPC_EUNKNOWN	0xfe
#define	GENOA_MPIO_RPC_ERROR	0xff

/*
 * Different data heaps that can be loaded.
 */
#define	GENOA_MPIO_HEAP_EMPTY		0x00
#define	GENOA_MPIO_HEAP_FABRIC_INIT	0x01
#define	GENOA_MPIO_HEAP_MACPCS		0x02
#define	GENOA_MPIO_HEAP_ENGINE_CONFIG	0x03
#define	GENOA_MPIO_HEAP_CAPABILITIES	0x04
#define	GENOA_MPIO_HEAP_GPIO		0x05
#define	GENOA_MPIO_HEAP_ANCILLARY	0x06

/*
 * Some commands refer to an explicit engine in their request.
 */
#define	ZEN_MPIO_ENGINE_NONE		0x00
#define	ZEN_MPIO_ENGINE_PCIE		0x01
#define	ZEN_MPIO_ENGINE_USB		0x02
#define	ZEN_MPIO_ENGINE_SATA		0x03

/*
 * Types of MPIO Link speed updates. These must be ORed in with the base code.
 */
#define	GENOA_MPIO_LINK_SPEED_SINGLE	0x800

typedef struct genoa_mpio_config {
	zen_mpio_port_conf_t	*gmc_port_conf;
	zen_mpio_ask_t		*gmc_ask;
	zen_mpio_ext_attrs_t	*gmc_ext_attrs;
	uint64_t		gmc_ask_pa;
	uint64_t		gmc_ext_attrs_pa;
	uint32_t		gmc_nports;
	uint32_t		gmc_ask_alloc_len;
	uint32_t		gmc_ext_attrs_alloc_len;
	uint32_t		gmc_ext_attrs_len;
} genoa_mpio_config_t;

typedef struct genoa_hotplug {
	smu_hotplug_table_t	*gh_table;
	uint64_t		gh_pa;
	uint32_t		gh_alloc_len;
} genoa_hotplug_t;

AMDZEN_MAKE_SMN_REG_FN(genoa_mpio_smn_reg, MPIO_RPC,
    0x0c910000U, 0xfffff000U, 1, 0);

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_DOORBELL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x554,		\
}
#define	GENOA_MPIO_RPC_DOORBELL()	\
    genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_DOORBELL, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_RESP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9c8,		\
}
#define	GENOA_MPIO_RPC_RESP()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_RESP, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9cc,		\
}
#define	GENOA_MPIO_RPC_ARG0()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_ARG0, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9d0,		\
}
#define	GENOA_MPIO_RPC_ARG1()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_ARG1, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9d4,		\
}
#define	GENOA_MPIO_RPC_ARG2()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_ARG2, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9d8,		\
}
#define	GENOA_MPIO_RPC_ARG3()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_ARG3, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9dc,		\
}
#define	GENOA_MPIO_RPC_ARG4()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_ARG4, 0)

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9e0,		\
}
#define	GENOA_MPIO_RPC_ARG5()	genoa_mpio_smn_reg(0, D_GENOA_MPIO_RPC_ARG5, 0)


#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_MPIO_IMPL_H */
