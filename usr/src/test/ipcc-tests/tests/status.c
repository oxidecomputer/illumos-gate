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
 * Copyright 2022 Oxide Computer Company
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <libgen.h>
#include <strings.h>
#include <err.h>

#include <sys/sysmacros.h>
#include <sys/ipcc.h>
#include <sys/ipcc_proto.h>

static struct {
	ipcc_sp_status_t flag;
	const char *descr;
} flags[] = {
	{ IPCC_STATUS_STARTED, "STARTED" },
	{ IPCC_STATUS_ALERT, "ALERT" },
	{ IPCC_STATUS_RESET, "RESET" },
};

int
main(int argc, char **argv)
{
	const char *suite_name = basename(argv[0]);
	uint64_t status = 0;
	int ret = 0;

	int fd = open(IPCC_DEV, O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "could not open ipcc device");

	ret = ioctl(fd, IPCC_STATUS, &status);
	if (ret < 0)
		err(EXIT_FAILURE, "IPCC_STATUS ioctl failed");

	(void) close(fd);

	(void) printf("Status: 0x%" PRIx64 "\n", status);
	for (uint_t i = 0; i < ARRAY_SIZE(flags); i++) {
		if (status & flags[i].flag) {
			(void) printf("        %s\n", flags[i].descr);
			status &= ~flags[i].flag;
		}
	}

	if (status != 0) {
		(void) printf("UNKNOWN BITS %" PRIx64 "\n", status);
		ret = 1;
	}

	(void) printf("%s\t%s\n", suite_name, ret == 0 ? "PASS" : "FAIL");
	return (ret);
}
