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
 * Copyright 2024 Oxide Computer Co.
 */

/*
 * Writes the contents of an APOB read out of memory from a file we can mmap,
 * normally a device.  This can work on both PCs and Oxide sleds; on PCs it
 * requires that firmware has kept the APOB around -- AMD's implementation will
 * -- and that you know where it is, which isn't really straightforward.  We try
 * a location that AMD uses on their reference machines but allow the user to
 * specify an offset that for /dev/xsvc corresponds to a physical address.
 *
 * The contents of the APOB are dumped to stdout in binary form.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/apob.h>
#include <sys/sysmacros.h>
#include <sys/stdbool.h>

#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>

static int src_fd = -1;
static bool opt_v;

typedef enum op {
	OP_NONE,
	OP_SAVE
} op_t;

static const struct {
	const char *opname;
	enum op op;
} optbl[] = {
	{ "save-apob", OP_SAVE }
};

static int
map_apob(apob_hdl_t *apob, const char *src, off_t off)
{
	const uint8_t *p = NULL;
	size_t mapped_size, real_size;
	off_t page_begin;
	off_t page_off;

	src_fd = open(src, O_RDONLY);
	if (src_fd < 0) {
		if (opt_v)
			warn("map_apob: open %s failed", src);
		goto fail;
	}

	page_begin = P2ALIGN(off, PAGESIZE);
	page_off = off - page_begin;
	mapped_size = APOB_MIN_LEN + page_off;

	p = mmap(NULL, mapped_size, PROT_READ, MAP_SHARED, src_fd, page_begin);
	if (p == MAP_FAILED) {
		if (opt_v) {
			warn("map_apob: header mmap (%zx@%lx) failed",
			    mapped_size, page_begin);
		}
		goto fail;
	}

	real_size = apob_init_handle(apob, p + page_off, APOB_MIN_LEN);
	if (real_size == 0) {
		if (opt_v) {
			warnc(apob_errno(apob),
			    "map_apob: failed to initialise APOB handle: %s",
			    apob_errmsg(apob));
		}
		goto fail;
	}

	(void) munmap((void *)p, mapped_size);
	mapped_size = real_size + page_off;
	p = mmap(NULL, mapped_size, PROT_READ, MAP_SHARED, src_fd, page_begin);
	if (p == MAP_FAILED) {
		if (opt_v) {
			warn("map_apob: APOB mmap (%zx@%lx) failed",
			    mapped_size, page_begin);
		}
		goto fail;
	}

	if (apob_init_handle(apob, p + page_off, real_size) != real_size) {
		if (opt_v) {
			warnc(apob_errno(apob),
			    "map_apob: failed to reinitialise APOB handle: %s",
			    apob_errmsg(apob));
		}
		goto fail;
	}

	return (0);

fail:
	if (p != NULL && p != MAP_FAILED)
		(void) munmap((void *)p, mapped_size);
	if (src_fd >= 0)
		(void) close(src_fd);
	src_fd = -1;
	return (-1);
}

static int
save_apob(apob_hdl_t *apob, int fd)
{
	const uint8_t *src_buf;
	size_t src_len;

	src_buf = apob_get_raw(apob);
	src_len = apob_get_len(apob);

	for (off_t i = 0; i < src_len; ) {
		ssize_t w = write(fd, src_buf + i, MIN(src_len - i, PAGESIZE));

		if (w < 0)
			return (-1);

		i += w;
	}

	return (0);
}

static const struct {
	const char *src;
	off_t off;
} try_srcs[] = {
	{ "/dev/apob", 0 },
	{ "/dev/xsvc", 0x4000000 }
};

static const char *usage_str =
"Usage: %s save-apob -f file [-s file [-o offset]] [-v]\n"
"\n"
"  -f file	Write output into <file>\n"
"  -o offset	Specify a 4-byte aligned starting offset within the source\n"
"  -s file	Use <file> (normally a device) as the data source\n"
"  -v		Write verbose output to standard error\n"
"\n"
"By default, each of the following data sources will be tried in turn\n"
"and data will be read from the first one that supplies a valid APOB:\n\n";

static void
usage(const char *pn)
{
	(void) fprintf(stderr, usage_str, pn);

	for (uint_t i = 0; i < ARRAY_SIZE(try_srcs); i++) {
		(void) fprintf(stderr, "\t%s @ %lx\n",
		    try_srcs[i].src, try_srcs[i].off);
	}

	(void) fprintf(stderr, "\n");

	exit(-1);
}

static void
cleanup(apob_hdl_t *apob)
{
	if (apob != NULL) {
		const uint8_t *src = apob_get_raw(apob);
		size_t len = apob_get_len(apob);

		if (src != NULL) {
			(void) munmap((void *)P2ALIGN((uintptr_t)src, PAGESIZE),
			    len + P2PHASE((uintptr_t)src, PAGESIZE));
		}

		free(apob);
	}

	(void) close(src_fd);
}

int
main(int argc, char *argv[])
{
	apob_hdl_t *apob;
	const char *opt_s = NULL;
	const char *opt_f = NULL;
	const char *errstr;
	const char *prog = argv[0];
	off_t opt_o = 0;
	op_t op = OP_NONE;
	int ch;

	if (argc < 2) {
		usage(prog);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(optbl); i++) {
		if (strcmp(argv[1], optbl[i].opname) == 0) {
			op = optbl[i].op;
			break;
		}
	}

	if (op == OP_NONE) {
		usage(prog);
	}

	struct option opts[] = {
		{ "output-file", required_argument, NULL, 'f' },
		{ "offset", required_argument, NULL, 'o' },
		{ "source-file", required_argument, NULL, 's' },
		{ "verbose", no_argument, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};

	while ((ch = getopt_long(argc, argv, "f:o:s:v", opts, NULL)) != -1) {
		switch (ch) {
		case 'f':
			opt_f = optarg;
			break;
		case 'o':
			opt_o = (off_t)strtonumx(optarg, 0, LONG_MAX,
			    &errstr, 0);
			if (errstr != NULL) {
				errx(-1, "offset %s is %s", optarg, errstr);
			} else if ((opt_o & 3) != 0) {
				errx(-1, "offset %lx is not 4-byte aligned",
				    opt_o);
			}
			break;
		case 's':
			opt_s = optarg;
			break;
		case 'v':
			opt_v = true;
			break;
		default:
			usage(prog);
			break;
		}
	}

	if ((opt_o != 0 && opt_s == NULL) || opt_f == NULL) {
		usage(prog);
	}

	apob = malloc(apob_handle_size());
	if (apob == NULL)
		err(1, "map_apob: failed to allocate APOB handle");

	if (opt_s != NULL) {
		if (map_apob(apob, opt_s, opt_o) < 0) {
			errx(-1, "failed to obtain APOB from %s@%lx",
			    opt_s, opt_o);
		}
	} else {
		for (uint_t i = 0; i < ARRAY_SIZE(try_srcs); i++) {
			if (opt_v) {
				(void) fprintf(stderr,
				    "trying APOB source %s@%lx... ",
				    try_srcs[i].src, try_srcs[i].off);
				(void) fflush(stderr);
			}
			if (map_apob(apob,
			    try_srcs[i].src, try_srcs[i].off) == 0) {
				if (opt_v) {
					(void) fprintf(stderr, "found\n");
					(void) fflush(stderr);
				}
				break;
			}
		}
		if (src_fd < 0) {
			errx(-1, "failed to find a usable source APOB");
		}
	}

	switch (op) {
	case OP_SAVE: {
		int out_fd = open(opt_f, O_WRONLY | O_CREAT | O_TRUNC, 0666);

		if (out_fd < 0) {
			err(-1, "unable to open output file '%s'", opt_f);
		}
		if (save_apob(apob, out_fd) != 0) {
			(void) unlink(opt_f);
			(void) close(out_fd);
			errx(-1, "failed to write APOB data to '%s'", opt_f);
		}
		if (close(out_fd) != 0) {
			errx(-1, "failed to close '%s'", opt_f);
		}
		break;
	}
	default:
		usage(prog);
	}

	cleanup(apob);

	return (0);
}
