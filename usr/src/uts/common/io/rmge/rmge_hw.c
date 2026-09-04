/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * Copyright 2026 Oxide Computer Company
 */

#include "rmge.h"

static uint32_t
rmge_read_bar2_32(rmge_t *rmge, uint32_t reg)
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

static uint8_t
rmge_read_bar2_8(rmge_t *rmge, uint32_t reg)
{
	uint8_t *off;

	VERIFY3P(rmge, !=, NULL);
	VERIFY(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	VERIFY3P(rmge->bar2_mmio_addr, !=, NULL);
	VERIFY3P(rmge->bar2_mmio_handle, !=, NULL);

	off = (uint8_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get8(rmge->bar2_mmio_handle, off));
}

int
rmge_identify_hw_rev(rmge_t *rmge)
{
	uint32_t txcfg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_ID_HW_REV);

	txcfg = rmge_read_bar2_32(rmge, RMGE_REG_TXCFG);
	rmge->hw_rev = txcfg & RMGE_REG_TXCFG_MASK_HW_REV;

	rmge->att_milestone |= RMGE_ATT_MILESTONE_ID_HW_REV;
	return (RMGE_SUCCESS);
}

void
rmge_read_mac_addr(rmge_t *rmge)
{
	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_ID_MAC);

	for (uint_t i = 0; i < ETHERADDRL; i++)
		rmge->hw_mac_addr[i]
		    = rmge_read_bar2_8(rmge, RMGE_REG_IDR0 + i);

	rmge->att_milestone |= RMGE_ATT_MILESTONE_ID_MAC;
}
