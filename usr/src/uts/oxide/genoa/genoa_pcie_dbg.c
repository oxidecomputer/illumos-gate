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
 * Copyright 2023 Oxide Computer Co.
 */

/*
 * List of PCIe core and port space registers to capture during debugging.
 * These are used only on DEBUG kernels and are consumed by code in
 * milan_fabric.c:milan_pcie_populate_xx_dbg().
 */

#include <sys/amdzen/smn.h>
#include <sys/io/milan/pcie.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/sysmacros.h>

#ifdef	DEBUG

const milan_pcie_reg_dbg_t milan_pcie_core_dbg_regs[] = {
	{
		.mprd_name = "PCIECORE::PCIE_HW_DEBUG",
		.mprd_def = D_PCIE_CORE_HW_DBG
	},
	{
		.mprd_name = "PCIECORE::PCIE_HW_DEBUG_LC",
		.mprd_def = D_PCIE_CORE_HW_DBG_LC
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_NUM_NAK",
		.mprd_def = D_PCIE_CORE_RX_NUM_NAK
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_NUM_NAK_GENERATED",
		.mprd_def = D_PCIE_CORE_RX_NUM_NAK_GEN
	},
	{
		.mprd_name = "PCIECORE::PCIE_CNTL",
		.mprd_def = D_PCIE_CORE_PCIE_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_CONFIG_CNTL",
		.mprd_def = D_PCIE_CORE_CFG_CTL_CONFIG
	},
	{
		.mprd_name = "PCIECORE::PCIE_DEBUG_CNTL",
		.mprd_def = D_PCIE_CORE_DBG_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_CNTL2",
		.mprd_def = D_PCIE_CORE_PCIE_CTL2
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_CNTL2",
		.mprd_def = D_PCIE_CORE_RX_CTL2
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_F0_ATTR_CNTL",
		.mprd_def = D_PCIE_CORE_TX_F0_ATTR_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_CI_CNTL",
		.mprd_def = D_PCIE_CORE_CI_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_BUS_CNTL",
		.mprd_def = D_PCIE_CORE_BUS_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATE6",
		.mprd_def = D_PCIE_CORE_LC_STATE6
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATE7",
		.mprd_def = D_PCIE_CORE_LC_STATE7
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATE8",
		.mprd_def = D_PCIE_CORE_LC_STATE8
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATE9",
		.mprd_def = D_PCIE_CORE_LC_STATE9
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATE10",
		.mprd_def = D_PCIE_CORE_LC_STATE10
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATE11",
		.mprd_def = D_PCIE_CORE_LC_STATE11
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATUS1",
		.mprd_def = D_PCIE_CORE_LC_STATUS1
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_STATUS2",
		.mprd_def = D_PCIE_CORE_LC_STATUS2
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_CNTL3",
		.mprd_def = D_PCIE_CORE_TX_CTL3
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_STATUS",
		.mprd_def = D_PCIE_CORE_TX_STATUS
	},
	{
		.mprd_name = "PCIECORE::PCIE_WPR_CNTL",
		.mprd_def = D_PCIE_CORE_WPR_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_LAST_TLP0",
		.mprd_def = D_PCIE_CORE_RX_LAST_TLP0
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_LAST_TLP1",
		.mprd_def = D_PCIE_CORE_RX_LAST_TLP1
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_LAST_TLP2",
		.mprd_def = D_PCIE_CORE_RX_LAST_TLP2
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_LAST_TLP3",
		.mprd_def = D_PCIE_CORE_RX_LAST_TLP3
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_LAST_TLP0",
		.mprd_def = D_PCIE_CORE_TX_LAST_TLP0
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_LAST_TLP1",
		.mprd_def = D_PCIE_CORE_TX_LAST_TLP1
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_LAST_TLP2",
		.mprd_def = D_PCIE_CORE_TX_LAST_TLP2
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_LAST_TLP3",
		.mprd_def = D_PCIE_CORE_TX_LAST_TLP3
	},
	{
		.mprd_name = "PCIECORE::PCIE_I2C_REG_ADDR_EXPAND",
		.mprd_def = D_PCIE_CORE_I2C_ADDR
	},
	{
		.mprd_name = "PCIECORE::PCIE_I2C_REG_DATA",
		.mprd_def = D_PCIE_CORE_I2C_DATA
	},
	{
		.mprd_name = "PCIECORE::PCIE_CFG_CNTL",
		.mprd_def = D_PCIE_CORE_CFG_CTL_CFG
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_PM_CNTL",
		.mprd_def = D_PCIE_CORE_LC_PM_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_PORT_ORDER_CNTL",
		.mprd_def = D_PCIE_CORE_LC_PORT_ORDER_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_P_CNTL",
		.mprd_def = D_PCIE_CORE_PCIE_P_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_P_BUF_STATUS",
		.mprd_def = D_PCIE_CORE_P_BUF_STATUS
	},
	{
		.mprd_name = "PCIECORE::PCIE_P_DECODER_STATUS",
		.mprd_def = D_PCIE_CORE_P_DECODER_STATUS
	},
	{
		.mprd_name = "PCIECORE::PCIE_P_MISC_STATUS",
		.mprd_def = D_PCIE_CORE_P_MISC_STATUS
	},
	{
		.mprd_name = "PCIECORE::PCIE_P_RX_L0S_FTS",
		.mprd_def = D_PCIE_CORE_P_RX_L0S_FTS
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_CCIX_CNTL0",
		.mprd_def = D_PCIE_CORE_TX_CCIX_CTL0
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_CCIX_CNTL1",
		.mprd_def = D_PCIE_CORE_TX_CCIX_CTL1
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_CCIX_PORT_MAP",
		.mprd_def = D_PCIE_CORE_TX_CCIX_PORT_MAP
	},
	{
		.mprd_name = "PCIECORE::PCIE_TX_CCIX_ERR_CTL",
		.mprd_def = D_PCIE_CORE_TX_CCIX_ERR_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_CCIX_CTL0",
		.mprd_def = D_PCIE_CORE_RX_CCIX_CTL0
	},
	{
		.mprd_name = "PCIECORE::PCIE_RX_AD",
		.mprd_def = D_PCIE_CORE_RX_AD
	},
	{
		.mprd_name = "PCIECORE::PCIE_SDP_CTRL",
		.mprd_def = D_PCIE_CORE_SDP_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_NBIO_CLKREQb_MAP_CNTL",
		.mprd_def = D_PCIE_CORE_NBIO_CLKREQ_B_MAP_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_SDP_RC_SLV_ATTR_CTRL",
		.mprd_def = D_PCIE_CORE_SDP_RC_SLV_ATTR_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_STRAP_F0",
		.mprd_def = D_PCIE_CORE_STRAP_F0
	},
	{
		.mprd_name = "PCIECORE::PCIE_STRAP_NTB",
		.mprd_def = D_PCIE_CORE_STRAP_NTB
	},
	{
		.mprd_name = "PCIECORE::PCIE_STRAP_MISC",
		.mprd_def = D_PCIE_CORE_STRAP_MISC
	},
	{
		.mprd_name = "PCIECORE::PCIE_STRAP_MISC2",
		.mprd_def = D_PCIE_CORE_STRAP_MISC2
	},
	{
		.mprd_name = "PCIECORE::PCIE_STRAP_PI",
		.mprd_def = D_PCIE_CORE_STRAP_PI
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_CLR",
		.mprd_def = D_PCIE_CORE_PRBS_CLR
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_STATUS1",
		.mprd_def = D_PCIE_CORE_PRBS_STATUS1
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_STATUS2",
		.mprd_def = D_PCIE_CORE_PRBS_STATUS2
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_FREERUN",
		.mprd_def = D_PCIE_CORE_PRBS_FREERUN
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_MISC",
		.mprd_def = D_PCIE_CORE_PRBS_MISC
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_USER_PATTERN",
		.mprd_def = D_PCIE_CORE_PRBS_USER_PATTERN
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_LO_BITCNT",
		.mprd_def = D_PCIE_CORE_PRBS_LO_BITCNT
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_HI_BITCNT",
		.mprd_def = D_PCIE_CORE_PRBS_HI_BITCNT
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT0",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT0
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT1",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT1
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT2",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT2
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT3",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT3
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT4",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT4
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT5",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT5
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT6",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT6
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT7",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT7
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT8",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT8
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT9",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT9
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT10",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT10
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT11",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT11
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT12",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT12
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT13",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT13
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT14",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT14
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRBS_ERRCNT15",
		.mprd_def = D_PCIE_CORE_PRBS_ERRCNT15
	},
	{
		.mprd_name = "PCIECORE::SWRST_COMMAND_STATUS",
		.mprd_def = D_PCIE_CORE_SWRST_CMD_STATUS
	},
	{
		.mprd_name = "PCIECORE::SWRST_GENERAL_CONTROL",
		.mprd_def = D_PCIE_CORE_SWRST_GEN_CTL
	},
	{
		.mprd_name = "PCIECORE::SWRST_COMMAND_0",
		.mprd_def = D_PCIE_CORE_SWRST_CMD0
	},
	{
		.mprd_name = "PCIECORE::SWRST_COMMAND_1",
		.mprd_def = D_PCIE_CORE_SWRST_CMD1
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_0",
		.mprd_def = D_PCIE_CORE_SWRST_CTL0
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_1",
		.mprd_def = D_PCIE_CORE_SWRST_CTL1
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_2",
		.mprd_def = D_PCIE_CORE_SWRST_CTL2
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_3",
		.mprd_def = D_PCIE_CORE_SWRST_CTL3
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_4",
		.mprd_def = D_PCIE_CORE_SWRST_CTL4
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_5",
		.mprd_def = D_PCIE_CORE_SWRST_CTL5
	},
	{
		.mprd_name = "PCIECORE::SWRST_CONTROL_6",
		.mprd_def = D_PCIE_CORE_SWRST_CTL6
	},
	{
		.mprd_name = "PCIECORE::CPM_CONTROL",
		.mprd_def = D_PCIE_CORE_CPM_CTL
	},
	{
		.mprd_name = "PCIECORE::CPM_SPLIT_CONTROL",
		.mprd_def = D_PCIE_CORE_CPM_SPLIT_CTL
	},
	{
		.mprd_name = "PCIECORE::SMN_APERTURE_A",
		.mprd_def = D_PCIE_CORE_SMN_APERTURE_A
	},
	{
		.mprd_name = "PCIECORE::SMN_APERTURE_B",
		.mprd_def = D_PCIE_CORE_SMN_APERTURE_B
	},
	{
		.mprd_name = "PCIECORE::RSMU_MASTER_CONTROL",
		.mprd_def = D_PCIE_CORE_RSMU_MASTER_CTL
	},
	{
		.mprd_name = "PCIECORE::RSMU_SLAVE_CONTROL",
		.mprd_def = D_PCIE_CORE_RSMU_SLAVE_CTL
	},
	{
		.mprd_name = "PCIECORE::RSMU_POWER_GATING_CONTROL",
		.mprd_def = D_PCIE_CORE_RSMU_PWR_GATE_CTL
	},
	{
		.mprd_name = "PCIECORE::RSMU_BIOS_TIMER_CMD",
		.mprd_def = D_PCIE_CORE_RSMU_TIMER_CMD
	},
	{
		.mprd_name = "PCIECORE::RSMU_BIOS_TIMER_CNTL",
		.mprd_def = D_PCIE_CORE_RSMU_TIMER_CTL
	},
	{
		.mprd_name = "PCIECORE::RSMU_BIOS_TIMER_DEBUG",
		.mprd_def = D_PCIE_CORE_RSMU_TIMER_DBG
	},
	{
		.mprd_name = "PCIECORE::LNCNT_CONTROL",
		.mprd_def = D_PCIE_CORE_LNCNT_CTL
	},
	{
		.mprd_name = "PCIECORE::LNCNT_QUAN_THRD",
		.mprd_def = D_PCIE_CORE_LNCNT_QUAN_THRD
	},
	{
		.mprd_name = "PCIECORE::LNCNT_WEIGHT",
		.mprd_def = D_PCIE_CORE_LNCNT_WEIGHT
	},
	{
		.mprd_name = "PCIECORE::SMU_HP_STATUS_UPDATE",
		.mprd_def = D_PCIE_CORE_SMU_HP_STATUS_UPDATE
	},
	{
		.mprd_name = "PCIECORE::HP_SMU_COMMAND_UPDATE",
		.mprd_def = D_PCIE_CORE_HP_SMU_CMD_UPDATE
	},
	{
		.mprd_name = "PCIECORE::SMU_HP_END_OF_INTERRUPT",
		.mprd_def = D_PCIE_CORE_SMU_HP_EOI
	},
	{
		.mprd_name = "PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR",
		.mprd_def = D_PCIE_CORE_SMU_INT_PIN_SHARING
	},
	{
		.mprd_name = "PCIECORE::PCIE_PGMST_CNTL",
		.mprd_def = D_PCIE_CORE_PGMST_CTL
	},
	{
		.mprd_name = "PCIECORE::PCIE_PGSLV_CNTL",
		.mprd_def = D_PCIE_CORE_PGSLV_CTL
	},
	{
		.mprd_name = "PCIECORE::SMU_PCIE_DF_Address",
		.mprd_def = D_PCIE_CORE_SMU_DF_ADDR
	},
	{
		.mprd_name = "PCIECORE::LC_CPM_CONTROL_0",
		.mprd_def = D_PCIE_CORE_LC_CPM_CTL0
	},
	{
		.mprd_name = "PCIECORE::LC_CPM_CONTROL_1",
		.mprd_def = D_PCIE_CORE_LC_CPM_CTL1
	},
	{
		.mprd_name = "PCIECORE::PCIE_RXMARGIN_CONTROL_CAPABILITIES",
		.mprd_def = D_PCIE_CORE_RX_MARGIN_CTL_CAP
	},
	{
		.mprd_name = "PCIECORE::PCIE_RXMARGIN_1_SETTINGS",
		.mprd_def = D_PCIE_CORE_RX_MARGIN1
	},
	{
		.mprd_name = "PCIECORE::PCIE_RXMARGIN_2_SETTINGS",
		.mprd_def = D_PCIE_CORE_RX_MARGIN2
	},
	{
		.mprd_name = "PCIECORE::PCIE_PRESENCE_DETECT_SELECT",
		.mprd_def = D_PCIE_CORE_PRES
	},
	{
		.mprd_name = "PCIECORE::PCIE_LC_DEBUG_CNTL",
		.mprd_def = D_PCIE_CORE_LC_DBG_CTL
	},
	{
		.mprd_name = "PCIECORE::SMU_PCIE_FENCED1_REG",
		.mprd_def = D_PCIE_CORE_SMU_FENCED1
	},
	{
		.mprd_name = "PCIECORE::SMU_PCIE_FENCED2_REG",
		.mprd_def = D_PCIE_CORE_SMU_FENCED2
	}
};
const size_t milan_pcie_core_dbg_nregs = ARRAY_SIZE(milan_pcie_core_dbg_regs);

const milan_pcie_reg_dbg_t milan_pcie_port_dbg_regs[] = {
	{
		.mprd_name = "PCIEPORT::PCIEP_HW_DEBUG",
		.mprd_def = D_PCIE_PORT_HW_DBG
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_HW_DEBUG_LC",
		.mprd_def = D_PCIE_PORT_HW_DBG_LC
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_PORT_CNTL",
		.mprd_def = D_PCIE_PORT_PCTL
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_SDP_CTRL",
		.mprd_def = D_PCIE_PORT_SDP_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CNTL",
		.mprd_def = D_PCIE_PORT_TX_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_REQUESTER_ID",
		.mprd_def = D_PCIE_PORT_TX_ID
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_VENDOR_SPECIFIC",
		.mprd_def = D_PCIE_PORT_TX_VS_DLLP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_REQUEST_NUM_CNTL",
		.mprd_def = D_PCIE_PORT_TX_REQ_NUM_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_SEQ",
		.mprd_def = D_PCIE_PORT_TX_SEQ
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_REPLAY",
		.mprd_def = D_PCIE_PORT_TX_REPLAY
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_ACK_LATENCY_LIMIT",
		.mprd_def = D_PCIE_PORT_TX_ACK_LAT_LIM
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_NOP_DLLP",
		.mprd_def = D_PCIE_PORT_TX_NOP_DLLP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CNTL_2",
		.mprd_def = D_PCIE_PORT_TX_CTL2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_P",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_P
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_NP",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_NP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_CPL",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_CPL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_P",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_INIT_P
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_NP",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_INIT_NP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_CPL",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_INIT_CPL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_STATUS",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_STATUS
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CREDITS_FCU_THRESHOLD",
		.mprd_def = D_PCIE_PORT_TX_CREDITS_FCU_THRESH
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CCIX_PORT_CNTL0",
		.mprd_def = D_PCIE_PORT_TX_CCIX_PORT_CTL0
	},
	{
		.mprd_name = "PCIEPORT::PCIE_TX_CCIX_PORT_CNTL1",
		.mprd_def = D_PCIE_PORT_TX_CCIX_PORT_CTL1
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_STACKED_BASE",
		.mprd_def = D_PCIE_PORT_CCIX_STACKED_BASE
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_STACKED_LIMIT",
		.mprd_def = D_PCIE_PORT_CCIX_STACKED_LIM
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_UPPER_ADDR",
		.mprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_HI
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_LOWER_ADDR",
		.mprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_LO
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_CTRL",
		.mprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_WR_UPPER_ADDR",
		.mprd_def = D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_HI
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_WR_LOWER_ADDR",
		.mprd_def = D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_LO
	},
	{
		.mprd_name = "PCIEPORT::PCIE_CCIX_MISC_STATUS",
		.mprd_def = D_PCIE_PORT_CCIX_MISC_STATUS
	},
	{
		.mprd_name = "PCIEPORT::PCIE_P_PORT_LANE_STATUS",
		.mprd_def = D_PCIE_PORT_P_LANE_STATUS
	},
	{
		.mprd_name = "PCIEPORT::PCIE_FC_P",
		.mprd_def = D_PCIE_PORT_FC_P
	},
	{
		.mprd_name = "PCIEPORT::PCIE_FC_NP",
		.mprd_def = D_PCIE_PORT_FC_NP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_FC_CPL",
		.mprd_def = D_PCIE_PORT_FC_CPL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_FC_P_VC1",
		.mprd_def = D_PCIE_PORT_FC_P_VC1
	},
	{
		.mprd_name = "PCIEPORT::PCIE_FC_NP_VC1",
		.mprd_def = D_PCIE_PORT_FC_P_VC1
	},
	{
		.mprd_name = "PCIEPORT::PCIE_FC_CPL_VC1",
		.mprd_def = D_PCIE_PORT_FC_CPL_VC1
	},
	{
		.mprd_name = "PCIEPORT::PCIE_ERR_CNTL",
		.mprd_def = D_PCIE_PORT_ERR_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_CNTL",
		.mprd_def = D_PCIE_PORT_RX_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_EXPECTED_SEQNUM",
		.mprd_def = D_PCIE_PORT_RX_EXP_SEQ
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_VENDOR_SPECIFIC",
		.mprd_def = D_PCIE_PORT_RX_VS_DLLP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_CNTL3",
		.mprd_def = D_PCIE_PORT_RX_CTL3
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_P",
		.mprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_P
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_NP",
		.mprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_NP
	},
	{
		.mprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_CPL",
		.mprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_CPL
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_ERROR_INJECT_PHYSICAL",
		.mprd_def = D_PCIE_PORT_ERR_INJ_PHYS
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_ERROR_INJECT_TRANSACTION",
		.mprd_def = D_PCIE_PORT_ERR_INJ_TXN
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_NAK_COUNTER",
		.mprd_def = D_PCIE_PORT_NAK_COUNTER
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_CTRL_STATUS",
		.mprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_CTL_STATUS
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_THRESHOLD_VALUES",
		.mprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_THRESH_VALS
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL",
		.mprd_def = D_PCIE_PORT_LC_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_TRAINING_CNTL",
		.mprd_def = D_PCIE_PORT_LC_TRAIN_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL",
		.mprd_def = D_PCIE_PORT_LC_WIDTH_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_N_FTS_CNTL",
		.mprd_def = D_PCIE_PORT_LC_NFTS_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_SPEED_CNTL",
		.mprd_def = D_PCIE_PORT_LC_SPEED_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_STATE0",
		.mprd_def = D_PCIE_PORT_LC_STATE0
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_STATE1",
		.mprd_def = D_PCIE_PORT_LC_STATE1
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_STATE2",
		.mprd_def = D_PCIE_PORT_LC_STATE2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_STATE3",
		.mprd_def = D_PCIE_PORT_LC_STATE3
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_STATE4",
		.mprd_def = D_PCIE_PORT_LC_STATE4
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_STATE5",
		.mprd_def = D_PCIE_PORT_LC_STATE5
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL2",
		.mprd_def = D_PCIE_PORT_LINK_MGMT_CTL2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL2",
		.mprd_def = D_PCIE_PORT_LC_CTL2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_BW_CHANGE_CNTL",
		.mprd_def = D_PCIE_PORT_LC_BW_CHANGE_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CDR_CNTL",
		.mprd_def = D_PCIE_PORT_LC_CDR_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_LANE_CNTL",
		.mprd_def = D_PCIE_PORT_LC_LANE_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL3",
		.mprd_def = D_PCIE_PORT_LC_CTL3
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL4",
		.mprd_def = D_PCIE_PORT_LC_CTL4
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL5",
		.mprd_def = D_PCIE_PORT_LC_CTL5
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF",
		.mprd_def = D_PCIE_PORT_LC_FORCE_COEFF
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_BEST_EQ_SETTINGS",
		.mprd_def = D_PCIE_PORT_LC_BEST_EQ
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF",
		.mprd_def = D_PCIE_PORT_LC_FORCE_EQ_COEFF
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL6",
		.mprd_def = D_PCIE_PORT_LC_CTL6
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL7",
		.mprd_def = D_PCIE_PORT_LC_CTL7
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_STATUS",
		.mprd_def = D_PCIE_PORT_LINK_MGMT_STATUS
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_MASK",
		.mprd_def = D_PCIE_PORT_LINK_MGMT_MASK
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL",
		.mprd_def = D_PCIE_PORT_LINK_MGMT_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_STRAP_LC",
		.mprd_def = D_PCIE_PORT_STRAP_LC
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_STRAP_MISC",
		.mprd_def = D_PCIE_PORT_STRAP_MISC
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_STRAP_LC2",
		.mprd_def = D_PCIE_PORT_STRAP_LC2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE",
		.mprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE2",
		.mprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_PORT_ORDER",
		.mprd_def = D_PCIE_PORT_LC_PORT_ORDER
	},
	{
		.mprd_name = "PCIEPORT::PCIE_BCH_ECC_CNTL",
		.mprd_def = D_PCIE_PORT_BCH_ECC_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_HPGI_PRIVATE",
		.mprd_def = D_PCIE_PORT_HPGI_PRIV
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_HPGI",
		.mprd_def = D_PCIE_PORT_HPGI
	},
	{
		.mprd_name = "PCIEPORT::PCIEP_HCNT_DESCRIPTOR",
		.mprd_def = D_PCIE_PORT_HP_CTL
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL8",
		.mprd_def = D_PCIE_PORT_LC_CTL8
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL9",
		.mprd_def = D_PCIE_PORT_LC_CTL9
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF2",
		.mprd_def = D_PCIE_PORT_LC_FORCE_COEFF2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF2",
		.mprd_def = D_PCIE_PORT_LC_FORCE_EQ_COEFF2
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_FINE_GRAIN_CLK_GATE_OVERRIDES",
		.mprd_def = D_PCIE_PORT_LC_FINE_GRAIN_CLK_GATE_OVR
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL10",
		.mprd_def = D_PCIE_PORT_LC_CTL10
	},
	{
		.mprd_name = "PCIEPORT::PCIE_LC_CNTL11",
		.mprd_def = D_PCIE_PORT_LC_CTL11
	}
};
const size_t milan_pcie_port_dbg_nregs = ARRAY_SIZE(milan_pcie_port_dbg_regs);

#endif	/* DEBUG */
