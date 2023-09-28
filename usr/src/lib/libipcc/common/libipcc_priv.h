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

#ifndef _LIBIPCC_PRIV_H
#define	_LIBIPCC_PRIV_H

#include <stdbool.h>
#include <sys/time.h>
#include <libipcc.h>

#define	SUPPORTED_IPCC_VERSION	1

/* All supported key lookup/set flags */
#define	LIBIPCC_KEYF_ALL	LIBIPCC_KEYF_COMPRESSED
#define	LIBIPCC_INVF_ALL	LIBIPCC_INV_INIT_CACHE

struct libipcc_handle {
	int			lih_fd;
	uint_t			lih_version;
	/* Last error information */
	libipcc_err_t		lih_err;
	int32_t			lih_syserr;
	char			lih_errmsg[LIBIPCC_ERR_LEN];
};

typedef struct libipcc_invcache {
	bool			lii_valid;
	int32_t			lii_errno;
	ipcc_inventory_t	lii_inv;
} libipcc_invcache_t;

struct libipcc_inv_handle {
	uint32_t		liih_vers;	/* Inventory version */
	uint32_t		liih_ninv;	/* Inventory count */
	libipcc_invcache_t	*liih_inv;	/* Inventory data */
};

/*
 * Definitions for the packed nvlist inventory data cache file.
 *
 * Note that we currently rely on LIBIPCC_INV_CACHEDIR being both owned by root
 * AND on tmpfs to ensure that effectively only root can create the cache
 * file. This takes advantage of the fact that the cache file is created via
 * librename which uses openat() to create the file, and a property of the
 * privileges check that tmpfs performs in this case.
 */
#define	LIBIPCC_INV_CACHEDIR	"/var/run"		/* See above comment */
#define	LIBIPCC_INV_CACHENAME	"libipcc_inventory.nvlist"
#define	LIBIPCC_INV_CHUNK	(128 * 1024)
#define	LIBIPCC_INV_NVL_NENTS	"inventory-entries"	/* uint32 */
#define	LIBIPCC_INV_NVL_VERS	"version"		/* uint32 */
#define	LIBIPCC_INV_NVL_HRTIME	"generated-hrtime"	/* int64 */

/*
 * Cache expiration time, fixed and random components. All times are in seconds.
 * The four hour base is pretty arbitrary.
 */
#define	LIBIPCC_INV_TIME_BASE		(NANOSEC * 60ULL * 60ULL * 4ULL)
#define	LIBIPCC_INV_TIME_RAND_SEC	(60 * 30)

#endif /* _LIBIPCC_PRIV_H */
