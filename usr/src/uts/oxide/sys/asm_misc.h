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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2019 Joyent, Inc.
 */

#ifndef _SYS_ASM_MISC_H
#define	_SYS_ASM_MISC_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	RET_INSTR	0xc3
#define	NOP_INSTR	0x90
#define	STI_INSTR	0xfb
#define	JMP_INSTR	0x00eb

#ifdef _ASM	/* The remainder of this file is only for assembly files */

/* Load reg with pointer to per-CPU structure */
#define	LOADCPU(reg)			\
	movq	%gs:CPU_SELF, reg;

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ASM_MISC_H */
