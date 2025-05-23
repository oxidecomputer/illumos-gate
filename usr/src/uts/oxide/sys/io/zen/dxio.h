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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef	_SYS_IO_ZEN_DXIO_H
#define	_SYS_IO_ZEN_DXIO_H

/*
 * Type, structure, and function definitions for interacting with DXIO via the
 * SMU for things like driving the DXIO crossbar to train PCIe lanes, etc.  Note
 * that these are SP3 specific.
 */

#include <sys/stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZEN_DXIO_PORT_NOT_PRESENT	0
#define	ZEN_DXIO_PORT_PRESENT		1

typedef enum zen_dxio_fw_link_speed {
	ZEN_DXIO_FW_LINK_SPEED_MAX	= 0,
	ZEN_DXIO_FW_LINK_SPEED_GEN1,
	ZEN_DXIO_FW_LINK_SPEED_GEN2,
	ZEN_DXIO_FW_LINK_SPEED_GEN3,
	ZEN_DXIO_FW_LINK_SPEED_GEN4
} zen_dxio_fw_link_speed_t;

typedef enum zen_dxio_fw_hotplug_type {
	ZEN_DXIO_FW_HOTPLUG_T_DISABLED	= 0,
	ZEN_DXIO_FW_HOTPLUG_T_BASIC,
	ZEN_DXIO_FW_HOTPLUG_T_EXPRESS_MODULE,
	ZEN_DXIO_FW_HOTPLUG_T_ENHANCED,
	ZEN_DXIO_FW_HOTPLUG_T_INBOARD,
	ZEN_DXIO_FW_HOTPLUG_T_ENT_SSD
} zen_dxio_fw_hotplug_type_t;

/*
 * There are two different versions that we need to track. That over the overall
 * structure, which is at version 0 and then that of individual payloads, which
 * is version 1.
 */
#define	ZEN_DXIO_FW_ANCILLARY_VERSION		0
#define	ZEN_DXIO_FW_ANCILLARY_PAYLOAD_VERSION	1

typedef enum zen_dxio_fw_anc_type {
	ZEN_DXIO_FW_ANCILLARY_T_XGBE		= 1,
	ZEN_DXIO_FW_ANCILLARY_T_OVERRIDE	= 3,
	ZEN_DXIO_FW_ANCILLARY_T_PSPP		= 4,
	ZEN_DXIO_FW_ANCILLARY_T_PHY		= 5
} zen_dxio_fw_anc_type_t;

/*
 * DXIO Link training state machine states
 */
typedef enum zen_dxio_sm_state {
	ZEN_DXIO_SM_INIT =		0x00,
	ZEN_DXIO_SM_DISABLED =		0x01,
	ZEN_DXIO_SM_SCANNED =		0x02,
	ZEN_DXIO_SM_CANNED =		0x03,
	ZEN_DXIO_SM_LOADED =		0x04,
	ZEN_DXIO_SM_CONFIGURED =	0x05,
	ZEN_DXIO_SM_IN_EARLY_TRAIN =	0x06,
	ZEN_DXIO_SM_EARLY_TRAINED =	0x07,
	ZEN_DXIO_SM_VETTING =		0x08,
	ZEN_DXIO_SM_GET_VET =		0x09,
	ZEN_DXIO_SM_NO_VET =		0x0a,
	ZEN_DXIO_SM_GPIO_INIT =		0x0b,
	ZEN_DXIO_SM_NHP_TRAIN =		0x0c,
	ZEN_DXIO_SM_DONE =		0x0d,
	ZEN_DXIO_SM_ERROR =		0x0e,
	ZEN_DXIO_SM_MAPPED =		0x0f
} zen_dxio_sm_state_t;

/*
 * Structures defined here are expected to be packed here by firmware.
 */
#pragma	pack(1)
typedef struct zen_dxio_fw_anc_data {
	uint8_t			zdad_type;
	uint8_t			zdad_vers:4;
	uint8_t			zdad_rsvd:4;
	uint16_t		zdad_nu32s;
} zen_dxio_fw_anc_data_t;

typedef struct zen_dxio_fw_link_cap {
	uint32_t		zdlc_present:1;
	uint32_t		zdlc_early_train:1;
	uint32_t		zdlc_comp_mode:1;
	uint32_t		zdlc_reverse:1;
	uint32_t		zdlc_max_speed:3;
	uint32_t		zdlc_ep_status:1;
	uint32_t		zdlc_hp:3;
	uint32_t		zdlc_size:5;
	uint32_t		zdlc_trained_speed:3;
	uint32_t		zdlc_en_off_config:1;
	uint32_t		zdlc_off_unused:1;
	uint32_t		zdlc_ntb_hp:1;
	uint32_t		zdlc_pspp_speed:2;
	uint32_t		zdlc_pspp_mode:3;
	uint32_t		zdlc_peer_type:2;
	uint32_t		zdlc_auto_change_ctrl:2;
	uint32_t		zdlc_primary_pll:1;
	uint32_t		zdlc_eq_mode:2;
	uint32_t		zdlc_eq_override:1;
	uint32_t		zdlc_invert_rx_pol:1;
	uint32_t		zdlc_tx_vet:1;
	uint32_t		zdlc_rx_vet:1;
	uint32_t		zdlc_tx_deemph:2;
	uint32_t		zdlc_tx_deemph_override:1;
	uint32_t		zdlc_invert_tx_pol:1;
	uint32_t		zdlc_targ_speed:3;
	uint32_t		zdlc_skip_eq_gen3:1;
	uint32_t		zdlc_skip_eq_gen4:1;
	uint32_t		zdlc_rsvd:17;
} zen_dxio_fw_link_cap_t;

/*
 * Note, this type is used for configuration descriptors involving SATA, USB,
 * GOP, GMI, and DP.
 */
typedef struct zen_dxio_fw_config_base {
	uint8_t			zdcb_chan_type;
	uint8_t			zdcb_chan_descid;
	uint16_t		zdcb_anc_off;
	uint32_t		zdcb_bdf_num;
	zen_dxio_fw_link_cap_t	zdcb_caps;
	uint8_t			zdcb_mac_id;
	uint8_t			zdcb_mac_port_id;
	uint8_t			zdcb_start_lane;
	uint8_t			zdcb_end_lane;
	uint8_t			zdcb_pcs_id;
	uint8_t			zdcb_rsvd0[3];
} zen_dxio_fw_config_base_t;

typedef struct zen_dxio_fw_config_net {
	uint8_t			zdcn_chan_type;
	uint8_t			zdcn_rsvd0;
	uint16_t		zdcn_anc_off;
	uint32_t		zdcn_bdf_num;
	zen_dxio_fw_link_cap_t	zdcn_caps;
	uint8_t			zdcb_rsvd1[8];
} zen_dxio_fw_config_net_t;

typedef struct zen_dxio_fw_config_pcie {
	uint8_t			zdcp_chan_type;
	uint8_t			zdcp_chan_descid;
	uint16_t		zdcp_anc_off;
	uint32_t		zdcp_bdf_num;
	zen_dxio_fw_link_cap_t	zdcp_caps;
	uint8_t			zdcp_mac_id;
	uint8_t			zdcp_mac_port_id;
	uint8_t			zdcp_start_lane;
	uint8_t			zdcp_end_lane;
	uint8_t			zdcp_pcs_id;
	uint8_t			zdcp_link_train;
	uint8_t			zdcp_rsvd0[2];
} zen_dxio_fw_config_pcie_t;

typedef union {
	zen_dxio_fw_config_base_t	zdc_base;
	zen_dxio_fw_config_net_t	zdc_net;
	zen_dxio_fw_config_pcie_t	zdc_pcie;
} zen_dxio_fw_config_t;

typedef enum zen_dxio_fw_engine_type {
	ZEN_DXIO_FW_ENGINE_UNUSED	= 0x00,
	ZEN_DXIO_FW_ENGINE_PCIE		= 0x01,
	ZEN_DXIO_FW_ENGINE_SATA		= 0x03,
	ZEN_DXIO_FW_ENGINE_ETH		= 0x10
} zen_dxio_fw_engine_type_t;

typedef struct zen_dxio_fw_engine {
	uint8_t			zde_type;
	uint8_t			zde_hp:1;
	uint8_t			zde_rsvd0:7;
	uint8_t			zde_start_lane;
	uint8_t			zde_end_lane;
	uint8_t			zde_gpio_group;
	uint8_t			zde_reset_group;
	uint16_t		zde_search_depth:1;
	uint16_t		zde_kpnp_reset:1;
	uint16_t		zde_rsvd1:14;
	zen_dxio_fw_config_t	zde_config;
	uint16_t		zde_mac_ptr;
	uint8_t			zde_first_lgd;
	uint8_t			zde_last_lgd;
	uint32_t		zde_train_state:4;
	uint32_t		zde_rsvd2:28;
} zen_dxio_fw_engine_t;

typedef struct zen_dxio_fw_platform {
	uint16_t		zdp_type;
	uint8_t			zdp_rsvd0[10];
	uint16_t		zdp_nengines;
	uint8_t			zdp_rsvd1[2];
	zen_dxio_fw_engine_t	zdp_engines[];
} zen_dxio_fw_platform_t;

typedef struct smu_hotplug_map {
	/*
	 * Indicates what kind of hotplug entity this is. One of the
	 * zen_hotplug_type_t values.
	 */
	uint32_t	shm_format:3;
	uint32_t	shm_rsvd0:2;
	/*
	 * If set to 1, indicates that the corresponding reset entry in the
	 * hotplug table should be looked at.
	 */
	uint32_t	shm_rst_valid:1;
	/*
	 * We believe this indicates whether or not this entry should be
	 * evaluated.
	 */
	uint32_t	shm_active:1;
	/*
	 * These next two are used to indicate which device to talk to. As far
	 * as we know, the die_id corresponds to the socket ID and apu should be
	 * left as 0 in SP3 systems we support.
	 */
	uint32_t	shm_apu:1;
	uint32_t	shm_die_id:1;
	/*
	 * The port ID indicates the PCIe port that was chosen by DXIO. This
	 * value is specific to the core.
	 */
	uint32_t	shm_port_id:3;
	/*
	 * This indicates which of the cores is in use. Valid values are
	 * microarchitecture specific.
	 */
	uint32_t	shm_tile_id:3;
	/*
	 * This indicates the logical bridge ID with the NBIO instance. That is,
	 * it is not specific to the PCIe core. Phrased differently, this
	 * corresponds to the bridge's index in the IOHC::IOHC_Bridge_CNTL
	 * register. Note, this is calculated from other parameters.
	 */
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
	/*
	 * Starting in Genoa with the v3 format, this is now used to represent a
	 * second I2C switch that can be in the topology.
	 */
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

#define	ZEN_SMU_HOTPLUG_MAX_PORTS	96

typedef struct smu_hotplug_table {
	smu_hotplug_map_t	smt_map[ZEN_SMU_HOTPLUG_MAX_PORTS];
	smu_hotplug_function_t	smt_func[ZEN_SMU_HOTPLUG_MAX_PORTS];
	smu_hotplug_reset_t	smt_reset[ZEN_SMU_HOTPLUG_MAX_PORTS];
} smu_hotplug_table_t;

#pragma pack()	/* pragma pack(1) */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_DXIO_H */
