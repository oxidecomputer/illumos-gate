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
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/ccx_impl.h>
#include "genoa_apob.h"

/*
 * There are two ways to find the data used to populate the map of "core
 * resources": CCDs, CCXs, cores, and threads.  The first is querying a
 * collection of DF and CCD registers, the other is extracting this data
 * from the APOB.  While we almost certainly want the former, this code
 * implements the latter, though the caller still needs to populate the
 * SMN base addresses for these resource's registers.  We use this primarily
 * to verify that our understanding from the DF matches the APOB during
 * bringup.
 *
 * This should probably go away when we're happy with it; there's no reason
 * to trust the APOB unless we can prove it was built from data we cannot
 * access.
 *
 * Standard error semantics: returns -1 on error and does not change *nccds nor
 * *ccdmap.  Otherwise, *nccds is the number of CCDs in socket 0 and *ccdmap is
 * filled in with logical and physical IDs for resources.  It is not clear from
 * AMD documentation whether we should expect anything useful from the socket 1
 * APOB instance here; ideally we would use that to detect mismatched SOCs and
 * panic.
 */
int
genoa_apob_populate_coremap(uint8_t *nccds, genoa_ccd_t *ccdmap)
{
	const genoa_apob_coremap_t *acmp;
	size_t map_len = 0;
	int err = 0;
	uint8_t ccd, ccx, core, thr;

	acmp = genoa_apob_find(GENOA_APOB_GROUP_CCX, 3, 0, &map_len, &err);

	if (err != 0) {
		cmn_err(CE_WARN,
		    "missing or invalid APOB CCD map (errno = %d)", err);
		return (-1);
	} else if (map_len < sizeof (*acmp)) {
		cmn_err(CE_WARN,
		    "APOB CCD map is too small (0x%lx < 0x%lx bytes)",
		    map_len, sizeof (*acmp));
		return (-1);
	}

	ccd = 0;

	for (uint8_t accd = 0; accd < GENOA_APOB_CCX_MAX_CCDS; accd++) {
		const genoa_apob_ccd_t *accdp = &acmp->macm_ccds[accd];
		genoa_ccd_t *mcdp = &ccdmap[ccd];

		if (accdp->macd_id == GENOA_APOB_CCX_NONE)
			continue;

		/*
		 * The APOB is telling us there are more CCDs than we expect.
		 * This suggests a corrupt APOB or broken firmware, but it's
		 * also possible that this is an unsupported (unreleased) CPU
		 * or our definitions (for the APOB or otherwise) are wrong.
		 * Ignore the unexpected CCDs and let the caller work it out.
		 */
		if (ccd == GENOA_MAX_CCDS_PER_IODIE) {
			cmn_err(CE_WARN, "unexpected extra CCDs found in APOB "
			    "descriptor (already have %d); ignored\n", ccd);
			break;
		}

		mcdp->mcd_logical_dieno = accd;
		mcdp->mcd_physical_dieno = accdp->macd_id;

		ccx = 0;

		for (uint8_t accx = 0; accx < GENOA_APOB_CCX_MAX_CCXS; accx++) {
			const genoa_apob_ccx_t *accxp = &accdp->macd_ccxs[accx];
			genoa_ccx_t *mcxp = &mcdp->mcd_ccxs[ccx];

			if (accxp->macx_id == GENOA_APOB_CCX_NONE)
				continue;

			if (ccx == GENOA_MAX_CCXS_PER_CCD) {
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
			    acore < GENOA_APOB_CCX_MAX_CORES; acore++) {
				const genoa_apob_core_t *acp =
				    &accxp->macx_cores[acore];
				genoa_core_t *mcp = &mcxp->mcx_cores[core];

				if (acp->mac_id == GENOA_APOB_CCX_NONE)
					continue;

				if (core == GENOA_MAX_CORES_PER_CCX) {
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
				    athr < GENOA_APOB_CCX_MAX_THREADS; athr++) {
					genoa_thread_t *mtp =
					    &mcp->mc_threads[thr];

					if (acp->mac_thread_exists[athr] == 0)
						continue;

					if (thr == GENOA_MAX_THREADS_PER_CORE) {
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
