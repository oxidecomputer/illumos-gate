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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _SYS_AMDZEN_GPIO_H
#define	_SYS_AMDZEN_GPIO_H

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

/*
 * This header file contains definitions for interacting with GPIOs. It does not
 * define the specific mapping of GPIO indexes to pins.
 *
 * The way that GPIOs are accessed varies on the chip family. The GPIO block is
 * built into the FCH (functional controller hub) and was traditionally accessed
 * via memory-mapped I/O. However, this proved a problem the moment you got to a
 * system that has more than one FCH present as they would have ended up at the
 * same part of MMIO space. Starting with Rome, the GPIO subsystem was made
 * available over the SMN (System Management Network). This allows us to get
 * around the issue with mulitple FCHs as each one is part of a different die
 * and therefore part of a different SMN.
 *
 * Of course, things aren't this simple. What has happened here is that starting
 * with Zen 2, systems that can support more than one processor node, aka more
 * than one DF, which are the Epyc and Threadripper parts like Rome, Milan,
 * Genoa, etc., all support the ability to access the GPIOs over the SMN alias
 * (which is preferred by us). Otherwise, all accesses must be performed over
 * MMIO.
 *
 * In general the actual data layout of each GPIO register is roughly the same
 * between all of the different families today between Zen 1 - Zen 4. This leads
 * us to prefer a single, general register definition. While a few cases don't
 * use all fields, we leave that to the actual GPIO driver to distinguish.
 *
 * GPIOs are generally organized into a series of banks. Towards the end of the
 * banks are extra registers that control the underlying subsystem or provide
 * status. It's important to note though: there are many more GPIOs that exist
 * than actually are connected to pins. In addition, several of the GPIOs in the
 * controller are connected to internal sources. The space is laid out roughly
 * the same in all systems and is contiguous. All registers are four bytes wide.
 *
 *   GPIO Bank 0
 *     +-> 63 GPIOs
 *     +-> Wake and Interrupt Control
 *   GPIO Bank 1
 *     +-> 64 GPIOs (64-127)
 *   GPIO Bank 2
 *     +-> 56 GPIOs (128-183)
 *     +-> 4 Entry (16 byte) reserved area
 *     +-> Wake Status 0
 *     +-> Wake Status 1
 *     +-> Interrupt Status 0
 *     +-> Interrupt Status 1
 *   Internal Bank
 *     +-> 32 Internal PME Related Registers
 *
 * After this, some systems may have what are called "Remote GPIOs". The exact
 * internal structure that leads to this distinction is unclear. They appear to
 * exist on a mix of different systems. When they do exist, they follow the same
 * SMN vs. MMIO semantics as everything else. The remote GPIOs are organized as
 * follows:
 *
 *    Remote GPIOs:
 *     +-> 0x00 -- Remote GPIOs (256-271)
 *     +-> 0x40 -- Unusable, Reserved Remote GPIOs (272-303)
 *     +-> 0xC0 -- 16 Remote IOMUX entries (1 byte per)
 *     +-> 0xF0 -- Wake Status
 *     +-> 0xF4 -- Interrupt Status
 *     +-> 0xFC -- Wake and Interrupt Control
 *
 * In the following, we will provide a single register definition for all of
 * the GPIO bits. There will be SMN and MMIO register values for the rest of the
 * misc. data as well. To better facilitate driver development, we treat the
 * non-remote GPIOs as a single block from an SMN addressing perspective to
 * simplify the implementation, though it means that if someone uses an invalid
 * GPIO id 63 they wil not get a GPIO, but will instead get the wake and
 * interrupt control register.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FCH::GPIO registers. As described above, these exist on a per-I/O die basis.
 * We use our own construction function here because the space is 0x400 bytes
 * large, but it is not naturally aligned. Similarly, there are no units here,
 * so we ensure that we always ASSERT that and ensure that users cannot pass us
 * an invalid value by simply not having it.
 */
#define	FCH_GPIO_MMIO_BASE	0xfed81500UL
static inline smn_reg_t
amdzen_gpio_smn_reg(const smn_reg_def_t def, const uint16_t reginst)
{
	const uint32_t APERTURE_BASE = 0x02d02500;
	const uint32_t APERTURE_MASK = 0xfffffc00;

	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 : def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;


	ASSERT3S(def.srd_unit, ==, SMN_UNIT_GPIO);
	ASSERT0(def.srd_reg & APERTURE_MASK);
	ASSERT3U(nents, >, reginst32);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & APERTURE_MASK);

	return (SMN_MAKE_REG(APERTURE_BASE + reg));
}

/*
 * FCH::GPIO::GPIO_<num> -- this is the general GPIO control register for all
 * non-remote GPIOs. We treat all banks as one large group here. The bit
 * definitions are true for both SMN and MMIO accesses.
 */
/*CSTYLED*/
#define	D_FCH_GPIO_GPIO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_GPIO,	\
	.srd_reg = 0x00,	\
	.srd_nents = 184	\
}
#define	FCH_GPIO_GPIO_SMN(n)	amdzen_gpio_smn_reg(D_FCH_GPIO_GPIO, n)

#define	FCH_GPIO_GPIO_GET_WAKE_STS(r)	bitx32(r, 29, 29)
#define	FCH_GPIO_GPIO_GET_INT_STS(r)	bitx32(r, 28, 28)
#define	FCH_GPIO_GPIO_GET_SW_EN(r)	bitx32(r, 25, 25)
#define	FCH_GPIO_GPIO_GET_SW_IN(r)	bitx32(r, 24, 24)
#define	FCH_GPIO_GPIO_GET_OUT_EN(r)	bitx32(r, 23, 23)
#define	FCH_GPIO_GPIO_GET_OUTPUT(r)	bitx32(r, 22, 22)
#define	FCH_GPIO_GPIO_OUTPUT_LOW	0
#define	FCH_GPIO_GPIO_OUTPUT_HIGH	1
#define	FCH_GPIO_GPIO_GET_PD_EN(r)	bitx32(r, 21, 21)
#define	FCH_GPIO_GPIO_GET_PU_EN(r)	bitx32(r, 20, 20)
#define	FCH_GPIO_GPIO_GET_PU_STR(r)	bitx32(r, 19, 19)
#define	FCH_GPIO_GPIO_PU_4K		0
#define	FCH_GPIO_GPIO_PU_8K		1
#define	FCH_GPIO_GPIO_GET_DRVSTR_1P8(r)	bitx32(r, 18, 17)
#define	FCH_GPIO_GPIO_GET_DRVSTR_3P3(r)	bitx32(r, 17, 17)
#define	FCH_GPIO_GPIO_DRVSTR_3P3_40R	0
#define	FCH_GPIO_GPIO_DRVSTR_3P3_80R	1
#define	FCH_GPIO_GPIO_DRVSTR_1P8_60R	1
#define	FCH_GPIO_GPIO_DRVSTR_1P8_40R	2
#define	FCH_GPIO_GPIO_DRVSTR_1P8_80R	3
#define	FCH_GPIO_GPIO_GET_INPUT(r)	bitx32(r, 16, 16)
#define	FCH_GPIO_GPIO_GET_WAKE_SOI3(r)	bitx32(r, 15, 15)
#define	FCH_GPIO_GPIO_GET_WAKE_S3(r)	bitx32(r, 14, 14)
#define	FCH_GPIO_GPIO_GET_WAKE_S5(r)	bitx32(r, 13, 13)
#define	FCH_GPIO_GPIO_GET_INT_STS_EN(r)	bitx32(r, 12, 12)
#define	FCH_GPIO_GPIO_GET_INT_EN(r)	bitx32(r, 11, 11)
#define	FCH_GPIO_GPIO_GET_LEVEL(r)	bitx32(r, 10, 9)
#define	FCH_GPIO_GPIO_LEVEL_ACT_HIGH	0
#define	FCH_GPIO_GPIO_LEVEL_ACT_LOW	1
#define	FCH_GPIO_GPIO_LEVEL_ACT_BOTH	2
#define	FCH_GPIO_GPIO_GET_TRIG(r)	bitx32(r, 8, 8)
#define	FCH_GPIO_GPIO_TRIG_EDGE		0
#define	FCH_GPIO_GPIO_TRIG_LEVEL	1
#define	FCH_GPIO_GPIO_GET_DBT_HIGH(r)	bitx32(r, 7, 7)
#define	FCH_GPIO_GPIO_GET_DBT_CTL(r)	bitx32(r, 6, 5)
#define	FCH_GPIO_GPIO_DBT_NO_DB		0
#define	FCH_GPIO_GPIO_DBT_KEEP_LOW	1
#define	FCH_GPIO_GPIO_DBT_KEEP_HIGH	2
#define	FCH_GPIO_GPIO_DBT_RM_GLITCH	3
#define	FCH_GPIO_GPIO_GET_DBT_LOW(r)	bitx32(r, 4, 4)
#define	FCH_GPIO_GPIO_DBT_2RTC		0
#define	FCH_GPIO_GPIO_DBT_8RTC		1
#define	FCH_GPIO_GPIO_DBT_512RTC	2
#define	FCH_GPIO_GPIO_DBT_2048RTC	3
#define	FCH_GPIO_GPIO_GET_DBT_TMR(r)	bitx32(r, 3, 0)

#define	FCH_GPIO_GPIO_SET_WAKE_STS(r, v)	bitset32(r, 29, 29, v)
#define	FCH_GPIO_GPIO_SET_INT_STS(r, v)		bitset32(r, 28, 28, v)
#define	FCH_GPIO_GPIO_SET_SW_EN(r, v)		bitset32(r, 25, 25, v)
#define	FCH_GPIO_GPIO_SET_SW_IN(r, v)		bitset32(r, 24, 24, v)
#define	FCH_GPIO_GPIO_SET_OUT_EN(r, v)		bitset32(r, 23, 23, v)
#define	FCH_GPIO_GPIO_SET_OUTPUT(r, v)		bitset32(r, 22, 22, v)
#define	FCH_GPIO_GPIO_SET_PD_EN(r, v)		bitset32(r, 21, 21, v)
#define	FCH_GPIO_GPIO_SET_PU_EN(r, v)		bitset32(r, 20, 20, v)
#define	FCH_GPIO_GPIO_SET_PU_STR(r, v)		bitset32(r, 19, 19, v)
#define	FCH_GPIO_GPIO_SET_DRVSTR(r, v)		bitset32(r, 18, 17, v)
#define	FCH_GPIO_GPIO_SET_INPUT(r, v)		bitset32(r, 16, 16, v)
#define	FCH_GPIO_GPIO_SET_WAKE_SOI3(r, v)	bitset32(r, 15, 15, v)
#define	FCH_GPIO_GPIO_SET_WAKE_S3(r, v)		bitset32(r, 14, 14, v)
#define	FCH_GPIO_GPIO_SET_WAKE_S5(r, v)		bitset32(r, 13, 13, v)
#define	FCH_GPIO_GPIO_SET_INT_STS_EN(r, v)	bitset32(r, 12, 12, v)
#define	FCH_GPIO_GPIO_SET_INT_EN(r, v)		bitset32(r, 11, 11, v)
#define	FCH_GPIO_GPIO_SET_LEVEL(r, v)		bitset32(r, 10, 9, v)
#define	FCH_GPIO_GPIO_SET_TRIG(r, v)		bitset32(r, 8, 8, v)
#define	FCH_GPIO_GPIO_SET_DBT_HIGH(r, v)	bitset32(r, 7, 7, v)
#define	FCH_GPIO_GPIO_SET_DBT_CTL(r, v)		bitset32(r, 6, 5, v)
#define	FCH_GPIO_GPIO_SET_DBT_LOW(r, v)		bitset32(r, 4, 4, v)
#define	FCH_GPIO_GPIO_SET_DBT_TMR(r, v)		bitset32(r, 3, 0, v)

/*
 * FCH::GPIO::GPIO_WAKE_INTERRUPT_MASTER_SWITCH -- This controls a lot of the
 * general interrupt generation and mask bits.
 */
/*CSTYLED*/
#define	D_FCH_GPIO_WAKE_CTL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_GPIO,	\
	.srd_reg = 0xfc,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_WAKE_CTL_SMN(n)	\
    amdzen_gpio_smn_reg(D_FCH_GPIO_WAKE_CTL, n)
#define	FCH_GPIO_WAKE_CTL_MMIO		(0xfc + FCH_GPIO_MMIO_BASE)
#define	FCH_GPIO_WAKE_CTL_SET_WAKE_EN(r, v)	bitset32(r, 31, 31, v)
#define	FCH_GPIO_WAKE_CTL_SET_INT_EN(r, v)	bitset32(r, 30, 30, v)
#define	FCH_GPIO_WAKE_CTL_SET_EOI(r, v)		bitset32(r, 29, 29, v)

/*
 * FCH::GPIO::GPIO_WAKE_STATUS_INDEX_0 -- Indicates whether a wake event
 * occurred.
 */
/* CSTYLED */
#define	D_FCH_GPIO_WAKE_STS0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_GPIO,	\
	.srd_reg = 0x2f0,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_WAKE_STS0_SMN(n)	\
    amdzen_gpio_smn_reg(D_FCH_GPIO_WAKE_STS0, n)

/*
 * FCH::GPIO::GPIO_WAKE_STATUS_INDEX_1 -- Indicates whether a wake event
 * occurred.
 */
/* CSTYLED */
#define	D_FCH_GPIO_WAKE_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_GPIO,	\
	.srd_reg = 0x2f4,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_WAKE_STS1_SMN(n)	\
    amdzen_gpio_smn_reg(D_FCH_GPIO_WAKE_STS1, n)

/*
 * FCH::GPIO::GPIO_INTERRUPT_STATUS_INDEX_0  -- Indicates whether an interrupt
 * has occurred.
 */
/* CSTYLED */
#define	D_FCH_GPIO_INT_STS0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_GPIO,	\
	.srd_reg = 0x2f8,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_INT_STS0_SMN(n)	\
    amdzen_gpio_smn_reg(D_FCH_GPIO_INT_STS0, n)

/*
 * FCH::GPIO::GPIO_INTERRUPT_STATUS_INDEX_1 -- Indicates whether an interrupt
 * has occurred.
 */
/* CSTYLED */
#define	D_FCH_GPIO_INT_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_GPIO,	\
	.srd_reg = 0x2fc,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_INT_STS1_SMN(n)	\
    amdzen_gpio_smn_reg(D_FCH_GPIO_INT_STS1, n)

/*
 * FCH::RMTGPIO registers. A single one of these exist per I/O die.
 */
static inline smn_reg_t
amdzen_rmtgpio_smn_reg(const smn_reg_def_t def, const uint16_t reginst)
{
	const uint32_t APERTURE_BASE = 0x02d02200;
	const uint32_t APERTURE_MASK = 0xffffff00;

	const uint32_t reginst32 = (const uint32_t)reginst;
	const uint32_t stride = (def.srd_stride == 0) ? 4 : def.srd_stride;
	const uint32_t nents = (def.srd_nents == 0) ? 1 :
	    (const uint32_t)def.srd_nents;

	ASSERT3S(def.srd_unit, ==, SMN_UNIT_RMT_GPIO);
	ASSERT0(def.srd_reg & APERTURE_MASK);
	ASSERT3U(nents, >, reginst32);

	const uint32_t reg = def.srd_reg + reginst32 * stride;
	ASSERT0(reg & APERTURE_MASK);

	return (SMN_MAKE_REG(APERTURE_BASE + reg));
}

/*
 * FCH::RMTGPIO::GPIO_<num> -- this is the set of remote GPIO banks that exist
 * in the system. These use the same register definition as for the normal GPIO
 * one.
 */
/*CSTYLED*/
#define	D_FCH_RMTGPIO_GPIO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_RMT_GPIO,	\
	.srd_reg = 0x00,	\
	.srd_nents = 16		\
}
#define	FCH_RMTGPIO_GPIO_SMN(n)	amdzen_rmtgpio_smn_reg(D_FCH_RMTGPIO_GPIO, n)

/*
 * FCH::RMTGPIO::RMT_GPIO_WAKE_STATUS -- This provides wake status information
 * for the remote GPIO set.
 */
/* CSTYLED */
#define	D_FCH_RMTGPIO_WAKE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_RMT_GPIO,	\
	.srd_reg = 0xf0,	\
	.srd_nents = 1		\
}
#define	FCH_RMTGPIO_WAKE_SMN(n)	amdzen_rmtgpio_smn_reg(D_FCH_RMTGPIO_WAKE, n)

/*
 * FCH::RMTGPIO::RMT_GPIO_INTERRUPT_STATUS -- This provides interrupt status
 * information for the remote GPIO set.
 */
/* CSTYLED */
#define	D_FCH_RMTGPIO_INT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_RMT_GPIO,	\
	.srd_reg = 0xf4,	\
	.srd_nents = 1		\
}
#define	FCH_RMTGPIO_INT_SMN(n)	amdzen_rmtgpio_smn_reg(D_FCH_RMTGPIO_INT, n)

/*
 * FCH::RMTGPIO::RMT_GPIO_MASTER_SWITCH -- This controls the mask settings for
 * the remote GPIO block.
 */
/* CSTYLED */
#define	D_FCH_RMTGPIO_MASK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_RMT_GPIO,	\
	.srd_reg = 0xfc,	\
	.srd_nents = 1		\
}
#define	FCH_RMTGPIO_MASK_SMN(n)	amdzen_rmtgpio_smn_reg(D_FCH_RMTGPIO_MASK, n)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_GPIO_H */
