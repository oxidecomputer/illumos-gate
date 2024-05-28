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
 * Copyright 2024 Oxide Computer Company
 */

#ifndef _ZEN_KMDB_H
#define	_ZEN_KMDB_H

/*
 * kmdb specific routines for use in getting at AMD Zen related functionality.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef	_KMDB

extern int rdpcicfg_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int wrpcicfg_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

extern void rddf_dcmd_help(void);
extern void wrdf_dcmd_help(void);
extern int rddf_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int wrdf_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

extern void rdsmn_dcmd_help(void);
extern void wrsmn_dcmd_help(void);
extern int rdsmn_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int wrsmn_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

extern boolean_t df_props_init(void);

extern void df_route_dcmd_help(void);
extern int df_route_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

extern void dimm_report_dcmd_help(void);
extern int dimm_report_dcmd(uintptr_t, uint_t, int, const mdb_arg_t *);

#endif	/* _KMDB */

#ifdef __cplusplus
}
#endif

#endif /* _ZEN_KMDB_H */
