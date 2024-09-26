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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/inttypes.h>
#include <sys/cpuid_drv.h>

static const char dev_cpu_self_cpuid[] = "/dev/" CPUID_SELF_NAME;

#define	MHZ(hz)	((hz) / 1000000.0)

int
main(int argc, char *argv[])
{
	struct cpuid_effi effi = { 0 };
	float ratio;
	uint64_t effhz;
	int fd, c, errflg = 0;
	bool verbose = false;
	bool raw = false;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "rv")) != EOF) {
		switch (c) {
		case 'r':
			raw = true;
			break;
		case 'v':
			verbose = true;
			break;
		case '?':
		default:
			errflg++;
			break;
		}
	}

	if (errflg != 0 || argc != optind) {
		fprintf(stderr, gettext("Usage: rdfreq [-v|r]\n"));
		return (EXIT_FAILURE);
	}

	if ((fd = open(dev_cpu_self_cpuid, O_RDONLY)) == -1) {
		err(EXIT_FAILURE, gettext("failed to open %s"),
		    dev_cpu_self_cpuid);
	}

	if (ioctl(fd, CPUID_EFFI, &effi) != 0)
		err(EXIT_FAILURE, gettext("rdfreq failed"));

	(void) close(fd);

	ratio = (float)effi.ce_aperf / (float)effi.ce_mperf;
	effhz = effi.ce_p0freq * ratio;

	if (verbose) {
		printf("APERF/MPERF: 0x%" PRIx64 " 0x%" PRIx64 " Ratio: %.4f\n",
		    effi.ce_aperf, effi.ce_mperf, ratio);
		printf("P0 Frequency: 0x%1$" PRIx64 " (%1$" PRIu64
		    " Hz ~ %.2f MHz)\n", effi.ce_p0freq, MHZ(effi.ce_p0freq));
		printf("Effective frequency: %" PRIu64 " Hz ~ %.2f MHz\n",
		    effhz, MHZ(effhz));
	} else {
		if (raw)
			printf("%" PRIu64 "\n", effhz);
		else
			printf("%.2f\n", MHZ(effhz));
	}

	return (0);
}
