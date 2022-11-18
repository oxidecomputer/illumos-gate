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

#ifndef _SYS_AMDZEN_FCH_IOMUX_H
#define	_SYS_AMDZEN_FCH_IOMUX_H

/*
 * FCH::IOMUX provides pinmuxing for low-speed peripherals including GPIO and
 * most of the other FCH peripherals.  In addition to FCH::IOMUX, pinmuxing for
 * the pins associated with FCH::RMTGPIO is provided by a separate unit
 * containing part of that logic's register space.  That is defined here;
 * however, to understand how it fits into the actual GPIO peripheral space more
 * generally, see <sys/amdzen/fch/gpio.h>.
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

#define	FCH_IOMUX_OFF		0x0d00
#define	FCH_IOMUX_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_IOMUX_OFF)
#define	FCH_IOMUX_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_IOMUX_OFF)
#define	FCH_IOMUX_SIZE		0x100

#ifndef	_ASM

MAKE_SMN_FCH_REG_FN(IOMUX, iomux, FCH_IOMUX_SMN_BASE, FCH_IOMUX_SIZE, 1);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(IOMUX, iomux, FCH_IOMUX_OFF, FCH_IOMUX_SIZE);
MAKE_MMIO_FCH_REG_FN(IOMUX, iomux, 1);

/*
 * FCH::IOMUX::IOMUX%u_GPIO -- This is an I/O mux register. Each I/O mux
 * register is used to select between one of four functions in its lower 2 bits.
 * Each register is only a single byte wide. On all diferent CPU families, the
 * size and shape of I/O mux entries is the same. While surveying AMD parts, we
 * have found that while the I/O mux is larger than the size listed below the
 * last valid entry varies. We can phrase these two camps as:
 *
 *   o Normal CPUs tend to have up to 0x99 entries. This includes Naples, Rome,
 *     Matisse, Vermeer, Genoa, and Bergamo.
 *   o APUs on the other hand have the last valid entry at 0x90. This includes
 *     all the other Zen 1-4 parts (e.g. Cezanne, Raphael, etc.).
 *
 * In all of these cases the subsequent FCH region (a MISC block) doesn't begin
 * until the next 0x100 byte aligned address (0xe00) therefore we opt to have a
 * single definition for the time being for all platforms and rely on the
 * drivers not to access beyond this. If this proves to be a bad idea, then we
 * should concoct per-CPU family specific versions of this. Valid mux entries
 * are intended to be driven by the per-CPU family/socket pin data.
 */
#define	D_FCH_IOMUX_IOMUX	(const smn_reg_def_t) {	\
	.srd_unit = SMN_UNIT_FCH_IOMUX,	\
	.srd_reg = 0x00,		\
	.srd_nents = 0x99		\
}

#define	FCH_IOMUX_IOMUX(r)		\
	fch_iomux_smn_reg(D_FCH_IOMUX_IOMUX, r)
#define	FCH_IOMUX_IOMUX_MMIO(b, r)	\
	fch_iomux_mmio_reg(b, D_FCH_IOMUX_IOMUX, r)

/*
 * The I/O mux uses two bits to select one of up to four alternate functions.
 * These are always in the lowest two bits.
 */
#define	FCH_IOMUX_IOMUX_GET_AF(r)	bitx32(r, 1, 0)
#define	FCH_IOMUX_IOMUX_SET_AF(r, v)	bitset32(r, 1, 0, v)

#endif	/* !_ASM */

#define	FCH_RMTMUX_OFF		0x12c0
#define	FCH_RMTMUX_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_RMTMUX_OFF)
#define	FCH_RMTMUX_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_RMTMUX_OFF)
#define	FCH_RMTMUX_SIZE		0x10


#ifndef	_ASM


MAKE_SMN_FCH_REG_FN(RMTMUX, rmtmux, FCH_RMTMUX_SMN_BASE, FCH_RMTMUX_SIZE, 1);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(RMTMUX, rmtmux, FCH_RMTMUX_OFF,
    FCH_RMTMUX_SIZE);
MAKE_MMIO_FCH_REG_FN(RMTMUX, rmtmux, 1);

/*
 * FCH::RMTGPIO::IOMUX%u -- These are additional IOMUX registers in the remote
 * section. While there are technically 16 entries here, there are only 12 that
 * are known to be usable on most platforms. The register definitions are shared
 * with the normal I/O mux. Remote GPIOs are not supported on all platforms. See
 * <sys/amdzen/fch/gpio.h> for more information.
 *
 * Note the start of this region defined above is 0xc0, thus our register base
 * is 0x00, not 0xc0.
 */
/*CSTYLED*/
#define	D_FCH_RMTMUX_IOMUX	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_RMTMUX,	\
	.srd_reg = 0x00, \
	.srd_size = 1, \
	.srd_nents = 12	\
}
#define	FCH_RMTMUX_IOMUX(i)	fch_rmtmux_smn_reg(D_FCH_RMTMUX_IOMUX, i)
#define	FCH_RMTMUX_IOMUX_MMIO(b, i)	\
    fch_rmtgpio_mmio_reg(b, D_FCH_RMTMUX_IOMUX, i)

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_FCH_IOMUX_H */
