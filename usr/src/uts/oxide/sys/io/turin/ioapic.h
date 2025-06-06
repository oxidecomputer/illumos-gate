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

#ifndef _SYS_IO_TURIN_IOAPIC_H
#define	_SYS_IO_TURIN_IOAPIC_H

/*
 * NB IOAPIC register definitions. While the NBIOAPICs are very similar to the
 * traditional IOAPIC interface, the latter is found in the FCH. These IOAPICs
 * are not normally programmed beyond initial setup and handle legacy
 * interrupts coming from PCIe and NBIF sources. Such interrupts, which are not
 * supported on this machine architecture, are then routed to the FCH IOAPIC.
 */

#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/ioapic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IOAPIC registers. These exist on a per-IOMS basis in SMN space. These are
 * not the traditional software IOAPIC registers that exist in the FCH. Each
 * IOAPIC block is 20 bits in size but most of the space contains no registers.
 * The standard address calculation method does not work for IOAPICs as there
 * is an aperture between the first and last four.
 */
static inline smn_reg_t
turin_ioapic_smn_reg(const uint8_t unitno, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const uint32_t unit32 = (const uint32_t)unitno;
	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t size32 = (def.srd_size == 0) ? 4 :
	    (const uint32_t)def.srd_size;
	ASSERT(size32 == 1 || size32 == 2 || size32 == 4);
	const uint32_t stride = (def.srd_stride == 0) ? size32 :
	    def.srd_stride;
	ASSERT3U(stride, >=, size32);
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOAPIC);
	ASSERT3U(unit32, <, 8);
	ASSERT3U(nents, >, reginst32);
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);

	const uint32_t aperture_base = unit32 < 4 ? 0x14300000 : 0x1d800000;

	const uint32_t aperture_off = ((unit32 % 4) << 20);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & ~SMN_APERTURE_MASK);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & SMN_APERTURE_MASK);

	return (SMN_MAKE_REG_SIZED(aperture + reg, size32, SMN_UNIT_IOAPIC));
}

/*
 * IOAPIC::FEATURES_ENABLE. This controls various features of the IOAPIC.
 */
/*CSTYLED*/
#define	D_IOAPIC_FEATURES	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAPIC,	\
	.srd_reg = 0x00	\
}
#define	IOAPIC_FEATURES(a)	\
	turin_ioapic_smn_reg(a, D_IOAPIC_FEATURES, 0)
#define	IOAPIC_FEATURES_SET_LEVEL_ONLY(r, v)	bitset32(r, 9, 9, v)
#define	IOAPIC_FEATURES_SET_PROC_MODE(r, v)	bitset32(r, 8, 8, v)
#define	IOAPIC_FEATURES_SET_SECONDARY(r, v)	bitset32(r, 5, 5, v)
#define	IOAPIC_FEATURES_SET_FCH(r, v)		bitset32(r, 4, 4, v)
#define	IOAPIC_FEATURES_SET_ID_EXT(r, v)	bitset32(r, 2, 2, v)
#define	IOAPIC_FEATURES_ID_EXT_4BIT	0
#define	IOAPIC_FEATURES_ID_EXT_8BIT	1

/*
 * IOAPIC::IOAPIC_BR_INTERRUPT_ROUTING. There are several instances of this
 * register and they determine how a given logical bridge on the IOMS maps to
 * the IOAPIC pins, hence the number of routes. The IOAPICs on the larger IOHC
 * instances have a different number of routes to those on the smaller ones.
 */
#define	IOAPIC_NROUTES_L		22
#define	IOAPIC_NROUTES_S		9
/*CSTYLED*/
#define	D_IOAPIC_ROUTE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAPIC,	\
	.srd_reg = 0x40,	\
	.srd_nents = IOAPIC_NROUTES_L	\
}
#define	IOAPIC_ROUTE(a, i)	\
	turin_ioapic_smn_reg(a, D_IOAPIC_ROUTE, i)
#define	IOAPIC_ROUTE_SET_BRIDGE_MAP(r, v)	bitset32(r, 20, 16, v)
#define	IOAPIC_ROUTE_SET_INTX_SWIZZLE(r, v)	bitset32(r, 5, 4, v)
#define	IOAPIC_ROUTE_SET_INTX_GROUP(r, v)	bitset32(r, 2, 0, v)

/*
 * IOAPIC::IOAPIC_GLUE_CG_LCLK_CTRL_0. LCLK Clock Gating Control.
 */
/*CSTYLED*/
#define	D_IOAPIC_GCG_LCLK_CTL0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOAPIC,	\
	.srd_reg = 0x100		\
}
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK0(r, v)		bitset32(r, 31, 31, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK1(r, v)		bitset32(r, 30, 30, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK2(r, v)		bitset32(r, 29, 29, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK3(r, v)		bitset32(r, 28, 28, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK4(r, v)		bitset32(r, 27, 27, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK5(r, v)		bitset32(r, 26, 26, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK6(r, v)		bitset32(r, 25, 25, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK7(r, v)		bitset32(r, 24, 24, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK8(r, v)		bitset32(r, 23, 23, v)
#define	IOAPIC_GCG_LCLK_CTL0_SET_SOCLK9(r, v)		bitset32(r, 22, 22, v)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_IOAPIC_H */
