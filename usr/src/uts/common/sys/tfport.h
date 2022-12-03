/*
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 */

/*
 * Copyright 2023 Oxide Computer Company
 */

#ifndef	_SYS_TFPORT_H
#define	_SYS_TFPORT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/mac.h>
#include <sys/socket.h>
#include <sys/ethernet.h>
#include <sys/dld_ioc.h>

/* ioctl()s used by dladm to manage the tfport link lifecycle */
#define	TFPORT_IOC_CREATE	TFPORTIOC(0x0001)
#define	TFPORT_IOC_DELETE	TFPORTIOC(0x0002)
#define	TFPORT_IOC_INFO		TFPORTIOC(0x0003)

/* ioctl()s used by tfportd provide link service */
#define	TFPORT_IOC_L2_NEEDED	TFPORTIOC(0x1001)

typedef struct tfport_ioc_create {
	datalink_id_t	tic_link_id;	/* tfport link id */
	datalink_id_t	tic_pkt_id;	/* link id of the packet source */
	uint_t		tic_port_id;	/* port# in the tofino asic / p4 code */
	uint_t		tic_mac_len;	/* should be 0 or ETHERADDRL */
	uchar_t		tic_mac_addr[ETHERADDRL];
} tfport_ioc_create_t;

typedef struct tfport_ioc_delete {
	datalink_id_t	tid_link_id;	/* tfport to delete */
} tfport_ioc_delete_t;

typedef struct tfport_ioc_info {
	datalink_id_t	tii_link_id;	/* IN: tfport link id */
	datalink_id_t	tii_pkt_id;	/* OUT: packet source link */
	uint_t		tii_port_id;	/* OUT: tofino asic's port# */
	uint_t		tii_mac_len;	/* OUT: ETHERADDRL */
	uchar_t		tii_mac_addr[ETHERADDRL];
} tfport_ioc_info_t;

typedef struct tfport_ioc_l2 {
	struct sockaddr_storage	til_addr;
	uint_t			til_ifindex;
} tfport_ioc_l2_t;

#ifdef _KERNEL

typedef struct tfport_dev tfport_dev_t;

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TFPORT_H */
