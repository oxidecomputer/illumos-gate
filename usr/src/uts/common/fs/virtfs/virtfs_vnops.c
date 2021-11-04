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

#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/fs/virtfs_impl.h>

/*
 * XXX virtfs
 */

struct vnodeops *lo_vnodeops;

const fs_operation_def_t virtfs_vnodeops_template[] = {
	{ .name = NULL, .func = NULL },
};
