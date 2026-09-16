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

/*
 * The BIOS Data ACPI Table (BDAT), as the name implies, is provided by the
 * BIOS/UEFI firmware via ACPI. On AMD Zen platforms, we can skip ACPI and
 * directly access the BDAT as provided by the system firmware.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/amdzen/bdat.h>
#include <sys/boot_data.h>
#include <sys/plat/bdat_prd.h>
#include <sys/psm.h>
#include <sys/cpuvar.h>
#include <sys/mc.h>
#include <sys/x86_archext.h>

#include "bdat_prd_impl.h"

/*
 * Pointer to the BDAT, if present.
 */
static const zen_bdat_header_t *bdat_prd_amdzen_raw = NULL;

/*
 * We only care for a subset of the data that the BDAT provides which we
 * cache here if found.
 */
static zen_bdat_rsrcs_t bdat_prd_amdzen_rsrcs;

static bool
zen_bdat_rsc_matches(bdat_prd_mem_rsrc_t rtype,
    const bdat_prd_mem_select_t *rsel,
    const zen_bdat_entry_header_t *ent)
{
	const zen_bdat_entry_spd_t *spd;
	const zen_bdat_entry_dram_mode_regs_t *dmr;
	const zen_bdat_entry_rank_margin_t *rm;
	const zen_bdat_entry_dq_margin_t *dm;

	switch (rtype) {
	case BDAT_PRD_MEM_SPD:
		spd = (const zen_bdat_entry_spd_t *)ent->zbe_data;
		return (spd->zbes_socket == rsel->bdat_sock &&
		    spd->zbes_channel == rsel->bdat_chan &&
		    spd->zbes_dimm == rsel->bdat_dimm);
	case BDAT_PRD_MEM_DRAM_MODE_REGS:
		dmr = (const zen_bdat_entry_dram_mode_regs_t *)ent->zbe_data;
		return (dmr->zbedmr_loc.zbml_socket == rsel->bdat_sock &&
		    dmr->zbedmr_loc.zbml_channel == rsel->bdat_chan &&
		    dmr->zbedmr_loc.zbml_sub_channel == rsel->bdat_subchan &&
		    dmr->zbedmr_loc.zbml_dimm == rsel->bdat_dimm &&
		    dmr->zbedmr_loc.zbml_rank == rsel->bdat_rank);
	case BDAT_PRD_MEM_AMD_RANK_MARGIN:
		rm = (const zen_bdat_entry_rank_margin_t *)ent->zbe_data;
		return (rm->zberm_loc.zbml_socket == rsel->bdat_sock &&
		    rm->zberm_loc.zbml_channel == rsel->bdat_chan &&
		    rm->zberm_loc.zbml_dimm == rsel->bdat_dimm &&
		    rm->zberm_loc.zbml_rank == rsel->bdat_rank);
	case BDAT_PRD_MEM_AMD_DQ_MARGIN:
		dm = (const zen_bdat_entry_dq_margin_t *)ent->zbe_data;
		return (dm->zbedm_loc.zbml_socket == rsel->bdat_sock &&
		    dm->zbedm_loc.zbml_channel == rsel->bdat_chan &&
		    dm->zbedm_loc.zbml_sub_channel == rsel->bdat_subchan &&
		    dm->zbedm_loc.zbml_dimm == rsel->bdat_dimm &&
		    dm->zbedm_loc.zbml_rank == rsel->bdat_rank);
	case BDAT_PRD_MEM_AMD_PHY_DATA:
		cmn_err(CE_PANIC, "bdat_prd: unexpected type");
	}

	return (false);
}

static bool
bdat_prd_mem_phy_data_present(const bdat_prd_mem_select_t *rsel, size_t *rsize,
    size_t pstate_idx[PDP_MAX])
{
	const zen_bdat_rsrcs_t *rsrcs = &bdat_prd_amdzen_rsrcs;
	const zen_bdat_phy_data_t *pdata = rsrcs->zbr_phy_rsrcs;
	size_t count = 0;

	for (size_t i = 0; i < rsrcs->zbr_nphy_rsrcs; i++) {
		const zen_bdat_phy_data_t *pd = &pdata[i];
		if (rsel->bdat_sock != pd->zbpd_sock ||
		    rsel->bdat_chan != pd->zbpd_chan) {
			continue;
		}

		VERIFY3U(pd->zbpd_pstate, <, PDP_MAX);
		if (pstate_idx != NULL) {
			pstate_idx[pd->zbpd_pstate] = i;
		}
		count++;
	}
	VERIFY3U(count, <, PDP_MAX);

	if (count == 0)
		return (false);

	*rsize = count * sizeof (zen_bdat_phy_data_t);
	return (true);
}

bool
bdat_prd_mem_present(bdat_prd_mem_rsrc_t rtype,
    const bdat_prd_mem_select_t *rsel, size_t *rsize)
{
	const zen_bdat_rsrcs_t *rsrcs = &bdat_prd_amdzen_rsrcs;
	const zen_bdat_entry_header_t **ents;
	size_t nents;

	*rsize = 0;

	switch (rtype) {
	case BDAT_PRD_MEM_SPD:
		ents = rsrcs->zbr_spd_rsrcs;
		nents = rsrcs->zbr_nspd_rsrcs;
		break;
	case BDAT_PRD_MEM_DRAM_MODE_REGS:
		ents = rsrcs->zbr_dmr_rsrcs;
		nents = rsrcs->zbr_ndmr_rsrcs;
		break;
	case BDAT_PRD_MEM_AMD_RANK_MARGIN:
		ents = rsrcs->zbr_rmargin_rsrcs;
		nents = rsrcs->zbr_nrmargin_rsrcs;
		break;
	case BDAT_PRD_MEM_AMD_DQ_MARGIN:
		ents = rsrcs->zbr_dmargin_rsrcs;
		nents = rsrcs->zbr_ndmargin_rsrcs;
		break;
	case BDAT_PRD_MEM_AMD_PHY_DATA:
		return (bdat_prd_mem_phy_data_present(rsel, rsize, NULL));
	default:
		return (false);
	}

	for (size_t i = 0; i < nents; i++) {
		const zen_bdat_entry_header_t *ent = ents[i];
		const zen_bdat_entry_spd_t *spd;
		const zen_bdat_entry_dram_mode_regs_t *dmr;

		if (!zen_bdat_rsc_matches(rtype, rsel, ent))
			continue;

		switch (rtype) {
		case BDAT_PRD_MEM_SPD:
			spd = (const zen_bdat_entry_spd_t *)ent->zbe_data;
			*rsize = spd->zbes_size;
			return (true);

		case BDAT_PRD_MEM_DRAM_MODE_REGS:
			dmr = (const zen_bdat_entry_dram_mode_regs_t *)
			    ent->zbe_data;
			*rsize = sizeof (mc_dram_mode_regs_t) +
			    ((size_t)dmr->zbedmr_nregs *
			    (size_t)dmr->zbedmr_ndrams);
			return (true);

		case BDAT_PRD_MEM_AMD_RANK_MARGIN:
			*rsize = sizeof (zen_bdat_margin_t);
			return (true);

		case BDAT_PRD_MEM_AMD_DQ_MARGIN:
			*rsize = ent->zbe_size -
			    sizeof (zen_bdat_entry_header_t) -
			    sizeof (zen_bdat_entry_dq_margin_t);
			return (true);
		case BDAT_PRD_MEM_AMD_PHY_DATA:
			cmn_err(CE_PANIC, "bdat_prd: unexpected type");
		}
	}

	return (false);
}

static bdat_prd_errno_t
bdat_prd_mem_phy_data_read(const bdat_prd_mem_select_t *rsel, void *rsrc,
    size_t rsize)
{
	const zen_bdat_rsrcs_t *rsrcs = &bdat_prd_amdzen_rsrcs;
	const zen_bdat_phy_data_t *pdata = rsrcs->zbr_phy_rsrcs;
	const zen_bdat_phy_data_t *pd0 = NULL;

	size_t pstate_idx[PDP_MAX] = { SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX };
	size_t size = 0;

	if (!bdat_prd_mem_phy_data_present(rsel, &size, pstate_idx))
		return (BPE_NORES);

	if (rsize < size)
		return (BPE_SIZE);

	for (size_t i = 0; i < PDP_MAX; i++) {
		const zen_bdat_phy_data_t *pd;

		if (pstate_idx[i] == SIZE_MAX)
			continue;

		pd = &pdata[pstate_idx[i]];
		VERIFY3U(pd->zbpd_pstate, ==, i);
		if (i == 0)
			pd0 = pd;

		VERIFY3U(rsize, >=, sizeof (*pd));
		bcopy(pd, rsrc, sizeof (*pd));

		/*
		 * Duplicate the non-P-state specific data stored on P0.
		 */
		if (i > 0) {
			VERIFY3P(pd0, !=, NULL);
			bcopy(pd0->zbpd_rxpbdly,
			    ((zen_bdat_phy_data_t *)rsrc)->zbpd_rxpbdly,
			    sizeof (pd0->zbpd_rxpbdly));
		}

		rsrc += sizeof (*pd);
		rsize -= sizeof (*pd);
	}

	return (BPE_OK);
}

bdat_prd_errno_t
bdat_prd_mem_read(bdat_prd_mem_rsrc_t rtype,
    const bdat_prd_mem_select_t *rsel, void *rsrc, size_t rsize)
{
	const zen_bdat_rsrcs_t *rsrcs = &bdat_prd_amdzen_rsrcs;
	const zen_bdat_entry_header_t **ents;
	size_t nents;

	switch (rtype) {
	case BDAT_PRD_MEM_SPD:
		ents = rsrcs->zbr_spd_rsrcs;
		nents = rsrcs->zbr_nspd_rsrcs;
		break;
	case BDAT_PRD_MEM_DRAM_MODE_REGS:
		ents = rsrcs->zbr_dmr_rsrcs;
		nents = rsrcs->zbr_ndmr_rsrcs;
		break;
	case BDAT_PRD_MEM_AMD_RANK_MARGIN:
		ents = rsrcs->zbr_rmargin_rsrcs;
		nents = rsrcs->zbr_nrmargin_rsrcs;
		break;
	case BDAT_PRD_MEM_AMD_DQ_MARGIN:
		ents = rsrcs->zbr_dmargin_rsrcs;
		nents = rsrcs->zbr_ndmargin_rsrcs;
		break;
	case BDAT_PRD_MEM_AMD_PHY_DATA:
		return (bdat_prd_mem_phy_data_read(rsel, rsrc, rsize));
	default:
		return (BPE_NORES);
	}

	for (size_t i = 0; i < nents; i++) {
		const zen_bdat_entry_header_t *ent = ents[i];
		const zen_bdat_entry_spd_t *spd;
		const zen_bdat_entry_dram_mode_regs_t *dmr;
		const zen_bdat_entry_rank_margin_t *rm;
		const zen_bdat_entry_dq_margin_t *dm;
		mc_dram_mode_regs_t *moderegs;

		if (!zen_bdat_rsc_matches(rtype, rsel, ent))
			continue;

		switch (rtype) {
		case BDAT_PRD_MEM_SPD:
			spd = (const zen_bdat_entry_spd_t *)ent->zbe_data;
			if (rsize < spd->zbes_size)
				return (BPE_SIZE);
			bcopy(spd->zbes_data, rsrc, spd->zbes_size);
			return (BPE_OK);

		case BDAT_PRD_MEM_DRAM_MODE_REGS:
			dmr = (const zen_bdat_entry_dram_mode_regs_t *)
			    ent->zbe_data;
			if (rsize != (sizeof (mc_dram_mode_regs_t) +
			    ((size_t)dmr->zbedmr_nregs *
			    (size_t)dmr->zbedmr_ndrams)))
				return (BPE_SIZE);
			moderegs = (mc_dram_mode_regs_t *)rsrc;
			moderegs->mdmr_nregs = dmr->zbedmr_nregs;
			moderegs->mdmr_ndies = dmr->zbedmr_ndrams;
			rsize -= sizeof (mc_dram_mode_regs_t);
			bcopy(dmr->zbedmr_data, moderegs->mdmr_moderegs, rsize);
			return (BPE_OK);

		case BDAT_PRD_MEM_AMD_RANK_MARGIN:
			rm = (const zen_bdat_entry_rank_margin_t *)
			    ent->zbe_data;
			if (rsize < sizeof (zen_bdat_margin_t))
				return (BPE_SIZE);
			bcopy(&rm->zberm_margin, rsrc,
			    sizeof (zen_bdat_margin_t));
			return (BPE_OK);

		case BDAT_PRD_MEM_AMD_DQ_MARGIN:
			dm = (const zen_bdat_entry_dq_margin_t *)ent->zbe_data;
			if (rsize != (ent->zbe_size -
			    sizeof (zen_bdat_entry_header_t) -
			    sizeof (zen_bdat_entry_dq_margin_t)))
				return (BPE_SIZE);
			bcopy(dm->zbedm_margin, rsrc, rsize);
			return (BPE_OK);

		case BDAT_PRD_MEM_AMD_PHY_DATA:
			cmn_err(CE_PANIC, "bdat_prd: unexpected type");
		}
	}

	return (BPE_NORES);
}

static zen_bdat_entry_valid_t
zen_bdat_entry_valid(const zen_bdat_entry_header_t *ent)
{
	const zen_bdat_entry_spd_t *spd;
	const zen_bdat_entry_dram_mode_regs_t *dmr;
	const zen_bdat_entry_rank_margin_t *rm;
	const zen_bdat_entry_dq_margin_t *dm;
	const zen_bdat_entry_phy_data_t *pd;
	zen_bdat_mem_location_t loc = { 0 };
	size_t ent_size = ent->zbe_size;

	if (ent_size < sizeof (zen_bdat_entry_header_t))
		return (ENT_INVALID_SIZE);
	ent_size -= sizeof (zen_bdat_entry_header_t);

	switch (ent->zbe_schema) {
	case BDAT_DIMM_SPD_SCHEMA:
		if (ent->zbe_type != BDAT_DIMM_SPD_TYPE)
			return (ENT_UNKNOWN);

		if (ent_size < sizeof (zen_bdat_entry_spd_t))
			return (ENT_INVALID_VARIANT);

		spd = (const zen_bdat_entry_spd_t *)ent->zbe_data;
		ent_size -= sizeof (zen_bdat_entry_spd_t);
		if (spd->zbes_size != ent_size) {
			return (ENT_INVALID_VARIANT);
		}
		loc.zbml_socket = spd->zbes_socket;
		loc.zbml_channel = spd->zbes_channel;
		loc.zbml_dimm = spd->zbes_dimm;
		break;
	case BDAT_MEM_TRAINING_DATA_SCHEMA:
		switch (ent->zbe_type) {
		case BDAT_MEM_TRAINING_DATA_MODE_REGS_TYPE:
			if (ent_size < sizeof (zen_bdat_entry_dram_mode_regs_t))
				return (ENT_INVALID_VARIANT);

			dmr = (const zen_bdat_entry_dram_mode_regs_t *)
			    ent->zbe_data;

			ent_size -= sizeof (zen_bdat_entry_dram_mode_regs_t);
			if (ent_size != ((size_t)dmr->zbedmr_ndrams *
			    (size_t)dmr->zbedmr_nregs)) {
				return (ENT_INVALID_VARIANT);
			}
			loc.zbml_socket = dmr->zbedmr_loc.zbml_socket;
			loc.zbml_channel = dmr->zbedmr_loc.zbml_channel;
			loc.zbml_sub_channel = dmr->zbedmr_loc.zbml_sub_channel;
			loc.zbml_dimm = dmr->zbedmr_loc.zbml_dimm;
			loc.zbml_rank = dmr->zbedmr_loc.zbml_rank;
			break;
		case BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE:
			if (ent_size != sizeof (zen_bdat_entry_rank_margin_t))
				return (ENT_INVALID_VARIANT);
			rm = (const zen_bdat_entry_rank_margin_t *)
			    ent->zbe_data;
			loc.zbml_socket = rm->zberm_loc.zbml_socket;
			loc.zbml_channel = rm->zberm_loc.zbml_channel;
			loc.zbml_dimm = rm->zberm_loc.zbml_dimm;
			loc.zbml_rank = rm->zberm_loc.zbml_rank;
			break;
		case BDAT_MEM_TRAINING_DATA_DQ_MARGIN_TYPE:
			if (ent_size < sizeof (zen_bdat_entry_dq_margin_t))
				return (ENT_INVALID_VARIANT);
			dm = (const zen_bdat_entry_dq_margin_t *)ent->zbe_data;
			/*
			 * The remaining space should be a positive multiple of
			 * `zen_bdat_margin_t` corresponding to an entry per DQ.
			 */
			ent_size -= sizeof (zen_bdat_entry_dq_margin_t);
			if (ent_size == 0 ||
			    (ent_size % sizeof (zen_bdat_margin_t) != 0)) {
				return (ENT_INVALID_VARIANT);
			}
			loc.zbml_socket = dm->zbedm_loc.zbml_socket;
			loc.zbml_channel = dm->zbedm_loc.zbml_channel;
			loc.zbml_sub_channel = dm->zbedm_loc.zbml_sub_channel;
			loc.zbml_dimm = dm->zbedm_loc.zbml_dimm;
			loc.zbml_rank = dm->zbedm_loc.zbml_rank;
			break;
		case BDAT_MEM_TRAINING_DATA_PHY_TYPE:
			if (ent_size < sizeof (zen_bdat_entry_phy_data_t))
				return (ENT_INVALID_VARIANT);
			/*
			 * Validate fields match our expectation and if so,
			 * the remaining space should match the stated number of
			 * elements multiplied by the per-element size.
			 */
			pd = (const zen_bdat_entry_phy_data_t *)ent->zbe_data;
			if (pd->zbepd_type >= PDT_MAX ||
			    pd->zbepd_scope >= PDS_MAX ||
			    (pd->zbepd_pstate >= PDP_MAX &&
			    pd->zbepd_pstate != PDP_NA) ||
			    (pd->zbepd_elems_size != 1 &&
			    pd->zbepd_elems_size != 2 &&
			    pd->zbepd_elems_size != 4)) {
				return (ENT_INVALID_VARIANT);
			}
			ent_size -= sizeof (zen_bdat_entry_phy_data_t);
			if ((pd->zbepd_nelems * pd->zbepd_elems_size) !=
			    ent_size) {
				return (ENT_INVALID_VARIANT);
			}
			loc.zbml_socket = pd->zbepd_loc.zbml_socket;
			loc.zbml_channel = pd->zbepd_loc.zbml_channel;
			/*
			 * The remaining indicies for PHY entries (DIMM, Rank,
			 * Sub-Channel) requires knowing which type we're
			 * dealing with.  But knowing what type we have also
			 * requires knowing if we need to apply the
			 * BFQ_F_SKIP_VREFDAC23 quirk fix.  We may not know
			 * that yet here so we defer checking until
			 * zen_bdat_fill_phy_ent().
			 */
			break;
		default:
			return (ENT_UNKNOWN);
		}
		break;
	default:
		return (ENT_UNKNOWN);
	}

	/*
	 * Verify the selector indicies.  The relevant fields for the entry get
	 * set above with any unused fields left zero.
	 */
	if (loc.zbml_socket >= BDAT_SOC_COUNT ||
	    loc.zbml_channel >= BDAT_NCHANS ||
	    loc.zbml_sub_channel >= BDAT_NSUBCHANS ||
	    loc.zbml_dimm >= BDAT_NDIMMS ||
	    loc.zbml_rank >= BDAT_NRANKS) {
		return (ENT_INVALID_INDEX);
	}

	return (ENT_OK);
}

/*
 * Walk the BDAT entries (for both sockets, if present), calling the provided
 * function for each one.
 */
static void
zen_bdat_walk_entries(const zen_bdat_header_t *bdat_base, zen_bdat_cb_f func,
    void *arg)
{
	for (unsigned int i = 0; i < BDAT_SOC_COUNT; i++) {
		const zen_bdat_header_t *bdat = (const zen_bdat_header_t *)
		    ((uintptr_t)bdat_base + (i * BDAT_SIZE));
		uintptr_t end = (uintptr_t)bdat + BDAT_SIZE;

		while (bdat->zbh_signature == BDAT_SIGNATURE) {
			const zen_bdat_entry_header_t *ent;
			zen_bdat_entry_valid_t ent_valid;
			size_t ent_off = sizeof (zen_bdat_header_t);
			do {
				/*
				 * Make sure the next entry header is within
				 * the expected region.
				 */
				if ((uintptr_t)bdat + ent_off
				    + sizeof (zen_bdat_entry_header_t) >= end) {
					break;
				}

				ent = (const zen_bdat_entry_header_t *)
				    ((uintptr_t)bdat + ent_off);

				/*
				 * Make sure the header-reported size also falls
				 * within our bounds.
				 */
				if ((uintptr_t)ent + ent->zbe_size >= end)
					break;

				ent_valid = zen_bdat_entry_valid(ent);
				if (ent_valid == ENT_INVALID_SIZE) {
					/*
					 * We can't trust the size field so we
					 * stop trying to walk the entries.
					 */
					break;
				}

				/*
				 * We'll only invoke the callback for entries we
				 * recognize and whose invariants hold.
				 */
				if (ent_valid == ENT_OK) {
					func(ent, arg);
				}

				/*
				 * But we'll still continue walking with the
				 * assumption that the size field is correct.
				 */
				ent_off += ent->zbe_size;
			} while (ent->zbe_size != 0);

			/*
			 * Next should point at least a header's worth forward.
			 */
			if (bdat->zbh_next < sizeof (zen_bdat_header_t))
				break;

			/*
			 * The next header should also be within the valid BDAT
			 * region.
			 */
			if ((uintptr_t)bdat + bdat->zbh_next +
			    sizeof (zen_bdat_header_t) >= end) {
				break;
			}
			bdat = (const zen_bdat_header_t *)
			    ((uintptr_t)bdat + bdat->zbh_next);
		}
	}
}

static void
zen_bdat_ent_counts_cb(const zen_bdat_entry_header_t *ent, void *arg)
{
	static bool phy_ents_seen[BDAT_SOC_COUNT][BDAT_NCHANS][PDP_MAX];

	zen_bdat_rsrcs_t *rs = arg;
	const zen_bdat_entry_phy_data_t *pde;
	uint8_t sock, chan, pstate;

	switch (ent->zbe_schema) {
	case BDAT_DIMM_SPD_SCHEMA:
		if (ent->zbe_type == BDAT_DIMM_SPD_TYPE)
			rs->zbr_nspd_rsrcs++;
		break;
	case BDAT_MEM_TRAINING_DATA_SCHEMA:
		switch (ent->zbe_type) {
		case BDAT_MEM_TRAINING_DATA_MODE_REGS_TYPE:
			rs->zbr_ndmr_rsrcs++;
			break;
		case BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE:
			rs->zbr_nrmargin_rsrcs++;
			break;
		case BDAT_MEM_TRAINING_DATA_DQ_MARGIN_TYPE:
			rs->zbr_ndmargin_rsrcs++;
			break;
		case BDAT_MEM_TRAINING_DATA_PHY_TYPE:
			/*
			 * Since the PHY data is spread across multiple entries,
			 * we do a little more to consolidate them into
			 * per-channel + p-state synthetic entries.
			 */
			pde = (const zen_bdat_entry_phy_data_t *)ent->zbe_data;
			sock = pde->zbepd_loc.zbml_socket;
			chan = pde->zbepd_loc.zbml_channel;
			/*
			 * Some entries are not P-state specific, but for
			 * the purpose of counting how many synthetic entries to
			 * make here we'll treat it as P0. P0 is the default
			 * with any additional P-states assigned sequentially.
			 * We'll go ahead duplicate those values across all
			 * P-states as part of returning those entries to a
			 * consumer.
			 */
			pstate = (pde->zbepd_pstate == PDP_NA) ? 0 :
			    pde->zbepd_pstate;

			/*
			 * The VrefDAC2/3 types were added in a backwards
			 * incompatible way unfortunately. Try to detect if
			 * we're on a previous version by looking at the
			 * type 13: on earlier versions that would be DFIMRL
			 * instead of RX_EN_DLY which have different scopes
			 * and data size.
			 */
			if (pde->zbepd_type == PDT_RX_EN_DLY &&
			    pde->zbepd_scope == PDS_PER_BYTE &&
			    pde->zbepd_elems_size == 1 &&
			    pde->zbepd_nelems == BDAT_NBYTES) {
				rs->zbr_quirks |= BFQ_F_SKIP_VREFDAC23;
			}

			if (phy_ents_seen[sock][chan][pstate])
				break;

			phy_ents_seen[sock][chan][pstate] = true;
			rs->zbr_nphy_rsrcs++;
			break;
		}
		break;
	}
}

static bool
zen_bdat_fill_phy_ent(zen_bdat_rsrcs_t *rs,
    const zen_bdat_entry_phy_data_t *pde)
{
	zen_bdat_phy_data_t *pd = NULL;
	uint8_t sock, chan, subchan, dimm, rank, pstate;
	uint8_t *dst = NULL;
	size_t size, max_size;
	zen_bdat_phy_data_type_t type;
	zen_bdat_phy_data_scope_t scope;

	sock = pde->zbepd_loc.zbml_socket;
	chan = pde->zbepd_loc.zbml_channel;
	subchan = pde->zbepd_loc.zbml_sub_channel;
	dimm = pde->zbepd_loc.zbml_dimm;
	rank = pde->zbepd_loc.zbml_rank;
	pstate = (pde->zbepd_pstate == PDP_NA) ? 0 : pde->zbepd_pstate;
	size = pde->zbepd_nelems * pde->zbepd_elems_size;

	if ((rs->zbr_quirks & BFQ_F_SKIP_VREFDAC23) != 0 &&
	    pde->zbepd_type >= PDT_VREF_DAC2) {
		type = pde->zbepd_type + 2;
	} else {
		type = pde->zbepd_type;
	}

	/*
	 * Find a matching consolidated entry...
	 */
	for (size_t i = 0; i < rs->zbr_nphy_rsrcs; i++) {
		if (rs->zbr_phy_rsrcs[i].zbpd_sock == sock &&
		    rs->zbr_phy_rsrcs[i].zbpd_chan == chan &&
		    rs->zbr_phy_rsrcs[i].zbpd_pstate == pstate)
			pd = &rs->zbr_phy_rsrcs[i];
	}
	/*
	 * ... or use a new one.
	 */
	if (pd == NULL) {
		/*
		 * The number of consolidated entries was determined during the
		 * counting pass and drives the allocation below. If we somehow
		 * need more than that here (i.e. the two passes disagreed) drop
		 * the entry rather than writing past the allocation.
		 */
		if (rs->zbr_nphy_rsrcs >= rs->zbr_nphy_alloc) {
			cmn_err(CE_WARN, "bdat_prd: more PHY entries than "
			    "expected (%lu); dropping socket %u channel %u "
			    "p-state %u", rs->zbr_nphy_alloc, sock, chan,
			    pstate);
			return (false);
		}
		pd = &rs->zbr_phy_rsrcs[rs->zbr_nphy_rsrcs++];
		pd->zbpd_sock = sock;
		pd->zbpd_chan = chan;
		pd->zbpd_pstate = pstate;
	}

	/*
	 * Find the right spot to fill in this data type (noting the expected
	 * size and scope).
	 */
	switch (type) {
#define	PHY_DATA_ENTRY(t, d, sc, valid) \
	case PDT_##t: \
		if (!(valid)) { \
			cmn_err(CE_WARN, "bdat_prd: out of range location " \
			    "for PHY data type %u (%u): sub-channel %u, " \
			    "DIMM %u, rank %u", type, pde->zbepd_type, \
			    subchan, dimm, rank); \
			return (false); \
		} \
		dst = (uint8_t *)&pd->zbpd_##d; \
		max_size = sizeof (pd->zbpd_##d); \
		scope = PDS_PER_##sc; \
		break;
	PHY_DATA_ENTRY(CS_DLY, csdly[subchan], STROBE, subchan < BDAT_NSUBCHANS)
	PHY_DATA_ENTRY(CLK_DLY, clkdly, DIMM, true)
	PHY_DATA_ENTRY(CA_DLY, cadly[subchan], BIT, subchan < BDAT_NSUBCHANS)
	PHY_DATA_ENTRY(RX_PB_DLY, rxpbdly[dimm][rank], BIT,
	    dimm < BDAT_NDIMMS && rank < BDAT_NRANKS)
	PHY_DATA_ENTRY(VREF_DAC0, vrefdac[0], BIT, true)
	PHY_DATA_ENTRY(VREF_DAC1, vrefdac[1], BIT, true)
	PHY_DATA_ENTRY(VREF_DAC2, vrefdac[2], BIT, true)
	PHY_DATA_ENTRY(VREF_DAC3, vrefdac[3], BIT, true)
	PHY_DATA_ENTRY(DFE_TAP2, dfetap[0], BIT, true)
	PHY_DATA_ENTRY(DFE_TAP3, dfetap[1], BIT, true)
	PHY_DATA_ENTRY(DFE_TAP4, dfetap[2], BIT, true)
	PHY_DATA_ENTRY(TX_DQ_DLY, txdqdly[dimm][rank], BIT,
	    dimm < BDAT_NDIMMS && rank < BDAT_NRANKS)
	PHY_DATA_ENTRY(TX_DQS_DLY, txdqsdly[dimm][rank], NIBBLE,
	    dimm < BDAT_NDIMMS && rank < BDAT_NRANKS)
	PHY_DATA_ENTRY(RX_EN_DLY, rxendly[dimm][rank], NIBBLE,
	    dimm < BDAT_NDIMMS && rank < BDAT_NRANKS)
	PHY_DATA_ENTRY(RX_CLK_DLY, rxclkdly[dimm][rank], NIBBLE,
	    dimm < BDAT_NDIMMS && rank < BDAT_NRANKS)
	PHY_DATA_ENTRY(DFIMRL, dfimrl, BYTE, true)
#undef	PHY_DATA_ENTRY
	default:
		cmn_err(CE_WARN, "bdat_prd: unknown PHY data type: %u (%u)",
		    type, pde->zbepd_type);
		return (false);
	}
	VERIFY3P(dst, !=, NULL);

	if (scope != pde->zbepd_scope) {
		cmn_err(CE_WARN, "bdat_prd: unexpected scope for PHY data "
		    "type %u (%u): %u vs %u", type, pde->zbepd_type,
		    pde->zbepd_scope, scope);
		return (false);
	}

	if (size > max_size) {
		cmn_err(CE_WARN, "bdat_prd: unexpected size for PHY data "
		    "type %u (%u): %u x %u = %lu > %lu", type, pde->zbepd_type,
		    pde->zbepd_nelems, pde->zbepd_elems_size, size, max_size);
		return (false);
	}

	bcopy(pde->zbepd_data, dst, size);
	return (true);
}

static void
zen_bdat_ent_preserve_cb(const zen_bdat_entry_header_t *ent, void *arg)
{
	zen_bdat_rsrcs_t *rs = arg;

	switch (ent->zbe_schema) {
	case BDAT_DIMM_SPD_SCHEMA:
		if (ent->zbe_type != BDAT_DIMM_SPD_TYPE)
			goto unknown;

		rs->zbr_spd_rsrcs[rs->zbr_nspd_rsrcs++] = ent;
		break;
	case BDAT_MEM_TRAINING_DATA_SCHEMA:
		switch (ent->zbe_type) {
		/*
		 * We recognize but ignore these.
		 */
		case BDAT_MEM_TRAINING_DATA_CAPABILITIES_TYPE:
		case BDAT_MEM_TRAINING_DATA_RCD_REGS_TYPE:
			break;
		case BDAT_MEM_TRAINING_DATA_MODE_REGS_TYPE:
			rs->zbr_dmr_rsrcs[rs->zbr_ndmr_rsrcs++] = ent;
			break;
		case BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE:
			rs->zbr_rmargin_rsrcs[rs->zbr_nrmargin_rsrcs++] = ent;
			break;
		case BDAT_MEM_TRAINING_DATA_DQ_MARGIN_TYPE:
			rs->zbr_dmargin_rsrcs[rs->zbr_ndmargin_rsrcs++] = ent;
			break;
		case BDAT_MEM_TRAINING_DATA_PHY_TYPE:
			(void) zen_bdat_fill_phy_ent(rs,
			    (const zen_bdat_entry_phy_data_t *)ent->zbe_data);
			break;
		default:
			goto unknown;
		}
		break;
	default:
unknown:
		cmn_err(CE_WARN, "bdat_prd: skipping unknown BDAT entry "
		    "schema %u, type %u", ent->zbe_schema, ent->zbe_type);
		break;
	}
}

static void
bdat_prd_amdzen_direct_init(void)
{
	uint64_t start, end;
	const zen_bdat_header_t *bdat;
	zen_bdat_rsrcs_t *rsrcs = &bdat_prd_amdzen_rsrcs;

	/*
	 * If BDAT support is enabled, its physical address will have been
	 * pulled out of the system memory map and available as a boot prop.
	 * If we can't find it, we don't fail the module load, but any requests
	 * for BDAT data will return a not present error.
	 */
	start = ddi_prop_get_int64(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, BTPROP_NAME_BDAT_START, 0);
	end = ddi_prop_get_int64(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, BTPROP_NAME_BDAT_END, 0);
	if (start == 0 || end == 0) {
		return;
	}

	if (start >= end || (end - start) < BDAT_AREA_SIZE) {
		cmn_err(CE_WARN, "bdat_prd: paddr range invalid: 0x%lx-0x%lx",
		    start, end);
		return;
	}

	bdat = (const zen_bdat_header_t *)psm_map(start, BDAT_AREA_SIZE,
	    PSM_PROT_READ);
	if (bdat == NULL) {
		cmn_err(CE_WARN, "bdat_prd: failed to map BDAT");
		return;
	}

	/*
	 * We do a first pass to get a count of the entries of each type we care
	 * about so we can allocate space for them all at once.
	 */
	zen_bdat_walk_entries(bdat, zen_bdat_ent_counts_cb, rsrcs);

	if (rsrcs->zbr_nspd_rsrcs != 0) {
		rsrcs->zbr_spd_rsrcs = kmem_zalloc(rsrcs->zbr_nspd_rsrcs *
		    sizeof (zen_bdat_entry_header_t *), KM_SLEEP);
	}
	if (rsrcs->zbr_ndmr_rsrcs != 0) {
		rsrcs->zbr_dmr_rsrcs = kmem_zalloc(rsrcs->zbr_ndmr_rsrcs *
		    sizeof (zen_bdat_entry_header_t *), KM_SLEEP);
	}
	if (rsrcs->zbr_nrmargin_rsrcs != 0) {
		rsrcs->zbr_rmargin_rsrcs = kmem_zalloc(
		    rsrcs->zbr_nrmargin_rsrcs *
		    sizeof (zen_bdat_entry_header_t *), KM_SLEEP);
	}
	if (rsrcs->zbr_ndmargin_rsrcs != 0) {
		rsrcs->zbr_dmargin_rsrcs = kmem_zalloc(
		    rsrcs->zbr_ndmargin_rsrcs *
		    sizeof (zen_bdat_entry_header_t *), KM_SLEEP);
	}
	rsrcs->zbr_nphy_alloc = rsrcs->zbr_nphy_rsrcs;
	if (rsrcs->zbr_nphy_rsrcs != 0) {
		rsrcs->zbr_phy_rsrcs = kmem_zalloc(rsrcs->zbr_nphy_rsrcs *
		    sizeof (zen_bdat_phy_data_t), KM_SLEEP);
	}

	rsrcs->zbr_nspd_rsrcs = rsrcs->zbr_ndmr_rsrcs =
	    rsrcs->zbr_nrmargin_rsrcs = rsrcs->zbr_ndmargin_rsrcs =
	    rsrcs->zbr_nphy_rsrcs = 0;

	/*
	 * Now we walk the entries again, this time saving the pointers to the
	 * entries we care about.
	 */
	zen_bdat_walk_entries(bdat, zen_bdat_ent_preserve_cb, rsrcs);

	bdat_prd_amdzen_raw = bdat;
}

static struct modlmisc bdat_prd_modlmisc_amdzen_direct = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "BDAT Resource Discovery (AMD Zen)"
};

static struct modlinkage bdat_prd_modlinkage_amdzen_direct = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &bdat_prd_modlmisc_amdzen_direct, NULL }
};

int
_init(void)
{
	if (cpuid_getvendor(CPU) != X86_VENDOR_AMD) {
		return (ENOTSUP);
	}

	bdat_prd_amdzen_direct_init();

	return (mod_install(&bdat_prd_modlinkage_amdzen_direct));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&bdat_prd_modlinkage_amdzen_direct, modinfop));
}

int
_fini(void)
{
	zen_bdat_rsrcs_t *rsrcs = &bdat_prd_amdzen_rsrcs;
	if (bdat_prd_amdzen_raw != NULL) {
		if (rsrcs->zbr_spd_rsrcs != NULL) {
			kmem_free(rsrcs->zbr_spd_rsrcs, rsrcs->zbr_nspd_rsrcs *
			    sizeof (zen_bdat_entry_header_t *));
		}
		if (rsrcs->zbr_rmargin_rsrcs != NULL) {
			kmem_free(rsrcs->zbr_rmargin_rsrcs,
			    rsrcs->zbr_nrmargin_rsrcs *
			    sizeof (zen_bdat_entry_header_t *));
		}
		if (rsrcs->zbr_dmargin_rsrcs != NULL) {
			kmem_free(rsrcs->zbr_dmargin_rsrcs,
			    rsrcs->zbr_ndmargin_rsrcs *
			    sizeof (zen_bdat_entry_header_t *));
		}
		if (rsrcs->zbr_phy_rsrcs != NULL) {
			kmem_free(rsrcs->zbr_phy_rsrcs, rsrcs->zbr_nphy_alloc *
			    sizeof (zen_bdat_phy_data_t));
		}
		bzero(rsrcs, sizeof (*rsrcs));
		psm_unmap((caddr_t)bdat_prd_amdzen_raw, BDAT_AREA_SIZE);
		bdat_prd_amdzen_raw = NULL;
	}

	return (mod_remove(&bdat_prd_modlinkage_amdzen_direct));
}
