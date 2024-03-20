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
 * Copyright 2021 Oxide Computer Company
 */

#include "serdev_impl.h"

static int
serdev_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    void *arg, void *result)
{
	serdev_handle_t *srdh;
	dev_info_t *child;
	char buf[32];

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == NULL) {
			return (DDI_FAILURE);
		}
		cmn_err(CE_CONT, "serial device: %s@%s, %s%d [%s@%s, %s%d]\n",
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    ddi_driver_name(rdip), ddi_get_instance(rdip),
		    ddi_node_name(dip), ddi_get_name_addr(dip),
		    ddi_driver_name(dip), ddi_get_instance(dip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		if ((child = arg) == NULL) {
			dev_err(dip, CE_WARN, "!no child passed for "
			    "DDI_CTLOPS_INITCHILD");
			return (DDI_FAILURE);
		}

		if ((srdh = ddi_get_parent_data(child)) == NULL) {
			dev_err(dip, CE_WARN, "!missing child parent data");
			return (DDI_FAILURE);
		}

		/*
		 * Our nodes will be created underneath the concrete device
		 * node.  Use the port number we were given (which will often
		 * be zero) to identify each serdev node under that parent:
		 */
		if (snprintf(buf, sizeof (buf), "%u", srdh->srdh_port) >=
		    sizeof (buf)) {
			dev_err(dip, CE_WARN, "!failed to construct device "
			    "address due to overflow");
			return (DDI_FAILURE);
		}

		ddi_set_name_addr(child, buf);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_UNINITCHILD:
		if ((child = arg) == NULL) {
			dev_err(dip, CE_WARN, "!no child passed for "
			    "DDI_CTLOPS_UNINITCHILD");
			return (DDI_FAILURE);
		}

		if ((srdh = ddi_get_parent_data(child)) != NULL) {
			/*
			 * Destruction of the child node may race with handle
			 * detach.  Clear out the handle's reference to this
			 * node before we are freed.
			 */
			VERIFY(DEVI_BUSY_OWNED(dip));
			srdh->srdh_child = NULL;
		}

		ddi_set_name_addr(child, NULL);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_ATTACH:
	case DDI_CTLOPS_DETACH:
		/*
		 * We do not want to pass the attach/detach requests up to our
		 * parent; the parent would not know what to do with our serdev
		 * children.
		 */
		return (DDI_SUCCESS);

	default:
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}
}

static struct bus_ops serdev_bus_ops = {
	.busops_rev =		BUSO_REV,
	.bus_ctl =		serdev_bus_ctl,
	.bus_prop_op =		ddi_bus_prop_op,

	/*
	 * We do not map any memory nor do any DMA:
	 */
	.bus_map =		nullbusmap,
	.bus_dma_allochdl =	ddi_no_dma_allochdl,
	.bus_dma_freehdl =	ddi_no_dma_freehdl,
	.bus_dma_bindhdl =	ddi_no_dma_bindhdl,
	.bus_dma_unbindhdl =	ddi_no_dma_unbindhdl,
	.bus_dma_flush =	ddi_no_dma_flush,
	.bus_dma_win =		ddi_no_dma_win,
	.bus_dma_ctl =		ddi_no_dma_mctl,
};

int
serdev_mod_init(struct dev_ops *devo)
{
	if (devo == NULL || devo->devo_bus_ops != NULL) {
		return (EINVAL);
	}

	devo->devo_bus_ops = &serdev_bus_ops;
	return (0);
}

void
serdev_mod_fini(struct dev_ops *devo)
{
	if (devo != NULL && devo->devo_bus_ops == &serdev_bus_ops) {
		devo->devo_bus_ops = NULL;
	}
}

speed_t
serdev_params_baudrate(serdev_params_t *srpp)
{
	return (srpp->srpp_baudrate);
}

uint_t
serdev_params_stop_bits(serdev_params_t *srpp)
{
	return (srpp->srpp_stop_bits);
}

serdev_parity_t
serdev_params_parity(serdev_params_t *srpp)
{
	return (srpp->srpp_parity);
}

uint_t
serdev_params_char_size(serdev_params_t *srpp)
{
	return (srpp->srpp_char_size);
}

bool
serdev_params_hard_flow_inbound(serdev_params_t *srpp)
{
	return (srpp->srpp_hard_flow_inbound);
}

bool
serdev_params_hard_flow_outbound(serdev_params_t *srpp)
{
	return (srpp->srpp_hard_flow_outbound);
}

serdev_handle_t *
serdev_handle_alloc(void *private, uint_t port, const serdev_ops_t *ops,
    int kmflag)
{
	serdev_handle_t *srdh;

	if (ops == NULL) {
		return (NULL);
	}

	switch (ops->srdo_version) {
	case SERDEV_OPS_VERSION_1:
		if (ops->srdo_open == NULL ||
		    ops->srdo_close == NULL ||
		    ops->srdo_rx == NULL ||
		    ops->srdo_tx == NULL ||
		    ops->srdo_flush_rx == NULL ||
		    ops->srdo_flush_tx == NULL ||
		    ops->srdo_drain == NULL ||
		    ops->srdo_break == NULL ||
		    ops->srdo_params_set == NULL ||
		    ops->srdo_modem_set == NULL ||
		    ops->srdo_modem_get == NULL) {
			cmn_err(CE_WARN, "serdev ops must be populated");
			return (NULL);
		}
		break;

	default:
		/*
		 * This is not a supported version number.
		 */
		return (NULL);
	}

	if ((srdh = kmem_zalloc(sizeof (*srdh), kmflag)) == NULL) {
		return (NULL);
	}

	srdh->srdh_private = private;
	srdh->srdh_port = port;
	srdh->srdh_ops = *ops;

	return (srdh);
}

static int
serdev_fetch_prop(dev_info_t *dip, uint_t port, const char *name, int defval)
{
	char perport[64];
	int r;

	/*
	 * First try the port-specific version:
	 */
	(void) snprintf(perport, sizeof (perport), "port-%u-%s", port, name);
	if ((r = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    perport, -1)) != -1) {
		return (r);
	}

	/*
	 * If not, fall back to the bare name which will apply to all ports:
	 */
	if ((r = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    (char *)name, -1)) != -1) {
		return (r);
	}

	/*
	 * Otherwise, return the default value:
	 */
	return (defval);
}

int
serdev_handle_attach(dev_info_t *dip, serdev_handle_t *srdh)
{
	int r;

	if (srdh->srdh_parent != NULL) {
		return (DDI_SUCCESS);
	}
	srdh->srdh_parent = dip;

	/*
	 * In the distant past, it was common for serial lines to be used with
	 * modems that provided signals like Data Carrier Detect (DCD).  These
	 * signals could be used by the computer to determine if there was
	 * another party connected to the modem.  This allowed a getty to block
	 * in open() of the line driver for the inbound (tty) device node,
	 * waiting for someone to dial in.
	 *
	 * Many or even most modern serial hardware provides a limited set of
	 * signals: often just the data lines (RX/TX), with the possible
	 * addition of hardware flow control (RTS/CTS).  In these instances we
	 * would be blocking waiting for a carrier that will never be detected.
	 *
	 * To ease the use of such serial lines, we allow a driver tunable to
	 * configure the framework to behave as if carrier detect was always
	 * asserted.  The driver configuration file we care about is the one
	 * for the actual device, so we read from its dip here.  This can be
	 * specified as a per-port property (e.g., "port-2-ignore-cd" for port
	 * 2) or a property for all ports (e.g., "ignore-cd").
	 */
	if (serdev_fetch_prop(dip, srdh->srdh_port, "ignore-cd", 0) != 0) {
		srdh->srdh_ignore_cd = true;
	} else {
		srdh->srdh_ignore_cd = false;
	}

	if (ndi_devi_alloc(dip, "serdev", (pnode_t)DEVI_SID_NODEID,
	    &srdh->srdh_child) != NDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!failed to allocate child dip for "
		    "port %u", srdh->srdh_port);
		goto bail;
	}

	ddi_set_parent_data(srdh->srdh_child, srdh);

	if ((r = ndi_devi_online(srdh->srdh_child, 0)) != NDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!failed to online child dip for "
		    "port %u: %d", srdh->srdh_port, r);
		(void) ndi_devi_free(srdh->srdh_child);
		goto bail;
	}

	return (DDI_SUCCESS);

bail:
	srdh->srdh_child = NULL;
	srdh->srdh_parent = NULL;
	srdh->srdh_ignore_cd = false;
	return (DDI_FAILURE);
}

int
serdev_handle_detach(serdev_handle_t *srdh)
{
	int r = DDI_FAILURE;

	if (srdh->srdh_parent == NULL) {
		return (DDI_SUCCESS);
	}

	ndi_devi_enter(srdh->srdh_parent);

	if (srdh->srdh_child == NULL) {
		/*
		 * The node was already removed by another thread.
		 */
		r = DDI_SUCCESS;
	} else if (i_ddi_node_state(srdh->srdh_child) < DS_INITIALIZED) {
		if (ddi_remove_child(srdh->srdh_child, 0) == DDI_SUCCESS) {
			r = DDI_SUCCESS;
		}
	} else {
		char *name = kmem_alloc(MAXNAMELEN + 1, KM_SLEEP);
		(void) ddi_deviname(srdh->srdh_child, name);
		(void) devfs_clean(srdh->srdh_parent, name + 1, DV_CLEAN_FORCE);
		if (ndi_devi_unconfig_one(srdh->srdh_parent, name + 1, NULL,
		    NDI_DEVI_REMOVE | NDI_UNCONFIG) == NDI_SUCCESS) {
			r = DDI_SUCCESS;
		}
	}

	ndi_devi_exit(srdh->srdh_parent);

	if (r == DDI_SUCCESS) {
		srdh->srdh_child = NULL;
		srdh->srdh_parent = NULL;
		srdh->srdh_ignore_cd = false;
	}

	return (r);
}

static serdev_t *
serdev_from_handle(serdev_handle_t *srdh)
{
	if (srdh->srdh_child == NULL) {
		return (NULL);
	}

	return (ddi_get_driver_private(srdh->srdh_child));
}

/*
 * Report to the framework that the modem status may have changed.
 */
void
serdev_handle_report_status(serdev_handle_t *srdh)
{
	serdev_t *srd = serdev_from_handle(srdh);
	if (srd == NULL) {
		return;
	}

	mutex_enter(&srd->srd_mutex);
	srd->srd_flags |= SERDEV_FL_NEED_STATUS;
	serdev_taskq_dispatch(srd);
	mutex_exit(&srd->srd_mutex);
}

/*
 * Report to the framework that the driver has completed sending all enqueued
 * data.
 */
void
serdev_handle_report_tx(serdev_handle_t *srdh)
{
	serdev_t *srd = serdev_from_handle(srdh);
	if (srd == NULL) {
		return;
	}

	mutex_enter(&srd->srd_mutex);

	/*
	 * Mark the device as ready to send more data.
	 */
	srd->srd_flags &= ~SERDEV_FL_TX_ACTIVE;

	if (srd->srd_tty.t_writeq != NULL) {
		/*
		 * Wake up the service routine for the write side of our stream
		 * so that we can pass more data to the device.
		 */
		qenable(srd->srd_tty.t_writeq);
	}

	mutex_exit(&srd->srd_mutex);
}

/*
 * Pass received data (M_DATA or M_BREAK messages) to the framework.
 */
void
serdev_handle_rx(serdev_handle_t *srdh, mblk_t *mp)
{
	serdev_t *srd = serdev_from_handle(srdh);
	if (srd == NULL) {
		freemsg(mp);
		return;
	}

	if (DB_TYPE(mp) == M_DATA && MBLKL(mp) < 1) {
		/*
		 * Don't accidentally accept a zero-length data block.
		 */
		freemsg(mp);
		return;
	}

	mutex_enter(&srd->srd_mutex);

	if (srd->srd_tty.t_readq == NULL || !(srd->srd_tty.t_cflag & CREAD)) {
		/*
		 * The port is not open or the control flags require us to drop
		 * incoming data.
		 */
		freemsg(mp);
	} else if (putq(srd->srd_tty.t_readq, mp) != 1) {
		/*
		 * XXX report failure?
		 */
		freemsg(mp);
	}

	mutex_exit(&srd->srd_mutex);
}

bool
serdev_handle_running_rx(serdev_handle_t *srdh)
{
	serdev_t *srd = serdev_from_handle(srdh);
	if (srd == NULL) {
		return (false);
	}

	mutex_enter(&srd->srd_mutex);
	bool running = (srd->srd_flags & SERDEV_FL_RX_STOPPED) == 0;
	mutex_exit(&srd->srd_mutex);

	return (running);
}

bool
serdev_handle_running_tx(serdev_handle_t *srdh)
{
	serdev_t *srd = serdev_from_handle(srdh);
	if (srd == NULL) {
		return (false);
	}

	mutex_enter(&srd->srd_mutex);
	bool running = (srd->srd_flags & SERDEV_FL_TX_STOPPED) == 0;
	mutex_exit(&srd->srd_mutex);

	return (running);
}

void
serdev_handle_free(serdev_handle_t *srdh)
{
	VERIFY3P(srdh->srdh_child, ==, NULL);
	VERIFY3P(srdh->srdh_parent, ==, NULL);

	kmem_free(srdh, sizeof (*srdh));
}
