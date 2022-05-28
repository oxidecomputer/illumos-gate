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
 * t6mfgadm flash tools
 */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ofmt.h>
#include <string.h>

#include "t6mfgadm.h"

static void
t6mfgadm_flash_read_usage(FILE *f)
{
	(void) fprintf(f, "\tflash read -d device -o output\n");
}

static int
t6mfgadm_flash_read(int argc, char *argv[])
{
	t6mfgadm_info_t info;

	t6mfgadm_dev_read_setup("flash", argc, argv, &info);
	if (!t6_mfg_flash_read(t6mfg, info.ti_source, T6_FLASH_READ_F_ALL)) {
		t6mfgadm_err("failed to read flash from device %d to file %s",
		    info.ti_dev, info.ti_file);
	}

	return (EXIT_SUCCESS);
}

static void
t6mfgadm_flash_erase_usage(FILE *f)
{
	(void) fprintf(f, "\tflash erase -d device\n");
}

static void
t6mfgadm_flash_erase_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s flash erase -d device\n",
	    t6mfgadm_progname);
	(void) fprintf(stderr, "\nErase the T6 flash device.\n\n"
	    "\t-d device\tread from the specified T6 instance\n");
}

static int
t6mfgadm_flash_erase(int argc, char *argv[])
{
	int c;
	const char *dev = NULL;
	boolean_t do_progress = B_FALSE;
	int32_t inst;

	while ((c = getopt(argc, argv, ":d:P")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		case 'P':
			do_progress = B_TRUE;
			break;
		case ':':
			t6mfgadm_flash_erase_help("option -%c requires an "
			    "argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_flash_erase_help("unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (dev == NULL) {
		errx(EXIT_USAGE, "missing required device to erase (-d)");
	}

	inst = t6mfgadm_device_parse(dev);
	if (!t6_mfg_set_dev(t6mfg, inst)) {
		t6mfgadm_err("failed to set T6 device to %s", dev);
	}

	if (do_progress) {
		if (!t6_mfg_set_progress_cb(t6mfg, t6mfgadm_progress_cb,
		    NULL)) {
			t6mfgadm_err("failed to setup progress callbacks");
		}
	}

	if (!t6_mfg_flash_erase(t6mfg, T6_MFG_SOURCE_DEVICE,
	    T6_FLASH_ERASE_F_ALL)) {
		t6mfgadm_err("failed to erase device %d", inst);
	}

	return (EXIT_SUCCESS);
}


static void
t6mfgadm_flash_verify_usage(FILE *f)
{
	(void) fprintf(f, "\tflash verify -b base | -F file [-i] -d device | "
	    "-f file\n");
}

static const char *t6mfgadm_flash_verify_str = "\n"
"Verify the specified Flash image against a base file. Either an entire flash\n"
"image may be checked or instead a portion of one may be. When only a subset\n"
"is being checked, then unspecified regions will expect to be filled with\n"
"1s. Such regions may be ignored.\n\n"
"\t-b base\t\tuse the specified file as the entire flash image\n"
"\t-d device\tverify the specified T6 instance\n"
"\t-f file\t\tverify the specified file\n"
"\t-i\t\tignore regions with unknown data (don't check for 1s)\n"
"\t-F file\tUse file as the primary firmware file\n";

static void
t6mfgadm_flash_verify_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s flash verify -b base | -F file [-i] "
	    "-d device | -f file", t6mfgadm_progname);
	(void) fputs(t6mfgadm_flash_verify_str, stderr);
}

typedef struct t6mfgadm_flash_verify {
	boolean_t tfver_ignore;
	boolean_t tfver_pass;
} t6mfgadm_flash_verify_t;

static boolean_t
t6mfgadm_flash_verify_cb(const t6_mfg_flash_vdata_t *regdata, void *arg)
{
	t6mfgadm_flash_verify_t *verif = arg;

	(void) printf("Region [0x%07lx,0x%07lx)", regdata->tfv_addr,
	    regdata->tfv_addr + regdata->tfv_range);
	if ((regdata->tfv_flags & T6_FLASH_VALIDATE_F_ERR) != 0) {
		(void) printf(" INVALID!\n\tOpaque data mismatch: first "
		    "incorrect byte offset: 0x%x\n", regdata->tfv_err);

		if (verif->tfver_ignore &&
		    (regdata->tfv_flags & T6_FLASH_VALIDATE_F_NO_SOURCE) != 0) {
			(void) puts("\tIgnoring region error (-i specified)");
		} else {
			verif->tfver_pass = B_FALSE;
		}
	} else {
		(void) puts(" OK");
	}

	if ((regdata->tfv_flags & T6_FLASH_VALIDATE_F_NO_SOURCE) != 0) {
		(void) puts("\tRegion has no source data");
	}

	return (B_TRUE);
}

static int
t6mfgadm_flash_verify(int argc, char *argv[])
{
	int c;
	const char *file = NULL, *dev = NULL;
	const char *base = NULL, *fwfile = NULL;
	t6mfgadm_flash_verify_t verif;
	t6_mfg_source_t source;

	verif.tfver_ignore = B_FALSE;
	verif.tfver_pass = B_TRUE;

	while ((c = getopt(argc, argv, ":b:d:f:F:i")) != -1) {
		switch (c) {
		case 'b':
			base = optarg;
			break;
		case 'd':
			dev = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		case 'F':
			fwfile = optarg;
			break;
		case 'i':
			verif.tfver_ignore = B_TRUE;
			break;
		case ':':
			t6mfgadm_flash_verify_help("option -%c requires an "
			    "argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_flash_verify_help("unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	source = t6mfgadm_setup_source(dev, file, B_FALSE, B_FALSE);
	if (base != NULL) {
		int fd = open(base, O_RDONLY);
		if (fd < 0) {
			err(EXIT_FAILURE, "failed to open file %s", base);
		}

		if (!t6_mfg_flash_set_base(t6mfg, T6_MFG_FLASH_BASE_ALL, fd)) {
			t6mfgadm_err("failed to set base file to %s", base);
		}
	}

	if (fwfile != NULL) {
		int fd = open(fwfile, O_RDONLY);
		if (fd < 0) {
			err(EXIT_FAILURE, "failed to open file %s", fwfile);
		}

		if (!t6_mfg_flash_set_base(t6mfg, T6_MFG_FLASH_BASE_FW, fd)) {
			t6mfgadm_err("failed to set base firmware file to %s",
			    fwfile);
		}
	}

	if (base == NULL && fwfile == NULL) {
		errx(EXIT_FAILURE, "at least one of base file, -b or -F must "
		    "be specified");
	}
	if (!t6_mfg_flash_validate(t6mfg, source, t6mfgadm_flash_verify_cb,
	    &verif)) {
		t6mfgadm_err("internal flash validation logic failed");
	}

	if (!verif.tfver_pass) {
		errx(EXIT_FAILURE, "T6 Flash verification failed");
	}

	return (EXIT_SUCCESS);
}

static void
t6mfgadm_flash_write_usage(FILE *f)
{
	(void) fprintf(f, "\tflash write -F fwfile -d device | "
	    "-f file [-P]\n");
}

static const char *t6mfgadm_flash_write_str = "\n"
"Write a flash image to a device or another file. The flash image is sourced\n"
"from the firmware file argument (-F). Regions not covered by the firmware\n"
"file will be left uninitialized.\n"
"Note: this will induce an erase of the entire device.\n\n"
"\t-d device\twrite to the specified T6 instance\n"
"\t-f file\t\twrite to the specified file\n"
"\t-F file\tUse file as the primary firmware file\n"
"\t-P\t\toutput progress information\n";

static void
t6mfgadm_flash_write_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Uage:  %s flash write F fwfile "
	    "-d device | -f file [-P]\n", t6mfgadm_progname);
	(void) fputs(t6mfgadm_flash_write_str, stderr);
}

static int
t6mfgadm_flash_write(int argc, char *argv[])
{
	int c;
	const char *file = NULL, *dev = NULL;
	const char *fwfile = NULL;
	t6_mfg_source_t source;
	boolean_t do_progress = B_FALSE;

	while ((c = getopt(argc, argv, ":d:f:F:P")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		case 'F':
			fwfile = optarg;
			break;
		case 'P':
			do_progress = B_TRUE;
			break;
		case ':':
			t6mfgadm_flash_write_help("option -%c requires an "
			    "argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_flash_write_help("unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	source = t6mfgadm_setup_source(dev, file, B_TRUE, B_FALSE);

	if (fwfile != NULL) {
		int fd = open(fwfile, O_RDONLY);
		if (fd < 0) {
			err(EXIT_FAILURE, "failed to open file %s", fwfile);
		}

		if (!t6_mfg_flash_set_base(t6mfg, T6_MFG_FLASH_BASE_FW, fd)) {
			t6mfgadm_err("failed to set base firmware file to %s",
			    fwfile);
		}
	} else {
		errx(EXIT_FAILURE, "A firmware file with -F must be specified");
	}

	if (do_progress) {
		if (!t6_mfg_set_progress_cb(t6mfg, t6mfgadm_progress_cb,
		    NULL)) {
			t6mfgadm_err("failed to setup progress callbacks");
		}
	}

	if (!t6_mfg_flash_write(t6mfg, source, T6_FLASH_WRITE_F_ALL)) {
		t6mfgadm_err("failed to write flash device");
	}

	return (EXIT_SUCCESS);
}

static void
t6mfgadm_flash_hwinfo_usage(FILE *f)
{
	(void) fprintf(f, "\tflash hwinfo -d device\n");
}

static int
t6mfgadm_flash_hwinfo(int argc, char *argv[])
{
	errx(EXIT_FAILURE, "implement hwinfo");
}

static void
t6mfgadm_flash_wp_usage(FILE *f)
{
	(void) fprintf(f, "\tflash write-protect -d device\n");
}

static int
t6mfgadm_flash_wp(int argc, char *argv[])
{
	errx(EXIT_FAILURE, "implement wp");
}

static void
t6mfgadm_flash_version_usage(FILE *f)
{
	(void) fprintf(f, "\tflash versions -f file | -d device [-H] "
	    "[-o field[,...] [-p]]\n");
}

static void
t6mfgadm_flash_version_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s flash versions -f file | -d device\n",
	    t6mfgadm_progname);

	(void) fprintf(stderr, "\nShow T6 NOR flash firmware fersion "
	    "information.\n"
	    "\t-d device\tuse the specified T6 instance\n"
	    "\t-f file\t\tuse the specified file as input\n"
	    "\t-H\t\tomit the column header\n"
	    "\t-o field\toutput fields to print\n"
	    "\t-p\t\tparsable output (reqiures -o)\n\n"
	    "The following fields are supported:\n"
	    "\toffset\tprint the offset into the VPD\n"
	    "\tflags\tprint the set of valid data\n"
	    "\texp\tprint the set of data we hoped was valid\n"
	    "\tid\tprint the product ID\n"
	    "\tpn\tprint the part number\n"
	    "\tsn\tprint the serial number\n"
	    "\tmac\tprint the MAC address\n");
}

typedef enum t6mfgadm_flash_version_field {
	T6MFGADM_FLASH_SHOW_SEC = 1,
	T6MFGADM_FLASH_SHOW_VALID,
	T6MFGADM_FLASH_SHOW_VERS
} t6mfgadm_flash_version_field_t;

typedef struct t6mfgadm_flash_version_arg {
	const char *tfva_sec;
	boolean_t tfva_valid;
	const t6_mfg_flash_vers_t *tfva_vers;
} t6mfgadm_flash_version_arg_t;

static boolean_t
t6mfgadm_flash_version_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const t6mfgadm_flash_version_arg_t *show = ofarg->ofmt_cbarg;

	switch (ofarg->ofmt_id) {
	case T6MFGADM_FLASH_SHOW_SEC:
		if (strlcpy(buf, show->tfva_sec, buflen) >= buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_FLASH_SHOW_VALID:
		if (strlcpy(buf, show->tfva_valid ? "yes" : "no", buflen) >= buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_FLASH_SHOW_VERS:
		if (!show->tfva_valid) {
			if (strlcpy(buf, "-", buflen) >= buflen) {
				return (B_FALSE);
			}
		} else {
			if (snprintf(buf, buflen, "%u.%u.%u.%u",
			    show->tfva_vers->tmfv_major,
			    show->tfva_vers->tmfv_minor,
			    show->tfva_vers->tmfv_micro,
			    show->tfva_vers->tmfv_build) >= buflen) {
				return (B_TRUE);
			}
		}
		break;
	}
	return (B_TRUE);
}

static const char *t6mfgadm_flash_version_fields = "section,valid,version";
static ofmt_field_t t6mfgadm_flash_version_ofmt[] = {
	{ "SECTION", 16, T6MFGADM_FLASH_SHOW_SEC, t6mfgadm_flash_version_ofmt_cb },
	{ "VALID", 8, T6MFGADM_FLASH_SHOW_VALID, t6mfgadm_flash_version_ofmt_cb },
	{ "VERSION", 20, T6MFGADM_FLASH_SHOW_VERS, t6mfgadm_flash_version_ofmt_cb },
	{ NULL, 0, 0, NULL }
};

static int
t6mfgadm_flash_version(int argc, char *argv[])
{
	int c;
	const char *file = NULL, *dev = NULL, *fields = NULL;
	ofmt_status_t oferr;
	boolean_t parse = B_FALSE;
	uint_t flags = 0;
	ofmt_handle_t ofmt;
	t6_mfg_source_t source;
	const t6_mfg_flash_info_t *info;
	t6mfgadm_flash_version_arg_t arg;

	while ((c = getopt(argc, argv, ":d:f:Hop")) != -1) {
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		case 'f':
			file = optarg;
			break;
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
			t6mfgadm_flash_version_help("option -%c requires an "
			    "argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_flash_version_help("unknown option -%c",
			    optopt);
			exit(EXIT_USAGE);
		}

	}

	if (parse && fields == NULL) {
		errx(EXIT_USAGE, "-p requires fields specified with -o");
	}

	if (fields == NULL) {
		fields = t6mfgadm_flash_version_fields;
	}

	source = t6mfgadm_setup_source(dev, file, B_FALSE, B_FALSE);
	oferr = ofmt_open(fields, t6mfgadm_flash_version_ofmt, flags, 0, &ofmt);
	ofmt_check(oferr, parse, ofmt, t6mfgadm_ofmt_errx, warnx);
	info = t6_mfg_flash_img_info(t6mfg, source);
	if (info == NULL) {
		t6mfgadm_err("failed to read flash image information");
	}

	(void) memset(&arg, 0, sizeof (arg));
	arg.tfva_sec = "Main Firmware";
	arg.tfva_valid = (info->tmff_flags & T6_MFG_FLASH_F_FW_VERS_INFO) != 0;
	arg.tfva_vers = &info->tmff_fw_vers;
	ofmt_print(ofmt, (void *)&arg);

	arg.tfva_sec = "Microcode";
	arg.tfva_vers = &info->tmff_uc_vers;
	ofmt_print(ofmt, (void *)&arg);

	arg.tfva_sec = "Expansion ROM";
	arg.tfva_valid = (info->tmff_flags & T6_MFG_FLASH_F_EXP_VERS_INFO) != 0;
	arg.tfva_vers = &info->tmff_exp_vers;
	ofmt_print(ofmt, (void *)&arg);

	arg.tfva_sec = "Bootstrap";
	arg.tfva_valid = (info->tmff_flags & T6_MFG_FLASH_F_BS_VERS_INFO) != 0;
	arg.tfva_vers = &info->tmff_bs_vers;
	ofmt_print(ofmt, (void *)&arg);

	return (EXIT_SUCCESS);
}

static const t6mfgadm_cmdtab_t t6mfgadm_cmds_flash[] = {
	{ "read", t6mfgadm_flash_read, t6mfgadm_flash_read_usage },
	{ "verify", t6mfgadm_flash_verify, t6mfgadm_flash_verify_usage },
	{ "erase", t6mfgadm_flash_erase, t6mfgadm_flash_erase_usage },
	{ "write", t6mfgadm_flash_write, t6mfgadm_flash_write_usage },
	{ "hwinfo", t6mfgadm_flash_hwinfo, t6mfgadm_flash_hwinfo_usage },
	{ "versions", t6mfgadm_flash_version, t6mfgadm_flash_version_usage },
	{ "write-protect", t6mfgadm_flash_wp, t6mfgadm_flash_wp_usage },
	{ NULL, NULL, NULL }
};

void
t6mfgadm_flash_usage(FILE *f)
{
	for (uint32_t cmd = 0; t6mfgadm_cmds_flash[cmd].tc_name != NULL;
	    cmd++) {
		t6mfgadm_cmds_flash[cmd].tc_use(stderr);
	}
}

int
t6mfgadm_flash(int argc, char *argv[])
{
	if (argc == 0) {
		t6mfgadm_usage(t6mfgadm_cmds_flash, "missing required srom "
		    "sub-command");
		exit(EXIT_USAGE);
	}

	return (t6mfgadm_walk_tab(t6mfgadm_cmds_flash, argc, argv));
}
