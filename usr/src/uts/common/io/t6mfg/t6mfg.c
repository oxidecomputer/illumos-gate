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
#include <sys/open.h>
#include <sys/pci.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/types.h>
#include <sys/uio.h>

#ifndef PCI_VENDOR_ID_CHELSIO
# define PCI_VENDOR_ID_CHELSIO 0x1425
#endif

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

typedef struct t6mfg_devstate {
	dev_info_t *		dip;
	dev_t			dev;

	ddi_acc_handle_t	pci_config_handle;

	ddi_acc_handle_t	pio_kernel_regs_handle;
	void			*pio_kernel_regs;
} t6mfg_devstate_t;

static int t6mfg_devo_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result_p);
static int t6mfg_devo_probe(dev_info_t *dip);
static int t6mfg_devo_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int t6mfg_devo_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

struct cb_ops t6mfg_cb_ops = {
	.cb_open =		nodev,
	.cb_close =		nodev,
	.cb_strategy =		nodev,
	.cb_print =		nodev,
	.cb_dump =		nodev,
	.cb_read =		nodev,
	.cb_write =		nodev,
	.cb_ioctl =		nodev,
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
