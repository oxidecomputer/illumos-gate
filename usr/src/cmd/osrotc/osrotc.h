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

#ifndef _OSROTC_H
#define	_OSROTC_H

#include <openssl/asn1t.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DICE TCBInfo ASN.1 structures, as defined in §6.1.1 TCB Info Evidence
 * Extension of the TCG DICE Attestation Architecture Specification,
 * Version 1.00, Revision 0.23, 3/1/2021.
 */

/*
 * tcg-dice-TcbInfo OBJECT IDENTIFIER ::= {tcg-dice 1}
 */
#define	TCBINFO_OID	"2.23.133.5.4.1"

/*
 * An Operational state may optionally be included as part of the TCBInfo.
 * Note these represent specific bit indices in the ASN.1 BIT STRING, not a
 * bitmask and multiple flags may be set simultaneously.  If the corresponding
 * bit is set, that operational state is active.  If the bit is clear, that
 * operational state is inactive.  If no flags are present, all flags are
 * assumed to be 0.
 *
 * OperationalFlags ::= BIT STRING {
 *     notConfigured (0),
 *     notSecure (1),
 *     recovery (2),
 *     debug (3)
 * }
 *
 * The AMD DPE implementation currently only defines the notSecure and debug
 * flags (where notSecure indicates whether the CPU is fused secure or not, and
 * debug indicates whether the CPU is debug unlocked or not).  See, AMD pub.
 * 68086, Rev. 1.00, March 2025.
 */
typedef enum {
	DICE_TCBINFO_F_NOT_CONFIGURED	= 0,
	DICE_TCBINFO_F_NOT_SECURE	= 1,
	DICE_TCBINFO_F_RECOVERY		= 2,
	DICE_TCBINFO_F_DEBUG		= 3,
} DICE_TCBINFO_OPERATIONAL_FLAGS;

/*
 * Each FWID in the TCBInfo has a corresponding type tag that identifies the
 * component being measured.  AMD DICE layer certificates are identified by a
 * GUID, while DPE layer certificates are identified by a 4-byte tag.
 */
#define	AMD_DICE_LAYER_FWID_TYPE_LEN	16
#define	AMD_DPE_LAYER_FWID_TYPE_LEN	4

/*
 * The AMD DPE alias certificate (subject CN "DPE_ALIAS_<unique device id>")
 * carries the measurement of the platform boot image, aka our Phase 1.
 * See "DPE Certificate Chain", AMD pub. 68086, Rev. 1.00, March 2025.
 */
#define	AMD_DPE_ALIAS_CN_PREFIX		"DPE_ALIAS_"

/*
 * A FWID is computed by the DICE layer for each component being measured.
 *
 * FWID ::= SEQUENCE {
 *     hashAlg    OBJECT IDENTIFIER,
 *     digest     OCTET STRING
 * }
 */
typedef struct dice_fwid_st {
	ASN1_OBJECT		*hashAlg;
	ASN1_OCTET_STRING	*digest;
} DICE_FWID;

/* BEGIN CSTYLED */

DEFINE_STACK_OF(DICE_FWID)

/*
 * The TCG DICE TCBInfo X.509 extension provides attestation evidence about a
 * given DICE layer (as identified the containing certificate's subject).
 * The included measurements describe the firmware and configuration executed
 * in that layer.
 *
 * DiceTcbInfo ::= SEQUENCE {
 *     vendor     [0] IMPLICIT UTF8String	OPTIONAL,
 *     model      [1] IMPLICIT UTF8String	OPTIONAL,
 *     version    [2] IMPLICIT UTF8String	OPTIONAL,
 *     svn        [3] IMPLICIT INTEGER		OPTIONAL,
 *     layer      [4] IMPLICIT INTEGER		OPTIONAL,
 *     index      [5] IMPLICIT INTEGER		OPTIONAL,
 *     fwids      [6] IMPLICIT SEQUENCE OF FWID	OPTIONAL,
 *     flags      [7] IMPLICIT OperationalFlags	OPTIONAL,
 *     vendorInfo [8] IMPLICIT OCTET STRING	OPTIONAL,
 *     type       [9] IMPLICIT OCTET STRING	OPTIONAL
 */
typedef struct dice_tcbinfo_st {
	ASN1_UTF8STRING		*vendor;
	ASN1_UTF8STRING		*model;
	ASN1_UTF8STRING		*version;
	ASN1_INTEGER		*svn;
	ASN1_INTEGER		*layer;
	ASN1_INTEGER		*index;
	STACK_OF(DICE_FWID)	*fwids;
	ASN1_BIT_STRING		*flags;
	ASN1_OCTET_STRING	*vendorInfo;
	ASN1_OCTET_STRING	*type;
} DICE_TCBINFO;

ASN1_SEQUENCE(DICE_FWID) = {
	ASN1_SIMPLE(DICE_FWID, hashAlg, ASN1_OBJECT),
	ASN1_SIMPLE(DICE_FWID, digest, ASN1_OCTET_STRING),
} ASN1_SEQUENCE_END(DICE_FWID)

DECLARE_ASN1_FUNCTIONS(DICE_FWID)

/*
 * Even though the TCBInfo ASN.1 structure is defined with IMPLICIT tags, we've
 * found AMD's DPE implementation actually generates the extension with EXPLICIT
 * tags.
 */

ASN1_SEQUENCE(DICE_TCBINFO) = {
	ASN1_EXP_OPT(DICE_TCBINFO, vendor, ASN1_UTF8STRING, 0),
	ASN1_EXP_OPT(DICE_TCBINFO, model, ASN1_UTF8STRING, 1),
	ASN1_EXP_OPT(DICE_TCBINFO, version, ASN1_UTF8STRING, 2),
	ASN1_EXP_OPT(DICE_TCBINFO, svn, ASN1_INTEGER, 3),
	ASN1_EXP_OPT(DICE_TCBINFO, layer, ASN1_INTEGER, 4),
	ASN1_EXP_OPT(DICE_TCBINFO, index, ASN1_INTEGER, 5),
	ASN1_EXP_SEQUENCE_OF_OPT(DICE_TCBINFO, fwids, DICE_FWID, 6),
	ASN1_EXP_OPT(DICE_TCBINFO, flags, ASN1_BIT_STRING, 7),
	ASN1_EXP_OPT(DICE_TCBINFO, vendorInfo, ASN1_OCTET_STRING, 8),
	ASN1_EXP_OPT(DICE_TCBINFO, type, ASN1_OCTET_STRING, 9),
} ASN1_SEQUENCE_END(DICE_TCBINFO)

DECLARE_ASN1_FUNCTIONS(DICE_TCBINFO)
/* END CSTYLED */

#ifdef __cplusplus
}
#endif

#endif /* _OSROTC_H */
