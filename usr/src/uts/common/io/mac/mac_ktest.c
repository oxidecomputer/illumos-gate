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
 * Copyright 2024 Ryan Zezeski
 */

/*
 * A test module for various mac routines.
 */
#include <inet/ip.h>
#include <inet/ip_impl.h>
#include <inet/udp_impl.h>
#include <sys/dlpi.h>
#include <sys/ethernet.h>
#include <sys/ktest.h>
#include <sys/mac_client.h>
#include <sys/mac_impl.h>
#include <sys/mac_provider.h>
#include <sys/pattr.h>
#include <sys/sdt.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/vxlan.h>

typedef int (*mac_partial_tun_info_t)(const mblk_t *, size_t,
    mac_ether_offload_info_t *);

/* Arbitrary limits for cksum tests */
#define	PADDING_MAX	32
#define	SPLITS_MAX	8

typedef struct emul_test_params {
	mblk_t		*etp_mp;
	uchar_t		*etp_raw;
	uint_t		etp_raw_sz;
	uchar_t		*etp_outputs;
	uint_t		etp_outputs_sz;
	boolean_t	etp_do_partial;
	boolean_t	etp_do_full;
	boolean_t	etp_do_ipv4;
	boolean_t	etp_do_lso;
	uint_t		etp_mss;
	uint_t		etp_splits[SPLITS_MAX];
} emul_test_params_t;

static void
etp_free(const emul_test_params_t *etp)
{
	if (etp->etp_mp != NULL) {
		freemsgchain(etp->etp_mp);
	}
	if (etp->etp_raw != NULL) {
		kmem_free(etp->etp_raw, etp->etp_raw_sz);
	}
	if (etp->etp_outputs != NULL) {
		kmem_free(etp->etp_outputs, etp->etp_outputs_sz);
	}
}

static mblk_t *
cksum_alloc_pkt(const emul_test_params_t *etp, uint32_t padding)
{
	uint32_t remain = etp->etp_raw_sz;
	uint_t split_idx = 0;
	const uint8_t *pkt_bytes = etp->etp_raw;

	mblk_t *head = NULL, *tail = NULL;
	while (remain > 0) {
		const boolean_t has_split = etp->etp_splits[split_idx] != 0;
		const uint32_t to_copy = has_split ?
		    MIN(remain, etp->etp_splits[split_idx]) : remain;
		const uint32_t to_alloc = padding + to_copy;

		mblk_t *mp = allocb(to_alloc, 0);
		if (mp == NULL) {
			freemsg(head);
			return (NULL);
		}
		if (head == NULL) {
			head = mp;
		}
		if (tail != NULL) {
			tail->b_cont = mp;
		}
		tail = mp;

		/* Pad the first mblk with zeros, if requested */
		if (padding != 0) {
			bzero(mp->b_rptr, padding);
			mp->b_rptr += padding;
			mp->b_wptr += padding;
			padding = 0;
		}

		bcopy(pkt_bytes, mp->b_rptr, to_copy);
		mp->b_wptr += to_copy;
		pkt_bytes += to_copy;
		remain -= to_copy;
		if (has_split) {
			split_idx++;
		}
	}
	return (head);
}

static boolean_t
emul_test_parse_input(ktest_ctx_hdl_t *ctx, emul_test_params_t *etp)
{
	uchar_t *bytes;
	size_t num_bytes = 0;

	ktest_get_input(ctx, &bytes, &num_bytes);
	bzero(etp, sizeof (*etp));

	nvlist_t *params = NULL;
	if (nvlist_unpack((char *)bytes, num_bytes, &params, KM_SLEEP) != 0) {
		KT_ERROR(ctx, "Invalid nvlist input");
		return (B_FALSE);
	}

	uchar_t *pkt_bytes, *out_pkt_bytes;
	uint_t pkt_sz, out_pkt_sz;

	if (nvlist_lookup_byte_array(params, "pkt_bytes", &pkt_bytes,
	    &pkt_sz) != 0) {
		KT_ERROR(ctx, "Input missing pkt_bytes field");
		goto bail;
	}
	if (pkt_sz == 0) {
		KT_ERROR(ctx, "Packet must not be 0-length");
		goto bail;
	}

	if (nvlist_lookup_byte_array(params, "out_pkt_bytes", &out_pkt_bytes,
	    &out_pkt_sz) == 0) {
		if (out_pkt_sz < sizeof (uint32_t)) {
			KT_ERROR(ctx, "Serialized packets need a u32 length");
			goto bail;
		}
		etp->etp_outputs = kmem_alloc(out_pkt_sz, KM_SLEEP);
		bcopy(out_pkt_bytes, etp->etp_outputs, out_pkt_sz);
		etp->etp_outputs_sz = out_pkt_sz;
	}

	(void) nvlist_lookup_uint32(params, "mss", &etp->etp_mss);

	uint32_t padding = 0;
	(void) nvlist_lookup_uint32(params, "padding", &padding);
	if (padding & 1) {
		KT_ERROR(ctx, "padding must be even");
		goto bail;
	} else if (padding > PADDING_MAX) {
		KT_ERROR(ctx, "padding greater than max of %u", PADDING_MAX);
		goto bail;
	}

	etp->etp_do_ipv4 = fnvlist_lookup_boolean(params, "cksum_ipv4");
	etp->etp_do_partial = fnvlist_lookup_boolean(params, "cksum_partial");
	etp->etp_do_full = fnvlist_lookup_boolean(params, "cksum_full");

	uint32_t *splits;
	uint_t nsplits;
	if (nvlist_lookup_uint32_array(params, "cksum_splits", &splits,
	    &nsplits) == 0) {
		if (nsplits > SPLITS_MAX) {
			KT_ERROR(ctx, "Too many splits requested");
			goto bail;
		}
		for (uint_t i = 0; i < nsplits; i++) {
			if (splits[i] == 0) {
				KT_ERROR(ctx, "Splits should not be 0");
				goto bail;
			} else if (splits[i] & 1) {
				KT_ERROR(ctx, "Splits must be 2-byte aligned");
				goto bail;
			}
			etp->etp_splits[i] = splits[i];
		}
	}

	if (etp->etp_do_partial && etp->etp_do_full) {
		KT_ERROR(ctx, "Cannot request full and partial cksum");
		goto bail;
	}

	etp->etp_raw = kmem_alloc(pkt_sz, KM_SLEEP);
	bcopy(pkt_bytes, etp->etp_raw, pkt_sz);
	etp->etp_raw_sz = pkt_sz;

	etp->etp_mp = cksum_alloc_pkt(etp, padding);
	if (etp->etp_mp == NULL) {
		KT_ERROR(ctx, "Could not allocate mblk");
		goto bail;
	}

	nvlist_free(params);
	return (B_TRUE);

bail:
	etp_free(etp);

	if (params != NULL) {
		nvlist_free(params);
	}
	return (B_FALSE);
}

/* Calculate pseudo-header checksum for a packet */
static uint16_t
cksum_calc_pseudo(ktest_ctx_hdl_t *ctx, const uint8_t *pkt_data,
    const mac_ether_offload_info_t *meoi, boolean_t exclude_len)
{
	if ((meoi->meoi_flags & MEOI_L4INFO_SET) == 0) {
		KT_ERROR(ctx, "MEOI lacks L4 info");
		return (0);
	}

	const uint16_t *iphs = (const uint16_t *)(pkt_data + meoi->meoi_l2hlen);
	uint32_t cksum = 0;

	/* Copied from ip_input_cksum_pseudo_v[46]() */
	if (meoi->meoi_l3proto == ETHERTYPE_IP) {
		cksum += iphs[6] + iphs[7] + iphs[8] + iphs[9];
	} else if (meoi->meoi_l3proto == ETHERTYPE_IPV6) {
		cksum += iphs[4] + iphs[5] + iphs[6] + iphs[7] +
		    iphs[8] + iphs[9] + iphs[10] + iphs[11] +
		    iphs[12] + iphs[13] + iphs[14] + iphs[15] +
		    iphs[16] + iphs[17] + iphs[18] + iphs[19];
	} else {
		KT_ERROR(ctx, "unexpected proto %u", meoi->meoi_l3proto);
		return (0);
	}

	switch (meoi->meoi_l4proto) {
	case IPPROTO_TCP:
		cksum += IP_TCP_CSUM_COMP;
		break;
	case IPPROTO_UDP:
		cksum += IP_UDP_CSUM_COMP;
		break;
	case IPPROTO_ICMPV6:
		cksum += IP_ICMPV6_CSUM_COMP;
		break;
	default:
		KT_ERROR(ctx, "unexpected L4 proto %u", meoi->meoi_l4proto);
		return (0);
	}

	uint16_t ulp_len =
	    meoi->meoi_len - ((uint16_t)meoi->meoi_l2hlen + meoi->meoi_l3hlen);
	if (meoi->meoi_l3proto == ETHERTYPE_IP) {
		/*
		 * IPv4 packets can fall below the 60-byte minimum for ethernet,
		 * resulting in padding which makes the "easy" means of
		 * determining ULP length potentially inaccurate.
		 *
		 * Reach into the v4 header to make that calculation.
		 */
		const ipha_t *ipha =
		    (const ipha_t *)(pkt_data + meoi->meoi_l2hlen);
		ulp_len = ntohs(ipha->ipha_length) - meoi->meoi_l3hlen;
	}

	/* LSO packets omit ULP length from cksum since it may be changing */
	if (!exclude_len) {
		cksum += htons(ulp_len);
	}

	cksum = (cksum >> 16) + (cksum & 0xffff);
	cksum = (cksum >> 16) + (cksum & 0xffff);
	return (cksum);
}

/*
 * Overwrite 2 bytes in mblk at given offset.
 *
 * Assumes:
 * - offset is 2-byte aligned
 * - mblk(s) in chain reference memory which is 2-byte aligned
 * - offset is within mblk chain
 */
static void
mblk_write16(mblk_t *mp, uint_t off, uint16_t val)
{
	VERIFY(mp != NULL);
	VERIFY3U(off & 1, ==, 0);
	VERIFY3U(off + 2, <=, msgdsize(mp));

	while (off >= MBLKL(mp)) {
		off -= MBLKL(mp);
		mp = mp->b_cont;
		VERIFY(mp != NULL);
	}

	uint16_t *datap = (uint16_t *)(mp->b_rptr + off);
	*datap = val;
}

/* Compare an individual mblk with known good value in test parameters.  */
static boolean_t
pkt_compare(ktest_ctx_hdl_t *ctx, const uchar_t *buf, const uint_t len,
    mblk_t *mp)
{
	if (msgdsize(mp) != len) {
		KT_FAIL(ctx, "mp size %u != %u", msgdsize(mp), len);
		return (B_FALSE);
	}

	uint32_t fail_val = 0, good_val = 0;
	uint_t mp_off = 0, fail_len = 0, i;
	for (i = 0; i < len; i++) {
		/*
		 * If we encounter a mismatch, collect up to 4 bytes of context
		 * to print with the failure.
		 */
		if (mp->b_rptr[mp_off] != buf[i] || fail_len != 0) {
			fail_val |= mp->b_rptr[mp_off] << (fail_len * 8);
			good_val |= buf[i] << (fail_len * 8);

			fail_len++;
			if (fail_len == 4) {
				break;
			}
		}

		mp_off++;
		if (mp_off == MBLKL(mp)) {
			mp = mp->b_cont;
			mp_off = 0;
		}
	}

	if (fail_len != 0) {
		KT_FAIL(ctx, "mp[%02X] %08X != %08X", (i - fail_len),
		    fail_val, good_val);
		return (B_FALSE);
	}

	return (B_TRUE);
}

/* Compare resulting mblk chain with known good values in test parameters. */
static boolean_t
pkt_result_compare_chain(ktest_ctx_hdl_t *ctx, const emul_test_params_t *etp,
    mblk_t *mp)
{
	uint_t remaining = etp->etp_outputs_sz;
	const uchar_t *raw_cur = etp->etp_outputs;

	uint_t idx = 0;
	while (remaining != 0 && mp != NULL) {
		uint32_t inner_pkt_len;
		if (remaining < sizeof (inner_pkt_len)) {
			KT_ERROR(ctx, "insufficient bytes to read packet len");
			return (B_FALSE);
		}
		bcopy(raw_cur, &inner_pkt_len, sizeof (inner_pkt_len));
		remaining -= sizeof (inner_pkt_len);
		raw_cur += sizeof (inner_pkt_len);

		if (remaining < inner_pkt_len) {
			KT_ERROR(ctx, "wanted %u bytes to read packet, had %u",
			    inner_pkt_len, remaining);
			return (B_FALSE);
		}

		if (!pkt_compare(ctx, raw_cur, inner_pkt_len, mp)) {
			ktest_msg_prepend(ctx, "packet %u: ", idx);
			return (B_FALSE);
		}

		remaining -= inner_pkt_len;
		raw_cur += inner_pkt_len;
		idx++;
		mp = mp->b_next;
	}

	if (remaining != 0) {
		KT_FAIL(ctx, "fewer packets returned than expected");
		return (B_FALSE);
	}

	if (mp != NULL) {
		KT_FAIL(ctx, "more packets returned than expected");
		return (B_FALSE);
	}

	return (B_TRUE);
}

static void
mac_hw_emul_test(ktest_ctx_hdl_t *ctx, emul_test_params_t *etp)
{
	mblk_t *mp = etp->etp_mp;

	mac_ether_offload_info_t meoi;
	mac_ether_offload_info(mp, &meoi, NULL);

	if ((meoi.meoi_flags & MEOI_L3INFO_SET) == 0 ||
	    (meoi.meoi_l3proto != ETHERTYPE_IP &&
	    meoi.meoi_l3proto != ETHERTYPE_IPV6)) {
		KT_SKIP(ctx, "l3 protocol not recognized/supported");
		return;
	}

	mac_emul_t emul_flags = 0;
	uint_t hck_flags = 0, hck_start = 0, hck_stuff = 0, hck_end = 0;

	if (etp->etp_do_lso) {
		emul_flags |= MAC_LSO_EMUL;
		hck_flags |= HW_LSO;
		if (etp->etp_mss == 0) {
			KT_ERROR(ctx, "invalid MSS for LSO");
			return;
		}
	}

	if (meoi.meoi_l3proto == ETHERTYPE_IP && etp->etp_do_ipv4) {
		mblk_write16(mp,
		    meoi.meoi_l2hlen + offsetof(ipha_t, ipha_hdr_checksum), 0);
		emul_flags |= MAC_IPCKSUM_EMUL;
		hck_flags |= HCK_IPV4_HDRCKSUM;
	}

	const boolean_t do_l4 = etp->etp_do_partial || etp->etp_do_full;
	if ((meoi.meoi_flags & MEOI_L4INFO_SET) != 0 && do_l4) {
		boolean_t skip_pseudo = B_FALSE;
		hck_start = meoi.meoi_l2hlen + meoi.meoi_l3hlen;
		hck_stuff = hck_start;
		hck_end = meoi.meoi_len;

		switch (meoi.meoi_l4proto) {
		case IPPROTO_TCP:
			hck_stuff += TCP_CHECKSUM_OFFSET;
			break;
		case IPPROTO_UDP:
			hck_stuff += UDP_CHECKSUM_OFFSET;
			break;
		case IPPROTO_ICMP:
			hck_stuff += ICMP_CHECKSUM_OFFSET;
			/*
			 * ICMP does not include the pseudo-header content in
			 * its checksum, but we can still do a partial with that
			 * field cleared.
			 */
			skip_pseudo = B_TRUE;
			break;
		case IPPROTO_ICMPV6:
			hck_stuff += ICMPV6_CHECKSUM_OFFSET;
			break;
		case IPPROTO_SCTP:
			/*
			 * Only full checksums are supported for SCTP, and the
			 * test logic for clearing the existing sum needs to
			 * account for its increased width.
			 */
			hck_stuff += SCTP_CHECKSUM_OFFSET;
			if (etp->etp_do_full) {
				mblk_write16(mp, hck_stuff, 0);
				mblk_write16(mp, hck_stuff + 2, 0);
			} else {
				KT_SKIP(ctx,
				    "Partial L4 cksum not supported for SCTP");
				return;
			}
			break;
		default:
			KT_SKIP(ctx,
			    "Partial L4 cksum not supported for proto");
			return;
		}

		emul_flags |= MAC_HWCKSUM_EMUL;
		if (etp->etp_do_partial) {
			hck_flags |= HCK_PARTIALCKSUM;
			if (!skip_pseudo) {
				/* Populate L4 pseudo-header cksum */
				const uint16_t pcksum = cksum_calc_pseudo(ctx,
				    etp->etp_raw, &meoi, etp->etp_do_lso);
				mblk_write16(mp, hck_stuff, pcksum);
			} else {
				mblk_write16(mp, hck_stuff, 0);
			}
		} else {
			hck_flags |= HCK_FULLCKSUM;
			/* Zero out the L4 cksum */
			mblk_write16(mp, hck_stuff, 0);
		}
	}
	if (do_l4 && (hck_flags & (HCK_FULLCKSUM|HCK_PARTIALCKSUM)) == 0) {
		KT_SKIP(ctx, "L4 checksum not supported for packet");
		return;
	}

	if (emul_flags != 0) {
		if ((hck_flags & HCK_PARTIALCKSUM) == 0) {
			hck_start = hck_stuff = hck_end = 0;
		} else {
			/*
			 * The offsets for mac_hcksum_set are all relative to
			 * the start of the L3 header.  Prior to here, these
			 * values were relative to the start of the packet.
			 */
			hck_start -= meoi.meoi_l2hlen;
			hck_stuff -= meoi.meoi_l2hlen;
			hck_end -= meoi.meoi_l2hlen;
		}
		/* Set hcksum information on all mblks in chain */
		for (mblk_t *cmp = mp; cmp != NULL; cmp = cmp->b_cont) {
			mac_hcksum_set(cmp, hck_start, hck_stuff, hck_end, 0,
			    hck_flags & HCK_FLAGS);
			lso_info_set(cmp, etp->etp_mss,
			    hck_flags & HW_LSO_FLAGS);
		}

		mac_hw_emul(&mp, NULL, NULL, emul_flags);
		KT_ASSERT3P(mp, !=, NULL, ctx);
		etp->etp_mp = mp;

		boolean_t success = (etp->etp_outputs == NULL) ?
		    pkt_compare(ctx, etp->etp_raw, etp->etp_raw_sz, mp) :
		    pkt_result_compare_chain(ctx, etp, mp);
		if (!success) {
			return;
		}
	} else {
		KT_SKIP(ctx, "offloads unsupported for packet");
		return;
	}

	KT_PASS(ctx);
}

/*
 * Verify checksum emulation against an arbitrary chain of packets.  If the
 * packet is of a supported protocol, any L3 and L4 checksums are cleared, and
 * then mac_hw_emul() is called to perform the offload emulation.  Afterwards,
 * the packet is compared to see if it equals the input, which is assumed to
 * have correct checksums.
 */
static void
mac_sw_cksum_test(ktest_ctx_hdl_t *ctx)
{
	emul_test_params_t etp;
	if (!emul_test_parse_input(ctx, &etp)) {
		goto cleanup;
	}

	mac_hw_emul_test(ctx, &etp);

cleanup:
	etp_free(&etp);
}

/*
 * Verify mac_sw_lso() (and checksum) emulation against an arbitrary input
 * packet.  This test functions like mac_sw_cksum_test insofar as checksums can
 * be customised, but also sets HW_LSO on any input packet, and compares the
 * outputs against a mandatory chain of packets provided by the caller.
 */
static void
mac_sw_lso_test(ktest_ctx_hdl_t *ctx)
{
	emul_test_params_t etp;
	if (!emul_test_parse_input(ctx, &etp)) {
		goto cleanup;
	}

	if (etp.etp_mss == 0) {
		KT_ERROR(ctx, "invalid MSS for LSO");
		goto cleanup;
	}

	if (etp.etp_outputs == NULL) {
		KT_ERROR(ctx, "LSO tests require explicit packet list");
		goto cleanup;
	}

	etp.etp_do_lso = B_TRUE;

	mac_hw_emul_test(ctx, &etp);

cleanup:
	etp_free(&etp);
}

typedef struct meoi_test_params {
	mblk_t				*mtp_mp;
	mac_ether_offload_info_t	mtp_partial;
	mac_ether_offload_info_t	mtp_results;
	uint_t				mtp_offset;
} meoi_test_params_t;

static void
nvlist_to_meoi(nvlist_t *results, mac_ether_offload_info_t *meoi)
{
	uint64_t u64_val;
	int int_val;
	uint16_t u16_val;
	uint8_t u8_val;

	bzero(meoi, sizeof (*meoi));
	if (nvlist_lookup_int32(results, "meoi_flags", &int_val) == 0) {
		meoi->meoi_flags = int_val;
	}
	if (nvlist_lookup_uint64(results, "meoi_len", &u64_val) == 0) {
		meoi->meoi_len = u64_val;
	}
	if (nvlist_lookup_uint8(results, "meoi_l2hlen", &u8_val) == 0) {
		meoi->meoi_l2hlen = u8_val;
	}
	if (nvlist_lookup_uint16(results, "meoi_l3proto", &u16_val) == 0) {
		meoi->meoi_l3proto = u16_val;
	}
	if (nvlist_lookup_uint16(results, "meoi_l3hlen", &u16_val) == 0) {
		meoi->meoi_l3hlen = u16_val;
	}
	if (nvlist_lookup_uint8(results, "meoi_l4proto", &u8_val) == 0) {
		meoi->meoi_l4proto = u8_val;
	}
	if (nvlist_lookup_uint8(results, "meoi_l4hlen", &u8_val) == 0) {
		meoi->meoi_l4hlen = u8_val;
	}
}

static mblk_t *
alloc_split_pkt(ktest_ctx_hdl_t *ctx, nvlist_t *nvl, const char *pkt_field)
{
	uchar_t *pkt_bytes;
	uint_t pkt_sz;

	if (nvlist_lookup_byte_array(nvl, pkt_field, &pkt_bytes,
	    &pkt_sz) != 0) {
		KT_ERROR(ctx, "Input missing %s field", pkt_field);
		return (NULL);
	}

	const uint32_t *splits = NULL;
	uint_t num_splits = 0;
	(void) nvlist_lookup_uint32_array(nvl, "splits", (uint32_t **)&splits,
	    &num_splits);

	uint_t split_idx = 0;
	mblk_t *result = NULL, *tail = NULL;

	do {
		uint_t block_sz = pkt_sz;
		if (split_idx < num_splits) {
			block_sz = MIN(block_sz, splits[split_idx]);
		}

		mblk_t *mp = allocb(block_sz, 0);
		if (mp == NULL) {
			KT_ERROR(ctx, "mblk alloc failure");
			freemsg(result);
			return (NULL);
		}

		if (result == NULL) {
			result = mp;
		} else {
			tail->b_cont = mp;
		}
		tail = mp;

		if (block_sz != 0) {
			bcopy(pkt_bytes, mp->b_wptr, block_sz);
			mp->b_wptr += block_sz;
		}
		pkt_sz -= block_sz;
		pkt_bytes += block_sz;
		split_idx++;
	} while (pkt_sz > 0);

	return (result);
}

/*
 * mac_ether_offload_info tests expect the following as input (via packed
 * nvlist)
 *
 * - pkt_bytes (byte array): packet bytes to parse
 * - splits (uint32 array, optional): byte sizes to split packet into mblks
 * - results (nvlist): mac_ether_offload_info result struct to compare
 *   - Field names and types should match those in the mac_ether_offload_info
 *     struct. Any fields not specified will be assumed to be zero.
 *
 * For mac_partial_offload_info tests, two additional fields are parsed:
 *
 * - offset (uint32, optional): offset into the packet at which the parsing
 *   should begin
 * - partial (nvlist): mac_ether_offload_info input struct to be used as
 *   starting point for partial parsing
 */
static boolean_t
meoi_test_parse_input(ktest_ctx_hdl_t *ctx, meoi_test_params_t *mtp,
    boolean_t test_partial)
{
	uchar_t *bytes;
	size_t num_bytes = 0;

	ktest_get_input(ctx, &bytes, &num_bytes);
	bzero(mtp, sizeof (*mtp));

	nvlist_t *params = NULL;
	if (nvlist_unpack((char *)bytes, num_bytes, &params, KM_SLEEP) != 0) {
		KT_ERROR(ctx, "Invalid nvlist input");
		return (B_FALSE);
	}

	nvlist_t *results;
	if (nvlist_lookup_nvlist(params, "results", &results) != 0) {
		KT_ERROR(ctx, "Input missing results field");
		nvlist_free(params);
		return (B_FALSE);
	}

	if (test_partial) {
		nvlist_t *partial;
		if (nvlist_lookup_nvlist(params, "partial", &partial) != 0) {
			KT_ERROR(ctx, "Input missing partial field");
			nvlist_free(params);
			return (B_FALSE);
		} else {
			nvlist_to_meoi(partial, &mtp->mtp_partial);
		}

		(void) nvlist_lookup_uint32(params, "offset", &mtp->mtp_offset);
	}

	mtp->mtp_mp = alloc_split_pkt(ctx, params, "pkt_bytes");
	if (mtp->mtp_mp == NULL) {
		nvlist_free(params);
		return (B_FALSE);
	}

	nvlist_to_meoi(results, &mtp->mtp_results);

	nvlist_free(params);
	return (B_TRUE);
}

void
mac_ether_offload_info_test(ktest_ctx_hdl_t *ctx)
{
	meoi_test_params_t mtp = { 0 };

	if (!meoi_test_parse_input(ctx, &mtp, B_FALSE)) {
		return;
	}

	/*
	 * Part of the contract with this function today is that it will
	 * zero-fill any unused fields -- the test data we receive into
	 * `expect` accounts for this.
	 *
	 * Initialise the struct with garbage data to be certain that this
	 * contract is upheld.
	 */
	mac_ether_offload_info_t result = {
		.meoi_flags = 0xbadd,
		.meoi_tuntype = 0xcafe,
		.meoi_len = 0xbadd,
		.meoi_l2hlen = 0xba,
		.meoi_l3proto = 0xcafe,
		.meoi_l3hlen = 0xbadd,
		.meoi_l4proto = 0xca,
		.meoi_l4hlen = 0xfe,
		.meoi_tunhlen = 0xbadd,
	};
	mac_ether_offload_info(mtp.mtp_mp, &result, NULL);

	const mac_ether_offload_info_t *expect = &mtp.mtp_results;
	KT_ASSERT3UG(result.meoi_flags, ==, expect->meoi_flags, ctx, done);
	KT_ASSERT3UG(result.meoi_l2hlen, ==, expect->meoi_l2hlen, ctx, done);
	KT_ASSERT3UG(result.meoi_l3proto, ==, expect->meoi_l3proto, ctx, done);
	KT_ASSERT3UG(result.meoi_l3hlen, ==, expect->meoi_l3hlen, ctx, done);
	KT_ASSERT3UG(result.meoi_l4proto, ==, expect->meoi_l4proto, ctx, done);
	KT_ASSERT3UG(result.meoi_l4hlen, ==, expect->meoi_l4hlen, ctx, done);

	KT_ASSERT3UG(result.meoi_tuntype, ==, METT_NONE, ctx, done);
	KT_ASSERT3UG(result.meoi_tunhlen, ==, 0, ctx, done);

	KT_PASS(ctx);

done:
	freemsg(mtp.mtp_mp);
}

void
mac_partial_offload_info_test(ktest_ctx_hdl_t *ctx)
{
	meoi_test_params_t mtp = { 0 };

	if (!meoi_test_parse_input(ctx, &mtp, B_TRUE)) {
		return;
	}

	mac_ether_offload_info_t *result = &mtp.mtp_partial;
	mac_partial_offload_info(mtp.mtp_mp, mtp.mtp_offset, result);

	const mac_ether_offload_info_t *expect = &mtp.mtp_results;
	KT_ASSERT3UG(result->meoi_flags, ==, expect->meoi_flags, ctx, done);
	KT_ASSERT3UG(result->meoi_l2hlen, ==, expect->meoi_l2hlen, ctx, done);
	KT_ASSERT3UG(result->meoi_l3proto, ==, expect->meoi_l3proto, ctx, done);
	KT_ASSERT3UG(result->meoi_l3hlen, ==, expect->meoi_l3hlen, ctx, done);
	KT_ASSERT3UG(result->meoi_l4proto, ==, expect->meoi_l4proto, ctx, done);
	KT_ASSERT3UG(result->meoi_l4hlen, ==, expect->meoi_l4hlen, ctx, done);

	KT_ASSERT3UG(result->meoi_tuntype, ==, METT_NONE, ctx, done);
	KT_ASSERT3UG(result->meoi_tunhlen, ==, 0, ctx, done);

	KT_PASS(ctx);

done:
	freemsg(mtp.mtp_mp);
}

typedef struct ether_test_params {
	mblk_t		*etp_mp;
	uint32_t	etp_tci;
	uint8_t		etp_dstaddr[ETHERADDRL];
	boolean_t	etp_is_err;
} ether_test_params_t;

/*
 * mac_ether_l2_info tests expect the following as input (via packed nvlist)
 *
 * - pkt_bytes (byte array): packet bytes to parse
 * - splits (uint32 array, optional): byte sizes to split packet into mblks
 * - tci (uint32): VLAN TCI result value to compare
 * - dstaddr (byte array): MAC addr result value to compare
 * - is_err (boolean): if test function should return error
 */
static boolean_t
ether_parse_input(ktest_ctx_hdl_t *ctx, ether_test_params_t *etp)
{
	uchar_t *bytes;
	size_t num_bytes = 0;

	ktest_get_input(ctx, &bytes, &num_bytes);
	bzero(etp, sizeof (*etp));

	nvlist_t *params = NULL;
	if (nvlist_unpack((char *)bytes, num_bytes, &params, KM_SLEEP) != 0) {
		KT_ERROR(ctx, "Invalid nvlist input");
		return (B_FALSE);
	}

	etp->etp_mp = alloc_split_pkt(ctx, params, "pkt_bytes");
	if (etp->etp_mp == NULL) {
		nvlist_free(params);
		return (B_FALSE);
	}

	if (nvlist_lookup_uint32(params, "tci", &etp->etp_tci) != 0) {
		KT_ERROR(ctx, "Input missing tci field");
		nvlist_free(params);
		return (B_FALSE);
	}

	uchar_t *dstaddr;
	uint_t dstaddr_sz;
	if (nvlist_lookup_byte_array(params, "dstaddr", &dstaddr,
	    &dstaddr_sz) != 0) {
		KT_ERROR(ctx, "Input missing dstaddr field");
		nvlist_free(params);
		return (B_FALSE);
	} else if (dstaddr_sz != ETHERADDRL) {
		KT_ERROR(ctx, "bad dstaddr size %u != %u", dstaddr_sz,
		    ETHERADDRL);
		nvlist_free(params);
		return (B_FALSE);
	}
	bcopy(dstaddr, &etp->etp_dstaddr, ETHERADDRL);

	etp->etp_is_err = nvlist_lookup_boolean(params, "is_err") == 0;

	nvlist_free(params);
	return (B_TRUE);
}

void
mac_ether_l2_info_test(ktest_ctx_hdl_t *ctx)
{
	ether_test_params_t etp = { 0 };

	if (!ether_parse_input(ctx, &etp)) {
		return;
	}

	uint8_t dstaddr[ETHERADDRL];
	uint32_t vlan_tci = 0;
	const boolean_t is_err =
	    !mac_ether_l2_info(etp.etp_mp, dstaddr, &vlan_tci);

	KT_ASSERTG(is_err == etp.etp_is_err, ctx, done);
	KT_ASSERTG(bcmp(dstaddr, etp.etp_dstaddr, ETHERADDRL) == 0, ctx,
	    done);
	KT_ASSERT3UG(vlan_tci, ==, etp.etp_tci, ctx, done);

	KT_PASS(ctx);

done:
	freemsg(etp.etp_mp);
}

/*
 * Allocate 2B extra length on an Ethernet frame to allow us to set up
 * 4B-alignment for all subsequent headers. The rptr must be moved forward by
 * 2 bytes to compensate.
 */
#define	ETHALIGN(len)	(2 + (len))

static uint32_t
mt_pseudo_sum(const uint8_t proto, ipha_t *ip)
{
	const uint32_t ip_hdr_sz = IPH_HDR_LENGTH(ip);
	const ipaddr_t src = ip->ipha_src;
	const ipaddr_t dst = ip->ipha_dst;
	uint16_t len;
	uint32_t sum = 0;

	switch (proto) {
	case IPPROTO_TCP:
		sum = IP_TCP_CSUM_COMP;
		break;

	case IPPROTO_UDP:
		sum = IP_UDP_CSUM_COMP;
		break;
	}

	len = ntohs(ip->ipha_length) - ip_hdr_sz;
	sum += (dst >> 16) + (dst & 0xFFFF) + (src >> 16) + (src & 0xFFFF);
	sum += htons(len);
	return (sum);
}

/*
 * An implementation of the internet checksum inspired by RFC 1071.
 * This implementation is as naive as possible. It serves as the
 * reference point for testing the optimized versions in the rest of
 * our stack. This is no place for optimization or cleverness.
 *
 * Arguments
 *
 *     initial: The initial sum value.
 *
 *     addr: Pointer to the beginning of the byte stream to sum.
 *
 *     len: The number of bytes to sum.
 *
 * Return
 *
 *     The resulting internet checksum.
 */
static uint32_t
mt_rfc1071_sum(uint32_t initial, uint16_t *addr, size_t len)
{
	uint32_t sum = initial;

	while (len > 1) {
		sum += *addr;
		addr++;
		len -= 2;
	}

	if (len == 1) {
		sum += *((uint8_t *)addr);
	}

	while ((sum >> 16) != 0) {
		sum = (sum >> 16) + (sum & 0xFFFF);
	}

	return (~sum & 0xFFFF);
}

static uint32_t
mt_pseudo6_sum(ip6_t *ip)
{
	/* Simplifying assumption: no EHs, 16-bit paylen (no jumboframe) */
	uint32_t sum = ~mt_rfc1071_sum(0, ip->ip6_src.s6_addr16,
	    sizeof (struct in6_addr) << 1) & 0xFFFF;
	uint16_t remainder[3] = {
		/* plen is already BE, nxt is a u8 shifted to last byte */
		ip->ip6_plen, 0, htons((uint16_t)ip->ip6_nxt)
	};

	return (~mt_rfc1071_sum(sum, remainder, sizeof (remainder)) & 0xFFFF);
}

/*
 * Fill out a basic TCP header in the given mblk at the given offset.
 * A TCP header should never straddle an mblk boundary.
 */
static tcpha_t *
mt_tcp_basic_hdr(mblk_t *mp, uint16_t offset, uint16_t lport, uint16_t fport,
    uint32_t seq, uint32_t ack, uint8_t flags, uint16_t win)
{
	tcpha_t *tcp = (tcpha_t *)(mp->b_rptr + offset);

	VERIFY3U((uintptr_t)tcp + sizeof (*tcp), <=, mp->b_wptr);
	tcp->tha_lport = htons(lport);
	tcp->tha_fport = htons(fport);
	tcp->tha_seq = htonl(seq);
	tcp->tha_ack = htonl(ack);
	tcp->tha_offset_and_reserved = 0x5 << 4;
	tcp->tha_flags = flags;
	tcp->tha_win = htons(win);
	tcp->tha_sum = 0x0;
	tcp->tha_urp = 0x0;

	return (tcp);
}

/*
 * Fill out a basic UDP header in the given mblk at the given offset.
 * A UDP header should never straddle an mblk boundary.
 */
static udpha_t *
mt_udp_basic_hdr(mblk_t *mp, uint16_t offset, uint16_t sport, uint16_t dport,
    uint16_t data_len)
{
	udpha_t *udp = (udpha_t *)(mp->b_rptr + offset);

	VERIFY3U((uintptr_t)udp + sizeof (*udp), <=, mp->b_wptr);
	udp->uha_src_port = htons(sport);
	udp->uha_dst_port = htons(dport);
	udp->uha_length = htons(sizeof (*udp) + data_len);
	udp->uha_checksum = 0;

	return (udp);
}

static ipha_t *
mt_ipv4_simple_hdr(mblk_t *mp, uint16_t offset, uint16_t datum_length,
    uint16_t ident, uint8_t proto, char *src, char *dst, boolean_t do_csum)
{
	uint32_t srcaddr, dstaddr;
	ipha_t *ip = (ipha_t *)(mp->b_rptr + offset);

	VERIFY3U((uintptr_t)ip + sizeof (*ip), <=, mp->b_wptr);

	VERIFY(inet_pton(AF_INET, src, &srcaddr));
	VERIFY(inet_pton(AF_INET, dst, &dstaddr));
	ip->ipha_version_and_hdr_length = IP_SIMPLE_HDR_VERSION;
	ip->ipha_type_of_service = 0x0;
	ip->ipha_length = htons(sizeof (*ip) + datum_length);
	ip->ipha_ident = htons(ident);
	ip->ipha_fragment_offset_and_flags = IPH_DF_HTONS;
	ip->ipha_ttl = 255;
	ip->ipha_protocol = proto;
	ip->ipha_hdr_checksum = 0x0;
	ip->ipha_src = srcaddr;
	ip->ipha_dst = dstaddr;

	if (do_csum)
		ip->ipha_hdr_checksum = ip_csum_hdr(ip);

	return (ip);
}

static ip6_t *
mt_ipv6_simple_hdr(mblk_t *mp, uint16_t offset, uint16_t datum_length,
    uint8_t proto, char *src, char *dst)
{
	ip6_t *ip = (ip6_t *)(mp->b_rptr + offset);

	VERIFY3U((uintptr_t)ip + sizeof (*ip), <=, mp->b_wptr);

	bzero(ip, sizeof (*ip));
	ip->ip6_vfc = 6 << 4;
	ip->ip6_plen = htons(datum_length);
	ip->ip6_nxt = proto;
	ip->ip6_hops = 255;
	VERIFY(inet_pton(AF_INET6, src, &ip->ip6_src));
	VERIFY(inet_pton(AF_INET6, dst, &ip->ip6_dst));

	return (ip);
}

static struct ether_header *
mt_ether_hdr(mblk_t *mp, uint16_t offset, char *dst, char *src, uint16_t etype)
{
	char *byte = dst;
	unsigned long tmp;
	struct ether_header *eh = (struct ether_header *)(mp->b_rptr + offset);

	VERIFY3U((uintptr_t)eh + sizeof (*eh), <=, mp->b_wptr);

	/* No strtok in these here parts. */
	for (uint_t i = 0; i < 6; i++) {
		char *end = strchr(byte, ':');
		VERIFY(i == 5 || end != NULL);
		VERIFY0(ddi_strtoul(byte, NULL, 16, &tmp));
		VERIFY3U(tmp, <=, 255);
		eh->ether_dhost.ether_addr_octet[i] = tmp;
		byte = end + 1;
	}

	byte = src;
	for (uint_t i = 0; i < 6; i++) {
		char *end = strchr(byte, ':');
		VERIFY(i == 5 || end != NULL);
		VERIFY0(ddi_strtoul(byte, NULL, 16, &tmp));
		VERIFY3U(tmp, <=, 255);
		eh->ether_shost.ether_addr_octet[i] = tmp;
		byte = end + 1;
	}

	eh->ether_type = htons(etype);
	return (eh);
}

#define	GENEVE_PORT	6081
#define	GENEVE_OPTCLASS_EXPERIMENT_START	0xFF00
#define	GENEVE_OPTCLASS_EXPERIMENT_END		0xFFFF

struct geneveh {
	uint8_t gh_vers_opt;
	uint8_t gh_flags;

	uint16_t gh_pdutype;
	uint8_t gh_vni[3];
	uint8_t gh_rsvd;
};

struct geneve_exth {
	uint16_t ghe_optclass;
	uint8_t ghe_opttype;
	uint8_t ghe_flags_len;
};

/*
 * Fill out VXLAN header in the given mblk at the given offset.
 */
static vxlan_hdr_t *
mt_vxlan_hdr(mblk_t *mp, uint16_t offset, uint32_t vni)
{
	vxlan_hdr_t *vxl = (vxlan_hdr_t *)(mp->b_rptr + offset);
	VERIFY3U((uintptr_t)vxl + sizeof (*vxl), <=, mp->b_wptr);

	bzero(vxl, sizeof (*vxl));
	vxl->vxlan_flags = htonl(VXLAN_F_VDI);
	vxl->vxlan_id = htonl(vni << VXLAN_ID_SHIFT);

	return (vxl);
}

/*
 * Fill out a basic Geneve header in the given mblk at the given offset,
 * inserting an optional extension header to fill the required length.
 *
 * optlen MUST be divisible by 4.
 */
static struct geneveh *
mt_geneve_basic_hdr(mblk_t *mp, uint16_t offset, uint32_t vni, uint16_t optlen)
{
	struct geneveh *gen = (struct geneveh *)(mp->b_rptr + offset);

	VERIFY3U((uintptr_t)gen + sizeof (*gen) + optlen, <=, mp->b_wptr);
	VERIFY0(optlen % 4);

	bzero(gen, sizeof (*gen) + optlen);
	vni &= 0xffffff;
	gen->gh_vers_opt = (uint8_t)(optlen >> 2);
	/* Assumption -- we'll be tunneling Ethernet in these tests */
	gen->gh_pdutype = htons(ETHERTYPE_TRANSETHER);
	gen->gh_vni[0] = (uint8_t)vni;
	gen->gh_vni[1] = (uint8_t)(vni >> 8);
	gen->gh_vni[2] = (uint8_t)(vni >> 16);

	if (optlen != 0) {
		struct geneve_exth *ext = (struct geneve_exth *)(gen + 1);
		optlen -= sizeof (struct geneve_exth);

		ext->ghe_optclass = htons(GENEVE_OPTCLASS_EXPERIMENT_START);
		ext->ghe_opttype = 0xff;
		ext->ghe_flags_len = (uint8_t)(optlen >> 2);
	}

	return (gen);
}

#define	TUN_IPV4_ID_OUTER	12000
#define	TUN_IPV4_ID_INNER	410

/*
 * Push an (unparsed) tunnel layer in front of existing packet facts.
 * Preserves the current packet info.
 *
 * Fails if the packet is already tunneled.
 */
static int
mac_ether_push_tun(mblk_t *pkt, mac_ether_tun_type_t ty)
{
	dblk_t *db = pkt->b_datap;

	if (db->db_pktinfo.t_tuntype != METT_NONE)
		return (-1);

	db->db_pktinfo.t_tuntype = ty;
	db->db_pktinfo.t_flags = 0;

	return (0);
}

/*
 * Generates an encapsulated packet having the given inner/outer/tun protocols
 * and payload length.
 */
static mblk_t *
mt_generate_tunpkt(uint16_t outer_l3ty, uint8_t tuntype, uint8_t tunoptlen,
    uint16_t inner_l3ty, uint8_t inner_l4ty, uint16_t paylen, uint16_t mss,
    uint32_t off_flags)
{
	mblk_t *mp = NULL;
	size_t encap_sz = sizeof (struct ether_header);
	size_t inner_sz = encap_sz;

	/*
	 * For simplicity, allocate enough space for the largest permutation
	 * of options we can admit.
	 */
	mp = allocb(ETHALIGN(sizeof (struct ether_header) + sizeof (ip6_t) +
	    sizeof (udpha_t) + sizeof (struct geneveh) + tunoptlen), 0);
	if (mp == NULL)
		return (NULL);
	mp->b_rptr += 2;
	mp->b_wptr = mp->b_datap->db_lim;

	mp->b_cont = allocb(ETHALIGN(sizeof (struct ether_header) +
	    sizeof (ip6_t) + sizeof (tcpha_t) + paylen), 0);
	if (mp->b_cont == NULL)
		goto bail;
	mp->b_cont->b_rptr += 2;
	mp->b_cont->b_wptr = mp->b_cont->b_datap->db_lim;

	/* build inner */
	(void) mt_ether_hdr(mp->b_cont, 0, "aa:aa:aa:aa:aa:aa",
	    "cc:cc:cc:cc:cc:cc", inner_l3ty);
	switch (inner_l3ty) {
	case ETHERTYPE_IP:
		(void) mt_ipv4_simple_hdr(mp->b_cont, inner_sz, paylen +
		    ((inner_l4ty == IPPROTO_TCP) ? sizeof (tcpha_t) :
		    sizeof (udpha_t)), TUN_IPV4_ID_INNER, inner_l4ty,
		    "172.30.0.5", "172.40.0.6", B_FALSE);
		inner_sz += sizeof (ipha_t);
		break;
	case ETHERTYPE_IPV6:
		(void) mt_ipv6_simple_hdr(mp->b_cont, inner_sz, paylen +
		    ((inner_l4ty == IPPROTO_TCP) ? sizeof (tcpha_t) :
		    sizeof (udpha_t)), inner_l4ty, "fd12::1", "fd12::2");
		inner_sz += sizeof (ip6_t);
		break;
	default:
		goto bail;
	}
	switch (inner_l4ty) {
	case IPPROTO_TCP:
		(void) mt_tcp_basic_hdr(mp->b_cont, inner_sz, 80, 49999, 1, 166,
		    0, 32000);
		inner_sz += sizeof (tcpha_t);
		break;
	case IPPROTO_UDP:
		(void) mt_udp_basic_hdr(mp->b_cont, inner_sz, 0xabcd, 53,
		    paylen);
		inner_sz += sizeof (udpha_t);
		break;
	default:
		goto bail;
	}

	/* Fill body with u16s up til len */
	for (uint16_t i = 0; i < (paylen >> 1); ++i) {
		uint16_t *wr = (uint16_t *)(mp->b_cont->b_rptr + inner_sz +
		    (i << 1));
		*wr = htons(i);
	}

	inner_sz += paylen;
	mp->b_cont->b_wptr = mp->b_cont->b_rptr + inner_sz;

	/* build outer */
	(void) mt_ether_hdr(mp, 0, "f2:35:c2:72:26:57", "92:ce:5a:29:46:9d",
	    outer_l3ty);
	switch (outer_l3ty) {
	case ETHERTYPE_IP:
		(void) mt_ipv4_simple_hdr(mp, encap_sz, inner_sz +
		    sizeof (udpha_t) + sizeof (struct geneveh) + tunoptlen,
		    TUN_IPV4_ID_OUTER, IPPROTO_UDP, "192.168.2.4",
		    "192.168.2.5", B_TRUE);
		encap_sz += sizeof (ipha_t);
		break;
	case ETHERTYPE_IPV6:
		(void) mt_ipv6_simple_hdr(mp, encap_sz, inner_sz +
		    sizeof (udpha_t) + sizeof (struct geneveh) + tunoptlen,
		    IPPROTO_UDP, "2001:db8::1", "2001:db8::2");
		encap_sz += sizeof (ip6_t);
		break;
	default:
		goto bail;
	}

	switch (tuntype) {
	case METT_NONE:
	default:
		goto bail;
	case METT_GENEVE:
		if ((tunoptlen % 4) != 0)
			goto bail;
		(void) mt_udp_basic_hdr(mp, encap_sz, 0xff11, GENEVE_PORT,
		    inner_sz + sizeof (struct geneveh) + tunoptlen);
		encap_sz += sizeof (udpha_t);
		(void) mt_geneve_basic_hdr(mp, encap_sz, 7777, tunoptlen);
		encap_sz += sizeof (struct geneveh) + tunoptlen;
		break;
	case METT_VXLAN:
		if (tunoptlen != 0)
			goto bail;
		(void) mt_udp_basic_hdr(mp, encap_sz, 0xff11, VXLAN_UDP_PORT,
		    inner_sz + sizeof (vxlan_hdr_t));
		encap_sz += sizeof (udpha_t);
		(void) mt_vxlan_hdr(mp, encap_sz, 7777);
		encap_sz += sizeof (vxlan_hdr_t);
		break;
	}
	mp->b_wptr = mp->b_rptr + encap_sz;

	if (mac_ether_push_tun(mp, tuntype) != 0)
		goto bail;

	DB_LSOFLAGS(mp) = off_flags;
	DB_LSOMSS(mp) = mss;

	return (mp);
bail:
	if (mp != NULL)
		freemsg(mp);
	return (NULL);
}

enum mt_cksum_state {
	MT_CSUM_NONE	= 0,
	MT_CSUM_INNER	= 1,
	MT_CSUM_OUTER	= 2
};

/*
 * Verifies that a chain of tunneled packets produced by LSO emulation
 * (mac_hw_emul) have correct contents and lengths recorded.
 *
 * Frees and unsets mp in all cases, and returns 0 on success.
 */
static int
mt_verify_tunlso(ktest_ctx_hdl_t *ctx, mblk_t **mp, size_t mss,
    size_t non_bodylen, size_t bodylen, mac_ether_tun_type_t tuntype,
    boolean_t outer_is_v4, uint8_t tunoptlen, enum mt_cksum_state csum_check)
{
	size_t i = 0;
	int err = -1;
	KT_EASSERT3UG(tuntype, !=, METT_NONE, ctx, cleanup);

	for (mblk_t *curr = *mp; curr != NULL; curr = curr->b_next, ++i) {
		mblk_t *body = curr->b_cont;
		boolean_t last = curr->b_next == NULL;
		uint32_t encap_len = 0;
		size_t cut_bodylen = last ? (bodylen % mss) : mss;

		mac_ether_offload_info_t outer_info, inner_info;
		ip6_t *oip6 = NULL;
		ipha_t *oip4 = NULL, *iip = NULL;
		udpha_t *ol4 = NULL;
		uint16_t ol4_len;
		tcpha_t *il4 = NULL;
		int err;

		/*
		 * structure of each frame is a pullup of all non-body, then
		 * body seg
		 */
		KT_ASSERT3UG(MBLKL(curr), ==, non_bodylen, ctx, cleanup);
		KT_ASSERT3PG(body, !=, NULL, ctx, cleanup);
		KT_ASSERT3UG(MBLKL(body), ==, cut_bodylen, ctx, cleanup);
		KT_ASSERT3PG(body->b_cont, ==, NULL, ctx, cleanup);

		/* Force a full reparse. */
		mac_ether_clear_pktinfo(curr);
		err = mac_ether_push_tun(curr, tuntype);
		KT_ASSERT3SG(err, ==, 0, ctx, cleanup);
		mac_ether_offload_info(curr, &outer_info, &inner_info);

		KT_ASSERT3UG(outer_info.meoi_flags, ==, MEOI_FULLTUN, ctx,
		    cleanup);
		KT_ASSERT3UG(outer_info.meoi_tuntype, ==, tuntype, ctx,
		    cleanup);
		KT_ASSERT3UG(outer_info.meoi_l2hlen, ==,
		    sizeof (struct ether_header), ctx, cleanup);
		KT_ASSERT3UG(outer_info.meoi_l3proto, ==,
		    outer_is_v4 ? ETHERTYPE_IP : ETHERTYPE_IPV6, ctx, cleanup);
		KT_ASSERT3UG(outer_info.meoi_l3hlen, ==,
		    outer_is_v4 ? sizeof (ipha_t) : sizeof (ip6_t), ctx,
		    cleanup);
		KT_ASSERT3UG(outer_info.meoi_l4proto, ==, IPPROTO_UDP, ctx,
		    cleanup);
		KT_ASSERT3UG(outer_info.meoi_l4hlen, ==, sizeof (udpha_t), ctx,
		    cleanup);
		switch (tuntype) {
		case METT_VXLAN:
			KT_EASSERT3UG(tunoptlen, ==, 0, ctx, cleanup);
			KT_ASSERT3UG(outer_info.meoi_tunhlen, ==,
			    sizeof (vxlan_hdr_t), ctx, cleanup);
			break;
		case METT_GENEVE:
			KT_ASSERT3UG(outer_info.meoi_tunhlen, ==,
			    sizeof (struct geneveh) + tunoptlen, ctx, cleanup);
			break;
		default:
			KT_ERROR(ctx, "unrecognised tunnel type");
		}

		KT_ASSERT3UG(inner_info.meoi_flags, ==, MEOI_FULL, ctx,
		    cleanup);
		KT_ASSERT3UG(inner_info.meoi_tuntype, ==, METT_NONE, ctx,
		    cleanup);
		KT_ASSERT3UG(inner_info.meoi_l2hlen, ==,
		    sizeof (struct ether_header), ctx, cleanup);
		KT_ASSERT3UG(inner_info.meoi_l3proto, ==, ETHERTYPE_IP, ctx,
		    cleanup);
		KT_ASSERT3UG(inner_info.meoi_l3hlen, ==, sizeof (ipha_t), ctx,
		    cleanup);
		KT_ASSERT3UG(inner_info.meoi_l4proto, ==, IPPROTO_TCP, ctx,
		    cleanup);
		KT_ASSERT3UG(inner_info.meoi_l4hlen, ==, sizeof (tcpha_t), ctx,
		    cleanup);

		encap_len = outer_info.meoi_l2hlen + outer_info.meoi_l3hlen +
		    outer_info.meoi_l4hlen + outer_info.meoi_tunhlen;

		DTRACE_PROBE4(mac__test__verpkt, mblk_t *, curr,
		    size_t, i, mac_ether_offload_info_t *, &outer_info,
		    mac_ether_offload_info_t *, &inner_info);

		switch (outer_info.meoi_l3proto) {
		case ETHERTYPE_IP:
			oip4 = (ipha_t *)(curr->b_rptr +
			    outer_info.meoi_l2hlen);
			KT_ASSERT3UG(ntohs(oip4->ipha_length), ==, non_bodylen -
			    sizeof (struct ether_header) + cut_bodylen, ctx,
			    cleanup);
			KT_ASSERT3UG(ip_csum_hdr(oip4), ==, 0, ctx, cleanup);
			ol4_len = ntohs(oip4->ipha_length) - sizeof (*oip4);
			KT_ASSERT3UG(ntohs(oip4->ipha_ident), ==,
			    TUN_IPV4_ID_OUTER + i, ctx, cleanup);
			break;
		case ETHERTYPE_IPV6:
			oip6 = (ip6_t *)(curr->b_rptr + outer_info.meoi_l2hlen);
			KT_ASSERT3UG(ntohs(oip6->ip6_plen), ==, non_bodylen -
			    sizeof (struct ether_header) - sizeof (*oip6) +
			    cut_bodylen, ctx, cleanup);
			ol4_len = ntohs(oip6->ip6_plen);
			break;
		default:
			KT_ERROR(ctx, "cannot handle non-IP L3");
			goto cleanup;
		}
		ol4 = (udpha_t *)(curr->b_rptr + outer_info.meoi_l2hlen +
		    outer_info.meoi_l3hlen);
		iip = (ipha_t *)(curr->b_rptr + encap_len +
		    inner_info.meoi_l2hlen);
		il4 = (tcpha_t *)((void *)iip + inner_info.meoi_l3hlen);

		KT_ASSERT3UG(ntohs(iip->ipha_ident), ==,
		    TUN_IPV4_ID_INNER + i, ctx, cleanup);

		if (csum_check & MT_CSUM_OUTER) {
			uint16_t ocsum = ol4->uha_checksum;
			uint32_t sum = 0;
			if (outer_info.meoi_l3proto == ETHERTYPE_IP)
				sum = mt_pseudo_sum(IPPROTO_UDP, oip4);
			else if (outer_info.meoi_l3proto == ETHERTYPE_IPV6)
				sum = mt_pseudo6_sum(oip6);
			ol4->uha_checksum = 0;
			sum = ~mt_rfc1071_sum(sum, (uint16_t *)ol4,
			    (void *) curr->b_wptr - (void *)ol4) & 0xFFFF;
			sum = mt_rfc1071_sum(sum & 0xFFFF,
			    (uint16_t *)body->b_rptr, cut_bodylen);
			DTRACE_PROBE4(mac__test__verpkt__sum, mblk_t *, curr,
			    size_t, i, uint32_t, ocsum, uint32_t, sum);
			KT_ASSERT3UG(ocsum, ==, sum, ctx, cleanup);
		} else {
			KT_ASSERT3UG(ol4->uha_checksum, ==, 0, ctx, cleanup);
		}

		if (csum_check & MT_CSUM_INNER) {
			uint16_t ocsum = il4->tha_sum;
			uint32_t sum = mt_pseudo_sum(IPPROTO_TCP, iip);
			il4->tha_sum = 0;
			sum = ~mt_rfc1071_sum(sum, (uint16_t *)il4,
			    (void *)curr->b_wptr - (void *)il4) & 0xFFFF;
			sum = mt_rfc1071_sum(sum, (uint16_t *)body->b_rptr,
			    cut_bodylen);
			DTRACE_PROBE4(mac__test__verpkt__sum, mblk_t *, curr,
			    size_t, i, uint32_t, ocsum, uint32_t, sum);
			KT_ASSERT3UG(ocsum, ==, sum, ctx, cleanup);

			KT_ASSERT3UG(ip_csum_hdr(iip), ==, 0, ctx,
			    cleanup);
		} else {
			KT_ASSERT3UG(il4->tha_sum, ==, 0, ctx, cleanup);
			KT_ASSERT3UG(iip->ipha_hdr_checksum, ==, 0, ctx,
			    cleanup);
		}

		KT_ASSERT3UG(ntohs(ol4->uha_length), ==, ol4_len, ctx, cleanup);
		KT_ASSERT3UG(ntohs(iip->ipha_length), ==, cut_bodylen +
		    sizeof (*iip) + sizeof (*il4), ctx, cleanup);
	}
	err = 0;

cleanup:
	freemsgchain(*mp);
	*mp = NULL;
	return (err);
}

/*
 * Verifies that checksum emulation can correctly fill inner and/or
 * outer checksums for IPv4 (TCP, UDP) traffic carried over a tunnel.
 */
void
mac_sw_cksum_tun_ipv4_test(ktest_ctx_hdl_t *ctx)
{
	/*
	 * Note that this test exclusively uses Geneve traffic.
	 * The LSO and tun_info tests fully test encap length detection --
	 * mac_hw_emul internally uses the same routine to determine offsets.
	 */
	mblk_t *mp = NULL, *mp2 = NULL;
	size_t non_bodylen = 0;
	size_t bodylen = 1200;
	size_t mss = 1448;

	/* IPv4 outer, IPv4 inner, inner csum only */
	mp = mt_generate_tunpkt(ETHERTYPE_IP, METT_GENEVE, 0, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HCK_IPV4_HDRCKSUM);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	/* Get this packet into the same format as LSO packets */
	mp2 = msgpullup(mp, non_bodylen);
	KT_EASSERT3PG(mp2, !=, NULL, ctx, cleanup);

	if (mt_verify_tunlso(ctx, &mp2, mss, non_bodylen, bodylen, METT_GENEVE,
	    B_TRUE, 0, MT_CSUM_INNER))
		goto cleanup;

	freemsgchain(mp);

	/* IPv4 outer, IPv4 inner, inner and outer csums */
	mp = mt_generate_tunpkt(ETHERTYPE_IP, METT_GENEVE, 0, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HCK_IPV4_HDRCKSUM | HCK_FULLCKSUM);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	mp2 = msgpullup(mp, non_bodylen);
	KT_EASSERT3PG(mp2, !=, NULL, ctx, cleanup);

	if (mt_verify_tunlso(ctx, &mp2, mss, non_bodylen, bodylen, METT_GENEVE,
	    B_TRUE, 0, MT_CSUM_OUTER | MT_CSUM_INNER))
		goto cleanup;

	freemsgchain(mp);

	/* IPv6 outer, IPv4 inner, inner csum only */
	mp = mt_generate_tunpkt(ETHERTYPE_IPV6, METT_GENEVE, 0, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	mp2 = msgpullup(mp, non_bodylen);
	KT_EASSERT3PG(mp2, !=, NULL, ctx, cleanup);

	if (mt_verify_tunlso(ctx, &mp2, mss, non_bodylen, bodylen, METT_GENEVE,
	    B_FALSE, 0, MT_CSUM_INNER))
		goto cleanup;

	freemsgchain(mp);

	/* IPv6 outer, IPv4 inner, inner and outer csums */
	mp = mt_generate_tunpkt(ETHERTYPE_IPV6, METT_GENEVE, 0, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HCK_FULLCKSUM);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	mp2 = msgpullup(mp, non_bodylen);
	KT_EASSERT3PG(mp2, !=, NULL, ctx, cleanup);

	if (mt_verify_tunlso(ctx, &mp2, mss, non_bodylen, bodylen, METT_GENEVE,
	    B_FALSE, 0, MT_CSUM_OUTER | MT_CSUM_INNER))
		goto cleanup;

	KT_PASS(ctx);

cleanup:
	if (mp != NULL)
		freemsgchain(mp);
	if (mp2 != NULL)
		freemsgchain(mp2);
}

/*
 * Verify that software LSO correctly operates for IPv4 traffic
 * contained in a Geneve (RFC8926) encapsulation over both IPv4
 * and IPv6 outer transport.
 */
void
mac_sw_lso_geneve_ipv4_test(ktest_ctx_hdl_t *ctx)
{
	mblk_t *mp = NULL;
	size_t non_bodylen = 0;
	size_t mss = 1448;
	size_t bodylen = 60000; /* chosen to be non-congruent to MSS */

	/* IPv4 outer, IPv4 inner */
	mp = mt_generate_tunpkt(ETHERTYPE_IP, METT_GENEVE, 12, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HCK_IPV4_HDRCKSUM | HW_LSO);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL |
	    MAC_LSO_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	if (mt_verify_tunlso(ctx, &mp, mss, non_bodylen, bodylen, METT_GENEVE,
	    B_TRUE, 12, MT_CSUM_INNER))
		goto cleanup;

	/* IPv6 outer, IPv4 inner */
	mp = mt_generate_tunpkt(ETHERTYPE_IPV6, METT_GENEVE, 12, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HW_LSO);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL |
	    MAC_LSO_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	if (mt_verify_tunlso(ctx, &mp, mss, non_bodylen, bodylen, METT_GENEVE,
	    B_FALSE, 12, MT_CSUM_INNER))
		goto cleanup;

	KT_PASS(ctx);

cleanup:
	if (mp != NULL)
		freemsgchain(mp);
}

/*
 * Verify that software LSO correctly operates for IPv4 traffic
 * contained in a Geneve (RFC8926) encapsulation over both IPv4
 * and IPv6 outer transport.
 */
void
mac_sw_lso_vxlan_ipv4_test(ktest_ctx_hdl_t *ctx)
{
	/*
	 * NOTE: inclusion of HCK_IPV4_HDRCKSUM and a partial/full ULP csum flag
	 * is mandated by debug assert within mac_sw_lso.
	 */
	mblk_t *mp = NULL;
	size_t non_bodylen = 0;
	size_t mss = 1448;
	size_t bodylen = 60000; /* chosen to be non-congruent to MSS */

	/* IPv4 outer, IPv4 inner */
	mp = mt_generate_tunpkt(ETHERTYPE_IP, METT_VXLAN, 0, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HCK_IPV4_HDRCKSUM | HW_LSO);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL |
	    MAC_LSO_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	if (mt_verify_tunlso(ctx, &mp, mss, non_bodylen, bodylen, METT_VXLAN,
	    B_TRUE, 0, MT_CSUM_INNER))
		goto cleanup;

	/* IPv6 outer, IPv4 inner */
	mp = mt_generate_tunpkt(ETHERTYPE_IPV6, METT_VXLAN, 0, ETHERTYPE_IP,
	    IPPROTO_TCP, bodylen, mss, HCK_INNER_V4CKSUM | HCK_INNER_FULL |
	    HW_LSO);
	KT_EASSERT3P(mp, !=, NULL, ctx);

	KT_EASSERT3UG(msgsize(mp), >=, bodylen, ctx, cleanup);
	non_bodylen = msgsize(mp) - bodylen;

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_IPCKSUM_EMUL |
	    MAC_LSO_EMUL);
	KT_ASSERT3P(mp, !=, NULL, ctx);

	if (mt_verify_tunlso(ctx, &mp, mss, non_bodylen, bodylen, METT_VXLAN,
	    B_FALSE, 0, MT_CSUM_INNER))
		goto cleanup;

	KT_PASS(ctx);

cleanup:
	if (mp != NULL)
		freemsgchain(mp);
}

void
mac_tun_info_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	mac_partial_tun_info_t mac_partial_tun_info = NULL;
	mblk_t *mp = NULL;
	mac_ether_offload_info_t tuninfo = {
		.meoi_flags = 0,
		.meoi_tuntype = METT_GENEVE,
	};
	uint32_t encap_len = 0;
	int err;

	if (ktest_hold_mod("mac", &hdl) != 0) {
		KT_ERROR(ctx, "failed to hold 'mac' module");
		return;
	}

	if (ktest_get_fn(hdl, "mac_partial_tun_info",
	    (void **)&mac_partial_tun_info) != 0) {
		KT_ERROR(ctx,
		    "failed to resolve symbol mac`mac_partial_tun_info");
		goto cleanup;
	}

	mp = mt_generate_tunpkt(ETHERTYPE_IP, METT_GENEVE, 12, ETHERTYPE_IP,
	    IPPROTO_TCP, 1200, 1448, 0);
	KT_EASSERT3P(mp, !=, NULL, ctx);
	err = mac_partial_tun_info(mp, 0, &tuninfo);
	KT_ASSERT3SG(err, ==, 0, ctx, cleanup);

	KT_ASSERT3UG(tuninfo.meoi_flags, ==, MEOI_FULLTUN, ctx, cleanup);
	KT_ASSERT3UG(tuninfo.meoi_l2hlen, ==, sizeof (struct ether_header),
	    ctx, cleanup);
	KT_ASSERT3UG(tuninfo.meoi_l3proto, ==, ETHERTYPE_IP, ctx, cleanup);
	KT_ASSERT3UG(tuninfo.meoi_l3hlen, ==, sizeof (ipha_t), ctx, cleanup);
	KT_ASSERT3UG(tuninfo.meoi_l4hlen, ==, sizeof (udpha_t), ctx, cleanup);
	KT_ASSERT3UG(tuninfo.meoi_tunhlen, ==, sizeof (struct geneveh) + 12,
	    ctx, cleanup);

	encap_len = tuninfo.meoi_l2hlen + tuninfo.meoi_l3hlen +
	    tuninfo.meoi_l4hlen + tuninfo.meoi_tunhlen;

	KT_ASSERT3UG(encap_len, ==, MBLKL(mp), ctx, cleanup);

	KT_PASS(ctx);

cleanup:
	if (hdl != NULL) {
		ktest_release_mod(hdl);
	}

	freemsg(mp);
}

static boolean_t
meoi_equal(ktest_ctx_hdl_t *ctx, mac_ether_offload_info_t *lhs,
    mac_ether_offload_info_t *rhs)
{
	KT_ASSERT3UG(lhs->meoi_flags, ==, rhs->meoi_flags, ctx, fail);
	KT_ASSERT3UG(lhs->meoi_tuntype, ==, rhs->meoi_tuntype, ctx, fail);
	KT_ASSERT3UG(lhs->meoi_l2hlen, ==, rhs->meoi_l2hlen, ctx, fail);
	KT_ASSERT3UG(lhs->meoi_l3proto, ==, rhs->meoi_l3proto, ctx, fail);
	KT_ASSERT3UG(lhs->meoi_l3hlen, ==, rhs->meoi_l3hlen, ctx, fail);
	KT_ASSERT3UG(lhs->meoi_l4proto, ==, rhs->meoi_l4proto, ctx, fail);
	KT_ASSERT3UG(lhs->meoi_l4hlen, ==, rhs->meoi_l4hlen, ctx, fail);
	/* meoi_len is not stored in mblk, have caller verify */
	return (B_TRUE);
fail:
	return (B_FALSE);
}

void
mac_pktinfo_test(ktest_ctx_hdl_t *ctx)
{
	/*
	 * We're testing storage/retrieval of packet facts. mblk contents are
	 * not a concern.
	 */
	mblk_t *mp = allocb(128, 0);
	KT_EASSERT3P(mp, !=, NULL, ctx);
	KT_ASSERTG(!mac_ether_any_set_pktinfo(mp), ctx, cleanup);

	mp->b_wptr += 128;

	/* Fill out only standard facts. */
	mac_ether_offload_info_t in_info = {
		.meoi_flags = MEOI_FULL,
		.meoi_l2hlen = 14,
		.meoi_l3proto = ETHERTYPE_IP,
		.meoi_l3hlen = 20,
		.meoi_l4proto = IPPROTO_TCP,
		.meoi_l4hlen = 28,
	};
	mac_ether_offload_info_t out_info = { 0 };

	mac_ether_set_pktinfo(mp, &in_info, NULL);
	KT_ASSERTG(mac_ether_any_set_pktinfo(mp), ctx, cleanup);

	mac_ether_offload_info(mp, &out_info, NULL);
	if (!meoi_equal(ctx, &out_info, &in_info)) {
		ktest_msg_prepend(ctx, "standard case: ");
		goto cleanup;
	}
	KT_ASSERT3UG(out_info.meoi_len, ==, msgdsize(mp), ctx, cleanup);

	mac_ether_clear_pktinfo(mp);
	KT_ASSERTG(!mac_ether_any_set_pktinfo(mp), ctx, cleanup);

	/* Are the VLAN & fragment flags preserved? */
	bzero(&out_info, sizeof (out_info));
	in_info.meoi_flags |= MEOI_VLAN_TAGGED | MEOI_L3_FRAG_OFFSET;
	in_info.meoi_l2hlen += 4;

	mac_ether_set_pktinfo(mp, &in_info, NULL);
	KT_ASSERTG(mac_ether_any_set_pktinfo(mp), ctx, cleanup);

	mac_ether_offload_info(mp, &out_info, NULL);
	if (!meoi_equal(ctx, &out_info, &in_info)) {
		ktest_msg_prepend(ctx, "extra flags case: ");
		goto cleanup;
	}
	KT_ASSERT3UG(out_info.meoi_len, ==, msgdsize(mp), ctx, cleanup);

	mac_ether_clear_pktinfo(mp);
	KT_ASSERTG(!mac_ether_any_set_pktinfo(mp), ctx, cleanup);

	/* Is state preserved in a tunnel? */
	bzero(&out_info, sizeof (out_info));
	mac_ether_offload_info_t tun_info = {
		.meoi_flags = MEOI_FULLTUN | MEOI_L3_FRAG_MORE,
		.meoi_tuntype = METT_GENEVE,

		.meoi_l2hlen = 14,
		.meoi_l3proto = ETHERTYPE_IP,
		.meoi_l3hlen = 20,
		.meoi_l4proto = IPPROTO_UDP,
		.meoi_l4hlen = 8,
		.meoi_tunhlen = 16,
	};
	mac_ether_offload_info_t out_tun_info = { 0 };

	mac_ether_set_pktinfo(mp, &tun_info, &in_info);
	KT_ASSERTG(mac_ether_any_set_pktinfo(mp), ctx, cleanup);

	mac_ether_offload_info(mp, &out_tun_info, &out_info);
	if (!meoi_equal(ctx, &out_tun_info, &tun_info)) {
		ktest_msg_prepend(ctx, "tuninfo: ");
		goto cleanup;
	}
	if (!meoi_equal(ctx, &out_info, &in_info)) {
		ktest_msg_prepend(ctx, "tunneled extra flags case: ");
		goto cleanup;
	}
	KT_ASSERT3UG(out_tun_info.meoi_len, ==, msgdsize(mp), ctx, cleanup);
	KT_ASSERT3UG(out_info.meoi_len, ==, msgdsize(mp) - 58, ctx, cleanup);

	KT_PASS(ctx);

cleanup:
	freemsg(mp);
}


static struct modlmisc mac_ktest_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "mac ktest module"
};

static struct modlinkage mac_ktest_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &mac_ktest_modlmisc, NULL }
};

int
_init()
{
	int ret;
	ktest_module_hdl_t *km = NULL;
	ktest_suite_hdl_t *ks = NULL;

	VERIFY0(ktest_create_module("mac", &km));
	VERIFY0(ktest_add_suite(km, "checksum", &ks));
	VERIFY0(ktest_add_test(ks, "mac_sw_cksum_test",
	    mac_sw_cksum_test, KTEST_FLAG_INPUT));
	VERIFY0(ktest_add_test(ks, "mac_sw_cksum_tun_ipv4_test",
	    mac_sw_cksum_tun_ipv4_test, KTEST_FLAG_NONE));

	ks = NULL;
	VERIFY0(ktest_add_suite(km, "lso", &ks));
	VERIFY0(ktest_add_test(ks, "mac_sw_lso_test",
	    mac_sw_lso_test, KTEST_FLAG_INPUT));
	VERIFY0(ktest_add_test(ks, "mac_sw_lso_geneve_ipv4_test",
	    mac_sw_lso_geneve_ipv4_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "mac_sw_lso_vxlan_ipv4_test",
	    mac_sw_lso_vxlan_ipv4_test, KTEST_FLAG_NONE));

	ks = NULL;
	VERIFY0(ktest_add_suite(km, "parsing", &ks));
	VERIFY0(ktest_add_test(ks, "mac_ether_offload_info_test",
	    mac_ether_offload_info_test, KTEST_FLAG_INPUT));
	VERIFY0(ktest_add_test(ks, "mac_partial_offload_info_test",
	    mac_partial_offload_info_test, KTEST_FLAG_INPUT));
	VERIFY0(ktest_add_test(ks, "mac_ether_l2_info_test",
	    mac_ether_l2_info_test, KTEST_FLAG_INPUT));
	VERIFY0(ktest_add_test(ks, "mac_tun_info_test",
	    mac_tun_info_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "mac_pktinfo_test",
	    mac_pktinfo_test, KTEST_FLAG_NONE));

	if ((ret = ktest_register_module(km)) != 0) {
		ktest_free_module(km);
		return (ret);
	}

	if ((ret = mod_install(&mac_ktest_modlinkage)) != 0) {
		ktest_unregister_module("mac");
		return (ret);
	}

	return (0);
}

int
_fini(void)
{
	ktest_unregister_module("mac");
	return (mod_remove(&mac_ktest_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&mac_ktest_modlinkage, modinfop));
}
