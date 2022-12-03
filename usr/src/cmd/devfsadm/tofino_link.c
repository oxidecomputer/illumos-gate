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
 * Copyright 2023 Oxide Computer Company
 */

#include <regex.h>
#include <devfsadm.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mkdev.h>

#define	TOFINO_DRIVER "tofino"

static int tofino(di_minor_t minor, di_node_t node);

/*
 * devfs create callback register
 */
static devfsadm_create_t tofino_create_cbt[] = {
	{ "pseudo", "ddi_pseudo", TOFINO_DRIVER,
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, tofino,
	},
};

DEVFSADM_CREATE_INIT_V0(tofino_create_cbt);

static devfsadm_remove_t tofino_remove_cbt[] = {
	{"pseudo", "^r?tofino/[0-9]+$", RM_ALWAYS | RM_POST | RM_HOT,
	    ILEVEL_0, devfsadm_rm_all},
};

DEVFSADM_REMOVE_INIT_V0(tofino_remove_cbt);

/*
 *	/dev/tofino -> /devices/pseudo/tofino@0:tofino
 */
static int
tofino(di_minor_t minor, di_node_t node)
{
	char path[PATH_MAX + 1];
	int instance = di_instance(node);

	if (strcmp(di_minor_name(minor), TOFINO_DRIVER) == 0) {
		(void) snprintf(path, sizeof (path), "%s/%d", TOFINO_DRIVER,
		    instance);
		(void) devfsadm_mklink(path, node, minor, 0);
	}

	return (DEVFSADM_CONTINUE);
}
