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

#include <sys/ddi.h>
#include <sys/sunndi.h>
#include <sys/tofino.h>
#include "tofino_impl.h"

static int
validate_child_name(char *name)
{
	char *dupnam, *devname, *addrname;
	size_t duplen;
	int ret = NDI_SUCCESS;

	duplen = strlen(name) + 1;
	dupnam = kmem_alloc(duplen, KM_SLEEP);
	bcopy(name, dupnam, duplen);

	i_ddi_parse_name(dupnam, &devname, &addrname, NULL);
	if (strcmp(devname, "tfpkt") != 0 || strcmp(addrname, "tfpkt") != 0) {
		ret = NDI_EINVAL;
	}
	kmem_free(dupnam, duplen);
	return (ret);
}

static int
tofino_bus_config(dev_info_t *dip, uint_t flags, ddi_bus_config_op_t op,
    void *arg, dev_info_t **childp)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	dev_info_t *tfpkt_dip;

	VERIFY3P(tf, !=, NULL);

	switch (op) {
	case BUS_CONFIG_ONE: {
		int ret;
		if ((ret = validate_child_name((char *)arg)) != NDI_SUCCESS) {
			tofino_err(tf, "!BUS_CONFIG_ONE invoked with invalid "
			    "child devname: %s", (char *)arg);
			return (ret);
		}
	}
	/* FALLTHROUGH */
	case BUS_CONFIG_ALL:
	case BUS_CONFIG_DRIVER:
		ndi_devi_enter(dip);
		break;
	default:
		return (NDI_FAILURE);
	}

	/*
	 * A tofino device can only have one child.  If we have already
	 * configured that child, then we're done.
	 */
	if (tf->tf_tfpkt != NULL) {
		goto done;
	}

	ndi_devi_alloc_sleep(tf->tf_dip, "tfpkt", DEVI_SID_NODEID,
	    &tfpkt_dip);

	tf->tf_tfpkt = tfpkt_dip;
	ddi_set_parent_data(tfpkt_dip, tf);
	(void) ndi_devi_bind_driver(tfpkt_dip, 0);
	flags |= NDI_ONLINE_ATTACH;

done:
	ndi_devi_exit(dip);
	return (ndi_busop_bus_config(dip, flags, op, arg, childp, 0));
}

static int
tofino_unconfig_tfpkt(tofino_t *tf)
{
	int ret;

	if ((ret = ndi_devi_free(tf->tf_tfpkt)) != NDI_SUCCESS) {
		tofino_err(tf, "!failed to free dip in unconfig");
	} else {
		tf->tf_tfpkt = NULL;
	}

	return (ret);
}

static int
tofino_bus_unconfig(dev_info_t *dip, uint_t flags, ddi_bus_config_op_t op,
    void *arg)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	int ret = NDI_SUCCESS;

	VERIFY3P(tf, !=, NULL);

	switch (op) {
	case BUS_UNCONFIG_ONE:
	case BUS_UNCONFIG_DRIVER:
	case BUS_UNCONFIG_ALL:
		flags |= NDI_UNCONFIG;
		break;
	default:
		return (NDI_FAILURE);
	}

	ret = ndi_busop_bus_unconfig(dip, flags, op, arg);
	if (ret != NDI_SUCCESS) {
		return (ret);
	}

	if (tf->tf_tfpkt == NULL) {
		return (NDI_SUCCESS);
	}
	VERIFY3P(tf, ==, ddi_get_parent_data(tf->tf_tfpkt));

	switch (op) {
	case BUS_UNCONFIG_ONE: {
		if (arg == NULL) {
			tofino_err(tf, "!BUS_UNCONFIG_ONE invoked with NULL "
			    "child devname");
			ret = NDI_EINVAL;
		} else if ((ret = validate_child_name((char *)arg)) !=
		    NDI_SUCCESS) {
			tofino_err(tf, "!BUS_UNCONFIG_ONE invoked with invalid "
			    "child devname: %s", arg);
		} else {
			ret = tofino_unconfig_tfpkt(tf);
		}
		break;
	}
	case BUS_UNCONFIG_DRIVER: {
		major_t major = (major_t)(uintptr_t)arg;
		if (major == ddi_driver_major(tf->tf_tfpkt)) {
			ret = tofino_unconfig_tfpkt(tf);
		}
		break;
	}
	case BUS_UNCONFIG_ALL: {
		ret = tofino_unconfig_tfpkt(tf);
		break;
	}
	default:
		ret = NDI_FAILURE;
	}

	return (ret);
}

static int
tofino_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    void *arg, void *result)
{
	dev_info_t *cdip;

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == NULL)
			return (DDI_FAILURE);
		cmn_err(CE_CONT, "Tofino: %s@%s, %s%d",
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    ddi_driver_name(rdip), ddi_get_instance(rdip));
		break;
	case DDI_CTLOPS_INITCHILD:
		if (arg == NULL)
			return (DDI_FAILURE);

		cdip = arg;
		ddi_set_name_addr(cdip, "tfpkt");
		break;
	case DDI_CTLOPS_UNINITCHILD:
		if (arg == NULL)
			return (DDI_FAILURE);

		cdip = arg;
		ddi_set_name_addr(cdip, NULL);
		break;
	default:
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}

	return (DDI_SUCCESS);
}

struct bus_ops tofino_bus_ops = {
	.busops_rev = BUSO_REV,
	.bus_map = nullbusmap,
	.bus_dma_allochdl = ddi_dma_allochdl,
	.bus_dma_freehdl = ddi_dma_freehdl,
	.bus_dma_bindhdl = ddi_dma_bindhdl,
	.bus_dma_unbindhdl = ddi_dma_unbindhdl,
	.bus_dma_flush = ddi_dma_flush,
	.bus_dma_win = ddi_dma_win,
	.bus_dma_ctl = ddi_dma_mctl,
	.bus_prop_op = ddi_bus_prop_op,
	.bus_ctl = tofino_bus_ctl,
	.bus_config = tofino_bus_config,
	.bus_unconfig = tofino_bus_unconfig
};
