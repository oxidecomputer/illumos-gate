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

/*
 * Inject errors via the PSP.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <psp_einj.h>

#define	EXIT_USAGE	2


/* PRINTFLIKE1 */
static void __NORETURN
usage(const char *format, ...)
{
	va_list	ap;

	if (format != NULL) {
		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
		fputc('\n', stderr);
	}

	(void) fprintf(stderr, "Usage: "
	    "\tpspeinj\t-d device [-n] -m <type> -t <memory address>\n"
	    "\t\t-d device [-n] -p <type> -t <pcie [s/]b/d/f>\n\n"
	    "\tType is one of: 'corr', 'uncorr', or 'fatal'.\n\n"
	    "\tPassing -n will cause the given error target to be injected\n"
	    "\tbut NOT triggered. The caller may then trigger an error\n"
	    "\tby, e.g., trying to read the target memory address.\n");
	exit(EXIT_USAGE);
}

static psp_einj_type_t
parse_type(const char *str, psp_einj_type_t base)
{
	if (strcmp(str, "corr") == 0) {
		return (base + 0);
	} else if (strcmp(str, "uncorr") == 0) {
		return (base + 1);
	} else if (strcmp(str, "fatal") == 0) {
		return (base + 2);
	} else {
		usage("invalid error injection type: %s", str);
	}
}

static void
parse_mem_target(const char *target, psp_einj_req_t *einj)
{
	char *eptr;

	errno = 0;
	einj->per_mem_addr = strtoull(target, &eptr, 0);
	if (errno != 0 || *eptr != '\0') {
		errx(EXIT_FAILURE, "failed to parse memory address value: %s",
		    target);
	}
}

static uint8_t
parse_pcie_sbdf(const char *str, const char *name, uint8_t max)
{
	const char *errstr = NULL;
	uint8_t val;

	val = (uint8_t)strtonumx(str, 0, max, &errstr, 0);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "target PCIe %s value %s (range 0x0-0x%x): "
		    "%s", name, errstr, max, str);
	}
	return (val);
}

static void
parse_pcie_target(const char *target, psp_einj_req_t *einj)
{
	char *tok_save;
	char *toks[4] = {0};
	char **bdf_toks;

	/*
	 * We expect at least 3 pieces for the more common B.D.F case which
	 * omits the Segment (assumed 0 in such cases).
	 */
	if (((toks[0] = strtok_r((char *)target, "./:", &tok_save)) == NULL) ||
	    ((toks[1] = strtok_r(NULL, "./:", &tok_save)) == NULL) ||
	    ((toks[2] = strtok_r(NULL, "./:", &tok_save)) == NULL)) {
		usage("invalid PCIe (S)BDF target");
	}

	if ((toks[3] = strtok_r(NULL, "./:", &tok_save)) != NULL) {
		einj->per_pcie_seg = parse_pcie_sbdf(toks[0], "segment",
		    UINT8_MAX);
		bdf_toks = &toks[1];
	} else {
		einj->per_pcie_seg = 0;
		bdf_toks = &toks[0];
	}

	einj->per_pcie_bus = parse_pcie_sbdf(bdf_toks[0], "bus", UINT8_MAX);
	einj->per_pcie_dev = parse_pcie_sbdf(bdf_toks[1], "device", 31);
	einj->per_pcie_func = parse_pcie_sbdf(bdf_toks[2], "function", 7);
}

int
main(int argc, char **argv)
{
	int c, fd, ret;
	const char *device = NULL;
	const char *mem_type = NULL;
	const char *pcie_type = NULL;
	const char *target = NULL;
	psp_einj_req_t einj = {0};

	while ((c = getopt(argc, argv, "hd:m:p:t:n")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 'm':
			mem_type = optarg;
			break;
		case 'p':
			pcie_type = optarg;
			break;
		case 't':
			target = optarg;
			break;
		case 'n':
			einj.per_no_trigger = 1;
			break;
		case 'h':
		default:
			usage(NULL);
		}
	}

	if (device == NULL)
		usage("missing required psp device path");

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage("invalid argument: %s", argv[0]);

	if (mem_type == NULL && pcie_type == NULL) {
		usage("an error type must be specified for injection (-m/-p)");
	} else if (mem_type != NULL && pcie_type != NULL) {
		usage("only one of -m or -p may be specified");
	}

	if (target == NULL) {
		usage("an error injection target (-t) must be specified");
	}

	if (mem_type != NULL) {
		einj.per_type = parse_type(mem_type,
		    PSP_EINJ_TYPE_MEM_CORRECTABLE);
		parse_mem_target(target, &einj);
	} else	if (pcie_type != NULL) {
		einj.per_type = parse_type(pcie_type,
		    PSP_EINJ_TYPE_PCIE_CORRECTABLE);
		parse_pcie_target(target, &einj);
	}

	if ((fd = open(device, O_RDWR)) < 0)
		err(EXIT_FAILURE, "failed to open %s", device);

	ret = EXIT_SUCCESS;
	if (ioctl(fd, PSP_EINJ_IOC_INJECT, &einj) != 0) {
		warn("PSP_EINJ_IOC_INJECT ioctl failed");
		ret = EXIT_FAILURE;
	}

	(void) close(fd);
	return (ret);
}
