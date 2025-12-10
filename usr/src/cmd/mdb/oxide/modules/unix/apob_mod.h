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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _APOB_MOD_H
#define	_APOB_MOD_H

#include <sys/x86_archext.h>

/*
 * APOB-related dcmds.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern void apob_set_target(x86_processor_family_t);

extern int apob_walk_init(mdb_walk_state_t *);
extern int apob_walk_step(mdb_walk_state_t *);

extern void apob_dcmd_help(void);
extern int apob_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int pmuerr_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void apob_event_dcmd_help(void);
extern int apob_event_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void apob_entry_dcmd_help(void);
extern int apob_entry_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int apob_target_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void apob_target_dcmd_help(void);

#define	APOB_DCMDS	\
	{ "apob", "?[-g group] [-t type]", "find APOB entries", apob_dcmd, \
	    apob_dcmd_help }, \
	{ "apob_entry", ":[-r|-x]", "display an APOB entry", apob_entry_dcmd, \
	    apob_entry_dcmd_help }, \
	{ "apob_event", "?[-c class -e event [-a payload 0] [-b payload 1]]", \
	    "decode the APOB event log", apob_event_dcmd, \
	    apob_event_dcmd_help }, \
	{ "apob_target", "[milan|genoa|turin]", \
	    "set the target CPU family used for APOB operations", \
	    apob_target_dcmd, apob_target_dcmd_help }, \
	{ "pmuerr", ":", "decode APOB PMU Training error data", pmuerr_dcmd }

#define	APOB_WALKERS	\
	{ "apob", "walk the APOB", apob_walk_init, apob_walk_step }

#ifdef __cplusplus
}
#endif

#endif /* _APOB_MOD_H */
