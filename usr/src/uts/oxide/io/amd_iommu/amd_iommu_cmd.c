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

/*
 * The maximum number of times a polling loop may spin.
 *
 * This constant is arbitrary; it is intended to be long enough that any actual
 * polling condition will be satisfied before we reach the maximum number of
 * iterations, but not infinite.
 */
static const size_t MAX_SPINS = 65536 * 2;

static bool
amd_iommu_cmd_waiting(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_status_t status;
	status = amd_iommu_read_status(iommu);
	return (status.aimr_status_cmd_wait_int != 1);
}

/*
 * Unconditionally clears IOMMU command completion status by resetting the
 * W1C ComWaitInt bit in the IOMMUMMIO status register.
 */
static void
amd_iommu_cmd_clear_wait(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_status_t status = { 0 };
	status.aimr_status_cmd_wait_int = 1;
	amd_iommu_write_status(iommu, status);
}

/*
 * Waits for the command completion bit to be set in the IOMMU status register.
 * Since this happens while holding the command buffer lock, only a single
 * waiter may poll at one time, and writers will be blocked until the current
 * set of commands in the buffer are complete.
 */
static void
amd_iommu_wait_for_completion(amd_iommu_t *iommu)
{
	ASSERT3P(iommu, !=, NULL);
	ASSERT3P(iommu->ai_cmd_buf, !=, NULL);
	ASSERT(MUTEX_HELD(&iommu->ai_cmd_buf->aicb_lock));
	size_t spins = 0;
	while (amd_iommu_cmd_waiting(iommu)) {
		if (spins++ >= MAX_SPINS) {
			cmn_err(CE_PANIC,
			    "amd_iommu%u: stuck waiting for command completion",
			    iommu->ai_instance);
		}
		amd_iommu_mmio_reg_ctl_t ctl;
		mutex_enter(&iommu->ai_ctl_lock);
		ctl = amd_iommu_read_ctl(iommu);
		ctl.aimr_ctl_cmd_buf_en = 1;
		amd_iommu_write_ctl(iommu, ctl);
		mutex_exit(&iommu->ai_ctl_lock);
		SMT_PAUSE();
	}
}

static amd_iommu_cbe_t
amd_iommu_make_wait_cmd(bool flush, bool intr)
{
	uint64_t op1 = 0;
	op1 = bitset64(op1, 1, 1, intr);
	op1 = bitset64(op1, 2, 2, flush);
	amd_iommu_cbe_t wait_cmd = {
	    .aicbe_op1 = op1,
	    .aicbe_opcode = AICO_COMPLETION_WAIT,
	    .aicbe_op2 = 0,
	};
	return (wait_cmd);
}

static amd_iommu_cbe_t
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

static amd_iommu_cbe_t
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

static amd_iommu_cbe_t
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
	ASSERT(MUTEX_HELD(&cmdbuf->aicb_lock));
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
	ASSERT(MUTEX_HELD(&cmdbuf->aicb_lock));
	ASSERT(amd_iommu_cmd_buf_is_running(cmdbuf));
	ASSERT3U(ncmds, <, AMD_IOMMU_CMD_BUF_NENTS);
	ASSERT3P(cmds, !=, NULL);

	if (ncmds == 0)
		return;

	size_t head, tail, next;
	tail = cmdbuf->aicb_tail;
	for (size_t k = 0; k < ncmds; k++) {
		size_t spins = 0;
		next = (tail + 1) % AMD_IOMMU_CMD_BUF_NENTS;
		do {
			if (spins++ == MAX_SPINS) {
				cmn_err(CE_PANIC,
				    "amd_iommu%u: timeout sending command",
				    cmdbuf->aicb_iommu->ai_instance);
			}
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

static void
amd_iommu_send_cmd_and_wait(amd_iommu_t *iommu, amd_iommu_cbe_t cmd)
{
	amd_iommu_cbe_t wait_cmd = amd_iommu_make_wait_cmd(false, true);
	amd_iommu_cbe_t cmds[2] = { cmd, wait_cmd };

	VERIFY3P(iommu, !=, NULL);
	amd_iommu_cmd_buf_t *cmdbuf = iommu->ai_cmd_buf;
	VERIFY3P(cmdbuf, !=, NULL);
	mutex_enter(&cmdbuf->aicb_lock);
	amd_iommu_cmd_clear_wait(iommu);
	amd_iommu_send_cmds_locked(cmdbuf, cmds, 2);
	amd_iommu_wait_for_completion(iommu);
	mutex_exit(&cmdbuf->aicb_lock);
}

void
amd_iommu_invalidate_intr_tbl(amd_iommu_t *iommu, uint16_t device_id)
{
	amd_iommu_cbe_t cmd = amd_iommu_make_invalidate_intr_tbl_cmd(device_id);
	amd_iommu_send_cmd_and_wait(iommu, cmd);
}

void
amd_iommu_invalidate_intr_tbl_segment(amd_iommu_segment_t *segment,
    uint16_t device_id)
{
	VERIFY3P(segment, !=, NULL);
	for (amd_iommu_t *iommu = list_head(&segment->ais_iommus);
	    iommu != NULL; iommu = list_next(&segment->ais_iommus, iommu))
		amd_iommu_invalidate_intr_tbl(iommu, device_id);
}

void
amd_iommu_invalidate_dte(amd_iommu_t *iommu, uint16_t device_id)
{
	amd_iommu_cbe_t cmd = amd_iommu_make_invalidate_dte_cmd(device_id);
	amd_iommu_send_cmd_and_wait(iommu, cmd);
}

void
amd_iommu_invalidate_dte_segment(amd_iommu_segment_t *segment,
    uint16_t device_id)
{
	VERIFY3P(segment, !=, NULL);
	for (amd_iommu_t *iommu = list_head(&segment->ais_iommus);
	    iommu != NULL; iommu = list_next(&segment->ais_iommus, iommu))
		amd_iommu_invalidate_dte(iommu, device_id);
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
	for (amd_iommu_t *iommu = list_head(&segment->ais_iommus);
	    iommu != NULL; iommu = list_next(&segment->ais_iommus, iommu))
		amd_iommu_invalidate_all(iommu);
}
