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

#ifndef _SYS_IO_TURIN_PCIE_H
#define	_SYS_IO_TURIN_PCIE_H

/*
 * Turin-specific register and bookkeeping definitions for PCIe root complexes,
 * ports, and bridges.
 */

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t turin_ioms_n_pcie_cores(const uint8_t);
extern uint8_t turin_pcie_core_n_ports(const uint8_t);

/*
 * PCIe related SMN addresses. This is determined based on a combination of
 * which IOMS we're on, which PCIe port we're on on the IOMS, and then finally
 * which PCIe bridge it is itself. We have broken this up into two separate
 * sub-units, one for per-port registers (the "core space") and one for
 * per-bridge registers ("port space").  There is a third sub-unit we don't
 * currently use where the common configuration space exists.
 *
 * The location of registers in each space is somewhat unusual; we've chosen to
 * model this so that in each unit the number of register (and sub-unit)
 * instances is fixed for a given sub-unit (unit). There are two reasons for
 * this: first, the number of register (sub-unit) instances varies depending on
 * the sub-unit (unit) instance number; second, the ioms and port instance
 * numbers are both used to compute the aperture base address.  To simplify our
 * implementation, we consider the bridge instance number to also form part of
 * the aperture base rather than treating the size of each port space as the
 * per-bridge register stride.  The upshot of this is that we ignore srd_nents
 * and srd_stride (more pointedly: they must not be set); similarly, all these
 * registers are 32 bits wide, so srd_size must be 0.
 *
 *       DXIO/COUNT            PPR         DEF    IOHC  IOHUB
 *       DXIO=PHY   IOMS CORE  NBIO/CORE   BUS    IDX   CLIENT BRIDGE
 * P0      0/16     0    0     0/0         0x00   0     PCIE0  1/[7:1], 2/[2:1]
 * G0 R   96/16     1    0     0/3         0x20   2     PCIE1  1/[7:1], 2/[2:1]
 * P2 R   48/16     2    0     1/0         0x40   0     PCIE0  1/[7:1], 2/[2:1]
 * G2    112/16     3    0     1/3         0x60   2     PCIE1  1/[7:1], 2/[2:1]
 * G1 R   64/16     4    0     0/1         0x80   1     PCIE2  1/[7:1], 2/[2:1]
 * P1     32/16     5    0     0/2         0xa0   3     PCIE3  1/[7:1], 2/[2:1]
 * G3     80/16     6    0     1/1         0xc0   1     PCIE2  1/[7:1], 2/[2:1]
 * P3 R   16/16     7    0     1/2         0xe0   3     PCIE3  1/[7:1], 2/[2:1]
 * P4    128/4      4    1     0/4         0x80   2     PCIE5  3/[7:1], 4/1
 */

static inline smn_reg_t
turin_pcie_core_smn_reg(const uint8_t iohcno, const smn_reg_def_t def,
    const uint8_t coreno)
{
	const uint32_t PCIE_CORE_SMN_REG_MASK = 0x7ffff;
	const uint32_t iohc32 = (const uint32_t)iohcno;
	const uint32_t size32 = (def.srd_size == 0) ? 4 :
	    (const uint32_t)def.srd_size;
	uint32_t inst = iohc32;
	if (coreno == 1)
		inst = 8;

	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_CORE);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(inst, <=, 8);
	ASSERT0(def.srd_reg & ~PCIE_CORE_SMN_REG_MASK);

	const uint32_t aperture_base = 0x1A380000;

	const uint32_t aperture_off = inst << 20;
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_CORE_SMN_REG_MASK);

	return (SMN_MAKE_REG_SIZED(aperture + def.srd_reg, size32));
}

static inline smn_reg_t
turin_pcie_port_smn_reg(const uint8_t iohcno, const smn_reg_def_t def,
    const uint8_t coreno, const uint8_t portno)
{
	const uint32_t PCIE_PORT_SMN_REG_MASK = 0xfff;
	const uint32_t iohc32 = (const uint32_t)iohcno;
	const uint32_t port32 = (const uint32_t)portno;
	const uint32_t size32 = (def.srd_size == 0) ? 4 :
	    (const uint32_t)def.srd_size;
	uint32_t inst = iohc32;
	if (coreno == 1)
		inst = 8;

	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_PORT);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(inst, <=, 8);
	ASSERT0(def.srd_reg & ~PCIE_PORT_SMN_REG_MASK);

	const uint32_t aperture_base = 0x1A340000;

	const uint32_t aperture_off = (inst << 20) + (port32 << 12);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_PORT_SMN_REG_MASK);

	return (SMN_MAKE_REG_SIZED(aperture + def.srd_reg, size32));
}

/*
 * PCIEPORT::PCIEP_HW_DEBUG - A bunch of mysterious bits that are used to
 * correct or override various hardware behaviors, presumably.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HW_DBG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x08	\
}
#define	PCIE_PORT_HW_DBG_SET_DBG15(r, v)		bitset32(r, 15, 15, v)
#define	PCIE_PORT_HW_DBG_SET_DBG13(r, v)		bitset32(r, 13, 13, v)

/*
 * PCIEPORT::PCIEP_HW_DEBUG_LC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HW_DBG_LC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x0c	\
}

/*
 * PCIEPORT::PCIEP_HW_DEBUG_TX - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HW_DBG_TX	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x10	\
}

/*
 * PCIEPORT::PCIEP_PORT_CNTL - General PCIe port controls. This is a register
 * that exists in 'Port Space' and is specific to a bridge.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_PCTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x40	\
}
#define	PCIE_PORT_PCTL_SET_PWRFLT_EN(r, v)		bitset32(r, 4, 4, v)

/*
 * PCIEPORT::PCIEP_SDP_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_SDP_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x44	\
}

/*
 * PCIEPORT::PCIEP_RX_EXT_CAP_AUTO_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_EXT_CAP_AUTO_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x48	\
}

/*
 * PCIEPORT::PCIE_PRIV_MSI_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_PRIV_MSI_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x52,	\
	.srd_size = 2		\
}

/*
 * PCIEPORT::PCIE_TX_REQUESTER_ID - Encodes information about the bridge's PCI
 * b/d/f.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_ID	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x84	\
}
#define	PCIE_PORT_TX_ID_SET_BUS(r, v)			bitset32(r, 15, 8, v)
#define	PCIE_PORT_TX_ID_SET_DEV(r, v)			bitset32(r, 7, 3, v)
#define	PCIE_PORT_TX_ID_SET_FUNC(r, v)			bitset32(r, 2, 0, v)

/*
 * PCIEPORT::PCIE_TX_SKID_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_SKID_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xbc	\
}

/*
 * PCIEPORT::PCIE_TX_SKID_CLKSW_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_SKID_CLKSW_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xc0	\
}

/*
 * PCIEPORT::PCIE_P_PORT_LANE_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_P_LANE_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x140	\
}

/*
 * PCIEPORT::PCIE_ERR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_ERR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1a8	\
}

/*
 * PCIEPORT::PCIE_STRAP_RX_TILE1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_STRAP_RX_TILE1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1b0	\
}

/*
 * PCIEPORT::PCIE_RX_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1c0	\
}

/*
 * PCIEPORT::PCIE_RX_EXPECTED_SEQNUM - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_EXP_SEQ	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1c4	\
}

/*
 * PCIEPORT::PCIE_RX_VENDOR_SPECIFIC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_VS_DLLP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1c8	\
}

/*
 * PCIEPORT::PCIE_RX_NOP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_NOP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1cc	\
}

/*
 * PCIEPORT::PCIE_RX_CNTL3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x1d0	\
}

/*
 * PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CREDITS_ALLOC_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x200	\
}

/*
 * PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CREDITS_ALLOC_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x204	\
}

/*
 * PCIEPORT::PCIE_RX_CREDITS_ALLOCATED_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CREDITS_ALLOC_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x208	\
}

/*
 * PCIEPORT::PCIEP_ERROR_INJECT_PHYSICAL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_ERR_INJ_PHYS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x20c	\
}

/*
 * PCIEPORT::PCIEP_ERROR_INJECT_TRANSACTION - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_ERR_INJ_TXN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x210	\
}

/*
 * PCIEPORT::PCIEP_AER_INJECT_TRANSACTION_SW_TRIG - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_AER_INJ_TXN_SW_TRIG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x214	\
}

/*
 * PCIEPORT::PCIEP_NAK_COUNTER - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_NAK_COUNTER	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x218	\
}

/*
 * PCIEPORT::PCIEP_RX_CAPTURED_LTR_CTRL_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CAPTURED_LTR_CTL_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x220	\
}

/*
 * PCIEPORT::PCIEP_RX_CAPTURED_LTR_THRESHOLD_VALUES - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_CAPTURED_LTR_THRESH_VALS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x224	\
}

/*
 * PCIEPORT::PCIEP_RX_FC_DEBUG_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_FC_DBG1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x228	\
}

/*
 * PCIEPORT::PCIEP_RX_FC_DEBUG_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_FC_DBG2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x22c	\
}

/*
 * PCIEPORT::PCIE_AER_PRIV_UNCORRECTABLE_MASK - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_AER_PRIV_UNCORRECTABLE_MASK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x230	\
}

/*
 * PCIEPORT::PCIE_AER_PRIV_TRIGGER - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_AER_PRIV_TRIGGER	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x234	\
}

/*
 * PCIEPORT::PCIEP_RSMU_INT_DISABLE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RSMU_INT_DISLE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x238	\
}

/*
 * PCIEPORT::PCIEP_RX_FC_DEBUG_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_FC_DBG_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x240	\
}

/*
 * PCIEPORT::PCIEP_RX_FC_DEBUG_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_FC_DBG_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x244	\
}

/*
 * PCIEPORT::PCIEP_RX_FC_DEBUG_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RX_FC_DBG_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x248	\
}

/*
 * PCIEPORT::PCIE_CXL_QOS_CTRL1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CXL_QOS_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x254	\
}

/*
 * PCIEPORT::PCIE_CXL_QOS_CTRL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CXL_QOS_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x258	\
}

/*
 * PCIEPORT::PCIE_CXL_QOS_CTRL3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CXL_QOS_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x25c	\
}

/*
 * PCIEPORT::PCIE_CXL_QOS_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CXL_QOS_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x260	\
}

/*
 * PCIEPORT::PCIEP_CXL_ISO_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CXL_ISO_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x264	\
}

/*
 * PCIEPORT::PCIEP_CXL_ISO_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CXL_ISO_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x268	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL - The first of several link controller control
 * registers.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x280	\
}
#define	PCIE_PORT_LC_CTL_SET_L1_IMM_ACK(r, v)		bitset32(r, 23, 23, v)

/*
 * PCIEPORT::PCIE_LC_TRAINING_CNTL - Port Link Training Control. This register
 * seems to control some amount of the general aspects of link training.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_TRAIN_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x284	\
}
#define	PCIE_PORT_LC_TRAIN_CTL_SET_TRAINBITS_DIS(r, v)	bitset32(r, 13, 13, v)
#define	PCIE_PORT_LC_TRAIN_CTL_SET_L0S_L1_TRAIN(r, v)	bitset32(r, 6, 6, v)

/*
 * PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL - Port Link Width Control Register. This
 * register is used as part of controlling the width during training.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_WIDTH_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x288	\
}
#define	PCIE_PORT_LC_WIDTH_CTL_SET_TURN_OFF_UNUSED_LANES(r, v) \
    bitset32(r, 30, 30, v)
#define	PCIE_PORT_LC_WIDTH_CTL_SET_DUAL_RECONFIG(r, v)	bitset32(r, 19, 19, v)
#define	PCIE_PORT_LC_WIDTH_CTL_SET_RENEG_EN(r, v)	bitset32(r, 10, 10, v)

/*
 * PCIEPORT::PCIE_LC_N_FTS_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_NFTS_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x28c	\
}

/*
 * PCIEPORT::PCIE_LC_SPEED_CNTL - Link speed control register. This is used to
 * see what has happened with training and could in theory be used to control
 * things. This is generally used for observability / debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_SPEED_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x290	\
}
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN5(r)	bitx32(r, 29, 29)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN5(r)	bitx32(r, 28, 28)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN4(r)	bitx32(r, 27, 27)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN4(r)	bitx32(r, 26, 26)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN3(r)	bitx32(r, 25, 25)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN3(r)	bitx32(r, 24, 24)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN2(r)	bitx32(r, 23, 23)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN2(r)	bitx32(r, 22, 22)
#define	PCIE_PORT_LC_SPEED_CTL_GET_CHECK_RATE(r)	bitx32(r, 21, 21)
#define	PCIE_PORT_LC_SPEED_CTL_GET_OVR_RATE(r)		bitx32(r, 14, 12)
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_2P5				0
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_5P0				1
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_8P0				2
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_16P0				3
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_32P0				4
#define	PCIE_PORT_LC_SPEED_CTL_GET_OVR_EN(r)		bitx32(r, 11, 11)
#define	PCIE_PORT_LC_SPEED_CTL_GET_ADV_RATE(r)		bitx32(r, 10, 8)
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_2P5				0
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_5P0				1
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_8P0				2
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_16P0				3
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_32P0				4
#define	PCIE_PORT_LC_SPEED_CTL_GET_CUR_RATE(r)		bitx32(r, 7, 5)
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_2P5				0
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_5P0				1
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_8P0				2
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_16P0				3
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_32P0				4
#define	PCIE_PORT_LC_SPEED_CTL_GET_GEN5_EN(r)		bitx32(r, 3, 3)
#define	PCIE_PORT_LC_SPEED_CTL_GET_GEN4_EN(r)		bitx32(r, 2, 2)
#define	PCIE_PORT_LC_SPEED_CTL_GET_GEN3_EN(r)		bitx32(r, 1, 1)
#define	PCIE_PORT_LC_SPEED_CTL_GET_GEN2_EN(r)		bitx32(r, 0, 0)

/*
 * PCIEPORT::PCIE_LC_STATE0 - Link Controller State 0 register. All the various
 * Link Controller state registers follow the same pattern, just keeping older
 * and older things in them. That is, you can calculate a given state by
 * multiplying the register number by four. Unfortunately, the meanings of the
 * states are more unknown, but we have reason to expect that at least 0x10 is
 * one of several successful training states. Note that additional history can
 * be captured in the parent core's registers for a single port selected in the
 * core's DBG_CTL (it's unclear what selecting multiple ports would do).
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x294	\
}

/*
 * These macros are generic for use across all PCIEPORT::PCIE_LC_STATE[11:0]
 */
#define	PCIE_PORT_LC_STATE_GET_PREV3(r)			bitx32(r, 29, 24)
#define	PCIE_PORT_LC_STATE_GET_PREV2(r)			bitx32(r, 21, 16)
#define	PCIE_PORT_LC_STATE_GET_PREV1(r)			bitx32(r, 13, 8)
#define	PCIE_PORT_LC_STATE_GET_CUR(r)			bitx32(r, 5, 0)

/*
 * PCIEPORT::PCIE_LC_STATE1 - Link Controller State 1 register.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x298	\
}

/*
 * PCIEPORT::PCIE_LC_STATE2 - Link Controller State 2 register.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x29c	\
}

/*
 * PCIEPORT::PCIE_LC_STATE3 - Link Controller State 3 register.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a0	\
}

/*
 * PCIEPORT::PCIE_LC_STATE4 - Link Controller State 4 register.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a4	\
}

/*
 * PCIEPORT::PCIE_LC_STATE5 - Link Controller State 5 register.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a8	\
}

/*
 * PCIEPORT::PCIE_LC_LINK_MANAGEMENT_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_LINK_MGMT_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2ac	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL2 - Port Link Control Register 2.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2c4	\
}
#define	PCIE_PORT_LC_CTL2_SET_ELEC_IDLE(r, v)		bitset32(r, 15, 14, v)
/*
 * These all have the same values as the corresponding
 * PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_<num> values.
 */
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M0					0
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M1					1
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M2					2
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M3					3
#define	PCIE_PORT_LC_CTL2_SET_TS2_CHANGE_REQ(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_PORT_LC_CTL2_TS2_CHANGE_16					0
#define	PCIE_PORT_LC_CTL2_TS2_CHANGE_128				1

/*
 * PCIEPORT::PCIE_LC_BW_CHANGE_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_BW_CHANGE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2c8	\
}

/*
 * PCIEPORT::PCIE_LC_CDR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CDR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2cc	\
}

/*
 * PCIEPORT::PCIE_LC_LANE_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_LANE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2d0	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL3 - Port Link Control Register 3. This isn't the last
 * of these and is a bunch of different settings.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2d4	\
}
#define	PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(r, v)	bitset32(r, 12, 12, v)
#define	PCIE_PORT_LC_CTL3_SET_RCVR_DET_OVR(r, v)	bitset32(r, 11, 11, v)
#define	PCIE_PORT_LC_CTL3_SET_ENH_HP_EN(r, v)		bitset32(r, 10, 10, v)

/*
 * PCIEPORT::PCIE_LC_CNTL4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2d8	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL5 - Port Link Control Register 5. There are several
 * others, but this one seems to be required for hotplug. Some fields in this
 * register capture data for a lane selected by LC_DBG_CTL in the port's parent
 * core.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2dc	\
}
#define	PCIE_PORT_LC_CTL5_SET_WAIT_DETECT(r, v)		bitset32(r, 28, 28, v)

/*
 * PCIEPORT::PCIE_LC_FORCE_COEFF - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_COEFF	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2e0	\
}

/*
 * PCIEPORT::PCIE_LC_BEST_EQ_SETTINGS - unused but captured for debugging. Data
 * captured in this register's fields applies to a lane selected by the
 * LC_DBG_CTL register in the port's parent core.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_BEST_EQ	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2e4	\
}

/*
 * PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF - unused but captured for debugging.
 * Data captured in some of this register's fields applies to a lane selected by
 * the LC_DBG_CTL register in the port's parent core.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_EQ_COEFF	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2e8	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL6 - Port Link Control Register 6. SRIS stuff lives
 * here, with other bits. Some field (not described here because they are not
 * used) capture data for a specific lane set in the parent core's LC_DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2ec	\
}
#define	PCIE_PORT_LC_CTL6_GET_SRIS_AUTODET_MODE(r)	bitx32(r, 24, 23)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_AUTODET_MODE(r, v)	bitset32(r, 24, 23, v)
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_MODE_SKP_OS_INT_LK		0
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_MODE_DYN_SKP_OS_INT_LK		1
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_MODE_FE_NOM_EMPTY		2
#define	PCIE_PORT_LC_CTL6_GET_SRIS_AUTODET_FACTOR(r)	bitx32(r, 22, 21)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_AUTODET_FACTOR(r, v)	bitset32(r, 22, 21, v)
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_1X			0
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_0_95X			1
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_0_9X			2
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_0_85X			3
#define	PCIE_PORT_LC_CTL6_GET_SRIS_AUTODET_EN(r)	bitx32(r, 20, 20)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_AUTODET_EN(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_PORT_LC_CTL6_GET_SRIS_EN(r)		bitx32(r, 12, 12)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_EN(r, v)		bitset32(r, 12, 12, v)
#define	PCIE_PORT_LC_CTL6_GET_SPC_MODE_32GT(r)		bitx32(r, 9, 8)
#define	PCIE_PORT_LC_CTL6_SET_SPC_MODE_32GT(r, v)	bitset32(r, 9, 8, v)
#define	PCIE_PORT_LC_CTL6_SPC_MODE_32GT_2				1
#define	PCIE_PORT_LC_CTL6_SPC_MODE_32GT_4				2
#define	PCIE_PORT_LC_CTL6_GET_SPC_MODE_16GT(r)		bitx32(r, 7, 6)
#define	PCIE_PORT_LC_CTL6_SET_SPC_MODE_16GT(r, v)	bitset32(r, 7, 6, v)
#define	PCIE_PORT_LC_CTL6_SPC_MODE_16GT_2				1
#define	PCIE_PORT_LC_CTL6_SPC_MODE_16GT_4				2
#define	PCIE_PORT_LC_CTL6_GET_SPC_MODE_8GT(r)		bitx32(r, 5, 4)
#define	PCIE_PORT_LC_CTL6_SET_SPC_MODE_8GT(r, v)	bitset32(r, 5, 4, v)
#define	PCIE_PORT_LC_CTL6_SPC_MODE_8GT_2				1
#define	PCIE_PORT_LC_CTL6_SPC_MODE_8GT_4				2
#define	PCIE_PORT_LC_CTL6_GET_SPC_MODE_5GT(r)		bitx32(r, 3, 2)
#define	PCIE_PORT_LC_CTL6_SET_SPC_MODE_5GT(r, v)	bitset32(r, 3, 2, v)
#define	PCIE_PORT_LC_CTL6_SPC_MODE_5GT_1				0
#define	PCIE_PORT_LC_CTL6_SPC_MODE_5GT_2				1
#define	PCIE_PORT_LC_CTL6_GET_SPC_MODE_2P5GT(r)		bitx32(r, 1, 0)
#define	PCIE_PORT_LC_CTL6_SET_SPC_MODE_2P5GT(r, v)	bitset32(r, 1, 0, v)
#define	PCIE_PORT_LC_CTL6_SPC_MODE_2P5GT_1				0
#define	PCIE_PORT_LC_CTL6_SPC_MODE_2P5GT_2				1

/*
 * PCIEPORT::PCIE_LC_CNTL7 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2f0	\
}

/*
 * PCIEPORT::PCIE_LINK_MANAGEMENT_STATUS - unused but captured for debugging.
 * Fields are RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LINK_MGMT_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2f4	\
}

/*
 * PCIEPORT::PCIE_LINK_MANAGEMENT_MASK - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LINK_MGMT_MASK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2f8	\
}

/*
 * PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LINK_MGMT_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2fc	\
}

/*
 * PCIEPORT::PCIEP_STRAP_LC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_STRAP_LC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x300	\
}

/*
 * PCIEPORT::PCIEP_STRAP_MISC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_STRAP_MISC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x304	\
}

/*
 * PCIEPORT::PCIEP_STRAP_LC2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_STRAP_LC2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x308	\
}

/*
 * PCIEPORT::PCIE_LC_L1_PM_SUBSTATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_L1_PM_SUBSTATE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x318	\
}

/*
 * PCIEPORT::PCIE_LC_L1_PM_SUBSTATE2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_L1_PM_SUBSTATE2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x31c	\
}

/*
 * PCIEPORT::PCIE_LC_L1_PM_SUBSTATE3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_L1_PM_SUBSTATE3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x320	\
}

/*
 * PCIEPORT::PCIE_LC_L1_PM_SUBSTATE4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_L1_PM_SUBSTATE4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x324	\
}

/*
 * PCIEPORT::PCIE_LC_L1_PM_SUBSTATE5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_L1_PM_SUBSTATE5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x328	\
}

/*
 * PCIEPORT::PCIEP_BCH_ECC_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_BCH_ECC_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x340	\
}

/*
 * PCIEPORT::PCIEP_HPGI_PRIVATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HPGI_PRIV	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x348	\
}

/*
 * PCIEPORT::PCIEP_HPGI - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HPGI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x368	\
}

/*
 * PCIEPORT::PCIEP_HCNT_DESCRIPTOR - Port Hotplug Descriptor control. This is a
 * register that exists in 'Port Space' and is specific to a bridge. This seems
 * to relate something in the port to the SMU's hotplug engine.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HP_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x36c	\
}
#define	PCIE_PORT_HP_CTL_SET_ACTIVE(r, v)		bitset32(r, 31, 31, v)
#define	PCIE_PORT_HP_CTL_SET_SLOT(r, v)			bitset32(r, 12, 0, v)

/*
 * PCIEPORT::PCIEP_PERF_CNTL_COUNT_TXCLK - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_PERF_CTL_COUNT_TXCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x370	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL8 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL8	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x374	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL9 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL9	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x378	\
}

/*
 * PCIEPORT::PCIE_LC_FORCE_COEFF2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_COEFF2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x37c	\
}

/*
 * PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF2 - unused but captured for debugging.
 * Data captured in some of this register's fields applies to a lane selected by
 * the LC_DBG_CTL register in the port's parent core.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_EQ_COEFF2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x380	\
}

/*
 * PCIEPORT::PCIEP_PERF_CNTL_COUNT_TXCLK_LC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_PERF_CTL_COUNT_TXCLK_LC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x384	\
}

/*
 * PCIEPORT::PCIE_LC_FINE_GRAIN_CLK_GATE_OVERRIDES - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FINE_GRAIN_CLK_GATE_OVR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x388	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL10 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL10	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x38c	\
}

/*
 * PCIEPORT::PCIE_LC_EQ_CNTL_8GT - Used to set equalization search modes etc.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_EQ_CTL_8GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x390	\
}
#define	PCIE_PORT_LC_EQ_CTL_8GT_SET_SKIP_PH23(r, v)	bitset32(r, 6, 6, v)
#define	PCIE_PORT_LC_EQ_CTL_8GT_SET_SEARCH_MODE(r, v)	bitset32(r, 3, 2, v)
#define	PCIE_PORT_LC_EQ_CTL_8GT_SEARCH_MODE_CB				0
#define	PCIE_PORT_LC_EQ_CTL_8GT_SEARCH_MODE_CE				1
#define	PCIE_PORT_LC_EQ_CTL_8GT_SEARCH_MODE_CE3X3			2
#define	PCIE_PORT_LC_EQ_CTL_8GT_SEARCH_MODE_PRESET			3

/*
 * PCIEPORT::PCIE_LC_EQ_CNTL_16GT - Used to set equalization search modes etc.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_EQ_CTL_16GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x394	\
}
#define	PCIE_PORT_LC_EQ_CTL_16GT_SET_SKIP_PH23(r, v)	bitset32(r, 6, 6, v)
#define	PCIE_PORT_LC_EQ_CTL_16GT_SET_SEARCH_MODE(r, v)	bitset32(r, 3, 2, v)
#define	PCIE_PORT_LC_EQ_CTL_16GT_SEARCH_MODE_CB				0
#define	PCIE_PORT_LC_EQ_CTL_16GT_SEARCH_MODE_CE				1
#define	PCIE_PORT_LC_EQ_CTL_16GT_SEARCH_MODE_CE3X3			2
#define	PCIE_PORT_LC_EQ_CTL_16GT_SEARCH_MODE_PRESET			3

/*
 * PCIEPORT::PCIE_LC_SAVE_RESTORE_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_SAVE_RESTORE1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x398	\
}

/*
 * PCIEPORT::PCIE_LC_SAVE_RESTORE_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_SAVE_RESTORE2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x39c	\
}

/*
 * PCIEPORT::PCIE_LC_SAVE_RESTORE_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_SAVE_RESTORE3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x3a0	\
}

/*
 * PCIEPORT::PCIE_LC_EQ_CNTL_32GT - Used to set equalization search modes etc.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_EQ_CTL_32GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x400	\
}
#define	PCIE_PORT_LC_EQ_CTL_32GT_SET_SKIP_PH23(r, v)	bitset32(r, 6, 6, v)
#define	PCIE_PORT_LC_EQ_CTL_32GT_SET_SEARCH_MODE(r, v)	bitset32(r, 3, 2, v)
#define	PCIE_PORT_LC_EQ_CTL_32GT_SEARCH_MODE_CB				0
#define	PCIE_PORT_LC_EQ_CTL_32GT_SEARCH_MODE_CE				1
#define	PCIE_PORT_LC_EQ_CTL_32GT_SEARCH_MODE_CE3X3			2
#define	PCIE_PORT_LC_EQ_CTL_32GT_SEARCH_MODE_PRESET			3

/*
 * PCIEPORT::PCIE_LC_PRESET_MASK_CNTL - Used to control preset masks.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_PRST_MASK_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x404	\
}
#define	PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_32GT(r, v) \
    bitset32(r, 29, 20, v)
#define	PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_16GT(r, v) \
    bitset32(r, 19, 10, v)
#define	PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_8GT(r, v) \
    bitset32(r, 9, 0, v)

/*
 * PCIEPORT::PCIE_LC_RXRECOVER_RXSTANDBY_CNTL - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_RXRCOV_RXSBY_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x408	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL11 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL11	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x40c	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL12 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL12	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x410	\
}

/*
 * PCIEPORT::PCIE_LC_SPEED_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_SPEED_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x414	\
}

/*
 * PCIEPORT::PCIE_LC_FORCE_COEFF3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_COEFF3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x418	\
}

/*
 * PCIEPORT::PCIE_LC_FORCE_EQ_REQ_COEFF3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_EQ_REQ_COEFF3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x41c	\
}

/*
 * PCIEPORT::PCIE_LC_LINK_MANAGEMENT_CNTL3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_LINK_MGMT_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x420	\
}

/*
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1 - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ALT_PROT_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x424	\
}

/*
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL2 - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ALT_PROT_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x428	\
}

/*
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL3 - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ALT_PROT_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x42c	\
}

/*
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL4 - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ALT_PROT_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x430	\
}

/*
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL5 - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ALT_PROT_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x434	\
}

/*
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6 - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ALT_PROT_CTL6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x438	\
}

/*
 * PCIEPORT::PCIE_LC_Z10_IDLE_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_Z10_IDLE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x43c	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x440	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x444	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x44c	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x450	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x454	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL6 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x458	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CNTL9 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CTL9	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x45c	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_IOVLSM_STATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_IOVLSM_STATE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x460	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_CAMEMVLSM_STATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_CAMEMVLSM_STATE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x464	\
}

/*
 * PCIEPORT::PCIE_LC_TRANMIT_FIFO_CDC_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_TRANMIT_FIFO_CDC_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x468	\
}

/*
 * PCIEPORT::PCIE_LC_LTSSM_CXL_CNTL_EXTRA - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_LTSSM_CXL_CTL_EXTRA	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x46c	\
}

/*
 * PCIEPORT::PCIE_LC_CNTL13 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL13	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x470	\
}

/*
 * PCIEPORT::PCIE_LC_ARBMUX_ERR_ISO_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_ARBMUX_ERR_ISO_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x478	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_CNTL_8GT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_CTL_8GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4d4	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_CNTL_16GT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_CTL_16GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4d8	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_CNTL_32GT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_CTL_32GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4dc	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4e0	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4e4	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4e8	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4ec	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4f0	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4f4	\
}

/*
 * PCIEPORT::PCIE_LC_FAPE_SETTINGS_GROUP_6 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAPE_SET_GRP6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4f8	\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_CNTL0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x4fc	\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_EVALUATED_SETTINGS_STATUS_LANE - unused but captured
 * for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_EVAL_SET_STS_LANE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x5f0	\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_SETTINGS_CNTL_1_LANE - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_SET_CTL_1_LANE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x5f4,	\
	.srd_size = 2		\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_SETTINGS_CNTL_2_LANE - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_SET_CTL_2_LANE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x5f6,	\
	.srd_size = 2		\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_SETTINGS_CNTL_FINAL_LANE - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_SET_CTL_FINAL_LANE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x5f8,	\
	.srd_size = 2		\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_SETTINGS_RESERVED_A_LANE - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_SET_RSVD_A_LANE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x5fa,	\
	.srd_size = 2		\
}

/*
 * PCIEPORT::PCIE_LC_FAAE_SETTINGS_RESERVED_B_LANE - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FAAE_SET_RSVD_B_LANE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x5fc	\
}

/*
 * PCIEPORT::PCIE_TX_PORT_CTRL_1 - PCIe TX Control. This is a register that
 * exists in 'Port Space' and is specific to a bridge. XXX figure out what other
 * bits we need.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_PORT_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x600	\
}
#define	PCIE_PORT_TX_PORT_CTL1_SET_CPL_PASS(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_PORT_TX_PORT_CTL1_SET_TLP_FLUSH_DOWN_DIS(r, v) \
    bitset32(r, 15, 15, v)

/*
 * PCIEPORT::PCIE_TX_PORT_CTRL_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_PORT_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x604	\
}

/*
 * PCIEPORT::PCIE_TX_SEQ - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_SEQ	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x620	\
}

/*
 * PCIEPORT::PCIE_TX_REPLAY - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_REPLAY	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x624	\
}

/*
 * PCIEPORT::PCIE_TX_REPLAY_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_REPLAY2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x628	\
}

/*
 * PCIEPORT::PCIE_TX_ACK_LATENCY_LIMIT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_ACK_LAT_LIM	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x630	\
}

/*
 * PCIEPORT::PCIE_TX_CREDIT_RELEASE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDIT_RELEASE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x634	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_FCU_THRESHOLD - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_FCU_THRESH	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x640	\
}

/*
 * PCIEPORT::PCIE_TX_FCU_TIMER_LIMIT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_FCU_TIMER_LIM	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x644	\
}

/*
 * PCIEPORT::PCIE_TX_VENDOR_SPECIFIC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_VS_DLLP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x650	\
}

/*
 * PCIEPORT::PCIE_TX_NOP_DLLP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_NOP_DLLP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x654	\
}

/*
 * PCIEPORT::PCIE_TX_DLLSM_HISTORY_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_DLLSM_HISTORY0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x658	\
}

/*
 * PCIEPORT::PCIE_TX_DLLSM_HISTORY_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_DLLSM_HISTORY1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x65c	\
}

/*
 * PCIEPORT::PCIE_TX_REQUEST_NUM_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_REQ_NUM_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x660	\
}

/*
 * PCIEPORT::PCIE_TX_ERR_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_ERR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x670	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_ADVT_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_ADVT_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x680	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_ADVT_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_ADVT_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x684	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_ADVT_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_ADVT_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x688	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_INIT_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_INIT_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x68c	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_INIT_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_INIT_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x690	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_INIT_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_INIT_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x694	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_STATUS - unused but captured for debugging. Some
 * fields are RW1c (read/write-1-to-clear).
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x698	\
}

/*
 * PCIEPORT::PCIE_FC_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6a0	\
}

/*
 * PCIEPORT::PCIE_FC_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6a4	\
}

/*
 * PCIEPORT::PCIE_FC_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6a8	\
}

/*
 * PCIEPORT::PCIE_FC_P_VC1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_P_VC1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6ac	\
}

/*
 * PCIEPORT::PCIE_FC_NP_VC1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_NP_VC1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6b0	\
}

/*
 * PCIEPORT::PCIE_FC_CPL_VC1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_CPL_VC1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6b4	\
}

/*
 * PCIEPORT::PCIE_SEND_MORE_INITFC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_SEND_MORE_INITFC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6b8	\
}

/*
 * PCIEPORT::PCIE_TX_FCP_CREDITS_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_FCP_CREDITS_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6c0	\
}

/*
 * PCIEPORT::PCIE_TX_FCNP_CREDITS_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_FCNP_CREDITS_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6c4	\
}

/*
 * PCIEPORT::PCIE_TX_FCCPL_CREDITS_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_FCCPL_CREDITS_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6c8	\
}

/*
 * PCIEPORT::PCIE_BW_MONITOR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_BW_MONITOR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6f8	\
}

/*
 * PCIEPORT::PCIE_BW_MONITOR_COUNT1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_BW_MONITOR_COUNT1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x6fc	\
}

/*
 * PCIEPORT::PCIE_BW_MONITOR_COUNT2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_BW_MONITOR_COUNT2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x700	\
}

/*
 * PCIEPORT::PCIE_MST_PORT_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_MST_PORT_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x704	\
}

/*
 * PCIEPORT::PCIEP_RCB_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_RCB_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x800	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x08	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_LC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_LC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x0c	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_TX - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_TX	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x10	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_TXRCB_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_TXRCB_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x14	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_LCRXP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_LCRXP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1c	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_TXRX_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_TXRX_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x20	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_TXLC_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_TXLC_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x24	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_RXTX_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_RXTX_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x28	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_RXLC_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_RXLC_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2c	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_LCTX_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_LCTX_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x30	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_LCRX_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_LCRX_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x34	\
}

/*
 * PCIECORE::PCIE_RX_NUM_NAK - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_NUM_NAK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x38	\
}

/*
 * PCIECORE::PCIE_RX_NUM_NAK_GENERATED - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_NUM_NAK_GEN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x3c	\
}

/*
 * PCIECORE::PCIE_CNTL - PCIe port level controls, generally around reordering,
 * error reporting, and additional fields.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x40	\
}
#define	PCIE_CORE_PCIE_CTL_SET_HW_LOCK(r, v)		bitset32(r, 0, 0, v)

/*
 * PCIECORE::PCIE_CONFIG_CNTL - unused but captured for debugging. There is
 * *also* a PCIE_CFG_CNTL at 0xf0. We keep our conventions but add
 * disambiguating characters to avoid confusion.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CFG_CTL_CONFIG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x44	\
}

/*
 * PCIECORE::PCIE_CXL_ERR_AER_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CXL_ERR_AER_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4c	\
}

/*
 * PCIECORE::PCIE_RX_CNTL5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x60	\
}

/*
 * PCIECORE::PCIE_RX_CNTL4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x64	\
}

/*
 * PCIECORE::PCIE_COMMON_AER_MASK - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_COMMON_AER_MASK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x68	\
}

/*
 * PCIECORE::PCIE_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x70	\
}

/*
 * PCIECORE::PCIE_RX_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x74	\
}

/*
 * PCIECORE::PCIE_Z10_DEBUG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_Z10_DBG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x78	\
}

/*
 * PCIECORE::PCIE_SLV_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SLV_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x7c	\
}

/*
 * PCIECORE::PCIE_CI_CNTL - PCIe Port level TX controls. Note, this register is
 * in 'core' space and is specific to the overall turin_pcie_core_t, as opposed
 * to the port or bridge.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CI_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x80	\
}
#define	PCIE_CORE_CI_CTL_SET_SLV_CMENT_MODE(r, v)	bitset32(r, 31, 31, v)
#define	PCIE_CORE_CI_CTL_SET_ARB_WIDTH_WEIGHTED_RR(r, v) \
    bitset32(r, 30, 30, v)
#define	PCIE_CORE_CI_CTL_SET_PGMEM_CTL_PGATE_DIS(r, v)	bitset32(r, 21, 21, v)
#define	PCIE_CORE_CI_CTL_SET_SLV_SDP_MODE(r, v)		bitset32(r, 19, 18, v)
#define	PCIE_CORE_CI_CTL_SLV_SDP_MODE_LEGACY				0
#define	PCIE_CORE_CI_CTL_SLV_SDP_MODE_UPSTREAM				1
#define	PCIE_CORE_CI_CTL_SLV_SDP_MODE_DNSTREAM				2
#define	PCIE_CORE_CI_CTL_SLV_SDP_MODE_BIDIR				3
#define	PCIE_CORE_CI_CTL_SET_SLV_SDP_CONNECT_EN(r, v)	bitset32(r, 17, 17, v)
#define	PCIE_CORE_CI_CTL_SET_SDP_POISON_ERR_DIS(r, v)	bitset32(r, 16, 16, v)
#define	PCIE_CORE_CI_CTL_SET_CPL_ALLOC_SOR_EN(r, v)	bitset32(r, 12, 12, v)
#define	PCIE_CORE_CI_CTL_SET_CPL_ALLOC_MODE(r, v)	bitset32(r, 11, 11, v)
#define	PCIE_CORE_CI_CTL_CPL_ALLOC_MODE_DYNAMIC				0
#define	PCIE_CORE_CI_CTL_CPL_ALLOC_MODE_STATIC_PORTCTL			1
#define	PCIE_CORE_CI_CTL_SET_CPL_ALLOC_DIVBYLANE_DIS(r, v) \
    bitset32(r, 10, 10, v)
#define	PCIE_CORE_CI_CTL_SET_SLV_MEM_WR_FULL_DIS(r, v)	bitset32(r, 9, 9, v)
#define	PCIE_CORE_CI_CTL_SET_SLV_ORDERING_DIS(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_CORE_CI_CTL_GET_RC_RD_REQ_SZ(r)		bitx32(r, 7, 6)
#define	PCIE_CORE_CI_CTL_SET_SLV_CPL_OVERSUB_MODE(r, v)	bitset32(r, 7, 6, v)
#define	PCIE_CORE_CI_CTL_GET_SLV_CPL_OVERSUB_MODE(r)	bitx32(r, 7, 6)
#define	PCIE_CORE_CI_CTL_SLV_CPL_OVERSUB_NONE				0
#define	PCIE_CORE_CI_CTL_SLV_CPL_OVERSUB_12_5P				1
#define	PCIE_CORE_CI_CTL_SLV_CPL_OVERSUB_25_0P				2
#define	PCIE_CORE_CI_CTL_SLV_CPL_OVERSUB_37_5P				4
#define	PCIE_CORE_CI_CTL_SLV_CPL_OVERSUB_DIS				7

/*
 * PCIECORE::PCIE_BUS_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUS_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x84	\
}

/*
 * PCIECORE::PCIE_LC_STATE6 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x88	\
}

/*
 * PCIECORE::PCIE_LC_STATE7 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x8c	\
}

/*
 * PCIECORE::PCIE_LC_STATE8 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE8	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x90	\
}

/*
 * PCIECORE::PCIE_LC_STATE9 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE9	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x94	\
}

/*
 * PCIECORE::PCIE_LC_STATE10 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE10	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x98	\
}

/*
 * PCIECORE::PCIE_LC_STATE11 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE11	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x9c	\
}

/*
 * PCIECORE::PCIE_LC_STATUS1 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xa0	\
}

/*
 * PCIECORE::PCIE_LC_STATUS2 - unused but captured for debugging. Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STS2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xa4	\
}

/*
 * PCIECORE::PCIE_LC_ARBMUX_CNTL7 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_ARBMUX_CTL7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xa8	\
}

/*
 * PCIECORE::PCIE_LC_ARBMUX_CNTL8 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_ARBMUX_CTL8	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xac	\
}

/*
 * PCIECORE::PCIE_CREDIT_RELEASE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CREDIT_RELEASE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xb0	\
}

/*
 * PCIECORE::PCIE_WPR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_WPR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xc0	\
}

/*
 * PCIECORE::PCIE_RX_LAST_TLP0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_LAST_TLP0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xc4	\
}

/*
 * PCIECORE::PCIE_RX_LAST_TLP1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_LAST_TLP1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xc8	\
}

/*
 * PCIECORE::PCIE_RX_LAST_TLP2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_LAST_TLP2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xcc	\
}

/*
 * PCIECORE::PCIE_RX_LAST_TLP3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_LAST_TLP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xd0	\
}

/*
 * PCIECORE::PCIE_SDP_SLV_WRRSP_EXPECTED_CTRL_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_SLV_WRRSP_EXP_CTL_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xd4	\
}

/*
 * PCIECORE::PCIE_I2C_REG_ADDR_EXPAND - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_I2C_ADDR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xe8	\
}

/*
 * PCIECORE::PCIE_I2C_REG_DATA - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_I2C_DATA	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xec	\
}

/*
 * PCIECORE::PCIE_CFG_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CFG_CTL_CFG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xf0	\
}

/*
 * PCIECORE::PCIE_LC_PM_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_PM_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xf4	\
}

/*
 * PCIECORE::PCIE_LC_PM_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_PM_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xf8	\
}

/*
 * PCIECORE::PCIE_LC_STRAP_BUFF_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STRAP_BUFF_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xfc	\
}

/*
 * PCIECORE::PCIE_P_CNTL - Various controls around the phy.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_P_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x100	\
}
#define	PCIE_CORE_PCIE_P_CTL_SET_ELEC_IDLE(r, v)	bitset32(r, 15, 14, v)
/*
 * 2.5G Entry uses phy detector.
 * 5.0+ Entry uses inference logic
 * Exit always uses phy detector
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M0				0
/*
 * Electrical idle always uses inference logic, exit always uses phy.
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M1				1
/*
 * Electrical idle entry/exit always uses phy detector
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M2				2
/*
 * 8.0+ entry uses inference, everything else uses phy detector
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M3				3
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_TOK_ERR(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_IDL_ERR(r, v)	bitset32(r, 7, 7, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_EDB_ERR(r, v)	bitset32(r, 6, 6, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_LEN_ERR(r, v)	bitset32(r, 5, 5, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_CRC_ERR(r, v)	bitset32(r, 4, 4, v)

/*
 * PCIECORE::PCIE_P_BUF_STATUS - unused but captured for debugging. RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_BUF_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x104	\
}

/*
 * PCIECORE::PCIE_P_DECODER_STATUS - unused but captured for debugging. RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_DECODER_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x108	\
}

/*
 * PCIECORE::PCIE_P_MISC_STATUS - unused but captured for debugging. RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_MISC_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x10c	\
}

/*
 * PCIECORE::PCIE_P_RCV_L0S_FTS_DET - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_RX_L0S_FTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x140	\
}

/*
 * PCIECORE::PCIE_RX_AD - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_AD	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x188	\
}

/*
 * PCIECORE::PCIE_SDP_CTRL - PCIe port SDP Control. This register seems to be
 * used to tell the system how to map a given port to the data fabric and
 * related.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x18c	\
}
#define	PCIE_CORE_SDP_CTL_SET_UNIT_ID_LO(r, v)		bitset32(r, 28, 26, v)
#define	PCIE_CORE_SDP_CTL_SET_UNIT_ID_HI(r, v)		bitset32(r, 3, 0, v)

/*
 * PCIECORE::PCIE_NBIO_CLKREQb_MAP_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_NBIO_CLKREQ_B_MAP_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x190	\
}

/*
 * PCIECORE::PCIE_SDP_RC_SLV_ATTR_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_RC_SLV_ATTR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x198	\
}

/*
 * PCIECORE::NBIO_CLKREQb_MAP_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_NBIO_CLKREQ_B_MAP_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x19c	\
}

/*
 * PCIECORE::PCIE_SDP_CTRL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1a0	\
}

/*
 * PCIECORE::PCIE_SDP_CTRL_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1a4	\
}

/*
 * PCIECORE::PCIE_SDP_CTRL4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1a8	\
}

/*
 * PCIECORE::PCIE_SDP_CTRL5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SDP_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1ac	\
}

/*
 * PCIECORE::PCIE_RCB_CNTL - Receiver Completion Buffer Control Register.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RCB_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1b0	\
}
#define	PCIE_CORE_RCB_CTL_SET_SWUS_CA_CTO_EN(r, v)	bitset32(r, 30, 30, v)
#define	PCIE_CORE_RCB_CTL_SET_RC_CA_CTO_EN(r, v)	bitset32(r, 29, 29, v)
#define	PCIE_CORE_RCB_CTL_SET_SWUS_UR_CTO_EN(r, v)	bitset32(r, 28, 28, v)
#define	PCIE_CORE_RCB_CTL_SET_RX_DPC_CPL_MODE(r, v)	bitset32(r, 27, 27, v)
#define	PCIE_CORE_RCB_CTL_RX_DPC_CPL_MODE_CTO				0
#define	PCIE_CORE_RCB_CTL_RX_DPC_CPL_MODE_URCA				1
#define	PCIE_CORE_RCB_CTL_SET_RX_DPC_RPIO_TO_CA_EN(r, v) \
    bitset32(r, 26, 26, v)
#define	PCIE_CORE_RCB_CTL_SET_IGN_SFI_CAM_DIS(r, v)	bitset32(r, 25, 25, v)
#define	PCIE_CORE_RCB_CTL_SET_IGN_LINK_DOWN_ERR(r, v)	bitset32(r, 24, 24, v)
#define	PCIE_CORE_RCB_CTL_SET_LINK_DOWN_CTO_EN(r, v)	bitset32(r, 23, 23, v)
#define	PCIE_CORE_RCB_CTL_SET_RX_ALL_CTO_TO_UR_EN(r, v)	bitset32(r, 22, 22, v)
#define	PCIE_CORE_RCB_CTL_SET_BAD_PREFIX_DIS(r, v)	bitset32(r, 4, 4, v)
#define	PCIE_CORE_RCB_CTL_SET_UNEXP_CPL_DIS(r, v)	bitset32(r, 3, 3, v)
#define	PCIE_CORE_RCB_CTL_SET_BAD_FUNC_DIS(r, v)	bitset32(r, 2, 2, v)
#define	PCIE_CORE_RCB_CTL_SET_BAD_ATTR_DIS(r, v)	bitset32(r, 1, 1, v)
#define	PCIE_CORE_RCB_CTL_SET_BAD_SIZE_DIS(r, v)	bitset32(r, 0, 0, v)

/*
 * PCIECORE::PCIE_SFI_CAM_BY_UNITID_RX - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SFI_CAM_BY_UNITID_RX	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1b4	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_RCB - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_RCB	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1b8	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_RCBRX_PORT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_RCBRX_PORT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x1bc	\
}

/*
 * PCIECORE::PCIE_ERR_INJECT_MODE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_ERR_INJ_MODE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x298	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_HDR0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_HDR0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2a0	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_HDR1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_HDR1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2a4	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_HDR2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_HDR2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2a8	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_HDR3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_HDR3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2ac	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_PREFIX0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2b0	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_PREFIX1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2b4	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_PREFIX2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2b8	\
}

/*
 * PCIECORE::PCIE_AER_ERROR_INJECT_PREFIX3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_AER_ERR_INJ_PREFIX3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2bc	\
}

/*
 * PCIECORE::PCIE_STRAP_F0 - PCIe Strap registers for function 0. As this
 * register is in the core, it's a little unclear if function 0 here refers to
 * the dummy device that is usually found on function 0, for the actual root
 * complex itself, or something else.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_STRAP_F0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2c0	\
}
#define	PCIE_CORE_STRAP_F0_SET_ATOMIC_ROUTE(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_CORE_STRAP_F0_SET_ATOMIC_EN(r, v)		bitset32(r, 18, 18, v)

/*
 * PCIECORE::PCIE_STRAP_NTB - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_STRAP_NTB	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x2c4	\
}

/*
 * PCIECORE::PCIE_STRAP_MISC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_STRAP_MISC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x300	\
}

/*
 * PCIECORE::PCIE_STRAP_MISC2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_STRAP_MISC2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x304	\
}

/*
 * PCIECORE::PCIE_STRAP_PI - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_STRAP_PI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x308	\
}

/*
 * PCIECORE::PCIE_PRBS_CLR - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_CLR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x320	\
}

/*
 * PCIECORE::PCIE_PRBS_STATUS1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x324	\
}

/*
 * PCIECORE::PCIE_PRBS_STATUS2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_STS2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x328	\
}

/*
 * PCIECORE::PCIE_PRBS_FREERUN - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_FREERUN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x32c	\
}

/*
 * PCIECORE::PCIE_PRBS_MISC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_MISC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x330	\
}

/*
 * PCIECORE::PCIE_PRBS_USER_PATTERN - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_USER_PATTERN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x334	\
}

/*
 * PCIECORE::PCIE_PRBS_LO_BITCNT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_LO_BITCNT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x338	\
}

/*
 * PCIECORE::PCIE_PRBS_HI_BITCNT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_HI_BITCNT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x33c	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x340	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x344	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x348	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x34c	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x350	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x354	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_6 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x358	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_7 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x35c	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_8 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT8	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x360	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_9 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT9	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x364	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_10 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT10	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x368	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_11 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT11	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x36c	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_12 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT12	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x370	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_13 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT13	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x374	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_14 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT14	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x378	\
}

/*
 * PCIECORE::PCIE_PRBS_ERRCNT_15 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_ERRCNT15	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x37c	\
}

/*
 * PCIECORE::SWRST_COMMAND_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CMD_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x400	\
}

/*
 * PCIECORE::SWRST_GENERAL_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_GEN_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x404	\
}

/*
 * PCIECORE::SWRST_COMMAND_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CMD0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x408	\
}

/*
 * PCIECORE::SWRST_COMMAND_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CMD1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x40c	\
}

/*
 * PCIECORE::SWRST_CONTROL_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x410	\
}

/*
 * PCIECORE::SWRST_CONTROL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x414	\
}

/*
 * PCIECORE::SWRST_CONTROL_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x418	\
}

/*
 * PCIECORE::SWRST_CONTROL_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x41c	\
}

/*
 * PCIECORE::SWRST_CONTROL_4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x420	\
}

/*
 * PCIECORE::SWRST_CONTROL_5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x424	\
}

/*
 * PCIECORE::SWRST_CONTROL_6 - PCIe Software Reset Control #6. This is in 'Core
 * Space' and controls whether or not all of a given set of ports are stopped
 * from training.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SWRST_CTL6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x428	\
}
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_K(r, v)		bitset32(r, 10, 10, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_J(r, v)		bitset32(r, 9, 9, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_I(r, v)		bitset32(r, 8, 8, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_H(r, v)		bitset32(r, 7, 7, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_G(r, v)		bitset32(r, 6, 6, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_F(r, v)		bitset32(r, 5, 5, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_E(r, v)		bitset32(r, 4, 4, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_D(r, v)		bitset32(r, 3, 3, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_C(r, v)		bitset32(r, 2, 2, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_B(r, v)		bitset32(r, 1, 1, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_A(r, v)		bitset32(r, 0, 0, v)

/*
 * PCIECORE::CPM_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CPM_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x460	\
}

/*
 * PCIECORE::CPM_SPLIT_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CPM_SPLIT_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x464	\
}

/*
 * PCIECORE::CPM_CONTROL_EXT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CPM_CTL_EXT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x468	\
}

/*
 * PCIECORE::CPM_CONTROL_EXT2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CPM_CTL_EXT2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x470	\
}

/*
 * PCIECORE::SMN_APERTURE_ID_A - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMN_APERTURE_A	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x474	\
}

/*
 * PCIECORE::SMN_APERTURE_ID_B - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMN_APERTURE_B	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x478	\
}

/*
 * PCIECORE::RSMU_MASTER_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RSMU_MASTER_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x47c	\
}

/*
 * PCIECORE::RSMU_SLAVE_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RSMU_SLAVE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x480	\
}

/*
 * PCIECORE::RSMU_POWER_GATING_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RSMU_PWR_GATE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x484	\
}

/*
 * PCIECORE::RSMU_BIOS_TIMER_CMD - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RSMU_TIMER_CMD	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x488	\
}

/*
 * PCIECORE::RSMU_BIOS_TIMER_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RSMU_TIMER_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x48c	\
}

/*
 * PCIECORE::RSMU_BIOS_TIMER_DEBUG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RSMU_TIMER_DBG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x490	\
}

/*
 * PCIECORE::LNCNT_CONTROL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LNCNT_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x494	\
}

/*
 * PCIECORE::CAC_MST_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CAC_MST_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x498	\
}

/*
 * PCIECORE::CAC_SLV_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CAC_SLV_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x49c	\
}

/*
 * PCIECORE::SMU_HP_STATUS_UPDATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_HP_STS_UPDATE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4b0	\
}

/*
 * PCIECORE::HP_SMU_COMMAND_UPDATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HP_SMU_CMD_UPDATE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4b4	\
}

/*
 * PCIECORE::SMU_HP_END_OF_INTERRUPT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_HP_EOI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4b8	\
}

/*
 * PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_INT_PIN_SHARING	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4bc	\
}

/*
 * PCIECORE::PCIE_PGMST_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PGMST_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4c0	\
}

/*
 * PCIECORE::PCIE_PGSLV_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PGSLV_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4c4	\
}

/*
 * PCIECORE::LC_CPM_CONTROL_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_CPM_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4cc	\
}

/*
 * PCIECORE::LC_CPM_CONTROL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_CPM_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4d0	\
}

/*
 * PCIECORE::PCIE_RXMARGIN_CONTROL_CAPABILITIES - PCIe RX Margining controls.
 * This is in 'Core Space' and controls what is advertised when the Lane
 * Margining at the Receiver capability is used to ask for capabilities. That
 * is, these aren't showing up in configuration space but rather are responses
 * to the margining commands.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_MARGIN_CTL_CAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4d4	\
}
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_ERRORS(r, v)	bitset32(r, 4, 4, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_ERRORS_EN				0
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_ERRORS_DIS				1
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_METHOD(r, v)	bitset32(r, 3, 3, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_METHOD_COUNT			0
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_METHOD_RATE				1
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_TIME(r, v)	bitset32(r, 2, 2, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_VOLT(r, v)	bitset32(r, 1, 1, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_VOLT_SUP(r, v)	bitset32(r, 0, 0, v)

/*
 * PCIECORE::PCIE_RXMARGIN_1_SETTINGS - This register controls the limits of
 * margining. The OFF fields control the maximum distance margining can travel.
 * The STEPS fields control how many steps margining can take.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_MARGIN1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4d8	\
}
#define	PCIE_CORE_RX_MARGIN1_SET_MAX_VOLT_OFF(r, v)	bitset32(r, 26, 20, v)
#define	PCIE_CORE_RX_MARGIN1_SET_MAX_TIME_OFF(r, v)	bitset32(r, 19, 13, v)
#define	PCIE_CORE_RX_MARGIN1_SET_NUM_TIME_STEPS(r, v)	bitset32(r, 12, 7, v)
#define	PCIE_CORE_RX_MARGIN1_SET_NUM_VOLT_STEPS(r, v)	bitset32(r, 6, 0, v)

/*
 * PCIECORE::PCIE_RXMARGIN_2_SETTINGS - This contains both controls and values
 * that are used during the margining process itself. The latter two fields
 * control the sampling ratio which continues until either the counter is
 * saturated or we hit the set error limit. This register is generally set
 * during PCIe initialization and is instead utilized by the internal IP in
 * response to PCIe margining commands.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_MARGIN2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4dc	\
}
#define	PCIE_CORE_RX_MARGIN2_SET_PRECODING_EN(r, v)	bitset32(r, 30, 30, v)
#define	PCIE_CORE_RX_MARGIN2_SET_ERR_LIM(r, v)		bitset32(r, 29, 24, v)
#define	PCIE_CORE_RX_MARGIN2_SET_NLANES(r, v)		bitset32(r, 23, 19, v)
#define	PCIE_CORE_RX_MARGIN2_GET_COUNT(r)		bitx32(r, 18, 12)
#define	PCIE_CORE_RX_MARGIN2_SET_TIME_RATIO(r, v)	bitset32(r, 11, 6, v)
#define	PCIE_CORE_RX_MARGIN2_SET_VOLT_RATIO(r, v)	bitset32(r, 5, 0, v)

/*
 * PCIECORE::PCIE_PRESENCE_DETECT_SELECT - PCIe Presence Detect Control. This is
 * 'Core Space', so it is shared among all the core's ports. This is used to
 * determine whether we should consider something present based on the link up
 * OR the side-band signals, or instead require both (e.g. AND).
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRES	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4e0	\
}
#define	PCIE_CORE_PRES_SET_TL_MODE(r, v)		bitset32(r, 27, 26, v)
#define	PCIE_CORE_PRES_TL_MODE_IN_BAND					0
#define	PCIE_CORE_PRES_TL_MODE_AND					1
#define	PCIE_CORE_PRES_TL_MODE_OR					2
#define	PCIE_CORE_PRES_TL_MODE_OUT_OF_BAND				3
#define	PCIE_CORE_PRES_SET_MODE(r, v)			bitset32(r, 25, 24, v)
#define	PCIE_CORE_PRES_MODE_OR						0
#define	PCIE_CORE_PRES_MODE_AND						1
#define	PCIE_CORE_PRES_MODE_IN_BAND					2
#define	PCIE_CORE_PRES_MODE_OUT_OF_BAND					3

/*
 * PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR_TWO - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_INT_PIN_SHARING2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4e8	\
}

/*
 * PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR_FOUR - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_INT_PIN_SHARING4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4ec	\
}

/*
 * PCIECORE::PCIE_DYN_RECONFIG_PSEUDO_RESET - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_DYN_RECFG_PSEUDO_RST	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4f4	\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE0_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE0_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x500,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE1_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE1_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x502,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE2_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE2_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x504,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE3_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE3_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x506,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE4_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE4_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x508,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE5_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE5_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x50a,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE6_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE6_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x50c,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE7_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE7_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x50e,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE8_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE8_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x510,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE9_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE9_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x512,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE10_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE10_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x514,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE11_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE11_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x516,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE12_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE12_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x518,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE13_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE13_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x51a,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE14_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE14_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x51c,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE15_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE15_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x51e,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE0_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE0_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x520,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE1_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE1_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x522,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE2_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE2_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x524,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE3_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE3_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x526,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE4_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE4_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x528,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE5_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE5_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x52a,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE6_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE6_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x52c,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE7_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE7_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x52e,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE8_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE8_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x530,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE9_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE9_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x532,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE10_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE10_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x534,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE11_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE11_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x536,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE12_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE12_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x538,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE13_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE13_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x53a,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE14_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE14_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x53c,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_LANE15_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_LANE15_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x53e,	\
	.srd_size = 2		\
}

/*
 * PCIECORE::PCIE_PHYSICAL_PORT_WIDTH_0_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_PORT_WIDTH_0_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x540	\
}

/*
 * PCIECORE::PCIE_PHYSICAL_PORT_WIDTH_1_MAPPING_STATUS - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PHYS_PORT_WIDTH_1_MAPSTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x544	\
}

/*
 * PCIECORE::PCIE_LC_DESKEW_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_DESKEW_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x548	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x600	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x604	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x608	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x60c	\
}

/*
 * PCIECORE::PCIE_TX_TRACKING_ADDR_LO - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_TRK_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x610	\
}

/*
 * PCIECORE::PCIE_TX_TRACKING_ADDR_HI - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_TRK_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x614	\
}

/*
 * PCIECORE::PCIE_TX_TRACKING_CTRL_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_TRK_CTL_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x618	\
}

/*
 * PCIECORE::PCIE_TX_POWER_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_PWR_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x61c	\
}

/*
 * PCIECORE::PCIE_TX_CTRL_1 - PCIe port level transmit controls.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_TX_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x620	\
}
#define	PCIE_CORE_PCIE_TX_CTL1_SET_TX_ATOMIC_ORD_DIS(r, v) \
    bitset32(r, 25, 25, v)
#define	PCIE_CORE_PCIE_TX_CTL1_SET_TX_ATOMIC_OPS_DIS(r, v) \
    bitset32(r, 24, 24, v)

/*
 * PCIECORE::PCIE_TX_CTRL_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_TX_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x624	\
}

/*
 * PCIECORE::PCIE_TX_CTRL_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_TX_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x628	\
}

/*
 * PCIECORE::PCIE_TX_CTRL_4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_TX_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x62c	\
}

/*
 * PCIECORE::PCIE_TX_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x650	\
}

/*
 * PCIECORE::PCIE_TX_F0_ATTR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_F0_ATTR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x670	\
}

/*
 * PCIECORE::PCIE_TX_SWUS_ATTR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_SWUS_ATTR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x674	\
}

/*
 * PCIECORE::PCIE_TX_ERR_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_ERR_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x690	\
}

/*
 * PCIECORE::PCIE_BUF_PORT0_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT0_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b0,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT1_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT1_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b1,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT2_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT2_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b2,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT3_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT3_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b3,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT4_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT4_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b4,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT5_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT5_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b5,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT6_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT6_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b6,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT7_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT7_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b7,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::PCIE_BUF_PORT8_MAPPING - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUF_PORT8_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6b8,	\
	.srd_size = 1		\
}

/*
 * PCIECORE::SMU_PCIE_DF_ADDRESS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_PCIE_DF_ADDRESS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6c0	\
}

/*
 * PCIECORE::SMU_PCIE_DF_ADDRESS_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_PCIE_DF_ADDRESS2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6c4	\
}

/*
 * PCIECORE::PCIE_ERR_HARVEST_RSP_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_ERR_HARVEST_RSP_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6d8	\
}

/*
 * PCIECORE::SMU_PCIE_USB_MCM_ADDRESS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_PCIE_USB_MCM_ADDRESS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x6e8	\
}

/*
 * PCIECORE::PCIE_BW_BY_UNITID - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BW_BY_UNITID	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x700	\
}

/*
 * PCIECORE::PCIE_SFI_CAM_BY_UNITID - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SFI_CAM_BY_UNITID	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x704	\
}

/*
 * PCIECORE::PCIE_MST_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x710	\
}

/*
 * PCIECORE::PCIE_MST_CTRL_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x714	\
}

/*
 * PCIECORE::PCIE_MST_CTRL_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x718	\
}

/*
 * PCIECORE::PCIE_MST_CTRL_4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_CTL4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x71c	\
}

/*
 * PCIECORE::PCIE_MST_ERR_CTRL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_ERR_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x760	\
}

/*
 * PCIECORE::PCIE_MST_ERR_STATUS_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_ERR_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x770	\
}

/*
 * PCIECORE::PCIE_MST_DEBUG_CNTL_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_DBG_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x77c	\
}

/*
 * PCIECORE::PCIE_HIP_REG0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x780	\
}

/*
 * PCIECORE::PCIE_HIP_REG1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x784	\
}

/*
 * PCIECORE::PCIE_HIP_REG2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x788	\
}

/*
 * PCIECORE::PCIE_HIP_REG3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x78c	\
}

/*
 * PCIECORE::PCIE_HIP_REG4 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x790	\
}

/*
 * PCIECORE::PCIE_HIP_REG5 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x794	\
}

/*
 * PCIECORE::PCIE_HIP_REG6 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x798	\
}

/*
 * PCIECORE::PCIE_HIP_REG7 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x79c	\
}

/*
 * PCIECORE::PCIE_HIP_REG8 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HIP_REG8	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x7a0	\
}

/*
 * PCIECORE::PCIE_MST_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_MST_STS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x7f0	\
}

/*
 * PCIECORE::PCIE_PERF_CNTL1_EVENT_CI_PORT_SEL - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PERF_CTL1_EV_CI_PORT_SEL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x880	\
}

/*
 * PCIECORE::PCIE_PERF_CNTL1_EVENT_TX_PORT_SEL - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PERF_CTL1_EV_TX_PORT_SEL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x884	\
}

/*
 * PCIECORE::PCIE_LANE_ERROR_COUNTERS_0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LANE_ERR_CNTRS0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x978	\
}

/*
 * PCIECORE::PCIE_LANE_ERROR_COUNTERS_1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LANE_ERR_CNTRS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x97c	\
}

/*
 * PCIECORE::PCIE_LANE_ERROR_COUNTERS_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LANE_ERR_CNTRS2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x980	\
}

/*
 * PCIECORE::PCIE_LANE_ERROR_COUNTERS_3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LANE_ERR_CNTRS3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x984	\
}

/*
 * PCIECORE::RXP_ERROR_MASK_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RXP_ERR_MASK_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x98c	\
}

/*
 * PCIECORE::SMU_INT_PIN_SHARING_PORT_INDICATOR_THREE - unused but captured for
 * debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_INT_PIN_SHARING3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x998	\
}

/*
 * PCIECORE::LC_CPM_CONTROL_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_CPM_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xb04	\
}

/*
 * PCIECORE::SMU_PCIE_FENCED1_REG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_FENCED1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x3ff8	\
}

/*
 * PCIECORE::SMU_PCIE_FENCED2_REG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_FENCED2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x3ffc	\
}

/*
 * The following definitions are all in normal PCI configuration space. These
 * represent the fixed offsets into capabilities that normally would be
 * something that one has to walk and find in the device. We opt to use the
 * fixed offsets here because we only care about one specific device, the
 * bridges here. Note, the actual bit definitions are not included here as they
 * are already present in sys/pcie.h.
 */

/*
 * PCIERCCFG::PCIE_CAP. This is the core PCIe capability register offset. This
 * is related to the PCIE_PCIECAP, but already adjusted for the fixed capability
 * offset.
 */
#define	TURIN_BRIDGE_R_PCI_PCIE_CAP	0x5a

/*
 * PCIERCCFG::SLOT_CAP, PCIERCCFG::SLOT_CNTL, PCIERCCFG::SLOT_STATUS. This is
 * the PCIe capability's slot capability, control, and status registers
 * respectively.  This is the illumos PCIE_SLOTCAP, PCIE_SLOTCTL, and
 * PCIE_SLOTSTS, but already adjusted for the capability offset.
 */

#define	TURIN_BRIDGE_R_PCI_SLOT_CAP	0x6c
#define	TURIN_BRIDGE_R_PCI_SLOT_CTL	0x70
#define	TURIN_BRIDGE_R_PCI_SLOT_STS	0x72

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_PCIE_H */
