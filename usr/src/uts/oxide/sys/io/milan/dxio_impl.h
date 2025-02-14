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

#ifndef _SYS_IO_MILAN_DXIO_IMPL_H
#define	_SYS_IO_MILAN_DXIO_IMPL_H

/*
 * Definitions for getting to the DXIO Engine configuration data format.
 */

#include <sys/param.h>
#include <sys/types.h>

#include <sys/io/zen/dxio_impl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These next structures are meant to assume standard x86 ILP32 alignment. These
 * structures are definitely Milan and firmware revision specific. Hence we have
 * different packing requirements from the dxio bits above.
 */
#pragma	pack(4)

#define	SMU_HOTPLUG_ENT_LAST	UINT_MAX

#pragma	pack()	/* pragma pack(4) */

extern const zen_dxio_fw_platform_t ethanolx_engine_s0;
extern const zen_dxio_fw_platform_t ethanolx_engine_s1;

extern const uint32_t ethanolx_pcie_slot_cap_entssd;
extern const uint32_t ethanolx_pcie_slot_cap_express;

extern const zen_dxio_fw_platform_t gimlet_engine;

/*
 * DXIO message codes. These are also specific to firmware.
 */
#define	MILAN_DXIO_OP_INIT		0x00
#define	MILAN_DXIO_OP_GET_SM_STATE	0x09
#define	MILAN_DXIO_OP_SET_LINK_SPEED	0x10
#define	MILAN_DXIO_OP_GET_VERSION	0x13
#define	MILAN_DXIO_OP_GET_ENGINE_CFG	0x14
#define	MILAN_DXIO_OP_SET_VARIABLE	0x22
#define	MILAN_DXIO_OP_LOAD_DATA		0x23
#define	MILAN_DXIO_OP_LOAD_CAPS		0x24
#define	MILAN_DXIO_OP_RELOAD_SM		0x2d
#define	MILAN_DXIO_OP_GET_ERROR_LOG	0x2b
#define	MILAN_DXIO_OP_SET_RUNTIME_PROP	0x3a
#define	MILAN_DXIO_OP_XGMI_BER_ADAPT	0x40
#define	MILAN_DXIO_OP_INIT_ESM		0x53

/*
 * The 0x300 in these are used to indicate deferred returns.
 */
#define	MILAN_DXIO_OP_START_SM		0x307
#define	MILAN_DXIO_OP_RESUME_SM		0x308

/*
 * Various DXIO Reply codes. Most of these codes are undocumented. In general,
 * most RPCs will return MILAN_DXIO_RPC_OK to indicate success. However, we have
 * seen MILAN_DXIO_OP_SET_VARIABLE actually return MILAN_DXIO_RPC_MBOX_IDLE as
 * it seems to actually be using the mailboxes under the hood.
 */
#define	MILAN_DXIO_RPC_NULL		0
#define	MILAN_DXIO_RPC_TIMEOUT		1
#define	MILAN_DXIO_RPC_ERROR		2
#define	MILAN_DXIO_RPC_OK		3
#define	MILAN_DXIO_RPC_UNKNOWN_LOCK	4
#define	MILAN_DXIO_RPC_EAGAIN		5
#define	MILAN_DXIO_RPC_MBOX_IDLE	6
#define	MILAN_DXIO_RPC_MBOX_BUSY	7
#define	MILAN_DXIO_RPC_MBOX_DONE	8

/*
 * Different data heaps that can be loaded.
 */
#define	MILAN_DXIO_HEAP_EMPTY		0x00
#define	MILAN_DXIO_HEAP_FABRIC_INIT	0x01
#define	MILAN_DXIO_HEAP_MACPCS		0x02
#define	MILAN_DXIO_HEAP_ENGINE_CONFIG	0x03
#define	MILAN_DXIO_HEAP_CAPABILITIES	0x04
#define	MILAN_DXIO_HEAP_GPIO		0x05
#define	MILAN_DXIO_HEAP_ANCILLARY	0x06

/*
 * Some commands refer to an explicit engine in their request.
 */
#define	MILAN_DXIO_ENGINE_NONE		0x00
#define	MILAN_DXIO_ENGINE_PCIE		0x01
#define	MILAN_DXIO_ENGINE_USB		0x02
#define	MILAN_DXIO_ENGINE_SATA		0x03

/*
 * The various variable codes that one can theoretically use with
 * MILAN_DXIO_OP_SET_VARIABLE.
 */
#define	MILAN_DXIO_VAR_SKIP_PSP			0x0d
#define	MLIAN_DXIO_VAR_RET_AFTER_MAP		0x0e
#define	MILAN_DXIO_VAR_RET_AFTER_CONF		0x0f
#define	MILAN_DXIO_VAR_ANCILLARY_V1		0x10
#define	MILAN_DXIO_VAR_NTB_HP_EN		0x11
#define	MILAN_DXIO_VAR_MAP_EXACT_MATCH		0x12
#define	MILAN_DXIO_VAR_S3_MODE			0x13
#define	MILAN_DXIO_VAR_PHY_PROG			0x14
#define	MILAN_DXIO_VAR_PCIE_COMPL		0x23
#define	MILAN_DXIO_VAR_SLIP_INTERVAL		0x24
#define	MILAN_DXIO_VAR_PCIE_POWER_OFF_DELAY	0x25

/*
 * The following are all values that can be used with
 * MILAN_DXIO_OP_SET_RUNTIME_PROP. It consists of various codes. Some of which
 * have their own codes.
 */
#define	MILAN_DXIO_RT_SET_CONF		0x00
#define	MILAN_DXIO_RT_SET_CONF_DXIO_WA		0x03
#define	MILAN_DXIO_RT_SET_CONF_SPC_WA		0x04
#define	MILAN_DXIO_RT_SET_CONF_FC_CRED_WA_DIS	0x05
#define	MILAN_DXIO_RT_SET_CONF_TX_CLOCK		0x07
#define	MILAN_DXIO_RT_SET_CONF_SRNS		0x08
#define	MILAN_DXIO_RT_SET_CONF_TX_FIFO_MODE	0x09
#define	MILAN_DXIO_RT_SET_CONF_DLF_WA_DIS	0x0a
#define	MILAN_DXIO_RT_SET_CONF_CE_SRAM_ECC	0x0b

#define	MILAN_DXIO_RT_CONF_PCIE_TRAIN	0x02
#define	MILAN_DXIO_RT_CONF_CLOCK_GATE	0x03
#define	MILAN_DXIO_RT_PLEASE_LEAVE	0x05
#define	MILAN_DXIO_RT_FORGET_BER	0x22


/*
 * PCIe Link Training States
 */
typedef enum milan_dxio_pcie_state {
	MILAN_DXIO_PCIE_ASSERT_RESET_GPIO	= 0x00,
	MILAN_DXIO_PCIE_ASSERT_RESET_DURATION	= 0x01,
	MILAN_DXIO_PCIE_DEASSERT_RESET_GPIO	= 0x02,
	MILAN_DXIO_PCIE_ASSERT_RESET_ENTRY	= 0x03,
	MILAN_DXIO_PCIE_GPIO_RESET_TIMEOUT	= 0x04,
	MILAN_DXIO_PCIE_RELEASE_LINK_TRAIN	= 0x05,
	MILAN_DXIO_PCIE_DETECT_PRESENCE		= 0x06,
	MILAN_DXIO_PCIE_DETECTING		= 0x07,
	MILAN_DXIO_PCIE_BAD_LANE		= 0x08,
	MILAN_DXIO_PCIE_GEN2_FAILURE		= 0x09,
	MILAN_DXIO_PCIE_REACHED_L0		= 0x0a,
	MILAN_DXIO_PCIE_VCO_NEGOTIATED		= 0x0b,
	MILAN_DXIO_PCIE_FORCE_RETRAIN		= 0x0c,
	MILAN_DXIO_PCIE_FAILED			= 0x0d,
	MILAN_DXIO_PCIE_SUCCESS			= 0x0e,
	MILAN_DXIO_PCIE_GRAPHICS_WORKAROUND	= 0x0f,
	MILAN_DXIO_PCIE_COMPLIANCE_MODE		= 0x10,
	MILAN_DXIO_PCIE_NO_DEVICE		= 0x11,
	MILAN_DXIO_PCIE_COMPLETED		= 0x12
} milan_dxio_pcie_state_t;

/*
 * When using MILAN_DXIO_OP_GET_SM_STATE, the following structure is actually
 * filled in via the RPC argument. This structure is more generally used amongst
 * different RPCs; however, since the state machine can often get different
 * types of requests this ends up mattering a bit more.
 */
typedef enum milan_dxio_data_type {
	MILAN_DXIO_DATA_TYPE_NONE	 = 0,
	MILAN_DXIO_DATA_TYPE_GENERIC,
	MILAN_DXIO_DATA_TYPE_SM,
	MILAN_DXIO_DATA_TYPE_HPSM,
	MILAN_DXIO_DATA_TYPE_RESET
} milan_dxio_data_type_t;

typedef struct milan_dxio_reply {
	milan_dxio_data_type_t	mds_type;
	uint8_t			mds_nargs;
	uint32_t		mds_arg0;
	uint32_t		mds_arg1;
	uint32_t		mds_arg2;
	uint32_t		mds_arg3;
} milan_dxio_reply_t;

/*
 * Types of DXIO Link speed updates. These must be ORed in with the base code.
 */
#define	MILAN_DXIO_LINK_SPEED_SINGLE	0x800

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_DXIO_IMPL_H */
