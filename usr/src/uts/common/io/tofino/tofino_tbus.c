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
tofino_gen_t
tofino_get_generation(dev_info_t *dip)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	return (tf->tf_gen);
}

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

		(void) tofino_read_reg(tf->tf_dip, shadow_msk_reg, &old);
		(void) tofino_write_reg(tf->tf_dip, shadow_msk_reg,
		    old & ~bit_fld);
	}

	if (tf->tf_gen == TOFINO_G_TF1) {
		(void) tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN0_1, en0);
		(void) tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN1_1, en1);
	} else {
		ASSERT(tf->tf_gen == TOFINO_G_TF2);
		(void) tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN0_1,
		    en0);
		(void) tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN1_1,
		    en1);
	}

	/*
	 * Unconditionally disable the interrupts we're not looking for
	 */
	if (tf->tf_gen == TOFINO_G_TF1) {
		(void) tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN2_1, 0);
		(void) tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN0_0, 0);
		(void) tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN1_0, 0);
		(void) tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_EN2_0, 0);
	} else {
		ASSERT(tf->tf_gen == TOFINO_G_TF2);
		(void) tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN2_1, 0);
		(void) tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN0_0, 0);
		(void) tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN1_0, 0);
		(void) tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_EN2_0, 0);
	}
	tofino_dlog(tf, "!%s interrupts", enable ? "enabled" : "disabled");
}

int
tofino_tbus_register_intr(dev_info_t *dip, tofino_intr_hdlr hdlr, void *arg)
{
	tofino_t *tf = ddi_get_driver_private(dip);

	mutex_enter(&tf->tf_mutex);
	if (tf->tf_tbus_intr != NULL) {
		tofino_err(tf, "interupt already registered");
		mutex_exit(&tf->tf_mutex);
		return (EEXIST);
	}

	ASSERT(!tf->tf_tbus_intr_busy);
	tf->tf_tbus_intr = hdlr;
	tf->tf_tbus_intr_arg = arg;
	tofino_tbus_intr_set(tf, true);
	mutex_exit(&tf->tf_mutex);

	return (0);
}

void
tofino_tbus_unregister_intr(dev_info_t *dip)
{
	tofino_t *tf = ddi_get_driver_private(dip);

	mutex_enter(&tf->tf_mutex);
	if (tf->tf_tbus_intr != NULL) {
		while (tf->tf_tbus_intr_busy)
			cv_wait(&tf->tf_cv, &tf->tf_mutex);

		tf->tf_tbus_intr = NULL;
		tf->tf_tbus_intr_arg = NULL;
	}
	mutex_exit(&tf->tf_mutex);
}

static int
tofino_tbus_reg_op(dev_info_t *dip, size_t offset, uint32_t *val, boolean_t rd)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	int rval;

	if (tf->tf_tbus_state != TF_TBUS_READY)
		rval = EAGAIN;
	else if (rd)
		rval = tofino_read_reg(dip, offset, val);
	else
		rval = tofino_write_reg(dip, offset, *val);

	return (rval);
}

int
tofino_tbus_read_reg(dev_info_t *dip, size_t offset, uint32_t *val)
{
	return (tofino_tbus_reg_op(dip, offset, val, true));
}

int
tofino_tbus_write_reg(dev_info_t *dip, size_t offset, uint32_t val)
{
	return (tofino_tbus_reg_op(dip, offset, &val, false));
}

int
tofino_tbus_clear_reg(dev_info_t *dip, size_t offset)
{
	return (tofino_write_reg(dip, offset, 0));
}

const char *
tofino_state_name(tofino_tbus_state_t s)
{
	switch (s) {
		case TF_TBUS_UNINITIALIZED: return ("Uninitialized");
		case TF_TBUS_REMOVED: return ("Removed");
		case TF_TBUS_RESETTING: return ("Resetting");
		case TF_TBUS_RESET: return ("Reset");
		case TF_TBUS_READY: return ("Ready");
		default: return ("Invalid");
	}
}

void
tofino_tbus_state_update(tofino_t *tf, tofino_tbus_state_t new_state)
{
	ASSERT(MUTEX_HELD(&tf->tf_mutex));
	ASSERT(new_state >= TF_TBUS_UNINITIALIZED &&
	    new_state <= TF_TBUS_READY);

	tofino_dlog(tf, "!updating tbus state %s -> %s",
	    tofino_state_name(tf->tf_tbus_state),
	    tofino_state_name(new_state));
	tf->tf_tbus_state = new_state;
}

tofino_tbus_state_t
tofino_tbus_state(dev_info_t *dip)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	int rval;

	mutex_enter(&tf->tf_mutex);
	rval = tf->tf_tbus_state;
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

static int
tofino_tbus_ready_locked(tofino_t *tf)
{
	switch (tf->tf_tbus_state) {
	case TF_TBUS_REMOVED:
		return (ENXIO);
	case TF_TBUS_RESET:
		return (0);
	default:
		return (EAGAIN);
	}
}

int
tofino_tbus_ready(dev_info_t *dip)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	int rval;

	mutex_enter(&tf->tf_mutex);
	rval = tofino_tbus_ready_locked(tf);
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

/*
 * If we ever support multiple tofino ASICs in a single system, this interface
 * will need to indicate for which ASIC the caller is registering.
 */
int
tofino_tbus_register(dev_info_t *dip)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	int rval;

	mutex_enter(&tf->tf_mutex);
	if ((rval = tofino_tbus_ready_locked(tf)) == 0) {
		tofino_tbus_state_update(tf, TF_TBUS_READY);
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

int
tofino_tbus_unregister(dev_info_t *dip)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	int rval;

	mutex_enter(&tf->tf_mutex);
	/*
	 * The client is required to unregister its interrupt handler first
	 */
	if (tf->tf_tbus_intr != NULL) {
		rval = EBUSY;
	} else {
		if (tf->tf_tbus_state == TF_TBUS_READY)
			tofino_tbus_state_update(tf, TF_TBUS_RESET);
		rval = 0;
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}
