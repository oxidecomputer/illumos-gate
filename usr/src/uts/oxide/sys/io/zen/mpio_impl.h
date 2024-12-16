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
 * Type, structure, and function definitions for interacting with MPIO, the
 * post-Milan AMD Zen "MicroProcessor for IO", which is the component that
 * handles things like driving the DXIO crossbar to train PCIe lanes, etc.
 */

#ifndef	_SYS_IO_ZEN_MPIO_IMPL_H
#define	_SYS_IO_ZEN_MPIO_IMPL_H

#include <sys/types.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/mpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MPIO RPC result codes.  These incorporate both the responses returned from
 * MPIO, as well as codes we have defined for e.g. RPC call failures.
 */
typedef enum zen_mpio_rpc_res {
	/*
	 * These are analogues of firmware errors.
	 */
	ZEN_MPIO_RPC_OK,
	ZEN_MPIO_RPC_EBUSY,
	ZEN_MPIO_RPC_EPREREQ,
	ZEN_MPIO_RPC_EUNKNOWN,
	ZEN_MPIO_RPC_ERROR,
	/*
	 * MPIO never became ready to receive an RPC.
	 */
	ZEN_MPIO_RPC_ENOTREADY,
	/*
	 * The RPC itself timed out.
	 */
	ZEN_MPIO_RPC_ETIMEOUT,
	/*
	 * Firmware on MPIO returned some other, possibly new, RPC error that we
	 * don't explicitly handle.
	 */
	ZEN_MPIO_RPC_EOTHER,
} zen_mpio_rpc_res_t;

/*
 * MPIO message codes.  These are specific to firmware revision 3.
 *
 * Note that AGESA defines a "message count" symbol that differs across
 * microarchitectures, and that we do not include here.  It is unclear what
 * exactly that message refers to: an interpretation is that it is an operation
 * that returns a total count of messages to sent (or received from?) MPIO or
 * something of that nature, but we believe it is just a count of the total
 * number of operation messages.  Making things less clear in this latter case
 * is that the ZEN_MPIO_OP_GET_XGMI_FREQ_ALL_LINKS message is numerically
 * greater than message count.
 */
#define	ZEN_MPIO_OP_POSTED			(3 << 8)

#define	ZEN_MPIO_OP_GET_VERSION			0x00
#define	ZEN_MPIO_OP_GET_STATUS			0x01
#define	ZEN_MPIO_OP_SET_GLOBAL_CONFIG		0x02
#define	ZEN_MPIO_OP_GET_ASK_RESULT		0x03
#define	ZEN_MPIO_OP_POSTED_SETUP_LINK		(0x04 | ZEN_MPIO_OP_POSTED)
#define	ZEN_MPIO_OP_EN_CLK_GATING		0x05
#define	ZEN_MPIO_OP_RECOVER_ASK			0x06
#define	ZEN_MPIO_OP_XFER_ASK			0x07
#define	ZEN_MPIO_OP_XFER_EXT_ATTRS		0x08
#define	ZEN_MPIO_OP_PCIE_SET_SPEED		0x09
#define	ZEN_MPIO_OP_PCIE_INIT_ESM		0x0a
#define	ZEN_MPIO_OP_PCIE_RST_CTLR		0x0b
#define	ZEN_MPIO_OP_PCIE_WRITE_STRAP		0x0c
#define	ZEN_MPIO_OP_CXL_INIT			0x0d
#define	ZEN_MPIO_OP_GET_DELI_INFO		0x0e
/* 0x0f unused on Turin and Genoa */
#define	ZEN_MPIO_OP_ENUMERATE_I2C		0x10
#define	ZEN_MPIO_OP_GET_I2C_DEV			0x11
#define	ZEN_MPIO_OP_GET_I2C_DEV_CHG		0x12
#define	ZEN_MPIO_OP_SEND_HP_CFG_TBL		0x13
#define	ZEN_MPIO_OP_HOTPLUG_EN			0x14
#define	ZEN_MPIO_OP_HOTPLUG_DIS			0x15
#define	ZEN_MPIO_OP_SET_HP_I2C_SW_ADDR		0x16
#define	ZEN_MPIO_OP_SET_HP_BLINK_IVAL		0x17
#define	ZEN_MPIO_OP_SET_HP_POLL_IVAL		0x18
#define	ZEN_MPIO_OP_SET_HP_FLAGS		0x19
#define	ZEN_MPIO_OP_SET_HP_GPIO_INT_CMD		0x1a
#define	ZEN_MPIO_OP_GET_HP_GPIO_INT_STATUS	0x1b
#define	ZEN_MPIO_OP_RDWR_HP_GPIO		0x1c
#define	ZEN_MPIO_OP_UNBLOCK_HP_PORT		0x1d
#define	ZEN_MPIO_OP_ADD_HP_CANCEL		0x1e
#define	ZEN_MPIO_OP_AUTH_CHIPSET		0x1f
#define	ZEN_MPIO_OP_TRAP_NVME_RAID		0x20
#define	ZEN_MPIO_OP_TRAP_NBIF_CFG0		0x21
#define	ZEN_MPIO_OP_POSTED_UPDATE_LINK		(0x22 | ZEN_MPIO_OP_POSTED)
#define	ZEN_MPIO_OP_RST_PCIE_GPIO		0x23
#define	ZEN_MPIO_OP_PORT_TRAINING		0x24	/* Turin only */
#define	ZEN_MPIO_OP_SET_EXT_PCIE_BUSES		0x25
#define	ZEN_MPIO_OP_RDWR_PCIE_PROXY		0x26
/* 0x27 unused on Turin and Genoa */
#define	ZEN_MPIO_OP_SET_PCIE_PSPP_SETTINGS	0x28
#define	ZEN_MPIO_OP_INIT_FRAME_BUF_TRAP		0x29
#define	ZEN_MPIO_OP_RELEASE_UBM_PERST		0x2a
#define	ZEN_MPIO_OP_SET_PCIE_LINK_SETTINGS	0x2b
#define	ZEN_MPIO_OP_INIT_CNLI			0x2c
#define	ZEN_MPIO_OP_DEASSERT_PERST		0x2d
#define	ZEN_MPIO_OP_CXL_ERR_FW_FIRST_EN		0x2e
/* 0x30 unused on Turin and Genoa */
#define	ZEN_MPIO_OP_GET_XGMI_FREQ_ALL_LINKS	0x31

/*
 * MPIO RPC response codes defined by firmware that may appear in the response
 * register.
 */
#define	ZEN_MPIO_RPC_FW_RESP_OK			0x01
#define	ZEN_MPIO_RPC_FW_RESP_REJ_BUSY		0xfc
#define	ZEN_MPIO_RPC_FW_RESP_REJ_PREREQ		0xfd
#define	ZEN_MPIO_RPC_FW_RESP_UNKNOWN_CMD	0xfe
#define	ZEN_MPIO_RPC_FW_RESP_FAILED		0xff

/*
 * On a successful ASK and extended attribute DMA transfers, the result field in
 * the respective response structures is set to one of these.
 */
#define	ZEN_MPIO_FW_ASK_XFER_RES_OK		1
#define	ZEN_MPIO_FW_EXT_ATTR_XFER_RES_OK	1

/*
 * The "ready" bit in the response register is set when MPIO is done processing
 * a command.
 */
#define	ZEN_MPIO_RPC_FW_RESP_READY		(1U << 31)

/*
 * The arguments, request, and response for an RPC sent to MPIO.  Note that the
 * response field holds the raw response from firmware and is kept for debugging
 * and error reporting, and not generally used by callers, which instead examine
 * a zen_mpio_rpc_res_t.
 */
typedef struct zen_mpio_rpc {
	uint32_t		zmr_req;
	uint32_t		zmr_resp;
	uint32_t		zmr_args[6];
} zen_mpio_rpc_t;

/*
 * Synchronously calls the given MPIO RPC.  Returns the RPC status.  Overwrites
 * rpc->zmr_args with data returned by the RPC on success; zmr_args is
 * unmodified if the RPC fails.
 */
extern zen_mpio_rpc_res_t zen_mpio_rpc(zen_iodie_t *iodie, zen_mpio_rpc_t *rpc);

#define	ZEN_MPIO_XFER_TO_RAM			0
#define	ZEN_MPIO_XFER_FROM_RAM			1

#define	ZEN_MPIO_LINK_ALL			0
#define	ZEN_MPIO_LINK_SELECTED			1

/*
 * These are the speed parameters understood by firmware on the
 * microarchitectures that we currently support.
 */
typedef enum zen_mpio_link_speed {
	ZEN_MPIO_LINK_SPEED_MAX			= 0,
	ZEN_MPIO_LINK_SPEED_GEN1,
	ZEN_MPIO_LINK_SPEED_GEN2,
	ZEN_MPIO_LINK_SPEED_GEN3,
	ZEN_MPIO_LINK_SPEED_GEN4,
	ZEN_MPIO_LINK_SPEED_GEN5,
} zen_mpio_link_speed_t;

typedef enum zen_mpio_hotplug_type {
	ZEN_MPIO_HOTPLUG_T_DISABLED		= 0,
	ZEN_MPIO_HOTPLUG_T_BASIC,
	ZEN_MPIO_HOTPLUG_T_EXPRESS_MODULE,
	ZEN_MPIO_HOTPLUG_T_ENHANCED,
	ZEN_MPIO_HOTPLUG_T_INBOARD,
	ZEN_MPIO_HOTPLUG_T_ENT_SSD,
	ZEN_MPIO_HOTPLUG_T_UBM,
	ZEN_MPIO_HOTPLUG_T_OCP,
} zen_mpio_hotplug_type_t;

/*
 * Structures defined here are expected to be packed by firmware.
 */
#pragma	pack(1)

/*
 * Wrapper structure for the global MPIO configuration, which is sent with a
 * ZEN_MPIO_OP_SET_GLOBAL_CONFIG RPC.  The actual values put into these words
 * are microarchitecture specific and set via an ops vector entry
 * (zfo_set_mpio_global_config) in a manner specific to a given
 * microarchitecture and its supported firmware versions; the member names here
 * do correspond to what is presently given by AMD, but the specific bit values
 * differ; we keep this structure mainly for asserting that sizes match in the
 * uarch code.
 */
typedef struct zen_mpio_global_config {
	/* uint32_t mpio_global_cfg_args[0]: General settings */
	uint32_t		zmgc_general;
	/* uint32_t mpio_global_cfg_args[1]: Power settings */
	uint32_t		zmgc_power;
	/* uint32_t mpio_global_cfg_args[2]: Link timeouts */
	uint32_t		zmgc_link_timeouts;
	/* uint32_t mpio_global_cfg_args[3]: Protocol settings */
	uint32_t		zmgc_protocol;
	/* uint32_t mpio_global_cfg_args[4]: Trap control */
	uint32_t		zmgc_trap_control;
	/* uint32_t mpio_global_cfg_args[5]: Misc (Reserved/Future or Gen5) */
	uint32_t		zmgc_misc;
} zen_mpio_global_config_t;

typedef struct zen_mpio_status {
	uint32_t		zms_cmd_stat;
	uint32_t		zms_cycle_stat;
	uint32_t		zms_fw_post_code;
	uint32_t		zms_fw_status;
	uint32_t		zms_resv[2];
} zen_mpio_status_t;

/*
 * Link attributes are part of the ASK, which is sent to and received from
 * MPIO as part of driving the link training state machine.
 */
typedef struct zen_mpio_link_attr {
	/* uint32_t zmla[0]: BDF */
	uint32_t		zmla_dev_func;

	/* uint32_t zmla[1]: General */
	uint8_t			zmla_port_present:1;
	uint8_t			zmla_early_link_train:1;
	uint8_t			zmla_link_compl_mode:1;
	uint8_t			zmla_pad0:1;
	uint8_t			zmla_link_hp_type:4;

	/* Speed parameters */
	uint8_t			zmla_max_link_speed_cap:4;
	uint8_t			zmla_target_link_speed:4;

	/* PSP parameters */
	uint8_t			zmla_psp_mode:3;
	uint8_t			zmla_partner_dev_type:2;
	uint8_t			zmla_pad1:3;

	/* Control parameters */
	uint8_t			zmla_local_perst:1;
	uint8_t			zmla_bif_mode:1;
	uint8_t			zmla_is_master_pll:1;
	uint8_t			zmla_invert_rx_pol:1;
	uint8_t			zmla_invert_tx_pol:1;
	uint8_t			zmla_pad2:3;

	/* uint32_t zmla[2]: Gen3 and Gen4 search parameters */
	uint8_t			zmla_gen3_eq_search_mode:2;
	uint8_t			zmla_en_gen3_eq_search_mode:2;
	uint8_t			zmla_gen4_eq_search_mode:2;
	uint8_t			zmla_en_gen4_eq_search_mode:2;

	/* Gen5 and Gen6 search parameters */
	uint8_t			zmla_gen5_eq_search_mode:2;
	uint8_t			zmla_en_gen5_eq_search_mode:2;
	uint8_t			zmla_gen6_eq_search_mode:2;
	uint8_t			zmla_en_gen6_eq_search_mode:2;

	/* Tx/Rx parameters */
	uint8_t			zmla_demph_tx:2;
	uint8_t			zmla_en_demph_tx:1;
	uint8_t			zmla_tx_vetting:1;
	uint8_t			zmla_rx_vetting:1;
	uint8_t			zmla_pad3:3;

	/* ESM parameters */
	uint8_t			zmla_esm_speed:6;
	uint8_t			zmla_esm_mode:2;

	/* uint32_t zmla[3]: Bridge parameters */
	uint8_t			zmla_hfc_idx;
	uint8_t			zmla_dfc_idx;
	uint8_t			zmla_log_bridge_id:5;
	uint8_t			zmla_swing_mode:3;
	uint8_t			zmla_sris_skip_ival:3;
	uint8_t			zmla_pad4:5;

	/* uint32_t zmla[4]: Reserved */
	uint32_t		zmla_resv0;

	/* uint32_t zmla[5]: Reserved */
	uint32_t		zmla_resv1;
} zen_mpio_link_attr_t;

CTASSERT(sizeof (zen_mpio_link_attr_t) == 24);
CTASSERT(offsetof(zen_mpio_link_attr_t, zmla_resv1) == 20);

/*
 * This describes the link in the ASK, its start and number of lanes, what type
 * (PCIe, SATA, etc) it is, and so on.  It is sent to MPIO as part of the ASK
 * and used for training.
 */
typedef struct zen_mpio_link {
	uint32_t		zml_lane_start:16;
	uint32_t		zml_num_lanes:6;
	uint32_t		zml_reversed:1;
	uint32_t		zml_status:5;
	uint32_t		zml_ctlr_type:4;
	uint32_t		zml_gpio_id:8;
	uint32_t		zml_chan_type:8;
	uint32_t		zml_anc_data_idx:16;

	zen_mpio_link_attr_t	zml_attrs;
} zen_mpio_link_t;

CTASSERT(sizeof (zen_mpio_link_t) == 32);

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

/*
 * The status is part of the ASK.  It is filled in by MPIO and returned to the
 * host.  In particular, the state field shows us the results of the training
 * procedure.
 */
typedef struct zen_mpio_ict_link_status {
	uint32_t		zmils_state:4;
	uint32_t		zmils_speed:7;
	uint32_t		zmils_width:5;
	uint32_t		zmils_port:8;
	uint32_t		zmils_resv:8;
} zen_mpio_ict_link_status_t;

CTASSERT(sizeof (zen_mpio_ict_link_status_t) == 4);

/*
 * An ASK port is the collection of data MPIO consumes and produces that
 * describes a single port that it is responsible for training.
 */
typedef struct zen_mpio_ask_port {
	zen_mpio_link_t			zma_link;
	zen_mpio_ict_link_status_t	zma_status;
	uint32_t			zma_resv[4];
} zen_mpio_ask_port_t;

CTASSERT(sizeof (zen_mpio_ask_port_t) == 52);

/*
 * The ASK itself is fairly straight-forward at this point: it is simply an
 * array of port structures describing the partitioning of the various lanes in
 * the system that MPIO will train.  This is the basic structure that is sent
 * to, and received from, MPIO via DMA.
 */
typedef struct zen_mpio_ask {
	zen_mpio_ask_port_t	zma_ports[ZEN_MPIO_ASK_MAX_PORTS];
} zen_mpio_ask_t;

typedef struct zen_mpio_ext_attrs {
	uint8_t			zmad_type;
	uint8_t			zmad_id;
	uint8_t			zmad_nu32s;
	uint8_t			zmad_rsvd1;
} zen_mpio_ext_attrs_t;

typedef struct zen_mpio_xfer_ask_args {
	uint32_t		zmxaa_paddr_hi;
	uint32_t		zmxaa_paddr_lo;
	uint32_t		zmxaa_links:1;
	uint32_t		zmxaa_dir:1;
	uint32_t		zmxaa_resv0:30;
	uint32_t		zmxaa_link_start;
	uint32_t		zmxaa_link_count;
	uint32_t		zmxaa_resv1;
} zen_mpio_xfer_ask_args_t;

typedef struct zen_mpio_xfer_ask_resp {
	uint32_t		zmxar_res;
	uint32_t		zmxar_nbytes;
	uint32_t		zmxar_resv[4];
} zen_mpio_xfer_ask_resp_t;

typedef struct zen_mpio_xfer_ext_attrs_args {
	uint32_t		zmxeaa_paddr_hi;
	uint32_t		zmxeaa_paddr_lo;
	uint32_t		zmxeaa_nwords;
	uint32_t		zmxeaa_resv[3];
} zen_mpio_xfer_ext_attrs_args_t;

typedef struct zen_mpio_xfer_ext_attrs_resp {
	uint32_t		zxear_res;
	uint32_t		zxear_nbytes;
	uint32_t		zxear_resv[4];
} zen_mpio_xfer_ext_attrs_resp_t;


/*
 * Instances of the link setup args type are sent to MPIO as part of driving the
 * link training state machine; conceptually, it is setting up a link.  The bit
 * map describes what should be done as part of the setting up:
 * configure/reconfigure, map, request PCIe reset (PERST), etc.
 */
typedef struct zen_mpio_link_setup_args {
	uint32_t		zmlsa_map:1;
	uint32_t		zmlsa_configure:1;
	uint32_t		zmlsa_reconfigure:1;
	uint32_t		zmlsa_perst_req:1;
	uint32_t		zmlsa_training:1;
	uint32_t		zmlsa_enumerate:1;
	uint32_t		zmlsa_resv0:26;
	uint32_t		zmlsa_early:1;
	uint32_t		zmlsa_resv1:31;
	uint32_t		zmlsa_resv2[4];
} zen_mpio_link_setup_args_t;

CTASSERT(sizeof (zen_mpio_link_setup_args_t) == 24);

/*
 * This is the response for each stage of link setup.
 */
typedef struct zen_mpio_link_setup_resp {
	uint32_t		zmlsr_result;
	uint32_t		zmlsr_map:1;
	uint32_t		zmlsr_configure:1;
	uint32_t		zmlsr_reconfigure:1;
	uint32_t		zmlsr_perst_req:1;
	uint32_t		zmlsr_training:1;
	uint32_t		zmlsr_enumerate:1;
	uint32_t		zmlsr_resv0:26;
	uint32_t		zmlsr_resv1[4];
} zen_mpio_link_setup_resp_t;

CTASSERT(sizeof (zen_mpio_link_setup_resp_t) == 24);

typedef enum zen_mpio_ask_link_type {
	ZEN_MPIO_ASK_LINK_PCIE	= 0x00,
	ZEN_MPIO_ASK_LINK_SATA	= 0x01,
	ZEN_MPIO_ASK_LINK_XGMI	= 0x02,
	ZEN_MPIO_ASK_LINK_GMI	= 0x03,
	ZEN_MPIO_ASK_LINK_ETH	= 0x04,
	ZEN_MPIO_ASK_LINK_USB	= 0x05,
} zen_mpio_ask_link_type_t;

/*
 * The rest of the types in this file are related to UBM (Universal Backplane
 * Management), a standard for flexible support between multiple electrically
 * compatible storage standards (for instance, an SATA or an NVMe device may be
 * physically connected to a compatible socket).  In UBM, there si a "host
 * facing connector" (HFC) and a "drive (or device) facing connector" (DFC), and
 * a structured interface for querying the device to determine what type it is,
 * and how to train it.
 *
 * Oxide does not support UBM on any of its products.  However, the development
 * systems that we use for bring-up do, and so we have a minimal implementation
 * of a subset of it for testing.
 */
typedef enum zen_mpio_i2c_node_type {
	ZEN_MPIO_I2C_NODE_TYPE_UBM		= 0,
	ZEN_MPIO_I2C_NODE_TYPE_OCP,
	ZEN_MPIO_I2C_NODE_TYPE_U2,
	ZEN_MPIO_I2C_NODE_TYPE_U3,
} zen_mpio_i2c_node_type_t;

typedef enum zen_mpio_ubm_dfc_type {
	ZEN_MPIO_UBM_DFC_TYPE_SATA_SAS	= 0x04,
	ZEN_MPIO_UBM_DFC_TYPE_QUAD_PCI	= 0x05,
	ZEN_MPIO_UBM_DFC_TYPE_EMPTY	= 0x07,
} zen_mpio_ubm_dfc_type_t;

typedef struct zen_mpio_i2c_switch {
	uint8_t			zmis_addr;
	uint8_t			zmis_select:4;
	uint8_t			zmis_type:4;
} zen_mpio_i2c_switch_t;

typedef struct zen_mpio_i2c_expander {
	uint8_t			zmie_addr;
	uint8_t			zmie_type:7;
	uint8_t			zmie_clear_intrs:1;
} zen_mpio_i2c_expander_t;

typedef struct zen_mpio_ubm_data {
	uint8_t			zmud_bp_type_bitno;
	uint8_t			zmud_i2c_reset_bitno;
	uint8_t			zmud_resv;
	uint8_t			zmud_slot_num;
} zen_mpio_ubm_data_t;

#define	ZEN_MPIO_I2C_SWITCH_DEPTH	2

typedef struct zen_mpio_ubm_hfc_port {
	uint8_t			zmuhp_node_type;
	zen_mpio_i2c_expander_t	zmuhp_expander;
	uint8_t			zmuhp_start_lane;
	zen_mpio_ubm_data_t	zmuhp_ubm_device;
	zen_mpio_i2c_switch_t	zmuhp_i2c_switch[ZEN_MPIO_I2C_SWITCH_DEPTH];
} zen_mpio_ubm_hfc_port_t;

typedef struct zen_mpio_anc_data {
	uint32_t		zmad_count;
	uint32_t		zmad_override;
} zen_mpio_anc_data_t;

typedef struct zen_mpio_ubm_dfc_data {
	uint8_t			zmudt_gen_speed;
	uint8_t			zmudt_type:3;
	uint8_t			zmudt_rsvd0:3;
	uint8_t			zmudt_bifurcate_port:1;
	uint8_t			zmudt_secondary_port:1;
	uint8_t			zmudt_ref_clk:1;
	uint8_t			zmudt_pwr_dis:1;
	uint8_t			zmudt_has_perst:1;
	uint8_t			zmudt_dual_port:1;
	uint8_t			zmudt_rsvd1:4;
	uint8_t			zmudt_slot;
	uint8_t			zmudt_pad[2];
} zen_mpio_ubm_dfc_data_t;

typedef struct zen_mpio_ubm_dfc_descr {
	uint8_t			zmudd_hfcno;
	uint8_t			zmudd_event;
	uint16_t		zmudd_ndfcs;
	uint8_t			zmudd_lane_start;
	uint8_t			zmudd_lane_width;
	zen_mpio_ubm_dfc_data_t	zmudd_data;
} zen_mpio_ubm_dfc_descr_t;

#pragma	pack()	/* pragma pack(1) */

/*
 * The base of the MPIO SMN register space.  This is common across Genoa and
 * Turin.  Note that Milan does not use MPIO.
 */
#define	ZEN_MPIO_SMN_REG_BASE	0x0c910000U

/*
 * Defines a function for accessing MPIO registers.
 */
AMDZEN_MAKE_SMN_REG_FN(zen_mpio_smn_reg, MPIO_RPC,
    ZEN_MPIO_SMN_REG_BASE, 0xfffff000U, 1, 0);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IO_ZEN_MPIO_IMPL_H */
