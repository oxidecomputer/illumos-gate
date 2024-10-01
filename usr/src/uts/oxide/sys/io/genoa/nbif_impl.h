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
 * Copyright 2024 Oxide Computer Company
 */

#ifndef _SYS_IO_GENOA_NBIF_IMPL_H
#define	_SYS_IO_GENOA_NBIF_IMPL_H

/*
 * Genoa-specific register and bookkeeping definitions for the north bridge
 * interface (nBIF or NBIF). This subsystem provides a PCIe-ish interface to a
 * variety of components like USB and SATA that are not supported by this
 * machine architecture.
 */

#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/nbif.h>
#include <sys/io/zen/nbif_impl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The maximum number of functions is based on the hardware design here. Each
 * NBIF has potentially one or more root complexes and endpoints.
 */
#define	GENOA_NBIF0_NFUNCS	10
#define	GENOA_NBIF1_NFUNCS	4
#define	GENOA_NBIF2_NFUNCS	3
#define	GENOA_NBIF_MAX_NFUNCS	GENOA_NBIF0_NFUNCS

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_NBIF_IMPL_H */
