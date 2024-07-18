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

#ifndef	_SYS_IO_ZEN_FABRIC_H
#define	_SYS_IO_ZEN_FABRIC_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_fabric_ops {
	void		*zfo_foo;
} zen_fabric_ops_t;

typedef struct zen_fabric {
	uint8_t		zf_nsocs;
} zen_fabric_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_H */
