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

#ifndef _LIBHSMP_PRIV_H
#define	_LIBHSMP_PRIV_H

/*
 * Private interfaces and structures for libhsmp.
 */

#include <stdint.h>
#include <libhsmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The name of the driver through which the HSMP is accessed.
 */
#define	LIBHSMP_DRV	"uhsmp"

typedef struct libhsmp_tgt {
	int lht_fd;
	uint32_t lht_ifver;
	uint32_t lht_sock;
	uint32_t lht_iod;
} libhsmp_tgt_t;

struct libhsmp_handle {
	uint32_t lhh_ntargets;
	libhsmp_tgt_t *lhh_tgts;
	/* Last error information */
	libhsmp_err_t lhh_err;
	int32_t lhh_syserr;
	char lhh_errmsg[LIBHSMP_ERR_LEN];
};

/*
 * An HSMP function known to the library, along with the minimum HSMP
 * interface version in which it appears.
 */
typedef struct libhsmp_fn {
	uint32_t lf_id;
	uint32_t lf_minver;
	const char *lf_name;
} libhsmp_fn_t;

#ifdef __cplusplus
}
#endif

#endif /* _LIBHSMP_PRIV_H */
