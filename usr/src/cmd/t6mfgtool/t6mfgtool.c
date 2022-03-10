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
 * Copyright 2022 Oxide Computer, Inc.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stropts.h>
#include <sys/spi.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int sflash_chip_id(int argc, char *argv[], int start_arg, const char *t6mfg_node_path) {
	char *sflash_node_path;
	if (!asprintf(&sflash_node_path, "%s:spidev", t6mfg_node_path)) {
		fprintf(stderr, "Unable to allocate buffer\n");
		return (-1);
	}

	int sflash_fd = open(sflash_node_path, O_RDWR);
	if (sflash_fd < 0) {
		fprintf(stderr, "Failed to open sflash device node \"%s\": %s\n", sflash_node_path, strerror(errno));
		return (-1);
	}

	uint8_t device_id_tx[] = { 0x9f };
	uint8_t device_id_rx[3];

	spidev_transfer_t xfers[] = {
		{
			.tx_buf = device_id_tx,
			.rx_buf = NULL,
			.len = sizeof(device_id_tx) / sizeof(uint8_t),
			.delay_usec = 0,
			.bits_per_word = 0,
			.cs_change = 0,
			.cs_change_delay_usec = 0,
			.word_delay_usec = 0,
			.tx_width = 1,
			.rx_width = 1,
		},
		{
			.tx_buf = NULL,
			.rx_buf = device_id_rx,
			.len = sizeof(device_id_rx) / sizeof(uint8_t),
			.delay_usec = 0,
			.bits_per_word = 0,
			.cs_change = 1,
			.cs_change_delay_usec = 0,
			.word_delay_usec = 0,
			.tx_width = 1,
			.rx_width = 1,
		}
	};

	spidev_transaction_t xact = {
		xfers,
		sizeof(xfers) / sizeof(spidev_transfer_t),
	};

	int rc = ioctl(sflash_fd, SPIDEV_TRANSACTION, &xact);
	if (rc < 0) {
		(void) fprintf(stderr, "ioctl failed with error %d: %s\n", errno, strerror(errno));
	}

	int ii;
	for (ii = 0; ii < sizeof(device_id_rx) / sizeof(uint8_t); ++ii) {
		printf("%02x ", device_id_rx[ii]);
	}
	printf("\n");

	(void) close(sflash_fd);
	return (rc);
}

static void usage(FILE *fp, const char *progname) {
	(void) fprintf(fp, "Usage: %s <path to t6mfg#> [operation]\n", progname);
	(void) fprintf(fp,
	    "\tsflash-chip-id                 Report SFLASH's SPI FLASH chip ID\n"
		);

	exit(fp == stderr ? 1 : 0);
}

int main(int argc, char *argv[]) {
	if (argc == 2) {
		if (strcmp(argv[1], "-h") == 0 ||
		    strcmp(argv[1], "--help") == 0) {
			usage(stdout, argv[0]);
		}
	}

	if (argc < 3)
		usage(stderr, argv[0]);

	char *t6mfg_base_path = argv[1];

	if (strcmp(argv[2], "sflash-chip-id") == 0)
		return sflash_chip_id(argc, argv, 3, t6mfg_base_path);
	else
		usage(stderr, argv[0]);

	return (0);
}
