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
 * Copyright 2022 Oxide Computer Company
 */

#include <sys/types.h>
#include <string.h>
#include <strings.h>
#include <sys/mac.h>
#include <sys/dls_mgmt.h>
#include <sys/dlpi.h>
#include <sys/ethernet.h>
#include <sys/tfport.h>
#include <errno.h>
#include <unistd.h>

#include <libdladm_impl.h>
#include <libdllink.h>
#include <libdlaggr.h>
#include <libdltfport.h>

static dladm_status_t
i_dladm_create_tfport(dladm_handle_t handle, dladm_tfport_attr_t *attrp)
{
	int rc;
	dladm_status_t status = DLADM_STATUS_OK;
	tfport_ioc_create_t ioc;

	bzero(&ioc, sizeof (ioc));
	ioc.tic_link_id = attrp->tfa_link_id;
	ioc.tic_pkt_id = attrp->tfa_pkt_id;
	ioc.tic_port_id = attrp->tfa_port_id;
	ioc.tic_mac_len = attrp->tfa_mac_len;
	if (attrp->tfa_mac_len)
		bcopy(attrp->tfa_mac_addr, ioc.tic_mac_addr, ioc.tic_mac_len);

	rc = ioctl(dladm_dld_fd(handle), TFPORT_IOC_CREATE, &ioc);
	if (rc < 0)
		status = dladm_errno2status(errno);

	if (status != DLADM_STATUS_OK)
		return (status);

	bcopy(ioc.tic_mac_addr, attrp->tfa_mac_addr, ioc.tic_mac_len);
	attrp->tfa_mac_len = ioc.tic_mac_len;
	return (status);
}

static dladm_status_t
i_dladm_delete_tfport(dladm_handle_t handle, dladm_tfport_attr_t *attrp)
{
	int rc;
	dladm_status_t status = DLADM_STATUS_OK;
	tfport_ioc_delete_t ioc;

	bzero(&ioc, sizeof (ioc));
	ioc.tid_link_id = attrp->tfa_link_id;

	rc = ioctl(dladm_dld_fd(handle), TFPORT_IOC_DELETE, &ioc);
	if (rc < 0)
		status = dladm_errno2status(errno);

	return (status);
}

static dladm_status_t
i_dladm_get_tfport_info(dladm_handle_t handle, dladm_tfport_attr_t *attrp)
{
	int rc;
	dladm_status_t status = DLADM_STATUS_OK;
	tfport_ioc_info_t ioc;

	bzero(&ioc, sizeof (ioc));
	ioc.tii_link_id = attrp->tfa_link_id;

	rc = ioctl(dladm_dld_fd(handle), TFPORT_IOC_INFO, &ioc);
	if (rc < 0) {
		status = dladm_errno2status(errno);
		return (status);
	}

	bcopy(ioc.tii_mac_addr, attrp->tfa_mac_addr, ioc.tii_mac_len);
	attrp->tfa_mac_len = ioc.tii_mac_len;
	attrp->tfa_port_id = ioc.tii_port_id;
	attrp->tfa_pkt_id = ioc.tii_pkt_id;
	return (status);
}

dladm_status_t
dladm_tfport_create(dladm_handle_t handle, const char *tfportname,
    datalink_id_t pkt_id, uint32_t port, char *mac_addr, size_t mac_len)
{
	int flags = DLADM_OPT_ACTIVE;
	datalink_id_t link_id;
	dladm_status_t status;
	dladm_tfport_attr_t attr;

	bzero(&attr, sizeof (attr));
	if (mac_len > 0) {
		uchar_t *mac_bytes = NULL;
		int len = mac_len;
		if (mac_len > MAXMACADDRLEN)
			return (DLADM_STATUS_INVALIDMACADDR);
		if ((mac_bytes = _link_aton((char *)mac_addr, &len)) == NULL)
			return (DLADM_STATUS_INVALIDMACADDR);
		if (len != ETHERADDRL) {
			free(mac_bytes);
			return (DLADM_STATUS_INVALIDMACADDR);
		}
		attr.tfa_mac_len = ETHERADDRL;
		bcopy(mac_bytes, &attr.tfa_mac_addr, ETHERADDRL);
		free(mac_bytes);
	}

	if ((status = dladm_create_datalink_id(handle, tfportname,
	    DATALINK_CLASS_TFPORT, 0, flags, &link_id)) != DLADM_STATUS_OK) {
		return (status);
	}

	attr.tfa_link_id = link_id;
	attr.tfa_pkt_id = pkt_id;
	attr.tfa_port_id = port;
	status = i_dladm_create_tfport(handle, &attr);
	if (status == DLADM_STATUS_OK)
		(void) dladm_set_linkprop(handle, link_id, NULL, NULL, 0,
		    flags);
	else
		(void) dladm_destroy_datalink_id(handle, link_id, flags);

	return (status);
}

dladm_status_t
dladm_tfport_delete(dladm_handle_t handle, datalink_id_t tfport_id)
{
	dladm_tfport_attr_t attr;
	dladm_status_t status;
	datalink_class_t class;

	if ((dladm_datalink_id2info(handle, tfport_id, NULL, &class,
	    NULL, NULL, 0) != DLADM_STATUS_OK)) {
		return (DLADM_STATUS_BADARG);
	}

	if (class != DATALINK_CLASS_TFPORT)
		return (DLADM_STATUS_BADARG);

	bzero(&attr, sizeof (attr));
	attr.tfa_link_id = tfport_id;
	status = i_dladm_delete_tfport(handle, &attr);
	if (status == DLADM_STATUS_OK) {
		(void) dladm_set_linkprop(handle, tfport_id, NULL,
		    NULL, 0, DLADM_OPT_ACTIVE);
		(void) dladm_destroy_datalink_id(handle, tfport_id,
		    DLADM_OPT_ACTIVE);
	}

	return (status);
}

dladm_status_t
dladm_tfport_info(dladm_handle_t handle, datalink_id_t tfport_id,
    dladm_tfport_attr_t *attrp)
{
	datalink_class_t class;

	if ((dladm_datalink_id2info(handle, tfport_id, NULL, &class,
	    NULL, NULL, 0) != DLADM_STATUS_OK)) {
		return (DLADM_STATUS_BADARG);
	}

	if (class != DATALINK_CLASS_TFPORT)
		return (DLADM_STATUS_BADARG);

	bzero(attrp, sizeof (attrp));
	attrp->tfa_link_id = tfport_id;

	return (i_dladm_get_tfport_info(handle, attrp));
}
