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

#ifndef _IPCC_CMD_H
#define	_IPCC_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#define	EXIT_USAGE	2

typedef struct ipcc_cmdtab {
	const char *ic_name;
	int (*ic_op)(int, char **);
	void (*ic_use)(FILE *);
} ipcc_cmdtab_t;

#ifdef __cplusplus
}
#endif

#endif /* _IPCC_CMD_H */
