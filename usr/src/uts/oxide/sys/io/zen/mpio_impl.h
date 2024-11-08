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
 * Type, structure, and function definitions for for interacting with MPIO,
 * the post-Milan AMD Zen "MicroProcessor for IO", which is the component that
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
 */
#define	ZEN_MPIO_OP_GET_VERSION		0x00

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

/*
 * Structures defined here are expected to be packed by firmware.
 */
#pragma	pack(1)

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
 * We size the maximum number of ports in the ask roughly based on the SP5
 * design and I/O die constraints as a rough swag. P0 and G3 can each support up
 * to 16 PCIe devices, while the remaining 6 groups cans upport up to 8-9
 * devices and P4/P5 can support up to 4 devices. That gives us 88 devices. We
 * currently require this to be a page size which can only fit up to 78 devices.
 */
#define	ZEN_MPIO_ASK_MAX_PORTS	78

/*
 * The ASK itself is fairly straight-forward at this point: it is simply an
 * array of port structures describing the partitioning of the various lanes in
 * the system that MPIO will train.  This is the basic structure that is sent
 * to, and received from, MPIO via DMA.
 */
typedef struct zen_mpio_ask {
	zen_mpio_ask_port_t	zma_ports[ZEN_MPIO_ASK_MAX_PORTS];
} zen_mpio_ask_t;

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
