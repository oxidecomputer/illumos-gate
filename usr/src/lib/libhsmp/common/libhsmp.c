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
 * Library routines providing access to a curated set of the functions that
 * the AMD Host System Management Port (HSMP) offers. The HSMP is reached
 * through the uhsmp(4D) driver, which exposes one minor node per HSMP
 * target. At initialisation the library locates and opens these device
 * nodes via a devinfo snapshot, and caches the HSMP interface version that
 * each target's SMU firmware reports. That version is subsequently used to
 * refuse requests for functions that the firmware does not implement.
 *
 * The identifiers and argument encodings for the HSMP functions used here
 * are documented in the HSMP chapter of AMD's Processor Programming
 * Reference volumes.
 *
 * The interfaces herein are MT-Safe only if each thread within a
 * multi-threaded caller uses its own library handle.
 */

#include <errno.h>
#include <fcntl.h>
#include <libdevinfo.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/bitext.h>
#include <sys/ccompile.h>
#include <sys/debug.h>
#include <sys/types.h>
#include <sys/amdzen/hsmp.h>

/*
 * sys/mkdev.h must follow sys/amdzen/hsmp.h so that its device number
 * macros replace, rather than clash with, those defined by
 * sys/sysmacros.h, which is included through the former.
 */
#include <sys/mkdev.h>

#include <uhsmp.h>
#include <libhsmp.h>
#include <libhsmp_priv.h>

/*
 * The HSMP functions used by the library. GetInterfaceVersion has a minimum
 * interface version of 0 since it must be usable during initialisation,
 * before the version for each target is known.
 */
static const libhsmp_fn_t libhsmp_fn_ifver = {
	.lf_id = HSMP_CMD_GETIFVERSION,
	.lf_minver = 0,
	.lf_name = "GetInterfaceVersion"
};

static const libhsmp_fn_t libhsmp_fn_smu_version = {
	.lf_id = HSMP_CMD_GETSMUVERSION,
	.lf_minver = 1,
	.lf_name = "GetSmuVersion"
};

static const libhsmp_fn_t libhsmp_fn_power = {
	.lf_id = HSMP_CMD_READSOCKETPOWER,
	.lf_minver = 1,
	.lf_name = "ReadSocketPower"
};

static const libhsmp_fn_t libhsmp_fn_power_limit_set = {
	.lf_id = HSMP_CMD_WRITESOCKETPOWERLIMIT,
	.lf_minver = 1,
	.lf_name = "WriteSocketPowerLimit"
};

static const libhsmp_fn_t libhsmp_fn_power_limit = {
	.lf_id = HSMP_CMD_READSOCKETPOWERLIMIT,
	.lf_minver = 1,
	.lf_name = "ReadSocketPowerLimit"
};

static const libhsmp_fn_t libhsmp_fn_power_limit_max = {
	.lf_id = HSMP_CMD_READMAXSOCKETPOWERLIMIT,
	.lf_minver = 1,
	.lf_name = "ReadMaxSocketPowerLimit"
};

static const libhsmp_fn_t libhsmp_fn_boost_limit_set = {
	.lf_id = HSMP_CMD_WRITEBOOSTLIMIT,
	.lf_minver = 1,
	.lf_name = "WriteBoostLimit"
};

static const libhsmp_fn_t libhsmp_fn_boost_limit_set_all = {
	.lf_id = HSMP_CMD_WRITEBOOSTLIMITALLCORES,
	.lf_minver = 1,
	.lf_name = "WriteBoostLimitAllCores"
};

static const libhsmp_fn_t libhsmp_fn_boost_limit = {
	.lf_id = HSMP_CMD_READBOOSTLIMIT,
	.lf_minver = 1,
	.lf_name = "ReadBoostLimit"
};

static const libhsmp_fn_t libhsmp_fn_prochot = {
	.lf_id = HSMP_CMD_READPROCHOTSTATUS,
	.lf_minver = 1,
	.lf_name = "ReadProchotStatus"
};

static const libhsmp_fn_t libhsmp_fn_fclk_memclk = {
	.lf_id = HSMP_CMD_READFCLKMEMCLK,
	.lf_minver = 1,
	.lf_name = "ReadCurrentFclkMemclk"
};

static const libhsmp_fn_t libhsmp_fn_cclk_limit = {
	.lf_id = HSMP_CMD_READCCLKFREQLIMIT,
	.lf_minver = 1,
	.lf_name = "ReadCclkFrequencyLimit"
};

static const libhsmp_fn_t libhsmp_fn_c0_residency = {
	.lf_id = HSMP_CMD_READC0RESIDENCY,
	.lf_minver = 1,
	.lf_name = "ReadSocketC0Residency"
};

static const libhsmp_fn_t libhsmp_fn_ddr_bandwidth = {
	.lf_id = HSMP_CMD_GETDDRBANDWIDTH,
	.lf_minver = 3,
	.lf_name = "GetMaxDDRBandwidthAndUtilization"
};

static const libhsmp_fn_t libhsmp_fn_freq_limit = {
	.lf_id = HSMP_CMD_GETSOCKETFREQLIMIT,
	.lf_minver = 5,
	.lf_name = "GetSocketFrequencyLimit"
};

libhsmp_err_t
libhsmp_err(const libhsmp_handle_t *lhh)
{
	return (lhh->lhh_err);
}

int32_t
libhsmp_syserr(const libhsmp_handle_t *lhh)
{
	return (lhh->lhh_syserr);
}

const char *
libhsmp_errmsg(const libhsmp_handle_t *lhh)
{
	return (lhh->lhh_errmsg);
}

const char *
libhsmp_strerror(libhsmp_err_t err)
{
	switch (err) {
	case LIBHSMP_ERR_OK:
		return ("LIBHSMP_ERR_OK");
	case LIBHSMP_ERR_NO_MEM:
		return ("LIBHSMP_ERR_NO_MEM");
	case LIBHSMP_ERR_NO_DEVICE:
		return ("LIBHSMP_ERR_NO_DEVICE");
	case LIBHSMP_ERR_PRIVILEGE:
		return ("LIBHSMP_ERR_PRIVILEGE");
	case LIBHSMP_ERR_INVALID_PARAM:
		return ("LIBHSMP_ERR_INVALID_PARAM");
	case LIBHSMP_ERR_BAD_TARGET:
		return ("LIBHSMP_ERR_BAD_TARGET");
	case LIBHSMP_ERR_UNSUPPORTED:
		return ("LIBHSMP_ERR_UNSUPPORTED");
	case LIBHSMP_ERR_BUSY:
		return ("LIBHSMP_ERR_BUSY");
	case LIBHSMP_ERR_PREREQ:
		return ("LIBHSMP_ERR_PREREQ");
	case LIBHSMP_ERR_INVALID_ARGS:
		return ("LIBHSMP_ERR_INVALID_ARGS");
	case LIBHSMP_ERR_TIMEOUT:
		return ("LIBHSMP_ERR_TIMEOUT");
	case LIBHSMP_ERR_INTERNAL:
		return ("LIBHSMP_ERR_INTERNAL");
	default:
		break;
	}
	return ("Unknown error");
}

static bool __PRINTFLIKE(4)
libhsmp_error(libhsmp_handle_t *lhh, libhsmp_err_t err, int syserr,
    const char *fmt, ...)
{
	va_list ap;

	lhh->lhh_err = err;
	lhh->lhh_syserr = syserr;
	va_start(ap, fmt);
	(void) vsnprintf(lhh->lhh_errmsg, sizeof (lhh->lhh_errmsg), fmt, ap);
	va_end(ap);

	return (false);
}

static bool
libhsmp_success(libhsmp_handle_t *lhh)
{
	lhh->lhh_err = LIBHSMP_ERR_OK;
	lhh->lhh_syserr = 0;
	lhh->lhh_errmsg[0] = '\0';

	return (true);
}

static bool __PRINTFLIKE(7)
libhsmp_init_error(libhsmp_err_t *libhsmp_errp, int32_t *syserrp,
    char *const errmsg, size_t errlen, libhsmp_err_t err, int32_t syserr,
    const char *fmt, ...)
{
	if (libhsmp_errp != NULL)
		*libhsmp_errp = err;
	if (syserrp != NULL)
		*syserrp = syserr;
	if (errmsg != NULL) {
		va_list ap;

		va_start(ap, fmt);
		(void) vsnprintf(errmsg, errlen, fmt, ap);
		va_end(ap);
	}

	return (false);
}

/*
 * Send an HSMP command to the nominated target and interpret the result.
 * The caller provides the argument registers in cmd and receives the
 * returned argument registers in the same structure. The function's
 * identifier is filled in here from the function table entry, which also
 * carries the minimum interface version required.
 */
static bool
libhsmp_cmd(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    const libhsmp_fn_t *fn, uhsmp_cmd_t *cmd)
{
	const libhsmp_tgt_t *lht;
	int ret;

	if (targ >= lhh->lhh_ntargets) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_BAD_TARGET, 0,
		    "target %u is out of range, %u target(s) present",
		    targ, lhh->lhh_ntargets));
	}

	lht = &lhh->lhh_tgts[targ];

	if (lht->lht_ifver < fn->lf_minver) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_UNSUPPORTED, 0,
		    "HSMP function %s requires interface version %u "
		    "but target %u reports version %u",
		    fn->lf_name, fn->lf_minver, targ, lht->lht_ifver));
	}

	cmd->uc_id = fn->lf_id;
	cmd->uc_response = 0;

	do {
		ret = ioctl(lht->lht_fd, UHSMP_GENERIC_COMMAND, cmd);
	} while (ret != 0 && errno == EINTR);

	if (ret != 0) {
		switch (errno) {
		case ETIMEDOUT:
			return (libhsmp_error(lhh, LIBHSMP_ERR_TIMEOUT,
			    errno, "the SMU did not respond to HSMP "
			    "function %s in time", fn->lf_name));
		case EPERM:
			return (libhsmp_error(lhh, LIBHSMP_ERR_PRIVILEGE,
			    errno, "insufficient privilege to issue HSMP "
			    "function %s", fn->lf_name));
		case EINVAL:
			return (libhsmp_error(lhh, LIBHSMP_ERR_UNSUPPORTED,
			    errno, "the uhsmp driver rejected HSMP "
			    "function %s", fn->lf_name));
		default:
			return (libhsmp_error(lhh, LIBHSMP_ERR_INTERNAL,
			    errno, "HSMP function %s failed: %s",
			    fn->lf_name, strerror(errno)));
		}
	}

	switch (cmd->uc_response) {
	case HSMP_RESPONSE_OK:
		return (libhsmp_success(lhh));
	case HSMP_RESPONSE_REJECTED_BUSY:
		return (libhsmp_error(lhh, LIBHSMP_ERR_BUSY, 0,
		    "the SMU was too busy to service HSMP function %s",
		    fn->lf_name));
	case HSMP_RESPONSE_REJECTED_PREREQ:
		return (libhsmp_error(lhh, LIBHSMP_ERR_PREREQ, 0,
		    "a prerequisite for HSMP function %s was not met",
		    fn->lf_name));
	case HSMP_RESPONSE_INVALID_MSGID:
		return (libhsmp_error(lhh, LIBHSMP_ERR_UNSUPPORTED, 0,
		    "the SMU firmware does not recognise HSMP function %s",
		    fn->lf_name));
	case HSMP_RESPONSE_INVALID_ARGS:
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_ARGS, 0,
		    "the SMU firmware rejected the arguments to HSMP "
		    "function %s", fn->lf_name));
	default:
		return (libhsmp_error(lhh, LIBHSMP_ERR_INTERNAL, 0,
		    "unexpected response 0x%x to HSMP function %s",
		    cmd->uc_response, fn->lf_name));
	}
}

/*
 * Issue the GetInterfaceVersion function to a target. This is used during
 * initialisation to populate the cached version that the public
 * libhsmp_interface_version() returns.
 */
static bool
libhsmp_ifver_fetch(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *versionp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_ifver, &cmd))
		return (false);

	*versionp = cmd.uc_args[0];
	return (true);
}

void
libhsmp_fini(libhsmp_handle_t *lhh)
{
	if (lhh == NULL)
		return;

	if (lhh->lhh_tgts != NULL) {
		for (uint32_t i = 0; i < lhh->lhh_ntargets; i++) {
			if (lhh->lhh_tgts[i].lht_fd != -1)
				VERIFY0(close(lhh->lhh_tgts[i].lht_fd));
		}
		free(lhh->lhh_tgts);
	}
	free(lhh);
}

bool
libhsmp_init(libhsmp_handle_t **lhhp, libhsmp_err_t *libhsmp_errp,
    int32_t *syserrp, char *const errmsg, size_t errlen)
{
	libhsmp_handle_t *lhh;
	di_node_t root, node;
	uint32_t ntargets;

	*lhhp = NULL;

	lhh = calloc(1, sizeof (*lhh));
	if (lhh == NULL) {
		return (libhsmp_init_error(libhsmp_errp, syserrp, errmsg,
		    errlen, LIBHSMP_ERR_NO_MEM, errno,
		    "failed to allocate memory for handle: %s",
		    strerror(errno)));
	}

	/*
	 * The uhsmp driver is a leaf pseudo device with no persistent holds
	 * and so, although it will usually have been attached during boot,
	 * it could have been unloaded since. Try to take a snapshot that
	 * loads and attaches the driver first. That requires privilege, and
	 * failure is not fatal here. Falling back to a plain snapshot means
	 * that an unprivileged caller on a system where the driver is
	 * already attached goes on to receive the more accurate privilege
	 * error when the device is opened.
	 */
	root = di_init_driver(LIBHSMP_DRV, DINFOSUBTREE | DINFOMINOR);
	if (root == DI_NODE_NIL)
		root = di_init("/", DINFOSUBTREE | DINFOMINOR);
	if (root == DI_NODE_NIL) {
		(void) libhsmp_init_error(libhsmp_errp, syserrp, errmsg,
		    errlen, LIBHSMP_ERR_INTERNAL, errno,
		    "failed to take a devinfo snapshot: %s", strerror(errno));
		free(lhh);
		return (false);
	}

	node = di_drv_first_node(LIBHSMP_DRV, root);
	if (node == DI_NODE_NIL) {
		(void) libhsmp_init_error(libhsmp_errp, syserrp, errmsg,
		    errlen, LIBHSMP_ERR_NO_DEVICE, 0,
		    "no uhsmp device was found, "
		    "this system may not support HSMP");
		goto err;
	}

	/*
	 * The uhsmp driver creates one minor node per data fabric instance,
	 * with the minor number set to the zero-based fabric index. Each
	 * fabric instance corresponds to an IO die, of which every processor
	 * that the driver currently supports has one per socket. Each minor
	 * is therefore exposed as an HSMP target, using the fabric index as
	 * the target identifier, and mapped to its socket on that basis.
	 * Count the minors, then confirm that they form a dense range as
	 * they are opened.
	 */
	ntargets = 0;
	for (di_minor_t dim = di_minor_next(node, DI_MINOR_NIL);
	    dim != DI_MINOR_NIL; dim = di_minor_next(node, dim)) {
		ntargets++;
	}

	if (ntargets == 0) {
		(void) libhsmp_init_error(libhsmp_errp, syserrp, errmsg,
		    errlen, LIBHSMP_ERR_NO_DEVICE, 0,
		    "the uhsmp device has no minor nodes");
		goto err;
	}

	lhh->lhh_tgts = calloc(ntargets, sizeof (libhsmp_tgt_t));
	if (lhh->lhh_tgts == NULL) {
		(void) libhsmp_init_error(libhsmp_errp, syserrp, errmsg,
		    errlen, LIBHSMP_ERR_NO_MEM, errno,
		    "failed to allocate memory for %u target(s): %s",
		    ntargets, strerror(errno));
		goto err;
	}
	lhh->lhh_ntargets = ntargets;
	for (uint32_t i = 0; i < ntargets; i++)
		lhh->lhh_tgts[i].lht_fd = -1;

	for (di_minor_t dim = di_minor_next(node, DI_MINOR_NIL);
	    dim != DI_MINOR_NIL; dim = di_minor_next(node, dim)) {
		const uint32_t m = (uint32_t)minor(di_minor_devt(dim));
		char path[PATH_MAX], *mpath;
		int fd;

		if (m >= ntargets || lhh->lhh_tgts[m].lht_fd != -1) {
			(void) libhsmp_init_error(libhsmp_errp, syserrp,
			    errmsg, errlen, LIBHSMP_ERR_INTERNAL, 0,
			    "unexpected uhsmp minor number %u", m);
			goto err;
		}

		mpath = di_devfs_minor_path(dim);
		if (mpath == NULL) {
			(void) libhsmp_init_error(libhsmp_errp, syserrp,
			    errmsg, errlen, LIBHSMP_ERR_INTERNAL, errno,
			    "failed to determine the device path for "
			    "target %u: %s", m, strerror(errno));
			goto err;
		}
		(void) snprintf(path, sizeof (path), "/devices%s", mpath);
		di_devfs_path_free(mpath);

		fd = open(path, O_RDWR);
		if (fd == -1) {
			libhsmp_err_t err = LIBHSMP_ERR_INTERNAL;

			if (errno == EPERM || errno == EACCES)
				err = LIBHSMP_ERR_PRIVILEGE;
			(void) libhsmp_init_error(libhsmp_errp, syserrp,
			    errmsg, errlen, err, errno,
			    "failed to open '%s': %s", path, strerror(errno));
			goto err;
		}
		lhh->lhh_tgts[m].lht_fd = fd;
		lhh->lhh_tgts[m].lht_sock = m;
		lhh->lhh_tgts[m].lht_iod = 0;
	}

	di_fini(root);

	/*
	 * Retrieve and cache the HSMP interface version for each target so
	 * that subsequent requests can be checked against it.
	 */
	for (uint32_t i = 0; i < ntargets; i++) {
		uint32_t vers;

		if (!libhsmp_ifver_fetch(lhh, i, &vers)) {
			(void) libhsmp_init_error(libhsmp_errp, syserrp,
			    errmsg, errlen, lhh->lhh_err, lhh->lhh_syserr,
			    "%s", lhh->lhh_errmsg);
			libhsmp_fini(lhh);
			return (false);
		}
		lhh->lhh_tgts[i].lht_ifver = vers;
	}

	*lhhp = lhh;
	return (true);

err:
	di_fini(root);
	libhsmp_fini(lhh);
	return (false);
}

uint32_t
libhsmp_ntargets(const libhsmp_handle_t *lhh)
{
	return (lhh->lhh_ntargets);
}

bool
libhsmp_target_info(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *sockp, uint32_t *iodp)
{
	const libhsmp_tgt_t *lht;

	if (targ >= lhh->lhh_ntargets) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_BAD_TARGET, 0,
		    "target %u is out of range, %u target(s) present",
		    targ, lhh->lhh_ntargets));
	}

	lht = &lhh->lhh_tgts[targ];
	if (sockp != NULL)
		*sockp = lht->lht_sock;
	if (iodp != NULL)
		*iodp = lht->lht_iod;
	return (libhsmp_success(lhh));
}

bool
libhsmp_interface_version(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *versionp)
{
	if (versionp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the version output pointer may not be NULL"));
	}

	if (targ >= lhh->lhh_ntargets) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_BAD_TARGET, 0,
		    "target %u is out of range, %u target(s) present",
		    targ, lhh->lhh_ntargets));
	}

	/*
	 * The interface version was retrieved and cached when the library
	 * was initialised, and cannot change while the system is running.
	 */
	*versionp = lhh->lhh_tgts[targ].lht_ifver;
	return (libhsmp_success(lhh));
}

bool
libhsmp_smu_version(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    libhsmp_smu_version_t *versionp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (versionp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the version output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_smu_version, &cmd))
		return (false);

	/*
	 * The SMU firmware version is packed into the low three bytes of the
	 * first argument register, most significant component first.
	 */
	versionp->lsv_major = bitx32(cmd.uc_args[0], 23, 16);
	versionp->lsv_minor = bitx32(cmd.uc_args[0], 15, 8);
	versionp->lsv_patch = bitx32(cmd.uc_args[0], 7, 0);
	return (true);
}

bool
libhsmp_power(libhsmp_handle_t *lhh, libhsmp_target_t targ, uint32_t *powerp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (powerp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the power output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_power, &cmd))
		return (false);

	*powerp = cmd.uc_args[0];
	return (true);
}

bool
libhsmp_power_limit(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *limitp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (limitp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the limit output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_power_limit, &cmd))
		return (false);

	*limitp = cmd.uc_args[0];
	return (true);
}

bool
libhsmp_power_limit_max(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *limitp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (limitp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the limit output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_power_limit_max, &cmd))
		return (false);

	*limitp = cmd.uc_args[0];
	return (true);
}

bool
libhsmp_power_limit_set(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t mwatt)
{
	uhsmp_cmd_t cmd = { 0 };

	cmd.uc_args[0] = mwatt;
	return (libhsmp_cmd(lhh, targ, &libhsmp_fn_power_limit_set, &cmd));
}

bool
libhsmp_boost_limit(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t apicid, uint32_t *limitp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (limitp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the limit output pointer may not be NULL"));
	}

	if (apicid > UINT16_MAX) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "APIC ID %u is out of range", apicid));
	}

	cmd.uc_args[0] = apicid;
	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_boost_limit, &cmd))
		return (false);

	*limitp = bitx32(cmd.uc_args[0], 15, 0);
	return (true);
}

bool
libhsmp_boost_limit_set(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t apicid, uint32_t mhz)
{
	uhsmp_cmd_t cmd = { 0 };

	if (apicid > UINT16_MAX) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "APIC ID %u is out of range", apicid));
	}

	if (mhz > UINT16_MAX) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "boost limit %u MHz is out of range", mhz));
	}

	/*
	 * The APIC ID of the target core is carried in the upper half of the
	 * first argument register, with the limit in the lower half.
	 */
	cmd.uc_args[0] = bitset32(0, 31, 16, apicid);
	cmd.uc_args[0] = bitset32(cmd.uc_args[0], 15, 0, mhz);
	return (libhsmp_cmd(lhh, targ, &libhsmp_fn_boost_limit_set, &cmd));
}

bool
libhsmp_boost_limit_set_all(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t mhz)
{
	uhsmp_cmd_t cmd = { 0 };

	if (mhz > UINT16_MAX) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "boost limit %u MHz is out of range", mhz));
	}

	cmd.uc_args[0] = mhz;
	return (libhsmp_cmd(lhh, targ, &libhsmp_fn_boost_limit_set_all,
	    &cmd));
}

bool
libhsmp_prochot(libhsmp_handle_t *lhh, libhsmp_target_t targ, bool *assertedp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (assertedp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the PROCHOT output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_prochot, &cmd))
		return (false);

	*assertedp = bitx32(cmd.uc_args[0], 0, 0) != 0;
	return (true);
}

bool
libhsmp_fclk_memclk(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *fclkp, uint32_t *memclkp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (fclkp == NULL || memclkp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the clock output pointers may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_fclk_memclk, &cmd))
		return (false);

	*fclkp = cmd.uc_args[0];
	*memclkp = cmd.uc_args[1];
	return (true);
}

bool
libhsmp_cclk_limit(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *limitp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (limitp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the limit output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_cclk_limit, &cmd))
		return (false);

	*limitp = cmd.uc_args[0];
	return (true);
}

bool
libhsmp_c0_residency(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *residencyp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (residencyp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the residency output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_c0_residency, &cmd))
		return (false);

	*residencyp = cmd.uc_args[0];
	return (true);
}

bool
libhsmp_ddr_bandwidth(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    libhsmp_ddr_bw_t *bwp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (bwp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the bandwidth output pointer may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_ddr_bandwidth, &cmd))
		return (false);

	/*
	 * The first argument register carries the theoretical maximum
	 * bandwidth in Gbps in its top twelve bits, the current utilised
	 * bandwidth in Gbps in the next twelve, and the utilisation as a
	 * percentage of the maximum in the bottom eight.
	 */
	bwp->ldb_max_gbps = bitx32(cmd.uc_args[0], 31, 20);
	bwp->ldb_util_gbps = bitx32(cmd.uc_args[0], 19, 8);
	bwp->ldb_util_pct = bitx32(cmd.uc_args[0], 7, 0);
	return (true);
}

bool
libhsmp_freq_limit(libhsmp_handle_t *lhh, libhsmp_target_t targ,
    uint32_t *limitp, libhsmp_freq_src_t *srcsp)
{
	uhsmp_cmd_t cmd = { 0 };

	if (limitp == NULL || srcsp == NULL) {
		return (libhsmp_error(lhh, LIBHSMP_ERR_INVALID_PARAM, 0,
		    "the limit and source output pointers may not be NULL"));
	}

	if (!libhsmp_cmd(lhh, targ, &libhsmp_fn_freq_limit, &cmd))
		return (false);

	/*
	 * The first argument register carries the frequency limit in MHz in
	 * its upper half, with the sources of the limit in the lower half.
	 */
	*limitp = bitx32(cmd.uc_args[0], 31, 16);
	*srcsp = (libhsmp_freq_src_t)bitx32(cmd.uc_args[0], 15, 0);
	return (true);
}

const char *
libhsmp_freq_src_str(libhsmp_freq_src_t src)
{
	switch (src) {
	case LIBHSMP_FREQ_SRC_CHTC_ACTIVE:
		return ("cHTC-Active");
	case LIBHSMP_FREQ_SRC_PROCHOT:
		return ("PROCHOT");
	case LIBHSMP_FREQ_SRC_TDC:
		return ("TDC");
	case LIBHSMP_FREQ_SRC_PPT:
		return ("PPT");
	case LIBHSMP_FREQ_SRC_OPN_MAX:
		return ("OPN-Max");
	case LIBHSMP_FREQ_SRC_RELIABILITY:
		return ("Reliability");
	case LIBHSMP_FREQ_SRC_APML_AGENT:
		return ("APML-Agent");
	case LIBHSMP_FREQ_SRC_HSMP_AGENT:
		return ("HSMP-Agent");
	default:
		break;
	}
	return (NULL);
}
