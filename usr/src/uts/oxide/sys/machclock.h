/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef	_SYS_MACHCLOCK_H
#define	_SYS_MACHCLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stubs relied upon by other parts of the system; we don't support a
 * time-of-day clock on this architecture.
 */

struct tod_ops;
typedef struct tod_ops tod_ops_t;

extern tod_ops_t	*tod_ops;

#define	TODOP_GET(_)		tod_get()
#define	TODOP_SET(_, _ts)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHCLOCK_H */
