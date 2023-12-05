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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <libgen.h>
#include <err.h>

#include <sys/ipcc.h>
#include <sys/ipcc_proto.h>
#include <ipcc_drv.h>

int
main(int argc, char **argv)
{
	const char *suite_name = basename(argv[0]);

	int fd = open(IPCC_DEV, O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "could not open ipcc device");

	int version = ioctl(fd, IPCC_GET_VERSION, 0);
	if (version < 0)
		err(EXIT_FAILURE, "IPCC_GET_VERSION ioctl failed");

	if (version != IPCC_DRIVER_VERSION) {
		errx(EXIT_FAILURE, "kernel driver version %d != expected %d\n",
		    version, IPCC_DRIVER_VERSION);
	}

	(void) close(fd);
	(void) printf("%s\tPASS\n", suite_name);
	return (0);
}
