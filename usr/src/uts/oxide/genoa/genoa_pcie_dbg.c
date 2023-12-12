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
 * genoa_fabric.c:genoa_pcie_populate_xx_dbg().
 */

#include <sys/amdzen/smn.h>
#include <sys/io/genoa/pcie.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/sysmacros.h>

#ifdef	DEBUG

const genoa_pcie_reg_dbg_t genoa_pcie_core_dbg_regs[] = {
	{
		.gprd_name = "PCIECORE::PCIE_HW_DEBUG",
		.gprd_def = D_PCIE_CORE_HW_DBG
	},
	{
		.gprd_name = "PCIECORE::PCIE_HW_DEBUG_LC",
		.gprd_def = D_PCIE_CORE_HW_DBG_LC
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_NUM_NAK",
		.gprd_def = D_PCIE_CORE_RX_NUM_NAK
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_NUM_NAK_GENERATED",
		.gprd_def = D_PCIE_CORE_RX_NUM_NAK_GEN
	},
	{
		.gprd_name = "PCIECORE::PCIE_CNTL",
		.gprd_def = D_PCIE_CORE_PCIE_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_CONFIG_CNTL",
		.gprd_def = D_PCIE_CORE_CFG_CTL_CONFIG
	},
	{
		.gprd_name = "PCIECORE::PCIE_DEBUG_CNTL",
		.gprd_def = D_PCIE_CORE_DBG_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_CNTL2",
		.gprd_def = D_PCIE_CORE_PCIE_CTL2
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_CNTL2",
		.gprd_def = D_PCIE_CORE_RX_CTL2
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_F0_ATTR_CNTL",
		.gprd_def = D_PCIE_CORE_TX_F0_ATTR_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_CI_CNTL",
		.gprd_def = D_PCIE_CORE_CI_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_BUS_CNTL",
		.gprd_def = D_PCIE_CORE_BUS_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATE6",
		.gprd_def = D_PCIE_CORE_LC_STATE6
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATE7",
		.gprd_def = D_PCIE_CORE_LC_STATE7
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATE8",
		.gprd_def = D_PCIE_CORE_LC_STATE8
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATE9",
		.gprd_def = D_PCIE_CORE_LC_STATE9
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATE10",
		.gprd_def = D_PCIE_CORE_LC_STATE10
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATE11",
		.gprd_def = D_PCIE_CORE_LC_STATE11
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATUS1",
		.gprd_def = D_PCIE_CORE_LC_STATUS1
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_STATUS2",
		.gprd_def = D_PCIE_CORE_LC_STATUS2
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_CNTL3",
		.gprd_def = D_PCIE_CORE_TX_CTL3
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_STATUS",
		.gprd_def = D_PCIE_CORE_TX_STATUS
	},
	{
		.gprd_name = "PCIECORE::PCIE_WPR_CNTL",
		.gprd_def = D_PCIE_CORE_WPR_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_LAST_TLP0",
		.gprd_def = D_PCIE_CORE_RX_LAST_TLP0
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_LAST_TLP1",
		.gprd_def = D_PCIE_CORE_RX_LAST_TLP1
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_LAST_TLP2",
		.gprd_def = D_PCIE_CORE_RX_LAST_TLP2
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_LAST_TLP3",
		.gprd_def = D_PCIE_CORE_RX_LAST_TLP3
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_LAST_TLP0",
		.gprd_def = D_PCIE_CORE_TX_LAST_TLP0
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_LAST_TLP1",
		.gprd_def = D_PCIE_CORE_TX_LAST_TLP1
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_LAST_TLP2",
		.gprd_def = D_PCIE_CORE_TX_LAST_TLP2
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_LAST_TLP3",
		.gprd_def = D_PCIE_CORE_TX_LAST_TLP3
	},
	{
		.gprd_name = "PCIECORE::PCIE_I2C_REG_ADDR_EXPAND",
		.gprd_def = D_PCIE_CORE_I2C_ADDR
	},
	{
		.gprd_name = "PCIECORE::PCIE_I2C_REG_DATA",
		.gprd_def = D_PCIE_CORE_I2C_DATA
	},
	{
		.gprd_name = "PCIECORE::PCIE_CFG_CNTL",
		.gprd_def = D_PCIE_CORE_CFG_CTL_CFG
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_PM_CNTL",
		.gprd_def = D_PCIE_CORE_LC_PM_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_PORT_ORDER_CNTL",
		.gprd_def = D_PCIE_CORE_LC_PORT_ORDER_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_P_CNTL",
		.gprd_def = D_PCIE_CORE_PCIE_P_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_P_BUF_STATUS",
		.gprd_def = D_PCIE_CORE_P_BUF_STATUS
	},
	{
		.gprd_name = "PCIECORE::PCIE_P_DECODER_STATUS",
		.gprd_def = D_PCIE_CORE_P_DECODER_STATUS
	},
	{
		.gprd_name = "PCIECORE::PCIE_P_MISC_STATUS",
		.gprd_def = D_PCIE_CORE_P_MISC_STATUS
	},
	{
		.gprd_name = "PCIECORE::PCIE_P_RX_L0S_FTS",
		.gprd_def = D_PCIE_CORE_P_RX_L0S_FTS
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_CCIX_CNTL0",
		.gprd_def = D_PCIE_CORE_TX_CCIX_CTL0
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_CCIX_CNTL1",
		.gprd_def = D_PCIE_CORE_TX_CCIX_CTL1
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_CCIX_PORT_MAP",
		.gprd_def = D_PCIE_CORE_TX_CCIX_PORT_MAP
	},
	{
		.gprd_name = "PCIECORE::PCIE_TX_CCIX_ERR_CTL",
		.gprd_def = D_PCIE_CORE_TX_CCIX_ERR_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_CCIX_CTL0",
		.gprd_def = D_PCIE_CORE_RX_CCIX_CTL0
	},
	{
		.gprd_name = "PCIECORE::PCIE_RX_AD",
		.gprd_def = D_PCIE_CORE_RX_AD
	},
	{
		.gprd_name = "PCIECORE::PCIE_SDP_CTRL",
		.gprd_def = D_PCIE_CORE_SDP_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_NBIO_CLKREQb_MAP_CNTL",
		.gprd_def = D_PCIE_CORE_NBIO_CLKREQ_B_MAP_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_SDP_RC_SLV_ATTR_CTRL",
		.gprd_def = D_PCIE_CORE_SDP_RC_SLV_ATTR_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_STRAP_F0",
		.gprd_def = D_PCIE_CORE_STRAP_F0
	},
	{
		.gprd_name = "PCIECORE::PCIE_STRAP_NTB",
		.gprd_def = D_PCIE_CORE_STRAP_NTB
	},
	{
		.gprd_name = "PCIECORE::PCIE_STRAP_MISC",
		.gprd_def = D_PCIE_CORE_STRAP_MISC
	},
	{
		.gprd_name = "PCIECORE::PCIE_STRAP_MISC2",
		.gprd_def = D_PCIE_CORE_STRAP_MISC2
	},
	{
		.gprd_name = "PCIECORE::PCIE_STRAP_PI",
		.gprd_def = D_PCIE_CORE_STRAP_PI
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_CLR",
		.gprd_def = D_PCIE_CORE_PRBS_CLR
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_STATUS1",
		.gprd_def = D_PCIE_CORE_PRBS_STATUS1
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_STATUS2",
		.gprd_def = D_PCIE_CORE_PRBS_STATUS2
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_FREERUN",
		.gprd_def = D_PCIE_CORE_PRBS_FREERUN
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_MISC",
		.gprd_def = D_PCIE_CORE_PRBS_MISC
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_USER_PATTERN",
		.gprd_def = D_PCIE_CORE_PRBS_USER_PATTERN
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_LO_BITCNT",
		.gprd_def = D_PCIE_CORE_PRBS_LO_BITCNT
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_HI_BITCNT",
		.gprd_def = D_PCIE_CORE_PRBS_HI_BITCNT
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT0",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT0
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT1",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT1
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT2",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT2
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT3",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT3
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT4",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT4
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT5",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT5
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT6",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT6
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT7",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT7
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT8",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT8
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT9",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT9
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT10",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT10
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT11",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT11
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT12",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT12
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT13",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT13
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT14",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT14
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRBS_ERRCNT15",
		.gprd_def = D_PCIE_CORE_PRBS_ERRCNT15
	},
	{
		.gprd_name = "PCIECORE::SWRST_COMMAND_STATUS",
		.gprd_def = D_PCIE_CORE_SWRST_CMD_STATUS
	},
	{
		.gprd_name = "PCIECORE::SWRST_GENERAL_CONTROL",
		.gprd_def = D_PCIE_CORE_SWRST_GEN_CTL
	},
	{
		.gprd_name = "PCIECORE::SWRST_COMMAND_0",
		.gprd_def = D_PCIE_CORE_SWRST_CMD0
	},
	{
		.gprd_name = "PCIECORE::SWRST_COMMAND_1",
		.gprd_def = D_PCIE_CORE_SWRST_CMD1
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_0",
		.gprd_def = D_PCIE_CORE_SWRST_CTL0
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_1",
		.gprd_def = D_PCIE_CORE_SWRST_CTL1
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_2",
		.gprd_def = D_PCIE_CORE_SWRST_CTL2
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_3",
		.gprd_def = D_PCIE_CORE_SWRST_CTL3
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_4",
		.gprd_def = D_PCIE_CORE_SWRST_CTL4
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_5",
		.gprd_def = D_PCIE_CORE_SWRST_CTL5
	},
	{
		.gprd_name = "PCIECORE::SWRST_CONTROL_6",
		.gprd_def = D_PCIE_CORE_SWRST_CTL6
	},
	{
		.gprd_name = "PCIECORE::CPM_CONTROL",
		.gprd_def = D_PCIE_CORE_CPM_CTL
	},
	{
		.gprd_name = "PCIECORE::CPM_SPLIT_CONTROL",
		.gprd_def = D_PCIE_CORE_CPM_SPLIT_CTL
	},
	{
		.gprd_name = "PCIECORE::SMN_APERTURE_A",
		.gprd_def = D_PCIE_CORE_SMN_APERTURE_A
	},
	{
		.gprd_name = "PCIECORE::SMN_APERTURE_B",
		.gprd_def = D_PCIE_CORE_SMN_APERTURE_B
	},
	{
		.gprd_name = "PCIECORE::RSMU_MASTER_CONTROL",
		.gprd_def = D_PCIE_CORE_RSMU_MASTER_CTL
	},
	{
		.gprd_name = "PCIECORE::RSMU_SLAVE_CONTROL",
		.gprd_def = D_PCIE_CORE_RSMU_SLAVE_CTL
	},
	{
		.gprd_name = "PCIECORE::RSMU_POWER_GATING_CONTROL",
		.gprd_def = D_PCIE_CORE_RSMU_PWR_GATE_CTL
	},
	{
		.gprd_name = "PCIECORE::RSMU_BIOS_TIMER_CMD",
		.gprd_def = D_PCIE_CORE_RSMU_TIMER_CMD
	},
	{
		.gprd_name = "PCIECORE::RSMU_BIOS_TIMER_CNTL",
		.gprd_def = D_PCIE_CORE_RSMU_TIMER_CTL
	},
	{
		.gprd_name = "PCIECORE::RSMU_BIOS_TIMER_DEBUG",
		.gprd_def = D_PCIE_CORE_RSMU_TIMER_DBG
	},
	{
		.gprd_name = "PCIECORE::LNCNT_CONTROL",
		.gprd_def = D_PCIE_CORE_LNCNT_CTL
	},
	{
		.gprd_name = "PCIECORE::LNCNT_QUAN_THRD",
		.gprd_def = D_PCIE_CORE_LNCNT_QUAN_THRD
	},
	{
		.gprd_name = "PCIECORE::LNCNT_WEIGHT",
		.gprd_def = D_PCIE_CORE_LNCNT_WEIGHT
	},
	{
		.gprd_name = "PCIECORE::SMU_HP_STATUS_UPDATE",
		.gprd_def = D_PCIE_CORE_SMU_HP_STATUS_UPDATE
	},
	{
		.gprd_name = "PCIECORE::HP_SMU_COMMAND_UPDATE",
		.gprd_def = D_PCIE_CORE_HP_SMU_CMD_UPDATE
	},
	{
		.gprd_name = "PCIECORE::SMU_HP_END_OF_INTERRUPT",
		.gprd_def = D_PCIE_CORE_SMU_HP_EOI
	},
	{
		.gprd_name = "PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR",
		.gprd_def = D_PCIE_CORE_SMU_INT_PIN_SHARING
	},
	{
		.gprd_name = "PCIECORE::PCIE_PGMST_CNTL",
		.gprd_def = D_PCIE_CORE_PGMST_CTL
	},
	{
		.gprd_name = "PCIECORE::PCIE_PGSLV_CNTL",
		.gprd_def = D_PCIE_CORE_PGSLV_CTL
	},
	{
		.gprd_name = "PCIECORE::SMU_PCIE_DF_Address",
		.gprd_def = D_PCIE_CORE_SMU_DF_ADDR
	},
	{
		.gprd_name = "PCIECORE::LC_CPM_CONTROL_0",
		.gprd_def = D_PCIE_CORE_LC_CPM_CTL0
	},
	{
		.gprd_name = "PCIECORE::LC_CPM_CONTROL_1",
		.gprd_def = D_PCIE_CORE_LC_CPM_CTL1
	},
	{
		.gprd_name = "PCIECORE::PCIE_RXMARGIN_CONTROL_CAPABILITIES",
		.gprd_def = D_PCIE_CORE_RX_MARGIN_CTL_CAP
	},
	{
		.gprd_name = "PCIECORE::PCIE_RXMARGIN_1_SETTINGS",
		.gprd_def = D_PCIE_CORE_RX_MARGIN1
	},
	{
		.gprd_name = "PCIECORE::PCIE_RXMARGIN_2_SETTINGS",
		.gprd_def = D_PCIE_CORE_RX_MARGIN2
	},
	{
		.gprd_name = "PCIECORE::PCIE_PRESENCE_DETECT_SELECT",
		.gprd_def = D_PCIE_CORE_PRES
	},
	{
		.gprd_name = "PCIECORE::PCIE_LC_DEBUG_CNTL",
		.gprd_def = D_PCIE_CORE_LC_DBG_CTL
	},
	{
		.gprd_name = "PCIECORE::SMU_PCIE_FENCED1_REG",
		.gprd_def = D_PCIE_CORE_SMU_FENCED1
	},
	{
		.gprd_name = "PCIECORE::SMU_PCIE_FENCED2_REG",
		.gprd_def = D_PCIE_CORE_SMU_FENCED2
	}
};
const size_t genoa_pcie_core_dbg_nregs = ARRAY_SIZE(genoa_pcie_core_dbg_regs);

const genoa_pcie_reg_dbg_t genoa_pcie_port_dbg_regs[] = {
	{
		.gprd_name = "PCIEPORT::PCIEP_HW_DEBUG",
		.gprd_def = D_PCIE_PORT_HW_DBG
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_HW_DEBUG_LC",
		.gprd_def = D_PCIE_PORT_HW_DBG_LC
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_PORT_CNTL",
		.gprd_def = D_PCIE_PORT_PCTL
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_SDP_CTRL",
		.gprd_def = D_PCIE_PORT_SDP_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CNTL",
		.gprd_def = D_PCIE_PORT_TX_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_REQUESTER_ID",
		.gprd_def = D_PCIE_PORT_TX_ID
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_VENDOR_SPECIFIC",
		.gprd_def = D_PCIE_PORT_TX_VS_DLLP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_REQUEST_NUM_CNTL",
		.gprd_def = D_PCIE_PORT_TX_REQ_NUM_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_SEQ",
		.gprd_def = D_PCIE_PORT_TX_SEQ
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_REPLAY",
		.gprd_def = D_PCIE_PORT_TX_REPLAY
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_ACK_LATENCY_LIMIT",
		.gprd_def = D_PCIE_PORT_TX_ACK_LAT_LIM
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_NOP_DLLP",
		.gprd_def = D_PCIE_PORT_TX_NOP_DLLP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CNTL_2",
		.gprd_def = D_PCIE_PORT_TX_CTL2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_P",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_P
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_NP",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_NP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_ADVT_CPL",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_ADVT_CPL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_P",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_INIT_P
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_NP",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_INIT_NP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_INIT_CPL",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_INIT_CPL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_STATUS",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_STATUS
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CREDITS_FCU_THRESHOLD",
		.gprd_def = D_PCIE_PORT_TX_CREDITS_FCU_THRESH
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CCIX_PORT_CNTL0",
		.gprd_def = D_PCIE_PORT_TX_CCIX_PORT_CTL0
	},
	{
		.gprd_name = "PCIEPORT::PCIE_TX_CCIX_PORT_CNTL1",
		.gprd_def = D_PCIE_PORT_TX_CCIX_PORT_CTL1
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_STACKED_BASE",
		.gprd_def = D_PCIE_PORT_CCIX_STACKED_BASE
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_STACKED_LIMIT",
		.gprd_def = D_PCIE_PORT_CCIX_STACKED_LIM
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_UPPER_ADDR",
		.gprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_HI
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_LOWER_ADDR",
		.gprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_LO
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_RD_CTRL",
		.gprd_def = D_PCIE_PORT_CCIX_DUMMY_RD_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_WR_UPPER_ADDR",
		.gprd_def = D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_HI
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_DUMMY_WR_LOWER_ADDR",
		.gprd_def = D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_LO
	},
	{
		.gprd_name = "PCIEPORT::PCIE_CCIX_MISC_STATUS",
		.gprd_def = D_PCIE_PORT_CCIX_MISC_STATUS
	},
	{
		.gprd_name = "PCIEPORT::PCIE_P_PORT_LANE_STATUS",
		.gprd_def = D_PCIE_PORT_P_LANE_STATUS
	},
	{
		.gprd_name = "PCIEPORT::PCIE_FC_P",
		.gprd_def = D_PCIE_PORT_FC_P
	},
	{
		.gprd_name = "PCIEPORT::PCIE_FC_NP",
		.gprd_def = D_PCIE_PORT_FC_NP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_FC_CPL",
		.gprd_def = D_PCIE_PORT_FC_CPL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_FC_P_VC1",
		.gprd_def = D_PCIE_PORT_FC_P_VC1
	},
	{
		.gprd_name = "PCIEPORT::PCIE_FC_NP_VC1",
		.gprd_def = D_PCIE_PORT_FC_P_VC1
	},
	{
		.gprd_name = "PCIEPORT::PCIE_FC_CPL_VC1",
		.gprd_def = D_PCIE_PORT_FC_CPL_VC1
	},
	{
		.gprd_name = "PCIEPORT::PCIE_ERR_CNTL",
		.gprd_def = D_PCIE_PORT_ERR_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_CNTL",
		.gprd_def = D_PCIE_PORT_RX_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_EXPECTED_SEQNUM",
		.gprd_def = D_PCIE_PORT_RX_EXP_SEQ
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_VENDOR_SPECIFIC",
		.gprd_def = D_PCIE_PORT_RX_VS_DLLP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_CNTL3",
		.gprd_def = D_PCIE_PORT_RX_CTL3
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_P",
		.gprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_P
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_NP",
		.gprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_NP
	},
	{
		.gprd_name = "PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_CPL",
		.gprd_def = D_PCIE_PORT_RX_CREDITS_ALLOC_CPL
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_ERROR_INJECT_PHYSICAL",
		.gprd_def = D_PCIE_PORT_ERR_INJ_PHYS
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_ERROR_INJECT_TRANSACTION",
		.gprd_def = D_PCIE_PORT_ERR_INJ_TXN
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_NAK_COUNTER",
		.gprd_def = D_PCIE_PORT_NAK_COUNTER
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_CTRL_STATUS",
		.gprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_CTL_STATUS
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_THRESHOLD_VALUES",
		.gprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_THRESH_VALS
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL",
		.gprd_def = D_PCIE_PORT_LC_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_TRAINING_CNTL",
		.gprd_def = D_PCIE_PORT_LC_TRAIN_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL",
		.gprd_def = D_PCIE_PORT_LC_WIDTH_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_N_FTS_CNTL",
		.gprd_def = D_PCIE_PORT_LC_NFTS_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_SPEED_CNTL",
		.gprd_def = D_PCIE_PORT_LC_SPEED_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_STATE0",
		.gprd_def = D_PCIE_PORT_LC_STATE0
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_STATE1",
		.gprd_def = D_PCIE_PORT_LC_STATE1
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_STATE2",
		.gprd_def = D_PCIE_PORT_LC_STATE2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_STATE3",
		.gprd_def = D_PCIE_PORT_LC_STATE3
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_STATE4",
		.gprd_def = D_PCIE_PORT_LC_STATE4
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_STATE5",
		.gprd_def = D_PCIE_PORT_LC_STATE5
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL2",
		.gprd_def = D_PCIE_PORT_LINK_MGMT_CTL2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL2",
		.gprd_def = D_PCIE_PORT_LC_CTL2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_BW_CHANGE_CNTL",
		.gprd_def = D_PCIE_PORT_LC_BW_CHANGE_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CDR_CNTL",
		.gprd_def = D_PCIE_PORT_LC_CDR_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_LANE_CNTL",
		.gprd_def = D_PCIE_PORT_LC_LANE_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL3",
		.gprd_def = D_PCIE_PORT_LC_CTL3
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL4",
		.gprd_def = D_PCIE_PORT_LC_CTL4
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL5",
		.gprd_def = D_PCIE_PORT_LC_CTL5
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF",
		.gprd_def = D_PCIE_PORT_LC_FORCE_COEFF
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_BEST_EQ_SETTINGS",
		.gprd_def = D_PCIE_PORT_LC_BEST_EQ
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF",
		.gprd_def = D_PCIE_PORT_LC_FORCE_EQ_COEFF
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL6",
		.gprd_def = D_PCIE_PORT_LC_CTL6
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL7",
		.gprd_def = D_PCIE_PORT_LC_CTL7
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_STATUS",
		.gprd_def = D_PCIE_PORT_LINK_MGMT_STATUS
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_MASK",
		.gprd_def = D_PCIE_PORT_LINK_MGMT_MASK
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL",
		.gprd_def = D_PCIE_PORT_LINK_MGMT_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_STRAP_LC",
		.gprd_def = D_PCIE_PORT_STRAP_LC
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_STRAP_MISC",
		.gprd_def = D_PCIE_PORT_STRAP_MISC
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_STRAP_LC2",
		.gprd_def = D_PCIE_PORT_STRAP_LC2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE",
		.gprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE2",
		.gprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_PORT_ORDER",
		.gprd_def = D_PCIE_PORT_LC_PORT_ORDER
	},
	{
		.gprd_name = "PCIEPORT::PCIE_BCH_ECC_CNTL",
		.gprd_def = D_PCIE_PORT_BCH_ECC_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_HPGI_PRIVATE",
		.gprd_def = D_PCIE_PORT_HPGI_PRIV
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_HPGI",
		.gprd_def = D_PCIE_PORT_HPGI
	},
	{
		.gprd_name = "PCIEPORT::PCIEP_HCNT_DESCRIPTOR",
		.gprd_def = D_PCIE_PORT_HP_CTL
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL8",
		.gprd_def = D_PCIE_PORT_LC_CTL8
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL9",
		.gprd_def = D_PCIE_PORT_LC_CTL9
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF2",
		.gprd_def = D_PCIE_PORT_LC_FORCE_COEFF2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF2",
		.gprd_def = D_PCIE_PORT_LC_FORCE_EQ_COEFF2
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_FINE_GRAIN_CLK_GATE_OVERRIDES",
		.gprd_def = D_PCIE_PORT_LC_FINE_GRAIN_CLK_GATE_OVR
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL10",
		.gprd_def = D_PCIE_PORT_LC_CTL10
	},
	{
		.gprd_name = "PCIEPORT::PCIE_LC_CNTL11",
		.gprd_def = D_PCIE_PORT_LC_CTL11
	}
};
const size_t genoa_pcie_port_dbg_nregs = ARRAY_SIZE(genoa_pcie_port_dbg_regs);

#endif	/* DEBUG */
