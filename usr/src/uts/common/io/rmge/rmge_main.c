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
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/sunddi.h>
#include <sys/mac_provider.h>

#include "rmge.h"

static void *rmge_soft_state;

static mac_callbacks_t rmge_mac_callbacks;
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

	if (pci_config_setup(rmge->dip, &rmge->cfg_space_handle)
	    != DDI_SUCCESS) {
		goto fail;
	}

	if (ddi_regs_map_setup(rmge->dip, RMGE_BAR2, &rmge->bar2_mmio_addr,
	    0, 0, &acc_attr, &rmge->bar2_mmio_handle) != DDI_SUCCESS) {
		goto fail;
	}

	rmge->att_milestone |= RMGE_ATT_MILESTONE_CSRS;

	return (RMGE_SUCCESS);

fail:
	return (RMGE_FAILURE);
}

static void
rmge_generic_optimisic_cleanup(rmge_t *rmge, dev_info_t *devinfo)
{
	if (rmge->cfg_space_handle != NULL)
		pci_config_teardown(&rmge->cfg_space_handle);

	if (rmge->bar2_mmio_handle != NULL)
		ddi_regs_map_free(&rmge->bar2_mmio_handle);

	if (rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC)
		mac_unregister(rmge->mh);

	ddi_remove_minor_node(devinfo, NULL);
	ddi_set_driver_private(devinfo, NULL);
	ddi_soft_state_free(rmge_soft_state, rmge->instance);
}

static uint32_t
rmge_read_bar2_32(rmge_t *rmge, uint32_t reg)
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
rmge_register_mac_device(rmge_t *rmge)
{
	int rc;

	mac_register_t *mr = mac_alloc(MAC_VERSION);

	mr->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mr->m_driver = rmge;
	mr->m_dip = rmge->dip;
	mr->m_instance = 0;
	mr->m_src_addr = rmge->hw_mac_addr;
	mr->m_callbacks = &rmge_mac_callbacks;

	rc = mac_register(mr, &rmge->mh);
	mac_free(mr);

	if (rc != 0) {
		return (RMGE_FAILURE);
	}

	rmge->att_milestone |= RMGE_ATT_MILESTONE_REG_MAC;
	return (RMGE_SUCCESS);
}

static int
rmge_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	rmge_t *rmge;
	int instance;
	int rc = DDI_SUCCESS;

	switch (cmd) {
	default:
		return (DDI_FAILURE);

	case DDI_RESUME:
		return (DDI_FAILURE);

	case DDI_ATTACH:
		break;
	}

	instance = ddi_get_instance(devinfo);
	rc = ddi_soft_state_zalloc(rmge_soft_state, instance);

	if (rc != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	rmge = ddi_get_soft_state(rmge_soft_state, instance);
	ddi_set_driver_private(devinfo, rmge);

	rmge->att_milestone |= RMGE_ATT_MILESTONE_SOFTSTATE;
	rmge->dip = devinfo;
	rmge->instance = instance;
	rmge->dev = makedevice(ddi_driver_major(devinfo), instance);

	if (rmge_internalize_csrs(rmge) != RMGE_SUCCESS) {
		cmn_err(CE_WARN, "failed to internalize csrs for rmge");
		goto rollback;
	}

	if (rmge_identify_hw_rev(rmge) != RMGE_SUCCESS) {
		cmn_err(CE_WARN, "failed to identify hw rev");
		goto rollback;
	}

	if (rmge_register_mac_device(rmge) != RMGE_SUCCESS) {
		cmn_err(CE_WARN, "failed to register mac device");
		goto rollback;
	}

	return (DDI_SUCCESS);
rollback:
	cmn_err(CE_WARN, "rolling back rmge attach at milestone %d",
	    rmge->att_milestone);
	rmge_generic_optimisic_cleanup(rmge, devinfo);
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

	rmge_generic_optimisic_cleanup(rmge, devinfo);

	return (DDI_SUCCESS);
}

static int
rmge_mc_getstat(void *arg, uint_t stat, uint64_t *val)
{
	(void) arg;
	(void) stat;
	(void) val;

	return (ENOTSUP);
}

static int
rmge_mc_start(void *arg)
{
	(void) arg;

	return (ENOTSUP);
}

static void
rmge_mc_stop(void *arg)
{
	(void) arg;
}

static int
rmge_mc_setpromisc(void *arg, boolean_t enable)
{
	(void) arg;
	(void) enable;

	return (ENOTSUP);
}

static int
rmge_mc_multicst(void *arg, boolean_t add, const uint8_t *addr)
{
	(void) arg;
	(void) add;
	(void) addr;

	return (ENOTSUP);
}

static int
rmge_mc_unicst(void *arg, const uint8_t *addr)
{
	(void) arg;
	(void) addr;

	return (ENOTSUP);
}

static mblk_t *
rmge_mc_tx(void *arg, mblk_t *mp)
{
	(void) arg;

	return (mp);
}

static mac_callbacks_t rmge_mac_callbacks = {
	.mc_getstat = rmge_mc_getstat,
	.mc_start = rmge_mc_start,
	.mc_stop = rmge_mc_stop,
	.mc_setpromisc = rmge_mc_setpromisc,
	.mc_multicst = rmge_mc_multicst,
	.mc_unicst = rmge_mc_unicst,
	.mc_tx = rmge_mc_tx,
};

static struct cb_ops rmge_cb_ops = {
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

static struct dev_ops rmge_dev_ops = {
	.devo_rev =		DEVO_REV,
	.devo_getinfo =		ddi_no_info,
	.devo_identify =	nulldev,
	.devo_probe =		nulldev,
	.devo_attach =		rmge_attach,
	.devo_detach =		rmge_detach,
	.devo_reset =		nodev,
	.devo_cb_ops =		&rmge_cb_ops,
	.devo_quiesce =		ddi_quiesce_not_supported,
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
	int rc;

	rc = ddi_soft_state_init(&rmge_soft_state, sizeof (rmge_t), 0);

	if (rc != DDI_SUCCESS) {
		return (rc);
	}

	mac_init_ops(&rmge_dev_ops, "rmge");

	rc = mod_install(&rmge_modlinkage);

	if (rc != DDI_SUCCESS) {
		mac_fini_ops(&rmge_dev_ops);
	}

	return (rc);
}

int
_fini(void)
{
	int rc;

	ddi_soft_state_fini(&rmge_soft_state);
	rc = mod_remove(&rmge_modlinkage);
	if (rc == DDI_SUCCESS) {
		mac_fini_ops(&rmge_dev_ops);
	}

	return (rc);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&rmge_modlinkage, modinfop));
}
