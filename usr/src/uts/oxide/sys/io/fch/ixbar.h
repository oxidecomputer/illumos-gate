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

#ifndef _SYS_IO_FCH_IXBAR_H
#define	_SYS_IO_FCH_IXBAR_H

/*
 * The part of FCH::IO that provides access to the interrupt crossbar (ixbar)
 * inside the AMD FCH is described here.  These registers are accessible only
 * (so far as we know) through legacy I/O space.  FCH::IO::PCI_INTR_INDEX and
 * FCH::IO::PCI_INTR_DATA.  This is a typical indirect pair for accessing the
 * interrupt routing crossbar registers.
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/types.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

/* FCH::IO::PCI_INTR_{INDEX,DATA}. */
#define	FCH_IXBAR_IDX	0xc00
#define	FCH_IXBAR_DATA	0xc01

#ifndef	_ASM

/*
 * There are 256 registers accessible via the data register by setting
 * FCH_IXBAR_IDX.  All but four set the destination pin number for the
 * model-specific source peripheral whose address and destination controller
 * (emulated dual-8259A -- "PIC" -- or integrated IOAPIC, selected by
 * FCH_IXBAR_x_DST()) is in the index register; AMD calls these registers
 * FCH::IO::PCIIntMap for the source index (FCH_IXBAR_IDX_x_SRC()) in [0,7] and
 * FCH::IO::PCIInterruptMap for [c,7f].  Note that these names are different but
 * confusingly similar and moreover most of the possible sources have nothing to
 * do with PCI, though included among them are emulated INTx messages should any
 * originate in devices attached via PCIe or NBIFs.  Their usage is described in
 * the PPR for these two registers.  The per-source registers vary by model with
 * respect to the physical source to which a given source index corresponds, and
 * are not named here.
 *
 * The other 4 are (as far as we can tell) the same across all FCH models.
 * Their official names are FCH::IO::IntrMisc{,0,1,2}Map.
 *
 * Although it is possible that a future FCH implementation could have 32 IOAPIC
 * pins (or even more), none currently is known to have more than 24, and we
 * reserve a destination pin of 0x1f to indicate a source that is not routed to
 * any pin.
 */

#define	FCH_IXBAR_IDX_GET_DST(r)	bitx8(r, 7, 7)
#define	FCH_IXBAR_IDX_SET_DST(r, v)	bitset8(r, 7, 7, v)
#define	FCH_IXBAR_IDX_DST_PIC		0
#define	FCH_IXBAR_IDX_DST_IOAPIC	1
#define	FCH_IXBAR_IDX_GET_SRC(r)	bitx8(r, 6, 0)
#define	FCH_IXBAR_IDX_SET_SRC(r, v)	bitset8(r, 6, 0, v)

#define	FCH_IXBAR_MAX_SRCS	128
#define	FCH_IXBAR_SRC_VALID(s)	\
	(s != FCH_IXBAR_IDX_MISC && s != FCH_IXBAR_IDX_MISC0 && \
	s != FCH_IXBAR_IDX_MISC1 && s != FCH_IXBAR_IDX_MISC2)

#define	FCH_IXBAR_PIN_GET(r)	bitx8(r, 4, 0)
#define	FCH_IXBAR_PIN_SET(r, v)	bitset8(r, 4, 0, v)
#define	FCH_IXBAR_PIN_NONE	0x1fU

#define	FCH_IXBAR_IDX_MISC	0x08
#define	FCH_IXBAR_MISC_GET_PIN15_SRC(r)		bitx8(r, 7, 6)
#define	FCH_IXBAR_MISC_SET_PIN15_SRC(r, v)	bitset8(r, 7, 6, v)
#define	FCH_IXBAR_MISC_GET_PIN14_SRC(r)		bitx8(r, 5, 4)
#define	FCH_IXBAR_MISC_SET_PIN14_SRC(r, v)	bitset8(r, 5, 4, v)
/*
 * These are used for both PIN{15,14}_SRC.
 */
#define	FCH_IXBAR_MISC_PIN1X_LEGACY_IDE	0
#define	FCH_IXBAR_MISC_PIN1X_SATA_IDE	1
#define	FCH_IXBAR_MISC_PIN1X_SATA2	2
#define	FCH_IXBAR_MISC_PIN1X_XBAR	3
#define	FCH_IXBAR_MISC_GET_PIN12_SRC(r)		bitx8(r, 3, 3)
#define	FCH_IXBAR_MISC_SET_PIN12_SRC(r, v)	bitset8(r, 3, 3, v)
#define	FCH_IXBAR_MISC_PIN12_IMC	0
#define	FCH_IXBAR_MISC_PIN12_XBAR	1
#define	FCH_IXBAR_MISC_GET_PIN8_SRC(r)		bitx8(r, 2, 2)
#define	FCH_IXBAR_MISC_SET_PIN8_SRC(r, v)	bitset8(r, 2, 2, v)
#define	FCH_IXBAR_MISC_PIN8_RTC		0
#define	FCH_IXBAR_MISC_PIN8_XBAR	1
#define	FCH_IXBAR_MISC_GET_PIN1_SRC(r)		bitx8(r, 1, 1)
#define	FCH_IXBAR_MISC_SET_PIN1_SRC(r, v)	bitset8(r, 1, 1, v)
#define	FCH_IXBAR_MISC_PIN1_IMC		0
#define	FCH_IXBAR_MISC_PIN1_XBAR	1
#define	FCH_IXBAR_MISC_GET_PIN0_SRC(r)		bitx8(r, 0, 0)
#define	FCH_IXBAR_MISC_SET_PIN0_SRC(r, v)	bitset8(r, 0, 0, v)
#define	FCH_IXBAR_MISC_PIN0_8254	0
#define	FCH_IXBAR_MISC_PIN0_XBAR	1

#define	FCH_IXBAR_IDX_MISC0	0x09
#define	FCH_IXBAR_MISC0_GET_DELAY(r)		bitx8(r, 7, 7)
#define	FCH_IXBAR_MISC0_SET_DELAY(r, v)		bitset8(r, 7, 7, v)
#define	FCH_IXBAR_MISC0_GET_PIN12_FILT_EN(r)	bitx8(r, 6, 6)
#define	FCH_IXBAR_MISC0_SET_PIN12_FILT_EN(r, v)	bitset8(r, 6, 6, v)
#define	FCH_IXBAR_MISC0_GET_PIN1_FILT_EN(r)	bitx8(r, 5, 5)
#define	FCH_IXBAR_MISC0_SET_PIN1_FILT_EN(r, v)	bitset8(r, 5, 5, v)
#define	FCH_IXBAR_MISC0_GET_XBAR_EN(r)		bitx8(r, 4, 4)
#define	FCH_IXBAR_MISC0_SET_XBAR_EN(r, v)	bitset8(r, 4, 4, v)
#define	FCH_IXBAR_MISC0_GET_PINS_1_12_DIS(r)	bitx8(r, 3, 3)
#define	FCH_IXBAR_MISC0_SET_PINS_1_12_DIS(r, v)	bitset8(r, 3, 3, v)
#define	FCH_IXBAR_MISC0_GET_MERGE_12(r)		bitx8(r, 2, 2)
#define	FCH_IXBAR_MISC0_SET_MERGE_12(r, v)	bitset8(r, 2, 2, v)
#define	FCH_IXBAR_MISC0_GET_MERGE_1(r)		bitx8(r, 1, 1)
#define	FCH_IXBAR_MISC0_SET_MERGE_1(r, v)	bitset8(r, 1, 1, v)
#define	FCH_IXBAR_MISC0_GET_CASCADE(r)		bitx8(r, 0, 0)
#define	FCH_IXBAR_MISC0_SET_CASCADE(r, v)	bitset8(r, 0, 0, v)
#define	FCH_IXBAR_MISC0_CASCADE_PIN2	0
#define	FCH_IXBAR_MISC0_CASCADE_PIN0	1

/* These are aliases for collections of HPET registers.  See the PPRs. */
#define	FCH_IXBAR_IDX_MISC1	0x0A
#define	FCH_IXBAR_IDX_MISC2	0x0B

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_IXBAR_H */
