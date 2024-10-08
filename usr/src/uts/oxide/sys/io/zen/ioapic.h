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

#ifndef _SYS_IO_ZEN_IOAPIC_H
#define	_SYS_IO_ZEN_IOAPIC_H

/*
 * Common definitons for working with the IOAPIC on zen platforms.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zen_ioapic_info {
	uint8_t zii_group;
	uint8_t zii_swiz;
	uint8_t zii_map;
} zen_ioapic_info_t;

#define	IOAPIC_ROUTE_INTX_SWIZZLE_ABCD	0
#define	IOAPIC_ROUTE_INTX_SWIZZLE_BCDA	1
#define	IOAPIC_ROUTE_INTX_SWIZZLE_CDAB	2
#define	IOAPIC_ROUTE_INTX_SWIZZLE_DABC	3

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_IOAPIC_H */
