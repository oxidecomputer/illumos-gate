/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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

#define TOFINO_DRIVER "tofino"

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

/*
 *	/dev/tofino -> /devices/pseudo/tofino@0:tofino
 */
static int
tofino(di_minor_t minor, di_node_t node)
{
	if (strcmp(di_minor_name(minor), TOFINO_DRIVER) == 0)
		(void) devfsadm_mklink(TOFINO_DRIVER, node, minor, 0);

	return (DEVFSADM_CONTINUE);
}
