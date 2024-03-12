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

#ifndef _SYS_AMDZEN_FCH_H
#define	_SYS_AMDZEN_FCH_H

/*
 * These are utility routines, mostly for metaprogramming, used to provide
 * access to functional units in the AMD Fusion Controller Hub (FCH).  While
 * most FCHs are fairly similar, the emphasis is on support for the EPYC-branded
 * server processor families, which contain the following FCH models (all FCHs
 * appear to be named for mountains):
 *
 * Processor Family		FCH Model
 * --------------------------------------
 *  Naples			Taishan
 *  Rome, Milan			Huashan
 *  Genoa, Bergamo		Songshan
 *
 * Support for additional models, as well as additional functional units and
 * registers for these models, can and should be added as the need arises.
 *
 * Most of the functional units in these FCHs can be accessed via SMN or MMIO,
 * and the region normally located between 0xfed8_0000 and 0xfed8_2000 can be
 * accessed on secondary FCHs via MMIO at a semi-arbitrary location specified by
 * a BAR.  We choose functional units so that all registers in each have the
 * same offset relative to an SMN base and an MMIO base, allowing us to reuse
 * the definitions to get a register instance representing either access method
 * as we choose.  This is easier than it might seem; as an example the Huashan
 * register space looks like this:
 *
 * MMIO PA			      SMN
 * 0xFFFF_FFFF	+-------------------+ 0x44FF_FFFF
 *		| SPI NOR flash R/O |
 * 0xFF00_0000	+-------------------+ 0x4400_0000
 *		|      Unknown      |
 * 0xFEF0_0000	+-------------------+ Unknown
 *		| APIC (non-FCH)(A) |
 * 0xFEE0_0000	+-------------------+ Unknown
 *		|      Unknown      |
 * 0xFEDD_6000	+-------------------+ Unknown
 *		|       eMMC        |
 * 0xFEDD_5000	+-------------------+ 0x02DC_5000
 *		|      SPI (*)      |
 * Unknown	+-------------------+ 0x02DC_4000
 *		|      Unknown      |
 * 0xFEDD_0000	+-------------------+ Unknown
 *		|       UART3       |
 * 0xFEDC_F000	+-------------------+ Unknown
 *		|       UART2       |
 * 0xFEDC_E000	+-------------------+ Unknown
 *		|     UART3 DMA     |
 * 0xFEDC_D000	+-------------------+ Unknown
 *		|     UART2 DMA     |
 * 0xFEDC_C000	+-------------------+ Unknown
 *		|       I2C5        |
 * 0xFEDC_B000	+-------------------+ N/A
 *		|       UART1       |
 * 0xFEDC_A000	+-------------------+ Unknown
 *		|       UART0       |
 * 0xFEDC_9000	+-------------------+ Unknown
 *		|     UART1 DMA     |
 * 0xFEDC_8000	+-------------------+ Unknown
 *		|     UART0 DMA     |
 * 0xFEDC_7000	+-------------------+ Unknown
 *		|       I2C4        |
 * 0xFEDC_6000	+-------------------+ N/A
 *		|       I2C3        |
 * 0xFEDC_5000	+-------------------+ N/A
 *		|       I2C2        |
 * 0xFEDC_4000	+-------------------+ N/A
 *		|       I2C1        |
 * 0xFEDC_3000	+-------------------+ 0x02DC_3000
 *		|       I2C0        |
 * 0xFEDC_2000	+-------------------+ 0x02DC_2000
 *		|      Unknown      |
 * 0xFEDC_0000	+-------------------+ Unknown
 *		|      Unknown      |
 * 0xFED8_2000	+-------------------+ Unknown
 *		|      Unknown      |
 * 0xFED8_1F00	+-------------------+ Unknown
 *		|       AOAC        |
 * 0xFED8_1E00	+-------------------+ 0x02D0_2E00
 *		|    Wake Timers    |
 * 0xFED8_1D00	+-------------------+ 0x02D0_2D00
 *		|      Unknown      |
 * 0xFED8_1900	+-------------------+ 0x02D0_2900
 *		|    KERNCZ GPIO    |
 * 0xFED8_1500	+-------------------+ 0x02D0_2500
 *		|     DP-VGA (?)    |
 * 0xFED8_1400	+-------------------+ Unknown
 *		|       MISC2       |
 * 0xFED8_1300	+-------------------+ 0x02D0_2300
 *		|    Remote GPIO    |
 * 0xFED8_1200	+-------------------+ 0x02D0_2200
 *		|   Shadow Counter  |
 * 0xFED8_1100	+-------------------+ 0x02D0_2100
 *		|      Unknown      |
 * 0xFED8_0F00	+-------------------+ Unknown
 *		|      PM MISC      |
 * 0xFED8_0E00	+-------------------+ 0x02D0_1E00
 *		|    KERNCZ IOMUX   |
 * 0xFED8_0D00	+-------------------+ 0x02D0_1D00
 *		|      HPET (+)     |
 * 0xFED8_0C00	+-------------------+ 0x02D0_1C00
 *		|     Watchdog      |
 * 0xFED8_0B00	+-------------------+ 0x02D0_1B00
 *		|       SMBus       |
 * 0xFED8_0A00	+-------------------+ 0x02D0_1A00
 *		|        ASF        |
 * 0xFED8_0900	+-------------------+ 0x02D0_1900
 *		|   FCH::PM (ACPI)  |
 * 0xFED8_0800	+-------------------+ 0x02D0_1800
 *		|        RTC        |
 * 0xFED8_0700	+-------------------+ 0x02D0_1700
 *		|    CMOS RAM (?)   |
 * 0xFED8_0600	+-------------------+ Unknown
 *		|    BIOS RAM (?)   |
 * 0xFED8_0500	+-------------------+ Unknown
 *		|        PM2        |
 * 0xFED8_0400	+-------------------+ 0x02D0_1400
 *		|   FCH::PM (PMIO)  |
 * 0xFED8_0300	+-------------------+ 0x02D0_1300
 *		|        SMI        |
 * 0xFED8_0200	+-------------------+ 0x02D0_1200
 *		|      Unknown      |
 * 0xFED8_0100	+-------------------+ Unknown
 *		|  SMBUS PCI config |
 * 0xFED8_0000	+-------------------+ 0x02D0_1000
 *		|      Unknown      |
 * 0xFED5_0000	+-------------------+ Unknown
 *		|      TPM LPC      |
 * 0xFED4_0000	+-------------------+ Unknown
 *		|      Unknown      |
 * 0xFED0_1000	+-------------------+ Unknown
 *		|      HPET (+)     |
 * 0xFED0_0000	+-------------------+ 0x02D0_1C00
 *		|      Unknown      |
 * 0xFEC2_1000	+-------------------+ Unknown
 *		|   eSPI JTAG (*)   |
 * 0xFEC2_0000	+-------------------+ Unknown
 *		|      Unknown      |
 * 0xFEC1_1000	+-------------------+ Unknown
 *		|      SPI (*)      |
 * 0xFEC1_0000	+-------------------+ 0x02DC_4000
 *		|      Unknown      |
 * 0xFEC0_1000	+-------------------+ Unknown
 *		|      IOAPIC       |
 * 0xFEC0_0000	+-------------------+ Unknown
 *		|      Unknown      |
 * 0xFEB0_1000	+-------------------+ Unknown
 *		|    Watchdog (+)   |
 * 0xFEB0_0000	+-------------------+ Unknown
 *
 * (*) The SPI and eSPI regions are set by a nonstandard BAR in the LPC
 * controller's PCI config space; eSPI is always SPI + 0x1_0000.  At reset, this
 * BAR holds 0xFEC1_0000.  We don't know whether the SPI controller also appears
 * at 0xFEDD_4000 as we would expect it to, or if it has a second set of
 * registers that appear there.
 *
 * (+) Documentation suggests that the FCH can be configured to decode a region
 * of memory at 0xFEB0_0000 for the watchdog timer, but doing so doesn't seem to
 * have any effect, and this region is normally used for PCI or other
 * general-purpose MMIO assignments.  The region at 0xFED8_0B00 is used instead;
 * it's possible that this alternate space was intended to allow accessing the
 * WDT on a page by itself.  The HPET region is similar, but the page-by-itself
 * decoding option definitely works and is the standard means of accessing the
 * HPET.
 *
 * (?) These regions are listed in documentation but their internal organisation
 * is not described.  So-called BIOS RAM and CMOS RAM are legacy PC concepts
 * that are most likely emulated in their respective regions; we do not use them
 * and they do not contain registers.  The function of DP-VGA is completely
 * unknown.
 *
 * (A) There are two different ways to think about this range, depending on the
 * origin of the access.  From the Zen core, this region is conventionally
 * decoded by the xAPIC when in xAPIC mode, though this can be changed by
 * setting Core::X86::Msr::APIC_BAR.  Because the local APIC is part of the
 * logical processor itself, the access never reaches the FCH when the core is
 * in xAPIC mode; this is discussed in the Milan B1 PPR section dealing with how
 * core memory accesses are decoded.  It's possible that these do reach the FCH
 * from a core when in x2APIC mode, but as nothing in the FCH decodes them there
 * is nothing to access.  From a northbridge (e.g., on behalf of MSIs sent
 * upstream by PCIe devices), there is no programmable BAR.  All MSI delivery is
 * done to this fixed address region that is decoded for local APICs (regardless
 * of whether in xAPIC or x2APIC mode) by the PIE; indeed, there is language
 * suggesting that the translation into HT interrupts to be decoded by the PIE
 * occurs in the NB itself, and these never hit the DF as normal memory accesses
 * at all.  Regardless, these accesses are not decoded by the FCH's IOMS via any
 * of the DF range registers.  So despite hitting in the FCH's range, these APIC
 * addresses do not refer to any FCH functionality and the FCH never decodes
 * them regardless of their origin.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These base addresses describe the start of the region of small functional
 * units that are accessible on secondary FCHs via a fixed SMN address
 * (targeting the FCH's containing node) or by MMIO at a relocatable base
 * address.  These units are referred to here as the relocatable part of the
 * FCH, even though the addresses are fixed when using the SMN access method.
 * Each functional unit, whether among the relocatable subset or not, has its
 * own header that contains the base addresses, register block lookup functions,
 * and register lookup functions for that functional unit and its sub-units.
 * These addresses are unsuffixed because they may be consumed by assembly.
 */
#define	FCH_RELOCATABLE_PHYS_BASE	0xfed80000
#define	FCH_RELOCATABLE_SMN_BASE	0x02d01000

#ifndef	_ASM

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/x86_archext.h>


typedef enum fch_kind {
	FK_NONE = 0,
	FK_TAISHAN,
	FK_HUASHAN,
	FK_SONGSHAN,
	FK_KUNLUN
} fch_kind_t;

/*
 * Returns the FCH model for the given chiprev.
 *
 * See the "FCH Identification" comment in fch.c for more information.
 */
static inline fch_kind_t
chiprev_fch_kind(x86_chiprev_t chiprev)
{
	switch (chiprev_family(chiprev)) {
	case X86_PF_AMD_NAPLES:
		return (FK_TAISHAN);
	case X86_PF_AMD_ROME:
	case X86_PF_AMD_MILAN:
		return (FK_HUASHAN);
	case X86_PF_AMD_GENOA:
	case X86_PF_AMD_BERGAMO:
		return (FK_SONGSHAN);
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		return (FK_KUNLUN);
	default:
		/* There may be an FCH but we don't know what it is. */
		return (FK_NONE);
	}
}

/*
 * Block-lookup and register-lookup function-generating macros for run of the
 * mill blocks, which are more than half of the FCH's contents.
 */
#define	MAKE_SMN_FCH_REG_FN(_unit, _unitlc, _base, _len, _defsz)	\
CTASSERT((_defsz) == 1 || (_defsz) == 2 || (_defsz) == 4);		\
static inline smn_reg_t							\
fch_ ## _unitlc ## _smn_reg(const smn_reg_def_t def, const uint16_t reginst) \
{									\
	const uint32_t APERTURE_BASE = (_base);				\
	const uint32_t APERTURE_LEN = (_len);				\
									\
	const uint32_t reginst32 = (const uint32_t)reginst;		\
									\
	const uint32_t nents = (def.srd_nents == 0) ? 1 :		\
	    (const uint32_t)def.srd_nents;				\
									\
	const uint32_t size = (def.srd_size == 0) ? (_defsz) :		\
	    (const uint32_t)def.srd_size;				\
	const uint32_t stride = (def.srd_stride == 0) ? size : def.srd_stride; \
									\
	ASSERT3U(stride, >=, size);					\
	ASSERT(size == 1 || size == 2 || size == 4);			\
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_FCH_ ## _unit);		\
	ASSERT3U(nents, >, reginst32);					\
									\
	const uint32_t reg = def.srd_reg + reginst32 * stride;		\
	ASSERT3U(reg, <=, APERTURE_LEN - size);				\
									\
	return (SMN_MAKE_REG_SIZED(APERTURE_BASE + reg,			\
	    (const uint8_t)size));					\
}

/*
 * There are numerous functional units within the 8 KiB relocatable region; this
 * region can be accessed on secondary FCHs via the alternate space BAR if so
 * configured.  Each such unit has a primary (fixed) and secondary
 * (BAR-relative) block-lookup function.
 */

/*
 * Expands to a primary FCH register block lookup function.  Applicable to any
 * FCH block with only a single sub-unit, whether relocatable or not.  This
 * cannot be used for units with multiple sub-units like I2C and UART.
 */
#define	MAKE_MMIO_FCH_REG_BLOCK_FN(_unit, _unitlc, _base, _len)		\
static inline mmio_reg_block_t						\
fch_ ## _unitlc ## _mmio_block(void)					\
{									\
	const mmio_reg_block_phys_t phys = {				\
		.mrbp_base = (_base),					\
		.mrbp_len = (_len)					\
	};								\
									\
	return (mmio_reg_block_map(SMN_UNIT_FCH_ ## _unit, phys));	\
}

/*
 * Expands to a pair of FCH register block-lookup functions, one for the primary
 * FCH at a fixed physical address and another for secondary FCHs that accepts
 * the base address from the BAR.  The base address argument to the macro is
 * replaced by an offset from the base of the relocatable region.  This is
 * useful only for units with a single sub-unit, which describes all relocatable
 * units in all supported FCHs.
 */
#define	MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(_unit, _unitlc, _off, _len)	\
static inline mmio_reg_block_t						\
fch_sec_ ## _unitlc ## _mmio_block(paddr_t base)			\
{									\
	const mmio_reg_block_phys_t phys = {				\
		.mrbp_base = base + (_off),				\
		.mrbp_len = (_len)					\
	};								\
									\
	return (mmio_reg_block_map(SMN_UNIT_FCH_ ## _unit, phys));	\
}									\
MAKE_MMIO_FCH_REG_BLOCK_FN(_unit, _unitlc,				\
    FCH_RELOCATABLE_PHYS_BASE + _off, (_len));

/*
 * Register lookup generator for FCH sub-units.
 */
#define	MAKE_MMIO_FCH_REG_FN(_unit, _unitlc, _defsz)	\
    MAKE_MMIO_REG_FN(fch_ ## _unitlc ## _mmio_reg, FCH_ ## _unit, (_defsz));

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_FCH_H */
