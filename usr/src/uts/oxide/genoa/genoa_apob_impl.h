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

#ifndef _GENOA_APOB_IMPL_H
#define	_GENOA_APOB_IMPL_H

/*
 * This header defines implementation details of the Genoa APOB for
 * sharing with kmdb.  Consumers should only use <genoa_apob.h>.
 */

#include <sys/stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the length of the HMAC for a given APOB entry.
 * XXX What is the format of this HMAC.
 */
#define	GENOA_APOB_HMAC_LEN	32

/*
 * AMD defines all of these structures as packed structures. Hence using
 * the `pack(1)` pragma here.
 */
#pragma pack(1)

/*
 * This is the structure of a single type of APOB entry. It is always followed
 * by its size.
 */
typedef struct genoa_apob_entry {
	uint32_t	gae_group;
	uint32_t	mae_type;
	uint32_t	mae_inst;
	/*
	 * Size in bytes oe this structure including the header.
	 */
	uint32_t	mae_size;
	uint8_t		mae_hmac[GENOA_APOB_HMAC_LEN];
	uint8_t		mae_data[];
} genoa_apob_entry_t;

/*
 * This structure represents the start of the APOB.
 */
typedef struct genoa_apob_header {
	uint8_t			mah_sig[4];
	uint32_t		mah_vers;
	uint32_t		mah_size;
	uint32_t		mah_off;
} genoa_apob_header_t;

#pragma pack()	/* pack(1) */

/*
 * This is the default address of the APOB; see the discussion in
 * eb_create_common_properties() for more information.  We define
 * it here for sharing with kmdb.
 */
#define	GENOA_APOB_ADDR	0x4000000UL

#ifdef __cplusplus
}
#endif

#endif /* _GENOA_APOB_IMPL_H */
