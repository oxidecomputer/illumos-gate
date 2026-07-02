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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _SYS_IO_TURIN_SMU_IMPL_H
#define	_SYS_IO_TURIN_SMU_IMPL_H

/*
 * Turin-specific SMU operation codes and constants. Most BIOS-mailbox opcodes
 * are common across microarchitectures; this header carries only those that
 * are Turin-specific, along with the "tool" mailbox opcodes used to drive the
 * PM log.
 */

#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/io/turin/smu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BIOS-mailbox opcode used to hand the SMU the physical address of the tools
 * (PM log) DRAM buffer.
 */
#define	TURIN_SMU_OP_TOOLS_ADDRESS			0x06

/*
 * Operation codes for the SMU "tool" mailbox (D_TURIN_SMU_TOOL_*).
 */
#define	TURIN_SMU_TOOL_OP_PMLOG_READ_SAMPLE		0x03
#define	TURIN_SMU_TOOL_OP_PMLOG_GET_DRAM_ADDR		0x04
#define	TURIN_SMU_TOOL_OP_PMLOG_GET_TABLE_VERSION	0x05

/*
 * Size of the DRAM buffer for the SMU PM log: four pages. This is the
 * default value of PcdCfgAgmLogDramSize.
 */
#define	TURIN_SMU_PM_TABLE_SIZE				(4 * MMU_PAGESIZE)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_SMU_IMPL_H */
