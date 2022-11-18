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

#ifndef _SYS_IO_FCH_UART_H
#define	_SYS_IO_FCH_UART_H

/*
 * FCH::UART contains a collection of DesignWare UART peripherals.  Huashan has
 * 4 of them; Songshan has 3; we model each as a functional sub-unit.  In
 * addition to FCH::UART, each UART is also associated with an AXI DMA
 * controller that does not normally seem to need anything done to/with it for
 * the UARTs to work.  Nevertheless, we include those here as additional
 * functional sub-units.
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMN access to the UART registers is possible only on Songshan (yes, I tried
 * it on Huashan; no go).  The DMA controllers are never accessible over SMN
 * apparently.
 */

#define	HUASHAN_MAX_UART	4
#define	SONGSHAN_MAX_UART	3

#define	FCH_UART_SMN_BASE	0x2dd9000
#define	FCH_UART_PHYS_BASE	0xfedc9000
#define	FCH_UART_SIZE		0x1000

#define	FCH_DMA_PHYS_BASE	0xfedc7000
#define	FCH_DMA_SIZE		0x1000

#ifndef	_ASM

/*
 * For consumers like fch(4d) that need the address rather than register
 * descriptors.
 */
static inline uint32_t
songshan_uart_smn_aperture(const uint8_t unit)
{
	const uint32_t base = FCH_UART_SMN_BASE;

	ASSERT3U(unit, <, SONGSHAN_MAX_UART);

	const uint32_t unit32 = (uint32_t)unit;

	if (unit == 2)
		return (base + 0x5000U);

	return (base + unit32 * FCH_UART_SIZE);
}

static inline smn_reg_t
songshan_uart_smn_reg(const uint8_t unit, const smn_reg_def_t def)
{
	const uint32_t aperture = songshan_uart_smn_aperture(unit);
	const uint32_t REG_MASK = 0xfffU;
	ASSERT0(aperture & REG_MASK);

	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_FCH_UART);
	ASSERT0(def.srd_reg & ~REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

/*
 * The MMIO physical blocks are always in the same place, provided the
 * peripheral instance exists.  These are not relocatable, so only the primary
 * FCH's peripherals can be accessed this way.
 */
static inline paddr_t
__common_uart_mmio_aperture(const uint8_t unit, const uint8_t count)
{
	const paddr_t base = FCH_UART_PHYS_BASE;
	const paddr_t unit64 = (const paddr_t)unit;

	ASSERT3U(unit, <, count);

	switch (unit) {
	case 0:
	case 1:
		return (base + unit64 * FCH_UART_SIZE);
	case 2:
	case 3:
		return (base + unit64 * FCH_UART_SIZE + 0x3000UL);
	default:
		panic("unreachable code: invalid UART unit %lu", unit64);
	}
}

static inline paddr_t
__common_dma_mmio_aperture(const uint8_t unit, const uint8_t count)
{
	const paddr_t base = FCH_DMA_PHYS_BASE;
	const paddr_t unit64 = (const paddr_t)unit;

	ASSERT3U(unit, <, count);

	switch (unit) {
	case 0:
	case 1:
		return (base + unit64 * FCH_DMA_SIZE);
	case 2:
	case 3:
		return (base + unit64 * FCH_DMA_SIZE + 0x3000UL);
	default:
		panic("unreachable code: invalid DMA unit %lu", unit64);
	}
}

static inline paddr_t
huashan_uart_mmio_aperture(const uint8_t unit)
{
	return (__common_uart_mmio_aperture(unit, HUASHAN_MAX_UART));
}

static inline paddr_t
songshan_uart_mmio_aperture(const uint8_t unit)
{
	return (__common_uart_mmio_aperture(unit, SONGSHAN_MAX_UART));
}

static inline paddr_t
huashan_dma_mmio_aperture(const uint8_t unit)
{
	return (__common_dma_mmio_aperture(unit, HUASHAN_MAX_UART));
}

static inline paddr_t
songshan_dma_mmio_aperture(const uint8_t unit)
{
	return (__common_dma_mmio_aperture(unit, SONGSHAN_MAX_UART));
}

static inline mmio_reg_block_t
__common_uart_mmio_block(const uint8_t unit, const uint8_t count)
{
	ASSERT3U(unit, <, count);

	const mmio_reg_block_phys_t phys = {
		.mrbp_base = __common_uart_mmio_aperture(unit, count),
		.mrbp_len = FCH_UART_SIZE
	};

	return (mmio_reg_block_map(SMN_UNIT_FCH_UART, phys));
}

static inline mmio_reg_block_t
__common_dma_mmio_block(const uint8_t unit, const uint8_t count)
{
	ASSERT3U(unit, <, count);

	const mmio_reg_block_phys_t phys = {
		.mrbp_base = __common_dma_mmio_aperture(unit, count),
		.mrbp_len = FCH_DMA_SIZE
	};

	return (mmio_reg_block_map(SMN_UNIT_FCH_DMA, phys));
}

static inline mmio_reg_block_t
huashan_uart_mmio_block(const uint8_t unit)
{
	return (__common_uart_mmio_block(unit, HUASHAN_MAX_UART));
}

static inline mmio_reg_block_t
songhan_uart_mmio_block(const uint8_t unit)
{
	return (__common_dma_mmio_block(unit, SONGSHAN_MAX_UART));
}

static inline mmio_reg_block_t
huashan_dma_mmio_block(const uint8_t unit)
{
	return (__common_dma_mmio_block(unit, HUASHAN_MAX_UART));
}

static inline mmio_reg_block_t
songhan_dma_mmio_block(const uint8_t unit)
{
	return (__common_dma_mmio_block(unit, SONGSHAN_MAX_UART));
}

/*
 * Compile-time constant versions of fch_x_mmio_aperture().  Normal code should
 * not use this, only where required for a const initialiser.  const_fn sure
 * would be nice!
 */
#define	FCH_UART_MMIO_APERTURE(u)	\
	((u < 2) ? FCH_UART_PHYS_BASE + (u * FCH_UART_SIZE) :	\
	FCH_UART_PHYS_BASE + u * FCH_UART_SIZE + 0x3000)

#define	FCH_DMA_MMIO_APERTURE(u)	\
	((u < 2) ? FCH_DMA_PHYS_BASE + (u * FCH_DMA_SIZE) :	\
	FCH_DMA_PHYS_BASE + u * FCH_DMA_SIZE + 0x3000)

MAKE_MMIO_FCH_REG_FN(UART, uart, 4);

#define	FCH_UART_REGOFF_DLL	0x00
#define	FCH_UART_REGOFF_RBR	0x00
#define	FCH_UART_REGOFF_THR	0x00
#define	FCH_UART_REGOFF_DLH	0x04
#define	FCH_UART_REGOFF_IER	0x04
#define	FCH_UART_REGOFF_FCR	0x08
#define	FCH_UART_REGOFF_IIR	0x08
#define	FCH_UART_REGOFF_LCR	0x0C
#define	FCH_UART_REGOFF_MCR	0x10
#define	FCH_UART_REGOFF_LSR	0x14
#define	FCH_UART_REGOFF_MSR	0x18
#define	FCH_UART_REGOFF_SCR	0x1c
#define	FCH_UART_REGOFF_FAR	0x70
#define	FCH_UART_REGOFF_USR	0x7c
#define	FCH_UART_REGOFF_TFL	0x80
#define	FCH_UART_REGOFF_RFL	0x84
#define	FCH_UART_REGOFF_SRR	0x88
#define	FCH_UART_REGOFF_SRTS	0x8C
#define	FCH_UART_REGOFF_SBCR	0x90
#define	FCH_UART_REGOFF_SDMAM	0x94
#define	FCH_UART_REGOFF_SFE	0x98
#define	FCH_UART_REGOFF_SRT	0x9C
#define	FCH_UART_REGOFF_STET	0xA0
#define	FCH_UART_REGOFF_CPR	0xF4
#define	FCH_UART_REGOFF_UCV	0xF8
#define	FCH_UART_REGOFF_CTR	0xFC

/*
 * FCH::UART::DLL.  Divisor latch low.
 */

/*CSTYLED*/
#define	D_FCH_UART_DLL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_DLL	\
}
#define	FCH_UART_DLL_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_DLL, 0)

/*
 * FCH::UART::RBR.  Receive buffer register.
 */

/*CSTYLED*/
#define	D_FCH_UART_RBR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_RBR	\
}
#define	FCH_UART_RBR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_RBR, 0)

/*
 * FCH::UART::THR.  Transmit hold register.
 */

/*CSTYLED*/
#define	D_FCH_UART_THR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_THR	\
}
#define	FCH_UART_THR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_THR, 0)

/*
 * FCH::UART::DLH.  Divisor latch high.
 */

/*CSTYLED*/
#define	D_FCH_UART_DLH	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_DLH	\
}
#define	FCH_UART_DLH_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_DLH, 0)

/*
 * FCH::UART::IER.  Interrupt enable register.
 */

/*CSTYLED*/
#define	D_FCH_UART_IER	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_IER	\
}
#define	FCH_UART_IER_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_IER, 0)

#define	FCH_UART_IER_GET_PTIME(r)	bitx32(r, 7, 7)
#define	FCH_UART_IER_SET_PTIME(r, v)	bitset32(r, 7, 7, v)
#define	FCH_UART_IER_GET_EDSSI(r)	bitx32(r, 3, 3)
#define	FCH_UART_IER_SET_EDSSI(r, v)	bitset32(r, 3, 3, v)
#define	FCH_UART_IER_GET_ELSI(r)	bitx32(r, 2, 2)
#define	FCH_UART_IER_SET_ELSI(r, v)	bitset32(r, 2, 2, v)
#define	FCH_UART_IER_GET_ETBEI(r)	bitx32(r, 1, 1)
#define	FCH_UART_IER_SET_ETBEI(r, v)	bitset32(r, 1, 1, v)
#define	FCH_UART_IER_GET_ERBFI(r)	bitx32(r, 0, 0)
#define	FCH_UART_IER_SET_ERBFI(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::UART::FCR.  FIFO control register.
 */

/*CSTYLED*/
#define	D_FCH_UART_FCR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_FCR	\
}
#define	FCH_UART_FCR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_FCR, 0)

#define	FCH_UART_FCR_SET_RT(r, v)	bitset32(r, 7, 6, v)
#define	FCH_UART_FCR_RT_1CH		0
#define	FCH_UART_FCR_RT_QUARTER		1
#define	FCH_UART_FCR_RT_HALF		2
#define	FCH_UART_FCR_RT_FULL_2CH	3
#define	FCH_UART_FCR_SET_TET(r, v)	bitset32(r, 5, 4, v)
#define	FCH_UART_FCR_TET_EMPTY		0
#define	FCH_UART_FCR_TET_2CH		1
#define	FCH_UART_FCR_TET_QUARTER	2
#define	FCH_UART_FCR_TET_HALF		3
#define	FCH_UART_FCR_SET_DMAM(r, v)	bitset32(r, 3, 3, v)
#define	FCH_UART_FCR_SET_XFIFOR(r, v)	bitset32(r, 2, 2, v)
#define	FCH_UART_FCR_SET_RFIFOR(r, v)	bitset32(r, 1, 1, v)
#define	FCH_UART_FCR_SET_FIFOE(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::UART::IIR.  Interrupt ID register.
 */

/*CSTYLED*/
#define	D_FCH_UART_IIR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_IIR	\
}
#define	FCH_UART_IIR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_IIR, 0)

#define	FCH_UART_IIR_GET_FIFOSE(r)	bitx32(r, 7, 6)
#define	FCH_UART_IIR_FIFOSE_DISABLED	0
#define	FCH_UART_IIR_FIFOSE_ENABLED	3
#define	FCH_UART_IIR_GET_IID(r)		bitx32(r, 3, 0)
#define	FCH_UART_IIR_IID_MODEMSTATUS	0
#define	FCH_UART_IIR_IID_NOINTRPENDING	1
#define	FCH_UART_IIR_IID_THREMPTY	2
#define	FCH_UART_IIR_IID_RCVDDATAAVAIL	4
#define	FCH_UART_IIR_IID_RCVRLINESTATUS	6
#define	FCH_UART_IIR_IID_BUSYDETECT	7
#define	FCH_UART_IIR_IID_CHARTIMEOUT	12

/*
 * FCH::UART::LCR.  Line control register.
 */

/*CSTYLED*/
#define	D_FCH_UART_LCR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_LCR	\
}
#define	FCH_UART_LCR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_LCR, 0)

#define	FCH_UART_LCR_GET_DLAB(r)	bitx32(r, 7, 7)
#define	FCH_UART_LCR_SET_DLAB(r, v)	bitset32(r, 7, 7, v)
#define	FCH_UART_LCR_GET_BREAK(r)	bitx32(r, 6, 6)
#define	FCH_UART_LCR_SET_BREAK(r, v)	bitset32(r, 6, 6, v)
#define	FCH_UART_LCR_GET_SP(r)		bitx32(r, 5, 5)
#define	FCH_UART_LCR_SET_SP(r, v)	bitset32(r, 5, 5, v)
#define	FCH_UART_LCR_GET_EPS(r)		bitx32(r, 4, 4)
#define	FCH_UART_LCR_SET_EPS(r, v)	bitset32(r, 4, 4, v)
#define	FCH_UART_LCR_GET_PEN(r)		bitx32(r, 3, 3)
#define	FCH_UART_LCR_SET_PEN(r, v)	bitset32(r, 3, 3, v)
#define	FCH_UART_LCR_GET_STOP(r)	bitx32(r, 2, 2)
#define	FCH_UART_LCR_SET_STOP(r, v)	bitset32(r, 2, 2, v)
#define	FCH_UART_LCR_STOP_1BIT		0
#define	FCH_UART_LCR_STOP_2BIT		1
#define	FCH_UART_LCR_GET_DLS(r)		bitx32(r, 1, 0)
#define	FCH_UART_LCR_SET_DLS(r, v)	bitset32(r, 1, 0, v)
#define	FCH_UART_LCR_DLS_5BIT		0
#define	FCH_UART_LCR_DLS_6BIT		1
#define	FCH_UART_LCR_DLS_7BIT		2
#define	FCH_UART_LCR_DLS_8BIT		3

/*
 * FCH::UART::MCR.  Modem control register.
 */

/*CSTYLED*/
#define	D_FCH_UART_MCR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_MCR	\
}
#define	FCH_UART_MCR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_MCR, 0)

#define	FCH_UART_MCR_GET_SIRE(r)	bitx32(r, 6, 6)
#define	FCH_UART_MCR_GET_AFCE(r)	bitx32(r, 5, 5)
#define	FCH_UART_MCR_SET_AFCE(r, v)	bitset32(r, 5, 5, v)
#define	FCH_UART_MCR_GET_LOOPBACK(r)	bitx32(r, 4, 4)
#define	FCH_UART_MCR_SET_LOOPBACK(r, v)	bitset32(r, 4, 4, v)
#define	FCH_UART_MCR_GET_OUT2(r)	bitx32(r, 3, 3)
#define	FCH_UART_MCR_SET_OUT2(r, v)	bitset32(r, 3, 3, v)
#define	FCH_UART_MCR_GET_OUT1(r)	bitx32(r, 2, 2)
#define	FCH_UART_MCR_SET_OUT1(r, v)	bitset32(r, 2, 2, v)
#define	FCH_UART_MCR_GET_RTS(r)		bitx32(r, 1, 1)
#define	FCH_UART_MCR_SET_RTS(r, v)	bitset32(r, 1, 1, v)
#define	FCH_UART_MCR_GET_DTR(r)		bitx32(r, 0, 0)
#define	FCH_UART_MCR_SET_DTR(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::UART::LSR.  Line status register.
 */

/*CSTYLED*/
#define	D_FCH_UART_LSR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_LSR	\
}
#define	FCH_UART_LSR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_LSR, 0)

#define	FCH_UART_LSR_GET_RFE(r)		bitx32(r, 7, 7)
#define	FCH_UART_LSR_GET_TEMT(r)	bitx32(r, 6, 6)
#define	FCH_UART_LSR_GET_THRE(r)	bitx32(r, 5, 5)
#define	FCH_UART_LSR_GET_BI(r)		bitx32(r, 4, 4)
#define	FCH_UART_LSR_GET_FE(r)		bitx32(r, 3, 3)
#define	FCH_UART_LSR_GET_PE(r)		bitx32(r, 2, 2)
#define	FCH_UART_LSR_GET_OE(r)		bitx32(r, 1, 1)
#define	FCH_UART_LSR_GET_DR(r)		bitx32(r, 0, 0)

/*
 * FCH::UART::MSR.  Modem status register.
 */

/*CSTYLED*/
#define	D_FCH_UART_MSR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_MSR	\
}
#define	FCH_UART_MSR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_MSR, 0)

#define	FCH_UART_MSR_GET_DCD(r)		bitx32(r, 7, 7)
#define	FCH_UART_MSR_GET_RI(r)		bitx32(r, 6, 6)
#define	FCH_UART_MSR_GET_DSR(r)		bitx32(r, 5, 5)
#define	FCH_UART_MSR_GET_CTS(r)		bitx32(r, 4, 4)
#define	FCH_UART_MSR_GET_DDCD(r)	bitx32(r, 3, 3)
#define	FCH_UART_MSR_GET_TERI(r)	bitx32(r, 2, 2)
#define	FCH_UART_MSR_GET_DDSR(r)	bitx32(r, 1, 1)
#define	FCH_UART_MSR_GET_DCTS(r)	bitx32(r, 0, 0)

/*
 * FCH::UART::SCR.  Scratch register.
 */

/*CSTYLED*/
#define	D_FCH_UART_SCR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SCR	\
}
#define	FCH_UART_SCR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SCR, 0)

/*
 * FCH::UART::FAR.  FIFO access register.
 */

/*CSTYLED*/
#define	D_FCH_UART_FAR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_FAR	\
}
#define	FCH_UART_FAR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_FAR, 0)

#define	FCH_UART_FAR_GET(r)		bitx32(r, 0, 0)
#define	FCH_UART_FAR_SET(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::UART::USR.  UART status register.
 */

/*CSTYLED*/
#define	D_FCH_UART_USR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_USR	\
	}
#define	FCH_UART_USR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_USR, 0)

#define	FCH_UART_USR_GET_RFF(r)		bitx32(r, 4, 4)
#define	FCH_UART_USR_GET_RFNE(r)	bitx32(r, 3, 3)
#define	FCH_UART_USR_GET_TFE(r)		bitx32(r, 2, 2)
#define	FCH_UART_USR_GET_TFNF(r)	bitx32(r, 1, 1)
#define	FCH_UART_USR_GE_BUSY(r)		bitx32(r, 0, 0)

/*
 * FCH::UART::TFL.  Transmit FIFO level.
 */

/*CSTYLED*/
#define	D_FCH_UART_TFL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_TFL	\
}
#define	FCH_UART_TFL_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_TFL, 0)

#define	FCH_UART_TFL_GET(r)		bitx32(r, 4, 0)

/*
 * FCH::UART::RFL.  Receive FIFO level.
 */

/*CSTYLED*/
#define	D_FCH_UART_RFL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_RFL	\
}
#define	FCH_UART_RFL_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_RFL, 0)

#define	FCH_UART_RFL_GET(r)		bitx32(r, 4, 0)

/*
 * FCH::UART::SRR.  Shadow reset register.
 */

/*CSTYLED*/
#define	D_FCH_UART_SRR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SRR	\
}
#define	FCH_UART_SRR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SRR, 0)

#define	FCH_UART_SRR_SET_XFR(r, v)	bitset32(r, 2, 2, v)
#define	FCH_UART_SRR_SET_RFR(r, v)	bitset32(r, 1, 1, v)
#define	FCH_UART_SRR_SET_UR(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::UART::SRTS.  Shadow request to send.
 */

/*CSTYLED*/
#define	D_FCH_UART_SRTS	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SRTS	\
}
#define	FCH_UART_SRTS_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SRTS, 0)

#define	FCH_UART_SRTS_SET(r, v)		bitset32(r, 0, 0, v)
#define	FCH_UART_SRTS_GET(r, v)		bitx32(r, 0, 0)

/*
 * FCH::UART::SBCR.  Shadow break control bit.
 */

/*CSTYLED*/
#define	D_FCH_UART_SBCR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SBCR	\
}
#define	FCH_UART_SBCR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SBCR, 0)

#define	FCH_UART_SBCR_GET_SBCB(r, v)	bitx32(r, 0, 0)
#define	FCH_UART_SBCR_SET_SBCB(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::UART::SDMAM.  Shadow DMA mode.
 */

/*CSTYLED*/
#define	D_FCH_UART_SDMAM	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SDMAM,\
}
#define	FCH_UART_SDMAM_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SDMAM, 0)

#define	FCH_UART_SDMAM_GET(r, v)	bitx32(r, 0, 0)
#define	FCH_UART_SDMAM_SET(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::UART::SFE.  Shadow FIFO enable.
 */

/*CSTYLED*/
#define	D_FCH_UART_SFE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SFE	\
}
#define	FCH_UART_SFE_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SFE, 0)

#define	FCH_UART_SFE_GET(r, v)		bitx32(r, 0, 0)
#define	FCH_UART_SFE_SET(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::UART::SRT.  Shadow RCVR trigger.
 */

/*CSTYLED*/
#define	D_FCH_UART_SRT	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_SRT	\
}
#define	FCH_UART_SRT_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_SRT, 0)

#define		FCH_UART_SRT_GET(r, v)	bitx32(r, 1, 0)
#define	FCH_UART_SRT_SET(r, v)		bitset32(r, 1, 0, v)
/* See FCH_UART_FCR_RT_ for possible values */

/*
 * FCH::UART::STET.  Shadow TX empty trigger.
 */

/*CSTYLED*/
#define	D_FCH_UART_STET	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_STET	\
}
#define	FCH_UART_STET_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_STET, 0)

#define	FCH_UART_STET_GET(r, v)		bitx32(r, 1, 0)
#define	FCH_UART_STET_SET(r, v)		bitset32(r, 1, 0, v)
/* See FCH_UART_FCR_TET_ for possible values */

/*
 * FCH::UART::CPR
 */

/*CSTYLED*/
#define	D_FCH_UART_CPR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_CPR	\
}
#define	FCH_UART_CPR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_CPR, 0)

#define	FCH_UART_CPR_GET_FIFO_MODE(r)	bitx32(r, 23, 16)
#define	FCH_UART_CPR_FIFO_MODE_TO_BYTES(v) (16U * (v))
#define	FCH_UART_CPR_GET_DMA_EXTRA(r)	bitx32(r, 13, 13);
#define	FCH_UART_CPR_GET_UAEP(r)	bitx32(r, 12, 12);
#define	FCH_UART_CPR_GET_SHADOW(r)	bitx32(r, 11, 11);
#define	FCH_UART_CPR_GET_FIFO_STAT(r)	bitx32(r, 10, 10);
#define	FCH_UART_CPR_GET_FIFO_ACCESS(r)	bitx32(r, 9, 9);
#define	FCH_UART_CPR_GET_FEAT(r)	bitx32(r, 8, 8);
#define	FCH_UART_CPR_GET_SIR_LP_MODE(r)	bitx32(r, 7, 7);
#define	FCH_UART_CPR_GET_SIR_MODE(r)	bitx32(r, 6, 6);
#define	FCH_UART_CPR_GET_THRE_MODE(r)	bitx32(r, 5, 5);
#define	FCH_UART_CPR_GET_AFCE_MODE(r)	bitx32(r, 4, 4);
#define	FCH_UART_CPR_GET_APB_WIDTH(r)	bitx32(r, 1, 0);
#define	FCH_UART_CPR_APB_WIDTH8		0
#define	FCH_UART_CPR_APB_WIDTH16	1
#define	FCH_UART_CPR_APB_WIDTH32	2

/*
 * FCH::UART::UCV.  UART component version.
 */

/*CSTYLED*/
#define	D_FCH_UART_UCV	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_UCV	\
}
#define	FCH_UART_UCV_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_UCV, 0)

/*
 * FCH::UART::CTR.  Peripheral's identification code.
 */

/*CSTYLED*/
#define	D_FCH_UART_CTR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_UART,	\
	.srd_reg = FCH_UART_REGOFF_CTR	\
}
#define	FCH_UART_CTR_MMIO(b)	\
	fch_uart_mmio_reg((b), D_FCH_UART_CTR, 0)

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_UART_H */
