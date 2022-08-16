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

#ifndef _IXBAR_H
#define	_IXBAR_H

/*
 * This is the interface to the ixbar for use by the rest of the FCH driver.
 * The entire surface should be subsumed by apix and provided via the existing
 * psm_intr_ops calls.  See notes at the top of ixbar.c.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct fch_ixbar;
typedef struct fch_ixbar fch_ixbar_t;

/*
 * XXX This opaque type represents a source->pin mapping, which corresponds
 * roughly to the IRQ number that the current apix needs.  This should be
 * encapsulated into apix and this intermediate interface deleted; the only
 * thing one can do with it today is obtain the IRQ number to hand apix.
 */
struct fch_intr_pin;
typedef struct fch_intr_pin fch_intr_pin_t;

/*
 * XXX Reliance on the fch node's dip here is rather vile; we need it to map the
 * ixbar registers.  In apix we would obtain them directly through an interface
 * similar to that in mmioreg.h; while it is part of the FCH, it should be
 * reserved out of the regions the FCH driver can use itself or hand out.
 */
extern fch_ixbar_t *fch_ixbar_setup(dev_info_t *);
extern void fch_ixbar_teardown(fch_ixbar_t *);

extern fch_intr_pin_t *fch_ixbar_alloc_pin(fch_ixbar_t *,
    const fch_intrspec_t *);
extern int fch_ixbar_pin_irqno(const fch_intr_pin_t *);
extern void fch_ixbar_free_pin(fch_ixbar_t *, fch_intr_pin_t *);

#ifdef __cplusplus
}
#endif

#endif /* _IXBAR_H */
