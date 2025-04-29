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
 * Send requests to the Host System Management Port (HSMP)
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/stdbool.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <uhsmp.h>
#include <sys/amdzen/hsmp.h>

#define	EXIT_USAGE	2

static const char *
uhsmp_response_str(uint32_t resp)
{
	switch (resp) {
	case HSMP_RESPONSE_INCOMPLETE:
		return ("incomplete");
	case HSMP_RESPONSE_OK:
		return ("success");
	case HSMP_RESPONSE_INVALID_ARGS:
		return ("rejected - invalid arguments");
	case HSMP_RESPONSE_INVALID_MSGID:
		return ("failure - invalid/unsupported message ID");
	case HSMP_RESPONSE_REJECTED_PREREQ:
		return ("rejected - missing pre-requisite(s)");
	case HSMP_RESPONSE_REJECTED_BUSY:
		return ("rejected - system busy");
	default:
		return ("unknown failure");
	}
}

static bool
uhsmp_parse_uint32(const char *str, uint32_t *valp)
{
	const char *errstr;
	uint32_t l;

	l = (uint32_t)strtonumx(str, 0, UINT32_MAX, &errstr, 0);
	if (errstr != NULL) {
		warnx("value %s (range 0-0x%x): %s", errstr, UINT32_MAX, str);
		return (false);
	}
	*valp = l;
	return (true);
}

static void
usage(const char *str)
{
	if (str != NULL)
		(void) fprintf(stderr, "%s\n", str);
	(void) fprintf(stderr, "Usage: uhsmp -d device "
	    "command [arg]...\n");
	exit(EXIT_USAGE);
}

int
main(int argc, char **argv)
{
	int c, fd, ret;
	const char *device = NULL;
	uhsmp_cmd_t cmd = { 0 };

	while ((c = getopt(argc, argv, "d:")) != -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		default:
			usage(NULL);
		}
	}

	if (device == NULL)
		errx(EXIT_FAILURE, "missing required device");

	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage("a command must be specified");

	if (!uhsmp_parse_uint32(argv[0], &cmd.uc_id))
		return (EXIT_FAILURE);
	argv++;
	argc--;

	for (uint_t i = 0; i < argc && i < ARRAY_SIZE(cmd.uc_args); i++) {
		if (!uhsmp_parse_uint32(argv[i], &cmd.uc_args[i]))
			return (EXIT_FAILURE);
	}

	if ((fd = open(device, O_RDWR)) < 0)
		err(EXIT_FAILURE, "failed to open %s", device);

	ret = EXIT_SUCCESS;
	if (ioctl(fd, UHSMP_GENERIC_COMMAND, &cmd) != 0) {
		warn("UHSMP ioctl failed");
		ret = EXIT_FAILURE;
	} else {
		(void) printf("Result: 0x%x (%s)\n",
		    cmd.uc_response, uhsmp_response_str(cmd.uc_response));
		for (uint_t i = 0; i < ARRAY_SIZE(cmd.uc_args); i++)
			(void) printf("  Arg%u: 0x%x\n", i, cmd.uc_args[i]);
	}

	(void) close(fd);
	return (ret);
}
