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
 * Copyright 2023 Oxide Computer Company
 */

#ifndef _MILAN_APOB_IMPL_H
#define	_MILAN_APOB_IMPL_H

/*
 * Implementation details of the Milan APOB. This is in a header so it can be
 * shared with kmdb. Consumers should only use <milan_apob.h>.
 */

#include <sys/stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the length of the HMAC for a given APOB entry. XXX What is the format
 * of this HMAC.
 */
#define	MILAN_APOB_HMAC_LEN	32

/*
 * AMD defines all of these structures as packed structures. Hence why we note
 * them as packed here.
 */
#pragma pack(1)

/*
 * This is the structure of a single type of APOB entry. It is always followed
 * by its size.
 */
typedef struct milan_apob_entry {
	uint32_t	mae_group;
	uint32_t	mae_type;
	uint32_t	mae_inst;
	/*
	 * Size in bytes oe this structure including the header.
	 */
	uint32_t	mae_size;
	uint8_t		mae_hmac[MILAN_APOB_HMAC_LEN];
	uint8_t		mae_data[];
} milan_apob_entry_t;

/*
 * This structure represents the start of the APOB that we should find in
 * memory.
 */
typedef struct milan_apob_header {
	uint8_t			mah_sig[4];
	uint32_t		mah_vers;
	uint32_t		mah_size;
	uint32_t		mah_off;
} milan_apob_header_t;

#pragma pack()	/* pack(1) */

/*
 * This is the default address of the APOB. See the discussion in
 * eb_create_common_properties() for more information. This is here so it can be
 * shared with kmdb.
 */
#define	MILAN_APOB_ADDR	0x4000000UL

#ifdef __cplusplus
}
#endif

#endif /* _MILAN_APOB_IMPL_H */
