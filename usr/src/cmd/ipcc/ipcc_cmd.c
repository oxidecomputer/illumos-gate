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
#include <libcmdutils.h>
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
#include <sys/time.h>
#include <sys/types.h>
#include <boot_image/oxide_boot_sp.h>

#include "ipcc_cmd.h"

const char *progname;
static int ipcc_fd;
static bool ipcc_istty;

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

static bool
ipcc_image_hash(const char *arg, uint8_t *hash)
{
	size_t len = strlen(arg);

	/*
	 * arg is expected to be a string containing a 64 hex-digit hash
	 * that should be parsed into an array of uint8_ts.
	 */
	if (len != 64) {
		warnx("hash length incorrect (got %zu, expected 64)", len);
		return (false);
	}

	for (uint_t i = 0; i < len / 2; i++) {
		if (sscanf(arg + 2 * i, "%2hhx", &hash[i]) != 1) {
			warnx("hash parse failed at offset %u '%s'",
			    2 * i, arg + 2 * i);
			return (false);
		}
	}

	return (true);
}

static void
ipcc_image_info_usage(FILE *f)
{
	(void) fprintf(f, "\timage info <hash>\n");
}

static struct {
	uint64_t	flag;
	const char	*descr;
} header_flags[] = {
	{ OBSH_FLAG_COMPRESSED,		"Compressed" }
};

static void
ipcc_image_header(uint8_t *hash, oxide_boot_sp_header_t *hdr)
{
	ipcc_imageblock_t ib;
	char nnbuf[NN_NUMBUF_SZ];

	bcopy(hash, ib.ii_hash, sizeof (ib.ii_hash));
	ib.ii_buflen = sizeof (*hdr);
	ib.ii_buf = (uint8_t *)hdr;

	/* The header is retrieved by specifying offset 0 */
	ib.ii_offset = 0;
	if (ioctl(ipcc_fd, IPCC_IMAGEBLOCK, &ib) < 0)
		err(EXIT_FAILURE, "IPCC_IMAGEBLOCK ioctl failed");

	if (ib.ii_datalen == 0)
		errx(EXIT_FAILURE, "No response from MGS");

	printf("Received %u bytes\n", ib.ii_datalen);

	if (ib.ii_datalen < sizeof (*hdr))
		errx(EXIT_FAILURE, "MGS response too short for header");

	(void) printf("\nImage header:\n");
	(void) printf("       magic: 0x%" PRIx32 " (%s)\n", hdr->obsh_magic,
	    hdr->obsh_magic == OXBOOT_SP_MAGIC ? "correct" : "! INCORRECT");
	(void) printf("     version: 0x%" PRIx32 "\n", hdr->obsh_version);

	uint64_t flags = hdr->obsh_flags;
	(void) printf("       flags: 0x%" PRIx64 "\n", flags);
	for (uint_t i = 0; i < ARRAY_SIZE(header_flags); i++) {
		if ((flags & header_flags[i].flag) != 0) {
			(void) printf("              - %s\n",
			    header_flags[i].descr);
			flags &= ~header_flags[i].flag;
		}
	}
	if (flags != 0) {
		(void) printf("              - ! UNKNOWN (0x%" PRIx64 ")\n",
		    flags);
	}

	nicenum(hdr->obsh_data_size, nnbuf, sizeof (nnbuf));
	(void) printf("   data size: 0x%" PRIx64 " (%siB)\n",
	    hdr->obsh_data_size, nnbuf);
	nicenum(hdr->obsh_image_size, nnbuf, sizeof (nnbuf));
	(void) printf("  image size: 0x%" PRIx64 " (%siB)\n",
	    hdr->obsh_image_size, nnbuf);
	nicenum(hdr->obsh_target_size, nnbuf, sizeof (nnbuf));
	(void) printf(" target size: 0x%" PRIx64 " (%siB)\n",
	    hdr->obsh_target_size, nnbuf);
	(void) printf("        hash: %s\n",
	    memcmp(ib.ii_hash, hdr->obsh_sha256, sizeof (ib.ii_hash)) == 0 ?
	    "match" : "! MISMATCH");
	(void) printf("     dataset: %.*s\n", sizeof (hdr->obsh_dataset),
	    hdr->obsh_dataset);
	(void) printf("        name: %.*s\n", sizeof (hdr->obsh_imagename),
	    hdr->obsh_imagename);
}

static int
ipcc_image_info(int argc, char *argv[])
{
	oxide_boot_sp_header_t hdr;
	uint8_t hash[IPCC_IMAGE_HASHLEN];

	if (argc == 0)
		errx(EXIT_USAGE, "image info <hash>");

	if (!ipcc_image_hash(argv[0], hash))
		errx(EXIT_FAILURE, "could not parse hash '%s'", argv[0]);

	ipcc_image_header(hash, &hdr);

	return (0);
}

static void
ipcc_image_fetch_usage(FILE *f)
{
	(void) fprintf(f, "\timage fetch <hash> <output file>\n");
}

static int
ipcc_image_fetch(int argc, char *argv[])
{
	oxide_boot_sp_header_t hdr;
	uint8_t hash[IPCC_IMAGE_HASHLEN];
	uint8_t buf[IPCC_MAX_DATA_SIZE];
	int fd;

	if (argc != 2)
		errx(EXIT_USAGE, "image fetch <hash> <output file>");

	if (!ipcc_image_hash(argv[0], hash))
		errx(EXIT_FAILURE, "could not parse hash '%s'", argv[0]);

	ipcc_image_header(hash, &hdr);

	fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		err(EXIT_FAILURE, "failed to open output file '%s'", argv[1]);

	hrtime_t start = gethrtime();
	size_t total = OXBOOT_SP_HEADER_SIZE + hdr.obsh_data_size;
	size_t rem = total;
	uint64_t offset = 0;
	ipcc_imageblock_t ib;
	uint8_t loop = 0;
	uint8_t report_interval = ipcc_istty ? 20 : UINT8_MAX;

	bcopy(hash, ib.ii_hash, sizeof (ib.ii_hash));

	while (rem > 0) {
		ib.ii_buf = (uint8_t *)buf;
		ib.ii_buflen = sizeof (buf);
		ib.ii_offset = offset;

		if (ioctl(ipcc_fd, IPCC_IMAGEBLOCK, &ib) < 0) {
			err(EXIT_FAILURE,
			    "failed to read offset 0x" PRIx64 " from SP",
			    offset);
		}

		if (ib.ii_datalen > rem) {
			errx(EXIT_FAILURE,
			    "too much data returned for offset 0x" PRIx64
			    ", len=0x%x expected <= 0x%x",
			    offset, ib.ii_datalen, rem);
		}

		offset += ib.ii_datalen;
		rem -= ib.ii_datalen;

		size_t done = 0;
		do {
			ssize_t ret;

			ret = write(fd, buf + done, ib.ii_datalen - done);

			if (ret > 0) {
				done += ret;
			} else if (errno != EINTR) {
				err(EXIT_FAILURE,
				    "writing to output file failed");
			}
		} while (done != ib.ii_datalen);

		/* Report progress periodically */
		if (++loop == report_interval) {
			uint64_t secs = (gethrtime() - start) / SEC2NSEC(1);
			uint_t pct = 100UL * offset / hdr.obsh_data_size;
			uint64_t bw = 0;

			if (secs > 0)
				bw = (offset / secs) / 1024;

			if (ipcc_istty)
				printf("\r ");
			printf("received %016" PRIx64 "/ %016" PRIx64
			    " (%3u%%) %" PRIu64 "KiB/s",
			    offset, total, pct, bw);
			if (ipcc_istty) {
				printf("                \r");
				fflush(stdout);
			} else {
				printf("\n");
			}
			loop = 0;
		}
	}

	uint64_t secs = (gethrtime() - start) / SEC2NSEC(1);
	printf("transfer finished after %" PRIu64 " seconds, %" PRIu64 "KiB/s"
	    "                        \n",
	    secs, secs > 0 ? (total / secs) / 1024 : 0);

	VERIFY0(close(fd));

	return (0);
}

static const ipcc_cmdtab_t ipcc_image_cmds[] = {
	{ "info", ipcc_image_info, ipcc_image_info_usage },
	{ "fetch", ipcc_image_fetch, ipcc_image_fetch_usage },
	{ NULL, NULL, NULL }
};

static void
ipcc_image_usage(FILE *f)
{
	(void) fprintf(f, "\timage info <hash>\n");
	(void) fprintf(f, "\timage fetch <hash> <output file>\n");
}

static int
ipcc_image(int argc, char *argv[])
{
	if (argc == 0) {
		ipcc_usage(ipcc_image_cmds,
		    "missing required image subcommand");
		exit(EXIT_USAGE);
	}

	return (ipcc_walk_tab(ipcc_image_cmds, argc, argv));
}

static void
ipcc_inventory_usage(FILE *f)
{
	(void) fprintf(f, "\tinventory <index>\n");
}

static const char *
ipcc_inventory_status2str(uint8_t res)
{
	switch (res) {
	case IPCC_INVENTORY_SUCCESS:
		return ("success");
	case IPCC_INVENTORY_INVALID_INDEX:
		return ("invalid index");
	case IPCC_INVENTORY_IO_DEV_MISSING:
		return ("I/O error -- device gone?");
	case IPCC_INVENTORY_IO_ERROR:
		return ("I/O error");
	default:
		return ("unknown");
	}
}

static int
ipcc_inventory(int argc, char *argv[])
{
	ipcc_inventory_t inv;
	const char *errstr;

	if (argc != 1)
		errx(EXIT_USAGE, "inventory <index>\n");

	inv.iinv_idx = (uint32_t)strtonumx(argv[0], 0, UINT32_MAX, &errstr, 0);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "inventory index is %s (range 0-%u): %s",
		    errstr, UINT32_MAX, argv[0]);
	}
	if (ioctl(ipcc_fd, IPCC_INVENTORY, &inv) < 0)
		err(EXIT_FAILURE, "IPCC_INVENTORY ioctl failed");

	switch (inv.iinv_res) {
	case IPCC_INVENTORY_SUCCESS:
	case IPCC_INVENTORY_IO_DEV_MISSING:
	case IPCC_INVENTORY_IO_ERROR:
		(void) printf("%s (%u) -- Result: %u [%s]\n", inv.iinv_name,
		    inv.iinv_idx, inv.iinv_res,
		    ipcc_inventory_status2str(inv.iinv_res));
		break;
	case IPCC_INVENTORY_INVALID_INDEX:
	default:
		(void) printf("unknown (%u) -- Result %u [%s]\n", inv.iinv_idx,
		    inv.iinv_res, ipcc_inventory_status2str(inv.iinv_res));
		return (0);
	}

	if (inv.iinv_res != IPCC_INVENTORY_SUCCESS)
		return (0);
	(void) printf("Type %u, Payload: %u bytes\n", inv.iinv_type,
	    inv.iinv_data_len);
	if (inv.iinv_data_len > 0) {
		hexdump(inv.iinv_data, inv.iinv_data_len);
	}

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
	kl.ik_key = strtonumx(argv[0], 0, 255, &errstr, 0);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "key is %s (range 0-255): %s",
		    errstr, argv[0]);
	}
	kl.ik_buflen = strtonumx(argv[1], 0, 8192, &errstr, 0);
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
		mac.im_group = strtonumx(argv[0], 0, 255, &errstr, 0);
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
	{ "image", ipcc_image, ipcc_image_usage },
	{ "inventory", ipcc_inventory, ipcc_inventory_usage },
	{ "keylookup", ipcc_keylookup, ipcc_keylookup_usage },
	{ "macs", ipcc_macs, ipcc_macs_usage },
	{ "status", ipcc_status, NULL },
	{ "version", ipcc_version, NULL },
	{ NULL, NULL, NULL }
};

int
main(int argc, char *argv[])
{
	progname = basename(argv[0]);

	argc--;
	argv++;
	optind = 0;

	ipcc_init();
	ipcc_istty = isatty(STDOUT_FILENO) == 1;

	return (ipcc_walk_tab(ipcc_cmds, argc, argv));
}
