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

/*
 * Implement logic related to the state of PCIe links. These operate on a PCIe
 * bridge (e.g. a root port) by way of the private ioctls implemented by the
 * 'pcieb' driver.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <ofmt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>

#include <pcieb_ioctl.h>

#include "pcieadm.h"

static const char *pcieadm_ltssm_state_names[] = {
	[PCIE_LTSSM_UNKNOWN]	= "Unknown",
	[PCIE_LTSSM_DETECT]	= "Detect",
	[PCIE_LTSSM_POLLING]	= "Polling",
	[PCIE_LTSSM_CONFIG]	= "Config",
	[PCIE_LTSSM_RECOVERY]	= "Recovery",
	[PCIE_LTSSM_L0]		= "L0",
	[PCIE_LTSSM_L0S]	= "L0s",
	[PCIE_LTSSM_L1]		= "L1",
	[PCIE_LTSSM_L2]		= "L2",
	[PCIE_LTSSM_DISABLED]	= "Disabled",
	[PCIE_LTSSM_LOOPBACK]	= "Loopback",
	[PCIE_LTSSM_HOT_RESET]	= "Hot Reset",
};

static const char *pcieadm_ltssm_substate_names[] = {
	[PCIE_LTSSM_SS_DETECT_QUIET]			= "Quiet",
	[PCIE_LTSSM_SS_DETECT_ACTIVE]			= "Active",
	[PCIE_LTSSM_SS_POLLING_ACTIVE]			= "Active",
	[PCIE_LTSSM_SS_POLLING_COMPLIANCE]		= "Compliance",
	[PCIE_LTSSM_SS_POLLING_CONFIGURATION]		= "Configuration",
	[PCIE_LTSSM_SS_POLLING_SPEED]			= "Speed",
	[PCIE_LTSSM_SS_CONFIG_LINKWIDTH_START]		= "Linkwidth.Start",
	[PCIE_LTSSM_SS_CONFIG_LINKWIDTH_ACCEPT]		= "Linkwidth.Accept",
	[PCIE_LTSSM_SS_CONFIG_LANENUM_WAIT]		= "Lanenum.Wait",
	[PCIE_LTSSM_SS_CONFIG_LANENUM_ACCEPT]		= "Lanenum.Accept",
	[PCIE_LTSSM_SS_CONFIG_COMPLETE]			= "Complete",
	[PCIE_LTSSM_SS_CONFIG_IDLE]			= "Idle",
	[PCIE_LTSSM_SS_RECOVERY_RCVRLOCK]		= "RcvrLock",
	[PCIE_LTSSM_SS_RECOVERY_RCVRCFG]		= "RcvrCfg",
	[PCIE_LTSSM_SS_RECOVERY_SPEED]			= "Speed",
	[PCIE_LTSSM_SS_RECOVERY_EQUALIZATION]		= "Equalization",
	[PCIE_LTSSM_SS_RECOVERY_IDLE]			= "Idle",
	[PCIE_LTSSM_SS_L0S_ENTRY]			= "Entry",
	[PCIE_LTSSM_SS_L0S_IDLE]			= "Idle",
	[PCIE_LTSSM_SS_L0S_FTS]				= "FTS",
	[PCIE_LTSSM_SS_L1_ENTRY]			= "Entry",
	[PCIE_LTSSM_SS_L1_IDLE]				= "Idle",
	[PCIE_LTSSM_SS_L1_1]				= "1",
	[PCIE_LTSSM_SS_L1_2]				= "2",
	[PCIE_LTSSM_SS_L2_IDLE]				= "Idle",
	[PCIE_LTSSM_SS_L2_TRANSMIT_WAKE]		= "TransmitWake",
	[PCIE_LTSSM_SS_LOOPBACK_ENTRY]			= "Entry",
	[PCIE_LTSSM_SS_LOOPBACK_ACTIVE]			= "Active",
	[PCIE_LTSSM_SS_LOOPBACK_EXIT]			= "Exit",
};

/*
 * Human-readable names for each kind of LTSSM capture, indexed by
 * pcie_ltssm_snap_t.
 */
static const char *pcieadm_ltssm_sources[PCIE_LTSSM_NSNAP] = {
	[PCIE_LTSSM_SNAP_LIVE]		= "live",
	[PCIE_LTSSM_SNAP_LINK_UP]	= "link-up",
	[PCIE_LTSSM_SNAP_LINK_DOWN]	= "link-down"
};

static const char *
pcieadm_ltssm_state_name(uint32_t state)
{
	if (state >= ARRAY_SIZE(pcieadm_ltssm_state_names) ||
	    pcieadm_ltssm_state_names[state] == NULL) {
		return ("Unknown");
	}

	return (pcieadm_ltssm_state_names[state]);
}

static const char *
pcieadm_ltssm_substate_name(uint32_t substate)
{
	if (substate >= ARRAY_SIZE(pcieadm_ltssm_substate_names) ||
	    pcieadm_ltssm_substate_names[substate] == NULL) {
		return (NULL);
	}

	return (pcieadm_ltssm_substate_names[substate]);
}

typedef enum {
	PCIEADM_LINK_LTSSM_SOURCE,
	PCIEADM_LINK_LTSSM_INDEX,
	PCIEADM_LINK_LTSSM_STATE,
	PCIEADM_LINK_LTSSM_RAW,
	PCIEADM_LINK_LTSSM_NAME,
	PCIEADM_LINK_LTSSM_TIME
} pcieadm_link_ltssm_otype_t;

typedef struct {
	const char *pll_source;
	uint_t pll_index;
	hrtime_t pll_time;
	const pcie_ltssm_entry_t *pll_entry;
} pcieadm_link_ltssm_ofmt_t;

static boolean_t
pcieadm_link_ltssm_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const pcieadm_link_ltssm_ofmt_t *pll = ofarg->ofmt_cbarg;
	const pcie_ltssm_entry_t *e = pll->pll_entry;
	size_t ret;

	switch (ofarg->ofmt_id) {
	case PCIEADM_LINK_LTSSM_SOURCE:
		ret = strlcat(buf, pll->pll_source, buflen);
		break;
	case PCIEADM_LINK_LTSSM_INDEX:
		ret = snprintf(buf, buflen, "%u", pll->pll_index);
		break;
	case PCIEADM_LINK_LTSSM_STATE: {
		const char *st = pcieadm_ltssm_state_name(e->ple_state);
		const char *ss = pcieadm_ltssm_substate_name(e->ple_substate);

		if (ss != NULL)
			ret = snprintf(buf, buflen, "%s.%s", st, ss);
		else
			ret = strlcat(buf, st, buflen);
		break;
	}
	case PCIEADM_LINK_LTSSM_RAW:
		ret = snprintf(buf, buflen, "0x%02x", e->ple_raw);
		break;
	case PCIEADM_LINK_LTSSM_NAME:
		ret = strlcat(buf, e->ple_name[0] != '\0' ? e->ple_name : "--",
		    buflen);
		break;
	case PCIEADM_LINK_LTSSM_TIME:
		if (pll->pll_time != 0)
			ret = snprintf(buf, buflen, "%" PRId64, pll->pll_time);
		else
			ret = strlcat(buf, "--", buflen);
		break;
	default:
		return (B_FALSE);
	}

	return (buflen > ret);
}

static const char *pcieadm_link_ltssm_fields = "source,index,state,raw,name";
static const ofmt_field_t pcieadm_link_ltssm_ofmt[] = {
	{ "SOURCE", 12, PCIEADM_LINK_LTSSM_SOURCE, pcieadm_link_ltssm_ofmt_cb },
	{ "INDEX", 6, PCIEADM_LINK_LTSSM_INDEX, pcieadm_link_ltssm_ofmt_cb },
	{ "STATE", 25, PCIEADM_LINK_LTSSM_STATE, pcieadm_link_ltssm_ofmt_cb },
	{ "RAW", 6, PCIEADM_LINK_LTSSM_RAW, pcieadm_link_ltssm_ofmt_cb },
	{ "NAME", 24, PCIEADM_LINK_LTSSM_NAME, pcieadm_link_ltssm_ofmt_cb },
	{ "TIME", 20, PCIEADM_LINK_LTSSM_TIME, pcieadm_link_ltssm_ofmt_cb },
	{ NULL, 0, 0, NULL }
};

static void
pcieadm_link_ltssm_usage(FILE *f)
{
	(void) fprintf(f, "\tlink ltssm\t[-H] [-o field,... [-p]] -d device "
	    "[snapshot]\n");
}

static void
pcieadm_link_ltssm_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}

	(void) fprintf(stderr, "Usage:  %s link ltssm [-H] [-o field,... [-p]] "
	    "-d device [snapshot]\n", pcieadm_progname);
	(void) fprintf(stderr, "Show the LTSSM state of a PCIe link.\n\n"
	    "\t-d device\tthe PCIe bridge to query (driver instance,\n"
	    "\t\t\t/devices path, or b/d/f)\n"
	    "\t-H\t\tomit the column header\n"
	    "\t-o field\toutput fields to print (required for -p)\n"
	    "\t-p\t\tparsable output (requires -o)\n\n");
	(void) fprintf(stderr, "The following fields are supported:\n"
	    "\tsource\t\tthe snapshot the row belongs to: live, link-up, or\n"
	    "\t\t\tlink-down\n"
	    "\tindex\t\tposition in the history, 0 being the current state\n"
	    "\tstate\t\tthe common LTSSM state, as State.SubState\n"
	    "\traw\t\tthe raw platform-specific state value\n"
	    "\tname\t\tthe platform-specific state name\n"
	    "\ttime\t\tthe time the capture was taken\n");
	(void) fprintf(stderr, "The optional snapshot argument selects which "
	    "capture to show (default live):\n"
	    "\tlive\t\tthe current live state\n"
	    "\tlink-up\t\tstate captured at the last link-up\n"
	    "\tlink-down\tstate captured at the last link-down\n");
}

/*
 * Go through and initialize the common logic for most of the link related
 * commands that need to use the pcieb ioctls. This includes:
 *
 *  - Verifying it makes sense
 *  - Setting privileges
 *  - Opening the actual devctl file
 *  - Finding the dip
 */
static int
pcieadm_link_pcieb_open(pcieadm_t *pcip, const char *device, bool write)
{
	const char *drv;

	/*
	 * We retain FILE_DAC_READ and FILE_DAC_SEARCH to find and open the
	 * device file and SYS_DEVICES for the ioctls themselves. To open it for
	 * writing (some bridge ioctls, such as retrain, mutate link state) we
	 * also need FILE_DAC_WRITE to override the node's permissions, and
	 * FILE_WRITE because, unlike FILE_READ, it is stripped from our minimal
	 * privilege set.
	 */
	VERIFY0(priv_addset(pcip->pia_priv_eff, PRIV_SYS_DEVICES));
	VERIFY0(priv_addset(pcip->pia_priv_eff, PRIV_FILE_DAC_READ));
	VERIFY0(priv_addset(pcip->pia_priv_eff, PRIV_FILE_DAC_SEARCH));
	if (write) {
		VERIFY0(priv_addset(pcip->pia_priv_eff, PRIV_FILE_WRITE));
		VERIFY0(priv_addset(pcip->pia_priv_eff, PRIV_FILE_DAC_WRITE));
	}
	pcieadm_init_privs(pcip);

	pcieadm_find_dip(pcip, device);
	drv = di_driver_name(pcip->pia_devi);
	if (drv == NULL || strcmp(drv, "pcieb") != 0) {
		errx(EXIT_FAILURE, "device %s is not a PCIe bridge: found "
		    "driver %s, but expected pcieb", device, drv == NULL ?
		    "<none attached>" : drv);
	}

	for (di_minor_t m = di_minor_next(pcip->pia_devi, DI_MINOR_NIL);
	    m != NULL; m = di_minor_next(pcip->pia_devi, m)) {
		if (strcmp(di_minor_name(m), "devctl") == 0) {
			char buf[PATH_MAX], *mp;
			int fd;

			mp = di_devfs_minor_path(m);
			if (mp == NULL) {
				err(EXIT_FAILURE, "failed to get devfs path "
				    "for %s devctl minor", device);
			}

			if (snprintf(buf, sizeof (buf), "/devices%s", mp) >=
			    sizeof (buf)) {
				errx(EXIT_FAILURE, "failed to construct devfs "
				    "minor path for %s devctl minor: internal "
				    "path buffer would have overflown", device);
			}
			di_devfs_path_free(mp);

			if (setppriv(PRIV_SET, PRIV_EFFECTIVE,
			    pcip->pia_priv_eff) != 0) {
				err(EXIT_FAILURE, "failed to raise privileges");
			}

			fd = open(buf, write ? O_RDWR : O_RDONLY);
			if (fd < 0) {
				err(EXIT_FAILURE,
				    "failed to open %s devctl minor %s",
				    device, buf);
			}

			if (setppriv(PRIV_SET, PRIV_EFFECTIVE,
			    pcip->pia_priv_min) != 0) {
				err(EXIT_FAILURE,
				    "failed to reduce privileges");
			}

			return (fd);
		}
	}

	errx(EXIT_FAILURE, "failed to find devctl minor for %s", device);
}

static void
pcieadm_ltssm_hrtime_to_str(hrtime_t hrt, char *buf, size_t buflen)
{
	hrtime_t now = gethrtime();
	struct timeval tv;
	struct tm tm;
	time_t when;

	(void) gettimeofday(&tv, NULL);

	when = tv.tv_sec - (time_t)((now - hrt) / NANOSEC);
	if (localtime_r(&when, &tm) == NULL ||
	    strftime(buf, buflen, "%Y-%m-%dT%H:%M:%S %Z", &tm) == 0) {
		(void) strlcpy(buf, "<unknown>", buflen);
	}
}

/*
 * Print a single snapshot: the current state as index 0 followed by the
 * history, most recent first.
 */
static void
pcieadm_link_ltssm_print(ofmt_handle_t ofmt, const char *source,
    const pcie_ltssm_snapshot_t *snap)
{
	pcieadm_link_ltssm_ofmt_t pll;

	pll.pll_source = source;
	pll.pll_time = snap->pls_time;

	pll.pll_index = 0;
	pll.pll_entry = &snap->pls_current;
	ofmt_print(ofmt, &pll);

	for (uint_t i = 0; i < snap->pls_nhistory; i++) {
		pll.pll_index = i + 1;
		pll.pll_entry = &snap->pls_history[i];
		ofmt_print(ofmt, &pll);
	}
}

static int
pcieadm_link_ltssm(pcieadm_t *pcip, int argc, char *argv[])
{
	int c, fd, ret;
	const char *device = NULL;
	const char *fields = NULL;
	uint_t flags = 0;
	bool parse = false;
	ofmt_status_t oferr;
	ofmt_handle_t ofmt;
	pcieb_ioctl_ltssm_t ltssm;
	pcie_ltssm_snap_t snap = PCIE_LTSSM_SNAP_LIVE;

	while ((c = getopt(argc, argv, ":d:Ho:p")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 'H':
			flags |= OFMT_NOHEADER;
			break;
		case 'o':
			fields = optarg;
			break;
		case 'p':
			parse = true;
			flags |= OFMT_PARSABLE;
			break;
		case ':':
			pcieadm_link_ltssm_help("Option -%c requires an "
			    "argument", optopt);
			exit(EXIT_USAGE);
		case '?':
		default:
			pcieadm_link_ltssm_help("unknown option: -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (device == NULL) {
		pcieadm_link_ltssm_help("missing required device argument");
		exit(EXIT_USAGE);
	}

	if (parse && fields == NULL)
		errx(EXIT_USAGE, "-p requires fields specified with -o");

	if (fields == NULL)
		fields = pcieadm_link_ltssm_fields;

	argc -= optind;
	argv += optind;

	/*
	 * An optional single argument selects which snapshot to show: the live
	 * state (the default), or the state captured the last time the link
	 * came up or went down.
	 */
	if (argc > 1) {
		errx(EXIT_USAGE, "only a single snapshot may be specified");
	} else if (argc == 1) {
		uint_t s;

		for (s = 0; s < PCIE_LTSSM_NSNAP; s++) {
			if (strcmp(argv[0], pcieadm_ltssm_sources[s]) == 0) {
				snap = (pcie_ltssm_snap_t)s;
				break;
			}
		}

		if (s == PCIE_LTSSM_NSNAP) {
			errx(EXIT_USAGE, "unknown snapshot '%s': expected "
			    "live, link-up, or link-down", argv[0]);
		}
	}

	oferr = ofmt_open(fields, pcieadm_link_ltssm_ofmt, flags, 0, &ofmt);
	ofmt_check(oferr, parse, ofmt, pcieadm_ofmt_errx, warnx);

	fd = pcieadm_link_pcieb_open(pcip, device, false);

	bzero(&ltssm, sizeof (ltssm));
	ltssm.pil_snap = snap;

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_eff) != 0)
		err(EXIT_FAILURE, "failed to raise privileges");

	ret = ioctl(fd, PCIEB_IOCTL_GET_LTSSM, &ltssm);

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_min) != 0)
		err(EXIT_FAILURE, "failed to reduce privileges");

	if (ret != 0) {
		if (errno == ENOENT) {
			errx(EXIT_FAILURE, "no %s capture is available for %s",
			    pcieadm_ltssm_sources[snap], device);
		}
		err(EXIT_FAILURE, "failed to get LTSSM state for %s", device);
	}

	/*
	 * The capture time applies to the whole snapshot rather than any single
	 * row, so we print it once as a caption above the table. It is omitted
	 * in parsable output but the TIME field remains available there via -o
	 * for callers that want it.
	 */
	if (!parse && ltssm.pil_snapshot.pls_time != 0) {
		char tbuf[64];

		pcieadm_ltssm_hrtime_to_str(ltssm.pil_snapshot.pls_time,
		    tbuf, sizeof (tbuf));
		(void) printf("%s state captured at ~%s (hrtime %" PRId64 ")\n",
		    pcieadm_ltssm_sources[snap], tbuf,
		    ltssm.pil_snapshot.pls_time);
	}

	pcieadm_link_ltssm_print(ofmt, pcieadm_ltssm_sources[snap],
	    &ltssm.pil_snapshot);

	VERIFY0(close(fd));
	ofmt_close(ofmt);

	return (EXIT_SUCCESS);
}

static void
pcieadm_link_limit_usage(FILE *f)
{
	(void) fprintf(f, "\tlink limit\t[-s speed] -d device\n");
}

static void
pcieadm_link_limit_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}

	(void) fprintf(stderr, "Usage:  %s link limit [-s speed] -d device\n",
	    pcieadm_progname);
	(void) fprintf(stderr, "Print or set the administrative limit on the "
	    "corresponding PCIe link\n\n"
	    "\t-d device\tthe PCIe bridge to operate on (driver instance,"
	    "\n\t\t\t/devices path, or b/d/f)\n"
	    "\t-s speed\tlimit the device to the specified PCIe gen/speed "
	    "(e.g.\n\t\t\t2.5, 32, gen2, gen3, etc.)\n");
}

typedef struct {
	uint32_t	pls_speed;
	const char	*pls_gts;
	const char	*pls_gen;
	const char	*pls_alt;
} pcieadm_link_speed_t;

static const pcieadm_link_speed_t pcieadm_link_speeds[] = {
	{ PCIEB_LINK_SPEED_GEN1, "2.5",  "gen1", NULL },
	{ PCIEB_LINK_SPEED_GEN2, "5.0",  "gen2", "5" },
	{ PCIEB_LINK_SPEED_GEN3, "8.0",  "gen3", "8" },
	{ PCIEB_LINK_SPEED_GEN4, "16.0", "gen4", "16" },
	{ PCIEB_LINK_SPEED_GEN5, "32.0", "gen5", "32" },
	{ PCIEB_LINK_SPEED_GEN6, "64.0", "gen6", "64" }
};

static uint32_t
pcieadm_parse_pcieb_speed(const char *str)
{
	for (uint_t i = 0; i < ARRAY_SIZE(pcieadm_link_speeds); i++) {
		const pcieadm_link_speed_t *pls = &pcieadm_link_speeds[i];

		if (strcasecmp(str, pls->pls_gts) == 0 ||
		    strcasecmp(str, pls->pls_gen) == 0 ||
		    (pls->pls_alt != NULL &&
		    strcasecmp(str, pls->pls_alt) == 0)) {
			return (pls->pls_speed);
		}
	}

	errx(EXIT_FAILURE, "failed to parse speed: %s", str);
}

static const pcieadm_link_speed_t *
pcieadm_link_speed_lookup(uint32_t speed)
{
	for (uint_t i = 0; i < ARRAY_SIZE(pcieadm_link_speeds); i++) {
		if (pcieadm_link_speeds[i].pls_speed == speed)
			return (&pcieadm_link_speeds[i]);
	}

	return (NULL);
}

static int
pcieadm_link_limit(pcieadm_t *pcip, int argc, char *argv[])
{
	int c, ret = EXIT_SUCCESS;
	const char *device = NULL, *speed = NULL;
	pcieb_ioctl_target_speed_t pits;

	while ((c = getopt(argc, argv, ":d:s:")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 's':
			speed = optarg;
			break;
		case ':':
			pcieadm_link_limit_help("Option -%c requires an "
			    "argument", optopt);
			exit(EXIT_USAGE);
		case '?':
		default:
			pcieadm_link_limit_help("unknown option: -%c",
			    optopt);
			exit(EXIT_USAGE);
		}

	}

	if (device == NULL) {
		pcieadm_link_limit_help("missing required device argument "
		    "(-d)");
		exit(EXIT_USAGE);
	}

	argc -= optind;
	argv += optind;
	if (argc != 0) {
		errx(EXIT_USAGE, "encountered extraneous arguments starting "
		    "with %s", argv[0]);
	}

	int fd = pcieadm_link_pcieb_open(pcip, device, speed != NULL);
	bzero(&pits, sizeof (pits));

	if (speed != NULL) {
		pits.pits_speed = pcieadm_parse_pcieb_speed(speed);

		if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_eff) != 0)
			err(EXIT_FAILURE, "failed to raise privileges");

		if (ioctl(fd, PCIEB_IOCTL_SET_TARGET_SPEED, &pits) != 0) {
			err(EXIT_FAILURE, "failed to set %s target speed",
			    device);
		}

		if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_min) != 0)
			err(EXIT_FAILURE, "failed to reduce privileges");

	} else {
		const pcieadm_link_speed_t *pls;

		if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_eff) != 0)
			err(EXIT_FAILURE, "failed to raise privileges");

		if (ioctl(fd, PCIEB_IOCTL_GET_TARGET_SPEED, &pits) != 0) {
			err(EXIT_FAILURE, "failed to get %s target speed",
			    device);
		}

		if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_min) != 0)
			err(EXIT_FAILURE, "failed to reduce privileges");

		pls = pcieadm_link_speed_lookup(pits.pits_speed);
		if (pls != NULL) {
			(void) printf("Target speed: %s GT/s (%s)\n",
			    pls->pls_gts, pls->pls_gen);
		} else {
			(void) printf("Target speed: unknown speed value: "
			    "0x%x\n", pits.pits_speed);
		}

		if ((pits.pits_flags & ~PCIEB_FLAGS_ADMIN_SET) != 0) {
			(void) printf("Unknown flags: 0x%x\n", pits.pits_flags);
		} else if ((pits.pits_flags & PCIEB_FLAGS_ADMIN_SET) != 0) {
			(void) printf("Flags: Admin Set Speed\n");
		}

	}

	VERIFY0(close(fd));
	return (ret);
}

static void
pcieadm_link_retrain_usage(FILE *f)
{
	(void) fprintf(f, "\tlink retrain\t-d device\n");
}

static void
pcieadm_link_retrain_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}

	(void) fprintf(stderr, "Usage:  %s link retrain -d device\n",
	    pcieadm_progname);
	(void) fprintf(stderr, "Retrain the link on the specified upstream "
	    "port by requesting it via the PCIe Link Control register.\n\n"
	    "\t-d device\tthe PCIe bridge to operate on (driver instance,"
	    "\n\t\t\t/devices path, or b/d/f)\n");
}

static int
pcieadm_link_retrain(pcieadm_t *pcip, int argc, char *argv[])
{
	int c, ret = EXIT_SUCCESS;
	const char *device = NULL;

	while ((c = getopt(argc, argv, ":d:")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case ':':
			pcieadm_link_retrain_help("Option -%c requires an "
			    "argument", optopt);
			exit(EXIT_USAGE);
		case '?':
		default:
			pcieadm_link_retrain_help("unknown option: -%c",
			    optopt);
			exit(EXIT_USAGE);
		}
	}

	if (device == NULL) {
		pcieadm_link_retrain_help("missing required device argument "
		    "(-d)");
		exit(EXIT_USAGE);
	}

	argc -= optind;
	argv += optind;
	if (argc != 0) {
		errx(EXIT_USAGE, "encountered extraneous arguments starting "
		    "with %s", argv[0]);
	}

	int fd = pcieadm_link_pcieb_open(pcip, device, true);

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_eff) != 0)
		err(EXIT_FAILURE, "failed to raise privileges");

	if (ioctl(fd, PCIEB_IOCTL_RETRAIN) != 0) {
		err(EXIT_FAILURE, "failed to retrain link %s", device);
	}

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_min) != 0)
		err(EXIT_FAILURE, "failed to reduce privileges");

	VERIFY0(close(fd));
	return (ret);
}

static const pcieadm_cmdtab_t pcieadm_cmds_link[] = {
	{ "limit", pcieadm_link_limit, pcieadm_link_limit_usage },
	{ "ltssm", pcieadm_link_ltssm, pcieadm_link_ltssm_usage },
	{ "retrain", pcieadm_link_retrain, pcieadm_link_retrain_usage },
	{ NULL }
};

int
pcieadm_link(pcieadm_t *pcip, int argc, char *argv[])
{
	return (pcieadm_walk_tab(pcip, pcieadm_cmds_link, argc, argv));
}

void
pcieadm_link_usage(FILE *f)
{
	pcieadm_walk_usage(pcieadm_cmds_link, f);
}
