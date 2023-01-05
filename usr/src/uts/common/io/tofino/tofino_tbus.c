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
 * Copyright 2023 Oxide Computer Company
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/pci.h>
#include <sys/ksynch.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/tofino.h>
#include <sys/tofino_regs.h>
#include "tofino_impl.h"

/*
 * This file provides access to the registers and interrupts associated with the
 * Tofino ASIC's "tbus".  This is the facility that provides a network
 * device-like collection of ringbufs, carrying traffic to and from the Tofino
 * over the PCI link.  These interfaces can be used by another driver to
 * implement a mac(9e) device, completing the illusion that the tbus is a
 * network device.
 */

extern dev_info_t *tofino_dip;

tofino_gen_t
tofino_get_generation(tf_tbus_hdl_t tf_hdl)
{
	return (tf_hdl->tbc_tofino->tf_gen);
}

static ddi_dma_attr_t tf_tbus_dma_attr_buf = {
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

static ddi_device_acc_attr_t tf_tbus_acc_attr = {
	.devacc_attr_version =		DDI_DEVICE_ATTR_V1,
	.devacc_attr_endian_flags =	DDI_STRUCTURE_LE_ACC,
	.devacc_attr_dataorder =	DDI_STRICTORDER_ACC,
	.devacc_attr_access =		DDI_DEFAULT_ACC,
};

/*
 * Enable or disable all of the tbus interrupts.
 */
static void
tofino_tbus_intr_set(tofino_t *tf, bool enable)
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
	for (uint32_t intr = intr_lo; intr <= intr_hi; intr++) {
		/*
		 * XXX: This is the long, canonical way to unmask the
		 * interrupts we care about.  This whole loop works out to
		 * setting reg 0xc4 to 0.
		 */
		uint32_t intr_reg = intr >> 5;
		uint32_t intr_bit = intr & 0x1f;
		uint32_t bit_fld = (1u << intr_bit);

		uint32_t shadow_msk_reg = shadow_msk_base + (4 * intr_reg);
		uint32_t old;

		tofino_read_reg(tf->tf_dip, shadow_msk_reg, &old);
		tofino_write_reg(tf->tf_dip, shadow_msk_reg, old & ~bit_fld);
	}

	if (tf->tf_gen == TOFINO_G_TF1) {
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN0_1, en0);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN1_1, en1);
	} else {
		ASSERT(tf->tf_gen == TOFINO_G_TF2);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN0_1, en0);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN1_1, en1);
	}

	/*
	 * Unconditionally disable the interrupts we're not looking for
	 */
	if (tf->tf_gen == TOFINO_G_TF1) {
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN2_1, 0);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN0_0, 0);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN1_0, 0);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN2_0, 0);
	} else {
		ASSERT(tf->tf_gen == TOFINO_G_TF2);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN2_1, 0);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN0_0, 0);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN1_0, 0);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN2_0, 0);
	}
	tofino_dlog(tf, "!%s interrupts", enable ? "enabled" : "disabled");
}

/*
 * Allocate a single buffer capable of DMA to/from the Tofino ASIC.
 *
 * The caller is responsible for providing an unused tf_tbus_dma_t structure,
 * which is used for tracking and managing a DMA buffer.  This routine will
 * populate that structure with all the necessary state.  Having the caller
 * provide the state structure lets us allocate them in bulk, rather than one
 * per buffer.
 */
int
tofino_tbus_dma_alloc(tf_tbus_hdl_t tf_hdl, tf_tbus_dma_t *dmap, size_t size,
    int flags)
{
	tofino_t *tf = tf_hdl->tbc_tofino;
	unsigned int count;
	int err;

	mutex_enter(&tf->tf_mutex);

	err = ddi_dma_alloc_handle(tf->tf_dip, &tf_tbus_dma_attr_buf,
	    DDI_DMA_SLEEP, NULL, &dmap->tpd_handle);
	if (err != DDI_SUCCESS) {
		tofino_err(tf, "!%s: alloc_handle failed: %d", __func__, err);
		goto fail0;
	}

	err = ddi_dma_mem_alloc(dmap->tpd_handle, size, &tf_tbus_acc_attr,
	    DDI_DMA_STREAMING, DDI_DMA_SLEEP, NULL, &dmap->tpd_addr,
	    &dmap->tpd_len, &dmap->tpd_acchdl);
	if (err != DDI_SUCCESS) {
		tofino_err(tf, "!%s: mem_alloc failed", __func__);
		goto fail1;
	}

	err = ddi_dma_addr_bind_handle(dmap->tpd_handle, NULL, dmap->tpd_addr,
	    dmap->tpd_len, flags, DDI_DMA_SLEEP, NULL, &dmap->tpd_cookie,
	    &count);
	if (err != DDI_DMA_MAPPED) {
		tofino_err(tf, "!%s: bind_handle failed", __func__);
		goto fail2;
	}

	if (count > 1) {
		tofino_err(tf, "!%s: more than one DMA cookie", __func__);
		goto fail2;
	}
	mutex_exit(&tf->tf_mutex);

	return (0);
fail2:
	ddi_dma_mem_free(&dmap->tpd_acchdl);
fail1:
	ddi_dma_free_handle(&dmap->tpd_handle);
fail0:
	mutex_exit(&tf->tf_mutex);
	return (-1);
}

/*
 * This routine frees a DMA buffer and its state, but does not free the
 * tf_tbus_dma_t structure itself.
 */
void
tofino_tbus_dma_free(tf_tbus_dma_t *dmap)
{
	VERIFY3S(ddi_dma_unbind_handle(dmap->tpd_handle), ==, DDI_SUCCESS);
	ddi_dma_mem_free(&dmap->tpd_acchdl);
	ddi_dma_free_handle(&dmap->tpd_handle);
}

int
tofino_tbus_register_intr(tf_tbus_hdl_t tf_hdl, tofino_intr_hdlr hdlr,
    void *arg)
{
	tofino_tbus_client_t *tbc = tf_hdl;
	tofino_t *tf = tbc->tbc_tofino;
	int rval;

	mutex_enter(&tf->tf_mutex);
	if (tbc->tbc_intr != NULL) {
		rval = EBUSY;
	} else {
		ASSERT(!tbc->tbc_intr_busy);
		tbc->tbc_intr = hdlr;
		tbc->tbc_intr_arg = arg;
		tofino_tbus_intr_set(tf, true);
		rval = 0;
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

int
tofino_tbus_unregister_intr(tf_tbus_hdl_t tf_hdl)
{
	tofino_tbus_client_t *tbc = tf_hdl;
	tofino_t *tf = tbc->tbc_tofino;
	int rval;

	mutex_enter(&tf->tf_mutex);
	if (tbc->tbc_intr == NULL) {
		rval = EINVAL;
	} else if (tbc->tbc_intr_busy) {
		rval = EBUSY;
	} else {
		tbc->tbc_intr = NULL;
		tbc->tbc_intr_arg = NULL;
		tofino_tbus_intr_set(tf, false);
		rval = 0;
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

static int
tofino_tbus_reg_op(tf_tbus_hdl_t tf_hdl, size_t offset, uint32_t *val,
    boolean_t rd)
{
	tofino_t *tf = tf_hdl->tbc_tofino;
	int rval;

	mutex_enter(&tf->tf_mutex);
	if (tf->tf_tbus_state != TF_TBUS_READY)
		rval = EAGAIN;
	else if (rd)
		rval = tofino_read_reg(tf->tf_dip, offset, val);
	else
		rval = tofino_write_reg(tf->tf_dip, offset, *val);
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

int
tofino_tbus_read_reg(tf_tbus_hdl_t tf_hdl, size_t offset, uint32_t *val)
{
	return (tofino_tbus_reg_op(tf_hdl, offset, val, true));
}

int
tofino_tbus_write_reg(tf_tbus_hdl_t tf_hdl, size_t offset, uint32_t val)
{
	return (tofino_tbus_reg_op(tf_hdl, offset, &val, false));
}

const char *
tofino_state_name(tofino_tbus_state_t s)
{
	switch (s) {
		case TF_TBUS_UNINITIALIZED: return ("Uninitialized");
		case TF_TBUS_RESETTING: return ("Resetting");
		case TF_TBUS_RESET: return ("Reset");
		case TF_TBUS_READY: return ("Ready");
		default: return ("Invalid");
	}
}

int
tofino_tbus_state_update(tofino_t *tf, tofino_tbus_state_t new_state)
{
	ASSERT(MUTEX_HELD(&tf->tf_mutex));
	if (new_state < TF_TBUS_UNINITIALIZED || new_state > TF_TBUS_READY)
		return (EINVAL);

	tofino_dlog(tf, "!updating tbus state %s -> %s",
	    tofino_state_name(tf->tf_tbus_state),
	    tofino_state_name(new_state));
	tf->tf_tbus_state = new_state;

	return (0);
}

tofino_tbus_state_t
tofino_tbus_state(tf_tbus_hdl_t tf_hdl)
{
	tofino_t *tf = tf_hdl->tbc_tofino;
	int rval;

	mutex_enter(&tf->tf_mutex);
	rval = tf->tf_tbus_state;
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

/*
 * If we ever support multiple tofino ASICs in a single system, this interface
 * will need to indicate for which ASIC the caller is registering.
 */
int
tofino_tbus_register(tf_tbus_hdl_t *tf_hdl)
{
	tofino_t *tf;
	tofino_tbus_client_t *tbc;
	int rval = 0;

	if (tofino_dip == NULL)
		return (ENXIO);

	tf = ddi_get_driver_private(tofino_dip);
	if (tf == NULL) {
		cmn_err(CE_WARN, "!%s() tofino driver has no state", __func__);
		return (ENXIO);
	}
	mutex_enter(&tf->tf_mutex);
	if (tf->tf_tbus_state != TF_TBUS_RESET) {
		rval = EAGAIN;
	} else if (tf->tf_tbus_client != NULL) {
		/* someone else is already handling the packets */
		rval = EBUSY;
	} else {
		tofino_tbus_state_update(tf, TF_TBUS_READY);
		tbc = kmem_zalloc(sizeof (*tbc), KM_SLEEP);
		tbc->tbc_tofino = tf;
		tf->tf_tbus_client = tbc;
		*tf_hdl = (tf_tbus_hdl_t)tbc;
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

int
tofino_tbus_unregister(tf_tbus_hdl_t tf_hdl)
{
	tofino_tbus_client_t *tbc = tf_hdl;
	tofino_t *tf = tbc->tbc_tofino;
	int rval;

	mutex_enter(&tf->tf_mutex);
	if (tbc != tf->tf_tbus_client) {
		rval = ENXIO;
	} else if (tbc->tbc_intr != NULL) {
		rval = EBUSY;
	} else {
		if (tf->tf_tbus_state == TF_TBUS_READY)
			tofino_tbus_state_update(tf, TF_TBUS_RESET);

		kmem_free(tf->tf_tbus_client, sizeof (tofino_tbus_client_t));
		tf->tf_tbus_client = NULL;
		rval = 0;
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}
