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

/*
 * Update a utmpx/wtmpx database using a new base timestamp, to correct the
 * recorded boot time and early accounting records after the system time is
 * synchronised.
 */

#include <assert.h>
#include <err.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ccompile.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <utmpx.h>

#define	EXIT_USAGE	2

const char *progname;

static void __NORETURN
usage(void)
{
	fprintf(stderr, "Usage: %s <epoch seconds> <path>\n\n"
	    "Updates the user accounting database at <path>, and adjusts\n"
	    "the timestamps of records therein, using the provided\n"
	    "<epoch seconds> as the boot time.\n", progname);

	exit(EXIT_USAGE);
}

static void
print_entry(struct utmpx *u, char context)
{
	char tmbuf[64];
	struct tm *tm;
	size_t l;
	time_t t;

	t = u->ut_tv.tv_sec;
	tm = localtime(&t);
	l = strftime(tmbuf, sizeof (tmbuf), "%Y-%m-%d %H:%M:%S", tm);
	assert(l > 0);
	printf("%c %s@%s - %s.%06ld\n", context,
	    u->ut_user, u->ut_line, tmbuf, u->ut_tv.tv_usec);
}

int
main(int argc, char **argv)
{
	const char *errstr;
	struct utmpx *u;
	time_t ts, start;

	progname = basename(argv[0]);

	if (argc != 3)
		usage();

	ts = strtonum(argv[1], 1, LONG_MAX, &errstr);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "epoch timestamp is %s: %s",
		    errstr, argv[1]);
	}

	/*
	 * pututxline(3C) is not privilege aware, it just checks for an
	 * effective UID of 0 when deciding whether to invoke the setuid
	 * helper. Going via the helper is no good to us as we need to be able
	 * to rewrite all of the records in the database and the helper
	 * restricts what can be done. Perform the same euid check to confirm
	 * that we won't use the helper and then the access(2) call below
	 * checks that we actually have privileges to write to the file (that
	 * is not a guarantee for euid 0 - privileges could have been removed).
	 */
	if (geteuid() != 0)
		errx(EXIT_FAILURE, "This program must be run as root");

	if (access(argv[2], R_OK | W_OK) == -1)
		err(EXIT_FAILURE, argv[2]);

	if (utmpxname(argv[2]) == 0) {
		errx(EXIT_FAILURE, "Invalid path: '%s' (see utmpxname(3C))",
		    argv[2]);
	}

	u = getutxent();
	if (u == NULL)
		return (0);
	start = u->ut_tv.tv_sec;
	do {
		/* Only adjust records from within the first hour after boot. */
		if (u->ut_tv.tv_sec - start > 3600) {
			print_entry(u, ' ');
		} else {
			print_entry(u, '<');
			u->ut_tv.tv_sec -= start;
			u->ut_tv.tv_sec += ts;
			u = pututxline(u);
			assert(u != NULL);
			print_entry(u, '>');
		}
	} while ((u = getutxent()) != NULL);

	endutxent();

	return (0);
}
