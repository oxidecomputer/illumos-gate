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

#ifndef	_FABRIC_H
#define	_FABRIC_H

#ifdef	__cplusplus
extern "C" {
#endif

extern int fabric_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_dcmd_help(void);

extern int fabric_ioms_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void fabric_ioms_dcmd_help(void);

extern int fabric_walk_init(mdb_walk_state_t *);
extern int fabric_walk_soc_step(mdb_walk_state_t *);
extern int fabric_walk_iodie_step(mdb_walk_state_t *);
extern int fabric_walk_nbio_step(mdb_walk_state_t *);
extern int fabric_walk_ioms_step(mdb_walk_state_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _FABRIC_H */
