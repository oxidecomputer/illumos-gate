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

#ifndef _MILAN_DXIO_DATA_H
#define	_MILAN_DXIO_DATA_H

/*
 * Definitions for getting to the DXIO Engine configuration data format.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	DXIO_PORT_NOT_PRESENT	0
#define	DXIO_PORT_PRESENT	1

typedef enum zen_dxio_link_speed {
	DXIO_LINK_SPEED_MAX	= 0,
	DXIO_LINK_SPEDD_GEN1,
	DXIO_LINK_SPEED_GEN2,
	DXIO_LINK_SPEED_GEN3,
	DXIO_LINK_SPEED_GEN4
} zen_dxio_link_speed_t;

typedef enum zen_dxio_hotplug_type {
	DXIO_HOTPLUG_T_DISABLED	= 0,
	DXIO_HOTPLUG_T_BASIC,
	DXIO_HOTPLUG_T_EXPRESS_MODULE,
	DXIO_HOTPLUG_T_ENHANCED,
	DXIO_HOTPLUG_T_INBOARD,
	DXIO_HOTPLUG_T_ENT_SSD
} zen_dxio_hotplug_type_t;

/*
 * There are two different versions that we need to track. That over the overall
 * structure, which is at version 0 and then that of individual payloads, which
 * is version 1.
 */
#define	DXIO_ANCILLARY_VERSION		0
#define	DXIO_ANCILLARY_PAYLOAD_VERSION	1

typedef enum zen_dxio_anc_type {
	ZEN_DXIO_ANCILLARY_T_XGBE = 1,
	ZEN_DXIO_ANCILLARY_T_OVERRIDE = 3,
	ZEN_DXIO_ANCILLARY_T_PSPP = 4,
	ZEN_DXIO_ANCILLARY_T_PHY = 5
} zen_dxio_anc_type_t;

/*
 * Structures defined here are expected to be packed here by firmware.
 */
#pragma	pack(1)
typedef struct zen_dxio_anc_data {
	uint8_t		zdad_type;
	uint8_t		zdad_vers:4;
	uint8_t		zdad_rsvd0:4;
	uint16_t	zdad_nu32s;
	uint8_t		zdad_rsvd1;
} zen_dxio_anc_data_t;

typedef struct zen_dxio_link_cap {
	uint32_t	zdlc_present:1;
	uint32_t	zdlc_early_train:1;
	uint32_t	zdlc_comp_mode:1;
	uint32_t	zdlc_reverse:1;
	uint32_t	zdlc_max_speed:3;
	uint32_t	zdlc_ep_status:1;
	uint32_t	zdlc_hp:3;
	uint32_t	zdlc_size:5;
	uint32_t	zdlc_trained_speed:3;
	uint32_t	zdlc_en_off_config:1;
	uint32_t	zdlc_off_unused:1;
	uint32_t	zdlc_ntb_hp:1;
	uint32_t	zdlc_pspp_speed:2;
	uint32_t	zdlc_pspp_mode:3;
	uint32_t	zdlc_peer_type:2;
	uint32_t	zdlc_auto_change_ctrl:2;
	uint32_t	zdlc_primary_pll:1;
	uint32_t	zdlc_eq_mode:2;
	uint32_t	zdlc_eq_override:1;
	uint32_t	zdlc_invert_rx_pol:1;
	uint32_t	zdlc_tx_vet:1;
	uint32_t	zdlc_rx_vet:1;
	uint32_t	zdlc_tx_deemph:2;
	uint32_t	zdlc_tx_deemph_override:1;
	uint32_t	zdlc_invert_tx_pol:1;
	uint32_t	zdlc_targ_speed:3;
	uint32_t	zdlc_skip_eq_gen3:1;
	uint32_t	zdlc_skip_eq_gen4:1;
	uint32_t	zdlc_rsvd:17;
} zen_dxio_link_cap_t;

/*
 * Note, this type is used for configuration descriptors involving SATA, USB,
 * GOP, GMI, and DP.
 */
typedef struct zen_dxio_config_base {
	uint8_t		zdcb_chan_type;
	uint8_t		zdcb_chan_descid;
	uint16_t	zdcb_anc_off;
	uint32_t	zdcb_bdf_num;
	zen_dxio_link_cap_t zdcb_caps;
	uint8_t		zdcb_mac_id;
	uint8_t		zdcb_mac_port_id;
	uint8_t		zdcb_start_lane;
	uint8_t		zdcb_end_lane;
	uint8_t		zdcb_pcs_id;
	uint8_t		zdcb_rsvd0[3];
} zen_dxio_config_base_t;

typedef struct zen_dxio_config_net {
	uint8_t		zdcn_chan_type;
	uint8_t		zdcn_rsvd0;
	uint16_t	zdcn_anc_off;
	uint32_t	zdcn_bdf_num;
	zen_dxio_link_cap_t zdcn_caps;
	uint8_t		zdcb_rsvd1[8];
} zen_dxio_config_net_t;

typedef struct zen_dxio_config_pcie {
	uint8_t		zdcp_chan_type;
	uint8_t		zdcp_chan_descid;
	uint16_t	zdcp_anc_off;
	uint32_t	zdcp_bdf_num;
	zen_dxio_link_cap_t zdcp_caps;
	uint8_t		zdcp_mac_id;
	uint8_t		zdcp_mac_port_id;
	uint8_t		zdcp_start_lane;
	uint8_t		zdcp_end_lane;
	uint8_t		zdcp_pcs_id;
	uint8_t		zdcp_link_train;
	uint8_t		zdcp_rsvd0[2];
} zen_dxio_config_pcie_t;

typedef union {
	zen_dxio_config_base_t	zdc_base;
	zen_dxio_config_net_t	zdc_net;
	zen_dxio_config_pcie_t	zdc_pcie;
} zen_dxio_config_t;

typedef enum zen_dxio_engine_type {
	DXIO_ENGINE_UNUSED	= 0x00,
	DXIO_ENGINE_PCIE	= 0x01,
	DXIO_ENGINE_SATA	= 0x03,
	DXIO_ENGINE_ETH		= 0x10
} zen_dxio_engine_type_t;

typedef struct zen_dxio_engine {
	uint8_t		zde_type;
	uint8_t		zde_hp:1;
	uint8_t		zde_rsvd0:7;
	uint8_t		zde_start_lane;
	uint8_t		zde_end_lane;
	uint8_t		zde_gpio_group;
	uint8_t		zde_reset_group;
	uint16_t	zde_search_depth:1;
	uint16_t	zde_kpnp_reset:1;
	uint16_t	zde_rsvd1:14;
	zen_dxio_config_t	zde_config;
	uint16_t	zde_mac_ptr;
	uint8_t		zde_first_lgd;
	uint8_t		zde_last_lgd;
	uint32_t	zde_train_state:4;
	uint32_t	zde_rsvd2:28;
} zen_dxio_engine_t;

/*
 * This macro should be a value like 0xff because this reset group is defined to
 * be an opaque token that is passed back to us. However, if we actually want to
 * do something with reset and get a chance to do something before the DXIO
 * engine begins training, that value will not work and experimentally the value
 * 0x1 (which is what Ethanol and others use, likely every other board too),
 * then it does. For the time being, use this for our internal things which
 * should go through GPIO expanders so we have a chance of being a fool of a
 * Took.
 */
#define	DXIO_GROUP_UNUSED	0x01
#define	DXIO_PLATFORM_EPYC	0x00

typedef struct zen_dxio_platform {
	uint16_t		zdp_type;
	uint8_t			zdp_rsvd0[10];
	uint16_t		zdp_nengines;
	uint8_t			zdp_rsvd1[2];
	zen_dxio_engine_t	zdp_engines[];
} zen_dxio_platform_t;
#pragma pack()	/* pragma pack(1) */

/*
 * These next structures are meant to assume standard x86 ILP32 alignment. These
 * structures are definitely Milan and firmware revision specific. Hence we have
 * different packing requirements from the dxio bits above.
 */
#pragma	pack(4)

/*
 * Power and Performance Table. XXX This seems to vary a bit depending on the
 * firmware version. We will need to be careful and figure out what version of
 * firmware we have to ensure that we have the right table.
 */
typedef struct milan_pptable {
	/*
	 * Default limits in the system.
	 */
	uint32_t	ppt_tdp;
	uint32_t	ppt_ppt;
	uint32_t	ppt_tdc;
	uint32_t	ppt_edc;
	uint32_t	ppt_tjmax;
	/*
	 * Platform specific limits.
	 */
	uint32_t	ppt_plat_tdp_lim;
	uint32_t	ppt_plat_ppt_lim;
	uint32_t	ppt_plat_tdc_lim;
	uint32_t	ppt_plat_edc_lim;
	/*
	 * Table of values that are meant to drive fans and can probably be left
	 * all at zero.
	 */
	uint8_t		ppt_fan_override;
	uint8_t		ppt_fan_hyst;
	uint8_t		ppt_fan_temp_low;
	uint8_t		ppt_fan_temp_med;
	uint8_t		ppt_fan_temp_high;
	uint8_t		ppt_fan_temp_crit;
	uint8_t		ppt_fan_pwm_low;
	uint8_t		ppt_fan_pwm_med;
	uint8_t		ppt_fan_pwm_high;
	uint8_t		ppt_fan_pwm_freq;
	uint8_t		ppt_fan_polarity;
	uint8_t		ppt_fan_spare;

	/*
	 * Misc. debug options.
	 */
	int32_t		ppt_core_dldo_margin;
	int32_t		ppt_vddcr_cpu_margin;
	int32_t		ppt_vddcr_soc_margin;
	uint8_t		ppt_cc1_dis;
	uint8_t		ppt_detpct_en;
	uint8_t		ppt_detpct;
	uint8_t		ppt_ccx_dci_mode;
	uint8_t		ppt_apb_dis;
	uint8_t		ppt_eff_mode_en;
	uint8_t		ppt_pwr_mgmt_override;
	uint8_t		ppt_pwr_mgmt;
	uint8_t		ppt_esm[4];

	/*
	 * DF Cstate configuration.
	 */
	uint8_t		ppt_df_override;
	uint8_t		ppt_df_clk_pwrdn;
	uint8_t		ppt_df_refresh_en;
	uint8_t		ppt_df_gmi_pwrdn;
	uint8_t		ppt_df_gop_pwrdn;
	uint8_t		ppt_df_spare[2];

	uint8_t		ppt_ccr_en;

	/*
	 * xGMI Configuration
	 */
	uint8_t		ppt_xgmi_max_width_en;
	uint8_t		ppt_xgmi_max_width;
	uint8_t		ppt_xgmi_min_width_en;
	uint8_t		ppt_xgmi_min_width;
	uint8_t		ppt_xgmi_force_width_en;
	uint8_t		ppt_xgmi_force_width;
	uint8_t		ppt_spare[2];

	/*
	 * Telemetry and Calibration
	 */
	uint32_t	ppt_cpu_full_scale;
	int32_t		ppt_cpu_offset;
	uint32_t	ppt_soc_full_scale;
	int32_t		ppt_soc_offset;

	/*
	 * Overclocking.
	 */
	uint8_t		ppt_oc_dis;
	uint8_t		ppt_oc_min_vid;
	uint16_t	ppt_oc_max_freq;

	/*
	 * Clock frequency forcing
	 */
	uint16_t	ppt_cclk_freq;
	uint16_t	ppt_fmax_override;
	uint8_t		ppt_apbdis_dfps;
	uint8_t		ppt_dfps_freqo_dis;
	uint8_t		ppt_dfps_lato_dis;
	uint8_t		ppt_cclk_spare[1];

	/*
	 * HTF Overrides
	 */
	uint16_t	ppt_htf_temp_max;
	uint16_t	ppt_htf_freq_max;
	uint16_t	ppt_mtf_temp_max;
	uint16_t	ppt_mtf_freq_max;

	/*
	 * Various CPPC settings.
	 */
	uint8_t		ppt_ccp_override;
	uint8_t		ppt_ccp_epp;
	uint8_t		ppt_ccp_perf_max;
	uint8_t		ppt_ccp_perf_min;
	uint16_t	ppt_ccp_thr_apic_size;
	uint8_t		ppt_ccp_spare[2];
	uint16_t	ppt_ccp_thr_map[256];

	/*
	 * Other Values
	 */
	uint16_t	ppt_vddcr_cpu_force;
	uint16_t	ppt_vddcr_soc_force;
	uint16_t	ppt_cstate_boost_override;
	uint8_t		ppt_max_did_override;
	uint8_t		ppt_cca_en;
	uint8_t		ppt_more_spare[2];
	uint32_t	ppt_l3credit_ceil;

	uint32_t	ppt_reserved[28];
} milan_pptable_t;

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

/*
 * XXX it may be nicer for us to define our own semantic set of bits here that
 * dont' change based on verison and then we change it.
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

typedef struct smu_hotplug_map {
	uint32_t	shm_format:3;
	uint32_t	shm_rsvd0:2;
	uint32_t	shm_rst_valid:1;
	uint32_t	shm_active:1;
	uint32_t	shm_apu:1;
	uint32_t	shm_die_id:1;
	uint32_t	shm_port_id:3;
	uint32_t	shm_tile_id:3;
	uint32_t	shm_bridge:5;
	uint32_t	shm_rsvd1:4;
	uint32_t	shm_alt_slot_no:6;
	uint32_t	shm_sec:1;
	uint32_t	shm_rsvsd2:1;
} smu_hotplug_map_t;

typedef struct smu_hotplug_function {
	uint32_t	shf_i2c_bit:3;
	uint32_t	shf_i2c_byte:3;
	uint32_t	shf_i2c_daddr:5;
	uint32_t	shf_i2c_dtype:2;
	uint32_t	shf_i2c_bus:5;
	uint32_t	shf_mask:8;
	uint32_t	shf_rsvd0:6;
} smu_hotplug_function_t;

typedef struct smu_hotpug_reset {
	uint32_t	shr_rsvd0:3;
	uint32_t	shr_i2c_gpio_byte:3;
	uint32_t	shr_i2c_daddr:5;
	uint32_t	shr_i2c_dtype:2;
	uint32_t	shr_i2c_bus:5;
	uint32_t	shr_i2c_reset:8;
	uint32_t	shr_rsvd1:6;
} smu_hotplug_reset_t;

#define	MILAN_HOTPLUG_MAX_PORTS	96

typedef struct smu_hotplug_table {
	smu_hotplug_map_t	smt_map[MILAN_HOTPLUG_MAX_PORTS];
	smu_hotplug_function_t	smt_func[MILAN_HOTPLUG_MAX_PORTS];
	smu_hotplug_reset_t	smt_reset[MILAN_HOTPLUG_MAX_PORTS];
} smu_hotplug_table_t;

typedef struct smu_hotplug_entry {
	uint_t			se_slotno;
	smu_hotplug_map_t	se_map;
	smu_hotplug_function_t	se_func;
	smu_hotplug_reset_t	se_reset;
} smu_hotplug_entry_t;

#define	SMU_HOTPLUG_ENT_LAST	UINT_MAX

#pragma	pack()	/* pragma pack(4) */

extern const zen_dxio_platform_t ethanolx_engine_s0;
extern const zen_dxio_platform_t ethanolx_engine_s1;
extern const smu_hotplug_entry_t ethanolx_hotplug_ents[];

extern const uint32_t ethanolx_pcie_slot_cap_entssd;
extern const uint32_t ethanolx_pcie_slot_cap_express;

extern const zen_dxio_platform_t gimlet_engine;
extern const smu_hotplug_entry_t gimlet_hotplug_ents[];

#ifdef __cplusplus
}
#endif

#endif /* _MILAN_DXIO_DATA_H */
