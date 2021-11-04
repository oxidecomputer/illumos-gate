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

/*
 * VIRTIO 9P DRIVER
 */

#ifndef _VIO9P_H
#define	_VIO9P_H

#include "virtio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * VIRTIO 9P CONFIGURATION REGISTERS
 *
 * These are offsets into the device-specific configuration space available
 * through the virtio_dev_*() family of functions.
 */
#define	VIRTIO_9P_CONFIG_TAG_SZ			0x00	/* 16 R   */
#define	VIRTIO_9P_CONFIG_TAG			0x02	/* SZ R   */

/*
 * VIRTIO 9P VIRTQUEUES
 *
 * Virtio 9P devices have just one queue which is used to make 9P requests.
 * Each submitted chain should include appropriately sized inbound and outbound
 * descriptors for the request and response messages.  The maximum size is
 * negotiated via the "msize" member of the 9P TVERSION request and RVERSION
 * response.  Some hypervisors may require the first 7 bytes (size, type, tag)
 * to be contiguous in the first descriptor.
 */
#define	VIRTIO_9P_VIRTQ_REQUESTS	0

/*
 * VIRTIO 9P FEATURE BITS
 */
#define	VIRTIO_9P_F_MOUNT_TAG		(1ULL << 0)

/*
 * These features are supported by the driver and we will request them from the
 * device.
 */
#define	VIRTIO_9P_WANTED_FEATURES	(VIRTIO_9P_F_MOUNT_TAG)

/*
 * DRIVER PARAMETERS
 */

/*
 * TYPE DEFINITIONS
 */

typedef struct vio9p_req {
	virtio_dma_t			*vnr_dma_in;
	virtio_dma_t			*vnr_dma_out;
	virtio_chain_t			*vnr_chain;
	list_node_t			vnr_link;
	list_node_t			vnr_link_complete;
} vio9p_req_t;
#define	VIRTIO_9P_TAGLEN		32

typedef struct vio9p {
	dev_info_t			*vin_dip;
	virtio_t			*vin_virtio;
	virtio_queue_t			*vin_vq;

	kmutex_t			vin_mutex;
	kcondvar_t			vin_cv;

	bool				vin_open;

	list_t				vin_reqs;
	list_t				vin_completes;

	char				vin_tag[VIRTIO_9P_TAGLEN + 1];
} vio9p_t;

/*
 * XXX ioctl values
 */
#define	VIO9P_IOC_BASE			(('9' << 16) | ('P' << 8))
#define	VIO9P_IOC_MOUNT_TAG		(VIO9P_IOC_BASE | 0x01)

#ifdef __cplusplus
}
#endif

#endif /* _VIO9P_H */
