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
typedef struct tf_tbus tf_tbus_t;

typedef enum tfpkt_runstate {
	TFPKT_RUNSTATE_RUNNING,
	TFPKT_RUNSTATE_STOPPING,
	TFPKT_RUNSTATE_STOPPED,
} tfpkt_runstate_t;

typedef enum tfpkt_tbus_state {
	TFPKT_TBUS_UNINIT,
	TFPKT_TBUS_ACTIVE,
	TFPKT_TBUS_RESETTING,
	TFPKT_TBUS_HALTING,
	TFPKT_TBUS_HALTED,
} tfpkt_tbus_state_t;

typedef struct tfpkt_stats {
	uint64_t		tfs_rbytes;
	uint64_t		tfs_obytes;
	uint64_t		tfs_xmit_errors;
	uint64_t		tfs_xmit_count;
	uint64_t		tfs_recv_count;
	uint64_t		tfs_recv_errors;
} tfpkt_stats_t;

struct tfpkt {
	kmutex_t		tfp_mutex;
	dev_info_t		*tfp_dip;	// tfpkt device
	int			tfp_instance;
	uint32_t		tfp_mac_refcnt;
	tfpkt_runstate_t	tfp_runstate;
	tfpkt_stats_t		tfp_stats;
	boolean_t		tfp_promisc;
	mac_handle_t		tfp_mh;

	kmutex_t		tfp_tbus_mutex;
	kcondvar_t		tfp_tbus_cv;
	uint32_t		tfp_tbus_refcnt;
	tfpkt_tbus_state_t	tfp_tbus_state;
	tf_tbus_t		*tfp_tbus_data;
	taskq_ent_t		tfp_tbus_monitor;
	taskq_t			*tfp_tbus_tq;
};

void tfpkt_reset_trigger(tfpkt_t *tfp);
void tfpkt_rx(tfpkt_t *tfp, void *vaddr, size_t mblk_sz);

typedef struct tf_tbus tf_tbus_t;
struct tf_tbus_dev;

typedef struct tf_tbus_stats {
	uint64_t		rbytes;
	uint64_t		obytes;
	uint64_t		xmit_errors;
	uint64_t		xmit_count;
	uint64_t		recv_count;
	uint64_t		recv_errors;
} tf_tbus_stats_t;

#define	TFPORT_NET_TX_BUFS	256
#define	TFPORT_NET_RX_BUFS	256
#define	TFPORT_BUF_SIZE		2048

#define	TFPORT_BUF_DMA_ALLOCED	0x01
#define	TFPORT_BUF_LOANED	0x02

/* Descriptor ring management */

/*
 * There are four types of Descriptor Ring involved with processing packets on
 * the PCI port:
 *   Rx: packets transferred from the ASIC across the PCI bus
 *   Fm: free memory handed to the ASIC into which packets can be received
 *   Tx: packets to be transferred across the PCI bus to the ASIC
 *   Cmp: completion notifications from the ASIC that a Tx packet has been
 *        processed
 */

typedef enum {
	TF_PKT_DR_TX,
	TF_PKT_DR_CMP,
	TF_PKT_DR_FM,
	TF_PKT_DR_RX,
} tf_tbus_dr_type_t;

/* Number of DRs of each type */
#define	TF_PKT_CMP_CNT		4
#define	TF_PKT_FM_CNT		8
#define	TF_PKT_TX_CNT		4
#define	TF_PKT_RX_CNT		8

/* Number of entries in each DR of each type */
#define	TF_PKT_CMP_DEPTH	16
#define	TF_PKT_FM_DEPTH		16
#define	TF_PKT_TX_DEPTH		16
#define	TF_PKT_RX_DEPTH		16

#define	DR_NAME_LEN		32

typedef struct {
	char		tfdrp_name[DR_NAME_LEN];
	kmutex_t	tfdrp_mutex;
	uint32_t	tfdrp_reg_base;		/* start of config registers */
	tf_tbus_dr_type_t	tfdrp_type;	/* variety of descriptors */
	int		tfdrp_id;		/* index into per-type list */
	uint64_t	tfdrp_phys_base;	/* PA of the descriptor ring */
	uint64_t	tfdrp_virt_base;	/* VA of the descriptor ring */
	uint64_t	*tfdrp_tail_ptr;	/* VA of the tail ptr copy */
	uint64_t	tfdrp_depth;		/* # of descriptors in ring */
	uint64_t	tfdrp_desc_size;	/* size of each descriptor */
	uint64_t	tfdrp_ring_size;	/* size of descriptor data */
	uint64_t	tfdrp_head;		/* head offset */
	uint64_t	tfdrp_tail;		/* tail offset */
	tf_tbus_dma_t	tfdrp_dma;		/* descriptor data */
} tf_tbus_dr_t;

/* rx descriptor entry */
typedef struct {
	uint64_t rx_s: 1;
	uint64_t rx_e: 1;
	uint64_t rx_type: 3;
	uint64_t rx_status: 2;
	uint64_t rx_attr: 25;
	uint64_t rx_size: 32;
	uint64_t rx_addr;
} tf_tbus_dr_rx_t;

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
} tf_tbus_dr_tx_t;

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
} tf_tbus_dr_cmp_t;

/*
 * Buffers are allocated in advance as a combination of DMA memory and
 * a descriptor chain.  Buffers can be loaned to the networking stack
 * to avoid copying, and this object contains the free routine to pass to
 * desballoc().
 */
typedef struct tf_tbus_buf {
	tf_tbus_t	*tfb_tbus;
	int		tfb_flags;
	tf_tbus_dma_t	tfb_dma;
	list_node_t	tfb_link;
} tf_tbus_buf_t;

/*
 * State managed by the tofino tbus handler
 */
struct tf_tbus {
	kmutex_t		tbp_mutex;
	tfpkt_t			*tbp_tfp;
	dev_info_t		*tbp_dip;
	ddi_softint_handle_t	tbp_softint;
	tf_tbus_hdl_t		tbp_tbus_hdl;	/* tofino driver handle */

	tofino_gen_t		tbp_gen;

	/* DR management */
	tf_tbus_dr_t	*tbp_rx_drs;	/* Rx DRs */
	tf_tbus_dr_t	*tbp_tx_drs;	/* Tx DRs */
	tf_tbus_dr_t	*tbp_fm_drs;	/* Free memory DRs */
	tf_tbus_dr_t	*tbp_cmp_drs;	/* Tx completion DRs */

	/* DMA buffer management */
	list_t		tbp_rxbufs_free;	/* unused rx bufs */
	list_t		tbp_rxbufs_pushed;	/* rx bufs in ASIC FM */
	list_t		tbp_rxbufs_loaned;	/* rx bufs loaned to pkt drv */
	list_t		tbp_txbufs_free;	/* unused tx bufs */
	list_t		tbp_txbufs_pushed;	/* tx bufs on TX DR */
	list_t		tbp_txbufs_loaned;	/* tx bufs loaned to pkt drv */
	uint_t		tbp_ntxbufs_onloan;	/* # of tx bufs on loan */
	uint_t		tbp_nrxbufs_onloan;	/* # of rx bufs on loan */
	uint_t		tbp_nrxbufs_onloan_max;	/* max bufs we can loan out */
	uint_t		tbp_bufs_capacity;	/* total rx+tx bufs */
	tf_tbus_buf_t	*tbp_bufs_mem;		/* all rx+tx bufs */

	/* Internal debugging statistics: */
	uint64_t	tbp_rxfail_excess_loans;
	// uint64_t	tbp_rxfail_dma_handle;
	// uint64_t	tbp_rxfail_dma_buffer;
	// uint64_t	tbp_rxfail_dma_bind;
	uint64_t	tbp_rxfail_no_descriptors;
	uint64_t	tbp_txfail_no_bufs;
	uint64_t	tbp_txfail_no_descriptors;
	// uint64_t	tbp_txfail_dma_handle;
	// uint64_t	tbp_txfail_dma_bind;
	// uint64_t	tbp_txfail_indirect_limit;
};


void tofino_tbus_rx_done(tf_tbus_t *, void *, size_t);
void *tofino_tbus_tx_alloc(tf_tbus_t *, size_t sz);
void tofino_tbus_tx_free(tf_tbus_t *, void *addr);
int tofino_tbus_tx(tf_tbus_t *, void *, size_t sz);
tf_tbus_t *tfpkt_tbus_hold(tfpkt_t *tfp);
void tfpkt_tbus_release(tfpkt_t *tfp);
void tfpkt_tbus_reset_detected(tfpkt_t *tfp);
void tfpkt_tbus_monitor(void *);
int tfpkt_tbus_monitor_halt(tfpkt_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TFPKT_IMPL_H */
