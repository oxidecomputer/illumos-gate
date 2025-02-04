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

#ifndef _SYS_APOB_IMPL_H
#define	_SYS_APOB_IMPL_H

/*
 * Implementation details of the generic APOB. This is in a header so it can be
 * shared with mdb. Consumers should only use <sys/apob.h> (kernel consumers may
 * also use <sys/kapob.h> on platforms that have it).  So far as we can tell,
 * all implementations of the APOB are structured as described here, though the
 * size of a given entry and the interpretation of ae_data is specific to a
 * processor family and firmware version.  Similarly, the group numbers appear
 * to be shared among all processor families, but the presence, absence, number
 * of instances, size, and interpretation of an entry for a particular group may
 * not be.
 */

#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/apob.h>

#ifdef	_KERNEL
#include <sys/varargs.h>
#else
#include <stdarg.h>
#endif	/* _KERNEL */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AMD defines all of these structures as packed structures. Hence why we note
 * them as packed here.
 */
#pragma pack(1)

/*
 * This is the structure of a single type of APOB entry. It is always followed
 * by its size worth of additional data.
 */
typedef struct apob_entry {
	uint32_t	ae_group;
	uint32_t	ae_type;
	uint32_t	ae_inst;
	/*
	 * Size in bytes of this structure including the header.
	 */
	uint32_t	ae_size;
	uint8_t		ae_hmac[APOB_HMAC_LEN];
	uint8_t		ae_data[];
} apob_entry_t;

/*
 * This structure represents the start of the APOB that we should find in
 * memory.
 */
typedef struct apob_header {
	uint8_t			ah_sig[4];
	uint32_t		ah_vers;
	uint32_t		ah_size;
	uint32_t		ah_off;		/* Offset of first entry */
} apob_header_t;

#pragma pack()	/* pack(1) */

#define	APOB_HDL_ERRMSGLEN	256

/*
 * Kernel-only implementation of vprintf that we use instead of vsnprintf before
 * genunix is available to us.  Instead of filling in a_errmsg, it spews to the
 * earlyboot console.  This is kind of gross and we probably ought to be
 * questioning why we go to such lengths to avoid including the basic string
 * functions in unix.
 */
#ifdef	_KERNEL
extern void kapob_eb_vprintf(const char *, va_list);
#endif	/* _KERNEL */

/*
 * Lockless data structure; the pointer and size are constant (except in the
 * kernel where we replace the pointer while still effectively single-threaded)
 * but the caller is responsible for guaranteeing mutual exclusion if it wants
 * the error state.  Note that the error state is optional; it's possible to
 * determine conclusively whether any APOB function succeeded without using it,
 * but it does provide additional detail that can be logged or returned to a
 * caller farther up the stack.
 */
struct apob_hdl {
	const apob_header_t	*ah_header;
	size_t			ah_len;
	int			ah_err;
	char			ah_errmsg[APOB_HDL_ERRMSGLEN];
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_APOB_IMPL_H */
