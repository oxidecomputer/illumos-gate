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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * A utility for interrogating and controlling the AMD Host System
 * Management Port (HSMP) via libhsmp.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <kstat.h>
#include <libgen.h>
#include <libhsmp.h>
#include <math.h>
#include <ofmt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ccompile.h>
#include <sys/types.h>

#define	EXIT_USAGE	2

typedef struct hsmp_cmdtab {
	const char *hc_name;
	int (*hc_op)(int, char **);
	void (*hc_usage)(FILE *);
} hsmp_cmdtab_t;

static const char *progname;

static libhsmp_handle_t *hsmp_handle;
static uint32_t hsmp_ntargets;
static bool hsmp_sock_set;
static uint32_t hsmp_sock;

static void
hsmp_usage(const hsmp_cmdtab_t *cmdtab)
{
	(void) fprintf(stderr, "Usage: %s [-s socket] <subcommand> ...\n"
	    "Available subcommands:\n", progname);

	for (uint32_t cmd = 0; cmdtab[cmd].hc_name != NULL; cmd++) {
		if (cmdtab[cmd].hc_usage != NULL)
			cmdtab[cmd].hc_usage(stderr);
		else
			(void) fprintf(stderr, "\t%s\n", cmdtab[cmd].hc_name);
	}
}

static int
hsmp_walk_tab(const hsmp_cmdtab_t *cmdtab, int argc, char **argv)
{
	uint32_t cmd;

	if (argc == 0) {
		warnx("missing required sub-command");
		hsmp_usage(cmdtab);
		exit(EXIT_USAGE);
	}

	for (cmd = 0; cmdtab[cmd].hc_name != NULL; cmd++) {
		if (strcmp(argv[0], cmdtab[cmd].hc_name) == 0)
			break;
	}

	if (cmdtab[cmd].hc_name == NULL) {
		warnx("unknown sub-command: %s", argv[0]);
		hsmp_usage(cmdtab);
		exit(EXIT_USAGE);
	}

	argc--;
	argv++;
	optind = 0;

	return (cmdtab[cmd].hc_op(argc, argv));
}

static void __PRINTFLIKE(1) __NORETURN
hsmp_fatal(const char *fmt, ...)
{
	const int32_t syserr = libhsmp_syserr(hsmp_handle);
	va_list ap;

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void) fprintf(stderr, "\n");

	(void) fprintf(stderr, "libhsmp error: '%s' (%s / %s)\n",
	    libhsmp_errmsg(hsmp_handle),
	    libhsmp_strerror(libhsmp_err(hsmp_handle)),
	    syserr == 0 ? "no system errno" : strerror(syserr));
	exit(EXIT_FAILURE);
}

static void __PRINTFLIKE(1) __NORETURN
hsmp_ofmt_die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
	exit(EXIT_USAGE);
}

/*
 * Parse the output formatting options that are common to the tabular
 * subcommands, consuming them from argc/argv, and construct an ofmt handle
 * for the provided field definitions.
 */
static ofmt_handle_t
hsmp_ofmt_getopt(int *argcp, char ***argvp, const ofmt_field_t *fields,
    const char *def_fields, void (*usage)(FILE *))
{
	const char *ofields = NULL;
	bool parse = false;
	uint_t flags = 0;
	ofmt_status_t oferr;
	ofmt_handle_t ofmt;
	int c;

	while ((c = getopt(*argcp, *argvp, "Ho:p")) != -1) {
		switch (c) {
		case 'H':
			flags |= OFMT_NOHEADER;
			break;
		case 'o':
			ofields = optarg;
			break;
		case 'p':
			parse = true;
			flags |= OFMT_PARSABLE;
			break;
		default:
			usage(stderr);
			exit(EXIT_USAGE);
		}
	}
	*argcp -= optind;
	*argvp += optind;

	if (parse && ofields == NULL)
		errx(EXIT_USAGE, "-p requires the use of -o");
	if (ofields == NULL)
		ofields = def_fields;

	oferr = ofmt_open(ofields, fields, flags, 0, &ofmt);
	ofmt_check(oferr, parse ? B_TRUE : B_FALSE, ofmt, hsmp_ofmt_die,
	    warnx);

	return (ofmt);
}

/*
 * Subcommands operate either on the socket nominated with the global -s
 * option or, in its absence, on every HSMP target in the system in turn.
 * hsmp_targ_selected() reports whether a target is covered by the current
 * selection.
 */
static bool
hsmp_targ_selected(libhsmp_target_t targ, uint32_t *sockp, uint32_t *iodp)
{
	uint32_t sock;

	if (!libhsmp_target_info(hsmp_handle, targ, &sock, iodp)) {
		hsmp_fatal("failed to get information for HSMP target %u",
		    targ);
	}
	if (sockp != NULL)
		*sockp = sock;
	return (!hsmp_sock_set || sock == hsmp_sock);
}

/*
 * Find the HSMP target that serves the nominated socket.
 */
static libhsmp_target_t
hsmp_targ_for_sock(uint32_t sock)
{
	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		uint32_t tsock;

		if (!libhsmp_target_info(hsmp_handle, t, &tsock, NULL)) {
			hsmp_fatal(
			    "failed to get information for HSMP target %u", t);
		}
		if (tsock == sock)
			return (t);
	}
	errx(EXIT_FAILURE, "no HSMP target was found for socket %u", sock);
}

static uint32_t
hsmp_parse_u32(const char *arg, uint32_t max, const char *what)
{
	const char *errstr;
	long long val;

	val = strtonumx(arg, 0, max, &errstr, 0);
	if (errstr != NULL) {
		errx(EXIT_USAGE, "failed to parse %s '%s': %s", what, arg,
		    errstr);
	}

	return ((uint32_t)val);
}

/*
 * Parse a power value. A bare number, or one with an "mW" suffix, is a
 * whole number of milliwatts. A number with a "W" suffix may have a
 * fractional part and is converted to milliwatts, rounded to the nearest.
 */
static uint32_t
hsmp_parse_power(const char *arg)
{
	bool watts = false;
	double val;
	char *end;

	errno = 0;
	val = strtod(arg, &end);
	if (!isdigit((uchar_t)arg[0]) || errno != 0 || end == arg)
		errx(EXIT_USAGE, "invalid power value '%s'", arg);

	if (strcmp(end, "W") == 0) {
		watts = true;
		val *= 1000.0;
	} else if (*end != '\0' && strcmp(end, "mW") != 0) {
		errx(EXIT_USAGE, "invalid power value suffix '%s'", end);
	}

	/*
	 * The value must be bounded before it can be converted to an
	 * integer type. This form also rejects NaN.
	 */
	if (!(val >= 0.0 && val <= (double)UINT32_MAX))
		errx(EXIT_USAGE, "power value '%s' is out of range", arg);

	if (watts) {
		val = round(val);
	} else if (val != (double)(uint32_t)val) {
		errx(EXIT_USAGE,
		    "power value '%s' must be a whole number of mW", arg);
	}

	return ((uint32_t)val);
}

typedef enum {
	HSMP_VERSION_F_SOCKET,
	HSMP_VERSION_F_IOD,
	HSMP_VERSION_F_SMU,
	HSMP_VERSION_F_IFVER,
} hsmp_version_field_t;

typedef struct hsmp_version_row {
	uint32_t hvr_sock;
	uint32_t hvr_iod;
	libhsmp_smu_version_t hvr_smu;
	uint32_t hvr_ifver;
} hsmp_version_row_t;

static boolean_t
hsmp_version_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_version_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_version_field_t)ofarg->ofmt_id) {
	case HSMP_VERSION_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hvr_sock);
		break;
	case HSMP_VERSION_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hvr_iod);
		break;
	case HSMP_VERSION_F_SMU:
		len = snprintf(buf, buflen, "%u.%u.%u",
		    row->hvr_smu.lsv_major, row->hvr_smu.lsv_minor,
		    row->hvr_smu.lsv_patch);
		break;
	case HSMP_VERSION_F_IFVER:
		len = snprintf(buf, buflen, "%u", row->hvr_ifver);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_VERSION_FIELDS	"socket,iod,smu,ifver"

static const ofmt_field_t hsmp_version_fields[] = {
	{ "SOCKET",	9,	HSMP_VERSION_F_SOCKET,	hsmp_version_ofmt_cb },
	{ "IOD",	5,	HSMP_VERSION_F_IOD,	hsmp_version_ofmt_cb },
	{ "SMU",	13,	HSMP_VERSION_F_SMU,	hsmp_version_ofmt_cb },
	{ "IFVER",	6,	HSMP_VERSION_F_IFVER,	hsmp_version_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_version_usage(FILE *f)
{
	(void) fprintf(f, "\tversion [-Hp] [-o field[,...]]\n");
}

static int
hsmp_version(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_version_fields,
	    HSMP_VERSION_FIELDS, hsmp_version_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "version does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_version_row_t row;

		if (!hsmp_targ_selected(t, &row.hvr_sock, &row.hvr_iod))
			continue;
		if (!libhsmp_smu_version(hsmp_handle, t, &row.hvr_smu)) {
			hsmp_fatal(
			    "failed to read the SMU version for socket %u",
			    row.hvr_sock);
		}
		if (!libhsmp_interface_version(hsmp_handle, t,
		    &row.hvr_ifver)) {
			hsmp_fatal("failed to read the interface version "
			    "for socket %u", row.hvr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_POWER_F_SOCKET,
	HSMP_POWER_F_IOD,
	HSMP_POWER_F_POWER,
	HSMP_POWER_F_LIMIT,
	HSMP_POWER_F_MAX,
} hsmp_power_field_t;

typedef struct hsmp_power_row {
	uint32_t hpr_sock;
	uint32_t hpr_iod;
	uint32_t hpr_power;
	uint32_t hpr_limit;
	uint32_t hpr_max;
} hsmp_power_row_t;

static boolean_t
hsmp_power_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_power_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_power_field_t)ofarg->ofmt_id) {
	case HSMP_POWER_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hpr_sock);
		break;
	case HSMP_POWER_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hpr_iod);
		break;
	case HSMP_POWER_F_POWER:
		len = snprintf(buf, buflen, "%.3f", row->hpr_power / 1000.0);
		break;
	case HSMP_POWER_F_LIMIT:
		len = snprintf(buf, buflen, "%.3f", row->hpr_limit / 1000.0);
		break;
	case HSMP_POWER_F_MAX:
		len = snprintf(buf, buflen, "%.3f", row->hpr_max / 1000.0);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_POWER_FIELDS	"socket,iod,power,limit,max"

static const ofmt_field_t hsmp_power_fields[] = {
	{ "SOCKET",	9,	HSMP_POWER_F_SOCKET,	hsmp_power_ofmt_cb },
	{ "IOD",	5,	HSMP_POWER_F_IOD,	hsmp_power_ofmt_cb },
	{ "POWER",	12,	HSMP_POWER_F_POWER,	hsmp_power_ofmt_cb },
	{ "LIMIT",	12,	HSMP_POWER_F_LIMIT,	hsmp_power_ofmt_cb },
	{ "MAX",	12,	HSMP_POWER_F_MAX,	hsmp_power_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_power_usage(FILE *f)
{
	(void) fprintf(f, "\tpower [-Hp] [-o field[,...]]\n");
}

static int
hsmp_power(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_power_fields,
	    HSMP_POWER_FIELDS, hsmp_power_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "power does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_power_row_t row;

		if (!hsmp_targ_selected(t, &row.hpr_sock, &row.hpr_iod))
			continue;
		if (!libhsmp_power(hsmp_handle, t, &row.hpr_power)) {
			hsmp_fatal("failed to read the current power for "
			    "socket %u", row.hpr_sock);
		}
		if (!libhsmp_power_limit(hsmp_handle, t, &row.hpr_limit)) {
			hsmp_fatal("failed to read the power limit for "
			    "socket %u", row.hpr_sock);
		}
		if (!libhsmp_power_limit_max(hsmp_handle, t, &row.hpr_max)) {
			hsmp_fatal("failed to read the maximum power limit "
			    "for socket %u", row.hpr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

static void
hsmp_set_power_limit_usage(FILE *f)
{
	(void) fprintf(f, "\tset-power-limit <limit>[W|mW]\n");
}

static int
hsmp_set_power_limit(int argc, char **argv)
{
	uint32_t mwatt;

	if (argc == 0)
		errx(EXIT_USAGE, "set-power-limit requires a power value");
	if (argc > 1)
		errx(EXIT_USAGE, "set-power-limit takes only a single value");
	mwatt = hsmp_parse_power(argv[0]);

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		uint32_t limit, s;

		if (!hsmp_targ_selected(t, &s, NULL))
			continue;
		if (!libhsmp_power_limit_set(hsmp_handle, t, mwatt)) {
			hsmp_fatal("failed to set the power limit for "
			    "socket %u", s);
		}
		if (!libhsmp_power_limit(hsmp_handle, t, &limit)) {
			hsmp_fatal("failed to read the power limit for "
			    "socket %u", s);
		}

		(void) printf("socket %u: power limit set to %.3f W\n",
		    s, limit / 1000.0);
	}

	return (EXIT_SUCCESS);
}

typedef struct hsmp_core {
	uint32_t hcr_sock;
	uint32_t hcr_core;
	uint32_t hcr_apicid;
	uint_t hcr_ncpus;
	uint32_t *hcr_cpus;
} hsmp_core_t;

static int
hsmp_core_cmp(const void *a, const void *b)
{
	const hsmp_core_t *l = a;
	const hsmp_core_t *r = b;

	if (l->hcr_sock != r->hcr_sock)
		return (l->hcr_sock < r->hcr_sock ? -1 : 1);
	if (l->hcr_apicid != r->hcr_apicid)
		return (l->hcr_apicid < r->hcr_apicid ? -1 : 1);
	return (0);
}

static int
hsmp_cpu_cmp(const void *a, const void *b)
{
	const uint32_t *l = a;
	const uint32_t *r = b;

	if (*l != *r)
		return (*l < *r ? -1 : 1);
	return (0);
}

static void
hsmp_core_add_cpu(hsmp_core_t *core, uint32_t cpu)
{
	uint32_t *cpus;

	cpus = realloc(core->hcr_cpus,
	    (core->hcr_ncpus + 1) * sizeof (uint32_t));
	if (cpus == NULL)
		err(EXIT_FAILURE, "out of memory");
	cpus[core->hcr_ncpus++] = cpu;
	core->hcr_cpus = cpus;
}

/*
 * Build a list of the cores in the system from the cpu_info kstats,
 * identified by socket, core and APIC ID, along with the logical CPUs that
 * each core provides. SMT sibling threads share a core, which is
 * represented by the thread with the lowest APIC ID since that is how the
 * HSMP addresses the core.
 */
static hsmp_core_t *
hsmp_cores_get(uint_t *ncoresp)
{
	hsmp_core_t *cores;
	uint_t ncpus = 0, ncores = 0;
	kstat_ctl_t *kcp;

	if ((kcp = kstat_open()) == NULL)
		err(EXIT_FAILURE, "failed to open /dev/kstat");

	/*
	 * The number of cpu_info kstats, one per logical CPU, is an upper
	 * bound on the number of cores.
	 */
	for (kstat_t *ksp = kcp->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
		if (strcmp(ksp->ks_module, "cpu_info") == 0)
			ncpus++;
	}
	if (ncpus == 0)
		errx(EXIT_FAILURE, "no cpu_info kstats were found");

	cores = calloc(ncpus, sizeof (hsmp_core_t));
	if (cores == NULL)
		err(EXIT_FAILURE, "out of memory");

	for (kstat_t *ksp = kcp->kc_chain; ksp != NULL; ksp = ksp->ks_next) {
		const kstat_named_t *chip, *core, *apic;
		uint32_t sock, coreid, apicid;
		uint_t i;

		if (strcmp(ksp->ks_module, "cpu_info") != 0)
			continue;
		if (kstat_read(kcp, ksp, NULL) == -1) {
			err(EXIT_FAILURE, "failed to read kstat %s:%d",
			    ksp->ks_module, ksp->ks_instance);
		}

		chip = kstat_data_lookup(ksp, "chip_id");
		core = kstat_data_lookup(ksp, "core_id");
		apic = kstat_data_lookup(ksp, "apic_id");
		if (chip == NULL || core == NULL || apic == NULL) {
			errx(EXIT_FAILURE, "kstat %s:%d does not provide "
			    "chip_id, core_id and apic_id",
			    ksp->ks_module, ksp->ks_instance);
		}

		sock = (uint32_t)chip->value.l;
		coreid = (uint32_t)core->value.l;
		apicid = apic->value.ui32;

		for (i = 0; i < ncores; i++) {
			if (cores[i].hcr_sock == sock &&
			    cores[i].hcr_core == coreid) {
				if (apicid < cores[i].hcr_apicid)
					cores[i].hcr_apicid = apicid;
				break;
			}
		}
		if (i == ncores) {
			cores[ncores].hcr_sock = sock;
			cores[ncores].hcr_core = coreid;
			cores[ncores].hcr_apicid = apicid;
			ncores++;
		}
		hsmp_core_add_cpu(&cores[i], (uint32_t)ksp->ks_instance);
	}

	(void) kstat_close(kcp);

	qsort(cores, ncores, sizeof (hsmp_core_t), hsmp_core_cmp);
	for (uint_t i = 0; i < ncores; i++) {
		qsort(cores[i].hcr_cpus, cores[i].hcr_ncpus,
		    sizeof (uint32_t), hsmp_cpu_cmp);
	}

	*ncoresp = ncores;
	return (cores);
}

static void
hsmp_cores_free(hsmp_core_t *cores, uint_t ncores)
{
	for (uint_t i = 0; i < ncores; i++)
		free(cores[i].hcr_cpus);
	free(cores);
}

/*
 * Render a core's logical CPU list as a comma-separated string. Returns
 * false if the list did not fit in the provided buffer.
 */
static bool
hsmp_core_cpustr(const hsmp_core_t *core, char *buf, size_t len)
{
	size_t off = 0;

	buf[0] = '\0';
	for (uint_t i = 0; i < core->hcr_ncpus; i++) {
		int n = snprintf(buf + off, len - off, "%s%u",
		    i == 0 ? "" : ",", core->hcr_cpus[i]);

		if (n < 0 || (size_t)n >= len - off)
			return (false);
		off += (size_t)n;
	}
	return (true);
}

/*
 * Find the core that provides the nominated logical CPU.
 */
static const hsmp_core_t *
hsmp_core_for_cpu(const hsmp_core_t *cores, uint_t ncores, uint32_t cpu)
{
	for (uint_t i = 0; i < ncores; i++) {
		for (uint_t j = 0; j < cores[i].hcr_ncpus; j++) {
			if (cores[i].hcr_cpus[j] == cpu)
				return (&cores[i]);
		}
	}
	errx(EXIT_FAILURE, "CPU %u was not found", cpu);
}

typedef enum {
	HSMP_CORES_F_SOCKET,
	HSMP_CORES_F_CORE,
	HSMP_CORES_F_APICID,
	HSMP_CORES_F_CPUS,
} hsmp_cores_field_t;

static boolean_t
hsmp_cores_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_core_t *core = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_cores_field_t)ofarg->ofmt_id) {
	case HSMP_CORES_F_SOCKET:
		len = snprintf(buf, buflen, "%u", core->hcr_sock);
		break;
	case HSMP_CORES_F_CORE:
		len = snprintf(buf, buflen, "%u", core->hcr_core);
		break;
	case HSMP_CORES_F_APICID:
		len = snprintf(buf, buflen, "%u", core->hcr_apicid);
		break;
	case HSMP_CORES_F_CPUS:
		return (hsmp_core_cpustr(core, buf, buflen) ?
		    B_TRUE : B_FALSE);
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_CORES_FIELDS	"socket,core,apicid,cpus"

static const ofmt_field_t hsmp_cores_fields[] = {
	{ "SOCKET",	9,	HSMP_CORES_F_SOCKET,	hsmp_cores_ofmt_cb },
	{ "CORE",	9,	HSMP_CORES_F_CORE,	hsmp_cores_ofmt_cb },
	{ "APICID",	9,	HSMP_CORES_F_APICID,	hsmp_cores_ofmt_cb },
	{ "CPUS",	13,	HSMP_CORES_F_CPUS,	hsmp_cores_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_cores_usage(FILE *f)
{
	(void) fprintf(f, "\tcores [-Hp] [-o field[,...]]\n");
}

static int
hsmp_cores(int argc, char **argv)
{
	hsmp_core_t *cores;
	uint_t ncores;
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_cores_fields,
	    HSMP_CORES_FIELDS, hsmp_cores_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "cores does not take arguments");

	cores = hsmp_cores_get(&ncores);

	for (uint_t i = 0; i < ncores; i++) {
		if (hsmp_sock_set && cores[i].hcr_sock != hsmp_sock)
			continue;
		ofmt_print(ofmt, &cores[i]);
	}

	ofmt_close(ofmt);
	hsmp_cores_free(cores, ncores);
	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_BOOST_F_SOCKET,
	HSMP_BOOST_F_CPUS,
	HSMP_BOOST_F_LIMIT,
} hsmp_boost_field_t;

typedef struct hsmp_boost_row {
	const hsmp_core_t *hbr_core;
	uint32_t hbr_limit;
} hsmp_boost_row_t;

static boolean_t
hsmp_boost_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_boost_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_boost_field_t)ofarg->ofmt_id) {
	case HSMP_BOOST_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hbr_core->hcr_sock);
		break;
	case HSMP_BOOST_F_CPUS:
		return (hsmp_core_cpustr(row->hbr_core, buf, buflen) ?
		    B_TRUE : B_FALSE);
	case HSMP_BOOST_F_LIMIT:
		len = snprintf(buf, buflen, "%u", row->hbr_limit);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_BOOST_FIELDS	"socket,cpus,limit"

static const ofmt_field_t hsmp_boost_fields[] = {
	{ "SOCKET",	9,	HSMP_BOOST_F_SOCKET,	hsmp_boost_ofmt_cb },
	{ "CPUS",	13,	HSMP_BOOST_F_CPUS,	hsmp_boost_ofmt_cb },
	{ "LIMIT",	11,	HSMP_BOOST_F_LIMIT,	hsmp_boost_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_boost_usage(FILE *f)
{
	(void) fprintf(f, "\tboost [-Hp] [-o field[,...]] [cpu]...\n");
}

/*
 * Boost limits are a per-core property, shared by all of a core's SMT
 * threads, and the HSMP addresses a core through the APIC ID of its
 * lowest-numbered thread. Cores are nominated on the command line by any
 * of their logical CPUs, and translated to an APIC ID here.
 */
static int
hsmp_boost(int argc, char **argv)
{
	hsmp_core_t *cores;
	uint_t ncores;
	bool *sel = NULL;
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_boost_fields,
	    HSMP_BOOST_FIELDS, hsmp_boost_usage);

	cores = hsmp_cores_get(&ncores);

	if (argc > 0) {
		sel = calloc(ncores, sizeof (bool));
		if (sel == NULL)
			err(EXIT_FAILURE, "out of memory");
		for (int i = 0; i < argc; i++) {
			const uint32_t cpu = hsmp_parse_u32(argv[i],
			    UINT32_MAX, "CPU ID");
			const hsmp_core_t *core = hsmp_core_for_cpu(cores,
			    ncores, cpu);

			if (hsmp_sock_set && core->hcr_sock != hsmp_sock) {
				errx(EXIT_FAILURE,
				    "CPU %u is not on socket %u",
				    cpu, hsmp_sock);
			}
			sel[core - cores] = true;
		}
	}

	for (uint_t i = 0; i < ncores; i++) {
		hsmp_boost_row_t row;

		if (sel != NULL) {
			if (!sel[i])
				continue;
		} else if (hsmp_sock_set && cores[i].hcr_sock != hsmp_sock) {
			continue;
		}

		row.hbr_core = &cores[i];
		if (!libhsmp_boost_limit(hsmp_handle,
		    hsmp_targ_for_sock(cores[i].hcr_sock),
		    cores[i].hcr_apicid, &row.hbr_limit)) {
			char buf[128];

			(void) hsmp_core_cpustr(&cores[i], buf, sizeof (buf));
			hsmp_fatal("failed to read the boost limit for "
			    "CPU(s) %s on socket %u", buf, cores[i].hcr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	free(sel);
	hsmp_cores_free(cores, ncores);
	return (EXIT_SUCCESS);
}

static void
hsmp_set_boost_usage(FILE *f)
{
	(void) fprintf(f, "\tset-boost [-c cpu] <MHz>\n");
}

static int
hsmp_set_boost(int argc, char **argv)
{
	bool cpu_set = false;
	uint32_t cpu = 0, mhz;
	int c;

	while ((c = getopt(argc, argv, "c:")) != -1) {
		switch (c) {
		case 'c':
			cpu = hsmp_parse_u32(optarg, UINT32_MAX, "CPU ID");
			cpu_set = true;
			break;
		default:
			hsmp_set_boost_usage(stderr);
			exit(EXIT_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		errx(EXIT_USAGE, "set-boost requires a limit in MHz");
	if (argc > 1)
		errx(EXIT_USAGE, "set-boost takes only a single limit");
	mhz = hsmp_parse_u32(argv[0], UINT16_MAX, "boost limit");

	/*
	 * With -c, the limit is applied only to the core providing the
	 * nominated logical CPU. Otherwise it is applied to every core in
	 * the selected sockets.
	 */
	if (cpu_set) {
		const hsmp_core_t *core;
		libhsmp_target_t targ;
		hsmp_core_t *cores;
		uint_t ncores;
		uint32_t limit;
		char buf[128];

		cores = hsmp_cores_get(&ncores);
		core = hsmp_core_for_cpu(cores, ncores, cpu);
		if (hsmp_sock_set && core->hcr_sock != hsmp_sock) {
			errx(EXIT_FAILURE, "CPU %u is not on socket %u",
			    cpu, hsmp_sock);
		}
		targ = hsmp_targ_for_sock(core->hcr_sock);

		(void) hsmp_core_cpustr(core, buf, sizeof (buf));
		if (!libhsmp_boost_limit_set(hsmp_handle, targ,
		    core->hcr_apicid, mhz)) {
			hsmp_fatal("failed to set the boost limit for "
			    "CPU(s) %s on socket %u", buf, core->hcr_sock);
		}
		if (!libhsmp_boost_limit(hsmp_handle, targ,
		    core->hcr_apicid, &limit)) {
			hsmp_fatal("failed to read the boost limit for "
			    "CPU(s) %s on socket %u", buf, core->hcr_sock);
		}

		(void) printf("socket %u: boost limit for CPU(s) %s "
		    "set to %u MHz\n", core->hcr_sock, buf, limit);

		hsmp_cores_free(cores, ncores);
		return (EXIT_SUCCESS);
	}

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		uint32_t s;

		if (!hsmp_targ_selected(t, &s, NULL))
			continue;
		if (!libhsmp_boost_limit_set_all(hsmp_handle, t, mhz)) {
			hsmp_fatal("failed to set the boost limit for "
			    "all cores on socket %u", s);
		}
	}

	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_PROCHOT_F_SOCKET,
	HSMP_PROCHOT_F_IOD,
	HSMP_PROCHOT_F_PROCHOT,
} hsmp_prochot_field_t;

typedef struct hsmp_prochot_row {
	uint32_t hhr_sock;
	uint32_t hhr_iod;
	bool hhr_asserted;
} hsmp_prochot_row_t;

static boolean_t
hsmp_prochot_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_prochot_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_prochot_field_t)ofarg->ofmt_id) {
	case HSMP_PROCHOT_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hhr_sock);
		break;
	case HSMP_PROCHOT_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hhr_iod);
		break;
	case HSMP_PROCHOT_F_PROCHOT:
		len = strlcpy(buf,
		    row->hhr_asserted ? "asserted" : "deasserted", buflen);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_PROCHOT_FIELDS	"socket,iod,prochot"

static const ofmt_field_t hsmp_prochot_fields[] = {
	{ "SOCKET",	9,	HSMP_PROCHOT_F_SOCKET,	hsmp_prochot_ofmt_cb },
	{ "IOD",	5,	HSMP_PROCHOT_F_IOD,	hsmp_prochot_ofmt_cb },
	{ "PROCHOT",	11,	HSMP_PROCHOT_F_PROCHOT,	hsmp_prochot_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_prochot_usage(FILE *f)
{
	(void) fprintf(f, "\tprochot [-Hp] [-o field[,...]]\n");
}

static int
hsmp_prochot(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_prochot_fields,
	    HSMP_PROCHOT_FIELDS, hsmp_prochot_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "prochot does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_prochot_row_t row;

		if (!hsmp_targ_selected(t, &row.hhr_sock, &row.hhr_iod))
			continue;
		if (!libhsmp_prochot(hsmp_handle, t, &row.hhr_asserted)) {
			hsmp_fatal("failed to read the PROCHOT status for "
			    "socket %u", row.hhr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_CLOCKS_F_SOCKET,
	HSMP_CLOCKS_F_IOD,
	HSMP_CLOCKS_F_FCLK,
	HSMP_CLOCKS_F_MEMCLK,
	HSMP_CLOCKS_F_CCLKMAX,
} hsmp_clocks_field_t;

typedef struct hsmp_clocks_row {
	uint32_t hkr_sock;
	uint32_t hkr_iod;
	uint32_t hkr_fclk;
	uint32_t hkr_memclk;
	uint32_t hkr_cclk;
} hsmp_clocks_row_t;

static boolean_t
hsmp_clocks_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_clocks_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_clocks_field_t)ofarg->ofmt_id) {
	case HSMP_CLOCKS_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hkr_sock);
		break;
	case HSMP_CLOCKS_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hkr_iod);
		break;
	case HSMP_CLOCKS_F_FCLK:
		len = snprintf(buf, buflen, "%u", row->hkr_fclk);
		break;
	case HSMP_CLOCKS_F_MEMCLK:
		len = snprintf(buf, buflen, "%u", row->hkr_memclk);
		break;
	case HSMP_CLOCKS_F_CCLKMAX:
		len = snprintf(buf, buflen, "%u", row->hkr_cclk);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_CLOCKS_FIELDS	"socket,iod,fclk,memclk,cclkmax"

static const ofmt_field_t hsmp_clocks_fields[] = {
	{ "SOCKET",	9,	HSMP_CLOCKS_F_SOCKET,	hsmp_clocks_ofmt_cb },
	{ "IOD",	5,	HSMP_CLOCKS_F_IOD,	hsmp_clocks_ofmt_cb },
	{ "FCLK",	8,	HSMP_CLOCKS_F_FCLK,	hsmp_clocks_ofmt_cb },
	{ "MEMCLK",	8,	HSMP_CLOCKS_F_MEMCLK,	hsmp_clocks_ofmt_cb },
	{ "CCLKMAX",	8,	HSMP_CLOCKS_F_CCLKMAX,	hsmp_clocks_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_clocks_usage(FILE *f)
{
	(void) fprintf(f, "\tclocks [-Hp] [-o field[,...]]\n");
}

static int
hsmp_clocks(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_clocks_fields,
	    HSMP_CLOCKS_FIELDS, hsmp_clocks_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "clocks does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_clocks_row_t row;

		if (!hsmp_targ_selected(t, &row.hkr_sock, &row.hkr_iod))
			continue;
		if (!libhsmp_fclk_memclk(hsmp_handle, t, &row.hkr_fclk,
		    &row.hkr_memclk)) {
			hsmp_fatal("failed to read the current clocks for "
			    "socket %u", row.hkr_sock);
		}
		if (!libhsmp_cclk_limit(hsmp_handle, t, &row.hkr_cclk)) {
			hsmp_fatal("failed to read the cclk frequency limit "
			    "for socket %u", row.hkr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_RESIDENCY_F_SOCKET,
	HSMP_RESIDENCY_F_IOD,
	HSMP_RESIDENCY_F_C0,
} hsmp_residency_field_t;

typedef struct hsmp_residency_row {
	uint32_t hrr_sock;
	uint32_t hrr_iod;
	uint32_t hrr_residency;
} hsmp_residency_row_t;

static boolean_t
hsmp_residency_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_residency_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_residency_field_t)ofarg->ofmt_id) {
	case HSMP_RESIDENCY_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hrr_sock);
		break;
	case HSMP_RESIDENCY_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hrr_iod);
		break;
	case HSMP_RESIDENCY_F_C0:
		len = snprintf(buf, buflen, "%u", row->hrr_residency);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_RESIDENCY_FIELDS	"socket,iod,c0"

static const ofmt_field_t hsmp_residency_fields[] = {
	{ "SOCKET",	9,	HSMP_RESIDENCY_F_SOCKET,
	    hsmp_residency_ofmt_cb },
	{ "IOD",	5,	HSMP_RESIDENCY_F_IOD,
	    hsmp_residency_ofmt_cb },
	{ "C0",		4,	HSMP_RESIDENCY_F_C0,
	    hsmp_residency_ofmt_cb },
	{ NULL,		0,	0,	NULL }
};

static void
hsmp_residency_usage(FILE *f)
{
	(void) fprintf(f, "\tresidency [-Hp] [-o field[,...]]\n");
}

static int
hsmp_residency(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_residency_fields,
	    HSMP_RESIDENCY_FIELDS, hsmp_residency_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "residency does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_residency_row_t row;

		if (!hsmp_targ_selected(t, &row.hrr_sock, &row.hrr_iod))
			continue;
		if (!libhsmp_c0_residency(hsmp_handle, t,
		    &row.hrr_residency)) {
			hsmp_fatal("failed to read the C0 residency for "
			    "socket %u", row.hrr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_DDR_F_SOCKET,
	HSMP_DDR_F_IOD,
	HSMP_DDR_F_MAX,
	HSMP_DDR_F_UTIL,
	HSMP_DDR_F_UTILPCT,
} hsmp_ddr_field_t;

typedef struct hsmp_ddr_row {
	uint32_t hdr_sock;
	uint32_t hdr_iod;
	libhsmp_ddr_bw_t hdr_bw;
} hsmp_ddr_row_t;

static boolean_t
hsmp_ddr_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_ddr_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_ddr_field_t)ofarg->ofmt_id) {
	case HSMP_DDR_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hdr_sock);
		break;
	case HSMP_DDR_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hdr_iod);
		break;
	case HSMP_DDR_F_MAX:
		len = snprintf(buf, buflen, "%u", row->hdr_bw.ldb_max_gbps);
		break;
	case HSMP_DDR_F_UTIL:
		len = snprintf(buf, buflen, "%u", row->hdr_bw.ldb_util_gbps);
		break;
	case HSMP_DDR_F_UTILPCT:
		len = snprintf(buf, buflen, "%u", row->hdr_bw.ldb_util_pct);
		break;
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_DDR_FIELDS		"socket,iod,max,util,utilpct"

static const ofmt_field_t hsmp_ddr_fields[] = {
	{ "SOCKET",	9,	HSMP_DDR_F_SOCKET,	hsmp_ddr_ofmt_cb },
	{ "IOD",	5,	HSMP_DDR_F_IOD,		hsmp_ddr_ofmt_cb },
	{ "MAX",	8,	HSMP_DDR_F_MAX,		hsmp_ddr_ofmt_cb },
	{ "UTIL",	8,	HSMP_DDR_F_UTIL,	hsmp_ddr_ofmt_cb },
	{ "UTILPCT",	8,	HSMP_DDR_F_UTILPCT,	hsmp_ddr_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_ddr_usage(FILE *f)
{
	(void) fprintf(f, "\tddr [-Hp] [-o field[,...]]\n");
}

static int
hsmp_ddr(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_ddr_fields,
	    HSMP_DDR_FIELDS, hsmp_ddr_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "ddr does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_ddr_row_t row;

		if (!hsmp_targ_selected(t, &row.hdr_sock, &row.hdr_iod))
			continue;
		if (!libhsmp_ddr_bandwidth(hsmp_handle, t, &row.hdr_bw)) {
			if (libhsmp_err(hsmp_handle) ==
			    LIBHSMP_ERR_UNSUPPORTED) {
				warnx("socket %u: %s", row.hdr_sock,
				    libhsmp_errmsg(hsmp_handle));
				ofmt_close(ofmt);
				return (EXIT_FAILURE);
			}
			hsmp_fatal("failed to read the DDR bandwidth for "
			    "socket %u", row.hdr_sock);
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

typedef enum {
	HSMP_FREQ_F_SOCKET,
	HSMP_FREQ_F_IOD,
	HSMP_FREQ_F_LIMIT,
	HSMP_FREQ_F_SOURCES,
} hsmp_freq_field_t;

typedef struct hsmp_freq_row {
	uint32_t hfr_sock;
	uint32_t hfr_iod;
	uint32_t hfr_limit;
	bool hfr_have_srcs;
	libhsmp_freq_src_t hfr_srcs;
} hsmp_freq_row_t;

static boolean_t
hsmp_freq_ofmt_cb(ofmt_arg_t *ofarg, char *buf, uint_t buflen)
{
	const hsmp_freq_row_t *row = ofarg->ofmt_cbarg;
	size_t len;

	switch ((hsmp_freq_field_t)ofarg->ofmt_id) {
	case HSMP_FREQ_F_SOCKET:
		len = snprintf(buf, buflen, "%u", row->hfr_sock);
		break;
	case HSMP_FREQ_F_IOD:
		len = snprintf(buf, buflen, "%u", row->hfr_iod);
		break;
	case HSMP_FREQ_F_LIMIT:
		len = snprintf(buf, buflen, "%u", row->hfr_limit);
		break;
	case HSMP_FREQ_F_SOURCES: {
		bool first = true;
		size_t off = 0;

		if (!row->hfr_have_srcs) {
			len = strlcpy(buf, "unknown", buflen);
			break;
		}

		/*
		 * The sources occupy the low 16 bits of the response
		 * register, with the limit itself carried above them, so
		 * no source bit beyond 15 can ever be set.
		 */
		buf[0] = '\0';
		for (uint32_t bit = 0; bit < 16; bit++) {
			const libhsmp_freq_src_t src =
			    (libhsmp_freq_src_t)(1U << bit);
			const char *name;
			char namebuf[16];
			int n;

			if ((row->hfr_srcs & src) == 0)
				continue;
			name = libhsmp_freq_src_str(src);
			if (name == NULL) {
				(void) snprintf(namebuf, sizeof (namebuf),
				    "bit%u", bit);
				name = namebuf;
			}
			n = snprintf(buf + off, buflen - off, "%s%s",
			    first ? "" : ",", name);
			if (n < 0 || (size_t)n >= buflen - off)
				return (B_FALSE);
			off += (size_t)n;
			first = false;
		}
		if (first)
			len = strlcpy(buf, "-", buflen);
		else
			len = off;
		break;
	}
	default:
		abort();
	}
	return (len < buflen);
}

#define	HSMP_FREQ_FIELDS	"socket,iod,limit,sources"

static const ofmt_field_t hsmp_freq_limit_fields[] = {
	{ "SOCKET",	9,	HSMP_FREQ_F_SOCKET,	hsmp_freq_ofmt_cb },
	{ "IOD",	5,	HSMP_FREQ_F_IOD,	hsmp_freq_ofmt_cb },
	{ "LIMIT",	8,	HSMP_FREQ_F_LIMIT,	hsmp_freq_ofmt_cb },
	{ "SOURCES",	28,	HSMP_FREQ_F_SOURCES,	hsmp_freq_ofmt_cb },
	{ NULL,		0,	0,			NULL }
};

static void
hsmp_freq_limit_usage(FILE *f)
{
	(void) fprintf(f, "\tfreq-limit [-Hp] [-o field[,...]]\n");
}

static int
hsmp_freq_limit(int argc, char **argv)
{
	ofmt_handle_t ofmt;

	ofmt = hsmp_ofmt_getopt(&argc, &argv, hsmp_freq_limit_fields,
	    HSMP_FREQ_FIELDS, hsmp_freq_limit_usage);
	if (argc != 0)
		errx(EXIT_USAGE, "freq-limit does not take arguments");

	for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
		hsmp_freq_row_t row;

		if (!hsmp_targ_selected(t, &row.hfr_sock, &row.hfr_iod))
			continue;

		row.hfr_have_srcs = true;
		if (!libhsmp_freq_limit(hsmp_handle, t, &row.hfr_limit,
		    &row.hfr_srcs)) {
			if (libhsmp_err(hsmp_handle) !=
			    LIBHSMP_ERR_UNSUPPORTED) {
				hsmp_fatal("failed to read the frequency "
				    "limit for socket %u", row.hfr_sock);
			}

			/*
			 * The firmware does not provide this function.
			 * Fall back to the cclk frequency limit, which
			 * reports the same cap but not the sources of it.
			 */
			if (!libhsmp_cclk_limit(hsmp_handle, t,
			    &row.hfr_limit)) {
				hsmp_fatal("failed to read the cclk "
				    "frequency limit for socket %u",
				    row.hfr_sock);
			}
			row.hfr_have_srcs = false;
		}

		ofmt_print(ofmt, &row);
	}

	ofmt_close(ofmt);
	return (EXIT_SUCCESS);
}

static const hsmp_cmdtab_t hsmp_cmds[] = {
	{ "boost", hsmp_boost, hsmp_boost_usage },
	{ "clocks", hsmp_clocks, hsmp_clocks_usage },
	{ "cores", hsmp_cores, hsmp_cores_usage },
	{ "ddr", hsmp_ddr, hsmp_ddr_usage },
	{ "freq-limit", hsmp_freq_limit, hsmp_freq_limit_usage },
	{ "power", hsmp_power, hsmp_power_usage },
	{ "prochot", hsmp_prochot, hsmp_prochot_usage },
	{ "residency", hsmp_residency, hsmp_residency_usage },
	{ "set-boost", hsmp_set_boost, hsmp_set_boost_usage },
	{ "set-power-limit", hsmp_set_power_limit,
	    hsmp_set_power_limit_usage },
	{ "version", hsmp_version, hsmp_version_usage },
	{ NULL, NULL, NULL }
};

int
main(int argc, char **argv)
{
	char errmsg[LIBHSMP_ERR_LEN];
	libhsmp_err_t lerr;
	int32_t syserr;
	int c, ret;

	progname = basename(argv[0]);

	while ((c = getopt(argc, argv, "s:")) != -1) {
		switch (c) {
		case 's':
			hsmp_sock = hsmp_parse_u32(optarg, UINT32_MAX,
			    "socket");
			hsmp_sock_set = true;
			break;
		default:
			hsmp_usage(hsmp_cmds);
			exit(EXIT_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (!libhsmp_init(&hsmp_handle, &lerr, &syserr, errmsg,
	    sizeof (errmsg))) {
		errx(EXIT_FAILURE, "failed to initialise libhsmp: %s (%s)",
		    errmsg, libhsmp_strerror(lerr));
	}

	hsmp_ntargets = libhsmp_ntargets(hsmp_handle);
	if (hsmp_sock_set) {
		bool found = false;

		for (libhsmp_target_t t = 0; t < hsmp_ntargets; t++) {
			if (hsmp_targ_selected(t, NULL, NULL)) {
				found = true;
				break;
			}
		}
		if (!found) {
			errx(EXIT_FAILURE,
			    "socket %u was not found on this system",
			    hsmp_sock);
		}
	}

	ret = hsmp_walk_tab(hsmp_cmds, argc, argv);

	libhsmp_fini(hsmp_handle);

	return (ret);
}
