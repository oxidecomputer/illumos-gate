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
 * Copyright 2025 Oxide Computer Co.
 */

#ifndef	_MC_AMDZEN_H
#define	_MC_AMDZEN_H

#include "zen_umc.h"

/*
 * Definitions, structures, constants etc pertaining to the memory controller
 * on AMD Zen systems.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Configuration constants
 */
#define	MC_ZEN_MAX_CHANS	ZEN_UMC_MAX_UMCS
#define	MC_ZEN_MAX_SUBCHANS	ZEN_UMC_MAX_SUBCHAN_PER_CHAN
#define	MC_ZEN_MAX_DIMMS	ZEN_UMC_MAX_DIMMS
#define	MC_ZEN_MAX_RANKS	ZEN_UMC_MAX_CS_PER_DIMM

/*
 * A single DRAM margin result. This may be for a whole rank or just a
 * particular DQ (lane).
 */
typedef struct mc_zen_margin_t {
	/*
	 * The left and right edges of the trained read DQS to read CLK delay.
	 */
	uint8_t		mzm_rd_dqdly[2];
	/*
	 * The left and right edges of the trained write DQ to write DQS delay.
	 */
	uint8_t		mzm_wr_dqdly[2];
	/*
	 * The max margin compared to the min/max trained read Vref.
	 */
	uint8_t		mzm_rd_vref[2];
	/*
	 * The max margin compared to the min/max trained write Vref.
	 */
	uint8_t		mzm_wr_vref[2];
} mc_zen_margin_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _MC_AMDZEN_H */
