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
 * Copyright 2023 Jason King
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/debug.h>
#include <sys/types.h>
#include <sys/tpm.h>

#define	TPM_DEVICE	"/dev/tpm"

static bool quiet;

int
main(int argc, char * const *argv)
{
	int fd, c, version;

	while ((c = getopt(argc, argv, "q")) != -1) {
		switch (c) {
		case 'q':
			quiet = true;
			break;
		case '?':
			(void) fprintf(stderr, "Unknown option: -%c\n", optopt);
			exit(2);
		}
	}

	fd = open(TPM_DEVICE, O_RDONLY);
	if (fd == -1) {
		if (quiet)
			exit(EXIT_FAILURE);
		err(EXIT_FAILURE, "%s", TPM_DEVICE);
	}

	if (ioctl(fd, TPMIOC_GETVERSION, &version) < 0) {
		if (quiet)
			exit(EXIT_FAILURE);
		err(EXIT_FAILURE, "failed to get TPM version");
	}

	switch (version) {
	case TPMDEV_VERSION_1_2:
		(void) printf("1.2\n");
		break;
	case TPMDEV_VERSION_2_0:
		(void) printf("2.0\n");
		break;
	default:
		if (!quiet)
			(void) printf("Unknown %d\n", version);
		break;
	}

	VERIFY0(close(fd));
	return (EXIT_SUCCESS);
}
