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
 * This utility transitions a T6 from manufacturing to mission mode after
 * verifying the firmware versions and SROM VPD contents are as expected. If
 * there is a mismatch, then it will program the correct versions before
 * verifying again and moving on.
 */

#include <config_admin.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <libdevinfo.h>
#include <libgen.h>
#include <libipcc.h>
#include <libt6mfg.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/gpio/dpio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include "t6init.h"

#include <pcieb_ioctl.h>

static const char *progname;
static bool verbose;
static char dpiopath[sizeof ("/dev/dpio/") + DPIO_NAMELEN];

typedef struct t6init_discover {
	uint_t td_num;
	int32_t td_inst;
} t6init_discover_t;

typedef enum {
	T6INIT_MODE_MISSION,
	T6INIT_MODE_MFG,
} t6init_mode_t;

static const char *
t6init_modename(t6init_mode_t mode)
{
	switch (mode) {
	case T6INIT_MODE_MISSION:
		return ("mission");
	case T6INIT_MODE_MFG:
		return ("manufacturing");
	}
	return ("unknown");
}

static void
t6init_log(const char *fmt, va_list va)
{
	(void) vfprintf(stdout, fmt, va);
	(void) fprintf(stdout, "\n");
	(void) fflush(stdout);
}

static void
t6init_verbose(const char *fmt, ...)
{
	va_list va;

	if (!verbose)
		return;

	va_start(va, fmt);
	t6init_log(fmt, va);
	va_end(va);
}

static void __VPRINTFLIKE(2)
lipcc_verr(libipcc_handle_t *lih, const char *fmt, va_list va)
{
	(void) fprintf(stderr, "%s: ", progname);
	(void) vfprintf(stderr, fmt, va);

	(void) fprintf(stderr, ": %s: %s (libipcc: 0x%x, sys: %d)\n",
	    libipcc_errmsg(lih), libipcc_strerror(libipcc_err(lih)),
	    libipcc_err(lih), libipcc_syserr(lih));
}

static void __PRINTFLIKE(2)
lipcc_err(libipcc_handle_t *lih, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	lipcc_verr(lih, fmt, va);
	va_end(va);
}

static void __VPRINTFLIKE(2)
t6_verr(t6_mfg_t *t6mfg, const char *fmt, va_list va)
{
	(void) fprintf(stderr, "%s: ", progname);
	(void) vfprintf(stderr, fmt, va);

	(void) fprintf(stderr, ": %s: %s (libt6: 0x%x, sys: %u)\n",
	    t6_mfg_errmsg(t6mfg), t6_mfg_err2str(t6mfg, t6_mfg_err(t6mfg)),
	    t6_mfg_err(t6mfg), t6_mfg_syserr(t6mfg));
}

static void __PRINTFLIKE(2)
t6_err(t6_mfg_t *t6mfg, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	t6_verr(t6mfg, fmt, va);
	va_end(va);
}

__NORETURN static void
t6_fatal(t6_mfg_t *t6mfg, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	t6_verr(t6mfg, fmt, va);
	va_end(va);

	exit(EXIT_FAILURE);
}

/*
 * To deal with a series of Gen 2 related training failures that we've seen, we
 * purposefully try to limit the bridge to only operate at Gen 1 speeds during
 * manufacturing mode. This is something that can be cleared through the pcieb
 * driver logic on the T6's bridge.
 */
static bool
t6init_bridge_limit(uint32_t speed)
{
	/*
	 * XXX discover based on pcie16 mapping and don't hardcode /devices path
	 */
	const char *bridge = "/devices/pci@70,0/pci1de,fff9@1,1:devctl";
	int fd;
	pcieb_ioctl_target_speed_t ioc;
	bool ret = true;

	fd = open(bridge, O_RDWR);
	if (fd < 0) {
		warn("failed to open bridge %s", bridge);
		return (false);
	}

	(void) memset(&ioc, 0, sizeof (pcieb_ioctl_target_speed_t));
	ioc.pits_speed = speed;
	if (ioctl(fd, PCIEB_IOCTL_SET_TARGET_SPEED, &ioc) != 0) {
		warn("ioctl to set target speed to PCIe Gen %u failed", speed);
		ret = false;
	}

	(void) close(fd);
	return (ret);
}

/*
 * Retrieve the MAC addresses assigned by the service processor for use by the
 * host OS. For programming the dual port T6, there need to be at least
 * T6_MAC_COUNT addresses separated exactly by T6_MAC_STRIDE. Only the base
 * address is programmed and the second port is automatically given an address
 * which is the base + T6_MAC_STRIDE.
 */
static bool
retrieve_macaddr(libipcc_handle_t *lih, ether_addr_t *macp,
    char macstr[ETHERADDRSTRL])
{
	const struct ether_addr *base;
	libipcc_mac_t *mac;
	uint8_t stride;
	uint16_t count;

	t6init_verbose("Retrieving MAC addresses from SP");

	if (!libipcc_mac_nic(lih, &mac)) {
		lipcc_err(lih, "could not retrieve MACs from SP");
		return (false);
	}

	base = libipcc_mac_addr(mac);
	count = libipcc_mac_count(mac);
	stride = libipcc_mac_stride(mac);

	if (ether_ntoa_r(base, macstr) == NULL) {
		warnx("Could not convert MAC address to string");
		libipcc_mac_free(mac);
		return (false);
	}

	libipcc_mac_free(mac);

	t6init_verbose("    Base:   %s", macstr);
	t6init_verbose("    Count:  %x", count);
	t6init_verbose("    Stride: %x", stride);

	if (strcmp(macstr, "0:0:0:0:0:0") == 0) {
		/*
		 * This can occur if the SP is unable to retrieve the MAC
		 * address from the gimlet VPD.
		 */
		warnx("All zero MAC address from SP - '%s'", macstr);
		return (false);
	}

	if (count < T6_MAC_COUNT) {
		warnx("too few MAC addresses from SP, got %d, need at least %d",
		    count, T6_MAC_COUNT);
		return (false);
	}
	if (stride != T6_MAC_STRIDE) {
		warnx("MAC address stride incorrect, got %d, need %d",
		    stride, T6_MAC_STRIDE);
		return (false);
	}

	bcopy(base, macp, sizeof (*base));

	return (true);
}


/*
 * The kernel guarantees that the ident strings are NUL terminated, but not
 * much else.
 */
char *
cleanstr(const char *str)
{
	const char *end, *cp;
	size_t dstsize;
	char *dst;

	end = str + strlen((char *)str);

	while (str < end && isspace(*str))
		str++;
	while (str < end && isspace(*(end - 1)))
		end--;

	if (str >= end) {
		errno = EOVERFLOW;
		return (NULL);
	}

	dstsize = end - str + 1;
	dst = calloc(1, dstsize);
	if (dst == NULL)
		return (NULL);

	cp = str;
	for (size_t i = 0; i < dstsize - 1; i++) {
		char c;

		if (cp >= end)
			break;
		c = *cp;
		if (isspace(c) || !isprint(c))
			dst[i] = '-';
		else
			dst[i] = c;
		cp++;
	}

	return (dst);
}

static bool
retrieve_ident(libipcc_handle_t *lih, char **modelp, char **serialp)
{
	libipcc_ident_t *ident;
	char *model, *serial;

	t6init_verbose("Retrieving ident from SP");

	if (!libipcc_ident(lih, &ident)) {
		lipcc_err(lih, "could not retrieve ident from SP");
		return (false);
	}

	model = cleanstr((const char *)libipcc_ident_model(ident));
	if (model == NULL) {
		warn("could not clean ident model string");
		libipcc_ident_free(ident);
		return (false);
	}

	serial = cleanstr((const char *)libipcc_ident_serial(ident));
	if (serial == NULL) {
		warn("could not clean ident serial string");
		free(model);
		libipcc_ident_free(ident);
		return (false);
	}

	libipcc_ident_free(ident);

	t6init_verbose("       Model: '%s'", model);
	t6init_verbose("      Serial: '%s'", serial);

	*modelp = model;
	*serialp = serial;

	return (true);
}

static t6init_mode_t
get_dpio_mode(void)
{
	dpio_input_t val;
	t6init_mode_t mode;
	int fd;

	t6init_verbose("Reading DPIO status");

	fd = open(dpiopath, O_RDONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "Could not open dpio at '%s'", dpiopath);

	if (read(fd, &val, sizeof (val)) != sizeof (val))
		err(EXIT_FAILURE, "Could not read dpio status");

	VERIFY0(close(fd));

	mode = (val == DPIO_INPUT_HIGH) ? T6INIT_MODE_MISSION : T6INIT_MODE_MFG;

	t6init_verbose("    DPIO is %s (%s mode)",
	    val == DPIO_INPUT_HIGH ? "high" : "low", t6init_modename(mode));

	return (mode);
}

static void
set_dpio_mode(t6init_mode_t mode)
{
	dpio_output_t val;
	int fd;

	t6init_verbose("Setting DPIO for %s mode", t6init_modename(mode));

	val = (mode == T6INIT_MODE_MISSION) ?
	    DPIO_OUTPUT_HIGH : DPIO_OUTPUT_LOW;

	fd = open(dpiopath, O_WRONLY);
	if (fd == -1)
		err(EXIT_FAILURE, "Could not open dpio at '%s'", dpiopath);

	if (write(fd, &val, sizeof (val)) != sizeof (val))
		err(EXIT_FAILURE, "Could not set dpio status");

	VERIFY0(close(fd));
}

static void
show_help(void)
{
	(void) fprintf(stderr,
	    "Usage:\n"
	    "  To switch from mission to manufacturing mode:\n"
	    "        %1$s [-v] -D <name> -A <attach> -s <file> -f <file>\n"
	    "  To switch from manufacturing to mission mode:\n"
	    "        %1$s [-v] -M -D <name> -A <attach>\n"
	    "  Options:\n"
	    "        -M                   Switch from mission to mfg mode\n"
	    "        -D <dpio name>       Specify the T6 mode DPIO name\n"
	    "        -A <attach>          Specify the attachment point\n"
	    "        -s <srom file>       Specify the SROM firmware file\n"
	    "        -f <fw file>         Specify the flash firmware file\n"
	    "        -v                   Enable verbose output\n",
	    progname);
}

__NORETURN static void
usage(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	(void) vfprintf(stderr, fmt, va);
	va_end(va);
	(void) fprintf(stderr, "\n");

	show_help();
	exit(EXIT_USAGE);
}

static boolean_t
t6mfg_discover_cb(t6_mfg_disc_info_t *info, void *arg)
{
	t6init_discover_t *d = arg;

	d->td_num++;

	t6init_verbose("Found T6 in manufacturing mode:");
	t6init_verbose("    Instance: %d", info->tmdi_inst);
	t6init_verbose("        Path: %s", info->tmdi_path);
	t6init_verbose("         PCI: %x/%x", info->tmdi_vendid,
	    info->tmdi_devid);

	/*
	 * Just record the first one found, we will abort if there are
	 * more than one in any case.
	 */
	if (d->td_inst == 0)
		d->td_inst = info->tmdi_inst;

	return (B_TRUE);
}

static boolean_t
srom_validate_cb(const t6_mfg_validate_data_t *val, void *arg __unused)
{
	if (val->tval_flags == T6_VALIDATE_F_OK) {
		t6init_verbose("Region [0x%04x,0x%04x) OK", val->tval_addr,
		    val->tval_addr + val->tval_range);
		return (B_TRUE);
	}

	t6init_verbose("Region [0x%04x,0x%04x) INVALID!", val->tval_addr,
	    val->tval_addr + val->tval_range);

	if ((val->tval_flags & T6_VALIDATE_F_ERR_OPAQUE) != 0) {
		t6init_verbose("\tOpaque data mismatch: first incorrect byte "
		    "offset: 0x%x\n", val->tval_opaque_err);
	}

	if ((val->tval_flags & T6_VALIDATE_F_ERR_VPD_ERR) != 0)
		t6init_verbose("\tVPD Section mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_VPD_CKSUM) != 0)
		t6init_verbose("\t\tVPD Checksum mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_ID) != 0)
		t6init_verbose("\t\tProduct ID mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_PN) != 0)
		t6init_verbose("\t\tPart Number mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_SN) != 0)
		t6init_verbose("\t\tSerial Number mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_MAC) != 0)
		t6init_verbose("\t\tMAC Address mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_SS_VID) != 0)
		t6init_verbose("\t\tSub-system Vendor ID mismatch");
	if ((val->tval_flags & T6_VALIDATE_F_ERR_SS_DID) != 0)
		t6init_verbose("\t\tSub-system Device ID mismatch");

	/* Return false to indicate failure and stop iteration */
	return (B_FALSE);
}

static bool
verify_srom(t6_mfg_t *t6mfg)
{
	boolean_t ret;

	t6init_verbose("Verifying SROM");

	ret = t6_mfg_srom_validate(t6mfg, T6_MFG_SOURCE_DEVICE,
	    srom_validate_cb, NULL);

	if (!ret) {
		t6_err(t6mfg, "failed to verify flash");
		return (false);
	}

	t6init_verbose("SROM verification succeeded");
	return (true);
}

static bool
program_srom(t6_mfg_t *t6mfg)
{
	t6init_verbose("Programming SROM");

	if (!t6_mfg_srom_write(t6mfg, T6_MFG_SOURCE_DEVICE,
	    T6_SROM_WRITE_F_ALL)) {
		t6_err(t6mfg, "failed to program SROM");
		return (false);
	}

	return (true);
}

static boolean_t
flash_validate_cb(const t6_mfg_flash_vdata_t *regdata, void *arg __unused)
{
	t6init_verbose("Region [0x%07lx,0x%07lx)%s", regdata->tfv_addr,
	    regdata->tfv_addr + regdata->tfv_range,
	    (regdata->tfv_flags & T6_FLASH_VALIDATE_F_NO_SOURCE) != 0 ?
	    " (empty)" : "");

	if ((regdata->tfv_flags & T6_FLASH_VALIDATE_F_ERR) != 0) {
		t6init_verbose(" INVALID!\n\tOpaque data mismatch: first "
		    "incorrect byte offset: 0x%x\n", regdata->tfv_err);
		/* Return false to indicate failure and stop iteration */
		return (B_FALSE);
	}

	return (B_TRUE);
}

static bool
verify_flash(t6_mfg_t *t6mfg)
{
	boolean_t ret;

	t6init_verbose("Verifying flash");

	ret = t6_mfg_flash_validate(t6mfg, T6_MFG_SOURCE_DEVICE,
	    flash_validate_cb, NULL);

	if (!ret) {
		t6_err(t6mfg, "failed to verify flash");
		return (false);
	}

	t6init_verbose("T6 flash verification succeeded");
	return (true);
}

static bool
program_flash(t6_mfg_t *t6mfg)
{
	t6init_verbose("Programming flash");

	if (!t6_mfg_flash_write(t6mfg, T6_MFG_SOURCE_DEVICE,
	    T6_FLASH_WRITE_F_ALL)) {
		t6_err(t6mfg, "failed to program flash");
		return (false);
	}

	return (true);
}

__NORETURN static void
cfg_err(cfga_err_t cfgerrnum, char *estrp)
{
	const char *ep;

	ep = config_strerror(cfgerrnum);
	if (ep == NULL)
		ep = "configuration administration unknown error";
	if (estrp != NULL && *estrp != '\0')
		(void) fprintf(stderr, "%s: %s\n", ep, estrp);
	else
		(void) fprintf(stderr, "%s\n", ep);

	free(estrp);
	exit(EXIT_FAILURE);
}

static int
cfg_confirm(void *arg __unused, const char *msg)
{
	t6init_verbose("config confirm: %s", msg);
	return (1);
}

static int
cfg_msg(void *arg __unused, const char *msg)
{
	t6init_verbose("config message: %s", msg);
	return (1);
}

static bool
verify_mode(t6init_mode_t mode)
{
	di_node_t dnroot, dn;
	bool ret;

	dnroot = di_init("/", DINFOCPYALL);
	if (dnroot == DI_NODE_NIL)
		return (false);

	dn = di_drv_first_node(
	    (mode == T6INIT_MODE_MISSION) ? T6_MISSION_DRIVER : T6_MFG_DRIVER,
	    dnroot);
	ret = (dn != DI_NODE_NIL);
	di_fini(dnroot);

	t6init_verbose("Looking for T6 in %s mode: %s", t6init_modename(mode),
	    ret ? "SUCCESS" : "FAILED");

	return (ret);
}

static bool
start_mode(const char *ap, t6init_mode_t mode)
{
	cfga_err_t cfgerr;
	struct cfga_confirm conf = {
		.confirm = cfg_confirm,
	};
	struct cfga_msg msg = {
		.message_routine = cfg_msg,
	};
	char * const aplist[] = { (char *)ap };
	char *errstr;
	uint32_t speed;

	t6init_verbose("Switching to %s mode", t6init_modename(mode));

	t6init_verbose("    disconnecting %s", ap);
	cfgerr = config_change_state(CFGA_CMD_DISCONNECT, 1, aplist, NULL,
	    &conf, &msg, &errstr, CFGA_FLAG_FORCE | CFGA_FLAG_VERBOSE);
	if (cfgerr != CFGA_OK)
		cfg_err(cfgerr, errstr);
	free(errstr);

	set_dpio_mode(mode);

	/*
	 * We need to wait long enough after de-asserting PWREN_L for the SP
	 * to notice and for the sequencer to release CLD_RST_L and PERST_L.
	 * 1s is much longer than required.
	 */
	t6init_verbose("    sleeping for 1s or so");
	(void) sleep(1);

	speed = mode == T6INIT_MODE_MISSION ? PCIEB_LINK_SPEED_GEN3 :
	    PCIEB_LINK_SPEED_GEN1;
	t6init_verbose("    setting bridge limit to PCIe Gen %u", speed);
	if (!t6init_bridge_limit(speed)) {
		return (false);
	}

	t6init_verbose("    configuring %s", ap);
	cfgerr = config_change_state(CFGA_CMD_CONFIGURE, 1, aplist, NULL,
	    &conf, &msg, &errstr, CFGA_FLAG_FORCE | CFGA_FLAG_VERBOSE);
	if (cfgerr != CFGA_OK)
		cfg_err(cfgerr, errstr);
	free(errstr);

	if (verify_mode(mode)) {
		t6init_verbose("Successfully switched to %s mode",
		    t6init_modename(mode));
		return (true);
	}

	t6init_verbose("Failed to switch to %s mode", t6init_modename(mode));

	return (false);
}

int
main(int argc, char **argv)
{
	t6_mfg_t *t6mfg;
	ether_addr_t mac;
	char macstr[ETHERADDRSTRL];
	char errmsg[LIBIPCC_ERR_LEN];
	t6init_discover_t discover = { 0 };
	const char *dpioname = NULL, *attachment = NULL;
	const char *sromfile = NULL, *flashfile = NULL;
	t6init_mode_t mode = T6INIT_MODE_MISSION;
	uint16_t pci_ss_did = UINT16_MAX;
	char *model, *serial;
	libipcc_handle_t *lih;
	int c, bfd;

	progname = basename(argv[0]);

	while ((c = getopt(argc, argv, ":hMvD:A:s:f:P:")) != -1) {
		switch (c) {
		case 'A':
			attachment = optarg;
			break;
		case 'D':
			dpioname = optarg;
			break;
			break;
		case 'f':
			flashfile = optarg;
			break;
		case 'h':
			show_help();
			return (0);
		case 'M':
			mode = T6INIT_MODE_MFG;
			break;
		case 'P': {
			char *eptr;
			unsigned long l;

			errno = 0;
			l = strtoul(optarg, &eptr, 0);
			if (errno != 0 || *eptr != '\0') {
				errx(EXIT_FAILURE,
				    "failed to parse PCI sub-system ID: %s",
				    optarg);
			}

			if (l >= UINT16_MAX) {
				errx(EXIT_FAILURE,
				    "PCI sub-system ID is out of range "
				    "[0, UINT16_MAX): %lu", l);
			}
			pci_ss_did = (uint16_t)l;
			break;
		}
		case 's':
			sromfile = optarg;
			break;
		case 'v':
			verbose = true;
			break;
		case ':':
			usage("Option -%c requires an argument", optopt);
			break;
		case '?':
		default:
			usage("unknown option: -%c", optopt);
		}
	}

	if (dpioname == NULL || attachment == NULL)
		usage("-D and -A must always be specified");

	if (mode == T6INIT_MODE_MISSION &&
	    (flashfile == NULL || sromfile == NULL ||
	    pci_ss_did == UINT16_MAX)) {
		usage("-s, -f and -P are mandatory when switching to "
		    "mission mode");
	}

	if (snprintf(dpiopath, sizeof (dpiopath), "/dev/dpio/%s", dpioname) >=
	    sizeof (dpiopath)) {
		errx(EXIT_FAILURE, "Could not build dpio path");
	}

	if (mode == T6INIT_MODE_MFG) {
		if (get_dpio_mode() != T6INIT_MODE_MISSION) {
			fprintf(stderr, "DPIO is not set for mission mode\n");
			if (!verify_mode(T6INIT_MODE_MFG))
				errx(EXIT_FAILURE, "no mfg mode device found");
			return (0);
		}
		if (!start_mode(attachment, T6INIT_MODE_MFG))
			errx(EXIT_FAILURE, "failed to switch to mfg mode");

		return (0);
	}

	if (get_dpio_mode() == T6INIT_MODE_MISSION) {
		fprintf(stderr, "DPIO is already set for mission mode\n");
		if (!verify_mode(T6INIT_MODE_MISSION))
			errx(EXIT_FAILURE, "no mission mode device found");
		return (0);
	}

	if (access(flashfile, R_OK) != 0)
		err(EXIT_FAILURE, "cannot read firmware file '%s'", flashfile);
	if (access(sromfile, R_OK) != 0)
		err(EXIT_FAILURE, "cannot read SROM file '%s'", sromfile);

	/* Retrieve required information from the service processor */

	if (!libipcc_init(&lih, NULL, NULL, errmsg, sizeof (errmsg)))
		errx(EXIT_FAILURE, "Failed to init libipcc handle: %s", errmsg);

	if (!retrieve_ident(lih, &model, &serial))
		errx(EXIT_FAILURE, "failed to obtain ident");

	if (!retrieve_macaddr(lih, &mac, macstr))
		errx(EXIT_FAILURE, "failed to obtain MAC address");

	libipcc_fini(lih);

	/* Find a T6 in manufacturing mode */

	t6mfg = t6_mfg_init();
	if (t6mfg == NULL)
		errx(EXIT_FAILURE, "failed to create T6 library handle");

	t6_mfg_discover(t6mfg, t6mfg_discover_cb, &discover);
	if (discover.td_num == 0)
		errx(EXIT_FAILURE, "failed to find any T6 in mfg mode");
	if (discover.td_num != 1)
		errx(EXIT_FAILURE, "found more than one T6 in mfg mode");

	if (!t6_mfg_set_dev(t6mfg, discover.td_inst)) {
		t6_fatal(t6mfg, "Failed to set T6 device to instance %d",
		    discover.td_inst);
	}

	/* Verify/program SROM */

	if (!t6_mfg_srom_set_pn(t6mfg, model))
		t6_fatal(t6mfg, "failed to set model number");
	free(model);

	if (!t6_mfg_srom_set_sn(t6mfg, serial))
		t6_fatal(t6mfg, "failed to set serial number");
	free(serial);

	if (!t6_mfg_srom_set_mac(t6mfg, (uint8_t *)&mac))
		t6_fatal(t6mfg, "failed to set MAC address");

	if (!t6_mfg_srom_set_id(t6mfg, (char *)T6_PRODUCT_STR))
		t6_fatal(t6mfg, "failed to set product string");

	if (!t6_mfg_srom_set_pci_ss_vid(t6mfg, T6_PCI_SUBSYSTEM_VENDORID))
		t6_fatal(t6mfg, "failed to set PCI sub-system vendor ID");

	if (!t6_mfg_srom_set_pci_ss_did(t6mfg, pci_ss_did))
		t6_fatal(t6mfg, "failed to set PCI sub-system device ID");

	if ((bfd = open(sromfile, O_RDONLY)) < 0)
		err(EXIT_FAILURE, "failed to open srom file %s", sromfile);

	if (!t6_mfg_srom_set_base(t6mfg, bfd))
		t6_fatal(t6mfg, "failed to set SROM base source");

	if (!verify_srom(t6mfg)) {
		t6init_verbose("SROM verification failed, programming");
		if (!program_srom(t6mfg))
			errx(EXIT_FAILURE, "failed to program SROM");
		if (!verify_srom(t6mfg)) {
			errx(EXIT_FAILURE,
			    "SROM verification failed AFTER programming");
		}
	}

	VERIFY0(close(bfd));

	/* Verify/program flash */

	if ((bfd = open(flashfile, O_RDONLY)) < 0)
		err(EXIT_FAILURE, "failed to open flash file %s", flashfile);

	if (!t6_mfg_flash_set_base(t6mfg, T6_MFG_FLASH_BASE_FW, bfd))
		t6_fatal(t6mfg, "failed to set flash base source");

	if (!verify_flash(t6mfg)) {
		t6init_verbose("flash verification failed, programming");
		if (!program_flash(t6mfg))
			errx(EXIT_FAILURE, "failed to program flash");
		if (!verify_flash(t6mfg)) {
			errx(EXIT_FAILURE,
			    "flash verification failed AFTER programming");
		}
	}

	VERIFY0(close(bfd));

	t6_mfg_fini(t6mfg);

	/* RoT measurement here? */

	/* Switch to mission mode */
	if (!start_mode(attachment, T6INIT_MODE_MISSION))
		errx(EXIT_FAILURE, "failed to switch to mission mode");

	return (0);
}
