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

#ifndef	_SYS_IO_ZEN_PHYSADDRS_H
#define	_SYS_IO_ZEN_PHYSADDRS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This address represents the beginning of a compatibility MMIO range. This
 * range is accessed using subtractive decoding somehow, which means that if we
 * program an address range into the DF that overlaps this we will lose access
 * to these compatibility devices which generally speaking contain the FCH.
 */
#define	ZEN_PHYSADDR_COMPAT_MMIO	0xfec00000UL
#define	ZEN_COMPAT_MMIO_SIZE		0x01400000UL
#define	ZEN_PHYSADDR_MMIO32_END		0x100000000UL

/*
 * The FCH also has a compatibility range for legacy I/O ports.
 */
#define	ZEN_IOPORT_COMPAT_BASE	0U
#define	ZEN_IOPORT_COMPAT_SIZE	0x1000U

/*
 * This 12 GiB range below 1 TiB can't be accessed as DRAM and is not supposed
 * to be used for MMIO in general, although it may be used for the 64 MiB flash
 * aperture from the SPI controller.  The exact reason for this hole is not well
 * documented but it is known to be an artefact of the IOMMU implementation.
 */
#define	ZEN_PHYSADDR_IOMMU_HOLE		0xfd00000000UL
#define	ZEN_PHYSADDR_IOMMU_HOLE_END	0x10000000000UL

/*
 * The physical addresses of the IOAPIC are set by us,
 * not architecturally defined.  However, these are the
 * most common addresses used.
 *
 * If we ever need to adjust them based on a new
 * microarchitecture, we can do so then.
 */
#define	ZEN_PHYSADDR_FCH_IOAPIC		0xfec00000UL
#define	ZEN_PORTADDR_FCH_IOAPIC		0xf0

#define	ZEN_PHYSADDR_IOHC_IOAPIC	0xfec01000UL
#define	ZEN_PORTADDR_IOHC_IOAPIC	0xf1

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PHYSADDRS_H */
