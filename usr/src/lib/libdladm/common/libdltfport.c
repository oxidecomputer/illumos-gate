/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
	ioc.tic_port_id = attrp->tfa_port_id;
	ioc.tic_mac_len = attrp->tfa_mac_len;
	if (attrp->tfa_mac_len)
		bcopy(attrp->tfa_mac_addr, ioc.tic_mac_addr, ioc.tic_mac_len);

	rc = ioctl(dladm_dld_fd(handle), TFPORT_IOC_CREATE, &ioc);
	if (rc < 0)
		status = dladm_errno2status(errno);

	if (status != DLADM_STATUS_OK)
		return (status);

	bcopy(ioc.tic_mac_addr, attrp->tfa_mac_addr, MAXMACADDRLEN);
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

	bcopy(ioc.tii_mac_addr, attrp->tfa_mac_addr, MAXMACADDRLEN);
	attrp->tfa_mac_len = ioc.tii_mac_len;
	attrp->tfa_port_id = ioc.tii_port_id;
	return (status);
}

dladm_status_t
dladm_tfport_create(dladm_handle_t handle, const char *tfportname,
	uint32_t port, uchar_t *mac_addr, size_t mac_len)
{
	int flags = DLADM_OPT_ACTIVE;
	datalink_id_t tfport_id;
	dladm_status_t status;
	dladm_tfport_attr_t attr;

	if (mac_len > MAXMACADDRLEN) 
		return (DLADM_STATUS_INVALIDMACADDR);

	if ((status = dladm_create_datalink_id(handle, tfportname,
	    DATALINK_CLASS_TFPORT, 0, flags, &tfport_id)) != DLADM_STATUS_OK)
		return (status);

	bzero(&attr, sizeof (attr));
	attr.tfa_link_id = tfport_id;
	attr.tfa_port_id = port;
	bcopy(mac_addr, &attr.tfa_mac_addr, mac_len);
	attr.tfa_mac_len = mac_len;

	status = i_dladm_create_tfport(handle, &attr);
	if (status == DLADM_STATUS_OK)
		(void) dladm_set_linkprop(handle, tfport_id, NULL, NULL, 0, flags);
	else
		(void) dladm_destroy_datalink_id(handle, tfport_id, flags);

	return (status);
}

dladm_status_t
dladm_tfport_delete(dladm_handle_t handle, datalink_id_t tfport_id)
{
	dladm_tfport_attr_t attr;
	dladm_status_t status;
	datalink_class_t class;

	if ((dladm_datalink_id2info(handle, tfport_id, NULL, &class,
	    NULL, NULL, 0) != DLADM_STATUS_OK))
		return (DLADM_STATUS_BADARG);

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
	    NULL, NULL, 0) != DLADM_STATUS_OK))
		return (DLADM_STATUS_BADARG);

	if (class != DATALINK_CLASS_TFPORT)
		return (DLADM_STATUS_BADARG);

	bzero(attrp, sizeof (attrp));
	attrp->tfa_link_id = tfport_id;

	return i_dladm_get_tfport_info(handle, attrp);
}
