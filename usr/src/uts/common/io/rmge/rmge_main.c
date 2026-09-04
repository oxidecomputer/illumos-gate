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
#include <sys/vlan.h>

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

	ASSERT3P(rmge, !=, NULL);
	ASSERT3P(rmge->dip, !=, NULL);
	ASSERT3P(rmge->cfg_space_handle, ==, NULL);
	ASSERT3P(rmge->bar2_mmio_addr, ==, NULL);
	ASSERT3P(rmge->bar2_mmio_handle, ==, NULL);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);

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

/*
 * Release the resources owned exclusively by the driver.  MAC must already
 * have been unregistered before this is called: until then it may still call
 * into the driver with rmge as its private argument.
 */
static void
rmge_free_resources(rmge_t *rmge)
{
	ASSERT3P(rmge, !=, NULL);
	ASSERT3P(rmge->dip, !=, NULL);
	ASSERT3P(ddi_get_driver_private(rmge->dip), ==, rmge);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_SOFTSTATE);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT3P(rmge->mh, ==, NULL);
	ASSERT0(rmge->started);

	if (rmge->bar2_mmio_handle != NULL)
		ddi_regs_map_free(&rmge->bar2_mmio_handle);

	if (rmge->cfg_space_handle != NULL)
		pci_config_teardown(&rmge->cfg_space_handle);

	ddi_set_driver_private(rmge->dip, NULL);
	ddi_soft_state_free(rmge_soft_state, rmge->instance);
}

static uint32_t
rmge_read_bar2_32(rmge_t *rmge, uint32_t reg)
{
	uint32_t *off;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT3P(rmge->bar2_mmio_addr, !=, NULL);
	ASSERT3P(rmge->bar2_mmio_handle, !=, NULL);
	ASSERT0(reg & (sizeof (uint32_t) - 1));

	off = (uint32_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get32(rmge->bar2_mmio_handle, off));
}

static uint8_t
rmge_read_bar2_8(rmge_t *rmge, uint32_t reg)
{
	uint8_t *off;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT3P(rmge->bar2_mmio_addr, !=, NULL);
	ASSERT3P(rmge->bar2_mmio_handle, !=, NULL);

	off = (uint8_t *)(rmge->bar2_mmio_addr + reg);
	return (ddi_get8(rmge->bar2_mmio_handle, off));
}

static int
rmge_identify_hw_rev(rmge_t *rmge)
{
	uint32_t txcfg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_ID_HW_REV);

	txcfg = rmge_read_bar2_32(rmge, RMGE_REG_TXCFG);
	rmge->hw_rev = txcfg & RMGE_REG_TXCFG_MASK_HW_REV;

	rmge->att_milestone |= RMGE_ATT_MILESTONE_ID_HW_REV;
	return (RMGE_SUCCESS);
}

static void
rmge_read_mac_addr(rmge_t *rmge)
{
	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_ID_MAC);

	for (uint_t i = 0; i < ETHERADDRL; i++)
		rmge->hw_mac_addr[i]
		    = rmge_read_bar2_8(rmge, RMGE_REG_IDR0 + i);

	rmge->att_milestone |= RMGE_ATT_MILESTONE_ID_MAC;
}

static int
rmge_register_mac_device(rmge_t *rmge)
{
	mac_register_t *mr;
	int rc;

	ASSERT3P(rmge, !=, NULL);
	ASSERT3P(rmge->dip, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_SOFTSTATE);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_CSRS);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_ID_HW_REV);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_ID_MAC);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT3P(rmge->mh, ==, NULL);
	ASSERT0(rmge->started);

	mr = mac_alloc(MAC_VERSION);
	if (mr == NULL)
		return (RMGE_FAILURE);

	mr->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	mr->m_driver = rmge;
	mr->m_dip = rmge->dip;
	mr->m_instance = 0;
	mr->m_src_addr = rmge->hw_mac_addr;
	mr->m_callbacks = &rmge_mac_callbacks;
	mr->m_min_sdu = 0;
	mr->m_max_sdu = ETHERMTU;
	mr->m_margin = VLAN_TAGSZ;

	rc = mac_register(mr, &rmge->mh);
	mac_free(mr);

	if (rc != 0) {
		return (RMGE_FAILURE);
	}

	rmge->att_milestone |= RMGE_ATT_MILESTONE_REG_MAC;
	ASSERT3P(rmge->mh, !=, NULL);
	mac_link_update(rmge->mh, LINK_STATE_DOWN);
	return (RMGE_SUCCESS);
}

static int
rmge_attach(dev_info_t *devinfo, ddi_attach_cmd_t cmd)
{
	rmge_t *rmge;
	int instance;
	int rc = DDI_SUCCESS;

	ASSERT3P(devinfo, !=, NULL);
	ASSERT3P(rmge_soft_state, !=, NULL);

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
	ASSERT3P(rmge, !=, NULL);
	ASSERT3P(ddi_get_driver_private(devinfo), ==, NULL);
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

	rmge_read_mac_addr(rmge);

	if (rmge_register_mac_device(rmge) != RMGE_SUCCESS) {
		cmn_err(CE_WARN, "failed to register mac device");
		goto rollback;
	}

	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT3P(rmge->mh, !=, NULL);
	return (DDI_SUCCESS);
rollback:
	cmn_err(CE_WARN, "rolling back rmge attach at milestone %d",
	    rmge->att_milestone);
	ASSERT0(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	rmge_free_resources(rmge);
	return (DDI_FAILURE);
}

static int
rmge_detach(dev_info_t *devinfo, ddi_detach_cmd_t cmd)
{
	rmge_t *rmge;
	int rc;

	ASSERT3P(devinfo, !=, NULL);

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
	ASSERT3P(rmge->dip, ==, devinfo);
	ASSERT3S(rmge->instance, ==, ddi_get_instance(devinfo));
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_SOFTSTATE);

	if (rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC) {
		ASSERT3P(rmge->mh, !=, NULL);
		rc = mac_unregister(rmge->mh);
		if (rc != 0) {
			dev_err(devinfo, CE_WARN,
			    "failed to unregister MAC: %d", rc);
			return (DDI_FAILURE);
		}

		rmge->mh = NULL;
		rmge->att_milestone &= ~RMGE_ATT_MILESTONE_REG_MAC;
	}

	rmge_free_resources(rmge);

	return (DDI_SUCCESS);
}

static int
rmge_mc_getstat(void *arg, uint_t stat, uint64_t *val)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT3P(val, !=, NULL);
	(void) stat;

	return (ENOTSUP);
}

static int
rmge_mc_start(void *arg)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT0(rmge->started);

	rmge->started = B_TRUE;

	return (0);
}

static void
rmge_mc_stop(void *arg)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT(rmge->started);

	rmge->started = B_FALSE;
}

static int
rmge_mc_setpromisc(void *arg, boolean_t enable)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT(enable == B_FALSE || enable == B_TRUE);
	(void) enable;

	return (ENOTSUP);
}

static int
rmge_mc_multicst(void *arg, boolean_t add, const uint8_t *addr)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT(add == B_FALSE || add == B_TRUE);
	ASSERT3P(addr, !=, NULL);
	(void) add;

	return (ENOTSUP);
}

static int
rmge_mc_unicst(void *arg, const uint8_t *addr)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT3P(addr, !=, NULL);

	return (ENOTSUP);
}

static mblk_t *
rmge_mc_tx(void *arg, mblk_t *mp)
{
	rmge_t *rmge = arg;

	ASSERT3P(rmge, !=, NULL);
	ASSERT(rmge->att_milestone & RMGE_ATT_MILESTONE_REG_MAC);
	ASSERT(rmge->started);
	ASSERT3P(mp, !=, NULL);

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
	.devo_getinfo =		NULL,
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

static struct modlinkage rmge_modlinkage = { MODREV_1,
	{ &rmge_modldrv, NULL }
};

int
_init(void)
{
	int rc;

	ASSERT3P(rmge_soft_state, ==, NULL);
	rc = ddi_soft_state_init(&rmge_soft_state, sizeof (rmge_t), 0);

	if (rc != DDI_SUCCESS) {
		return (rc);
	}
	ASSERT3P(rmge_soft_state, !=, NULL);

	mac_init_ops(&rmge_dev_ops, "rmge");

	rc = mod_install(&rmge_modlinkage);

	if (rc != DDI_SUCCESS) {
		mac_fini_ops(&rmge_dev_ops);
		ddi_soft_state_fini(&rmge_soft_state);
	}

	return (rc);
}

int
_fini(void)
{
	int rc;

	ASSERT3P(rmge_soft_state, !=, NULL);
	rc = mod_remove(&rmge_modlinkage);
	if (rc == DDI_SUCCESS) {
		mac_fini_ops(&rmge_dev_ops);
		ddi_soft_state_fini(&rmge_soft_state);
	}

	return (rc);
}

int
_info(struct modinfo *modinfop)
{
	ASSERT3P(modinfop, !=, NULL);

	return (mod_info(&rmge_modlinkage, modinfop));
}
