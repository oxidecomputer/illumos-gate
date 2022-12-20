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

/*
 * Tofino tbus handler
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/sunddi.h>
#include <sys/ethernet.h>
#include <sys/sysmacros.h>
#include <sys/strsun.h>
#include <sys/stdbool.h>

#include <sys/tofino.h>
#include <sys/tofino_regs.h>
#include "tfpkt_impl.h"

static int tfpkt_tbus_debug = 0;

/*
 * Forward references
 */
static int tf_tbus_dr_push(tf_tbus_t *, tf_tbus_dr_t *, uint64_t *);
static int tf_tbus_dr_pull(tf_tbus_t *, tf_tbus_dr_t *, uint64_t *);
static int tf_tbus_push_free_bufs(tf_tbus_t *, int);

static void
tf_tbus_dlog(tf_tbus_t *tbp, const char *fmt, ...)
{
	va_list args;

	if (tfpkt_tbus_debug) {
		va_start(args, fmt);
		vdev_err(tbp->tbp_dip, CE_NOTE, fmt, args);
		va_end(args);
	}
}

static void
tf_tbus_err(tf_tbus_t *tbp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(tbp->tbp_dip, CE_WARN, fmt, args);
	va_end(args);
}

/*
 * This routine frees a DMA buffer and its state, but does not free the
 * tf_tbus_dma_t structure itself.
 */
static void
tf_tbus_dma_free(tf_tbus_dma_t *dmap)
{
	VERIFY3S(ddi_dma_unbind_handle(dmap->tpd_handle), ==, DDI_SUCCESS);
	ddi_dma_mem_free(&dmap->tpd_acchdl);
	ddi_dma_free_handle(&dmap->tpd_handle);
}

/*
 * Free a single tf_tbus_buf_t structure.  If the buffer includes a DMA buffer,
 * that is freed as well.
 */
static void
tf_tbus_free_buf(tf_tbus_buf_t *buf)
{
	VERIFY3U((buf->tfb_flags & TFPORT_BUF_LOANED), ==, 0);

	if (buf->tfb_flags & TFPORT_BUF_DMA_ALLOCED) {
		tf_tbus_dma_free(&buf->tfb_dma);
		buf->tfb_flags &= ~TFPORT_BUF_DMA_ALLOCED;
	}
}

/*
 * Free all of the buffers on a list.  Returns the number of buffers freed.
 */
static int
tf_tbus_free_buf_list(list_t *list)
{
	tf_tbus_buf_t *buf;
	int freed = 0;

	while ((buf = list_remove_head(list)) != NULL) {
		tf_tbus_free_buf(buf);
		freed++;
	}

	return (freed);
}

/*
 * Free all of the buffers allocated by the packet handler
 */
static void
tf_tbus_free_bufs(tf_tbus_t *tbp)
{
	int freed;

	if (tbp->tbp_bufs_mem == NULL)
		return;

	freed = tf_tbus_free_buf_list(&tbp->tbp_rxbufs_free);
	freed += tf_tbus_free_buf_list(&tbp->tbp_rxbufs_pushed);
	freed += tf_tbus_free_buf_list(&tbp->tbp_txbufs_free);
	freed += tf_tbus_free_buf_list(&tbp->tbp_txbufs_pushed);

	if (freed != tbp->tbp_bufs_capacity)
		tf_tbus_err(tbp, "lost track of %d/%d buffers",
		    tbp->tbp_bufs_capacity - freed, tbp->tbp_bufs_capacity);

	kmem_free(tbp->tbp_bufs_mem,
	    sizeof (tf_tbus_buf_t) * tbp->tbp_bufs_capacity);
	tbp->tbp_bufs_mem = NULL;
	tbp->tbp_bufs_capacity = 0;
}

static void
tf_tbus_buf_list_init(list_t *list)
{
	list_create(list, sizeof (tf_tbus_buf_t),
	    offsetof(tf_tbus_buf_t, tfb_link));
}

/*
 * Allocate memory for the buffers used when staging packet data into and out of
 * the ASIC.  Each buffer is the same size and the number of buffers is fixed at
 * build time.  XXX: in the future we could have caches of multiple buffer sizes
 * for transfers.  When passing a buffer to the ASIC for staging rx data we
 * indicate the buffer's size, but there's no indication that it is capable of
 * choosing between different sizes.  The number of buffers is fixed at compile
 * time, but could be made more dynamic.
 */
static int
tf_tbus_alloc_bufs(tf_tbus_t *tbp)
{
	tbp->tbp_bufs_capacity = TFPORT_NET_RX_BUFS + TFPORT_NET_TX_BUFS;
	tbp->tbp_bufs_mem = kmem_zalloc(
	    sizeof (tf_tbus_buf_t) * tbp->tbp_bufs_capacity, KM_SLEEP);
	tf_tbus_buf_list_init(&tbp->tbp_rxbufs_free);
	tf_tbus_buf_list_init(&tbp->tbp_rxbufs_pushed);
	tf_tbus_buf_list_init(&tbp->tbp_rxbufs_loaned);
	tf_tbus_buf_list_init(&tbp->tbp_txbufs_free);
	tf_tbus_buf_list_init(&tbp->tbp_txbufs_pushed);
	tf_tbus_buf_list_init(&tbp->tbp_txbufs_loaned);

	/*
	 * Do not loan more than half of our allocated receive buffers into
	 * the networking stack.
	 */
	tbp->tbp_nrxbufs_onloan_max = TFPORT_NET_RX_BUFS / 2;

	for (uint_t i = 0; i < tbp->tbp_bufs_capacity; i++) {
		tf_tbus_buf_t *buf = &tbp->tbp_bufs_mem[i];
		if (tofino_tbus_dma_alloc(tbp->tbp_tbus_hdl, &buf->tfb_dma,
		    TFPORT_BUF_SIZE, DDI_DMA_STREAMING | DDI_DMA_READ) != 0) {
			goto fail;
		}
		buf->tfb_flags |= TFPORT_BUF_DMA_ALLOCED;
		buf->tfb_tbus = tbp;
		if (i < TFPORT_NET_RX_BUFS)
			list_insert_tail(&tbp->tbp_rxbufs_free, buf);
		else
			list_insert_tail(&tbp->tbp_txbufs_free, buf);
	}

	return (0);

fail:
	tf_tbus_free_bufs(tbp);
	return (ENOMEM);
}

static void
tf_tbus_free_dr(tf_tbus_dr_t *drp)
{
	if (drp->tfdrp_virt_base != 0) {
		tf_tbus_dma_free(&drp->tfdrp_dma);
	}
	drp->tfdrp_virt_base = 0;
	drp->tfdrp_phys_base = 0;
}

/*
 * Free all of the memory allocated to contain and manage the descriptor rings.
 */
static void
tf_tbus_free_drs(tf_tbus_t *tbp)
{
	int i;

	if (tbp->tbp_rx_drs != NULL) {
		for (i = 0; i < TF_PKT_RX_CNT; i++) {
			tf_tbus_free_dr(&tbp->tbp_rx_drs[i]);
		}
		kmem_free(tbp->tbp_rx_drs,
		    sizeof (tf_tbus_dr_t) * TF_PKT_RX_CNT);
	}
	if (tbp->tbp_tx_drs != NULL) {
		for (i = 0; i < TF_PKT_TX_CNT; i++) {
			tf_tbus_free_dr(&tbp->tbp_tx_drs[i]);
		}
		kmem_free(tbp->tbp_tx_drs,
		    sizeof (tf_tbus_dr_t) * TF_PKT_TX_CNT);
	}
	if (tbp->tbp_fm_drs != NULL) {
		for (i = 0; i < TF_PKT_FM_CNT; i++) {
			tf_tbus_free_dr(&tbp->tbp_fm_drs[i]);
		}
		kmem_free(tbp->tbp_fm_drs,
		    sizeof (tf_tbus_dr_t) * TF_PKT_FM_CNT);
	}
	if (tbp->tbp_cmp_drs != NULL) {
		for (i = 0; i < TF_PKT_CMP_CNT; i++) {
			tf_tbus_free_dr(&tbp->tbp_cmp_drs[i]);
		}
		kmem_free(tbp->tbp_cmp_drs,
		    sizeof (tf_tbus_dr_t) * TF_PKT_CMP_CNT);
	}
}

/*
 * Allocate a DMA memory in which to store a single descriptor ring.  Fill in
 * the provided DR management structure.  We calculate the offsets of the
 * different registers used to configure and manage the DR, but do not actually
 * update those registers here.
 */
static int
tf_tbus_alloc_dr(tf_tbus_t *tbp, tf_tbus_dr_t *drp, tf_tbus_dr_type_t dr_type,
    int dr_id, size_t depth)
{
	uint32_t reg_base = 0;
	uint32_t desc_sz = 0;
	size_t ring_sz, total_sz;
	char *prefix = NULL;

	/*
	 * The Tofino registers that are used to configure each descriptor ring
	 * are segregated according to the type of ring.  The addresses and
	 * sizes of those register vary between Tofino generations.  The size of
	 * each descriptor varies depending on the ring, but is consistent
	 * between generations.
	 */
	if (tbp->tbp_gen == TOFINO_G_TF1) {
		switch (dr_type) {
		case TF_PKT_DR_TX:
			reg_base = TF_REG_TBUS_TX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_TX;
			prefix = "tx";
			break;
		case TF_PKT_DR_RX:
			reg_base = TF_REG_TBUS_RX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_RX;
			prefix = "rx";
			break;
		case TF_PKT_DR_FM:
			reg_base = TF_REG_TBUS_FM_BASE;
			desc_sz = TBUS_DR_DESC_SZ_FM;
			prefix = "fm";
			break;
		case TF_PKT_DR_CMP:
			reg_base = TF_REG_TBUS_CMP_BASE;
			desc_sz = TBUS_DR_DESC_SZ_CMP;
			prefix = "cmp";
			break;
		default:
			ASSERT(0);
		}
		reg_base += dr_id * TF_DR_SIZE;
	} else {
		ASSERT(tbp->tbp_gen == TOFINO_G_TF2);
		switch (dr_type) {
		case TF_PKT_DR_TX:
			reg_base = TF2_REG_TBUS_TX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_TX;
			prefix = "tx";
			break;
		case TF_PKT_DR_RX:
			reg_base = TF2_REG_TBUS_RX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_RX;
			prefix = "rx";
			break;
		case TF_PKT_DR_FM:
			reg_base = TF2_REG_TBUS_FM_BASE;
			desc_sz = TBUS_DR_DESC_SZ_FM;
			prefix = "fm";
			break;
		case TF_PKT_DR_CMP:
			reg_base = TF2_REG_TBUS_CMP_BASE;
			desc_sz = TBUS_DR_DESC_SZ_CMP;
			prefix = "cmp";
			break;
		default:
			ASSERT(0);
		}
		reg_base += dr_id * TF2_DR_SIZE;
	}

	/*
	 * The DR size must be a power-of-2 multiple of 64 bytes no larger than
	 * 1MB.
	 */
	ring_sz = depth * desc_sz * sizeof (uint64_t);
	uint64_t fixed = 0;
	for (int top_bit = 19; top_bit >= 6; top_bit--) {
		if (ring_sz & (1 << top_bit)) {
			fixed = 1 << top_bit;
			break;
		}
	}
	ASSERT(fixed > 0);
	if (ring_sz != fixed) {
		ring_sz = fixed;
	}

	/*
	 * Allocate the memory for the ring contents, as well as space at the
	 * end of the ring to store the pushed pointer.
	 *
	 * It's not clear to me why we need to store that pointer after the
	 * descriptors as well as in the tail pointer register.  It appears to
	 * be optional, with a bit in the config register indicating whether
	 * we've opted in or not.  The Intel reference driver opts for it,
	 * without discussing what (if any) advantage it offers, so for now
	 * we'll follow suit.
	 */
	total_sz = ring_sz + sizeof (uint64_t);
	if (tofino_tbus_dma_alloc(tbp->tbp_tbus_hdl, &drp->tfdrp_dma, total_sz,
	    DDI_DMA_STREAMING | DDI_DMA_RDWR) != 0) {
		return (-1);
	}

	(void) snprintf(drp->tfdrp_name, DR_NAME_LEN, "%s_%d", prefix, dr_id);
	mutex_init(&drp->tfdrp_mutex, NULL, MUTEX_DRIVER, NULL);
	drp->tfdrp_reg_base = reg_base;
	drp->tfdrp_type = dr_type;
	drp->tfdrp_id = dr_id;
	drp->tfdrp_phys_base = drp->tfdrp_dma.tpd_cookie.dmac_laddress;
	drp->tfdrp_virt_base = (uint64_t)drp->tfdrp_dma.tpd_addr;
	drp->tfdrp_tail_ptr = (uint64_t *)(drp->tfdrp_virt_base + ring_sz);
	drp->tfdrp_depth = depth;
	drp->tfdrp_desc_size = desc_sz * sizeof (uint64_t);
	drp->tfdrp_ring_size = ring_sz;

	drp->tfdrp_head = 0;
	drp->tfdrp_tail = 0;

	tf_tbus_dlog(tbp, "allocated DR %s.  phys_base: %llx  reg: %lx",
	    drp->tfdrp_name, drp->tfdrp_phys_base, drp->tfdrp_reg_base);

	return (0);
}

/*
 * Allocate memory for all of the descriptor rings and the metadata structures
 * we use to manage them.
 */
static int
tf_tbus_alloc_drs(tf_tbus_t *tbp)
{
	int i;

	tbp->tbp_rx_drs = kmem_zalloc(sizeof (tf_tbus_dr_t) * TF_PKT_RX_CNT,
	    KM_SLEEP);
	tbp->tbp_tx_drs = kmem_zalloc(sizeof (tf_tbus_dr_t) * TF_PKT_TX_CNT,
	    KM_SLEEP);
	tbp->tbp_fm_drs = kmem_zalloc(sizeof (tf_tbus_dr_t) * TF_PKT_FM_CNT,
	    KM_SLEEP);
	tbp->tbp_cmp_drs = kmem_zalloc(sizeof (tf_tbus_dr_t) * TF_PKT_CMP_CNT,
	    KM_SLEEP);

	for (i = 0; i < TF_PKT_RX_CNT; i++) {
		if (tf_tbus_alloc_dr(tbp, &tbp->tbp_rx_drs[i], TF_PKT_DR_RX,
		    i, TF_PKT_RX_DEPTH) != 0) {
			tf_tbus_err(tbp, "failed to alloc rx dr");
			goto fail;
		}
	}
	for (i = 0; i < TF_PKT_TX_CNT; i++) {
		if (tf_tbus_alloc_dr(tbp, &tbp->tbp_tx_drs[i], TF_PKT_DR_TX,
		    i, TF_PKT_TX_DEPTH) != 0) {
			tf_tbus_err(tbp, "failed to alloc tx dr");
			goto fail;
		}
	}
	for (i = 0; i < TF_PKT_FM_CNT; i++) {
		if (tf_tbus_alloc_dr(tbp, &tbp->tbp_fm_drs[i], TF_PKT_DR_FM,
		    i, TF_PKT_FM_DEPTH) != 0) {
			tf_tbus_err(tbp, "failed to alloc fm dr");
			goto fail;
		}
	}
	for (i = 0; i < TF_PKT_CMP_CNT; i++) {
		if (tf_tbus_alloc_dr(tbp, &tbp->tbp_cmp_drs[i], TF_PKT_DR_CMP,
		    i, TF_PKT_CMP_DEPTH) != 0) {
			tf_tbus_err(tbp, "failed to alloc cmp dr");
			goto fail;
		}
	}

	return (0);

fail:
	tf_tbus_free_drs(tbp);
	return (-1);
}

/*
 * Given a virtual address, search for the tf_tbus_buf_t that contains it.
 */
static tf_tbus_buf_t *
tf_tbus_buf_by_va(list_t *list, caddr_t va)
{
	tf_tbus_buf_t *buf;

	for (buf = list_head(list); buf != NULL; buf = list_next(list, buf)) {
		if (buf->tfb_dma.tpd_addr == va) {
			list_remove(list, buf);
			return (buf);
		}
	}
	return (NULL);
}

/*
 * Given a physical address, search for the tf_tbus_buf_t that contains it.
 */
static tf_tbus_buf_t *
tf_tbus_buf_by_pa(list_t *list, uint64_t pa)
{
	tf_tbus_buf_t *buf;

	for (buf = list_head(list); buf != NULL; buf = list_next(list, buf)) {
		if (buf->tfb_dma.tpd_cookie.dmac_laddress == pa) {
			list_remove(list, buf);
			return (buf);
		}
	}
	return (NULL);
}

static tf_tbus_buf_t *
tf_tbus_loaned_buf_by_va(tf_tbus_t *tbp, list_t *list, caddr_t va)
{
	tf_tbus_buf_t *buf = tf_tbus_buf_by_va(list, va);

	if (buf == NULL) {
		tf_tbus_err(tbp, "unrecognized loaned buf: %p", va);
	} else if ((buf->tfb_flags & TFPORT_BUF_LOANED) == 0) {
		tf_tbus_err(tbp, "buf not marked as loaned: %p", va);
	}
	return (buf);
}

/*
 * Mark a tx buffer for loaning to the ASIC, and do the necessary accounting.
 */
static void
tf_tbus_tx_loan(tf_tbus_t *tbp, tf_tbus_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->tbp_mutex));
	buf->tfb_flags |= TFPORT_BUF_LOANED;
	tbp->tbp_ntxbufs_onloan++;
	list_insert_tail(&tbp->tbp_txbufs_loaned, buf);
}

/*
 * Process the return of a tx buffer from the ASIC.
 */
static void
tf_tbus_tx_return(tf_tbus_t *tbp, tf_tbus_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->tbp_mutex));
	buf->tfb_flags &= ~TFPORT_BUF_LOANED;
	ASSERT(tbp->tbp_ntxbufs_onloan > 0);
	tbp->tbp_ntxbufs_onloan--;
}

/*
 * Mark an rx buffer for loaning to the mac framework, and do the necessary
 * accounting.
 */
static void
tf_tbus_rx_loan(tf_tbus_t *tbp, tf_tbus_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->tbp_mutex));
	buf->tfb_flags |= TFPORT_BUF_LOANED;
	tbp->tbp_nrxbufs_onloan++;
	list_insert_tail(&tbp->tbp_rxbufs_loaned, buf);
}

/*
 * Process the return of an rx buffer from the mac framework.
 */
static void
tf_tbus_rx_return(tf_tbus_t *tbp, tf_tbus_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->tbp_mutex));
	buf->tfb_flags &= ~TFPORT_BUF_LOANED;
	ASSERT(tbp->tbp_nrxbufs_onloan > 0);
	tbp->tbp_nrxbufs_onloan--;
}

/*
 * Allocate a transmit-ready buffer capable of holding at least sz bytes.
 *
 * The return value is the virtual address at which the data should be stored,
 * and which must be provided to the transmit routine.
 */
void *
tofino_tbus_tx_alloc(tf_tbus_t *tbp, size_t sz)
{
	dev_info_t *dip = tbp->tbp_dip;
	tf_tbus_buf_t *buf;
	void *va;

	if (sz > TFPORT_BUF_SIZE) {
		dev_err(dip, CE_WARN, "packet too large");
		return (NULL);
	}

	mutex_enter(&tbp->tbp_mutex);
	buf = list_remove_head(&tbp->tbp_txbufs_free);
	if (buf == NULL) {
		tbp->tbp_txfail_no_bufs++;
		va = NULL;
	} else {
		va = buf->tfb_dma.tpd_addr;
		tf_tbus_tx_loan(tbp, buf);
	}
	mutex_exit(&tbp->tbp_mutex);

	return (va);
}

/*
 * Return a transmit buffer to the freelist from whence it came.
 */
void
tofino_tbus_tx_free(tf_tbus_t *tbp, void *addr)
{
	tf_tbus_buf_t *buf;

	mutex_enter(&tbp->tbp_mutex);
	buf = tf_tbus_loaned_buf_by_va(tbp, &tbp->tbp_txbufs_loaned, addr);
	if (buf != NULL) {
		tf_tbus_tx_return(tbp, buf);
		list_insert_tail(&tbp->tbp_txbufs_free, buf);
	} else {
		tf_tbus_err(tbp, "freeing unknown buf %p", addr);
	}

	mutex_exit(&tbp->tbp_mutex);
}

/*
 * Push a single message to the ASIC.
 *
 * On success, that call returns 0 and consumes the provided buffer.  On
 * failure, the call returns -1 and buffer ownership remains with the caller.
 */
int
tofino_tbus_tx(tf_tbus_t *tbp, void *addr, size_t sz)
{
	tf_tbus_buf_t *buf;
	tf_tbus_dr_t *drp = &tbp->tbp_tx_drs[0];
	tf_tbus_dr_tx_t tx_dr;
	int rval = 0;

	mutex_enter(&tbp->tbp_mutex);
	if (tbp->tbp_reset) {
		rval = ENXIO;
	} else if (sz > TFPORT_BUF_SIZE) {
		rval = EINVAL;
	} else {
		buf = tf_tbus_loaned_buf_by_va(tbp, &tbp->tbp_txbufs_loaned,
		    addr);
		if (buf == NULL)  {
			tf_tbus_err(tbp, "sending unknown buf %p", addr);
			rval = EINVAL;
		}
	}
	mutex_exit(&tbp->tbp_mutex);
	if (rval != 0)
		return (rval);

	bzero(&tx_dr, sizeof (tx_dr));
	tx_dr.tx_s = 1;
	tx_dr.tx_e = 1;
	tx_dr.tx_type = TFPRT_TX_DESC_TYPE_PKT;
	tx_dr.tx_size = sz;
	tx_dr.tx_src = buf->tfb_dma.tpd_cookie.dmac_laddress;
	/*
	 * the reference driver sets the dst field to the same address, but has
	 * a comment asking if it's necessary.  Let's find out...
	 */
	tx_dr.tx_msg_id = tx_dr.tx_src;

	rval = tf_tbus_dr_push(tbp, drp, (uint64_t *)&tx_dr);
	mutex_enter(&tbp->tbp_mutex);
	if (rval == 0) {
		tf_tbus_tx_return(tbp, buf);
		list_insert_tail(&tbp->tbp_txbufs_pushed, buf);
	} else  {
		if (rval == ENOSPC) {
			tbp->tbp_txfail_no_descriptors++;
		}
		tf_tbus_tx_loan(tbp, buf);
	}
	mutex_exit(&tbp->tbp_mutex);

	return (rval);
}

/*
 * The packet driver has finished processing the received packet, so we are free
 * to reuse the buffer.
 */
void
tofino_tbus_rx_done(tf_tbus_t *tbp, void *addr, size_t sz)
{
	tf_tbus_buf_t *buf;

	mutex_enter(&tbp->tbp_mutex);
	buf = tf_tbus_loaned_buf_by_va(tbp, &tbp->tbp_rxbufs_loaned, addr);
	if (buf != NULL) {
		tf_tbus_rx_return(tbp, buf);
		list_insert_tail(&tbp->tbp_rxbufs_free, buf);
	}
	mutex_exit(&tbp->tbp_mutex);
}

static void
tf_tbus_process_rx(tf_tbus_t *tbp, tf_tbus_dr_t *drp, tf_tbus_dr_rx_t *rx_dr)
{
	tf_tbus_buf_t *buf;
	int loan = 0;

	mutex_enter(&tbp->tbp_mutex);
	buf = tf_tbus_buf_by_pa(&tbp->tbp_rxbufs_pushed, rx_dr->rx_addr);
	if (buf == NULL) {
		tf_tbus_err(tbp, "unrecognized rx buf: %lx", rx_dr->rx_addr);
		mutex_exit(&tbp->tbp_mutex);
		return;
	}

	if (rx_dr->rx_type != TFPRT_RX_DESC_TYPE_PKT) {
		/* should never happen. */
		tf_tbus_err(tbp, "non-pkt descriptor (%d) on %s",
		    rx_dr->rx_type, drp->tfdrp_name);
	} else if (tbp->tbp_nrxbufs_onloan < tbp->tbp_nrxbufs_onloan_max) {
		tf_tbus_rx_loan(tbp, buf);
		loan = 1;
	} else {
		tbp->tbp_rxfail_excess_loans++;
	}
	if (!loan) {
		list_insert_tail(&tbp->tbp_rxbufs_free, buf);
	}

	mutex_exit(&tbp->tbp_mutex);

	if (loan)
		tfpkt_rx(tbp->tbp_tfp, buf->tfb_dma.tpd_addr, rx_dr->rx_size);
}

static void
tf_tbus_process_cmp(tf_tbus_t *tbp, tf_tbus_dr_t *drp, tf_tbus_dr_cmp_t *cmp_dr)
{
	tf_tbus_buf_t *buf;

	mutex_enter(&tbp->tbp_mutex);
	buf = tf_tbus_buf_by_pa(&tbp->tbp_txbufs_pushed, cmp_dr->cmp_addr);
	if (buf == NULL) {
		tf_tbus_err(tbp, "unrecognized tx buf: %lx", cmp_dr->cmp_addr);
		mutex_exit(&tbp->tbp_mutex);
		return;
	}

	if (cmp_dr->cmp_type != TFPRT_TX_DESC_TYPE_PKT) {
		/* should never happen. */
		tf_tbus_err(tbp, "non-pkt descriptor (%d) on %s",
		    cmp_dr->cmp_type, drp->tfdrp_name);
	}

	list_insert_tail(&tbp->tbp_txbufs_free, buf);
	mutex_exit(&tbp->tbp_mutex);
}

static int
tf_tbus_reg_op(tf_tbus_t *tbp, size_t offset, uint32_t *val, bool rd)
{
	int rval;

	if (rd) {
		rval = tofino_tbus_read_reg(tbp->tbp_tbus_hdl, offset, val);
	} else {
		rval = tofino_tbus_write_reg(tbp->tbp_tbus_hdl, offset, *val);
	}

	if (rval != 0) {
		if (!tbp->tbp_reset)
			tf_tbus_err(tbp, "tofino has been reset");
		tbp->tbp_reset = true;
	}

	return (rval);
}

static int
tf_tbus_dr_read(tf_tbus_t *tbp, tf_tbus_dr_t *drp, size_t offset, uint32_t *val)
{
	return (tf_tbus_reg_op(tbp, drp->tfdrp_reg_base + offset, val, true));
}

static int
tf_tbus_dr_write(tf_tbus_t *tbp, tf_tbus_dr_t *drp, size_t offset,
    uint32_t val)
{
	return (tf_tbus_reg_op(tbp, drp->tfdrp_reg_base + offset, &val, false));
}

static int
tf_tbus_cmp_poll(tf_tbus_t *tbp, int ring)
{
	tf_tbus_dr_t *drp = &tbp->tbp_cmp_drs[ring];
	tf_tbus_dr_cmp_t cmp_dr;
	int processed = 0;

	if (tf_tbus_dr_pull(tbp, drp, (uint64_t *)&cmp_dr) == 0) {
		tf_tbus_process_cmp(tbp, drp, &cmp_dr);
		processed++;
	}

	return (processed);
}

static int
tf_tbus_rx_poll(tf_tbus_t *tbp, int ring)
{
	tf_tbus_dr_t *drp = &tbp->tbp_rx_drs[ring];
	tf_tbus_dr_rx_t rx_dr;
	int processed = 0;

	if (tf_tbus_dr_pull(tbp, drp, (uint64_t *)&rx_dr) == 0) {
		tf_tbus_process_rx(tbp, drp, &rx_dr);
		processed++;
	}

	return (processed);
}

/*
 * Program the ASIC with the location, range, and characteristics of this
 * descriptor ring.
 */
static int
tf_tbus_init_dr(tf_tbus_t *tbp, tf_tbus_dr_t *drp)
{
	uint64_t phys;
	uint32_t ctrl;

	/*
	 * The DR range has to be 64-byte aligned.
	 */
	phys = (drp->tfdrp_phys_base + 63ull) & ~(63ull);

	/* disable DR */
	ctrl = 0;
	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_CTRL, ctrl);

	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_SIZE,
	    drp->tfdrp_ring_size);
	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_BASE_ADDR_LOW,
	    (uint32_t)(phys & 0xFFFFFFFFULL));
	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_BASE_ADDR_HIGH,
	    (uint32_t)(phys >> 32));

	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_LOW,
	    (uint32_t)((phys + drp->tfdrp_ring_size) & 0xFFFFFFFFULL));
	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_HIGH,
	    (uint32_t)((phys + drp->tfdrp_ring_size) >> 32));

	*drp->tfdrp_tail_ptr = 0;
	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_HEAD_PTR, 0);
	(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_TAIL_PTR, 0);

	/* Tofino2 has two additional registers */
	if (tbp->tbp_gen == TOFINO_G_TF2) {
		(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_EMPTY_INT_TIME,
		    0);
		(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_EMPTY_INT_CNT,
		    0);
	}

	switch (drp->tfdrp_type) {
		case TF_PKT_DR_TX:
		case TF_PKT_DR_FM:
			ctrl = TBUS_DR_CTRL_HEAD_PTR_MODE;
			break;
		case TF_PKT_DR_RX:
			(void) tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_DATA_TIMEOUT, 1);
			/* fallthru */
		case TF_PKT_DR_CMP:
			ctrl = TBUS_DR_CTRL_TAIL_PTR_MODE;
			break;
	}

	/* enable DR */
	ctrl |= TBUS_DR_CTRL_ENABLE;
	return (tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_CTRL, ctrl));
}

/*
 * Push the configuration info for all of the DRs into the ASIC
 */
static int
tf_tbus_init_drs(tf_tbus_t *tbp)
{
	int i, rval;

	rval = 0;
	for (i = 0; (rval == 0) && (i < TF_PKT_FM_CNT); i++)
		rval = tf_tbus_init_dr(tbp, &tbp->tbp_fm_drs[i]);
	for (i = 0; (rval == 0) && (i < TF_PKT_RX_CNT); i++)
		rval = tf_tbus_init_dr(tbp, &tbp->tbp_rx_drs[i]);
	for (i = 0; (rval == 0) && (i < TF_PKT_TX_CNT); i++)
		rval = tf_tbus_init_dr(tbp, &tbp->tbp_tx_drs[i]);
	for (i = 0; (rval == 0) && (i < TF_PKT_CMP_CNT); i++)
		rval = tf_tbus_init_dr(tbp, &tbp->tbp_cmp_drs[i]);

	return (rval);
}

/*
 * Refresh our in-core copy of the tail pointer from the DR's config register.
 */
static int
tf_tbus_dr_refresh_tail(tf_tbus_t *tbp, tf_tbus_dr_t *drp)
{
	uint32_t tail;
	int rval;

	rval = tf_tbus_dr_read(tbp, drp, TBUS_DR_OFF_TAIL_PTR, &tail);
	if (rval == 0)
		drp->tfdrp_tail = tail;

	return (rval);
}

/*
 * Refresh our in-core copy of the head pointer from the DR's config register.
 */
static int
tf_tbus_dr_refresh_head(tf_tbus_t *tbp, tf_tbus_dr_t *drp)
{
	uint32_t head;
	int rval;

	rval = tf_tbus_dr_read(tbp, drp, TBUS_DR_OFF_HEAD_PTR, &head);
	if (rval == 0)
		drp->tfdrp_head = head;
	return (rval);
}

#define	DR_PTR_WRAP_BIT (1 << 20)
#define	DR_PTR_GET_WRAP_BIT(p) ((p) & DR_PTR_WRAP_BIT)
#define	DR_PTR_GET_BODY(p) ((p) & (DR_PTR_WRAP_BIT - 1))

static int
tf_tbus_dr_full(tf_tbus_dr_t *drp)
{
	uint64_t head_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_head);
	uint64_t tail_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_tail);
	uint64_t head = DR_PTR_GET_BODY(drp->tfdrp_head);
	uint64_t tail = DR_PTR_GET_BODY(drp->tfdrp_tail);

	ASSERT(mutex_owned(&drp->tfdrp_mutex));

	return ((head == tail) && (head_wrap_bit != tail_wrap_bit));
}

static int
tf_tbus_dr_empty(tf_tbus_dr_t *drp)
{
	ASSERT(mutex_owned(&drp->tfdrp_mutex));
	return (drp->tfdrp_head == drp->tfdrp_tail);
}

/*
 * If the ring isn't full, advance the tail pointer to the next empty slot.
 * Return 0 if it advances, -1 if it doesn't.
 */
static int
tf_tbus_dr_advance_tail(tf_tbus_dr_t *drp)
{
	uint64_t tail, tail_wrap_bit;

	ASSERT(mutex_owned(&drp->tfdrp_mutex));
	if (tf_tbus_dr_full(drp)) {
		return (-1);
	}

	tail_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_tail);
	tail = DR_PTR_GET_BODY(drp->tfdrp_tail);
	tail += drp->tfdrp_desc_size;
	if (tail == drp->tfdrp_ring_size) {
		tail = 0;
		tail_wrap_bit ^= DR_PTR_WRAP_BIT;
	}

	drp->tfdrp_tail = tail | tail_wrap_bit;
	return (0);
}

/*
 * If the ring is non-empty, advance the head pointer to the next descriptor.
 * Return 0 if it advances, -1 if it doesn't.
 */
static int
tf_tbus_dr_advance_head(tf_tbus_dr_t *drp)
{
	uint64_t head, head_wrap_bit;

	ASSERT(mutex_owned(&drp->tfdrp_mutex));
	if (tf_tbus_dr_empty(drp)) {
		return (-1);
	}

	head_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_head);
	head = DR_PTR_GET_BODY(drp->tfdrp_head);
	head += drp->tfdrp_desc_size;
	if (head == drp->tfdrp_ring_size) {
		head = 0;
		head_wrap_bit ^= DR_PTR_WRAP_BIT;
	}
	drp->tfdrp_head = head | head_wrap_bit;
	return (0);
}

/*
 * Pull a single descriptor off the head of a ring.
 * Returns 0 if it successfully pushes a descriptor, ENOENT if the ring is
 * empty, and ENXIO if we detect that the rings have been reset.
 */
static int
tf_tbus_dr_pull(tf_tbus_t *tbp, tf_tbus_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, head;
	int rval;

	mutex_enter(&drp->tfdrp_mutex);
	if (tbp->tbp_reset || tf_tbus_dr_refresh_tail(tbp, drp) != 0) {
		mutex_exit(&drp->tfdrp_mutex);
		return (ENXIO);
	}

	if (tf_tbus_dr_empty(drp)) {
		mutex_exit(&drp->tfdrp_mutex);
		return (ENOENT);
	}

	head = DR_PTR_GET_BODY(drp->tfdrp_head);
	slot = (uint64_t *)(drp->tfdrp_virt_base + head);

	if (tfpkt_tbus_debug > 1) {
		uint64_t offset = DR_PTR_GET_BODY(drp->tfdrp_head);
		uint64_t wrap = DR_PTR_GET_WRAP_BIT(drp->tfdrp_head) != 0;
		int idx = offset / drp->tfdrp_desc_size;

		tf_tbus_dlog(tbp, "pulling from %s at %ld (wrap: %ld %d/%ld)",
		    drp->tfdrp_name, drp->tfdrp_head, wrap, idx,
		    drp->tfdrp_depth);
	}

	for (int i = 0; i < (drp->tfdrp_desc_size >> 3); i++)
		desc[i] = slot[i];

	(void) tf_tbus_dr_advance_head(drp);
	rval = tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_HEAD_PTR,
	    drp->tfdrp_head);
	mutex_exit(&drp->tfdrp_mutex);

	return (rval);
}

/*
 * Push a single descriptor onto the tail of a ring.
 * Returns 0 if it successfully pushes a descriptor, ENOSPC if the ring is full,
 * and ENXIO if we detect that the rings have been reset.
 */
static int
tf_tbus_dr_push(tf_tbus_t *tbp, tf_tbus_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, tail;
	int rval;

	mutex_enter(&drp->tfdrp_mutex);
	if (tbp->tbp_reset || tf_tbus_dr_refresh_head(tbp, drp) != 0) {
		mutex_exit(&drp->tfdrp_mutex);
		return (ENXIO);
	}

	if (tf_tbus_dr_full(drp)) {
		mutex_exit(&drp->tfdrp_mutex);
		return (ENOSPC);
	}
	if (tfpkt_tbus_debug > 1) {
		uint64_t offset = DR_PTR_GET_BODY(drp->tfdrp_tail);
		uint64_t wrap = DR_PTR_GET_WRAP_BIT(drp->tfdrp_tail) != 0;
		int idx = offset / drp->tfdrp_desc_size;

		tf_tbus_dlog(tbp, "pushing to %s at %ld (wrap: %ld %d/%ld)",
		    drp->tfdrp_name, drp->tfdrp_tail, wrap, idx,
		    drp->tfdrp_depth);
	}

	tail = DR_PTR_GET_BODY(drp->tfdrp_tail);
	slot = (uint64_t *)(drp->tfdrp_virt_base + tail);
	for (int i = 0; i < (drp->tfdrp_desc_size >> 3); i++)
		slot[i] = desc[i];

	(void) tf_tbus_dr_advance_tail(drp);
	tail = DR_PTR_GET_BODY(drp->tfdrp_tail);
	*drp->tfdrp_tail_ptr = tail;
	rval = tf_tbus_dr_write(tbp, drp, TBUS_DR_OFF_TAIL_PTR,
	    drp->tfdrp_tail);
	mutex_exit(&drp->tfdrp_mutex);

	return (rval);
}

/*
 * Push a free DMA buffer onto a free_memory descriptor ring.
 */
static int
tf_tbus_push_fm(tf_tbus_t *tbp, tf_tbus_dr_t *drp, uint64_t addr, uint64_t size)
{
	uint64_t descriptor;
	uint64_t bucket = 0;

	/*
	 * The DMA address must be 256-byte aligned, as the lower 8 bits are
	 * used to encode the buffer size.
	 */
	if ((addr & 0xff) != 0) {
		return (EINVAL);
	}

	/*
	 * From the Intel source, it appears that this is the maxmimum DMA size.
	 * Presumably this is the sort of detail they would put in their
	 * documentation, should they ever provide any.
	 */
	if (size > 32768) {
		return (EINVAL);
	}
	size >>= 9;
	while (size != 0) {
		bucket++;
		size >>= 1;
	}
	descriptor = (addr & ~(0xff)) | (bucket & 0xf);

	return (tf_tbus_dr_push(tbp, drp, &descriptor));
}

/*
 * Push all free receive buffers onto the free_memory DR until the ring is full,
 * or we run out of buffers.
 */
static int
tf_tbus_push_free_bufs(tf_tbus_t *tbp, int ring)
{
	int rval = 0;
	uint64_t dma_addr;
	tf_tbus_dr_t *drp = &tbp->tbp_fm_drs[ring];
	tf_tbus_buf_t *buf, *next;
	int cnt = 0;

	mutex_enter(&tbp->tbp_mutex);
	for (buf = list_head(&tbp->tbp_rxbufs_free); buf != NULL; buf = next) {
		next = list_next(&tbp->tbp_rxbufs_free, buf);
		dma_addr = buf->tfb_dma.tpd_cookie.dmac_laddress;
		rval = tf_tbus_push_fm(tbp, drp, dma_addr, TFPORT_BUF_SIZE);
		if (rval != 0) {
			/*
			 * ENOSPC is an indication that we've pushed as
			 * many buffers as the ASIC can handle.  It means we
			 * should stop trying to push more, but that we
			 * shouldn't return an error to the caller.
			 */
			if (rval == ENOSPC)
				rval = 0;
			break;
		}
		list_remove(&tbp->tbp_rxbufs_free, buf);
		list_insert_tail(&tbp->tbp_rxbufs_pushed, buf);
		cnt++;
	}
	mutex_exit(&tbp->tbp_mutex);
	dev_err(tbp->tbp_dip, CE_NOTE, "pushed %d rx bufs to the ASIC", cnt);

	return (rval);
}

/*
 * Setup the tbus control register to enable the pci network port
 */
static int 
tf_tbus_port_init(tf_tbus_t *tbp, dev_info_t *tfp_dip)
{
	tf_tbus_hdl_t hdl = tbp->tbp_tbus_hdl;
	tf_tbus_ctrl_t ctrl;
	uint32_t reg;
	uint32_t *ctrlp = (uint32_t *)&ctrl;
	int rval;

	ASSERT(tbp->tbp_gen == TOFINO_G_TF1 || tbp->tbp_gen == TOFINO_G_TF2);
	if (tbp->tbp_gen == TOFINO_G_TF1) {
		reg = TF_REG_TBUS_CTRL;
	} else {
		reg = TF2_REG_TBUS_CTRL;
	}
	if ((rval = tofino_tbus_read_reg(hdl, reg, ctrlp)) != 0)
		return (rval);

	ctrl.tftc_pfc_fm = 0x03;
	ctrl.tftc_pfc_rx = 0x03;
	ctrl.tftc_port_alive = 1;
	ctrl.tftc_rx_en = 1;
	ctrl.tftc_ecc_dec_dis = 0;
	ctrl.tftc_crcchk_dis = 1;
	ctrl.tftc_crcrmv_dis = 0;
	if (tbp->tbp_gen != TOFINO_G_TF1)
		ctrl.tftc_rx_channel_offset = 0;

	return (tofino_tbus_write_reg(hdl, reg, *ctrlp));
}

static uint_t
tf_tbus_intr(caddr_t arg1, caddr_t arg2)
{
	tf_tbus_t *tbp = (tf_tbus_t *)arg1;
	int processed = 1;

	while (!tbp->tbp_reset && (processed > 0)) {
		processed = 0;
		for (int i = 0; i < TF_PKT_RX_CNT; i++) {
			if (tf_tbus_rx_poll(tbp, i) > 0) {
				processed++;
				if (tf_tbus_push_free_bufs(tbp, i))
					break;
			}
		}

		for (int i = 0; i < TF_PKT_CMP_CNT; i++) {
			if (tf_tbus_cmp_poll(tbp, i)) {
				processed++;
			}
		}
	}

	return (DDI_INTR_CLAIMED);
}

void
tf_tbus_fini(tfpkt_t *tfp)
{
	tf_tbus_t *tbp = tfp->tfp_tbus_state;

	if (tfp->tfp_runstate == TFPKT_RUNSTATE_UNINITIALIZED)
		return;

	ASSERT(tbp != NULL);
	if (tbp->tbp_tbus_hdl != NULL) {
		VERIFY0(tofino_tbus_unregister_softint(tbp->tbp_tbus_hdl,
		    tbp->tbp_softint));
		VERIFY0(tofino_tbus_unregister(tbp->tbp_tbus_hdl));
	}
	if (tbp->tbp_softint != NULL) {
		VERIFY3S(ddi_intr_remove_softint(tbp->tbp_softint), ==,
		    DDI_SUCCESS);
	}

	tf_tbus_free_bufs(tbp);
	tf_tbus_free_drs(tbp);
	mutex_destroy(&tbp->tbp_mutex);
	kmem_free(tbp, sizeof (*tbp));

	tfp->tfp_tbus_state = NULL;
	tfp->tfp_runstate = TFPKT_RUNSTATE_UNINITIALIZED;
}

int
tf_tbus_init(tfpkt_t *tfp)
{
	dev_info_t *tfp_dip = tfp->tfp_dip;
	tf_tbus_t *tbp;
	tf_tbus_hdl_t hdl;
	int err;

	ASSERT(tfp->tfp_runstate == TFPKT_RUNSTATE_UNINITIALIZED);

	tbp = kmem_zalloc(sizeof (*tbp), KM_SLEEP);
	mutex_init(&tbp->tbp_mutex, NULL, MUTEX_DRIVER, NULL);
	tbp->tbp_reset = false;
	tbp->tbp_dip = tfp_dip;
	tbp->tbp_tfp = tfp;
	tfp->tfp_tbus_state = tbp;
	tfp->tfp_runstate = TFPKT_RUNSTATE_STOPPED;

	if ((err = tofino_tbus_register(&hdl)) != 0) {
		if (err == EBUSY) {
			tf_tbus_err(tbp, "tofino tbus already in use");
		} else if (err == ENXIO) {
			/* The driver was loaded but not attached. */
			tf_tbus_err(tbp, "tofino driver offline");
		} else if (err == EAGAIN) {
			/*
			 * The userspace daemon hasn't yet initialized the
			 * ASIC
			 */
			tf_tbus_err(tbp, "tofino asic not ready");
		} else {
			tf_tbus_err(tbp, "tofino_tbus_register failed: %d",
			    err);
		}
		goto fail;
	}

	tbp->tbp_tbus_hdl = hdl;
	tbp->tbp_gen = tofino_get_generation(hdl);

	if ((err = tf_tbus_alloc_bufs(tbp)) != 0) {
		tf_tbus_err(tbp, "failed to allocate buffers");
	} else if ((err = tf_tbus_alloc_drs(tbp)) != 0) {
		tf_tbus_err(tbp, "failed to allocate drs");
	} else if ((err = tf_tbus_init_drs(tbp)) != 0) {
		tf_tbus_err(tbp, "failed to init drs");
	}
	if (err != 0)
		goto fail;

	tf_tbus_port_init(tbp, tfp_dip);

	err = ddi_intr_add_softint(tfp_dip, &tbp->tbp_softint,
	    DDI_INTR_SOFTPRI_DEFAULT, tf_tbus_intr, tbp);
	if (err != 0) {
		dev_err(tfp_dip, CE_WARN, "failed to allocate softint");
		goto fail;
	}
	if ((err = tofino_tbus_register_softint(hdl, tbp->tbp_softint)) != 0) {
		dev_err(tfp_dip, CE_WARN, "failed to register softint");
		VERIFY0(tofino_tbus_unregister(hdl));
	}

	for (int i = 0; i < TF_PKT_RX_CNT; i++)
		if ((err = tf_tbus_push_free_bufs(tbp, i)) != 0)
			goto fail;

	return (0);

fail:
	tf_tbus_fini(tfp);
	tfp->tfp_tbus_state = NULL;
	return (err);
}
