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

#ifndef _T6INIT_H
#define	_T6INIT_H

/*
 * Internal, common definitions for t6init.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	EXIT_USAGE	2

#define	T6_PRODUCT_STR			"T62100-KR"
#define	T6_MAC_COUNT			2
#define	T6_MAC_STRIDE			8
#define	T6_PCI_SUBSYSTEM_VENDORID	0x1de

#define	T6_MFG_DRIVER			"t6mfg"
#define	T6_MISSION_DRIVER		"t4nex"
#define	T6_PCIEB_DRIVER			"pcieb"
#define	T6_PCIEB_MINOR			"devctl"

#ifdef __cplusplus
}
#endif

#endif /* _T6INIT_H */
