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
#include <sys/ethernet.h>
#include <sys/mac_provider.h>
#include <sys/mac_ether.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	RMGE_DRIVER_NAME		"rmge"
#define	RMGE_BAR2			2

#define	RMGE_SUCCESS			DDI_SUCCESS
#define	RMGE_FAILURE			DDI_FAILURE

/*
 * Unfortunately, the same PCI device ID is shared by multiple mutually
 * incompatible device models. One should not expect small differences as
 * suggested by the name "hardware revisions". Distinct "revisions" are
 * tantamount to distinct devices.
 */
typedef enum {
	RMGE_HW_UNKNOWN = 0,
	RMGE_HW_REV_RTL8125B
} rmge_hw_rev_t;

typedef enum {
	RMGE_ATT_MILESTONE_SOFTSTATE			= 1 << 0,
	RMGE_ATT_MILESTONE_CSRS				= 1 << 1,
	RMGE_ATT_MILESTONE_ID_HW_REV			= 1 << 2,
	RMGE_ATT_MILESTONE_RESET_HW			= 1 << 3,
	RMGE_ATT_MILESTONE_REG_MAC			= 1 << 4,
	RMGE_ATT_MILESTONE_HW_RESET			= 1 << 5
} rmge_att_milestone;

typedef struct rmge_hw {
	rmge_hw_rev_t			rev;
	const char			*firmware_name;
	uint8_t				mac_addr[ETHERADDRL];
} rmge_hw_t;

typedef struct rmge {
	dev_info_t		*dip;
	int			instance;
	dev_t			dev;

	rmge_att_milestone	att_milestone;

	/* TODO: decide if we need to actually keep a config space handle */
	ddi_acc_handle_t	cfg_space_handle;
	caddr_t			bar2_mmio_addr;
	ddi_acc_handle_t	bar2_mmio_handle;

	mac_handle_t		mh;

	rmge_hw_t		hw;
} rmge_t;

int rmge_identify_device_particulars(rmge_t *);
int rmge_drive_to_reset(rmge_t *);
void rmge_fw_stub();

#ifdef __cplusplus
}
#endif

#endif /* _RMGE_H */
