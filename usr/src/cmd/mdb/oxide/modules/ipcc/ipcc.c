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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright 2020 Joyent, Inc.
 * Copyright 2022 Oxide Computer Company
 */

#include <mdb/mdb_ctf.h>
#include <sys/mdb_modapi.h>
#include <sys/list.h>

typedef struct {
	mdb_ctf_id_t	cb_id;
	ulong_t		cb_off;
	ulong_t		cb_tsoff;
	boolean_t	cb_initdone;
} callback_t;

static int
dbgmsg_cb(uintptr_t addr, const void *unknown, void *arg)
{
	callback_t *cb = arg;
	time_t timestamp;
	char buf[1024];

	if (!cb->cb_initdone) {
		if (mdb_ctf_lookup_by_name("ipcc_dbgmsg_t", &cb->cb_id) == -1) {
			mdb_warn("couldn't find struct ipcc_dbgmsg");
			return (WALK_ERR);
		}

		if (mdb_ctf_offsetof(cb->cb_id, "idm_msg", &cb->cb_off) == -1) {
			mdb_warn("couldn't find idm_msg");
			return (WALK_ERR);
		}
		cb->cb_off /= 8;

		if (mdb_ctf_offsetof(cb->cb_id, "idm_timestamp",
		    &cb->cb_tsoff) == -1) {
			mdb_warn("couldn't find idm_timestamp");
			return (WALK_ERR);
		}
		cb->cb_tsoff /= 8;

		cb->cb_initdone = TRUE;
	}

	if (mdb_vread(&timestamp, sizeof (timestamp),
	    addr + cb->cb_tsoff) == -1) {
		mdb_warn("failed to read idm_timestamp at %p\n",
		    addr + cb->cb_tsoff);
		return (DCMD_ERR);
	}

	if (mdb_readstr(buf, sizeof (buf), addr + cb->cb_off) == -1) {
		mdb_warn("failed to read idm_msg at %p\n",
		    addr + cb->cb_off);
		return (DCMD_ERR);
	}

	mdb_printf("%Y ", timestamp);
	mdb_printf("%s\n", buf);

	return (WALK_NEXT);
}

static int
dbgmsg(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	callback_t cb = { 0, };
	GElf_Sym sym;

	if (mdb_lookup_by_name("ipcc_dbgmsgs", &sym)) {
		mdb_warn("failed to find ipcc_dbgmsgs");
		return (DCMD_ERR);
	}

	if (mdb_pwalk("list", dbgmsg_cb, &cb, sym.st_value) != 0) {
		mdb_warn("can't walk ipcc_dbgmsgs");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

static const mdb_dcmd_t dcmds[] = {
	{ "ipcc_dbgmsg", "",
	    "print ipcc debug message log", dbgmsg},
	{ NULL }
};


static const mdb_modinfo_t modinfo = {
	.mi_dvers	= MDB_API_VERSION,
	.mi_dcmds	= dcmds,
	.mi_walkers	= NULL,
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
