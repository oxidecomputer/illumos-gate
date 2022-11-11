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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright 2022 Oxide Computer Company
 */

/*
 * Maintain a ring buffer containing the log messages sent back by the kernel
 * IPCC framework. This can be inspected with mdb or dtrace.
 *
 * Print these messages by running:
 * mdb -ke ::ipcc_dbgmsg
 *
 * Monitor these messages by running:
 * dtrace -qn 'ipcc-dbgmsg{printf("%s\n", stringof(arg0))}'
 */

#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/time.h>
#include <sys/sdt.h>
#include <sys/kmem.h>
#include <sys/varargs.h>
#include <sys/stddef.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>

#include <ipcc_debug.h>

static list_t ipcc_dbgmsgs;
static uint_t ipcc_dbgmsg_size;
static kmutex_t ipcc_dbgmsgs_lock;
uint_t ipcc_dbgmsg_maxsize = 1 << 20;	/* 1 MiB */

void
ipcc_dbgmsg_init(void)
{
	list_create(&ipcc_dbgmsgs, sizeof (ipcc_dbgmsg_t),
	    offsetof(ipcc_dbgmsg_t, idm_node));
	mutex_init(&ipcc_dbgmsgs_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
ipcc_dbgmsg_fini(void)
{
	ipcc_dbgmsg_t *idm;

	while ((idm = list_remove_head(&ipcc_dbgmsgs)) != NULL) {
		size_t size = sizeof (ipcc_dbgmsg_t) + strlen(idm->idm_msg) + 1;
		kmem_free(idm, size);
		ipcc_dbgmsg_size -= size;
	}
	mutex_destroy(&ipcc_dbgmsgs_lock);
	ASSERT0(ipcc_dbgmsg_size);
}

/*
 * This function is used as the logging callback to the IPCC protocol framework
 * (see os/ipcc_proto.c). The type field indicates the type of message being
 * passed back - here everything gets added to the ring buffer regardless of
 * type.
 */
void
ipcc_dbgmsg(void *arg __unused, ipcc_log_type_t type __unused,
    const char *fmt, ...)
{
	size_t msgsize;
	va_list adx;
	ipcc_dbgmsg_t *idm;

	va_start(adx, fmt);
	msgsize = vsnprintf(NULL, 0, fmt, adx) + 1;
	va_end(adx);

	idm = kmem_alloc(sizeof (ipcc_dbgmsg_t) + msgsize, KM_SLEEP);
	idm->idm_timestamp = gethrestime_sec();
	idm->idm_hrtime = gethrtime();

	va_start(adx, fmt);
	(void) vsnprintf(idm->idm_msg, msgsize, fmt, adx);
	va_end(adx);

	DTRACE_PROBE1(ipcc__dbgmsg, char *, idm->idm_msg);

	mutex_enter(&ipcc_dbgmsgs_lock);
	list_insert_tail(&ipcc_dbgmsgs, idm);
	ipcc_dbgmsg_size += sizeof (ipcc_dbgmsg_t) + msgsize;
	while (ipcc_dbgmsg_size > ipcc_dbgmsg_maxsize) {
		size_t size = sizeof (ipcc_dbgmsg_t) + strlen(idm->idm_msg) + 1;
		idm = list_remove_head(&ipcc_dbgmsgs);
		kmem_free(idm, size);
		ipcc_dbgmsg_size -= size;
	}
	mutex_exit(&ipcc_dbgmsgs_lock);
}
