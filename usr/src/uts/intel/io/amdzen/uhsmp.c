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
 * A device driver that provides user access to the AMD Host System Management
 * Port (HSMP) for debugging purposes.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/x86_archext.h>
#include <sys/cpuvar.h>
#include <sys/cmn_err.h>
#include <sys/policy.h>
#include <amdzen_client.h>
#include <sys/amdzen/hsmp.h>

#include "uhsmp.h"

uint_t uhsmp_reply_retry_count = 100;
uint_t uhsmp_reply_retry_delay = 10;	/* ticks */

typedef struct {
	dev_info_t *uhsmp_dip;
	x86_processor_family_t uhsmp_fam;
	uint_t uhsmp_ndfs;
	uint_t uhsmp_ifver;
	uint_t uhsmp_maxfn;
	kmutex_t uhsmp_lock;
} uhsmp_t;

/*
 * This provides a mapping between the interface version, as reported by the
 * HSMP "GetInterfaceVersion" function, and the number of available functions.
 * The versions start at 1 and AMD documentation does not mention version 6
 * which was presumably never released. If we encounter it we will log a
 * warning and fail to attach.
 */
static const uint_t uhsmp_ifver_maxfn[] = {
	[1] = HSMP_IFVER1_FUNCS,
	[2] = HSMP_IFVER2_FUNCS,
	[3] = HSMP_IFVER3_FUNCS,
	[4] = HSMP_IFVER4_FUNCS,
	[5] = HSMP_IFVER5_FUNCS,
	[7] = HSMP_IFVER7_FUNCS
};

static uhsmp_t uhsmp_data;

static int
uhsmp_open(dev_t *devp, int flags, int otype, cred_t *credp)
{
	minor_t m;
	uhsmp_t *uhsmp = &uhsmp_data;

	if (crgetzoneid(credp) != GLOBAL_ZONEID ||
	    secpolicy_hwmanip(credp) != 0) {
		return (EPERM);
	}

	if ((flags & (FEXCL | FNDELAY | FNONBLOCK)) != 0)
		return (EINVAL);

	if (otype != OTYP_CHR)
		return (EINVAL);

	m = getminor(*devp);
	if (m >= uhsmp->uhsmp_ndfs)
		return (ENXIO);

	return (0);
}

static int
uhsmp_cmd(uhsmp_t *uhsmp, uint_t dfno, uhsmp_cmd_t *cmd)
{
	const smn_reg_t id = SMN_HSMP_MSGID(uhsmp->uhsmp_fam);
	const smn_reg_t resp = SMN_HSMP_RESP;
	const smn_reg_t args[] = {
		SMN_HSMP_ARG(0),
		SMN_HSMP_ARG(1),
		SMN_HSMP_ARG(2),
		SMN_HSMP_ARG(3),
		SMN_HSMP_ARG(4),
		SMN_HSMP_ARG(5),
		SMN_HSMP_ARG(6),
		SMN_HSMP_ARG(7)
	};
	int ret = 0;

	cmd->uc_response = 0;
	mutex_enter(&uhsmp->uhsmp_lock);
	if ((ret = amdzen_c_smn_write(dfno, resp, cmd->uc_response)) != 0)
		goto out;
	for (size_t i = 0; i < ARRAY_SIZE(args); i++) {
		ret = amdzen_c_smn_write(dfno, args[i], cmd->uc_args[i]);
		if (ret != 0)
			goto out;
	}
	if ((ret = amdzen_c_smn_write(dfno, id, cmd->uc_id)) != 0)
		goto out;
	for (uint_t i = 0; i < uhsmp_reply_retry_count; i++) {
		ret = amdzen_c_smn_read(dfno, resp, &cmd->uc_response);
		if (ret != 0)
			break;
		if (cmd->uc_response != 0)
			break;
		delay(uhsmp_reply_retry_delay);
	}
	if (cmd->uc_response == 0) {
		ret = ETIMEDOUT;
		goto out;
	}
	for (size_t i = 0; i < ARRAY_SIZE(args); i++) {
		ret = amdzen_c_smn_read(dfno, args[i], &cmd->uc_args[i]);
		if (ret != 0)
			goto out;
	}

out:
	mutex_exit(&uhsmp->uhsmp_lock);
	return (ret);
}

static int
uhsmp_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	uhsmp_t *uhsmp = &uhsmp_data;
	uhsmp_cmd_t ucmd;
	uint_t dfno;
	int ret;

	if (cmd != UHSMP_GENERIC_COMMAND)
		return (ENOTTY);

	/* The only currently supported command requires read/write */
	if ((mode & (FREAD|FWRITE)) != (FREAD|FWRITE))
		return (EINVAL);

	dfno = getminor(dev);
	if (dfno >= uhsmp->uhsmp_ndfs)
		return (ENXIO);

	if (crgetzoneid(credp) != GLOBAL_ZONEID ||
	    secpolicy_hwmanip(credp) != 0) {
		return (EPERM);
	}

	if (ddi_copyin((void *)arg, &ucmd, sizeof (ucmd), mode & FKIOCTL) != 0)
		return (EFAULT);

	if (ucmd.uc_id > uhsmp->uhsmp_maxfn)
		return (EINVAL);

	ret = uhsmp_cmd(uhsmp, 0, &ucmd);

	if (ret == 0 && ddi_copyout(&ucmd, (void *)arg, sizeof (ucmd),
	    mode & FKIOCTL) != 0) {
		ret = EFAULT;
	}

	return (ret);
}

static int
uhsmp_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	return (0);
}

static void
uhsmp_cleanup(uhsmp_t *uhsmp)
{
	ddi_remove_minor_node(uhsmp->uhsmp_dip, NULL);
	uhsmp->uhsmp_ndfs = 0;
	uhsmp->uhsmp_dip = NULL;
	mutex_destroy(&uhsmp->uhsmp_lock);
}

static int
uhsmp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	uhsmp_t *uhsmp = &uhsmp_data;
	int ret;

	if (cmd == DDI_RESUME)
		return (DDI_SUCCESS);
	else if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (uhsmp->uhsmp_dip != NULL) {
		dev_err(dip, CE_WARN,
		    "!uhsmp is already attached to a dev_info_t: %p",
		    uhsmp->uhsmp_dip);
		return (DDI_FAILURE);
	}

	uhsmp->uhsmp_fam = chiprev_family(cpuid_getchiprev(CPU));

	switch (uarchrev_uarch(cpuid_getuarchrev(CPU))) {
	case X86_UARCH_AMD_ZEN3:
	case X86_UARCH_AMD_ZEN4:
	case X86_UARCH_AMD_ZEN5:
		break;
	default:
		return (DDI_FAILURE);
	}

	uhsmp->uhsmp_dip = dip;
	mutex_init(&uhsmp->uhsmp_lock, NULL, MUTEX_DRIVER, NULL);

	/*
	 * Determine if HSMP is available by sending a test message and
	 * checking that it completes successfully in a reasonable amount of
	 * time. Working HSMP depends on some SMU setup having been done.
	 */
#define	HSMP_TESTVAL	0x1234567
	uhsmp_cmd_t testcmd = {
		.uc_id = HSMP_CMD_TESTMESSAGE,
		.uc_args[0] = HSMP_TESTVAL
	};
	if ((ret = uhsmp_cmd(uhsmp, 0, &testcmd)) != 0) {
		dev_err(dip, CE_CONT, "?UHSMP test error %d\n", ret);
		goto err;
	}
	if (testcmd.uc_response != HSMP_RESPONSE_OK ||
	    testcmd.uc_args[0] != HSMP_TESTVAL + 1) {
		dev_err(dip, CE_CONT, "?UHSMP test failed. "
		    "Response 0x%x, returned value 0x%x (expected 0x%x)\n",
		    testcmd.uc_response, testcmd.uc_args[0],
		    HSMP_TESTVAL + 1);
		goto err;
	}

	/* Determine the number of available HSMP functions */
	uhsmp_cmd_t vercmd = {
		.uc_id = HSMP_CMD_GETIFVERSION
	};
	if ((ret = uhsmp_cmd(uhsmp, 0, &vercmd)) != 0) {
		dev_err(dip, CE_CONT, "?UHSMP version command error %d", ret);
		goto err;
	}
	if (testcmd.uc_response != HSMP_RESPONSE_OK) {
		dev_err(dip, CE_CONT,
		    "?UHSMP version command failed. Response 0x%x",
		    testcmd.uc_response);
		goto err;
	}

	uhsmp->uhsmp_ifver = vercmd.uc_args[0];
	uhsmp->uhsmp_maxfn = 0;
	if (uhsmp->uhsmp_ifver < ARRAY_SIZE(uhsmp_ifver_maxfn)) {
		uhsmp->uhsmp_maxfn =
		    uhsmp_ifver_maxfn[uhsmp->uhsmp_ifver];
	}
	if (uhsmp->uhsmp_maxfn == 0) {
		dev_err(dip, CE_WARN,
		    "Unsupported UHSMP interface version 0x%x",
		    uhsmp->uhsmp_ifver);
		goto err;
	}

	uhsmp->uhsmp_ndfs = amdzen_c_df_count();
	for (uint_t i = 0; i < uhsmp->uhsmp_ndfs; i++) {
		char buf[32];

		(void) snprintf(buf, sizeof (buf), "uhsmp.%u", i);
		if (ddi_create_minor_node(dip, buf, S_IFCHR, i, DDI_PSEUDO,
		    0) != DDI_SUCCESS) {
			dev_err(dip, CE_WARN, "!failed to create minor %s",
			    buf);
			goto err;
		}
	}

	return (DDI_SUCCESS);

err:
	uhsmp_cleanup(uhsmp);
	return (DDI_FAILURE);
}

static int
uhsmp_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	uhsmp_t *uhsmp = &uhsmp_data;
	minor_t m;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		m = getminor((dev_t)arg);
		if (m >= uhsmp->uhsmp_ndfs)
			return (DDI_FAILURE);
		*resultp = (void *)uhsmp->uhsmp_dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		m = getminor((dev_t)arg);
		if (m >= uhsmp->uhsmp_ndfs)
			return (DDI_FAILURE);
		*resultp =
		    (void *)(uintptr_t)ddi_get_instance(uhsmp->uhsmp_dip);
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
uhsmp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	uhsmp_t *uhsmp = &uhsmp_data;

	if (cmd == DDI_SUSPEND)
		return (DDI_SUCCESS);
	else if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	if (uhsmp->uhsmp_dip != dip) {
		dev_err(dip, CE_WARN,
		    "!asked to detach uhsmp, but dip doesn't match");
		return (DDI_FAILURE);
	}

	uhsmp_cleanup(uhsmp);
	return (DDI_SUCCESS);
}

static struct cb_ops uhsmp_cb_ops = {
	.cb_open = uhsmp_open,
	.cb_close = uhsmp_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = uhsmp_ioctl,
	.cb_devmap = nodev,
	.cb_mmap = nodev,
	.cb_segmap = nodev,
	.cb_chpoll = nochpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_flag = D_MP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops uhsmp_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = uhsmp_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = uhsmp_attach,
	.devo_detach = uhsmp_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_needed,
	.devo_cb_ops = &uhsmp_cb_ops
};

static struct modldrv uhsmp_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "AMD User HSMP Access",
	.drv_dev_ops = &uhsmp_dev_ops
};

static struct modlinkage uhsmp_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &uhsmp_modldrv, NULL }
};

int
_init(void)
{
	return (mod_install(&uhsmp_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&uhsmp_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&uhsmp_modlinkage));
}
