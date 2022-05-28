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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _T6MFGADM_H
#define	_T6MFGADM_H

/*
 * Internal, common definitions for t6mfgadm.
 */

#include <libt6mfg.h>
#include <stdio.h>
#include <sys/ccompile.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	EXIT_USAGE	2

typedef struct t6mfgadm_cmdtab {
	const char *tc_name;
	int (*tc_op)(int, char *[]);
	void (*tc_use)(FILE *);
} t6mfgadm_cmdtab_t;

extern const char *t6mfgadm_progname;
extern t6_mfg_t *t6mfg;

extern void t6mfgadm_err(const char *, ...) __NORETURN __PRINTFLIKE(1);
extern void t6mfgadm_ofmt_errx(const char *, ...);
extern int32_t t6mfgadm_device_parse(const char *);

extern void t6mfgadm_srom_usage(FILE *);
extern int t6mfgadm_srom(int, char *[]);

extern void t6mfgadm_flash_usage(FILE *);
extern int t6mfgadm_flash(int, char *[]);

extern int t6mfgadm_walk_tab(const t6mfgadm_cmdtab_t *, int, char *argv[]);
extern void t6mfgadm_usage(const t6mfgadm_cmdtab_t *, const char *, ...);

extern t6_mfg_source_t t6mfgadm_setup_source(const char *, const char *,
    boolean_t, boolean_t);

typedef struct {
	t6_mfg_source_t ti_source;
	int32_t ti_dev;
	const char *ti_file;
} t6mfgadm_info_t;

extern void t6mfgadm_dev_read_setup(const char *, int, char *[],
    t6mfgadm_info_t *);

extern void t6mfgadm_progress_cb(const t6_mfg_progress_t *, void *);

#ifdef __cplusplus
}
#endif

#endif /* _T6MFGADM_H */
