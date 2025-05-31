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
 * Copyright 2023 Jason King
 * Copyright 2025 RackTop Systems, Inc.
 */

#ifndef _TPM20_H
#define	_TPM20_H

#ifdef __cplusplus
extern "C" {
#endif

struct tpm;

/*
 * From PTP 6.5.1.3 Table 17, note that it doesn't explicitly
 * label them, but there are three defined durations, so they're interpreted
 * as short, medium, and long (all in milliseconds).
 */
#define	TPM20_DURATION_SHORT	20
#define	TPM20_DURATION_MEDIUM	750
#define	TPM20_DURATION_LONG	1000

/*
 * PTP 6.5.1.4, Table 18 (all in milliseconds)
 * Unlike TPM1.2, these are fixed values.
 */
#define	TPM20_TIMEOUT_A		750
#define	TPM20_TIMEOUT_B		2000
#define	TPM20_TIMEOUT_C		200
#define	TPM20_TIMEOUT_D		30

/*
 * The TCG PC Client Device Driver Design Principles for TPM 2.0, Section 10
 * states that the Create, CreatePrimary, and CreateLoaded commands should
 * have a 180s timeout. For consistency with the above timeouts, the timeout
 * is defined in milliseconds.
 */
#define	TPM20_TIMEOUT_CREATE	(180 * MILLISEC)

/*
 * Similarly, it says commands not explicitly mentioned in [PTP] should
 * use a 90s timeout. Similar to the Create* timeouts, it's also defined
 * in milliseconds for consistency with the other timeouts.
 */
#define	TPM20_TIMEOUT_DEFAULT	(90 * MILLISEC)

#define	TPM20_TIMEOUT_CANCEL	TPM20_TIMEOUT_B

#define	TPM2_ST_NO_SESSIONS	(uint16_t)(0x8001)

#define	TPM2_RC	uint32_t
#define	TPM2_RC_SUCCESS		(TPM2_RC)(0)

/*
 * The TPM2.0 commands that have explicit timeouts. These might get removed
 * in lieu of a common header file listing all of the commands.
 *
 * Taken from s 6.5.2, Table 12, of
 * "Trusted Platform Module Library Part 2: Structures", Rev 01.59
 */
#define	TPM2_CC	uint32_t

#define	TPM2_CC_Startup			(TPM2_CC)(0x00000144)
#define	TPM2_CC_SelfTest		(TPM2_CC)(0x00000143)
#define	TPM2_CC_GetRandom		(TPM2_CC)(0x0000017b)
#define	TPM2_CC_StirRandom		(TPM2_CC)(0x00000146)
#define	TPM2_CC_HashSequenceStart	(TPM2_CC)(0x00000186)
#define	TPM2_CC_SequenceUpdate		(TPM2_CC)(0x0000015c)
#define	TPM2_CC_SequenceComplete	(TPM2_CC)(0x0000013e)
#define	TPM2_CC_EventSequenceComplete	(TPM2_CC)(0x00000185)
#define	TPM2_CC_VerifySignature		(TPM2_CC)(0x00000177)
#define	TPM2_CC_PCR_Extend		(TPM2_CC)(0x00000182)
#define	TPM2_CC_HierarchyControl	(TPM2_CC)(0x00000121)
#define	TPM2_CC_HierarchyChangeAuth	(TPM2_CC)(0x00000129)
#define	TPM2_CC_GetCapability		(TPM2_CC)(0x0000017a)
#define	TPM2_CC_NV_Read			(TPM2_CC)(0x0000014e)
#define	TPM2_CC_Create			(TPM2_CC)(0x00000153)
#define	TPM2_CC_CreatePrimary		(TPM2_CC)(0x00000131)
#define	TPM2_CC_CreateLoaded		(TPM2_CC)(0x00000191)

#ifdef __cplusplus
}
#endif

#endif /* _TPM20_H */
