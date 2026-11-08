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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _SYS_IO_FCH_HPET_H
#define	_SYS_IO_FCH_HPET_H

/*
 * FCH::TMR::HPET is a standard IA-PC HPET at a fixed physical address. Its
 * decoding is controlled by FCH::PM::DECODEEN[hpeten]. We access it only via
 * MMIO and only with 32-bit accesses, which every supported FCH permits
 * regardless of FCH::PM::DECODEEN[hpetwidthsel].
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the standard HPET register block. When FCH::PM::DECODEEN[hpeten] is
 * set the FCH decodes 0xfed00000 to 0xfed001ff, which covers all of its HPET
 * registers. It is not the FCH's own 256-byte HPET window at offset 0xC00 in
 * the ACPI MMIO region, which has a different layout.
 */
#define	FCH_HPET_PHYS_BASE	0xfed00000
#define	FCH_HPET_SIZE		0x200

#ifndef	_ASM

MAKE_MMIO_FCH_REG_BLOCK_FN(HPET, hpet, FCH_HPET_PHYS_BASE, FCH_HPET_SIZE);
MAKE_MMIO_FCH_REG_FN(HPET, hpet, 4);

/*
 * FCH::TMR::HPET::ID, the low half of the general capabilities and ID
 * register.
 */
/*CSTYLED*/
#define	D_FCH_HPET_ID	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_HPET,	\
	.srd_reg = 0x00	\
}
#define	FCH_HPET_ID_MMIO(b)	\
    fch_hpet_mmio_reg((b), D_FCH_HPET_ID, 0)
#define	FCH_HPET_ID_GET_VENDORID(r)		bitx32(r, 31, 16)
#define	FCH_HPET_ID_GET_LEGACY_CAP(r)		bitx32(r, 15, 15)
#define	FCH_HPET_ID_GET_COUNTER_SIZE_CAP(r)	bitx32(r, 13, 13)
#define	FCH_HPET_ID_GET_NUM_TMR_CAP(r)		bitx32(r, 12, 8)
#define	FCH_HPET_ID_GET_REVID(r)		bitx32(r, 7, 0)

/*
 * FCH::TMR::HPET::CLKPERIOD, the high half of the general capabilities and
 * ID register. This is the period of the main counter in femtoseconds.
 */
/*CSTYLED*/
#define	D_FCH_HPET_CLKPERIOD	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_HPET,	\
	.srd_reg = 0x04	\
}
#define	FCH_HPET_CLKPERIOD_MMIO(b)	\
    fch_hpet_mmio_reg((b), D_FCH_HPET_CLKPERIOD, 0)

/*
 * The HPET specification requires a period no greater than 100ns.
 */
#define	FCH_HPET_CLKPERIOD_MAX_FS	100000000U

/*
 * FCH::TMR::HPET::HPETCONFIG, the general configuration register. Setting
 * tmren starts the main counter and allows any enabled timer interrupts.
 * Clearing it stops the counter and disables all timer interrupts. Setting
 * legacyen routes timer 0 to IRQ0 of the PIC and input 2 of the IOAPIC, and
 * timer 1 to IRQ8 of the PIC and input 8 of the IOAPIC.
 */
/*CSTYLED*/
#define	D_FCH_HPET_HPETCONFIG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_HPET,	\
	.srd_reg = 0x10	\
}
#define	FCH_HPET_HPETCONFIG_MMIO(b)	\
    fch_hpet_mmio_reg((b), D_FCH_HPET_HPETCONFIG, 0)
#define	FCH_HPET_HPETCONFIG_GET_LEGACYEN(r)	bitx32(r, 1, 1)
#define	FCH_HPET_HPETCONFIG_SET_LEGACYEN(r, v)	bitset32(r, 1, 1, v)
#define	FCH_HPET_HPETCONFIG_GET_TMREN(r)	bitx32(r, 0, 0)
#define	FCH_HPET_HPETCONFIG_SET_TMREN(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::TMR::HPET::MAIN_COUNTER_L, the low 32 bits of the main counter.
 */
/*CSTYLED*/
#define	D_FCH_HPET_MAIN_COUNTER_L	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_HPET,	\
	.srd_reg = 0xf0	\
}
#define	FCH_HPET_MAIN_COUNTER_L_MMIO(b)	\
    fch_hpet_mmio_reg((b), D_FCH_HPET_MAIN_COUNTER_L, 0)

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_HPET_H */
