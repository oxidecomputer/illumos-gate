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
 * Copyright 2025 Oxide Computer Company
 */

/*
 * A dtrace helper, responsible for managing the /etc/system and
 * /kernel/drv/dtrace.conf files in the SP to enable anonymous dtrace on Oxide
 * hardware.
 */

#include <fcntl.h>
#include <err.h>
#include <libdevinfo.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <libipcc.h>
#include <sys/ccompile.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#define	EXIT_USAGE	2

static const char *progname;
static libipcc_handle_t *ipcc_handle;

typedef struct cmdtab {
	const char *c_name;
	int (*c_op)(int, char **);
	void (*c_use)(FILE *);
} cmdtab_t;

static struct {
	const char *filename;
	uint8_t ipcc_key;
} files[] = {
	{ "/etc/system",		LIBIPCC_KEY_ETC_SYSTEM },
	{ "/kernel/drv/dtrace.conf",	LIBIPCC_KEY_DTRACE_CONF },
};

static const char *platforms[] = {
	"Oxide,Cosmo",
	"Oxide,Gimlet",
	"Oxide,RubyRed"
};

static bool
check_platform(void)
{
	const char *name;
	di_node_t did;
	bool ret = false;

	did = di_init("/", DINFOCACHE);
	if (did == DI_NODE_NIL)
		return (false);
	name = di_node_name(did);

	for (uint_t i = 0; i < ARRAY_SIZE(platforms); i++) {
		if (strcmp(name, platforms[i]) == 0) {
			ret = true;
			break;
		}
	}
	di_fini(did);

	return (ret);
}

static char *
strip_file(const char *filename)
{
	struct stat st;
	char *buf;
	char *line = NULL;
	size_t cap = 0;
	FILE *fp;

	if (stat(filename, &st) == -1)
		err(EXIT_FAILURE, "Could not stat '%s'", filename);

	fp = fopen(filename, "r");
	if (fp == NULL)
		err(EXIT_FAILURE, "Could not open '%s'", filename);

	buf = calloc(1, st.st_size);
	if (buf == NULL) {
		err(EXIT_FAILURE,
		    "Could not allocate 0x%zx bytes to buffer '%s'",
		    st.st_size, filename);
	}

	while (getline(&line, &cap, fp) != -1) {
		if (line[0] == '#' || line[0] == '*' || line[0] == '\n')
			continue;
		(void) strlcat(buf, line, st.st_size);
	}

	(void) fclose(fp);
	free(line);

	return (buf);
}

static void __PRINTFLIKE(2)
usage(const cmdtab_t *cmdtab, const char *format, ...)
{
	if (format != NULL) {
		va_list ap;

		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage: %s <subcommand> <args> ...\n"
	    "Available subcommands:\n", progname);

	if (cmdtab != NULL) {
		for (uint32_t cmd = 0; cmdtab[cmd].c_name != NULL; cmd++) {
			if (cmdtab[cmd].c_use != NULL) {
				cmdtab[cmd].c_use(stderr);
			} else {
				(void) fprintf(stderr, "\t%s\n",
				    cmdtab[cmd].c_name);
			}
		}
	}
}

static int
walk_tab(const cmdtab_t *cmdtab, int argc, char *argv[])
{
	uint32_t cmd;

	if (argc == 0) {
		usage(cmdtab, "missing required sub-command");
		exit(EXIT_USAGE);
	}

	for (cmd = 0; cmdtab[cmd].c_name != NULL; cmd++) {
		if (strcmp(argv[0], cmdtab[cmd].c_name) == 0)
			break;
	}

	if (cmdtab[cmd].c_name == NULL) {
		usage(cmdtab, "unknown sub-command: %s", argv[0]);
		exit(EXIT_USAGE);
	}

	argc--;
	argv++;
	optind = 0;

	return (cmdtab[cmd].c_op(argc, argv));
}

static void
anon_usage(FILE *f)
{
	(void) fprintf(f, "\tanon activate\n");
	(void) fprintf(f, "\tanon deactivate\n");
}

static int cmd_activate(int, char **);
static int cmd_deactivate(int, char **);

static const cmdtab_t anon_cmds[] = {
	{ "activate", cmd_activate, NULL },
	{ "deactivate", cmd_deactivate, NULL },
	{ NULL },
};

static int
cmd_activate(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;

	if (argc != 0) {
		usage(NULL, "unexpected additional arguments");
		anon_usage(stderr);
		return (EXIT_USAGE);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(files); i++) {
		char *content;

		content = strip_file(files[i].filename);

		if (!libipcc_keyset(ipcc_handle, files[i].ipcc_key,
		    (uint8_t *)content, strlen(content),
		    LIBIPCC_KEYF_COMPRESSED)) {
			fprintf(stderr, "Failed to store '%s' in SP: %s\n",
			    files[i].filename, libipcc_errmsg(ipcc_handle));
			ret = EXIT_FAILURE;
		} else {
			printf("Successfully stored '%s' in SP\n",
			    files[i].filename);
		}

		free(content);
	}

	return (ret);
}

static int
cmd_deactivate(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;

	if (argc != 0) {
		usage(NULL, "unexpected additional arguments");
		anon_usage(stderr);
		return (EXIT_USAGE);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(files); i++) {
		printf("Clearing '%s' from SP\n", files[i].filename);
		if (!libipcc_keyset(ipcc_handle, files[i].ipcc_key,
		    NULL, 0, 0)) {
			fprintf(stderr, "Failed to clear '%s' from SP: %s\n",
			    files[i].filename, libipcc_errmsg(ipcc_handle));
			ret = EXIT_FAILURE;
		}
	}

	return (ret);
}

static int
cmd_anon(int argc, char *argv[])
{
	if (argc == 0) {
		usage(anon_cmds, "missing required anon subcommand");
		return (EXIT_USAGE);
	}

	return (walk_tab(anon_cmds, argc, argv));
}

static const cmdtab_t cmds[] = {
	{ "anon", cmd_anon, anon_usage },
	{ NULL },
};

int
main(int argc, char *argv[])
{
	char errmsg[LIBIPCC_ERR_LEN];
	int rc;

	progname = basename(argv[0]);

	argc--;
	argv++;
	optind = 0;

	/*
	 * If this is not a supported platform, there is nothing to do.
	 */
	if (!check_platform())
		return (EXIT_SUCCESS);

	if (!libipcc_init(&ipcc_handle, NULL, NULL, errmsg, sizeof (errmsg)))
		errx(EXIT_FAILURE, "Failed to init libipcc handle: %s", errmsg);

	rc = walk_tab(cmds, argc, argv);

	libipcc_fini(ipcc_handle);

	return (rc);
}
