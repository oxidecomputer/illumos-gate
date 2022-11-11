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

#include <sys/ipcc.h>

int
main(int argc, char **argv)
{
	const char *suite_name = basename(argv[0]);
	ipcc_mac_t mac;

	int fd = open(IPCC_DEV, O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "could not open ipcc device");

	bzero(&mac, sizeof (mac));
	int ret = ioctl(fd, IPCC_MACS, &mac);
	if (ret < 0)
		err(EXIT_FAILURE, "IPCC_MACS ioctl failed");

	(void) close(fd);

	(void) printf("Base:   %x:%x:%x:%x:%x:%x\n",
	    mac.im_base[0], mac.im_base[1], mac.im_base[2],
	    mac.im_base[3], mac.im_base[4], mac.im_base[5]);
	(void) printf("Count:  %x\n", mac.im_count);
	(void) printf("Stride: %x\n", mac.im_stride);

	(void) printf("%s\tPASS\n", suite_name);
	return (0);
}
