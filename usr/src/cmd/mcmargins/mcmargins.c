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
#include <locale.h>
#include <ofmt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mc.h>
#include <sys/mc_amdzen.h>

static ofmt_cb_t print_margin_cb;

typedef enum {
	MCMARGINS_FIELD_LANE,
	MCMARGINS_FIELD_RD_DQDLY_L,
	MCMARGINS_FIELD_RD_DQDLY_R,
	MCMARGINS_FIELD_WR_DQDLY_L,
	MCMARGINS_FIELD_WR_DQDLY_R,
	MCMARGINS_FIELD_RD_VREF_N,
	MCMARGINS_FIELD_RD_VREF_P,
	MCMARGINS_FIELD_WR_VREF_N,
	MCMARGINS_FIELD_WR_VREF_P,
} mcmargins_field_index_t;

#define	COMMON_FIELDS	\
{ "RD_DQDLY_L",	12,	MCMARGINS_FIELD_RD_DQDLY_L,	print_margin_cb }, \
{ "RD_DQDLY_R",	12,	MCMARGINS_FIELD_RD_DQDLY_R,	print_margin_cb }, \
{ "WR_DQDLY_L",	12,	MCMARGINS_FIELD_WR_DQDLY_L,	print_margin_cb }, \
{ "WR_DQDLY_R",	12,	MCMARGINS_FIELD_WR_DQDLY_R,	print_margin_cb }, \
{ "RD_VREF_N",	12,	MCMARGINS_FIELD_RD_VREF_N,	print_margin_cb }, \
{ "RD_VREF_P",	12,	MCMARGINS_FIELD_RD_VREF_P,	print_margin_cb }, \
{ "WR_VREF_N",	12,	MCMARGINS_FIELD_WR_VREF_N,	print_margin_cb }, \
{ "WR_VREF_P",	12,	MCMARGINS_FIELD_WR_VREF_P,	print_margin_cb }, \
{ NULL,		0,	0,				NULL }

static ofmt_field_t per_rank_fields[] = {
	COMMON_FIELDS
};

static ofmt_field_t per_lane_fields[] = {
{ "LANE",	8,	MCMARGINS_FIELD_LANE,		print_margin_cb },
	COMMON_FIELDS
};

typedef struct {
	uint8_t mfs_lane;
	mc_zen_margin_t *mfs_margin;
} mcmargins_fmt_state_t;

static void
mcmargins_oferr(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verrx(EXIT_FAILURE, fmt, ap);
}

static boolean_t
print_margin_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	mcmargins_fmt_state_t *state  = ofarg->ofmt_cbarg;
	mc_zen_margin_t *margin = state->mfs_margin;
	int ret;

	switch (ofarg->ofmt_id) {
	case MCMARGINS_FIELD_LANE:
		ret = snprintf(buf, bufsize, "%2u", state->mfs_lane);
		break;
	case MCMARGINS_FIELD_RD_DQDLY_L:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_rd_dqdly[0]);
		break;
	case MCMARGINS_FIELD_RD_DQDLY_R:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_rd_dqdly[1]);
		break;
	case MCMARGINS_FIELD_WR_DQDLY_L:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_wr_dqdly[0]);
		break;
	case MCMARGINS_FIELD_WR_DQDLY_R:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_wr_dqdly[1]);
		break;
	case MCMARGINS_FIELD_RD_VREF_N:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_rd_vref[0]);
		break;
	case MCMARGINS_FIELD_RD_VREF_P:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_rd_vref[1]);
		break;
	case MCMARGINS_FIELD_WR_VREF_N:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_wr_vref[0]);
		break;
	case MCMARGINS_FIELD_WR_VREF_P:
		ret = snprintf(buf, bufsize, "%2u", margin->mzm_wr_vref[1]);
		break;
	default:
		abort();
	}
	return ((ret > 0 && ret < bufsize) ? B_TRUE : B_FALSE);
}

/*
 * Returns `nmargins` number of training margin data results in `margins` for
 * the given Channel:DIMM:CS. If `subchan` is 0xFF and margin data is available,
 * `nmargins` will be 1 and `margins[0]` will be the per-rank results, otherwise
 * `margins` will be the per-DQ/lane results.
 *
 * The return value indicates whether a present/enabled channel/DIMM/rank was
 * specified. If true, the caller is expected to free the returned `margins`
 * array.
 */
static bool
get_margin_data(int fd, uint8_t chan, uint8_t dimm, uint8_t rank,
    uint8_t subchan, mc_zen_margin_t **margins, size_t *nmargins)
{
	mc_get_data_t data = {
		.mgd_type = MDT_MARGINS,
		.mgd_chan = chan,
		.mgd_dimm = dimm,
		.mgd_rank = rank,
		.mgd_subchan = subchan,
		.mgd_size = 0,
	};

	*margins = NULL;
	*nmargins = 0;

	if (ioctl(fd, MC_IOC_GET_DATA, &data) == -1) {
		errx(EXIT_FAILURE, gettext("MC_IOC_GET_DATA (1) failed for "
		    "%u:%u:%u:%u: %s (%d)"), chan, dimm, rank, subchan,
		    strerror(errno), errno);
	}

	switch (data.mgd_error) {
	case MGD_OK:
		err(EXIT_FAILURE, gettext("unexpected success getting margin "
		    "data size for %u:%u:%u:%u"), chan, dimm, rank, subchan);

	case MGD_NO_DATA:
		/* Valid location but no margin data present. */
		return (true);

	case MGD_INVALID_SIZE:
		VERIFY3U(data.mgd_size % sizeof (mc_zen_margin_t), ==, 0);
		break;

	case MGD_INVALID_CHAN:
		errx(EXIT_FAILURE, gettext("invalid channel: %u"), chan);
	case MGD_INVALID_SUBCHAN:
		errx(EXIT_FAILURE, gettext("invalid sub-channel: %u"), subchan);
	case MGD_INVALID_DIMM:
		errx(EXIT_FAILURE, gettext("invalid dimm: %u"), dimm);
	case MGD_INVALID_RANK:
		errx(EXIT_FAILURE, gettext("invalid rank: %u"), rank);

	case MGD_CHAN_EMPTY:
	case MGD_DIMM_NOT_PRESENT:
	case MGD_RANK_NOT_ENABLED:
		return (false);

	case MGD_INVALID_TYPE:
	default:
		abort();
	}

	data.mgd_addr = (uintptr_t)malloc(data.mgd_size);
	if (data.mgd_addr == (uintptr_t)NULL) {
		errx(EXIT_FAILURE, gettext("failed to alloc"));
	}

	if (ioctl(fd, MC_IOC_GET_DATA, &data) == -1) {
		errx(EXIT_FAILURE, gettext("MC_IOC_GET_DATA (2) failed for "
		    "%u:%u:%u:%u: %s (%d)"), chan, dimm, rank, subchan,
		    strerror(errno), errno);
	}

	/*
	 * Any other result should've caused us to bail on the first pass.
	 */
	if (data.mgd_error != MGD_OK) {
		errx(EXIT_FAILURE, gettext("failed to get margin data for "
		    "%u:%u:%u:%u: %d"), chan, dimm, rank, subchan,
		    data.mgd_error);
	}

	*margins = (mc_zen_margin_t *)data.mgd_addr;
	*nmargins = data.mgd_size / sizeof (mc_zen_margin_t);

	return (true);
}

static void
print_margin_data(int fd, uint8_t chan, uint8_t dimm, uint8_t rank,
    uint8_t subchan, const char *ofields, bool parsable, bool omit_headers)
{
	bool per_rank = (subchan == 0xFF);
	mc_zen_margin_t *margins;
	size_t nmargins;
	ofmt_handle_t ofmt;
	ofmt_status_t oferr;
	uint_t ofmtflags = 0;
	ofmt_field_t *oftemplate = per_rank ? per_rank_fields : per_lane_fields;

	if (parsable)
		ofmtflags |= OFMT_PARSABLE;
	if (omit_headers)
		ofmtflags |= OFMT_NOHEADER;

	oferr = ofmt_open(ofields, oftemplate, ofmtflags, 0, &ofmt);
	ofmt_check(oferr, parsable, ofmt, mcmargins_oferr, warnx);

	if (!get_margin_data(fd, chan, dimm, rank, subchan, &margins,
	    &nmargins)) {
		warnx(gettext("channel %u dimm %u rank %u not present or"
		    " enabled"), chan, dimm, rank);
		return;
	}

	if (nmargins == 0) {
		if (per_rank) {
			warnx(gettext("no per-rank margin data "
			    "available for channel %u dimm %u rank %u"),
			    chan, dimm, rank);
		} else {
			warnx(gettext("no per-lane margin data available for "
			    "channel %u dimm %u rank %u subchan %u"),
			    chan, dimm, rank, subchan);
		}
		return;
	}

	if (!omit_headers) {
		if (per_rank) {
			(void) printf("Channel %u DIMM %u Rank %u\n", chan,
			    dimm, rank);
		} else {
			(void) printf("Channel %u DIMM %u Rank %u SubChannel %u"
			    "\n", chan, dimm, rank, subchan);
		}
	}

	for (size_t i = 0; i < nmargins; i++) {
		mcmargins_fmt_state_t state = {
			.mfs_lane = i,
			.mfs_margin = &margins[i],
		};
		ofmt_print(ofmt, &state);
	}
	if (!parsable)
		(void) printf("\n");

	free(margins);
	margins = NULL;
}

static void
print_all_margin_data(int fd, bool per_rank, const char *ofields, bool parsable,
    bool omit_headers)
{
	for (uint8_t c = 0; c < MC_ZEN_MAX_CHANS; c++) {
		for (uint8_t d = 0; d < MC_ZEN_MAX_DIMMS; d++) {
			for (uint8_t r = 0; r < MC_ZEN_MAX_RANKS; r++) {
				if (per_rank) {
					print_margin_data(fd, c, d, r, 0xFF,
					    ofields, parsable, omit_headers);
					continue;
				}
				for (uint8_t sc = 0; sc < MC_ZEN_MAX_SUBCHANS;
				    sc++) {
					print_margin_data(fd, c, d, r, sc,
					    ofields, parsable, omit_headers);
				}
			}
		}
	}
}

static void
usage(const char *progname)
{
	errx(EXIT_FAILURE, gettext("Usage: %s "
	    "[-c <channel> -d <dimm> -r <rank> [-s <subchannel>]]"
	    " [-R] [-H] [[-p] -o <fields>,...] <mc-dev-path>\n"
	    "If no qualifiers (channel/dimm/rank/subchannel) are given, "
	    "all available margin data will be returned.\n"
	    "Passing -R will return the per-rank margin data rather than "
	    "per-lane and any subchannel specified will be ignored.\n"
	    "The set of fields to output can optionally be specified "
	    "as a comma-separated string with -o and further outputted\n"
	    "in a machine-parsable manner by passing -p. "
	    "Headers may be omitted by passing -H\n"),
	    progname);
}

int
main(int argc, char *argv[])
{
	int fd, c, errflg = 0;
	char *devpath;
	const char *errstr;
	bool per_rank = false;
	uint8_t chan = UINT8_MAX;
	uint8_t dimm = UINT8_MAX;
	uint8_t rank = UINT8_MAX;
	uint8_t subchan = UINT8_MAX;
	bool chan_set = false;
	bool dimm_set = false;
	bool rank_set = false;
	bool subchan_set = false;
	char *ofields = NULL;
	bool parsable = false;
	bool omit_headers = false;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "c:d:r:s:RHpo:")) != EOF) {
		switch (c) {
		case 'c':
			chan = (uint8_t)strtonum(optarg, 0, UINT8_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "channel is %s: %s", errstr,
				    optarg);
			}
			chan_set = true;
			break;
		case 'd':
			dimm = (uint8_t)strtonum(optarg, 0, UINT8_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "dimm is %s: %s", errstr,
				    optarg);
			}
			dimm_set = true;
			break;
		case 'r':
			rank = (uint8_t)strtonum(optarg, 0, UINT8_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "rank is %s: %s", errstr,
				    optarg);
			}
			rank_set = true;
			break;
		case 's':
			subchan = (uint8_t)strtonum(optarg, 0, UINT8_MAX,
			    &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "sub-channel is %s: %s",
				    errstr, optarg);
			}
			subchan_set = true;
			break;
		case 'R':
			per_rank = true;
			break;
		case 'H':
			omit_headers = true;
			break;
		case 'p':
			parsable = true;
			break;
		case 'o':
			ofields = optarg;
			break;
		case '?':
		default:
			errflg++;
			break;
		}
	}

	if (errflg != 0 || optind >= argc) {
		usage(argv[0]);
	}

	devpath = argv[optind];
	if ((fd = open(devpath, O_RDONLY)) == -1) {
		err(EXIT_FAILURE, gettext("failed to open %s"), devpath);
	}

	if (!chan_set && !dimm_set && !rank_set && !subchan_set) {
		print_all_margin_data(fd, per_rank, ofields, parsable,
		    omit_headers);
	} else if (chan_set && dimm_set && rank_set &&
	    (subchan_set || per_rank)) {
		if (per_rank)
			subchan = 0xFF;
		print_margin_data(fd, chan, dimm, rank, subchan, ofields,
		    parsable, omit_headers);
	} else {
		usage(argv[0]);
	}

	(void) close(fd);

	return (EXIT_SUCCESS);
}
