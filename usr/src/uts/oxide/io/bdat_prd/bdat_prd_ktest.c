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
 * This is a kernel test (ktest) module exercising the AMD Zen direct BDAT
 * parser (bdat_prd_amdzen_direct.c).  The BDAT is provided by system firmware
 * out of our control, so the parser must tolerate arbitrary malformed input
 * without hanging or writing out of bounds.  These tests feed hand-crafted BDAT
 * entries directly to the parser routines and confirm our boundary, index, and
 * allocation validation tests.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/ktest.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/amdzen/bdat.h>

#include "bdat_prd_impl.h"

/*
 * Signatures for the static parser routines we resolve out of the bdat_prd
 * module.
 */
typedef zen_bdat_entry_valid_t (*bdat_entry_valid_f)(
    const zen_bdat_entry_header_t *);
typedef void (*bdat_walk_entries_f)(const zen_bdat_header_t *, zen_bdat_cb_f,
    void *);
typedef bool (*bdat_fill_phy_ent_f)(zen_bdat_rsrcs_t *,
    const zen_bdat_entry_phy_data_t *);

/*
 * Scratch buffer large enough for any single crafted entry (entry header plus
 * a PHY data header and a modest payload).
 */
#define	BDAT_KT_ENT_BUFSZ	512

/*
 * Offset within the BDAT region at which the walk tests place a "decoy" block.
 */
#define	BDAT_KT_DECOY_OFF	0x1000

/*
 * Upper bound on entries a walk test should ever legitimately see.  If a
 * bug leaves the walk re-walking a block forever, the count callback uses this
 * to break the cycle (see bdat_kt_walk_count_cb) so the test fails cleanly
 * instead of hanging.
 */
#define	BDAT_KT_WALK_CAP	16

/*
 * For the entry-bounds tests, a poison entry is placed this many bytes from
 * the end of the region to exercise the walk's header/size checks.
 */
#define	BDAT_KT_END_WIN		0x100


/*
 * Verify that a region of `len` bytes starting at `buf` is entirely `val`.
 */
static boolean_t
bdat_kt_memcmp(const void *buf, uint8_t val, size_t len)
{
	const uint8_t *p = buf;

	for (size_t i = 0; i < len; i++) {
		if (p[i] != val)
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Verify that the body of a consolidated PHY record (everything past the
 * socket/channel/p-state identity bytes) is still the canary value, i.e. that
 * no PHY data was written into (or near) the record.
 */
static boolean_t
bdat_kt_rec_body_is(const zen_bdat_phy_data_t *rec, uint8_t val)
{
	const uint8_t *base = (const uint8_t *)rec;
	const uint8_t *body = (const uint8_t *)&rec->zbpd_csdly;
	const size_t off = (size_t)(body - base);

	return (bdat_kt_memcmp(body, val, sizeof (*rec) - off));
}

/*
 * Resolve the parser routines from the bdat_prd module. On success the caller
 * must release the returned handle with ktest_release_mod(). Any of the out
 * pointers may be NULL if that routine is not needed.
 */
static boolean_t
bdat_kt_get_fns(ktest_ctx_hdl_t *ctx, ddi_modhandle_t *hdlp,
    bdat_entry_valid_f *validp, bdat_walk_entries_f *walkp,
    bdat_fill_phy_ent_f *fillp)
{
	ddi_modhandle_t hdl = NULL;

	if (ktest_hold_mod("bdat_prd", &hdl) != 0) {
		KT_ERROR(ctx, "failed to hold 'bdat_prd' module");
		return (B_FALSE);
	}

	if (validp != NULL && ktest_get_fn(hdl, "zen_bdat_entry_valid",
	    (void **)validp) != 0) {
		KT_ERROR(ctx, "failed to resolve zen_bdat_entry_valid");
		ktest_release_mod(hdl);
		return (B_FALSE);
	}
	if (walkp != NULL && ktest_get_fn(hdl, "zen_bdat_walk_entries",
	    (void **)walkp) != 0) {
		KT_ERROR(ctx, "failed to resolve zen_bdat_walk_entries");
		ktest_release_mod(hdl);
		return (B_FALSE);
	}
	if (fillp != NULL && ktest_get_fn(hdl, "zen_bdat_fill_phy_ent",
	    (void **)fillp) != 0) {
		KT_ERROR(ctx, "failed to resolve zen_bdat_fill_phy_ent");
		ktest_release_mod(hdl);
		return (B_FALSE);
	}

	*hdlp = hdl;
	return (B_TRUE);
}

/*
 * Build a minimal, valid RANK_MARGIN entry at `buf` with the given location
 * selectors. Returns the total size of the entry in bytes.
 */
static size_t
bdat_kt_put_rank_margin(uint8_t *buf, uint8_t sock, uint8_t chan, uint8_t dimm,
    uint8_t rank)
{
	zen_bdat_entry_header_t *e = (zen_bdat_entry_header_t *)buf;
	zen_bdat_entry_rank_margin_t *rm;

	e->zbe_schema = BDAT_MEM_TRAINING_DATA_SCHEMA;
	e->zbe_type = BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE;
	e->zbe_size = sizeof (zen_bdat_entry_header_t) +
	    sizeof (zen_bdat_entry_rank_margin_t);

	rm = (zen_bdat_entry_rank_margin_t *)e->zbe_data;
	rm->zberm_loc.zbml_socket = sock;
	rm->zberm_loc.zbml_channel = chan;
	rm->zberm_loc.zbml_dimm = dimm;
	rm->zberm_loc.zbml_rank = rank;

	return (e->zbe_size);
}

/*
 * Build a PHY data payload (i.e. what ent->zbe_data points at for a PHY entry)
 * at `buf`.  `buf` must be at least BDAT_KT_ENT_BUFSZ bytes.
 */
static zen_bdat_entry_phy_data_t *
bdat_kt_put_phy(uint8_t *buf, uint8_t type, uint8_t scope, uint8_t pstate,
    uint8_t subchan, uint8_t dimm, uint8_t rank, uint8_t nelems,
    uint8_t elems_size, uint8_t data_fill)
{
	zen_bdat_entry_phy_data_t *pde = (zen_bdat_entry_phy_data_t *)buf;

	bzero(buf, BDAT_KT_ENT_BUFSZ);
	pde->zbepd_loc.zbml_sub_channel = subchan;
	pde->zbepd_loc.zbml_dimm = dimm;
	pde->zbepd_loc.zbml_rank = rank;
	pde->zbepd_type = type;
	pde->zbepd_scope = scope;
	pde->zbepd_pstate = pstate;
	pde->zbepd_nelems = nelems;
	pde->zbepd_elems_size = elems_size;
	memset(pde->zbepd_data, data_fill, (size_t)nelems * elems_size);

	return (pde);
}

/*
 * Callback used by the walk tests: count how many entries the walk hands us.
 *
 * If a bug leaves the walk spinning on a block forever, we don't want the test
 * thread to hang.  The callback will clear the block's signature (terminating
 * the walk) if the count exceeds a given threshold.
 */
typedef struct {
	uint_t			bkwc_count;
	uint_t			bkwc_cap;
	zen_bdat_header_t	*bkwc_block;
} bdat_kt_walk_ctr_t;

static void
bdat_kt_walk_count_cb(const zen_bdat_entry_header_t *ent __unused, void *arg)
{
	bdat_kt_walk_ctr_t *c = arg;

	c->bkwc_count++;

	if (c->bkwc_block != NULL && c->bkwc_cap != 0 &&
	    c->bkwc_count > c->bkwc_cap) {
		c->bkwc_block->zbh_signature = 0;
	}
}

/*
 * Common body for the walk-termination tests: build a single BDAT block with
 * two valid entries and the `next` value under test, plus a decoy block with
 * its own entries further along the region.  A correct walk visits only the two
 * primary entries and stops.
 */
static void
bdat_kt_walk_next(ktest_ctx_hdl_t *ctx, uint32_t next)
{
	ddi_modhandle_t hdl = NULL;
	bdat_walk_entries_f walk = NULL;
	uint8_t *bdat = NULL;
	bdat_kt_walk_ctr_t c = { 0 };
	zen_bdat_header_t *h;
	size_t off;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, &walk, NULL))
		return;

	bdat = kmem_zalloc(BDAT_AREA_SIZE, KM_SLEEP);

	/*
	 * Primary block: two entries and the zbh_next value under test.
	 */
	h = (zen_bdat_header_t *)bdat;
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = next;
	off = sizeof (zen_bdat_header_t);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 0, 0, 0);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 1, 0, 0);

	/*
	 * Decoy block further along, which none of the `next` values under
	 * test should legitimately reach.
	 */
	h = (zen_bdat_header_t *)(bdat + BDAT_KT_DECOY_OFF);
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = 0;
	off = BDAT_KT_DECOY_OFF + sizeof (zen_bdat_header_t);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 2, 0, 0);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 3, 0, 0);

	c.bkwc_count = 0;
	c.bkwc_cap = BDAT_KT_WALK_CAP;
	c.bkwc_block = (zen_bdat_header_t *)bdat;

	walk((const zen_bdat_header_t *)bdat, bdat_kt_walk_count_cb, &c);

	KT_ASSERT3UG(c.bkwc_count, ==, 2, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (bdat != NULL)
		kmem_free(bdat, BDAT_AREA_SIZE);
	ktest_release_mod(hdl);
}

/*
 * If we encounter `zbh_next == 0`, we must terminate the walk and not keep
 * going endlessly.
 */
static void
bdat_walk_next_zero_test(ktest_ctx_hdl_t *ctx)
{
	bdat_kt_walk_next(ctx, 0);
}

/*
 * A zbh_next smaller than a header (1..7) would point back into the current
 * header.  In that case, we must terminate the walk rather than advance to an
 * overlapping "next".
 */
static void
bdat_walk_next_subheader_test(ktest_ctx_hdl_t *ctx)
{
	bdat_kt_walk_next(ctx, sizeof (zen_bdat_header_t) - 1);
}

/*
 * A zbh_next that would place the next header at/after the end of the region
 * must terminate.
 */
static void
bdat_walk_next_overrun_test(ktest_ctx_hdl_t *ctx)
{
	bdat_kt_walk_next(ctx, BDAT_SIZE);
}

/*
 * We also want to confirm that we can walk a chained block each with their own
 * set of entries.
 */
static void
bdat_walk_chain_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_walk_entries_f walk = NULL;
	uint8_t *bdat = NULL;
	bdat_kt_walk_ctr_t c = { 0 };
	const uint32_t next = 0x1000;
	zen_bdat_header_t *h;
	size_t off;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, &walk, NULL))
		return;

	bdat = kmem_zalloc(BDAT_AREA_SIZE, KM_SLEEP);

	h = (zen_bdat_header_t *)bdat;
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = next;
	off = sizeof (zen_bdat_header_t);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 0, 0, 0);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 0, 1, 0);

	h = (zen_bdat_header_t *)(bdat + next);
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = 0;
	off = next + sizeof (zen_bdat_header_t);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 1, 0, 0);
	off += bdat_kt_put_rank_margin(bdat + off, 0, 1, 1, 0);

	c.bkwc_count = 0;
	c.bkwc_cap = BDAT_KT_WALK_CAP;
	c.bkwc_block = h;

	walk((const zen_bdat_header_t *)bdat, bdat_kt_walk_count_cb, &c);

	KT_ASSERT3UG(c.bkwc_count, ==, 4, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (bdat != NULL)
		kmem_free(bdat, BDAT_AREA_SIZE);
	ktest_release_mod(hdl);
}

/*
 * Common body for the inner entry-header bounds tests: we build a set of
 * entries near the end of the region, with a "poison" entry header at a chosen
 * offset.
 */
static void
bdat_kt_entry_near_end(ktest_ctx_hdl_t *ctx, uint16_t poison_off)
{
	ddi_modhandle_t hdl = NULL;
	bdat_walk_entries_f walk = NULL;
	uint8_t *bdat = NULL;
	bdat_kt_walk_ctr_t c = { 0 };
	const size_t endish = BDAT_SIZE - BDAT_KT_END_WIN;
	zen_bdat_header_t *h;
	zen_bdat_entry_header_t *ent;
	size_t off;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, &walk, NULL))
		return;

	bdat = kmem_zalloc(BDAT_AREA_SIZE, KM_SLEEP);

	/*
	 * Leading block with no entries of its own.
	 */
	h = (zen_bdat_header_t *)bdat;
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = endish;

	/*
	 * Another block just before the end of the region which contains:
	 * 1) a valid entry,
	 * 2) an entry with an unknown schema and size set so we walk to the
	 *    desired offset for the poison entry, and
	 * 3) a poison entry header.
	 */
	h = (zen_bdat_header_t *)(bdat + endish);
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = 0;
	off = endish + sizeof (zen_bdat_header_t);

	/* (1) valid entry */
	off += bdat_kt_put_rank_margin(bdat + off, 0, 0, 0, 0);

	/* (2) spacer entry */
	ent = (zen_bdat_entry_header_t *)(bdat + off);
	ent->zbe_schema = 0;
	ent->zbe_type = 0;
	ent->zbe_size = poison_off - (off - endish);
	off += ent->zbe_size;

	/* (3) poison entry header */
	ent = (zen_bdat_entry_header_t *)(bdat + off);
	ent->zbe_schema = BDAT_MEM_TRAINING_DATA_SCHEMA;
	ent->zbe_type = BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE;
	ent->zbe_size = sizeof (zen_bdat_entry_header_t) +
	    sizeof (zen_bdat_entry_rank_margin_t);

	c.bkwc_count = 0;
	c.bkwc_cap = BDAT_KT_WALK_CAP;
	c.bkwc_block = (zen_bdat_header_t *)(bdat + endish);

	walk((const zen_bdat_header_t *)bdat, bdat_kt_walk_count_cb, &c);

	/*
	 * Only the valid entry should have been counted.
	 */
	KT_ASSERT3UG(c.bkwc_count, ==, 1, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (bdat != NULL)
		kmem_free(bdat, BDAT_AREA_SIZE);
	ktest_release_mod(hdl);
}

/*
 * Test if the walk correctly stops when the next entry header would run past
 * the end of the region.
 */
static void
bdat_walk_entry_hdr_oob_test(ktest_ctx_hdl_t *ctx)
{
	bdat_kt_entry_near_end(ctx,
	    BDAT_KT_END_WIN - sizeof (zen_bdat_entry_header_t));
}

/*
 * Test if the walk correctly stops when the total size of the next entry would
 * run past the end of the region (but the header itself fits).
 */
static void
bdat_walk_entry_size_oob_test(ktest_ctx_hdl_t *ctx)
{
	bdat_kt_entry_near_end(ctx,
	    BDAT_KT_END_WIN - (sizeof (zen_bdat_entry_header_t) + 4));
}

/*
 * Test to make sure we stop walking when we encounter an entry with an invalid
 * size.
 */
static void
bdat_walk_entry_invalid_size_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_walk_entries_f walk = NULL;
	uint8_t *bdat = NULL;
	bdat_kt_walk_ctr_t c = { 0 };
	zen_bdat_entry_header_t *bad;
	zen_bdat_header_t *h;
	size_t off;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, &walk, NULL))
		return;

	bdat = kmem_zalloc(BDAT_AREA_SIZE, KM_SLEEP);

	h = (zen_bdat_header_t *)bdat;
	h->zbh_signature = BDAT_SIGNATURE;
	h->zbh_next = 0;
	off = sizeof (zen_bdat_header_t);

	/* A valid entry that should be counted. */
	off += bdat_kt_put_rank_margin(bdat + off, 0, 0, 0, 0);

	/*
	 * An entry with a bogus size that should terminate the walk.
	 */
	bad = (zen_bdat_entry_header_t *)(bdat + off);
	bad->zbe_schema = BDAT_MEM_TRAINING_DATA_SCHEMA;
	bad->zbe_type = BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE;
	bad->zbe_size = sizeof (zen_bdat_entry_header_t) - 1;
	off += sizeof (zen_bdat_entry_header_t);

	/*
	 * A valid entry beyond the bad one that must not be reached.
	 */
	(void) bdat_kt_put_rank_margin(bdat + off, 0, 1, 0, 0);

	c.bkwc_count = 0;
	c.bkwc_cap = BDAT_KT_WALK_CAP;
	c.bkwc_block = h;

	walk((const zen_bdat_header_t *)bdat, bdat_kt_walk_count_cb, &c);

	KT_ASSERT3UG(c.bkwc_count, ==, 1, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (bdat != NULL)
		kmem_free(bdat, BDAT_AREA_SIZE);
	ktest_release_mod(hdl);
}

/*
 * A valid entry validates as ENT_OK.
 */
static void
bdat_validate_ok_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_entry_valid_f valid = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];

	if (!bdat_kt_get_fns(ctx, &hdl, &valid, NULL, NULL))
		return;

	bzero(buf, sizeof (buf));
	(void) bdat_kt_put_rank_margin(buf, 0, 0, 0, 0);

	KT_ASSERT3SG(valid((const zen_bdat_entry_header_t *)buf), ==, ENT_OK,
	    ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	ktest_release_mod(hdl);
}

/*
 * An entry whose reported size can't even hold the entry header is rejected as
 * ENT_INVALID_SIZE.
 */
static void
bdat_validate_undersize_test(ktest_ctx_hdl_t *ctx)
{
	static const struct {
		uint8_t	schema;
		uint8_t	type;
	} schema_types[] = {
		{ BDAT_DIMM_SPD_SCHEMA, BDAT_DIMM_SPD_TYPE },
		{ BDAT_MEM_TRAINING_DATA_SCHEMA,
		    BDAT_MEM_TRAINING_DATA_CAPABILITIES_TYPE },
		{ BDAT_MEM_TRAINING_DATA_SCHEMA,
		    BDAT_MEM_TRAINING_DATA_MODE_REGS_TYPE },
		{ BDAT_MEM_TRAINING_DATA_SCHEMA,
		    BDAT_MEM_TRAINING_DATA_RCD_REGS_TYPE },
		{ BDAT_MEM_TRAINING_DATA_SCHEMA,
		    BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE },
		{ BDAT_MEM_TRAINING_DATA_SCHEMA,
		    BDAT_MEM_TRAINING_DATA_DQ_MARGIN_TYPE },
		{ BDAT_MEM_TRAINING_DATA_SCHEMA,
		    BDAT_MEM_TRAINING_DATA_PHY_TYPE },
	};
	ddi_modhandle_t hdl = NULL;
	bdat_entry_valid_f valid = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_header_t *e = (zen_bdat_entry_header_t *)buf;

	if (!bdat_kt_get_fns(ctx, &hdl, &valid, NULL, NULL))
		return;

	for (uint_t i = 0; i < ARRAY_SIZE(schema_types); i++) {
		zen_bdat_entry_valid_t res;

		bzero(buf, sizeof (buf));
		e->zbe_schema = schema_types[i].schema;
		e->zbe_type = schema_types[i].type;
		/*
		 * Deliberately too small to hold the entry header, so the
		 * parser must reject it.
		 */
		e->zbe_size = sizeof (zen_bdat_entry_header_t) - 1;

		res = valid(e);
		if (res != ENT_INVALID_SIZE) {
			KT_FAIL(ctx, "schema %u type %u: expected "
			    "ENT_INVALID_SIZE (%d), got %d",
			    schema_types[i].schema, schema_types[i].type,
			    ENT_INVALID_SIZE, res);
			goto cleanup;
		}
	}

	KT_PASS(ctx);
cleanup:
	ktest_release_mod(hdl);
}

/*
 * Each of the common selector indices out of range yields ENT_INVALID_INDEX.
 */
static void
bdat_validate_index_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_entry_valid_f valid = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_header_t *e = (zen_bdat_entry_header_t *)buf;

	if (!bdat_kt_get_fns(ctx, &hdl, &valid, NULL, NULL))
		return;

	bzero(buf, sizeof (buf));
	(void) bdat_kt_put_rank_margin(buf, BDAT_SOC_COUNT, 0, 0, 0);
	KT_ASSERT3SG(valid(e), ==, ENT_INVALID_INDEX, ctx, cleanup);

	bzero(buf, sizeof (buf));
	(void) bdat_kt_put_rank_margin(buf, 0, BDAT_NCHANS, 0, 0);
	KT_ASSERT3SG(valid(e), ==, ENT_INVALID_INDEX, ctx, cleanup);

	bzero(buf, sizeof (buf));
	(void) bdat_kt_put_rank_margin(buf, 0, 0, BDAT_NDIMMS, 0);
	KT_ASSERT3SG(valid(e), ==, ENT_INVALID_INDEX, ctx, cleanup);

	bzero(buf, sizeof (buf));
	(void) bdat_kt_put_rank_margin(buf, 0, 0, 0, BDAT_NRANKS);
	KT_ASSERT3SG(valid(e), ==, ENT_INVALID_INDEX, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	ktest_release_mod(hdl);
}

/*
 * A PHY entry's DIMM/rank/sub-channel are intentionally NOT checked in
 * zen_bdat_entry_valid() (the relevant type, and hence which indices matter,
 * isn't known until the quirk-aware fill pass).  A PHY entry with valid
 * socket/channel but wild DIMM/rank/sub-channel must therefore validate OK and
 * be caught later in zen_bdat_fill_phy_ent().
 */
static void
bdat_validate_phy_defers_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_entry_valid_f valid = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_header_t *e = (zen_bdat_entry_header_t *)buf;
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, &valid, NULL, NULL))
		return;

	bzero(buf, sizeof (buf));
	e->zbe_schema = BDAT_MEM_TRAINING_DATA_SCHEMA;
	e->zbe_type = BDAT_MEM_TRAINING_DATA_PHY_TYPE;
	e->zbe_size = sizeof (zen_bdat_entry_header_t) +
	    sizeof (zen_bdat_entry_phy_data_t) + 2;
	pde = (zen_bdat_entry_phy_data_t *)e->zbe_data;
	pde->zbepd_type = PDT_CLK_DLY;
	pde->zbepd_scope = PDS_PER_DIMM;
	pde->zbepd_pstate = PDP_0;
	pde->zbepd_nelems = 2;
	pde->zbepd_elems_size = 1;
	/* Deliberately out of range; must be ignored here. */
	pde->zbepd_loc.zbml_sub_channel = 0xff;
	pde->zbepd_loc.zbml_dimm = 0xff;
	pde->zbepd_loc.zbml_rank = 0xff;

	KT_ASSERT3SG(valid(e), ==, ENT_OK, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	ktest_release_mod(hdl);
}

/*
 * An unrecognized schema or type is reported as ENT_UNKNOWN.
 */
static void
bdat_validate_unknown_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_entry_valid_f valid = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_header_t *e = (zen_bdat_entry_header_t *)buf;

	if (!bdat_kt_get_fns(ctx, &hdl, &valid, NULL, NULL))
		return;

	bzero(buf, sizeof (buf));
	e->zbe_size = sizeof (zen_bdat_entry_header_t);

	/* Unknown schema. */
	e->zbe_schema = 0;
	KT_ASSERT3SG(valid(e), ==, ENT_UNKNOWN, ctx, cleanup);

	/* Known schema but unknown type. */
	e->zbe_schema = BDAT_MEM_TRAINING_DATA_SCHEMA;
	e->zbe_type = 0;
	KT_ASSERT3SG(valid(e), ==, ENT_UNKNOWN, ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	ktest_release_mod(hdl);
}

/*
 * Shared setup for the PHY fill tests: allocate `nrecs` consolidated records,
 * fill them (and the resource struct) with a canary, and hand back a resource
 * struct sized to allow `alloc` new records.
 */
static void
bdat_kt_fill_setup(zen_bdat_rsrcs_t *rs, zen_bdat_phy_data_t **recs,
    size_t nrecs, size_t alloc, uint8_t canary)
{
	*recs = kmem_alloc(nrecs * sizeof (zen_bdat_phy_data_t), KM_SLEEP);
	memset(*recs, canary, nrecs * sizeof (zen_bdat_phy_data_t));

	bzero(rs, sizeof (*rs));
	rs->zbr_phy_rsrcs = *recs;
	rs->zbr_nphy_alloc = alloc;
	rs->zbr_nphy_rsrcs = 0;
}

/*
 * An out-of-range sub-channel on a sub-channel-indexed type must be dropped
 * without writing into the record.
 */
static void
bdat_phyfill_subchan_oob_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_fill_phy_ent_f fill = NULL;
	zen_bdat_rsrcs_t rs;
	zen_bdat_phy_data_t *recs = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, NULL, &fill))
		return;

	bdat_kt_fill_setup(&rs, &recs, 1, 1, 0xaa);
	pde = bdat_kt_put_phy(buf, PDT_CS_DLY, PDS_PER_STROBE, PDP_0,
	    BDAT_NSUBCHANS, 0, 0, 4, 1, 0x5a);

	/*
	 * The fill function returns false if it dropped the entry, which is
	 * what we expect here.
	 */
	KT_ASSERTG_IMPL(fill(&rs, pde), ==, false, bool, ctx, cleanup);

	/*
	 * Which also implies that the record body is still the canary value.
	 */
	KT_ASSERTG(bdat_kt_rec_body_is(&recs[0], 0xaa), ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (recs != NULL)
		kmem_free(recs, sizeof (zen_bdat_phy_data_t));
	ktest_release_mod(hdl);
}

/*
 * In-range sub-channel writes into the expected field.
 */
static void
bdat_phyfill_subchan_ok_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_fill_phy_ent_f fill = NULL;
	zen_bdat_rsrcs_t rs;
	zen_bdat_phy_data_t *recs = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, NULL, &fill))
		return;

	bdat_kt_fill_setup(&rs, &recs, 1, 1, 0xaa);
	pde = bdat_kt_put_phy(buf, PDT_CS_DLY, PDS_PER_STROBE, PDP_0,
	    1, 0, 0, sizeof (recs[0].zbpd_csdly[1]), 1, 0x5a);

	/*
	 * The fill should succeed here.
	 */
	KT_ASSERTG_IMPL(fill(&rs, pde), ==, true, bool, ctx, cleanup);

	/*
	 * Meaning the record body is no longer the initial canary.
	 */
	KT_ASSERTG(bdat_kt_memcmp(&recs[0].zbpd_csdly[1], 0x5a,
	    sizeof (recs[0].zbpd_csdly[1])), ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (recs != NULL)
		kmem_free(recs, sizeof (zen_bdat_phy_data_t));
	ktest_release_mod(hdl);
}

/*
 * An out-of-range DIMM on a DIMM/rank-indexed type must be dropped without
 * writing into the record.
 */
static void
bdat_phyfill_dimmrank_oob_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_fill_phy_ent_f fill = NULL;
	zen_bdat_rsrcs_t rs;
	zen_bdat_phy_data_t *recs = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, NULL, &fill))
		return;

	bdat_kt_fill_setup(&rs, &recs, 1, 1, 0xaa);
	pde = bdat_kt_put_phy(buf, PDT_TX_DQ_DLY, PDS_PER_BIT, PDP_0,
	    0, BDAT_NDIMMS, 0, 4, 1, 0x5a);

	KT_ASSERTG_IMPL(fill(&rs, pde), ==, false, bool, ctx, cleanup);
	KT_ASSERTG(bdat_kt_rec_body_is(&recs[0], 0xaa), ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (recs != NULL)
		kmem_free(recs, sizeof (zen_bdat_phy_data_t));
	ktest_release_mod(hdl);
}

/*
 * With the BFQ_F_SKIP_VREFDAC23 quirk active, a raw type of PDT_DFE_TAP3 is
 * remapped to PDT_TX_DQ_DLY, which is DIMM/rank indexed.  The index bounds
 * check keys off the remapped type, so an out-of-range DIMM here must be
 * dropped rather than writing out of bounds.
 */
static void
bdat_phyfill_quirk_remap_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_fill_phy_ent_f fill = NULL;
	zen_bdat_rsrcs_t rs;
	zen_bdat_phy_data_t *recs = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, NULL, &fill))
		return;

	bdat_kt_fill_setup(&rs, &recs, 1, 1, 0xaa);
	rs.zbr_quirks |= BFQ_F_SKIP_VREFDAC23;
	pde = bdat_kt_put_phy(buf, PDT_DFE_TAP3, PDS_PER_BIT, PDP_0,
	    0, BDAT_NDIMMS, 0, 4, 1, 0x5a);

	KT_ASSERTG_IMPL(fill(&rs, pde), ==, false, bool, ctx, cleanup);
	KT_ASSERTG(bdat_kt_rec_body_is(&recs[0], 0xaa), ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (recs != NULL)
		kmem_free(recs, sizeof (zen_bdat_phy_data_t));
	ktest_release_mod(hdl);
}

/*
 * The flip side of the quirk remap: a raw PDT_RX_EN_DLY becomes PDT_DFIMRL
 * (which uses no DIMM/rank index), so a non-zero DIMM/rank must NOT cause the
 * entry to be dropped, and the payload must be written.  This guards against
 * over-rejecting legitimate old-firmware DFIMRL entries.
 */
static void
bdat_phyfill_quirk_dfimrl_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_fill_phy_ent_f fill = NULL;
	zen_bdat_rsrcs_t rs;
	zen_bdat_phy_data_t *recs = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, NULL, &fill))
		return;

	bdat_kt_fill_setup(&rs, &recs, 1, 1, 0xaa);
	rs.zbr_quirks |= BFQ_F_SKIP_VREFDAC23;
	pde = bdat_kt_put_phy(buf, PDT_RX_EN_DLY, PDS_PER_BYTE, PDP_0,
	    0, 1, 1, sizeof (recs[0].zbpd_dfimrl), 1, 0x5a);

	KT_ASSERTG_IMPL(fill(&rs, pde), ==, true, bool, ctx, cleanup);
	KT_ASSERTG(bdat_kt_memcmp(recs[0].zbpd_dfimrl, 0x5a,
	    sizeof (recs[0].zbpd_dfimrl)), ctx, cleanup);

	KT_PASS(ctx);
cleanup:
	if (recs != NULL)
		kmem_free(recs, sizeof (zen_bdat_phy_data_t));
	ktest_release_mod(hdl);
}

/*
 * If the fill pass would need more consolidated records than the counting pass
 * allocated, the extra entry must be dropped rather than written past the
 * allocation.  We allocate two records but tell the parser only one was sized,
 * then present two distinct (socket, channel, p-state) tuples.  The second must
 * not touch the second record.
 */
static void
bdat_phyfill_alloc_bound_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	bdat_fill_phy_ent_f fill = NULL;
	zen_bdat_rsrcs_t rs;
	zen_bdat_phy_data_t *recs = NULL;
	uint8_t buf[BDAT_KT_ENT_BUFSZ];
	zen_bdat_entry_phy_data_t *pde;

	if (!bdat_kt_get_fns(ctx, &hdl, NULL, NULL, &fill))
		return;

	bdat_kt_fill_setup(&rs, &recs, 2, 1, 0xaa);

	pde = bdat_kt_put_phy(buf, PDT_CLK_DLY, PDS_PER_DIMM, PDP_0,
	    0, 0, 0, sizeof (recs[0].zbpd_clkdly), 1, 0x5a);
	/*
	 * This first entry should succeed, write into the first record, and
	 * increment the count.
	 */
	KT_ASSERTG_IMPL(fill(&rs, pde), ==, true, bool, ctx, cleanup);
	KT_ASSERT3UG(rs.zbr_nphy_rsrcs, ==, 1, ctx, cleanup);

	/* A distinct p-state forces a request for a second record. */
	pde = bdat_kt_put_phy(buf, PDT_CLK_DLY, PDS_PER_DIMM, PDP_1,
	    0, 0, 0, sizeof (recs[0].zbpd_clkdly), 1, 0x5a);
	/*
	 * This second entry should fail because the allocation only allowed one
	 * record.
	 */
	KT_ASSERTG_IMPL(fill(&rs, pde), ==, false, bool, ctx, cleanup);
	KT_ASSERT3UG(rs.zbr_nphy_rsrcs, ==, 1, ctx, cleanup);
	KT_ASSERTG(bdat_kt_memcmp(&recs[1], 0xaa, sizeof (recs[1])), ctx,
	    cleanup);

	KT_PASS(ctx);
cleanup:
	if (recs != NULL)
		kmem_free(recs, 2 * sizeof (zen_bdat_phy_data_t));
	ktest_release_mod(hdl);
}

static struct modlmisc bdat_prd_ktest_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "AMD Zen BDAT parser test module"
};

static struct modlinkage bdat_prd_ktest_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &bdat_prd_ktest_modlmisc, NULL }
};

int
_init(void)
{
	ktest_module_hdl_t *km;
	ktest_suite_hdl_t *ks;
	int ret;

	VERIFY0(ktest_create_module("bdat_prd", &km));

	VERIFY0(ktest_add_suite(km, "walk", &ks));
	VERIFY0(ktest_add_test(ks, "next_zero", bdat_walk_next_zero_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "next_subheader",
	    bdat_walk_next_subheader_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "next_overrun", bdat_walk_next_overrun_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "chain", bdat_walk_chain_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "entry_hdr_oob",
	    bdat_walk_entry_hdr_oob_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "entry_size_oob",
	    bdat_walk_entry_size_oob_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "entry_invalid_size",
	    bdat_walk_entry_invalid_size_test, KTEST_FLAG_NONE));

	VERIFY0(ktest_add_suite(km, "validate", &ks));
	VERIFY0(ktest_add_test(ks, "ok", bdat_validate_ok_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "undersize", bdat_validate_undersize_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "index", bdat_validate_index_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "phy_defers", bdat_validate_phy_defers_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "unknown", bdat_validate_unknown_test,
	    KTEST_FLAG_NONE));

	VERIFY0(ktest_add_suite(km, "phyfill", &ks));
	VERIFY0(ktest_add_test(ks, "subchan_oob", bdat_phyfill_subchan_oob_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "subchan_ok", bdat_phyfill_subchan_ok_test,
	    KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "dimmrank_oob",
	    bdat_phyfill_dimmrank_oob_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "quirk_remap",
	    bdat_phyfill_quirk_remap_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "quirk_dfimrl",
	    bdat_phyfill_quirk_dfimrl_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "alloc_bound",
	    bdat_phyfill_alloc_bound_test, KTEST_FLAG_NONE));

	if ((ret = ktest_register_module(km)) != 0) {
		ktest_free_module(km);
		return (ret);
	}

	if ((ret = mod_install(&bdat_prd_ktest_modlinkage)) != 0) {
		ktest_unregister_module("bdat_prd");
		return (ret);
	}

	return (0);
}

int
_fini(void)
{
	ktest_unregister_module("bdat_prd");
	return (mod_remove(&bdat_prd_ktest_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&bdat_prd_ktest_modlinkage, modinfop));
}
