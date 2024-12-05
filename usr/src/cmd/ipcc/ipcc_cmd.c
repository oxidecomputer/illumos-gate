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
 * Copyright 2024 Oxide Computer Company
 */

/*
 * A userland IPCC client, for exercising libipcc.
 */

#include <err.h>
#include <fcntl.h>
#include <libcmdutils.h>
#include <libgen.h>
#include <libipcc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/hexdump.h>
#include <sys/ipcc.h>
#include <sys/param.h>
#include <sys/sha2.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/types.h>
#include <boot_image/oxide_boot_sp.h>

#include "ipcc_cmd.h"

const char *progname;
static bool ipcc_istty;

static libipcc_handle_t *ipcc_handle;

static void
ipcc_hexdump(const uint8_t *buf, size_t len)
{
	(void) hexdump_file(buf, len, HDF_DEFAULT, stdout);
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

static void __NORETURN
libipcc_fatal_impl(libipcc_err_t lerr, int32_t syserr, const char *errmsg)
{
	(void) fprintf(stderr, "libipcc error: '%s' (%s / %s)\n",
	    errmsg, libipcc_strerror(lerr),
	    syserr == 0 ? "no system errno" : strerror(syserr));
	exit(EXIT_FAILURE);
}

static void __PRINTFLIKE(1)
libipcc_fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	libipcc_fatal_impl(libipcc_err(ipcc_handle),
	    libipcc_syserr(ipcc_handle), libipcc_errmsg(ipcc_handle));
}

static uint8_t *
ipcc_mapfile(const char *filename, size_t *lenp)
{
	uint8_t *p;
	struct stat st;
	int fd;

	if ((fd = open(filename, O_RDONLY)) == -1)
		err(EXIT_FAILURE, "could not open '%s'", filename);

	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, "failed to stat '%s'", filename);

	p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED)
		err(EXIT_FAILURE, "failed to mmap 0x%zx bytes from '%s'",
		    st.st_size, filename);

	VERIFY0(close(fd));

	*lenp = st.st_size;
	return (p);
}

static void
ipcc_init(void)
{
	char errmsg[LIBIPCC_ERR_LEN];
	libipcc_err_t lerr;
	int32_t syserr;

	if (!libipcc_init(&ipcc_handle, &lerr, &syserr,
	    errmsg, sizeof (errmsg))) {
		(void) fprintf(stderr, "Could not init libipcc handle\n");
		libipcc_fatal_impl(lerr, syserr, errmsg);
	}
}

static void
ipcc_fini(void)
{
	libipcc_fini(ipcc_handle);
}

static int
ipcc_ident(int argc, char *argv[])
{
	libipcc_ident_t *ident;

	if (!libipcc_ident(ipcc_handle, &ident))
		libipcc_fatal("Could not retrieve ident");

	(void) printf("Serial: '%s'\n", libipcc_ident_serial(ident));
	(void) printf("Model:  '%s'\n", libipcc_ident_model(ident));
	(void) printf("Rev:    0x%x\n", libipcc_ident_rev(ident));

	libipcc_ident_free(ident);

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
	if (len != SHA256_DIGEST_LENGTH * 2) {
		warnx("hash length incorrect (got %zu, expected %u)",
		    len, SHA256_DIGEST_LENGTH * 2);
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
	char nnbuf[NN_NUMBUF_SZ];
	size_t buflen = sizeof (*hdr);

	/* The header is retrieved by specifying offset 0 */
	if (!libipcc_imageblock(ipcc_handle, hash, SHA256_DIGEST_LENGTH,
	    0, (uint8_t *)hdr, &buflen)) {
		libipcc_fatal("Image header request failed");
	}

	if (buflen == 0)
		errx(EXIT_FAILURE, "No response from MGS");

	printf("Received 0x%zx bytes\n", buflen);

	if (buflen < sizeof (*hdr))
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
	    memcmp(hash, hdr->obsh_sha256, sizeof (hdr->obsh_sha256)) == 0 ?
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
	uint8_t loop = 0;
	uint8_t report_interval = ipcc_istty ? 20 : UINT8_MAX;

	while (rem > 0) {
		size_t buflen = sizeof (buf);

		if (!libipcc_imageblock(ipcc_handle, hash,
		    SHA256_DIGEST_LENGTH, offset, buf, &buflen)) {
			libipcc_fatal("failed to read offset 0x%" PRIx64
			    " from SP", offset);
		}

		if (buflen > rem) {
			errx(EXIT_FAILURE,
			    "too much data returned for offset 0x%" PRIx64
			    ", len=0x%zx expected <= 0x%zx",
			    offset, buflen, rem);
		}

		offset += buflen;
		rem -= buflen;

		size_t done = 0;
		do {
			ssize_t ret;

			ret = write(fd, buf + done, buflen - done);

			if (ret > 0) {
				done += ret;
			} else if (errno != EINTR) {
				err(EXIT_FAILURE,
				    "writing to output file failed");
			}
		} while (done != buflen);

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
	(void) fprintf(f, "\tinventory [-c] [index]\n");
}

static int
ipcc_inventory(int argc, char *argv[])
{
	libipcc_inv_handle_t *liih;
	libipcc_inv_t *inv;
	const char *errstr;
	uint32_t idx;
	const uint8_t *data;
	uint32_t ver, nents;
	size_t datalen;
	libipcc_inv_status_t status;
	libipcc_inv_init_flag_t flags = 0;
	int c;

	while ((c = getopt(argc, argv, ":c")) != -1) {
		switch (c) {
		case 'c':
			flags |= LIBIPCC_INV_INIT_CACHE;
			break;
		case '?':
			fprintf(stderr, "Unknown option: -%c\n", optopt);
			ipcc_inventory_usage(stderr);
			return (EXIT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 1)
		errx(EXIT_USAGE, "inventory [-c] [index]\n");

	if (!libipcc_inv_hdl_init(ipcc_handle, &ver, &nents, flags, &liih))
		libipcc_fatal("Inventory init request failed");

	printf("metadata:\n    version: 0x%x\n    entries: 0x%x\n", ver, nents);
	if (argc == 0)
		return (0);

	idx = (uint32_t)strtonumx(argv[0], 0, UINT32_MAX, &errstr, 0);
	if (errstr != NULL) {
		errx(EXIT_FAILURE, "inventory index is %s (range 0-%u): %s",
		    errstr, UINT32_MAX, argv[0]);
	}
	if (!libipcc_inv(ipcc_handle, liih, idx, &inv))
		libipcc_fatal("Inventory request failed");

	status = libipcc_inv_status(inv);

	switch (status) {
	case LIBIPCC_INV_STATUS_SUCCESS:
	case LIBIPCC_INV_STATUS_IO_DEV_MISSING:
	case LIBIPCC_INV_STATUS_IO_ERROR: {
		size_t namelen;

		(void) printf("%s (%u) -- Result: %u [%s]\n",
		    libipcc_inv_name(inv, &namelen), idx,
		    status, libipcc_inv_status_str(status));
		break;
	}
	case LIBIPCC_INV_STATUS_INVALID_INDEX:
	default:
		(void) printf("unknown (%u) -- Result %u [%s]\n", idx,
		    status, libipcc_inv_status_str(status));
		goto out;
	}

	if (status != LIBIPCC_INV_STATUS_SUCCESS)
		goto out;

	data = libipcc_inv_data(inv, &datalen);
	(void) printf("Type %u, Payload: 0x%zx bytes\n",
	    libipcc_inv_type(inv), datalen);
	if (datalen > 0)
		ipcc_hexdump(data, datalen);

out:
	libipcc_inv_free(inv);
	libipcc_inv_hdl_fini(liih);
	return (0);
}

static struct {
	const char *key;
	uint8_t val;
} ipcc_keys[] = {
	{ "ping",		LIBIPCC_KEY_PING },
	{ "imageid",		LIBIPCC_KEY_INSTALLINATOR_IMAGE_ID },
	{ "inventory",		LIBIPCC_KEY_INVENTORY },
	{ "system",		LIBIPCC_KEY_ETC_SYSTEM },
	{ "dtrace",		LIBIPCC_KEY_DTRACE_CONF },
};

static void
ipcc_keylookup_usage(FILE *f)
{
	(void) fprintf(f, "\tkeylookup [-c] [-b <buflen>] <key>\n");
}

static int
ipcc_keylookup(int argc, char *argv[])
{
	libipcc_key_flag_t flags = 0;
	const char *errstr;
	bool keyfound = false, alloced = false;
	uint8_t key;
	uint8_t *buf;
	size_t buflen = 0;
	int c;

	while ((c = getopt(argc, argv, ":b:c")) != -1) {
		switch (c) {
		case 'b':
			/*
			 * This allows for testing the API with a
			 * caller-supplied buffer. Values in excess of
			 * IPCC_MAX_DATA_SIZE are permitted to facilitate
			 * testing edge conditions.
			 */
			buflen = strtonumx(optarg, 1, IPCC_MAX_DATA_SIZE * 2,
			    &errstr, 0);
			if (errstr != NULL) {
				errx(EXIT_FAILURE,
				    "buffer length is %s (range 1-%u): %s",
				    errstr, IPCC_MAX_DATA_SIZE * 2, optarg);
			}
			break;
		case 'c':
			flags |= LIBIPCC_KEYF_COMPRESSED;
			break;
		case '?':
			fprintf(stderr, "Unknown option: -%c\n", optopt);
			ipcc_keylookup_usage(stderr);
			return (EXIT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "%s: missing parameter:\n", progname);
		ipcc_keylookup_usage(stderr);
		fprintf(stderr, "Keys may be specified by name or number:\n");
		for (uint_t i = 0; i < ARRAY_SIZE(ipcc_keys); i++) {
			fprintf(stderr, "        %4d - %s\n",
			    ipcc_keys[i].val, ipcc_keys[i].key);
		}
		return (EXIT_USAGE);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(ipcc_keys); i++) {
		if (strcmp(argv[0], ipcc_keys[i].key) == 0) {
			key = ipcc_keys[i].val;
			keyfound = true;
			break;
		}
	}

	if (!keyfound) {
		key = strtonumx(argv[0], 0, 255, &errstr, 0);
		if (errstr != NULL) {
			errx(EXIT_FAILURE, "key is %s (range 0-255): %s",
			    errstr, argv[0]);
		}
	}

	if (buflen > 0) {
		buf = calloc(1, buflen);
		if (buf == NULL) {
			err(EXIT_FAILURE,
			    "could not allocate 0x%zx bytes for buffer",
			    buflen);
		}
		alloced = true;
	} else {
		/* Let the library allocate the buffer */
		buflen = 0;
		buf = NULL;
	}

	if (!libipcc_keylookup(ipcc_handle, key, &buf, &buflen, flags))
		libipcc_fatal("Failed to perform key lookup");

	(void) printf("(length %u)\n", buflen);
	ipcc_hexdump(buf, buflen);

	if (alloced)
		free(buf);
	else
		libipcc_keylookup_free(buf, buflen);

	return (0);
}

static void
ipcc_keyset_usage(FILE *f)
{
	(void) fprintf(f, "\tkeyset [-c] <key> <filename>\n");
	(void) fprintf(f, "\tkeyset -z <key>\n");
}

static int
ipcc_keyset(int argc, char *argv[])
{
	libipcc_key_flag_t flags = 0;
	const char *errstr, *keyname;
	bool keyfound = false;
	bool blank = false;
	size_t len;
	uint8_t key;
	uint8_t *data;
	int c;

	while ((c = getopt(argc, argv, ":cz")) != -1) {
		switch (c) {
		case 'c':
			flags |= LIBIPCC_KEYF_COMPRESSED;
			break;
		case 'z':
			blank = true;
			break;
		case '?':
			fprintf(stderr, "Unknown option: -%c\n", optopt);
			ipcc_keylookup_usage(stderr);
			return (EXIT_USAGE);
		}
	}

	argc -= optind;
	argv += optind;

	if ((blank && argc != 1) || (!blank && argc != 2)) {
		fprintf(stderr, "%s: missing parameter:\n", progname);
		ipcc_keyset_usage(stderr);
		fprintf(stderr, "Keys may be specified by name or number:\n");
		for (uint_t i = 0; i < ARRAY_SIZE(ipcc_keys); i++) {
			fprintf(stderr, "        %4d - %s\n",
			    ipcc_keys[i].val, ipcc_keys[i].key);
		}
		return (EXIT_USAGE);
	}

	keyname = argv[0];

	for (uint_t i = 0; i < ARRAY_SIZE(ipcc_keys); i++) {
		if (strcmp(keyname, ipcc_keys[i].key) == 0) {
			key = ipcc_keys[i].val;
			keyfound = true;
			break;
		}
	}

	if (!keyfound) {
		key = strtonumx(keyname, 0, 255, &errstr, 0);
		if (errstr != NULL) {
			errx(EXIT_FAILURE, "key is %s (range 0-255): %s",
			    errstr, keyname);
		}
	}

	if (blank) {
		if (!libipcc_keyset(ipcc_handle, key, NULL, 0, 0))
			libipcc_fatal("Failed to perform key blank operation");
		printf("Successfully cleared '%s'\n", keyname);
	} else {
		const char *filename = argv[1];
		data = ipcc_mapfile(filename, &len);

		if (!libipcc_keyset(ipcc_handle, key, data, len, flags))
			libipcc_fatal("Failed to perform key set operation");

		printf("Successfully set '%s'\n", keyname);

		VERIFY0(munmap(data, len));
	}
	return (0);
}

static void
ipcc_rot_usage(FILE *f)
{
	(void) fprintf(f, "\trot <filename>\n");
}

static int
ipcc_rot(int argc, char *argv[])
{
	libipcc_rot_resp_t *response;
	uint8_t *req;
	const uint8_t *data;
	size_t len;

	if (argc != 1) {
		fprintf(stderr, "%s: missing parameter:\n", progname);
		ipcc_rot_usage(stderr);
		return (EXIT_USAGE);
	}

	req = ipcc_mapfile(argv[0], &len);

	if (!libipcc_rot_send(ipcc_handle, req, len, &response))
		libipcc_fatal("Failed to perform RoT operation");
	VERIFY0(munmap(req, len));

	printf("Success\n");

	data = libipcc_rot_resp_get(response, &len);
	ipcc_hexdump(data, len);
	libipcc_rot_resp_free(response);

	return (0);
}

static void
ipcc_macs_usage(FILE *f)
{
	(void) fprintf(f, "\tmacs [group]\n");
}

static int
ipcc_macs(int argc, char *argv[])
{
	static const char *mac_groups[] = { "all", "nic", "bootstrap" };
	uint_t group = 0;
	const char *groupname;
	char buf[ETHERADDRSTRL];
	libipcc_mac_t *mac;
	uint_t i;
	bool ret;

	if (argc != 0 && argc != 1) {
		fprintf(stderr, "Syntax: %s macs [group]\n", progname);
		return (EXIT_FAILURE);
	}

	groupname = argc == 1 ? argv[0] : "all";

	for (i = 0; i < ARRAY_SIZE(mac_groups); i++) {
		if (strcmp(groupname, mac_groups[i]) == 0) {
			group = i;
			break;
		}
	}
	if (i >= ARRAY_SIZE(mac_groups)) {
		fprintf(stderr, "Invalid group '%s' choose from:", groupname);
		for (i = 0; i < ARRAY_SIZE(mac_groups); i++)
			fprintf(stderr, " %s", mac_groups[i]);
		fprintf(stderr, "\n");
		return (EXIT_FAILURE);
	}

	switch (group) {
	case 0:
		ret = libipcc_mac_all(ipcc_handle, &mac);
		break;
	case 1:
		ret = libipcc_mac_nic(ipcc_handle, &mac);
		break;
	case 2:
		ret = libipcc_mac_bootstrap(ipcc_handle, &mac);
		break;
	}

	if (!ret) {
		libipcc_fatal("Could not retrieve %s mac address(es)",
		    groupname);
	}

	(void) printf("Base:   %s\n",
	    ether_ntoa_r(libipcc_mac_addr(mac), buf));
	(void) printf("Count:  0x%x\n", libipcc_mac_count(mac));
	(void) printf("Stride: 0x%x\n", libipcc_mac_stride(mac));

	libipcc_mac_free(mac);

	return (0);
}

static int
ipcc_status(int argc, char *argv[])
{
	uint64_t status, startup;

	if (!libipcc_status(ipcc_handle, &status))
		libipcc_fatal("Could not retrieve status");
	(void) printf("Status:          0x%" PRIx64 "\n", status);

	if (!libipcc_startup_options(ipcc_handle, &startup))
		libipcc_fatal("Could not retrieve startup options");
	(void) printf("Startup Options: 0x%" PRIx64 "\n", startup);

	return (0);
}

static const ipcc_cmdtab_t ipcc_cmds[] = {
	{ "ident", ipcc_ident, NULL },
	{ "image", ipcc_image, ipcc_image_usage },
	{ "inventory", ipcc_inventory, ipcc_inventory_usage },
	{ "keylookup", ipcc_keylookup, ipcc_keylookup_usage },
	{ "keyset", ipcc_keyset, ipcc_keyset_usage },
	{ "macs", ipcc_macs, ipcc_macs_usage },
	{ "rot", ipcc_rot, ipcc_rot_usage },
	{ "status", ipcc_status, NULL },
	{ NULL, NULL, NULL }
};

int
main(int argc, char *argv[])
{
	int rc;

	progname = basename(argv[0]);

	argc--;
	argv++;
	optind = 0;

	ipcc_init();
	ipcc_istty = isatty(STDOUT_FILENO) == 1;

	rc = ipcc_walk_tab(ipcc_cmds, argc, argv);

	ipcc_fini();

	return (rc);
}
