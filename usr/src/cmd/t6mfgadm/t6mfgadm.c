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

/*
 * Interact with various T6 manufacturing tools.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <err.h>
#include <libgen.h>
#include <strings.h>
#include <ofmt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "t6mfgadm.h"

const char *t6mfgadm_progname;
t6_mfg_t *t6mfg;

static boolean_t t6mfgadm_istty;
static hrtime_t t6mfgadm_erase_start;

static void
t6mfgadm_list_usage(FILE *f)
{
	(void) fprintf(f, "\tlist [-H] [-o field[,...] [-p]]\n");
}

typedef enum t6mfgadm_list_field {
	T6MFGADM_LIST_INST = 1,
	T6MFGADM_LIST_PCI,
	T6MFGADM_LIST_PATH,
	T6MFGADM_LIST_SROM,
	T6MFGADM_LIST_FLASH
} t6mfgadm_list_field_t;

typedef struct t6mfgadm_list {
	uint32_t	tl_nfound;
	ofmt_handle_t	tl_ofmt;
} t6mfgadm_list_t;

void
t6mfgadm_err(const char *fmt, ...)
{
	va_list ap;

	(void) fprintf(stderr, "%s: ", t6mfgadm_progname);
	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);

	(void) fprintf(stderr, ": %s: %s (libt6: 0x%x, sys: %u)\n",
	    t6_mfg_errmsg(t6mfg), t6_mfg_err2str(t6mfg, t6_mfg_err(t6mfg)),
	    t6_mfg_err(t6mfg), t6_mfg_syserr(t6mfg));

	exit(EXIT_FAILURE);
}

void
t6mfgadm_ofmt_errx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verrx(EXIT_FAILURE, fmt, ap);
}

int32_t
t6mfgadm_device_parse(const char *str)
{
	long l;
	char *eptr;

	errno = 0;
	l = strtol(str, &eptr, 0);
	if (errno != 0 || *eptr != '\0') {
		errx(EXIT_FAILURE, "failed to parse device instance: %s", str);
	}

	if (l < 0 || l > INT32_MAX) {
		errx(EXIT_FAILURE, "parsed device instance is outside valid "
		    "range [0, INT32_MAX]: %ld", l);

	}

	return ((int32_t)l);
}

t6_mfg_source_t
t6mfgadm_setup_source(const char *dev, const char *file, boolean_t is_write,
    boolean_t srom)
{
	if (dev != NULL && file != NULL) {
		errx(EXIT_USAGE, "only one of -d and -f may be specified");
	} else if (file != NULL) {
		int fd;

		if (is_write) {
			fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0644);
		} else {
			fd = open(file, O_RDONLY);
		}
		if (fd < 0) {
			err(EXIT_FAILURE, "failed to open file %s",
			    file);
		}

		if (srom) {
			if (!t6_mfg_srom_set_file(t6mfg, fd)) {
				t6mfgadm_err("failed to set file %s", file);
			}
		} else {
			if (!t6_mfg_flash_set_file(t6mfg, fd)) {
				t6mfgadm_err("failed to set file %s", file);
			}
		}

		return (T6_MFG_SOURCE_FILE);
	} else if (dev != NULL) {
		int32_t inst;

		inst = t6mfgadm_device_parse(dev);
		if (!t6_mfg_set_dev(t6mfg, inst)) {
			t6mfgadm_err("failed to set T6 device to %s", dev);
		}

		return (T6_MFG_SOURCE_DEVICE);
	} else {
		errx(EXIT_USAGE, "at least one of -d and -f are required");
	}
}

void
t6mfgadm_progress_cb(const t6_mfg_progress_t *prog, void *arg)
{
	static boolean_t progress_printed = B_FALSE;
	const char *head, *tail;
	hrtime_t elapse;

	if (t6mfgadm_istty) {
		head = "\r";
		tail = "";
	} else {
		head = "";
		tail = "\n";
	}

	switch (prog->tmp_type) {
	case T6_MFG_PROG_ERROR:
		/*
		 * If we're in TTY mode then we'll want to add an extra newline
		 * here to make sure that we're not going to overwrite the last
		 * thing we printed, but only if we'ev ever printed something.
		 */
		if (t6mfgadm_istty && progress_printed) {
			(void) fputc('\n', stdout);
		}
		progress_printed = B_FALSE;
		break;
	case T6_MFG_PROG_IO_START:
	case T6_MFG_PROG_IO:
	case T6_MFG_PROG_IO_END:
		(void) printf("%sI/O: %8u/%u bytes (%4.1f%%)%s", head, prog->tmp_offset,
		    prog->tmp_total, (float)prog->tmp_offset /
		    (float)prog->tmp_total * (float)100, tail);
		progress_printed = B_TRUE;

		if (prog->tmp_type == T6_MFG_PROG_IO_END && t6mfgadm_istty) {
			(void) fputc('\n', stdout);
			progress_printed = B_FALSE;
		}
		break;
	case T6_MFG_PROG_ERASE_BEGIN:
		t6mfgadm_erase_start = gethrtime();
		(void) printf("Erasing... ");
		break;
	case T6_MFG_PROG_ERASE_END:
		elapse = gethrtime() - t6mfgadm_erase_start;
		elapse /= NANOSEC;
		(void) printf("done (%u seconds)\n", elapse);
		break;
	default:
		if (t6mfgadm_istty && progress_printed) {
			(void) fputc('\n', stdout);
		}
		(void) printf("encountered unknown progress type: 0x%x\n",
		    prog->tmp_type);
		progress_printed = B_FALSE;
		break;
	}

	/*
	 * Always flush stdout no matter what mode we're in.
	 */
	(void) fflush(stdout);
}

static boolean_t
t6mfgadm_list_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	t6_mfg_disc_info_t *info = ofarg->ofmt_cbarg;

	switch (ofarg->ofmt_id) {
	case T6MFGADM_LIST_INST:
		if (snprintf(buf, buflen, "%-4d", info->tmdi_inst) >= buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_LIST_PCI:
		if (snprintf(buf, buflen, "%x.%x", info->tmdi_vendid,
		    info->tmdi_devid) >= buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_LIST_PATH:
		if (snprintf(buf, buflen, "/devices%s", info->tmdi_path) >=
		    buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_LIST_SROM:
		if (snprintf(buf, buflen, "/devices%s:srom", info->tmdi_path) >=
		    buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_LIST_FLASH:
		if (snprintf(buf, buflen, "/devices%s:spidev", info->tmdi_path) >=
		    buflen) {
			return (B_FALSE);
		}
		break;
	default:
		abort();
	}

	return (B_TRUE);
}

static const char *t6mfgadm_list_fields = "inst,pci,path";
static ofmt_field_t t6mfgadm_list_ofmt[] = {
	{ "INST", 6, T6MFGADM_LIST_INST, t6mfgadm_list_ofmt_cb },
	{ "PCI", 12, T6MFGADM_LIST_PCI, t6mfgadm_list_ofmt_cb },
	{ "PATH", 50, T6MFGADM_LIST_PATH, t6mfgadm_list_ofmt_cb },
	{ "SROM", 50, T6MFGADM_LIST_SROM, t6mfgadm_list_ofmt_cb },
	{ "FLASH", 50, T6MFGADM_LIST_FLASH, t6mfgadm_list_ofmt_cb },
	{ NULL, 0, 0, NULL }
};

static boolean_t
t6mfgadm_list_cb(t6_mfg_disc_info_t *info, void *arg)
{
	t6mfgadm_list_t *list = arg;

	ofmt_print(list->tl_ofmt, info);
	list->tl_nfound++;
	return (B_TRUE);
}

static void
t6mfgadm_list_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s list [-H] [-o field[,...] [-p]]\n",
	    t6mfgadm_progname);

	(void) fprintf(stderr, "\nList T6 devices in manufacturing mode.\n"
	    "\t-H\t\tomit the column header\n"
	    "\t-o field\toutput fields to print\n"
	    "\t-p\t\tparsable output (reqiures -o)\n\n"
	    "The following fields are supported:\n"
	    "\tinst\tprint the device instance number\n"
	    "\tpci\tprint the vendor and device ID\n"
	    "\tpath\tprint the /devices path of the device\n"
	    "\tsrom\tprint the srom minor node of the device\n"
	    "\tflash\tprint the flash minor node of the device\n");
}

static int
t6mfgadm_list(int argc, char *argv[])
{
	int c;
	const char *fields = NULL;
	uint_t flags = 0;
	ofmt_status_t oferr;
	t6mfgadm_list_t list;
	boolean_t parse = B_FALSE;

	while ((c = getopt(argc, argv, ":Ho:p")) != -1) {
		switch (c) {
		case 'H':
			flags |= OFMT_NOHEADER;
			break;
		case 'o':
			fields = optarg;
			break;
		case 'p':
			flags |= OFMT_PARSABLE;
			parse = B_TRUE;
			break;
		case ':':
			t6mfgadm_list_help("option -%c requires an argunent",
			    optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_list_help("unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (parse && fields == NULL) {
		errx(EXIT_USAGE, "-p requires fields specified with -o");
	}

	if (fields == NULL) {
		fields = t6mfgadm_list_fields;
	}

	argc -= optind;
	argv += optind;
	if (argc != 0){
		errx(EXIT_FAILURE, "unkonwn extraneous arguments: %s", argv[0]);
	}


	oferr = ofmt_open(fields, t6mfgadm_list_ofmt, flags, 0, &list.tl_ofmt);
	ofmt_check(oferr, parse, list.tl_ofmt, t6mfgadm_ofmt_errx, warnx);

	list.tl_nfound = 0;
	t6_mfg_discover(t6mfg, t6mfgadm_list_cb, &list);

	if (list.tl_nfound == 0) {
		errx(EXIT_FAILURE, "failed to discover any T6 devices in "
		    "manufacturing mode");
	}

	return (EXIT_SUCCESS);
}

/*
 * Common interface for device read as these are the same modulo the type /
 * function.
 */
static void
t6mfgadm_dev_read_help(const char *type, const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s %s read -d device -o output\n",
	    t6mfgadm_progname, type);
	(void) fprintf(stderr, "\nRead the T6 %s image from a device.\n\n"
	    "\t-d device\tread from the specified T6 instance\n"
	    "\t-o output\twrite data to the specified file\n", type);

}

void
t6mfgadm_dev_read_setup(const char *type, int argc, char *argv[],
    t6mfgadm_info_t *info)
{
	int c, fd;
	int32_t inst;
	const char *dev = NULL, *output = NULL;
	boolean_t do_progress = B_FALSE;

	while ((c = getopt(argc, argv, ":d:o:P")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		case 'o':
			output = optarg;
			break;
		case 'P':
			do_progress = B_TRUE;
			break;
		case ':':
			t6mfgadm_dev_read_help(type, "option -%c requires an "
			    "argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_dev_read_help(type, "unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (dev == NULL) {
		errx(EXIT_USAGE, "missing required device to read from (-d)");
	}

	inst = t6mfgadm_device_parse(dev);
	if (!t6_mfg_set_dev(t6mfg, inst)) {
		t6mfgadm_err("failed to set T6 device to %s", dev);
	}
	info->ti_source = T6_MFG_SOURCE_DEVICE;

	if (output == NULL) {
		errx(EXIT_USAGE, "missing required output file (-o)");
	}

	fd = open(output, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		err(EXIT_FAILURE, "failed to open output file %s", output);
	}

	if (!t6_mfg_set_output(t6mfg, fd)) {
		t6mfgadm_err("failed to set output file to %s", output);
	}

	info->ti_dev = inst;
	info->ti_file = output;

	if (do_progress) {
		if (!t6_mfg_set_progress_cb(t6mfg, t6mfgadm_progress_cb,
		    NULL)) {
			t6mfgadm_err("failed to setup progress callbacks");
		}
	}
}


static const t6mfgadm_cmdtab_t t6mfgadm_cmds[] = {
	{ "list", t6mfgadm_list, t6mfgadm_list_usage },
	{ "srom", t6mfgadm_srom, t6mfgadm_srom_usage },
	{ "flash", t6mfgadm_flash, t6mfgadm_flash_usage },
	{ NULL, NULL, NULL }
};

void
t6mfgadm_usage(const t6mfgadm_cmdtab_t *cmdtab, const char *format, ...)
{
	if (format != NULL) {
		va_list ap;

		va_start(ap, format);
		vwarnx(format, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "usage:  %s <subcommand> <args> ... \n\n",
	    t6mfgadm_progname);

	for (uint32_t cmd = 0; cmdtab[cmd].tc_name != NULL; cmd++) {
		cmdtab[cmd].tc_use(stderr);
	}
}

int
t6mfgadm_walk_tab(const t6mfgadm_cmdtab_t *cmdtab, int argc, char *argv[])
{
	uint32_t cmd;

	if (argc == 0) {
		t6mfgadm_usage(cmdtab, "missing required sub-command");
		exit(EXIT_USAGE);
	}

	for (cmd = 0; cmdtab[cmd].tc_name != NULL; cmd++) {
		if (strcmp(argv[0], cmdtab[cmd].tc_name) == 0) {
			break;
		}
	}

	if (cmdtab[cmd].tc_name == NULL) {
		t6mfgadm_usage(cmdtab, "unknown sub-command: %s", argv[0]);
		exit(EXIT_USAGE);
	}

	argc -= 1;
	argv += 1;
	optind = 0;

	return (cmdtab[cmd].tc_op(argc, argv));
}

int
main(int argc, char *argv[])
{
	t6mfgadm_progname = basename(argv[0]);

	t6mfg = t6_mfg_init();
	if (t6mfg == NULL) {
		err(EXIT_FAILURE, "failed to create t6 library handle");
	}

	if (argc < 2) {
		t6mfgadm_usage(t6mfgadm_cmds, "missing required sub-command");
		exit(EXIT_USAGE);
	}

	argc -= 1;
	argv += 1;
	optind = 0;

	t6mfgadm_istty = isatty(STDOUT_FILENO) == 1;

	return (t6mfgadm_walk_tab(t6mfgadm_cmds, argc, argv));
}
