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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef	_SYS_TFPORT_H
#define	_SYS_TFPORT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/mac.h>

#define	TFPORT_IOC_CREATE	TFPORTIOC(0x0001)
#define	TFPORT_IOC_DELETE	TFPORTIOC(0x0002)
#define	TFPORT_IOC_INFO		TFPORTIOC(0x0003)

#define	TFPORT_IOC_L2_NEEDED	TFPORTIOC(0x1001)

typedef struct tfport_ioc_create {
	datalink_id_t	tic_link_id;
	datalink_id_t	tic_pkt_id;
	uint_t		tic_port_id;
	uint_t		tic_mac_len;
	uchar_t		tic_mac_addr[ETHERADDRL];
} tfport_ioc_create_t;

typedef struct tfport_ioc_delete {
	datalink_id_t	tid_link_id;
} tfport_ioc_delete_t;

typedef struct tfport_ioc_info {
	datalink_id_t	tii_link_id;
	datalink_id_t	tii_pkt_id;
	uint_t		tii_port_id;
	uint_t		tii_mac_len;
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
