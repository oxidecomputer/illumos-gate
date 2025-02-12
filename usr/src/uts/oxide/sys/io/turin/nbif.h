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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _SYS_IO_TURIN_NBIF_H
#define	_SYS_IO_TURIN_NBIF_H

/*
 * Turin-specific register and bookkeeping definitions for North Bridge
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
#define	TURIN_MAX_NBIO			2
#define	TURIN_NBIO_MAX_NBIF		3
#define	TURIN_NBIO_MAX_NBIF_ALT		2
/*
 * These are the maximum number of devices and functions on any nBIF instance
 * according to the PPR. This must be kept synchronized with the
 * turin_nbif_data structure which lays out the individual functions on each
 * device.
 */
#define	TURIN_NBIF_MAX_PORTS		2
#define	TURIN_NBIF_MAX_FUNCS		8

/*
 * nBIF SMN Addresses. These have multiple different shifts that we need to
 * account for. There are different bases based on which nbio, which NBIF, and
 * which downstream device and function as well. There is a second SMN aperture
 * ID that seems to be used that deals with the nBIF's clock gating, DMA
 * enhancements with the syshub, and related.
 *
 * There is no way to pretend that the NBIF addressing schemes fit any kind of
 * coherent plan. We model them as well as we practically can with completely
 * custom register calculation routines because that's just how the hardware
 * is.
 *
 * All NBIF registers are 32 bits wide; we check for violations.
 */

static inline smn_reg_t
turin_nbif_func_smn_reg(const uint8_t nbiono, const smn_reg_def_t def,
    const uint8_t nbifno, const uint8_t devno, const uint8_t funcno)
{
	const uint32_t NBIF_FUNC_SMN_REG_MASK = 0x1ff;

	/*
	 * Each entry in this matrix is a bitmask of valid function numbers for
	 * each device on each NBIF (on all nbios). This is used only for
	 * checking the device and function numbers passed to us when built
	 * with DEBUG enabled. This must be in sync with turin_nbifN in
	 * turin_fabric.c, though these describe hardware so no changes are
	 * forseen.
	 */
#ifdef	DEBUG
	const uint8_t TURIN_NBIF_FNVALID[TURIN_NBIO_MAX_NBIF]
	    [TURIN_NBIF_MAX_PORTS] = {
		{ 0xff, 0x03 },
		{ 0x00, 0x00 },
		{ 0x03, 0x00 }
	};
#endif
	const uint32_t nbio32 = (const uint32_t)nbiono;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t dev32 = (const uint32_t)devno;
	const uint32_t func32 = (const uint32_t)funcno;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF_FUNC);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT0(def.srd_reg & ~NBIF_FUNC_SMN_REG_MASK);

	ASSERT3U(nbio32, <, TURIN_MAX_NBIO);
	ASSERT3U(nbif32, <, TURIN_NBIO_MAX_NBIF);
	ASSERT3U(dev32, <, TURIN_NBIF_MAX_PORTS);
	ASSERT3U(func32, <, TURIN_NBIF_MAX_FUNCS);

	ASSERT3U(bitx8(TURIN_NBIF_FNVALID[nbifno][devno], funcno, funcno), !=,
	    0);

	const uint32_t aperture_base = 0x10134000;

	const uint32_t aperture_off = (nbio32 << 21) + (nbif32 << 20) +
	    (dev32 << 12) + (func32 << 9);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & NBIF_FUNC_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

static inline smn_reg_t
turin_nbif_smn_reg(const uint8_t nbiono, const smn_reg_def_t def,
    const uint8_t nbifno, const uint16_t reginst)
{
	const uint32_t nbio32 = (const uint32_t)nbiono;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF);
	ASSERT3U(nbio32, <, TURIN_MAX_NBIO);
	ASSERT3U(nbif32, <, TURIN_NBIO_MAX_NBIF);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_bases[] = {
		[0] = 0x10100000,
		[1] = 0x10200000,
		[2] = 0x10500000
	};

	const uint32_t aperture_base = aperture_bases[nbifno];

	const uint32_t aperture_off = (nbio32 << 21);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + reg));
}

static inline smn_reg_t
turin_nbif_alt_smn_reg(const uint8_t nbiono, const smn_reg_def_t def,
    const uint8_t nbifno, const uint16_t reginst)
{
	const uint32_t nbio32 = (const uint32_t)nbiono;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF_ALT);
	ASSERT3U(nbio32, <, TURIN_MAX_NBIO);
	ASSERT3U(nbif32, <, TURIN_NBIO_MAX_NBIF_ALT);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_base = 0x01400000;

	const uint32_t aperture_off = (nbio32 << 21) + (nbif32 << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + reg));
}

static inline smn_reg_t
turin_nbif_alt2_smn_reg(const uint8_t nbiono, const smn_reg_def_t def,
    const uint8_t nbifno, const uint16_t reginst)
{
	const uint32_t NBIF_ALT2_SMN_REG_MASK = 0xfff;
	const uint32_t nbio32 = (const uint32_t)nbiono;
	const uint32_t nbif32 = (const uint32_t)nbifno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_NBIF_ALT2);
	ASSERT3U(nbio32, <, TURIN_MAX_NBIO);
	ASSERT3U(nbif32, <, TURIN_NBIO_MAX_NBIF_ALT);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & ~NBIF_ALT2_SMN_REG_MASK);

	const uint32_t aperture_base = 0x1013a000;

	const uint32_t aperture_off = (nbio32 << 21) + (nbif32 << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & NBIF_ALT2_SMN_REG_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & ~NBIF_ALT2_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + reg));
}

/*
 * NBIFMM::RCC_DEVn_EPFn_STRAP0. NBIF Function strap 0. This SMN address is
 * relative to the actual function space.
 */
/*CSTYLED*/
#define	D_NBIF_FUNC_STRAP0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_FUNC,	\
	.srd_reg = 0x00	\
}
#define	NBIF_FUNC_STRAP0(i, n, d, f)	\
    turin_nbif_func_smn_reg(i, D_NBIF_FUNC_STRAP0, n, d, f)
#define	NBIF_FUNC_STRAP0_SET_SUP_D2(r, v)	bitset32(r, 31, 31, v)
#define	NBIF_FUNC_STRAP0_SET_SUP_D1(r, v)	bitset32(r, 30, 30, v)
#define	NBIF_FUNC_STRAP0_SET_BE_PCIE(r, v)	bitset32(r, 29, 29, v)
#define	NBIF_FUNC_STRAP0_SET_EXIST(r, v)	bitset32(r, 28, 28, v)
#define	NBIF_FUNC_STRAP0_SET_MIN_REV(r, v)	bitset32(r, 23, 20, v)
#define	NBIF_FUNC_STRAP0_SET_MAJ_REV(r, v)	bitset32(r, 19, 16, v)
#define	NBIF_FUNC_STRAP0_SET_DEV_ID(r, v)	bitset32(r, 15, 0, v)

/* NBIFMM::RCC_DEVn_EPFn_STRAP1 is reserved */

/*
 * NBIFMM::RCC_DEVn_EPFn_STRAP2. NBIF Function strap 2. This SMN address is
 * relative to the actual function space.
 */
/*CSTYLED*/
#define	D_NBIF_FUNC_STRAP2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_FUNC,	\
	.srd_reg = 0x08	\
}
#define	NBIF_FUNC_STRAP2(i, n, d, f)	\
    turin_nbif_func_smn_reg(i, D_NBIF_FUNC_STRAP2, n, d, f)
#define	NBIF_FUNC_STRAP2_SET_ACS_EN(r, v)	bitset32(r, 17, 17, v)
#define	NBIF_FUNC_STRAP2_SET_AER_EN(r, v)	bitset32(r, 16, 16, v)

/*
 * NBIFMM::RCC_DEVn_EPFn_STRAP3. NBIF Function strap 3. This SMN address is
 * relative to the actual function space.
 */
/*CSTYLED*/
#define	D_NBIF_FUNC_STRAP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_FUNC,	\
	.srd_reg = 0x0c	\
}
#define	NBIF_FUNC_STRAP3(i, n, d, f)	\
    turin_nbif_func_smn_reg(i, D_NBIF_FUNC_STRAP3, n, d, f)
#define	NBIF_FUNC_STRAP3_SET_PM_STATUS_EN(r, v)	bitset32(r, 30, 30, v)
#define	NBIF_FUNC_STRAP3_SET_PANF_EN(r, v)	bitset32(r, 16, 16, v)

/*
 * NBIFMM::RCC_DEVn_EPFn_STRAP4. NBIF Function strap 4. This SMN address is
 * relative to the actual function space.
 */
/*CSTYLED*/
#define	D_NBIF_FUNC_STRAP4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_FUNC,	\
	.srd_reg = 0x10	\
}
#define	NBIF_FUNC_STRAP4(i, n, d, f)	\
    turin_nbif_func_smn_reg(i, D_NBIF_FUNC_STRAP4, n, d, f)
#define	NBIF_FUNC_STRAP4_SET_FLR_EN(r, v)	bitset32(r, 22, 22, v)

/*
 * NBIFMM::RCC_DEVn_EPFn_STRAP7. NBIF Function strap 7. This SMN address is
 * relative to the actual function space. Note that this strap does not exist
 * for function 0.
 */
/*CSTYLED*/
#define	D_NBIF_FUNC_STRAP7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_FUNC,	\
	.srd_reg = 0x1c	\
}
#define	NBIF_FUNC_STRAP7(i, n, d, f)	\
    turin_nbif_func_smn_reg(i, D_NBIF_FUNC_STRAP7, n, d, f)
#define	NBIF_FUNC_STRAP7_SET_TPH_EN(r, v)	bitset32(r, 22, 22, v)
#define	NBIF_FUNC_STRAP7_SET_TPH_CPLR_EN(r, v)	bitset32(r, 21, 20, v)

/*
 * NBIFMM::INTR_LINE_ENABLE. This register is arranged with one byte per
 * device. Each bit corresponds to an endpoint function.
 */
/*CSTYLED*/
#define	D_NBIF_INTR_LINE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a008	\
}
#define	NBIF_INTR_LINE_EN(i, n)	turin_nbif_smn_reg(i, D_NBIF_INTR_LINE_EN, n)

/*
 * NBIFMM::BIFC_MISC_CTRL0. As the name suggests, miscellaneous per-NBIF
 * control bits.
 */
/*CSTYLED*/
#define	D_NBIF_BIFC_MISC_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a010	\
}
#define	NBIF_BIFC_MISC_CTL0(i, n)	\
    turin_nbif_smn_reg(i, D_NBIF_BIFC_MISC_CTL0, n, 0)
#define	NBIF_BIFC_MISC_CTL0_SET_PME_TURNOFF(r, v)	bitset32(r, 28, 28, v)
#define	NBIF_BIFC_MISC_CTL0_PME_TURNOFF_BYPASS		0
#define	NBIF_BIFC_MISC_CTL0_PME_TURNOFF_FW		1

/*
 * NBIFMM::NBIF_PG_MISC_CTRL. nBIF PG misc control.
 */
/*CSTYLED*/
#define	D_NBIF_PG_MISC_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a0e8	\
}
#define	NBIF_PG_MISC_CTL0(i, n)	\
    turin_nbif_smn_reg(i, D_NBIF_PG_MISC_CTL0, n, 0)
#define	NBIF_PG_MISC_CTL0_SET_LDMASK(r, v)		bitset32(r, 30, 30, v)

/*
 * NBIFMM::BIFC_GMI_SDP_REQ_POOLCRED_ALLOC. nBIF pool credit allocation for GMI
 * Req.
 */
/*CSTYLED*/
#define	D_NBIF_BIFC_GMI_SDP_REQ_PCRED	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a308	\
}
#define	NBIF_BIFC_GMI_SDP_REQ_PCRED(i, n)	\
    turin_nbif_smn_reg(i, D_NBIF_BIFC_GMI_SDP_REQ_PCRED, n, 0)
#define	NBIF_BIFC_GMI_SDP_REQ_PCRED_SET_VC5(r, v)	bitset32(r, 23, 20, v)
#define	NBIF_BIFC_GMI_SDP_REQ_PCRED_SET_VC4(r, v)	bitset32(r, 19, 16, v)

/*
 * NBIFMM::BIFC_GMI_SDP_DAT_POOLCRED_ALLOC. nBIF pool credit allocation for GMI
 * OrigData.
 */
/*CSTYLED*/
#define	D_NBIF_BIFC_GMI_SDP_DAT_PCRED	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a30c	\
}
#define	NBIF_BIFC_GMI_SDP_DAT_PCRED(i, n)	\
    turin_nbif_smn_reg(i, D_NBIF_BIFC_GMI_SDP_DAT_PCRED, n, 0)
#define	NBIF_BIFC_GMI_SDP_DAT_PCRED_SET_VC5(r, v)	bitset32(r, 23, 20, v)
#define	NBIF_BIFC_GMI_SDP_DAT_PCRED_SET_VC4(r, v)	bitset32(r, 19, 16, v)

/*
 * NBIFMM::BIF_GMI_WRR_WEIGHT[3:2]. These two registers are used for some
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
    turin_nbif_smn_reg(i, D_NBIF_GMI_WRR_WEIGHT2, n, 0)
#define	NBIF_GMI_WRR_WEIGHT3(i, n)	\
    turin_nbif_smn_reg(i, D_NBIF_GMI_WRR_WEIGHT3, n, 0)
#define	NBIF_GMI_WRR_WEIGHTn_VAL	0x04040404

/*
 * NBIFMM::NBIF_MGCG_CTRL_LCLK
 */
/*CSTYLED*/
#define	D_NBIF_MGCG_CTL_LCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a21c	\
}
#define	NBIF_MGCG_CTL_LCLK_SET_EN(r, v)			bitset32(r, 0, 0, v)

/*
 * NBIFMM::NBIF_DS_CTRL_LCLK
 */
/*CSTYLED*/
#define	D_NBIF_DS_CTL_LCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3a220	\
}
#define	NBIF_DS_CTL_LCLK_SET_EN(r, v)			bitset32(r, 0, 0, v)

/*
 * NBIFMM::RCC_DEVn_PORT_STRAP3. Straps for the NBIF port. These are relative
 * to the main NBIF base aperture.
 */
/*CSTYLED*/
#define	D_NBIF_PORT_STRAP3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3100c,	\
	.srd_nents = TURIN_NBIF_MAX_PORTS,	\
	.srd_stride = 0x200	\
}
#define	NBIF_PORT_STRAP3(i, n, d)	\
    turin_nbif_smn_reg(i, D_NBIF_PORT_STRAP3, n, d)
#define	NBIF_PORT_STRAP3_SET_COMP_TO(r, v)	bitset32(r, 7, 7, v)

/*
 * NBIFMM::RCC_DEVn_PORT_STRAP6. Straps for the NBIF port. These are relative
 * to the main NBIF base aperture.
 */
/*CSTYLED*/
#define	D_NBIF_PORT_STRAP6	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x31018,	\
	.srd_nents = TURIN_NBIF_MAX_PORTS,	\
	.srd_stride = 0x200	\
}
#define	NBIF_PORT_STRAP6(i, n, d)	\
    turin_nbif_smn_reg(i, D_NBIF_PORT_STRAP6, n, d)
#define	NBIF_PORT_STRAP6_SET_TPH_CPLR_EN(r, v)	bitset32(r, 17, 16, v)
#define	NBIF_PORT_STRAP6_TPH_CPLR_UNSUP		0
#define	NBIF_PORT_STRAP6_TPH_CPLR_SUP		1
#define	NBIF_PORT_STRAP6_TPH_CPLR_ESUP		3

/*
 * NBIFMM::RCC_DEVn_PORT_STRAP7. Straps for the NBIF port. These are relative
 * to the main NBIF base aperture.
 */
/*CSTYLED*/
#define	D_NBIF_PORT_STRAP7	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF,	\
	.srd_reg = 0x3101c,	\
	.srd_nents = TURIN_NBIF_MAX_PORTS,	\
	.srd_stride = 0x200	\
}
#define	NBIF_PORT_STRAP7(i, n, d)	\
    turin_nbif_smn_reg(i, D_NBIF_PORT_STRAP7, n, d)
#define	NBIF_PORT_STRAP7_SET_FUNC(r, v)		bitset32(r, 31, 29, v)
#define	NBIF_PORT_STRAP7_SET_DEV(r, v)		bitset32(r, 28, 24, v)
#define	NBIF_PORT_STRAP7_SET_BUS(r, v)		bitset32(r, 23, 16, v)
#define	NBIF_PORT_STRAP7_SET_PORT(r, v)		bitset32(r, 7, 0, v)

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
 * SYSHUBMM::SYSHUB_DS_CTRL_SOCCLK - SOCCLK DeepSleep control register.
 */
/*CSTYLED*/
#define	D_NBIF_ALT_DS_CTL_SOCCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x10000	\
}
#define	NBIF_ALT_DS_CTL_SOCCLK_SET_EN(r, v)		bitset32(r, 31, 31, v)

/*
 * SYSHUBMM::SYSHUB_BGEN_ENHANCEMENT_BYPASS_EN_SOCCLK. Yes, really. This
 * register is a weird SYSHUB and NBIF crossover that is in the alternate
 * space.
 */
/*CSTYLED*/
#define	D_NBIF_ALT_BGEN_BYP_SOC	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x10008	\
}
#define	NBIF_ALT_BGEN_BYP_SOC(i, n)	\
    turin_nbif_alt_smn_reg(i, D_NBIF_ALT_BGEN_BYP_SOC, n, 0)
#define	NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW2(r, v)	bitset32(r, 18, 18, v)
#define	NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW1(r, v)	bitset32(r, 17, 17, v)
#define	NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW0(r, v)	bitset32(r, 16, 16, v)
#define	NBIF_ALT_BGEN_BYP_SOC_SET_HST_SW0(r, v)	bitset32(r, 0, 0, v)

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
 * SYSHUBMM::SYSHUB_DS_CTRL_SHUBCLK - SHUBCLK DeepSleep control register.
 */
/*CSTYLED*/
#define	D_NBIF_ALT_DS_CTL_SHUBCLK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x11000	\
}
#define	NBIF_ALT_DS_CTL_SHUBCLK_SET_EN(r, v)		bitset32(r, 31, 31, v)

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
 * SYSHUBMM::GDC_HST_SION_CNTL_REG0
 */
/*CSTYLED*/
#define	D_NBIF_ALT_GDC_HST_SION_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x1e8f0	\
}

#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL9(r, v)		bitset32(r, 19, 19, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL8(r, v)		bitset32(r, 18, 18, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL7(r, v)		bitset32(r, 17, 17, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL6(r, v)		bitset32(r, 16, 16, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL5(r, v)		bitset32(r, 15, 15, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL4(r, v)		bitset32(r, 14, 14, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL3(r, v)		bitset32(r, 13, 13, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL2(r, v)		bitset32(r, 12, 12, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL1(r, v)		bitset32(r, 11, 11, v)
#define	NBIF_ALT_GDC_HST_SION_CTL1_SOCKL0(r, v)		bitset32(r, 10, 10, v)

#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL9(r, v)		bitset32(r, 9, 9, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL8(r, v)		bitset32(r, 8, 8, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL7(r, v)		bitset32(r, 7, 7, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL6(r, v)		bitset32(r, 6, 6, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL5(r, v)		bitset32(r, 5, 5, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL4(r, v)		bitset32(r, 4, 4, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL3(r, v)		bitset32(r, 3, 3, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL2(r, v)		bitset32(r, 2, 2, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL1(r, v)		bitset32(r, 1, 1, v)
#define	NBIF_ALT_GDC_HST_SION_CTL0_SOCKL0(r, v)		bitset32(r, 0, 0, v)

/*
 * SYSHUBMM::GDC_DMA_SION_CNTL_REG0
 */
/*CSTYLED*/
#define	D_NBIF_ALT_GDC_DMA_SION_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT,	\
	.srd_reg = 0x1e190	\
}

#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL9(r, v)		bitset32(r, 19, 19, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL8(r, v)		bitset32(r, 18, 18, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL7(r, v)		bitset32(r, 17, 17, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL6(r, v)		bitset32(r, 16, 16, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL5(r, v)		bitset32(r, 15, 15, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL4(r, v)		bitset32(r, 14, 14, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL3(r, v)		bitset32(r, 13, 13, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL2(r, v)		bitset32(r, 12, 12, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL1(r, v)		bitset32(r, 11, 11, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL0(r, v)		bitset32(r, 10, 10, v)

#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL9(r, v)		bitset32(r, 9, 9, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL8(r, v)		bitset32(r, 8, 8, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL7(r, v)		bitset32(r, 7, 7, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL6(r, v)		bitset32(r, 6, 6, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL5(r, v)		bitset32(r, 5, 5, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL4(r, v)		bitset32(r, 4, 4, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL3(r, v)		bitset32(r, 3, 3, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL2(r, v)		bitset32(r, 2, 2, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL1(r, v)		bitset32(r, 1, 1, v)
#define	NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL0(r, v)		bitset32(r, 0, 0, v)

/*
 * SYSHUBMM::NBIF_HST_SION_CNTL_REG0
 */
/*CSTYLED*/
#define	D_NBIF_HST_SION_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_NBIF_ALT2,	\
	.srd_reg = 0x8f0	\
}

#define	NBIF_HST_SION_CTL1_SOCKL9(r, v)			bitset32(r, 19, 19, v)
#define	NBIF_HST_SION_CTL1_SOCKL8(r, v)			bitset32(r, 18, 18, v)
#define	NBIF_HST_SION_CTL1_SOCKL7(r, v)			bitset32(r, 17, 17, v)
#define	NBIF_HST_SION_CTL1_SOCKL6(r, v)			bitset32(r, 16, 16, v)
#define	NBIF_HST_SION_CTL1_SOCKL5(r, v)			bitset32(r, 15, 15, v)
#define	NBIF_HST_SION_CTL1_SOCKL4(r, v)			bitset32(r, 14, 14, v)
#define	NBIF_HST_SION_CTL1_SOCKL3(r, v)			bitset32(r, 13, 13, v)
#define	NBIF_HST_SION_CTL1_SOCKL2(r, v)			bitset32(r, 12, 12, v)
#define	NBIF_HST_SION_CTL1_SOCKL1(r, v)			bitset32(r, 11, 11, v)
#define	NBIF_HST_SION_CTL1_SOCKL0(r, v)			bitset32(r, 10, 10, v)

#define	NBIF_HST_SION_CTL0_SOCKL9(r, v)			bitset32(r, 9, 9, v)
#define	NBIF_HST_SION_CTL0_SOCKL8(r, v)			bitset32(r, 8, 8, v)
#define	NBIF_HST_SION_CTL0_SOCKL7(r, v)			bitset32(r, 7, 7, v)
#define	NBIF_HST_SION_CTL0_SOCKL6(r, v)			bitset32(r, 6, 6, v)
#define	NBIF_HST_SION_CTL0_SOCKL5(r, v)			bitset32(r, 5, 5, v)
#define	NBIF_HST_SION_CTL0_SOCKL4(r, v)			bitset32(r, 4, 4, v)
#define	NBIF_HST_SION_CTL0_SOCKL3(r, v)			bitset32(r, 3, 3, v)
#define	NBIF_HST_SION_CTL0_SOCKL2(r, v)			bitset32(r, 2, 2, v)
#define	NBIF_HST_SION_CTL0_SOCKL1(r, v)			bitset32(r, 1, 1, v)
#define	NBIF_HST_SION_CTL0_SOCKL0(r, v)			bitset32(r, 0, 0, v)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_NBIF_H */
