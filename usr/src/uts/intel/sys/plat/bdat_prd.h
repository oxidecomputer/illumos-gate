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

#ifndef	_SYS_PLAT_BDAT_PRD_H
#define	_SYS_PLAT_BDAT_PRD_H

/*
 * BIOS Data ACPI Table (BDAT) Platform Resource Discovery (PRD)
 *
 * The BDAT contains various verification related data (e.g., memory margining)
 * that can be provided by system firmware. This file contains the platform-
 * specific interfaces that a given platform must implement to support the
 * discovery of BDAT resources.
 *
 * These interfaces are all expected to be implemented by a platform's
 * 'bdat_prd' module. This is left as a module and not a part of say, unix, so
 * that it can in turn depend on other modules that a platform might require,
 * such as ACPI.
 *
 * In general, unless otherwise indicated, these interfaces will always be
 * called from kernel context. The interfaces will only be called from a single
 * thread at this time and any locking is managed at a layer outside of the
 * bdat_prd interfaces. If the subsystem is using some other interfaces that may
 * be used by multiple consumers and needs locking (e.g. ACPI), then that still
 * must be considered in the design and implementation.
 *
 * Note this is private interface to the system and subject to change.
 */

#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Memory resources that can be asked after.
 */
typedef enum bdat_prd_mem_rsrc {
	/*
	 * The raw SPD data for a given DIMM.
	 * Selectors: Socket, Channel & DIMM.
	 */
	BDAT_PRD_MEM_SPD,
	/*
	 * The training margin data per rank (AMD-specific extension).
	 * Selectors: Socket, Channel, DIMM & Rank.
	 */
	BDAT_PRD_MEM_AMD_RANK_MARGIN,
	/*
	 * The training margin data per DQ/lane (AMD-specific extension).
	 * Selectors: Socket, Channel, Sub Channel, DIMM, & Rank.
	 */
	BDAT_PRD_MEM_AMD_DQ_MARGIN
} bdat_prd_mem_rsrc_t;

/*
 * Selector type for BDAT memory resources. Depending on the resource type, not
 * all fields may be used.
 */
typedef struct bdat_prd_mem_select {
	uint8_t	bdat_sock;
	uint8_t	bdat_chan;
	uint8_t	bdat_subchan;
	uint8_t	bdat_dimm;
	uint8_t	bdat_rank;
} bdat_prd_mem_select_t;

typedef enum bdat_prd_errno {
	BPE_OK,		/* No error */
	BPE_NOBDAT,	/* BDAT not present */
	BPE_NORES,	/* Requested BDAT resource not found */
	BPE_SIZE	/* Provided buffer too small to read in BDAT resource */
} bdat_prd_errno_t;


/*
 * Check if the requested BDAT memory resource is present for the given selector
 * and if so, returns its size.
 */
extern bool bdat_prd_mem_present(bdat_prd_mem_rsrc_t,
    const bdat_prd_mem_select_t *, size_t *);

/*
 * Read the requested BDAT memory resource for the given selector into the
 * provided buffer.
 */
extern bdat_prd_errno_t bdat_prd_mem_read(bdat_prd_mem_rsrc_t,
    const bdat_prd_mem_select_t *, void *, size_t);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_PLAT_BDAT_PRD_H */
