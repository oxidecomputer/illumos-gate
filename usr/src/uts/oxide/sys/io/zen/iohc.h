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

#ifndef _SYS_IO_ZEN_IOHC_H
#define	_SYS_IO_ZEN_IOHC_H

/*
 * Macros for constructing IOHC SMN register definitions which are common
 * across platforms.
 */

#include <sys/debug.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All individual register addresses within the IOHCDEV blocks must fit within
 * the bottom 10 bits. There are three groups of IOHCDEV blocks, one each for
 * PCIe bridges, NBIFs, and the southbridge (FCH). Each group contains one or
 * more blocks of registers, each of which in turn contains an instance of each
 * register per bridge.
 *
 * The following macro can be used to generate functions for building
 * these registers with appropriate input and bounds checks.
 */

#define	ZEN_MAKE_SMN_IOHCDEV_REG_FN(_platform, _unit, _unitlc, _base, _apmask, \
    _nunits, _unitshift, _unitmult)	\
static inline smn_reg_t							\
_platform ## _iohcdev_ ## _unitlc ## _smn_reg(const uint8_t iohcno,	\
    const smn_reg_def_t def, const uint8_t unitno, const uint8_t reginst) \
{									\
	const uint32_t SMN_IOHCDEV_REG_MASK = 0x3ff;			\
	const uint32_t iohc32 = (const uint32_t)iohcno;			\
	const uint32_t unit32 = (const uint32_t)unitno;			\
	const uint32_t reginst32 = (const uint32_t)reginst;		\
	const uint32_t stride = (def.srd_stride == 0) ? 4 :		\
	    (const uint32_t)def.srd_stride;				\
	const uint32_t nents = (def.srd_nents == 0) ? 1 :		\
	    (const uint32_t) def.srd_nents;				\
									\
	ASSERT0(def.srd_size);						\
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOHCDEV_ ## _unit);		\
	ASSERT3U(iohc32, <, 4);						\
	ASSERT3U(unit32, <, _nunits);					\
	ASSERT3U(nents, >, reginst32);					\
	ASSERT0(def.srd_reg & ~SMN_IOHCDEV_REG_MASK);			\
									\
	const uint32_t aperture_base = (_base);				\
	const uint32_t aperture_off = (iohc32 << 20) +			\
	    ((unit32 * _unitmult) << _unitshift);			\
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);		\
									\
	const uint32_t aperture = aperture_base + aperture_off;		\
	ASSERT0(aperture & SMN_IOHCDEV_REG_MASK);			\
									\
	const uint32_t reg = def.srd_reg + reginst32 * stride;		\
	ASSERT0(reg & (_apmask));					\
									\
	return (SMN_MAKE_REG(aperture + reg));				\
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_IOHC_H */
