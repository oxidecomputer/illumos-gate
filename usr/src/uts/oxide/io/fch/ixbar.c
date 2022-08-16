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
 * Copyright 2022 Oxide Computer Co.
 */

/*
 * The interrupt routing crossbar (ixbar) is a logic component in the FCH that
 * routes fixed/legacy interrupts from sources into IOAPIC virtual pins (and/or
 * to emulated dual-8259A pins, which we do not use).  With a few exceptions,
 * any source may be mapped onto any pin.  A single pin may receive interrupts
 * from multiple sources, but no source can be routed to multiple pins.  By
 * setting the destination pin number associated with a source to a value
 * greater than the number of pins on the IOAPIC, interrupts from that source
 * can be effectively blackholed.  Interrupt sources managed by the ixbar
 * include PCI INTx emulation messages from PCIe devices attached via normal
 * (external) PCIe root ports, such messages from PCIe devices attached via
 * NBIFs (e.g., USB and ATA controllers), serial interrupt messages originating
 * on the external LPC bus if configured, and ordinary fixed interrupt signals
 * from peripherals built into the FCH itself.
 *
 * The current implementation does not support PCI INTx or emulated PCIe INTx
 * messages at all, nor do we support LPC and the associated serial IRQ
 * mechanism.  This leaves us with only the FCH internal peripherals to support,
 * allowing this code to live temporarily in the FCH nexus driver itself.  A
 * more general implementation would be part of the apix module, which would in
 * turn allow associating these source identifiers with any device node
 * (including those that are children of PCI/-X/e or LPC/ISA nexi) and
 * allocating IOAPIC pins for them just as we do for children of the FCH.  This
 * is challenging because the definitions of data structures used to pass
 * metadata about interrupt sources into nexus drivers (and into PSM, if that's
 * how the platform kernel is implemented) are found in common code.  In several
 * cases, even definitions from machdep headers are used in common nexus
 * drivers.  The result of all this is that there is no straightforward way for
 * multiple nexus drivers (other than exclusively machdep nexi like this one) to
 * decorate their children with ixbar source information and then pass that into
 * apix or some other PSM implementation.  In principle this can be fixed but it
 * will require significant changes to "common" code that today assumes
 * essentially the PC model in which interrupts are identified by IRQ numbers
 * (essentially, IOAPIC pin numbers) rather than unique sources.  Because that
 * model also assumes that other metadata like polarity and trigger mode come
 * from a table external to the devinfo tree, there is no way to manage those
 * here and they are effectively hardcoded in apix.
 *
 * Some additional notes on how the ixbar works, specifically its registers, may
 * be found in sys/io/fch/ixbar.h.  From all that we can tell, the ixbars in
 * secondary FCHs are not useful, at least in part because their IOAPICs do not
 * seem to be useful.  The exact reasons for this are not well understood, but
 * the effect is that secondary FCH peripherals cannot generate interrupts, a
 * limitation AMD mentions more in passing than as part of any comprehensive
 * discussion of how these devices work.
 */

#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/types.h>
#include <sys/io/fch/ixbar.h>

#include "fch_impl.h"
#include "ixbar.h"

/*
 * An IOAPIC can have at most 256 (usually virtual) pins, though in practice all
 * have fewer.  It's an absolute travesty that we need to know anything at all
 * about the IOAPIC but the block comment above addresses that aspect.  There is
 * a lot of legacy goop in the documentation for the IOAPIC, suggesting that a
 * few pins may not be safe to use (see the additional flags in the
 * miscellaneous ixbar register definitions for examples of these).  These are
 * marked FIP_F_RESERVED and we don't allocate them; at least a few (likely 8,
 * 14, and 15) are safe to use with the proper additional configuration, but for
 * new we'll be extra careful as we are not short of pins.
 *
 * fix_mutex protects both our pin mappings and the underlying ixbar's
 * index/data register pair.
 */

typedef enum fch_intr_pin_flag {
	FIP_F_VALID = (1 << 0),
	FIP_F_RESERVED = (1 << 1)
} fch_intr_pin_flag_t;

struct fch_intr_pin {
	uint8_t			fip_idx;
	fch_intr_pin_flag_t	fip_flags;
	uint32_t		fip_src;
};

struct fch_ixbar {
	kmutex_t		fix_mutex;
	fch_intr_pin_t		*fix_pins;
	ddi_acc_handle_t	fix_reg_hdl;
	uint8_t			*fix_reg;
	uint32_t		fix_npins;
};

static const uint8_t fch_ioapic_reserved_pins[] = { 0, 1, 2, 8, 12, 14, 15 };

static uint8_t
fch_ixbar_get8(fch_ixbar_t *ixp, uint32_t reg)
{
	ASSERT(MUTEX_HELD(&ixp->fix_mutex));
	ASSERT3U(reg, >=, FCH_IXBAR_IDX);

	return (ddi_get8(ixp->fix_reg_hdl,
	    ixp->fix_reg + (reg - FCH_IXBAR_IDX)));
}

static void
fch_ixbar_put8(fch_ixbar_t *ixp, uint32_t reg, uint8_t val)
{
	ASSERT(MUTEX_HELD(&ixp->fix_mutex));
	ASSERT3U(reg, >=, FCH_IXBAR_IDX);

	ddi_put8(ixp->fix_reg_hdl, ixp->fix_reg + (reg - FCH_IXBAR_IDX), val);
}

static fch_intr_pin_t *
fch_ixbar_get_pin_locked(fch_ixbar_t *ixp, uint32_t src)
{
	fch_intr_pin_t *pp;
	uint8_t xbval, pidx;

	ASSERT(MUTEX_HELD(&ixp->fix_mutex));

	if (src == FCH_INTRSRC_NONE)
		return (NULL);

	ASSERT3U(src, <, FCH_IXBAR_MAX_SRCS);

	xbval = FCH_IXBAR_IDX_SET_SRC(0, (uint8_t)src);
	xbval = FCH_IXBAR_IDX_SET_DST(xbval, FCH_IXBAR_IDX_DST_IOAPIC);
	fch_ixbar_put8(ixp, FCH_IXBAR_IDX, xbval);

	xbval = fch_ixbar_get8(ixp, FCH_IXBAR_DATA);
	pidx = FCH_IXBAR_PIN_GET(xbval);

	if (pidx == FCH_IXBAR_PIN_NONE)
		return (NULL);

	/*
	 * During initialisation, we set every source's destination (whether or
	 * not the source index is associated with any hardware) to the black
	 * hole destination pin FCH_IXBAR_PIN_NONE.  Since then, if we have
	 * allocated a pin to src, that pin should be within the range valid for
	 * the IOAPIC.  We are in exclusive control of this ixbar, so we assert
	 * this invariant here, having already ruled out the possibility that
	 * src is routed to the black hole.
	 */
	ASSERT3U(pidx, <, ixp->fix_npins);
	if (pidx >= ixp->fix_npins)
		return (NULL);

	pp = ixp->fix_pins + pidx;

	/*
	 * Our knowledge of the pin's source should match the hardware's.  We do
	 * not support sharing pins among multiple sources, though the hardware
	 * does.  The mapping should also be valid and the pin not reserved.
	 */
	ASSERT3U(pp->fip_src, ==, src);
	ASSERT3U(pp->fip_flags & FIP_F_VALID, !=, 0);
	ASSERT0(pp->fip_flags & FIP_F_RESERVED);

	return (pp);
}

/*
 * Allocate and set up a destination pin for this child's interrupt.  If it has
 * no interrupt or no pins are available we fail by returning B_FALSE.  This
 * function is idempotent; if the interrupt has already been allocated a pin and
 * that allocation is valid, we succeed without changing anything.
 *
 * XXX We don't have any way to honour the flags here.
 */
fch_intr_pin_t *
fch_ixbar_alloc_pin(fch_ixbar_t *ixp, const fch_intrspec_t *fip)
{
	fch_intr_pin_t *pp;
	uint32_t src;
	uint8_t xbval;

	src = fip->fi_src;

	if (src == FCH_INTRSRC_NONE)
		return (NULL);

	mutex_enter(&ixp->fix_mutex);

	pp = fch_ixbar_get_pin_locked(ixp, src);
	if (pp != NULL && (pp->fip_flags & FIP_F_VALID) != 0) {
		ASSERT3U(pp->fip_src, ==, src);
		mutex_exit(&ixp->fix_mutex);
		return (pp);
	}

	for (uint8_t pidx = 0; pidx < ixp->fix_npins; pidx++) {
		pp = ixp->fix_pins + pidx;
		if ((pp->fip_flags & (FIP_F_VALID | FIP_F_RESERVED)) == 0) {
			xbval = FCH_IXBAR_IDX_SET_SRC(0, (uint8_t)src);
			xbval = FCH_IXBAR_IDX_SET_DST(xbval,
			    FCH_IXBAR_IDX_DST_IOAPIC);
			fch_ixbar_put8(ixp, FCH_IXBAR_IDX, xbval);

			xbval = FCH_IXBAR_PIN_SET(0, pidx);
			fch_ixbar_put8(ixp, FCH_IXBAR_DATA, xbval);

			pp->fip_src = src;
			pp->fip_flags |= FIP_F_VALID;

			mutex_exit(&ixp->fix_mutex);

			return (pp);
		}
	}

	/*
	 * We're out of pins.  While sharing is possible, we don't currently
	 * support it and there aren't enough sources that it should ever be
	 * necessary.
	 */
	mutex_exit(&ixp->fix_mutex);
	return (NULL);
}

/*
 * XXX We can't *really* give the caller the "IRQ number" because that's
 * technically private to apix and will be different from the IOAPIC pin number
 * if either the IOAPIC isn't the first one or there is IRQ sharing going on and
 * apix chooses to allocate a new IRQ number beyond all IOAPIC pins.  However,
 * under the conditions we know we have (no PIC, no sharing, only the first
 * IOAPIC is ever the destination for these interrupts), they're the same.  Not
 * to be a broken record, but this will be fixed by moving this all into apix.
 */

int
fch_ixbar_pin_irqno(const fch_intr_pin_t *pp)
{
	ASSERT3U(pp->fip_flags & (FIP_F_VALID | FIP_F_RESERVED), ==,
	    FIP_F_VALID);
	ASSERT3U(pp->fip_idx, !=, FCH_IXBAR_PIN_NONE);

	return (pp->fip_idx);
}

static void
fch_ixbar_blackhole_src(fch_ixbar_t *ixp, uint32_t src)
{
	uint8_t xbval;

	ASSERT(MUTEX_HELD(&ixp->fix_mutex));
	ASSERT3U(src, <, FCH_IXBAR_MAX_SRCS);

	xbval = FCH_IXBAR_IDX_SET_SRC(0, src);
	xbval = FCH_IXBAR_IDX_SET_DST(xbval, FCH_IXBAR_IDX_DST_IOAPIC);
	fch_ixbar_put8(ixp, FCH_IXBAR_IDX, xbval);

	xbval = FCH_IXBAR_PIN_SET(0, FCH_IXBAR_PIN_NONE);
	fch_ixbar_put8(ixp, FCH_IXBAR_DATA, xbval);

	/*
	 * We never direct any source to the 8259A-compatible PIC, but this code
	 * is used to initialise the ixbar so we want to make sure those
	 * connections are all disabled.  It won't hurt anything to clear them
	 * again when we free an interrupt.
	 */
	xbval = FCH_IXBAR_IDX_SET_SRC(0, src);
	xbval = FCH_IXBAR_IDX_SET_DST(xbval, FCH_IXBAR_IDX_DST_PIC);
	fch_ixbar_put8(ixp, FCH_IXBAR_IDX, xbval);

	xbval = FCH_IXBAR_PIN_SET(0, FCH_IXBAR_PIN_NONE);
	fch_ixbar_put8(ixp, FCH_IXBAR_DATA, xbval);
}

/*
 * Free the destination pin for this child.  If the source has no configured
 * destination pin, this does nothing.  It is the caller's responsibility to
 * ensure that the interrupt is disabled; it won't be received if it fires after
 * this.
 */

static void
fch_ixbar_free_pin_locked(fch_ixbar_t *ixp, fch_intr_pin_t *pp)
{
	ASSERT(MUTEX_HELD(&ixp->fix_mutex));

	if (pp == NULL)
		return;

	ASSERT0(pp->fip_flags & FIP_F_RESERVED);
	if ((pp->fip_flags & FIP_F_VALID) == 0)
		return;

	fch_ixbar_blackhole_src(ixp, pp->fip_src);
	pp->fip_flags &= ~FIP_F_VALID;
	pp->fip_src = FCH_INTRSRC_NONE;
}

void
fch_ixbar_free_pin(fch_ixbar_t *ixp, fch_intr_pin_t *pp)
{
	mutex_enter(&ixp->fix_mutex);
	fch_ixbar_free_pin_locked(ixp, pp);
	mutex_exit(&ixp->fix_mutex);
}

fch_ixbar_t *
fch_ixbar_setup(dev_info_t *dip)
{
	fch_ixbar_t *ixp;
	uint8_t xbval;
	static ddi_device_acc_attr_t reg_attr = {
		.devacc_attr_version = DDI_DEVICE_ATTR_V1,
		.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC,
		.devacc_attr_dataorder = DDI_STRICTORDER_ACC,
		.devacc_attr_access = DDI_DEFAULT_ACC
	};

	ixp = kmem_zalloc(sizeof (fch_ixbar_t), KM_SLEEP);

	/*
	 * XXX If we were in apix where we belong, we would already know how
	 * many pins our IOAPIC has (and which IOAPIC to use, though it's always
	 * the first one on every currently supported platform).  Here, we don't
	 * really have any way to tell.  While apic_io_vect{base,end} are
	 * global, apix may not be loaded yet.
	 */
	ixp->fix_npins = 24;

	if (ddi_regs_map_setup(dip, 1, (caddr_t *)&ixp->fix_reg, 0, 0,
	    &reg_attr, &ixp->fix_reg_hdl) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "mapping ixbar registers failed");
		kmem_free(ixp, sizeof (fch_ixbar_t));
		return (NULL);
	}

	mutex_init(&ixp->fix_mutex, NULL, MUTEX_DRIVER, NULL);

	ixp->fix_pins = kmem_zalloc(sizeof (fch_intr_pin_t) * ixp->fix_npins,
	    KM_SLEEP);

	for (uint_t i = 0; i < ARRAY_SIZE(fch_ioapic_reserved_pins); i++) {
		if (fch_ioapic_reserved_pins[i] < ixp->fix_npins) {
			ixp->fix_pins[fch_ioapic_reserved_pins[i]].fip_flags |=
			    FIP_F_RESERVED;
		}
	}

	/*
	 * Clear the ixbar's pin assignment for each source, then set up our own
	 * internal state.  As much as possible we want the registers themselves
	 * to be the source of truth but the ixbar doesn't provide us any way to
	 * get the source(s) assigned to a pin without walking the entire
	 * register space.
	 */

	for (uint_t i = 0; i < ixp->fix_npins; i++) {
		fch_intr_pin_t *pp = ixp->fix_pins + i;

		pp->fip_idx = i;
		pp->fip_src = FCH_INTRSRC_NONE;
	}

	/*
	 * For convenience, we take the lock here: we're about to call other
	 * functions that expect us to be holding it.  There is obviously no way
	 * anyone can access these data structures until we return.
	 */
	mutex_enter(&ixp->fix_mutex);

	for (uint_t i = 0; i < FCH_IXBAR_MAX_SRCS; i++)
		fch_ixbar_blackhole_src(ixp, i);

	/*
	 * We've set up our initial state and the xbar itself.  Now we need to
	 * set up the ancillary control registers.  We want as much as possible
	 * for all interrupt sources to come through the xbar itself; the
	 * mostly-fixed outside sources include SATA/IDE, RTC, PIT (i8254) and
	 * "IMC" which is probably not the memory controller but rather a pile
	 * of legacy kludges for emulating an i8042 via USB (this impression is
	 * strengthened by the use of pins 1 and 12 when enabled).  We use and
	 * want none of these things, ever, and in principle turning off their
	 * bypass bits should allow us to use the corresponding virtual IOAPIC
	 * pins for other things.
	 *
	 * One brief note on the PIT (i8254): the PIT is used to calibrate the
	 * TSC, but we do not otherwise use it and do not enable its interrupt.
	 * Timer interrupts come from the local APIC timer directly and do not
	 * go through the IOAPIC.
	 *
	 * We really don't want the PIC cascading into the IOAPIC at all because
	 * we don't have any PIC interrupt sources we care about (and we don't
	 * configure any of them).  Unfortunately there's no option to do that,
	 * so we set the cascade into pin 2 because it's much less confusing; we
	 * simply reserve pin 2 on the IOAPIC.
	 *
	 * Other bits are left at their POR values, including the mysterious
	 * FCH::IO::IntrMisc0Map[IntrDelay] which presumably works around some
	 * internal timing bug.
	 */

	fch_ixbar_put8(ixp, FCH_IXBAR_IDX, FCH_IXBAR_IDX_MISC);
	xbval = fch_ixbar_get8(ixp, FCH_IXBAR_DATA);
	xbval = FCH_IXBAR_MISC_SET_PIN15_SRC(xbval, FCH_IXBAR_MISC_PIN1X_XBAR);
	xbval = FCH_IXBAR_MISC_SET_PIN14_SRC(xbval, FCH_IXBAR_MISC_PIN1X_XBAR);
	xbval = FCH_IXBAR_MISC_SET_PIN12_SRC(xbval, FCH_IXBAR_MISC_PIN12_XBAR);
	xbval = FCH_IXBAR_MISC_SET_PIN8_SRC(xbval, FCH_IXBAR_MISC_PIN8_XBAR);
	xbval = FCH_IXBAR_MISC_SET_PIN1_SRC(xbval, FCH_IXBAR_MISC_PIN1_XBAR);
	xbval = FCH_IXBAR_MISC_SET_PIN0_SRC(xbval, FCH_IXBAR_MISC_PIN0_XBAR);
	fch_ixbar_put8(ixp, FCH_IXBAR_DATA, xbval);

	fch_ixbar_put8(ixp, FCH_IXBAR_IDX, FCH_IXBAR_IDX_MISC0);
	xbval = fch_ixbar_get8(ixp, FCH_IXBAR_DATA);
	xbval = FCH_IXBAR_MISC0_SET_PIN12_FILT_EN(xbval, 0);
	xbval = FCH_IXBAR_MISC0_SET_PIN1_FILT_EN(xbval, 0);
	xbval = FCH_IXBAR_MISC0_SET_XBAR_EN(xbval, 1);
	xbval = FCH_IXBAR_MISC0_SET_PINS_1_12_DIS(xbval, 0);
	xbval = FCH_IXBAR_MISC0_SET_CASCADE(xbval,
	    FCH_IXBAR_MISC0_CASCADE_PIN2);
	fch_ixbar_put8(ixp, FCH_IXBAR_DATA, xbval);

	mutex_exit(&ixp->fix_mutex);

	return (ixp);
}

void
fch_ixbar_teardown(fch_ixbar_t *ixp)
{
	/*
	 * The only way we should ever get here is if all the FCH's children
	 * have detached.  If they have, all pins should already have been
	 * freed.  If they have not, something has gone wrong and we'll panic on
	 * DEBUG bits.  Regardless, we reset the ixbar.
	 */
	if (ixp->fix_npins > 0 && ixp->fix_pins != NULL) {
		mutex_enter(&ixp->fix_mutex);

		for (uint_t i = 0; i < FCH_IXBAR_MAX_SRCS; i++) {
			fch_intr_pin_t *pp = fch_ixbar_get_pin_locked(ixp, i);
			ASSERT3P(pp, ==, NULL);
			fch_ixbar_free_pin_locked(ixp, pp);
		}

		mutex_exit(&ixp->fix_mutex);

		kmem_free(ixp->fix_pins,
		    sizeof (fch_intr_pin_t) * ixp->fix_npins);
	}
	ixp->fix_npins = 0;
	ixp->fix_pins = NULL;
	mutex_destroy(&ixp->fix_mutex);

	if (ixp->fix_reg != NULL) {
		ddi_regs_map_free(&ixp->fix_reg_hdl);
		ixp->fix_reg = NULL;
	}

	kmem_free(ixp, sizeof (fch_ixbar_t));
}
