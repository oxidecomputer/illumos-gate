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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mc.h>
#include <sys/mc_amdzen.h>

#define	EXIT_USAGE	2

typedef enum spd_dump_err {
	SDE_OK,
	SDE_NO_SPD,
	SDE_INVALID_SPD,
	SDE_DIMM_OR_CHAN_NOT_PRESENT,
	SDE_IO_ERROR,
} spd_dump_err_t;

static spd_dump_err_t
get_spd_data(int fd, uint8_t chan, uint8_t dimm, uint8_t **spd, size_t *spd_sz)
{
	mc_get_data_t data = {
		.mgd_type = MDT_SPD,
		.mgd_chan = chan,
		.mgd_dimm = dimm,
		.mgd_size = 0,
	};
	int ret;

	*spd = NULL;
	*spd_sz = 0;

	do {
		if ((ret = ioctl(fd, MC_IOC_GET_DATA, &data)) == 0)
			break;
	} while (errno == EINTR);
	if (ret == -1) {
		errx(EXIT_FAILURE, gettext("initial MC_IOC_GET_DATA failed for "
		    "Channel %u DIMM %u: %s (%d)"), chan, dimm,
		    strerrorname_np(errno), errno);
	}

	switch (data.mgd_error) {
	case MGD_OK:
		err(EXIT_FAILURE, gettext("unexpected success getting SPD "
		    "data size (0x%zx bytes) for Channel %u DIMM %u"),
		    data.mgd_size, chan, dimm);

	case MGD_NO_DATA:
		/* Valid location but no SPD present. */
		return (SDE_NO_SPD);

	case MGD_INVALID_SIZE:
		/*
		 * Check the returned size against a conservative upper bound.
		 * JEDEC currently specifies up to 2 KiB for DDR5.
		 */
		if (data.mgd_size > 4096) {
			warnx(gettext("got unexpectedly large SPD data "
			    "(0x%zx bytes) for Channel %u DIMM %u"),
			    data.mgd_size, chan, dimm);
			return (SDE_INVALID_SPD);
		}
		break;

	case MGD_INVALID_CHAN:
		errx(EXIT_FAILURE, gettext("invalid channel: %u"), chan);
	case MGD_INVALID_DIMM:
		errx(EXIT_FAILURE, gettext("invalid dimm: %u"), dimm);

	case MGD_CHAN_EMPTY:
	case MGD_DIMM_NOT_PRESENT:
		return (SDE_DIMM_OR_CHAN_NOT_PRESENT);

	case MGD_INVALID_TYPE:
	default:
		abort();
	}

	data.mgd_addr = (uintptr_t)malloc(data.mgd_size);
	if (data.mgd_addr == (uintptr_t)NULL) {
		errx(EXIT_FAILURE, gettext("failed to alloc 0x%zu bytes for "
		    "Channel %u DIMM %u SPD data"), data.mgd_size, chan, dimm);
	}

	do {
		if ((ret = ioctl(fd, MC_IOC_GET_DATA, &data)) == 0)
			break;
	} while (errno == EINTR);
	if (ret == -1) {
		errx(EXIT_FAILURE, gettext("MC_IOC_GET_DATA failed for "
		    "Channel %u DIMM %u: %s (%d)"), chan, dimm,
		    strerrorname_np(errno), errno);
	}

	/*
	 * Any other result should've caused us to bail on the first pass.
	 */
	if (data.mgd_error != MGD_OK) {
		errx(EXIT_FAILURE, gettext("failed to get SPD for Channel %u "
		    "DIMM %u: %u"), chan, dimm, data.mgd_error);
	}

	*spd = (uint8_t *)data.mgd_addr;
	*spd_sz = data.mgd_size;

	return (SDE_OK);
}

static spd_dump_err_t
dump_spd(int fd, uint8_t chan, uint8_t dimm, const char *out)
{
	spd_dump_err_t err;
	int spd_fd = -1;
	uint8_t *spd = NULL;
	size_t spd_sz, off;

	if ((err = get_spd_data(fd, chan, dimm, &spd, &spd_sz)) != SDE_OK)
		goto out;

	do {
		spd_fd = open(out, O_CREAT | O_WRONLY | O_TRUNC);
	} while (spd_fd == -1 && errno == EINTR);
	if (spd_fd == -1) {
		warn("failed to open %s to write SPD", out);
		err = SDE_IO_ERROR;
		goto out;
	}

	off = 0;
	while (spd_sz > 0) {
		ssize_t written = write(spd_fd, spd + off, spd_sz);
		if (written == -1) {
			if (errno == EINTR)
				continue;
			warn("failed to write 0x%zx bytes of SPD at 0x%zx to "
			    "%s", spd_sz, off, out);
			err = SDE_IO_ERROR;
			goto out;
		}
		off += written;
		spd_sz -= written;
	}

out:
	if (spd_fd != -1)
		(void) close(spd_fd);
	if (spd != NULL)
		free(spd);

	return (err);
}

/*PRINTFLIKE1*/
static void __NORETURN
usage(const char *format, ...)
{
	va_list	ap;

	if (format != NULL) {
		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
		(void) fputc('\n', stderr);
	}

	(void) fprintf(stderr, "Usage: "
	    "\tspddump [-c <channel> -d <dimm> -o <output file>] <mc-dev-path>"
	    "\n\n"
	    "\tOmitting the channel, dimm and output file will instead cause\n"
	    "\tthe SPD data for all present DIMMs to be dumped in the current\n"
	    "\tdirectory as 'CHXX-DIMMYY.spd.bin' (XX - Channel, YY - DIMM)."
	    "\n");
	exit(EXIT_USAGE);
}

int
main(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;
	int fd, c;
	char *devpath;
	const char *errstr;
	uint8_t chan = UINT8_MAX;
	uint8_t dimm = UINT8_MAX;
	char *output = NULL;
	bool chan_set = false;
	bool dimm_set = false;
	bool output_set = false;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "c:d:o:")) != EOF) {
		switch (c) {
		case 'c':
			chan = (uint8_t)strtonumx(optarg, 0, UINT8_MAX,
			    &errstr, 0);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "channel is %s: %s", errstr,
				    optarg);
			}
			chan_set = true;
			break;
		case 'd':
			dimm = (uint8_t)strtonumx(optarg, 0, UINT8_MAX,
			    &errstr, 0);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "dimm is %s: %s", errstr,
				    optarg);
			}
			dimm_set = true;
			break;
		case 'o':
			output = optarg;
			output_set = true;
			break;
		case '?':
		default:
			usage(NULL);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage("missing required mc device path");
	} else if (argc > 1) {
		usage("invalid argument: %s", argv[1]);
	}

	devpath = argv[0];
	if ((fd = open(devpath, O_RDONLY)) == -1) {
		err(EXIT_FAILURE, gettext("failed to open %s"), devpath);
	}

	if ((chan_set != dimm_set) || (dimm_set != output_set)) {
		usage("Channel, DIMM and output file must all be set together");
	}

	if (output_set) {
		/* SPD for a single DIMM requested */

		switch (dump_spd(fd, chan, dimm, output)) {
		case SDE_OK:
			break;
		case SDE_NO_SPD:
			errx(EXIT_FAILURE,
			    "SPD not found for Channel %u DIMM %u", chan, dimm);
		case SDE_DIMM_OR_CHAN_NOT_PRESENT:
			errx(EXIT_FAILURE, "did not find Channel %u DIMM %u",
			    chan, dimm);
		case SDE_INVALID_SPD:
		case SDE_IO_ERROR:
			/*
			 * An error message has already been written to stderr
			 * for these so just bail at this point.
			 */
			exit(EXIT_FAILURE);
		default:
			abort();
		}
	} else {
		/* Write out SPD for all present DIMMs */

		for (uint8_t c = 0; c < MC_ZEN_MAX_CHANS; c++) {
			for (uint8_t d = 0; d < MC_ZEN_MAX_DIMMS; d++) {
				char out[PATH_MAX];
				int n;

				n = snprintf(out, sizeof (out),
				    "CH%02u-DIMM%u.spd.bin", c, d);
				if (n < 0) {
					warn("failed to format SPD output file "
					    "for Channel %u DIMM %u", c, d);
					continue;
				} else if (n >= sizeof (out)) {
					warnx("buffer too small to format SPD "
					    "output file for Channel %u DIMM %u"
					    "(%d vs %ld)", c, d, ret,
					    sizeof (out));
					continue;
				}

				switch (dump_spd(fd, c, d, out)) {
				case SDE_OK:
					break;
				case SDE_NO_SPD:
					/*
					 * Warn but don't fail if any discovered
					 * DIMM has no SPD data.
					 */
					warnx("SPD not found for Channel %u "
					    "DIMM %u", c, d);
					continue;
				case SDE_DIMM_OR_CHAN_NOT_PRESENT:
					/*
					 * Silently skip any non-present
					 * channels/DIMMs in this mode.
					 */
					break;
				case SDE_INVALID_SPD:
				case SDE_IO_ERROR:
					/*
					 * An error message has already been
					 * written to stderr for these but we
					 * don't want to immediately bail.
					 * Note the error occurred and proceed.
					 */
					ret = EXIT_FAILURE;
					break;
				default:
					abort();
				}
			}
		}
	}

	(void) close(fd);

	return (ret);
}
