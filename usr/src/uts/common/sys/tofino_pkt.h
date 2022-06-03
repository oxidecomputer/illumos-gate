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

#ifndef _SYS_TOFINO_PKT_H
#define	_SYS_TOFINO_PKT_H

#include <sys/types.h>
#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/tofino.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct tfpkt tfpkt_t;

typedef void (*tofino_rx_handler_t)(void *, void *, size_t);
typedef void (*tofino_cmp_handler_t)(void);

typedef struct tfpkt_handler {
	tfpkt_t			*th_tfpkt;
	void			*th_arg;
	tofino_rx_handler_t	th_rx_hdlr;
	tofino_cmp_handler_t	th_cmp_hdlr;
} tfpkt_handler_t;

struct tfpkt_dev;

typedef struct tfpkt_stats {
	uint64_t		rbytes;
	uint64_t		obytes;
	uint64_t		xmit_errors;
	uint64_t		xmit_count;
	uint64_t		recv_count;
	uint64_t		recv_errors;
} tfpkt_stats_t;

#define	TFPORT_NET_TX_BUFS		256
#define	TFPORT_NET_RX_BUFS		256
#define	TFPORT_BUF_SIZE			2048

#define	TFPORT_BUF_DMA_ALLOCED	0x01
#define	TFPORT_BUF_LOANED	0x02

/*
 * Metadata used for tracking each DMA memory allocation.
 */
typedef struct tfpkt_dma {
	ddi_dma_handle_t	tpd_handle;
	ddi_acc_handle_t	tpd_acchdl;
	ddi_dma_cookie_t	tpd_cookie;
	caddr_t			tpd_addr;
	size_t			tpd_len;
} tfpkt_dma_t;

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
} tfpkt_dr_type_t;

/* Number of DRs of each type */
#define	TF_PKT_CMP_CNT 	4
#define	TF_PKT_FM_CNT 	8
#define	TF_PKT_TX_CNT 	4
#define	TF_PKT_RX_CNT 	8

/* Number of entries in each DR of each type */
#define	TF_PKT_CMP_DEPTH 	16
#define	TF_PKT_FM_DEPTH 	16
#define	TF_PKT_TX_DEPTH 	16
#define	TF_PKT_RX_DEPTH 	16

#define	DR_NAME_LEN 32

typedef struct {
	char		tfdrp_name[DR_NAME_LEN];
	kmutex_t	tfdrp_mutex;
	uint32_t	tfdrp_reg_base;		/* start of config registers */
	tfpkt_dr_type_t	tfdrp_type;		/* variety of descriptors */
	int		tfdrp_id;		/* index into per-type list */
	uint64_t	tfdrp_phys_base;	/* PA of the descriptor ring */
	uint64_t	tfdrp_virt_base;	/* VA of the descriptor ring */
	uint64_t	*tfdrp_tail_ptr;	/* VA of the tail ptr copy */
	uint64_t	tfdrp_depth;		/* # of descriptors in ring */
	uint64_t	tfdrp_desc_size;	/* size of each descriptor */
	uint64_t	tfdrp_ring_size;	/* size of the descriptor data */
	uint64_t	tfdrp_head;		/* head offset */
	uint64_t	tfdrp_tail;		/* tail offset */
	tfpkt_dma_t	tfdrp_dma;		/* descriptor data */
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

/*
 * Buffers are allocated in advance as a combination of DMA memory and
 * a descriptor chain.  Buffers can be loaned to the networking stack
 * to avoid copying, and this object contains the free routine to pass to
 * desballoc().
 */
typedef struct tfpkt_buf {
	tfpkt_t		*tfb_tfport;
	int		tfb_flags;
	tfpkt_dma_t	tfb_dma;
	list_node_t	tfb_link;
} tfpkt_buf_t;

/*
 * State managed by the tofino packet handler
 */
struct tfpkt {
	kmutex_t	tfp_mutex;
	dev_info_t	*tfp_dip;
	tofino_gen_t	tfp_gen;
	uint_t		tfp_mtu;

	tfpkt_handler_t		*tfp_pkt_hdlr;
	ddi_softint_handle_t	tfp_softint;

	/* DR management */
	tfpkt_dr_t	*tfp_rx_drs;	/* Rx DRs */
	tfpkt_dr_t	*tfp_tx_drs;	/* Tx DRs */
	tfpkt_dr_t	*tfp_fm_drs;	/* Free memory DRs */
	tfpkt_dr_t	*tfp_cmp_drs;	/* Tx completion DRs */

	/* DMA buffer management */
	list_t		tfp_rxbufs_free;	/* unused rx bufs */
	list_t		tfp_rxbufs_pushed;	/* rx bufs in ASIC FM */
	list_t		tfp_rxbufs_loaned;	/* rx bufs loaned to tfport */
	list_t		tfp_txbufs_free;	/* unused tx bufs */
	list_t		tfp_txbufs_pushed;	/* tx bufs on TX DR */
	list_t		tfp_txbufs_loaned;	/* tx bufs loaned to tfport */
	uint_t		tfp_ntxbufs_onloan;	/* # of tx bufs on loan */
	uint_t		tfp_nrxbufs_onloan;	/* # of rx bufs on loan */
	uint_t		tfp_nrxbufs_onloan_max;	/* max bufs we can loan out */
	uint_t		tfp_bufs_capacity;	/* total rx+tx bufs */
	tfpkt_buf_t	*tfp_bufs_mem;		/* all rx+tx bufs */

	/* Internal debugging statistics: */
	uint64_t	tfp_rxfail_excess_loans;
	uint64_t	tfp_rxfail_dma_handle;
	uint64_t	tfp_rxfail_dma_buffer;
	uint64_t	tfp_rxfail_dma_bind;
	uint64_t	tfp_rxfail_chain_undersize;
	uint64_t	tfp_rxfail_no_descriptors;
	uint64_t	tfp_txfail_no_bufs;
	uint64_t	tfp_txfail_no_descriptors;
	uint64_t	tfp_txfail_dma_handle;
	uint64_t	tfp_txfail_dma_bind;
	uint64_t	tfp_txfail_indirect_limit;

	uint64_t	tfp_stat_tx_reclaim;
};


/*
 * Interfaces provided by the packet handler to the core driver
 */
int tfpkt_init(tofino_t *tf);
int tfpkt_fini(tofino_t *tf);

/*
 * Interfaces provided by the packet handler to a mac driver
 */
tfpkt_handler_t *tfpkt_reg_handler(tfpkt_t *, tofino_rx_handler_t,
    tofino_cmp_handler_t, void *);
int tfpkt_unreg_handler(tfpkt_t *, tfpkt_handler_t *);

/*
 * Interfaces provided by the packet handler to a mac driver
 */
typedef void *tofino_pkt_cookie_t;

tfpkt_handler_t *tfpkt_reg_handler(tfpkt_t *, tofino_rx_handler_t,
    tofino_cmp_handler_t, void *);
int tfpkt_unreg_handler(tfpkt_t *, tfpkt_handler_t *);

int tofino_pkt_register(int, tofino_pkt_cookie_t *, void *, tofino_rx_handler_t,
    tofino_cmp_handler_t);
int tofino_pkt_unregister(int, tofino_pkt_cookie_t);

void tofino_rx_done(tofino_pkt_cookie_t, void *, size_t);
void *tofino_tx_alloc(tofino_pkt_cookie_t cookie, size_t sz);
void tofino_tx_free(tofino_pkt_cookie_t cookie, void *addr);
int tofino_tx(tofino_pkt_cookie_t cookie, void *, size_t sz);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TOFINO_PKT_H */
