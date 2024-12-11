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

#ifndef _SYS_IO_MILAN_NBIF_H
#define	_SYS_IO_MILAN_NBIF_H

/*
 * Milan-specific register and bookkeeping definitions for North Bridge
 * Interfaces (nBIFs).
 */

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/nbif.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * There are always three primary nBIFs in each NBIO unit, but only two of the
 * SYSHUB nBIFs in alternate space. These definitions live here because they
 * are consumed by the register calculations below.
 */
#define	MILAN_IOMS_MAX_NBIF		3
#define	MILAN_IOMS_MAX_NBIF_ALT		2
/*
 * These are the maximum number of devices and functions on any nBIF instance
 * according to the PPR. This must be kept synchronized with the
 * milan_nbif_data structure which lays out the individual functions on each
 * device.
 */
#define	MILAN_NBIF_MAX_DEVS		3
#define	MILAN_NBIF_MAX_FUNCS		7

/*
 * nBIF SMN Addresses. These have multiple different shifts that we need to
 * account for. There are different bases based on which IOMS, which NBIF, and
 * which downstream device and function as well. There is a second SMN aperture
 * ID that seems to be used that deals with the nBIF's clock gating, DMA
 * enhancements with the syshub, and related.
 *
 * There is no way to pretend that the NBIF addressing schemes fit any kind of
 * coherent plan.  We model them as well as we practically can with completely
 * custom register calculation routines because that's just how the hardware is.
 *
 * All NBIF registers are 32 bits wide; we check for violations.
 */

static inline smn_reg_t
milan_nbif_func_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t nbifno, const uint8_t devno, const uint8_t funcno)
{
	const uint32_t NBIF_FUNC_SMN_REG_MASK = 0x1ff;

	/*
	 * Each entry in this matrix is a bitmask of valid function numbers for
	 * each device on each NBIF (on all IOMSs).  This is used only for
	 * checking the device and function numbers passed to us when built with
	 * DEBUG enabled.  This must be in sync with milan_nbif_data in
	 * milan_fabric.c, though these describe hardware so no changes are
	 * forseen.
	 */
#ifdef	DEBUG
	const uint8_t MILAN_NBIF_FNVALID[MILAN_IOMS_MAX_NBIF]
	    [MILAN_NBIF_MAX_DEVS] = {
		{ 0x07, 0x00, 0x00 },
		{ 0x1f, 0x01, 0x01 },
		{ 0x07, 0x00, 0x00 }
	};
#endif
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t dev32 = (const uint32_t)devno;
	const uint32_t func32 = (const uint32_t)funcno;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF_FUNC);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT0(def.srd_reg & ~NBIF_FUNC_SMN_REG_MASK);

	ASSERT3U(ioms32, <, 4);
	ASSERT3U(nbif32, <, MILAN_IOMS_MAX_NBIF);
	ASSERT3U(dev32, <, MILAN_NBIF_MAX_DEVS);
	ASSERT3U(func32, <, MILAN_NBIF_MAX_FUNCS);

	ASSERT3U(bitx8(MILAN_NBIF_FNVALID[nbifno][devno], funcno, funcno), !=,
	    0);

	const uint32_t aperture_base = 0x10134000;

	const uint32_t aperture_off = (ioms32 << 20) + (nbif32 << 22) +
	    (dev32 << 12) + (func32 << 9);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & NBIF_FUNC_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

static inline smn_reg_t
milan_nbif_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t nbifno, const uint16_t reginst)
{
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF);
	ASSERT3U(ioms32, <, 4);
	ASSERT3U(nbif32, <, MILAN_IOMS_MAX_NBIF);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_base = 0x10100000;

	const uint32_t aperture_off = (ioms32 << 20) + (nbif32 << 22);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + reg));
}

static inline smn_reg_t
milan_nbif_alt_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint8_t nbifno, const uint16_t reginst)
{
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF_ALT);
	ASSERT3U(ioms32, <, 4);
	ASSERT3U(nbif32, <, MILAN_IOMS_MAX_NBIF_ALT);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_base = 0x01400000;

	const uint32_t aperture_off = (ioms32 << 20) + (nbif32 << 22);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + reg));
}

/*
 * NBIFMM::RCC_DEVn_EPFn_STRAP0.  NBIF Function strap 0. This SMN address is
 * relative to the actual function space.
 */
/*CSTYLED*/
#define	D_NBIF_FUNC_STRAP0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_FUNC,	\
	.srd_reg = 0x00	\
}
#define	NBIF_FUNC_STRAP0(i, n, d, f)	\
    milan_nbif_func_smn_reg(i, D_NBIF_FUNC_STRAP0, n, d, f)
#define	NBIF_FUNC_STRAP0_SET_SUP_D2(r, v)	bitset32(r, 31, 31, v)
#define	NBIF_FUNC_STRAP0_SET_SUP_D1(r, v)	bitset32(r, 30, 30, v)
#define	NBIF_FUNC_STRAP0_SET_BE_PCIE(r, v)	bitset32(r, 29, 29, v)
#define	NBIF_FUNC_STRAP0_SET_EXIST(r, v)	bitset32(r, 28, 28, v)
#define	NBIF_FUNC_STRAP0_SET_GFX_REV(r, v)	bitset32(r, 27, 24, v)
#define	NBIF_FUNC_STRAP0_SET_MIN_REV(r, v)	bitset32(r, 23, 20, v)
#define	NBIF_FUNC_STRAP0_SET_MAJ_REV(r, v)	bitset32(r, 19, 16, v)
#define	NBIF_FUNC_STRAP0_SET_DEV_ID(r, v)	bitset32(r, 15, 0, v)

/*
 * NBIFMM::INTR_LINE_ENABLE.  This register is arranged with one byte per
 * device. Each bit corresponds to an endpoint function.
 */
/*CSTYLED*/
#define	D_NBIF_INTR_LINE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a008	\
}
#define	NBIF_INTR_LINE_EN(i, n)	milan_nbif_smn_reg(i, D_NBIF_INTR_LINE_EN, n)

/*
 * NBIFMM::BIFC_MISC_CTRL0.  As the name suggests, miscellaneous per-NBIF
 * control bits.
 */
/*CSTYLED*/
#define	D_NBIF_BIFC_MISC_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a010	\
}
#define	NBIF_BIFC_MISC_CTL0(i, n)	\
    milan_nbif_smn_reg(i, D_NBIF_BIFC_MISC_CTL0, n, 0)
#define	NBIF_BIFC_MISC_CTL0_SET_PME_TURNOFF(r, v)	bitset32(r, 28, 28, v)
#define	NBIF_BIFC_MISC_CTL0_PME_TURNOFF_BYPASS		0
#define	NBIF_BIFC_MISC_CTL0_PME_TURNOFF_FW		1

/*
 * NBIFMM::BIF_GMI_WRR_WEIGHT[3:2].  These two registers are used for some
 * amount of arbitration in the same vein as the SION values. The base register
 * which we don't use has a bit that selects between payload-based and
 * request-based interpretation of these values.
 */
/*CSTYLED*/
#define	D_NBIF_GMI_WRR_WEIGHT2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a124	\
}
/*CSTYLED*/
#define	D_NBIF_GMI_WRR_WEIGHT3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a128	\
}
#define	NBIF_GMI_WRR_WEIGHT2(i, n)	\
    milan_nbif_smn_reg(i, D_NBIF_GMI_WRR_WEIGHT2, n, 0)
#define	NBIF_GMI_WRR_WEIGHT3(i, n)	\
    milan_nbif_smn_reg(i, D_NBIF_GMI_WRR_WEIGHT3, n, 0)
#define	NBIF_GMI_WRR_WEIGHTn_VAL	0x04040404

/*
 * NBIFMM::NBIF_MGCG_CTRL_LCLK
 */
/*CSTYLED*/
#define	D_NBIF_MGCG_CTL_LCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a21c	\
}
#define	NBIF_MGCG_CTL_LCLK_SET_HYST(r, v)		bitset32(r, 9, 2, v)
/*
 * The reset value for this hysteresis setting according to the PPR is 0x40,
 * but AGESA explicitly sets 0x20. We do the same.
 */
#define	NBIF_MGCG_CTL_LCLK_HYST		0x20
#define	NBIF_MGCG_CTL_LCLK_SET_MODE(r, v)		bitset32(r, 1, 1, v)
#define	NBIF_MGCG_CTL_LCLK_SET_EN(r, v)			bitset32(r, 0, 0, v)

/*
 * NBIFMM::RCC_DEVn_PORT_STRAP3.  Straps for the NBIF port. These are relative
 * to the main NBIF base aperture.
 */
/*CSTYLED*/
#define	D_NBIF_PORT_STRAP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3100c,	\
	.srd_nents = MILAN_NBIF_MAX_DEVS,	\
	.srd_stride = 0x200	\
}
#define	NBIF_PORT_STRAP3(i, n, d)	\
    milan_nbif_smn_reg(i, D_NBIF_PORT_STRAP3, n, d)
#define	NBIF_PORT_STRAP3_SET_COMP_TO(r, v)		bitset32(r, 7, 7, v)

/*
 * SYSHUBMM::NGDC_MGCG_CTRL
 */
/*CSTYLED*/
#define	D_NBIF_ALT_NGDC_MGCG_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x3ba8	\
}
#define	NBIF_ALT_NGDC_MGCG_CTL_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * SYSHUBMM::SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK.  Yes, really.  This
 * register is a weird SYSHUB and NBIF crossover that is in the alternate space.
 */
/*CSTYLED*/
#define	D_NBIF_ALT_BGEN_BYP_SOC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x10008	\
}
#define	NBIF_ALT_BGEN_BYP_SOC(i, n)	\
    milan_nbif_alt_smn_reg(i, D_NBIF_ALT_BGEN_BYP_SOC, n, 0)
#define	NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW1(r, v)		bitset32(r, 17, 17, v)
#define	NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW0(r, v)		bitset32(r, 16, 16, v)

/*
 * SYSHUBMM::SYSHUB_MGCG_CTRL_SOCCLK
 */
/*CSTYLED*/
#define	D_NBIF_ALT_MGCG_CTL_SCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x10020	\
}
#define	NBIF_ALT_MGCG_CTL_SCLK_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * SYSHUBMM::SYSHUB_MGCG_CTRL_SHUBCLK
 */
/*CSTYLED*/
#define	D_NBIF_ALT_MGCG_CTL_SHCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x11020	\
}
#define	NBIF_ALT_MGCG_CTL_SHCLK_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * SYSHUBMM::SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SHUBCLK. As the previous
 * register, this register is a weird SYSHUB and NBIF crossover that is in the
 * alternate space.
 */
/*CSTYLED*/
#define	D_NBIF_ALT_BGEN_BYP_SHUB	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x11008	\
}
#define	NBIF_ALT_BGEN_BYP_SHUB(i, n)	\
    milan_nbif_alt_smn_reg(i, D_NBIF_ALT_BGEN_BYP_SHUB, n, 0)
#define	NBIF_ALT_BGEN_BYP_SHUB_SET_DMA_SW1(r, v)	bitset32(r, 17, 17, v)
#define	NBIF_ALT_BGEN_BYP_SHUB_SET_DMA_SW0(r, v)	bitset32(r, 16, 16, v)

/*
 * SYSHUBMM::SION_CNTL_REG0
 */
/*CSTYLED*/
#define	D_NBIF_ALT_SION_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x1e140	\
}
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK9(r, v)		bitset32(r, 19, 19, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK8(r, v)		bitset32(r, 18, 18, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK7(r, v)		bitset32(r, 17, 17, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK6(r, v)		bitset32(r, 16, 16, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK5(r, v)		bitset32(r, 15, 15, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK4(r, v)		bitset32(r, 14, 14, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK3(r, v)		bitset32(r, 13, 13, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK2(r, v)		bitset32(r, 12, 12, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK1(r, v)		bitset32(r, 11, 11, v)
#define	NBIF_ALT_SION_CTL_SET_CTL1_SOCLK0(r, v)		bitset32(r, 10, 10, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK9(r, v)		bitset32(r, 9, 9, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK8(r, v)		bitset32(r, 8, 8, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK7(r, v)		bitset32(r, 7, 7, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK6(r, v)		bitset32(r, 6, 6, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK5(r, v)		bitset32(r, 5, 5, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK4(r, v)		bitset32(r, 4, 4, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK3(r, v)		bitset32(r, 3, 3, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK2(r, v)		bitset32(r, 2, 2, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK1(r, v)		bitset32(r, 1, 1, v)
#define	NBIF_ALT_SION_CTL_SET_CTL0_SOCLK0(r, v)		bitset32(r, 0, 0, v)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_NBIF_H */
