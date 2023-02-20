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

#ifndef _SYS_AMDZEN_FCH_GPIO_H
#define	_SYS_AMDZEN_FCH_GPIO_H

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

/*
 * This header file contains definitions for interacting with GPIOs. It does not
 * define the specific mapping of GPIO indexes to pins.
 *
 * In general the actual data layout of each GPIO register is roughly the same
 * between all of the different families today between Zen 1 - Zen 4. This leads
 * us to prefer a single, general register definition. While a few cases don't
 * use all fields, we leave that to the actual GPIO driver to distinguish. The
 * wake and interrupt status registers vary in which bits they use; however, the
 * registers themselves are always the same.
 *
 * The way that GPIOs are accessed varies on the chip family. The GPIO block is
 * built into the FCH (fusion controller hub) and was traditionally accessed via
 * memory-mapped I/O. However, this proved a problem the moment you got to a
 * system that has more than one FCH present as they would have ended up at the
 * same part of MMIO space. Starting with Rome, the GPIO subsystem was made
 * available over the SMN (System Management Network). This allows us to get
 * around the issue with multiple FCHs as each one is part of a different die
 * and therefore part of a different SMN.
 *
 * Of course, things aren't this simple. What has happened here is that starting
 * with Zen 2, systems that can support more than one processor node, aka more
 * than one DF (Data Fabric), which are the Epyc and Threadripper parts like
 * Rome, Milan, Genoa, etc., all support the ability to access the GPIOs over
 * the SMN alias (which is preferred by us). Otherwise, all accesses must be
 * performed over MMIO.
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
 * SMN vs. MMIO semantics as everything else. Support for remote GPIOs starts
 * with Zen 2 families (e.g. Rome, Matisse, Renoir), but not all APUs or CPUs
 * support the remote GPIOs. These are organized as:
 *
 *    Remote GPIOs:
 *     +-> 0x00 -- Remote GPIOs (256-271)
 *     +-> 0x40 -- Unusable, Reserved Remote GPIOs (272-303)
 *     +-> 0xC0 -- 16 Remote IOMUX entries (1 byte per)
 *     +-> 0xF0 -- Wake Status
 *     +-> 0xF4 -- Interrupt Status
 *     +-> 0xFC -- Wake and Interrupt Control
 *
 * We structure the GPIO regions as a total of four different register blocks.
 * There is one block that covers the entire non-remote GPIO segment. Then there
 * are three segments for the remote GPIOs covering the actual GPIOs, then the
 * I/O Mux, and then the control and status registers. These are broken up into
 * three regions because the drivers that want control over the I/O mux are not
 * the same as those that want control of the GPIOs. The actual remote I/O mux
 * definitions can be found in <sys/amdzen/fch/iomux.h>. While the non-remote
 * GPIOs do contain control segments in their block, because a single driver
 * will use all this, we don't consider it worthwhile to break this up, though
 * it does mean that if someone uses an invalid GPIO id 63, they will not get a
 * GPIO, but will instead get the wake and interrupt control register. We've
 * opted to make this tradeoff to simplify parts of the driver writing.
 *
 * We use a single register definition to represent every GPIO itself. While
 * there are minor differences between which fields and voltages are valid in
 * the GPIOs, those ultimately require knowledge of the actual hardware family
 * and socket and are better served kept in our per-CPU-family/socket data.
 * Similarly, the actual register offsets and most meanings of them are the same
 * between different AMD CPU platforms; however, occasionally there is an extra
 * reserved bit or a bit that is used differently in the various status and
 * control registers for GPIOs. The differences are noted in the register where
 * appropriate. As the actual offset and meaning is generally the same, we have
 * not opted to break this into a per-CPU family/socket definition either.
 *
 * As suggested above, remote GPIOs are not present on all AMD CPU platforms.
 * The notion of the Remote block was only introduced starting with Zen 2 family
 * CPUs. The presence or lack thereof of the remote GPIO block is less obviously
 * regular. In particular, we've seen some APUs with this. The following
 * families are known to have this: Rome, Renoir, Matisse, Milan, Cezanne,
 * Genoa, and Bergamo.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	FCH_GPIO_OFF		0x1500
#define	FCH_GPIO_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_GPIO_OFF)
#define	FCH_GPIO_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_GPIO_OFF)
#define	FCH_GPIO_SIZE		0x400

#ifndef	_ASM

/*
 * FCH::GPIO registers. As described above, these exist on a per-I/O die basis.
 * We use our own construction function here because the space is 0x400 bytes
 * large, but it is not naturally aligned. Similarly, there are no units here,
 * so we ensure that we always ASSERT that and ensure that users cannot pass us
 * an invalid value by simply not having it.
 */
MAKE_SMN_FCH_REG_FN(GPIO, gpio, FCH_GPIO_SMN_BASE, FCH_GPIO_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(GPIO, gpio, FCH_GPIO_OFF, FCH_GPIO_SIZE);
MAKE_MMIO_FCH_REG_FN(GPIO, gpio, 4);

/*
 * FCH::GPIO::GPIO_<num> -- this is the general GPIO control register for all
 * non-remote GPIOs. We treat all banks as one large group here. The bit
 * definitions are true for both SMN and MMIO accesses.
 *
 * While most GPIOs are identical, as always, there is an exception. In
 * particular, when we have I2C pads on certain families (Naples, Rome, Milan,
 * etc.) bits 22:17 are reserved. That is, there is no control over the output,
 * drive strength, etc. If you are using this directly and not as part of the
 * GPIO driver, please consult the corresponding pin data to understand how to
 * properly set the GPIO's values or reference the corresponding PPR. Generally
 * speaking this means that the universal way to implement an open-drain pin is
 * to enable and disable the output as a way of driving the pin low or allowing
 * high-impedance respectively.
 */
/*CSTYLED*/
#define	D_FCH_GPIO_GPIO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0x00,	\
	.srd_nents = 184	\
}
#define	FCH_GPIO_GPIO_SMN(n)		fch_gpio_smn_reg(D_FCH_GPIO_GPIO, n)
#define	FCH_GPIO_GPIO_MMIO(b, n)	fch_gpio_mmio_reg(b, D_FCH_GPIO_GPIO, n)

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
#define	FCH_GPIO_GPIO_INPUT_LOW	0
#define	FCH_GPIO_GPIO_INPUT_HIGH	1
#define	FCH_GPIO_GPIO_GET_WAKE_S5(r)	bitx32(r, 15, 15)
#define	FCH_GPIO_GPIO_GET_WAKE_S3(r)	bitx32(r, 14, 14)
#define	FCH_GPIO_GPIO_GET_WAKE_S0I3(r)	bitx32(r, 13, 13)
#define	FCH_GPIO_GPIO_GET_INT_EN(r)	bitx32(r, 12, 12)
#define	FCH_GPIO_GPIO_GET_INT_STS_EN(r)	bitx32(r, 11, 11)
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
/*
 * These constants represent the values that are split among both the low and
 * high bit (GET_DBT_LOW and GET_DBT_HIGH). They cannot be used directly with
 * either register.
 */
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
#define	FCH_GPIO_GPIO_SET_WAKE_S5(r, v)		bitset32(r, 15, 15, v)
#define	FCH_GPIO_GPIO_SET_WAKE_S3(r, v)		bitset32(r, 14, 14, v)
#define	FCH_GPIO_GPIO_SET_WAKE_S0I3(r, v)	bitset32(r, 13, 13, v)
#define	FCH_GPIO_GPIO_SET_INT_EN(r, v)		bitset32(r, 12, 12, v)
#define	FCH_GPIO_GPIO_SET_INT_STS_EN(r, v)	bitset32(r, 11, 11, v)
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
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0xfc,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_WAKE_CTL_SMN(n)	fch_gpio_smn_reg(D_FCH_GPIO_WAKE_CTL, n)
#define	FCH_GPIO_WAKE_CTL_MMIO(b, n)	\
    fch_gpio_mmio_reg(b, D_FCH_GPIO_WAKE_CTL, n)
#define	FCH_GPIO_WAKE_CTL_GET_WAKE_EN(r)	bitx32(r, 31, 31)
#define	FCH_GPIO_WAKE_CTL_GET_INT_EN(r)		bitx32(r, 30, 30)
#define	FCH_GPIO_WAKE_CTL_GET_EOI(r)		bitx32(r, 29, 29)
#define	FCH_GPIO_WAKE_CTL_GET_MASK_STS(r)	bitx32(r, 28, 28)
#define	FCH_GPIO_WAKE_CTL_GET_MASK_LO(r)	bitx32(r, 27, 24)
#define	FCH_GPIO_WAKE_CTL_GET_MASK_HI(r)	bitx32(r, 23, 16)
#define	FCH_GPIO_WAKE_CTL_GET_PWR_BTN(r)	bitx32(r, 15, 15)
#define	FCH_GPIO_WAKE_CTL_PWR_BTN_4S	0
#define	FCH_GPIO_WAKE_CTL_PWR_BTN_WIN8	1
#define	FCH_GPIO_WAKE_CTL_GET_INTR_ACT(r)	bitx32(r, 14, 14)
#define	FCH_GPIO_WAKE_CTL_INTR_ACT_LOW	0
#define	FCH_GPIO_WAKE_CTL_INTR_ACT_HIGH	1
#define	FCH_GPIO_WAKE_CTL_GET_GPIO0_SRC(r)	bitx32(r, 13, 13)
#define	FCH_GPIO_WAKE_CTL_GPIO0_SRC_DET_2ND	0
#define	FCH_GPIO_WAKE_CTL_GPIO0_SRC_DET_1ST	1
#define	FCH_GPIO_WAKE_CTL_GET_INTR_TRIG(r)	bitx32(r, 12, 12)
#define	FCH_GPIO_WAKE_CTL_INTR_TRIG_LEVEL	0
#define	FCH_GPIO_WAKE_CTL_INTR_TRIG_PULSE	1

#define	FCH_GPIO_WAKE_CTL_SET_WAKE_EN(r, v)	bitset32(r, 31, 31, v)
#define	FCH_GPIO_WAKE_CTL_SET_INT_EN(r, v)	bitset32(r, 30, 30, v)
#define	FCH_GPIO_WAKE_CTL_SET_EOI(r, v)		bitset32(r, 29, 29, v)
#define	FCH_GPIO_WAKE_CTL_SET_MASK_STS(r, v)	bitset32(r, 28, 28, v)
#define	FCH_GPIO_WAKE_CTL_SET_MASK_LO(r, v)	bitset32(r, 27, 24, v)
#define	FCH_GPIO_WAKE_CTL_SET_MASK_HI(r, v)	bitset32(r, 23, 16, v)
#define	FCH_GPIO_WAKE_CTL_SET_PWR_BTN(r, v)	bitset32(r, 15, 15, v)
#define	FCH_GPIO_WAKE_CTL_SET_INTR_ACT(r, v)	bitset32(r, 14, 14, v)
#define	FCH_GPIO_WAKE_CTL_SET_GPIO0_SRC(r, v)	bitset32(r, 13, 13, v)
#define	FCH_GPIO_WAKE_CTL_SET_INTR_TRIG(r, v)	bitset32(r, 12, 12, v)

/*
 * FCH::GPIO::GPIO_WAKE_STATUS_INDEX_0 -- Indicates whether a wake event
 * occurred. Each bit in this register is used to indicate the wake status of 4
 * pins. There are three different common configurations of this register:
 *
 *  1) Bits 14:0 are reserved. This by Rome, Matisse, and Milan
 *  2) Bits 14:0 are used.
 *
 * There is a bit of an additional wrinkle here to think through. In particular,
 * the Zen 4 APUs (e.g. Raphael, Phoenix, etc.) end up nominally phrasing this
 * as 31:16 are for S0 and 15:0 are for S5, but the documentation is unclear if
 * the index resets here.
 */
/* CSTYLED */
#define	D_FCH_GPIO_WAKE_STS0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0x2f0,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_WAKE_STS0_SMN(n)	\
    fch_gpio_smn_reg(D_FCH_GPIO_WAKE_STS0, n)
#define	FCH_GPIO_WAKE_STS0_MMIO(b, n)	\
    fch_gpio_mmio_reg(b, D_FCH_GPIO_WAKE_STS0, n)

/*
 * FCH::GPIO::GPIO_WAKE_STATUS_INDEX_1 -- Indicates whether a wake event
 * occurred. Just as with the entry above, there is again a small amount of
 * variance here. There are two modes:
 *
 *  1) Bit 14 is reserved. This is true for a wide array of processors:
 *    * All Zen 1 CPUs and APUs
 *    * Some Zen 2/3: Renoir, Van Gogh, Mendocino, Vermeer, Rembrandt, Cezanne
 *    * Zen 4 Server CPUs (Genoa, Bergamo)
 *
 *  2) Bit 14 is valid:
 *    * Some Zen 2/3: Rome, Matisse, and Milan
 *    * Zen 4 APUs (Raphael, Phoenix)
 */
/* CSTYLED */
#define	D_FCH_GPIO_WAKE_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0x2f4,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_WAKE_STS1_SMN(n)	\
    fch_gpio_smn_reg(D_FCH_GPIO_WAKE_STS1, n)
#define	FCH_GPIO_WAKE_STS1_MMIO(b, n)	\
    fch_gpio_mmio_reg(b, D_FCH_GPIO_WAKE_STS1, n)
#define	FCH_GPIO_WAKE_STS1_GET_PME_WAKE(r)	bitx32(r, 15, 15)
#define	FCH_GPIO_WAKE_STS1_GET_WAKE(r)		bitx32(r, 14, 0)

#define	FCH_GPIO_WAKE_STS1_SET_PME_WAKE(r, v)	bitset32(r, 15, 15, v)
#define	FCH_GPIO_WAKE_STS1_SET_WAKE(r, v)	bitset32(r, 14, 0, v)

/*
 * FCH::GPIO::GPIO_INTERRUPT_STATUS_INDEX_0  -- Indicates whether an interrupt
 * has occurred. This has the same splits as GPIO_WAKE_STATUS_INDEX_0.
 * Specifically in the validity of bits 14:0.
 */
/* CSTYLED */
#define	D_FCH_GPIO_INT_STS0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0x2f8,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_INT_STS0_SMN(n)	\
    fch_gpio_smn_reg(D_FCH_GPIO_INT_STS0, n)
#define	FCH_GPIO_INT_STS0_MMIO(b, n)	\
    fch_gpio_mmio_reg(b, D_FCH_GPIO_INT_STS0, n)

/*
 * FCH::GPIO::GPIO_INTERRUPT_STATUS_INDEX_1 -- Indicates whether an interrupt
 * has occurred. This also has additional interrupt controls. Bits 14:0 have a
 * similar split as with WAKE_STATUS_INDEX_1. The non-status bits are identical
 * across everything.
 */
/* CSTYLED */
#define	D_FCH_GPIO_INT_STS1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_GPIO,	\
	.srd_reg = 0x2fc,	\
	.srd_nents = 1		\
}
#define	FCH_GPIO_INT_STS1_SMN(n)	fch_gpio_smn_reg(D_FCH_GPIO_INT_STS1, n)
#define	FCH_GPIO_INT_STS1_MMIO(b, n)	\
    fch_gpio_mmio_reg(b, D_FCH_GPIO_INT_STS1, n)
#define	FCH_GPIO_INT_STS1_GET_MASK_STS(r)	bitx32(r, 28, 28)
#define	FCH_GPIO_INT_STS1_GET_MASK_LO(r)	bitx32(r, 27, 24)
#define	FCH_GPIO_INT_STS1_GET_MASK_HI(r)	bitx32(r, 23, 16)
#define	FCH_GPIO_INT_STS1_GET_PME_INTR(r)	bitx32(r, 15, 15)
#define	FCH_GPIO_INT_STS1_GET_INTR(r)		bitx32(r, 14, 0)

#define	FCH_GPIO_INT_STS1_SET_MASK_STS(r, v)	bitset32(r, 28, 28, v)
#define	FCH_GPIO_INT_STS1_SET_MASK_LO(r, v)	bitset32(r, 27, 24, v)
#define	FCH_GPIO_INT_STS1_SET_MASK_HI(r, v)	bitset32(r, 23, 16, v)
#define	FCH_GPIO_INT_STS1_SET_PME_INTR(r, v)	bitset32(r, 15, 15, v)
#define	FCH_GPIO_INT_STS1_SET_INTR(r, v)	bitset32(r, 14, 0, v)

#endif	/* !_ASM */

#define	FCH_RMTGPIO_OFF		0x1200
#define	FCH_RMTGPIO_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_RMTGPIO_OFF)
#define	FCH_RMTGPIO_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_RMTGPIO_OFF)
#define	FCH_RMTGPIO_SIZE	0xc0

#define	FCH_RMTGPIO_AGG_OFF	0x12f0
#define	FCH_RMTGPIO_AGG_SMN_BASE	\
	(FCH_RELOCATABLE_SMN_BASE + FCH_RMTGPIO_AGG_OFF)
#define	FCH_RMTGPIO_AGG_PHYS_BASE	\
	(FCH_RELOCATABLE_PHYS_BASE + FCH_RMTGPIO_AGG_OFF)
#define	FCH_RMTGPIO_AGG_SIZE	0x10

#ifndef	_ASM

MAKE_SMN_FCH_REG_FN(RMTGPIO, rmtgpio,
    FCH_RMTGPIO_SMN_BASE, FCH_RMTGPIO_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(RMTGPIO, rmtgpio, FCH_RMTGPIO_OFF,
    FCH_RMTGPIO_SIZE);
MAKE_MMIO_FCH_REG_FN(RMTGPIO, rmtgpio, 4);

MAKE_SMN_FCH_REG_FN(RMTGPIO_AGG, rmtgpio_agg,
    FCH_RMTGPIO_AGG_SMN_BASE, FCH_RMTGPIO_AGG_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(RMTGPIO_AGG, rmtgpio_agg, FCH_RMTGPIO_AGG_OFF,
    FCH_RMTGPIO_AGG_SIZE);
MAKE_MMIO_FCH_REG_FN(RMTGPIO_AGG, rmtgpio_agg, 4);

/*
 * FCH::RMTGPIO::GPIO_<num> -- this is the set of remote GPIO banks that exist
 * in the system. These use the same register definition as for the normal GPIO
 * one.
 */
/*CSTYLED*/
#define	D_FCH_RMTGPIO_GPIO	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_RMTGPIO,	\
	.srd_reg = 0x00,	\
	.srd_nents = 16		\
}
#define	FCH_RMTGPIO_GPIO_SMN(n)		\
    fch_rmtgpio_smn_reg(D_FCH_RMTGPIO_GPIO, n)
#define	FCH_RMTGPIO_GPIO_MMIO(b, n)	\
    fch_rmtgpio_mmio_reg(b, D_FCH_RMTGPIO_GPIO, n)

/*
 * FCH::RMTGPIO::RMT_GPIO_WAKE_STATUS -- This provides wake status information
 * for the remote GPIO set. Here, each bit corresponds to a GPIO rather than a
 * group.
 */
/* CSTYLED */
#define	D_FCH_RMTGPIO_WAKE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_RMTGPIO_AGG,	\
	.srd_reg = 0xf0,	\
	.srd_nents = 1		\
}
#define	FCH_RMTGPIO_WAKE_SMN(n)		\
    fch_rmtgpio_agg_smn_reg(D_FCH_RMTGPIO_WAKE, n)
#define	FCH_RMTGPIO_WAKE_MMMIO(b, n)	\
    fch_rmtgpio_agg_mmio_reg(b, D_FCH_RMTGPIO_WAKE, n)
#define	FCH_RMTGPIO_WAKE_GET_WAKE(r)	bitx32(r, 15, 0)
#define	FCH_RMTGPIO_WAKE_SET_WAKE(r, v)	bitset32(r, 15, 0, v)

/*
 * FCH::RMTGPIO::RMT_GPIO_INTERRUPT_STATUS -- This provides interrupt status
 * information for the remote GPIO set. Here, each bit corresponds to a GPIO
 * rather than a group.
 */
/* CSTYLED */
#define	D_FCH_RMTGPIO_INT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_RMTGPIO_AGG,	\
	.srd_reg = 0xf4,	\
	.srd_nents = 1		\
}
#define	FCH_RMTGPIO_INT_SMN(n)		\
    fch_rmtgpio_agg_smn_reg(D_FCH_RMTGPIO_INT, n)
#define	FCH_RMTGPIO_INT_MMMIO(b, n)	\
    fch_rmtgpio_agg_mmio_reg(b, D_FCH_RMTGPIO_INT, n)
#define	FCH_RMTGPIO_INT_GET_INTR(r)	bitx32(r, 15, 0)

/*
 * FCH::RMTGPIO::RMT_GPIO_MASTER_SWITCH -- This controls the mask settings for
 * the remote GPIO block.
 */
/* CSTYLED */
#define	D_FCH_RMTGPIO_MASK	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_RMTGPIO_AGG,	\
	.srd_reg = 0xfc,	\
	.srd_nents = 1		\
}
#define	FCH_RMTGPIO_MASK_SMN(n)		\
    fch_rmtgpio_agg_smn_reg(D_FCH_RMTGPIO_MASK, n)
#define	FCH_RMTGPIO_MASK_MMMIO(b, n)	\
    fch_rmtgpio_agg_mmio_reg(b, D_FCH_RMTGPIO_MASK, n)
#define	FCH_RMTGPIO_MASK_GET_STS(r)	bitx32(r, 28, 28)
#define	FCH_RMTGPIO_MASK_GET_LO(r)	bitx32(r, 27, 24)
#define	FCH_RMTGPIO_MASK_GET_HI(r)	bitx32(r, 23, 16)

#define	FCH_RMTGPIO_MASK_SET_STS(r, v)	bitset32(r, 28, 28, v)
#define	FCH_RMTGPIO_MASK_SET_LO(r, v)	bitset32(r, 27, 24, v)
#define	FCH_RMTGPIO_MASK_SET_HI(r, v)	bitset32(r, 23, 16, v)

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_FCH_GPIO_H */
