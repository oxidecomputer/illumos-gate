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
#include <err.h>

#include <sys/ipcc.h>

/*
 * Until RoT requests are finalised, this test just sends some arbitrary data
 * to the SP in an RoT Request packet, and the SP sends some arbitrary data
 * back. This test needs replacing once the RoT work is further along.
 */

int
main(int argc, char **argv)
{
	const char *suite_name = basename(argv[0]);
	ipcc_rot_t rot;

	int fd = open("/etc/release", O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "could not open /etc/release");
	rot.ir_len = read(fd, rot.ir_data, sizeof (rot.ir_data));
	if (rot.ir_len <= 0)
		err(EXIT_FAILURE, "could not slurp /etc/release");
	(void) printf("+ Prepared 0x%" PRIx64 " bytes\n", rot.ir_len);
	(void) close(fd);

	fd = open(IPCC_DEV, O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "could not open ipcc device");

	int ret = ioctl(fd, IPCC_ROT, &rot);
	if (ret < 0)
		err(EXIT_FAILURE, "IPCC_ROT ioctl failed");

	(void) printf("+ Output size 0x%" PRIx64" bytes\n", rot.ir_len);

	(void) close(fd);
	(void) printf("%s\tPASS\n", suite_name);
	return (0);
}
