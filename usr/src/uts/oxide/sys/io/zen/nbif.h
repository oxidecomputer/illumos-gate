/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2024 Oxide Computer Company
 */

#ifndef _SYS_IO_ZEN_NBIF_H
#define	_SYS_IO_ZEN_NBIF_H

/*
 * Common definitions for working with North Bridge Interfaces (nBIFs) on
 * zen platforms.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Some registers such as NBIFMM::INTR_LINE_ENABLE are arranged with one byte
 * per device and with each bit corresponding to an endpoint function. The
 * following macro sets the correct bit corresponding to a device and function.
 */
#define	NBIF_INTR_LINE_EN_SET_I(reg, dev, func, val)	\
    bitset32(reg, ((dev) * 8) + (func), ((dev) * 8) + (func), val)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_NBIF_H */
