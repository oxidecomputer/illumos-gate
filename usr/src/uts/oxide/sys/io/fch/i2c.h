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

#ifndef _SYS_IO_FCH_I2C_H
#define	_SYS_IO_FCH_I2C_H

/*
 * FCH::I2C contains a collection of DesignWare I2C peripherals.  Each of
 * Taishan, Huashan, Songshan, and Kunlun have 6 of these, each of which we
 * model as a functional sub-unit.
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/cmn_err.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Huashan, Songshan, and Kunlun all have 6 I2C peripherals.  They are found at
 * the same MMIO locations. The first 2 peripherals are also found at the same
 * SMN location across all 3 but only Songshan and Kunlun further provide SMN
 * access at the same locations for the last 4 peripherals.  They also share a
 * common register set, with minor differences:
 *  - Songshan and Kunlun include 3 additional registers (xAC, xB0, xB4).
 *  - Songshan is possibly missing 1 register (xFC) or at least undocumented.
 * Taishan and Huashan are the same in all I2C respects.  All I2C registers are
 * 32 bits wide.
 */

#define	FCH_MAX_I2C		6
#define	TAISHAN_MAX_SMN_I2C	2
#define	HUASHAN_MAX_SMN_I2C	TAISHAN_MAX_SMN_I2C

#define	FCH_I2C_SMN_BASE	0x2dc2000
#define	FCH_I2C_PHYS_BASE	0xfedc2000
#define	FCH_I2C_SIZE		0x1000

#ifndef	_ASM

static inline uint32_t
__common_i2c_smn_aperture(const uint8_t unit, const uint8_t count)
{
	const uint32_t base = FCH_I2C_SMN_BASE;

	ASSERT3U(unit, <, count);

	const uint32_t unit32 = (uint32_t)unit;

	switch (unit) {
	case 0:
	case 1:
		return (base + unit32 * FCH_I2C_SIZE);
	case 2:
	case 3:
	case 4:
		return (base + unit32 * FCH_I2C_SIZE + 0x10000U);
	case 5:
		return (base + 0x19000U);
	default:
		panic("unreachable code: invalid I2C unit %u", unit32);
	}
}

static inline smn_reg_t
__common_i2c_smn_reg(const uint8_t unit, const smn_reg_def_t def,
    const uint8_t count)
{
	const uint32_t aperture = __common_i2c_smn_aperture(unit, count);
	const uint32_t REG_MASK = 0xfffU;
	ASSERT0(aperture & REG_MASK);

	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_FCH_I2C);
	ASSERT0(def.srd_reg & ~REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

/*
 * For consumers like fch(4d) that need the address rather than register
 * descriptors.
 */
static inline uint32_t
huashan_i2c_smn_aperture(const uint8_t unit)
{
	return (__common_i2c_smn_aperture(unit, HUASHAN_MAX_SMN_I2C));
}

static inline uint32_t
songshan_i2c_smn_aperture(const uint8_t unit)
{
	return (__common_i2c_smn_aperture(unit, FCH_MAX_I2C));
}

static inline uint32_t
kunlun_i2c_smn_aperture(const uint8_t unit)
{
	return (__common_i2c_smn_aperture(unit, FCH_MAX_I2C));
}

static inline smn_reg_t
huashan_i2c_smn_reg(const uint8_t unit, const smn_reg_def_t def)
{
	return (__common_i2c_smn_reg(unit, def, HUASHAN_MAX_SMN_I2C));
}

static inline smn_reg_t
songshan_i2c_smn_reg(const uint8_t unit, const smn_reg_def_t def)
{
	return (__common_i2c_smn_reg(unit, def, FCH_MAX_I2C));
}

static inline smn_reg_t
kunlun_i2c_smn_reg(const uint8_t unit, const smn_reg_def_t def)
{
	return (__common_i2c_smn_reg(unit, def, FCH_MAX_I2C));
}

/*
 * Unlike in SMN space, all the FCHs have the same number of MMIO-addressable
 * I2C peripherals, and they're (so far!) always in the same place.  These are
 * not relocatable, so only the primary FCH's peripherals can be accessed this
 * way.
 */
static inline paddr_t
fch_i2c_mmio_aperture(const uint8_t unit)
{
	const paddr_t base = FCH_I2C_PHYS_BASE;

	ASSERT3U(unit, <, FCH_MAX_I2C);

	if (unit == 5) {
		return (base + 0x9000UL);
	} else {
		return (base + (const paddr_t)unit * FCH_I2C_SIZE);
	}
}

static inline mmio_reg_block_t
fch_i2c_mmio_block(const uint8_t unit)
{
	const mmio_reg_block_phys_t phys = {
		.mrbp_base = fch_i2c_mmio_aperture(unit),
		.mrbp_len = FCH_I2C_SIZE
	};

	return (mmio_reg_block_map(SMN_UNIT_FCH_I2C, phys));
}

MAKE_MMIO_FCH_REG_FN(I2C, i2c, 4);

/*
 * Compile-time constant version of fch_i2c_mmio_aperture().  Normal code should
 * not use this, only where required for a const initialiser.  const_fn sure
 * would be nice!
 */
#define	FCH_I2C_MMIO_APERTURE(u)	\
	((u == 5) ? FCH_I2C_PHYS_BASE + 0x9000 :	\
	FCH_I2C_PHYS_BASE + u * FCH_I2C_SIZE)

/*
 * FCH::I2C::IC_CON.  Main configuration register for each I2C peripheral.
 */
/*CSTYLED*/
#define	D_FCH_I2C_IC_CON	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_I2C,	\
	.srd_reg = 0x00			\
}
#define	FCH_I2C_IC_CON_MMIO(b)	\
    fch_i2c_mmio_reg((b), D_FCH_I2C_IC_CON, 0)

#define	FCH_I2C_IC_CON_GET_HOLD_ON_RX_FULL(r)	bitx32(r, 9, 9)
#define	FCH_I2C_IC_CON_SET_HOLD_ON_RX_FULL(r, v)	bitset32(r, 9, 9, v)
#define	FCH_I2C_IC_CON_GET_TXE_INTR_EN(r)	bitx32(r, 8, 8)
#define	FCH_I2C_IC_CON_SET_TXE_INTR_EN(r, v)	bitset32(r, 8, 8, v)
#define	FCH_I2C_IC_CON_GET_SD_INTR_ADDRONLY(r)	bitx32(r, 7, 7)
#define	FCH_I2C_IC_CON_SET_SD_INTR_ADDRONLY(r, v)	bitset32(r, 7, 7, v)
#define	FCH_I2C_IC_CON_GET_SLAVE_DIS(r)		bitx32(r, 6, 6)
#define	FCH_I2C_IC_CON_SET_SLAVE_DIS(r, v)	bitset32(r, 6, 6, v)
#define	FCH_I2C_IC_CON_GET_RESTART_EN(r)	bitx32(r, 5, 5)
#define	FCH_I2C_IC_CON_SET_RESTART_EN(r, v)	bitset32(r, 5, 5, v)

#define	FCH_I2C_IC_CON_GET_MA_ADDRWIDTH(r)	bitx32(r, 4, 4)
#define	FCH_I2C_IC_CON_SET_MA_ADDRWIDTH(r, v)	bitset32(r, 4, 4, v)
#define	FCH_I2C_IC_CON_GET_SL_ADDRWIDTH(r)	bitx32(r, 3, 3)
#define	FCH_I2C_IC_CON_SET_SL_ADDRWIDTH(r, v)	bitset32(r, 3, 3, v)
#define	FCH_I2C_IC_CON_ADDRWIDTH_7	0
#define	FCH_I2C_IC_CON_ADDRWIDTH_10	1

#define	FCH_I2C_IC_CON_GET_SPEED(r)		bitx32(r, 2, 1)
#define	FCH_I2C_IC_CON_SET_SPEED(r, v)		bitset32(r, 2, 1, v)
#define	FCH_I2C_IC_CON_SPEED_STD	1
#define	FCH_I2C_IC_CON_SPEED_FAST	2
#define	FCH_I2C_IC_CON_SPEED_HIGH	3

#define	FCH_I2C_IC_CON_GET_MASTER_EN(r)		bitx32(r, 0, 0)
#define	FCH_I2C_IC_CON_SET_MASTER_EN(r, v)	bitset32(r, 0, 0, v)

#endif	/* _ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_I2C_H */
