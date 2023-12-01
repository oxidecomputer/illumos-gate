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
 * This driver provides access to the kernel's copy of the APOB on the oxide
 * architecture, allowing a user client to map the APOB read-only.  It does not
 * interpret any part of the APOB; code to do that from userland (or the kernel)
 * is available in common/apob.
 *
 * Each instance of this driver -- why you'd ever want more than one is a
 * mystery -- has its own handle, which means it has its own error state and
 * could have its own lock to protect same.  The APOB itself is always read-only
 * and is shared among all consumers.  At present, we don't use any of the
 * common APOB functions, so we don't use the error state and don't need any
 * locks, but someday we might decide to implement an ioctl interface or
 * something fancier in the kernel instead of just exposing the entire APOB to
 * userland.
 */

#include <sys/file.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/stat.h>
#include <sys/policy.h>
#include <sys/modctl.h>
#include <sys/apob.h>
#include <sys/kapob.h>
#include <sys/sysmacros.h>

typedef struct apob_state {
	dev_info_t		*apob_dip;
	dev_t			apob_dev;
	apob_hdl_t		*apob_hdl;
	ddi_umem_cookie_t	apob_umem_cookie;
} apob_state_t;

static void *apob_state;

static int
apob_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **result_p)
{
	if (cmd == DDI_INFO_DEVT2DEVINFO) {
		dev_t dev = (dev_t)arg;
		minor_t minor = getminor(dev);
		int instance = (int)minor;

		apob_state_t *apob =
		    ddi_get_soft_state(apob_state, instance);
		if (apob == NULL)
			return (DDI_FAILURE);

		*result_p = (void *)(apob->apob_dip);
		return (DDI_SUCCESS);
	} else if (cmd == DDI_INFO_DEVT2INSTANCE) {
		dev_t dev = (dev_t)arg;
		minor_t minor = getminor(dev);

		*result_p = (void *)(uintptr_t)(int)minor;
		return (DDI_SUCCESS);
	} else {
		return (DDI_FAILURE);
	}
}

static int
apob_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int instance;
	apob_state_t *apob;

	if (cmd == DDI_SUSPEND) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	apob = ddi_get_soft_state(apob_state, instance);
	if (apob == NULL)
		return (DDI_SUCCESS);

	kmem_free(apob->apob_hdl, apob_handle_size());
	ddi_remove_minor_node(dip, NULL);
	ddi_soft_state_free(apob_state, instance);

	return (DDI_SUCCESS);
}

static int
apob_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	apob_state_t *apob;
	int instance;
	int rc;

	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	rc = ddi_soft_state_zalloc(apob_state, instance);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to allocate soft state: %d", rc);
		return (DDI_FAILURE);
	}

	apob = ddi_get_soft_state(apob_state, instance);
	apob->apob_hdl = kmem_zalloc(apob_handle_size(), KM_SLEEP);

	if (!kapob_clone_handle(apob->apob_hdl, &apob->apob_umem_cookie)) {
		dev_err(dip, CE_WARN, "failed to clone APOB handle");
		rc = DDI_FAILURE;
		goto done;
	}

	ddi_set_driver_private(dip, apob);
	apob->apob_dip = dip;

	rc = ddi_create_minor_node(dip, "apob", S_IFCHR,
	    (minor_t)instance, DDI_PSEUDO, 0);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN,
		    "failed to create device minor node: %d", rc);
		goto done;
	}

done:
	if (rc != DDI_SUCCESS)
		(void) apob_detach(dip, DDI_DETACH);

	return (rc);
}

static int
apob_cb_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	minor_t minor = getminor(*dev_p);
	int instance = (int)minor;
	apob_state_t *apob;

	if (crgetzoneid(cred_p) != GLOBAL_ZONEID ||
	    secpolicy_sys_config(cred_p, B_FALSE) != 0) {
		return (EPERM);
	}

	if ((flag & (FEXCL | FNDELAY | FNONBLOCK)) != 0) {
		return (EINVAL);
	}

	if ((flag & (FREAD | FWRITE)) != FREAD) {
		return (EINVAL);
	}

	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}

	apob = ddi_get_soft_state(apob_state, instance);
	if (apob == NULL)
		return (ENXIO);

	return (0);
}

static int
apob_cb_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	minor_t minor = getminor(dev);
	int instance = (int)minor;
	apob_state_t *apob = ddi_get_soft_state(apob_state, instance);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	if (apob == NULL)
		return (ENXIO);

	return (0);
}

static int
apob_cb_devmap(dev_t dev, devmap_cookie_t dmc, offset_t off, size_t len,
    size_t *maplen, uint_t model)
{
	int rc;
	minor_t minor = getminor(dev);
	int instance = (int)minor;
	apob_state_t *apob = ddi_get_soft_state(apob_state, instance);
	size_t apob_len;
	offset_t off_aligned;
	size_t len_aligned;

	/*
	 * This is documented to be ignored, but we provide it anyway and
	 * specify the most permissive configuration we can because this is
	 * simply ordinary cacheable memory.  We don't specify STORECACHING
	 * because stores are never allowed at all.
	 */
	struct ddi_device_acc_attr acc = {
		.devacc_attr_version = DDI_DEVICE_ATTR_V1,
		.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC,
		.devacc_attr_dataorder = DDI_LOADCACHING_OK_ACC,
		.devacc_attr_access = DDI_DEFAULT_ACC
	};
	struct devmap_callback_ctl cb = {
		.devmap_rev = DEVMAP_OPS_REV,
		.devmap_map = NULL,
		.devmap_access = NULL,
		.devmap_dup = NULL,
		.devmap_unmap = NULL
	};

	if (apob == NULL)
		return (ENXIO);

	apob_len = apob_get_len(apob->apob_hdl);

	if (apob_len == 0)
		return (ENXIO);

	if (off >= apob_len || off < 0)
		return (EINVAL);

	if (len > apob_len - off)
		len = apob_len - off;

	off_aligned = P2ALIGN(off, PAGESIZE);
	len_aligned = P2ROUNDUP(off + len, PAGESIZE) - off_aligned;

	rc = devmap_umem_setup(dmc, apob->apob_dip, &cb, apob->apob_umem_cookie,
	    off_aligned, len_aligned, PROT_READ | PROT_USER, 0, &acc);

	if (rc == 0)
		*maplen = len_aligned;

	return (rc);
}

struct cb_ops apob_cb_ops = {
	.cb_open =		apob_cb_open,
	.cb_close =		apob_cb_close,
	.cb_strategy =		nodev,
	.cb_print =		nodev,
	.cb_dump =		nodev,
	.cb_read =		nodev,
	.cb_write =		nodev,
	.cb_ioctl =		nodev,
	.cb_devmap =		apob_cb_devmap,
	.cb_mmap =		nodev,
	.cb_segmap =		ddi_devmap_segmap,
	.cb_chpoll =		nochpoll,
	.cb_prop_op =		ddi_prop_op,
	.cb_flag =		D_MP | D_DEVMAP,
	.cb_rev =		CB_REV,
	.cb_aread =		nodev,
	.cb_awrite =		nodev
};

struct dev_ops apob_dev_ops = {
	.devo_rev =		DEVO_REV,
	.devo_getinfo =		apob_getinfo,
	.devo_identify =	nulldev,
	.devo_probe =		nulldev,
	.devo_attach =		apob_attach,
	.devo_detach =		apob_detach,
	.devo_reset =		nodev,
	.devo_cb_ops =		&apob_cb_ops,
	.devo_bus_ops =		NULL,
	.devo_quiesce =		ddi_quiesce_not_needed,
};

static struct modldrv apob_modldrv = {
	.drv_modops =	&mod_driverops,
	.drv_linkinfo =	"Oxide APOB access driver",
	.drv_dev_ops =	&apob_dev_ops
};

static struct modlinkage apob_modlinkage = {
	.ml_rev =	MODREV_1,
	.ml_linkage =	{ &apob_modldrv, NULL }
};

int
_init(void)
{
	int rc = ddi_soft_state_init(&apob_state, sizeof (apob_state_t), 0);
	if (rc != 0)
		return (rc);

	rc = mod_install(&apob_modlinkage);
	if (rc != 0)
		ddi_soft_state_fini(&apob_state);

	return (rc);
}

int
_fini(void)
{
	int rc = mod_remove(&apob_modlinkage);
	if (rc != 0)
		return (rc);

	ddi_soft_state_fini(&apob_state);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&apob_modlinkage, modinfop));
}
