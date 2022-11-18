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

#ifndef _SYS_IO_FCH_MISC_H
#define	_SYS_IO_FCH_MISC_H

/*
 * FCH::MISC doesn't even pretend not to be a garbage barge.  There is also
 * MISC2, which is a separate discontiguous functional unit described by a
 * different header.  Additionally, we exclude the I2C pad control registers
 * from this functional unit because they are properly part of either the IOMUX
 * or the GPIO subsystem, and the drivers that want those shouldn't have access
 * to the rest of the contents of this block.  As a result, we have split this
 * into three virtual functional units: MISC_A, I2CPAD, and MISC_B.
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/io/fch/i2c.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	FCH_MISC_A_OFF		0x0e00
#define	FCH_MISC_A_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_MISC_A_OFF)
#define	FCH_MISC_A_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_MISC_A_OFF)
#define	FCH_MISC_A_SIZE		0xd8

#define	FCH_I2CPAD_OFF		0x0ed8
#define	FCH_I2CPAD_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_I2CPAD_OFF)
#define	FCH_I2CPAD_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_I2CPAD_OFF)
#define	FCH_I2CPAD_SIZE		0x18

#define	FCH_MISC_B_OFF		0x0ef0
#define	FCH_MISC_B_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_MISC_B_OFF)
#define	FCH_MISC_B_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_MISC_B_OFF)
#define	FCH_MISC_B_SIZE		0x10

#ifndef	_ASM

MAKE_SMN_FCH_REG_FN(MISC_A, misc_a, FCH_MISC_A_SMN_BASE, FCH_MISC_A_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(MISC_A, misc_a, FCH_MISC_A_OFF,
    FCH_MISC_A_SIZE);
MAKE_MMIO_FCH_REG_FN(MISC_A, misc_a, 4);

MAKE_SMN_FCH_REG_FN(I2CPAD, i2cpad, FCH_I2CPAD_SMN_BASE, FCH_I2CPAD_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(I2CPAD, i2cpad, FCH_I2CPAD_OFF,
    FCH_I2CPAD_SIZE);
MAKE_MMIO_FCH_REG_FN(I2CPAD, i2cpad, 4);

MAKE_SMN_FCH_REG_FN(MISC_B, misc_b, FCH_MISC_B_SMN_BASE, FCH_MISC_B_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(MISC_B, misc_b, FCH_MISC_B_OFF,
    FCH_MISC_B_SIZE);
MAKE_MMIO_FCH_REG_FN(MISC_B, misc_b, 4);

/*
 * FCH::MISC::I2Cn_PADCTRL.  Sets electrical parameters of pads that may be (but
 * are not always, depending on the IOMUX) associated with I2C functions.  These
 * pads are designed for I2C and have somewhat limited functionality as a
 * result; most significantly, they have open-drain drivers and selectable
 * voltages.
 *
 * All the I2C pad control registers are identical in a given FCH, but are quite
 * different between Huashan and Songshan, where the latter supports I3C on the
 * same pads.  The PPRs do give these as distinct registers rather than
 * instances of the same register, but we feel that's overly tedious and treat
 * them as 6 instances of the same one.
 *
 * Many of the Songshan fields have 2 bits with the same meaning, one for "pad
 * 0" and the other for "pad 1"; one bit controls the pad associated with the
 * clock signal and one with the data signal.  We aren't told which is which.
 * XXX Get the logic analyser and figure it out; for now we assume clock is 0.
 */
#define	I2CPAD_CLK	0
#define	I2CPAD_DAT	1

/*CSTYLED*/
#define	D_FCH_I2CPAD_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_I2CPAD,	\
	.srd_reg = 0x00,	\
	.srd_nents = FCH_MAX_I2C	\
}
#define	FCH_I2CPAD_CTL(i)	fch_i2cpad_smn_reg(D_FCH_I2CPAD_CTL, i)
#define	FCH_I2CPAD_CTL_MMIO(b, i)	\
    fch_i2cpad_mmio_reg((b), D_FCH_I2CPAD_CTL, (i))

/*
 * Both Huashan and Songshan have a pair of registers to control I2C-mode spike
 * suppression via what appears to be a simple low-pass RC filter.  In Huashan,
 * it's clearly documented that RCSEL chooses between a 50ns and 20ns RC
 * constant and RCEN enables or disables the filter.  The Songshan documentation
 * probably incorrectly pastes the description for RCEN into the description for
 * the field named spikercsel_1_0; we assume in the absence of contrary evidence
 * that the semantics of the RCSEL and RCEN bits are similar to those in
 * Huashan.
 */
#define	SONGSHAN_I2CPAD_CTL_GET_SPIKERCSEL(r, p)	\
    bitx32(r, 30 + p, 30 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_SPIKERCSEL(r, p, v)	\
    bitset32(r, 30 + p, 30 + p, v)
#define	HUASHAN_I2CPAD_CTL_GET_SPIKERCSEL(r)	bitx32(r, 11, 11)
#define	HUASHAN_I2CPAD_CTL_SET_SPIKERCSEL(r, v)	bitset32(r, 11, 11, v)
#define	FCH_I2CPAD_CTL_SPIKERCSEL_20NS	1
#define	FCH_I2CPAD_CTL_SPIKERCSEL_50NS	0

/*
 * On Huashan, a single field controls both the Rx trigger level and whether the
 * receiver is on at all.  On Songshan, these are controlled separately, and
 * independently for each pad.  1.1 V operation is documented as unsupported on
 * Songshan I2C[5:4]; not being able to represent that is the cost of modeling
 * these registers as instances instead of separate entities.
 */
#define	SONGSHAN_I2CPAD_CTL_GET_VOLTAGE(r, p)	\
    bitx32(r, 28 + p, 28 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_VOLTAGE(r, p, v)	\
    bitset32(r, 28 + p, 28 + p, v)
#define	SONGSHAN_I2CPAD_CTL_VOLTAGE_1_1	0
#define	SONGSHAN_I2CPAD_CTL_VOLTAGE_1_8	1

#define	SONGSHAN_I2CPAD_CTL_GET_MODE(r, p)	\
    bitx32(r, 26 + p, 26 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_MODE(r, p, v)	\
    bitset32(r, 26 + p, 26 + p, v)
#define	SONGSHAN_I2CPAD_CTL_MODE_I2C	0
#define	SONGSHAN_I2CPAD_CTL_MODE_I3C	1

#define	SONGSHAN_I2CPAD_CTL_GET_SLEW_RISE_EXTRA(r, p)	\
    bitx32(r, 24 + p, 24 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_SLEW_RISE_EXTRA(r, p, v)	\
    bitset32(r, 24 + p, 24 + p, v)

#define	SONGSHAN_I2CPAD_CTL_GET_RES_BIAS(r, p)	\
    bitx32(r, 22 + p, 22 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_RES_BIAS(r, p, v)	\
    bitset32(r, 22 + p, 22 + p, v)
#define	SONGSHAN_I2CPAD_CTL_RES_BIAS_TEMP	0
#define	SONGSHAN_I2CPAD_CTL_RES_BIAS_CONST	1

/*
 * The bias circuit in the pad needs to be enabled to support Fast Mode or Fast
 * Mode+, and can be left off (saving power) for Standard Mode.  Note that
 * turning it on doesn't by itself enable FM/FM+ in the peripheral, and in fact
 * is not sufficient to support it either as one must also set SLEW_FALL_FAST
 * (see below).  It may also be necessary to tweak other of these settings to
 * obtain acceptable electrical performance at these higher speeds; e.g., extra
 * rise/fall slew rate compensation, spike suppression, etc.  See the prose
 * descriptions of these registers in the applicable PPR.  Note that Songshan
 * also has I3C pad control registers that, under poorly understood
 * circumstances, may affect the behaviour of the same pads these registers
 * govern.
 */
#define	SONGSHAN_I2CPAD_CTL_GET_BIAS_EN(r, p)	\
    bitx32(r, 16 + p, 16 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_BIAS_EN(r, p, v)	\
    bitset32(r, 16 + p, 16 + p, v)
#define	HUASHAN_I2CPAD_CTL_GET_BIAS_EN(r)	bitx32(r, 16, 16)
#define	HUASHAN_I2CPAD_CTL_SET_BIAS_EN(r, v)	bitset32(r, 16, 16, v)

#define	FCH_I2CPAD_CTL_GET_RSEL_110(r)		bitx32(r, 15, 15)
#define	FCH_I2CPAD_CTL_SET_RSEL_110(r, v)	bitset32(r, 15, 15, v)
#define	FCH_I2CPAD_CTL_GET_RSEL_90(r)		bitx32(r, 14, 14)
#define	FCH_I2CPAD_CTL_SET_RSEL_90(r, v)	bitset32(r, 14, 14, v)
#define	FCH_I2CPAD_CTL_GET_CSEL_110(r)		bitx32(r, 13, 13)
#define	FCH_I2CPAD_CTL_SET_CSEL_110(r, v)	bitset32(r, 13, 13, v)
#define	FCH_I2CPAD_CTL_GET_CSEL_90(r)		bitx32(r, 12, 12)
#define	FCH_I2CPAD_CTL_SET_CSEL_90(r, v)	bitset32(r, 12, 12, v)

#define	SONGSHAN_I2CPAD_CTL_GET_SPIKERCEN(r, p)	\
    bitx32(r, 10 + p, 10 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_SPIKERCEN(r, p, v)	\
    bitset32(r, 10 + p, 10 + p, v)
#define	HUASHAN_I2CPAD_CTL_GET_SPIKERCEN(r)	bitx32(r, 10, 10)
#define	HUASHAN_I2CPAD_CTL_SET_SPIKERCEN(r, v)	bitset32(r, 10, 10, 1)

/*
 * This field, like others, affects both pads on Huashan, but it occupies 2
 * bits.  The valid values are the same for both FCHs however.
 */
#define	SONGSHAN_I2CPAD_CTL_GET_SLEW_FALL(r, p)	\
    bitx32(r, 8 + p, 8 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_SLEW_FALL(r, p, v)	\
    bitset32(r, 8 + p, 8 + p, v)
#define	HUASHAN_I2CPAD_CTL_GET_SLEW_FALL(r)	bitx32(r, 8, 7)
#define	HUASHAN_I2CPAD_CTL_SET_SLEW_FALL(r, v)	bitset32(r, 8, 7, v)
#define	FCH_I2CPAD_CTL_SLEW_FALL_STD	0
#define	FCH_I2CPAD_CTL_SLEW_FALL_FAST	1

#define	SONGSHAN_I2CPAD_CTL_GET_SLEW_FALL_EXTRA(r, p)	\
    bitx32(r, 6 + p, 6 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_SLEW_FALL_EXTRA(r, p, v)	\
    bitset32(r, 6 + p, 6 + p, v)
#define	HUASHAN_I2CPAD_CTL_GET_SLEW_FALL_EXTRA(r)	bitx32(r, 9, 9)
#define	HUASHAN_I2CPAD_CTL_SET_SLEW_FALL_EXTRA(r, v)	bitset32(r, 9, 9, v)

#define	SONGSHAN_I2CPAD_CTL_GET_RX_EN(r, p)	\
    bitx32(r, 4 + p, 4 + p)
#define	SONGSHAN_I2CPAD_CTL_SET_RX_EN(r, p, v)	\
    bitset32(r, 4 + p, 4 + p, v)
#define	SONGSHAN_I2CPAD_CTL_RX_DIS	0
#define	SONGSHAN_I2CPAD_CTL_RX_EN	1
#define	SONGSHAN_I2CPAD_CTL_RX_1_X	SONGSHAN_I2CPAD_CTL_RX_EN

#define	HUASHAN_I2CPAD_CTL_GET_RX(r)	bitx32(r, 5, 4)
#define	HUASHAN_I2CPAD_CTL_SET_RX(r, v)	bitset32(r, 5, 4, v)
#define	HUASHAN_I2CPAD_CTL_RX_DIS	0
#define	HUASHAN_I2CPAD_CTL_RX_3_3	1
#define	HUASHAN_I2CPAD_CTL_RX_3_3_ALSO	2
#define	HUASHAN_I2CPAD_CTL_RX_1_8	3

/*
 * It appears that this field has similar semantics on Huashan and Songshan,
 * though the latter's is slightly better documented: each pad has 2 bits, and
 * those bits select the signal strength or pullup strength for that pad.  We
 * are just guessing here that the values in each sub-field are the same as
 * those in the standard GPIO pullup selector registers; it's undocumented.
 * It's further complicated by the fact that Songshan's GPIOs are mostly 1.8 V
 * with some 1.1 but the documentation has been pasted from Huashan where they
 * are mostly 3.3 V with some 1.8. XXX There is a lot of guesswork here that
 * needs to be verified concerning the semantics of these bits before we risk
 * any hardware!
 */
#define	FCH_I2CPAD_CTL_GET_STRENGTH(r, p)	bitx32(r, 2 * p + 1, 2 * p)
#define	FCH_I2CPAD_CTL_SET_STRENGTH(r, p, v)	bitset32(r, 2 * p + 1, 2 * p, v)
#define	FCH_I2CPAD_CTL_STRENGTH_60OHM	1
#define	FCH_I2CPAD_CTL_STRENGTH_40OHM	2
#define	FCH_I2CPAD_CTL_STRENGTH_80OHM	3

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_MISC_H */
