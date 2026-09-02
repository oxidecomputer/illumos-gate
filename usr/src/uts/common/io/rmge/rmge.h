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

#ifdef __cplusplus
extern "C" {
#endif

#define	RMGE_DRIVER_NAME	"rmge"
#define	RMGE_REGS_MAP		2

#define	RMGE_SUCCESS			DDI_SUCCESS
#define	RMGE_FAILURE			DDI_FAILURE

typedef enum {
	RMGE_ATT_MILESTONE_START		= 1 << 0,
	RMGE_ATT_MILESTONE_CSRS			= 1 << 1
} rmge_att_milestone;

typedef struct rmge {
	dev_info_t		*devinfo;
	int			instance;
	ddi_acc_handle_t	cfg_space_handle;
	caddr_t			regmap_handle;
	rmge_att_milestone	att_milestone;
} rmge_t;

#ifdef __cplusplus
}
#endif

#endif /* _RMGE_H */
