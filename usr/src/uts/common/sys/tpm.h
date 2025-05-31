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
 */

#ifndef _SYS_TPM_H
#define	_SYS_TPM_H

#ifdef __cplusplus
extern "C" {
#endif

#define	TPMDEV_VERSION_1_2	1
#define	TPMDEV_VERSION_2_0	2

#define	TPMIOC			('T' << 24)|('P' << 16)|('M' << 8)
#define	TPMIOC_GETVERSION	(TPMIOC|1)
#define	TPMIOC_SETLOCALITY	(TPMIOC|2)
#define	TPMIOC_CANCEL		(TPMIOC|3)
#define	TPMIOC_MAKESTICKY	(TPMIOC|4)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TPM_H */
