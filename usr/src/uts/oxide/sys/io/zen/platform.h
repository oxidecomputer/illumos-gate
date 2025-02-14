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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef	_SYS_IO_ZEN_PLATFORM_H
#define	_SYS_IO_ZEN_PLATFORM_H

/*
 * Provides forward declarations of known microarchitecture-specific platforms
 * for use in the platform detection code.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_platform zen_platform_t;
typedef struct zen_iodie zen_iodie_t;
typedef struct zen_pcie_core zen_pcie_core_t;
typedef struct zen_pcie_port zen_pcie_port_t;
typedef struct zen_ioms zen_ioms_t;
typedef struct zen_nbif zen_nbif_t;

extern const zen_platform_t milan_platform;
extern const zen_platform_t genoa_platform;
extern const zen_platform_t turin_platform;
extern const zen_platform_t dense_turin_platform;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PLATFORM_H */
