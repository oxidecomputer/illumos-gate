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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_IO_FCH_GPIO_H
#define	_SYS_IO_FCH_GPIO_H

/*
 * FCH::GPIO provides fairly standard GPIO functionality that can be muxed onto
 * many of the processor's low-speed pads.  Some of them are "remote" and are
 * instead found in FCH::RMTGPIO; see sys/io/fch/rmtgpio.h.
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/io/mmioreg.h>
#endif	/* !_ASM */

#include <sys/io/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	FCH_GPIO_OFF		0x1500
#define	FCH_GPIO_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_GPIO_OFF)
#define	FCH_GPIO_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_GPIO_OFF)
#define	FCH_GPIO_SIZE		0x400

#ifndef	_ASM

MAKE_SMN_FCH_REG_FN(GPIO, gpio, FCH_GPIO_SMN_BASE, FCH_GPIO_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(GPIO, gpio, FCH_GPIO_OFF, FCH_GPIO_SIZE);
MAKE_MMIO_FCH_REG_FN(GPIO, gpio, 4);

/*
 * FCH::GPIO::GPIO_x.  Not all of these are exactly the same; the I2C ones are
 * different, for example.  This is the representation of the most common type.
 * We represent this as having one instance per GPIO for now, though this also
 * means it's possible to get a handle for a register that doesn't actually have
 * this format.  XXX other formats, other fields; there are also some completely
 * different registers at index 62 and after 183.
 */
/*CSTYLED*/
#define	D_FCH_GPIO_STD	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0x00,	\
	.srd_nents = 183	\
}
#define	FCH_GPIO_STD(i)	fch_gpio_smn_reg(D_FCH_GPIO_STD, i)
#define	FCH_GPIO_STD_MMIO(b, i)	\
    fch_gpio_mmio_reg(b, D_FCH_GPIO_STD, i)

#define	FCH_GPIO_STD_GET_OUTPUT_EN(r)		bitx32(r, 23, 23)
#define	FCH_GPIO_STD_SET_OUTPUT_EN(r, v)	bitset32(r, 23, 23, v)
#define	FCH_GPIO_STD_GET_OUTPUT_VAL(r)		bitx32(r, 22, 22)
#define	FCH_GPIO_STD_SET_OUTPUT_VAL(r, v)	bitset32(r, 22, 22, v)
#define	FCH_GPIO_STD_OUTPUT_VAL_DEASSERT	0
#define	FCH_GPIO_STD_OUTPUT_VAL_ASSERT		1
#define	FCH_GPIO_STD_GET_PD_EN(r)		bitx32(r, 21, 21)
#define	FCH_GPIO_STD_SET_PD_EN(r, v)		bitset32(r, 21, 21, v)
#define	FCH_GPIO_STD_GET_PU_EN(r)		bitx32(r, 20, 20)
#define	FCH_GPIO_STD_SET_PU_EN(r, v)		bitset32(r, 20, 20, v)
#define	FCH_GPIO_STD_GET_PU_STRENGTH(r)		bitx32(r, 19, 19)
#define	FCH_GPIO_STD_SET_PU_STRENGTH(r, v)	bitset32(r, 19, 19, v)
#define	FCH_GPIO_STD_PU_STRENGTH_4K		0
#define	FCH_GPIO_STD_PU_STRENGTH_8K		1
#define	FCH_GPIO_STD_GET_STRENGTH(r)		bitx32(r, 18, 17)
#define	FCH_GPIO_STD_SET_STRENGTH(r, v)		bitset32(r, 18, 17, v)
#define	FCH_GPIO_STD_STRENGTH_60OHM		1	/* 1.8 V only */
#define	FCH_GPIO_STD_STRENGTH_40OHM		2
#define	FCH_GPIO_STD_STRENGTH_80OHM		3
#define	FCH_GPIO_STD_GET_INPUT(r)		bitx32(r, 16, 16)
#define	FCH_GPIO_STD_INPUT_VAL_LOW		0
#define	FCH_GPIO_STD_INPUT_VAL_HIGH		1
#define	FCH_GPIO_STD_GET_INT_EN(r)		bitx32(r, 11, 11)
#define	FCH_GPIO_STD_SET_INT_EN(r, v)		bitset32(r, 11, 11, v)
#define	FCH_GPIO_STD_GET_LEVEL(r)		bitx32(r, 10, 9)
#define	FCH_GPIO_STD_SET_LEVEL(r, v)		bitset32(r, 10, 9, v)
#define	FCH_GPIO_STD_LEVEL_ACT_HIGH		0
#define	FCH_GPIO_STD_LEVEL_ACT_LOW		1
#define	FCH_GPIO_STD_LEVEL_ACT_BOTH		2 /* TRIG_EDGE only */
#define	FCH_GPIO_STD_GET_TRIG(r)		bitx32(r, 8, 8)
#define	FCH_GPIO_STD_SET_TRIG(r, v)		bitset32(r, 8, 8, v)
#define	FCH_GPIO_STD_TRIG_EDGE			0
#define	FCH_GPIO_STD_TRIG_LEVEL			1

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_GPIO_H */
