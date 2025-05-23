/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_PSM_DEFS_H
#define	_SYS_PSM_DEFS_H

/*
 * Platform Specific Module Definitions
 */

#include <sys/pic.h>
#include <sys/xc_levels.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__STDC__
typedef	void *opaque_t;
#else	/* __STDC__ */
typedef	char *opaque_t;
#endif	/* __STDC__ */

/*
 *	External Kernel Interface
 */

extern void picsetup(void);	/* isp initialization */
extern u_longlong_t mul32(uint_t a, uint_t b);
				/* u_long_long = uint_t x uint_t */

/*
 *	External Kernel Reference Data
 */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PSM_DEFS_H */
