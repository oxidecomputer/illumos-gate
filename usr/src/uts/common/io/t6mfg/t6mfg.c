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
#include <sys/sdt.h>
#include <sys/spi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
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

#define PCI_CAP_VPD_POLL_INTERVAL_USEC	100
#define PCI_CAP_VPD_POLL_ITERATIONS		100

#define T6MFG_MINOR_NODE_BITS		2
#define T6MFG_MINOR_NODE_SHIFT		0
#define T6MFG_MINOR_NODE_MASK		((1U << T6MFG_MINOR_NODE_BITS) - 1)
#define T6MFG_MINOR_NODE(x)		(((x) >> T6MFG_MINOR_NODE_SHIFT) & T6MFG_MINOR_NODE_MASK)

#define T6MFG_MINOR_INSTANCE_BITS	(NBITSMINOR - T6MFG_MINOR_INSTANCE_SHIFT)
#define T6MFG_MINOR_INSTANCE_SHIFT	2
#define T6MFG_MINOR_INSTANCE_MASK	((1U << T6MFG_MINOR_INSTANCE_BITS) - 1)
#define T6MFG_MINOR_INSTANCE(x)		(((x) >> T6MFG_MINOR_INSTANCE_SHIFT) & T6MFG_MINOR_INSTANCE_MASK)

#define T6MFG_MINOR(i, n)		((((i) & T6MFG_MINOR_INSTANCE_MASK) << T6MFG_MINOR_INSTANCE_SHIFT) \
					 | (((n) & T6MFG_MINOR_NODE_MASK) << T6MFG_MINOR_NODE_SHIFT))

#define T6MFG_NODE_SROM			0
#define T6MFG_NODE_SPIDEV		1

/* 
 * T6 SROM contains a 1kB initialization block before the VPD data. When using
 * the VPD capability to access SROM, the provided address is offset by the
 * hardware so that VPD address 0x0 points to SROM address 0x400 where the VPD
 * data begins.  The hardware wraps the address space so the initialization block
 * becomes the last 1kB of the VPD address space (e.g. VPD address 0x7C00 == SROM
 * address 0x0).
 */
#define T6MFG_VPD_TO_SROM_OFFSET			0x400

/*
 * SROM is accessed via VPD which provides a 15-bit, byte-indexed address space
 * that must be accessed with dword-alignment. T6 reserves the last dword of the
 * SROM address space to access the SPI EEPROM status register.
 */
#define T6MFG_SROM_MAX_ADDRESS				0x7ffb
#define T6MFG_SROM_STATUS_REGISTER_ADDRESS	0x7ffc

#define T6MFG_SROM_STATUS_REGISTER_RDY_L_BITS	1
#define T6MFG_SROM_STATUS_REGISTER_RDY_L_SHIFT	0
#define T6MFG_SROM_STATUS_REGISTER_RDY_L_MASK	((1U << T6MFG_SROM_STATUS_REGISTER_RDY_L_BITS) - 1)
#define T6MFG_SROM_STATUS_REGISTER_RDY_L(x)		(((x) >> T6MFG_SROM_STATUS_REGISTER_RDY_L_SHIFT) & T6MFG_SROM_STATUS_REGISTER_RDY_L_MASK)

#define T6MFG_SROM_WRITE_POLL_INTERVAL_USEC		1000
#define T6MFG_SROM_WRITE_POLL_ITERATIONS		20

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

typedef struct t6mfg_devstate {
	dev_info_t *		dip;
	dev_t			dev;

	ddi_acc_handle_t	pci_config_handle;
	uint16_t		vpd_base;
	kmutex_t		vpd_lock;

	ddi_acc_handle_t	pio_kernel_regs_handle;
	void			*pio_kernel_regs;

	kmutex_t		sf_lock;
} t6mfg_devstate_t;

static int t6mfg_devo_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result_p);
static int t6mfg_devo_probe(dev_info_t *dip);
static int t6mfg_devo_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int t6mfg_devo_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

static int t6mfg_cb_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p);
static int t6mfg_cb_close(dev_t dev, int flag, int otyp, cred_t *cred_p);
static int t6mfg_cb_read(dev_t dev, struct uio *uio_p, cred_t *cred_p);
static int t6mfg_cb_write(dev_t dev, struct uio *uio_p, cred_t *cred_p);
static int t6mfg_cb_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p);

static int t6mfg_srom_open(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int t6mfg_srom_close(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int t6mfg_srom_read(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int t6mfg_srom_write(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int t6mfg_srom_ioctl(t6mfg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p);

static int t6mfg_spidev_open(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int t6mfg_spidev_close(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p);
static int t6mfg_spidev_read(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int t6mfg_spidev_write(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p);
static int t6mfg_spidev_ioctl(t6mfg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p);

struct cb_ops t6mfg_cb_ops = {
	.cb_open =		t6mfg_cb_open,
	.cb_close =		t6mfg_cb_close,
	.cb_strategy =		nodev,
	.cb_print =		nodev,
	.cb_dump =		nodev,
	.cb_read =		t6mfg_cb_read,
	.cb_write =		t6mfg_cb_write,
	.cb_ioctl =		t6mfg_cb_ioctl,
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

struct dev_ops t6mfg_dev_ops = {
	.devo_rev =		DEVO_REV,
	.devo_getinfo =		t6mfg_devo_getinfo,
	.devo_identify =	nulldev,
	.devo_probe =		t6mfg_devo_probe,
	.devo_attach =		t6mfg_devo_attach,
	.devo_detach =		t6mfg_devo_detach,
	.devo_reset =		nodev,
	.devo_cb_ops =		&t6mfg_cb_ops,
	.devo_bus_ops =		NULL,
	.devo_quiesce =		ddi_quiesce_not_needed,
};

static struct modldrv modldrv = {
	.drv_modops =		&mod_driverops,
	.drv_linkinfo =		"Chelsio T6 manufacturing mode",
	.drv_dev_ops =		&t6mfg_dev_ops
};

static struct modlinkage modlinkage = {
	.ml_rev =		MODREV_1,
	.ml_linkage =		{&modldrv, NULL},
};

void *t6mfg_devstate_list;

int
_init(void)
{
	int rc = ddi_soft_state_init(&t6mfg_devstate_list, sizeof (t6mfg_devstate_t), 0);
	if (rc != 0)
		return (rc);

	rc = mod_install(&modlinkage);
	if (rc != 0)
		ddi_soft_state_fini(&t6mfg_devstate_list);

	return (rc);
}

int
_fini(void)
{
	int rc = mod_remove(&modlinkage);
	if (rc != 0)
		return (rc);

	ddi_soft_state_fini(&t6mfg_devstate_list);
	return (0);
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&modlinkage, mi));
}

/* ARGSUSED */
static int
t6mfg_devo_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result_p)
{

	dev_t dev = (dev_t)arg;
	minor_t minor = getminor(dev);
	int instance = T6MFG_MINOR_INSTANCE(minor);

	if (cmd == DDI_INFO_DEVT2DEVINFO) {
		t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
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
t6mfg_devo_probe(dev_info_t *dip)
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
t6mfg_devo_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	/*
	 * Allocate space for soft state.
	 */
	int instance = ddi_get_instance(dip);
	int rc = ddi_soft_state_zalloc(t6mfg_devstate_list, instance);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to allocate soft state: %d", rc);
		return (DDI_FAILURE);
	}

	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
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
	rc = ddi_create_minor_node(dip, "srom", S_IFCHR, T6MFG_MINOR(instance, T6MFG_NODE_SROM), DDI_PSEUDO, 0);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to create SROM device node: %d", rc);
		goto done;
	}

	rc = ddi_create_minor_node(dip, "spidev", S_IFCHR, T6MFG_MINOR(instance, T6MFG_NODE_SPIDEV), DDI_PSEUDO, 0);
	if (rc != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "failed to create SROM device node: %d", rc);
		goto done;
	}

done:
	if (rc != DDI_SUCCESS) {
		(void) t6mfg_devo_detach(dip, DDI_DETACH);

		/* rc may have errno style errors or DDI errors */
		rc = DDI_FAILURE;
	}

	return (rc);
}

static int
t6mfg_devo_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	int instance = ddi_get_instance(dip);
	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
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
	ddi_soft_state_free(t6mfg_devstate_list, instance);

	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
t6mfg_cb_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	minor_t minor = getminor(*dev_p);
	int instance = T6MFG_MINOR_INSTANCE(minor);
	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (T6MFG_MINOR_NODE(minor)) {
		case T6MFG_NODE_SROM:
			return t6mfg_srom_open(devstate_p, flag, otyp, cred_p);
		case T6MFG_NODE_SPIDEV:
			return t6mfg_spidev_open(devstate_p, flag, otyp, cred_p);
		default:
			return (ENXIO);
	}
}

/* ARGSUSED */
static int
t6mfg_cb_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	minor_t minor = getminor(dev);
	int instance = T6MFG_MINOR_INSTANCE(minor);
	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (T6MFG_MINOR_NODE(minor)) {
		case T6MFG_NODE_SROM:
			return t6mfg_srom_close(devstate_p, flag, otyp, cred_p);
		case T6MFG_NODE_SPIDEV:
			return t6mfg_spidev_close(devstate_p, flag, otyp, cred_p);
		default:
			return (ENXIO);
	}
}

static int
t6mfg_cb_read(dev_t dev, struct uio *uio_p, cred_t *cred_p) {
	minor_t minor = getminor(dev);
	int instance = T6MFG_MINOR_INSTANCE(minor);
	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (T6MFG_MINOR_NODE(minor)) {
		case T6MFG_NODE_SROM:
		       return t6mfg_srom_read(devstate_p, uio_p, cred_p);
		case T6MFG_NODE_SPIDEV:
		       return t6mfg_spidev_read(devstate_p, uio_p, cred_p);
		default:
			return (ENXIO);
	}
}

static int
t6mfg_cb_write(dev_t dev, struct uio *uio_p, cred_t *cred_p) {
	minor_t minor = getminor(dev);
	int instance = T6MFG_MINOR_INSTANCE(minor);
	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (T6MFG_MINOR_NODE(minor)) {
		case T6MFG_NODE_SROM:
		       return t6mfg_srom_write(devstate_p, uio_p, cred_p);
		case T6MFG_NODE_SPIDEV:
		       return t6mfg_spidev_write(devstate_p, uio_p, cred_p);
		default:
			return (ENXIO);
	}
}

/* ARGSUSED */
static int
t6mfg_cb_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p)
{
	minor_t minor = getminor(dev);
	int instance = T6MFG_MINOR_INSTANCE(minor);
	t6mfg_devstate_t *devstate_p = ddi_get_soft_state(t6mfg_devstate_list, instance);
	if (devstate_p == NULL)
		return (ENXIO);

	switch (T6MFG_MINOR_NODE(minor)) {
		case T6MFG_NODE_SROM:
			return t6mfg_srom_ioctl(devstate_p, cmd, arg, mode, cred_p, rval_p);
		case T6MFG_NODE_SPIDEV:
			return t6mfg_spidev_ioctl(devstate_p, cmd, arg, mode, cred_p, rval_p);
		default:
			return (ENXIO);
	}
}

static int t6mfg_vpd_read(t6mfg_devstate_t *devstate_p, uint16_t vpd_address, uint32_t *data) {
	int retval = DDI_SUCCESS;

	/* Per PCI Local Bus 3.0 spec, VPD address must be DWORD aligned */
	if (vpd_address & 0x0003)
		return (DDI_EINVAL);

	mutex_enter(&devstate_p->vpd_lock);

	/* Trigger read */
	int rc = pci_cap_put(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_16,
			PCI_CAP_ID_VPD, devstate_p->vpd_base, PCI_CAP_VPD_ADDRESS_OFFSET,
			PCI_CAP_VPD_ADDRESS(PCI_CAP_VPD_ADDRESS_FLAG_READ, vpd_address));
	if (rc != DDI_SUCCESS) {
		dev_err(devstate_p->dip, CE_WARN, "write to VPD address register failed: %d", rc);
		retval = DDI_FAILURE;
		goto done;
	}

	/* Poll until read is completed */
	for(int ii = 0; ; ++ii) {
		uint32_t vpd_reg_addr = pci_cap_get(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_16,
			PCI_CAP_ID_VPD, devstate_p->vpd_base, PCI_CAP_VPD_ADDRESS_OFFSET);

		if (vpd_reg_addr == PCI_CAP_EINVAL16) {
			dev_err(devstate_p->dip, CE_WARN, "error reading VPD address register");
			retval = DDI_FAILURE;
			goto done;
		} else if (PCI_CAP_VPD_ADDRESS_FLAG(vpd_reg_addr) != PCI_CAP_VPD_ADDRESS_FLAG_READ) {
			break;
		} else if (ii == PCI_CAP_VPD_POLL_ITERATIONS) {
			dev_err(devstate_p->dip, CE_WARN, "VPD read timeout");
			retval = DDI_FAILURE;
			goto done;
		} else {
			drv_usecwait(PCI_CAP_VPD_POLL_INTERVAL_USEC);
		}
	}

	*data = pci_cap_get(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_32,
		PCI_CAP_ID_VPD, devstate_p->vpd_base, PCI_CAP_VPD_DATA_OFFSET);	

	DTRACE_PROBE3(t6mfg__vpd__read, t6mfg_devstate_t *, devstate_p,
			uint16_t, vpd_address, uint32_t, *data);

done:
	mutex_exit(&devstate_p->vpd_lock);

	return retval;
}

static int t6mfg_vpd_write(t6mfg_devstate_t *devstate_p, uint16_t vpd_address, uint32_t data) {
	int retval = DDI_SUCCESS;

	/* Per PCI Local Bus 3.0 spec, VPD address must be DWORD aligned */
	if (vpd_address & 0x0003)
		return (DDI_EINVAL);

	mutex_enter(&devstate_p->vpd_lock);


	DTRACE_PROBE3(t6mfg__vpd__read, t6mfg_devstate_t *, devstate_p,
			uint16_t, vpd_address, uint32_t, data);

	/* Stage dword to be written */
	int rc = pci_cap_put(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_32,
			PCI_CAP_ID_VPD, devstate_p->vpd_base, PCI_CAP_VPD_DATA_OFFSET,
			data);
	if (rc != DDI_SUCCESS) {
		dev_err(devstate_p->dip, CE_WARN, "write to VPD data register failed: %d", rc);
		retval = DDI_FAILURE;
		goto done;
	}

	/* Trigger write */
	rc = pci_cap_put(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_16,
				PCI_CAP_ID_VPD, devstate_p->vpd_base, PCI_CAP_VPD_ADDRESS_OFFSET,
				PCI_CAP_VPD_ADDRESS(PCI_CAP_VPD_ADDRESS_FLAG_WRITE, vpd_address));
	if (rc != DDI_SUCCESS) {
		dev_err(devstate_p->dip, CE_WARN, "write to VPD address register failed: %d", rc);
		retval = DDI_FAILURE;
		goto done;
	}

	/* Poll until write is complete */
	for (int ii = 0; ; ++ii) {
		uint32_t vpd_reg_addr = pci_cap_get(devstate_p->pci_config_handle, PCI_CAP_CFGSZ_16,
				PCI_CAP_ID_VPD, devstate_p->vpd_base, PCI_CAP_VPD_ADDRESS_OFFSET);

		if (vpd_reg_addr == PCI_CAP_EINVAL16) {
			dev_err(devstate_p->dip, CE_WARN, "error reading VPD address register");
			retval = DDI_FAILURE;
			goto done;
		} else if (PCI_CAP_VPD_ADDRESS_FLAG(vpd_reg_addr) != PCI_CAP_VPD_ADDRESS_FLAG_WRITE) {
			break;
		} else if (ii == PCI_CAP_VPD_POLL_ITERATIONS) {
			dev_err(devstate_p->dip, CE_WARN, "VPD write timeout");
			retval = DDI_FAILURE;
			goto done;
		} else {
			drv_usecwait(PCI_CAP_VPD_POLL_INTERVAL_USEC);
		}
	}

done:
	mutex_exit(&devstate_p->vpd_lock);

	return retval;
}

static int t6mfg_srom_open(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	int retval = 0;

	/*
	 * SROM access is via VPD capability.  Locate it now both to tell the
	 * user early if there is a problem and to speed up read/write
	 * accesses.
	 */

	mutex_enter(&devstate_p->vpd_lock);

	if (devstate_p->vpd_base == 0) {
		int rc = PCI_CAP_LOCATE(devstate_p->pci_config_handle, PCI_CAP_ID_VPD,
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

static int t6mfg_srom_close(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	return (0);
}

static int t6mfg_srom_read(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	while (uio_p->uio_offset <= T6MFG_SROM_MAX_ADDRESS && uio_p->uio_resid > 0) {
		uint16_t vpd_address = uio_p->uio_offset - T6MFG_VPD_TO_SROM_OFFSET;

		/* Per PCI 3.0 spec, VPD accesses must be DWORD aligned */
		uint16_t vpd_dword_address = vpd_address & 0xfffc;
		uint16_t vpd_dword_byte_offset = vpd_address & 0x0003;

		uint32_t vpd_dword_data;
		int rc = t6mfg_vpd_read(devstate_p, vpd_dword_address, &vpd_dword_data);
		if (rc != DDI_SUCCESS) {
			dev_err(devstate_p->dip, CE_WARN, "SROM read via VPD failed: %d", rc);
			return EIO;
		}

		rc = uiomove((void*)&vpd_dword_data + vpd_dword_byte_offset,
				MIN(uio_p->uio_resid, sizeof(uint32_t) - vpd_dword_byte_offset),
				UIO_READ, uio_p);
		if (rc != DDI_SUCCESS) {
			dev_err(devstate_p->dip, CE_WARN, "error copying SROM data to uio buffer: %d", rc);
			return EIO;
		}
	}

	return 0;
}

static int t6mfg_srom_write(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	while (uio_p->uio_offset <= T6MFG_SROM_MAX_ADDRESS && uio_p->uio_resid > 0) {
		uint16_t vpd_address = uio_p->uio_offset - T6MFG_VPD_TO_SROM_OFFSET;

		/* Per PCI 3.0 spec, VPD accesses must be DWORD aligned */
		uint16_t vpd_dword_address = vpd_address & 0xfffc;
		uint16_t vpd_dword_byte_offset = vpd_address & 0x0003;

		uint32_t vpd_dword_data;

		/*
		 * If destination is not dword aligned, read the existing full dword
		 * to turn this into a read-modify-write.
		 */
		if (vpd_dword_byte_offset != 0 || uio_p->uio_resid < sizeof(uint32_t)) {
			int rc = t6mfg_vpd_read(devstate_p, vpd_dword_address, &vpd_dword_data);
			if (rc != DDI_SUCCESS) {
				dev_err(devstate_p->dip, CE_WARN, "SROM read via VPD failed: %d", rc);
				return EIO;
			}
		}

		int rc = uiomove((void*)&vpd_dword_data + vpd_dword_byte_offset,
				MIN(uio_p->uio_resid, sizeof(uint32_t) - vpd_dword_byte_offset),
				UIO_WRITE, uio_p);
		if (rc != DDI_SUCCESS) {
			return EIO;
		}

		rc = t6mfg_vpd_write(devstate_p, vpd_dword_address, vpd_dword_data);
		if (rc != DDI_SUCCESS) {
			dev_err(devstate_p->dip, CE_WARN, "SROM write via VPD failed: %d", rc);
			return EIO;
		}

		/*
		 * VPD write only initiates the write to the SPI EEPROM.  Need to wait
		 * for the write to complete which can be determined by polling the SROM
		 * Status Register.
		 */
		for (int ii = 0; ; ++ii) {
			uint32_t srom_status_reg;
			rc = t6mfg_vpd_read(devstate_p, T6MFG_SROM_STATUS_REGISTER_ADDRESS - T6MFG_VPD_TO_SROM_OFFSET, &srom_status_reg);
			if (rc != DDI_SUCCESS) {
				dev_err(devstate_p->dip, CE_WARN, "failed to read SROM status register: %d", rc);
				return EIO;
			}

			if (T6MFG_SROM_STATUS_REGISTER_RDY_L(srom_status_reg) == 0) {
				break;
			} else if (ii == T6MFG_SROM_WRITE_POLL_ITERATIONS) {
				dev_err(devstate_p->dip, CE_WARN, "SROM write timeout");
				return EIO;
			} else {
				drv_usecwait(T6MFG_SROM_WRITE_POLL_INTERVAL_USEC);
			}
		}
	}

	return 0;
}

static int t6mfg_srom_ioctl(t6mfg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p) {
	return (ENOTTY);
}

static int t6mfg_spidev_open(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	return (0);
}

static int t6mfg_spidev_close(t6mfg_devstate_t *devstate_p, int flag, int otype, cred_t *cred_p) {
	return (0);
}

static int t6mfg_spidev_read(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	return (ENOTSUP);
}

static int t6mfg_spidev_write(t6mfg_devstate_t *devstate_p, struct uio *uio_p, cred_t *cred_p) {
	return (ENOTSUP);
}

static uint32_t
t6mfg_reg_read(t6mfg_devstate_t *devstate_p, uint32_t reg)
{
	uint32_t val;
	uintptr_t addr = (uintptr_t)devstate_p->pio_kernel_regs + reg;
	val = ddi_get32(devstate_p->pio_kernel_regs_handle, (void *)addr);
	DTRACE_PROBE3(t6mfg__reg__read, t6mfg_devstate_t *, devstate_p,
	    uint32_t, reg, uint32_t, val);
	return (val);
}

static void
t6mfg_reg_write(t6mfg_devstate_t *devstate_p, uint32_t reg, uint32_t val)
{
	uintptr_t addr = (uintptr_t)devstate_p->pio_kernel_regs + reg;
	DTRACE_PROBE3(t6mfg__reg__write, t6mfg_devstate_t *, devstate_p,
	    uint32_t, reg, uint32_t, val);
	ddi_put32(devstate_p->pio_kernel_regs_handle, (void *)addr, val);
}

static int t6mfg_spidev_ioctl(t6mfg_devstate_t *devstate_p, int cmd, intptr_t arg, int mode, cred_t *cred_p, int *rval_p) {
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

	for (int ii = 0; ii < 10; ++ii) {
		if (!SF_OP_BUSY(t6mfg_reg_read(devstate_p, SF_OP_ADDR)))
			break;

		drv_usecwait(1);
	}

	if (SF_OP_BUSY(t6mfg_reg_read(devstate_p, SF_OP_ADDR))) {
		mutex_exit(&devstate_p->sf_lock);
		return (EBUSY);
	}

	int nxfers = STRUCT_FGET(xact, spidev_nxfers);
	for (int xfer_idx = 0; xfer_idx < nxfers; xfer_idx++) {
		void *xfer_up = STRUCT_FGETP(xact, spidev_xfers) + xfer_idx * STRUCT_SIZE(xfer);

		if (copyin(xfer_up, STRUCT_BUF(xfer), STRUCT_SIZE(xfer))) {
			rc = EFAULT;
			goto done;
		}

		// CS# is implicitly asserted at the start of each transfer.
		// CS# is implicilty deasserted at the end of the last transfer
		// in a transaction.
		// CS# may be explicitly deasserted at the end of any transfer
		// by setting deassert_cs_after to 1.
		int deassert_cs_after_xfer = ((xfer_idx + 1) == nxfers) || STRUCT_FGET(xfer, deassert_cs);

		for (int cur_byte = 0; cur_byte < STRUCT_FGET(xfer, len); cur_byte += 4) {
			int bytes_to_transfer = min(STRUCT_FGET(xfer, len) - cur_byte, 4);
			int sf_op_op = SF_OP_OP_READ;

			/* Stage transmit word into transmit register */
			if (STRUCT_FGETP(xfer, tx_buf) != NULL) {
				sf_op_op = SF_OP_OP_WRITE;

				uint32_t tx_data = 0;
				const void *tx_buf_up = STRUCT_FGETP(xfer, tx_buf) + cur_byte;
				if (copyin(tx_buf_up, &tx_data, bytes_to_transfer)) {
					rc = EFAULT;
					goto done;
				}

				t6mfg_reg_write(devstate_p, SF_DATA_ADDR,
				    tx_data);
			}

			/* Trigger transfer. If this is the last chunk of a transfer and CS is to be deasserted, do so. */
			int deassert_cs = ((cur_byte + bytes_to_transfer) == STRUCT_FGET(xfer, len)) ? deassert_cs_after_xfer : 0;
			t6mfg_reg_write(devstate_p, SF_OP_ADDR, SF_OP(sf_op_op, bytes_to_transfer, !deassert_cs, 1));

			/* Poll until controller has finished the operation */
			for (int ii = 0; ii < 10; ++ii) {
				if (!SF_OP_BUSY(t6mfg_reg_read(devstate_p, SF_OP_ADDR)))
					break;

				drv_usecwait(1);
			}

			if (SF_OP_BUSY(t6mfg_reg_read(devstate_p, SF_OP_ADDR))) {
				rc = EIO;
				goto done;
			}

			/* Retrieve received word */
			if (STRUCT_FGETP(xfer, rx_buf) != NULL) {
				uint32_t rx_data = t6mfg_reg_read(devstate_p, SF_DATA_ADDR);

				void *rx_buf_up = STRUCT_FGETP(xfer, rx_buf) + cur_byte;
				if (copyout(&rx_data, rx_buf_up, bytes_to_transfer)) {
					rc = EFAULT;
					goto done;
				}
			}
		}

		/* User-requested delay between transfers */
		drv_usecwait(STRUCT_FGET(xfer, delay_usec));
	}

done:
	/* Unlock SF */
	t6mfg_reg_write(devstate_p, SF_OP_ADDR, SF_OP(SF_OP_OP_READ, 0, 0, 0));

	mutex_exit(&devstate_p->sf_lock);
	return (rc);
}
