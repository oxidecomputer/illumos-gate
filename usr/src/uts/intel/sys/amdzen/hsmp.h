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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _SYS_AMDZEN_HSMP_H
#define	_SYS_AMDZEN_HSMP_H

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>
#include <sys/x86_archext.h>

/*
 * This header covers the SMN Mailbox Registers and associated data for the
 * HSMP (Host System Management Port).
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HSMP commands.
 */
#define	HSMP_CMD_TESTMESSAGE		0x1
#define	HSMP_CMD_GETIFVERSION		0x3

/*
 * Documented HSMP response codes.
 */
#define	HSMP_RESPONSE_INCOMPLETE	0x0
#define	HSMP_RESPONSE_OK		0x1
#define	HSMP_RESPONSE_REJECTED_BUSY	0xfc
#define	HSMP_RESPONSE_REJECTED_PREREQ	0xfd
#define	HSMP_RESPONSE_INVALID_MSGID	0xfe
#define	HSMP_RESPONSE_INVALID_ARGS	0xff

/*
 * Supported number of functions for each interface version.
 */
#define	HSMP_IFVER1_FUNCS		0x11
#define	HSMP_IFVER2_FUNCS		0x12
#define	HSMP_IFVER3_FUNCS		0x14
#define	HSMP_IFVER4_FUNCS		0x15
#define	HSMP_IFVER5_FUNCS		0x2f
#define	HSMP_IFVER7_FUNCS		0x3f

/*
 * HSMP register block.
 */
#define	SMN_HSMP_APERTURE_MASK	0xffffffffffffff00
AMDZEN_MAKE_SMN_REG_FN(amdzen_smn_hsmp_reg, HSMP, 0x3b10900,
    SMN_HSMP_APERTURE_MASK, 1, 0);

/*
 * HSMP Message ID.
 * The address of the message ID register changed in Turin to something in the
 * same range as the others.
 */
/*CSTYLED*/
#define	D_SMN_HSMP_MSGID	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_HSMP,	\
	.srd_reg = 0x34,		\
}
#define	HSMP_LEGACY_ID_REG	SMN_MAKE_REG(0x3b10534, SMN_UNIT_HSMP)
static inline smn_reg_t
SMN_HSMP_MSGID(x86_processor_family_t fam)
{
	switch (fam) {
	case X86_PF_AMD_MILAN:
	case X86_PF_AMD_GENOA:
	case X86_PF_AMD_VERMEER:
	case X86_PF_AMD_REMBRANDT:
	case X86_PF_AMD_CEZANNE:
	case X86_PF_AMD_RAPHAEL:
	case X86_PF_AMD_PHOENIX:
	case X86_PF_AMD_BERGAMO:
		return (HSMP_LEGACY_ID_REG);
	default:
		break;
	}
	return (amdzen_smn_hsmp_reg(0, D_SMN_HSMP_MSGID, 0));
}

/*
 * HSMP Response Status.
 */
/*CSTYLED*/
#define	D_SMN_HSMP_RESP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_HSMP,	\
	.srd_reg = 0x80,		\
}
#define	SMN_HSMP_RESP	\
    amdzen_smn_hsmp_reg(0, D_SMN_HSMP_RESP, 0)

/*
 * HSMP Arguments.
 */
/*CSTYLED*/
#define	D_SMN_HSMP_ARG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_HSMP,	\
	.srd_reg = 0xe0,		\
	.srd_stride = 0x4,		\
	.srd_nents = 8			\
}
#define	SMN_HSMP_ARG(n)	\
    amdzen_smn_hsmp_reg(0, D_SMN_HSMP_ARG, n)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_HSMP_H */
