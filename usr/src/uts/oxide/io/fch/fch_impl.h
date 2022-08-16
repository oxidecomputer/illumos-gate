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

#ifndef _FCH_IMPL_H
#define	_FCH_IMPL_H

/*
 * Private implementation for the FCH driver.  Some of these definitions really
 * belong as part of the machdep or common DDI but aren't there yet.
 */

#include <sys/apix.h>
#include <sys/debug.h>
#include <sys/stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XXX There is a ddi_intrspec_t in the DDI, but it's supposed to be obsolete;
 * there is a struct intrspec that implements that opaque type in PCI but it's
 * not useful either.  Here's something that would be useful if only we had some
 * way to pass it to apix.
 *
 * The fi_src is a source index in the FCH's ixbar downstream of the IOAPIC.
 * The polarity and trigger mode describe how the IOAPIC pin chosen to receive
 * the interrupts should be configured.  These enumerated types from sys/apix.h
 * describe hardware in the sense that they correspond to configuration that can
 * be set up in the IOAPIC, but the in-memory representation here is not
 * intended to, and need not, match that in any APIC registers.
 *
 * For now, we support only one interrupt source per child node, but there is no
 * reason this couldn't be expanded if needed in future since it looks exactly
 * like the register specs.  While the actual source identifiers are only 7 bits
 * wide, we allow an abstract 32-bit source ID should we need to accommodate
 * other types of interrupt source.
 */

typedef struct fch_intrspec {
	intr_polarity_t		fi_pol;
	intr_trigger_mode_t	fi_tm;
	uint32_t		fi_src;
} fch_intrspec_t;

#define	FCH_INTRSRC_NONE	(uint32_t)-1

#ifdef __cplusplus
}
#endif

#endif /* _FCH_IMPL_H */
