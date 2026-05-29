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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _TARGET_H
#define	_TARGET_H

#include <sys/x86_archext.h>

/*
 * Tracking of the target AMD processor family, shared by the oxide unix and
 * apob mdb modules. See target.c.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern x86_processor_family_t oxide_mdb_target_family(void);
extern void oxide_mdb_set_target_family(x86_processor_family_t);

extern int oxide_mdb_target_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void oxide_mdb_target_dcmd_help(void);

#define	TARGET_DCMDS	\
	{ "target", "[milan|genoa|turin]", \
	    "set the target CPU family used for APOB and LTSSM decode", \
	    oxide_mdb_target_dcmd, oxide_mdb_target_dcmd_help }, \
	{ "apob_target", "[milan|genoa|turin]", "alias for ::target", \
	    oxide_mdb_target_dcmd, oxide_mdb_target_dcmd_help }

#ifdef __cplusplus
}
#endif

#endif /* _TARGET_H */
