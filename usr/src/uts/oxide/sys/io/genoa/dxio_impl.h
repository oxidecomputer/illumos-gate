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

#ifndef _SYS_IO_GENOA_DXIO_IMPL_H
#define	_SYS_IO_GENOA_DXIO_IMPL_H

/*
 * Definitions for the MPIO Engine configuration data format.
 */

#include <sys/param.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MPIO_PORT_NOT_PRESENT	0
#define	MPIO_PORT_PRESENT	1

typedef enum zen_mpio_link_speed {
	ZEN_MPIO_LINK_SPEED_MAX	= 0,
	ZEN_MPIO_LINK_SPEDD_GEN1,
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
typedef struct zen_mpio_anc_data {
	uint8_t		zmad_type;
	uint8_t		zmad_vers:4;
	uint8_t		zmad_rsvd0:4;
	uint8_t		zmad_nu32s;
	uint8_t		zmad_rsvd1;
} zen_mpio_anc_data_t;

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

typedef enum zen_mpio_engine_type {
	ZEN_MPIO_ENGINE_UNUSED	= 0x00,
	ZEN_MPIO_ENGINE_PCIE	= 0x01,
	ZEN_MPIO_ENGINE_SATA	= 0x03,
	ZEN_MPIO_ENGINE_ETH	= 0x10,
} zen_mpio_engine_type_t;

typedef struct zen_mpio_engine {
	uint8_t		zme_type;
	uint8_t		zme_hotpluggable:1;
	uint8_t		zme_rsvd0:7;
	uint8_t		zme_start_lane;
	uint8_t		zme_end_lane;
	uint8_t		zme_gpio_group;
	uint8_t		zme_reset_group;
	uint16_t	zme_search_depth:1;
	uint16_t	zme_force_kpnp_reset:1;
	uint16_t	zme_rsvd1:14;
	zen_mpio_config_t	zme_config;
	uint16_t	zme_mac_ptr;
	uint8_t		zme_first_lgd;
	uint8_t		zme_last_lgd;
	uint32_t	zme_train_state:4;
	uint32_t	zde_rsvd2:28;
} zen_mpio_engine_t;

typedef struct zen_mpio_engine_data {
	uint8_t		zmed_type;
	uint8_t		zmed_hotpluggable:1;
	uint8_t		zmed_rsvd0:7;
	uint8_t		zmed_start_lane;
	uint8_t		zmed_end_lane;
	uint8_t		zmed_gpio_group;
	uint8_t		zmed_mpio_start_lane;
	uint8_t		zmed_mpio_end_lane;
	uint8_t		zmed_search_depth;
} zen_mpio_engine_data_t;

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
#define	MPIO_GROUP_UNUSED	0x01
#define	MPIO_PLATFORM_EPYC	0x00

typedef struct zen_mpio_platform {
	uint16_t		zmp_type;
	uint8_t			zmp_rsvd0[10];
	uint16_t		zmp_nengines;
	uint8_t			zmp_rsvd1[2];
	zen_mpio_engine_t	zmp_engines[];
} zen_mpio_platform_t;
#pragma pack()	/* pragma pack(1) */

/*
 * These next structures are meant to assume standard x86 ILP32 alignment. These
 * structures are definitely Genoa and firmware revision specific. Hence we have
 * different packing requirements from the dxio bits above.
 */
#pragma	pack(4)

/*
 * Power and Performance Table. XXX This seems to vary a bit depending on the
 * firmware version. We will need to be careful and figure out what version of
 * firmware we have to ensure that we have the right table.
 */
typedef struct genoa_pptable {
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

#define	GENOA_HOTPLUG_MAX_PORTS	96

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

extern const zen_mpio_platform_t ruby_engine_s0;
extern const smu_hotplug_entry_t ruby_hotplug_ents[];

extern const uint32_t ruby_pcie_slot_cap_entssd;
extern const uint32_t ruby_pcie_slot_cap_express;

extern const zen_mpio_platform_t cosmo_engine;
extern const smu_hotplug_entry_t cosmo_hotplug_ents[];

/*
 * DXIO message codes. These are also specific to firmware revision.
 */
#define	GENOA_DXIO_OP_INIT		0x00
#define	GENOA_DXIO_OP_GET_SM_STATE	0x09
#define	GENOA_DXIO_OP_SET_LINK_SPEED	0x10
#define	GENOA_DXIO_OP_GET_VERSION	0x13
#define	GENOA_DXIO_OP_GET_ENGINE_CFG	0x14
#define	GENOA_DXIO_OP_SET_VARIABLE	0x22
#define	GENOA_DXIO_OP_LOAD_DATA		0x23
#define	GENOA_DXIO_OP_LOAD_CAPS		0x24
#define	GENOA_DXIO_OP_RELOAD_SM		0x2d
#define	GENOA_DXIO_OP_GET_ERROR_LOG	0x2b
#define	GENOA_DXIO_OP_SET_RUNTIME_PROP	0x3a
#define	GENOA_DXIO_OP_XGMI_BER_ADAPT	0x40
#define	GENOA_DXIO_OP_INIT_ESM		0x53

/*
 * The 0x300 in these are used to indicate deferred returns.
 */
#define	GENOA_DXIO_OP_START_SM		0x307
#define	GENOA_DXIO_OP_RESUME_SM		0x308

/*
 * MPIO RPC reply codes.
 *
 * While most of these codes are undocumented, most RPCs return
 * GENOA_DXIO_RPC_OK to indicate success.  But note that we have seen
 * GENOA_DXIO_OP_SET_VARIABLE return GENOA_DXIO_RPC_MBOX_IDLE in this
 * case as it seems to actually be using the mailboxes under the hood.
 */
#define	GENOA_DXIO_RPC_NULL		0
#define	GENOA_DXIO_RPC_TIMEOUT		1
#define	GENOA_DXIO_RPC_ERROR		2
#define	GENOA_DXIO_RPC_OK		3
#define	GENOA_DXIO_RPC_UNKNOWN_LOCK	4
#define	GENOA_DXIO_RPC_EAGAIN		5
#define	GENOA_DXIO_RPC_MBOX_IDLE	6
#define	GENOA_DXIO_RPC_MBOX_BUSY	7
#define	GENOA_DXIO_RPC_MBOX_DONE	8

/*
 * Different data heaps that can be loaded.
 */
#define	GENOA_DXIO_HEAP_EMPTY		0x00
#define	GENOA_DXIO_HEAP_FABRIC_INIT	0x01
#define	GENOA_DXIO_HEAP_MACPCS		0x02
#define	GENOA_DXIO_HEAP_ENGINE_CONFIG	0x03
#define	GENOA_DXIO_HEAP_CAPABILITIES	0x04
#define	GENOA_DXIO_HEAP_GPIO		0x05
#define	GENOA_DXIO_HEAP_ANCILLARY	0x06

/*
 * Some commands refer to an explicit engine in their request.
 */
#define	GENOA_DXIO_ENGINE_NONE		0x00
#define	GENOA_DXIO_ENGINE_PCIE		0x01
#define	GENOA_DXIO_ENGINE_USB		0x02
#define	GENOA_DXIO_ENGINE_SATA		0x03

/*
 * The various variable codes that one can theoretically use with
 * GENOA_DXIO_OP_SET_VARIABLE.
 */
#define	GENOA_DXIO_VAR_SKIP_PSP			0x0d
#define	MLIAN_DXIO_VAR_RET_AFTER_MAP		0x0e
#define	GENOA_DXIO_VAR_RET_AFTER_CONF		0x0f
#define	GENOA_DXIO_VAR_ANCILLARY_V1		0x10
#define	GENOA_DXIO_VAR_NTB_HP_EN		0x11
#define	GENOA_DXIO_VAR_MAP_EXACT_MATCH		0x12
#define	GENOA_DXIO_VAR_S3_MODE			0x13
#define	GENOA_DXIO_VAR_PHY_PROG			0x14
#define	GENOA_DXIO_VAR_PCIE_COMPL		0x23
#define	GENOA_DXIO_VAR_SLIP_INTERVAL		0x24
#define	GENOA_DXIO_VAR_PCIE_POWER_OFF_DELAY	0x25

/*
 * The following are all values that can be used with
 * GENOA_DXIO_OP_SET_RUNTIME_PROP. It consists of various codes. Some of which
 * have their own codes.
 */
#define	GENOA_DXIO_RT_SET_CONF		0x00
#define	GENOA_DXIO_RT_SET_CONF_DXIO_WA		0x03
#define	GENOA_DXIO_RT_SET_CONF_SPC_WA		0x04
#define	GENOA_DXIO_RT_SET_CONF_FC_CRED_WA_DIS	0x05
#define	GENOA_DXIO_RT_SET_CONF_TX_CLOCK		0x07
#define	GENOA_DXIO_RT_SET_CONF_SRNS		0x08
#define	GENOA_DXIO_RT_SET_CONF_TX_FIFO_MODE	0x09
#define	GENOA_DXIO_RT_SET_CONF_DLF_WA_DIS	0x0a
#define	GENOA_DXIO_RT_SET_CONF_CE_SRAM_ECC	0x0b

#define	GENOA_DXIO_RT_CONF_PCIE_TRAIN	0x02
#define	GENOA_DXIO_RT_CONF_CLOCK_GATE	0x03
#define	GENOA_DXIO_RT_PLEASE_LEAVE	0x05
#define	GENOA_DXIO_RT_FORGET_BER	0x22

/*
 * DXIO Link training state machine states
 */
typedef enum genoa_dxio_sm_state {
	GENOA_DXIO_SM_INIT =		0x00,
	GENOA_DXIO_SM_DISABLED =	0x01,
	GENOA_DXIO_SM_SCANNED =		0x02,
	GENOA_DXIO_SM_CANNED =		0x03,
	GENOA_DXIO_SM_LOADED =		0x04,
	GENOA_DXIO_SM_CONFIGURED =	0x05,
	GENOA_DXIO_SM_IN_EARLY_TRAIN =	0x06,
	GENOA_DXIO_SM_EARLY_TRAINED =	0x07,
	GENOA_DXIO_SM_VETTING =		0x08,
	GENOA_DXIO_SM_GET_VET =		0x09,
	GENOA_DXIO_SM_NO_VET =		0x0a,
	GENOA_DXIO_SM_GPIO_INIT =	0x0b,
	GENOA_DXIO_SM_NHP_TRAIN =	0x0c,
	GENOA_DXIO_SM_DONE =		0x0d,
	GENOA_DXIO_SM_ERROR =		0x0e,
	GENOA_DXIO_SM_MAPPED =		0x0f
} genoa_dxio_sm_state_t;

/*
 * PCIe Link Training States
 */
typedef enum genoa_dxio_pcie_state {
	GENOA_DXIO_PCIE_ASSERT_RESET_GPIO	= 0x00,
	GENOA_DXIO_PCIE_ASSERT_RESET_DURATION	= 0x01,
	GENOA_DXIO_PCIE_DEASSERT_RESET_GPIO	= 0x02,
	GENOA_DXIO_PCIE_ASSERT_RESET_ENTRY	= 0x03,
	GENOA_DXIO_PCIE_GPIO_RESET_TIMEOUT	= 0x04,
	GENOA_DXIO_PCIE_RELEASE_LINK_TRAIN	= 0x05,
	GENOA_DXIO_PCIE_DETECT_PRESENCE		= 0x06,
	GENOA_DXIO_PCIE_DETECTING		= 0x07,
	GENOA_DXIO_PCIE_BAD_LANE		= 0x08,
	GENOA_DXIO_PCIE_GEN2_FAILURE		= 0x09,
	GENOA_DXIO_PCIE_REACHED_L0		= 0x0a,
	GENOA_DXIO_PCIE_VCO_NEGOTIATED		= 0x0b,
	GENOA_DXIO_PCIE_FORCE_RETRAIN		= 0x0c,
	GENOA_DXIO_PCIE_FAILED			= 0x0d,
	GENOA_DXIO_PCIE_SUCCESS			= 0x0e,
	GENOA_DXIO_PCIE_GRAPHICS_WORKAROUND	= 0x0f,
	GENOA_DXIO_PCIE_COMPLIANCE_MODE		= 0x10,
	GENOA_DXIO_PCIE_NO_DEVICE		= 0x11,
	GENOA_DXIO_PCIE_COMPLETED		= 0x12
} genoa_dxio_pcie_state_t;

/*
 * When using GENOA_DXIO_OP_GET_SM_STATE, the following structure is actually
 * filled in via the RPC argument. This structure is more generally used amongst
 * different RPCs; however, since the state machine can often get different
 * types of requests this ends up mattering a bit more.
 */
typedef enum genoa_dxio_data_type {
	GENOA_DXIO_DATA_TYPE_NONE	 = 0,
	GENOA_DXIO_DATA_TYPE_GENERIC,
	GENOA_DXIO_DATA_TYPE_SM,
	GENOA_DXIO_DATA_TYPE_HPSM,
	GENOA_DXIO_DATA_TYPE_RESET
} genoa_dxio_data_type_t;

typedef struct genoa_dxio_reply {
	genoa_dxio_data_type_t	gdr_type;
	uint8_t			gdr_nargs;
	uint32_t		gdr_arg0;
	uint32_t		gdr_arg1;
	uint32_t		gdr_arg2;
	uint32_t		gdr_arg3;
} genoa_dxio_reply_t;

/*
 * Types of DXIO Link speed updates. These must be ORed in with the base code.
 */
#define	GENOA_DXIO_LINK_SPEED_SINGLE	0x800

typedef struct genoa_mpio_config {
	zen_mpio_platform_t	*gmc_conf;
	zen_mpio_anc_data_t	*gmc_anc;
	uint64_t		gmc_pa;
	uint64_t		gmc_anc_pa;
	uint32_t		gmc_alloc_len;
	uint32_t		gmc_conf_len;
	uint32_t		gmc_anc_len;
} genoa_mpio_config_t;

typedef struct genoa_hotplug {
	smu_hotplug_table_t	*gh_table;
	uint64_t		gh_pa;
	uint32_t		gh_alloc_len;
} genoa_hotplug_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_DXIO_IMPL_H */
