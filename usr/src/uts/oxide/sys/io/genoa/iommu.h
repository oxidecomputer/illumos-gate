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

#ifndef _SYS_IO_GENOA_IOMMU_H
#define	_SYS_IO_GENOA_IOMMU_H

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/iommu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IOMMU Registers. The IOMMU is broken into an L1 and L2. The IOMMU L1
 * registers work a lot like the IOHCDEV registers in that there is a block for
 * each of several other devices: two PCIe ports (even on NBIO0) and an IOAGR.
 * The L2 register set only exists on a per-IOMS basis and looks like a
 * standard SMN functional unit. All these registers are 32 bits wide; we check
 * for violations.
 */

#define	IOMMUL1_N_UNITS		4
#define	IOMMUL1_N_PCIE_CORES	2

ZEN_MAKE_SMN_IOMMUL1_REG_FN(genoa, PCIE, pcie, 0x14700000,
    IOMMUL1_N_PCIE_CORES, 22, IOMMUL1_N_UNITS);
ZEN_MAKE_SMN_IOMMUL1_REG_FN(genoa, IOAGR, ioagr, 0x15300000, 1, 0,
    IOMMUL1_N_UNITS);

AMDZEN_MAKE_SMN_REG_FN(genoa_iommul2_smn_reg, IOMMUL2, 0x13f00000,
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
#define	IOMMUL1_PCIE_CTL1(i, p)	\
    genoa_iommul1_pcie_smn_reg(i, D_IOMMUL1_CTL1, p)
#define	IOMMUL1_NBIF_CTL1(i)	\
    genoa_iommul1_ioagr_smn_reg(i, D_IOMMUL1_CTL1, 0)
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
    genoa_iommul1_pcie_smn_reg(i, IOMMUL1_PCIE_SB_LOCATION, p)
#define	IOMMUL1_IOAGR_SB_LOCATION(i)	\
    genoa_iommul1_ioagr_smn_reg(i, IOMMUL1_IOAGR_SB_LOCATION, 0)

/*
 * IOMMUL2::L2_SB_LOCATION. Yet another place we program the FCH information.
 */
/*CSTYLED*/
#define	D_IOMMUL2_SB_LOCATION	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUL2,	\
	.srd_reg = 0x112c	\
}
#define	IOMMUL2_SB_LOCATION(i)	genoa_iommul2_smn_reg(i, IOMMUL2_SB_LOCATION)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_IOMMU_H */
