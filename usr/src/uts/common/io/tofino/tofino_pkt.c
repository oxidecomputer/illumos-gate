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
 * Tofino packet handler
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
#include <sys/tofino_impl.h>
#include <sys/tofino_pkt.h>
#include "tofino_regs.h"

/*
 * Forward references
 */
static int tfpkt_dr_push(dev_info_t *, tfpkt_dr_t *, uint64_t *);
static int tfpkt_dr_pull(dev_info_t *, tfpkt_dr_t *, uint64_t *);
static int tfpkt_push_free_bufs(dev_info_t *, tfpkt_t *, int);

static ddi_dma_attr_t tfpkt_dma_attr_buf = {
	.dma_attr_version =		DMA_ATTR_V0,
	.dma_attr_addr_lo =		0x0000000000000000,
	.dma_attr_addr_hi =		0xFFFFFFFFFFFFFFFF,
	.dma_attr_count_max =		0x00000000FFFFFFFF,
	.dma_attr_align =		0x0000000000000800,
	.dma_attr_burstsizes =		0x00000FFF,
	.dma_attr_minxfer =		1,
	.dma_attr_maxxfer =		0x00000000FFFFFFFF,
	.dma_attr_seg =			0xFFFFFFFFFFFFFFFF,
	.dma_attr_sgllen =		1,
	.dma_attr_granular =		1,
	.dma_attr_flags =		DDI_DMA_FLAGERR,
};

static ddi_device_acc_attr_t tfpkt_acc_attr = {
	.devacc_attr_version = DDI_DEVICE_ATTR_V1,
	.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
	.devacc_attr_dataorder = DDI_STRICTORDER_ACC,
	.devacc_attr_access = DDI_DEFAULT_ACC,
};

static void
tfpkt_log(tfpkt_t *tfp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	tofino_vlog(tfp->tfp_dip, CE_NOTE, fmt, args);
	va_end(args);
}

static void
tfpkt_err(tfpkt_t *tfp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	tofino_vlog(tfp->tfp_dip, CE_WARN, fmt, args);
	va_end(args);
}

/*
 * Allocate a single buffer capable of DMA to/from the Tofino ASIC.
 *
 * The caller is responsible for providing an unused tfpkt_dma_t structure,
 * which is used for tracking and managing a DMA buffer.  This routine will
 * populate that structure with all the necessary state.  Having the caller
 * provide the state structure lets us allocate them in bulk, rather than one
 * per buffer.
 */
static int
tfpkt_dma_alloc(tfpkt_t *tfp, tfpkt_dma_t *dmap, size_t size, int flags) {
	unsigned int count;
	int err;

	err = ddi_dma_alloc_handle(tfp->tfp_dip, &tfpkt_dma_attr_buf,
	    DDI_DMA_SLEEP, NULL, &dmap->tpd_handle);
	if (err != DDI_SUCCESS) {
		tfpkt_err(tfp, "%s: alloc_handle failed: %d", __func__, err);
		goto fail0;
	}

	err = ddi_dma_mem_alloc(dmap->tpd_handle, size, &tfpkt_acc_attr,
	    DDI_DMA_STREAMING, DDI_DMA_SLEEP, NULL, &dmap->tpd_addr,
	    &dmap->tpd_len, &dmap->tpd_acchdl);
	if (err != DDI_SUCCESS) {
		tfpkt_err(tfp, "%s: mem_alloc failed", __func__);
		goto fail1;
	}

	err = ddi_dma_addr_bind_handle(dmap->tpd_handle, NULL, dmap->tpd_addr,
	    dmap->tpd_len, flags, DDI_DMA_SLEEP, NULL, &dmap->tpd_cookie, &count);
	if (err != DDI_DMA_MAPPED) {
		tfpkt_err(tfp, "%s: bind_handle failed", __func__);
		goto fail2;
	}

	if (count > 1) {
		tfpkt_err(tfp, "%s: more than one DMA cookie", __func__);
		goto fail2;
	}

	return (0);
fail2:
	ddi_dma_mem_free(&dmap->tpd_acchdl);
fail1:
	ddi_dma_free_handle(&dmap->tpd_handle);
fail0:
	return (-1);
}

/*
 * This routine frees a DMA buffer and its state, but does not free the
 * tfpkt_dma_t structure itself.
 */
static void
tfpkt_dma_free(tfpkt_dma_t *dmap)
{
	ddi_dma_unbind_handle(dmap->tpd_handle);
	ddi_dma_mem_free(&dmap->tpd_acchdl);
	ddi_dma_free_handle(&dmap->tpd_handle);
}

/*
 * Free a single tfpkt_buf_t structure.  If the buffer includes a DMA buffer,
 * that is freed as well.
 */
static void
tfpkt_free_buf(tfpkt_buf_t *buf)
{
	VERIFY3U((buf->tfb_flags & TFPORT_BUF_LOANED), ==, 0);

	if (buf->tfb_flags & TFPORT_BUF_DMA_ALLOCED) {
		tfpkt_dma_free(&buf->tfb_dma);
		buf->tfb_flags &= ~TFPORT_BUF_DMA_ALLOCED;
	}
}

/*
 * Free all of the buffers on a list.  Returns the number of buffers freed.
 */
static int
tfpkt_free_buf_list(list_t *list)
{
	tfpkt_buf_t *buf;
	int freed = 0;

	while ((buf = list_remove_head(list)) != NULL) {
		tfpkt_free_buf(buf);
		freed++;
	}

	return (freed);
}

/*
 * Free all of the buffers allocated by the packet handler
 */
static void
tfpkt_free_bufs(tfpkt_t *tfp)
{
	int freed;

	VERIFY(MUTEX_HELD(&tfp->tfp_mutex));

	if (tfp->tfp_bufs_mem == NULL)
		return;

	// VERIFY3U(tfp->tfp_nrxbufs_onloan, ==, 0);
	// VERIFY3U(tfp->tfp_ntxbufs_onloan, ==, 0);
	freed = tfpkt_free_buf_list(&tfp->tfp_rxbufs_free);
	freed += tfpkt_free_buf_list(&tfp->tfp_rxbufs_pushed);
	freed += tfpkt_free_buf_list(&tfp->tfp_txbufs_free);
	freed += tfpkt_free_buf_list(&tfp->tfp_txbufs_pushed);

	if (freed != tfp->tfp_bufs_capacity)
		dev_err(tfp->tfp_dip, CE_WARN, "lost track of %d/%d buffers",
		    tfp->tfp_bufs_capacity - freed, tfp->tfp_bufs_capacity);

	// VERIFY3U(tfp->tfp_bufs_capacity, ==, freed);
	kmem_free(tfp->tfp_bufs_mem,
	    sizeof (tfpkt_buf_t) * tfp->tfp_bufs_capacity);
	tfp->tfp_bufs_mem = NULL;
	tfp->tfp_bufs_capacity = 0;
}

static void
tfpkt_buf_list_init(list_t *list)
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
tfpkt_alloc_bufs(tfpkt_t *tfp)
{
	VERIFY(MUTEX_HELD(&tfp->tfp_mutex));

	tfpkt_log(tfp, "allocating bufs");
	tfp->tfp_bufs_capacity = TFPORT_NET_RX_BUFS + TFPORT_NET_TX_BUFS;
	tfp->tfp_bufs_mem = kmem_zalloc(
	    sizeof (tfpkt_buf_t) * tfp->tfp_bufs_capacity, KM_SLEEP);
	tfpkt_buf_list_init(&tfp->tfp_rxbufs_free);
	tfpkt_buf_list_init(&tfp->tfp_rxbufs_pushed);
	tfpkt_buf_list_init(&tfp->tfp_rxbufs_loaned);
	tfpkt_buf_list_init(&tfp->tfp_txbufs_free);
	tfpkt_buf_list_init(&tfp->tfp_txbufs_pushed);
	tfpkt_buf_list_init(&tfp->tfp_txbufs_loaned);

	/*
	 * Do not loan more than half of our allocated receive buffers into
	 * the networking stack.
	 */
	tfp->tfp_nrxbufs_onloan_max = TFPORT_NET_RX_BUFS / 2;

	for (uint_t i = 0; i < tfp->tfp_bufs_capacity; i++) {
		tfpkt_buf_t *buf = &tfp->tfp_bufs_mem[i];
		if ((tfpkt_dma_alloc(tfp, &buf->tfb_dma, TFPORT_BUF_SIZE,
		    DDI_DMA_STREAMING | DDI_DMA_READ)) != 0) {
			goto fail;
		}
		buf->tfb_flags |= TFPORT_BUF_DMA_ALLOCED;
		buf->tfb_tfport = tfp;
		if (i < TFPORT_NET_RX_BUFS)
			list_insert_tail(&tfp->tfp_rxbufs_free, buf);
		else
			list_insert_tail(&tfp->tfp_txbufs_free, buf);
	}

	return (0);

fail:
	tfpkt_free_bufs(tfp);
	return (ENOMEM);
}

static void
tfpkt_free_dr(tfpkt_dr_t *drp)
{
	if (drp->tfdrp_virt_base != 0) {
		tfpkt_dma_free(&drp->tfdrp_dma);
	}
	drp->tfdrp_virt_base = 0;
	drp->tfdrp_phys_base = 0;
}

/*
 * Free all of the memory allocated to contain and manage the descriptor rings.
 */
static void
tfpkt_free_drs(tfpkt_t *tfp)
{
	int i;

	if (tfp->tfp_rx_drs != NULL) {
		for (i = 0; i < TF_PKT_RX_CNT; i++) {
			tfpkt_free_dr(&tfp->tfp_rx_drs[i]);
		}
		kmem_free(tfp->tfp_rx_drs, sizeof (tfpkt_dr_t) * TF_PKT_RX_CNT);
	}
	if (tfp->tfp_tx_drs != NULL) {
		for (i = 0; i < TF_PKT_TX_CNT; i++) {
			tfpkt_free_dr(&tfp->tfp_tx_drs[i]);
		}
		kmem_free(tfp->tfp_tx_drs, sizeof (tfpkt_dr_t) * TF_PKT_TX_CNT);
	}
	if (tfp->tfp_fm_drs != NULL) {
		for (i = 0; i < TF_PKT_FM_CNT; i++) {
			tfpkt_free_dr(&tfp->tfp_fm_drs[i]);
		}
		kmem_free(tfp->tfp_fm_drs, sizeof (tfpkt_dr_t) * TF_PKT_FM_CNT);
	}
	if (tfp->tfp_cmp_drs != NULL) {
		for (i = 0; i < TF_PKT_CMP_CNT; i++) {
			tfpkt_free_dr(&tfp->tfp_cmp_drs[i]);
		}
		kmem_free(tfp->tfp_cmp_drs, sizeof (tfpkt_dr_t) * TF_PKT_CMP_CNT);
	}
}

/*
 * Allocate a DMA memory in which to store a single descriptor ring.  Fill in
 * the provided DR management structure.  We calculate the offsets of the
 * different registers used to configure and manage the DR, but do not actually
 * update those registers here.
 */
int
tfpkt_alloc_dr(tfpkt_t *tfp, tfpkt_dr_t *drp, tfpkt_dr_type_t dr_type,
	int dr_id, size_t depth)
{
	uint32_t reg_base = 0;
	uint32_t desc_sz = 0;
	size_t ring_sz, total_sz;
	char *prefix = NULL;
	int flags = DDI_DMA_STREAMING | DDI_DMA_RDWR;

	/*
	 * The Tofino registers that are used to configure each descriptor ring
	 * are segregated according to the type of ring.  The addresses and
	 * sizes of those register vary between Tofino generations.  The size of
	 * each descriptor varies depending on the ring, but is consistent
	 * between generations.
	 */
	if (tfp->tfp_gen == TOFINO_G_TF1) {
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
		ASSERT(tfp->tfp_gen == TOFINO_G_TF2);
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
		tfpkt_log(tfp, "adjusting %s from %lx to %lx", drp->tfdrp_name,
		    ring_sz, fixed);
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
	if (tfpkt_dma_alloc(tfp, &drp->tfdrp_dma, total_sz, flags) != 0) {
		return (-1);
	}

	snprintf(drp->tfdrp_name, DR_NAME_LEN, "%s_%d", prefix, dr_id);
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

#if 0
	tfpkt_log(tfp, "allocated DR %s.  phys_base: %llx  reg: %lx",
			drp->tfdrp_name, drp->tfdrp_phys_base, drp->tfdrp_reg_base);
#endif

	return (0);
}

/*
 * Allocate memory for all of the descriptor rings and the metadata structures
 * we use to manage them.
 */
static int
tfpkt_alloc_drs(tfpkt_t *tfp)
{
	int i;

	VERIFY(MUTEX_HELD(&tfp->tfp_mutex));

	tfpkt_log(tfp, "allocating DRs");

	tfp->tfp_rx_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TF_PKT_RX_CNT,
	    KM_SLEEP);
	tfp->tfp_tx_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TF_PKT_TX_CNT,
	    KM_SLEEP);
	tfp->tfp_fm_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TF_PKT_FM_CNT,
	    KM_SLEEP);
	tfp->tfp_cmp_drs = kmem_zalloc(sizeof (tfpkt_dr_t) * TF_PKT_CMP_CNT,
	    KM_SLEEP);

	for (i = 0; i < TF_PKT_RX_CNT; i++) {
		if (tfpkt_alloc_dr(tfp, &tfp->tfp_rx_drs[i], TF_PKT_DR_RX,
		    i, TF_PKT_RX_DEPTH) != 0) {
			tfpkt_err(tfp, "failed to alloc rx dr");
			goto fail;
		}
	}
	for (i = 0; i < TF_PKT_TX_CNT; i++) {
		if (tfpkt_alloc_dr(tfp, &tfp->tfp_tx_drs[i], TF_PKT_DR_TX,
		    i, TF_PKT_TX_DEPTH) != 0) {
			tfpkt_err(tfp, "failed to alloc tx dr");
			goto fail;
		}
	}
	for (i = 0; i < TF_PKT_FM_CNT; i++) {
		if (tfpkt_alloc_dr(tfp, &tfp->tfp_fm_drs[i], TF_PKT_DR_FM,
		    i, TF_PKT_FM_DEPTH) != 0) {
			tfpkt_err(tfp, "failed to alloc fm dr");
			goto fail;
		}
	}
	for (i = 0; i < TF_PKT_CMP_CNT; i++) {
		if (tfpkt_alloc_dr(tfp, &tfp->tfp_cmp_drs[i], TF_PKT_DR_CMP,
		    i, TF_PKT_CMP_DEPTH) != 0) {
			tfpkt_err(tfp, "failed to alloc cmp dr");
			goto fail;
		}
	}

	return (0);

fail:
	tfpkt_free_drs(tfp);
	return (-1);
}

/*
 * Given a virtual address, search for the tfpkt_buf_t that contains it.
 */
static tfpkt_buf_t *
tfpkt_buf_by_va(list_t *list, caddr_t va)
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
tfpkt_buf_by_pa(list_t *list, uint64_t pa)
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
tfpkt_loaned_buf_by_va(tfpkt_t *tfp, list_t *list, caddr_t va)
{
	tfpkt_buf_t *buf = tfpkt_buf_by_va(list, va);

	if (buf == NULL) {
		tfpkt_err(tfp, "unrecognized loaned buf: %p", va);
	} else if ((buf->tfb_flags & TFPORT_BUF_LOANED) == 0) {
		tfpkt_err(tfp, "buf not marked as loaned: %p", va);
	}
	return (buf);
}

/*
 * Mark a tx buffer for loaning, and do the necessary accounting.
 */
static void
tfpkt_tx_loan(tfpkt_t *tfp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tfp->tfp_mutex));
	buf->tfb_flags |= TFPORT_BUF_LOANED;
	tfp->tfp_ntxbufs_onloan++;
	list_insert_tail(&tfp->tfp_txbufs_loaned, buf);
}

/*
 * Process the return of a tx buffer
 */
static void
tfpkt_tx_return(tfpkt_t *tfp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tfp->tfp_mutex));
	buf->tfb_flags &= ~TFPORT_BUF_LOANED;
	ASSERT(tfp->tfp_ntxbufs_onloan > 0);
	tfp->tfp_ntxbufs_onloan--;
}

/*
 * Mark an rx buffer for loaning, and do the necessary accounting.
 */
static void
tfpkt_rx_loan(tfpkt_t *tfp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tfp->tfp_mutex));
	buf->tfb_flags |= TFPORT_BUF_LOANED;
	tfp->tfp_nrxbufs_onloan++;
	list_insert_tail(&tfp->tfp_rxbufs_loaned, buf);
}

/*
 * Process the return of an rx buffer
 */
static void
tfpkt_rx_return(tfpkt_t *tfp, tfpkt_buf_t *buf)
{
	ASSERT(mutex_owned(&tfp->tfp_mutex));
	buf->tfb_flags &= ~TFPORT_BUF_LOANED;
	ASSERT(tfp->tfp_nrxbufs_onloan > 0);
	tfp->tfp_nrxbufs_onloan--;
}

/*
 * Allocate a transmit-ready buffer capable of holding at least sz bytes.
 *
 * The return value is the virtual address at which the data should be stored,
 * and which must be provided to the transmit routine.
 */
void *
tofino_tx_alloc(tofino_pkt_cookie_t cookie, size_t sz)
{
	tfpkt_handler_t *hdlr = (tfpkt_handler_t *)cookie;
	tfpkt_t *tfp = hdlr->th_tfpkt;
	dev_info_t *dip = tfp->tfp_dip;
	tfpkt_buf_t *buf;
	void *va;

	if (sz > TFPORT_BUF_SIZE) {
		dev_err(dip, CE_WARN, "packet too large");
		return (NULL);
	}

	mutex_enter(&tfp->tfp_mutex);
	buf = list_remove_head(&tfp->tfp_txbufs_free);
	if (buf == NULL) {
		tfp->tfp_txfail_no_bufs++;
		va = NULL;
	} else {
		va = buf->tfb_dma.tpd_addr;
		tfpkt_tx_loan(tfp, buf);
	}
	mutex_exit(&tfp->tfp_mutex);

	return (va);
}

/*
 * Return a transmit buffer to the freelist from whence it came.
 */
void
tofino_tx_free(tofino_pkt_cookie_t cookie, void *addr)
{
	tfpkt_handler_t *hdlr = (tfpkt_handler_t *)cookie;
	tfpkt_t *tfp = hdlr->th_tfpkt;
	tfpkt_buf_t *buf;

	mutex_enter(&tfp->tfp_mutex);
	buf = tfpkt_loaned_buf_by_va(tfp, &tfp->tfp_txbufs_loaned, addr);
	if (buf != NULL) {
		tfpkt_tx_return(tfp, buf);
		list_insert_tail(&tfp->tfp_txbufs_free, buf);
	} else {
		tfpkt_err(tfp, "freeing unknown buf %p", addr);
	}

	mutex_exit(&tfp->tfp_mutex);
}

/*
 * Push a single message to the ASIC.
 *
 * On success, that call returns 0 and consumes the provided buffer.  On
 * failure, the call returns -1 and buffer ownership remains with the caller.
 */
int
tofino_tx(tofino_pkt_cookie_t cookie, void *addr, size_t sz)
{
	tfpkt_handler_t *hdlr = (tfpkt_handler_t *)cookie;
	tfpkt_t *tfp = hdlr->th_tfpkt;
	dev_info_t *dip = tfp->tfp_dip;
	tfpkt_buf_t *buf;
	tfpkt_dr_t *drp = &tfp->tfp_tx_drs[0];
	tfpkt_dr_tx_t tx_dr;
	int rval;

	if (sz > TFPORT_BUF_SIZE) {
		dev_err(dip, CE_WARN, "packet too large");
		return (-1);
	}

	mutex_enter(&tfp->tfp_mutex);
	buf = tfpkt_loaned_buf_by_va(tfp, &tfp->tfp_txbufs_loaned, addr);
	mutex_exit(&tfp->tfp_mutex);
	if (buf == NULL)  {
		tfpkt_err(tfp, "sending unknown buf %p", addr);
		return (-1);
	}

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

	rval = tfpkt_dr_push(dip, drp, (uint64_t *)&tx_dr);
	mutex_enter(&tfp->tfp_mutex);
	if (rval == 0) {
		tfpkt_tx_return(tfp, buf);
		list_insert_tail(&tfp->tfp_txbufs_pushed, buf);
	} else {
		tfp->tfp_txfail_no_descriptors++;
		tfpkt_tx_loan(tfp, buf);
	}
	mutex_exit(&tfp->tfp_mutex);

	return (rval);
}

/*
 * The tfport driver has finished processing the received packet, so we are free
 * to reuse the buffer.
 */
void
tofino_rx_done(tofino_pkt_cookie_t cookie, void *addr, size_t sz)
{
	tfpkt_handler_t *hdlr = (tfpkt_handler_t *)cookie;
	tfpkt_t *tfp = hdlr->th_tfpkt;
	tfpkt_buf_t *buf;

	mutex_enter(&tfp->tfp_mutex);
	buf = tfpkt_loaned_buf_by_va(tfp, &tfp->tfp_rxbufs_loaned, addr);
	if (buf != NULL) {
		tfpkt_rx_return(tfp, buf);
		list_insert_tail(&tfp->tfp_rxbufs_free, buf);
	}
	mutex_exit(&tfp->tfp_mutex);
}

static void
tfpkt_process_rx(tfpkt_t *tfp, tfpkt_dr_t *drp, tfpkt_dr_rx_t *rx_dr)
{
	tfpkt_handler_t *hdlr = tfp->tfp_pkt_hdlr;
	tfpkt_buf_t *buf;
	int loan = 0;

	mutex_enter(&tfp->tfp_mutex);
	buf = tfpkt_buf_by_pa(&tfp->tfp_rxbufs_pushed, rx_dr->rx_addr);
	if (buf == NULL) {
		tfpkt_err(tfp, "unrecognized rx buf: %lx", rx_dr->rx_addr);
		mutex_exit(&tfp->tfp_mutex);
		return;
	}

	if (rx_dr->rx_type != TFPRT_RX_DESC_TYPE_PKT) {
		/* should never happen. */
		tfpkt_err(tfp, "non-pkt descriptor (%d) on %s", rx_dr->rx_type,
		    drp->tfdrp_name);
	} else if (hdlr != NULL) {
		if (tfp->tfp_nrxbufs_onloan < tfp->tfp_nrxbufs_onloan_max) {
			tfpkt_rx_loan(tfp, buf);
			loan = 1;
		} else {
			tfp->tfp_rxfail_excess_loans++;
		}
	}
	if (!loan) {
		list_insert_tail(&tfp->tfp_rxbufs_free, buf);
	}

	mutex_exit(&tfp->tfp_mutex);

	if (loan) {
		hdlr->th_rx_hdlr(hdlr->th_arg, buf->tfb_dma.tpd_addr,
		    rx_dr->rx_size);
	}
}

static void
tfpkt_process_cmp(tfpkt_t *tfp, tfpkt_dr_t *drp, tfpkt_dr_cmp_t *cmp_dr)
{
	tfpkt_buf_t *buf;

	mutex_enter(&tfp->tfp_mutex);
	buf = tfpkt_buf_by_pa(&tfp->tfp_txbufs_pushed, cmp_dr->cmp_addr);
	if (buf == NULL) {
		tfpkt_err(tfp, "unrecognized tx buf: %lx", cmp_dr->cmp_addr);
		mutex_exit(&tfp->tfp_mutex);
		return;
	}

	if (cmp_dr->cmp_type != TFPRT_TX_DESC_TYPE_PKT) {
		/* should never happen. */
		tfpkt_err(tfp, "non-pkt descriptor (%d) on %s", cmp_dr->cmp_type,
		    drp->tfdrp_name);
	}

	list_insert_tail(&tfp->tfp_txbufs_free, buf);
	mutex_exit(&tfp->tfp_mutex);
}

static uint32_t
tfpkt_dr_read(dev_info_t *dip, tfpkt_dr_t *drp, size_t offset)
{
	return (tofino_read_reg(dip, drp->tfdrp_reg_base + offset));
}

static void
tfpkt_dr_write(dev_info_t *dip, tfpkt_dr_t *drp, size_t offset, uint32_t val)
{
	if (0) dev_err(dip, CE_NOTE, "%s(%lx, %x)", __func__,
	    drp->tfdrp_reg_base + offset, val);

	tofino_write_reg(dip, drp->tfdrp_reg_base + offset, val);
}

static int
tfpkt_cmp_poll(tfpkt_t *tfp, int ring)
{
	tfpkt_dr_t *drp = &tfp->tfp_cmp_drs[ring];
	tfpkt_dr_cmp_t cmp_dr;
	int processed = 0;

	if (tfpkt_dr_pull(tfp->tfp_dip, drp, (uint64_t *)&cmp_dr) == 0) {
		tfpkt_process_cmp(tfp, drp, &cmp_dr);
		processed++;
	}

	return (processed);
}

static int
tfpkt_rx_poll(tfpkt_t *tfp, int ring)
{
	tfpkt_dr_t *drp = &tfp->tfp_rx_drs[ring];
	tfpkt_dr_rx_t rx_dr;
	int processed = 0;

	if (tfpkt_dr_pull(tfp->tfp_dip, drp, (uint64_t *)&rx_dr) == 0) {
		tfpkt_process_rx(tfp, drp, &rx_dr);
		processed++;
	}

	return (processed);
}

/*
 * Program the ASIC with the location, range, and characteristics of this
 * descriptor ring.
 */
static void
tfpkt_init_dr(tfpkt_t *tfp, tfpkt_dr_t *drp)
{
	dev_info_t *dip = tfp->tfp_dip;
	uint64_t phys;
	uint32_t ctrl;

	// the DR range has to be 64-byte aligned
	// virt = (drp->tfdrp_virt_base + 63ull) & ~(63ull);
	phys = (drp->tfdrp_phys_base + 63ull) & ~(63ull);

	/* disable DR  */
	ctrl = 0;
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_CTRL, ctrl);

	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_SIZE, drp->tfdrp_ring_size);
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_BASE_ADDR_LOW,
	    (uint32_t)(phys & 0xFFFFFFFFULL));
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_BASE_ADDR_HIGH,
	    (uint32_t)(phys >> 32));

	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_LIMIT_ADDR_LOW,
	    (uint32_t)((phys + drp->tfdrp_ring_size) & 0xFFFFFFFFULL));
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_LIMIT_ADDR_HIGH,
	    (uint32_t)((phys + drp->tfdrp_ring_size) >> 32));

	*drp->tfdrp_tail_ptr = 0;
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_HEAD_PTR, 0);
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_TAIL_PTR, 0);

	/* Tofino2 has two additional registers */
	if (tfp->tfp_gen == TOFINO_G_TF2) {
		tfpkt_dr_write(dip, drp, TBUS_DR_OFF_EMPTY_INT_TIME, 0);
		tfpkt_dr_write(dip, drp, TBUS_DR_OFF_EMPTY_INT_CNT, 0);
	}

	switch (drp->tfdrp_type) {
		case TF_PKT_DR_TX:
		case TF_PKT_DR_FM:
			ctrl = TBUS_DR_CTRL_HEAD_PTR_MODE;
			break;
		case TF_PKT_DR_RX:
			tfpkt_dr_write(dip, drp, TBUS_DR_OFF_DATA_TIMEOUT, 1);
			/* fallthru */
		case TF_PKT_DR_CMP:
			ctrl = TBUS_DR_CTRL_TAIL_PTR_MODE;
			break;
	}

	/* enable DR  */
	ctrl |= TBUS_DR_CTRL_ENABLE;
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_CTRL, ctrl);
}

/*
 * Push the configuration info for all of the DRs into the ASIC
 */
static int
tfpkt_init_drs(tfpkt_t *pkt)
{
	int i;

	for (i = 0; i < TF_PKT_FM_CNT; i++) {
		tfpkt_init_dr(pkt, &pkt->tfp_fm_drs[i]);
	}
	for (i = 0; i < TF_PKT_RX_CNT; i++) {
		tfpkt_init_dr(pkt, &pkt->tfp_rx_drs[i]);
	}
	for (i = 0; i < TF_PKT_TX_CNT; i++) {
		tfpkt_init_dr(pkt, &pkt->tfp_tx_drs[i]);
	}
	for (i = 0; i < TF_PKT_CMP_CNT; i++) {
		tfpkt_init_dr(pkt, &pkt->tfp_cmp_drs[i]);
	}

	return (0);
}

/*
 * Refresh our in-core copy of the tail pointer from the DR's config register.
 */
static void
tfpkt_dr_refresh_tail(dev_info_t *dip, tfpkt_dr_t *drp)
{
	drp->tfdrp_tail = tfpkt_dr_read(dip, drp, TBUS_DR_OFF_TAIL_PTR);
}

/*
 * Refresh our in-core copy of the head pointer from the DR's config register.
 */
static void
tfpkt_dr_refresh_head(dev_info_t *dip, tfpkt_dr_t *drp)
{
	drp->tfdrp_head = tfpkt_dr_read(dip, drp, TBUS_DR_OFF_HEAD_PTR);
}

#define	DR_PTR_WRAP_BIT (1 << 20)
#define	DR_PTR_GET_WRAP_BIT(p) ((p) & DR_PTR_WRAP_BIT)
#define	DR_PTR_GET_BODY(p) ((p) & (DR_PTR_WRAP_BIT - 1))

static int
tfpkt_dr_full(tfpkt_dr_t *drp)
{
	uint64_t head_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_head);
	uint64_t tail_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_tail);
	uint64_t head = DR_PTR_GET_BODY(drp->tfdrp_head);
	uint64_t tail = DR_PTR_GET_BODY(drp->tfdrp_tail);

	ASSERT(mutex_owned(&drp->tfdrp_mutex));

	return ((head == tail) && (head_wrap_bit != tail_wrap_bit));
}

static int
tfpkt_dr_empty(tfpkt_dr_t *drp)
{
	ASSERT(mutex_owned(&drp->tfdrp_mutex));
	return (drp->tfdrp_head == drp->tfdrp_tail);
}

/*
 * If the ring isn't full, advance the tail pointer to the next empty slot.
 * Return 0 if it advances, -1 if it doesn't.
 */
static int
tfpkt_dr_advance_tail(tfpkt_dr_t *drp)
{
	uint64_t tail, tail_wrap_bit;

	ASSERT(mutex_owned(&drp->tfdrp_mutex));
	if (tfpkt_dr_full(drp)) {
		return -1;
	}

	tail_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_tail);
	tail = DR_PTR_GET_BODY(drp->tfdrp_tail);
	tail += drp->tfdrp_desc_size;
	if (tail == drp->tfdrp_ring_size) {
		tail = 0;
		tail_wrap_bit ^= DR_PTR_WRAP_BIT;
	}

	drp->tfdrp_tail = tail | tail_wrap_bit;
	return 0;
}

/*
 * If the ring is non-empty, advance the head pointer to the next descriptor.
 * Return 0 if it advances, -1 if it doesn't.
 */
static int
tfpkt_dr_advance_head(tfpkt_dr_t *drp)
{
	uint64_t head, head_wrap_bit;

	ASSERT(mutex_owned(&drp->tfdrp_mutex));
	if (tfpkt_dr_empty(drp)) {
		return -1;
	}

	head_wrap_bit = DR_PTR_GET_WRAP_BIT(drp->tfdrp_head);
	head = DR_PTR_GET_BODY(drp->tfdrp_head);
	head += drp->tfdrp_desc_size;
	if (head == drp->tfdrp_ring_size) {
		head = 0;
		head_wrap_bit ^= DR_PTR_WRAP_BIT;
	}
	drp->tfdrp_head = head | head_wrap_bit;
	return 0;
}

/*
 * Pull a single descriptor off the head of a ring.
 * Returns 0 if it successfully gets a descriptor, -1 if the ring is empty.
 */
static int
tfpkt_dr_pull(dev_info_t *dip, tfpkt_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, head;

	mutex_enter(&drp->tfdrp_mutex);
	tfpkt_dr_refresh_tail(dip, drp);
	if (tfpkt_dr_empty(drp)) {
		mutex_exit(&drp->tfdrp_mutex);
		return (-1);
	}

	head = DR_PTR_GET_BODY(drp->tfdrp_head);
	slot = (uint64_t *) (drp->tfdrp_virt_base + head);
	if (0) dev_err(dip, CE_NOTE, "pulling from %s at %ld (%p)",
		drp->tfdrp_name, drp->tfdrp_head, (void *)slot);

	for (int i = 0; i < (drp->tfdrp_desc_size >> 3); i++)
		desc[i] = slot[i];

	tfpkt_dr_advance_head(drp);
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_HEAD_PTR, drp->tfdrp_head);
	mutex_exit(&drp->tfdrp_mutex);

	return (0);
}

/*
 * Push a single descriptor onto the tail of a ring
 * Returns 0 if it successfully pushes a descriptor, -1 if the ring is full.
 */
static int
tfpkt_dr_push(dev_info_t *dip, tfpkt_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, tail;

	mutex_enter(&drp->tfdrp_mutex);
	if (0) dev_err(dip, CE_NOTE, "pushing to %s at %ld / %ld",
		drp->tfdrp_name, drp->tfdrp_tail, drp->tfdrp_depth);
	tfpkt_dr_refresh_head(dip, drp);
	if (tfpkt_dr_full(drp)) {
		mutex_exit(&drp->tfdrp_mutex);
		return -1;
	}

	tail = DR_PTR_GET_BODY(drp->tfdrp_tail);
	slot = (uint64_t *) (drp->tfdrp_virt_base + tail);
	for (int i = 0; i < (drp->tfdrp_desc_size >> 3); i++)
		slot[i] = desc[i];

	tfpkt_dr_advance_tail(drp);
	tail = DR_PTR_GET_BODY(drp->tfdrp_tail);
	*drp->tfdrp_tail_ptr = tail;
	tfpkt_dr_write(dip, drp, TBUS_DR_OFF_TAIL_PTR, drp->tfdrp_tail);
	mutex_exit(&drp->tfdrp_mutex);

	return (0);
}

/*
 * Push a free DMA buffer onto a free_memory descriptor ring.
 */
static int
tfpkt_push_fm(dev_info_t *dip, tfpkt_dr_t *drp, uint64_t addr, uint64_t size)
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

	return (tfpkt_dr_push(dip, drp, &descriptor));
}

/*
 * Push all free receive buffers onto the free_memory DR until the ring is full,
 * or we run out of buffers.
 */
static int
tfpkt_push_free_bufs(dev_info_t *dip, tfpkt_t *tfp, int ring)
{
	int pushed = 0;
	uint64_t dma_addr;
	tfpkt_dr_t *drp = &tfp->tfp_fm_drs[ring];
	tfpkt_buf_t *buf, *next;

	mutex_enter(&tfp->tfp_mutex);
	for (buf = list_head(&tfp->tfp_rxbufs_free); buf != NULL; buf = next) {
		next = list_next(&tfp->tfp_rxbufs_free, buf);
		dma_addr = buf->tfb_dma.tpd_cookie.dmac_laddress;
		if (tfpkt_push_fm(dip, drp, dma_addr, TFPORT_BUF_SIZE) < 0)
			break;
		list_remove(&tfp->tfp_rxbufs_free, buf);
		list_insert_tail(&tfp->tfp_rxbufs_pushed, buf);
		pushed++;
	}
	mutex_exit(&tfp->tfp_mutex);

	return (pushed);
}

/*
 * Register as the upstream tfport driver for packet data.
 *
 * If there is already a handler registered, return NULL.  Otherwise, return an
 * opaque handle which can be used in all subsequent interactions with this
 * driver.
 */
tfpkt_handler_t *
tfpkt_reg_handler(tfpkt_t *tfp, tofino_rx_handler_t rx,
    tofino_cmp_handler_t cmp, void *arg)
{
	tfpkt_handler_t *h = NULL;

	mutex_enter(&tfp->tfp_mutex);
	if (tfp->tfp_pkt_hdlr == NULL) {
		h = kmem_zalloc(sizeof (*h), KM_SLEEP);
		h->th_tfpkt = tfp;
		h->th_arg = arg;
		h->th_rx_hdlr = rx;
		h->th_cmp_hdlr = cmp;
		tfp->tfp_pkt_hdlr = h;
	}
	mutex_exit(&tfp->tfp_mutex);

	return (h);
}

/*
 * Unregister a tfport handler.  The caller cannot unregister while it still
 * holds any of our buffers.
 */
int
tfpkt_unreg_handler(tfpkt_t *tfp, tfpkt_handler_t *h)
{
	int rval;

	mutex_enter(&tfp->tfp_mutex);
	if (tfp->tfp_pkt_hdlr != h) {
		rval = EINVAL;
	} else if (!list_is_empty(&tfp->tfp_rxbufs_loaned)) {
		tfpkt_err(tfp, "unregister with rx buffers still on loan");
		rval = EBUSY;
	} else if (!list_is_empty(&tfp->tfp_txbufs_loaned)) {
		tfpkt_err(tfp, "unregister with tx buffers still on loan");
		rval = EBUSY;
	} else {
		kmem_free(tfp->tfp_pkt_hdlr, sizeof (*tfp->tfp_pkt_hdlr));
		tfp->tfp_pkt_hdlr = NULL;
		rval = 0;
	}
	mutex_exit(&tfp->tfp_mutex);

	return (rval);
}

/*
 * Enable or disable all of the tbus interrupts.
 */
static void
tfpkt_intr_set(tfpkt_t *tfp, bool enable)
{
	uint32_t en0 = enable ? TBUS_INT0_CPL_EVENT : 0;
	uint32_t en1 = enable ? TBUS_INT1_RX_EVENT : 0;
	uint32_t shadow_msk_base = 0xc0;
	int intr_lo = 32;
	int intr_hi = 63;

	/*
	 * Tofino defines 70 different conditions that can trigger a tbus
	 * interrupt.  We're only looking for a subset of them: those that
	 * indicate a change in the completion and/or rx descriptor rings.
	 */
	for (int intr = intr_lo ; intr <= intr_hi; intr++) {
		/*
		 * XXX: This is the long, canonical way to unmask the
		 * interrupts we care about.  This whole loop works out to
		 * setting reg 0xc4 to 0.
		 */
		uint32_t intr_reg = intr >> 5;
		uint32_t intr_bit = intr & 0x1f;
		uint32_t bit_fld = (1u << intr_bit);

		uint32_t shadow_msk_reg = shadow_msk_base + (4 * intr_reg);
		uint32_t old = tofino_read_reg(tfp->tfp_dip, shadow_msk_reg);

		uint32_t mask = old & ~bit_fld;
		tofino_write_reg(tfp->tfp_dip, shadow_msk_reg, mask);
	}

	if (tfp->tfp_gen == TOFINO_G_TF1) {
		tofino_write_reg(tfp->tfp_dip, TF_REG_TBUS_INT_EN0_1, en0);
		tofino_write_reg(tfp->tfp_dip, TF_REG_TBUS_INT_EN1_1, en1);
	} else {
		ASSERT(tfp->tfp_gen == TOFINO_G_TF2);
		tofino_write_reg(tfp->tfp_dip, TF2_REG_TBUS_INT_EN0_1, en0);
		tofino_write_reg(tfp->tfp_dip, TF2_REG_TBUS_INT_EN1_1, en1);
	}

	/*
	 * Unconditionally disable the interrupts we're not looking for
	 */
	if (tfp->tfp_gen == TOFINO_G_TF1) {
		tofino_write_reg(tfp->tfp_dip, TF_REG_TBUS_INT_EN2_1, 0);
		tofino_write_reg(tfp->tfp_dip, TF_REG_TBUS_INT_EN0_0, 0);
		tofino_write_reg(tfp->tfp_dip, TF_REG_TBUS_INT_EN1_0, 0);
		tofino_write_reg(tfp->tfp_dip, TF_REG_TBUS_INT_EN2_0, 0);
	} else {
		ASSERT(tfp->tfp_gen == TOFINO_G_TF2);
		tofino_write_reg(tfp->tfp_dip, TF2_REG_TBUS_INT_EN2_1, 0);
		tofino_write_reg(tfp->tfp_dip, TF2_REG_TBUS_INT_EN0_0, 0);
		tofino_write_reg(tfp->tfp_dip, TF2_REG_TBUS_INT_EN1_0, 0);
		tofino_write_reg(tfp->tfp_dip, TF2_REG_TBUS_INT_EN2_0, 0);
	}

	tfpkt_log(tfp, "%s interrupts", enable ? "enabled" : "disabled");
}

/*
 * Setup the tbus control register to enable the pci network port
 */
static void
tfpkt_port_init(tfpkt_t *tfp, dev_info_t *tf_dip)
{
	tf_tbus_ctrl_t ctrl;
	uint32_t *ctrlp = (uint32_t *)&ctrl;

	ASSERT(tfp->tfp_gen == TOFINO_G_TF1 || tfp->tfp_gen == TOFINO_G_TF2);
	if (tfp->tfp_gen == TOFINO_G_TF1) {
		*ctrlp = tofino_read_reg(tf_dip, TF_REG_TBUS_CTRL);
	} else {
		*ctrlp = tofino_read_reg(tf_dip, TF2_REG_TBUS_CTRL);
	}

	ctrl.tftc_pfc_fm = 0x03;
	ctrl.tftc_pfc_rx = 0x03;
	ctrl.tftc_port_alive = 1;
	ctrl.tftc_rx_en = 1;
	ctrl.tftc_ecc_dec_dis = 0;
	ctrl.tftc_crcchk_dis = 1;
	ctrl.tftc_crcrmv_dis = 0;

	if (tfp->tfp_gen == TOFINO_G_TF1) {
		tofino_write_reg(tf_dip, TF_REG_TBUS_CTRL, *ctrlp);
	} else {
		ctrl.tftc_rx_channel_offset = 0;
		ctrl.tftc_crcerr_keep = 1;
		tofino_write_reg(tf_dip, TF2_REG_TBUS_CTRL, *ctrlp);
	}
}

static uint_t
tpkt_intr(caddr_t arg1, caddr_t arg2)
{
	tfpkt_t *tfp = (tfpkt_t *)arg1;
	int processed = 1;

	while (processed > 0) {
		processed = 0;
		for (int i = 0; i < TF_PKT_RX_CNT; i++) {
			if (tfpkt_rx_poll(tfp, i) > 0) {
				processed++;
				tfpkt_push_free_bufs(tfp->tfp_dip, tfp, i);
			}
		}

		for (int i = 0; i < TF_PKT_CMP_CNT; i++) {
			if (tfpkt_cmp_poll(tfp, i)) {
				processed++;
			}
		}
	}

	return (DDI_INTR_CLAIMED);
}

int
tfpkt_init(tofino_t *tf)
{
	dev_info_t *tf_dip = tf->tf_dip;
	tfpkt_t *tfp;
	ddi_softint_handle_t sh = 0;
	int err;

	dev_err(tf->tf_dip, CE_NOTE, "%s", __func__);

	ASSERT(mutex_owned(&tf->tf_mutex));
	if (tf->tf_pkt_state != NULL)
		return (EBUSY);

	tfp = kmem_zalloc(sizeof (*tfp), KM_SLEEP);
	tfp->tfp_gen = tf->tf_gen;
	tfp->tfp_mtu = ETHERMTU;
	tfp->tfp_dip = tf_dip;
	mutex_init(&tfp->tfp_mutex, NULL, MUTEX_DRIVER, NULL);

	/* disable tbus interrupts */
	tfpkt_intr_set(tfp, B_FALSE);

	mutex_enter(&tfp->tfp_mutex);
	err = ddi_intr_add_softint(tf_dip, &sh, DDI_INTR_SOFTPRI_DEFAULT,
	    tpkt_intr, tfp);

	if (err != 0) {
		dev_err(tf_dip, CE_WARN, "failed to allocate softint");
	} else if ((err = tfpkt_alloc_bufs(tfp)) != 0) {
		dev_err(tf_dip, CE_WARN, "failed to allocate buffers");
	} else if ((err = tfpkt_alloc_drs(tfp)) != 0) {
		dev_err(tf_dip, CE_WARN, "failed to allocate drs");
	} else if ((err = tfpkt_init_drs(tfp)) != 0) {
		dev_err(tf_dip, CE_WARN, "failed to init drs");
	}
	mutex_exit(&tfp->tfp_mutex);

	if (err != 0) {
		if (sh != 0) {
			ddi_intr_remove_softint(sh);
		}
		tfpkt_free_drs(tfp);
		tfpkt_free_bufs(tfp);
		kmem_free(tfp, sizeof (*tfp));
		return (err);
	}

	tfpkt_port_init(tfp, tf_dip);
	for (int i = 0; i < TF_PKT_RX_CNT; i++)
		(void) tfpkt_push_free_bufs(tf_dip, tfp, i);

	/* enable tbus interrupts */
	tfp->tfp_softint = sh;
	tfpkt_intr_set(tfp, B_TRUE);
	tf->tf_pkt_state = tfp;

	return (0);
}

int
tfpkt_fini(tofino_t *tf)
{
	tfpkt_t *tfp;

	if ((tfp = tf->tf_pkt_state) != NULL) {
		mutex_enter(&tfp->tfp_mutex);
		ddi_intr_remove_softint(tfp->tfp_softint);
		tfpkt_free_bufs(tfp);
		mutex_exit(&tfp->tfp_mutex);
		mutex_destroy(&tfp->tfp_mutex);
		kmem_free(tfp, sizeof (*tfp));
		tf->tf_pkt_state = NULL;
	}

	return (0);
}
