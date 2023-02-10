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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_PCIE_H
#define	_SYS_IO_MILAN_PCIE_H

/*
 * Milan-specific register and bookkeeping definitions for PCIe root complexes,
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
struct milan_pcie_bridge;
struct milan_pcie_port;

typedef struct milan_pcie_bridge milan_pcie_bridge_t;
typedef struct milan_pcie_port milan_pcie_port_t;

typedef int (*milan_pcie_port_cb_f)(milan_pcie_port_t *, void *);
typedef int (*milan_bridge_cb_f)(milan_pcie_bridge_t *, void *);

extern uint8_t milan_nbio_n_pcie_ports(const uint8_t);
extern uint8_t milan_pcie_port_n_bridges(const uint8_t);

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
milan_pcie_core_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t portno)
{
	const uint32_t PCIE_CORE_SMN_REG_MASK = 0x7ffff;
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t port32 = (const uint32_t)portno;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_CORE);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(ioms32, <, 4);
	ASSERT0(def.srd_reg & ~PCIE_CORE_SMN_REG_MASK);

#ifdef	DEBUG
	const uint32_t nents = milan_nbio_n_pcie_ports(iomsno);
	ASSERT3U(nents, >, port32);
#endif	/* DEBUG */

	const uint32_t aperture_base = 0x11180000;

	const uint32_t aperture_off = (ioms32 << 20) + (port32 << 22);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_CORE_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

static inline smn_reg_t
milan_pcie_port_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t portno, const uint8_t bridgeno)
{
	const uint32_t PCIE_PORT_SMN_REG_MASK = 0xfff;
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t port32 = (const uint32_t)portno;
	const uint32_t bridge32 = (const uint32_t)bridgeno;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_PORT);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(ioms32, <, 4);
	ASSERT0(def.srd_reg & ~PCIE_PORT_SMN_REG_MASK);

#ifdef	DEBUG
	const uint32_t nports = (const uint32_t)milan_nbio_n_pcie_ports(iomsno);
	ASSERT3U(nports, >, port32);
	const uint32_t nents =
	    (const uint32_t)milan_pcie_port_n_bridges(portno);
	ASSERT3U(nents, >, bridge32);
#endif	/* DEBUG */

	const uint32_t aperture_base = 0x11140000;

	const uint32_t aperture_off = (ioms32 << 20) + (port32 << 22) +
	    (bridge32 << 12);
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
    milan_pcie_port_smn_reg(n, D_PCIE_PORT_HW_DBG, p, b)
#define	PCIE_PORT_HW_DBG_SET_DBG15(r, v)	bitset32(r, 15, 15, v)

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
    milan_pcie_port_smn_reg(n, D_PCIE_PORT_PCTL, p, b)
#define	PCIE_PORT_PCTL_SET_PWRFLT_EN(r, v)	bitset32(r, 4, 4, v)

/*
 * PCIEPORT::PCIE_TX_CNTL - PCIe TX Control. This is a register that exists in
 * 'Port Space' and is specific to a bridge. XXX figure out what other bits we
 * need.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_TX_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x80	\
}
#define	PCIE_PORT_TX_CTL(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_TX_CTL, (p), (b))
#define	PCIE_PORT_TX_CTL_SET_TLP_FLUSH_DOWN_DIS(r, v)	bitset32(r, 15, 15, v)

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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_TX_ID, (p), (b))
#define	PCIE_PORT_TX_ID_SET_BUS(r, v)	bitset32(r, 15, 8, v)
#define	PCIE_PORT_TX_ID_SET_DEV(r, v)	bitset32(r, 7, 3, v)
#define	PCIE_PORT_TX_ID_SET_FUNC(r, v)	bitset32(r, 2, 0, v)

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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL, (p), (b))
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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_TRAIN_CTL, (p), (b))
#define	PCIE_PORT_LC_TRAIN_CTL_SET_TRAIN_DIS(r, v)	bitset32(r, 13, 13, v)
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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_WIDTH_CTL, (p), (b))
#define	PCIE_PORT_LC_WIDTH_CTL_SET_DUAL_RECONFIG(r, v)	bitset32(r, 19, 19, v)
#define	PCIE_PORT_LC_WIDTH_CTL_SET_RENEG_EN(r, v)	bitset32(r, 10, 10, v)

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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_SPEED_CTL, (p), (b))
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
 * one of several successful training states.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x294	\
}
#define	PCIE_PORT_LC_STATE0(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE0, (p), (b))
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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE1, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x29c	\
}
#define	PCIE_PORT_LC_STATE2(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE2, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a0	\
}
#define	PCIE_PORT_LC_STATE3(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE3, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a4	\
}
#define	PCIE_PORT_LC_STATE4(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE4, (p), (b))

/*CSTYLED*/
#define	D_PCIE_PORT_LC_STATE5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2a8	\
}
#define	PCIE_PORT_LC_STATE5(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_STATE5, (p), (b))

/*
 * PCIEPORT::PCIE_LC_CNTL2 - Port Link Control Register 2.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2c4	\
}
#define	PCIE_PORT_LC_CTL2(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL2, (p), (b))
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
 * PCIEPORT::PCIE_LC_CNTL3 - Port Link Control Register 3. This isn't the last
 * of these and is a bunch of different settings.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2d4	\
}
#define	PCIE_PORT_LC_CTL3(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL3, (p), (b))
#define	PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(r, v)	bitset32(r, 12, 12, v)
#define	PCIE_PORT_LC_CTL3_RCVR_DET_OVR(r, v)		bitset32(r, 11, 11, v)
#define	PCIE_PORT_LC_CTL3_ENH_HP_EN(r, v)		bitset32(r, 10, 10, v)

/*
 * PCIEPORT::PCIE_LC_CNTL5 - Port Link Control Register 5. There are several
 * others, but this one seems to be required for hotplug.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2dc	\
}
#define	PCIE_PORT_LC_CTL5(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL5, (p), (b))
#define	PCIE_PORT_LC_CTL5_SET_WAIT_DETECT(r, v)	bitset32(r, 28, 28, v)

/*
 * PCIEPORT::PCIE_LC_CNTL6 - Port Link Control Register 6.  SRIS stuff lives
 * here, with other bits.
 */
/*CSTYLED*/
#define	D_PCIE_PORT_LC_CTL6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_PORT,	\
	.srd_reg = 0x2ec	\
}
#define	PCIE_PORT_LC_CTL6(n, p, b)	\
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_LC_CTL6, (p), (b))
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
    milan_pcie_port_smn_reg((n), D_PCIE_PORT_HP_CTL, (p), (b))
#define	PCIE_PORT_HP_CTL_SET_ACTIVE(r, v)	bitset32(r, 31, 31, v)
#define	PCIE_PORT_HP_CTL_SET_SLOT(r, v)		bitset32(r, 5, 0, v)

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
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_PCIE_CTL, (p))
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_FUNC_DIS(r, v)	bitset32(r, 22, 22, v)
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_ATTR_DIS(r, v)	bitset32(r, 21, 21, v)
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_PREFIX_DIS(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_CORE_PCIE_CTL_SET_RCB_BAD_SIZE_DIS(r, v)	bitset32(r, 17, 17, v)
#define	PCIE_CORE_PCIE_CTL_SET_HW_LOCK(r, v)		bitset32(r, 0, 0, v)

/*
 * PCIECORE::PCIE_CNTL2 - Additional PCIe port level controls. Covers power,
 * atomics, and some amount of transmit.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x70	\
}
#define	PCIE_CORE_PCIE_CTL2(n, p)	\
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_PCIE_CTL2, (p))
#define	PCIE_CORE_PCIE_CTL2_TX_ATOMIC_ORD_DIS(r, v)	bitset32(r, 14, 14, v)
#define	PCIE_CORE_PCIE_CTL2_TX_ATOMIC_OPS_DIS(r, v)	bitset32(r, 13, 13, v)

/*
 * PCIECORE::PCIE_CI_CNTL - PCIe Port level TX controls. Note, this register is
 * in 'core' space and is specific to the overall milan_pcie_port_t, as opposed
 * to the bridge. XXX Add in other bits.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_CI_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x80	\
}
#define	PCIE_CORE_CI_CTL(n, p)	\
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_CI_CTL, (p))
#define	PCIE_CORE_CI_CTL_SET_IGN_LINK_DOWN_CTO_ERR(r, v)	\
    bitset32(r, 31, 31, v)
#define	PCIE_CORE_CI_CTL_SET_LINK_DOWN_CTO_EN(r, v)	bitset32(r, 29, 29, v)

/*
 * PCIECORE::PCIE_P_CNTL - Various controls around the phy.
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PCIE_P_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x100	\
}
#define	PCIE_CORE_PCIE_P_CTL(n, p)	\
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_PCIE_P_CTL, (p))
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
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_SDP_CTL, (p))
#define	PCIE_CORE_SDP_CTL_SET_PORT_ID(r, v)	bitset32(r, 28, 26, v)
#define	PCIE_CORE_SDP_CTL_SET_UNIT_ID(r, v)	bitset32(r, 3, 0, v)

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
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_STRAP_F0, (p))
#define	PCIE_CORE_STRAP_F0_SET_ATOMIC_ROUTE(r, v)	bitset32(r, 20, 20, v)
#define	PCIE_CORE_STRAP_F0_SET_ATOMIC_EN(r, v)		bitset32(r, 18, 18, v)

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
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_SWRST_CTL6, (p))
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
 * PCIECORE::PCIE_PRESENCE_DETECT_SELECT - PCIe Presence Detect Control. This is
 * 'Core Space', so it exists once per port. This is used to determine whether
 * we should consider something present based on the link up OR the side-band
 * signals, or instead require both (e.g. AND).
 */
/*CSTYLED*/
#define	D_PCIE_CORE_PRES	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_CORE,	\
	.srd_reg = 0x4e0	\
}
#define	PCIE_CORE_PRES(n, p)	\
    milan_pcie_core_smn_reg((n), D_PCIE_CORE_PRES, (p))
#define	PCIE_CORE_PRES_SET_MODE(r, v)	bitset32(r, 24, 24, v)
#define	PCIE_CORE_PRES_MODE_OR	0
#define	PCIE_CORE_PRES_MODE_AND	1

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
#define	MILAN_BRIDGE_R_PCI_PCIE_CAP	0x5a

/*
 * PCIERCCFG::SLOT_CAP, PCIERCCFG::SLOT_CNTL, PCIERCCFG::SLOT_STATUS. This is
 * the PCIe capability's slot capability, control, and status registers
 * respectively.  This is the illumos PCIE_SLOTCAP, PCIE_SLOTCTL, and
 * PCIE_SLOTSTS, but already adjusted for the capability offset.
 */

#define	MILAN_BRIDGE_R_PCI_SLOT_CAP	0x6c
#define	MILAN_BRIDGE_R_PCI_SLOT_CTL	0x70
#define	MILAN_BRIDGE_R_PCI_SLOT_STS	0x72

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_PCIE_H */
