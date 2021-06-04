/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (C) 2021 Oxide Computer Company.  All rights reserved.
 */

#include <sys/conf.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/devops.h>
#include <sys/errno.h>
#include <sys/mkdev.h>
#include <sys/modctl.h>
#include <sys/model.h>
#include <sys/open.h>
#include <sys/pci.h>
#include <sys/pci_cap.h>
#include <sys/spi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/types.h>
#include <sys/uio.h>

#ifndef PCI_VENDOR_ID_CHELSIO
# define PCI_VENDOR_ID_CHELSIO 0x1425
#endif


#define PCI_CAP_VPD_ADDRESS_OFFSET	2
#define PCI_CAP_VPD_DATA_OFFSET		4

#define PCI_CAP_VPD_ADDRESS(f, a)	((((f) & PCI_CAP_VPD_ADDRESS_FLAG_MASK) << PCI_CAP_VPD_ADDRESS_FLAG_SHIFT) \
					 | (((a) & PCI_CAP_VPD_ADDRESS_ADDRESS_MASK) << PCI_CAP_VPD_ADDRESS_ADDRESS_SHIFT))

#define PCI_CAP_VPD_ADDRESS_FLAG_BITS	1
#define PCI_CAP_VPD_ADDRESS_FLAG_SHIFT	15
#define PCI_CAP_VPD_ADDRESS_FLAG_MASK	((1U << PCI_CAP_VPD_ADDRESS_FLAG_BITS) - 1)
#define PCI_CAP_VPD_ADDRESS_FLAG(x)	(((x) >> PCI_CAP_VPD_ADDRESS_FLAG_SHIFT) & PCI_CAP_VPD_ADDRESS_FLAG_MASK)

#define PCI_CAP_VPD_ADDRESS_FLAG_READ	0
#define PCI_CAP_VPD_ADDRESS_FLAG_WRITE	1

#define PCI_CAP_VPD_ADDRESS_ADDRESS_BITS	15
#define PCI_CAP_VPD_ADDRESS_ADDRESS_SHIFT	0
#define PCI_CAP_VPD_ADDRESS_ADDRESS_MASK	((1U << PCI_CAP_VPD_ADDRESS_ADDRESS_BITS) - 1)
#define PCI_CAP_VPD_ADDRESS_ADDRESS(x)		(((x) >> PCI_CAP_VPD_ADDRESS_ADDRESS_SHIFT) & PCI_CAP_VPD_ADDRESS_ADDRESS_MASK)

#define CHDBG_MINOR_NODE_BITS		2
#define CHDBG_MINOR_NODE_SHIFT		0
#define CHDBG_MINOR_NODE_MASK		((1U << CHDBG_MINOR_NODE_BITS) - 1)
#define CHDBG_MINOR_NODE(x)		(((x) >> CHDBG_MINOR_NODE_SHIFT) & CHDBG_MINOR_NODE_MASK)

#define CHDBG_MINOR_INSTANCE_BITS	(NBITSMINOR - CHDBG_MINOR_INSTANCE_SHIFT)
#define CHDBG_MINOR_INSTANCE_SHIFT	2
#define CHDBG_MINOR_INSTANCE_MASK	((1U << CHDBG_MINOR_INSTANCE_BITS) - 1)
#define CHDBG_MINOR_INSTANCE(x)		(((x) >> CHDBG_MINOR_INSTANCE_SHIFT) & CHDBG_MINOR_INSTANCE_MASK)

#define CHDBG_MINOR(i, n)		((((i) & CHDBG_MINOR_INSTANCE_MASK) << CHDBG_MINOR_INSTANCE_SHIFT) \
					 | (((n) & CHDBG_MINOR_NODE_MASK) << CHDBG_MINOR_NODE_SHIFT))

#define CHDBG_NODE_SROM			0
#define CHDBG_NODE_SPIDEV		1

/* SPI Flash (SF) controller registers */

#define SF_BASE		0x193f8

#define SF_DATA_OFFSET		0x0
#define SF_OP_OFFSET		0x4

#define SF_DATA_ADDR        SF_BASE + SF_DATA_OFFSET
#define SF_OP_ADDR          SF_BASE + SF_OP_OFFSET

#define SF_OP(op, bytecnt, cont, lock)	((((op) & SF_OP_OP_MASK) << SF_OP_OP_SHIFT) \
										 | ((((bytecnt) - 1) & SF_OP_BYTECNT_MASK) << SF_OP_BYTECNT_SHIFT) \
										 | (((cont) & SF_OP_CONT_MASK) << SF_OP_CONT_SHIFT) \
										 | (((lock) & SF_OP_LOCK_MASK) << SF_OP_LOCK_SHIFT))

#define SF_OP_OP_BITS		1
#define SF_OP_OP_SHIFT		0
#define SF_OP_OP_MASK		((1U << SF_OP_OP_BITS) - 1)
#define SF_OP_OP(x)		(((x) >> SF_OP_OP_SHIFT) & SF_OP_OP_MASK)

#define SF_OP_OP_READ		0
#define SF_OP_OP_WRITE		1

#define SF_OP_BYTECNT_BITS	2
#define SF_OP_BYTECNT_SHIFT	1
#define SF_OP_BYTECNT_MASK	((1U << SF_OP_BYTECNT_BITS) - 1)
#define SF_OP_BYTECNT(x)	((((x) - 1) >> SF_OP_BYTECNT_SHIFT) & SF_OP_BYTECNT_MASK)

#define SF_OP_CONT_BITS		1
#define SF_OP_CONT_SHIFT	3
#define SF_OP_CONT_MASK		((1U << SF_OP_CONT_BITS) - 1)
#define SF_OP_CONT(x)		(((x) >> SF_OP_CONT_SHIFT) & SF_OP_CONT_MASK)

#define SF_OP_LOCK_BITS		1
#define SF_OP_LOCK_SHIFT	4
#define SF_OP_LOCK_MASK		((1U << SF_OP_LOCK_BITS) - 1)
#define SF_OP_LOCK(x)		(((x) >> SF_OP_LOCK_SHIFT) & SF_OP_LOCK_MASK)

#define SF_OP_BUSY_BITS		1
#define SF_OP_BUSY_SHIFT	31
#define SF_OP_BUSY_MASK		((1U << SF_OP_BUSY_BITS) - 1)
#define SF_OP_BUSY(x)		(((x) >> SF_OP_BUSY_SHIFT) & SF_OP_BUSY_MASK)

typedef struct chdbg_devstate {
	dev_info_t *		dip;
	dev_t			dev;

	ddi_acc_handle_t	pci_config_handle;
	uint16_t		vpd_base;
	kmutex_t		vpd_lock;

	ddi_acc_handle_t	pio_kernel_regs_handle;
	void			*pio_kernel_regs;
	kmutex_t		sf_lock;
} chdbg_devstate_t;

static int chdbg_devo_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result_p);
static int chdbg_devo_probe(dev_info_t *dip);
static int chdbg_devo_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int chdbg_devo_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

static int chdbg_cb_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p);
static int chdbg_cb_close(dev_t dev, int flag, int otyp, cred_t *cred_p);
static int chdbg_cb_read(dev_t dev, struct uio *uio_p, cred_t *cred_p);
static int chdbg_cb_write(dev_t dev, struct uio *uio_p, cred_t *cred_p);
static int chdbg_cb_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p);

static int chdbg_srom_open(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int chdbg_srom_close(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int chdbg_srom_read(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int chdbg_srom_write(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int chdbg_srom_ioctl(chdbg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p);

static int chdbg_spidev_open(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int chdbg_spidev_close(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int chdbg_spidev_read(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int chdbg_spidev_write(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int chdbg_spidev_ioctl(chdbg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p);

struct cb_ops chdbg_cb_ops = {
	.cb_open =		chdbg_cb_open,
	.cb_close =		chdbg_cb_close,
	.cb_strategy =		nodev,
	.cb_print =		nodev,
	.cb_dump =		nodev,
	.cb_read =		chdbg_cb_read,
	.cb_write =		chdbg_cb_write,
	.cb_ioctl =		chdbg_cb_ioctl,
	.cb_devmap =		nodev,
	.cb_mmap =		nodev,
	.cb_segmap =		nodev,
	.cb_chpoll =		nochpoll,
	.cb_prop_op =		ddi_prop_op,
	.cb_flag =		D_MP,
	.cb_rev =		CB_REV,
	.cb_aread =		nodev,
	.cb_awrite =		nodev
};

struct dev_ops chdbg_dev_ops = {
	.devo_rev =		DEVO_REV,
	.devo_getinfo =		chdbg_devo_getinfo,
	.devo_identify =	nulldev,
	.devo_probe =		chdbg_devo_probe,
	.devo_attach =		chdbg_devo_attach,
	.devo_detach =		chdbg_devo_detach,
	.devo_reset =		nodev,
	.devo_cb_ops =		&chdbg_cb_ops,
	.devo_bus_ops =		NULL,
	.devo_quiesce =		ddi_quiesce_not_needed,
};

static struct modldrv modldrv = {
	.drv_modops =		&mod_driverops,
	.drv_linkinfo =		"Chelsio T6 debug mode",
	.drv_dev_ops =		&chdbg_dev_ops
};

static struct modlinkage modlinkage = {
	.ml_rev =		MODREV_1,
	.ml_linkage =		{&modldrv, NULL},
};

void *chdbg_devstate_list;

int
_init(void)
{
	int rc = ddi_soft_state_init(&chdbg_devstate_list, sizeof (chdbg_devstate_t), 0);
	if (rc != 0)
		return (rc);

	rc = mod_install(&modlinkage);
	if (rc != 0)
		ddi_soft_state_fini(&chdbg_devstate_list);

	return (rc);
}

int
_fini(void)
{
	int rc = mod_remove(&modlinkage);
	if (rc != 0)
		return (rc);

	ddi_soft_state_fini(&chdbg_devstate_list);
	return (0);
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&modlinkage, mi));
}

/* ARGSUSED */
static int
chdbg_devo_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result_p)
{

	dev_t dev = (dev_t)arg;
	minor_t minor = getminor(dev);
	int instance = CHDBG_MINOR_INSTANCE(minor);

	if (cmd == DDI_INFO_DEVT2DEVINFO) {
		chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
		if (devstate_p == NULL)
			return (DDI_FAILURE);

		*result_p = (void *)(devstate_p->dip);
	} else if (cmd == DDI_INFO_DEVT2INSTANCE)
		*result_p = (void *) (unsigned long) instance;
	else
		ASSERT(0);

	return (DDI_SUCCESS);
}

static int
chdbg_devo_probe(dev_info_t *dip)
{

	/* Prevent driver attachment on any PF except 0 */
	int *reg;
	uint_t n;
	int rc = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "reg", &reg, &n);
	if (rc != DDI_SUCCESS || n < 1)
		return (DDI_PROBE_DONTCARE);

	uint_t pf = PCI_REG_FUNC_G(reg[0]);
	ddi_prop_free(reg);

	if (pf != 0)
		return (DDI_PROBE_FAILURE);

	return (DDI_PROBE_DONTCARE);
}

static int
chdbg_devo_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	/*
	 * Allocate space for soft state.
	 */
	int instance = ddi_get_instance(dip);
	int rc = ddi_soft_state_zalloc(chdbg_devstate_list, instance);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to allocate soft state: %d", rc);
		return (DDI_FAILURE);
	}

	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	ddi_set_driver_private(dip, (caddr_t)devstate_p);
	devstate_p->dip = dip;

	mutex_init(&devstate_p->vpd_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&devstate_p->sf_lock, NULL, MUTEX_DRIVER, NULL);

	/*
	 * Enable access to the PCI config space.
	 */
	rc = pci_config_setup(dip, &devstate_p->pci_config_handle);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to enable PCI config space access: %d", rc);
		goto done;
	}

	/*
	 * Enable MMIO access.
	 */
	ddi_device_acc_attr_t da = {
		.devacc_attr_version = DDI_DEVICE_ATTR_V0,
		.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
		.devacc_attr_dataorder = DDI_UNORDERED_OK_ACC
	};

	rc = ddi_regs_map_setup(dip, 1, (caddr_t *)&devstate_p->pio_kernel_regs, 0, 0, &da, &devstate_p->pio_kernel_regs_handle);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to map device registers: %d", rc);
		goto done;
	}

	/*
	 * Create minor nodes for SROM and SPIDEV
	 */
	rc = ddi_create_minor_node(dip, "srom", S_IFCHR, CHDBG_MINOR(instance, CHDBG_NODE_SROM), DDI_PSEUDO, 0);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to create SROM device node: %d", rc);
		goto done;
	}

	rc = ddi_create_minor_node(dip, "spidev", S_IFCHR, CHDBG_MINOR(instance, CHDBG_NODE_SPIDEV), DDI_PSEUDO, 0);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to create SROM device node: %d", rc);
		goto done;
	}

done:
	if (rc != DDI_SUCCESS) {
		(void) chdbg_devo_detach(dip, DDI_DETACH);

		/* rc may have errno style errors or DDI errors */
		rc = DDI_FAILURE;
	}

	return (rc);
}

static int
chdbg_devo_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	int instance = ddi_get_instance(dip);
	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	if (devstate_p == NULL)
		return (DDI_SUCCESS);

	ddi_prop_remove_all(dip);
	ddi_remove_minor_node(dip, NULL);

	mutex_destroy(&devstate_p->sf_lock);
	mutex_destroy(&devstate_p->vpd_lock);

	if (devstate_p->pio_kernel_regs_handle != NULL)
		ddi_regs_map_free(&devstate_p->pio_kernel_regs_handle);

	if (devstate_p->pci_config_handle != NULL)
		pci_config_teardown(&devstate_p->pci_config_handle);

#ifdef DEBUG
	bzero(devstate_p, sizeof (*devstate_p));
#endif
	ddi_soft_state_free(chdbg_devstate_list, instance);

	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
chdbg_cb_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	minor_t minor = getminor(*dev_p);
	int instance = CHDBG_MINOR_INSTANCE(minor);
	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (CHDBG_MINOR_NODE(minor)) {
		case CHDBG_NODE_SROM:
			return chdbg_srom_open(devstate_p, flag, otyp, cred_p);
		case CHDBG_NODE_SPIDEV:
			return chdbg_spidev_open(devstate_p, flag, otyp, cred_p);
		default:
			return (ENXIO);
	}
}

/* ARGSUSED */
static int
chdbg_cb_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	minor_t minor = getminor(dev);
	int instance = CHDBG_MINOR_INSTANCE(minor);
	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (CHDBG_MINOR_NODE(minor)) {
		case CHDBG_NODE_SROM:
			return chdbg_srom_close(devstate_p, flag, otyp, cred_p);
		case CHDBG_NODE_SPIDEV:
			return chdbg_spidev_close(devstate_p, flag, otyp, cred_p);
		default:
			return (ENXIO);
	}
}

static int
chdbg_cb_read(dev_t dev, struct uio *uio_p, cred_t *cred_p) {
	minor_t minor = getminor(dev);
	int instance = CHDBG_MINOR_INSTANCE(minor);
	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (CHDBG_MINOR_NODE(minor)) {
		case CHDBG_NODE_SROM:
		       return chdbg_srom_read(devstate_p, uio_p, cred_p);
		case CHDBG_NODE_SPIDEV:
		       return chdbg_spidev_read(devstate_p, uio_p, cred_p);
		default:
			return (ENXIO);
	}
}

static int
chdbg_cb_write(dev_t dev, struct uio *uio_p, cred_t *cred_p) {
	minor_t minor = getminor(dev);
	int instance = CHDBG_MINOR_INSTANCE(minor);
	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (CHDBG_MINOR_NODE(minor)) {
		case CHDBG_NODE_SROM:
		       return chdbg_srom_write(devstate_p, uio_p, cred_p);
		case CHDBG_NODE_SPIDEV:
		       return chdbg_spidev_write(devstate_p, uio_p, cred_p);
		default:
			return (ENXIO);
	}
}

/* ARGSUSED */
static int
chdbg_cb_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p)
{
	minor_t minor = getminor(dev);
	int instance = CHDBG_MINOR_INSTANCE(minor);
	chdbg_devstate_t *devstate_p = ddi_get_soft_state(chdbg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (CHDBG_MINOR_NODE(minor)) {
		case CHDBG_NODE_SROM:
			return chdbg_srom_ioctl(devstate_p, cmd, arg, mode, cred_p, rval_p);
		case CHDBG_NODE_SPIDEV:
			return chdbg_spidev_ioctl(devstate_p, cmd, arg, mode, cred_p, rval_p);
		default:
			return (ENXIO);
	}
}

static int chdbg_srom_open(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	int retval = 0;

	/*
	 * SROM access is via VPD capability.  Locate it now both to tell the
	 * user early if there is a problem and to speed up read/write
	 * accesses.
	 */

	mutex_enter(&devstate_p->vpd_lock);

	if (devstate_p->vpd_base == 0) {
		int rc = pci_lcap_locate(devstate_p->pci_config_handle, PCI_CAP_ID_VPD,
				&devstate_p->vpd_base);
		if (rc != DDI_SUCCESS) {
			dev_err(devstate_p->dip, CE_WARN, "unable to locate VPD capability: %d", rc);
			retval = ENXIO;
			goto done;
		}
	}

done:
	mutex_exit(&devstate_p->vpd_lock);

	return retval;
}

static int chdbg_srom_close(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	return (0);
}

static int chdbg_srom_read(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	/* Return EOF for out-of-range offsets. */
	if (uio_p->uio_offset > 0xffff)
		return (0);

	int retval = 0;

	mutex_enter(&devstate_p->vpd_lock);

	while (uio_p->uio_offset <= 0xffff && uio_p->uio_resid > 0) {
		/* Per PCI 3.0 spec, VPD address must be DWORD aligned */
		uint16_t vpd_dword = uio_p->uio_offset & 0xfffc;
		uint16_t vpd_offset = uio_p->uio_offset - vpd_dword;

		int rc = pci_cap_put(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_16,
			       	PCI_CAP_ID_VPD, devstate_p->vpd_base,
				PCI_CAP_VPD_ADDRESS_OFFSET, PCI_CAP_VPD_ADDRESS(PCI_CAP_VPD_ADDRESS_FLAG_READ, vpd_dword));
		if (rc != DDI_SUCCESS) {
			dev_err(devstate_p->dip, CE_WARN, "write to VPD address register failed: %d", rc);
			retval = EIO;
			goto done;
		}


		for(int ii = 0; ; ++ii) {
			uint32_t vpd_addr = pci_cap_get(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_16,
			       	PCI_CAP_ID_VPD, devstate_p->vpd_base,
				PCI_CAP_VPD_ADDRESS_OFFSET);

			if (vpd_addr == PCI_CAP_EINVAL32) {
				dev_err(devstate_p->dip, CE_WARN, "read from VPD address register failed");
				retval = EIO;
				goto done;
			} else if (ii == 100) {
				dev_err(devstate_p->dip, CE_WARN, "VPD read timeout");
				retval = EIO;
				goto done;
			} else if (PCI_CAP_VPD_ADDRESS_FLAG(vpd_addr) == 1)
				break;
		}

		uint32_t vpd_data = pci_cap_get(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_32,
			       	PCI_CAP_ID_VPD, devstate_p->vpd_base,
				PCI_CAP_VPD_DATA_OFFSET);
		dev_err(devstate_p->dip, CE_WARN, "%s: read VPD data: %08x", __FUNCTION__, vpd_data);

		rc = uiomove((void*)&vpd_data + vpd_offset, sizeof(uint32_t) - vpd_offset, UIO_READ, uio_p);
		if (rc != DDI_SUCCESS) {
			retval = EIO;
			goto done;
		}
	}

done:
	mutex_exit(&devstate_p->vpd_lock);

	return retval;
}

static int chdbg_srom_write(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	return (ENOTSUP);
}

static int chdbg_srom_ioctl(chdbg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p) {
	return (ENOTTY);
}

static int chdbg_spidev_open(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	return (0);
}

static int chdbg_spidev_close(chdbg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	return (0);
}

static int chdbg_spidev_read(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	return (ENOTSUP);
}

static int chdbg_spidev_write(chdbg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	return (ENOTSUP);
}

static int chdbg_spidev_ioctl(chdbg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p) {
	STRUCT_DECL(spidev_transaction, xact);
	STRUCT_DECL(spidev_transfer, xfer);

	if (cmd != SPIDEV_TRANSACTION)
		return (ENOTTY);

	STRUCT_INIT(xact, mode);
	STRUCT_INIT(xfer, mode);

	if (copyin((void*)arg, STRUCT_BUF(xact), STRUCT_SIZE(xact)))
		return (EFAULT);

	int rc = 0;
	mutex_enter(&devstate_p->sf_lock);

	if (SF_OP_BUSY(ddi_get32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_OP_ADDR))) {
		rc = EBUSY;
		goto done;
	}

	for (void *xfer_up = STRUCT_FGETP(xact, spidev_xfers);
		 xfer_up < STRUCT_FGETP(xact, spidev_xfers) + STRUCT_FGET(xact, spidev_nxfers) * STRUCT_SIZE(xfer);
		 xfer_up += STRUCT_SIZE(xfer)) {
		
		if (copyin(xfer_up, STRUCT_BUF(xfer), STRUCT_SIZE(xfer))) {
			rc = EFAULT;
			goto done;
		}

		for (int cur_byte = 0; cur_byte < STRUCT_FGET(xfer, len); cur_byte += 4) {
			int bytes_to_transfer = min(STRUCT_FGET(xfer, len) - cur_byte, 4);
			int sf_op_op = SF_OP_OP_READ;

			/* Stage transmit word into transmit register */
			if (STRUCT_FGETP(xfer, tx_buf) != NULL) {
				sf_op_op = SF_OP_OP_WRITE;

				uint32_t tx_data;
				void *tx_buf_up = STRUCT_FGETP(xfer, tx_buf) + cur_byte;
				if (copyin(tx_buf_up, &tx_data, bytes_to_transfer)) {
					rc = EFAULT;
					goto done;
				}

				ddi_put32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_DATA_ADDR, tx_data);
			}

			/* Trigger transfer. If this is the last chunk of a transfer, change CS if requested. */
			int conn = !((cur_byte + bytes_to_transfer) == STRUCT_FGET(xfer, len) && STRUCT_FGET(xfer, cs_change));
			ddi_put32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_OP_ADDR,
				SF_OP(sf_op_op, bytes_to_transfer, conn, 1));

			/* Poll until controller has finished the operation */
			for (int ii = 0; ii < 10; ++ii) {
				if (!SF_OP_BUSY(ddi_get32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_OP_ADDR)))
					break;

				drv_usecwait(1);
			}

			/* Retrieve received word */
			if (STRUCT_FGETP(xfer, rx_buf) != NULL) {
				uint32_t rx_data = ddi_get32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_DATA_ADDR);

				void *rx_buf_up = STRUCT_FGETP(xfer, rx_buf) + cur_byte;
				if (copyout(&rx_data, rx_buf_up, bytes_to_transfer)) {
					rc = EFAULT;
					goto done;
				}
			}
		}

		drv_usecwait(STRUCT_FGET(xfer, delay_usec));

		/* Deassert CS as directed */
		if (STRUCT_FGET(xfer, cs_change)) {
			ddi_put32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_OP_ADDR,
				SF_OP(SF_OP_OP_READ, 0, 0, 1));
			drv_usecwait(STRUCT_FGET(xfer, cs_change_delay_usec));
		}
	}

	/* Unlock SF */
	ddi_put32(devstate_p->pio_kernel_regs_handle, devstate_p->pio_kernel_regs + SF_OP_ADDR,
		SF_OP(SF_OP_OP_READ, 1, 0, 0));

done:
	mutex_exit(&devstate_p->sf_lock);
	return (rc);
}
