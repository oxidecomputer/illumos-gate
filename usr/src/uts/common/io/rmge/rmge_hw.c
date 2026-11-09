/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019, 2020, 2023-2025 Kevin Lo <kevlo@openbsd.org>
 * Copyright (c) 2025 Adrian Chadd <adrian@FreeBSD.org>
 *
 * Hardware programming portions from Realtek Semiconductor.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*	$OpenBSD: if_rge.c,v 1.38 2025/09/19 00:41:14 kevlo Exp $	*/

/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * Copyright 2026 Oxide Computer Company
 *
 * This file, while modified to suit our needs, is a derived work of the
 * FreeBSD driver, and as such, carries the license header above. While some
 * functions are line-by-line translations, others have been entirely re-
 * written. Some do not appear in the FreeBSD driver at all. This file has been
 * imported because Realtek does not distribute programming literature for
 * these devices. Where possible, we have tried to divine the meaning of
 * unexplained constants. This proves challenging, as the ultimate source of
 * these values is undocumented, out-of-tree Linux kernel driver drops from
 * Realtek themselves.
 *
 * In the cases where our code is less opaque than upstream
 * FreeBSD, meaning of constants has been inferred from corroborating their
 * usage across:
 *
 * 1. The XpressReal board's source https://github.com/XpressReal/linux-sdk/
 * 2. DPDK: https://github.com/DPDK/dpdk/blob/main/drivers/net/r8169/r8169_hw.c
 * 3. Linux's Realtek USB (!) code: drivers/net/usb/r8152.c
 *
 * These inferences should not be considered dependable, and exist only to make
 * the code more readable.
 */

#include <sys/errno.h>
#include <sys/miiregs.h>
#include <sys/sunddi.h>

#include "rmge.h"

#define	RMGE_CMD			0x0037
#define	RMGE_CMD_TXENB			0x04
#define	RMGE_CMD_RXENB			0x08
#define	RMGE_CMD_RESET			0x10
#define	RMGE_CMD_STOPREQ		0x80

#define	RMGE_IMR			0x0038
#define	RMGE_ISR			0x003c

#define	RMGE_REG_TXCFG			0x40
#define	RMGE_REG_TXCFG_MASK_HW_REV	0x7cf00000

#define	RMGE_RXCFG			0x0044

#define	RMGE_PMCH			0x006f

#define	RMGE_TIMERINT0			0x0058
#define	RMGE_TIMERINT1			0x005c
#define	RMGE_TIMERINT2			0x008c
#define	RMGE_TIMERINT3			0x00f4

#define	RMGE_TWICMD			0x00d2

#define	RMGE_MCUCMD			0x00d3
#define	RMGE_MCUCMD_RXFIFO_EMPTY	0x10
#define	RMGE_MCUCMD_TXFIFO_EMPTY	0x20
#define	RMGE_MCUCMD_IS_OOB		0x80

#define	RMGE_PPSW			0x00f2

#define	RMGE_IM				0x00e2

#define	RMGE_MACOCP			0x00b0
#define	RMGE_MACOCP_DATA_MASK		0x0000ffff
#define	RMGE_MACOCP_BUSY		0x80000000
#define	RMGE_MACOCP_ADDR_SHIFT		16

#define	RMGE_PHYOCP			0x00b8
#define	RMGE_PHYOCP_DATA_MASK		0x0000ffff
#define	RMGE_PHYOCP_BUSY		0x80000000
#define	RMGE_PHYOCP_ADDR_SHIFT		16

#define	RMGE_PHYBASE			0x0a40

#define	RMGE_TIMEOUT			100

#define	RMGE_RXCFG_ALLPHYS		0x00000001
#define	RMGE_RXCFG_INDIV		0x00000002
#define	RMGE_RXCFG_MULTI		0x00000004
#define	RMGE_RXCFG_BROAD		0x00000008
#define	RMGE_RXCFG_RUNT			0x00000010
#define	RMGE_RXCFG_ERRPKT		0x00000020
#define	RMGE_RXCFG_VLANSTRIP		0x00c00000

/* Undocumented, inferred register and constant values */
#define	RMGE_INF_REALWOW		0x0000c0bc
#define	RMGE_INF_REALWOW_DISABLE	0x000000ff

static uint32_t
rmge_read4(rmge_t *rmge, uint32_t reg)
{
	uint32_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);
	VERIFY0(reg & (sizeof (uint32_t) - 1));

	off = (uint32_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get32(rmge->bar2_mmio_handle, off));
}

static void
rmge_write4(rmge_t *rmge, uint32_t reg, uint32_t val)
{
	uint32_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);
	VERIFY0(reg & (sizeof (uint32_t) - 1));

	off = (uint32_t *)(rmge->bar2_mmio_addr + reg);
	ddi_put32(rmge->bar2_mmio_handle, off, val);
}

static uint16_t
rmge_read2(rmge_t *rmge, uint32_t reg)
{
	uint16_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);
	VERIFY0(reg & (sizeof (uint16_t) - 1));

	off = (uint16_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get16(rmge->bar2_mmio_handle, off));
}

static void
rmge_write2(rmge_t *rmge, uint32_t reg, uint16_t val)
{
	uint16_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);
	VERIFY0(reg & (sizeof (uint16_t) - 1));

	off = (uint16_t *)(rmge->bar2_mmio_addr + reg);
	ddi_put16(rmge->bar2_mmio_handle, off, val);
}

static uint8_t
rmge_read1(rmge_t *rmge, uint32_t reg)
{
	uint8_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);

	off = (uint8_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get8(rmge->bar2_mmio_handle, off));
}

static void
rmge_write1(rmge_t *rmge, uint32_t reg, uint8_t val)
{
	uint8_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);

	off = (uint8_t *)(rmge->bar2_mmio_addr + reg);
	ddi_put8(rmge->bar2_mmio_handle, off, val);
}

static void
rmge_setbit1(rmge_t *rmge, uint32_t reg, uint8_t val)
{
	rmge_write1(rmge, reg, rmge_read1(rmge, reg) | val);
}

static void
rmge_clrbit1(rmge_t *rmge, uint32_t reg, uint8_t val)
{
	rmge_write1(rmge, reg, rmge_read1(rmge, reg) & ~val);
}

static void
rmge_clrbit4(rmge_t *rmge, uint32_t reg, uint32_t val)
{
	rmge_write4(rmge, reg, rmge_read4(rmge, reg) & ~(val));
}

static uint16_t
rmge_read_mac_ocp(rmge_t *rmge, uint16_t reg)
{
	uint32_t val;

	val = (reg >> 1) << RMGE_MACOCP_ADDR_SHIFT;
	rmge_write4(rmge, RMGE_MACOCP, val);

	return (rmge_read4(rmge, RMGE_MACOCP) & RMGE_MACOCP_DATA_MASK);
}

static void
rmge_write_mac_ocp(rmge_t *rmge, uint16_t reg, uint16_t val)
{
	uint32_t u;

	u = (reg >> 1) << RMGE_MACOCP_ADDR_SHIFT;
	u += val;
	u |= RMGE_MACOCP_BUSY;
	rmge_write4(rmge, RMGE_MACOCP, u);
}

static void
rmge_mac_clrbit(rmge_t *rmge, uint16_t reg, uint16_t val)
{
	rmge_write_mac_ocp(rmge, reg, rmge_read_mac_ocp(rmge, reg) & ~val);
}

static uint16_t
rmge_read_phy_ocp(rmge_t *rmge, uint16_t reg)
{
	uint32_t val;

	val = (reg >> 1) << RMGE_PHYOCP_ADDR_SHIFT;
	rmge_write4(rmge, RMGE_PHYOCP, val);

	for (uint_t i = 0; i < 20000; i++) {
		drv_usecwait(1);
		val = rmge_read4(rmge, RMGE_PHYOCP);
		if ((val & RMGE_PHYOCP_BUSY) != 0) {
			break;
		}
	}

	return (val & RMGE_PHYOCP_DATA_MASK);
}

static void
rmge_write_phy_ocp(rmge_t *rmge, uint16_t reg, uint16_t val)
{
	uint32_t tmp;

	tmp = (reg >> 1) << RMGE_PHYOCP_ADDR_SHIFT;
	tmp |= RMGE_PHYOCP_BUSY | val;
	rmge_write4(rmge, RMGE_PHYOCP, tmp);

	for (uint_t i = 0; i < 20000; i++) {
		drv_usecwait(1);
		if ((rmge_read4(rmge, RMGE_PHYOCP) & RMGE_PHYOCP_BUSY) == 0) {
			break;
		}
	}
}

static void
rmge_phy_clrbit(rmge_t *rmge, uint16_t reg, uint16_t val)
{
	rmge_write_phy_ocp(rmge, reg, rmge_read_phy_ocp(rmge, reg) & ~val);
}

static void
rmge_write_phy(rmge_t *rmge, uint16_t addr, uint16_t reg, uint16_t val)
{
	uint16_t off, phyaddr;

	phyaddr = addr ? addr : RMGE_PHYBASE + (reg / 8);
	phyaddr <<= 4;

	off = addr ? reg : 0x10 + (reg % 8);
	phyaddr += (off - 16) << 1;

	rmge_write_phy_ocp(rmge, phyaddr, val);
}

static int
rmge_reset(rmge_t *rmge)
{
	rmge_clrbit4(rmge, RMGE_RXCFG, RMGE_RXCFG_ALLPHYS | RMGE_RXCFG_INDIV |
	    RMGE_RXCFG_MULTI | RMGE_RXCFG_BROAD | RMGE_RXCFG_RUNT |
	    RMGE_RXCFG_ERRPKT);

	/* Enable RXDV gate. */
	rmge_setbit1(rmge, RMGE_PPSW, 0x08);

	rmge_setbit1(rmge, RMGE_CMD, RMGE_CMD_STOPREQ);
	drv_usecwait(200);

	for (uint_t i = 0; i < 3000; i++) {
		drv_usecwait(50);
		if ((rmge_read1(rmge, RMGE_MCUCMD) &
		    (RMGE_MCUCMD_RXFIFO_EMPTY | RMGE_MCUCMD_TXFIFO_EMPTY)) ==
		    (RMGE_MCUCMD_RXFIFO_EMPTY | RMGE_MCUCMD_TXFIFO_EMPTY)) {
			break;
		}
	}

	for (uint_t i = 0; i < 3000; i++) {
		drv_usecwait(50);
		if ((rmge_read2(rmge, RMGE_IM) & 0x0103) == 0x0103) {
			break;
		}
	}

	rmge_write1(rmge, RMGE_CMD,
	    rmge_read1(rmge, RMGE_CMD) & (RMGE_CMD_TXENB | RMGE_CMD_RXENB));

	/* Soft reset. */
	rmge_write1(rmge, RMGE_CMD, RMGE_CMD_RESET);

	for (uint_t i = 0; i < RMGE_TIMEOUT; i++) {
		drv_usecwait(100);
		if ((rmge_read1(rmge, RMGE_CMD) & RMGE_CMD_RESET) == 0) {
			return (RMGE_SUCCESS);
		}
	}

	dev_err(rmge->dip, CE_WARN, "reset never completed");
	return (RMGE_FAILURE);
}

/*
 * Realtek NICs have a MIPS-based embedded Linux system on them that hosts a
 * IPMI/BMC-like interface, which is nominally an implementation of a DMTF
 * standard known as 'DASH'. Realtek refer to this as out-of-band management
 * (OOB). Unfortunately, OOB and operating systems cannot simultaneously
 * control the NIC. Moreover, it has a chequered security history, with
 * sentiment quite dour on the Linux kernel mailing list. We turn it off with
 * prejudice.
 *
 * On the plus side, this operation seems to be idempotent, as early firmware
 * (BIOS/UEFI) may also disable OOB.
 */
static int
rmge_exit_oob(rmge_t *rmge)
{
	int rc;

	if ((rc = rmge_reset(rmge)) != RMGE_SUCCESS) {
		return (rc);
	}

	rmge_clrbit1(rmge, RMGE_MCUCMD, RMGE_MCUCMD_IS_OOB);

	rmge_mac_clrbit(rmge, 0xe8de, 0x4000);

	for (uint_t i = 0; i < 10; i++) {
		drv_usecwait(100);
		if ((rmge_read2(rmge, RMGE_TWICMD) & 0x0200) != 0) {
			break;
		}
	}

	rmge_write_mac_ocp(rmge, 0xc0aa, 0x07d0);
	rmge_write_mac_ocp(rmge, 0xc0a6, 0x01b5);
	rmge_write_mac_ocp(rmge, 0xc01e, 0x5555);

	for (uint_t i = 0; i < 10; i++) {
		drv_usecwait(100);
		if ((rmge_read2(rmge, RMGE_TWICMD) & 0x0200) != 0) {
			break;
		}
	}

	if ((rmge_read_mac_ocp(rmge, 0xd42c) & 0x0100) != 0) {
		for (uint_t i = 0; i < RMGE_TIMEOUT; i++) {
			if ((rmge_read_phy_ocp(rmge, 0xa420) & 0x0007) == 2) {
				break;
			}
			drv_usecwait(1000);
		}
		rmge_mac_clrbit(rmge, 0xd42c, 0x0100);
		rmge_phy_clrbit(rmge, 0xa466, 0x0001);
		rmge_phy_clrbit(rmge, 0xa468, 0x000a);
	}

	return (RMGE_SUCCESS);
}

static void
rmge_set_phy_power(rmge_t *rmge)
{
	rmge_setbit1(rmge, RMGE_PMCH, 0xc0);

	rmge_write_phy(rmge, 0, MII_CONTROL, MII_CONTROL_ANE);

	for (uint_t i = 0; i < RMGE_TIMEOUT; i++) {
		if ((rmge_read_phy_ocp(rmge, 0xa420) & 0x0007) == 3) {
			break;
		}
		drv_usecwait(1000);
	}
}

static void
rmge_hw_init(rmge_t *rmge)
{
}

static void
rmge_hw_reset(rmge_t *rmge)
{
	/* Disable interrupts. */
	rmge_write4(rmge, RMGE_IMR, 0);
	rmge_write4(rmge, RMGE_ISR, rmge_read4(rmge, RMGE_ISR));

	/* Clear timer interrupts. */
	rmge_write4(rmge, RMGE_TIMERINT0, 0);
	rmge_write4(rmge, RMGE_TIMERINT1, 0);
	rmge_write4(rmge, RMGE_TIMERINT2, 0);
	rmge_write4(rmge, RMGE_TIMERINT3, 0);

	(void) rmge_reset(rmge);
}

int
rmge_drive_to_reset(rmge_t *rmge)
{
	int rc;

	/* Disable the "wake-on-WAN" feature. */
	rmge_write_mac_ocp(rmge, RMGE_INF_REALWOW, RMGE_INF_REALWOW_DISABLE);

	if ((rc = rmge_exit_oob(rmge)) != RMGE_SUCCESS) {
		return (rc);
	}

	rmge_set_phy_power(rmge);
	rmge_hw_init(rmge);
	rmge_hw_reset(rmge);

	rmge->att_milestone |= RMGE_ATT_MILESTONE_RESET_HW;

	return (RMGE_SUCCESS);
}
