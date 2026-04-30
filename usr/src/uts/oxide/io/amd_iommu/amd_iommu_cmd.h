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

/* Copyright 2026 Oxide Computer Company */

/*
 * Defines internal types, function prototypes, and so on, for working with the
 * AMD IOMMU's Command Buffer: this is a memory ring shared between the host
 * and IOMMU that allows the host to send commands to the device to flush state,
 * invalidate entries, and so on.  See AMD document 48882 for details.
 */

#ifndef _SYS_AMD_IOMMU_CMD_H
#define	_SYS_AMD_IOMMU_CMD_H

#include <sys/stddef.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/amd_iommu.h>

#include "amd_iommu_impl.h"

extern void amd_iommu_send_cmd_and_wait(amd_iommu_t *, amd_iommu_cbe_t);
extern void amd_iommu_send_cmd(amd_iommu_t *, amd_iommu_cbe_t);

extern void amd_iommu_invalidate_intr_tbl(amd_iommu_t *, uint16_t);
extern void amd_iommu_invalidate_all(amd_iommu_t *);
extern void amd_iommu_invalidate_all_segment(amd_iommu_segment_t *);

#endif	/* !_SYS_AMD_IOMMU_CMD_H */
