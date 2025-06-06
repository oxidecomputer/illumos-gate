/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2019 Joyent, Inc.
 * Copyright 2025 Oxide Computer Company
 */

#ifndef	_SYS_PCIE_H
#define	_SYS_PCIE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/stdint.h>
#include <sys/pci.h>

/*
 * PCI Express capability registers in PCI configuration space relative to
 * the PCI Express Capability structure.
 */
#define	PCIE_CAP_ID			PCI_CAP_ID
#define	PCIE_CAP_NEXT_PTR		PCI_CAP_NEXT_PTR
#define	PCIE_PCIECAP			0x02	/* PCI-e Capability Reg */
#define	PCIE_DEVCAP			0x04	/* Device Capability */
#define	PCIE_DEVCTL			0x08	/* Device Control */
#define	PCIE_DEVSTS			0x0A	/* Device Status */
#define	PCIE_LINKCAP			0x0C	/* Link Capability */
#define	PCIE_LINKCTL			0x10	/* Link Control */
#define	PCIE_LINKSTS			0x12	/* Link Status */
#define	PCIE_SLOTCAP			0x14	/* Slot Capability */
#define	PCIE_SLOTCTL			0x18	/* Slot Control */
#define	PCIE_SLOTSTS			0x1A	/* Slot Status */
#define	PCIE_ROOTCTL			0x1C	/* Root Control */
#define	PCIE_ROOTCAP			0x1E	/* Root Capabilities */
#define	PCIE_ROOTSTS			0x20	/* Root Status */
#define	PCIE_DEVCAP2			0x24	/* Device Capability 2 */
#define	PCIE_DEVCTL2			0x28	/* Device Control 2 */
#define	PCIE_DEVSTS2			0x2A	/* Device Status 2 */
#define	PCIE_LINKCAP2			0x2C	/* Link Capability 2 */
#define	PCIE_LINKCTL2			0x30	/* Link Control 2 */
#define	PCIE_LINKSTS2			0x32	/* Link Status 2 */
#define	PCIE_SLOTCAP2			0x34	/* Slot Capability 2 */
#define	PCIE_SLOTCTL2			0x38	/* Slot Control 2 */
#define	PCIE_SLOTSTS2			0x3A	/* Slot Status 2 */

/*
 * PCI-Express Config Space size
 */
#define	PCIE_CONF_HDR_SIZE	4096	/* PCIe configuration header size */

/*
 * PCI-Express Capabilities Register (2 bytes)
 */
#define	PCIE_PCIECAP_VER_1_0		0x1	/* PCI-E spec 1.0 */
#define	PCIE_PCIECAP_VER_2_0		0x2	/* PCI-E spec 2.0 */
#define	PCIE_PCIECAP_VER_MASK		0xF	/* Version Mask */
#define	PCIE_PCIECAP_DEV_TYPE_PCIE_DEV	0x00	/* PCI-E Endpont Device */
#define	PCIE_PCIECAP_DEV_TYPE_PCI_DEV	0x10	/* "Leg PCI" Endpont Device */
#define	PCIE_PCIECAP_DEV_TYPE_ROOT	0x40	/* Root Port of Root Complex */
#define	PCIE_PCIECAP_DEV_TYPE_UP	0x50	/* Upstream Port of Switch */
#define	PCIE_PCIECAP_DEV_TYPE_DOWN	0x60	/* Downstream Port of Switch */
#define	PCIE_PCIECAP_DEV_TYPE_PCIE2PCI	0x70	/* PCI-E to PCI Bridge */
#define	PCIE_PCIECAP_DEV_TYPE_PCI2PCIE	0x80	/* PCI to PCI-E Bridge */
#define	PCIE_PCIECAP_DEV_TYPE_RC_IEP	0x90	/* RootComplex Integrated Dev */
#define	PCIE_PCIECAP_DEV_TYPE_RC_EC	0xA0	/* RootComplex Evt Collector */
#define	PCIE_PCIECAP_DEV_TYPE_MASK	0xF0	/* Device/Port Type Mask */
#define	PCIE_PCIECAP_DEV_TYPE_SHIFT	0x4	/* Device/Port Type Shift */
#define	PCIE_PCIECAP_SLOT_IMPL		0x100	/* Slot Impl vs Integrated */
#define	PCIE_PCIECAP_INT_MSG_NUM	0x3E00	/* Interrupt Message Number */

/*
 * Device Capabilities Register (4 bytes)
 */
#define	PCIE_DEVCAP_MAX_PAYLOAD_128	0x0
#define	PCIE_DEVCAP_MAX_PAYLOAD_256	0x1
#define	PCIE_DEVCAP_MAX_PAYLOAD_512	0x2
#define	PCIE_DEVCAP_MAX_PAYLOAD_1024	0x3
#define	PCIE_DEVCAP_MAX_PAYLOAD_2048	0x4
#define	PCIE_DEVCAP_MAX_PAYLOAD_4096	0x5
#define	PCIE_DEVCAP_MAX_PAYLOAD_MASK	0x7	/* Max Payload Size Supported */

#define	PCIE_DEVCAP_PHTM_FUNC_NONE	0x00	/* No Function # bits used */
#define	PCIE_DEVCAP_PHTM_FUNC_ONE	0x08	/* First most sig. bit used */
#define	PCIE_DEVCAP_PHTM_FUNC_TWO	0x10	/* First 2 most sig bit used */
#define	PCIE_DEVCAP_PHTM_FUNC_THREE	0x18	/* All 3 bits used */
#define	PCIE_DEVCAP_PHTM_FUNC_MASK	0x18	/* Phantom Func Supported */

#define	PCIE_DEVCAP_EXT_TAG_5BIT	0x00	/* 5-Bit Tag Field Supported */
#define	PCIE_DEVCAP_EXT_TAG_8BIT	0x20	/* 8-Bit Tag Field Supported */
#define	PCIE_DEVCAP_EXT_TAG_MASK	0x20	/* Ext. Tag Field Supported */

#define	PCIE_DEVCAP_EP_L0S_LAT_MIN	0x000	/* < 64 ns */
#define	PCIE_DEVCAP_EP_L0S_LAT_64ns	0x040	/* 64 ns - 128 ns */
#define	PCIE_DEVCAP_EP_L0S_LAT_128ns	0x080	/* 128 ns - 256 ns */
#define	PCIE_DEVCAP_EP_L0S_LAT_256ns	0x0C0	/* 256 ns - 512 ns */
#define	PCIE_DEVCAP_EP_L0S_LAT_512ns	0x100	/* 512 ns - 1 us */
#define	PCIE_DEVCAP_EP_L0S_LAT_1us	0x140	/* 1 us - 2 us */
#define	PCIE_DEVCAP_EP_L0S_LAT_2us	0x180	/* 2 us - 4 us */
#define	PCIE_DEVCAP_EP_L0S_LAT_MAX	0x1C0	/* > 4 us */
#define	PCIE_DEVCAP_EP_L0S_LAT_MASK	0x1C0	/* EP L0s Accetable Latency */

#define	PCIE_DEVCAP_EP_L1_LAT_MIN	0x000	/* < 1 us */
#define	PCIE_DEVCAP_EP_L1_LAT_1us	0x140	/* 1 us - 2 us */
#define	PCIE_DEVCAP_EP_L1_LAT_2us	0x180	/* 2 us - 4 us */
#define	PCIE_DEVCAP_EP_L1_LAT_4us	0x140	/* 4 us - 8 us */
#define	PCIE_DEVCAP_EP_L1_LAT_8us	0x180	/* 8 us - 16 us */
#define	PCIE_DEVCAP_EP_L1_LAT_16us	0x140	/* 16 us - 32 us */
#define	PCIE_DEVCAP_EP_L1_LAT_32us	0x180	/* 32 us - 64 us */
#define	PCIE_DEVCAP_EP_L1_LAT_MAX	0x1C0	/* > 64 us */
#define	PCIE_DEVCAP_EP_L1_LAT_MASK	0x700	/* EP L1 Accetable Latency */

/*
 * As of PCIe 2.x these three bits are now undefined.
 */
#define	PCIE_DEVCAP_ATTN_BUTTON		0x1000	/* Attention Button Present */
#define	PCIE_DEVCAP_ATTN_INDICATOR	0x2000	/* Attn Indicator Present */
#define	PCIE_DEVCAP_PWR_INDICATOR	0x4000	/* Power Indicator Present */

#define	PCIE_DEVCAP_ROLE_BASED_ERR_REP	0x8000	/* Role Based Error Reporting */

#define	PCIE_DEVCAP_PLMT_VAL_SHIFT	18	/* Power Limit Value Shift */
#define	PCIE_DEVCAP_PLMT_VAL_MASK	0xFF	/* Power Limit Value Mask */

#define	PCIE_DEVCAP_PLMT_SCL_1_BY_1	0x0000000	/* 1x Scale */
#define	PCIE_DEVCAP_PLMT_SCL_1_BY_10	0x4000000	/* 0.1x Scale */
#define	PCIE_DEVCAP_PLMT_SCL_1_BY_100	0x8000000	/* 0.01x Scale */
#define	PCIE_DEVCAP_PLMT_SCL_1_BY_1000	0xC000000	/* 0.001x Scale */
#define	PCIE_DEVCAP_PLMT_SCL_MASK	0xC000000	/* Power Limit Scale */

#define	PCIE_DEVCAP_FLR			0x10000000 /* Function Level Reset */

/*
 * Device Control Register (2 bytes)
 */
#define	PCIE_DEVCTL_CE_REPORTING_EN	0x1	/* Correctable Error Enable */
#define	PCIE_DEVCTL_NFE_REPORTING_EN	0x2	/* Non-Fatal Error Enable */
#define	PCIE_DEVCTL_FE_REPORTING_EN	0x4	/* Fatal Error Enable */
#define	PCIE_DEVCTL_UR_REPORTING_EN	0x8	/* Unsupported Request Enable */
#define	PCIE_DEVCTL_ERR_MASK		0xF	/* All of the above bits */

#define	PCIE_DEVCTL_RO_EN		0x10	/* Enable Relaxed Ordering */

#define	PCIE_DEVCTL_MAX_PAYLOAD_128	0x00
#define	PCIE_DEVCTL_MAX_PAYLOAD_256	0x20
#define	PCIE_DEVCTL_MAX_PAYLOAD_512	0x40
#define	PCIE_DEVCTL_MAX_PAYLOAD_1024	0x60
#define	PCIE_DEVCTL_MAX_PAYLOAD_2048	0x80
#define	PCIE_DEVCTL_MAX_PAYLOAD_4096	0xA0
#define	PCIE_DEVCTL_MAX_PAYLOAD_MASK	0xE0	/* Max_Payload_Size */
#define	PCIE_DEVCTL_MAX_PAYLOAD_SHIFT	0x5

#define	PCIE_DEVCTL_EXT_TAG_FIELD_EN	0x100	/* Extended Tag Field Enable */
#define	PCIE_DEVCTL_PHTM_FUNC_EN	0x200	/* Phantom Functions Enable */
#define	PCIE_DEVCTL_AUX_POWER_PM_EN	0x400	/* Auxiliary Power PM Enable */
#define	PCIE_DEVCTL_ENABLE_NO_SNOOP	0x800	/* Enable No Snoop */

#define	PCIE_DEVCTL_MAX_READ_REQ_128	0x0000
#define	PCIE_DEVCTL_MAX_READ_REQ_256	0x1000
#define	PCIE_DEVCTL_MAX_READ_REQ_512	0x2000
#define	PCIE_DEVCTL_MAX_READ_REQ_1024	0x3000
#define	PCIE_DEVCTL_MAX_READ_REQ_2048	0x4000
#define	PCIE_DEVCTL_MAX_READ_REQ_4096	0x5000
#define	PCIE_DEVCTL_MAX_READ_REQ_MASK	0x7000	/* Max_Read_Request_Size */
#define	PCIE_DEVCTL_MAX_READ_REQ_SHIFT	0xC

#define	PCIE_DEVCTL_BRIDGE_RETRY	0x8000	/* Bridge can return CRS */
#define	PCIE_DEVCTL_INITIATE_FLR	0x8000	/* Start Function Level Reset */

/*
 * Device Status Register (2 bytes)
 */
#define	PCIE_DEVSTS_CE_DETECTED		0x1	/* Correctable Error Detected */
#define	PCIE_DEVSTS_NFE_DETECTED	0x2	/* Non Fatal Error Detected */
#define	PCIE_DEVSTS_FE_DETECTED		0x4	/* Fatal Error Detected */
#define	PCIE_DEVSTS_UR_DETECTED		0x8	/* Unsupported Req Detected */
#define	PCIE_DEVSTS_AUX_POWER		0x10	/* AUX Power Detected */
#define	PCIE_DEVSTS_TRANS_PENDING	0x20	/* Transactions Pending */
#define	PCIE_DEVSTS_EPR_DETECTED	0x40	/* Emergency Power Reduction */

/*
 * Link Capability Register (4 bytes)
 */
#define	PCIE_LINKCAP_MAX_SPEED_2_5	0x1	/* 2.5 GT/s Speed */
/*
 * In version 2 of PCI express, this indicated that both 5.0 GT/s and 2.5 GT/s
 * speeds were supported. The use of this as the maximum link speed was added
 * with PCIe v3.
 */
#define	PCIE_LINKCAP_MAX_SPEED_5	0x2	/* 5.0 GT/s Speed */
#define	PCIE_LINKCAP_MAX_SPEED_8	0x3	/* 8.0 GT/s Speed */
#define	PCIE_LINKCAP_MAX_SPEED_16	0x4	/* 16.0 GT/s Speed */
#define	PCIE_LINKCAP_MAX_SPEED_32	0x5	/* 32.0 GT/s Speed */
#define	PCIE_LINKCAP_MAX_SPEED_64	0x6	/* 64.0 GT/s Speed */
#define	PCIE_LINKCAP_MAX_SPEED_MASK	0xF	/* Maximum Link Speed */
#define	PCIE_LINKCAP_MAX_WIDTH_X1	0x010
#define	PCIE_LINKCAP_MAX_WIDTH_X2	0x020
#define	PCIE_LINKCAP_MAX_WIDTH_X4	0x040
#define	PCIE_LINKCAP_MAX_WIDTH_X8	0x080
#define	PCIE_LINKCAP_MAX_WIDTH_X12	0x0C0
#define	PCIE_LINKCAP_MAX_WIDTH_X16	0x100
#define	PCIE_LINKCAP_MAX_WIDTH_X32	0x200
#define	PCIE_LINKCAP_MAX_WIDTH_MASK	0x3f0	/* Maximum Link Width */

#define	PCIE_LINKCAP_ASPM_SUP_L0S	0x400	/* L0s Entry Supported */
#define	PCIE_LINKCAP_ASPM_SUP_L1	0x800	/* L1 Entry Supported */
#define	PCIE_LINKCAP_ASPM_SUP_L0S_L1	0xC00	/* L0s abd L1 Supported */
#define	PCIE_LINKCAP_ASPM_SUP_MASK	0xC00	/* ASPM Support */

#define	PCIE_LINKCAP_L0S_EXIT_LAT_MIN	0x0000	/* < 64 ns */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_64ns	0x1000	/* 64 ns - 128 ns */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_128ns	0x2000	/* 128 ns - 256 ns */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_256ns	0x3000	/* 256 ns - 512 ns */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_512ns	0x4000	/* 512 ns - 1 us */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_1us	0x5000	/* 1 us - 2 us */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_2us	0x6000	/* 2 us - 4 us */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_MAX	0x7000	/* > 4 us */
#define	PCIE_LINKCAP_L0S_EXIT_LAT_MASK	0x7000	/* L0s Exit Latency */

#define	PCIE_LINKCAP_L1_EXIT_LAT_MIN	0x00000	/* < 1 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_1us	0x08000	/* 1 us - 2 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_2us	0x10000	/* 2 us - 4 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_4us	0x18000	/* 4 us - 8 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_8us	0x20000	/* 8 us - 16 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_16us	0x28000	/* 16 us - 32 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_32us	0x30000	/* 32 us - 64 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_MAX	0x38000	/* > 64 us */
#define	PCIE_LINKCAP_L1_EXIT_LAT_MASK	0x38000	/* L1 Exit Latency */

#define	PCIE_LINKCAP_CLOCK_POWER_MGMT	0x40000	/* Clock Power Management */
#define	PCIE_LINKCAP_SDER_CAP		0x80000 /* Surprise Down Err report */
#define	PCIE_LINKCAP_DLL_ACTIVE_REP_CAPABLE	0x100000    /* DLL Active */
							    /* Capable bit */
#define	PCIE_LINKCAP_LINK_BW_NOTIFY_CAP	0x200000 /* Link Bandwidth Notify Cap */
#define	PCIE_LINKCAP_ASPM_OPTIONAL	0x400000 /* ASPM Opt. Comp. */

#define	PCIE_LINKCAP_PORT_NUMBER	0xFF000000	/* Port Number */
#define	PCIE_LINKCAP_PORT_NUMBER_SHIFT	24	/* Port Number Shift */
#define	PCIE_LINKCAP_PORT_NUMBER_MASK	0xFF	/* Port Number Mask */

/*
 * Link Control Register (2 bytes)
 */
#define	PCIE_LINKCTL_ASPM_CTL_DIS	0x0	/* ASPM Disable */
#define	PCIE_LINKCTL_ASPM_CTL_L0S	0x1	/* ASPM L0s only */
#define	PCIE_LINKCTL_ASPM_CTL_L1	0x2	/* ASPM L1 only */
#define	PCIE_LINKCTL_ASPM_CTL_L0S_L1	0x3	/* ASPM L0s and L1 only */
#define	PCIE_LINKCTL_ASPM_CTL_MASK	0x3	/* ASPM Control */

#define	PCIE_LINKCTL_RCB_64_BYTE	0x0	/* 64 Byte */
#define	PCIE_LINKCTL_RCB_128_BYTE	0x8	/* 128 Byte */
#define	PCIE_LINKCTL_RCB_MASK		0x8	/* Read Completion Boundary */

#define	PCIE_LINKCTL_LINK_DISABLE	0x10	/* Link Disable */
#define	PCIE_LINKCTL_RETRAIN_LINK	0x20	/* Retrain Link */
#define	PCIE_LINKCTL_COMMON_CLK_CFG	0x40	/* Common Clock Configuration */
#define	PCIE_LINKCTL_EXT_SYNCH		0x80	/* Extended Synch */
#define	PCIE_LINKCTL_CLOCK_POWER_MGMT	0x100	/* Enable Clock Power Mgmt. */
#define	PCIE_LINKCTL_HW_WIDTH_DISABLE	0x200	/* hw auto width disable */
#define	PCIE_LINKCTL_LINK_BW_INTR_EN	0x400	/* Link bw mgmt intr */
#define	PCIE_LINKCTL_LINK_AUTO_BW_INTR_EN	0x800	/* Auto bw intr */

#define	PCI_LINKCTRL_DRS_SIG_CTRL_NO_REP	0x00
#define	PCI_LINKCTRL_DRS_SIG_CTRL_IE		0x4000
#define	PCI_LINKCTRL_DRS_SIG_CTRL_DRS_FRS	0x8000
#define	PCIE_LINKCTL_DRS_SIG_CTRL_MASK	0xC000	/* DRS Signaling Control */

/*
 * Link Status Register (2 bytes)
 */
#define	PCIE_LINKSTS_SPEED_2_5		0x1	/* 2.5 GT/s Link Speed */
#define	PCIE_LINKSTS_SPEED_5		0x2	/* 5.0 GT/s Link Speed */
#define	PCIE_LINKSTS_SPEED_8		0x3	/* 8.0 GT/s Link Speed */
#define	PCIE_LINKSTS_SPEED_16		0x4	/* 16.0 GT/s Link Speed */
#define	PCIE_LINKSTS_SPEED_32		0x5	/* 32.0 GT/s Link Speed */
#define	PCIE_LINKSTS_SPEED_64		0x6	/* 64.0 GT/s Link Speed */
#define	PCIE_LINKSTS_SPEED_MASK		0xF	/* Link Speed */

#define	PCIE_LINKSTS_NEG_WIDTH_X1	0x010
#define	PCIE_LINKSTS_NEG_WIDTH_X2	0x020
#define	PCIE_LINKSTS_NEG_WIDTH_X4	0x040
#define	PCIE_LINKSTS_NEG_WIDTH_X8	0x080
#define	PCIE_LINKSTS_NEG_WIDTH_X12	0x0C0
#define	PCIE_LINKSTS_NEG_WIDTH_X16	0x100
#define	PCIE_LINKSTS_NEG_WIDTH_X32	0x200
#define	PCIE_LINKSTS_NEG_WIDTH_MASK	0x3F0	/* Negotiated Link Width */

/* This bit is undefined as of PCIe 2.x */
#define	PCIE_LINKSTS_TRAINING_ERROR	0x400	/* Training Error */
#define	PCIE_LINKSTS_LINK_TRAINING	0x800	/* Link Training */
#define	PCIE_LINKSTS_SLOT_CLK_CFG	0x1000	/* Slot Clock Configuration */
#define	PCIE_LINKSTS_DLL_LINK_ACTIVE	0x2000	/* DLL Link Active */
#define	PCIE_LINKSTS_LINK_BW_MGMT	0x4000	/* Link bw mgmt status */
#define	PCIE_LINKSTS_AUTO_BW		0x8000	/* Link auto BW status */

/*
 * Slot Capability Register (4 bytes)
 */
#define	PCIE_SLOTCAP_ATTN_BUTTON	0x1	/* Attention Button Present */
#define	PCIE_SLOTCAP_POWER_CONTROLLER	0x2	/* Power Controller Present */
#define	PCIE_SLOTCAP_MRL_SENSOR		0x4	/* MRL Sensor Present */
#define	PCIE_SLOTCAP_ATTN_INDICATOR	0x8	/* Attn Indicator Present */
#define	PCIE_SLOTCAP_PWR_INDICATOR	0x10	/* Power Indicator Present */
#define	PCIE_SLOTCAP_HP_SURPRISE	0x20	/* Hot-Plug Surprise */
#define	PCIE_SLOTCAP_HP_CAPABLE		0x40	/* Hot-Plug Capable */

#define	PCIE_SLOTCAP_PLMT_VAL_SHIFT	7	/* Slot Pwr Limit Value Shift */
#define	PCIE_SLOTCAP_PLMT_VAL_MASK	0xFF	/* Slot Pwr Limit Value */

#define	PCIE_SLOTCAP_PLMT_SCL_1_BY_1	0x00000	/* 1x Scale */
#define	PCIE_SLOTCAP_PLMT_SCL_1_BY_10	0x08000	/* 0.1x Scale */
#define	PCIE_SLOTCAP_PLMT_SCL_1_BY_100	0x10000	/* 0.01x Scale */
#define	PCIE_SLOTCAP_PLMT_SCL_1_BY_1000	0x18000	/* 0.001x Scale */
#define	PCIE_SLOTCAP_PLMT_SCL_MASK	0x18000	/* Slot Power Limit Scale */
#define	PCIE_SLOTCAP_EMI_LOCK_PRESENT	0x20000 /* EMI Lock Present */
#define	PCIE_SLOTCAP_NO_CMD_COMP_SUPP	0x40000 /* No Command Comp. Supported */

#define	PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT	19	/* Physical Slot Num Shift */
#define	PCIE_SLOTCAP_PHY_SLOT_NUM_MASK	0x1FFF	/* Physical Slot Num Mask */

#define	PCIE_SLOTCAP_PHY_SLOT_NUM(reg) \
	    (((reg) >> PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT) & \
	    PCIE_SLOTCAP_PHY_SLOT_NUM_MASK)

/*
 * Slot Control Register (2 bytes)
 */
#define	PCIE_SLOTCTL_ATTN_BTN_EN	0x1	/* Attn Button Pressed Enable */
#define	PCIE_SLOTCTL_PWR_FAULT_EN	0x2	/* Pwr Fault Detected Enable */
#define	PCIE_SLOTCTL_MRL_SENSOR_EN	0x4	/* MRL Sensor Changed Enable */
#define	PCIE_SLOTCTL_PRESENCE_CHANGE_EN	0x8	/* Presence Detect Changed En */
#define	PCIE_SLOTCTL_CMD_INTR_EN	0x10	/* CMD Completed Interrupt En */
#define	PCIE_SLOTCTL_HP_INTR_EN		0x20	/* Hot-Plug Interrupt Enable */
#define	PCIE_SLOTCTL_PWR_CONTROL	0x0400	/* Power controller Control */
#define	PCIE_SLOTCTL_EMI_LOCK_CONTROL	0x0800	/* EMI Lock control */
#define	PCIE_SLOTCTL_DLL_STATE_EN	0x1000	/* DLL State Changed En */
#define	PCIE_SLOTCTL_AUTO_SLOT_PL_DIS	0x2000	/* Auto Slot Power Limit Dis */
#define	PCIE_SLOTCTL_INB_PRES_DET_DIS	0x4000	/* Inband Presence Detect Dis */
#define	PCIE_SLOTCTL_ATTN_INDICATOR_MASK 0x00C0	/* Attn Indicator mask */
#define	PCIE_SLOTCTL_PWR_INDICATOR_MASK	0x0300	/* Power Indicator mask */
#define	PCIE_SLOTCTL_INTR_MASK		0x103f	/* Supported intr mask */

/* State values for the Power and Attention Indicators */
#define	PCIE_SLOTCTL_INDICATOR_STATE_ON		0x1	/* indicator ON */
#define	PCIE_SLOTCTL_INDICATOR_STATE_BLINK	0x2	/* indicator BLINK */
#define	PCIE_SLOTCTL_INDICATOR_STATE_OFF	0x3	/* indicator OFF */

/*
 * Macros to set/get the state of Power and Attention Indicators
 * in the PCI Express Slot Control Register.
 */
#define	pcie_slotctl_pwr_indicator_get(reg)	\
	(((reg) & PCIE_SLOTCTL_PWR_INDICATOR_MASK) >> 8)
#define	pcie_slotctl_attn_indicator_get(ctrl)	\
	(((ctrl) & PCIE_SLOTCTL_ATTN_INDICATOR_MASK) >> 6)
#define	pcie_slotctl_attn_indicator_set(ctrl, v)\
	(((ctrl) & ~PCIE_SLOTCTL_ATTN_INDICATOR_MASK) | ((v) << 6))
#define	pcie_slotctl_pwr_indicator_set(ctrl, v)\
	(((ctrl) & ~PCIE_SLOTCTL_PWR_INDICATOR_MASK) | ((v) << 8))

/*
 * Slot Status register (2 bytes)
 */
#define	PCIE_SLOTSTS_ATTN_BTN_PRESSED	0x1	/* Attention Button Pressed */
#define	PCIE_SLOTSTS_PWR_FAULT_DETECTED	0x2	/* Power Fault Detected */
#define	PCIE_SLOTSTS_MRL_SENSOR_CHANGED	0x4	/* MRL Sensor Changed */
#define	PCIE_SLOTSTS_PRESENCE_CHANGED	0x8	/* Presence Detect Changed */
#define	PCIE_SLOTSTS_COMMAND_COMPLETED	0x10	/* Command Completed */
#define	PCIE_SLOTSTS_MRL_SENSOR_OPEN	0x20	/* MRL Sensor Open */
#define	PCIE_SLOTSTS_PRESENCE_DETECTED	0x40	/* Card Present in slot */
#define	PCIE_SLOTSTS_EMI_LOCK_SET	0x0080	/* EMI Lock set */
#define	PCIE_SLOTSTS_DLL_STATE_CHANGED	0x0100	/* DLL State Changed */
#define	PCIE_SLOTSTS_STATUS_EVENTS	0x11f	/* Supported events */

/*
 * Root Control Register (2 bytes)
 */
#define	PCIE_ROOTCTL_SYS_ERR_ON_CE_EN	0x1	/* Sys Err on Cor Err Enable */
#define	PCIE_ROOTCTL_SYS_ERR_ON_NFE_EN	0x2	/* Sys Err on NF Err Enable */
#define	PCIE_ROOTCTL_SYS_ERR_ON_FE_EN	0x4	/* Sys Err on Fatal Err En */
#define	PCIE_ROOTCTL_PME_INTERRUPT_EN	0x8	/* PME Interrupt Enable */
#define	PCIE_ROOTCTL_CRS_SW_VIS_EN	0x10	/* CRS SW Visibility EN */

/*
 * Root Capabilities register (2 bytes)
 */
#define	PCIE_ROOTCAP_CRS_SW_VIS		0x01	/* CRS SW Visible */

/*
 * Root Status Register (4 bytes)
 */
#define	PCIE_ROOTSTS_PME_REQ_ID_SHIFT	0	/* PME Requestor ID */
#define	PCIE_ROOTSTS_PME_REQ_ID_MASK	0xFFFF	/* PME Requestor ID */

#define	PCIE_ROOTSTS_PME_STATUS		0x10000	/* PME Status */
#define	PCIE_ROOTSTS_PME_PENDING	0x20000	/* PME Pending */

/*
 * Device Capabilities 2 Register (4 bytes)
 */
#define	PCIE_DEVCAP2_COM_TO_RANGE_MASK	0xF
#define	PCIE_DEVCAP2_COM_TO_DISABLE	0x10
#define	PCIE_DEVCAP2_ARI_FORWARD	0x20
#define	PCIE_DEVCAP2_ATOMICOP_ROUTING	0x40
#define	PCIE_DEVCAP2_32_ATOMICOP_COMPL  0x80
#define	PCIE_DEVCAP2_64_ATOMICOP_COMPL  0x100
#define	PCIE_DEVCAP2_128_CAS_COMPL	0x200
#define	PCIE_DEVCAP2_NO_RO_PR_PR_PASS	0x400
#define	PCIE_DEVCAP2_LTR_MECH		0x800
#define	PCIE_DEVCAP2_TPH_COMP_SHIFT	12
#define	PCIE_DEVCAP2_TPH_COMP_MASK	0x3
#define	PCIE_DEVCAP2_LNSYS_CLS_SHIFT	14
#define	PCIE_DEVCAP2_LNSYS_CLS_MASK	0x3
#define	PCIE_DEVCAP2_10B_TAG_COMP_SUP	0x10000
#define	PCIE_DEVCAP2_10B_TAG_REQ_SUP	0x20000
#define	PCIE_DEVCAP2_OBFF_SHIFT		18
#define	PCIE_DEVCAP2_OBFF_MASK		0x3
#define	PCIE_DEVCAP2_EXT_FMT_FIELD	0x100000
#define	PCIE_DEVCAP2_END_END_TLP_PREFIX	0x200000
#define	PCIE_DEVCAP2_MAX_END_END_SHIFT	22
#define	PCIE_DEVCAP2_MAX_END_END_MASK	0x3
#define	PCIE_DEVCAP2_EPR_SUP_SHIFT	24
#define	PCIE_DEVCAP2_EPR_SUP_MASK	0x3
#define	PCIE_DEVCAP2_EPR_INIT_REQ	0x4000000
#define	PCIE_DEVCAP2_FRS_SUP		0x80000000

/*
 * Device Control 2 Register (2 bytes)
 */
#define	PCIE_DEVCTL2_COM_TO_RANGE_MASK	0xf
#define	PCIE_DEVCTL2_COM_TO_RANGE_0	0x0
#define	PCIE_DEVCTL2_COM_TO_RANGE_1	0x1
#define	PCIE_DEVCTL2_COM_TO_RANGE_2	0x2
#define	PCIE_DEVCTL2_COM_TO_RANGE_3	0x5
#define	PCIE_DEVCTL2_COM_TO_RANGE_4	0x6
#define	PCIE_DEVCTL2_COM_TO_RANGE_5	0x9
#define	PCIE_DEVCTL2_COM_TO_RANGE_6	0xa
#define	PCIE_DEVCTL2_COM_TO_RANGE_7	0xd
#define	PCIE_DEVCTL2_COM_TO_RANGE_8	0xe
#define	PCIE_DEVCTL2_COM_TO_DISABLE	0x10
#define	PCIE_DEVCTL2_ARI_FORWARD_EN	0x20
#define	PCIE_DEVCTL2_ATOMICOP_REQ_EN	0x40
#define	PCIE_DEVCTL2_ATOMICOP_EGRS_BLK	0x80
#define	PCIE_DEVCTL2_IDO_REQ_EN		0x100
#define	PCIE_DEVCTL2_IDO_COMPL_EN	0x200
#define	PCIE_DEVCTL2_LTR_MECH_EN	0x400
#define	PCIE_DEVCTL2_EPR_REQ		0x800
#define	PCIE_DEVCTL2_10B_TAG_REQ_EN	0x1000
#define	PCIE_DEVCTL2_OBFF_MASK		0x6000
#define	PCIE_DEVCTL2_OBFF_DISABLE	0x0000
#define	PCIE_DEVCTL2_OBFF_EN_VARA	0x2000
#define	PCIE_DEVCTL2_OBFF_EN_VARB	0x4000
#define	PCIE_DEVCTL2_OBFF_EN_WAKE	0x6000
#define	PCIE_DEVCTL2_END_END_TLP_PREFIX	0x8000


/*
 * Link Capabilities 2 Register (4 bytes)
 */
#define	PCIE_LINKCAP2_SPEED_2_5		0x02
#define	PCIE_LINKCAP2_SPEED_5		0x04
#define	PCIE_LINKCAP2_SPEED_8		0x08
#define	PCIE_LINKCAP2_SPEED_16		0x10
#define	PCIE_LINKCAP2_SPEED_32		0x20
#define	PCIE_LINKCAP2_SPEED_64		0x40
#define	PCIE_LINKCAP2_SPEED_MASK	0xfe
#define	PCIE_LINKCAP2_CROSSLINK		0x100
#define	PCIE_LINKCAP2_LSKP_OSGSS_MASK	0xfe00
#define	PCIE_LINKCAP2_LKSP_OSGSS_2_5	0x0200
#define	PCIE_LINKCAP2_LKSP_OSGSS_5	0x0400
#define	PCIE_LINKCAP2_LKSP_OSGSS_8	0x0800
#define	PCIE_LINKCAP2_LKSP_OSGSS_16	0x1000
#define	PCIE_LINKCAP2_LKSP_OSGSS_32	0x2000
#define	PCIE_LINKCAP2_LKSP_OSGSS_64	0x4000
#define	PCIE_LINKCAP2_LKSP_OSRSS_MASK	0x7f0000
#define	PCIE_LINKCAP2_LKSP_OSRSS_2_5	0x010000
#define	PCIE_LINKCAP2_LKSP_OSRSS_5	0x020000
#define	PCIE_LINKCAP2_LKSP_OSRSS_8	0x040000
#define	PCIE_LINKCAP2_LKSP_OSRSS_16	0x080000
#define	PCIE_LINKCAP2_LKSP_OSRSS_32	0x100000
#define	PCIE_LINKCAP2_LKSP_OSRSS_64	0x200000
#define	PCIE_LINKCAP2_RTPD_SUP		0x800000
#define	PCIE_LINKCAP2_TRTPD_SUP		0x01000000
#define	PCIE_LINKCAP2_DRS		0x80000000

/*
 * Link Control 2 Register (2 bytes)
 */

#define	PCIE_LINKCTL2_TARGET_SPEED_2_5	0x1	/* 2.5 GT/s Speed */
#define	PCIE_LINKCTL2_TARGET_SPEED_5	0x2	/* 5.0 GT/s Speed */
#define	PCIE_LINKCTL2_TARGET_SPEED_8	0x3	/* 8.0 GT/s Speed */
#define	PCIE_LINKCTL2_TARGET_SPEED_16	0x4	/* 16.0 GT/s Speed */
#define	PCIE_LINKCTL2_TARGET_SPEED_32	0x5	/* 32.0 GT/s Speed */
#define	PCIE_LINKCTL2_TARGET_SPEED_64	0x6	/* 64.0 GT/s Speed */
#define	PCIE_LINKCTL2_TARGET_SPEED_MASK	0x000f
#define	PICE_LINKCTL2_ENTER_COMPLIANCE	0x0010
#define	PCIE_LINKCTL2_HW_AUTO_SPEED_DIS	0x0020
#define	PCIE_LINKCTL2_SELECT_DEEMPH	0x0040
#define	PCIE_LINKCTL2_TX_MARGIN_MASK	0x0380
#define	PCIE_LINKCTL2_ENTER_MOD_COMP	0x0400
#define	PCIE_LINKCTL2_COMP_SOS		0x0800
#define	PCIE_LINKCTL2_COMP_DEEMPM_MASK	0xf000

/*
 * Link Status 2 Register (2 bytes)
 */
#define	PCIE_LINKSTS2_CUR_DEEMPH	0x0001
#define	PCIE_LINKSTS2_EQ8GT_COMP	0x0002
#define	PCIE_LINKSTS2_EQ8GT_P1_SUC	0x0004
#define	PCIE_LINKSTS2_EQ8GT_P2_SUC	0x0008
#define	PCIE_LINKSTS2_EQ8GT_P3_SUC	0x0010
#define	PCIE_LINKSTS2_LINK_EQ_REQ	0x0020
#define	PCIE_LINKSTS2_RETIMER_PRES_DET	0x0040
#define	PCIE_LINKSTS2_2RETIMER_PRES_DET	0x0080
#define	PCIE_LINKSTS2_XLINK_RES		0x0300
#define	PCIE_LINKSTS2_DS_COMP_PRES_MASK	0x7000
#define	PCIE_LINKSTS2_DRS_MSG_RX	0x8000

/*
 * Slot Capabilities 2 Register (4 bytes)
 */
#define	PCIE_SLOTCAP2_INB_PRES_DET_DIS_SUP	0x1

/*
 * PCI-Express Enhanced Capabilities Link Entry Bit Offsets
 */
#define	PCIE_EXT_CAP			0x100	/* Base Address of Ext Cap */

#define	PCIE_EXT_CAP_ID_SHIFT		0	/* PCI-e Ext Cap ID */
#define	PCIE_EXT_CAP_ID_MASK		0xFFFF
#define	PCIE_EXT_CAP_VER_SHIFT		16	/* PCI-e Ext Cap Ver */
#define	PCIE_EXT_CAP_VER_MASK		0xF
#define	PCIE_EXT_CAP_NEXT_PTR_SHIFT	20	/* PCI-e Ext Cap Next Ptr */
#define	PCIE_EXT_CAP_NEXT_PTR_MASK	0xFFF

#define	PCIE_EXT_CAP_NEXT_PTR_NULL	0x0
#define	PCIE_EXT_CAP_MAX_PTR		0x3c0	/* max. number of caps */

/*
 * PCI-Express Enhanced Capability Identifier Values
 */
#define	PCIE_EXT_CAP_ID_AER		0x1	/* Advanced Error Handling */
#define	PCIE_EXT_CAP_ID_VC		0x2	/* Virtual Channel, no MFVC */
#define	PCIE_EXT_CAP_ID_SER		0x3	/* Serial Number */
#define	PCIE_EXT_CAP_ID_PWR_BUDGET	0x4	/* Power Budgeting */
#define	PCIE_EXT_CAP_ID_RC_LINK_DECL	0x5	/* RC Link Declaration */
#define	PCIE_EXT_CAP_ID_RC_INT_LINKCTRL	0x6	/* RC Internal Link Control */
#define	PCIE_EXT_CAP_ID_RC_EVNT_CEA	0x7	/* RC Event Collector */
						/* Endpoint Association */
#define	PCIE_EXT_CAP_ID_MFVC		0x8	/* Multi-func Virtual Channel */
#define	PCIE_EXT_CAP_ID_VC_WITH_MFVC	0x9	/* Virtual Channel w/ MFVC */
#define	PCIE_EXT_CAP_ID_RCRB		0xA	/* Root Complex Register Blck */
#define	PCIE_EXT_CAP_ID_VS		0xB	/* Vendor Spec Extended Cap */
#define	PCIE_EXT_CAP_ID_CAC		0xC	/* Config Access Correlation */
#define	PCIE_EXT_CAP_ID_ACS		0xD	/* Access Control Services */
#define	PCIE_EXT_CAP_ID_ARI		0xE	/* Alternative Routing ID */
#define	PCIE_EXT_CAP_ID_ATS		0xF	/* Address Translation Svcs */
#define	PCIE_EXT_CAP_ID_SRIOV		0x10	/* Single Root I/O Virt. */
#define	PCIE_EXT_CAP_ID_MRIOV		0x11	/* Multi Root I/O Virt. */
#define	PCIE_EXT_CAP_ID_MULTICAST	0x12	/* Multicast Services */
#define	PCIE_EXT_CAP_ID_PGREQ		0x13	/* Page Request */
#define	PCIE_EXT_CAP_ID_EA		0x14	/* Enhanced Allocation */
#define	PCIE_EXT_CAP_ID_RESIZE_BAR	0x15	/* Resizable BAR */
#define	PCIE_EXT_CAP_ID_DPA		0x16	/* Dynamic Power Allocation */
#define	PCIE_EXT_CAP_ID_TPH_REQ		0x17	/* TPH Requester */
#define	PCIE_EXT_CAP_ID_LTR		0x18	/* Latency Tolerance Report */
#define	PCIE_EXT_CAP_ID_PCIE2		0x19	/* PCI Express Capability 2 */
#define	PCIE_EXT_CAP_ID_PASID		0x1B	/* PASID */
#define	PCIE_EXT_CAP_ID_LNR		0x1C	/* LNR */
#define	PCIE_EXT_CAP_ID_DPC		0x1D	/* DPC */
#define	PCIE_EXT_CAP_ID_L1PM		0x1E	/* L1 PM Substrates */
#define	PCIE_EXT_CAP_ID_PTM		0x1F	/* Precision Time Management */
#define	PCIE_EXT_CAP_ID_FRS		0x21	/* Function Ready Stat. Queue */
#define	PCIE_EXT_CAP_ID_RTR		0x22	/* Readiness Time Reporting */
#define	PCIE_EXT_CAP_ID_DVS		0x23	/* Designated Vendor-Specific */
#define	PCIE_EXT_CAP_ID_VFRBAR		0x24	/* VF Resizable BAR */
#define	PCIE_EXT_CAP_ID_DLF		0x25	/* Data Link Feature */
#define	PCIE_EXT_CAP_ID_PL16GT		0x26	/* Physical Layer 16.0 GT/s */
#define	PCIE_EXT_CAP_ID_LANE_MARGIN	0x27	/* Lane Margining */
#define	PCIE_EXT_CAP_ID_HIEARCHY_ID	0x28	/* Hierarchy ID */
#define	PCIE_EXT_CAP_ID_NPEM		0x29	/* Native PCIe Enclosure Mgmt */
#define	PCIE_EXT_CAP_ID_PL32GT		0x2A	/* Physical Layer 32.0 GT/s */
#define	PCIE_EXT_CAP_ID_AP		0x2B	/* Alternate Protocol */
#define	PCIE_EXT_CAP_ID_SFI		0x2C	/* Sys. Firmware Intermediary */
#define	PCIE_EXT_CAP_ID_SHDW_FUNC	0x2D	/* Shadow Functions */
#define	PCIE_EXT_CAP_ID_DOE		0x2E	/* Data Object Exchange */
#define	PCIE_EXT_CAP_ID_DEV3		0x2F	/* Device 3 */
#define	PCIE_EXT_CAP_ID_IDE		0x30	/* Integrity and Data Encr. */
#define	PCIE_EXT_CAP_ID_PL64GT		0x31	/* Physical Layer 64.0 GT/s */
#define	PCIE_EXT_CAP_ID_FLIT_LOG	0x32	/* Flit Logging */
#define	PCIE_EXT_CAP_ID_FLIT_PERF	0x33	/* Flit Perf. Measurement */
#define	PCIE_EXT_CAP_ID_FLIT_ERR	0x34	/* Flit Error Injection */
#define	PCIE_EXT_CAP_ID_SVC		0x35	/* Streamlined Virtual Chan. */
#define	PCIE_EXT_CAP_ID_MMIO_RBL	0x36	/* MMIO Register Block Loc. */
#define	PCIE_EXT_CAP_ID_NOP_FLIT	0x37	/* NOP Flit */
#define	PCIE_EXT_CAP_ID_SIOV		0x38	/* Scalable I/O Virt. */
#define	PCIE_EXT_CAP_ID_PL128GT		0x39	/* Physical Layer 128.0 GT/s */
#define	PCIE_EXT_CAP_ID_CAP_DATA	0x3a	/* Captured Data */

/*
 * PCI-Express Advanced Error Reporting Extended Capability Offsets
 */
#define	PCIE_AER_CAP			0x0	/* Enhanced Capability Header */
#define	PCIE_AER_UCE_STS		0x4	/* Uncorrectable Error Status */
#define	PCIE_AER_UCE_MASK		0x8	/* Uncorrectable Error Mask */
#define	PCIE_AER_UCE_SERV		0xc	/* Uncor Error Severity */
#define	PCIE_AER_CE_STS			0x10	/* Correctable Error Status */
#define	PCIE_AER_CE_MASK		0x14	/* Correctable Error Mask */
#define	PCIE_AER_CTL			0x18	/* AER Capability & Control */
#define	PCIE_AER_HDR_LOG		0x1c	/* Header Log */

/* Root Ports Only */
#define	PCIE_AER_RE_CMD			0x2c	/* Root Error Command */
#define	PCIE_AER_RE_STS			0x30	/* Root Error Status */
#define	PCIE_AER_CE_SRC_ID		0x34	/* Error Source ID */
#define	PCIE_AER_ERR_SRC_ID		0x36	/* Error Source ID */
#define	PCIE_AER_TLP_PRE_LOG		0x38	/* TLP Prefix Log */

/* Bridges Only */
#define	PCIE_AER_SUCE_STS		0x2c	/* Secondary UCE Status */
#define	PCIE_AER_SUCE_MASK		0x30	/* Secondary UCE Mask */
#define	PCIE_AER_SUCE_SERV		0x34	/* Secondary UCE Severity */
#define	PCIE_AER_SCTL			0x38	/* Secondary Cap & Ctl */
#define	PCIE_AER_SHDR_LOG		0x3c	/* Secondary Header Log */

/*
 * AER Uncorrectable Error Status/Mask/Severity Register
 */
#define	PCIE_AER_UCE_TRAINING		0x1	/* Training Error Status */
#define	PCIE_AER_UCE_DLP		0x10	/* Data Link Protocol Error */
#define	PCIE_AER_UCE_SD			0x20	/* Link Surprise down */
#define	PCIE_AER_UCE_PTLP		0x1000	/* Poisoned TLP Status */
#define	PCIE_AER_UCE_FCP		0x2000	/* Flow Control Protocol Sts */
#define	PCIE_AER_UCE_TO			0x4000	/* Completion Timeout Status */
#define	PCIE_AER_UCE_CA			0x8000	/* Completer Abort Status */
#define	PCIE_AER_UCE_UC			0x10000	/* Unexpected Completion Sts */
#define	PCIE_AER_UCE_RO			0x20000	/* Receiver Overflow Status */
#define	PCIE_AER_UCE_MTLP		0x40000	/* Malformed TLP Status */
#define	PCIE_AER_UCE_ECRC		0x80000	/* ECRC Error Status */
#define	PCIE_AER_UCE_UR			0x100000 /* Unsupported Req */
#define	PCIE_AER_UCE_BITS		(PCIE_AER_UCE_TRAINING | \
    PCIE_AER_UCE_DLP | PCIE_AER_UCE_SD | PCIE_AER_UCE_PTLP | \
    PCIE_AER_UCE_FCP | PCIE_AER_UCE_TO | PCIE_AER_UCE_CA | \
    PCIE_AER_UCE_UC | PCIE_AER_UCE_RO | PCIE_AER_UCE_MTLP | \
    PCIE_AER_UCE_ECRC | PCIE_AER_UCE_UR)
#define	PCIE_AER_UCE_LOG_BITS		(PCIE_AER_UCE_PTLP | PCIE_AER_UCE_CA | \
    PCIE_AER_UCE_UC | PCIE_AER_UCE_MTLP | PCIE_AER_UCE_ECRC | PCIE_AER_UCE_UR)

/*
 * AER Correctable Error Status/Mask Register
 */
#define	PCIE_AER_CE_RECEIVER_ERR	0x1	/* Receiver Error Status */
#define	PCIE_AER_CE_BAD_TLP		0x40	/* Bad TLP Status */
#define	PCIE_AER_CE_BAD_DLLP		0x80	/* Bad DLLP Status */
#define	PCIE_AER_CE_REPLAY_ROLLOVER	0x100	/* REPLAY_NUM Rollover Status */
#define	PCIE_AER_CE_REPLAY_TO		0x1000	/* Replay Timer Timeout Sts */
#define	PCIE_AER_CE_AD_NFE		0x2000	/* Advisory Non-Fatal Status */
#define	PCIE_AER_CE_BITS		(PCIE_AER_CE_RECEIVER_ERR | \
    PCIE_AER_CE_BAD_TLP | PCIE_AER_CE_BAD_DLLP | PCIE_AER_CE_REPLAY_ROLLOVER | \
    PCIE_AER_CE_REPLAY_TO)

/*
 * AER Capability & Control
 */
#define	PCIE_AER_CTL_FST_ERR_PTR_MASK	0x1F	/* First Error Pointer */
#define	PCIE_AER_CTL_ECRC_GEN_CAP	0x20	/* ECRC Generation Capable */
#define	PCIE_AER_CTL_ECRC_GEN_ENA	0x40	/* ECRC Generation Enable */
#define	PCIE_AER_CTL_ECRC_CHECK_CAP	0x80	/* ECRC Check Capable */
#define	PCIE_AER_CTL_ECRC_CHECK_ENA	0x100	/* ECRC Check Enable */

/*
 * AER Root Command Register
 */
#define	PCIE_AER_RE_CMD_CE_REP_EN	0x1	/* Correctable Error Enable */
#define	PCIE_AER_RE_CMD_NFE_REP_EN	0x2	/* Non-Fatal Error Enable */
#define	PCIE_AER_RE_CMD_FE_REP_EN	0x4	/* Fatal Error Enable */

/*
 * AER Root Error Status Register
 */
#define	PCIE_AER_RE_STS_CE_RCVD		0x1	/* ERR_COR Received */
#define	PCIE_AER_RE_STS_MUL_CE_RCVD	0x2	/* Multiple ERR_COR Received */
#define	PCIE_AER_RE_STS_FE_NFE_RCVD	0x4	/* FATAL/NON-FATAL Received */
#define	PCIE_AER_RE_STS_MUL_FE_NFE_RCVD	0x8	/* Multiple ERR_F/NF Received */
#define	PCIE_AER_RE_STS_FIRST_UC_FATAL	0x10	/* First Uncorrectable Fatal */
#define	PCIE_AER_RE_STS_NFE_MSGS_RCVD	0x20	/* Non-Fatal Error Msgs Rcvd */
#define	PCIE_AER_RE_STS_FE_MSGS_RCVD	0x40	/* Fatal Error Messages Rcvd */

#define	PCIE_AER_RE_STS_MSG_NUM_SHIFT	27	/* Offset of Intr Msg Number */
#define	PCIE_AER_RE_STS_MSG_NUM_MASK	0x1F	/* Intr Msg Number Mask */

/*
 * AER Error Source Identification Register
 */
#define	PCIE_AER_ERR_SRC_ID_CE_SHIFT	0	/* ERR_COR Source ID */
#define	PCIE_AER_ERR_SRC_ID_CE_MASK	0xFFFF
#define	PCIE_AER_ERR_SRC_ID_UE_SHIFT	16	/* ERR_FATAL/NONFATAL Src ID */
#define	PCIE_AER_ERR_SRC_ID_UE_MASK	0xFFFF

/*
 * AER Secondary Uncorrectable Error Register
 */
#define	PCIE_AER_SUCE_TA_ON_SC		0x1	/* Target Abort on Split Comp */
#define	PCIE_AER_SUCE_MA_ON_SC		0x2	/* Master Abort on Split Comp */
#define	PCIE_AER_SUCE_RCVD_TA		0x4	/* Received Target Abort */
#define	PCIE_AER_SUCE_RCVD_MA		0x8	/* Received Master Abort */
#define	PCIE_AER_SUCE_USC_ERR		0x20	/* Unexpected Split Comp Err */
#define	PCIE_AER_SUCE_USC_MSG_DATA_ERR	0x40	/* USC Message Data Error */
#define	PCIE_AER_SUCE_UC_DATA_ERR	0x80	/* Uncorrectable Data Error */
#define	PCIE_AER_SUCE_UC_ATTR_ERR	0x100	/* UC Attribute Err */
#define	PCIE_AER_SUCE_UC_ADDR_ERR	0x200	/* Uncorrectable Address Err */
#define	PCIE_AER_SUCE_TIMER_EXPIRED	0x400	/* Delayed xtion discard */
#define	PCIE_AER_SUCE_PERR_ASSERT	0x800	/* PERR Assertion Detected */
#define	PCIE_AER_SUCE_SERR_ASSERT	0x1000	/* SERR Assertion Detected */
#define	PCIE_AER_SUCE_INTERNAL_ERR	0x2000	/* Internal Bridge Err Detect */

#define	PCIE_AER_SUCE_HDR_CMD_LWR_MASK	0xF	/* Lower Command Mask */
#define	PCIE_AER_SUCE_HDR_CMD_LWR_SHIFT	4	/* Lower Command Shift */
#define	PCIE_AER_SUCE_HDR_CMD_UP_MASK	0xF	/* Upper Command Mask */
#define	PCIE_AER_SUCE_HDR_CMD_UP_SHIFT	8	/* Upper Command Shift */
#define	PCIE_AER_SUCE_HDR_ADDR_SHIFT	32	/* Upper Command Shift */

#define	PCIE_AER_SUCE_BITS		(PCIE_AER_SUCE_TA_ON_SC | \
    PCIE_AER_SUCE_MA_ON_SC | PCIE_AER_SUCE_RCVD_TA | PCIE_AER_SUCE_RCVD_MA | \
    PCIE_AER_SUCE_USC_ERR | PCIE_AER_SUCE_USC_MSG_DATA_ERR | \
    PCIE_AER_SUCE_UC_DATA_ERR | PCIE_AER_SUCE_UC_ATTR_ERR | \
    PCIE_AER_SUCE_UC_ADDR_ERR |	PCIE_AER_SUCE_TIMER_EXPIRED | \
    PCIE_AER_SUCE_PERR_ASSERT |	PCIE_AER_SUCE_SERR_ASSERT | \
    PCIE_AER_SUCE_INTERNAL_ERR)
#define	PCIE_AER_SUCE_LOG_BITS		(PCIE_AER_SUCE_TA_ON_SC | \
    PCIE_AER_SUCE_MA_ON_SC | PCIE_AER_SUCE_RCVD_TA | PCIE_AER_SUCE_RCVD_MA | \
    PCIE_AER_SUCE_USC_ERR | PCIE_AER_SUCE_USC_MSG_DATA_ERR | \
    PCIE_AER_SUCE_UC_DATA_ERR | PCIE_AER_SUCE_UC_ATTR_ERR | \
    PCIE_AER_SUCE_UC_ADDR_ERR |	PCIE_AER_SUCE_PERR_ASSERT)

/*
 * AER Secondary Capability & Control
 */
#define	PCIE_AER_SCTL_FST_ERR_PTR_MASK	0x1F	/* First Error Pointer */

/*
 * AER Secondary Headers
 * The Secondary Header Logs is 4 DW long.
 * The first 2 DW are split into 3 sections
 * o Transaction Attribute
 * o Transaction Command Lower
 * o Transaction Command Higher
 * The last 2 DW is the Transaction Address
 */
#define	PCIE_AER_SHDR_LOG_ATTR_MASK	0xFFFFFFFFF
#define	PCIE_AER_SHDR_LOG_CMD_LOW_MASK	0xF000000000
#define	PCIE_AER_SHDR_LOG_CMD_HIGH_MASK	0xF0000000000
#define	PCIE_AER_SHDR_LOG_ADDR_MASK	0xFFFFFFFFFFFFFFFF

/*
 * PCI-Express Device Serial Number Capability Offsets.
 */
#define	PCIE_SER_CAP		0x0	/* Enhanced Capability Header */
#define	PCIE_SER_SID_LOWER_DW	0x4	/* Lower 32-bit Serial Number */
#define	PCIE_SER_SID_UPPER_DW	0x8	/* Upper 32-bit Serial Number */

/*
 * ARI Capability Offsets
 */
#define	PCIE_ARI_HDR	0x0		/* Enhanced Capability Header */
#define	PCIE_ARI_CAP	0x4		/* ARI Capability Register */
#define	PCIE_ARI_CTL	0x6		/* ARI Control Register */

#define	PCIE_ARI_CAP_MFVC_FUNC_GRP	0x01
#define	PCIE_ARI_CAP_ASC_FUNC_GRP	0x02

#define	PCIE_ARI_CAP_NEXT_FUNC_SHIFT	8
#define	PCIE_ARI_CAP_NEXT_FUNC_MASK	0xffff

#define	PCIE_ARI_CTRL_MFVC_FUNC_GRP	0x01
#define	PCIE_ARI_CTRL_ASC_FUNC_GRP	0x02

#define	PCIE_ARI_CTRL_FUNC_GRP_SHIFT	4
#define	PCIE_ARI_CTRL_FUNC_GRP_MASK	0x7

#define	PCIE_ARI_MAX_FUNCTIONS		0x100

/*
 * PCIe Device 3 Extended Capability Header (PCIE_EXT_CAP_ID_DEV3)
 */
#define	PCIE_DEVCAP3		0x04
#define	PCIE_DEVCAP3_DMWR_REQ_ROUTE	0x01
#define	PCIE_DEVCAP3_14B_TAG_COMP_SUP	0x02
#define	PCIE_DEVCAP3_14B_TAG_REQ_SUP	0x04
#define	PCIE_DEVCAP3_PORT_L0P_SUP	0x08
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_MASK	0x070
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_MIN	0x0	/* < 1us */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_1us	0x1	/* [ 1us, 2us ) */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_2us	0x2	/* [ 2us, 4us ) */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_4us	0x3	/* [ 4us, 8us ) */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_8us	0x4	/* [ 8us, 16us ) */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_16us	0x5	/* [ 16us, 32us ) */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_32us	0x6	/* [ 32us, 64us ] */
#define	PCIE_DEVCAP3_PORT_L0P_EXIT_LAT_MAX	0x7	/* > 64us */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_MASK	0x380
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_MIN	0x0	/* < 1us */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_1us	0x1	/* [ 1us, 2us ) */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_2us	0x2	/* [ 2us, 4us ) */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_4us	0x3	/* [ 4us, 8us ) */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_8us	0x4	/* [ 8us, 16us ) */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_16us	0x5	/* [ 16us, 32us ) */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_32us	0x6	/* [ 32us, 64us ] */
#define	PCIE_DEVCAP3_RTMR_L0P_EXIT_LAT_MAX	0x7	/* > 64us */

#define	PCIE_DEVCTL3		0x08
#define	PCIE_DEVCTL3_DMWR_REQ_EN	0x01
#define	PCIE_DEVCTL3_DMWR_EG_BLOCK	0x02
#define	PCIE_DEVCTL3_14B_TAG_REQ_EN	0x04
#define	PCIE_DEVCTL3_L0P_EN		0x08
#define	PCIE_DEVCTL3_TARGET_WIDTH_MASK	0x70
#define	PCIE_DEVCTL3_TARGET_WIDTH_X1	0x00
#define	PCIE_DEVCTL3_TARGET_WIDTH_X2	0x10
#define	PCIE_DEVCTL3_TARGET_WIDTH_X4	0x20
#define	PCIE_DEVCTL3_TARGET_WIDTH_X8	0x30
#define	PCIE_DEVCTL3_TARGET_WIDTH_X16	0x40
#define	PCIE_DEVCTL3_TARGET_WIDTH_DYN	0x70

#define	PCIE_DEVSTS3		0x0c
#define	PCIE_DEVSTS3_INIT_WIDTH_MASK	0x07
#define	PCIE_DEVSTS3_INIT_WIDTH_X1	0x00
#define	PCIE_DEVSTS3_INIT_WIDTH_X2	0x01
#define	PCIE_DEVSTS3_INIT_WIDTH_X4	0x02
#define	PCIE_DEVSTS3_INIT_WIDTH_X8	0x03
#define	PCIE_DEVSTS3_INIT_WIDTH_X16	0x04
#define	PCIE_DEVSTS3_SEG_CAP		0x08
#define	PCIE_DEVSTS3_REM_L0P_SUP	0x10

/*
 * PCI-E Common TLP Header Fields
 */
#define	PCIE_TLP_FMT_3DW	0x00
#define	PCIE_TLP_FMT_4DW	0x20
#define	PCIE_TLP_FMT_3DW_DATA	0x40
#define	PCIE_TLP_FMT_4DW_DATA	0x60

#define	PCIE_TLP_TYPE_MEM	0x0
#define	PCIE_TLP_TYPE_MEMLK	0x1
#define	PCIE_TLP_TYPE_IO	0x2
#define	PCIE_TLP_TYPE_CFG0	0x4
#define	PCIE_TLP_TYPE_CFG1	0x5
#define	PCIE_TLP_TYPE_MSG	0x10
#define	PCIE_TLP_TYPE_CPL	0xA
#define	PCIE_TLP_TYPE_CPLLK	0xB
#define	PCIE_TLP_TYPE_MSI	0x18

#define	PCIE_TLP_MRD3		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_MEM)
#define	PCIE_TLP_MRD4		(PCIE_TLP_FMT_4DW | PCIE_TLP_TYPE_MEM)
#define	PCIE_TLP_MRDLK3		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_MEMLK)
#define	PCIE_TLP_MRDLK4		(PCIE_TLP_FMT_4DW | PCIE_TLP_TYPE_MEMLK)
#define	PCIE_TLP_MRDWR3		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_MEM)
#define	PCIE_TLP_MRDWR4		(PCIE_TLP_FMT_4DW_DATA | PCIE_TLP_TYPE_MEM)
#define	PCIE_TLP_IORD		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_IO)
#define	PCIE_TLP_IOWR		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_IO)
#define	PCIE_TLP_CFGRD0		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_CFG0)
#define	PCIE_TLP_CFGWR0		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_CFG0)
#define	PCIE_TLP_CFGRD1		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_CFG1)
#define	PCIE_TLP_CFGWR1		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_CFG1)
#define	PCIE_TLP_MSG		(PCIE_TLP_FMT_4DW | PCIE_TLP_TYPE_MSG)
#define	PCIE_TLP_MSGD		(PCIE_TLP_FMT_4DW_DATA | PCIE_TLP_TYPE_MSG)
#define	PCIE_TLP_CPL		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_CPL)
#define	PCIE_TLP_CPLD		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_CPL)
#define	PCIE_TLP_CPLLK		(PCIE_TLP_FMT_3DW | PCIE_TLP_TYPE_CPLLK)
#define	PCIE_TLP_CPLDLK		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_CPLLK)
#define	PCIE_TLP_MSI32		(PCIE_TLP_FMT_3DW_DATA | PCIE_TLP_TYPE_MSI)
#define	PCIE_TLP_MSI64		(PCIE_TLP_FMT_4DW_DATA | PCIE_TLP_TYPE_MSI)

typedef uint16_t pcie_req_id_t;

#define	PCIE_REQ_ID_BUS_SHIFT	8
#define	PCIE_REQ_ID_BUS_MASK	0xFF00
#define	PCIE_REQ_ID_DEV_SHIFT	3
#define	PCIE_REQ_ID_DEV_MASK	0x00F8
#define	PCIE_REQ_ID_FUNC_SHIFT	0
#define	PCIE_REQ_ID_FUNC_MASK	0x0007
#define	PCIE_REQ_ID_ARI_FUNC_MASK	0x00FF

#define	PCIE_CPL_STS_SUCCESS	0
#define	PCIE_CPL_STS_UR		1
#define	PCIE_CPL_STS_CRS	2
#define	PCIE_CPL_STS_CA		4

#if defined(_BIT_FIELDS_LTOH)
/*
 * PCI Express little-endian common TLP header format
 */
typedef struct pcie_tlp_hdr {
	uint32_t	len	:10,
			rsvd3   :2,
			attr    :2,
			ep	:1,
			td	:1,
			rsvd2   :4,
			tc	:3,
			rsvd1   :1,
			type    :5,
			fmt	:2,
			rsvd0   :1;
} pcie_tlp_hdr_t;

typedef struct pcie_mem64 {
	uint32_t	fbe	:4,
			lbe	:4,
			tag	:8,
			rid	:16;
	uint32_t	addr1;
	uint32_t	rsvd0   :2,
			addr0   :30;
} pcie_mem64_t;

typedef struct pcie_memio32 {
	uint32_t	fbe	:4,
			lbe	:4,
			tag	:8,
			rid	:16;
	uint32_t	rsvd0   :2,
			addr0   :30;
} pcie_memio32_t;

typedef struct pcie_cfg {
	uint32_t	fbe	:4,
			lbe	:4,
			tag	:8,
			rid	:16;
	uint32_t	rsvd1   :2,
			reg	:6,
			extreg  :4,
			rsvd0   :4,
			func    :3,
			dev	:5,
			bus	:8;
} pcie_cfg_t;

typedef struct pcie_cpl {
	uint32_t	bc	:12,
			bcm	:1,
			status  :3,
			cid	:16;
	uint32_t	laddr   :7,
			rsvd0   :1,
			tag	:8,
			rid	:16;
} pcie_cpl_t;

/*
 * PCI-Express Message Request Header
 */
typedef struct pcie_msg {
	uint32_t	msg_code:8,	/* DW1 */
			tag	:8,
			rid	:16;
	uint32_t	unused[2];	/* DW 2 & 3 */
} pcie_msg_t;

#elif defined(_BIT_FIELDS_HTOL)
/*
 * PCI Express big-endian common TLP header format
 */
typedef struct pcie_tlp_hdr {
	uint32_t	rsvd0	:1,
			fmt	:2,
			type	:5,
			rsvd1	:1,
			tc	:3,
			rsvd2	:4,
			td	:1,
			ep	:1,
			attr	:2,
			rsvd3	:2,
			len	:10;
} pcie_tlp_hdr_t;

typedef struct pcie_mem64 {
	uint32_t	rid	:16,
			tag	:8,
			lbe	:4,
			fbe	:4;
	uint32_t	addr1;
	uint32_t	addr0	:30,
			rsvd0	:2;
} pcie_mem64_t;

typedef struct pcie_memio32 {
	uint32_t	rid	:16,
			tag	:8,
			lbe	:4,
			fbe	:4;
	uint32_t	addr0	:30,
			rsvd0	:2;
} pcie_memio32_t;

typedef struct pcie_cfg {
	uint32_t	rid	:16,
			tag	:8,
			lbe	:4,
			fbe	:4;
	uint32_t	bus	:8,
			dev	:5,
			func	:3,
			rsvd0	:4,
			extreg	:4,
			reg	:6,
			rsvd1	:2;
} pcie_cfg_t;

typedef struct pcie_cpl {
	uint32_t	cid	:16,
			status	:3,
			bcm	:1,
			bc	:12;
	uint32_t	rid	:16,
			tag	:8,
			rsvd0	:1,
			laddr	:7;
} pcie_cpl_t;

/*
 * PCI-Express Message Request Header
 */
typedef struct pcie_msg {
	uint32_t	rid	:16,	/* DW1 */
			tag	:8,
			msg_code:8;
	uint32_t	unused[2];	/* DW 2 & 3 */
} pcie_msg_t;
#else
#error "bit field not defined"
#endif

#define	PCIE_MSG_CODE_ERR_COR		0x30
#define	PCIE_MSG_CODE_ERR_NONFATAL	0x31
#define	PCIE_MSG_CODE_ERR_FATAL		0x33

/*
 * Receiver preset hint encodings for PCIe Gen 3 (8 GT/s) receivers. These match
 * the PCIe Base 3/4/5 specification, section 4.2.3.2. These are used in the
 * Lane Equalization Control Register in the Secondary PCI Express Extended
 * Capability.
 */
#define	PCIE_GEN3_RX_PRESET_6DB		0
#define	PCIE_GEN3_RX_PRESET_7DB		1
#define	PCIE_GEN3_RX_PRESET_8DB		2
#define	PCIE_GEN3_RX_PRESET_9DB		3
#define	PCIE_GEN3_RX_PRESET_10DB	4
#define	PCIE_GEN3_RX_PRESET_11DB	5
#define	PCIE_GEN3_RX_PRESET_12DB	6
#define	PCIE_GEN3_RX_PRESET_RSVD	7

/*
 * The following are used for transmitter preset hints and are shared in all
 * PCIe versions from PCIe Gen 3+. Table 4.2.3.2 (PCIe 3/4/5) describes the
 * meaning of the transmitter hints. These basically correspond to 10 values
 * labeled P0-P10. Section 8.3.3.3 (PCIe 4/5) translates these into the
 * corresponding values in Table 8-1 Tx Preset Ratios and Corresponding
 * Coefficient Values.
 */
#define	PCIE_TX_PRESET_0	0
#define	PCIE_TX_PRESET_1	1
#define	PCIE_TX_PRESET_2	2
#define	PCIE_TX_PRESET_3	3
#define	PCIE_TX_PRESET_4	4
#define	PCIE_TX_PRESET_5	5
#define	PCIE_TX_PRESET_6	6
#define	PCIE_TX_PRESET_7	7
#define	PCIE_TX_PRESET_8	8
#define	PCIE_TX_PRESET_9	9
#define	PCIE_TX_PRESET_10	10

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCIE_H */
