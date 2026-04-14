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

#ifndef _OS_ROT_H
#define	_OS_ROT_H

/*
 * Oxide OS RoT Driver Interface
 *
 * This driver makes use of the DICE Protection Environment (DPE) present on
 * the AMD CPU to measure and attest to the phase 1 and 2 images.
 */

#include <sys/stdint.h>
#include <sys/debug.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The path to the os_rot device to issue ioctls.
 */
#define	OS_ROT_DEV		"/dev/os_rot"

/*
 * os_rot IOCTLs.
 */
#define	OS_ROT_IOC		(('R' << 24) | ('O' << 16) | ('T' << 8))

/*
 * DPE measurement type tags are 4 bytes.
 */
#define	OS_ROT_TYPE_SIZE	4

/*
 * DPE measurement type tag for the phase 2 boot image.
 */
#define	OS_ROT_TYPE_OXP2	\
	((const uint8_t[OS_ROT_TYPE_SIZE]) {'O', 'X', 'P', '2'})

/*
 * For dev scenarios, we may not have a full phase 2 image and instead just
 * an early-boot ramdisk as the root filesystem. We use a slightly different
 * tag to distinguish the measurement in that case.
 */
#define	OS_ROT_TYPE_OXRD	\
	((const uint8_t[OS_ROT_TYPE_SIZE]) {'O', 'X', 'R', 'D'})

CTASSERT(sizeof (OS_ROT_TYPE_OXP2) == OS_ROT_TYPE_SIZE);
CTASSERT(sizeof (OS_ROT_TYPE_OXRD) == OS_ROT_TYPE_SIZE);

/*
 * Measurements are assumed to be SHA2-384 digests.
 */
#define	OS_ROT_HASH_SIZE	48

/*
 * Attestations are assumed to be 384-bit ECDSA signatures.
 */
#define	OS_ROT_SIG_SIZE		96

/*
 * The set of errors returned to os_rot ioctl consumers.
 */
typedef enum {
	OS_ROT_E_OK = 0,
	/*
	 * Not enough space to write result, the necessary size is returned
	 * (e.g., as `osrc_chain_size` in `os_rot_certs_t`).
	 */
	OS_ROT_E_SIZE,
	/*
	 * Encountered an unexpected DPE error.
	 */
	OS_ROT_E_DPE,
	/*
	 * No DPE provider backing the driver is available (yet).  The DPE
	 * provider driver attaches asynchronously relative to os_rot; the
	 * caller may retry later.
	 */
	OS_ROT_E_NO_PROVIDER,
} os_rot_error_t;

static inline const char *
os_rot_strerror(os_rot_error_t error)
{
	switch (error) {
	case OS_ROT_E_OK:
		return ("no error");
	case OS_ROT_E_SIZE:
		return ("bad buffer size");
	case OS_ROT_E_DPE:
		return ("DPE operation failed");
	case OS_ROT_E_NO_PROVIDER:
		return ("no DPE provider is available (yet)");
	default:
		return ("unknown error");
	}
}

/*
 * Retrieve the certificate chain that links our attestation signing keys to
 * a trusted PKI root (os_rot_certs_t).
 */
#define	OS_ROT_IOC_GET_CERTS	(OS_ROT_IOC | 0x01)

/*
 * When `osrc_chain_size` is 0 on input, the driver returns the required size.
 * Otherwise it fills `osrc_chain` with the certificate chain data.
 */
typedef struct os_rot_certs {
	os_rot_error_t	osrc_error;
	uint32_t	osrc_chain_size;
	uint8_t		osrc_chain[];
} os_rot_certs_t;

/*
 * Provides an attestation over the current set of measurements with a given
 * nonce for freshness and returns the resulting signature (os_rot_attest_t).
 *
 * The attestation is a signature over the SHA2-384 digest of the
 * caller-provided nonce (which should be random).
 */
#define	OS_ROT_IOC_ATTEST	(OS_ROT_IOC | 0x02)

/*
 * The caller provides `osra_nonce` which the driver uses to provide an
 * attestation signature.
 */
typedef struct os_rot_attest {
	os_rot_error_t	osra_error;
	uint8_t		osra_nonce[OS_ROT_HASH_SIZE];
	uint8_t		osra_sig[OS_ROT_SIG_SIZE];
} os_rot_attest_t;

#ifdef __cplusplus
}
#endif

#endif /* _OS_ROT_H */
