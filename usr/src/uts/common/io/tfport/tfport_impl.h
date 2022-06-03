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

#ifndef	_SYS_TFPORT_IMPL_H
#define	_SYS_TFPORT_IMPL_H

#include <sys/types.h>
#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/mac.h>
#include <sys/net80211.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct tfport tfport_t;

typedef enum tfport_runstate {
	TPORT_RUNSTATE_STOPPED = 1,
	TPORT_RUNSTATE_STOPPING,
	TPORT_RUNSTATE_RUNNING
} tfport_runstate_t;

typedef struct tfport_stats {
	uint64_t		tfs_rbytes;
	uint64_t		tfs_obytes;
	uint64_t		tfs_xmit_errors;
	uint64_t		tfs_xmit_count;
	uint64_t		tfs_recv_count;
	uint64_t		tfs_recv_errors;
} tfport_stats_t;

// Represents a single port on the switch
typedef struct tfport_port {
	list_node_t		tp_listnode;
	tfport_t		*tp_tfport;
	uint32_t		tp_port;
	datalink_id_t		tp_link_id;
	tfport_runstate_t	tp_runstate;
	int			tp_loaned_bufs;
	kmutex_t		tp_mutex;
	mac_handle_t		tp_mh;
	boolean_t		tp_promisc;
	uint32_t		tp_mac_len;
	uchar_t			tp_mac_addr[MAXMACADDRLEN];
	tfport_stats_t		tp_stats;
	link_state_t		tp_ls;
} tfport_port_t;

struct tfport {
	kmutex_t		tfp_mutex;
	dev_info_t		*tfp_dip;
	int			tfp_instance;
	int32_t			tfp_refcnt;
	tofino_pkt_cookie_t	tfp_pkt_cookie;
	list_t			tfp_ports;
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TFPORT_IMPL_H */
