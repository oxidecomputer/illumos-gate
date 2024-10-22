/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2024 Oxide Computer Company
 */

#ifndef _SYS_IO_TURIN_IOMMU_H
#define	_SYS_IO_TURIN_IOMMU_H

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/iommu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IOMMU Registers. The IOMMU is broken into an L1 and L2. The IOMMU L1
 * registers work a lot like the IOHCDEV registers in that there is a block for
 * each of several other devices: four PCIe ports and an IOAGR. The L2 register
 * set only exists on a per-IOMS basis and looks like a standard SMN functional
 * unit. All these registers are 32 bits wide; we check for violations.
 */
#define	IOMMUL1_N_IOAGR_UNITS	4
#define	IOMMUL1_N_PCIE_UNITS	8
#define	IOMMUL1_N_PCIE_CORES	4

static inline smn_reg_t
turin_iommul1_pcie_smn_reg(const uint8_t iohcno,
    const smn_reg_def_t def, const uint8_t unitno)
{
	const uint32_t iohc32 = (const uint32_t)iohcno;
	const uint32_t unit32 = (const uint32_t)unitno;

	ASSERT0(def.srd_size);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOMMUL1);
	ASSERT3U(iohc32, <, IOMMUL1_N_PCIE_UNITS);
	ASSERT3U(unit32, <, IOMMUL1_N_PCIE_CORES);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_base = unitno < 2 ?
	    0x14700000 : 0x14B0000;

	const uint32_t aperture_off = (iohc32 << 20) +
	    ((unit32 % 2) << 22);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

ZEN_MAKE_SMN_IOMMUL1_REG_FN(turin, IOAGR, ioagr, 0x15300000, 1, 0,
    IOMMUL1_N_IOAGR_UNITS);

AMDZEN_MAKE_SMN_REG_FN(turin_iommul2_smn_reg, IOMMUL2, 0x13f00000,
    SMN_APERTURE_MASK, 4, 20);

/*
 * Unlike IOHCDEV, all the registers in IOMMUL1 space exist for each functional
 * unit, and none has any further instances beyond one per unit (i.e., no
 * per-bridge registers in PCIe or NBIF space). This leads to a lot of
 * duplication which C gives us no way to avoid other than external
 * metaprogramming.
 */

/*
 * IOMMUL1::L1_MISC_CNTRL_1. This register contains a smorgasbord of settings.
 * Some of which are used in the hotplug path.
 */
/*CSTYLED*/
#define	D_IOMMUL1_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUL1,	\
	.srd_reg = 0x1c	\
}
#define	IOMMUL1_CTL1_SET_ORDERING(r, v)	bitset32(r, 0, 0, v)

/*
 * IOMMUL1::L1_SB_LOCATION. Programs where the FCH is into a given L1 IOMMU.
 */
/*CSTYLED*/
#define	D_IOMMUL1_SB_LOCATION	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUL1,	\
	.srd_reg = 0x24	\
}
#define	IOMMUL1_PCIE_SB_LOCATION(i, p)	\
    turin_iommul1_pcie_smn_reg(i, IOMMUL1_PCIE_SB_LOCATION, p)
#define	IOMMUL1_IOAGR_SB_LOCATION(i)	\
    turin_iommul1_ioagr_smn_reg(i, IOMMUL1_IOAGR_SB_LOCATION, 0)

/* These macros are common across SB_LOCATION in IOMMUL1 and IOMMUL2 */
#define	IOMMUL_SB_LOCATION_SET_CORE(r, v)	bitset32(r, 31, 16, v)
#define	IOMMUL_SB_LOCATION_CORE_GPP0	1
#define	IOMMUL_SB_LOCATION_CORE_GPP1	2
#define	IOMMUL_SB_LOCATION_CORE_GPP2	4
#define	IOMMUL_SB_LOCATION_SET_PORT(r, v)	bitset32(r, 15, 0, v)
#define	IOMMUL_SB_LOCATION_PORT_A	1
#define	IOMMUL_SB_LOCATION_PORT_B	2
#define	IOMMUL_SB_LOCATION_PORT_C	4
#define	IOMMUL_SB_LOCATION_PORT_D	8

/*
 * IOMMUL2::L2_SB_LOCATION. Yet another place we program the FCH information.
 */
/*CSTYLED*/
#define	D_IOMMUL2_SB_LOCATION	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUL2,	\
	.srd_reg = 0x112c	\
}
#define	IOMMUL2_SB_LOCATION(i)	turin_iommul2_smn_reg(i, IOMMUL2_SB_LOCATION)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_IOMMU_H */
