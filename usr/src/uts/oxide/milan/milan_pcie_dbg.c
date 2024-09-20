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
 * Copyright 2024 Oxide Computer Co.
 */

/*
 * List of PCIe core and port space registers to capture during debugging.
 * These are used only on DEBUG kernels and are embedded in the constant
 * milan_platform definition and consumed by code in zen/zen_fabric.c
 */

#include <sys/amdzen/smn.h>
#include <sys/io/milan/pcie.h>
#include <sys/io/milan/pcie_impl.h>

#ifdef	DEBUG

const zen_pcie_reg_dbg_t milan_pcie_core_dbg_regs[] = {
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG",
		.zprd_def = D_PCIE_CORE_HW_DBG
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_LC",
		.zprd_def = D_PCIE_CORE_HW_DBG_LC
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_NUM_NAK",
		.zprd_def = D_PCIE_CORE_RX_NUM_NAK
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_NUM_NAK_GENERATED",
		.zprd_def = D_PCIE_CORE_RX_NUM_NAK_GEN
	},
	{
		.zprd_name = "PCIECORE::PCIE_CNTL",
		.zprd_def = D_PCIE_CORE_PCIE_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_CONFIG_CNTL",
		.zprd_def = D_PCIE_CORE_CFG_CTL_CONFIG
	},
	{
		.zprd_name = "PCIECORE::PCIE_DEBUG_CNTL",
		.zprd_def = D_PCIE_CORE_DBG_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_CNTL2",
		.zprd_def = D_PCIE_CORE_PCIE_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_CNTL2",
		.zprd_def = D_PCIE_CORE_RX_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_F0_ATTR_CNTL",
		.zprd_def = D_PCIE_CORE_TX_F0_ATTR_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_CI_CNTL",
		.zprd_def = D_PCIE_CORE_CI_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_BUS_CNTL",
		.zprd_def = D_PCIE_CORE_BUS_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATE6",
		.zprd_def = D_PCIE_CORE_LC_STATE6
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATE7",
		.zprd_def = D_PCIE_CORE_LC_STATE7
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATE8",
		.zprd_def = D_PCIE_CORE_LC_STATE8
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATE9",
		.zprd_def = D_PCIE_CORE_LC_STATE9
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATE10",
		.zprd_def = D_PCIE_CORE_LC_STATE10
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATE11",
		.zprd_def = D_PCIE_CORE_LC_STATE11
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATUS1",
		.zprd_def = D_PCIE_CORE_LC_STATUS1
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATUS2",
		.zprd_def = D_PCIE_CORE_LC_STATUS2
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CNTL3",
		.zprd_def = D_PCIE_CORE_TX_CTL3
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_STATUS",
		.zprd_def = D_PCIE_CORE_TX_STATUS
	},
	{
		.zprd_name = "PCIECORE::PCIE_WPR_CNTL",
		.zprd_def = D_PCIE_CORE_WPR_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_LAST_TLP0",
		.zprd_def = D_PCIE_CORE_RX_LAST_TLP0
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_LAST_TLP1",
		.zprd_def = D_PCIE_CORE_RX_LAST_TLP1
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_LAST_TLP2",
		.zprd_def = D_PCIE_CORE_RX_LAST_TLP2
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_LAST_TLP3",
		.zprd_def = D_PCIE_CORE_RX_LAST_TLP3
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_LAST_TLP0",
		.zprd_def = D_PCIE_CORE_TX_LAST_TLP0
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_LAST_TLP1",
		.zprd_def = D_PCIE_CORE_TX_LAST_TLP1
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_LAST_TLP2",
		.zprd_def = D_PCIE_CORE_TX_LAST_TLP2
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_LAST_TLP3",
		.zprd_def = D_PCIE_CORE_TX_LAST_TLP3
	},
	{
		.zprd_name = "PCIECORE::PCIE_I2C_REG_ADDR_EXPAND",
		.zprd_def = D_PCIE_CORE_I2C_ADDR
	},
	{
		.zprd_name = "PCIECORE::PCIE_I2C_REG_DATA",
		.zprd_def = D_PCIE_CORE_I2C_DATA
	},
	{
		.zprd_name = "PCIECORE::PCIE_CFG_CNTL",
		.zprd_def = D_PCIE_CORE_CFG_CTL_CFG
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_PM_CNTL",
		.zprd_def = D_PCIE_CORE_LC_PM_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_PORT_ORDER_CNTL",
		.zprd_def = D_PCIE_CORE_LC_PORT_ORDER_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_CNTL",
		.zprd_def = D_PCIE_CORE_PCIE_P_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_BUF_STATUS",
		.zprd_def = D_PCIE_CORE_P_BUF_STATUS
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_DECODER_STATUS",
		.zprd_def = D_PCIE_CORE_P_DECODER_STATUS
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_MISC_STATUS",
		.zprd_def = D_PCIE_CORE_P_MISC_STATUS
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_RX_L0S_FTS",
		.zprd_def = D_PCIE_CORE_P_RX_L0S_FTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CCIX_CNTL0",
		.zprd_def = D_PCIE_CORE_TX_CCIX_CTL0
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CCIX_CNTL1",
		.zprd_def = D_PCIE_CORE_TX_CCIX_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CCIX_PORT_MAP",
		.zprd_def = D_PCIE_CORE_TX_CCIX_PORT_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CCIX_ERR_CTL",
		.zprd_def = D_PCIE_CORE_TX_CCIX_ERR_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_CCIX_CTL0",
		.zprd_def = D_PCIE_CORE_RX_CCIX_CTL0
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_AD",
		.zprd_def = D_PCIE_CORE_RX_AD
	},
	{
		.zprd_name = "PCIECORE::PCIE_SDP_CTRL",
		.zprd_def = D_PCIE_CORE_SDP_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_NBIO_CLKREQb_MAP_CNTL",
		.zprd_def = D_PCIE_CORE_NBIO_CLKREQ_B_MAP_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_SDP_RC_SLV_ATTR_CTRL",
		.zprd_def = D_PCIE_CORE_SDP_RC_SLV_ATTR_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_STRAP_F0",
		.zprd_def = D_PCIE_CORE_STRAP_F0
	},
	{
		.zprd_name = "PCIECORE::PCIE_STRAP_NTB",
		.zprd_def = D_PCIE_CORE_STRAP_NTB
	},
	{
		.zprd_name = "PCIECORE::PCIE_STRAP_MISC",
		.zprd_def = D_PCIE_CORE_STRAP_MISC
	},
	{
		.zprd_name = "PCIECORE::PCIE_STRAP_MISC2",
		.zprd_def = D_PCIE_CORE_STRAP_MISC2
	},
	{
		.zprd_name = "PCIECORE::PCIE_STRAP_PI",
		.zprd_def = D_PCIE_CORE_STRAP_PI
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_CLR",
		.zprd_def = D_PCIE_CORE_PRBS_CLR
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_STATUS1",
		.zprd_def = D_PCIE_CORE_PRBS_STATUS1
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_STATUS2",
		.zprd_def = D_PCIE_CORE_PRBS_STATUS2
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_FREERUN",
		.zprd_def = D_PCIE_CORE_PRBS_FREERUN
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_MISC",
		.zprd_def = D_PCIE_CORE_PRBS_MISC
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_USER_PATTERN",
		.zprd_def = D_PCIE_CORE_PRBS_USER_PATTERN
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_LO_BITCNT",
		.zprd_def = D_PCIE_CORE_PRBS_LO_BITCNT
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_HI_BITCNT",
		.zprd_def = D_PCIE_CORE_PRBS_HI_BITCNT
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT0",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT0
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT1",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT1
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT2",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT2
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT3",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT3
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT4",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT4
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT5",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT5
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT6",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT6
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT7",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT7
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT8",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT8
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT9",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT9
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT10",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT10
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT11",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT11
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT12",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT12
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT13",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT13
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT14",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT14
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT15",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT15
	},
	{
		.zprd_name = "PCIECORE::SWRST_COMMAND_STATUS",
		.zprd_def = D_PCIE_CORE_SWRST_CMD_STATUS
	},
	{
		.zprd_name = "PCIECORE::SWRST_GENERAL_CONTROL",
		.zprd_def = D_PCIE_CORE_SWRST_GEN_CTL
	},
	{
		.zprd_name = "PCIECORE::SWRST_COMMAND_0",
		.zprd_def = D_PCIE_CORE_SWRST_CMD0
	},
	{
		.zprd_name = "PCIECORE::SWRST_COMMAND_1",
		.zprd_def = D_PCIE_CORE_SWRST_CMD1
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_0",
		.zprd_def = D_PCIE_CORE_SWRST_CTL0
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_1",
		.zprd_def = D_PCIE_CORE_SWRST_CTL1
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_2",
		.zprd_def = D_PCIE_CORE_SWRST_CTL2
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_3",
		.zprd_def = D_PCIE_CORE_SWRST_CTL3
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_4",
		.zprd_def = D_PCIE_CORE_SWRST_CTL4
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_5",
		.zprd_def = D_PCIE_CORE_SWRST_CTL5
	},
	{
		.zprd_name = "PCIECORE::SWRST_CONTROL_6",
		.zprd_def = D_PCIE_CORE_SWRST_CTL6
	},
	{
		.zprd_name = "PCIECORE::CPM_CONTROL",
		.zprd_def = D_PCIE_CORE_CPM_CTL
	},
	{
		.zprd_name = "PCIECORE::CPM_SPLIT_CONTROL",
		.zprd_def = D_PCIE_CORE_CPM_SPLIT_CTL
	},
	{
		.zprd_name = "PCIECORE::SMN_APERTURE_A",
		.zprd_def = D_PCIE_CORE_SMN_APERTURE_A
	},
	{
		.zprd_name = "PCIECORE::SMN_APERTURE_B",
		.zprd_def = D_PCIE_CORE_SMN_APERTURE_B
	},
	{
		.zprd_name = "PCIECORE::RSMU_MASTER_CONTROL",
		.zprd_def = D_PCIE_CORE_RSMU_MASTER_CTL
	},
	{
		.zprd_name = "PCIECORE::RSMU_SLAVE_CONTROL",
		.zprd_def = D_PCIE_CORE_RSMU_SLAVE_CTL
	},
	{
		.zprd_name = "PCIECORE::RSMU_POWER_GATING_CONTROL",
		.zprd_def = D_PCIE_CORE_RSMU_PWR_GATE_CTL
	},
	{
		.zprd_name = "PCIECORE::RSMU_BIOS_TIMER_CMD",
		.zprd_def = D_PCIE_CORE_RSMU_TIMER_CMD
	},
	{
		.zprd_name = "PCIECORE::RSMU_BIOS_TIMER_CNTL",
		.zprd_def = D_PCIE_CORE_RSMU_TIMER_CTL
	},
	{
		.zprd_name = "PCIECORE::RSMU_BIOS_TIMER_DEBUG",
		.zprd_def = D_PCIE_CORE_RSMU_TIMER_DBG
	},
	{
		.zprd_name = "PCIECORE::LNCNT_CONTROL",
		.zprd_def = D_PCIE_CORE_LNCNT_CTL
	},
	{
		.zprd_name = "PCIECORE::LNCNT_QUAN_THRD",
		.zprd_def = D_PCIE_CORE_LNCNT_QUAN_THRD
	},
	{
		.zprd_name = "PCIECORE::LNCNT_WEIGHT",
		.zprd_def = D_PCIE_CORE_LNCNT_WEIGHT
	},
	{
		.zprd_name = "PCIECORE::SMU_HP_STATUS_UPDATE",
		.zprd_def = D_PCIE_CORE_SMU_HP_STATUS_UPDATE
	},
	{
		.zprd_name = "PCIECORE::HP_SMU_COMMAND_UPDATE",
		.zprd_def = D_PCIE_CORE_HP_SMU_CMD_UPDATE
	},
	{
		.zprd_name = "PCIECORE::SMU_HP_END_OF_INTERRUPT",
		.zprd_def = D_PCIE_CORE_SMU_HP_EOI
	},
	{
		.zprd_name = "PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR",
		.zprd_def = D_PCIE_CORE_SMU_INT_PIN_SHARING
	},
	{
		.zprd_name = "PCIECORE::PCIE_PGMST_CNTL",
		.zprd_def = D_PCIE_CORE_PGMST_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_PGSLV_CNTL",
		.zprd_def = D_PCIE_CORE_PGSLV_CTL
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_DF_ADDRESS",
		.zprd_def = D_PCIE_CORE_SMU_DF_ADDR
	},
	{
		.zprd_name = "PCIECORE::LC_CPM_CONTROL_0",
		.zprd_def = D_PCIE_CORE_LC_CPM_CTL0
	},
	{
		.zprd_name = "PCIECORE::LC_CPM_CONTROL_1",
		.zprd_def = D_PCIE_CORE_LC_CPM_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_RXMARGIN_CONTROL_CAPABILITIES",
		.zprd_def = D_PCIE_CORE_RX_MARGIN_CTL_CAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_RXMARGIN_1_SETTINGS",
		.zprd_def = D_PCIE_CORE_RX_MARGIN1
	},
	{
		.zprd_name = "PCIECORE::PCIE_RXMARGIN_2_SETTINGS",
		.zprd_def = D_PCIE_CORE_RX_MARGIN2
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRESENCE_DETECT_SELECT",
		.zprd_def = D_PCIE_CORE_PRES
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_DEBUG_CNTL",
		.zprd_def = D_PCIE_CORE_LC_DBG_CTL
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_FENCED1_REG",
		.zprd_def = D_PCIE_CORE_SMU_FENCED1
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_FENCED2_REG",
		.zprd_def = D_PCIE_CORE_SMU_FENCED2
	}
};

const zen_pcie_reg_dbg_t milan_pcie_port_dbg_regs[] = {
	{
		.zprd_name = "PCIEPORT::PCIEP_HW_DEBUG",
		.zprd_def = D_PCIE_PORT_HW_DBG
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_HW_DEBUG_LC",
		.zprd_def = D_PCIE_PORT_HW_DBG_LC
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_PORT_CNTL",
		.zprd_def = D_PCIE_PORT_PCTL
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_SDP_CTRL",
		.zprd_def = D_PCIE_PORT_SDP_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CNTL",
		.zprd_def = D_PCIE_PORT_TX_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_REQUESTER_ID",
		.zprd_def = D_PCIE_PORT_TX_ID
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_VENDOR_SPECIFIC",
		.zprd_def = D_PCIE_PORT_TX_VS_DLLP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_REQUEST_NUM_CNTL",
		.zprd_def = D_PCIE_PORT_TX_REQ_NUM_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_SEQ",
		.zprd_def = D_PCIE_PORT_TX_SEQ
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_REPLAY",
		.zprd_def = D_PCIE_PORT_TX_REPLAY
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_ACK_LATENCY_LIMIT",
		.zprd_def = D_PCIE_PORT_TX_ACK_LAT_LIM
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_NOP_DLLP",
		.zprd_def = D_PCIE_PORT_TX_NOP_DLLP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CNTL_2",
		.zprd_def = D_PCIE_PORT_TX_CTL2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_P",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_P
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_NP",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_NP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_CPL",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_CPL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_P",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_INIT_P
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_NP",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_INIT_NP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_CPL",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_INIT_CPL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_STATUS",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_STATUS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_FCU_THRESHOLD",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_FCU_THRESH
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CCIX_PORT_CNTL0",
		.zprd_def = D_PCIE_PORT_TX_CCIX_PORT_CTL0
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_CCIX_PORT_CNTL1",
		.zprd_def = D_PCIE_PORT_TX_CCIX_PORT_CTL1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_STACKED_BASE",
		.zprd_def = D_PCIE_PORT_CCIX_STACKED_BASE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_STACKED_LIMIT",
		.zprd_def = D_PCIE_PORT_CCIX_STACKED_LIM
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_UPPER_ADDR",
		.zprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_HI
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_LOWER_ADDR",
		.zprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_LO
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_CTRL",
		.zprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_WR_UPPER_ADDR",
		.zprd_def = D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_HI
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_WR_LOWER_ADDR",
		.zprd_def = D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_LO
	},
	{
		.zprd_name = "PCIEPORT::PCIE_CCIX_MISC_STATUS",
		.zprd_def = D_PCIE_PORT_CCIX_MISC_STATUS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_P_PORT_LANE_STATUS",
		.zprd_def = D_PCIE_PORT_P_LANE_STATUS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_FC_P",
		.zprd_def = D_PCIE_PORT_FC_P
	},
	{
		.zprd_name = "PCIEPORT::PCIE_FC_NP",
		.zprd_def = D_PCIE_PORT_FC_NP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_FC_CPL",
		.zprd_def = D_PCIE_PORT_FC_CPL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_FC_P_VC1",
		.zprd_def = D_PCIE_PORT_FC_P_VC1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_FC_NP_VC1",
		.zprd_def = D_PCIE_PORT_FC_NP_VC1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_FC_CPL_VC1",
		.zprd_def = D_PCIE_PORT_FC_CPL_VC1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_ERR_CNTL",
		.zprd_def = D_PCIE_PORT_ERR_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_CNTL",
		.zprd_def = D_PCIE_PORT_RX_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_EXPECTED_SEQNUM",
		.zprd_def = D_PCIE_PORT_RX_EXP_SEQ
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_VENDOR_SPECIFIC",
		.zprd_def = D_PCIE_PORT_RX_VS_DLLP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_CNTL3",
		.zprd_def = D_PCIE_PORT_RX_CTL3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_P",
		.zprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_P
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_NP",
		.zprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_NP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_CPL",
		.zprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_CPL
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_ERROR_INJECT_PHYSICAL",
		.zprd_def = D_PCIE_PORT_ERR_INJ_PHYS
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_ERROR_INJECT_TRANSACTION",
		.zprd_def = D_PCIE_PORT_ERR_INJ_TXN
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_NAK_COUNTER",
		.zprd_def = D_PCIE_PORT_NAK_COUNTER
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_CTRL_STATUS",
		.zprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_CTL_STATUS
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_THRESHOLD_VALUES",
		.zprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_THRESH_VALS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL",
		.zprd_def = D_PCIE_PORT_LC_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_TRAINING_CNTL",
		.zprd_def = D_PCIE_PORT_LC_TRAIN_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL",
		.zprd_def = D_PCIE_PORT_LC_WIDTH_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_N_FTS_CNTL",
		.zprd_def = D_PCIE_PORT_LC_NFTS_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_SPEED_CNTL",
		.zprd_def = D_PCIE_PORT_LC_SPEED_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_STATE0",
		.zprd_def = D_PCIE_PORT_LC_STATE0
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_STATE1",
		.zprd_def = D_PCIE_PORT_LC_STATE1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_STATE2",
		.zprd_def = D_PCIE_PORT_LC_STATE2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_STATE3",
		.zprd_def = D_PCIE_PORT_LC_STATE3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_STATE4",
		.zprd_def = D_PCIE_PORT_LC_STATE4
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_STATE5",
		.zprd_def = D_PCIE_PORT_LC_STATE5
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL2",
		.zprd_def = D_PCIE_PORT_LINK_MGMT_CTL2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL2",
		.zprd_def = D_PCIE_PORT_LC_CTL2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_BW_CHANGE_CNTL",
		.zprd_def = D_PCIE_PORT_LC_BW_CHANGE_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CDR_CNTL",
		.zprd_def = D_PCIE_PORT_LC_CDR_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_LANE_CNTL",
		.zprd_def = D_PCIE_PORT_LC_LANE_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL3",
		.zprd_def = D_PCIE_PORT_LC_CTL3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL4",
		.zprd_def = D_PCIE_PORT_LC_CTL4
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL5",
		.zprd_def = D_PCIE_PORT_LC_CTL5
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF",
		.zprd_def = D_PCIE_PORT_LC_FORCE_COEFF
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_BEST_EQ_SETTINGS",
		.zprd_def = D_PCIE_PORT_LC_BEST_EQ
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF",
		.zprd_def = D_PCIE_PORT_LC_FORCE_EQ_COEFF
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL6",
		.zprd_def = D_PCIE_PORT_LC_CTL6
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL7",
		.zprd_def = D_PCIE_PORT_LC_CTL7
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_STATUS",
		.zprd_def = D_PCIE_PORT_LINK_MGMT_STATUS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_MASK",
		.zprd_def = D_PCIE_PORT_LINK_MGMT_MASK
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL",
		.zprd_def = D_PCIE_PORT_LINK_MGMT_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_STRAP_LC",
		.zprd_def = D_PCIE_PORT_STRAP_LC
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_STRAP_MISC",
		.zprd_def = D_PCIE_PORT_STRAP_MISC
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_STRAP_LC2",
		.zprd_def = D_PCIE_PORT_STRAP_LC2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE",
		.zprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE2",
		.zprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_PORT_ORDER",
		.zprd_def = D_PCIE_PORT_LC_PORT_ORDER
	},
	{
		.zprd_name = "PCIEPORT::PCIE_BCH_ECC_CNTL",
		.zprd_def = D_PCIE_PORT_BCH_ECC_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_HPGI_PRIVATE",
		.zprd_def = D_PCIE_PORT_HPGI_PRIV
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_HPGI",
		.zprd_def = D_PCIE_PORT_HPGI
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_HCNT_DESCRIPTOR",
		.zprd_def = D_PCIE_PORT_HP_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL8",
		.zprd_def = D_PCIE_PORT_LC_CTL8
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL9",
		.zprd_def = D_PCIE_PORT_LC_CTL9
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF2",
		.zprd_def = D_PCIE_PORT_LC_FORCE_COEFF2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF2",
		.zprd_def = D_PCIE_PORT_LC_FORCE_EQ_COEFF2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FINE_GRAIN_CLK_GATE_OVERRIDES",
		.zprd_def = D_PCIE_PORT_LC_FINE_GRAIN_CLK_GATE_OVR
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL10",
		.zprd_def = D_PCIE_PORT_LC_CTL10
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL11",
		.zprd_def = D_PCIE_PORT_LC_CTL11
	}
};

#endif	/* DEBUG */
