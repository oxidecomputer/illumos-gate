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

#ifndef	_SYS_TFPKT_IMPL_H
#define	_SYS_TFPKT_IMPL_H

#include <sys/types.h>
#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/mac.h>
#include <sys/net80211.h>
#include <sys/types.h>
#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/taskq_impl.h>
#include <sys/tofino.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct tfpkt tfpkt_t;
typedef struct tfpkt_tbus tfpkt_tbus_t;

typedef enum tfpkt_runstate {
	TFPKT_RUNSTATE_STOPPED,
	TFPKT_RUNSTATE_RUNNING,
	TFPKT_RUNSTATE_STOPPING,
} tfpkt_runstate_t;

typedef enum tfpkt_tbus_state {
	TFPKT_TBUS_UNINIT,
	TFPKT_TBUS_ACTIVE,
	TFPKT_TBUS_RESETTING,
	TFPKT_TBUS_HALTING,
	TFPKT_TBUS_HALTED,
} tfpkt_tbus_state_t;

typedef struct tfpkt_stats {
	uint64_t tps_tx_pkts;
	uint64_t tps_tx_bytes;
	uint64_t tps_tx_errs;
	uint64_t tps_tx_zombie;
	uint64_t tps_tx_alloc_fails;
	uint64_t tps_tx_tbus_fails;
	uint64_t tps_tx_missing_schdr;
	uint64_t tps_tx_truncated_eth;

	uint64_t tps_rx_pkts;
	uint64_t tps_rx_bytes;
	uint64_t tps_rx_errs;
	uint64_t tps_rx_zombie;
	uint64_t tps_rx_alloc_fails;
	uint64_t tps_rx_truncated_eth;

	uint64_t tps_detach_fails;
	uint64_t tps_tbus_inactive;
	uint64_t tps_tbus_hold_fails;
} tfpkt_stats_t;

typedef struct tfpkt_tbus_stats {
	uint64_t	ttb_rxfail_no_descriptors;
	uint64_t	ttb_txfail_pkt_too_large;
	uint64_t	ttb_txfail_no_bufs;
	uint64_t	ttb_txfail_no_descriptors;
	uint64_t	ttb_txfail_other;
} tfpkt_tbus_stats_t;

/* Descriptor ring management */

/*
 * There are four types of Descriptor Ring involved with processing packets on
 * the PCI port:
 *   Tx: packets to be transferred across the PCI bus to the ASIC
 *   Cmp: completion notifications from the ASIC that a Tx packet has been
 *        processed
 *   Fm: free memory handed to the ASIC into which packets can be received
 *   Rx: packets transferred from the ASIC across the PCI bus
 */

typedef enum {
	TFPKT_DR_TX,
	TFPKT_DR_CMP,
	TFPKT_DR_FM,
	TFPKT_DR_RX,
} tfpkt_dr_type_t;

/* Number of DRs of each type */
#define	TFPKT_CMP_CNT		4
#define	TFPKT_FM_CNT		8
#define	TFPKT_TX_CNT		4
#define	TFPKT_RX_CNT		8

/* Number of entries in each DR of each type */
#define	TFPKT_CMP_DEPTH	16
#define	TFPKT_FM_DEPTH		16
#define	TFPKT_TX_DEPTH		16
#define	TFPKT_RX_DEPTH		16

typedef struct {
	char		tdr_name[32];
	kmutex_t	tdr_mutex;
	uint32_t	tdr_reg_base;		/* start of config registers */
	tfpkt_dr_type_t	tdr_type;	/* variety of descriptors */
	int		tdr_id;		/* index into per-type list */
	uint64_t	tdr_phys_base;	/* PA of the descriptor ring */
	uint64_t	tdr_virt_base;	/* VA of the descriptor ring */
	uint64_t	*tdr_tail_ptr;	/* VA of the tail ptr copy */
	uint64_t	tdr_depth;		/* # of descriptors in ring */
	uint64_t	tdr_desc_size;	/* size of each descriptor */
	uint64_t	tdr_ring_size;	/* size of descriptor data */
	uint64_t	tdr_head;		/* head offset */
	uint64_t	tdr_tail;		/* tail offset */
	tf_tbus_dma_t	tdr_dma;		/* descriptor data */
} tfpkt_dr_t;

/* rx descriptor entry */
typedef struct {
	uint64_t rx_s: 1;
	uint64_t rx_e: 1;
	uint64_t rx_type: 3;
	uint64_t rx_status: 2;
	uint64_t rx_attr: 25;
	uint64_t rx_size: 32;
	uint64_t rx_addr;
} tfpkt_dr_rx_t;

#define	TFPRT_RX_DESC_TYPE_LRT		0
#define	TFPRT_RX_DESC_TYPE_IDLE		1
#define	TFPRT_RX_DESC_TYPE_LEARN	3
#define	TFPRT_RX_DESC_TYPE_PKT		4
#define	TFPRT_RX_DESC_TYPE_DIAG		7
#define	TFPRT_TX_DESC_TYPE_MAC_STAT	0

/* tx descriptor entry */
typedef struct {
	uint64_t tx_s: 1;
	uint64_t tx_e: 1;
	uint64_t tx_type: 3;
	uint64_t tx_attr: 27;
	uint64_t tx_size: 32;
	uint64_t tx_src;
	uint64_t tx_dst;
	uint64_t tx_msg_id;
} tfpkt_dr_tx_t;

#define	TFPRT_TX_DESC_TYPE_IL		1
#define	TFPRT_TX_DESC_TYPE_WR_BLK	3
#define	TFPRT_TX_DESC_TYPE_RD_BLK	4
#define	TFPRT_TX_DESC_TYPE_QUE_RD_BLK	4
#define	TFPRT_TX_DESC_TYPE_QUE_WR_LIST	5
#define	TFPRT_TX_DESC_TYPE_PKT		6
#define	TFPRT_TX_DESC_TYPE_MAC_WR_BLK	7

/* completion descriptor entry */
typedef struct {
	uint64_t cmp_s: 1;
	uint64_t cmp_e: 1;
	uint64_t cmp_type: 3;
	uint64_t cmp_status: 2;
	uint64_t cmp_attr: 25;
	uint64_t cmp_size: 32;
	uint64_t cmp_addr;
} tfpkt_dr_cmp_t;

#define	TFPKT_NET_TX_BUFS	256
#define	TFPKT_NET_RX_BUFS	256
#define	TFPKT_BUF_SIZE		2048

#define	TFPKT_BUF_DMA_ALLOCED	0x01

/*
 * Buffers are allocated in advance with memory capable of DMA to/from
 * the Tofino ASIC.
 */
typedef struct tfpkt_buf {
	tfpkt_tbus_t	*tfb_tbus;
	int		tfb_flags;
	tf_tbus_dma_t	tfb_dma;
	list_node_t	tfb_link;
} tfpkt_buf_t;

typedef struct tfpkt_buf_list {
	kmutex_t	tbl_mutex;
	list_t		tbl_data;

	uint64_t	tbl_count;
	uint64_t	tbl_low_water;
	uint64_t	tbl_high_water;
	uint64_t	tbl_alloc_fails;
	uint64_t	tbl_va_lookup_fails;
	uint64_t	tbl_pa_lookup_fails;
} tfpkt_buf_list_t;

/*
 * State managed by the tofino tbus handler
 */
struct tfpkt_tbus {
	kmutex_t		ttb_mutex;
	tfpkt_t			*ttb_tfp;
	dev_info_t		*ttb_dip;
	tf_tbus_hdl_t		ttb_tbus_hdl;	/* tofino driver handle */

	tofino_gen_t		ttb_gen;

	/* DR management */
	tfpkt_dr_t	*ttb_rx_drs;	/* Rx DRs */
	tfpkt_dr_t	*ttb_tx_drs;	/* Tx DRs */
	tfpkt_dr_t	*ttb_fm_drs;	/* Free memory DRs */
	tfpkt_dr_t	*ttb_cmp_drs;	/* Tx completion DRs */

	/* DMA buffer management */
	tfpkt_buf_list_t	ttb_rxbufs_free;	/* unused rx bufs */
	tfpkt_buf_list_t	ttb_rxbufs_pushed;	/* rx bufs in ASIC FM */
	tfpkt_buf_list_t	ttb_rxbufs_inuse;	/* rx bufs in use */
	tfpkt_buf_list_t	ttb_txbufs_free;	/* unused tx bufs */
	tfpkt_buf_list_t	ttb_txbufs_pushed;	/* tx bufs on TX DR */
	tfpkt_buf_list_t	ttb_txbufs_inuse;	/* tx bufs inuse */

	uint_t		ttb_bufs_capacity;	/* total rx+tx bufs */
	tfpkt_buf_t	*ttb_bufs_mem;		/* all rx+tx bufs */

	tfpkt_tbus_stats_t	ttb_stats;
};

typedef enum {
	TFPKT_DR_HALTED,
	TFPKT_DR_STARTING,
	TFPKT_DR_RUNNING,
	TFPKT_DR_HALTING
} tfpkt_dr_process_state_t;

struct tfpkt {
	kmutex_t		tfp_mutex;
	dev_info_t		*tfp_dip;	// tfpkt device
	int			tfp_instance;
	uint32_t		tfp_mac_refcnt;
	tfpkt_runstate_t	tfp_runstate;
	tfpkt_stats_t		tfp_stats;
	mac_handle_t		tfp_mh;

	/*
	 * task queue for the threads used to monitor the tbus state and to
	 * process incoming packets and tx completions.
	 */
	taskq_t			*tfp_tbus_tq;

	/*
	 * Tracks the state of the tofino tbus, ensuring that we don't release
	 * it while in use, and that we don't use it while the userspace
	 * dataplane daemon is resetting it.
	 */
	kmutex_t		tfp_tbus_mutex;
	kcondvar_t		tfp_tbus_cv;
	uint32_t		tfp_tbus_refcnt;
	tfpkt_tbus_state_t	tfp_tbus_state;
	tfpkt_tbus_t		*tfp_tbus_data;
	taskq_ent_t		tfp_tbus_monitor;

	/*
	 * Used to coordinate the lifecycle of the rx/cmp processing thread.
	 */
	kmutex_t			tfp_dr_process_mutex;
	kcondvar_t			tfp_dr_process_cv;
	tfpkt_dr_process_state_t	tfp_dr_process_state;
	taskq_ent_t			tfp_dr_process;
};

caddr_t tfpkt_buf_va(tfpkt_buf_t *buf);
tfpkt_buf_t *tfpkt_tbus_tx_alloc(tfpkt_tbus_t *, size_t sz);
void tfpkt_tbus_tx_free(tfpkt_tbus_t *, tfpkt_buf_t *);
int tfpkt_tbus_tx(tfpkt_tbus_t *, tfpkt_buf_t *, size_t sz);
void tfpkt_rx(tfpkt_t *tfp, void *vaddr, size_t mblk_sz);
void tfpkt_tbus_rx_done(tfpkt_tbus_t *, void *, size_t);

tfpkt_tbus_t *tfpkt_tbus_hold(tfpkt_t *tfp);
void tfpkt_tbus_release(tfpkt_t *tfp);
void tfpkt_tbus_monitor(void *);
int tfpkt_tbus_monitor_halt(tfpkt_t *tfp);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TFPKT_IMPL_H */
