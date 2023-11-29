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
#include <kstat.h>
#include <libgen.h>
#include <libzonecfg.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmpx.h>
#include <zone.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/fcntl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#define	EXIT_USAGE	2

const char *progname;

static void __NORETURN
usage(int ec)
{
	fprintf(stderr,
	    "Updates user accounting databases, adjusting the timestamps of \n"
	    "records therein.\n\n"
	    "Usage:\n"
	    "    %1$s -Z\n"
	    "        Update the databases within all running zones, using\n"
	    "        each zone's boot time as a reference.\n"
	    "    %1$s -z <zone>\n"
	    "        Updates the databases within the specified <zone>, using\n"
	    "        the zone's boot time as a reference.\n"
	    "    %1$s <epoch seconds> <path>\n"
	    "        Updates the database at <path> using the provided\n"
	    "        <epoch seconds> as the system boot time.\n",
	    progname);

	exit(ec);
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

static void
process_database(const char *dbfile, const char *dir, time_t ts)
{
	struct utmpx *u;
	time_t start;

	/*
	 * Unfortunately, utmpxname(3C) and friends only work with file paths
	 * up to 78 characters in length and the paths to zone roots can easily
	 * exceed this. If a directory name is provided, chdir() and use the
	 * provided relative path.
	 */
	if (dir != NULL) {
		VERIFY(*dir == '/' && *dbfile != '/');
		if (chdir(dir) != 0) {
			err(EXIT_FAILURE, "Could not change directory to '%s'",
			    dir);
		}
		printf("Updating database '%s/%s'\n", dir, dbfile);
	} else {
		printf("Updating database '%s'\n", dbfile);
	}

	if (access(dbfile, R_OK | W_OK) == -1)
		err(EXIT_FAILURE, dbfile);

	if (utmpxname(dbfile) == 0) {
		errx(EXIT_FAILURE, "Invalid database: '%s' (see utmpxname(3C))",
		    dbfile);
	}

	u = getutxent();
	if (u == NULL) {
		printf("Database is empty\n");
		goto out;
	}
	start = u->ut_tv.tv_sec;

	if (start == ts) {
		printf("First entry looks correct; nothing to do\n");
		goto out;
	}

	/*
	 * If the database contains more than one "system boot" record, we
	 * leave it alone. We only update pristine databases, not those which
	 * have persisted through a reboot (such as wtmpx in a non-global zone).
	 */
	bool boot_seen = false;
	do {
		if (strcmp(u->ut_line, BOOT_MSG) != 0)
			continue;
		if (boot_seen) {
			printf("Multiple boots seen in database; skipping\n");
			goto out;
		}
		boot_seen = true;
	} while ((u = getutxent()) != NULL);

	setutxent();
	while ((u = getutxent()) != NULL) {
		print_entry(u, '<');
		u->ut_tv.tv_sec -= start;
		u->ut_tv.tv_sec += ts;
		u = pututxline(u);
		assert(u != NULL);
		print_entry(u, '>');
	}

out:
	if (dir != NULL)
		(void) chdir("/");
	endutxent();
}

static time_t
boot_time(zoneid_t zid)
{
	kstat_ctl_t *kc;
	kstat_t *ks;
	kstat_named_t *bootv;
	time_t boot;

	kc = kstat_open();
	if (kc == NULL)
		err(EXIT_FAILURE, "Failed to open kstat interface");

	ks = kstat_lookup(kc, "zones", zid, NULL);
	if (ks == NULL)
		err(EXIT_FAILURE, "Failed to fetch zones kstat");

	if (kstat_read(kc, ks, NULL) == -1)
		err(EXIT_FAILURE, "Failed to read zones kstat");

	bootv = kstat_data_lookup(ks, "boot_time");
	if (bootv == NULL)
		err(EXIT_FAILURE, "Failed to retrieve boot_time");

	boot = bootv->value.ui64;

	VERIFY0(kstat_close(kc));

	return (boot);
}

static void
process_zone(char *zonename)
{
	zoneid_t zid;
	time_t boot_ts;
	char zoneroot[MAXPATHLEN] = "/";
	static char *files[] = { "var/adm/wtmpx", "var/adm/utmpx" };

	zid = getzoneidbyname(zonename);

	if (zid == -1)
		errx(EXIT_FAILURE, "Can not look up zone '%s'", zonename);

	printf("[zone:%s:%d]\n", zonename, zid);
	boot_ts = boot_time(zid);
	printf("Boot time: %ld\n", boot_ts);

	if (zid != GLOBAL_ZONEID) {
		int ret;

		ret = zone_get_rootpath(zonename, zoneroot, sizeof (zoneroot));
		if (ret != Z_OK) {
			errx(EXIT_FAILURE,
			    "Failed to retrieve zone root path for '%s': %s",
			    zonename, zonecfg_strerror(ret));
		}
	}
	printf("Root path: %s\n", zoneroot);

	for (uint_t i = 0; i < ARRAY_SIZE(files); i++)
		process_database(files[i], zoneroot, boot_ts);
}

static void
process_zones(void)
{
	uint_t nzids, nzids_alloced;
	zoneid_t *zids;

	if (zone_list(NULL, &nzids) != 0)
		err(EXIT_FAILURE, "failed to get zone ID list");

	if (nzids == 0) {
		printf("No zones in system\n");
		return;
	}

	zids = NULL;
	nzids_alloced = 0;
	do {
		zoneid_t *newzids;

		newzids = recallocarray(zids, nzids_alloced,
		    nzids, sizeof (zoneid_t));

		if (newzids == NULL) {
			free(zids);
			err(EXIT_FAILURE,
			    "failed to allocate memory for zone list");
		}

		zids = newzids;
		nzids_alloced = nzids;

		if (zone_list(zids, &nzids) != 0)
			err(EXIT_FAILURE, "failed to get zone list");
	} while (nzids != nzids_alloced);

	for (uint_t i = 0; i < nzids; i++) {
		char name[ZONENAME_MAX];

		if (getzonenamebyid(zids[i], name, sizeof (name)) < 0) {
			/*
			 * The zone may have shut down since we retrieved
			 * the list.
			 */
			continue;
		}
		process_zone(name);
	}

	free(zids);
}

int
main(int argc, char **argv)
{
	const char *errstr;
	char *zonename = NULL;
	zoneid_t zid;
	int c;

	/*
	 * pututxline(3C) is not privilege aware, it just checks for an
	 * effective UID of 0 when deciding whether to invoke the setuid
	 * helper. Going via the helper is no good to us as we need to be able
	 * to rewrite all of the records in the database and the helper
	 * restricts what can be done. Perform the same euid check to confirm
	 * that we won't use the helper and then the access(2) call on each
	 * database file checks that we actually have privileges to write to
	 * the file (that is not a guarantee for euid 0 - privileges could have
	 * been removed).
	 */
	if (geteuid() != 0)
		errx(EXIT_FAILURE, "This program must be run as root");

	zid = getzoneid();

	progname = basename(argv[0]);

	while ((c = getopt(argc, argv, ":hz:Z")) != -1) {
		switch (c) {
		case 'h':
			usage(EXIT_SUCCESS);
			break;
		case 'z':
			if (zid != GLOBAL_ZONEID) {
				errx(EXIT_FAILURE, "The -z option can only be "
				    "used in the global zone");
			}
			zonename = optarg;
			break;
		case 'Z':
			if (zid != GLOBAL_ZONEID) {
				errx(EXIT_FAILURE, "The -Z option can only be "
				    "used in the global zone");
			}
			process_zones();
			return (EXIT_SUCCESS);
		case '?':
			fprintf(stderr, "Unknown option: -%c\n", optopt);
			usage(EXIT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (zonename == NULL) {
		time_t ts;

		if (argc != 2) {
			fprintf(stderr, "Missing parameters\n\n");
			usage(EXIT_USAGE);
		}
		ts = strtonum(argv[0], 1, LONG_MAX, &errstr);
		if (errstr != NULL) {
			errx(EXIT_FAILURE, "epoch timestamp is %s: %s",
			    errstr, argv[0]);
		}
		process_database(argv[1], NULL, ts);
	} else {
		if (argc > 0) {
			fprintf(stderr, "Unexpected additional arguments found "
			    "starting with '%s'\n\n", argv[0]);
			usage(EXIT_USAGE);
		}

		process_zone(zonename);
	}

	return (0);
}
