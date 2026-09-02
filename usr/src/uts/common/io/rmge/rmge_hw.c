/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * Copyright 2026 Oxide Computer Company
 *
 * Portions of this driver heavily borrowed from Open/FreeBSD:
 * Copyright 2019, 2020, 2023-2025 Kevin Lo <kevlo@openbsd.org>
 * Copyright 2025 Adrian Chadd <adrian@FreeBSD.org>
 */

#include <sys/errno.h>
#include <sys/miiregs.h>
#include <sys/sunddi.h>

#include "rmge.h"

#define	RMGE_HW_REV_RTL8125B		0x64100000
#define	RMGE_REG_IDR0			0x00

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

static void
rmge_read_mac_addr(rmge_t *rmge)
{
	for (uint_t i = 0; i < ETHERADDRL; i++) {
		rmge->hw.mac_addr[i] =
		    rmge_read1(rmge, RMGE_REG_IDR0 + i);
	}
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

static int
rmge_exit_oob(rmge_t *rmge)
{
	int rc;

	/* Disable RealWoW. */
	rmge_write_mac_ocp(rmge, 0xc0bc, 0x00ff);

	if ((rc = rmge_reset(rmge)) != RMGE_SUCCESS) {
		return (rc);
	}

	/* Disable OOB. */
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
rmge_set_phy_power(rmge_t *rmge, uint32_t on)
{
	if (on) {
		rmge_setbit1(rmge, RMGE_PMCH, 0xc0);

		rmge_write_phy(rmge, 0, MII_CONTROL, MII_CONTROL_ANE);

		for (uint_t i = 0; i < RMGE_TIMEOUT; i++) {
			if ((rmge_read_phy_ocp(rmge, 0xa420) & 0x0007) == 3) {
				break;
			}
			drv_usecwait(1000);
		}
	} else {
		rmge_write_phy(rmge, 0, MII_CONTROL,
		    MII_CONTROL_ANE | MII_CONTROL_PWRDN);
		rmge_clrbit1(rmge, RMGE_PMCH, 0x80);
		rmge_clrbit1(rmge, RMGE_PPSW, 0x40);
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
rmge_identify_device_particulars(rmge_t *rmge)
{
	uint32_t txcfg;
	uint32_t hw_rev_raw;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT3U(rmge->hw->chip_id, ==, RMGE_HW_UNKNOWN);

	txcfg = rmge_read4(rmge, RMGE_REG_TXCFG);
	hw_rev_raw = txcfg & RMGE_REG_TXCFG_MASK_HW_REV;

	switch (hw_rev_raw) {
	case RMGE_HW_REV_RTL8125B:
		rmge->hw.rev = RMGE_HW_REV_RTL8125B;
		rmge->hw.firmware_name = "rtl8125b-2.bin";
		break;
	default:
		dev_err(rmge->dip, CE_WARN,
		    "unsupported hardware revision 0x%08x (TXCFG 0x%08x)",
		    hw_rev_raw, txcfg);
		return (RMGE_FAILURE);
	}


	rmge_read_mac_addr(rmge);
	rmge->att_milestone |= RMGE_ATT_MILESTONE_ID_HW_REV;

	return (RMGE_SUCCESS);
}

int
rmge_drive_to_reset(rmge_t *rmge)
{
	int rc;

	if ((rc = rmge_exit_oob(rmge)) != RMGE_SUCCESS) {
		return (rc);
	}

	rmge_set_phy_power(rmge, 1);
	rmge_hw_init(rmge);
	rmge_hw_reset(rmge);

	rmge->att_milestone |= RMGE_ATT_MILESTONE_RESET_HW;

	return (RMGE_SUCCESS);
}
