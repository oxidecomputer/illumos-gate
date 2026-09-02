/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * Copyright 2026 Oxide Computer Company
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/modctl.h>
#include <sys/sunddi.h>

#include "rmge.h"

/*
 * Setup internal mappings to the device's config and status registers, in
 * both the config space and second BAR window.
 */
static int
rmge_internalize_csrs(rmge_t *rmge)
{
	ddi_device_acc_attr_t acc_attr = {
		DDI_DEVICE_ATTR_V1,
		DDI_STRUCTURE_LE_ACC,
		DDI_STRICTORDER_ACC,
		DDI_DEFAULT_ACC
	};

	if (pci_config_setup(rmge->devinfo, &rmge->cfg_space_handle)
	    != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_regs_map_setup(rmge->devinfo, RMGE_BAR2, &rmge->bar2_mmio_addr,
	    0, 0, &acc_attr, &rmge->bar2_mmio_handle) != DDI_SUCCESS) {
		goto fail;
	}

	rmge->att_milestone |= RMGE_ATT_MILESTONE_CSRS;

	return (RMGE_SUCCESS);

fail:
	return (RMGE_FAILURE);
}

static uint32_t
rmge_read_bar2_32(rmge_t *rmge, uint64_t reg)
{
	uint32_t *off = (uint32_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get32(rmge->bar2_mmio_handle, off));
}

static int
rmge_identify_hw_rev(rmge_t *rmge)
{
	uint32_t txcfg;

	txcfg = rmge_read_bar2_32(rmge, RMGE_REG_TXCFG);
	rmge->hw_rev = txcfg & RMGE_REG_TXCFG_MASK_HW_REV;

	rmge->att_milestone |= RMGE_ATT_MILESTONE_ID_HW_REV;
	return (RMGE_SUCCESS);
}

static int
rmge_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	rmge_t *rmge;

	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_RESUME:
		return (DDI_FAILURE);

	case DDI_ATTACH:
		break;
	}

	rmge = kmem_zalloc(sizeof (*rmge), KM_SLEEP);
	ddi_set_driver_private(devinfo, rmge);

	rmge->devinfo = devinfo;
	rmge->instance = ddi_get_instance(devinfo);

	rmge->att_milestone |= RMGE_ATT_MILESTONE_START;

	if (rmge_internalize_csrs(rmge) != RMGE_SUCCESS) {
		cmn_err(CE_WARN, "failed to internalize csrs for rmge");
		goto rollback;
	}

	if (rmge_identify_hw_rev(rmge) != RMGE_SUCCESS) {
		cmn_err(CE_WARN, "failed to identify hw rev");
		goto rollback;
	}

	return (DDI_SUCCESS);
rollback:
	kmem_free(rmge, sizeof (*rmge));
	ddi_set_driver_private(devinfo, NULL);
	return (DDI_FAILURE);
}

static int
rmge_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	rmge_t *rmge;


	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		return (DDI_FAILURE);

	case DDI_DETACH:
		break;
	}

	rmge = ddi_get_driver_private(devinfo);

	if (rmge == NULL)
		return (DDI_FAILURE);

	if (rmge->cfg_space_handle != NULL)
		pci_config_teardown(&rmge->cfg_space_handle);

	if (rmge->bar2_mmio_handle != NULL)
		ddi_regs_map_free(&rmge->bar2_mmio_handle);

	kmem_free(rmge, sizeof (*rmge));

	ddi_set_driver_private(devinfo, NULL);

	return (DDI_SUCCESS);
}

static struct dev_ops rmge_dev_ops = {
	DEVO_REV,
	0,
	ddi_no_info,
	nulldev,
	nulldev,
	rmge_attach,
	rmge_detach,
	nodev,
	NULL,
	NULL,
	nodev,
	ddi_quiesce_not_supported
};

static struct modldrv rmge_modldrv = {
	&mod_driverops,
	"Realtek Multi-Gigabit Ethernet",
	&rmge_dev_ops
};

static struct modlinkage rmge_modlinkage = {
	MODREV_1,
	{ &rmge_modldrv, NULL }
};

int
_init(void)
{
	return (mod_install(&rmge_modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&rmge_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&rmge_modlinkage, modinfop));
}
