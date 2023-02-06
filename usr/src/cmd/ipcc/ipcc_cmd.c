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

/*
 * A userland IPCC client, mostly for testing.
 */

#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/ipcc.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include "ipcc_cmd.h"

const char *progname;
static int ipcc_fd;

static void
hexdump(const void *ptr, size_t length)
{
	const unsigned char *cp;
	const char delim = ' ';
	const int cols = 16;

	cp = ptr;
	for (uint_t i = 0; i < length; i += cols) {
		printf("%04x  ", i);

		for (uint_t j = 0; j < cols; j++) {
			uint_t k = i + j;
			if (k < length)
				printf("%c%02x", delim, cp[k]);
			else
				printf("   ");
		}

		printf("  |");
		for (uint_t j = 0; j < cols; j++) {
			uint_t k = i + j;
			if (k >= length)
				printf(" ");
			else if (cp[k] >= ' ' && cp[k] <= '~')
				printf("%c", cp[k]);
			else
				printf(".");
		}
		printf("|\n");
	}
}

static void
ipcc_init(void)
{
	ipcc_fd = open(IPCC_DEV, O_RDWR);

	if (ipcc_fd == -1)
		err(EXIT_FAILURE, "could not open ipcc device");
}

static int
ipcc_ident(int argc, char *argv[])
{
	ipcc_ident_t ident;

	bzero(&ident, sizeof (ident));
	if (ioctl(ipcc_fd, IPCC_IDENT, &ident) < 0)
		err(EXIT_FAILURE, "IPCC_IDENT ioctl failed");

	(void) printf("Serial: '%s'\n", ident.ii_serial);
	(void) printf("Model:  '%s'\n", ident.ii_model);
	(void) printf("Rev:    0x%x\n", ident.ii_rev);

	return (0);
}

static void
ipcc_keylookup_usage(FILE *f)
{
	(void) fprintf(f, "\tkeylookup <key> <buflen>\n");
}

static const char *
ipcc_keylookup_result(uint8_t result)
{
	switch (result) {
	case IPCC_KEYLOOKUP_SUCCESS:
		return ("Success");
	case IPCC_KEYLOOKUP_UNKNOWN_KEY:
		return ("Invalid key");
	case IPCC_KEYLOOKUP_NO_VALUE:
		return ("No value");
	case IPCC_KEYLOOKUP_BUFFER_TOO_SMALL:
		return ("Buffer too small");
	}
	return ("Unknown");
}

static int
ipcc_keylookup(int argc, char *argv[])
{
	ipcc_keylookup_t kl;
	const char *errstr;

	if (argc != 2) {
		fprintf(stderr, "Syntax: %s keylookup <key> <buflen>\n",
		    progname);
		switch (argc) {
		case 0:
			fprintf(stderr,
			    "missing required key and buffer length\n");
			break;
		case 1:
			fprintf(stderr, "missing required buffer length\n");
			break;
		default:
			fprintf(stderr,
			    "encountered extraneous arguments beginning "
			    "with '%s'\n", argv[2]);
			break;
		}
		return (EXIT_FAILURE);
	}

	bzero(&kl, sizeof (kl));
	kl.ik_key = strtonum(argv[0], 0, 255, &errstr);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "key is %s (range 0-255): %s",
		    errstr, argv[0]);
	}
	kl.ik_buflen = strtonum(argv[1], 0, 8192, &errstr);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "buflen is %s (range 0-8192): %s",
		    errstr, argv[1]);
	}

	kl.ik_buf = calloc(kl.ik_buflen, sizeof (*kl.ik_buf));
	if (kl.ik_buf == NULL)
		err(EXIT_FAILURE, "allocation failed");

	if (ioctl(ipcc_fd, IPCC_KEYLOOKUP, &kl) < 0)
		err(EXIT_FAILURE, "IPCC_KEYLOOKUP ioctl failed");

	(void) printf("Result: %u [%s] (length %u)\n",
	    kl.ik_result, ipcc_keylookup_result(kl.ik_result),  kl.ik_datalen);
	hexdump(kl.ik_buf, kl.ik_datalen);

	free(kl.ik_buf);

	return (0);
}

static void
ipcc_macs_usage(FILE *f)
{
	(void) fprintf(f, "\tmacs [group]\n");
}

static const char *
mac_group_name(uint8_t group)
{
	switch (group) {
	case IPCC_MAC_GROUP_ALL:
		return ("ALL");
	case IPCC_MAC_GROUP_NIC:
		return ("NIC");
	case IPCC_MAC_GROUP_BOOTSTRAP:
		return ("BOOTSTRAP");
	default:
		break;
	}
	return ("UNKNOWN");
}

static int
ipcc_macs(int argc, char *argv[])
{
	ipcc_mac_t mac;
	char buf[ETHERADDRSTRL];
	const char *errstr;

	if (argc != 0 && argc != 1) {
		fprintf(stderr, "Syntax: %s macs [group]\n", progname);
		return (EXIT_FAILURE);
	}

	bzero(&mac, sizeof (mac));

	if (argc == 1) {
		mac.im_group = strtonum(argv[0], 0, 255, &errstr);
		if (errstr != NULL) {
			errx(EXIT_FAILURE, "group is %s (range 0-255): %s",
			    errstr, argv[0]);
		}
	}

	if (ioctl(ipcc_fd, IPCC_MACS, &mac) < 0)
		err(EXIT_FAILURE, "IPCC_MACS ioctl failed");

	(void) printf("Group   %u (%s)\n", mac.im_group,
	    mac_group_name(mac.im_group));
	(void) printf("Base:   %s\n",
	    ether_ntoa_r((struct ether_addr *)&mac.im_base, buf));
	(void) printf("Count:  0x%x\n", mac.im_count);
	(void) printf("Stride: 0x%x\n", mac.im_stride);

	return (0);
}

static int
ipcc_status(int argc, char *argv[])
{
	ipcc_status_t status;

	bzero(&status, sizeof (status));
	if (ioctl(ipcc_fd, IPCC_STATUS, &status) < 0)
		err(EXIT_FAILURE, "IPCC_STATUS ioctl failed");

	(void) printf("Status:          0x%" PRIx64 "\n", status.is_status);
	(void) printf("Startup Options: 0x%" PRIx64 "\n", status.is_startup);

	return (0);
}

static int
ipcc_version(int argc, char *argv[])
{
	int version;

	version = ioctl(ipcc_fd, IPCC_GET_VERSION, 0);
	if (version < 0)
		err(EXIT_FAILURE, "IPCC_GET_VERSION ioctl failed");

	(void) printf("Kernel interface version: %d\n", version);

	return (0);
}

static const ipcc_cmdtab_t ipcc_cmds[] = {
	{ "ident", ipcc_ident, NULL },
	{ "keylookup", ipcc_keylookup, ipcc_keylookup_usage },
	{ "macs", ipcc_macs, ipcc_macs_usage },
	{ "status", ipcc_status, NULL },
	{ "version", ipcc_version, NULL },
	{ NULL, NULL, NULL }
};

void
ipcc_usage(const ipcc_cmdtab_t *cmdtab, const char *format, ...)
{
	if (format != NULL) {
		va_list ap;

		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage: %s <subcommand> <args> ...\n"
	    "Available subcommands:\n", progname);

	for (uint32_t cmd = 0; cmdtab[cmd].ic_name != NULL; cmd++) {
		if (cmdtab[cmd].ic_use != NULL)
			cmdtab[cmd].ic_use(stderr);
		else
			(void) fprintf(stderr, "\t%s\n", cmdtab[cmd].ic_name);
	}
}

static int
ipcc_walk_tab(const ipcc_cmdtab_t *cmdtab, int argc, char *argv[])
{
	uint32_t cmd;

	if (argc == 0) {
		ipcc_usage(cmdtab, "missing required sub-command");
		exit(EXIT_USAGE);
	}

	for (cmd = 0; cmdtab[cmd].ic_name != NULL; cmd++) {
		if (strcmp(argv[0], cmdtab[cmd].ic_name) == 0)
			break;
	}

	if (cmdtab[cmd].ic_name == NULL) {
		ipcc_usage(cmdtab, "unknown sub-command: %s", argv[0]);
		exit(EXIT_USAGE);
	}

	argc--;
	argv++;
	optind = 0;

	return (cmdtab[cmd].ic_op(argc, argv));
}

int
main(int argc, char *argv[])
{
	progname = basename(argv[0]);

	argc--;
	argv++;
	optind = 0;

	ipcc_init();

	return (ipcc_walk_tab(ipcc_cmds, argc, argv));
}
