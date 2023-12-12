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

#ifndef _GENOA_GENOA_PHYSADDRS_H
#define	_GENOA_GENOA_PHYSADDRS_H

/*
 * This header contains a bunch of information about physical addresses in the
 * system, what exists, and related.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * XXX This memory map is definitely incomplete. Please expand it.
 *
 * The following diagram describes how physical memory is allocated on this
 * system. There are a couple of things to note. First, there are two major
 * reserved areas that exist in the > 4GiB space, each of which is 12 GiB in
 * size. The lower one is problematic in that it shows up right in the middle of
 * the above 4 GiB region of DRAM. As such, we will make sure that we never
 * start MMIO space below this point as we have plenty of space and there's not
 * really much point.
 *
 * +---------------------+ UINT64_MAX
 * |                     |
 * |     End of the      |  All addresses here are aborted by the CPU.
 * |        World        |
 * |                     |
 * +---------------------+ 0xffff_ffff_ffff -- 48 TiB
 * |                     |
 * |       System        |  Reserved by the SoC.
 * |      Reserved       |
 * |                     |
 * +---------------------+ 0xfffd_0000_0000 -- 48 TiB - 12 GiB
 * |                     |
 * |      Primary        |  Primary MMIO Space. Must be assigned to each IOMS
 * |      MMIO to        |  and can then be assigned to each PCIe root complex.
 * |    be assigned      |  Starting address varies based on DRAM population.
 * |                     |
 * +---------------------+ Upper MMIO Base + 0x1000_0000
 * |                     |
 * |       PCIe          |  Home of our classical memory mapped way of getting
 * |     Extended        |  at PCIe since we no longer need to use I/O ports.
 * | Configuration Space |  There is 1 MiB for each of 256 buses.
 * |                     |
 * +---------------------+ MAX(Core::X86::Msr::TOM2, 0x100_0000_0000 -- 1 TiB)
 *			   Upper MMIO Base
 *          ~~~~
 * +---------------------+ 0x100_0000_0000 -- 1 TiB
 * |                     |
 * |       System        |  Reserved by the SoC.
 * |      Reserved       |
 * |                     |
 * +---------------------+ 0xfd_0000_0000 -- 1 TiB - 12 GiB
 *          ~~~~
 * +---------------------+ Core::X86::Msr::TOM2
 * |                     |
 * |        DRAM         |  This is the second region of DRAM that continues
 * |       Again!        |  across the lower 4 GiB hole.
 * |                     |
 * +---------------------+ 0x1_0000_0000 -- 4 GiB
 * | boot flash aperture |
 * |     read-only       |
 * +---------------------+ 0xff00_0000
 * |   XXX fill me in!   |
 * +---------------------+ 0xfee0_1000
 * |  legacy LAPIC regs  |
 * |  (movable via BAR)  |
 * +---------------------+ 0xfee0_0000
 * |  XXX more FCH here  |
 * +---------------------+ 0xfedd_0000
 * |        UART3        |
 * +---------------------+ 0xfedc_f000
 * |        UART2        |
 * +---------------------+ 0xfedc_e000
 * |  XXX more FCH here  |
 * +---------------------+ 0xfedc_b000
 * |        UART1        |
 * +---------------------+ 0xfedc_a000
 * |        UART0        |
 * +---------------------+ 0xfedc_9000
 * |  XXX more FCH here  |
 * +---------------------+ 0xfed8_1200
 *          ~~~~			There is much more to fill in here!
 * +---------------------+ 0xfed8_0f00
 * |  FCH miscellaneous  |
 * +- - - - - - - - - - -+ 0xfed8_0e00
 * |        IOMUX        |
 * +- - - - - - - - - - -+ 0xfed8_0d00	Note that all of these devices are
 * |    Watchdog timer   |		part of a single page, so we cannot
 * +- - - - - - - - - - -+ 0xfed8_0b00	protect one driver from another if
 * |   SMBus registers   |		they are separate.
 * +- - - - - - - - - - -+ 0xfed8_0a00
 * |    ASF registers    |
 * +- - - - - - - - - - -+ 0xfed8_0900
 * |    RTC registers    |
 * +- - - - - - - - - - -+ 0xfed8_0700
 * |  ACPI PM2 registers |
 * +- - - - - - - - - - -+ 0xfed8_0400
 * |  ACPI PM registers  |
 * +- - - - - - - - - - -+ 0xfed8_0300
 * |   SMI control regs  |
 * +- - - - - - - - - - -+ 0xfed8_0200
 * |  SMBus controller   |
 * | fake PCI cfg space  |
 * +---------------------+ 0xfed8_0000
 * |        HPET         |
 * +---------------------+ 0xfed0_0000
 * |   eSPI registers    |
 * +---------------------+ 0xfec2_0000
 * |   SPI registers     |
 * +---------------------+ 0xfec1_0000
 * |       IOAPIC        |
 * +---------------------+ 0xfec0_0000
 * |                     |
 * |      Free MMIO      |  This region of MMIO is assigned to the 'primary'
 * |  Assigned to FCH    |  FCH's IOMS contiguous with the fixed region above.
 * |        IOMS         |
 * +- - - - - - - - - - -+ 0xe000_0000 -- 3.5 GiB
 * |                     |
 * |                     |
 * |      Available      |  This provides access to 32-bit addresses for PCI
 * |     32-bit MMIO     |  bars and other devices. This is split evenly among
 * |                     |  all of the IOMSes except for the one containing the
 * |                     |  primary FCH.
 * |                     |
 * +---------------------+ Core::X86::Msr::TOM = 0x8000_0000 -- 2 GiB
 * |                     |
 * |        DRAM         | In general, this region is the lower part of DRAM.
 * |    from before      | On PCs, some of this is MMIO but we do not enable
 * |       64-bit        | any of that.
 * |                     |
 * +---------------------+ 0x0000_0000 - 0
 */

/*
 * This address represents the beginning of a compatibility MMIO range. This
 * range is accessed using subtractive decoding somehow, which means that if we
 * program an address range into the DF that overlaps this we will lose access
 * to these compatibility devices which generally speaking contain the FCH.
 */
#define	GENOA_PHYSADDR_COMPAT_MMIO	0xfec00000UL
#define	GENOA_COMPAT_MMIO_SIZE		0x01400000UL
#define	GENOA_PHYSADDR_MMIO32_END	0x100000000UL

/*
 * The FCH also has a compatibility range for legacy I/O ports.
 */
#define	GENOA_IOPORT_COMPAT_BASE	0U
#define	GENOA_IOPORT_COMPAT_SIZE	0x1000U

/*
 * This 12 GiB range below 1 TiB can't be accessed as DRAM and is not supposed
 * to be used for MMIO in general, although it may be used for the 64 MiB flash
 * aperture from the SPI controller.  The exact reason for this hole is not well
 * documented but it is known to be an artefact of the IOMMU implementation.
 */
#define	GENOA_PHYSADDR_IOMMU_HOLE	0xfd00000000UL
#define	GENOA_PHYSADDR_IOMMU_HOLE_END	0x10000000000UL

/*
 * This is the final address that we can use for MMIO. Beyond this is an
 * explicitly reserved area that we're not supposed to touch.
 */
#define	GENOA_PHYSADDR_MMIO_END		0xfffd00000000UL

/*
 * These are the MMIO Addresses for the IOAPICs.  One of them is in the FCH
 * and cannot be moved, the other is in the IOH/NBIO3.  The latter can be put
 * almost anywhere, as long as it is part of the non-PCI range routed to IOMS3.
 * That link is necessitated by the connection between NBIO3 and the FCH. This
 * address is fairly arbitrary; AGESA on Ethanol-X puts it here by default; we
 * may wish to change it to something else.
 */
#define	GENOA_PHYSADDR_FCH_IOAPIC	0xfec00000UL
#define	GENOA_PHYSADDR_IOHC_IOAPIC	0xfec01000UL

#ifdef __cplusplus
}
#endif

#endif /* _GENOA_GENOA_PHYSADDRS_H */
