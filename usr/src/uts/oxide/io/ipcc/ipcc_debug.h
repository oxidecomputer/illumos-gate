/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _IPCC_DEBUG_H
#define	_IPCC_DEBUG_H

#include <sys/ipcc_proto.h>
#include <ipcc_drv.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct ipcc_dbgmsg {
	list_node_t idm_node;
	time_t idm_timestamp;
	hrtime_t idm_hrtime;
	char idm_msg[];
} ipcc_dbgmsg_t;

extern void ipcc_dbgmsg_init(void);
extern void ipcc_dbgmsg_fini(void);
extern void ipcc_dbgmsg(void *, ipcc_log_type_t, const char *fmt, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _IPCC_DEBUG_H */
