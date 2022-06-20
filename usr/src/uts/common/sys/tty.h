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
 * Copyright (c) 1987-1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_TTY_H
#define	_SYS_TTY_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"	/* SunOS 4.0 2.13 */

#include <sys/stream.h>
#include <sys/termios.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct tty_common {
	int	t_flags;
	queue_t	*t_readq;	/* stream's read queue */
	queue_t	*t_writeq;	/* stream's write queue */
	tcflag_t t_iflag;	/* copy of iflag from tty modes */
	tcflag_t t_cflag;	/* copy of cflag from tty modes */
	uchar_t	t_stopc;	/* copy of c_cc[VSTOP] from tty modes */
	uchar_t	t_startc;	/* copy of c_cc[VSTART] from tty modes */
	struct winsize t_size;	/* screen/page size */
	mblk_t	*t_iocpending;	/* ioctl reply pending successful allocation */
	kmutex_t t_excl;	/* keeps struct consistent */
} tty_common_t;

#define	TS_XCLUDE	0x00000001	/* tty is open for exclusive use */
#define	TS_SOFTCAR	0x00000002	/* force carrier on */

#ifdef	_KERNEL
extern void	ttycommon_init(tty_common_t *);
extern void	ttycommon_close(tty_common_t *);
extern speed_t	ttycommon_ospeed(tty_common_t *);
extern uint_t	ttycommon_char_size(tty_common_t *);
extern void	ttycommon_qfull(tty_common_t *, queue_t *);
extern size_t	ttycommon_ioctl(tty_common_t *, queue_t *, mblk_t *, int *);
extern mblk_t	*ttycommon_iocpending_take(tty_common_t *);
extern void	ttycommon_iocpending_discard(tty_common_t *);
extern void	ttycommon_iocpending_set(tty_common_t *, mblk_t *);
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TTY_H */
