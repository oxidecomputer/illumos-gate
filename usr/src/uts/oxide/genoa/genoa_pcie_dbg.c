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
 * List of PCIe core and port space registers to capture during debugging.
 * These are used only on DEBUG kernels and are embedded in the constant
 * genoa_platform definition and consumed by code in zen/zen_fabric.c
 */

#include <sys/amdzen/smn.h>
#include <sys/io/genoa/pcie.h>
#include <sys/io/genoa/pcie_impl.h>

#ifdef	DEBUG

const zen_pcie_reg_dbg_t genoa_pcie_core_dbg_regs[] = {
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG",
		.zprd_def = D_PCIE_CORE_HW_DBG
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_LC",
		.zprd_def = D_PCIE_CORE_HW_DBG_LC
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_TX",
		.zprd_def = D_PCIE_CORE_HW_DBG_TX
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_LCRXP",
		.zprd_def = D_PCIE_CORE_HW_DBG_LCRXP
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_TXRX_PORT",
		.zprd_def = D_PCIE_CORE_HW_DBG_TXRX_PORT
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_TXLC_PORT",
		.zprd_def = D_PCIE_CORE_HW_DBG_TXLC_PORT
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_RXTX_PORT",
		.zprd_def = D_PCIE_CORE_HW_DBG_RXTX_PORT
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_RXLC_PORT",
		.zprd_def = D_PCIE_CORE_HW_DBG_RXLC_PORT
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_LCTX_PORT",
		.zprd_def = D_PCIE_CORE_HW_DBG_LCTX_PORT
	},
	{
		.zprd_name = "PCIECORE::PCIE_HW_DEBUG_LCRX_PORT",
		.zprd_def = D_PCIE_CORE_HW_DBG_LCRX_PORT
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
		.zprd_name = "PCIECORE::PCIE_CXL_ERR_AER_CTRL",
		.zprd_def = D_PCIE_CORE_CXL_ERR_AER_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_CNTL5",
		.zprd_def = D_PCIE_CORE_RX_CTL5
	},
	{
		.zprd_name = "PCIECORE::PCIE_RX_CNTL4",
		.zprd_def = D_PCIE_CORE_RX_CTL4
	},
	{
		.zprd_name = "PCIECORE::PCIE_COMMON_AER_MASK",
		.zprd_def = D_PCIE_CORE_COMMON_AER_MASK
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
		.zprd_name = "PCIECORE::PCIE_Z10_DEBUG",
		.zprd_def = D_PCIE_CORE_Z10_DBG
	},
	{
		.zprd_name = "PCIECORE::PCIE_SLV_CTRL_1",
		.zprd_def = D_PCIE_CORE_SLV_CTL1
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
		.zprd_def = D_PCIE_CORE_LC_STS1
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STATUS2",
		.zprd_def = D_PCIE_CORE_LC_STS2
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_ARBMUX_CNTL7",
		.zprd_def = D_PCIE_CORE_LC_ARBMUX_CTL7
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_ARBMUX_CNTL8",
		.zprd_def = D_PCIE_CORE_LC_ARBMUX_CTL8
	},
	{
		.zprd_name = "PCIECORE::PCIE_CREDIT_RELEASE",
		.zprd_def = D_PCIE_CORE_CREDIT_RELEASE
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
		.zprd_name =
		    "PCIECORE::PCIE_SDP_SLV_WRRSP_EXPECTED_CTRL_STATUS",
		.zprd_def = D_PCIE_CORE_SDP_SLV_WRRSP_EXP_CTL_STS
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
		.zprd_name = "PCIECORE::PCIE_LC_PM_CNTL2",
		.zprd_def = D_PCIE_CORE_LC_PM_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_STRAP_BUFF_CNTL",
		.zprd_def = D_PCIE_CORE_LC_STRAP_BUFF_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_CNTL",
		.zprd_def = D_PCIE_CORE_PCIE_P_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_BUF_STATUS",
		.zprd_def = D_PCIE_CORE_P_BUF_STS
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_DECODER_STATUS",
		.zprd_def = D_PCIE_CORE_P_DECODER_STS
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_MISC_STATUS",
		.zprd_def = D_PCIE_CORE_P_MISC_STS
	},
	{
		.zprd_name = "PCIECORE::PCIE_P_RCV_L0S_FTS_DET",
		.zprd_def = D_PCIE_CORE_P_RX_L0S_FTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_SFI_CAM_BY_UNITID_RX",
		.zprd_def = D_PCIE_CORE_SFI_CAM_BY_UNITID_RX
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_DF_ADDRESS",
		.zprd_def = D_PCIE_CORE_SMU_PCIE_DF_ADDRESS
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_DF_ADDRESS_2",
		.zprd_def = D_PCIE_CORE_SMU_PCIE_DF_ADDRESS2
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
		.zprd_name = "PCIECORE::NBIO_CLKREQb_MAP_CNTL2",
		.zprd_def = D_PCIE_CORE_NBIO_CLKREQ_B_MAP_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_SDP_CTRL2",
		.zprd_def = D_PCIE_CORE_SDP_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_SDP_CTRL_3",
		.zprd_def = D_PCIE_CORE_SDP_CTL3
	},
	{
		.zprd_name = "PCIECORE::PCIE_ERR_INJECT_MODE",
		.zprd_def = D_PCIE_CORE_ERR_INJ_MODE
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_HDR0",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_HDR0
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_HDR1",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_HDR1
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_HDR2",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_HDR2
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_HDR3",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_HDR3
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX0",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_PREFIX0
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX1",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_PREFIX1
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX2",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_PREFIX2
	},
	{
		.zprd_name = "PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX3",
		.zprd_def = D_PCIE_CORE_AER_ERR_INJ_PREFIX3
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
		.zprd_def = D_PCIE_CORE_PRBS_STS1
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_STATUS2",
		.zprd_def = D_PCIE_CORE_PRBS_STS2
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
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_0",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT0
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_1",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT1
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_2",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT2
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_3",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT3
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_4",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT4
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_5",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT5
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_6",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT6
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_7",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT7
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_8",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT8
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_9",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT9
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_10",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT10
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_11",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT11
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_12",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT12
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_13",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT13
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_14",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT14
	},
	{
		.zprd_name = "PCIECORE::PCIE_PRBS_ERRCNT_15",
		.zprd_def = D_PCIE_CORE_PRBS_ERRCNT15
	},
	{
		.zprd_name = "PCIECORE::SWRST_COMMAND_STATUS",
		.zprd_def = D_PCIE_CORE_SWRST_CMD_STS
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
		.zprd_name = "PCIECORE::CPM_CONTROL_EXT",
		.zprd_def = D_PCIE_CORE_CPM_CTL_EXT
	},
	{
		.zprd_name = "PCIECORE::SMN_APERTURE_ID_A",
		.zprd_def = D_PCIE_CORE_SMN_APERTURE_A
	},
	{
		.zprd_name = "PCIECORE::SMN_APERTURE_ID_B",
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
		.zprd_name = "PCIECORE::SMU_HP_STATUS_UPDATE",
		.zprd_def = D_PCIE_CORE_SMU_HP_STS_UPDATE
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
		.zprd_name = "PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR_TWO",
		.zprd_def = D_PCIE_CORE_SMU_INT_PIN_SHARING2
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE0_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE0_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE1_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE1_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE2_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE2_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE3_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE3_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE4_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE4_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE5_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE5_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE6_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE6_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE7_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE7_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE8_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE8_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE9_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE9_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE10_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE10_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE11_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE11_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE12_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE12_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE13_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE13_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE14_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE14_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE15_MAPPING",
		.zprd_def = D_PCIE_CORE_PHYS_LANE15_MAP
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE0_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE0_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE1_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE1_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE2_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE2_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE3_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE3_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE4_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE4_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE5_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE5_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE6_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE6_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE7_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE7_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE8_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE8_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE9_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE9_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE10_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE10_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE11_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE11_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE12_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE12_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE13_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE13_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE14_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE14_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_PHYSICAL_LANE15_MAPPING_STATUS",
		.zprd_def = D_PCIE_CORE_PHYS_LANE15_MAPSTS
	},
	{
		.zprd_name = "PCIECORE::PCIE_LC_DESKEW_CNTL",
		.zprd_def = D_PCIE_CORE_LC_DESKEW_CTL
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
		.zprd_name = "PCIECORE::PCIE_TX_TRACKING_ADDR_LO",
		.zprd_def = D_PCIE_CORE_TX_TRK_ADDR_LO
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_TRACKING_ADDR_HI",
		.zprd_def = D_PCIE_CORE_TX_TRK_ADDR_HI
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_TRACKING_CTRL_STATUS",
		.zprd_def = D_PCIE_CORE_TX_TRK_CTL_STS
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_POWER_CTRL_1",
		.zprd_def = D_PCIE_CORE_TX_PWR_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CTRL_1",
		.zprd_def = D_PCIE_CORE_PCIE_TX_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CTRL_2",
		.zprd_def = D_PCIE_CORE_PCIE_TX_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CTRL_3",
		.zprd_def = D_PCIE_CORE_PCIE_TX_CTL3
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_CTRL_4",
		.zprd_def = D_PCIE_CORE_PCIE_TX_CTL4
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_STATUS",
		.zprd_def = D_PCIE_CORE_TX_STS
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_F0_ATTR_CNTL",
		.zprd_def = D_PCIE_CORE_TX_F0_ATTR_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_SWUS_ATTR_CNTL",
		.zprd_def = D_PCIE_CORE_TX_SWUS_ATTR_CTL
	},
	{
		.zprd_name = "PCIECORE::PCIE_TX_ERR_CTRL_1",
		.zprd_def = D_PCIE_CORE_TX_ERR_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_BW_BY_UNITID",
		.zprd_def = D_PCIE_CORE_BW_BY_UNITID
	},
	{
		.zprd_name = "PCIECORE::PCIE_SFI_CAM_BY_UNITID",
		.zprd_def = D_PCIE_CORE_SFI_CAM_BY_UNITID
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_CTRL_1",
		.zprd_def = D_PCIE_CORE_MST_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_CTRL_2",
		.zprd_def = D_PCIE_CORE_MST_CTL2
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_CTRL_3",
		.zprd_def = D_PCIE_CORE_MST_CTL3
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_CTRL_4",
		.zprd_def = D_PCIE_CORE_MST_CTL4
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_ERR_CTRL_1",
		.zprd_def = D_PCIE_CORE_MST_ERR_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_ERR_STATUS_1",
		.zprd_def = D_PCIE_CORE_MST_ERR_STS1
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_DEBUG_CNTL_1",
		.zprd_def = D_PCIE_CORE_MST_DBG_CTL1
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG0",
		.zprd_def = D_PCIE_CORE_HIP_REG0
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG1",
		.zprd_def = D_PCIE_CORE_HIP_REG1
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG2",
		.zprd_def = D_PCIE_CORE_HIP_REG2
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG3",
		.zprd_def = D_PCIE_CORE_HIP_REG3
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG4",
		.zprd_def = D_PCIE_CORE_HIP_REG4
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG5",
		.zprd_def = D_PCIE_CORE_HIP_REG5
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG6",
		.zprd_def = D_PCIE_CORE_HIP_REG6
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG7",
		.zprd_def = D_PCIE_CORE_HIP_REG7
	},
	{
		.zprd_name = "PCIECORE::PCIE_HIP_REG8",
		.zprd_def = D_PCIE_CORE_HIP_REG8
	},
	{
		.zprd_name = "PCIECORE::PCIE_MST_STATUS",
		.zprd_def = D_PCIE_CORE_MST_STS
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_FENCED1_REG",
		.zprd_def = D_PCIE_CORE_SMU_FENCED1
	},
	{
		.zprd_name = "PCIECORE::SMU_PCIE_FENCED2_REG",
		.zprd_def = D_PCIE_CORE_SMU_FENCED2
	},
	{
		.zprd_name = "PCIECORE::PCIE_PERF_CNTL1_EVENT_CI_PORT_SEL",
		.zprd_def = D_PCIE_CORE_PERF_CTL1_EV_CI_PORT_SEL
	},
	{
		.zprd_name = "PCIECORE::PCIE_PERF_CNTL1_EVENT_TX_PORT_SEL",
		.zprd_def = D_PCIE_CORE_PERF_CTL1_EV_TX_PORT_SEL
	},
	{
		.zprd_name = "PCIECORE::PCIE_LANE_ERROR_COUNTERS_0",
		.zprd_def = D_PCIE_CORE_LANE_ERR_CNTRS0
	},
	{
		.zprd_name = "PCIECORE::PCIE_LANE_ERROR_COUNTERS_1",
		.zprd_def = D_PCIE_CORE_LANE_ERR_CNTRS1
	},
	{
		.zprd_name = "PCIECORE::PCIE_LANE_ERROR_COUNTERS_2",
		.zprd_def = D_PCIE_CORE_LANE_ERR_CNTRS2
	},
	{
		.zprd_name = "PCIECORE::PCIE_LANE_ERROR_COUNTERS_3",
		.zprd_def = D_PCIE_CORE_LANE_ERR_CNTRS3
	},
	{
		.zprd_name = "PCIECORE::RXP_ERROR_MASK_CNTL",
		.zprd_def = D_PCIE_CORE_RXP_ERR_MASK_CTL
	},
	{
		.zprd_name =
		    "PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR_THREE",
		.zprd_def = D_PCIE_CORE_SMU_INT_PIN_SHARING3
	},
};

const zen_pcie_reg_dbg_t genoa_pcie_port_dbg_regs[] = {
	{
		.zprd_name = "PCIEPORT::PCIEP_HW_DEBUG",
		.zprd_def = D_PCIE_PORT_HW_DBG
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_HW_DEBUG_LC",
		.zprd_def = D_PCIE_PORT_HW_DBG_LC
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_HW_DEBUG_TX",
		.zprd_def = D_PCIE_PORT_HW_DBG_TX
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
		.zprd_name = "PCIEPORT::PCIEP_RX_EXT_CAP_AUTO_CONTROL",
		.zprd_def = D_PCIE_PORT_RX_EXT_CAP_AUTO_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_PRIV_MSI_CTRL",
		.zprd_def = D_PCIE_PORT_PRIV_MSI_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_REQUESTER_ID",
		.zprd_def = D_PCIE_PORT_TX_ID
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_SKID_CTRL",
		.zprd_def = D_PCIE_PORT_TX_SKID_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_SKID_CLKSW_CTRL",
		.zprd_def = D_PCIE_PORT_TX_SKID_CLKSW_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_P_PORT_LANE_STATUS",
		.zprd_def = D_PCIE_PORT_P_LANE_STS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_ERR_CNTL",
		.zprd_def = D_PCIE_PORT_ERR_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_STRAP_RX_TILE1",
		.zprd_def = D_PCIE_PORT_STRAP_RX_TILE1
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
		.zprd_name = "PCIEPORT::PCIE_RX_NOP",
		.zprd_def = D_PCIE_PORT_RX_NOP
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
		.zprd_name = "PCIEPORT::PCIEP_AER_INJECT_TRANSACTION_SW_TRIG",
		.zprd_def = D_PCIE_PORT_AER_INJ_TXN_SW_TRIG
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_NAK_COUNTER",
		.zprd_def = D_PCIE_PORT_NAK_COUNTER
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_CTRL_STATUS",
		.zprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_CTL_STS
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RX_CAPTURED_LTR_THRESHOLD_VALUES",
		.zprd_def = D_PCIE_PORT_RX_CAPTURED_LTR_THRESH_VALS
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RX_FC_DEBUG_1",
		.zprd_def = D_PCIE_PORT_RX_FC_DBG1
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RX_FC_DEBUG_2",
		.zprd_def = D_PCIE_PORT_RX_FC_DBG2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_AER_PRIV_UNCORRECTABLE_MASK",
		.zprd_def = D_PCIE_PORT_AER_PRIV_UNCORRECTABLE_MASK
	},
	{
		.zprd_name = "PCIEPORT::PCIE_AER_PRIV_TRIGGER",
		.zprd_def = D_PCIE_PORT_AER_PRIV_TRIGGER
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_RSMU_INT_DISABLE",
		.zprd_def = D_PCIE_PORT_RSMU_INT_DISLE
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
		.zprd_name = "PCIEPORT::PCIE_LC_LINK_MANAGEMENT_CNTL2",
		.zprd_def = D_PCIE_PORT_LC_LINK_MGMT_CTL2
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
		.zprd_def = D_PCIE_PORT_LINK_MGMT_STS
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
		.zprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE3",
		.zprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE4",
		.zprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE4
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_L1_PM_SUBSTATE5",
		.zprd_def = D_PCIE_PORT_LC_L1_PM_SUBSTATE5
	},
	{
		.zprd_name = "PCIEPORT::PCIEP_BCH_ECC_CNTL",
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
		.zprd_name = "PCIEPORT::PCIEP_PERF_CNTL_COUNT_TXCLK",
		.zprd_def = D_PCIE_PORT_PERF_CTL_COUNT_TXCLK
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
		.zprd_name = "PCIEPORT::PCIEP_PERF_CNTL_COUNT_TXCLK_LC",
		.zprd_def = D_PCIE_PORT_PERF_CTL_COUNT_TXCLK_LC
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
		.zprd_name = "PCIEPORT::PCIE_LC_EQ_CNTL_8GT",
		.zprd_def = D_PCIE_PORT_LC_EQ_CTL_8GT
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_EQ_CNTL_16GT",
		.zprd_def = D_PCIE_PORT_LC_EQ_CTL_16GT
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_SAVE_RESTORE_1",
		.zprd_def = D_PCIE_PORT_LC_SAVE_RESTORE1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_SAVE_RESTORE_2",
		.zprd_def = D_PCIE_PORT_LC_SAVE_RESTORE2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_SAVE_RESTORE_3",
		.zprd_def = D_PCIE_PORT_LC_SAVE_RESTORE3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_EQ_CNTL_32GT",
		.zprd_def = D_PCIE_PORT_LC_EQ_CTL_32GT
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_PRESET_MASK_CNTL",
		.zprd_def = D_PCIE_PORT_LC_PRST_MASK_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_RXRECOVER_RXSTANDBY_CNTL",
		.zprd_def = D_PCIE_PORT_LC_RXRCOV_RXSBY_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL11",
		.zprd_def = D_PCIE_PORT_LC_CTL11
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL12",
		.zprd_def = D_PCIE_PORT_LC_CTL12
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_SPEED_CNTL2",
		.zprd_def = D_PCIE_PORT_LC_SPEED_CTL2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FORCE_COEFF3",
		.zprd_def = D_PCIE_PORT_LC_FORCE_COEFF3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF3",
		.zprd_def = D_PCIE_PORT_LC_FORCE_EQ_REQ_COEFF3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_LINK_MANAGEMENT_CNTL3",
		.zprd_def = D_PCIE_PORT_LC_LINK_MGMT_CTL3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1",
		.zprd_def = D_PCIE_PORT_LC_ALT_PROT_CTL1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL2",
		.zprd_def = D_PCIE_PORT_LC_ALT_PROT_CTL2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL3",
		.zprd_def = D_PCIE_PORT_LC_ALT_PROT_CTL3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL4",
		.zprd_def = D_PCIE_PORT_LC_ALT_PROT_CTL4
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL5",
		.zprd_def = D_PCIE_PORT_LC_ALT_PROT_CTL5
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6",
		.zprd_def = D_PCIE_PORT_LC_ALT_PROT_CTL6
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_Z10_IDLE_CNTL",
		.zprd_def = D_PCIE_PORT_LC_Z10_IDLE_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL2",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL3",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL4",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL4
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL5",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL5
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL6",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL6
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CNTL9",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CTL9
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_IOVLSM_STATE",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_IOVLSM_STATE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_ARBMUX_CAMEMVLSM_STATE",
		.zprd_def = D_PCIE_PORT_LC_ARBMUX_CAMEMVLSM_STATE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_TRANMIT_FIFO_CDC_CNTL",
		.zprd_def = D_PCIE_PORT_LC_TRANMIT_FIFO_CDC_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_LTSSM_CXL_CNTL_EXTRA",
		.zprd_def = D_PCIE_PORT_LC_LTSSM_CXL_CTL_EXTRA
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_CNTL13",
		.zprd_def = D_PCIE_PORT_LC_CTL13
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_CNTL_8GT",
		.zprd_def = D_PCIE_PORT_LC_FAPE_CTL_8GT
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_CNTL_16GT",
		.zprd_def = D_PCIE_PORT_LC_FAPE_CTL_16GT
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_CNTL_32GT",
		.zprd_def = D_PCIE_PORT_LC_FAPE_CTL_32GT
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_0",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP0
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_1",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_2",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP2
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_3",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP3
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_4",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP4
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_5",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP5
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_6",
		.zprd_def = D_PCIE_PORT_LC_FAPE_SET_GRP6
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAAE_CNTL0",
		.zprd_def = D_PCIE_PORT_LC_FAAE_CTL0
	},
	{
		.zprd_name =
		    "PCIEPORT::PCIE_LC_FAAE_EVALUATED_SETTINGS_STATUS_LANE",
		.zprd_def = D_PCIE_PORT_LC_FAAE_EVAL_SET_STS_LANE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAAE_SETTINGS_CNTL_1_LANE",
		.zprd_def = D_PCIE_PORT_LC_FAAE_SET_CTL_1_LANE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAAE_SETTINGS_CNTL_2_LANE",
		.zprd_def = D_PCIE_PORT_LC_FAAE_SET_CTL_2_LANE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAAE_SETTINGS_CNTL_FINAL_LANE",
		.zprd_def = D_PCIE_PORT_LC_FAAE_SET_CTL_FINAL_LANE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAAE_SETTINGS_RESERVED_A_LANE",
		.zprd_def = D_PCIE_PORT_LC_FAAE_SET_RSVD_A_LANE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_LC_FAAE_SETTINGS_RESERVED_B_LANE",
		.zprd_def = D_PCIE_PORT_LC_FAAE_SET_RSVD_B_LANE
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_PORT_CTRL_1",
		.zprd_def = D_PCIE_PORT_TX_PORT_CTL1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_PORT_CTRL_2",
		.zprd_def = D_PCIE_PORT_TX_PORT_CTL2
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
		.zprd_name = "PCIEPORT::PCIE_TX_CREDITS_FCU_THRESHOLD",
		.zprd_def = D_PCIE_PORT_TX_CREDITS_FCU_THRESH
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_FCU_TIMER_LIMIT",
		.zprd_def = D_PCIE_PORT_TX_FCU_TIMER_LIM
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_VENDOR_SPECIFIC",
		.zprd_def = D_PCIE_PORT_TX_VS_DLLP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_NOP_DLLP",
		.zprd_def = D_PCIE_PORT_TX_NOP_DLLP
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_DLLSM_HISTORY_0",
		.zprd_def = D_PCIE_PORT_TX_DLLSM_HISTORY0
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_DLLSM_HISTORY_1",
		.zprd_def = D_PCIE_PORT_TX_DLLSM_HISTORY1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_REQUEST_NUM_CNTL",
		.zprd_def = D_PCIE_PORT_TX_REQ_NUM_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_ERR_CTRL",
		.zprd_def = D_PCIE_PORT_TX_ERR_CTL
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
		.zprd_def = D_PCIE_PORT_TX_CREDITS_STS
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
		.zprd_name = "PCIEPORT::PCIE_SEND_MORE_INITFC",
		.zprd_def = D_PCIE_PORT_SEND_MORE_INITFC
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_FCP_CREDITS_STATUS",
		.zprd_def = D_PCIE_PORT_TX_FCP_CREDITS_STS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_FCNP_CREDITS_STATUS",
		.zprd_def = D_PCIE_PORT_TX_FCNP_CREDITS_STS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_TX_FCCPL_CREDITS_STATUS",
		.zprd_def = D_PCIE_PORT_TX_FCCPL_CREDITS_STS
	},
	{
		.zprd_name = "PCIEPORT::PCIE_BW_MONITOR_CNTL",
		.zprd_def = D_PCIE_PORT_BW_MONITOR_CTL
	},
	{
		.zprd_name = "PCIEPORT::PCIE_BW_MONITOR_COUNT1",
		.zprd_def = D_PCIE_PORT_BW_MONITOR_COUNT1
	},
	{
		.zprd_name = "PCIEPORT::PCIE_BW_MONITOR_COUNT2",
		.zprd_def = D_PCIE_PORT_BW_MONITOR_COUNT2
	},
};

#endif	/* DEBUG */
