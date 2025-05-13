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

#ifndef _SYS_PLAT_AMDZEN_H
#define	_SYS_PLAT_AMDZEN_H

/*
 * Platform-specific amdzen support
 *
 * This file forms the platform-specific interfaces that a given platform must
 * implement to support the more generic 'amdzen' driver.
 *
 * These interfaces are all expected to be implemented by a platform's
 * 'amdzen_plat' module. This is left as a module and not a part of say, unix,
 * so that it can in turn depend on other modules that a platform might
 * require.
 *
 * In general, unless otherwise indicated, these interfaces will always be
 * called from kernel context. The interfaces will only be called from a single
 * thread at this time and any locking is managed at a layer outside of the
 * amdzen_plat interfaces. If the subsystem is using some other interfaces that
 * may be used by multiple consumers and needs locking, then that still must be
 * considered in the design and implementation.
 */

#include <sys/types.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read from, or write to, an SMN register. For registers which should be
 * handled by common code these routines will return ESRCH.
 */
extern int amdzen_plat_smn_read(uint8_t, const smn_reg_t, uint32_t *);
extern int amdzen_plat_smn_write(uint8_t, const smn_reg_t, uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_PLAT_AMDZEN_H */
