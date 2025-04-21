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
 * Copyright 2025 Oxide Computer Co.
 */

#ifndef _SYS_IO_FCH_I3C_H
#define	_SYS_IO_FCH_I3C_H

/*
 * There are two effectively contiguous I3C register blocks for each peripheral,
 * separated by a region of reserved/unused address space.  For sake of
 * simplicity, we ignore the hole and simply treat each peripheral as a single
 * functional unit.  Like I2C, we model each peripheral as a functional
 * sub-unit.  Only Songshan and Kunlun, among supported FCHs, have I3C.
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

#define	FCH_MAX_I3C		4

#define	FCH_I3C_SMN_BASE	0x2de2000
#define	FCH_I3C_PHYS_BASE	0xfedd2000
#define	FCH_I3C_SIZE		0x1000

#ifndef	_ASM

static inline uint32_t
fch_i3c_smn_aperture(const uint8_t unit)
{
	const uint32_t base = FCH_I3C_SMN_BASE;

	ASSERT3U(unit, <, FCH_MAX_I3C);

	const uint32_t unit32 = (uint32_t)unit;

	return (base + unit32 * FCH_I3C_SIZE);
}

static inline smn_reg_t
fch_i3c_smn_reg(const uint8_t unit, const smn_reg_def_t def)
{
	const uint32_t aperture = fch_i3c_smn_aperture(unit);
	const uint32_t REG_MASK = 0xfffU;
	ASSERT0(aperture & REG_MASK);

	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_FCH_I3C);
	ASSERT0(def.srd_reg & ~REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg, SMN_UNIT_FCH_I3C));
}

/*
 * Non-relocatable MMIO addressing for I3Cs.  Note that the last peripheral is
 * at a different location from the obvious.  Only the primary FCH's peripherals
 * can be accessed this way.
 */
static inline paddr_t
fch_i3c_mmio_aperture(const uint8_t unit)
{
	const paddr_t base = FCH_I3C_PHYS_BASE;

	ASSERT3U(unit, <, FCH_MAX_I3C);

	if (unit == 3) {
		return (base + 0x4000UL);
	} else {
		return (base + (const paddr_t)unit * FCH_I3C_SIZE);
	}
}

static inline mmio_reg_block_t
fch_i3c_mmio_block(const uint8_t unit)
{
	const mmio_reg_block_phys_t phys = {
		.mrbp_base = fch_i3c_mmio_aperture(unit),
		.mrbp_len = FCH_I3C_SIZE
	};

	return (mmio_reg_block_map(SMN_UNIT_FCH_I3C, phys));
}

/*
 * Compile-time constant version of fch_i3c_mmio_aperture().  Normal code should
 * not use this, only where required for a const initialiser.  const_fn sure
 * would be nice!
 */
#define	FCH_I3C_MMIO_APERTURE(u)	\
	((u == 3) ? FCH_I3C_PHYS_BASE + 0x4000 :	\
	FCH_I3C_PHYS_BASE + u * FCH_I3C_SIZE)

#endif	/* _ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_I3C_H */
