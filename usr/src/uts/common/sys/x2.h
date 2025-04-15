/*
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 */

/*
 * Copyright 2023 Oxide Computer Company
 */

#ifndef	_SYS_X2_H
#define	_SYS_X2_H

#include <sys/ccompile.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	X2_IOC_PREFIX	0x1e6c
#define	X2_IOC(x) ((X2_IOC_PREFIX << 16) | x)

/*
 * Used to communicate the x2 driver version number to the userspace daemon.
 */
typedef struct {
	uint32_t x2_major;
	uint32_t x2_minor;
	uint32_t x2_patch;
} x2_version_t;

#ifdef _KERNEL

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_X2_H */
