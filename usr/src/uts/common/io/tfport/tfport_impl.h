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

typedef struct tfport_header_info {
	uint16_t	thi_eth_type;
	uint16_t	thi_sc_eth_type;
	uint16_t	thi_sc_port;
	uint8_t		thi_sc_code;
} tfport_header_info_t;

/*
 * Represents a single source/target for tofino/sidecar packets:
 */
typedef struct tfport_source {
	list_node_t	tps_listnode;
	kmutex_t	tps_mutex;
	uint32_t	tps_refcnt;
	tfport_t	*tps_tfport;

	/*
	 * All the handles and state used to manage our interaction with the
	 * mac device over which the tfport multiplexer is layered:
	 */
	uint8_t			tps_init_state;
	datalink_id_t		tps_id;
	mac_handle_t		tps_mh;
	mac_unicast_handle_t	tps_muh;
	mac_client_handle_t	tps_mch;
	mac_notify_handle_t	tps_mnh;
} tfport_source_t;

typedef enum tfport_runstate {
	TFPORT_RUNSTATE_STOPPED = 1,
	TFPORT_RUNSTATE_STOPPING,
	TFPORT_RUNSTATE_RUNNING
} tfport_runstate_t;

typedef struct tfport_port_stats {
	uint64_t tfs_rx_bytes;
	uint64_t tfs_rx_pkts;
	uint64_t tfs_tx_bytes;
	uint64_t tfs_tx_pkts;
} tfport_port_stats_t;

#define	TFPORT_INIT_MAC_REGISTER	0x01
#define	TFPORT_INIT_INDEXED		0x02
#define	TFPORT_INIT_DEVNET		0x04

/*
 * Represents a single port on the switch.
 */
typedef struct tfport_port {
	kmutex_t		tp_mutex;
	uint32_t		tp_refcnt;
	uint32_t		tp_port;	/* tofino port ID */
	datalink_id_t		tp_link_id;	/* dladm link ID */
	datalink_id_t		tp_src_id;	/* dladm link ID of source */
	uint16_t		tp_init_state;
	tfport_runstate_t	tp_run_state;
	mac_handle_t		tp_mh;
	boolean_t		tp_promisc;
	uint32_t		tp_mac_len;
	uint8_t			tp_mac_addr[ETHERADDRL];
	tfport_port_stats_t	tp_stats;
	link_state_t		tp_ls;
	tfport_t		*tp_tfport;	/* link to global state */
	avl_node_t		tp_link_node;	/* link-indexed tree */
	avl_node_t		tp_port_node;	/* source/port-indexed tree */
} tfport_port_t;

typedef struct tfport_stats {
	uint64_t tfs_unclaimed_pkts;	/* no matching tfport device */
	uint64_t tfs_non_sidecar;	/* no sidecar header */
	uint64_t tfs_zombie_pkts;	/* pkts for tfport shutting down */
	uint64_t tfs_truncated_eth;	/* pkts shorter than an eth header */
	uint64_t tfs_truncated_sidecar;	/* pkts shorter than eth+sidecar hdrs */
	uint64_t tfs_loopback_pkts;	/* pkts both in and out on pci port */
	uint64_t tfs_mac_loopback;	/* rx marked as "loopback" by mac */
	uint64_t tfs_tx_nomem_drops;	/* failed to alloc tx buffer */
	uint64_t tfs_rx_nomem_drops;	/* failed to alloc rx buffer */
} tfport_stats_t;

#define	TFPORT_SOURCE_OPEN		0x01
#define	TFPORT_SOURCE_CLIENT_OPEN	0x02
#define	TFPORT_SOURCE_UNICAST_ADD	0x04
#define	TFPORT_SOURCE_NOTIFY_ADD	0x08
#define	TFPORT_SOURCE_RX_SET		0x10

struct tfport {
	kmutex_t	tfp_mutex;
	dev_info_t	*tfp_dip;
	int		tfp_instance;
	list_t		tfp_sources;

	/* all tfport_port_t nodes */
	avl_tree_t	tfp_ports_by_port;
	avl_tree_t	tfp_ports_by_link;

	tfport_stats_t	tfp_stats;
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TFPORT_IMPL_H */
