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
 * Copyright 2021 Oxide Computer Company
 */

#include <regex.h>
#include <devfsadm.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/mkdev.h>

#define	TFPKT_DRIVER "tfpkt"

static int tfpkt(di_minor_t minor, di_node_t node);

/*
 * devfs create callback register
 */
static devfsadm_create_t tfpkt_create_cbt[] = {
	{ "pseudo", "ddi_pseudo", TFPKT_DRIVER,
	    TYPE_EXACT | DRV_EXACT, ILEVEL_0, tfpkt,
	},
};

DEVFSADM_CREATE_INIT_V0(tfpkt_create_cbt);

static int
tfpkt(di_minor_t minor, di_node_t node)
{
	char path[MAXPATHLEN];

	if (strcmp(di_minor_name(minor), TFPKT_DRIVER) == 0) {
		int instance = di_instance(node);
		(void) snprintf(path, sizeof (path), "tfpkt%d", instance);
		(void) devfsadm_mklink(path, node, minor, 0);
	}

	return (DEVFSADM_CONTINUE);
}
