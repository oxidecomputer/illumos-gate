/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _RMGE_H
#define	_RMGE_H

#include <sys/ddi.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	RMGE_DRIVER_NAME	"rmge"
#define	RMGE_BAR2		2

#define	RMGE_SUCCESS			DDI_SUCCESS
#define	RMGE_FAILURE			DDI_FAILURE

#define	RMGE_REG_TXCFG			0x40
#define	RMGE_REG_TXCFG_MASK_HW_REV	0x7cf00000

typedef enum {
	RMGE_ATT_MILESTONE_SOFTSTATE			= 1 << 0,
	RMGE_ATT_MILESTONE_CSRS				= 1 << 1,
	RMGE_ATT_MILESTONE_ID_HW_REV			= 1 << 2,
	RMGE_ATT_MILESTONE_REG_MAC			= 1 << 3
} rmge_att_milestone;

typedef struct rmge {
	dev_info_t		*dip;
	int			instance;
	dev_t			dev;

	ddi_acc_handle_t	cfg_space_handle;
	caddr_t			bar2_mmio_addr;
	ddi_acc_handle_t	bar2_mmio_handle;

	rmge_att_milestone	att_milestone;
	uint32_t		hw_rev;

	mac_handle_t		mh;
	uint8_t			hw_mac_addr[ETHERADDRL];
} rmge_t;

#ifdef __cplusplus
}
#endif

#endif /* _RMGE_H */
