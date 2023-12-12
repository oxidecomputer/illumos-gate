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
 * Copyright 2022 Oxide Computer Co.
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/io/milan/ccx.h>
#include <sys/io/milan/ccx_impl.h>
#include "milan_apob.h"

/*
 * There are two ways for us to populate the map of "core resources" (CCDs,
 * CCXs, cores, and threads): one is a collection of DF and CCD registers, and
 * that is almost certainly what we want.  The other is the APOB, which this
 * does -- in part.  The caller still needs to go populate the SMN base
 * addresses for these resources' registers.  This exists primarily to support
 * a chicken switch during bringup, to verify that our understanding from the
 * DF matches the APOB.  This should probably go away when we're happy with it;
 * there's no reason to trust the APOB unless we can prove it was built from
 * data we can't access.
 *
 * Standard error semantics: returns -1 on error and does not change *nccds nor
 * *ccdmap.  Otherwise, *nccds is the number of CCDs in socket 0 and *ccdmap is
 * filled in with logical and physical IDs for things.  It is not clear from
 * AMD documentation whether we should expect anything useful from the socket 1
 * APOB instance here; ideally we would use that to detect mismatched SOCs and
 * blow up.
 */
int
milan_apob_populate_coremap(uint8_t *nccds, milan_ccd_t *ccdmap)
{
	const milan_apob_coremap_t *acmp;
	size_t map_len = 0;
	int err = 0;
	uint8_t ccd, ccx, core, thr;

	acmp = milan_apob_find(MILAN_APOB_GROUP_CCX, 3, 0, &map_len, &err);

	if (err != 0) {
		cmn_err(CE_WARN, "missing or invalid APOB CCD map (errno = %d)",
		    err);
		return (-1);
	} else if (map_len < sizeof (*acmp)) {
		cmn_err(CE_WARN, "APOB CCD map is too small "
		    "(0x%lx < 0x%lx bytes)", map_len, sizeof (*acmp));
		return (-1);
	}

	ccd = 0;

	for (uint8_t accd = 0; accd < MILAN_APOB_CCX_MAX_CCDS; accd++) {
		const milan_apob_ccd_t *accdp = &acmp->macm_ccds[accd];
		milan_ccd_t *mcdp = &ccdmap[ccd];

		if (accdp->macd_id == MILAN_APOB_CCX_NONE)
			continue;

		/*
		 * The APOB is telling us there are more CCDs than we expect.
		 * This suggests a corrupt APOB or broken firmware, but it's
		 * also possible that this is an unsupported (unreleased) CPU
		 * or our definitions (for the APOB or otherwise) are wrong.
		 * Ignore the unexpected CCDs and let the caller work it out.
		 */
		if (ccd == MILAN_MAX_CCDS_PER_IODIE) {
			cmn_err(CE_WARN, "unexpected extra CCDs found in APOB "
			    "descriptor (already have %d); ignored\n", ccd);
			break;
		}

		mcdp->mcd_logical_dieno = accd;
		mcdp->mcd_physical_dieno = accdp->macd_id;

		ccx = 0;

		for (uint8_t accx = 0; accx < MILAN_APOB_CCX_MAX_CCXS; accx++) {
			const milan_apob_ccx_t *accxp =
			    &accdp->macd_ccxs[accx];
			milan_ccx_t *mcxp = &mcdp->mcd_ccxs[ccx];

			if (accxp->macx_id == MILAN_APOB_CCX_NONE)
				continue;

			if (ccx == MILAN_MAX_CCXS_PER_CCD) {
				cmn_err(CE_WARN,
				    "unexpected extra CCXs found in APOB for "
				    "CCD 0x%x (already have %d); ignored",
				    mcdp->mcd_physical_dieno,
				    ccx);
				break;
			}

			mcxp->mcx_logical_cxno = accx;
			mcxp->mcx_physical_cxno = accxp->macx_id;

			core = 0;

			for (uint8_t acore = 0;
			    acore < MILAN_APOB_CCX_MAX_CORES; acore++) {
				const milan_apob_core_t *acp =
				    &accxp->macx_cores[acore];
				milan_core_t *mcp = &mcxp->mcx_cores[core];

				if (acp->mac_id == MILAN_APOB_CCX_NONE)
					continue;

				if (core == MILAN_MAX_CORES_PER_CCX) {
					cmn_err(CE_WARN,
					    "unexpected extra cores found in "
					    "APOB for CCX (0x%x, 0x%x) "
					    "(already have %d); "
					    "ignored",
					    mcdp->mcd_physical_dieno,
					    mcxp->mcx_physical_cxno,
					    core);
					break;
				}

				mcp->mc_logical_coreno = acore;
				mcp->mc_physical_coreno = acp->mac_id;

				thr = 0;

				for (uint8_t athr = 0;
				    athr < MILAN_APOB_CCX_MAX_THREADS; athr++) {
					milan_thread_t *mtp =
					    &mcp->mc_threads[thr];

					if (acp->mac_thread_exists[athr] == 0)
						continue;

					if (thr == MILAN_MAX_THREADS_PER_CORE) {
						cmn_err(CE_WARN,
						    "unexpected extra threads "
						    "found in APOB for core "
						    "(0x%x, 0x%x, 0x%x) "
						    "(already have %d); "
						    "ignored\n",
						    mcdp->mcd_physical_dieno,
						    mcxp->mcx_physical_cxno,
						    mcp->mc_physical_coreno,
						    thr);
						break;
					}

					mtp->mt_threadno = athr;
					++thr;
				}

				mcp->mc_nthreads = thr;
				++core;
			}

			mcxp->mcx_ncores = core;
			++ccx;
		}

		mcdp->mcd_nccxs = ccx;
		++ccd;
	}

	*nccds = ccd;
	return (0);
}
