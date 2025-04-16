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

#define X2_GET_VERSION	X2_IOC(0)
#define X2_REG_READ	X2_IOC(1)
#define X2_REG_WRITE	X2_IOC(2)

/*
 * Used to pass register information between userspace and the kernel.  The
 * address is an offset into the MMIO register space and the value contains
 * the contents of a 64-bit register.
 */
typedef struct x2_reg_op {
	uint32_t xro_address;
	uint64_t xro_value;
} x2_reg_op_t;

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
