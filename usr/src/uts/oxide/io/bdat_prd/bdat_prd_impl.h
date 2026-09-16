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

#ifndef _BDAT_PRD_IMPL_H
#define	_BDAT_PRD_IMPL_H

/*
 * Internal definitions for the direct AMD Zen BIOS Data ACPI Table (BDAT)
 * provider (bdat_prd_amdzen_direct.c).  These are shared with the bdat_prd
 * ktest module.
 */

#include <sys/types.h>
#include <sys/amdzen/bdat.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Though the raw BDAT data provided by AMD's firmware is not necessarily a
 * stable interface, the overall shape has remained the same. Even still, there
 * are some backwards incompatible changes we try to paper over. These flags
 * represent when such a change has been detected.
 */
typedef enum {
	/*
	 * The PDT_VREF_DAC2 and PDT_VREF_DAC3 types did not exist in earlier
	 * versions and were added right after PDT_VREF_DAC1. Unfortunately,
	 * that ended up shifting the previous set of types that came after.
	 * This flag indicates we're on an older version and should thus adjust
	 * the `zen_bdat_phy_data_type_t` values appropriately.
	 */
	BFQ_F_SKIP_VREFDAC23	= (1 << 0),
} bdat_phy_data_quirks_t;

/*
 * We only care for a subset of the data that the BDAT provides which we
 * bundle together here.
 */
typedef struct {
	size_t				zbr_nspd_rsrcs;
	const zen_bdat_entry_header_t	**zbr_spd_rsrcs;
	size_t				zbr_ndmr_rsrcs;
	const zen_bdat_entry_header_t	**zbr_dmr_rsrcs;
	size_t				zbr_nrmargin_rsrcs;
	const zen_bdat_entry_header_t	**zbr_rmargin_rsrcs;
	size_t				zbr_ndmargin_rsrcs;
	const zen_bdat_entry_header_t	**zbr_dmargin_rsrcs;
	size_t				zbr_nphy_rsrcs;
	size_t				zbr_nphy_alloc;
	zen_bdat_phy_data_t		*zbr_phy_rsrcs;
	bdat_phy_data_quirks_t		zbr_quirks;
} zen_bdat_rsrcs_t;

typedef void (*zen_bdat_cb_f)(const zen_bdat_entry_header_t *, void *);

/*
 * Result of validating a single BDAT entry.
 */
typedef enum {
	ENT_OK,
	ENT_UNKNOWN,
	ENT_INVALID_SIZE,
	ENT_INVALID_VARIANT,
	ENT_INVALID_INDEX
} zen_bdat_entry_valid_t;

#ifdef __cplusplus
}
#endif

#endif /* _BDAT_PRD_IMPL_H */
