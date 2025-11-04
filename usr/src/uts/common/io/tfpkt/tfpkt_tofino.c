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
 * Copyright 2025 Oxide Computer Company
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
 * We preallocate buffers that are capable of DMA to/from the tofino.  This
 * tuneable determines the size of those buffers.
 */
uint32_t tfpkt_buf_size = 2048;

/*
 * The following tuneables determine the number of entries in each descriptor
 * ring.
 */
uint32_t tfpkt_rx_depth = 256;
uint32_t tfpkt_tx_depth = 256;

static tfpkt_tbus_stats_t tfpkt_tbus_stats_template = {
	{ "ttb_rxfail_no_descriptors",		KSTAT_DATA_UINT64 },
	{ "ttb_rxfail_bad_descriptor_type",	KSTAT_DATA_UINT64 },
	{ "ttb_rxfail_unknown_buf",		KSTAT_DATA_UINT64 },
	{ "ttb_txfail_pkt_too_large",		KSTAT_DATA_UINT64 },
	{ "ttb_txfail_no_bufs",			KSTAT_DATA_UINT64 },
	{ "ttb_txfail_no_descriptors",		KSTAT_DATA_UINT64 },
	{ "ttb_txfail_bad_descriptor_type",	KSTAT_DATA_UINT64 },
	{ "ttb_txfail_unknown_buf",		KSTAT_DATA_UINT64 },
	{ "ttb_txfail_other",			KSTAT_DATA_UINT64 },
};

#define	TBUS_STAT_BUMP(TBP, STAT) \
	atomic_inc_64(&(TBP)->ttb_stats.STAT.value.ui64)

/*
 * Forward references
 */
static int tfpkt_dr_push(tfpkt_tbus_t *, tfpkt_dr_t *, uint64_t *);
static int tfpkt_dr_pull(tfpkt_tbus_t *, tfpkt_dr_t *, uint64_t *);
static int tfpkt_tbus_push_free_bufs(tfpkt_tbus_t *, int);
static void tfpkt_tbus_reset_detected(tfpkt_t *);

static void
tfpkt_tbus_dlog(tfpkt_tbus_t *tbp, const char *fmt, ...)
{
	va_list args;

	if (tfpkt_tbus_debug) {
		va_start(args, fmt);
		vdev_err(tbp->ttb_tfpkt_dip, CE_NOTE, fmt, args);
		va_end(args);
	}
}

static void
tfpkt_tbus_err(tfpkt_tbus_t *tbp, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(tbp->ttb_tfpkt_dip, CE_WARN, fmt, args);
	va_end(args);
}

uint64_t
tfpkt_buf_pa(tfpkt_buf_t *buf)
{
	return (buf->tfb_dma.tpd_cookie.dmac_laddress);
}

caddr_t
tfpkt_buf_va(tfpkt_buf_t *buf)
{
	return (buf->tfb_dma.tpd_addr);
}

/* 4k aligned DMA for in-kernel buffers */
static const ddi_dma_attr_t tfpkt_tbus_dma_attr = {
	.dma_attr_version =		DMA_ATTR_V0,
	.dma_attr_addr_lo =		0x0000000000000000ull,
	.dma_attr_addr_hi =		0xFFFFFFFFFFFFFFFFull,
	.dma_attr_count_max =		0x00000000FFFFFFFFull,
	.dma_attr_align =		0x0000000000001000ull,
	.dma_attr_burstsizes =		0x00000FFF,
	.dma_attr_minxfer =		0x00000001,
	.dma_attr_maxxfer =		0x00000000FFFFFFFFull,
	.dma_attr_seg =			0xFFFFFFFFFFFFFFFFull,
	.dma_attr_sgllen =		1,
	.dma_attr_granular =		1,
	.dma_attr_flags =		0,
};

static const ddi_device_acc_attr_t tfpkt_tbus_acc_attr = {
	.devacc_attr_version =		DDI_DEVICE_ATTR_V1,
	.devacc_attr_endian_flags =	DDI_STRUCTURE_LE_ACC,
	.devacc_attr_dataorder =	DDI_STRICTORDER_ACC,
	.devacc_attr_access =		DDI_DEFAULT_ACC,
};

/*
 * Allocate a single buffer capable of DMA to/from the Tofino ASIC.
 */
static int
tbus_dma_alloc(tfpkt_tbus_t *tbp, tf_tbus_dma_t *dmap, size_t size,
    int flags)
{
	unsigned int count;
	int err;

	err = ddi_dma_alloc_handle(tbp->ttb_tfpkt_dip, &tfpkt_tbus_dma_attr,
	    DDI_DMA_SLEEP, NULL, &dmap->tpd_handle);
	if (err != DDI_SUCCESS) {
		tfpkt_tbus_err(tbp, "!%s: alloc_handle failed: %d",
		    __func__, err);
		goto fail0;
	}

	err = ddi_dma_mem_alloc(dmap->tpd_handle, size, &tfpkt_tbus_acc_attr,
	    DDI_DMA_STREAMING, DDI_DMA_SLEEP, NULL, &dmap->tpd_addr,
	    &dmap->tpd_len, &dmap->tpd_acchdl);
	if (err != DDI_SUCCESS) {
		tfpkt_tbus_err(tbp, "!%s: mem_alloc failed", __func__);
		goto fail1;
	}

	err = ddi_dma_addr_bind_handle(dmap->tpd_handle, NULL, dmap->tpd_addr,
	    dmap->tpd_len, flags, DDI_DMA_SLEEP, NULL, &dmap->tpd_cookie,
	    &count);
	if (err != DDI_DMA_MAPPED) {
		tfpkt_tbus_err(tbp, "!%s: bind_handle failed", __func__);
		goto fail2;
	}

	if (count > 1) {
		tfpkt_tbus_err(tbp, "!%s: more than one DMA cookie", __func__);
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
 * tf_tbus_dma_t structure itself.
 */
static void
tofino_tbus_dma_free(tf_tbus_dma_t *dmap)
{
	VERIFY3S(ddi_dma_unbind_handle(dmap->tpd_handle), ==, DDI_SUCCESS);
	ddi_dma_mem_free(&dmap->tpd_acchdl);
	ddi_dma_free_handle(&dmap->tpd_handle);
}

static void
tfpkt_buf_list_init(tfpkt_buf_list_t *list)
{
	bzero(list, sizeof (*list));
	list->tbl_low_water = UINT64_MAX;
	mutex_init(&list->tbl_mutex, NULL, MUTEX_DRIVER, NULL);
	list_create(&list->tbl_data, sizeof (tfpkt_buf_t),
	    offsetof(tfpkt_buf_t, tfb_link));
}

static void
tfpkt_buf_list_fini(tfpkt_buf_list_t *list)
{
	mutex_destroy(&list->tbl_mutex);
	list_destroy(&list->tbl_data);
}

static void
tfpkt_buf_remove_locked(tfpkt_buf_list_t *list, tfpkt_buf_t *buf)
{
	ASSERT(MUTEX_HELD(&list->tbl_mutex));
	list_remove(&list->tbl_data, buf);
	ASSERT3U(list->tbl_count, >, 0);
	if (--list->tbl_count < list->tbl_low_water)
		list->tbl_low_water = list->tbl_count;
}

/*
 * Remove a specific buffer from the list
 */
static void
tfpkt_buf_remove(tfpkt_buf_list_t *list, tfpkt_buf_t *buf)
{
	mutex_enter(&list->tbl_mutex);

#ifdef DEBUG
	tfpkt_buf_t *scan;
	for (scan = list_head(&list->tbl_data);
	    scan != NULL && scan != buf;
	    scan = list_next(&list->tbl_data, scan))
		;
	VERIFY3P(buf, ==, scan);
#endif

	tfpkt_buf_remove_locked(list, buf);
	mutex_exit(&list->tbl_mutex);
}

/*
 * Pull a single buffer from the head of the list.
 */
static tfpkt_buf_t *
tfpkt_buf_alloc(tfpkt_buf_list_t *list)
{
	tfpkt_buf_t *buf;

	mutex_enter(&list->tbl_mutex);
	buf = list_head(&list->tbl_data);
	if (buf == NULL) {
		list->tbl_alloc_fail = true;
		list->tbl_alloc_fails++;
	} else {
		tfpkt_buf_remove_locked(list, buf);
	}
	mutex_exit(&list->tbl_mutex);

	return (buf);
}

/*
 * Pull a single buffer from the head of the list.  This differs from
 * tfpkt_buf_alloc() in that it isn't an error if the list is empty.
 */
static tfpkt_buf_t *
tfpkt_buf_pop(tfpkt_buf_list_t *list)
{
	tfpkt_buf_t *buf;

	mutex_enter(&list->tbl_mutex);
	buf = list_head(&list->tbl_data);
	if (buf != NULL)
		tfpkt_buf_remove_locked(list, buf);
	mutex_exit(&list->tbl_mutex);

	return (buf);
}

/*
 * Given a virtual address, search for the tfpkt_buf_t that contains it.
 */
static tfpkt_buf_t *
tfpkt_buf_by_va(tfpkt_buf_list_t *list, caddr_t va)
{
	tfpkt_buf_t *buf;

	mutex_enter(&list->tbl_mutex);
	for (buf = list_head(&list->tbl_data);
	    buf != NULL;
	    buf = list_next(&list->tbl_data, buf)) {
		if (tfpkt_buf_va(buf) == va) {
			tfpkt_buf_remove_locked(list, buf);
			break;
		}
	}
	if (buf == NULL)
		list->tbl_va_lookup_fails++;

	mutex_exit(&list->tbl_mutex);

	return (buf);
}

/*
 * Given a physical address, search for the tfpkt_buf_t that contains it.
 */
static tfpkt_buf_t *
tfpkt_buf_by_pa(tfpkt_buf_list_t *list, uint64_t pa)
{
	tfpkt_buf_t *buf;

	mutex_enter(&list->tbl_mutex);
	for (buf = list_head(&list->tbl_data);
	    buf != NULL;
	    buf = list_next(&list->tbl_data, buf)) {
		if (tfpkt_buf_pa(buf) == pa) {
			tfpkt_buf_remove_locked(list, buf);
			break;
		}
	}
	if (buf == NULL)
		list->tbl_pa_lookup_fails++;

	mutex_exit(&list->tbl_mutex);

	return (buf);
}

/*
 * Push a buffer on the list.  Returns "true" if this buffer is refilling a list
 * which had failed an allocation due to being empty.  Returns "false" if the
 * list was already populated, or hasn't had any allocation attempts since
 * running dry.
 */
static bool
tfpkt_buf_insert(tfpkt_buf_list_t *list, tfpkt_buf_t *buf)
{
	bool rval;

	mutex_enter(&list->tbl_mutex);
	list_insert_tail(&list->tbl_data, buf);
	rval = list->tbl_alloc_fail;
	if (rval) {
		ASSERT(list->tbl_count == 0);
		list->tbl_alloc_fail = false;
	}

	if (++list->tbl_count > list->tbl_high_water)
		list->tbl_high_water = list->tbl_count;
	mutex_exit(&list->tbl_mutex);

	return (rval);
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
	ASSERT3U(tfp->tfp_tbus_refcnt, >, 0);
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
 * Free all of the buffers on a list.  Returns the number of buffers freed.
 */
static int
tfpkt_tbus_list_free_all(tfpkt_buf_list_t *list)
{
	tfpkt_buf_t *buf;
	uint16_t freed = 0;

	while ((buf = tfpkt_buf_pop(list)) != NULL) {
		if (buf->tfb_flags & TFPKT_BUF_DMA_ALLOCED) {
			tofino_tbus_dma_free(&buf->tfb_dma);
			buf->tfb_flags &= ~TFPKT_BUF_DMA_ALLOCED;
		}
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
	uint32_t freed;

	if (tbp->ttb_bufs_mem == NULL)
		return;

	VERIFY3U(tfpkt_tbus_list_free_all(&tbp->ttb_rxbufs_inuse), ==, 0);
	VERIFY3U(tfpkt_tbus_list_free_all(&tbp->ttb_txbufs_inuse), ==, 0);

	freed = tfpkt_tbus_list_free_all(&tbp->ttb_rxbufs_free);
	freed += tfpkt_tbus_list_free_all(&tbp->ttb_rxbufs_pushed);
	freed += tfpkt_tbus_list_free_all(&tbp->ttb_txbufs_free);
	freed += tfpkt_tbus_list_free_all(&tbp->ttb_txbufs_pushed);

	if (freed != tbp->ttb_bufs_capacity)
		tfpkt_tbus_err(tbp, "!lost track of %d/%d buffers",
		    tbp->ttb_bufs_capacity - freed, tbp->ttb_bufs_capacity);

	tfpkt_buf_list_fini(&tbp->ttb_rxbufs_free);
	tfpkt_buf_list_fini(&tbp->ttb_rxbufs_pushed);
	tfpkt_buf_list_fini(&tbp->ttb_rxbufs_inuse);
	tfpkt_buf_list_fini(&tbp->ttb_txbufs_free);
	tfpkt_buf_list_fini(&tbp->ttb_txbufs_pushed);
	tfpkt_buf_list_fini(&tbp->ttb_txbufs_inuse);

	kmem_free(tbp->ttb_bufs_mem,
	    sizeof (tfpkt_buf_t) * tbp->ttb_bufs_capacity);
	tbp->ttb_bufs_mem = NULL;
	tbp->ttb_bufs_capacity = 0;
}

/*
 * Allocate memory for the buffers used when staging packet data into and out of
 * the ASIC.  Each buffer is the same size and the number of buffers is fixed at
 * startup.
 */
static int
tfpkt_tbus_alloc_bufs(tfpkt_tbus_t *tbp)
{
	uint32_t rx_bufs, tx_bufs;

	/*
	 * We want to allocate slightly more buffers than required to fill
	 * each ring, allowing us to fully utilize the asic while still having
	 * memory available for packets being processed in the kernel.
	 */
	rx_bufs = TFPKT_RX_CNT * (tfpkt_rx_depth + 8);
	tx_bufs = TFPKT_TX_CNT * (tfpkt_rx_depth + 8);

	tbp->ttb_bufs_capacity = rx_bufs + tx_bufs;
	tbp->ttb_bufs_mem = kmem_zalloc(
	    sizeof (tfpkt_buf_t) * tbp->ttb_bufs_capacity, KM_SLEEP);
	tfpkt_buf_list_init(&tbp->ttb_rxbufs_free);
	tfpkt_buf_list_init(&tbp->ttb_rxbufs_pushed);
	tfpkt_buf_list_init(&tbp->ttb_rxbufs_inuse);
	tfpkt_buf_list_init(&tbp->ttb_txbufs_free);
	tfpkt_buf_list_init(&tbp->ttb_txbufs_pushed);
	tfpkt_buf_list_init(&tbp->ttb_txbufs_inuse);

	for (uint_t i = 0; i < tbp->ttb_bufs_capacity; i++) {
		tfpkt_buf_t *buf = &tbp->ttb_bufs_mem[i];
		if (tbus_dma_alloc(tbp, &buf->tfb_dma, tfpkt_buf_size,
		    DDI_DMA_STREAMING | DDI_DMA_RDWR) != 0) {
			goto fail;
		}
		buf->tfb_flags |= TFPKT_BUF_DMA_ALLOCED;
		buf->tfb_tbus = tbp;
		if (i < rx_bufs)
			(void) tfpkt_buf_insert(&tbp->ttb_rxbufs_free, buf);
		else
			(void) tfpkt_buf_insert(&tbp->ttb_txbufs_free, buf);
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
		tofino_tbus_dma_free(&drp->tdr_dma);
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
		ASSERT3U(tbp->ttb_gen, ==, TOFINO_G_TF2);
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
	 * The DR size must be a power-of-2 multiple of 64 bits no larger than
	 * 1MB.
	 */
	ring_sz = depth * desc_sz * sizeof (uint64_t);
	if (ring_sz > (1024 * 1024)) {
		ring_sz = (1024 * 1024);
	} else {
		ring_sz = 1 << (ddi_fls(ring_sz) - 1);
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
	 * we'll follow suit.  Note that the size alignment requirement in
	 * combination with the final pointer
	 */
	total_sz = ring_sz + sizeof (uint64_t);
	if (tbus_dma_alloc(tbp, &drp->tdr_dma, total_sz,
	    DDI_DMA_STREAMING | DDI_DMA_RDWR) != 0) {
		return (-1);
	}

	(void) snprintf(drp->tdr_name, sizeof (drp->tdr_name), "%s_%d",
	    prefix, dr_id);
	mutex_init(&drp->tdr_mutex, NULL, MUTEX_DRIVER, NULL);
	drp->tdr_locked = false;
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

	tfpkt_tbus_dlog(tbp, "!allocated DR %s.  phys_base: %llx  reg: %lx",
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
		    i, tfpkt_rx_depth) != 0) {
			tfpkt_tbus_err(tbp, "!failed to alloc rx dr");
			goto fail;
		}
	}
	for (i = 0; i < TFPKT_TX_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_tx_drs[i], TFPKT_DR_TX,
		    i, tfpkt_tx_depth) != 0) {
			tfpkt_tbus_err(tbp, "!failed to alloc tx dr");
			goto fail;
		}
	}
	for (i = 0; i < TFPKT_FM_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_fm_drs[i], TFPKT_DR_FM,
		    i, tfpkt_rx_depth) != 0) {
			tfpkt_tbus_err(tbp, "!failed to alloc fm dr");
			goto fail;
		}
	}
	for (i = 0; i < TFPKT_CMP_CNT; i++) {
		if (tfpkt_tbus_alloc_dr(tbp, &tbp->ttb_cmp_drs[i], TFPKT_DR_CMP,
		    i, tfpkt_tx_depth) != 0) {
			tfpkt_tbus_err(tbp, "!failed to alloc cmp dr");
			goto fail;
		}
	}

	return (0);

fail:
	tfpkt_tbus_free_drs(tbp);
	return (-1);
}

/*
 * Allocate a transmit-ready buffer capable of holding at least sz bytes.
 */
tfpkt_buf_t *
tfpkt_tbus_tx_alloc(tfpkt_tbus_t *tbp, size_t sz)
{
	tfpkt_buf_t *buf = NULL;

	if (sz > tfpkt_buf_size) {
		TBUS_STAT_BUMP(tbp, ttb_txfail_pkt_too_large);
	} else if ((buf = tfpkt_buf_alloc(&tbp->ttb_txbufs_free)) == NULL) {
		TBUS_STAT_BUMP(tbp, ttb_txfail_no_bufs);
	} else {
		(void) tfpkt_buf_insert(&tbp->ttb_txbufs_inuse, buf);
	}

	return (buf);
}

/*
 * Return a transmit buffer to the freelist from whence it came.
 */
void
tfpkt_tbus_tx_free(tfpkt_tbus_t *tbp, tfpkt_buf_t *buf)
{
	tfpkt_buf_remove(&tbp->ttb_txbufs_inuse, buf);
	if (tfpkt_buf_insert(&tbp->ttb_txbufs_free, buf)) {
		tfpkt_t *tfp = tbp->ttb_tfp;

		mutex_enter(&tfp->tfp_mutex);
		tfp->tfp_stats.tps_tx_updates.value.ui64++;
		mutex_exit(&tfp->tfp_mutex);
		/* Let mac know we just repopulated the freelist */
		mac_tx_update(tfp->tfp_mh);
	}
}

/*
 * Select a tx ring for this buffer.  We currently just use a simple
 * round-robin, but we could try something more clever in the future.
 */
static uint32_t
tfpkt_tx_ring(tfpkt_tbus_t *tbp, void *addr, size_t sz)
{
	static uint32_t next_ring = 0;
	uint32_t proposed, rval;

	while (1) {
		rval = next_ring;
		proposed = (rval + 1) % TFPKT_TX_CNT;
		if (atomic_cas_32(&next_ring, rval, proposed) == rval)
			return (rval);
	}
}

/*
 * Push a single message to the ASIC.
 *
 * On success, that call returns 0 and consumes the provided buffer.  On
 * failure, the call returns -1 and buffer ownership remains with the caller.
 */
int
tfpkt_tbus_tx(tfpkt_tbus_t *tbp, tfpkt_buf_t *buf, size_t sz)
{
	tfpkt_dr_t *drp;
	tfpkt_dr_tx_t tx_dr;
	uint32_t ring;
	int rval;

	tfpkt_buf_remove(&tbp->ttb_txbufs_inuse, buf);
	bzero(&tx_dr, sizeof (tx_dr));
	tx_dr.tx_s = 1;
	tx_dr.tx_e = 1;
	tx_dr.tx_type = TFPRT_TX_DESC_TYPE_PKT;
	tx_dr.tx_size = sz;
	tx_dr.tx_src = tfpkt_buf_pa(buf);

	/*
	 * the reference driver sets the dst field to the same address, but has
	 * a comment asking if it's necessary.  Let's find out...
	 */
	tx_dr.tx_msg_id = tx_dr.tx_src;

	/*
	 * Try to push the descriptor onto the selected ring.  If the initial
	 * ring is full, we try each of the others in turn before giving up.
	 * This is fine with our simple ring-selection algorithm, but may not be
	 * acceptable with something more sophisticated.
	 */
	(void) tfpkt_buf_insert(&tbp->ttb_txbufs_pushed, buf);

	rval = 0;
	ring = tfpkt_tx_ring(tbp, tfpkt_buf_va(buf), sz);
	for (uint32_t i = 0; i < TFPKT_TX_CNT; i++) {
		drp = &tbp->ttb_tx_drs[ring];
		if ((rval = tfpkt_dr_push(tbp, drp, (uint64_t *)&tx_dr)) == 0)
			break;
		ring = (ring + 1) % TFPKT_TX_CNT;
	}

	if (rval != 0) {
		tfpkt_buf_remove(&tbp->ttb_txbufs_pushed, buf);
		(void) tfpkt_buf_insert(&tbp->ttb_txbufs_inuse, buf);
		if (rval == ENOSPC) {
			TBUS_STAT_BUMP(tbp, ttb_txfail_no_descriptors);
		} else {
			TBUS_STAT_BUMP(tbp, ttb_txfail_other);
		}
	}

	return (rval);
}

/*
 * We've finished processing the received packet, so we are free to reuse the
 * buffer.
 */
void
tfpkt_tbus_rx_done(tfpkt_tbus_t *tbp, void *addr, size_t sz)
{
	tfpkt_buf_t *buf;

	buf = tfpkt_buf_by_va(&tbp->ttb_rxbufs_inuse, addr);
	if (buf != NULL) {
		(void) tfpkt_buf_insert(&tbp->ttb_rxbufs_free, buf);
	}
}

/*
 * Process a single rx descriptor, representing a single incoming packet.
 */
static void
tfpkt_tbus_process_rx(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, tfpkt_dr_rx_t *rx_dr)
{
	tfpkt_buf_t *buf;

	if (rx_dr->rx_type != TFPRT_RX_DESC_TYPE_PKT) {
		/* should never happen. */
		tfpkt_tbus_err(tbp, "!non-pkt descriptor (%d) on %s",
		    rx_dr->rx_type, drp->tdr_name);
		TBUS_STAT_BUMP(tbp, ttb_rxfail_bad_descriptor_type);
		return;
	}

	buf = tfpkt_buf_by_pa(&tbp->ttb_rxbufs_pushed, rx_dr->rx_addr);
	if (buf == NULL) {
		tfpkt_tbus_dlog(tbp, "!unrecognized rx buf: %lx",
		    rx_dr->rx_addr);
		TBUS_STAT_BUMP(tbp, ttb_rxfail_unknown_buf);
	} else {
		(void) tfpkt_buf_insert(&tbp->ttb_rxbufs_inuse, buf);
		tfpkt_rx(tbp->ttb_tfp, tfpkt_buf_va(buf), rx_dr->rx_size);
		(void) tfpkt_buf_remove(&tbp->ttb_rxbufs_inuse, buf);
		(void) tfpkt_buf_insert(&tbp->ttb_rxbufs_free, buf);
	}
}

/*
 * Process a single cmp descriptor, representing the completion of a single
 * packet transmit operation.
 */
static void
tfpkt_tbus_process_cmp(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp,
    tfpkt_dr_cmp_t *cmp_dr)
{
	tfpkt_buf_t *buf;

	buf = tfpkt_buf_by_pa(&tbp->ttb_txbufs_pushed, cmp_dr->cmp_msg_id);
	if (buf == NULL) {
		tfpkt_tbus_dlog(tbp, "!unrecognized tx buf: %lx",
		    cmp_dr->cmp_msg_id);
		TBUS_STAT_BUMP(tbp, ttb_txfail_unknown_buf);
	} else if (cmp_dr->cmp_type != TFPRT_TX_DESC_TYPE_PKT) {
		/* should never happen. */
		tfpkt_tbus_err(tbp, "!non-pkt descriptor (%d) on %s",
		    cmp_dr->cmp_type, drp->tdr_name);
		TBUS_STAT_BUMP(tbp, ttb_txfail_bad_descriptor_type);
	} else if (tfpkt_buf_insert(&tbp->ttb_txbufs_free, buf)) {
		tfpkt_t *tfp = tbp->ttb_tfp;

		mutex_enter(&tfp->tfp_mutex);
		tfp->tfp_stats.tps_tx_updates.value.ui64++;
		mutex_exit(&tfp->tfp_mutex);
		/* Let mac know we just repopulated the freelist */
		mac_tx_update(tbp->ttb_tfp->tfp_mh);
	}
}

/*
 * Read or write a single tbus register, returning 0 on success and -1 on
 * failure.
 *
 * The only reason a failure should occur is if the tbus has been reset.  In
 * that case, we signal our tbus monitor thread to begin the cleanup process.
 */
static int
tfpkt_tbus_reg_op(tfpkt_tbus_t *tbp, size_t offset, uint32_t *val, bool rd)
{
	int rval;

	if (rd) {
		rval = tofino_tbus_read_reg(tbp->ttb_tofino_dip, offset, val);
	} else {
		rval = tofino_tbus_write_reg(tbp->ttb_tofino_dip, offset, *val);
	}

	if (rval != 0)
		tfpkt_tbus_reset_detected(tbp->ttb_tfp);

	return (rval);
}

/* Read a single field from a descriptor ring's register set */
static int
tfpkt_dr_read(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, size_t offset, uint32_t *val)
{
	return (tfpkt_tbus_reg_op(tbp, drp->tdr_reg_base + offset, val, true));
}

/* Write a single field to a descriptor ring's register set */
static int
tfpkt_dr_write(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, size_t offset,
    uint32_t val)
{
	return (tfpkt_tbus_reg_op(tbp, drp->tdr_reg_base + offset, &val,
	    false));
}

/*
 * Clear a single field in a descriptor ring's register set.  This is similar to
 * writing a 0 to the register, but we bypass the "is the tbus active" check so
 * we can use this to clean up when the tbus is about to be reset.
 */
static void
tfpkt_dr_clear(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, size_t dr_offset)
{
	uint64_t offset = drp->tdr_reg_base + dr_offset;

	(void) tofino_tbus_clear_reg(tbp->ttb_tofino_dip, offset);
}

/*
 * Poll a cmp ring for completions to process.  There are three possible return
 * codes:
 *   -1: Error while reading the ring
 *    0: The ring is empty
 *    1: We pulled a descriptor off the ring
 */
static int
tfpkt_tbus_cmp_poll(tfpkt_tbus_t *tbp, int ring)
{
	tfpkt_dr_t *drp = &tbp->ttb_cmp_drs[ring];
	tfpkt_dr_cmp_t cmp_dr;
	int rval, err;

	err = tfpkt_dr_pull(tbp, drp, (uint64_t *)&cmp_dr);
	if (err == 0) {
		tfpkt_tbus_process_cmp(tbp, drp, &cmp_dr);
		rval = 1;
	} else if (err == ENOENT) {
		rval = 0;
	} else {
		rval = -1;
	}

	return (rval);
}

/*
 * Poll an rx ring for descriptors to process.  There are three possible return
 * codes:
 *   -1: Error while reading the ring
 *    0: The ring is empty
 *    1: We pulled a descriptor off the ring
 */
static int
tfpkt_tbus_rx_poll(tfpkt_tbus_t *tbp, int ring)
{
	tfpkt_dr_t *drp = &tbp->ttb_rx_drs[ring];
	tfpkt_dr_rx_t rx_dr;
	int rval, err;

	err = tfpkt_dr_pull(tbp, drp, (uint64_t *)&rx_dr);
	if (err == 0) {
		tfpkt_tbus_process_rx(tbp, drp, &rx_dr);
		rval = 1;
	} else if (err == ENOENT) {
		rval = 0;
	} else {
		rval = -1;
	}

	return (rval);
}

/*
 * Disable a descriptor ring and clear its configuration registers.
 */
static void
tfpkt_tbus_fini_dr(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp)
{
	mutex_enter(&drp->tdr_mutex);

	drp->tdr_locked = true;
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_CTRL);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_SIZE);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_BASE_ADDR_LOW);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_BASE_ADDR_HIGH);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_LOW);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_HIGH);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_HEAD_PTR);
	tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_TAIL_PTR);
	if (tbp->ttb_gen == TOFINO_G_TF2) {
		tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_EMPTY_INT_TIME);
		tfpkt_dr_clear(tbp, drp, TBUS_DR_OFF_EMPTY_INT_CNT);
	}
	mutex_exit(&drp->tdr_mutex);
}

/*
 * Program the ASIC with the location, range, and characteristics of this
 * descriptor ring.
 */
static int
tfpkt_tbus_init_dr(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp)
{
	uint32_t ctrl;
	uint32_t phys_low = (uint32_t)(drp->tdr_phys_base & 0xFFFFFFFFULL);
	uint32_t phys_high = (uint32_t)(drp->tdr_phys_base >> 32);
	uint64_t limit = drp->tdr_phys_base + drp->tdr_ring_size;
	uint32_t limit_low = (uint32_t)(limit & 0xFFFFFFFFULL);
	uint32_t limit_high = (uint32_t)(limit >> 32);

	/*
	 * The DR range has to aligned on a 64b boundary.  As the DMA attributes
	 * specify that the buffer must have a 4k alignment, this should always
	 * be the case.
	 */
	ASSERT3U((phys_low & 63ull), ==, 0);

	/* disable DR */
	ctrl = 0;
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_CTRL, ctrl);

	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_SIZE,
	    drp->tdr_ring_size);
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_BASE_ADDR_LOW, phys_low);
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_BASE_ADDR_HIGH, phys_high);

	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_LOW, limit_low);
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_LIMIT_ADDR_HIGH,
	    limit_high);

	*drp->tdr_tail_ptr = 0;
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_HEAD_PTR, 0);
	(void) tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_TAIL_PTR, 0);

	/*
	 * Tofino2 has two additional registers, which enable an additional
	 * interrupt if an rx or cmp DR is non-empty.
	 */
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
			(void) tfpkt_dr_write(tbp, drp,
			    TBUS_DR_OFF_DATA_TIMEOUT, 1);
			/* fallthru */
		case TFPKT_DR_CMP:
			ctrl = TBUS_DR_CTRL_TAIL_PTR_MODE;
			break;
	}

	/* enable DR */
	ctrl |= TBUS_DR_CTRL_ENABLE;
	return (tfpkt_dr_write(tbp, drp, TBUS_DR_OFF_CTRL, ctrl));
}

static void
tfpkt_tbus_fini_drs(tfpkt_tbus_t *tbp)
{
	int i;

	for (i = 0; i < TFPKT_FM_CNT; i++)
		tfpkt_tbus_fini_dr(tbp, &tbp->ttb_fm_drs[i]);
	for (i = 0; i < TFPKT_RX_CNT; i++)
		tfpkt_tbus_fini_dr(tbp, &tbp->ttb_rx_drs[i]);
	for (i = 0; i < TFPKT_TX_CNT; i++)
		tfpkt_tbus_fini_dr(tbp, &tbp->ttb_tx_drs[i]);
	for (i = 0; i < TFPKT_CMP_CNT; i++)
		tfpkt_tbus_fini_dr(tbp, &tbp->ttb_cmp_drs[i]);
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
 * Returns 0 if it successfully pulls a descriptor, ENOENT if the ring is
 * empty, and ENXIO if we detect that the rings have been reset.
 */
static int
tfpkt_dr_pull(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, uint64_t *desc)
{
	uint64_t *slot, head;
	int rval;

	mutex_enter(&drp->tdr_mutex);
	if (drp->tdr_locked) {
		mutex_exit(&drp->tdr_mutex);
		return (ENXIO);
	}

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

		tfpkt_tbus_dlog(tbp,
		    "!pulling from %s at %ld (wrap: %ld %d/%ld)",
		    drp->tdr_name, drp->tdr_head, wrap, idx,
		    drp->tdr_depth);
	}

	for (uint64_t i = 0; i < (drp->tdr_desc_size >> 3); i++)
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
	if (drp->tdr_locked) {
		mutex_exit(&drp->tdr_mutex);
		return (ENXIO);
	}

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

		tfpkt_tbus_dlog(tbp, "!pushing to %s at %ld (wrap: %ld %d/%ld)",
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
tfpkt_tbus_push_fm(tfpkt_tbus_t *tbp, tfpkt_dr_t *drp, uint64_t addr,
    uint64_t size)
{
	tfpkt_dr_fm_t descriptor;
	uint64_t encoded_size;

	/*
	 * The buffers must be at least 256 bytes. The DMA address must be
	 * 256-byte aligned, as the lower 8 bits are used to encode the buffer
	 * size.  Because we have ensured that all buffers are allocated with a
	 * 4k alignment, this should always be true.
	 */
	ASSERT3U((addr & 0xff), ==, 0);

	if (size > TOFINO_MAX_DMA_SZ) {
		return (EINVAL);
	}

	/*
	 * Because the buffers must be at least 256 bytes, the size encoding
	 * is 0 for 256 bytes, 1 for 512 bytes, etc.  Hence the -9 below.
	 */
	encoded_size = ddi_fls(size) - 9;
	ASSERT3U((encoded_size & 0xff), ==, encoded_size);
	descriptor.fm_addr = addr >> 8;
	descriptor.fm_size = encoded_size;

	return (tfpkt_dr_push(tbp, drp, (uint64_t *)&descriptor));
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
	tfpkt_buf_t *buf;
	int cnt = 0;

	while ((buf = tfpkt_buf_pop(&tbp->ttb_rxbufs_free)) != NULL) {
		dma_addr = tfpkt_buf_pa(buf);
		rval = tfpkt_tbus_push_fm(tbp, drp, dma_addr, tfpkt_buf_size);
		if (rval != 0) {
			/*
			 * ENOSPC is an indication that we've pushed as
			 * many buffers as the ASIC can handle.  It means we
			 * should stop trying to push more, but that we
			 * shouldn't return an error to the caller.
			 */
			if (rval == ENOSPC)
				rval = 0;
			(void) tfpkt_buf_insert(&tbp->ttb_rxbufs_free, buf);
			break;
		}
		(void) tfpkt_buf_insert(&tbp->ttb_rxbufs_pushed, buf);
		cnt++;
	}

	return (rval);
}

/*
 * Setup the tbus control register to enable the pci network port
 */
static int
tfpkt_tbus_port_init(tfpkt_tbus_t *tbp, dev_info_t *tfp_dip)
{
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
	if ((rval = tofino_tbus_read_reg(tbp->ttb_tofino_dip, reg, ctrlp)) != 0)
		return (rval);

	ctrl.tftc_port_alive = 1;	/* turn on the port */
	ctrl.tftc_rx_en = 1;		/* enable receive traffic */
	ctrl.tftc_ecc_dec_dis = 0;	/* do not disable ecc */
	ctrl.tftc_crcchk_dis = 1;	/* disable crc32 check */
	ctrl.tftc_crcrmv_dis = 0;	/* do not disable crc32 removal */
	if (tbp->ttb_gen != TOFINO_G_TF1) {
		/* payload is not offset in the buffer */
		ctrl.tftc_rx_channel_offset = 0;
	}

	return (tofino_tbus_write_reg(tbp->ttb_tofino_dip, reg, *ctrlp));
}

static int
tfpkt_tbus_intr(void *arg)
{
	tfpkt_t *tfp = (tfpkt_t *)arg;
	tfpkt_tbus_t *tbp;
	uint32_t active_rings;

	/*
	 * Iterate over all of the rx and cmp rings, looking for descriptors to
	 * process.  Bump the active_rings count each time we find a descriptor.
	 * Continue iterating over the rings for as long as there are
	 * descriptors to process.
	 */
	do {
		active_rings = 0;

		if ((tbp = tfpkt_tbus_hold(tfp)) == NULL)
			break;

		for (int i = 0; i < TFPKT_RX_CNT; i++) {
			int rval = tfpkt_tbus_rx_poll(tbp, i);
			if (rval < 0) {
				goto err;
			}
			if (rval > 0) {
				if (tfpkt_tbus_push_free_bufs(tbp, i) == 0) {
					active_rings++;
				} else {
					goto err;
				}
			}
		}

		for (int i = 0; i < TFPKT_CMP_CNT; i++) {
			int rval = tfpkt_tbus_cmp_poll(tbp, i);
			if (rval < 0) {
				goto err;
			}
			if (rval > 0) {
				active_rings++;
			}
		}
		tfpkt_tbus_release(tfp);

	} while (active_rings > 0);

	return (0);
err:
	tfpkt_tbus_release(tfp);
	return (0);

}

static void
tfpkt_tbus_fini(tfpkt_t *tfp, tfpkt_tbus_t *tbp)
{
	if (tbp->ttb_tofino_dip != NULL) {
		tofino_tbus_unregister_intr(tbp->ttb_tofino_dip);
		VERIFY0(tofino_tbus_unregister(tbp->ttb_tofino_dip));
	}

	tfpkt_tbus_free_bufs(tbp);
	tfpkt_tbus_free_drs(tbp);
	kstat_delete(tbp->ttb_kstat);
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
	dev_info_t *tofino_dip = ddi_get_parent(tfp_dip);
	tfpkt_tbus_t *tbp;
	kstat_t *kstat;
	int count, err;

	/*
	 * This peforms the same check as tofino_tbus_register(), but we can
	 * call it before doing all of the allocations below.  Since we don't
	 * hold the lock between now and then, we might end up failing to
	 * register anyway, but this pre-check will save some cycles in the
	 * overwhelming majority of cases.
	 *
	 * The check being performed is whether the packet transfer mechanism on
	 * the ASIC is in a well-defined state.  This check is necessary because
	 * the bulk of the ASIC initialization is carried out by the userspace
	 * dataplane daemon.  Thus, we can't initialize this mechanism until we
	 * know that userspace has initialized the rest of the ASIC.
	 */
	if (tofino_tbus_ready(tofino_dip) != 0) {
		return (NULL);
	}

	count = sizeof (tfpkt_tbus_stats_t) / sizeof (kstat_named_t);
	kstat = kstat_create("tfpkt_tbus", ddi_get_instance(tfp_dip),
	    "tfpkt_tbus", "tofino", KSTAT_TYPE_NAMED, count,
	    KSTAT_FLAG_VIRTUAL);
	if (kstat == NULL) {
		dev_err(tfp_dip, CE_WARN, "failed to alloc tfpkt_tbus kstats");
		return (NULL);
	}

	tbp = kmem_zalloc(sizeof (*tbp), KM_SLEEP);
	tbp->ttb_tfpkt_dip = tfp_dip;
	tbp->ttb_tofino_dip = tofino_dip;
	tbp->ttb_tfp = tfp;

	tbp->ttb_kstat = kstat;
	kstat->ks_data = &tbp->ttb_stats;
	bcopy(&tfpkt_tbus_stats_template, &tbp->ttb_stats,
	    sizeof (tfpkt_tbus_stats_t));
	kstat_install(kstat);

	if ((err = tofino_tbus_register(tofino_dip)) != 0) {
		if (err == EBUSY) {
			oneshot_error(tbp, "!tofino tbus in use");
		} else if (err == ENXIO) {
			/* The driver was loaded but not attached. */
			oneshot_error(tbp, "!tofino driver offline");
		} else if (err == EAGAIN) {
			/*
			 * The userspace daemon hasn't yet initialized the
			 * ASIC
			 */
			oneshot_error(tbp, "!tofino asic not ready");
		} else {
			oneshot_error(tbp, "!tofino_tbus_register failed");
		}
		goto fail;
	}

	tbp->ttb_gen = tofino_get_generation(tofino_dip);

	if ((err = tfpkt_tbus_alloc_bufs(tbp)) != 0) {
		oneshot_error(tbp, "!failed to allocate buffers");
	} else if ((err = tfpkt_tbus_alloc_drs(tbp)) != 0) {
		oneshot_error(tbp, "!failed to allocate drs");
	} else if ((err = tfpkt_tbus_init_drs(tbp)) != 0) {
		oneshot_error(tbp, "!failed to init drs");
	}

	if (err != 0)
		goto fail;

	if (tfpkt_tbus_port_init(tbp, tfp_dip) != 0)
		goto fail;

	err = tofino_tbus_register_intr(tofino_dip, tfpkt_tbus_intr, tfp);
	if (err != 0) {
		oneshot_error(tbp, "!failed to register softint");
		VERIFY0(tofino_tbus_unregister(tofino_dip));
	}

	for (int i = 0; i < TFPKT_RX_CNT; i++)
		if ((err = tfpkt_tbus_push_free_bufs(tbp, i)) != 0)
			goto fail;

	return (tbp);

fail:
	tfpkt_tbus_fini(tfp, tbp);
	return (NULL);
}

static void
tfpkt_bus_update_state(tfpkt_t *tfp, tfpkt_tbus_state_t state)
{
	ASSERT(MUTEX_HELD(&tfp->tfp_tbus_mutex));

	if (state == tfp->tfp_tbus_state)
		return;

	tfp->tfp_tbus_state = state;
}

static void
tfpkt_tbus_reset_detected(tfpkt_t *tfp)
{
	mutex_enter(&tfp->tfp_tbus_mutex);
	if (tfp->tfp_tbus_state == TFPKT_TBUS_ACTIVE) {
		tfpkt_bus_update_state(tfp, TFPKT_TBUS_RESETTING);
		cv_broadcast(&tfp->tfp_tbus_cv);
	}
	mutex_exit(&tfp->tfp_tbus_mutex);
}

void
tfpkt_tbus_monitor(void *arg)
{
	dev_info_t *dip = arg;
	tfpkt_t *tfp = (tfpkt_t *)ddi_get_driver_private(dip);
	link_state_t updated_ls;
	clock_t time;

	updated_ls = LINK_STATE_DOWN;
	mac_link_update(tfp->tfp_mh, LINK_STATE_DOWN);

	dev_err(tfp->tfp_dip, CE_NOTE, "!tbus monitor started");

	mutex_enter(&tfp->tfp_tbus_mutex);

	tfpkt_bus_update_state(tfp, TFPKT_TBUS_UNINIT);

	while (tfp->tfp_tbus_state != TFPKT_TBUS_HALTING) {
		tfpkt_tbus_t *tbp = tfp->tfp_tbus_data;

		switch (tfp->tfp_tbus_state) {
		case TFPKT_TBUS_UNINIT:
			/*
			 * Keep asking the tofino driver to let us use the tbus
			 * until it says OK.  The two most likely reasons for
			 * this to fail is that the tofino has been removed and
			 * we're waiting to be detached, or if the userspace
			 * daemon is in the process of reinitializing the ASIC.
			 */
			ASSERT(tbp == NULL);
			tfp->tfp_tbus_data = tfpkt_tbus_init(tfp);
			if (tfp->tfp_tbus_data != NULL) {
				tfpkt_bus_update_state(tfp, TFPKT_TBUS_ACTIVE);
				continue;
			}
			break;

		case TFPKT_TBUS_ACTIVE:
			/*
			 * Verify that the tbus registers haven't been reset on
			 * us.  In most cases, this will already have been
			 * detected in one of the packet processing paths.
			 */
			if (tofino_tbus_state(tbp->ttb_tofino_dip) ==
			    TF_TBUS_READY)
				break;
			tfpkt_bus_update_state(tfp, TFPKT_TBUS_RESETTING);
			/* FALLTHRU */

		case TFPKT_TBUS_RESETTING:
			/*
			 * Don't clean up the tbus data while someone is
			 * actively using it.
			 */
			if (tfp->tfp_tbus_refcnt != 0) {
				break;
			}

			/*
			 * We drop and reacquire the tbus_mutex here to maintain
			 * the dr -> tbus lock ordering.  Because we aren't in
			 * the BUS_ACTIVE state we know that nobody else will
			 * attempt to take the DR locks so there is no risk of
			 * deadlock, but maintaining the order is still good
			 * hygiene.
			 */
			mutex_exit(&tfp->tfp_tbus_mutex);
			tfpkt_tbus_fini_drs(tbp);
			tfpkt_tbus_fini(tfp, tbp);
			mutex_enter(&tfp->tfp_tbus_mutex);

			/*
			 * While we were cleaning up up the DRs, it's possible
			 * that the driver started to detach.  If so, the state
			 * will have changed and we should leave it alone.
			 */
			if (tfp->tfp_tbus_state == TFPKT_TBUS_RESETTING)
				tfpkt_bus_update_state(tfp, TFPKT_TBUS_UNINIT);

			continue;

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

		if (tfp->tfp_tbus_state == TFPKT_TBUS_ACTIVE)
			tfp->tfp_link_state = LINK_STATE_UP;
		else
			tfp->tfp_link_state = LINK_STATE_DOWN;

		if (tfp->tfp_link_state != updated_ls) {
			updated_ls = tfp->tfp_link_state;
			mutex_exit(&tfp->tfp_tbus_mutex);
			mac_link_update(tfp->tfp_mh, updated_ls);
			mutex_enter(&tfp->tfp_tbus_mutex);
		}

		time = ddi_get_lbolt() + hz;
		(void) cv_timedwait(&tfp->tfp_tbus_cv, &tfp->tfp_tbus_mutex,
		    time);
	}

	while (tfp->tfp_tbus_refcnt != 0) {
		dev_err(tfp->tfp_dip, CE_NOTE,
		    "!waiting for %d tbus refs to drop", tfp->tfp_tbus_refcnt);
		time = ddi_get_lbolt() + hz;
		(void) cv_timedwait(&tfp->tfp_tbus_cv, &tfp->tfp_tbus_mutex,
		    time);
	}

	if (tfp->tfp_tbus_data != NULL)
		tfpkt_tbus_fini(tfp, tfp->tfp_tbus_data);

	tfpkt_bus_update_state(tfp, TFPKT_TBUS_HALTED);
	cv_broadcast(&tfp->tfp_tbus_cv);
	mutex_exit(&tfp->tfp_tbus_mutex);
	dev_err(tfp->tfp_dip, CE_NOTE, "!tbus monitor exiting");
}

int
tfpkt_tbus_monitor_halt(tfpkt_t *tfp)
{
	time_t deadline, left;
	int rval;

	dev_err(tfp->tfp_dip, CE_NOTE, "!halting tbus monitor");
	mutex_enter(&tfp->tfp_tbus_mutex);
	if (tfp->tfp_tbus_state != TFPKT_TBUS_HALTED) {
		tfpkt_bus_update_state(tfp, TFPKT_TBUS_HALTING);
		cv_broadcast(&tfp->tfp_tbus_cv);
	}

	left = hz;
	deadline = ddi_get_lbolt() + left;
	while (left > 0 && tfp->tfp_tbus_state != TFPKT_TBUS_HALTED) {
		left = cv_timedwait(&tfp->tfp_tbus_cv, &tfp->tfp_tbus_mutex,
		    deadline);

	}

	if (tfp->tfp_tbus_state == TFPKT_TBUS_HALTED) {
		dev_err(tfp->tfp_dip, CE_NOTE, "!halted tbus monitor");
		rval = 0;
	} else {
		dev_err(tfp->tfp_dip, CE_WARN,
		    "timed out waiting for tbus monitor to halt");
		rval = -1;
	}

	mutex_exit(&tfp->tfp_tbus_mutex);

	return (rval);
}
