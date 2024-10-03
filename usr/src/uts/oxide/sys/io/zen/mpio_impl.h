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
#define	ZEN_MPIO_OP_GET_VERSION			0x00
#define	ZEN_MPIO_OP_GET_STATUS			0x01
#define	ZEN_MPIO_OP_SET_GLOBAL_CONFIG		0x02
#define	ZEN_MPIO_OP_GET_ASK_RESULT		0x03
#define	ZEN_MPIO_OP_SETUP_LINK			0x04
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
#define	ZEN_MPIO_OP_ENUMERATE_I2C		0x10
#define	ZEN_MPIO_OP_GET_I2C_DEV			0x11
#define	ZEN_MPIO_OP_GET_I2C_DEV_CHG		0x12
#define	ZEN_MPIO_OP_SET_HP_CFG_TBL		0x13
#define	ZEN_MPIO_OP_HOTPLUG_EN			0x14
#define	ZEN_MPIO_OP_LEGACY_HP_DIS		0x15
#define	ZEN_MPIO_OP_SET_HP_I2C_SW_ADDR		0x16
#define	ZEN_MPIO_OP_SET_HP_BLINK_IVAL		0x17
#define	ZEN_MPIO_OP_SET_HP_POLL_IVAL		0x18
#define	ZEN_MPIO_OP_SET_HP_FLAGS		0x19

#define	ZEN_MPIO_OP_POSTED			(3 << 8)

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

#define	ZEN_MPIO_XFER_TO_RAM			0
#define	ZEN_MPIO_XFER_FROM_RAM			1

#define	ZEN_MPIO_PORT_NOT_PRESENT		0
#define	ZEN_MPIO_PORT_PRESENT			1

#define	ZEN_MPIO_LINK_ALL			0
#define	ZEN_MPIO_LINK_SELECTED			1

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

typedef struct zen_mpio_global_config {
	/* uint32_t mpio_global_cfg_args[0]: General settings */
	uint32_t		zmgc_skip_vet:1;
	uint32_t		zmgc_ntb_hp_ival:1;
	uint32_t		zmgc_save_restore_mode:2;
	uint32_t		zmgc_exact_match_port_size:1;
	uint32_t		zmgc_skip_disable_link_on_fail:1;
	uint32_t		zmgc_use_phy_sram:1;
	uint32_t		zmgc_valid_phy_firmware:1;
	uint32_t		zmgc_enable_loopback_support:1;
	uint32_t		zmgc_stb_verbosity:2;
	uint32_t		zmgc_en_pcie_noncomp_wa:1;
	uint32_t		zmgc_active_slt_mode:1;
	uint32_t		zmgc_legacy_dev_boot_fail_wa:1;
	uint32_t		zmgc_deferred_msg_supt:1;
	uint32_t		zmgc_cxl_gpf_phase2_timeout:4;
	uint32_t		zmgc_run_xgmi_safe_recov_odt:1;
	uint32_t		zmgc_run_z_cal:1;
	uint32_t		zmgc_pad0:11;
	/* uint32_t mpio_global_cfg_args[1]: Power settings */
	uint32_t		zmgc_pwr_mgmt_clk_gating:1;
	uint32_t		zmgc_pwr_mgmt_static_pwr_gating:1;
	uint32_t		zmgc_pwr_mgmt_refclk_shutdown:1;
	uint32_t		zmgc_cbs_opts_en_pwr_mgmt:1;
	uint32_t		zmgc_pwr_mgmt_pma_pwr_gating:1;
	uint32_t		zmgc_pwr_mgmt_pma_clk_gating:1;
	uint32_t		zmgc_pad1:26;
	/* uint32_t mpio_global_cfg_args[2]: Link timeouts */
	uint16_t		zmgc_link_rcvr_det_poll_timeout_ms;
	uint16_t		zmgc_link_l0_poll_timeout_ms;
	/* uint32_t mpio_global_cfg_args[3]: Protocol settings */
	uint16_t		zmgc_link_reset_to_training_time_ms;
	uint16_t		zmgc_pcie_allow_completion_pass:1;
	uint16_t		zmgc_cbs_opts_allow_ptr_slip_ival:1;
	uint16_t		zmgc_link_dis_at_pwr_off_delay:4;
	uint16_t		zmgc_en_2spc_gen4:1;
	uint16_t		zmgc_pad2:9;
	/* uint32_t mpio_global_cfg_args[4]: Trap control */
	uint32_t		zmgc_dis_sbr_trap:1;
	uint32_t		zmgc_dis_lane_margining_trap:1;
	uint32_t		zmgc_pad3:30;
	/* uint32_t mpio_global_cfg_args[5]: Reserved */
	uint32_t		zmgc_resv;
} zen_mpio_global_config_t;

typedef struct zen_mpio_status {
	uint32_t		zms_cmd_stat;
	uint32_t		zms_cycle_stat;
	uint32_t		zms_fw_post_code;
	uint32_t		zms_fw_status;
	uint32_t		zms_resv[2];
} zen_mpio_status_t;

typedef struct zen_mpio_link_attr {
	/* uint32_t zmla[0]: BDF */
	uint32_t		zmla_dev_func;

	/* uint32_t zmla[1]: General */
	uint32_t		zmla_port_present:1;
	uint32_t		zmla_early_link_train:1;
	uint32_t		zmla_link_compl_mode:1;
	uint32_t		zmla_pad0:1;
	uint32_t		zmla_link_hp_type:4;

	/* Speed parameters */
	uint32_t		zmla_max_link_speed_cap:4;
	uint32_t		zmla_target_link_speed:4;

	/* PSP parameters */
	uint32_t		zmla_psp_mode:3;
	uint32_t		zmla_partner_dev_type:2;
	uint32_t		zmla_pad1:3;

	/* Control parameters */
	uint32_t		zmla_local_perst:1;
	uint32_t		zmla_bif_mode:1;
	uint32_t		zmla_is_master_pll:1;
	uint32_t		zmla_invert_rx_pol:1;
	uint32_t		zmla_invert_tx_pol:1;
	uint32_t		zmla_pad2:3;

	/* uint32_t zmla[2]: Gen3 and Gen4 search parameters */
	uint32_t		zmla_gen3_eq_search_mode:2;
	uint32_t		zmla_en_gen3_eq_search_mode:2;
	uint32_t		zmla_gen4_eq_search_mode:2;
	uint32_t		zmla_en_gen4_eq_search_mode:2;

	/* Gen5 and Gen6 search parameters */
	uint32_t		zmla_gen5_eq_search_mode:2;
	uint32_t		zmla_en_gen5_eq_search_mode:2;
	uint32_t		zmla_gen6_eq_search_mode:2;
	uint32_t		zmla_en_gen6_eq_search_mode:2;

	/* Tx/Rx parameters */
	uint32_t		zmla_demph_tx:2;
	uint32_t		zmla_en_demph_tx:1;
	uint32_t		zmla_tx_vetting:1;
	uint32_t		zmla_rx_vetting:1;
	uint32_t		zmla_pad3:3;

	/* ESM parameters */
	uint32_t		zmla_esm_speed:6;
	uint32_t		zmla_esm_mode:2;

	/* uint32_t zmla[3]: Bridge parameters */
	uint8_t			zmla_hfc_idx;
	uint8_t			zmla_dfc_idx;
	uint16_t		zmla_log_bridge_id:5;
	uint16_t		zmla_swing_mode:3;
	uint16_t		zmla_sris_skip_ival:3;
	uint16_t		zmla_pad4:5;

	/* uint32_t zmla[4]: Reserved */
	uint32_t		zmla_resv0;

	/* uint32_t zmla[5]: Reserved */
	uint32_t		zmla_resv1;
} zen_mpio_link_attr_t;

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
	uint32_t		zmils_state:4;
	uint32_t		zmils_speed:7;
	uint32_t		zmils_width:5;
	uint32_t		zmils_port:8;
	uint32_t		zmils_resv:8;
} zen_mpio_ict_link_status_t;

typedef struct zen_mpio_ask_port {
	zen_mpio_link_t			zma_link;
	zen_mpio_ict_link_status_t	zma_status;
	uint32_t			zma_resv[4];
} zen_mpio_ask_port_t;

/*
 * We size the maximum number of ports in the ask roughly based on the SP5
 * design and I/O die constraints as a rough swag. P0 and G3 can each support up
 * to 16 PCIe devices, while the remaining 6 groups cans upport up to 8-9
 * devices and P4/P5 can support up to 4 devices. That gives us 88 devices. We
 * currently require this to be a page size which can only fit up to 78 devices.
 */
#define	ZEN_MPIO_ASK_MAX_PORTS	78

typedef struct zen_mpio_ask {
	zen_mpio_ask_port_t	zma_ports[ZEN_MPIO_ASK_MAX_PORTS];
} zen_mpio_ask_t;

typedef struct zen_mpio_port_conf {
	zen_mpio_ask_port_t	zmpc_ask;
} zen_mpio_port_conf_t;

typedef struct zen_mpio_ext_attrs {
	uint8_t			zmad_type;
	uint8_t			zmad_vers:4;
	uint8_t			zmad_rsvd0:4;
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

typedef enum zen_mpio_ask_link_type {
	ZEN_MPIO_ASK_LINK_PCIE	= 0x00,
	ZEN_MPIO_ASK_LINK_SATA	= 0x01,
	ZEN_MPIO_ASK_LINK_XGMI	= 0x02,
	ZEN_MPIO_ASK_LINK_GMI	= 0x03,
	ZEN_MPIO_ASK_LINK_ETH	= 0x04,
	ZEN_MPIO_ASK_LINK_USB	= 0x05,
} zen_mpio_ask_link_type_t;

typedef enum zen_mpio_i2c_node_type {
	ZEN_MPIO_I2C_NODE_TYPE_UBM,
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

typedef struct zen_mpio_ocp_data {
	uint8_t			zmod_present_start;
	uint8_t			zmod_num_hosts:4;
	uint8_t			zmod_num_sockets:4;
	uint8_t			zmod_bif_prim:3;
	uint8_t			zmod_bif_sec:3;
	uint8_t			zmod_form_factor:1;
	uint8_t			zmod_resv:1;
	uint8_t			zmod_slot_num;
} zen_mpio_ocp_data_t;

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

typedef struct zen_mpio_htod_data {
	uint16_t		zmhdd_npem_en;
	uint16_t		zmhdd_npem_cap;
	zen_mpio_anc_data_t	zmhdd_anc_data;
	uint8_t			zmhdd_ocp_def_valid;
	uint8_t			zmhdd_ocp_def_prim_present_b;
	uint8_t			zmhdd_ocp_def_sec_present_b;
	uint8_t			zmhdd_ocp_slot_num;
} zen_mpio_htod_data_t;

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

typedef struct zen_mpio_ubm_extra {
	bool			zmue_ubm;
	uint8_t			zmue_slot;
	uint8_t			zmue_npem_en;
	uint16_t		zmue_npem_cap;
} zen_mpio_ubm_extra_t;

#pragma	pack()	/* pragma pack(1) */

typedef struct zen_mpio_config {
	zen_mpio_port_conf_t		*zmc_port_conf;
	zen_mpio_ask_t			*zmc_ask;
	zen_mpio_ubm_hfc_port_t		*zmc_ubm_hfc_ports;
	zen_mpio_ubm_extra_t		*zmc_ubm_extra;
	zen_mpio_ext_attrs_t		*zmc_ext_attrs;
	uint64_t			zmc_ask_pa;
	uint64_t			zmc_ext_attrs_pa;
	uint64_t			zmc_ubm_hfc_ports_pa;
	uint32_t			zmc_nports;
	uint32_t			zmc_ubm_hfc_nports;
	uint32_t			zmc_ubm_extra_len;
	uint32_t			zmc_ask_alloc_len;
	uint32_t			zmc_ext_attrs_alloc_len;
	uint32_t			zmc_ext_attrs_len;
	uint32_t			zmc_ubm_hfc_ports_alloc_len;
} zen_mpio_config_t;

/*
 * Data definitions for Ruby.
 */
extern const zen_mpio_port_conf_t ruby_mpio_pcie_s0[];
extern size_t RUBY_MPIO_PCIE_S0_LEN;

extern const zen_mpio_ubm_hfc_port_t ruby_mpio_hfc_ports[];
extern const size_t RUBY_MPIO_UBM_HFC_DESCR_NPORTS;

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
