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

#ifndef _SYS_IO_TURIN_IOHC_H
#define	_SYS_IO_TURIN_IOHC_H

/*
 * Addresses and register definitions for the I/O hub core (IOHC) found in
 * Turin processors. The IOHC is part of the NBIO block, which comes from the
 * legacy "north bridge" designation, and connects the internal HT-based fabric
 * with PCIe, the FCH, and other I/O devices and fabrics. Turin has are eight
 * IOHC instances (4 per NBIO), each of which is connected to the DF via I/O
 * master (IOM) and I/O slave (IOS) components, has its own independent set of
 * registers, and connects its own collection of downstream resources (root
 * ports, NBIFs, etc.) to the DF. There are several sub-blocks in the IOHC
 * including the IOAGR and SDP mux, and their registers are defined here.
 * Registers in connected components such as PCIe root ports, NBIFs, IOAPICs,
 * IOMMUs, and the FCH are defined elsewhere.
 */

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/iohc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	TURIN_SDPMUX_COUNT	2
#define	TURIN_NBIO_COUNT	2
#define	TURIN_NBIO_SST_COUNT	2

/*
 * This is a variant of ZEN_MAKE_SMN_REG_FN() for Turin that handles the
 * registers that have an aperture gap between the larger and smaller IOHC
 * units.
 */
#define	TURIN_MAKE_SMN_REG_FN(_fn, _unit, _base1, _base2, _mask, _unitshift) \
CTASSERT(((_base1) & ~(_mask)) == 0);					\
CTASSERT(((_base2) & ~(_mask)) == 0);					\
static inline smn_reg_t							\
_fn(const uint8_t unitno, const smn_reg_def_t def, const uint16_t reginst) \
{									\
	const uint32_t unit32 = (const uint32_t)unitno;			\
	const uint32_t reginst32 = (const uint32_t)reginst;		\
	const uint32_t size32 = (def.srd_size == 0) ? 4 :		\
	    (const uint32_t)def.srd_size;				\
	ASSERT(size32 == 1 || size32 == 2 || size32 == 4);		\
	const uint32_t stride = (def.srd_stride == 0) ? size32 :	\
	    def.srd_stride;						\
	ASSERT3U(stride, >=, size32);					\
	const uint32_t nents = (def.srd_nents == 0) ? 1 :		\
	    (const uint32_t)def.srd_nents;				\
									\
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_ ## _unit);			\
	ASSERT3U(unit32, <, 8);						\
	ASSERT3U(nents, >, reginst32);					\
	ASSERT0(def.srd_reg & (_mask));					\
									\
	const uint32_t aperture_base = unit32 < 4 ? (_base1) : (_base2); \
									\
	const uint32_t aperture_off = ((unit32 % 4) << (_unitshift));	\
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);		\
									\
	const uint32_t aperture = aperture_base + aperture_off;		\
	ASSERT0(aperture & ~(_mask));					\
									\
	const uint32_t reg = def.srd_reg + reginst32 * stride;		\
	ASSERT0(reg & (_mask));						\
									\
	return (SMN_MAKE_REG_SIZED(aperture + reg, size32,		\
	    def.srd_unit));						\
}

TURIN_MAKE_SMN_REG_FN(turin_iohc_smn_reg, IOHC, 0x13b00000, 0x1d400000,
    SMN_APERTURE_MASK, 20);

TURIN_MAKE_SMN_REG_FN(turin_ioagr_smn_reg, IOAGR, 0x15b00000, 0x1e000000,
    SMN_APERTURE_MASK, 20);

/*
 * The SDPMUX SMN addresses are a bit weird. Unlike IOHC and IOAGR units, there
 * are only 2 SDPMUX units (one per IOHUB0 in each NBIO). The aperture number
 * of the first SDPMUX is found where we would expect; however, after that we
 * not only skip the next aperture but also add (1 << 23) to the base address to
 * get the second SDPMUX instance. It's unclear why this is so. All registers
 * are 32 bits wide; we check for violations.
 */
static inline smn_reg_t
turin_sdpmux_smn_reg(const uint8_t sdpmuxno, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const uint32_t sdpmux32 = (const uint32_t)sdpmuxno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t) def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_SDPMUX);
	ASSERT3U(sdpmux32, <, TURIN_SDPMUX_COUNT);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_base = 0x04400000;

	const uint32_t aperture_off = (sdpmux32 == 0) ? 0 :
	    (1 << 23) + ((sdpmux32 + 1) << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + reg, SMN_UNIT_SDPMUX));
}

/*
 * The SST SMN addresses are a bit weird. Each NBIO has an SST1 and then
 * NBIO0 also has a second instance, SST0. The addresses are as follows:
 *
 *	NBIO		SST		Address
 *	0		0		1740_0000
 *	0		1		1750_0000
 *	1		1		1770_0000
 *
 * There is no SST instance 0 on NBIO1.
 */
static inline smn_reg_t
turin_sst_smn_reg(const uint8_t nbiono, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const uint32_t nbio32 = (const uint32_t)nbiono;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t) def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_SST);
	ASSERT3U(nbio32, <, TURIN_NBIO_COUNT);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	/* There is no instance 0 on NBIO1 */
	ASSERT(reginst32 == 1 || nbio32 == 0);

	const uint32_t aperture_base = 0x17400000;
	const uint32_t aperture_off = (nbio32 << 21) + (reginst << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG(aperture + reg, SMN_UNIT_SST));
}

static inline smn_reg_t
turin_iohcdev_sb_smn_reg(const uint8_t iohcno, const smn_reg_def_t def,
    const uint8_t unitno, const uint8_t reginst)
{
	const uint32_t SMN_IOHCDEV_REG_MASK = 0x3ff;
	const uint32_t iohc32 = (const uint32_t)iohcno;
	const uint32_t unit32 = (const uint32_t)unitno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t) def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOHCDEV_SB);
	ASSERT3U(iohc32, <, 8);
	ASSERT3U(unit32, <, 1);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & ~SMN_IOHCDEV_REG_MASK);

	const uint32_t aperture_base = iohc32 < 4 ? 0x13b3c000 : 0x1d43c000;
	const uint32_t aperture_off = ((iohc32 % 4) << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & SMN_IOHCDEV_REG_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & 0xffffc000);

	return (SMN_MAKE_REG(aperture + reg, SMN_UNIT_IOHCDEV_SB));
}

/*
 * The IOHC::IOHC_Bridge_CNTL register contains blocks for several other
 * devices including three PCIe cores. The first such PCIe core contains 9
 * registers, the second contains 8 and the third contains 3. Since we need to
 * account for the varying widths the common generator macro cannot be used.
 * When calling the following function, the desired PCIe core is specified as
 * the unit number and the port as the register instance.
 */
static inline smn_reg_t
turin_iohcdev_pcie_smn_reg(const uint8_t iohcno, const smn_reg_def_t def,
    const uint8_t unitno, const uint8_t reginst)
{
	const uint32_t SMN_IOHCDEV_REG_MASK = 0x3ff;
	const uint32_t iohc32 = (const uint32_t)iohcno;
	const uint32_t unit32 = (const uint32_t)unitno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t) def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOHCDEV_PCIE);
	ASSERT3U(iohc32, <, 8);
	ASSERT3U(unit32, <, 3);
	/* There is only a single PCIe unit on the smaller IOHC types */
	ASSERT(iohc32 < 4 || unit32 < 1);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & ~SMN_IOHCDEV_REG_MASK);

	const uint32_t aperture_base = iohc32 < 4 ? 0x13b31000 : 0x1d431000;
	const uint32_t aperture_offsets[] = {
		[0] = 0,
		[1] = 9,
		[2] = 17,
	};

	uint32_t aperture_off = ((iohc32 % 4) << 20) +
	    (aperture_offsets[unit32] << 10);

	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & SMN_IOHCDEV_REG_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	const uint32_t apmask = 0xffff8000;
	ASSERT0(reg & apmask);

	return (SMN_MAKE_REG(aperture + reg, SMN_UNIT_IOHCDEV_PCIE));
}

/*
 * This is the base for unit 0, which as indicated above is the only
 * unit in the bridge control register, and therefore the only one that
 * we accept.  We believe this pertains to nBIF0 ports 0 and 1, but note
 * that the register is named IOHC0NBIF1DEVINDCFG[1:0]; NBIF1 in that
 * name is a misnomer.
 */
static inline smn_reg_t
turin_iohcdev_nbif_smn_reg(const uint8_t iohcno, const smn_reg_def_t def,
    const uint8_t unitno, const uint8_t reginst)
{
	const uint32_t SMN_IOHCDEV_REG_MASK = 0x3ff;
	const uint32_t iohc32 = (const uint32_t)iohcno;
	const uint32_t unit32 = (const uint32_t)unitno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 :
	    (const uint32_t)def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t) def.srd_nents;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOHCDEV_NBIF);
	/* Not present on smaller IOHC types. */
	VERIFY3U(iohc32, <, 4);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & ~SMN_IOHCDEV_REG_MASK);

	/*
	 * This is the base for unit 0, which as indicated above is the only
	 * unit in the bridge control register, and therefore the only one that
	 * we accept.
	 */
	const uint32_t aperture_base = 0x13b38000;
	VERIFY3U(unit32, ==, 0);

	const uint32_t aperture_off = (iohc32 << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & SMN_IOHCDEV_REG_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	const uint32_t apmask = 0xffffc000;
	ASSERT0(reg & apmask);

	return (SMN_MAKE_REG(aperture + reg, SMN_UNIT_IOHCDEV_NBIF));
}

/*
 * IOHC Registers of Interest. The SMN based addresses are all relative to the
 * IOHC base address.
 */

/*
 * IOHC::NB_ADAPTER_ID_W. This allows us to override the default subsystem
 * vendor and device ID for the IOHC's PCI device. By default, this is
 * 1022,153A which is the Turin pDID and can be left as-is. This is in config
 * space, not SMN!
 */
#define	IOHC_NB_ADAPTER_ID_W	0x50
#define	IOHC_NB_ADAPTER_ID_W_GET_SDID(r)	bitx32(r, 31, 16)
#define	IOHC_NB_ADAPTER_ID_W_SET_SDID(r, v)	bitset32(r, 31, 16, v)
#define	IOHC_NB_ADAPTER_ID_W_GET_SVID(r)	bitx32(r, 15, 0)
#define	IOHC_NB_ADAPTER_ID_W_SET_SVID(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::NB_PCI_ARB. Most of this register is occupied by PME functionality
 * that we don't use; however, for no obvious reason it also contains the
 * VGA_HOLE bit that controls how accesses to the legacy VGA address range at
 * memory [0xA_0000, 0xC_0000) from downstream devices are handled. NOTE: This
 * register is in PCI space, not SMN!
 */
#define	IOHC_NB_PCI_ARB	0x84
#define	IOHC_NB_PCI_ARB_GET_VGA_HOLE(r)		bitx32(r, 3, 3)
#define	IOHC_NB_PCI_ARB_SET_VGA_HOLE(r, v)	bitset32(r, 3, 3, v)
#define	IOHC_NB_PCI_ARB_VGA_HOLE_RAM	0
#define	IOHC_NB_PCI_ARB_VGA_HOLE_MMIO	1

/*
 * IOHC::NB_TOP_OF_DRAM_SLOT1. This indicates where the top of DRAM below (or
 * at) 4 GiB is. Note, bit 32 for getting to 4 GiB is actually in bit 0.
 * Otherwise it's all bits 31:23. NOTE: This register is in PCI space, not SMN!
 */
#define	IOHC_TOM	0x90
#define	IOHC_TOM_SET_TOM(r, v)		bitset32(r, 31, 23, v)
#define	IOHC_TOM_SET_BIT32(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::DEBUG0. Not documented in the PPR.
 */
/*CSTYLED*/
#define	D_IOHC_DBG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10004	\
}
/*
 * This bit forces the request stream ID for local peer-to-peer memory requests
 * to use the BDF of the root port (i.e., primary bus number, device 0,
 * function 0) instead of the actual BDF of the requesting device.
 */
#define	IOHC_DBG0_SET_ROOT_STRMID(r, v)	bitset32(r, 21, 21, v)

/*
 * IOHC::IOHC_REFCLK_MODE. Seemingly controls the speed of the reference clock
 * that is presumably used by PCIe.
 */
/*CSTYLED*/
#define	D_IOHC_REFCLK_MODE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10020	\
}
#define	IOHC_REFCLK_MODE_SET_27MHZ(r, v)	bitset32(r, 2, 2, v)
#define	IOHC_REFCLK_MODE_SET_25MHZ(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_REFCLK_MODE_SET_100MHZ(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::NBIO_LCLK_DS_MASK. Seemingly controls masking of the LCLK deep sleep.
 */
/*CSTYLED*/
#define	D_IOHC_NBIO_LCLK_DS_MASK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10024	\
}
#define	IOHC_NBIO_LCLK_DS_MASK_SET(r, v)	bitset32(r, 31, 0, v)

/*
 * IOHC::IOHC_PCIE_CRS_Count. Controls configuration space retries. The limit
 * indicates the length of time that retries can be issued for. Apparently in
 * 1.6ms units. The delay is the amount of time that is used between retries,
 * which are in units of 1.6us.
 */
/*CSTYLED*/
#define	D_IOHC_PCIE_CRS_COUNT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10028	\
}
#define	IOHC_PCIE_CRS_COUNT_SET_LIMIT(r, v)	bitset32(r, 27, 16, v)
#define	IOHC_PCIE_CRS_COUNT_SET_DELAY(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::NB_BUS_NUM_CNTL
 */
/*CSTYLED*/
#define	D_IOHC_BUS_NUM_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10044	\
}
#define	IOHC_BUS_NUM_CTL_SET_SEGMENT(r, v)	bitset32(r, 23, 16, v)
#define	IOHC_BUS_NUM_CTL_SET_EN(r, v)		bitset32(r, 8, 8, v)
#define	IOHC_BUS_NUM_CTL_SET_BUS(r, v)		bitset32(r, 7, 0, v)

/*
 * IOHC::NB_LOWER_TOP_OF_DRAM2. Indicates to the NB where DRAM above 4 GiB goes
 * up to. Note, that due to the wholes where there are system reserved ranges of
 * memory near 1 TiB, this may be split into two values.
 */
/*CSTYLED*/
#define	D_IOHC_DRAM_TOM2_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10064	\
}
#define	IOHC_DRAM_TOM2_LOW_SET_TOM2(r, v)	bitset32(r, 31, 23, v)
#define	IOHC_DRAM_TOM2_LOW_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * IOHC::NB_UPPER_TOP_OF_DRAM2.
 */
/*CSTYLED*/
#define	D_IOHC_DRAM_TOM2_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10068	\
}
#define	IOHC_DRAM_TOM2_HI_SET_TOM2(r, v)	bitset32(r, 8, 0, v)

/*
 * IOHC::NB_LOWER_DRAM2_BASE. This indicates the starting address of DRAM at 4
 * GiB. This register resets to all zeros indicating that it starts at 4 GiB,
 * hence why it is not set. This contains the lower 32 bits (of which 31:23) are
 * valid.
 */
/*CSTYLED*/
#define	D_IOHC_DRAM_BASE2_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1006c	\
}
#define	IOHC_DRAM_BASE2_LOW_SET_BASE(x)		bitset32(r, 31, 23, v)

/*
 * IOHC::NB_UPPER_DRAM2_BASE. This indicates the starting address of DRAM at 4
 * GiB. This register resets to 001h indicating that it starts at 4 GiB, hence
 * why it is not set. This contains the upper 8 bits (47:32) of the starting
 * address.
 */
/*CSTYLED*/
#define	D_IOHC_DRAM_BASE2_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10070	\
}
#define	IOHC_DRAM_BASE2_HI_SET_BASE(r, v)	bitset32(r, 8, 0, v)

/*
 * IOHC::SB_LOCATION. Indicates where the FCH aka the old south bridge is
 * located.
 */
/*CSTYLED*/
#define	D_IOHC_SB_LOCATION	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1007c	\
}
#define	IOHC_SB_LOCATION_SET_CORE(r, v)		bitset32(r, 31, 16, v)
#define	IOHC_SB_LOCATION_SET_PORT(r, v)		bitset32(r, 15, 0, v)

/*
 * IOHC::IOHC_GLUE_CG_LCLK_CTRL_0. IOHC clock gating control.
 */
/*CSTYLED*/
#define	D_IOHC_GCG_LCLK_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10088	\
}

/*
 * These setters are common across the IOHC::IOHC_GLUE_CG_LCLK_CTRL_ registers.
 */
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK0(r, v)	bitset32(r, 31, 31, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK1(r, v)	bitset32(r, 30, 30, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK2(r, v)	bitset32(r, 29, 29, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK3(r, v)	bitset32(r, 28, 28, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK4(r, v)	bitset32(r, 27, 27, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK5(r, v)	bitset32(r, 26, 26, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK6(r, v)	bitset32(r, 25, 25, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK7(r, v)	bitset32(r, 24, 24, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK8(r, v)	bitset32(r, 23, 23, v)
#define	IOHC_GCG_LCLK_CTL_SET_SOCLK9(r, v)	bitset32(r, 22, 22, v)

/*
 * IOHC::IOHC_GLUE_CG_LCLK_CTRL_1. IOHC clock gating control.
 */
/*CSTYLED*/
#define	D_IOHC_GCG_LCLK_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1008c	\
}

/*
 * IOHC::IOHC_GLUE_CG_LCLK_CTRL_2. IOHC clock gating control.
 */
/*CSTYLED*/
#define	D_IOHC_GCG_LCLK_CTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10090	\
}

/*
 * IOHC::IOHC_FEATURE_CNTL. As it says on the tin, controls some various feature
 * bits here.
 */
/*CSTYLED*/
#define	D_IOHC_FCTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10118	\
}
#define	IOHC_FCTL_GET_DGPU(r)		bitx32(r, 28, 28)
#define	IOHC_FCTL_DGPU_CPU		0
#define	IOHC_FCTL_DGPU_DGPU		1
#define	IOHC_FCTL_GET_TRAP_DGPU(r)	bitx32(r, 27, 27)
#define	IOHC_FCTL_TRAP_DGPU_CPU		0
#define	IOHC_FCTL_TRAP_DGPU_DGPU	1
#define	IOHC_FCTL_GET_RAS_DGPU(r)	bitx32(r, 26, 26)
#define	IOHC_FCTL_RAS_DGPU_CPU		0
#define	IOHC_FCTL_RAS_DGPU_DGPU		1
#define	IOHC_FCTL_SET_ARI(r, v)		bitset32(r, 22, 22, v)
#define	IOHC_FCTL_SET_P2P(r, v)		bitset32(r, 2, 1, v)
#define	IOHC_FCTL_P2P_DROP_NMATCH	0
#define	IOHC_FCTL_P2P_FWD_NMATCH	1
#define	IOHC_FCTL_P2P_FWD_ALL		2
#define	IOHC_FCTL_P2P_DISABLE		3
#define	IOHC_FCTL_GET_HP_DEVID_EN(x)	bitx32(r, 0, 0)

/*
 * IOHC::IOHC_INTERRUPT_EOI. Used to indicate that an SCI, NMI, or SMI
 * originating from this (or possibly any) IOHC has been serviced. All fields
 * in this register are write-only and can only meaningfully be set, not
 * cleared.
 */
/*CSTYLED*/
#define	D_IOHC_INTR_EOI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10120	\
}
#define	IOHC_INTR_EOI_SET_NMI(r)	bitset32(r, 2, 2, 1)
#define	IOHC_INTR_EOI_SET_SCI(r)	bitset32(r, 1, 1, 1)
#define	IOHC_INTR_EOI_SET_SMI(r)	bitset32(r, 0, 0, 1)

/*
 * IOHC::IOHC_PIN_CNTL. This register has only a single field, which defines
 * whether external assertion of the NMI_SYNCFLOOD_L pin causes an NMI or a SYNC
 * FLOOD. This register is defined only for the IOHC which shares an IOMS with
 * the FCH.
 */
/*CSTYLED*/
#define	D_IOHC_PIN_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10128	\
}
#define	IOHC_PIN_CTL_GET_MODE(r)		bitx32(r, 0, 0)
#define	IOHC_PIN_CTL_SET_MODE_SYNCFLOOD(r)	bitset32(r, 0, 0, 0)
#define	IOHC_PIN_CTL_SET_MODE_NMI(r)		bitset32(r, 0, 0, 1)

/*
 * IOHC::IOHC_INTR_CNTL. Used to indicate where NMIs should be directed.
 * Amazingly, if this is set to the default (0xff), NMIs sent *before* an
 * AP is up appear to be latched -- and then delivered to the AP upon being
 * powered up! (If it needs to be said: this results in an undebuggable
 * failure of the AP.)
 */
/*CSTYLED*/
#define	D_IOHC_INTR_CTL (const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1012c	\
}
#define	IOHC_INTR_CTL_SET_NMI_DEST_CTRL(r, v)	bitset32(r, 15, 8, v)

/*
 * IOHC::IOHC_FEATURE_CNTL2. Status register that indicates whether certain
 * error events have occurred, including NMI drops, CRS retries, SErrs, and NMI
 * generation. All fields are RW1c except for SErr which is RO.
 */
/*CSTYLED*/
#define	D_IOHC_FCTL2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10130	\
}
#define	IOHC_FCTL2_GET_NP_DMA_DROP(r)	bitx32(r, 18, 18)
#define	IOHC_FCTL2_SET_NP_DMA_DROP(r)	bitset32(r, 18, 18, 1)
#define	IOHC_FCTL2_GET_P_DMA_DROP(r)	bitx32(r, 17, 17)
#define	IOHC_FCTL2_SET_P_DMA_DROP(r)	bitset32(r, 17, 17, 1)
#define	IOHC_FCTL2_GET_CRS(r)		bitx32(r, 16, 16)
#define	IOHC_FCTL2_SET_CRS(r)		bitset32(r, 16, 16, 1)
#define	IOHC_FCTL2_GET_SERR(r)		bitx32(r, 1, 1)
#define	IOHC_FCTL2_GET_NMI(r)		bitx32(r, 0, 0)
#define	IOHC_FCTL2_SET_NMI(r)		bitset32(r, 0, 0, 1)

/*
 * IOHC::NB_TOP_OF_DRAM3. This is another use of defining memory. It starts at
 * bit 40 of PA. This register is a bit different from the others in that it is
 * an inclusive register. The register containts bits 51:22, mapped to the
 * register's 29:0.
 */
/*CSTYLED*/
#define	D_IOHC_DRAM_TOM3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10138	\
}
#define	IOHC_DRAM_TOM3_SET_EN(r, v)	bitset32(r, 31, 31, v)
#define	IOHC_DRAM_TOM3_SET_LIMIT(r, v)	bitset32(r, 29, 0, v)

/*
 * IOHC::PSP_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * PSP.
 */
/*CSTYLED*/
#define	D_IOHC_PSP_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102e0	\
}
#define	IOHC_PSP_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	IOHC_PSP_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 8, 8, v)
#define	IOHC_PSP_ADDR_LO_SET_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::PSP_BASE_ADDR_HI. This contains the upper bits of the PSP base
 * address.
 */
/*CSTYLED*/
#define	D_IOHC_PSP_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102e4	\
}
#define	IOHC_PSP_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::SMU_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * SMU.
 */
/*CSTYLED*/
#define	D_IOHC_SMU_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102e8	\
}
#define	IOHC_SMU_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	IOHC_SMU_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_SMU_ADDR_LO_SET_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::SMU_BASE_ADDR_HI. This contains the upper bits of the SMU base
 * address.
 */
/*CSTYLED*/
#define	D_IOHC_SMU_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102ec	\
}
#define	IOHC_SMU_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::IOAPIC_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * IOAPIC.
 */
/*CSTYLED*/
#define	D_IOHC_IOAPIC_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102f0	\
}
#define	IOHC_IOAPIC_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 8, v)
#define	IOHC_IOAPIC_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_IOAPIC_ADDR_LO_SET_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::IOAPIC_BASE_ADDR_HI. This contains the upper bits of the IOAPIC base
 * address.
 */
/*CSTYLED*/
#define	D_IOHC_IOAPIC_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102f4	\
}
#define	IOHC_IOAPIC_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::DBG_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * DBG registers. What this debugs, is unfortunately unclear.
 */
/*CSTYLED*/
#define	D_IOHC_DBG_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102f8	\
}
#define	IOHC_DBG_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	IOHC_DBG_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_DBG_ADDR_LO_SET_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::DBG_BASE_ADDR_HI. This contains the upper bits of the DBG base
 * address.
 */
/*CSTYLED*/
#define	D_IOHC_DBG_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x102fc	\
}
#define	IOHC_DBG_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::FASTREG_BASE_ADDR_LO. This contains the MMIO address that is used by
 * the 'FastRegs' which provides access to an SMN aperture.
 */
/*CSTYLED*/
#define	D_IOHC_FASTREG_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10300	\
}
#define	IOHC_FASTREG_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	IOHC_FASTREG_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_FASTREG_ADDR_LO_SET_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::FASTREG_BASE_ADDR_HI. This contains the upper bits of the fast register
 * access aperture base address.
 */
/*CSTYLED*/
#define	D_IOHC_FASTREG_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10304	\
}
#define	IOHC_FASTREG_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::FASTREGCNTL_BASE_ADDR_LO. This contains the MMIO address that is used
 * by the fast register access control page.
 */
/*CSTYLED*/
#define	D_IOHC_FASTREGCTL_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10308	\
}
#define	IOHC_FASTREGCTL_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 12, v)
#define	IOHC_FASTREGCTL_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_FASTREGCTL_ADDR_LO_SET_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::FASTREGCNTL_BASE_ADDR_HI. This contains the upper bits of the
 * fast register access control page.
 */
/*CSTYLED*/
#define	D_IOHC_FASTREGCTL_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1030c	\
}
#define	IOHC_FASTREGCTL_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::MPIO_BASE_ADDR_LO. This contains the MMIO address that is used
 * by MPIO.
 */
/*CSTYLED*/
#define	D_IOHC_MPIO_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10310	\
}
#define	IOHC_MPIO_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	IOHC_MPIO_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 8, 8, v)
#define	IOHC_MPIO_ADDR_LO_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * IOHC::MPIO_BASE_ADDR_HI. This contains the upper bits of the MPIO page.
 */
/*CSTYLED*/
#define	D_IOHC_MPIO_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10314	\
}
#define	IOHC_MPIO_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::SMMU_BASE_ADDR_LO. This contains the MMIO address that is used
 * by the SMMU.
 */
/*CSTYLED*/
#define	D_IOHC_SMMU_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10318	\
}
#define	IOHC_SMMU_ADDR_LO_SET_ADDR(r, v)	bitset32(r, 31, 19, v)
#define	IOHC_SMMU_ADDR_LO_SET_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	IOHC_SMMU_ADDR_LO_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * IOHC::SMMU_BASE_ADDR_HI. This contains the upper bits of the SMMU page.
 */
/*CSTYLED*/
#define	D_IOHC_SMMU_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1031c	\
}
#define	IOHC_SMMU_ADDR_HI_SET_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::MPM_BASE_ADDR_LO. This contains the MMIO address that is used
 * by the MPM.
 */
/*CSTYLED*/
#define	D_IOHC_MPM_ADDR_LO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10338	\
}
#define	IOHC_MPM_ADDR_LO_SET_ADDR(r, v)		bitset32(r, 31, 20, v)
#define	IOHC_MPM_ADDR_LO_SET_LOCK(r, v)		bitset32(r, 1, 1, v)
#define	IOHC_MPM_ADDR_LO_SET_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * IOHC::MPM_BASE_ADDR_HI. This contains the upper bits of the MPM page.
 */
/*CSTYLED*/
#define	D_IOHC_MPM_ADDR_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1033c	\
}
#define	IOHC_MPM_ADDR_HI_SET_ADDR(r, v)		bitset32(r, 15, 0, v)

/*
 * IOHC::IOHC_SDP_PORT_CONTROL. This is used to control how the port disconnect
 * behavior operates for the connection to the data fabric.
 */
/*CSTYLED*/
#define	D_IOHC_SDP_PORT_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x10344	\
}
#define	IOHC_SDP_PORT_CTL_SET_IOS_RT_HYSTERESIS(r, v)	bitset32(r, 27, 20, v)
#define	IOHC_SDP_PORT_CTL_SET_IOM_RT_HYSTERESIS(r, v)	bitset32(r, 19, 12, v)
#define	IOHC_SDP_PORT_CTL_SET_PORT_HYSTERESIS(r, v)	bitset32(r, 11, 0, v)

/*
 * IOHC::IOHC_QOS_CONTROL. This controls the data fabric DMA priority.
 */
/*CSTYLED*/
#define	D_IOHC_QOS_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14040	\
}
#define	IOHC_QOS_CTL(h)	\
	genoa_iohc_smn_reg(h, D_IOHC_QOS_CTL, 0)
#define	IOHC_QOS_CTL_SET_VC7_PRI(r, v)		bitset32(r, 31, 28, v)
#define	IOHC_QOS_CTL_SET_VC6_PRI(r, v)		bitset32(r, 27, 24, v)
#define	IOHC_QOS_CTL_SET_VC5_PRI(r, v)		bitset32(r, 23, 20, v)
#define	IOHC_QOS_CTL_SET_VC4_PRI(r, v)		bitset32(r, 19, 16, v)
#define	IOHC_QOS_CTL_SET_VC3_PRI(r, v)		bitset32(r, 15, 12, v)
#define	IOHC_QOS_CTL_SET_VC2_PRI(r, v)		bitset32(r, 11, 8, v)
#define	IOHC_QOS_CTL_SET_VC1_PRI(r, v)		bitset32(r, 7, 4, v)
#define	IOHC_QOS_CTL_SET_VC0_PRI(r, v)		bitset32(r, 3, 0, v)

/*
 * IOHC::USB_QoS_CNTL. This controls the USB data fabric priority.
 */
/*CSTYLED*/
#define	D_IOHC_USB_QOS_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14044	\
}
#define	IOHC_USB_QOS_CTL_SET_UNID1_EN(r, v)	bitset32(r, 28, 28, v)
#define	IOHC_USB_QOS_CTL_SET_UNID1_PRI(r, v)	bitset32(r, 27, 24, v)
#define	IOHC_USB_QOS_CTL_SET_UNID1_ID(r, v)	bitset32(r, 22, 16, v)
#define	IOHC_USB_QOS_CTL_SET_UNID0_EN(r, v)	bitset32(r, 12, 12, v)
#define	IOHC_USB_QOS_CTL_SET_UNID0_PRI(r, v)	bitset32(r, 11, 8, v)
#define	IOHC_USB_QOS_CTL_SET_UNID0_ID(r, v)	bitset32(r, 6, 0, v)

/*
 * IOHC::IOHC_SION_S0_CLIENT_REQ_BURSTTARGET_LOWER and friends. There are a
 * bunch of these and a varying number of them. These registers all seem to
 * adjust arbitration targets, what should be preferred, and related. There are
 * a varying number of instances of this in each IOHC MISC. There are also
 * definitions for values to go in these. Not all of the registers in the PPR
 * are set. Not all instances of these are always set with values. I'm sorry, I
 * can only speculate as to why.
 */
#define	IOHC_SION_MAX_ENTS	6
#define	IOHC_SION_ENTS(h)	((h) < 4 ? 6 : 3)

/*CSTYLED*/
#define	D_IOHC_SION_S0_CLIREQ_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14400,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_CLIREQ_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14404,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_CLIREQ_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14408,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_CLIREQ_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1440c,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_RDRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14410,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_RDRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14414,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_RDRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14418,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_RDRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1441c,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_WRRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14420,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_WRRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14424,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_WRRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14428,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S0_WRRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1442c,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

#define	IOHC_SION_CLIREQ_BURST_VAL	0x04040404
#define	IOHC_SION_RDRSP_BURST_VAL	0x02020202

/*CSTYLED*/
#define	D_IOHC_SION_S1_CLIREQ_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14430,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_CLIREQ_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14434,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_CLIREQ_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14438,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_CLIREQ_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1443c,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_RDRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14440,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_RDRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14444,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_RDRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14448,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_RDRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1444c,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_WRRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14450,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_WRRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14454,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_WRRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14458,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

/*CSTYLED*/
#define	D_IOHC_SION_S1_WRRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x1445c,	\
	.srd_nents = IOHC_SION_MAX_ENTS,	\
	.srd_stride = 0x404	\
}

#define	IOAGR_SION_CLIREQ_BURST_VAL	0x04040404

/*
 * IOHC::IOHC_SION_LiveLock_WatchDog_Threshold. This is used to set an
 * arbitration threshold for the overall bus. The register offset differs
 * between the larger and smaller IOHCs.
 */
/*CSTYLED*/
#define	D_IOHC_SION_LLWD_THRESH_L	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x158b8,	\
}
/*CSTYLED*/
#define	D_IOHC_SION_LLWD_THRESH_S	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x14cac,	\
}
#define	IOHC_SION_LLWD_THRESH_SET(r, v)	bitset32(r, 7, 0, v)

/*
 * IOHC::MISC_RAS_CONTROL. Controls the effects of RAS events, including
 * interrupt generation and PCIe link disable. Also controls whether the
 * NMI_SYNCFLOOD_L pin is enabled at all. The register offset differs between
 * the larger and smaller IOHCs.
 */
/*CSTYLED*/
#define	D_IOHC_MISC_RAS_CTL_L	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x20244,	\
}
/*CSTYLED*/
#define	D_IOHC_MISC_RAS_CTL_S	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHC,	\
	.srd_reg = 0x200ec,	\
}
#define	IOHC_MISC_RAS_CTL_GET_SW_NMI_EN(r)	bitx32(r, 17, 17)
#define	IOHC_MISC_RAS_CTL_SET_SW_NMI_EN(r, v)	bitset32(r, 17, 17, v)
#define	IOHC_MISC_RAS_CTL_GET_SW_SMI_EN(r)	bitx32(r, 16, 16)
#define	IOHC_MISC_RAS_CTL_SET_SW_SMI_EN(r, v)	bitset32(r, 16, 16, v)
#define	IOHC_MISC_RAS_CTL_GET_SW_SCI_EN(r)	bitx32(r, 15, 15)
#define	IOHC_MISC_RAS_CTL_SET_SW_SCI_EN(r, v)	bitset32(r, 15, 15, v)
#define	IOHC_MISC_RAS_CTL_GET_PCIE_SMI_EN(r)	bitx32(r, 14, 14)
#define	IOHC_MISC_RAS_CTL_SET_PCIE_SMI_EN(r, v)	bitset32(r, 14, 14, v)
#define	IOHC_MISC_RAS_CTL_GET_PCIE_SCI_EN(r)	bitx32(r, 13, 13)
#define	IOHC_MISC_RAS_CTL_SET_PCIE_SCI_EN(r, v)	bitset32(r, 13, 13, v)
#define	IOHC_MISC_RAS_CTL_GET_PCIE_NMI_EN(r)	bitx32(r, 12, 12)
#define	IOHC_MISC_RAS_CTL_SET_PCIE_NMI_EN(r, v)	bitset32(r, 12, 12, v)
#define	IOHC_MISC_RAS_CTL_GET_SYNCFLOOD_DIS(r)	bitx32(r, 11, 11)
#define	IOHC_MISC_RAS_CTL_SET_SYNCFLOOD_DIS(r, v)	\
    bitset32(r, 11, 11, v)
#define	IOHC_MISC_RAS_CTL_GET_LINKDIS_DIS(r)	bitx32(r, 10, 10)
#define	IOHC_MISC_RAS_CTL_SET_LINKDIS_DIS(r, v)	bitset32(r, 10, 10, v)
#define	IOHC_MISC_RAS_CTL_GET_INTR_DIS(r)	bitx32(r, 9, 9)
#define	IOHC_MISC_RAS_CTL_SET_INTR_DIS(r, v)	bitset32(r, 9, 9, v)
#define	IOHC_MISC_RAS_CTL_GET_NMI_SYNCFLOOD_EN(r)	\
    bitx32(r, 2, 2)
#define	IOHC_MISC_RAS_CTL_SET_NMI_SYNCFLOOD_EN(r, v)	\
    bitset32(r, 2, 2, v)

/*
 * IOHC Device specific addresses. There are a region of IOHC addresses that are
 * devoted to each PCIe bridge, NBIF, and the southbridge.
 */

/*
 * IOHC::IOHC_Bridge_CNTL. This register controls several internal properties of
 * the various bridges. The address of this register is confusing because it
 * shows up in different locations with a large number of instances at different
 * bases; see TURIN_MAKE_SMN_IOHCDEV_REG_FN() and its notes above for details.
 */
/*CSTYLED*/
#define	D_IOHCDEV_PCIE_BRIDGE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHCDEV_PCIE,	\
	.srd_reg = 0x4,	\
	.srd_nents = 9,	\
	.srd_stride = 0x400	\
}
#define	IOHCDEV_PCIE_BRIDGE_CTL(h, p, i)	\
	turin_iohcdev_pcie_smn_reg(h, D_IOHCDEV_PCIE_BRIDGE_CTL, p, i)

/*CSTYLED*/
#define	D_IOHCDEV_NBIF_BRIDGE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHCDEV_NBIF,	\
	.srd_reg = 0x4,	\
	.srd_nents = 2,	\
	.srd_stride = 0x400	\
}
#define	IOHCDEV_NBIF_BRIDGE_CTL(h, n, i)	\
	turin_iohcdev_nbif_smn_reg(h, D_IOHCDEV_NBIF_BRIDGE_CTL, n, i)

/*CSTYLED*/
#define	D_IOHCDEV_SB_BRIDGE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOHCDEV_SB,	\
	.srd_reg = 0x4	\
}
#define	IOHCDEV_SB_BRIDGE_CTL(h)	\
	turin_iohcdev_sb_smn_reg(h, D_IOHCDEV_SB_BRIDGE_CTL, 0, 0)

#define	IOHCDEV_BRIDGE_CTL_GET_APIC_RANGE(r)		bitx32(r, 31, 24)
#define	IOHCDEV_BRIDGE_CTL_GET_APIC_ENABLE(r)		bitx32(r, 23, 23)
#define	IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(r, v)		bitset32(r, 18, 18, v)
#define	IOHCDEV_BRIDGE_CTL_SET_IDO_MODE(r, v)		bitset32(r, 11, 10, v)
#define	IOHCDEV_BRIDGE_CTL_IDO_MODE_NO_MOD	0
#define	IOHCDEV_BRIDGE_CTL_IDO_MODE_DIS		1
#define	IOHCDEV_BRIDGE_CTL_IDO_MODE_FORCE_ON	2
#define	IOHCDEV_BRIDGE_CTL_SET_FORCE_RSP_PASS(r, v)	bitset32(r, 9, 9, v)
#define	IOHCDEV_BRIDGE_CTL_SET_DISABLE_NO_SNOOP(r, v)	bitset32(r, 8, 8, v)
#define	IOHCDEV_BRIDGE_CTL_SET_DISABLE_RELAX_POW(r, v)	bitset32(r, 7, 7, v)
#define	IOHCDEV_BRIDGE_CTL_SET_MASK_UR(r, v)		bitset32(r, 6, 6, v)
#define	IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(r, v)	bitset32(r, 2, 2, v)
#define	IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(r, v)	bitset32(r, 1, 1, v)
#define	IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(r, v)	bitset32(r, 0, 0, v)

/*
 * IOAGR Registers. The SMN based addresses are all relative to the IOAGR base
 * address.
 */

/*
 * IOAGR::IOAGR_GLUE_CG_LCLK_CTRL_0. IOAGR clock gating control.
 */
/*CSTYLED*/
#define	D_IOAGR_GCG_LCLK_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0		\
}

/*
 * These setters are common across the IOAGR::IOAGR_GLUE_CG_LCLK_CTRL_
 * registers.
 */
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK0(r, v)	bitset32(r, 31, 31, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK1(r, v)	bitset32(r, 30, 30, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK2(r, v)	bitset32(r, 29, 29, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK3(r, v)	bitset32(r, 28, 28, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK4(r, v)	bitset32(r, 27, 27, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK5(r, v)	bitset32(r, 26, 26, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK6(r, v)	bitset32(r, 25, 25, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK7(r, v)	bitset32(r, 24, 24, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK8(r, v)	bitset32(r, 23, 23, v)
#define	IOAGR_GCG_LCLK_CTL_SET_SOCLK9(r, v)	bitset32(r, 22, 22, v)

/*
 * IOAGR::IOAGR_GLUE_CG_LCLK_CTRL_1. IOAGR clock gating control.
 */
/*CSTYLED*/
#define	D_IOAGR_GCG_LCLK_CTL1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x4		\
}
#define	IOAGR_GCG_LCLK_CTL1(h)	\
	genoa_iohc_smn_reg(h, D_IOAGR_GCG_LCLK_CTL1, 0)

/*
 * IOAGR::IOAGR_SION_S0_Client_Req_BurstTarget_Lower. While the case has
 * changed and the number of entries from our friends in the IOHC, everything
 * said above is still true.
 */
#define	IOAGR_SION_MAX_ENTS	6

/*CSTYLED*/
#define	D_IOAGR_SION_S0_CLIREQ_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00400,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_CLIREQ_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00404,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_CLIREQ_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00408,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_CLIREQ_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0040c,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_RDRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00410,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_RDRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00414,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_RDRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00418,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_RDRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0041c,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_WRRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00420,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_WRRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00424,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_WRRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00428,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S0_WRRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0042c,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_CLIREQ_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00430,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_CLIREQ_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00434,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_CLIREQ_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00438,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_CLIREQ_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0043c,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_RDRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00440,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_RDRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00444,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_RDRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00448,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_RDRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0044c,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_WRRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00450,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_WRRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00454,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_WRRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00458,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*CSTYLED*/
#define	D_IOAGR_SION_S1_WRRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x0045c,	\
	.srd_nents = IOAGR_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}

/*
 * IOAGR::IOAGR_SION_LiveLock_WatchDog_Threshold. This is used to set an
 * arbitration threshold for the IOAGR. Companion to the IOHC variant. The
 * register offset differs between the larger and smaller IOHCs.
 */
/*CSTYLED*/
#define	D_IOAGR_SION_LLWD_THRESH_L	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x018a4	\
}
/*CSTYLED*/
#define	D_IOAGR_SION_LLWD_THRESH_S	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAGR,	\
	.srd_reg = 0x00ca4	\
}
#define	IOAGR_SION_LLWD_THRESH_SET(r, v)	bitset32(r, 7, 0, v)

/*
 * SDPMUX registers of interest.
 */

/*
 * SDPMUX::SDPMUX_SDP_PORT_CONTROL. More Clock request bits in the spirit of
 * other blocks.
 */
/*CSTYLED*/
#define	D_SDPMUX_SDP_PORT_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00008	\
}
#define	SDPMUX_SDP_PORT_CTL(m)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SDP_PORT_CTL, 0)
#define	SDPMUX_SDP_PORT_CTL_SET_PORT_HYSTERESIS(r, v)	bitset32(r, 11, 0, v)

/*
 * SDPMUX::SDPMUX_HST_ORIG_EARLY_WAKE_UP_EN
 */
/*CSTYLED*/
#define	D_SDPMUX_HST_OEWAKE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00014	\
}
#define	SDPMUX_HST_OEWAKE_EN_SET_EGR(r, v)	bitset32(r, 31, 16, v)
#define	SDPMUX_HST_OEWAKE_EN_SET_INGR(r, v)	bitset32(r, 15, 0, v)

#define	SDPMUX_HST_OEWAKE_EN_EGR_VAL	0x2
#define	SDPMUX_HST_OEWAKE_EN_INGR_VAL	0x1

/*
 * SDPMUX::SDPMUX_DMA_ORIG_EARLY_WAKE_UP_EN
 */
/*CSTYLED*/
#define	D_SDPMUX_DMA_OEWAKE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00018	\
}
#define	SDPMUX_DMA_OEWAKE_EN_SET_EGR(r, v)	bitset32(r, 31, 16, v)
#define	SDPMUX_DMA_OEWAKE_EN_SET_INGR(r, v)	bitset32(r, 15, 0, v)

#define	SDPMUX_DMA_OEWAKE_EN_EGR_VAL	0x1
#define	SDPMUX_DMA_OEWAKE_EN_INGR_VAL	0x2

/*
 * SDPMUX::SDPMUX_NTB_ORIG_EARLY_WAKE_UP_EN
 */
/*CSTYLED*/
#define	D_SDPMUX_NTB_OEWAKE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0001c	\
}
#define	SDPMUX_NTB_OEWAKE_EN_SET_EGR(r, v)	bitset32(r, 31, 16, v)
#define	SDPMUX_NTB_OEWAKE_EN_SET_INGR(r, v)	bitset32(r, 15, 0, v)

#define	SDPMUX_NTB_OEWAKE_EN_EGR_VAL	0x2
#define	SDPMUX_NTB_OEWAKE_EN_INGR_VAL	0x4

/*
 * SDPMUX::SDPMUX_HST_COMP_EARLY_WAKE_UP_EN
 */
/*CSTYLED*/
#define	D_SDPMUX_HST_CEWAKE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00020	\
}
#define	SDPMUX_HST_CEWAKE_EN_SET_EGR(r, v)	bitset32(r, 31, 16, v)
#define	SDPMUX_HST_CEWAKE_EN_SET_INGR(r, v)	bitset32(r, 15, 0, v)

#define	SDPMUX_HST_CEWAKE_EN_EGR_VAL	0x1
#define	SDPMUX_HST_CEWAKE_EN_INGR_VAL	0x2

/*
 * SDPMUX::SDPMUX_DMA_COMP_EARLY_WAKE_UP_EN
 */
/*CSTYLED*/
#define	D_SDPMUX_DMA_CEWAKE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00024	\
}
#define	SDPMUX_DMA_CEWAKE_EN_SET_EGR(r, v)	bitset32(r, 31, 16, v)
#define	SDPMUX_DMA_CEWAKE_EN_SET_INGR(r, v)	bitset32(r, 15, 0, v)

#define	SDPMUX_DMA_CEWAKE_EN_EGR_VAL	0x2
#define	SDPMUX_DMA_CEWAKE_EN_INGR_VAL	0x1

/*
 * SDPMUX::SDPMUX_NTB_COMP_EARLY_WAKE_UP_EN
 */
/*CSTYLED*/
#define	D_SDPMUX_NTB_CEWAKE_EN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00028	\
}
#define	SDPMUX_NTB_CEWAKE_EN_SET_EGR(r, v)	bitset32(r, 31, 16, v)
#define	SDPMUX_NTB_CEWAKE_EN_SET_INGR(r, v)	bitset32(r, 15, 0, v)

#define	SDPMUX_NTB_CEWAKE_EN_EGR_VAL	0x0
#define	SDPMUX_NTB_CEWAKE_EN_INGR_VAL	0x0

/*
 * SDPMUX::SDPMUX_SION_LiveLock_WatchDog_Threshold. This is used to set an
 * arbitration threshold for the SDPMUX. Companion to the IOHC variant.
 */
/*CSTYLED*/
#define	D_SDPMUX_SION_LLWD_THRESH	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0xca0	\
}
#define	SDPMUX_SION_LLWD_THRESH(m)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_LLWD_THRESH, 0)
#define	SDPMUX_SION_LLWD_THRESH_SET(r, v)	bitset32(r, 7, 0, v)

/*
 * SDPMUX::SDPMUX_SION_S0_Client_Req_BurstTarget_Lower. While the case has
 * changed and the number of entries from our friends in the IOHC, everything
 * said above is still true.
 */
#define	SDPMUX_SION_MAX_ENTS	3

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_CLIREQ_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00400,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_CLIREQ_BURST_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_CLIREQ_BURST_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_CLIREQ_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00404,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_CLIREQ_BURST_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_CLIREQ_BURST_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_CLIREQ_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00408,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_CLIREQ_TIME_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_CLIREQ_TIME_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_CLIREQ_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0040c,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_CLIREQ_TIME_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_CLIREQ_TIME_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_RDRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00410,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_RDRSP_BURST_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_RDRSP_BURST_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_RDRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00414,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_RDRSP_BURST_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_RDRSP_BURST_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_RDRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00418,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_RDRSP_TIME_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_RDRSP_TIME_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_RDRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0041c,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_RDRSP_TIME_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_RDRSP_TIME_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_WRRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00420,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_WRRSP_BURST_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_WRRSP_BURST_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_WRRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00424,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_WRRSP_BURST_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_WRRSP_BURST_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_WRRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00428,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_WRRSP_TIME_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_WRRSP_TIME_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S0_WRRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0042c,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S0_WRRSP_TIME_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S0_WRRSP_TIME_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_CLIREQ_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00430,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_CLIREQ_BURST_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_CLIREQ_BURST_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_CLIREQ_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00434,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_CLIREQ_BURST_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_CLIREQ_BURST_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_CLIREQ_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00438,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_CLIREQ_TIME_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_CLIREQ_TIME_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_CLIREQ_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0043c,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_CLIREQ_TIME_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_CLIREQ_TIME_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_RDRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00440,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_RDRSP_BURST_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_RDRSP_BURST_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_RDRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00444,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_RDRSP_BURST_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_RDRSP_BURST_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_RDRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00448,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_RDRSP_TIME_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_RDRSP_TIME_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_RDRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0044c,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_RDRSP_TIME_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_RDRSP_TIME_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_WRRSP_BURST_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00450,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_WRRSP_BURST_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_WRRSP_BURST_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_WRRSP_BURST_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00454,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_WRRSP_BURST_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_WRRSP_BURST_HI, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_WRRSP_TIME_LOW	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x00458,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_WRRSP_TIME_LOW(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_WRRSP_TIME_LOW, i)

/*CSTYLED*/
#define	D_SDPMUX_SION_S1_WRRSP_TIME_HI	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SDPMUX,	\
	.srd_reg = 0x0045c,	\
	.srd_nents = SDPMUX_SION_MAX_ENTS,	\
	.srd_stride = 0x400	\
}
#define	SDPMUX_SION_S1_WRRSP_TIME_HI(m, i)	\
	turin_sdpmux_smn_reg(m, D_SDPMUX_SION_S1_WRRSP_TIME_HI, i)

#define	SDPMUX_SION_CLIREQ_BURST_VAL	0x04040404

/*
 * SST (Source Synchronous Tunnel) registers of interest.
 */

/*
 * SST::SST_CLOCK_CTRL.
 */
/*CSTYLED*/
#define	D_SST_CLOCK_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SST,	\
	.srd_reg = 0x4,			\
	.srd_nents = 2			\
}
#define	SST_CLOCK_CTL_SET_RXCLKGATE_EN(r, v)		bitset32(r, 16, 16, v)
#define	SST_CLOCK_CTL_SET_PCTRL_IDLE_TIME(r, v)		bitset32(r, 15, 8, v)
#define	SST_CLOCK_CTL_SET_TXCLKGATE_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * SST::SST_DEBUG0.
 */
/*CSTYLED*/
#define	D_SST_DBG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SST,	\
	.srd_reg = 0x18,		\
	.srd_nents = 2			\
}
#define	SST_DBG0_SET_LCLK_CTL_NBIO_DIS(r, v)		bitset32(r, 5, 5, v)

/*
 * SST::SION_WRAPPER_CFG_SSTSION_GLUE_CG_LCLK_CTRL_SOFT_OVERRIDE_CLK
 */
/*CSTYLED*/
#define	D_SST_SION_WRAP_CFG_GCG_LCLK_CTL (const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SST,	\
	.srd_reg = 0x404,		\
	.srd_nents = 2			\
}
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK9(r, v)	bitset32(r, 9, 9, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK8(r, v)	bitset32(r, 8, 8, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK7(r, v)	bitset32(r, 7, 7, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK6(r, v)	bitset32(r, 6, 6, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK5(r, v)	bitset32(r, 5, 5, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK4(r, v)	bitset32(r, 4, 4, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK3(r, v)	bitset32(r, 3, 3, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK2(r, v)	bitset32(r, 2, 2, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK1(r, v)	bitset32(r, 1, 1, v)
#define	SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK0(r, v)	bitset32(r, 0, 0, v)

/*
 * SST::CFG_SST_RdRspPoolCredit_Alloc_LO
 */
/*CSTYLED*/
#define	D_SST_RDRSPPOOLCREDIT_ALLOC_LO (const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SST,	\
	.srd_reg = 0x418,		\
	.srd_nents = 2			\
}
#define	SST_RDRSPPOOLCREDIT_ALLOC_LO_SET(r, v)	bitset32(r, 31, 0, v)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_IOHC_H */
