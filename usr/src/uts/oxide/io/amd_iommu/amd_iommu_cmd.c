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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2026 Oxide Computer Company
 */

#include <sys/stdint.h>
#include <sys/sunddi.h>
#include <sys/amd_iommu.h>

#include "amd_iommu_impl.h"
#include "amd_iommu_cmd.h"

static bool
amd_iommu_cmd_waiting(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_status_t status;
	status = amd_iommu_read_status(iommu);
	return (status.aimr_status_cmd_wait_int != 1);
}

/*
 * Unconditionally clears IOMMU command completion status by resetting the
 * W1C CmdWaitInt bit in the IOMMUMMIO status register.
 */
static void
amd_iommu_cmd_clear_wait(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_status_t status = { 0 };
	status.aimr_status_cmd_wait_int = 1;
	amd_iommu_write_status(iommu, status);
}

static void
amd_iommu_wait_for_completion(amd_iommu_t *iommu)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(iommu->ai_cmd_buf, !=, NULL);
	ASSERT(MUTEX_HELD(&iommu->ai_cmd_buf->aicb_cmd_lock));
	while (amd_iommu_cmd_waiting(iommu)) {
		amd_iommu_mmio_reg_ctl_t ctl;
		ctl = amd_iommu_read_ctl(iommu);
		ctl.aimr_ctl_cmd_buf_en = 1;
		amd_iommu_write_ctl(iommu, ctl);
	}
}

amd_iommu_cbe_t
amd_iommu_make_wait_cmd(bool f, bool i, bool s)
{
	uint64_t op1 = 0;
	op1 = bitset64(op1, 0, 0, s);
	op1 = bitset64(op1, 1, 1, i);
	op1 = bitset64(op1, 2, 2, f);
	amd_iommu_cbe_t wait_cmd = {
	    .aicbe_op1 = op1,
	    .aicbe_opcode = AICO_COMPLETION_WAIT,
	    .aicbe_op2 = 0,
	};
	return (wait_cmd);
}

amd_iommu_cbe_t
amd_iommu_make_invalidate_dte_cmd(uint16_t device_id)
{
	uint64_t op1 = 0;
	op1 = bitset64(op1, 15, 0, device_id);
	amd_iommu_cbe_t invdte = {
	    .aicbe_op1 = op1,
	    .aicbe_opcode = AICO_INVALIDATE_DTE,
	    .aicbe_op2 = 0,
	};
	return (invdte);
}

amd_iommu_cbe_t
amd_iommu_make_invalidate_intr_tbl_cmd(uint16_t device_id)
{
	uint64_t op1 = 0;
	op1 = bitset64(op1, 15, 0, device_id);
	amd_iommu_cbe_t invit = {
	    .aicbe_op1 = op1,
	    .aicbe_opcode = AICO_INVALIDATE_INTR_TBL,
	    .aicbe_op2 = 0,
	};
	return (invit);
}

amd_iommu_cbe_t
amd_iommu_make_invalidate_all_cmd(void)
{
	amd_iommu_cbe_t invall = {
	    .aicbe_op1 = 0,
	    .aicbe_opcode = AICO_INVALIDATE_ALL,
	    .aicbe_op2 = 0,
	};
	return (invall);
}

static bool
amd_iommu_cmd_buf_is_running(amd_iommu_cmd_buf_t *cmdbuf)
{
	ASSERT3P(cmdbuf, !=, NULL);
	ASSERT(MUTEX_HELD(&cmdbuf->aicb_cmd_lock));
	amd_iommu_t *iommu = cmdbuf->aicb_iommu;
	ASSERT3P(iommu, !=, NULL);
	amd_iommu_mmio_reg_status_t status;
	status = amd_iommu_read_status(iommu);
	return (status.aimr_status_cmd_buf_run == 1);
}

static void
amd_iommu_send_cmds_locked(amd_iommu_cmd_buf_t *cmdbuf,
    amd_iommu_cbe_t *cmds, size_t ncmds)
{
	ASSERT3P(cmdbuf, !=, NULL);
	ASSERT(MUTEX_HELD(&cmdbuf->aicb_cmd_lock));
	ASSERT(amd_iommu_cmd_buf_is_running(cmdbuf));
	ASSERT3U(ncmds, <, AMD_IOMMU_CMD_BUF_NENTS);
	ASSERT3P(cmds, !=, NULL);

	if (ncmds == 0)
		return;

	size_t head, tail, next;
	tail = cmdbuf->aicb_tail;
	for (size_t k = 0; k < ncmds; k++) {
		next = (tail + 1) % AMD_IOMMU_CMD_BUF_NENTS;
		do {
			head = amd_iommu_read_cmd_buf_head(cmdbuf);
			SMT_PAUSE();
		} while (head == next);
		cmdbuf->aicb_cmd_buf[tail] = cmds[k];
		cmdbuf->aicb_tail = next;
		tail = next;
	}
	membar_producer();
	amd_iommu_write_cmd_buf_tail(cmdbuf, next);
}

void
amd_iommu_send_cmd(amd_iommu_t *iommu, amd_iommu_cbe_t cmd)
{
	VERIFY3P(iommu, !=, NULL);
	amd_iommu_cmd_buf_t *cmdbuf = iommu->ai_cmd_buf;
	VERIFY3P(cmdbuf, !=, NULL);
	mutex_enter(&cmdbuf->aicb_cmd_lock);
	amd_iommu_send_cmds_locked(cmdbuf, &cmd, 1);
	mutex_exit(&cmdbuf->aicb_cmd_lock);
}

void
amd_iommu_send_cmd_and_wait(amd_iommu_t *iommu, amd_iommu_cbe_t cmd)
{
	amd_iommu_cbe_t wait_cmd = amd_iommu_make_wait_cmd(false, true, false);
	amd_iommu_cbe_t cmds[2] = { cmd, wait_cmd };

	VERIFY3P(iommu, !=, NULL);
	amd_iommu_cmd_buf_t *cmdbuf = iommu->ai_cmd_buf;
	VERIFY3P(cmdbuf, !=, NULL);
	mutex_enter(&cmdbuf->aicb_cmd_lock);
	amd_iommu_cmd_clear_wait(iommu);
	amd_iommu_send_cmds_locked(cmdbuf, cmds, 2);
	amd_iommu_wait_for_completion(iommu);
	mutex_exit(&cmdbuf->aicb_cmd_lock);
}

void
amd_iommu_invalidate_intr_tbl(amd_iommu_t *iommu, uint16_t device_id)
{
	amd_iommu_cbe_t cmd = amd_iommu_make_invalidate_intr_tbl_cmd(device_id);
	amd_iommu_send_cmd_and_wait(iommu, cmd);
}

void
amd_iommu_invalidate_all(amd_iommu_t *iommu)
{
	amd_iommu_cbe_t cmd = amd_iommu_make_invalidate_all_cmd();
	amd_iommu_send_cmd_and_wait(iommu, cmd);
}

void
amd_iommu_invalidate_all_segment(amd_iommu_segment_t *segment)
{
	VERIFY3P(segment, !=, NULL);
	for (amd_iommu_t *iommu = segment->ais_iommus;
	    iommu != NULL;
	    iommu = iommu->ai_next)
		amd_iommu_invalidate_all(iommu);
}
