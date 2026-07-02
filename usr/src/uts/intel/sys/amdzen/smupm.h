/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _SYS_AMDZEN_SMUPM_H
#define	_SYS_AMDZEN_SMUPM_H

#include <sys/amdzen/smn.h>

/*
 * This header covers the SMN registers and message numbers of the SMU "tool"
 * mailbox, which drives the PM log. The tool mailbox is distinct from both the
 * BIOS mailbox and the HSMP, with its own message/response/argument registers
 * and its own opcode space.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tool mailbox response codes.
 */
#define	SMUPM_RESPONSE_INCOMPLETE	0x00
#define	SMUPM_RESPONSE_OK		0x01

/*
 * PM log message numbers, Milan opcode space.
 */
#define	SMUPM_MILAN_OP_READ_SAMPLE		0x05
#define	SMUPM_MILAN_OP_GET_DRAM_ADDR		0x06
#define	SMUPM_MILAN_OP_GET_TABLE_VERSION	0x08

/*
 * PM log message numbers, Turin opcode space.
 */
#define	SMUPM_TURIN_OP_READ_SAMPLE		0x03
#define	SMUPM_TURIN_OP_GET_DRAM_ADDR		0x04
#define	SMUPM_TURIN_OP_GET_TABLE_VERSION	0x05

/*
 * The size of the DRAM region in which the SMU places the PM table. The tool
 * mailbox has no operation for discovering this. The AGESA default is four
 * pages (the PcdCfgAgmLogDramSize PCD). Every known table layout fits within
 * this.
 */
#define	SMUPM_TABLE_SIZE	(16 * 1024)

/*
 * Tool mailbox register block.
 */
#define	SMN_SMUPM_APERTURE_MASK	0xfffffffffffff000
AMDZEN_MAKE_SMN_REG_FN(amdzen_smn_smupm_reg, SMU_RPC, 0x3b10000,
    SMN_SMUPM_APERTURE_MASK, 1, 0);

/*
 * Tool mailbox message register.
 */
/*CSTYLED*/
#define	D_SMN_SMUPM_MSG_MILAN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x524,		\
}
/*CSTYLED*/
#define	D_SMN_SMUPM_MSG_TURIN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x924,		\
}

/*
 * Tool mailbox response register.
 */
/*CSTYLED*/
#define	D_SMN_SMUPM_RESP_MILAN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x570,		\
}
/*CSTYLED*/
#define	D_SMN_SMUPM_RESP_TURIN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x970,		\
}

/*
 * Tool mailbox arguments, common to both generations.
 */
#define	SMUPM_NARGS	6
/*CSTYLED*/
#define	D_SMN_SMUPM_ARG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0xa40,		\
	.srd_stride = 0x4,		\
	.srd_nents = SMUPM_NARGS	\
}
#define	SMN_SMUPM_ARG(n)	\
    amdzen_smn_smupm_reg(0, D_SMN_SMUPM_ARG, n)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_SMUPM_H */
