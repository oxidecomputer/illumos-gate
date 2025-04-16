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
 * Device driver to work with Xsight Labs programmable network ASICs.
 * Supports X2.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/pci.h>
#include <sys/ksynch.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/list.h>

#include <sys/x2.h>
#include "x2_impl.h"

#define	X2_MAX_INSTANCE	16

static kmutex_t x2_mutex;	/* protects the list below */
static list_t x2_devices;	/* all x2 devices attached to the system */

static void		*x2_soft_state = NULL;
static id_space_t	*x2_minors = NULL;
int			x2_debug = 1;

static int x2_instance_init(x2_t *x2, minor_t minor);
static void x2_instance_fini(x2_t *x2, minor_t minor);

static x2_t *
x2_minor_to_device(int instance)
{
	x2_instance_data_t *xid;

	xid = ddi_get_soft_state(x2_soft_state, instance);

	return ((xid == NULL) ? NULL : xid->xid_x2);
}

/*
 * Utility function for debug logging
 */
void
x2_dlog(x2_t *x2, const char *fmt, ...)
{
	va_list args;

	if (x2_debug) {
		va_start(args, fmt);
		vdev_err(x2->x2_dip, CE_NOTE, fmt, args);
		va_end(args);
	}
}

/*
 * Utility function for error logging
 */
void
x2_err(x2_t *x2, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(x2->x2_dip, CE_WARN, fmt, args);
	va_end(args);
}

/*
 * Read a single 64-bit register from the device's MMIO space.  The offset is
 * provided in bytes.
 */
int
x2_read_reg(dev_info_t *dip, size_t offset, uint64_t *val)
{
	x2_t *x2 = ddi_get_driver_private(dip);
	ddi_acc_handle_t hdl = x2->x2_regs_hdls[0];
	caddr_t base = x2->x2_regs_bases[0];

	if (offset > x2->x2_regs_lens[0]) {
		x2_dlog(x2, "out of range.  Offset: %lx  limit: %lx",
			offset, x2->x2_regs_lens[0]);

		return (EINVAL);
	}
	*val = ddi_get64(hdl, (uint64_t *)(base + offset));
	return (0);
}

/*
 * Write to a single 64-bit register in the device's MMIO space.  The offset is
 * provided in bytes.
 */
int
x2_write_reg(dev_info_t *dip, size_t offset, uint64_t val)
{
	x2_t *x2 = ddi_get_driver_private(dip);
	ddi_acc_handle_t hdl = x2->x2_regs_hdls[0];
	caddr_t base = x2->x2_regs_bases[0];

	if (offset > x2->x2_regs_lens[0])
		return (EINVAL);

	ddi_put64(hdl, (uint64_t *)(base + offset), val);
	return (0);
}

static int
x2_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	minor_t minor = getminor(*devp);
	x2_t *x2;
	int err;

	if ((x2 = x2_minor_to_device(minor)) == NULL)
		return (ENXIO);

	/*
	 * The x2 management software is always expected to be 64-bit, so
	 * the driver will not support 32-bit clients.
	 */
	if (get_udatamodel() != DATAMODEL_LP64)
		return (ENOSYS);
	if (otyp != OTYP_CHR)
		return (EINVAL);

	minor = id_alloc_nosleep(x2_minors);
	if (minor == -1) {
		/* All minors are busy */
		return (EBUSY);
	}

	if ((err = x2_instance_init(x2, minor)) != 0) {
		id_free(x2_minors, minor);
		return (err);
	}
	*devp = makedevice(getmajor(*devp), minor);

	return (0);
}

/* 4k aligned DMA for in-kernel buffers */
ddi_dma_attr_t x2_dma_attr = {
	.dma_attr_version =		DMA_ATTR_V0,
	.dma_attr_addr_lo =		0x0000000000000000ull,
	.dma_attr_addr_hi =		0xFFFFFFFFFFFFFFFFull,
	.dma_attr_count_max =		0x00000000FFFFFFFFull,
	.dma_attr_align =		0x0000000000001000ull,
	.dma_attr_burstsizes =		0x00000FFF,
	.dma_attr_minxfer =		0x00000001,
	.dma_attr_maxxfer =		0x00000000FFFFFFFFull,
	.dma_attr_seg =			0xFFFFFFFFFFFFFFFFull,
	.dma_attr_sgllen =		1,
	.dma_attr_granular =		1,
	.dma_attr_flags =		0,
};

/* 2MB aligned allocations for the userspace daemon */
ddi_dma_attr_t x2_user_dma_attr = {
	.dma_attr_version =		DMA_ATTR_V0,
	.dma_attr_addr_lo =		0x0000000000000000ull,
	.dma_attr_addr_hi =		0xFFFFFFFFFFFFFFFFull,
	.dma_attr_count_max =		0x00000000FFFFFFFFull,
	.dma_attr_align =		0x0000000000200000ull,
	.dma_attr_burstsizes =		0x00000FFF,
	.dma_attr_minxfer =		0x00000001,
	.dma_attr_maxxfer =		0x00000000FFFFFFFFull,
	.dma_attr_seg =			0xFFFFFFFFFFFFFFFFull,
	.dma_attr_sgllen =		1,
	.dma_attr_granular =		1,
	.dma_attr_flags =		0,
};

static int
x2_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	const x2_version_t x2_version = {
		.x2_major = X2_DRIVER_MAJOR,
		.x2_minor = X2_DRIVER_MINOR,
		.x2_patch = X2_DRIVER_PATCH,
	};

	x2_instance_data_t *xid;
	x2_t *x2;

	xid = ddi_get_soft_state(x2_soft_state, getminor(dev));
	ASSERT(xid != NULL);
	x2 = xid->xid_x2;

	switch (cmd) {
		case X2_GET_VERSION: {
			if (ddi_copyout(&x2_version, (void *)arg,
			    sizeof (x2_version), mode)) {
				return (EFAULT);
			} else {
				return (0);
			}
		}
		case X2_REG_READ: {
			x2_reg_op_t op;
			uint64_t value;
			int rval;

			if (ddi_copyin((void *)(uintptr_t)arg, &op,
			    sizeof (op), mode) != 0) {
				return (EFAULT);
			}
			rval = x2_read_reg(x2->x2_dip, op.xro_address, &value);
			if (rval == 0) {
				op.xro_value = value;
				if (ddi_copyout(&op, (void *)arg, sizeof (op),
				    mode) != 0) {
					    rval = EFAULT;
				}
			}
			return (rval);
		}
		case X2_REG_WRITE: {
			x2_reg_op_t op;
			int rval;

			if (ddi_copyin((void *)(uintptr_t)arg, &op,
			    sizeof (op), mode) != 0) {
				return (EFAULT);
			}
			return (x2_write_reg(x2->x2_dip, op.xro_address,
			    op.xro_value));
		}
	}

	return (ENOTTY);
}

static int
x2_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	minor_t m = getminor(dev);
	x2_t *x2 = x2_minor_to_device(m);

	if (x2 == NULL)
		return (ENXIO);

	x2_instance_fini(x2, m);
	id_free(x2_minors, m);
	return (0);
}

static int
x2_regs_map(x2_t *x2)
{
	ddi_device_acc_attr_t da;

	bzero(&da, sizeof (da));
	da.devacc_attr_version = DDI_DEVICE_ATTR_V1;
	da.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	da.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	da.devacc_attr_access = DDI_DEFAULT_ACC;

	for (uint_t i = 0; i < X2_NBARS; i++) {
		int ret;
		off_t memsize;
		caddr_t base;
		ddi_acc_handle_t hdl;
		uint_t regno = i + 1;

		/*
		 * Entry zero into the regs[] array is device information.
		 * Registers to map start at entry 1.
		 */
		regno = i + 1;
		if (ddi_dev_regsize(x2->x2_dip, regno, &memsize) != 0) {
			x2_err(x2, "!failed to get register set size for "
			    "regs[%u]", i + 1);
			continue;
			return (-1);
		}

		ret = ddi_regs_map_setup(x2->x2_dip, regno, &base, 0, memsize,
		    &da, &hdl);

		if (ret != DDI_SUCCESS) {
			x2_err(x2, "!failed to map register set %u: %d",
			    i, ret);
			continue;
			return (-1);
		}

		x2->x2_regs_lens[i] = memsize;
		x2->x2_regs_bases[i] = base;
		x2->x2_regs_hdls[i] = hdl;
	}

	return (0);
}

static int
x2_minor_create(x2_t *x2)
{
	minor_t m = (minor_t)ddi_get_instance(x2->x2_dip);
	int err;

	if (ddi_create_minor_node(x2->x2_dip, "x2", S_IFCHR, m, DDI_PSEUDO,
	    0) != DDI_SUCCESS) {
		dev_err(x2->x2_dip, CE_WARN, "unable to create minor node");
		return (-1);
	}

	if ((err = x2_instance_init(x2, m)) != 0) {
		ddi_remove_minor_node(x2->x2_dip, "x2");
		return (err);
	}

	return (0);
}

static void
x2_cleanup(x2_t *x2)
{
	ddi_set_driver_private(x2->x2_dip, NULL);
	mutex_exit(&x2->x2_mutex);
	mutex_destroy(&x2->x2_mutex);
	cv_destroy(&x2->x2_cv);

	kmem_free(x2, sizeof (*x2));
}

static int
x2_instance_init(x2_t *x2, minor_t minor)
{
	x2_instance_data_t *xid;

	if (ddi_soft_state_zalloc(x2_soft_state, minor) != DDI_SUCCESS) {
		x2_err(x2, "!failed to alloc softstate for %d", minor);
		return (ENOMEM);
	}

	xid = ddi_get_soft_state(x2_soft_state, minor);
	xid->xid_x2 = x2;
	mutex_init(&xid->xid_mutex, NULL, MUTEX_DRIVER, NULL);

	return (0);
}

static void
x2_instance_fini(x2_t *x2, minor_t minor)
{
	x2_instance_data_t *xid;

	xid = ddi_get_soft_state(x2_soft_state, minor);
	if (xid == NULL)
		return;

	mutex_destroy(&xid->xid_mutex);

	ddi_soft_state_free(x2_soft_state, minor);
}

static int
x2_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	x2_t *x2;
	int instance;

	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);

	x2 = kmem_zalloc(sizeof (*x2), KM_SLEEP);
	x2->x2_dip = dip;
	x2->x2_instance = instance;
	ddi_set_driver_private(dip, x2);

	mutex_init(&x2->x2_mutex, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(x2->x2_intr_pri));
	cv_init(&x2->x2_cv,  NULL, CV_DRIVER, NULL);

	if (pci_config_setup(dip, &x2->x2_cfgspace) != DDI_SUCCESS) {
		x2_err(x2, "!failed to set up pci config space");
		goto cleanup;
	}

	if (x2_regs_map(x2) != 0) {
		goto cleanup;
	}

	if (x2_minor_create(x2) != 0) {
		goto cleanup;
	}
	mutex_enter(&x2_mutex);
	list_insert_head(&x2_devices, x2);
	mutex_exit(&x2_mutex);

	ddi_report_dev(dip);
	x2_dlog(x2, "!%s(): x2 driver attached", __func__);
	return (DDI_SUCCESS);

cleanup:
	mutex_enter(&x2->x2_mutex);
	x2_cleanup(x2);
	return (DDI_FAILURE);
}

static int
x2_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	x2_t *x2;
	minor_t minor;

	if (cmd != DDI_INFO_DEVT2DEVINFO && cmd != DDI_INFO_DEVT2INSTANCE)
		return (DDI_FAILURE);

	minor = getminor((dev_t)arg);
	x2 = x2_minor_to_device(minor);
	if (x2 == NULL)
		return (DDI_FAILURE);

	if (cmd == DDI_INFO_DEVT2DEVINFO)
		*resultp = (void *)x2->x2_dip;
	else
		*resultp = (void *)(uintptr_t)x2->x2_instance;

	return (DDI_SUCCESS);
}

static int
x2_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	x2_t *x2;
	int rval;

	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	x2 = ddi_get_driver_private(dip);
	if (x2 == NULL) {
		dev_err(dip, CE_WARN, "!asked to detach but no private data");
		return (DDI_FAILURE);
	}

	mutex_enter(&x2_mutex);
	mutex_enter(&x2->x2_mutex);
	list_remove(&x2_devices, x2);
	rval = DDI_SUCCESS;
	mutex_exit(&x2_mutex);

	if (rval == DDI_SUCCESS) {
		x2_cleanup(x2);
	} else {
		mutex_exit(&x2->x2_mutex);
	}

	return (rval);
}

static struct cb_ops x2_cb_ops = {
	.cb_open =			x2_open,
	.cb_close =			x2_close,
	.cb_strategy =			nodev,
	.cb_print =			nodev,
	.cb_dump =			nodev,
	.cb_read =			nodev,
	.cb_write =			nodev,
	.cb_ioctl =			x2_ioctl,
	.cb_devmap =			nodev,
	.cb_mmap =			nodev,
	.cb_segmap =			nodev,
	.cb_chpoll =			nochpoll,
	.cb_prop_op =			ddi_prop_op,
	.cb_flag =			D_MP,
	.cb_rev =			CB_REV,
	.cb_aread =			nodev,
	.cb_awrite =			nodev
};

static struct dev_ops x2_dev_ops = {
	.devo_rev =			DEVO_REV,
	.devo_getinfo =			x2_getinfo,
	.devo_identify =		nulldev,
	.devo_probe =			nulldev,
	.devo_attach =			x2_attach,
	.devo_detach =			x2_detach,
	.devo_reset =			nodev,
	.devo_quiesce =			ddi_quiesce_not_supported,
	.devo_cb_ops =			&x2_cb_ops
};

static struct modldrv x2_modldrv = {
	.drv_modops =			&mod_driverops,
	.drv_linkinfo =			"X2 ASIC Driver",
	.drv_dev_ops =			&x2_dev_ops
};

static struct modlinkage x2_modlinkage = {
	.ml_rev =			MODREV_1,
	.ml_linkage =			{ &x2_modldrv, NULL },
};

static void
x2_mod_cleanup(void)
{
	ddi_soft_state_fini(&x2_soft_state);
	id_space_destroy(x2_minors);
	x2_soft_state = NULL;

	mutex_enter(&x2_mutex);
	ASSERT3P(list_head(&x2_devices), ==, NULL);
	list_destroy(&x2_devices);
	mutex_exit(&x2_mutex);
	mutex_destroy(&x2_mutex);
}

int
_init(void)
{
	int err;

	err = ddi_soft_state_init(&x2_soft_state,
	    sizeof (x2_instance_data_t), 0);
	if (err != 0) {
		return (err);
	}

	mutex_init(&x2_mutex, NULL, MUTEX_DRIVER, NULL);
	list_create(&x2_devices, sizeof (x2_t),
	    offsetof(x2_t, x2_link));
	x2_minors = id_space_create("x2_minors", X2_MAX_INSTANCE + 1,
	    UINT16_MAX);

	err = mod_install(&x2_modlinkage);
	if (err != 0) {
		x2_mod_cleanup();
	}

	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&x2_modlinkage, modinfop));
}

int
_fini(void)
{
	int err;

	if ((err = mod_remove(&x2_modlinkage)) == 0)
		x2_mod_cleanup();

	return (err);
}
