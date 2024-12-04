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

#ifndef _SYS_IO_GENOA_MPIO_H
#define	_SYS_IO_GENOA_MPIO_H

/*
 * Defines Genoa-specific types, SMN register addresses, etc, for MPIO RPCs.
 */

#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Genoa-specific MPIO global configuration type.  Note, we try to keep this
 * structure at the latest defined by AMD firmware; whether all of the items are
 * used, however, depends on the specific firmware version supported.
 */
typedef struct genoa_mpio_global_config {
	/* uint32_t mpio_global_cfg_args[0]: General settings */
	uint32_t		gmgc_skip_vet:1;
	uint32_t		gmgc_ntb_hp_ival:1;
	uint32_t		gmgc_save_restore_mode:2;
	uint32_t		gmgc_exact_match_port_size:1;
	uint32_t		gmgc_skip_disable_link_on_fail:1;
	uint32_t		gmgc_use_phy_sram:1;
	uint32_t		gmgc_valid_phy_firmware:1;
	uint32_t		gmgc_enable_loopback_support:1;
	uint32_t		gmgc_stb_verbosity:2;
	uint32_t		gmgc_en_pcie_noncomp_wa:1;
	uint32_t		gmgc_active_slt_mode:1;
	uint32_t		gmgc_legacy_dev_boot_fail_wa:1;
	uint32_t		gmgc_deferred_msg_supt:1;
	uint32_t		gmgc_cxl_gpf_phase2_timeout:4;
	uint32_t		gmgc_run_xgmi_safe_recov_odt:1;
	uint32_t		gmgc_run_z_cal:1;
	uint32_t		gmgc_avoid_pcie_sata_bw_drop_p4_wa:1;
	uint32_t		gmgc_pad0:10;
	/* uint32_t mpio_global_cfg_args[1]: Power settings */
	uint32_t		gmgc_pwr_mgmt_clk_gating:1;
	uint32_t		gmgc_pwr_mgmt_static_pwr_gating:1;
	uint32_t		gmgc_pwr_mgmt_refclk_shutdown:1;
	uint32_t		gmgc_pwr_mgmt_en:1;
	uint32_t		gmgc_pwr_mgmt_pma_pwr_gating:1;
	uint32_t		gmgc_pwr_mgmt_pma_clk_gating:1;
	uint32_t		gmgc_pad1:26;
	/* uint32_t mpio_global_cfg_args[2]: Link timeouts */
	uint16_t		gmgc_link_rcvr_det_poll_timeout_ms;
	uint16_t		gmgc_link_l0_poll_timeout_ms;
	/* uint32_t mpio_global_cfg_args[3]: Protocol settings */
	uint16_t		gmgc_link_reset_to_training_time_ms;
	uint16_t		gmgc_pcie_allow_completion_pass:1;
	uint16_t		gmgc_allow_ptr_slip_ival:1;
	uint16_t		gmgc_link_dis_at_pwr_off_delay:4;
	uint16_t		gmgc_2spc_gen4_en:1;
	uint16_t		gmgc_pad2:9;
	/* uint32_t mpio_global_cfg_args[4]: Trap control */
	uint32_t		gmgc_dis_sbr_trap:1;
	uint32_t		gmgc_dis_lane_margining_trap:1;
	uint32_t		gmgc_pad3:30;
	/* uint32_t mpio_global_cfg_args[5]: Reserved */
	uint32_t		gmgc_resv;
} genoa_mpio_global_config_t;

/*
 * Genoa-specific SMN register addresses. These are stored in the
 * microarchitecture-specific platform constants, and consumed in by the
 * Zen-generic MPIO SMN register generator function defined in
 * sys/io/zen/mpio_impl.h and called from the MPIO RPC code zen_mpio.c.
 */

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_DOORBELL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x554,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_RESP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9c8,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9cc,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9d0,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9d4,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9d8,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9dc,		\
}

/*CSTYLED*/
#define	D_GENOA_MPIO_RPC_ARG5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_MPIO_RPC,	\
	.srd_reg = 0x9e0,		\
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_MPIO_H */
