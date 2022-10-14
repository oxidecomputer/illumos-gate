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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <stdbool.h>

#include <sys/param.h>
#include <sys/xcaller.h>

static void
usage(const char *name, int code)
{
	fprintf(stderr, "Usage: %s [-c cpu] [-t] <count>\n", name);
	exit(code);
}

int
main(int argc, char *argv[])
{
	int c_flag = -1;
	bool t_flag = false;
	int count;

	int opt;
	while ((opt = getopt(argc, argv, "htc:")) != -1) {
		switch (opt) {
		case 't':
			t_flag = true;
			break;
		case 'c':
			c_flag = atoi(optarg);
			break;
		case 'h':
			usage(argv[0], EXIT_SUCCESS);
			break;
		default:
			usage(argv[0], EXIT_FAILURE);
			break;
		}
	}
	if (optind >= argc) {
		usage(argv[0], EXIT_FAILURE);
	}
	count = atoi(argv[optind]);
	if (count <= 0) {
		usage(argv[0], EXIT_FAILURE);
	}

	int fd = open("/devices/pseudo/xcaller@0:xcaller", O_RDWR, 0);
	if (fd < 0) {
		err(EXIT_FAILURE, "Could not open xcaller device");
	}

	uint64_t *timings = NULL;
	if (t_flag) {
		timings = calloc(count, sizeof (uint64_t));
		if (timings == NULL) {
			err(EXIT_FAILURE, "Could not alloc buffer for timings");
		}
	}

	struct xcaller_basic_test test = {
		.xbt_count = count,
		.xbt_target = c_flag,
		.xbt_duration = 0,
		.xbt_timings = timings,
	};
	if (ioctl(fd, XCALLER_BASIC_TEST, &test) != 0) {
		err(EXIT_FAILURE, "Failed to execute test");
	}
	(void) close(fd);

	if (timings == NULL) {
		printf("Count:\t%d\n"
		    "Total Duration:\t%lluns\n"
		    "Avg. Duration:\t%fns\n",
		    count, test.xbt_duration,
		    ((double)test.xbt_duration) / count);
	} else {
		for (int i = 0; i < count; i++) {
			printf("%llu\n", timings[i]);
		}
		free(timings);
	}

	return (EXIT_SUCCESS);
}
