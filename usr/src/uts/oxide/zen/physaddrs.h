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

#ifndef	_ZEN_PHYSADDRS_H
#define	_ZEN_PHYSADDRS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This 12 GiB range below 1 TiB can't be accessed as DRAM and is not supposed
 * to be used for MMIO in general, although it may be used for the 64 MiB flash
 * aperture from the SPI controller.  The exact reason for this hole is not well
 * documented but it is known to be an artefact of the IOMMU implementation.
 */
#define	ZEN_PHYSADDR_IOMMU_HOLE		0xfd00000000UL
#define	ZEN_PHYSADDR_IOMMU_HOLE_END	0x10000000000UL

#ifdef	__cplusplus
}
#endif

#endif /* _ZEN_PHYSADDRS_H */