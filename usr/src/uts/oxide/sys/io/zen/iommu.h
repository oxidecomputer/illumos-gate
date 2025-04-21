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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _SYS_IO_ZEN_IOMMU_H
#define	_SYS_IO_ZEN_IOMMU_H

/*
 * Macros for constructing IOMMU SMN register definitions which are common
 * across platforms.
 */

#include <sys/debug.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	ZEN_MAKE_SMN_IOMMUL1_REG_FN(_platform, _unit, _unitlc, _base,	\
    _nunits, _unitshift, _niommu)					\
static inline smn_reg_t							\
_platform ## _iommul1_ ## _unitlc ## _smn_reg(const uint8_t iommuno,	\
    const smn_reg_def_t def, const uint8_t unitno)			\
{									\
	const uint32_t iommu32 = (const uint32_t)iommuno;		\
	const uint32_t unit32 = (const uint32_t)unitno;			\
									\
	ASSERT0(def.srd_size);						\
	ASSERT0(def.srd_nents);						\
	ASSERT0(def.srd_stride);					\
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_IOMMUL1);			\
	ASSERT3U(iommu32, <, _niommu);					\
	ASSERT3U(unit32, <, _nunits);					\
	ASSERT0(def.srd_reg & SMN_APERTURE_MASK);			\
									\
	const uint32_t aperture_base = (_base);				\
									\
	const uint32_t aperture_off = (iommu32 << 20) +			\
	    (unit32 << _unitshift);					\
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);		\
									\
	const uint32_t aperture = aperture_base + aperture_off;		\
	ASSERT0(aperture & ~SMN_APERTURE_MASK);				\
									\
	return (SMN_MAKE_REG(aperture + def.srd_reg, SMN_UNIT_IOMMUL1));\
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_IOMMU_H */
