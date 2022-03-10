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
 * t6mfgadm SROM tools
 *
 * The SROM is a small EEPROM that contains a number of different pieces of
 * configuration. While most of these are opaque to us, there are a few that are
 * important and that we can control:
 * 
 *   o The programmed Part Number
 *   o The programmed Serial Number
 *   o The programmed Base MAC address
 *   o The programmed PCI subsystem ID
 *
 * The first three of these are stored in a traditional PCI VPD set. The latter
 * is currently unknown. The T6 has 8 physical functions and each of them has a
 * copy of this metadata. Our job is to make sure that we can understand each of
 * these and the different offsets that they're at. For a given function, there
 * appears to be two different copies of this information. Because the set of
 * these is different in different locations, we note which treat these as two
 * different bits here.
 *
 * In general, we don't try to process the actual VPD sections themselves (as
 * there are lots of windows here). Rather, we rely on the fact that everything
 * is fairly standard and instead just keep track of what offsets we expect to
 * deal with what set of information at this time, sanity check that we have the
 * right actual offsets, and go from there.
 */

#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ofmt.h>
#include <unistd.h>
#include <string.h>

#include "t6mfgadm.h"

/* XXX Not checking for extraneous arguments */

static void
t6mfgadm_srom_read_usage(FILE *f)
{
	(void) fprintf(f, "\tsrom read -d device -o output\n");
}

static int
t6mfgadm_srom_read(int argc, char *argv[])
{
	t6mfgadm_info_t info;

	t6mfgadm_dev_read_setup("srom", argc, argv, &info);
	if (!t6_mfg_srom_read(t6mfg, info.ti_source, T6_SROM_READ_F_ALL)) {
		t6mfgadm_err("failed to read out SROM from device %d to "
		    "file %s", info.ti_dev, info.ti_file);
	}

	return (EXIT_SUCCESS);
}

/*
 * Both the write and verify endpoints are very similar in terms of options,
 * loading up the t6mfg handle with things found, etc. 
 */
static void
t6mfgadm_srom_common_init(boolean_t write, int argc, char *argv[],
    void (*helpf)(const char *, ...), t6_mfg_source_t *sourcep)
{
	int c, bfd;
	const char *base = NULL, *file = NULL, *mac = NULL, *pn = NULL;
	const char *sn = NULL, *product = NULL, *dev = NULL;

	while ((c = getopt(argc, argv, ":b:d:f:m:p:P:s:")) != -1) {
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
		case 'm':
			mac = optarg;
			break;
		case 'p':
			pn = optarg;
			break;
		case 'P':
			product = optarg;
			break;
		case 's':
			sn = optarg;
			break;
		case ':':
			helpf("option -%c requires an argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			helpf("unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (base == NULL) {
		errx(EXIT_USAGE, "a base file must be specified with -b");
	}

	*sourcep = t6mfgadm_setup_source(dev, file, write, B_TRUE);

	if ((bfd = open(base, O_RDONLY)) < 0) {
		err(EXIT_FAILURE, "failed to open base file %s", base);
	}

	if (!t6_mfg_srom_set_base(t6mfg, bfd)) {
		t6mfgadm_err("failed to set base source");
	}

	if (mac != NULL) {
		struct ether_addr e;

		if (ether_aton_r(mac, &e) == NULL) {
			errx(EXIT_FAILURE, "failed to parse mac adress %s",
			    mac);
		}

		if (!t6_mfg_srom_set_mac(t6mfg, (const uint8_t *)&e)) {
			t6mfgadm_err("failed to set MAC address");
		}
	}

	if (product != NULL) {
		if (!t6_mfg_srom_set_id(t6mfg, product)) {
			t6mfgadm_err("failed to set T6 product string");
		}
	}

	if (sn != NULL) {
		if (!t6_mfg_srom_set_sn(t6mfg, sn)) {
			t6mfgadm_err("failed to set serial number");
		}
	}

	if (pn != NULL) {
		if (!t6_mfg_srom_set_pn(t6mfg, pn)) {
			t6mfgadm_err("failed to set part number");
		}
	}
}

static void
t6mfgadm_srom_verify_usage(FILE *f)
{
	(void) fprintf(f, "\tsrom verify -b base -d device | -f file [-m mac] "
	    "[-p pn] [-P product]\n\t\t    [-s sn]\n");
}

static const char *t6mfgadm_srom_verify_str = "\n"
"Verify the specified SROM image aginst a base file. The optional flags\n"
"allow one to override the variable VPD data to check against. If not\n"
"specified, the values from the original SROM are used instead.\n\n"
"\t-b base\t\tuse the specified base file for verification\n"
"\t-d device\tverify the specified T6 instance\n"
"\t-f base\t\tverify the specified file\n"
"\t-m mac\t\tuse the specified MAC address for verification\n"
"\t-p pn\t\tuse the specified part number for verification\n"
"\t-P product\tuse the specified product name for verification\n"
"\t-s sn\t\tuse the specified serial number for verification\n";

static void
t6mfgadm_srom_verify_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s srom verify -b base -f base | -d "
	    "device [-m mac] [-p pn]\n\t    [-P product] [-s sn]\n",
	    t6mfgadm_progname);
	(void) fputs(t6mfgadm_srom_verify_str, stderr);
}

static boolean_t
t6mfgadm_srom_verify_cb(const t6_mfg_validate_data_t *val, void *arg)
{
	boolean_t *pass = arg;

	if (val->tval_flags == T6_VALIDATE_F_OK) {
		(void) printf("Region [0x%04x,0x%04x) OK\n", val->tval_addr,
		    val->tval_addr + val->tval_range);
		return (B_TRUE);
	}

	*pass = B_FALSE;
	(void) printf("Region [0x%04x,0x%04x) INVALID!\n", val->tval_addr,
	    val->tval_addr + val->tval_range);
	if ((val->tval_flags & T6_VALIDATE_F_ERR_OPAQUE) != 0) {
		(void) printf("\tOpaque data mismatch: first incorrect byte "
		    "offset: 0x%x\n", val->tval_opaque_err);
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_VPD_ERR) != 0) {
		(void) printf("\tVPD Section mismatch\n");
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_VPD_CKSUM) != 0) {
		(void) printf("\t\tVPD Checksum mismatch\n");
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_ID) != 0) {
		(void) printf("\t\tProduct ID mismatch\n");
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_PN) != 0) {
		(void) printf("\t\tPart Number mismatch\n");
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_SN) != 0) {
		(void) printf("\t\tSerial Number mismatch\n");
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_MAC) != 0) {
		(void) printf("\t\tMAC Address mismatch\n");
	}

	return (B_TRUE);
}

static int
t6mfgadm_srom_verify(int argc, char *argv[])
{
	boolean_t pass = B_TRUE;
	t6_mfg_source_t source;

	t6mfgadm_srom_common_init(B_FALSE, argc, argv,
	    t6mfgadm_srom_verify_help, &source);

	if (!t6_mfg_srom_validate(t6mfg, source, t6mfgadm_srom_verify_cb,
	    &pass)) {
		t6mfgadm_err("failed to run validation");
	}

	if (!pass) {
		errx(EXIT_FAILURE, "T6 SROM verification failed");
	}

	return (EXIT_SUCCESS);
}

static void
t6mfgadm_srom_write_usage(FILE *f)
{
	(void) fprintf(f, "\tsrom write -b base -d device | -f file [-m mac] "
	    "[-p pn] [-P product]\n\t\t   [-s sn]\n");
}

static const char *t6mfgadm_srom_write_str = "\n"
"Write an SROM image to a device or another file. The optional flags\n"
"allow one to override the variable VPD data to check against. If not\n"
"specified, the values from the original image are used instead.\n\n"
"\t-b base\t\tuse the specified base file for verification\n"
"\t-d device\twrite o the specified T6 instance\n"
"\t-f file\t\twrite to the specified file\n"
"\t-m mac\t\tuse the specified MAC address for verification\n"
"\t-p pn\t\tuse the specified part number for verification\n"
"\t-P product\tuse the specified product name for verification\n"
"\t-s sn\t\tuse the specified serial number for verification\n";

static void
t6mfgadm_srom_write_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s srom write -b base -f base | -d "
	    "device [-m mac] [-p pn]\n\t    [-P product] [-s sn]\n",
	    t6mfgadm_progname);
	(void) fputs(t6mfgadm_srom_write_str, stderr);
}

static int
t6mfgadm_srom_write(int argc, char *argv[])
{
	t6_mfg_source_t source;

	t6mfgadm_srom_common_init(B_TRUE, argc, argv, t6mfgadm_srom_write_help,
	    &source);

	if (!t6_mfg_srom_write(t6mfg, source, T6_SROM_WRITE_F_ALL)) {
		t6mfgadm_err("failed to write SROM");
	}

	return (EXIT_SUCCESS);
}

static void
t6mfgadm_srom_show_usage(FILE *f)
{
	(void) fprintf(f, "\tsrom show -f file | -d device [-H] "
	    "[-o field[,...] [-p]]\n");
}

static void
t6mfgadm_srom_show_help(const char *fmt, ...)
{
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	(void) fprintf(stderr, "Usage:  %s srom show -f file | -d device\n",
	    t6mfgadm_progname);

	(void) fprintf(stderr, "\nShow VPD and related from the T6 SROM.\n"
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

typedef enum t6mfgadm_srom_show_field {
	T6MFGADM_SROM_SHOW_OFFSET = 1,
	T6MFGADM_SROM_SHOW_FLAGS,
	T6MFGADM_SROM_SHOW_EXP,
	T6MFGADM_SROM_SHOW_PROD,
	T6MFGADM_SROM_SHOW_PN,
	T6MFGADM_SROM_SHOW_SN,
	T6MFGADM_SROM_SHOW_MAC
} t6mfgadm_srom_show_field_t;

static boolean_t
t6mfgadm_srom_show_flags_to_str(char *buf, uint_t buflen,
    t6_mfg_region_flags_t flags)
{
	return (snprintf(buf, buflen, "%c%c%c%c%c",
	    (flags & T6_REGION_F_CKSUM_VALID) != 0 ? 'C' : '-',
	    (flags & T6_REGION_F_ID_INFO) != 0 ? 'I' : '-',
	    (flags & T6_REGION_F_PN_INFO) != 0 ? 'P' : '-',
	    (flags & T6_REGION_F_SN_INFO) != 0 ? 'S' : '-',
	    (flags & T6_REGION_F_MAC_INFO) != 0 ? 'M' : '-') < buflen);
}

static boolean_t
t6mfgadm_srom_show_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const t6_mfg_region_data_t *reg = ofarg->ofmt_cbarg;

	switch (ofarg->ofmt_id) {
	case T6MFGADM_SROM_SHOW_OFFSET:
		if (snprintf(buf, buflen, "0x%04x", reg->treg_base) >= buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_SROM_SHOW_FLAGS:
		if (!t6mfgadm_srom_show_flags_to_str(buf, buflen,
		    reg->treg_flags)) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_SROM_SHOW_EXP:
		if (!t6mfgadm_srom_show_flags_to_str(buf, buflen,
		    reg->treg_exp)) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_SROM_SHOW_PROD:
		if ((reg->treg_flags & T6_REGION_F_ID_INFO) == 0) {
			(void) strlcpy(buf, "-", buflen);
			break;
		}

		if (strlcpy(buf, (const char *)reg->treg_id, buflen) >=
		    buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_SROM_SHOW_PN:
		if ((reg->treg_flags & T6_REGION_F_PN_INFO) == 0) {
			(void) strlcpy(buf, "-", buflen);
			break;
		}

		if (strlcpy(buf, (const char *)reg->treg_part, buflen) >=
		    buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_SROM_SHOW_SN:
		if ((reg->treg_flags & T6_REGION_F_SN_INFO) == 0) {
			(void) strlcpy(buf, "-", buflen);
			break;
		}

		if (strlcpy(buf, (const char *)reg->treg_serial, buflen) >=
		    buflen) {
			return (B_FALSE);
		}
		break;
	case T6MFGADM_SROM_SHOW_MAC:
		if ((reg->treg_flags & T6_REGION_F_MAC_INFO) == 0) {
			(void) strlcpy(buf, "-", buflen);
			break;
		}

		if (buflen < ETHERADDRSTRL) {
			return (B_FALSE);
		}

		(void) ether_ntoa_r((struct ether_addr *)reg->treg_mac, buf);
		break;
	default:
		abort();
	}
	return (B_TRUE);
}

static const char *t6mfgadm_srom_show_fields = "offset,flags,id,pn,sn,mac";
static ofmt_field_t t6mfgadm_srom_show_ofmt[] = {
	{ "OFFSET", 8, T6MFGADM_SROM_SHOW_OFFSET, t6mfgadm_srom_show_ofmt_cb },
	{ "FLAGS", 8, T6MFGADM_SROM_SHOW_FLAGS, t6mfgadm_srom_show_ofmt_cb },
	{ "EXP", 8, T6MFGADM_SROM_SHOW_EXP, t6mfgadm_srom_show_ofmt_cb },
	{ "ID", 16, T6MFGADM_SROM_SHOW_PROD, t6mfgadm_srom_show_ofmt_cb },
	{ "PN", 16, T6MFGADM_SROM_SHOW_PN, t6mfgadm_srom_show_ofmt_cb },
	{ "SN", 16, T6MFGADM_SROM_SHOW_SN, t6mfgadm_srom_show_ofmt_cb },
	{ "MAC", 18, T6MFGADM_SROM_SHOW_MAC, t6mfgadm_srom_show_ofmt_cb },
	{ NULL, 0, 0, NULL }
};

static boolean_t
t6mfgadm_srom_show_cb(const t6_mfg_region_data_t *reg, void *arg)
{
	ofmt_handle_t ofmt = arg;

	ofmt_print(ofmt, (void *)reg);
	return (B_TRUE);
}

static int
t6mfgadm_srom_show(int argc, char *argv[])
{
	int c;
	const char *file = NULL, *fields = NULL, *dev = NULL;
	ofmt_status_t oferr;
	boolean_t parse = B_FALSE;
	uint_t flags = 0;
	ofmt_handle_t ofmt;
	t6_mfg_source_t source;

	while ((c = getopt(argc, argv, ":f:d:Hop")) != -1) {
		switch (c) {
		case 'f':
			file = optarg;
			break;
		case 'd':
			dev = optarg;
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
			t6mfgadm_srom_show_help("option -%c requires an "
			    "argunent", optopt);
			exit(EXIT_USAGE);
		case '?':
			t6mfgadm_srom_show_help("unknown option -%c", optopt);
			exit(EXIT_USAGE);
		}
	}

	if (parse && fields == NULL) {
		errx(EXIT_USAGE, "-p requires fields specified with -o");
	}

	if (fields == NULL) {
		fields = t6mfgadm_srom_show_fields;
	}

	source = t6mfgadm_setup_source(dev, file, B_FALSE, B_TRUE);
	oferr = ofmt_open(fields, t6mfgadm_srom_show_ofmt, flags, 0, &ofmt);
	ofmt_check(oferr, parse, ofmt, t6mfgadm_ofmt_errx, warnx);

	if (!t6_mfg_srom_region_iter(t6mfg, source, t6mfgadm_srom_show_cb,
	    ofmt)) {
		t6mfgadm_err("failed to iterate regions");
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

static void
t6mfgadm_srom_wp_usage(FILE *f)
{
	(void) fprintf(f, "\tsrom write-protect -d device\n");
}

static int
t6mfgadm_srom_wp(int argc, char *argv[])
{
	errx(EXIT_FAILURE, "implement wp");
}

static const t6mfgadm_cmdtab_t t6mfgadm_cmds_srom[] = {
	{ "read", t6mfgadm_srom_read, t6mfgadm_srom_read_usage },
	{ "verify", t6mfgadm_srom_verify, t6mfgadm_srom_verify_usage },
	{ "write", t6mfgadm_srom_write, t6mfgadm_srom_write_usage },
	{ "show", t6mfgadm_srom_show, t6mfgadm_srom_show_usage },
	{ "write-protect", t6mfgadm_srom_wp, t6mfgadm_srom_wp_usage },
	{ NULL, NULL, NULL }
};

void
t6mfgadm_srom_usage(FILE *f)
{
	for (uint32_t cmd = 0; t6mfgadm_cmds_srom[cmd].tc_name != NULL;
	    cmd++) {
		t6mfgadm_cmds_srom[cmd].tc_use(stderr);
	}
}

int
t6mfgadm_srom(int argc, char *argv[])
{
	if (argc == 0) {
		t6mfgadm_usage(t6mfgadm_cmds_srom, "missing required srom "
		    "sub-command");
		exit(EXIT_USAGE);
	}

	return (t6mfgadm_walk_tab(t6mfgadm_cmds_srom, argc, argv));
}
