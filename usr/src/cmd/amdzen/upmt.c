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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * A client for the upmt driver that can retrieve the PM log (metrics) table
 * from the SMU. As well as one-shot operations, it can sample the table
 * periodically, framing each sample with a small header.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libdevinfo.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <port.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ccompile.h>
#include <sys/mkdev.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <upmt.h>

#include "amdzen_common.h"

/*
 * The minimum sampling interval accepted by the dump subcommand. An interval
 * of 0 is also accepted and means that samples are taken as quickly as
 * possible.
 */
#define	UPMT_MIN_INTERVAL	0.001	/* seconds */

/*
 * Each sample written via the dump subcommand is framed with this header,
 * followed immediately by prh_len bytes of raw PM table data. All fields
 * are stored in the byte order of the host that wrote the file, which is
 * little-endian on all supported systems.
 */
typedef struct upmt_rec_hdr {
	uint32_t	prh_magic;	/* UPMT_REC_MAGIC */
	uint16_t	prh_hdrlen;	/* size of this header */
	uint8_t		prh_df;		/* DF (IO die) number */
	uint8_t		prh_pad;
	uint32_t	prh_version;	/* SMU table layout version */
	uint32_t	prh_len;	/* payload length in bytes */
	uint64_t	prh_time_sec;	/* wall clock at sample time */
	uint32_t	prh_time_nsec;
	uint32_t	prh_pad2;
} upmt_rec_hdr_t;

#define	UPMT_REC_MAGIC	0x57464d50	/* PMFW */

typedef struct upmt_dev {
	uint_t		ud_df;
	int		ud_fd;
} upmt_dev_t;

static const char *progname;
static const char *upmt_device;
static upmt_dev_t *upmt_devs;
static uint_t upmt_ndevs;

static volatile sig_atomic_t upmt_intr;

typedef struct upmt_cmdtab {
	const char	*uc_name;
	int		(*uc_op)(int, char **);
	void		(*uc_use)(FILE *);
} upmt_cmdtab_t;

/*
 * SIGINT is recorded in a flag and acted upon between samples, so that any
 * record being written when the signal arrives is emitted in full before the
 * dump loop terminates.
 */
static void
upmt_sigint(int sig __unused)
{
	upmt_intr = 1;
}

/*
 * Open the device(s) to operate on. With -d, the single given device is
 * used, otherwise a devinfo walk of the upmt driver's minors finds every
 * device. In both cases the DF number is recovered from the device's minor
 * number. The driver creates one minor per DF.
 */
static void
upmt_open_devices(void)
{
	if (upmt_device != NULL) {
		struct stat st;
		upmt_dev_t *ud;

		upmt_devs = calloc(1, sizeof (upmt_dev_t));
		if (upmt_devs == NULL)
			err(EXIT_FAILURE, "failed to allocate device state");
		ud = &upmt_devs[0];
		ud->ud_fd = amdzen_open_device("upmt", upmt_device, O_RDWR);
		if (ud->ud_fd < 0)
			exit(EXIT_FAILURE);
		if (fstat(ud->ud_fd, &st) != 0)
			err(EXIT_FAILURE, "failed to stat %s", upmt_device);
		ud->ud_df = minor(st.st_rdev);
		upmt_ndevs = 1;
		return;
	}

	di_node_t root, node;
	di_minor_t dmin;
	uint_t count = 0;

	root = di_init_driver("upmt", DINFOSUBTREE | DINFOMINOR);
	if (root == DI_NODE_NIL) {
		err(EXIT_FAILURE,
		    "failed to take a devinfo snapshot with upmt attached");
	}

	/* upmt is a single-instance driver with one minor per DF */
	node = di_drv_first_node("upmt", root);
	if (node == DI_NODE_NIL)
		errx(EXIT_FAILURE, "no upmt devices found");

	dmin = DI_MINOR_NIL;
	while ((dmin = di_minor_next(node, dmin)) != DI_MINOR_NIL)
		count++;
	if (count == 0)
		errx(EXIT_FAILURE, "no upmt devices found");

	upmt_devs = calloc(count, sizeof (upmt_dev_t));
	if (upmt_devs == NULL)
		err(EXIT_FAILURE, "failed to allocate device state");

	dmin = DI_MINOR_NIL;
	while ((dmin = di_minor_next(node, dmin)) != DI_MINOR_NIL) {
		upmt_dev_t *ud = &upmt_devs[upmt_ndevs];
		char *bpath, *path;

		bpath = di_devfs_minor_path(dmin);
		if (bpath == NULL) {
			err(EXIT_FAILURE, "failed to get minor path for %s",
			    di_minor_name(dmin));
		}
		if (asprintf(&path, "/devices%s", bpath) < 0)
			err(EXIT_FAILURE, "failed to construct full path");
		di_devfs_path_free(bpath);

		if ((ud->ud_fd = open(path, O_RDWR)) < 0)
			err(EXIT_FAILURE, "failed to open %s", path);
		free(path);
		ud->ud_df = minor(di_minor_devt(dmin));
		upmt_ndevs++;
	}

	di_fini(root);
}

static int
upmt_cmd_version(int argc, char **argv __unused)
{
	if (argc != 0)
		errx(EXIT_USAGE, "version takes no arguments");

	for (uint_t i = 0; i < upmt_ndevs; i++) {
		const upmt_dev_t *ud = &upmt_devs[i];
		upmt_info_t ui;

		if (ioctl(ud->ud_fd, UPMT_INFO, &ui) != 0) {
			warn("failed to retrieve info for DF %u", ud->ud_df);
			continue;
		}
		(void) printf("DF %u: table version 0x%x, %llu bytes\n",
		    ud->ud_df, ui.ui_version, (u_longlong_t)ui.ui_len);
	}

	return (EXIT_SUCCESS);
}

static int
upmt_cmd_refresh(int argc, char **argv __unused)
{
	int ret = EXIT_SUCCESS;

	if (argc != 0)
		errx(EXIT_USAGE, "refresh takes no arguments");

	for (uint_t i = 0; i < upmt_ndevs; i++) {
		const upmt_dev_t *ud = &upmt_devs[i];

		if (ioctl(ud->ud_fd, UPMT_REFRESH, 0) != 0) {
			warn("refresh failed for DF %u", ud->ud_df);
			ret = EXIT_FAILURE;
		}
	}

	return (ret);
}

static void
upmt_dump_usage(FILE *f)
{
	(void) fprintf(f,
	    "\tdump -o file [-i seconds] [-c count] [-s df]...\n"
	    "\t    an interval of 0 means sample as fast as possible\n");
}

static bool
upmt_write_rec(FILE *fp, uint_t df, uint32_t version, const void *buf,
    size_t len)
{
	upmt_rec_hdr_t hdr = { 0 };
	struct timespec ts;

	(void) clock_gettime(CLOCK_REALTIME, &ts);
	hdr.prh_magic = UPMT_REC_MAGIC;
	hdr.prh_hdrlen = sizeof (hdr);
	hdr.prh_df = (uint8_t)df;
	hdr.prh_version = version;
	hdr.prh_len = len;
	hdr.prh_time_sec = ts.tv_sec;
	hdr.prh_time_nsec = ts.tv_nsec;

	if (fwrite(&hdr, sizeof (hdr), 1, fp) != 1 ||
	    fwrite(buf, len, 1, fp) != 1) {
		warn("failed to write record");
		return (false);
	}

	return (true);
}

/*
 * Wait for the next firing of the sampling timer, reporting any missed
 * firings. Returns false if SIGINT arrived instead.
 */
static bool
upmt_wait_tick(int port)
{
	port_event_t pe;

	for (;;) {
		if (port_get(port, &pe, NULL) == 0)
			break;
		if (errno != EINTR)
			err(EXIT_FAILURE, "failed to wait for timer");
		if (upmt_intr != 0)
			return (false);
	}

	if (pe.portev_events > 1) {
		warnx("missed %d sample%s", pe.portev_events - 1,
		    pe.portev_events == 2 ? "" : "s");
	}

	return (true);
}

static int
upmt_cmd_dump(int argc, char **argv)
{
	bool *selected;
	bool anysel = false;
	const char *outfile = NULL;
	const char *errstr;
	double interval = 1.0;
	ulong_t count = 1;
	size_t buflen = 0;
	timer_t tid = 0;
	uint8_t *buf;
	FILE *fp;
	char *eptr;
	int port = -1;
	int c;

	selected = calloc(upmt_ndevs, sizeof (bool));
	if (selected == NULL)
		err(EXIT_FAILURE, "failed to allocate selection state");

	while ((c = getopt(argc, argv, ":c:i:o:s:")) != -1) {
		switch (c) {
		case 'c':
			count = (ulong_t)strtonum(optarg, 0, LLONG_MAX,
			    &errstr);
			if (errstr != NULL) {
				errx(EXIT_USAGE, "count is %s: %s", errstr,
				    optarg);
			}
			break;
		case 'i':
			interval = strtod(optarg, &eptr);
			if (!isdigit((uchar_t)optarg[0]) || *eptr != '\0' ||
			    !(interval >= 0.0 &&
			    interval <= (double)NANOSEC)) {
				errx(EXIT_USAGE, "invalid interval: %s",
				    optarg);
			}
			if (interval != 0.0 && interval < UPMT_MIN_INTERVAL) {
				errx(EXIT_USAGE,
				    "interval must be 0 or at least %g "
				    "seconds", UPMT_MIN_INTERVAL);
			}
			break;
		case 'o':
			outfile = optarg;
			break;
		case 's': {
			long long df;
			bool found = false;

			df = strtonum(optarg, 0, UINT8_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_USAGE, "DF is %s: %s", errstr,
				    optarg);
			}
			for (uint_t i = 0; i < upmt_ndevs; i++) {
				if (upmt_devs[i].ud_df == (uint_t)df) {
					selected[i] = true;
					found = true;
				}
			}
			if (!found) {
				errx(EXIT_FAILURE, "no device for DF %s",
				    optarg);
			}
			anysel = true;
			break;
		}
		case ':':
			errx(EXIT_USAGE, "option -%c requires an argument",
			    optopt);
		default:
			errx(EXIT_USAGE, "unknown option: -%c", optopt);
		}
	}

	if (argc - optind != 0)
		errx(EXIT_USAGE, "unexpected arguments after options");

	for (uint_t i = 0; i < upmt_ndevs; i++) {
		upmt_info_t ui;

		if (!anysel)
			selected[i] = true;
		if (selected[i] &&
		    ioctl(upmt_devs[i].ud_fd, UPMT_INFO, &ui) == 0 &&
		    ui.ui_len > buflen) {
			buflen = ui.ui_len;
		}
	}

	if (buflen == 0)
		errx(EXIT_FAILURE, "no selected DF has a PM table");

	if ((buf = malloc(buflen)) == NULL)
		err(EXIT_FAILURE, "failed to allocate %zu bytes", buflen);

	if (outfile == NULL)
		errx(EXIT_USAGE, "an output file must be specified with -o");
	if ((fp = fopen(outfile, "w")) == NULL)
		err(EXIT_FAILURE, "failed to open %s", outfile);
	if (isatty(fileno(fp)) != 0) {
		errx(EXIT_FAILURE,
		    "refusing to write binary data to a terminal");
	}

	(void) sigset(SIGINT, upmt_sigint);

	if (interval > 0.0 && count != 1) {
		port_notify_t pn;
		struct sigevent sev;
		struct itimerspec its;
		double whole, frac;

		if ((port = port_create()) < 0)
			err(EXIT_FAILURE, "failed to create event port");
		pn.portnfy_port = port;
		pn.portnfy_user = NULL;
		(void) memset(&sev, 0, sizeof (sev));
		sev.sigev_notify = SIGEV_PORT;
		sev.sigev_value.sival_ptr = &pn;
		if (timer_create(CLOCK_HIGHRES, &sev, &tid) != 0)
			err(EXIT_FAILURE, "failed to create timer");
		frac = modf(interval, &whole);
		its.it_value.tv_sec = (time_t)whole;
		its.it_value.tv_nsec = (long)(frac * (double)NANOSEC);
		its.it_interval = its.it_value;
		if (timer_settime(tid, 0, &its, NULL) != 0)
			err(EXIT_FAILURE, "failed to arm timer");
	}

	for (ulong_t iter = 0; count == 0 || iter < count; iter++) {
		if (iter > 0 && port >= 0 && !upmt_wait_tick(port))
			break;
		if (upmt_intr != 0)
			break;

		for (uint_t i = 0; i < upmt_ndevs; i++) {
			const upmt_dev_t *ud = &upmt_devs[i];
			upmt_read_t ur;

			if (!selected[i])
				continue;

			if (ioctl(ud->ud_fd, UPMT_REFRESH, 0) != 0) {
				warn("refresh failed for DF %u", ud->ud_df);
				continue;
			}

			(void) memset(&ur, 0, sizeof (ur));
			ur.ur_buf = (uint64_t)(uintptr_t)buf;
			ur.ur_len = buflen;

			if (ioctl(ud->ud_fd, UPMT_READ, &ur) != 0) {
				warn("failed to read PM table for DF %u",
				    ud->ud_df);
				continue;
			}

			if (!upmt_write_rec(fp, ud->ud_df, ur.ur_version, buf,
			    (size_t)ur.ur_len)) {
				return (EXIT_FAILURE);
			}
		}

		if (fflush(fp) != 0)
			err(EXIT_FAILURE, "failed to write output");
	}

	if (port >= 0) {
		(void) timer_delete(tid);
		(void) close(port);
	}
	free(buf);

	if (fflush(fp) != 0 || fclose(fp) != 0)
		err(EXIT_FAILURE, "failed to write output");

	return (EXIT_SUCCESS);
}

static const upmt_cmdtab_t upmt_cmds[] = {
	{ "version", upmt_cmd_version, NULL },
	{ "refresh", upmt_cmd_refresh, NULL },
	{ "dump", upmt_cmd_dump, upmt_dump_usage },
	{ NULL, NULL, NULL }
};

static void
upmt_usage(const char *format, ...)
{
	if (format != NULL) {
		va_list ap;

		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage: %s [-d device] <subcommand> ...\n"
	    "Available subcommands:\n", progname);

	for (uint32_t cmd = 0; upmt_cmds[cmd].uc_name != NULL; cmd++) {
		if (upmt_cmds[cmd].uc_use != NULL) {
			upmt_cmds[cmd].uc_use(stderr);
		} else {
			(void) fprintf(stderr, "\t%s\n",
			    upmt_cmds[cmd].uc_name);
		}
	}
}

int
main(int argc, char **argv)
{
	uint32_t cmd;
	int c;

	progname = basename(argv[0]);

	while ((c = getopt(argc, argv, "d:")) != -1) {
		switch (c) {
		case 'd':
			upmt_device = optarg;
			break;
		default:
			upmt_usage(NULL);
			return (EXIT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		upmt_usage("missing required sub-command");
		return (EXIT_USAGE);
	}

	for (cmd = 0; upmt_cmds[cmd].uc_name != NULL; cmd++) {
		if (strcmp(argv[0], upmt_cmds[cmd].uc_name) == 0)
			break;
	}

	if (upmt_cmds[cmd].uc_name == NULL) {
		upmt_usage("unknown sub-command: %s", argv[0]);
		return (EXIT_USAGE);
	}

	upmt_open_devices();

	argc--;
	argv++;
	optind = 0;

	return (upmt_cmds[cmd].uc_op(argc, argv));
}
