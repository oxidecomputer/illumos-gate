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

int tfpkt_tbus_debug = 0;

/*
 * Forward references
 */
static int tfpkt_dr_push(tfpkt_tbus_t *, tfpkt_dr_t *, uint64_t *);
static int tfpkt_dr_pull(tfpkt_tbus_t *, tfpkt_dr_t *, uint64_t *);
static int tfpkt_tbus_push_free_bufs(tfpkt_tbus_t *, int);

static void
tfpkt_tbus_dlog(tfpkt_tbus_t *tbp, const char *fmt, ...)
{
	va_list args;

	if (tfpkt_tbus_debug) {
		va_start(args, fmt);
		vdev_err(tbp->ttb_dip, CE_NOTE, fmt, args);
		va_end(args);
	}
}

static void
tfpkt_tbus_err(tfpkt_tbus_t *tbp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(tbp->ttb_dip, CE_WARN, fmt, args);
	va_end(args);
}

tfpkt_tbus_t *
tfpkt_tbus_hold(tfpkt_t *tfp)
{
	tfpkt_tbus_t *rval = NULL;

	mutex_enter(&tfp->tfp_tbus_mutex);
	if (tfp->tfp_tbus_state == TFPKT_TBUS_ACTIVE) {
		rval = tfp->tfp_tbus_data;
		tfp->tfp_tbus_refcnt++;
	}
	mutex_exit(&tfp->tfp_tbus_mutex);

	return (rval);
}

void
tfpkt_tbus_release(tfpkt_t *tfp)
{
	mutex_enter(&tfp->tfp_tbus_mutex);
	ASSERT(tfp->tfp_tbus_refcnt > 0);
	tfp->tfp_tbus_refcnt--;

	/*
	 * If the refcnt drops to 0 when we're in a state in which someone might
	 * care, wake 'em up.
	 */
	if ((tfp->tfp_tbus_refcnt == 0) &&
	    (tfp->tfp_tbus_state != TFPKT_TBUS_ACTIVE))
		cv_broadcast(&tfp->tfp_tbus_cv);

	mutex_exit(&tfp->tfp_tbus_mutex);
}

/*
 * This routine frees a DMA buffer and its state, but does not free the
 * tf_tbus_dma_t structure itself.
 */
static void
tfpkt_tbus_dma_free(tf_tbus_dma_t *dmap)
{
	VERIFY3S(ddi_dma_unbind_handle(dmap->tpd_handle), ==, DDI_SUCCESS);
	ddi_dma_mem_free(&dmap->tpd_acchdl);
	ddi_dma_free_handle(&dmap->tpd_handle);
}

/*
 * Free a single tfpkt_buf_t structure.  If the buffer includes a DMA
 * buffer, that is freed as well.
 */
static void
tfpkt_tbus_free_buf(tfpkt_buf_t *buf)
{
	VERIFY3U((buf->tfb_flags & TFPKT_BUF_LOANED), ==, 0);

	if (buf->tfb_flags & TFPKT_BUF_DMA_ALLOCED) {
		tfpkt_tbus_dma_free(&buf->tfb_dma);
		buf->tfb_flags &= ~TFPKT_BUF_DMA_ALLOCED;
	}
}

/*
 * Free all of the buffers on a list.  Returns the number of buffers freed.
 */
static int
tfpkt_tbus_free_buf_list(list_t *list)
{
	tfpkt_buf_t *buf;
	int freed = 0;

	while ((buf = list_remove_head(list)) != NULL) {
		tfpkt_tbus_free_buf(buf);
		freed++;
	}

	return (freed);
}

/*
 * Free all of the buffers allocated by the packet handler
 */
static void
tfpkt_tbus_free_bufs(tfpkt_tbus_t *tbp)
{
	int freed;

	if (tbp->ttb_bufs_mem == NULL)
		return;

	freed = tfpkt_tbus_free_buf_list(&tbp->ttb_rxbufs_free);
	freed += tfpkt_tbus_free_buf_list(&tbp->ttb_rxbufs_pushed);
	freed += tfpkt_tbus_free_buf_list(&tbp->ttb_txbufs_free);
	freed += tfpkt_tbus_free_buf_list(&tbp->ttb_txbufs_pushed);

	if (freed != tbp->ttb_bufs_capacity)
		tfpkt_tbus_err(tbp, "lost track of %d/%d buffers",
		    tbp->ttb_bufs_capacity - freed, tbp->ttb_bufs_capacity);

	kmem_free(tbp->ttb_bufs_mem,
	    sizeof (tfpkt_buf_t) * tbp->ttb_bufs_capacity);
	tbp->ttb_bufs_mem = NULL;
	tbp->ttb_bufs_capacity = 0;
}

static void
tfpkt_tbus_buf_list_init(list_t *list)
{
	list_create(list, sizeof (tfpkt_buf_t),
	    offsetof(tfpkt_buf_t, tfb_link));
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
tfpkt_tbus_alloc_bufs(tfpkt_tbus_t *tbp)
{
	tbp->ttb_bufs_capacity = TFPKT_NET_RX_BUFS + TFPKT_NET_TX_BUFS;
	tbp->ttb_bufs_mem = kmem_zalloc(
	    sizeof (tfpkt_buf_t) * tbp->ttb_bufs_capacity, KM_SLEEP);
	tfpkt_tbus_buf_list_init(&tbp->ttb_rxbufs_free);
	tfpkt_tbus_buf_list_init(&tbp->ttb_rxbufs_pushed);
	tfpkt_tbus_buf_list_init(&tbp->ttb_rxbufs_loaned);
	tfpkt_tbus_buf_list_init(&tbp->ttb_txbufs_free);
	tfpkt_tbus_buf_list_init(&tbp->ttb_txbufs_pushed);
	tfpkt_tbus_buf_list_init(&tbp->ttb_txbufs_loaned);

	/*
	 * Do not loan more than half of our allocated receive buffers into
	 * the networking stack.
	 */
	tbp->ttb_nrxbufs_onloan_max = TFPKT_NET_RX_BUFS / 2;

	for (uint_t i = 0; i < tbp->ttb_bufs_capacity; i++) {
		tfpkt_buf_t *buf = &tbp->ttb_bufs_mem[i];
		if (tofino_tbus_dma_alloc(tbp->ttb_tbus_hdl, &buf->tfb_dma,
		    TFPKT_BUF_SIZE, DDI_DMA_STREAMING | DDI_DMA_READ) != 0) {
			goto fail;
		}
		buf->tfb_flags |= TFPKT_BUF_DMA_ALLOCED;
		buf->tfb_tbus = tbp;
		if (i < TFPKT_NET_RX_BUFS)
			list_insert_tail(&tbp->ttb_rxbufs_free, buf);
		else
			list_insert_tail(&tbp->ttb_txbufs_free, buf);
	}

	return (0);

fail:
	tfpkt_tbus_free_bufs(tbp);
	return (ENOMEM);
}

static void
tfpkt_tbus_free_dr(tfpkt_dr_t *drp)
{
	if (drp->tdr_virt_base != 0) {
		tfpkt_tbus_dma_free(&drp->tdr_dma);
	}
	drp->tdr_virt_base = 0;
	drp->tdr_phys_base = 0;
}

/*
 * Free all of the memory allocated to contain and manage the descriptor rings.
 */
static void
tfpkt_tbus_free_drs(tfpkt_tbus_t *tbp)
{
	int i;

	if (tbp->ttb_rx_drs != NULL) {
		for (i = 0; i < TFPKT_RX_CNT; i++) {
			tfpkt_tbus_free_dr(&tbp->ttb_rx_drs[i]);
		}
		kmem_free(tbp->ttb_rx_drs,
		    sizeof (tfpkt_dr_t) * TFPKT_RX_CNT);
	}
	if (tbp->ttb_tx_drs != NULL) {
		for (i = 0; i < TFPKT_TX_CNT; i++) {
			tfpkt_tbus_free_dr(&tbp->ttb_tx_drs[i]);
		}
		kmem_free(tbp->ttb_tx_drs,
		    sizeof (tfpkt_dr_t) * TFPKT_TX_CNT);
	}
	if (tbp->ttb_fm_drs != NULL) {
		for (i = 0; i < TFPKT_FM_CNT; i++) {
			tfpkt_tbus_free_dr(&tbp->ttb_fm_drs[i]);
		}
		kmem_free(tbp->ttb_fm_drs,
		    sizeof (tfpkt_dr_t) * TFPKT_FM_CNT);
	}
	if (tbp->ttb_cmp_drs != NULL) {
		for (i = 0; i < TFPKT_CMP_CNT; i++) {
			tfpkt_tbus_free_dr(&tbp->ttb_cmp_drs[i]);
		}
		kmem_free(tbp->ttb_cmp_drs,
		    sizeof (tfpkt_dr_t) * TFPKT_CMP_CNT);
	}
}

/*
 * Allocate DMA memory in which to store a single descriptor ring.  Fill in
 * the provided DR management structure.  We calculate the offsets of the
 * different registers used to configure and manage the DR, but do not actually
 * update those registers here.
 */
static int
tfpkt_tbus_alloc_dr(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp,
    tfpkt_dr_type_t dr_type, int dr_id, size_t depth)
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
	if (tbp->ttb_gen == TOFINO_G_TF1) {
		switch (dr_type) {
		case TFPKT_DR_TX:
			reg_base = TF_REG_TBUS_TX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_TX;
			prefix = "tx";
			break;
		case TFPKT_DR_RX:
			reg_base = TF_REG_TBUS_RX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_RX;
			prefix = "rx";
			break;
		case TFPKT_DR_FM:
			reg_base = TF_REG_TBUS_FM_BASE;
			desc_sz = TBUS_DR_DESC_SZ_FM;
			prefix = "fm";
			break;
		case TFPKT_DR_CMP:
			reg_base = TF_REG_TBUS_CMP_BASE;
			desc_sz = TBUS_DR_DESC_SZ_CMP;
			prefix = "cmp";
			break;
		default:
			ASSERT(0);
		}
		reg_base += dr_id * TF_DR_SIZE;
	} else {
		ASSERT(tbp->ttb_gen == TOFINO_G_TF2);
		switch (dr_type) {
		case TFPKT_DR_TX:
			reg_base = TF2_REG_TBUS_TX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_TX;
			prefix = "tx";
			break;
		case TFPKT_DR_RX:
			reg_base = TF2_REG_TBUS_RX_BASE;
			desc_sz = TBUS_DR_DESC_SZ_RX;
			prefix = "rx";
			break;
		case TFPKT_DR_FM:
			reg_base = TF2_REG_TBUS_FM_BASE;
			desc_sz = TBUS_DR_DESC_SZ_FM;
			prefix = "fm";
			break;
		case TFPKT_DR_CMP:
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
	if (tofino_tbus_dma_alloc(tbp->ttb_tbus_hdl, &drp->tdr_dma, total_sz,
	    DDI_DMA_STREAMING | DDI_DMA_RDWR) != 0) {
		return (-1);
	}

	(void) snprintf(drp->tdr_name, sizeof (drp->tdr_name), "%s_%d",
	    prefix, dr_id);
	mutex_init(&drp->tdr_mutex, NULL, MUTEX_DRIVER, NULL);
	drp->tdr_reg_base = reg_base;
	drp->tdr_type = dr_type;
	drp->tdr_id = dr_id;
	drp->tdr_phys_base = drp->tdr_dma.tpd_cookie.dmac_laddress;
	drp->tdr_virt_base = (uint64_t)drp->tdr_dma.tpd_addr;
	drp->tdr_tail_ptr = (uint64_t *)(drp->tdr_virt_base + ring_sz);
	drp->tdr_depth = depth;
	drp->tdr_desc_size = desc_sz * sizeof (uint64_t);
	drp->tdr_ring_size = ring_sz;

	drp->tdr_head = 0;
	drp->tdr_tail = 0;

	tfpkt_tbus_dlog(tbp, "allocated DR %s.  phys_base: %llx  reg: %lx",
	    drp->tdr_name, drp->tdr_phys_base, drp->tdr_reg_base);

	return (0);
}

/*
 * Allocate memory for all of the descriptor rings and the metadata structures
 * we use to manage them.
 */
static int
tfpkt_tbus_alloc_drs(tfpkt_tbus_t *tbp)
{
	int i;

	tbp->ttb_rx_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TFPKT_RX_CNT,
	    KM_SLEEP);
	tbp->ttb_tx_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TFPKT_TX_CNT,
	    KM_SLEEP);
	tbp->ttb_fm_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TFPKT_FM_CNT,
	    KM_SLEEP);
	tbp->ttb_cmp_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TFPKT_CMP_CNT,
	    KM_SLEEP);

	for (i = 0; i < TFPKT_RX_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_rx_drs[i], TFPKT_DR_RX,
		    i, TFPKT_RX_DEPTH) != 0) {
			tfpkt_tbus_err(tbp, "failed to alloc rx dr");
			goto fail;
		}
	}
	for (i = 0; i < TFPKT_TX_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_tx_drs[i], TFPKT_DR_TX,
		    i, TFPKT_TX_DEPTH) != 0) {
			tfpkt_tbus_err(tbp, "failed to alloc tx dr");
			goto fail;
		}
	}
	for (i = 0; i < TFPKT_FM_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_fm_drs[i], TFPKT_DR_FM,
		    i, TFPKT_FM_DEPTH) != 0) {
			tfpkt_tbus_err(tbp, "failed to alloc fm dr");
			goto fail;
		}
	}
	for (i = 0; i < TFPKT_CMP_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_cmp_drs[i], TFPKT_DR_CMP,
		    i, TFPKT_CMP_DEPTH) != 0) {
			tfpkt_tbus_err(tbp, "failed to alloc cmp dr");
			goto fail;
		}
	}

	return (0);

fail:
	tfpkt_tbus_free_drs(tbp);
	return (-1);
}

/*
 * Given a virtual address, search for the tfpkt_buf_t that contains it.
 */
static tfpkt_buf_t *
tfpkt_tbus_buf_by_va(list_t *list, caddr_t va)
{
	tfpkt_buf_t *buf;

	for (buf = list_head(list); buf != NULL; buf = list_next(list, buf)) {
		if (buf->tfb_dma.tpd_addr == va) {
			list_remove(list, buf);
			return (buf);
		}
	}
	return (NULL);
}

/*
 * Given a physical address, search for the tfpkt_buf_t that contains it.
 */
static tfpkt_buf_t *
tfpkt_tbus_buf_by_pa(list_t *list, uint64_t pa)
{
	tfpkt_buf_t *buf;

	for (buf = list_head(list); buf != NULL; buf = list_next(list, buf)) {
		if (buf->tfb_dma.tpd_cookie.dmac_laddress == pa) {
			list_remove(list, buf);
			return (buf);
		}
	}
	return (NULL);
}

static tfpkt_buf_t *
tfpkt_tbus_loaned_buf_by_va(tfpkt_tbus_t *tbp, list_t *list, caddr_t va)
{
	tfpkt_buf_t *buf = tfpkt_tbus_buf_by_va(list, va);

	if (buf == NULL) {
		tfpkt_tbus_err(tbp, "unrecognized loaned buf: %p", va);
	} else if ((buf->tfb_flags & TFPKT_BUF_LOANED) == 0) {
		tfpkt_tbus_err(tbp, "buf not marked as loaned: %p", va);
	}
	return (buf);
}

/*
 * Mark a tx buffer for loaning to a client, and do the necessary accounting.
 */
static void
tfpkt_tbus_tx_loan(tfpkt_tbus_t *tbp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->ttb_mutex));
	buf->tfb_flags |= TFPKT_BUF_LOANED;
	tbp->ttb_ntxbufs_onloan++;
	list_insert_tail(&tbp->ttb_txbufs_loaned, buf);
}

/*
 * Process the return of a tx buffer from a client.
 */
static void
tfpkt_tbus_tx_return(tfpkt_tbus_t *tbp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->ttb_mutex));
	buf->tfb_flags &= ~TFPKT_BUF_LOANED;
	ASSERT(tbp->ttb_ntxbufs_onloan > 0);
	tbp->ttb_ntxbufs_onloan--;
}

/*
 * Mark an rx buffer for loaning to the mac framework, and do the necessary
 * accounting.
 */
static void
tfpkt_tbus_rx_loan(tfpkt_tbus_t *tbp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->ttb_mutex));
	buf->tfb_flags |= TFPKT_BUF_LOANED;
	tbp->ttb_nrxbufs_onloan++;
	list_insert_tail(&tbp->ttb_rxbufs_loaned, buf);
}

/*
 * Process the return of an rx buffer from the mac framework.
 */
static void
tfpkt_tbus_rx_return(tfpkt_tbus_t *tbp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tbp->ttb_mutex));
	buf->tfb_flags &= ~TFPKT_BUF_LOANED;
	ASSERT(tbp->ttb_nrxbufs_onloan > 0);
	tbp->ttb_nrxbufs_onloan--;
}

/*
 * Allocate a transmit-ready buffer capable of holding at least sz bytes.
 *
 * The return value is the virtual address at which the data should be stored,
 * and which must be provided to the transmit routine.
 */
void *
tfpkt_tbus_tx_alloc(tfpkt_tbus_t *tbp, size_t sz)
{
	tfpkt_buf_t *buf;
	void *va = NULL;

	mutex_enter(&tbp->ttb_mutex);

	if (sz > TFPKT_BUF_SIZE) {
		tbp->ttb_txfail_pkt_too_large++;
	} else if ((buf = list_remove_head(&tbp->ttb_txbufs_free)) == NULL) {
		tbp->ttb_txfail_no_bufs++;
	} else {
		va = buf->tfb_dma.tpd_addr;
		tfpkt_tbus_tx_loan(tbp, buf);
	}

	mutex_exit(&tbp->ttb_mutex);

	return (va);
}

/*
 * Return a transmit buffer to the freelist from whence it came.
 */
void
tfpkt_tbus_tx_free(tfpkt_tbus_t *tbp, void *addr)
{
	tfpkt_buf_t *buf;

	mutex_enter(&tbp->ttb_mutex);
	buf = tfpkt_tbus_loaned_buf_by_va(tbp, &tbp->ttb_txbufs_loaned, addr);
	if (buf != NULL) {
		tfpkt_tbus_tx_return(tbp, buf);
		list_insert_tail(&tbp->ttb_txbufs_free, buf);
	} else {
		tfpkt_tbus_dlog(tbp, "freeing unknown buf %p", addr);
	}

	mutex_exit(&tbp->ttb_mutex);
}

/*
 * Push a single message to the ASIC.
 *
 * On success, that call returns 0 and consumes the provided buffer.  On
 * failure, the call returns -1 and buffer ownership remains with the caller.
 */
int
tfpkt_tbus_tx(tfpkt_tbus_t *tbp, void *addr, size_t sz)
{
	tfpkt_buf_t *buf;
	tfpkt_dr_t *drp = &tbp->ttb_tx_drs[0];
	tfpkt_dr_tx_t tx_dr;
	int rval = 0;

	/*
	 * The caller should be handing us back a buffer that we allocated on
	 * its behalf, so both the size and identity checks below should always
	 * succeed.
	 */
	if (sz > TFPKT_BUF_SIZE)
		return (EINVAL);

	mutex_enter(&tbp->ttb_mutex);
	buf = tfpkt_tbus_loaned_buf_by_va(tbp, &tbp->ttb_txbufs_loaned, addr);
	if (buf == NULL)  {
		tfpkt_tbus_dlog(tbp, "sending unknown buf %p", addr);
		rval = EINVAL;
	}
	mutex_exit(&tbp->ttb_mutex);

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

	rval = tfpkt_dr_push(tbp, drp, (uint64_t *)&tx_dr);
	mutex_enter(&tbp->ttb_mutex);
	if (rval == 0) {
		tfpkt_tbus_tx_return(tbp, buf);
		list_insert_tail(&tbp->ttb_txbufs_pushed, buf);
	} else if (rval == ENOSPC) {
		tbp->ttb_txfail_no_descriptors++;
	} else {
		tbp->ttb_txfail_other++;
	}
	mutex_exit(&tbp->ttb_mutex);

	return (rval);
}

/*
 * The packet driver has finished processing the received packet, so we are free
 * to reuse the buffer.
 */
void
tfpkt_tbus_rx_done(tfpkt_tbus_t *tbp, void *addr, size_t sz)
{
	tfpkt_buf_t *buf;

	mutex_enter(&tbp->ttb_mutex);
	buf = tfpkt_tbus_loaned_buf_by_va(tbp, &tbp->ttb_rxbufs_loaned, addr);
	if (buf != NULL) {
		tfpkt_tbus_rx_return(tbp, buf);
		list_insert_tail(&tbp->ttb_rxbufs_free, buf);
	}
	mutex_exit(&tbp->ttb_mutex);
}

static void
tfpkt_tbus_process_rx(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, tfpkt_dr_rx_t *rx_dr)
{
	tfpkt_buf_t *buf;
	int loan = 0;

	mutex_enter(&tbp->ttb_mutex);
	buf = tfpkt_tbus_buf_by_pa(&tbp->ttb_rxbufs_pushed, rx_dr->rx_addr);
	if (buf == NULL) {
		tfpkt_tbus_dlog(tbp, "unrecognized rx buf: %lx", rx_dr->rx_addr);
		mutex_exit(&tbp->ttb_mutex);
		return;
	}

	if (rx_dr->rx_type != TFPRT_RX_DESC_TYPE_PKT) {
		/* should never happen. */
		tfpkt_tbus_err(tbp, "non-pkt descriptor (%d) on %s",
		    rx_dr->rx_type, drp->tdr_name);
	} else if (tbp->ttb_nrxbufs_onloan < tbp->ttb_nrxbufs_onloan_max) {
		tfpkt_tbus_rx_loan(tbp, buf);
		loan = 1;
	} else {
		tbp->ttb_rxfail_excess_loans++;
	}
	if (!loan) {
		list_insert_tail(&tbp->ttb_rxbufs_free, buf);
	}

	mutex_exit(&tbp->ttb_mutex);

	if (loan)
		tfpkt_rx(tbp->ttb_tfp, buf->tfb_dma.tpd_addr, rx_dr->rx_size);
}

static void
tfpkt_tbus_process_cmp(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, tfpkt_dr_cmp_t *cmp_dr)
{
	tfpkt_buf_t *buf;

	mutex_enter(&tbp->ttb_mutex);

	buf = tfpkt_tbus_buf_by_pa(&tbp->ttb_txbufs_pushed, cmp_dr->cmp_addr);
	if (buf == NULL) {
		tfpkt_tbus_dlog(tbp, "unrecognized tx buf: %lx", cmp_dr->cmp_addr);
	} else if (cmp_dr->cmp_type != TFPRT_TX_DESC_TYPE_PKT) {
		/* should never happen. */
		tfpkt_tbus_err(tbp, "non-pkt descriptor (%d) on %s",
		    cmp_dr->cmp_type, drp->tdr_name);
	} else {
		list_insert_tail(&tbp->ttb_txbufs_free, buf);
	}

	mutex_exit(&tbp->ttb_mutex);
}

static int
tfpkt_tbus_reg_op(tfpkt_tbus_t *tbp, size_t offset, uint32_t *val, bool rd)
{
	int rval;

	if (rd) {
		rval = tofino_tbus_read_reg(tbp->ttb_tbus_hdl, offset, val);
	} else {
		rval = tofino_tbus_write_reg(tbp->ttb_tbus_hdl, offset, *val);
	}

	if (rval != 0)
		tfpkt_tbus_reset_detected(tbp->ttb_tfp);

	return (rval);
}

static int
tfpkt_dr_read(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, size_t offset, uint32_t *val)
{
	return (tfpkt_tbus_reg_op(tbp, drp->tdr_reg_base + offset, val, true));
}

static int
tfpkt_dr_write(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, size_t offset,
    uint32_t val)
{
	return (tfpkt_tbus_reg_op(tbp, drp->tdr_reg_base + offset, &val, false));
}

static int
tfpkt_tbus_cmp_poll(tfpkt_tbus_t *tbp, int ring)
{
	tfpkt_dr_t *drp = &tbp->ttb_cmp_drs[ring];
	tfpkt_dr_cmp_t cmp_dr;
	int processed = 0;

	if (tfpkt_dr_pull(tbp, drp, (uint64_t *)&cmp_dr) == 0) {
		tfpkt_tbus_process_cmp(tbp, drp, &cmp_dr);
		processed++;
	}

	return (processed);
}

static int
tfpkt_tbus_rx_poll(tfpkt_tbus_t *tbp, int ring)
{
	tfpkt_dr_t *drp = &tbp->ttb_rx_drs[ring];
	tfpkt_dr_rx_t rx_dr;
	int processed = 0;

	if (tfpkt_dr_pull(tbp, drp, (uint64_t *)&rx_dr) == 0) {
		tfpkt_tbus_process_rx(tbp, drp, &rx_dr);
		processed++;
	}

	return (processed);
}

/*
 * Program the ASIC with the location, range, and characteristics of this
 * descriptor ring.
 */
static int
tfpkt_tbus_init_dr(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp)
{
	uint64_t phys;
	uint32_t ctrl;

	/*
	 * The DR range has to be 64-byte aligned.
	 */
	phys = (drp->tdr_phys_base + 63ull) & ~(63ull);

	/* disable DR */
	ctrl = 0;
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_CTRL, ctrl);

	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_SIZE,
	    drp->tdr_ring_size);
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_BASE_ADDR_LOW,
	    (uint32_t)(phys & 0xFFFFFFFFULL));
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_BASE_ADDR_HIGH,
	    (uint32_t)(phys >> 32));

	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_LOW,
	    (uint32_t)((phys + drp->tdr_ring_size) & 0xFFFFFFFFULL));
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_HIGH,
	    (uint32_t)((phys + drp->tdr_ring_size) >> 32));

	*drp->tdr_tail_ptr = 0;
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_HEAD_PTR, 0);
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_TAIL_PTR, 0);

	/* Tofino2 has two additional registers */
	if (tbp->ttb_gen == TOFINO_G_TF2) {
		(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_EMPTY_INT_TIME,
		    0);
		(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_EMPTY_INT_CNT,
		    0);
	}

	switch (drp->tdr_type) {
		case TFPKT_DR_TX:
		case TFPKT_DR_FM:
			ctrl = TBUS_DR_CTRL_HEAD_PTR_MODE;
			break;
		case TFPKT_DR_RX:
			(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_DATA_TIMEOUT, 1);
			/* fallthru */
		case TFPKT_DR_CMP:
			ctrl = TBUS_DR_CTRL_TAIL_PTR_MODE;
			break;
	}

	/* enable DR */
	ctrl |= TBUS_DR_CTRL_ENABLE;
	return (tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_CTRL, ctrl));
}

/*
 * Push the configuration info for all of the DRs into the ASIC
 */
static int
tfpkt_tbus_init_drs(tfpkt_tbus_t *tbp)
{
	int i, rval;

	rval = 0;
	for (i = 0; (rval == 0) && (i < TFPKT_FM_CNT); i++)
		rval = tfpkt_tbus_init_dr(tbp, &tbp->ttb_fm_drs[i]);
	for (i = 0; (rval == 0) && (i < TFPKT_RX_CNT); i++)
		rval = tfpkt_tbus_init_dr(tbp, &tbp->ttb_rx_drs[i]);
	for (i = 0; (rval == 0) && (i < TFPKT_TX_CNT); i++)
		rval = tfpkt_tbus_init_dr(tbp, &tbp->ttb_tx_drs[i]);
	for (i = 0; (rval == 0) && (i < TFPKT_CMP_CNT); i++)
		rval = tfpkt_tbus_init_dr(tbp, &tbp->ttb_cmp_drs[i]);

	return (rval);
}

/*
 * Refresh our in-core copy of the tail pointer from the DR's config register.
 */
static int
tfpkt_dr_refresh_tail(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp)
{
	uint32_t tail;
	int rval;

	rval = tfpkt_dr_read(tbp, drp, TBUS_DR_OFF_TAIL_PTR, &tail);
	if (rval == 0)
		drp->tdr_tail = tail;

	return (rval);
}

/*
 * Refresh our in-core copy of the head pointer from the DR's config register.
 */
static int
tfpkt_dr_refresh_head(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp)
{
	uint32_t head;
	int rval;

	rval = tfpkt_dr_read(tbp, drp, TBUS_DR_OFF_HEAD_PTR, &head);
	if (rval == 0)
		drp->tdr_head = head;
	return (rval);
}

#define	DR_PTR_WRAP_BIT (1 << 20)
#define	DR_PTR_GET_WRAP_BIT(p) ((p) & DR_PTR_WRAP_BIT)
#define	DR_PTR_GET_BODY(p) ((p) & (DR_PTR_WRAP_BIT - 1))

static int
tfpkt_dr_full(tfpkt_dr_t *drp)
{
	uint64_t head_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tdr_head);
	uint64_t tail_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tdr_tail);
	uint64_t head = DR_PTR_GET_BODY(drp->tdr_head);
	uint64_t tail = DR_PTR_GET_BODY(drp->tdr_tail);

	ASSERT(mutex_owned(&drp->tdr_mutex));

	return ((head == tail) && (head_wrap_bit != tail_wrap_bit));
}

static int
tfpkt_dr_empty(tfpkt_dr_t *drp)
{
	ASSERT(mutex_owned(&drp->tdr_mutex));
	return (drp->tdr_head == drp->tdr_tail);
}

/*
 * If the ring isn't full, advance the tail pointer to the next empty slot.
 * Return 0 if it advances, -1 if it doesn't.
 */
static int
tfpkt_dr_advance_tail(tfpkt_dr_t *drp)
{
	uint64_t tail, tail_wrap_bit;

	ASSERT(mutex_owned(&drp->tdr_mutex));
	if (tfpkt_dr_full(drp)) {
		return (-1);
	}

	tail_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tdr_tail);
	tail = DR_PTR_GET_BODY(drp->tdr_tail);
	tail += drp->tdr_desc_size;
	if (tail == drp->tdr_ring_size) {
		tail = 0;
		tail_wrap_bit ^= DR_PTR_WRAP_BIT;
	}

	drp->tdr_tail = tail | tail_wrap_bit;
	return (0);
}

/*
 * If the ring is non-empty, advance the head pointer to the next descriptor.
 * Return 0 if it advances, -1 if it doesn't.
 */
static int
tfpkt_dr_advance_head(tfpkt_dr_t *drp)
{
	uint64_t head, head_wrap_bit;

	ASSERT(mutex_owned(&drp->tdr_mutex));
	if (tfpkt_dr_empty(drp)) {
		return (-1);
	}

	head_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tdr_head);
	head = DR_PTR_GET_BODY(drp->tdr_head);
	head += drp->tdr_desc_size;
	if (head == drp->tdr_ring_size) {
		head = 0;
		head_wrap_bit ^= DR_PTR_WRAP_BIT;
	}
	drp->tdr_head = head | head_wrap_bit;
	return (0);
}

/*
 * Pull a single descriptor off the head of a ring.
 * Returns 0 if it successfully pushes a descriptor, ENOENT if the ring is
 * empty, and ENXIO if we detect that the rings have been reset.
 */
static int
tfpkt_dr_pull(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, head;
	int rval;

	mutex_enter(&drp->tdr_mutex);
	if (tfpkt_dr_refresh_tail(tbp, drp) != 0) {
		mutex_exit(&drp->tdr_mutex);
		return (ENXIO);
	}

	if (tfpkt_dr_empty(drp)) {
		mutex_exit(&drp->tdr_mutex);
		return (ENOENT);
	}

	head = DR_PTR_GET_BODY(drp->tdr_head);
	slot = (uint64_t *)(drp->tdr_virt_base + head);

	if (tfpkt_tbus_debug > 1) {
		uint64_t offset = DR_PTR_GET_BODY(drp->tdr_head);
		uint64_t wrap = DR_PTR_GET_WRAP_BIT(drp->tdr_head) != 0;
		int idx = offset / drp->tdr_desc_size;

		tfpkt_tbus_dlog(tbp, "pulling from %s at %ld (wrap: %ld %d/%ld)",
		    drp->tdr_name, drp->tdr_head, wrap, idx,
		    drp->tdr_depth);
	}

	for (int i = 0; i < (drp->tdr_desc_size >> 3); i++)
		desc[i] = slot[i];

	(void) tfpkt_dr_advance_head(drp);
	rval = tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_HEAD_PTR,
	    drp->tdr_head);
	mutex_exit(&drp->tdr_mutex);

	return (rval);
}

/*
 * Push a single descriptor onto the tail of a ring.
 * Returns 0 if it successfully pushes a descriptor, ENOSPC if the ring is full,
 * and ENXIO if we detect that the rings have been reset.
 */
static int
tfpkt_dr_push(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, tail;
	int rval;

	mutex_enter(&drp->tdr_mutex);
	if (tfpkt_dr_refresh_head(tbp, drp) != 0) {
		mutex_exit(&drp->tdr_mutex);
		return (ENXIO);
	}

	if (tfpkt_dr_full(drp)) {
		mutex_exit(&drp->tdr_mutex);
		return (ENOSPC);
	}
	if (tfpkt_tbus_debug > 1) {
		uint64_t offset = DR_PTR_GET_BODY(drp->tdr_tail);
		uint64_t wrap = DR_PTR_GET_WRAP_BIT(drp->tdr_tail) != 0;
		int idx = offset / drp->tdr_desc_size;

		tfpkt_tbus_dlog(tbp, "pushing to %s at %ld (wrap: %ld %d/%ld)",
		    drp->tdr_name, drp->tdr_tail, wrap, idx,
		    drp->tdr_depth);
	}

	tail = DR_PTR_GET_BODY(drp->tdr_tail);
	slot = (uint64_t *)(drp->tdr_virt_base + tail);
	for (int i = 0; i < (drp->tdr_desc_size >> 3); i++)
		slot[i] = desc[i];

	(void) tfpkt_dr_advance_tail(drp);
	tail = DR_PTR_GET_BODY(drp->tdr_tail);
	*drp->tdr_tail_ptr = tail;
	rval = tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_TAIL_PTR, drp->tdr_tail);
	mutex_exit(&drp->tdr_mutex);

	return (rval);
}

/*
 * Push a free DMA buffer onto a free_memory descriptor ring.
 */
static int
tfpkt_tbus_push_fm(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, uint64_t addr, uint64_t size)
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

	return (tfpkt_dr_push(tbp, drp, &descriptor));
}

/*
 * Push all free receive buffers onto the free_memory DR until the ring is full,
 * or we run out of buffers.
 */
static int
tfpkt_tbus_push_free_bufs(tfpkt_tbus_t *tbp, int ring)
{
	int rval = 0;
	uint64_t dma_addr;
	tfpkt_dr_t *drp = &tbp->ttb_fm_drs[ring];
	tfpkt_buf_t *buf, *next;
	int cnt = 0;

	mutex_enter(&tbp->ttb_mutex);
	for (buf = list_head(&tbp->ttb_rxbufs_free); buf != NULL; buf = next) {
		next = list_next(&tbp->ttb_rxbufs_free, buf);
		dma_addr = buf->tfb_dma.tpd_cookie.dmac_laddress;
		rval = tfpkt_tbus_push_fm(tbp, drp, dma_addr, TFPKT_BUF_SIZE);
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
		list_remove(&tbp->ttb_rxbufs_free, buf);
		list_insert_tail(&tbp->ttb_rxbufs_pushed, buf);
		cnt++;
	}
	mutex_exit(&tbp->ttb_mutex);

	return (rval);
}

/*
 * Setup the tbus control register to enable the pci network port
 */
static int 
tfpkt_tbus_port_init(tfpkt_tbus_t *tbp, dev_info_t *tfp_dip)
{
	tf_tbus_hdl_t hdl = tbp->ttb_tbus_hdl;
	tf_tbus_ctrl_t ctrl;
	uint32_t reg;
	uint32_t *ctrlp = (uint32_t *)&ctrl;
	int rval;

	ASSERT(tbp->ttb_gen == TOFINO_G_TF1 || tbp->ttb_gen == TOFINO_G_TF2);
	if (tbp->ttb_gen == TOFINO_G_TF1) {
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
	if (tbp->ttb_gen != TOFINO_G_TF1)
		ctrl.tftc_rx_channel_offset = 0;

	return (tofino_tbus_write_reg(hdl, reg, *ctrlp));
}

static uint_t
tfpkt_tbus_intr(caddr_t arg1, caddr_t arg2)
{
	tfpkt_t *tfp = (tfpkt_t *)arg1;
	tfpkt_tbus_t *tbp;
	int processed = 1;

	while (processed > 0 && ((tbp = tfpkt_tbus_hold(tfp)) != NULL)) {
		processed = 0;
		for (int i = 0; i < TFPKT_RX_CNT; i++) {
			if (tfpkt_tbus_rx_poll(tbp, i) > 0) {
				processed++;
				if (tfpkt_tbus_push_free_bufs(tbp, i))
					break;
			}
		}

		for (int i = 0; i < TFPKT_CMP_CNT; i++) {
			if (tfpkt_tbus_cmp_poll(tbp, i)) {
				processed++;
			}
		}
		tfpkt_tbus_release(tfp);
	}

	return (DDI_INTR_CLAIMED);
}

static void
tfpkt_tbus_fini(tfpkt_t *tfp, tfpkt_tbus_t *tbp)
{
	ASSERT(tbp != NULL);
	if (tbp->ttb_tbus_hdl != NULL) {
		VERIFY0(tofino_tbus_unregister_softint(tbp->ttb_tbus_hdl,
		    tbp->ttb_softint));
		VERIFY0(tofino_tbus_unregister(tbp->ttb_tbus_hdl));
	}
	if (tbp->ttb_softint != NULL) {
		VERIFY3S(ddi_intr_remove_softint(tbp->ttb_softint), ==,
		    DDI_SUCCESS);
	}

	tfpkt_tbus_free_bufs(tbp);
	tfpkt_tbus_free_drs(tbp);
	mutex_destroy(&tbp->ttb_mutex);
	kmem_free(tbp, sizeof (*tbp));
	tfp->tfp_tbus_data = NULL;
}

/*
 * tfpkt_tbus_init() is called in a loop, and can reasonably be expected to fail
 * the same way many times in a row.  There is no benefit to repeating the error
 * message each time, so we don't.
 */
static void
oneshot_error(tfpkt_tbus_t *tbp, const char *msg)
{
	static const char *last_msg = NULL;

	if (msg != last_msg) {
		tfpkt_tbus_err(tbp, msg);
		last_msg = msg;
	}
}

static tfpkt_tbus_t *
tfpkt_tbus_init(tfpkt_t *tfp)
{
	dev_info_t *tfp_dip = tfp->tfp_dip;
	tfpkt_tbus_t *tbp;
	tf_tbus_hdl_t hdl;
	int err;

	tbp = kmem_zalloc(sizeof (*tbp), KM_SLEEP);
	mutex_init(&tbp->ttb_mutex, NULL, MUTEX_DRIVER, NULL);
	tbp->ttb_dip = tfp_dip;
	tbp->ttb_tfp = tfp;

	if ((err = tofino_tbus_register(&hdl)) != 0) {
		if (err == EBUSY) {
			oneshot_error(tbp, "tofino tbus in use");
		} else if (err == ENXIO) {
			/* The driver was loaded but not attached. */
			 oneshot_error(tbp, "tofino driver offline");
		} else if (err == EAGAIN) {
			/*
			 * The userspace daemon hasn't yet initialized the
			 * ASIC
			 */
			 oneshot_error(tbp, "tofino asic not ready");
		} else {
			 oneshot_error(tbp, "tofino_tbus_register failed");
		}
		goto fail;
	}

	tbp->ttb_tbus_hdl = hdl;
	tbp->ttb_gen = tofino_get_generation(hdl);

	if ((err = tfpkt_tbus_alloc_bufs(tbp)) != 0) {
		 oneshot_error(tbp, "failed to allocate buffers");
	} else if ((err = tfpkt_tbus_alloc_drs(tbp)) != 0) {
		 oneshot_error(tbp, "failed to allocate drs");
	} else if ((err = tfpkt_tbus_init_drs(tbp)) != 0) {
		 oneshot_error(tbp, "failed to init drs");
	}
	if (err != 0)
		goto fail;

	tfpkt_tbus_port_init(tbp, tfp_dip);

	err = ddi_intr_add_softint(tfp_dip, &tbp->ttb_softint,
	    DDI_INTR_SOFTPRI_DEFAULT, tfpkt_tbus_intr, tfp);
	if (err != 0) {
		oneshot_error(tbp, "failed to allocate softint");
		goto fail;
	}
	if ((err = tofino_tbus_register_softint(hdl, tbp->ttb_softint)) != 0) {
		oneshot_error(tbp, "failed to register softint");
		VERIFY0(tofino_tbus_unregister(hdl));
	}


	for (int i = 0; i < TFPKT_RX_CNT; i++)
		if ((err = tfpkt_tbus_push_free_bufs(tbp, i)) != 0)
			goto fail;

	return (tbp);

fail:
	tfpkt_tbus_fini(tfp, tbp);
	return (NULL);
}

void
tfpkt_tbus_reset_detected(tfpkt_t *tfp)
{
	mutex_enter(&tfp->tfp_tbus_mutex);
	if (tfp->tfp_tbus_state == TFPKT_TBUS_ACTIVE) {
		tfp->tfp_tbus_state = TFPKT_TBUS_RESETTING;
		cv_broadcast(&tfp->tfp_tbus_cv);
	}
	mutex_exit(&tfp->tfp_tbus_mutex);
}

void
tfpkt_tbus_monitor(void *arg)
{
	dev_info_t *dip = arg;
	tfpkt_t *tfp = (tfpkt_t *)ddi_get_driver_private(dip);
	clock_t time;

	dev_err(tfp->tfp_dip, CE_NOTE, "tbus monitor started");

	mutex_enter(&tfp->tfp_tbus_mutex);
	while (tfp->tfp_tbus_state != TFPKT_TBUS_HALTING) {
		tfpkt_tbus_t *tbp = tfp->tfp_tbus_data;

		switch (tfp->tfp_tbus_state) {
			case TFPKT_TBUS_UNINIT:
				/*
				 * Keep asking the tofino driver to let us
				 * use the tbus until it says OK.
				 */
				ASSERT(tbp == NULL);
				tfp->tfp_tbus_data = tfpkt_tbus_init(tfp);
				if (tfp->tfp_tbus_data != NULL)
					tfp->tfp_tbus_state = TFPKT_TBUS_ACTIVE;
				break;

			case TFPKT_TBUS_ACTIVE:
				/*
				 * Verify that the tbus registers haven't been
				 * reset on us.  In most cases, this will
				 * already have been detected in one of the
				 * packet processing paths.
				 */
				if (tofino_tbus_state(tbp->ttb_tbus_hdl) ==
				    TF_TBUS_READY)
					break;
				tfp->tfp_tbus_state = TFPKT_TBUS_RESETTING;
				/* FALLTHRU */

			case TFPKT_TBUS_RESETTING:
				/*
				 * Don't clean up the tbus data while someone
				 * is actively using it.
				 */
				if (tfp->tfp_tbus_refcnt == 0) {
					tfpkt_tbus_fini(tfp, tbp);
					tfp->tfp_tbus_state = TFPKT_TBUS_UNINIT;
				}
				break;

			case TFPKT_TBUS_HALTING:
				/*
				 * A no-op to make the default case useful
				 */
				continue;

			case TFPKT_TBUS_HALTED:
				panic("tbus monitor halted by third party");

			default:
				ASSERT(0);
		}

		time = ddi_get_lbolt() + hz;
		cv_timedwait(&tfp->tfp_tbus_cv, &tfp->tfp_tbus_mutex, time);
	}

	while (tfp->tfp_tbus_refcnt != 0) {
		dev_err(dip, CE_NOTE, "waiting for %d tbus refs to drop",
				tfp->tfp_tbus_refcnt);
		time = ddi_get_lbolt() + hz;
		cv_timedwait(&tfp->tfp_tbus_cv, &tfp->tfp_tbus_mutex, time);
	}

	if (tfp->tfp_tbus_data != NULL)
		tfpkt_tbus_fini(tfp, tfp->tfp_tbus_data);

	tfp->tfp_tbus_state = TFPKT_TBUS_HALTED;
	cv_broadcast(&tfp->tfp_tbus_cv);
	mutex_exit(&tfp->tfp_tbus_mutex);
	dev_err(tfp->tfp_dip, CE_NOTE, "tbus monitor exiting");
}

int
tfpkt_tbus_monitor_halt(tfpkt_t *tfp)
{
	time_t deadline, left;
	int rval;

	dev_err(tfp->tfp_dip, CE_NOTE, "halting tbus monitor");
	mutex_enter(&tfp->tfp_tbus_mutex);
	if (tfp->tfp_tbus_state != TFPKT_TBUS_HALTED) {
		tfp->tfp_tbus_state = TFPKT_TBUS_HALTING;
		cv_broadcast(&tfp->tfp_tbus_cv);
	}

	left = hz;
	deadline = ddi_get_lbolt() + left;
	while (left > 0 && tfp->tfp_tbus_state != TFPKT_TBUS_HALTED) {
		left = cv_timedwait(&tfp->tfp_tbus_cv, &tfp->tfp_tbus_mutex,
		    deadline);

	}

	if (tfp->tfp_tbus_state == TFPKT_TBUS_HALTED) {
		dev_err(tfp->tfp_dip, CE_NOTE, "halted tbus monitor");
		rval = 0;
	} else {
		dev_err(tfp->tfp_dip, CE_WARN,
		    "timed out waiting for tbus monitor to halt");
		rval = -1;
	}

	mutex_exit(&tfp->tfp_tbus_mutex);

	return rval;
}

