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

#ifndef _FCH_PROPS_H
#define	_FCH_PROPS_H

/*
 * This header is private to fch(4D) and its children. It contains common
 * properties that they both share.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	FCH_PROPNAME_FABRIC_ROLE	"fabric-role"
#define	FCH_FABRIC_ROLE_PRI		"primary"
#define	FCH_FABRIC_ROLE_SEC		"secondary"

#ifdef __cplusplus
}
#endif

#endif /* _FCH_PROPS_H */
