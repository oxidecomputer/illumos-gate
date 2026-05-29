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
pcieadm_link_hrtime_to_str(hrtime_t hrt, char *buf, size_t buflen)
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

		pcieadm_link_hrtime_to_str(ltssm.pil_snapshot.pls_time,
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

typedef enum {
	PCIEADM_LINK_EQ_GEN,
	PCIEADM_LINK_EQ_MASK,
	PCIEADM_LINK_EQ_UPMASK,
	PCIEADM_LINK_EQ_LANE,
	PCIEADM_LINK_EQ_PRESET,
	PCIEADM_LINK_EQ_PRE,
	PCIEADM_LINK_EQ_CUR,
	PCIEADM_LINK_EQ_POST,
	PCIEADM_LINK_EQ_FOM,
	PCIEADM_LINK_EQ_USED,
	PCIEADM_LINK_EQ_LPRESET,
	PCIEADM_LINK_EQ_LPRE,
	PCIEADM_LINK_EQ_LCUR,
	PCIEADM_LINK_EQ_LPOST
} pcieadm_link_eq_otype_t;

/*
 * The per-lane EQ data is reported as one row per lane, but the generation and
 * the preset masks (configured and at last link-up) are properties of the whole
 * capture.
 */
typedef struct {
	const pcie_eq_t		*peo_eq;
	const pcie_eq_lane_t	*peo_lane;
} pcieadm_link_eq_ofmt_t;

static boolean_t
pcieadm_link_eq_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const pcieadm_link_eq_ofmt_t *peo = ofarg->ofmt_cbarg;
	const pcie_eq_t *pe = peo->peo_eq;
	const pcie_eq_lane_t *pel = peo->peo_lane;
	size_t ret;

	switch (ofarg->ofmt_id) {
	case PCIEADM_LINK_EQ_GEN:
		ret = snprintf(buf, buflen, "gen%u", pe->peq_gen);
		break;
	case PCIEADM_LINK_EQ_MASK:
		ret = snprintf(buf, buflen, "0x%03x", pe->peq_mask);
		break;
	case PCIEADM_LINK_EQ_UPMASK:
		if ((pe->peq_flags & PCIE_EQ_F_LINKUP_VALID) == 0) {
			ret = snprintf(buf, buflen, "--");
		} else {
			ret = snprintf(buf, buflen, "0x%03x",
			    pe->peq_mask_linkup);
		}
		break;
	case PCIEADM_LINK_EQ_LANE:
		ret = snprintf(buf, buflen, "%u", pel->pel_lane);
		break;
	case PCIEADM_LINK_EQ_PRESET:
		ret = snprintf(buf, buflen, "P%u", pel->pel_best_preset);
		break;
	case PCIEADM_LINK_EQ_PRE:
		ret = snprintf(buf, buflen, "%u", pel->pel_best_precursor);
		break;
	case PCIEADM_LINK_EQ_CUR:
		ret = snprintf(buf, buflen, "%u", pel->pel_best_cursor);
		break;
	case PCIEADM_LINK_EQ_POST:
		ret = snprintf(buf, buflen, "%u", pel->pel_best_postcursor);
		break;
	case PCIEADM_LINK_EQ_FOM:
		ret = snprintf(buf, buflen, "%u", pel->pel_best_fom);
		break;
	case PCIEADM_LINK_EQ_USED:
		if ((pel->pel_flags & PCIE_EQ_LANE_F_USE_PRESET_VALID) == 0) {
			ret = strlcat(buf, "--", buflen);
		} else {
			ret = strlcat(buf,
			    (pel->pel_flags & PCIE_EQ_LANE_F_USE_PRESET) != 0 ?
			    "yes" : "no", buflen);
		}
		break;
	case PCIEADM_LINK_EQ_LPRESET:
		ret = snprintf(buf, buflen, "P%u", pel->pel_local_preset);
		break;
	case PCIEADM_LINK_EQ_LPRE:
		ret = snprintf(buf, buflen, "%u", pel->pel_local_precursor);
		break;
	case PCIEADM_LINK_EQ_LCUR:
		ret = snprintf(buf, buflen, "%u", pel->pel_local_cursor);
		break;
	case PCIEADM_LINK_EQ_LPOST:
		ret = snprintf(buf, buflen, "%u", pel->pel_local_postcursor);
		break;
	default:
		return (B_FALSE);
	}

	return (buflen > ret);
}

static const char *pcieadm_link_eq_fields =
	"lane,preset,pre,cur,post,fom,used";
static const ofmt_field_t pcieadm_link_eq_ofmt[] = {
	{ "GEN", 6, PCIEADM_LINK_EQ_GEN, pcieadm_link_eq_ofmt_cb },
	{ "MASK", 8, PCIEADM_LINK_EQ_MASK, pcieadm_link_eq_ofmt_cb },
	{ "UPMASK", 8, PCIEADM_LINK_EQ_UPMASK, pcieadm_link_eq_ofmt_cb },
	{ "LANE", 6, PCIEADM_LINK_EQ_LANE, pcieadm_link_eq_ofmt_cb },
	{ "PRESET", 8, PCIEADM_LINK_EQ_PRESET, pcieadm_link_eq_ofmt_cb },
	{ "PRE", 6, PCIEADM_LINK_EQ_PRE, pcieadm_link_eq_ofmt_cb },
	{ "CUR", 6, PCIEADM_LINK_EQ_CUR, pcieadm_link_eq_ofmt_cb },
	{ "POST", 6, PCIEADM_LINK_EQ_POST, pcieadm_link_eq_ofmt_cb },
	{ "FOM", 6, PCIEADM_LINK_EQ_FOM, pcieadm_link_eq_ofmt_cb },
	{ "USED", 6, PCIEADM_LINK_EQ_USED, pcieadm_link_eq_ofmt_cb },
	{ "LPRESET", 8, PCIEADM_LINK_EQ_LPRESET, pcieadm_link_eq_ofmt_cb },
	{ "LPRE", 6, PCIEADM_LINK_EQ_LPRE, pcieadm_link_eq_ofmt_cb },
	{ "LCUR", 6, PCIEADM_LINK_EQ_LCUR, pcieadm_link_eq_ofmt_cb },
	{ "LPOST", 6, PCIEADM_LINK_EQ_LPOST, pcieadm_link_eq_ofmt_cb },
	{ NULL, 0, 0, NULL }
};

static void
pcieadm_link_eq_usage(FILE *f)
{
	(void) fprintf(f, "\tlink eq\t\t[-H] [-o field,... [-p]] [-s speed] "
	    "-d device\n");
}

static void
pcieadm_link_eq_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}

	(void) fprintf(stderr, "Usage:  %s link eq [-H] [-o field,... [-p]] "
	    "[-s speed] -d device\n", pcieadm_progname);
	(void) fprintf(stderr, "Show the equalization settings of a "
	    "PCIe link.\n\n"
	    "\t-d device\tthe PCIe bridge to operate on (driver instance,\n"
	    "\t\t\t/devices path, or b/d/f)\n"
	    "\t-H\t\tomit the column header\n"
	    "\t-o field\toutput fields to print (required for -p)\n"
	    "\t-p\t\tparsable output (requires -o)\n"
	    "\t-s speed\tthe PCIe gen/speed to report (gen3 and above;\n"
	    "\t\t\tdefaults to the current link speed)\n\n");
	(void) fprintf(stderr, "The following fields are supported:\n"
	    "\tgen\t\tthe PCIe generation the data is for\n"
	    "\tmask\t\tthe configured preset search mask\n"
	    "\tupmask\t\tthe preset search mask at the last link-up\n"
	    "\tlane\t\tthe logical lane number\n"
	    "\tpreset\t\tthe best preset found during equalization\n"
	    "\tpre\t\tthe best pre-cursor coefficient\n"
	    "\tcur\t\tthe best cursor coefficient\n"
	    "\tpost\t\tthe best post-cursor coefficient\n"
	    "\tfom\t\tthe figure of merit for the best settings\n"
	    "\tused\t\twhether the local transmitter is using a preset, shown\n"
	    "\t\t\tas '--' when the platform cannot report it\n"
	    "\tlpreset\t\tthe preset used by the local transmitter\n"
	    "\tlpre\t\tthe local pre-cursor coefficient\n"
	    "\tlcur\t\tthe local cursor coefficient\n"
	    "\tlpost\t\tthe local post-cursor coefficient\n");
}

static void
pcieadm_link_preset_mask_usage(FILE *f)
{
	(void) fprintf(f, "\tlink preset-mask\t-s speed -m mask -d device\n");
}

static void
pcieadm_link_preset_mask_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
		(void) fprintf(stderr, "\n");
	}

	(void) fprintf(stderr, "Usage:  %s link preset-mask -s speed -m mask "
	    "-d device\n", pcieadm_progname);
	(void) fprintf(stderr, "Set the equalization preset search mask of a "
	    "PCIe link.\n\n"
	    "\t-d device\tthe PCIe bridge to operate on (driver instance,\n"
	    "\t\t\t/devices path, or b/d/f)\n"
	    "\t-m mask\t\tthe preset search mask to program; bit i selects\n"
	    "\t\t\tpreset Pi. The meaning of an all-zero mask is\n"
	    "\t\t\tplatform-specific\n"
	    "\t-s speed\tthe PCIe gen/speed to set the mask for (gen3 and\n"
	    "\t\t\tabove)\n\n");
	(void) fprintf(stderr, "The new mask takes effect on the next "
	    "equalization, i.e. after the link is\ndisabled and re-enabled, "
	    "or the device is power cycled.\n");
}

/*
 * Parse a user-specified speed into a PCIe generation that supports
 * equalisation (gen3 and above), exiting on error.
 */
static uint32_t
pcieadm_link_eq_parse_gen(const char *speed)
{
	uint32_t gen = pcieadm_parse_pcieb_speed(speed);

	if (gen < PCIEB_LINK_SPEED_GEN3) {
		errx(EXIT_FAILURE,
		    "equalization only applies to gen3 and above");
	}

	return (gen);
}

/*
 * Print the human-readable interpretation of a preset search mask: if the
 * platform has told us it considers every preset (all_presets) say so,
 * otherwise list the presets the mask selects, or "none" if it selects nothing.
 */
static void
pcieadm_link_eq_print_mask(uint32_t mask, bool all_presets)
{
	if (all_presets) {
		(void) printf("all presets");
	} else if (mask == 0) {
		(void) printf("none");
	} else {
		for (uint_t i = 0, n = 0; i < PCIE_EQ_NPRESETS; i++) {
			if ((mask & (1U << i)) != 0)
				(void) printf("%sP%u", n++ == 0 ? "" : " ", i);
		}
	}
}

static int
pcieadm_link_eq(pcieadm_t *pcip, int argc, char *argv[])
{
	int c, fd;
	const char *device = NULL, *fields = NULL, *speed = NULL;
	uint_t flags = 0;
	bool parse = false;
	ofmt_status_t oferr;
	ofmt_handle_t ofmt;
	pcieb_ioctl_eq_t eq;
	const pcie_eq_t *pe;

	while ((c = getopt(argc, argv, ":d:Ho:ps:")) != -1) {
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
		case 's':
			speed = optarg;
			break;
		case ':':
			pcieadm_link_eq_help(
			    "Option -%c requires an argument", optopt);
			exit(EXIT_USAGE);
		case '?':
		default:
			pcieadm_link_eq_help("unknown option: -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (device == NULL) {
		pcieadm_link_eq_help("missing required device argument");
		exit(EXIT_USAGE);
	}

	argc -= optind;
	argv += optind;
	if (argc != 0) {
		errx(EXIT_USAGE,
		    "encountered extraneous arguments starting with %s",
		    argv[0]);
	}

	if (parse && fields == NULL)
		errx(EXIT_USAGE, "-p requires fields specified with -o");

	if (fields == NULL)
		fields = pcieadm_link_eq_fields;

	bzero(&eq, sizeof (eq));
	if (speed != NULL)
		eq.pie_gen = pcieadm_link_eq_parse_gen(speed);

	oferr = ofmt_open(fields, pcieadm_link_eq_ofmt, flags, 0, &ofmt);
	ofmt_check(oferr, parse, ofmt, pcieadm_ofmt_errx, warnx);

	fd = pcieadm_link_pcieb_open(pcip, device, false);

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_eff) != 0)
		err(EXIT_FAILURE, "failed to raise privileges");

	if (ioctl(fd, PCIEB_IOCTL_GET_EQ, &eq) != 0)
		err(EXIT_FAILURE, "failed to get EQ data for %s", device);

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_min) != 0)
		err(EXIT_FAILURE, "failed to reduce privileges");

	pe = &eq.pie_eq;

	if (!parse) {
		const pcieadm_link_speed_t *pls =
		    pcieadm_link_speed_lookup(pe->peq_gen);

		if (pls != NULL) {
			(void) printf("Generation: %s (%s GT/s)\n",
			    pls->pls_gen, pls->pls_gts);
		}

		/*
		 * This is the mask programmed now, which governs the next
		 * equalisation. If it differs from the link-up mask below, the
		 * per-lane results were produced with a different mask that has
		 * not yet taken effect.
		 */
		(void) printf("Preset search mask (configured): 0x%03x (",
		    pe->peq_mask);
		pcieadm_link_eq_print_mask(pe->peq_mask,
		    (pe->peq_flags & PCIE_EQ_F_ALL_PRESETS) != 0);
		(void) printf(")\n");

		/*
		 * If the platform captured the mask in effect at the last
		 * link-up, show it too, with when that was, so a mask changed
		 * since can be told apart from the one actually in use.
		 */
		if ((pe->peq_flags & PCIE_EQ_F_LINKUP_VALID) != 0) {
			bool all = (pe->peq_flags &
			    PCIE_EQ_F_LINKUP_ALL_PRESETS) != 0;
			char tbuf[64];

			pcieadm_link_hrtime_to_str(pe->peq_linkup_time, tbuf,
			    sizeof (tbuf));
			(void) printf("Preset search mask (at last link-up, "
			    "~%s): 0x%03x (", tbuf, pe->peq_mask_linkup);
			pcieadm_link_eq_print_mask(pe->peq_mask_linkup, all);
			(void) printf(")\n");
		}
	}

	for (uint_t i = 0; i < pe->peq_nlanes; i++) {
		pcieadm_link_eq_ofmt_t peo;

		peo.peo_eq = pe;
		peo.peo_lane = &pe->peq_lanes[i];
		ofmt_print(ofmt, &peo);
	}

	VERIFY0(close(fd));
	ofmt_close(ofmt);

	return (EXIT_SUCCESS);
}

static int
pcieadm_link_preset_mask(pcieadm_t *pcip, int argc, char *argv[])
{
	int c, fd;
	const char *device = NULL, *speed = NULL, *mask_str = NULL;
	const char *errstr;
	long long mask;
	pcieb_ioctl_preset_mask_t pm;

	while ((c = getopt(argc, argv, ":d:m:s:")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			mask_str = optarg;
			break;
		case 's':
			speed = optarg;
			break;
		case ':':
			pcieadm_link_preset_mask_help(
			    "Option -%c requires an argument", optopt);
			exit(EXIT_USAGE);
		case '?':
		default:
			pcieadm_link_preset_mask_help("unknown option: -%c",
			    optopt);
			exit(EXIT_USAGE);
		}
	}

	if (device == NULL) {
		pcieadm_link_preset_mask_help(
		    "missing required device argument");
		exit(EXIT_USAGE);
	}
	if (speed == NULL) {
		pcieadm_link_preset_mask_help(
		    "setting the preset mask requires a speed (-s)");
		exit(EXIT_USAGE);
	}
	if (mask_str == NULL) {
		pcieadm_link_preset_mask_help(
		    "setting the preset mask requires a mask (-m)");
		exit(EXIT_USAGE);
	}

	argc -= optind;
	argv += optind;
	if (argc != 0) {
		errx(EXIT_USAGE,
		    "encountered extraneous arguments starting with %s",
		    argv[0]);
	}

	mask = strtonumx(mask_str, 0, (1 << PCIE_EQ_NPRESETS) - 1, &errstr, 0);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "invalid preset mask '%s': %s", mask_str,
		    errstr);
	}

	bzero(&pm, sizeof (pm));
	pm.pipm_gen = pcieadm_link_eq_parse_gen(speed);
	pm.pipm_mask = (uint32_t)mask;

	fd = pcieadm_link_pcieb_open(pcip, device, true);

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_eff) != 0)
		err(EXIT_FAILURE, "failed to raise privileges");

	if (ioctl(fd, PCIEB_IOCTL_SET_PRESET_MASK, &pm) != 0)
		err(EXIT_FAILURE, "failed to set preset mask for %s", device);

	if (setppriv(PRIV_SET, PRIV_EFFECTIVE, pcip->pia_priv_min) != 0)
		err(EXIT_FAILURE, "failed to reduce privileges");

	VERIFY0(close(fd));
	return (EXIT_SUCCESS);
}

static const pcieadm_cmdtab_t pcieadm_cmds_link[] = {
	{ "eq", pcieadm_link_eq, pcieadm_link_eq_usage },
	{ "preset-mask", pcieadm_link_preset_mask,
	    pcieadm_link_preset_mask_usage },
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
