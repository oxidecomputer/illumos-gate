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

#ifndef _SYS_IO_GENOA_PCIE_H
#define	_SYS_IO_GENOA_PCIE_H

/*
 * Genoa-specific register and bookkeeping definitions for PCIe root complexes,
 * ports, and bridges.
 */

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
struct genoa_pcie_core;
struct genoa_pcie_port;

typedef struct genoa_pcie_core genoa_pcie_core_t;
typedef struct genoa_pcie_port genoa_pcie_port_t;

typedef int (*genoa_pcie_core_cb_f)(genoa_pcie_core_t *, void *);
typedef int (*genoa_pcie_port_cb_f)(genoa_pcie_port_t *, void *);

extern uint8_t genoa_ioms_n_pcie_cores(const uint8_t);
extern uint8_t genoa_pcie_core_n_ports(const uint8_t);

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
 */

static inline smn_reg_t
genoa_pcie_core_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t coreno)
{
	const uint32_t PCIE_CORE_SMN_REG_MASK = 0x7ffff;
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t core32 = (const uint32_t)coreno;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_CORE);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(ioms32, <, 4);
	ASSERT0(def.srd_reg & ~PCIE_CORE_SMN_REG_MASK);

#ifdef	DEBUG
	const uint32_t nents = genoa_ioms_n_pcie_cores(iomsno);
	ASSERT3U(nents, >, core32);
#endif	/* DEBUG */

	const uint32_t aperture_base = 0x1A380000;

	const uint32_t aperture_off = (ioms32 << 20) + (core32 << 22);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_CORE_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

static inline smn_reg_t
genoa_pcie_port_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t coreno, const uint8_t portno)
{
	const uint32_t PCIE_PORT_SMN_REG_MASK = 0xfff;
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t core32 = (const uint32_t)coreno;
	const uint32_t port32 = (const uint32_t)portno;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_PORT);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(ioms32, <, 4);
	ASSERT0(def.srd_reg & ~PCIE_PORT_SMN_REG_MASK);

#ifdef	DEBUG
	const uint32_t ncores = (const uint32_t)genoa_ioms_n_pcie_cores(iomsno);
	ASSERT3U(ncores, >, core32);
	const uint32_t nents = (const uint32_t)genoa_pcie_core_n_ports(coreno);
	ASSERT3U(nents, >, port32);
#endif	/* DEBUG */

	const uint32_t aperture_base = 0x1A340000;

	const uint32_t aperture_off = (ioms32 << 20) + (core32 << 22) +
	    (port32 << 12);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_PORT_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

/*
 * PCIEPORT::PCIEP_HW_DEBUG - A bunch of mysterious bits that are used to
 * correct or override various hardware behaviors presumably.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HW_DBG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x08	\
}
#define	PCIE_PORT_HW_DBG(n, p, b)	\
    genoa_pcie_port_smn_reg(n, D_PCIE_PORT_HW_DBG, p, b)
#define	PCIE_PORT_HW_DBG_SET_DBG15(r, v)	bitset32(r, 15, 15, v)

/*
 * PCIEPORT::PCIEP_HW_DEBUG_LC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_HW_DBG_LC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x0c	\
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
#define	PCIE_PORT_PCTL(n, p, b)	\
    genoa_pcie_port_smn_reg(n, D_PCIE_PORT_PCTL, p, b)
#define	PCIE_PORT_PCTL_SET_PWRFLT_EN(r, v)	bitset32(r, 4, 4, v)

/*
 * PCIEPORT::PCIEP_SDP_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_SDP_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x44	\
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
#define	PCIE_PORT_TX_PORT_CTL1(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_TX_PORT_CTL1, (p), (b))
#define	PCIE_PORT_TX_PORT_CTL1_SET_TLP_FLUSH_DOWN_DIS(r, v)	\
    bitset32(r, 15, 15, v)
#define	PCIE_PORT_TX_PORT_CTL1_SET_CPL_PASS(r, v)	bitset32(r, 20, 20, v)

/*
 * PCIEPORT::PCIE_TX_REQUESTER_ID - Encodes information about the bridge's PCI
 * b/d/f.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_ID	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x84	\
}
#define	PCIE_PORT_TX_ID(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_TX_ID, (p), (b))
#define	PCIE_PORT_TX_ID_SET_BUS(r, v)	bitset32(r, 15, 8, v)
#define	PCIE_PORT_TX_ID_SET_DEV(r, v)	bitset32(r, 7, 3, v)
#define	PCIE_PORT_TX_ID_SET_FUNC(r, v)	bitset32(r, 2, 0, v)

/*
 * PCIEPORT::PCIE_TX_VENDOR_SPECIFIC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_VS_DLLP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x88	\
}

/*
 * PCIEPORT::PCIE_TX_REQUEST_NUM_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_REQ_NUM_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x8c	\
}

/*
 * PCIEPORT::PCIE_TX_SEQ - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_SEQ	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x90	\
}

/*
 * PCIEPORT::PCIE_TX_REPLAY - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_REPLAY	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x94	\
}

/*
 * PCIEPORT::PCIE_TX_ACK_LATENCY_LIMIT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_ACK_LAT_LIM	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x98	\
}

/*
 * PCIEPORT::PCIE_TX_NOP_DLLP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_NOP_DLLP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x9c	\
}

/*
 * PCIEPORT::PCIE_TX_CNTL_2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xa0	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_ADVT_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_ADVT_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xc0	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_ADVT_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_ADVT_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xc4	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_ADVT_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_ADVT_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xc8	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_INIT_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_INIT_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xcc	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_INIT_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_INIT_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xd0	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_INIT_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_INIT_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xd4	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_STATUS - unused but captured for debugging.  Some
 * fields are RW1c (read/write-1-to-clear).
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_STATUS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xd8	\
}

/*
 * PCIEPORT::PCIE_TX_CREDITS_FCU_THRESHOLD - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CREDITS_FCU_THRESH	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xdc	\
}

/*
 * PCIEPORT::PCIE_TX_CCIX_PORT_CNTL0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CCIX_PORT_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xe0	\
}

/*
 * PCIEPORT::PCIE_TX_CCIX_PORT_CNTL1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CCIX_PORT_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xe4	\
}

/*
 * PCIEPORT::PCIE_CCIX_STACKED_BASE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_STACKED_BASE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xe8	\
}

/*
 * PCIEPORT::PCIE_CCIX_STACKED_LIMIT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_STACKED_LIM	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xec	\
}

/*
 * PCIEPORT::PCIE_CCIX_DUMMY_RD_UPPER_ADDR - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xf0	\
}

/*
 * PCIEPORT::PCIE_CCIX_DUMMY_RD_LOWER_ADDR - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_DUMMY_RD_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xf4	\
}

/*
 * PCIEPORT::PCIE_CCIX_DUMMY_RD_CTRL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_DUMMY_RD_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xf8	\
}

/*
 * PCIEPORT::PCIE_CCIX_DUMMY_WR_UPPER_ADDR - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0xfc	\
}

/*
 * PCIEPORT::PCIE_CCIX_DUMMY_WR_LOWER_ADDR - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_DUMMY_WR_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x100	\
}

/*
 * PCIEPORT::PCIE_CCIX_MISC_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_CCIX_MISC_STATUS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x104	\
}

/*
 * PCIEPORT::PCIE_P_PORT_LANE_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_P_LANE_STATUS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x140	\
}

/*
 * PCIEPORT::PCIE_FC_P - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_P	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x180	\
}

/*
 * PCIEPORT::PCIE_FC_NP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_NP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x184	\
}

/*
 * PCIEPORT::PCIE_FC_CPL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_CPL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x188	\
}

/*
 * PCIEPORT::PCIE_FC_P_VC1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_P_VC1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x18c	\
}

/*
 * PCIEPORT::PCIE_FC_NP_VC1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_NP_VC1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x190	\
}

/*
 * PCIEPORT::PCIE_FC_CPL_VC1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_FC_CPL_VC1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x194	\
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
#define	D_PCIE_PORT_RX_CAPTURED_LTR_CTL_STATUS	(const smn_reg_def_t){	\
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
 * PCIEPORT::PCIE_LC_CNTL - The first of several link controller control
 * registers.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x280	\
}
#define	PCIE_PORT_LC_CTL(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL, (p), (b))
#define	PCIE_PORT_LC_CTL_SET_L1_IMM_ACK(r, v)	bitset32(r, 23, 23, v)

/*
 * PCIEPORT::PCIE_LC_TRAINING_CNTL - Port Link Training Control. This register
 * seems to control some amount of the general aspects of link training.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_TRAIN_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x284	\
}
#define	PCIE_PORT_LC_TRAIN_CTL(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_TRAIN_CTL, (p), (b))
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
#define	PCIE_PORT_LC_WIDTH_CTL(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_WIDTH_CTL, (p), (b))
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
#define	PCIE_PORT_LC_SPEED_CTL(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_SPEED_CTL, (p), (b))
#define	PCIE_PORT_LC_SPEED_CTL_GET_L1_NEG_EN(r)		bitx32(r, 31, 31)
#define	PCIE_PORT_LC_SPEED_CTL_GET_L0S_NEG_EN(r)	bitx32(r, 30, 30)
#define	PCIE_PORT_LC_SPEED_CTL_GET_UPSTREAM_AUTO(r)	bitx32(r, 29, 29)
#define	PCIE_PORT_LC_SPEED_CTL_GET_CHECK_RATE(r)	bitx32(r, 28, 28)
#define	PCIE_PORT_LC_SPEED_CTL_GET_ADV_RATE(r)		bitx32(r, 27, 26)
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_2P5	0
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_5P0	1
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_8P0	2
#define	PCIE_PORT_LC_SPEED_CTL_ADV_RATE_16P0	3
#define	PCIE_PORT_LC_SPEED_CTL_GET_SPEED_CHANGE(r)	bitx32(r, 25, 25)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN4(r)	bitx32(r, 24, 24)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN4(r)	bitx32(r, 23, 23)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN3(r)	bitx32(r, 22, 22)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN3(r)	bitx32(r, 21, 21)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SUP_GEN2(r)	bitx32(r, 20, 20)
#define	PCIE_PORT_LC_SPEED_CTL_GET_REM_SENT_GEN2(r)	bitx32(r, 19, 19)
#define	PCIE_PORT_LC_SPEED_CTL_GET_PART_TS2_EN(r)	bitx32(r, 18, 18)
#define	PCIE_PORT_LC_SPEED_CTL_GET_NO_CLEAR_FAIL(r)	bitx32(r, 16, 16)
#define	PCIE_PORT_LC_SPEED_CTL_GET_CUR_RATE(r)		bitx32(r, 15, 14)
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_2P5	0
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_5P0	1
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_8P0	2
#define	PCIE_PORT_LC_SPEED_CTL_CUR_RATE_16P0	3
#define	PCIE_PORT_LC_SPEED_CTL_GET_CHANGE_FAILED(r)	bitx32(r, 13, 13)
#define	PCIE_PORT_LC_SPEED_CTL_GET_MAX_ATTEMPTS(r)	bitx32(r, 12, 11)
#define	PCIE_PORT_LC_SPEED_CTL_MAX_ATTEMPTS_BASE	1
#define	PCIE_PORT_LC_SPEED_CTL_GET_OVR_RATE(r)		bitx32(r, 5, 4)
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_2P5	0
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_5P0	1
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_8P0	2
#define	PCIE_PORT_LC_SPEED_CTL_OVR_RATE_16P0	3
#define	PCIE_PORT_LC_SPEED_CTL_GET_OVR_EN(r)		bitx32(r, 3, 3)

/*
 * PCIEPORT::PCIE_LC_STATE0 - Link Controller State 0 register. All the various
 * Link Controller state registers follow the same pattern, just keeping older
 * and older things in them. That is, you can calculate a given state by
 * multiplying the register number by four. Unfortunately, the meanings of the
 * states are more unknown, but we have reason to expect that at least 0x10 is
 * one of several successful training states.  Note that additional history can
 * be captured in the parent core's registers for a single port selected in the
 * core's DBG_CTL (it's unclear what selecting multiple ports would do).
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x294	\
}
#define	PCIE_PORT_LC_STATE0(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE0, (p), (b))
#define	PCIE_PORT_LC_STATE_GET_PREV3(r)		bitx32(r, 29, 24)
#define	PCIE_PORT_LC_STATE_GET_PREV2(r)		bitx32(r, 21, 16)
#define	PCIE_PORT_LC_STATE_GET_PREV1(r)		bitx32(r, 13, 8)
#define	PCIE_PORT_LC_STATE_GET_CUR(r)		bitx32(r, 5, 0)

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x298	\
}
#define	PCIE_PORT_LC_STATE1(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE1, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x29c	\
}
#define	PCIE_PORT_LC_STATE2(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE2, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a0	\
}
#define	PCIE_PORT_LC_STATE3(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE3, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a4	\
}
#define	PCIE_PORT_LC_STATE4(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE4, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a8	\
}
#define	PCIE_PORT_LC_STATE5(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE5, (p), (b))

/*
 * PCIEPORT::PCIE_LINK_MANAGEMENT_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LINK_MGMT_CTL2	(const smn_reg_def_t){	\
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
#define	PCIE_PORT_LC_CTL2(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL2, (p), (b))
#define	PCIE_PORT_LC_CTL2_SET_ELEC_IDLE(r, v)	bitset32(r, 15, 14, v)
/*
 * These all have the same values as the corresponding
 * PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_<num> values.
 */
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M0	0
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M1	1
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M2	2
#define	PCIE_PORT_LC_CTL2_ELEC_IDLE_M3	3
#define	PCIE_PORT_LC_CTL2_SET_TS2_CHANGE_REQ(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_PORT_LC_CTL2_TS2_CHANGE_16		0
#define	PCIE_PORT_LC_CTL2_TS2_CHANGE_128	1

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
#define	PCIE_PORT_LC_CTL3(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL3, (p), (b))
#define	PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(r, v)	bitset32(r, 12, 12, v)
#define	PCIE_PORT_LC_CTL3_RCVR_DET_OVR(r, v)		bitset32(r, 11, 11, v)
#define	PCIE_PORT_LC_CTL3_ENH_HP_EN(r, v)		bitset32(r, 10, 10, v)

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
 * others, but this one seems to be required for hotplug.  Some fields in this
 * register capture data for a lane selected by LC_DBG_CTL in the port's parent
 * core.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2dc	\
}
#define	PCIE_PORT_LC_CTL5(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL5, (p), (b))
#define	PCIE_PORT_LC_CTL5_SET_WAIT_DETECT(r, v)	bitset32(r, 28, 28, v)

/*
 * PCIEPORT::PCIE_LC_FORCE_COEFF - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_FORCE_COEFF	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2e0	\
}

/*
 * PCIEPORT::PCIE_LC_BEST_EQ_SETTINGS - unused but captured for debugging.  Data
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
 * PCIEPORT::PCIE_LC_CNTL6 - Port Link Control Register 6.  SRIS stuff lives
 * here, with other bits.  Some field (not described here because they are not
 * used) capture data for a specific lane set in the parent core's LC_DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2ec	\
}
#define	PCIE_PORT_LC_CTL6(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL6, (p), (b))
#define	PCIE_PORT_LC_CTL6_GET_SRIS_AUTODET_MODE(r)	bitx32(r, 17, 16)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_AUTODET_MODE(r, v)	bitset32(r, 17, 16, v)
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_MODE_SKP_OS_INT_LK	0
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_MODE_DYN_SKP_OS_INT_LK	1
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_MODE_FE_NOM_EMPTY	2
#define	PCIE_PORT_LC_CTL6_GET_SRIS_AUTODET_FACTOR(r)	bitx32(r, 15, 14)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_AUTODET_FACTOR(r, v)	bitset32(r, 15, 14, v)
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_1X	0
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_0_95X	1
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_0_9X	2
#define	PCIE_PORT_LC_CTL6_SRIS_AUTODET_FACTOR_0_85X	3
#define	PCIE_PORT_LC_CTL6_GET_SRIS_AUTODET_EN(r)	bitx32(r, 13, 13)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_AUTODET_EN(r, v)	bitset32(r, 13, 13, v)
#define	PCIE_PORT_LC_CTL6_GET_SRIS_EN(r)		bitx32(r, 8, 8)
#define	PCIE_PORT_LC_CTL6_SET_SRIS_EN(r, v)		bitset32(r, 8, 8, v)
#define	PCIE_PORT_LC_CTL6_GET_SPC_MODE_8GT(r)		bitx32(r, 5, 4)
#define	PCIE_PORT_LC_CTL6_SET_SPC_MODE_8GT(r, v)	bitset32(r, 5, 4, v)
#define	PCIE_PORT_LC_CTL6_SPC_MODE_8GT_2	1
#define	PCIE_PORT_LC_CTL6_SPC_MODE_8GT_4	2

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
#define	D_PCIE_PORT_LINK_MGMT_STATUS	(const smn_reg_def_t){	\
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
 * PCIEPORT::PCIE_LC_PORT_ORDER - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_PORT_ORDER	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x320	\
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
#define	PCIE_PORT_HP_CTL(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_HP_CTL, (p), (b))
#define	PCIE_PORT_HP_CTL_SET_ACTIVE(r, v)	bitset32(r, 31, 31, v)
#define	PCIE_PORT_HP_CTL_SET_SLOT(r, v)		bitset32(r, 5, 0, v)

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
 * PCIEPORT::PCIE_LC_EQ_CNTL_8GT - Used to set equalization
 * search modes etc.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_EQ_CTL_8GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x390	\
}
#define	PCIE_PORT_LC_EQ_CTL_8GT(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_EQ_CTL_8GT, (p), (b))
#define	PCIE_PORT_LC_EQ_CTL_8GT_SET_SEARCH_MODE(r, v) bitset32(r, 3, 2, v)

/*
 * PCIEPORT::PCIE_LC_EQ_CNTL_16GT - Used to set equalization
 * search modes etc.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_EQ_CTL_16GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x394	\
}
#define	PCIE_PORT_LC_EQ_CTL_16GT(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_EQ_CTL_16GT, (p), (b))
#define	PCIE_PORT_LC_EQ_CTL_16GT_SET_SEARCH_MODE(r, v) bitset32(r, 3, 2, v)

/*
 * PCIEPORT::PCIE_LC_EQ_CNTL_32GT - Used to set equalization
 * search modes etc.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_EQ_CTL_32GT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x400	\
}
#define	PCIE_PORT_LC_EQ_CTL_32GT(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_EQ_CTL_32GT, (p), (b))
#define	PCIE_PORT_LC_EQ_CTL_32GT_SET_SEARCH_MODE(r, v) bitset32(r, 3, 2, v)

/*
 * PCIEPORT::PCIE_LC_PRESET_MASK_CNTL - Used to control
 * preset masks.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_PRESET_MASK_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x404	\
}
#define	PCIE_PORT_LC_PRESET_MASK_CTL(n, p, b)	\
    genoa_pcie_port_smn_reg((n), D_PCIE_PORT_LC_PRESET_MASK_CTL, (p), (b))
#define	PCIE_PORT_LC_PRESET_MASK_CTL_SET_PRESET_MASK_8GT(r, v) \
    bitset32(r, 9, 0, v)
#define	PCIE_PORT_LC_PRESET_MASK_CTL_SET_PRESET_MASK_16GT(r, v) \
    bitset32(r, 19, 10, v)
#define	PCIE_PORT_LC_PRESET_MASK_CTL_SET_PRESET_MASK_32GT(r, v) \
    bitset32(r, 29, 20, v)

/*
 * PCIECORE::PCIE_HW_DEBUG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x8	\
}

/*
 * PCIECORE::PCIE_HW_DEBUG_LC - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_HW_DBG_LC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xc	\
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
#define	PCIE_CORE_PCIE_CTL(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_PCIE_CTL, (p))
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_FUNC_DIS(r, v)	bitset32(r, 22, 22, v)
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_ATTR_DIS(r, v)	bitset32(r, 21, 21, v)
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_PREFIX_DIS(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_SIZE_DIS(r, v)	bitset32(r, 17, 17, v)
#define	PCIE_CORE_PCIE_CTL_SET_HW_LOCK(r, v)		bitset32(r, 0, 0, v)

/*
 * PCIECORE::PCIE_CONFIG_CNTL - unused but captured for debugging.  Note that
 * there is *also* a PCIE_CFG_CNTL at 0xf0.  We keep our conventions but add
 * disambiguating characters to avoid confusion.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CFG_CTL_CONFIG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x44	\
}

/*
 * PCIECORE::PCIE_DEBUG_CNTL - Selects the port(s) for which numerous other
 * counters and state capture registers will be collected by hardware.  The
 * PORT_EN field is a mask of ports, A=0, B=1, ... so that it is possible in
 * some cases to advance counters for multiple ports if desired.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_DBG_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x48	\
}
#define	PCIE_CORE_DBG_CTL_GET_DBG_SELECT(r)	bitx32(r, 8, 8)
#define	PCIE_CORE_DBG_CTL_SET_DBG_SELECT(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_CORE_DBG_CTL_GET_PORT_EN(r)	bitx32(r, 7, 0)
#define	PCIE_CORE_DBG_CTL_SET_PORT_EN(r, v)	bitset32(r, 7, 0, v)

/*
 * PCIECORE::PCIE_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x70	\
}

/*
 * PCIECORE::PCIE_TX_CTRL_1 - PCIe port level transmit controls.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_TX_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x620	\
}
#define	PCIE_CORE_PCIE_TX_CTL1(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_PCIE_TX_CTL1, (p))
#define	PCIE_CORE_PCIE_TX_CTL1_TX_ATOMIC_ORD_DIS(r, v)	bitset32(r, 25, 25, v)
#define	PCIE_CORE_PCIE_TX_CTL1_TX_ATOMIC_OPS_DIS(r, v)	bitset32(r, 24, 24, v)

/*
 * PCIECORE::PCIE_RX_CNTL2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x74	\
}

/*
 * PCIECORE::PCIE_TX_F0_ATTR_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_F0_ATTR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x78	\
}

/*
 * PCIECORE::PCIE_CI_CNTL - PCIe Port level TX controls. Note, this register is
 * in 'core' space and is specific to the overall genoa_pcie_core_t, as opposed
 * to the port or bridge.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CI_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x80	\
}
#define	PCIE_CORE_CI_CTL(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_CI_CTL, (p))
#define	PCIE_CORE_CI_CTL_SET_IGN_LINK_DOWN_CTO_ERR(r, v)	\
    bitset32(r, 31, 31, v)
#define	PCIE_CORE_CI_CTL_SET_ARB_WIDTH_WEIGHTED_RR(r, v)	\
    bitset32(r, 30, 30, v)
#define	PCIE_CORE_CI_CTL_SET_LINK_DOWN_CTO_EN(r, v)	bitset32(r, 29, 29, v)
#define	PCIE_CORE_CI_CTL_SET_MST_TAG_BORROW_DIS(r, v)	bitset32(r, 28, 28, v)
#define	PCIE_CORE_CI_CTL_SET_TXWR_SPLIT_QW_EN(r, v)	bitset32(r, 27, 27, v)
#define	PCIE_CORE_CI_CTL_SET_MSTSPLIT_REQ_CHAIN_DIS(r, v)	\
    bitset32(r, 26, 26, v)
#define	PCIE_CORE_CI_CTL_SET_MSTSPLIT_DIS(r, v)		bitset32(r, 25, 25, v)
#define	PCIE_CORE_CI_CTL_SET_RX_DPC_CPL_MODE(r, v)	bitset32(r, 24, 24, v)
#define	PCIE_CORE_CI_CTL_RX_DPC_CPL_MODE_CTO	0
#define	PCIE_CORE_CI_CTL_RX_DPC_CPL_MODE_URCA	1
#define	PCIE_CORE_CI_CTL_SET_RX_DPC_RPIO_TO_CA_EN(r, v)	bitset32(r, 23, 23, v)
#define	PCIE_CORE_CI_CTL_SET_RX_ALL_CTO_TO_UR_EN(r, v)	bitset32(r, 22, 22, v)
#define	PCIE_CORE_CI_CTL_SET_TX_DIS_SLOW_PWR_LIM(r, v)	bitset32(r, 21, 21, v)
#define	PCIE_CORE_CI_CTL_SET_DIS_SLOTCTL_PWR_LIM(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_CORE_CI_CTL_SET_TX_ATOMIC_EGR_BLOCK_DIS(r, v)	\
    bitset32(r, 19, 19, v)
#define	PCIE_CORE_CI_CTL_SET_TX_POISON_EGR_BLOCK_DIS(r, v)	\
    bitset32(r, 18, 18, v)
#define	PCIE_CORE_CI_CTL_SET_TX_TLP_PFX_BLOCK_DIS(r, v)	bitset32(r, 17, 17, v)
#define	PCIE_CORE_CI_CTL_SET_SDP_POISON_ERR_DIS(r, v)	bitset32(r, 16, 16, v)
#define	PCIE_CORE_CI_CTL_SET_CPL_ALLOC_SOR_EN(r, v)	bitset32(r, 12, 12, v)
#define	PCIE_CORE_CI_CTL_SET_CPL_ALLOC_MODE(r, v)	bitset32(r, 11, 11, v)
#define	PCIE_CORE_CI_CTL_CPL_ALLOC_MODE_DYNAMIC		0
#define	PCIE_CORE_CI_CTL_CPL_ALLOC_MODE_STATIC_PORTCTL	1
#define	PCIE_CORE_CI_CTL_SET_CPL_ALLOC_DIVBYLANE_DIS(r, v)	\
    bitset32(r, 10, 10, v)
#define	PCIE_CORE_CI_CTL_SET_RC_ORDERING_DIS(r, v)	bitset32(r, 9, 9, v)
#define	PCIE_CORE_CI_CTL_SET_SLV_ORDERING_DIS(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_CORE_CI_CTL_GET_RC_RD_REQ_SZ(r)		bitx32(r, 7, 6)
#define	PCIE_CORE_CI_CTL_SET_BAD_CPL_DUMMY(r, v)	bitset32(r, 4, 4, v)
#define	PCIE_CORE_CI_CTL_BAD_CPL_DUMMY_DEADBEEF	0
#define	PCIE_CORE_CI_CTL_BAD_CPL_DUMMY_ALL_1	1
#define	PCIE_CORE_CI_CTL_SET_BAD_ADDR_UR_DIS(r, v)	bitset32(r, 3, 3, v)
#define	PCIE_CORE_CI_CTL_SET_SPLIT_MODE(r, v)		bitset32(R, 2, 2, v)
#define	PCIE_CORE_CI_CTL_SPLIT_MODE_FULL	0
#define	PCIE_CORE_CI_CTL_SPLIT_MODE_EVEN	1

/*
 * PCIECORE::PCIE_BUS_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_BUS_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x84	\
}

/*
 * PCIECORE::PCIE_LC_STATE6 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x88	\
}

/*
 * PCIECORE::PCIE_LC_STATE7 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x8c	\
}

/*
 * PCIECORE::PCIE_LC_STATE8 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE8	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x90	\
}

/*
 * PCIECORE::PCIE_LC_STATE9 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE9	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x94	\
}

/*
 * PCIECORE::PCIE_LC_STATE10 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE10	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x98	\
}

/*
 * PCIECORE::PCIE_LC_STATE11 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATE11	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x9c	\
}

/*
 * PCIECORE::PCIE_LC_STATUS1 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATUS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xa0	\
}

/*
 * PCIECORE::PCIE_LC_STATUS2 - unused but captured for debugging.  Uses port
 * selection in DBG_CTL.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_STATUS2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xa4	\
}

/*
 * PCIECORE::PCIE_TX_CNTL3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xa8	\
}

/*
 * PCIECORE::PCIE_TX_STATUS - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_STATUS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xac	\
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
 * PCIECORE::PCIE_TX_LAST_TLP0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xd4	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xd8	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xdc	\
}

/*
 * PCIECORE::PCIE_TX_LAST_TLP3 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_LAST_TLP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xe0	\
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
 * PCIECORE::PCIE_LC_PORT_ORDER_CNTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_PORT_ORDER_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0xf8	\
}

/*
 * PCIECORE::PCIE_P_CNTL - Various controls around the phy.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_P_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x100	\
}
#define	PCIE_CORE_PCIE_P_CTL(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_PCIE_P_CTL, (p))
#define	PCIE_CORE_PCIE_P_CTL_SET_ELEC_IDLE(r, v)	bitset32(r, 15, 14, v)
/*
 * 2.5G Entry uses phy detector.
 * 5.0+ Entry uses inference logic
 * Exit always uses phy detector
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M0	0
/*
 * Electrical idle always uses inference logic, exit always uses phy.
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M1	1
/*
 * Electrical idle entry/exit always uses phy detector
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M2	2
/*
 * 8.0+ entry uses inference, everything else uses phy detector
 */
#define	PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M3	3
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_TOK_ERR(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_IDL_ERR(r, v)	bitset32(r, 7, 7, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_EDB_ERR(r, v)	bitset32(r, 6, 6, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_LEN_ERR(r, v)	bitset32(r, 5, 5, v)
#define	PCIE_CORE_PCIE_P_CTL_SET_IGN_CRC_ERR(r, v)	bitset32(r, 4, 4, v)

/*
 * PCIECORE::PCIE_P_BUF_STATUS - unused but captured for debugging.  RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_BUF_STATUS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x104	\
}

/*
 * PCIECORE::PCIE_P_DECODER_STATUS - unused but captured for debugging.  RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_DECODER_STATUS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x108	\
}

/*
 * PCIECORE::PCIE_P_MISC_STATUS - unused but captured for debugging.  RW1c.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_P_MISC_STATUS	(const smn_reg_def_t){	\
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
 * PCIECORE::PCIE_TX_CCIX_CNTL0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_CCIX_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x150	\
}

/*
 * PCIECORE::PCIE_TX_CCIX_CNTL1 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_CCIX_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x154	\
}

/*
 * PCIECORE::PCIE_TX_CCIX_PORT_MAP - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_CCIX_PORT_MAP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x158	\
}

/*
 * PCIECORE::PCIE_TX_CCIX_ERR_CTL - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_TX_CCIX_ERR_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x15c	\
}

/*
 * PCIECORE::PCIE_RX_CCIX_CTL0 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_RX_CCIX_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x160	\
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
#define	PCIE_CORE_SDP_CTL(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_SDP_CTL, (p))
#define	PCIE_CORE_SDP_CTL_SET_PORT_ID(r, v)	bitset32(r, 28, 26, v)
#define	PCIE_CORE_SDP_CTL_SET_UNIT_ID(r, v)	bitset32(r, 3, 0, v)

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
#define	PCIE_CORE_STRAP_F0(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_STRAP_F0, (p))
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
#define	D_PCIE_CORE_PRBS_STATUS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x324	\
}

/*
 * PCIECORE::PCIE_PRBS_STATUS2 - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRBS_STATUS2	(const smn_reg_def_t){	\
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
#define	D_PCIE_CORE_SWRST_CMD_STATUS	(const smn_reg_def_t){	\
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
#define	PCIE_CORE_SWRST_CTL6(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_SWRST_CTL6, (p))
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_K(r, v)	bitset32(r, 10, 10, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_J(r, v)	bitset32(r, 9, 9, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_I(r, v)	bitset32(r, 8, 8, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_H(r, v)	bitset32(r, 7, 7, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_G(r, v)	bitset32(r, 6, 6, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_F(r, v)	bitset32(r, 5, 5, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_E(r, v)	bitset32(r, 4, 4, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_D(r, v)	bitset32(r, 3, 3, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_C(r, v)	bitset32(r, 2, 2, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_B(r, v)	bitset32(r, 1, 1, v)
#define	PCIE_CORE_SWRST_CTL6_SET_HOLD_A(r, v)	bitset32(r, 0, 0, v)

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
 * PCIECORE::LNCNT_QUAN_THRD - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LNCNT_QUAN_THRD	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x49c	\
}

/*
 * PCIECORE::LNCNT_WEIGHT - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LNCNT_WEIGHT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4a0	\
}

/*
 * PCIECORE::SMU_HP_STATUS_UPDATE - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_HP_STATUS_UPDATE	(const smn_reg_def_t){	\
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
 * PCIECORE::SMU_PCIE_DF_Address - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_DF_ADDR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4c8	\
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
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_ERRORS_EN	0
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_ERRORS_DIS	1
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_METHOD(r, v)	bitset32(r, 3, 3, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_METHOD_COUNT	0
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_METHOD_RATE		1
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_TIME(r, v)	bitset32(r, 2, 2, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_VOLT(r, v)	bitset32(r, 1, 1, v)
#define	PCIE_CORE_RX_MARGIN_CTL_CAP_SET_VOLT_SUP(r, v)	bitset32(r, 0, 0, v)

/*
 * PCIECORE::PCIE_RXMARGIN_1_SETTINGS - This register controls the limits of
 * margining. The OFF fields control the maximum distance margining can
 * travel. The STEPS fields control how many steps margining can take.
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
 * that are used during the margining process itself.  The latter two fields
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
#define	PCIE_CORE_RX_MARGIN2_SET_ERR_LIM(r, v)		bitset32(r, 29, 24, v)
#define	PCIE_CORE_RX_MARGIN2_SET_NLANES(r, v)		bitset32(r, 23, 19, v)
#define	PCIE_CORE_RX_MARGIN2_GET_COUNT(r)		bitx32(r, 18, 12)
#define	PCIE_CORE_RX_MARGIN2_SET_TIME_RATIO(r, v)	bitx32(r, 11, 6, v)
#define	PCIE_CORE_RX_MARGIN2_SET_VOLT_RATIO(r, v)	bitx32(r, 5, 0, v)

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
#define	PCIE_CORE_PRES(n, p)	\
    genoa_pcie_core_smn_reg((n), D_PCIE_CORE_PRES, (p))
#define	PCIE_CORE_PRES_SET_MODE(r, v)	bitset32(r, 24, 24, v)
#define	PCIE_CORE_PRES_MODE_OR	0
#define	PCIE_CORE_PRES_MODE_AND	1

/*
 * PCIECORE::PCIE_LC_DEBUG_CNTL - Analogous to the DBG_CTL register's ability to
 * select specific port(s) for which other data should be collected in debugging
 * registers, this selects lane(s) for certain registers and fields that collect
 * per-lane debug data.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_LC_DBG_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4e4	\
}
#define	PCIE_CORE_LC_DBG_CTL_SET_LANE_MASK(r, v)	bitset32(r, 31, 16, v)
#define	PCIE_CORE_LC_DBG_CTL_GET_LANE_MASK(r)		bitx32(r, 31, 16)

/*
 * PCIECORE::SMU_PCIE_FENCED1_REG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_FENCED1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x600	\
}

/*
 * PCIECORE::SMU_PCIE_FENCED2_REG - unused but captured for debugging.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_SMU_FENCED2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x604	\
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
#define	GENOA_BRIDGE_R_PCI_PCIE_CAP	0x5a

/*
 * PCIERCCFG::SLOT_CAP, PCIERCCFG::SLOT_CNTL, PCIERCCFG::SLOT_STATUS. This is
 * the PCIe capability's slot capability, control, and status registers
 * respectively.  This is the illumos PCIE_SLOTCAP, PCIE_SLOTCTL, and
 * PCIE_SLOTSTS, but already adjusted for the capability offset.
 */

#define	GENOA_BRIDGE_R_PCI_SLOT_CAP	0x6c
#define	GENOA_BRIDGE_R_PCI_SLOT_CTL	0x70
#define	GENOA_BRIDGE_R_PCI_SLOT_STS	0x72

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_PCIE_H */
