/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2019 Joyent, Inc.
 * Copyright 2025 Oxide Computer Company
 */

/*
 * MAC Services Module - misc utilities
 */

#include <sys/types.h>
#include <sys/mac.h>
#include <sys/mac_impl.h>
#include <sys/mac_client_priv.h>
#include <sys/mac_client_impl.h>
#include <sys/mac_soft_ring.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/vlan.h>
#include <sys/pattr.h>
#include <sys/pci_tools.h>
#include <inet/ip.h>
#include <inet/ip_impl.h>
#include <inet/ip6.h>
#include <sys/vtrace.h>
#include <sys/dlpi.h>
#include <sys/sunndi.h>
#include <inet/ipsec_impl.h>
#include <inet/sadb.h>
#include <inet/ipsecesp.h>
#include <inet/ipsecah.h>
#include <inet/tcp.h>
#include <inet/udp_impl.h>
#include <inet/sctp_ip.h>

/*
 * The next two functions are used for dropping packets or chains of
 * packets, respectively. We could use one function for both but
 * separating the use cases allows us to specify intent and prevent
 * dropping more data than intended.
 *
 * The purpose of these functions is to aid the debugging effort,
 * especially in production. Rather than use freemsg()/freemsgchain(),
 * it's preferable to use these functions when dropping a packet in
 * the MAC layer. These functions should only be used during
 * unexpected conditions. That is, any time a packet is dropped
 * outside of the regular, successful datapath. Consolidating all
 * drops on these functions allows the user to trace one location and
 * determine why the packet was dropped based on the msg. It also
 * allows the user to inspect the packet before it is freed. Finally,
 * it allows the user to avoid tracing freemsg()/freemsgchain() thus
 * keeping the hot path running as efficiently as possible.
 *
 * NOTE: At this time not all MAC drops are aggregated on these
 * functions; but that is the plan. This comment should be erased once
 * completed.
 */

/*PRINTFLIKE2*/
void
mac_drop_pkt(mblk_t *mp, const char *fmt, ...)
{
	va_list adx;
	char msg[128];
	char *msgp = msg;

	ASSERT3P(mp->b_next, ==, NULL);

	va_start(adx, fmt);
	(void) vsnprintf(msgp, sizeof (msg), fmt, adx);
	va_end(adx);

	DTRACE_PROBE2(mac__drop, mblk_t *, mp, char *, msgp);
	freemsg(mp);
}

/*PRINTFLIKE2*/
void
mac_drop_chain(mblk_t *chain, const char *fmt, ...)
{
	va_list adx;
	char msg[128];
	char *msgp = msg;

	va_start(adx, fmt);
	(void) vsnprintf(msgp, sizeof (msg), fmt, adx);
	va_end(adx);

	/*
	 * We could use freemsgchain() for the actual freeing but
	 * since we are already walking the chain to fire the dtrace
	 * probe we might as well free the msg here too.
	 */
	for (mblk_t *mp = chain, *next; mp != NULL; ) {
		next = mp->b_next;
		DTRACE_PROBE2(mac__drop, mblk_t *, mp, char *, msgp);
		mp->b_next = NULL;
		freemsg(mp);
		mp = next;
	}
}

/*
 * Copy an mblk, preserving its hardware checksum flags.
 */
static mblk_t *
mac_copymsg_cksum(mblk_t *mp)
{
	mblk_t *mp1;

	mp1 = copymsg(mp);
	if (mp1 == NULL)
		return (NULL);

	mac_hcksum_clone(mp, mp1);

	return (mp1);
}

/*
 * Copy an mblk chain, presenting the hardware checksum flags of the
 * individual mblks.
 */
mblk_t *
mac_copymsgchain_cksum(mblk_t *mp)
{
	mblk_t *nmp = NULL;
	mblk_t **nmpp = &nmp;

	for (; mp != NULL; mp = mp->b_next) {
		if ((*nmpp = mac_copymsg_cksum(mp)) == NULL) {
			freemsgchain(nmp);
			return (NULL);
		}

		nmpp = &((*nmpp)->b_next);
	}

	return (nmp);
}

/* Moves a set of checksum flags from the inner layer to the outer. */
static uint32_t
mac_hcksum_flags_shift_out(uint32_t flags)
{
	uint32_t out = flags & ~HCK_FLAGS;

	if (flags & HCK_INNER_V4CKSUM)
		out |= HCK_IPV4_HDRCKSUM;
	if (flags & HCK_INNER_V4CKSUM_OK)
		out |= HCK_IPV4_HDRCKSUM_OK;
	if (flags & HCK_INNER_PARTIAL)
		out |= HCK_PARTIALCKSUM;
	if (flags & HCK_INNER_FULL)
		out |= HCK_FULLCKSUM;
	if (flags & HCK_INNER_FULL_OK)
		out |= HCK_FULLCKSUM_OK;

	return (out);
}

static mblk_t *
mac_sw_cksum_impl(mblk_t *mp, mac_emul_t emul, const uint32_t encap_len,
    const mac_ether_offload_info_t *meoi)
{
	const char *err = "";
	const boolean_t is_outer = encap_len == 0;

	/*
	 * The only current caller is mac_hw_emul(), which handles any chaining
	 * of mblks prior to now.
	 */
	VERIFY3P(mp->b_next, ==, NULL);

	uint32_t flags = DB_CKSUMFLAGS(mp);
	uint32_t layer_flags = is_outer ? flags :
	    mac_hcksum_flags_shift_out(flags);

	/* Requesting both ULP cksum types is improper */
	if ((layer_flags & HCK_FULLCKSUM) != 0 &&
	    (layer_flags & HCK_PARTIALCKSUM) != 0) {
		err = "full and partial ULP cksum requested";
		goto bail;
	}

	const boolean_t do_v4_cksum = (emul & MAC_IPCKSUM_EMUL) != 0 &&
	    (layer_flags & HCK_IPV4_HDRCKSUM) != 0;
	const boolean_t do_ulp_cksum = (emul & MAC_HWCKSUM_EMUL) != 0 &&
	    (layer_flags & (HCK_FULLCKSUM | HCK_PARTIALCKSUM)) != 0;
	const boolean_t ulp_prefer_partial =
	    (layer_flags & HCK_PARTIALCKSUM) != 0;

	if ((meoi->meoi_flags & MEOI_L2INFO_SET) == 0 ||
	    (meoi->meoi_l3proto != ETHERTYPE_IP &&
	    meoi->meoi_l3proto != ETHERTYPE_IPV6)) {
		/* Non-IP traffic (like ARP) is left alone */
		return (mp);
	}

	/*
	 * Ensure that requested checksum type(s) are supported by the
	 * protocols encoded in the packet headers.
	 */
	if (do_v4_cksum) {
		if (meoi->meoi_l3proto != ETHERTYPE_IP) {
			err = "IPv4 csum requested on non-IPv4 packet";
			goto bail;
		}
	}
	if (do_ulp_cksum) {
		if (!mac_meoi_is_full(meoi)) {
			err = "missing ULP header";
			goto bail;
		}
		switch (meoi->meoi_l4proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
		case IPPROTO_ICMP:
		case IPPROTO_ICMPV6:
		case IPPROTO_SCTP:
			break;
		default:
			err = "unexpected ULP";
			goto bail;
		}
	}

	/*
	 * Walk past encapsulation and this frame's L2 to reach the inner frame
	 * (may just be mp).
	 */
	mblk_t *parent = NULL, *target_mp = mp;
	uint_t l3_off = encap_len + meoi->meoi_l2hlen;
	while (target_mp != NULL && l3_off >= MBLKL(target_mp)) {
		size_t seglen = MBLKL(target_mp);
		size_t n = MIN(seglen, l3_off);

		l3_off -= n;
		if (n == seglen) {
			parent = target_mp;
			target_mp = target_mp->b_cont;
		}
	}
	const uint_t l4_off = l3_off + meoi->meoi_l3hlen;

	if (target_mp == NULL) {
		err = "no mblks after encapsulation + L2";
		goto bail;
	}

	/*
	 * Ensure that all of the headers we need to access are:
	 * 1. Collected in the first mblk after (optional) encap + l2
	 * 2. Held in a data-block which is safe for us to modify
	 *    (It must have a refcount of 1)
	 * To simplify mblk management, also copy any preceding bytes in
	 * target_mp.
	 */
	const size_t hdr_len_reqd = l4_off +
	    (do_ulp_cksum ? meoi->meoi_l4hlen : 0);
	if (MBLKL(target_mp) < hdr_len_reqd || DB_REF(target_mp) > 1) {
		mblk_t *hdrmp = msgpullup(target_mp, hdr_len_reqd);

		if (hdrmp == NULL) {
			err = "could not pullup msg headers";
			goto bail;
		}

		if (parent == NULL) {
			mac_hcksum_clone(mp, hdrmp);
			mp = hdrmp;
		} else {
			parent->b_cont = hdrmp;
		}

		freemsg(target_mp);
		target_mp = hdrmp;
	}

	/* Calculate IPv4 header checksum, if requested */
	if (do_v4_cksum) {
		/*
		 * While unlikely, it's possible to write code that might end up
		 * calling mac_sw_cksum() twice on the same mblk (performing
		 * both LSO and checksum emulation in a single mblk chain loop
		 * -- the LSO emulation inserts a new chain into the existing
		 * chain and then the loop iterates back over the new segments
		 * and emulates the checksum a second time).  Normally this
		 * wouldn't be a problem, because the HCK_*_OK flags are
		 * supposed to indicate that we don't need to do peform the
		 * work. But HCK_IPV4_HDRCKSUM and HCK_IPV4_HDRCKSUM_OK have the
		 * same value; so we cannot use these flags to determine if the
		 * IP header checksum has already been calculated or not. For
		 * this reason, we zero out the the checksum first. In the
		 * future, we should fix the HCK_* flags.
		 */
		ipha_t *ipha = (ipha_t *)(target_mp->b_rptr + l3_off);
		ipha->ipha_hdr_checksum = 0;
		ipha->ipha_hdr_checksum = (uint16_t)ip_csum_hdr(ipha);
		if (is_outer) {
			flags &= ~HCK_IPV4_HDRCKSUM;
			flags |= HCK_IPV4_HDRCKSUM_OK;
		} else {
			flags &= ~HCK_INNER_V4CKSUM;
			flags |= HCK_INNER_V4CKSUM_OK;
		}
	}

	/*
	 * The SCTP is different from all the other protocols in that it uses
	 * CRC32 for its checksum, rather than ones' complement.
	 */
	if (do_ulp_cksum && meoi->meoi_l4proto == IPPROTO_SCTP) {
		if (ulp_prefer_partial) {
			err = "SCTP does not support partial checksum";
			goto bail;
		}

		sctp_hdr_t *sctph = (sctp_hdr_t *)(target_mp->b_rptr + l4_off);

		sctph->sh_chksum = 0;
		sctph->sh_chksum = sctp_cksum(target_mp, l4_off);

		if (is_outer) {
			flags &= ~HCK_FULLCKSUM;
			flags |= HCK_FULLCKSUM_OK;
		} else {
			flags &= ~HCK_INNER_FULL;
			flags |= HCK_INNER_FULL_OK;
		}

		goto success;
	}

	/* Calculate full ULP checksum, if requested */
	if (do_ulp_cksum && !ulp_prefer_partial) {
		/*
		 * Calculate address and length portions of pseudo-header csum
		 */
		uint32_t cksum = 0;
		if (meoi->meoi_l3proto == ETHERTYPE_IP) {
			const ipha_t *ipha =
			    (const ipha_t *)(target_mp->b_rptr + l3_off);
			const uint16_t *saddr =
			    (const uint16_t *)(&ipha->ipha_src);
			const uint16_t *daddr =
			    (const uint16_t *)(&ipha->ipha_dst);

			cksum += saddr[0] + saddr[1] + daddr[0] + daddr[1];

			/*
			 * While it is tempting to calculate the payload length
			 * solely from `meoi`, doing so is a trap.  Packets
			 * shorter than 60 bytes will get padded out to that
			 * length in order to meet the minimums for Ethernet.
			 * Additionally, in the LSO case `meoi->meoi_len` refers
			 * to the *input frame* (i.e., far larger than MTU).
			 * Instead, we pull the length from the IP header.
			 */
			const uint16_t payload_len =
			    ntohs(ipha->ipha_length) - meoi->meoi_l3hlen;
			cksum += htons(payload_len);
		} else if (meoi->meoi_l3proto == ETHERTYPE_IPV6) {
			const ip6_t *ip6h =
			    (const ip6_t *)(target_mp->b_rptr + l3_off);
			const uint16_t *saddr =
			    ip6h->ip6_src.s6_addr16;
			const uint16_t *daddr =
			    ip6h->ip6_dst.s6_addr16;

			cksum += saddr[0] + saddr[1] + saddr[2] + saddr[3] +
			    saddr[4] + saddr[5] + saddr[6] + saddr[7];
			cksum += daddr[0] + daddr[1] + daddr[2] + daddr[3] +
			    daddr[4] + daddr[5] + daddr[6] + daddr[7];

			const uint16_t payload_len = ntohs(ip6h->ip6_plen) +
			    sizeof (*ip6h) - meoi->meoi_l3hlen;
			cksum += htons(payload_len);
		} else {
			/*
			 * Since we already checked for recognized L3 protocols
			 * earlier, this should not be reachable.
			 */
			panic("L3 protocol unexpectedly changed");
		}

		/* protocol portion of pseudo-header */
		uint_t cksum_off;
		switch (meoi->meoi_l4proto) {
		case IPPROTO_TCP:
			cksum += IP_TCP_CSUM_COMP;
			cksum_off = TCP_CHECKSUM_OFFSET;
			break;
		case IPPROTO_UDP:
			cksum += IP_UDP_CSUM_COMP;
			cksum_off = UDP_CHECKSUM_OFFSET;
			break;
		case IPPROTO_ICMP:
			/* ICMP cksum does not include pseudo-header contents */
			cksum = 0;
			cksum_off = ICMP_CHECKSUM_OFFSET;
			break;
		case IPPROTO_ICMPV6:
			cksum += IP_ICMPV6_CSUM_COMP;
			cksum_off = ICMPV6_CHECKSUM_OFFSET;
			break;
		default:
			err = "unrecognized L4 protocol";
			goto bail;
		}

		/*
		 * With IP_CSUM() taking into account the pseudo-header
		 * checksum, make sure the ULP checksum field is zeroed before
		 * computing the rest;
		 */
		uint16_t *up = (uint16_t *)(target_mp->b_rptr + l4_off +
		    cksum_off);
		*up = 0;
		cksum = IP_CSUM(target_mp, l4_off, cksum);

		if (meoi->meoi_l4proto == IPPROTO_UDP && cksum == 0) {
			/*
			 * A zero checksum is not allowed on UDPv6, and on UDPv4
			 * implies no checksum.  In either case, invert to a
			 * values of all-1s.
			 */
			*up = 0xffff;
		} else {
			*up = cksum;
		}

		if (is_outer) {
			flags &= ~HCK_FULLCKSUM;
			flags |= HCK_FULLCKSUM_OK;
		} else {
			flags &= ~HCK_INNER_FULL;
			flags |= HCK_INNER_FULL_OK;
		}

		goto success;
	}

	/* Calculate partial ULP checksum, if requested */
	if (do_ulp_cksum && ulp_prefer_partial) {
		uint32_t start, stuff, end, value;
		mac_hcksum_get(mp, &start, &stuff, &end, &value, NULL);

		/*
		 * For tunneled packets, the above should not be set (and would
		 * be tricky to disambiguate with two partial checksums on the
		 * scene. Derive them in this case, and always convert to
		 * positions inclusive of ethernet/encap.
		 */
		if (encap_len != 0 || meoi->meoi_tuntype != METT_NONE) {
			stuff = start = l4_off;
			switch (meoi->meoi_l4proto) {
			case IPPROTO_TCP:
				stuff += TCP_CHECKSUM_OFFSET;
				break;
			case IPPROTO_UDP:
				stuff += UDP_CHECKSUM_OFFSET;
				break;
			case IPPROTO_ICMP:
				stuff += ICMP_CHECKSUM_OFFSET;
				break;
			case IPPROTO_ICMPV6:
				stuff += ICMPV6_CHECKSUM_OFFSET;
				break;
			default:
				err = "unrecognized L4 protocol";
				goto bail;
			}
		} else {
			ASSERT3U(end, >, start);
			start += l3_off;
			stuff += l3_off;
		}

		/*
		 * The prior size checks against the header length data ensure
		 * that the mblk contains everything through at least the ULP
		 * header, but if the partial checksum (unexpectedly) requests
		 * its result be stored past that, we cannot continue.
		 */
		if (stuff + sizeof (uint16_t) > MBLKL(target_mp)) {
			err = "partial csum request is out of bounds";
			goto bail;
		}

		uint16_t *up = (uint16_t *)(target_mp->b_rptr + stuff);

		const uint16_t partial = *up;
		*up = 0;
		const uint16_t cksum =
		    ~IP_CSUM_PARTIAL(target_mp, start, partial);
		*up = (cksum != 0) ? cksum : ~cksum;

		if (is_outer) {
			flags &= ~HCK_PARTIALCKSUM;
			flags |= HCK_FULLCKSUM_OK;
		} else {
			flags &= ~HCK_INNER_PARTIAL;
			flags |= HCK_INNER_FULL_OK;
		}
	}

success:
	/*
	 * With the checksum(s) calculated, store the updated flags to reflect
	 * the current status, and zero out any of the partial-checksum fields
	 * which would be irrelevant now.
	 */
	mac_hcksum_set(mp, 0, 0, 0, 0, flags);

	if (parent != NULL && mp != target_mp) {
		ASSERT3P(parent->b_cont, ==, target_mp);

		/*
		 * Duplicate the HCKSUM data into the header mblk.
		 *
		 * This mimics mac_add_vlan_tag() which ensures that both the
		 * first mblk _and_ the first data bearing mblk possess the
		 * HCKSUM information. Consumers like IP will end up discarding
		 * the ether_header mblk, so for now, it is important that the
		 * data be available in both places.
		 */
		mac_hcksum_clone(mp, target_mp);
	}

	return (mp);

bail:
	mac_drop_pkt(mp, err);
	return (NULL);
}

typedef struct mac_emul_ctx {
	const uint32_t encap_len;
	const mac_ether_offload_info_t *outer_info;
	const mac_ether_offload_info_t *inner_info;
} mac_emul_ctx_t;

/*
 * Perform software checksum on a single message, if needed. The emulation
 * performed is determined by an intersection of the mblk's flags and the emul
 * flags requested. The emul flags are documented in mac.h.
 *
 * To correctly handle tunneled packets, frames are processed from the inside
 * out (i.e., any outer L4 packet checksums are reliant on correct inner
 * checksums). A non-zero encap_len is treated as entering from the tunneled
 * case.
 */
static mblk_t *
mac_sw_cksum(mblk_t *mp, mac_emul_t emul, const mac_emul_ctx_t *ctx)
{
	uint32_t flags = DB_CKSUMFLAGS(mp) & (HCK_FLAGS);

	/* Why call this if checksum emulation isn't needed? */
	ASSERT3U(flags, !=, 0);

	/* process inner before outer */
	if (ctx->encap_len != 0 && (flags & HCK_INNER_TX_FLAGS) != 0) {
		mp = mac_sw_cksum_impl(mp, emul, ctx->encap_len,
		    ctx->inner_info);
		if (mp == NULL) {
			return (mp);
		}
	}

	if ((DB_CKSUMFLAGS(mp) & HCK_OUTER_TX_FLAGS) != 0) {
		mp = mac_sw_cksum_impl(mp, emul, 0, ctx->outer_info);
	}

	return (mp);
}

/*
 * Build a single data segment from an LSO packet. The mblk chain
 * returned, seg_head, represents the data segment and is always
 * exactly seg_len bytes long. The lso_mp and offset input/output
 * parameters track our position in the LSO packet. This function
 * exists solely as a helper to mac_sw_lso().
 *
 * Case A
 *
 *     The current lso_mp is larger than the requested seg_len. The
 *     beginning of seg_head may start at the beginning of lso_mp or
 *     offset into it. In either case, a single mblk is returned, and
 *     *offset is updated to reflect our new position in the current
 *     lso_mp.
 *
 *          +----------------------------+
 *          |  in *lso_mp / out *lso_mp  |
 *          +----------------------------+
 *          ^                        ^
 *          |                        |
 *          |                        |
 *          |                        |
 *          +------------------------+
 *          |        seg_head        |
 *          +------------------------+
 *          ^                        ^
 *          |                        |
 *   in *offset = 0        out *offset = seg_len
 *
 *          |------   seg_len    ----|
 *
 *
 *       +------------------------------+
 *       |   in *lso_mp / out *lso_mp   |
 *       +------------------------------+
 *          ^                        ^
 *          |                        |
 *          |                        |
 *          |                        |
 *          +------------------------+
 *          |        seg_head        |
 *          +------------------------+
 *          ^                        ^
 *          |                        |
 *   in *offset = N        out *offset = N + seg_len
 *
 *          |------   seg_len    ----|
 *
 *
 *
 * Case B
 *
 *    The requested seg_len consumes exactly the rest of the lso_mp.
 *    I.e., the seg_head's b_wptr is equivalent to lso_mp's b_wptr.
 *    The seg_head may start at the beginning of the lso_mp or at some
 *    offset into it. In either case we return a single mblk, reset
 *    *offset to zero, and walk to the next lso_mp.
 *
 *          +------------------------+           +------------------------+
 *          |       in *lso_mp       |---------->|      out *lso_mp       |
 *          +------------------------+           +------------------------+
 *          ^                        ^           ^
 *          |                        |           |
 *          |                        |    out *offset = 0
 *          |                        |
 *          +------------------------+
 *          |        seg_head        |
 *          +------------------------+
 *          ^
 *          |
 *   in *offset = 0
 *
 *          |------   seg_len    ----|
 *
 *
 *
 *      +----------------------------+           +------------------------+
 *      |         in *lso_mp         |---------->|      out *lso_mp       |
 *      +----------------------------+           +------------------------+
 *          ^                        ^           ^
 *          |                        |           |
 *          |                        |    out *offset = 0
 *          |                        |
 *          +------------------------+
 *          |        seg_head        |
 *          +------------------------+
 *          ^
 *          |
 *   in *offset = N
 *
 *          |------   seg_len    ----|
 *
 *
 * Case C
 *
 *    The requested seg_len is greater than the current lso_mp. In
 *    this case we must consume LSO mblks until we have enough data to
 *    satisfy either case (A) or (B) above. We will return multiple
 *    mblks linked via b_cont, offset will be set based on the cases
 *    above, and lso_mp will walk forward at least one mblk, but maybe
 *    more.
 *
 *    N.B. This digram is not exhaustive. The seg_head may start on
 *    the beginning of an lso_mp. The seg_tail may end exactly on the
 *    boundary of an lso_mp. And there may be two (in this case the
 *    middle block wouldn't exist), three, or more mblks in the
 *    seg_head chain. This is meant as one example of what might
 *    happen. The main thing to remember is that the seg_tail mblk
 *    must be one of case (A) or (B) above.
 *
 *  +------------------+    +----------------+    +------------------+
 *  |    in *lso_mp    |--->|    *lso_mp     |--->|   out *lso_mp    |
 *  +------------------+    +----------------+    +------------------+
 *        ^            ^    ^                ^    ^            ^
 *        |            |    |                |    |            |
 *        |            |    |                |    |            |
 *        |            |    |                |    |            |
 *        |            |    |                |    |            |
 *        +------------+    +----------------+    +------------+
 *        |  seg_head  |--->|                |--->|  seg_tail  |
 *        +------------+    +----------------+    +------------+
 *        ^                                                    ^
 *        |                                                    |
 *  in *offset = N                          out *offset = MBLKL(seg_tail)
 *
 *        |-------------------   seg_len    -------------------|
 *
 */
static mblk_t *
build_data_seg(mblk_t **lso_mp, uint32_t *offset, uint32_t seg_len)
{
	mblk_t *seg_head, *seg_tail, *seg_mp;

	ASSERT3P(*lso_mp, !=, NULL);
	ASSERT3U((*lso_mp)->b_rptr + *offset, <, (*lso_mp)->b_wptr);

	seg_mp = dupb(*lso_mp);
	if (seg_mp == NULL)
		return (NULL);

	seg_head = seg_mp;
	seg_tail = seg_mp;

	/* Continue where we left off from in the lso_mp. */
	seg_mp->b_rptr += *offset;

last_mblk:
	/* Case (A) */
	if ((seg_mp->b_rptr + seg_len) < seg_mp->b_wptr) {
		*offset += seg_len;
		seg_mp->b_wptr = seg_mp->b_rptr + seg_len;
		return (seg_head);
	}

	/* Case (B) */
	if ((seg_mp->b_rptr + seg_len) == seg_mp->b_wptr) {
		*offset = 0;
		*lso_mp = (*lso_mp)->b_cont;
		return (seg_head);
	}

	/* Case (C) */
	ASSERT3U(seg_mp->b_rptr + seg_len, >, seg_mp->b_wptr);

	/*
	 * The current LSO mblk doesn't have enough data to satisfy
	 * seg_len -- continue peeling off LSO mblks to build the new
	 * segment message. If allocation fails we free the previously
	 * allocated segment mblks and return NULL.
	 */
	while ((seg_mp->b_rptr + seg_len) > seg_mp->b_wptr) {
		ASSERT3U(MBLKL(seg_mp), <=, seg_len);
		seg_len -= MBLKL(seg_mp);
		*offset = 0;
		*lso_mp = (*lso_mp)->b_cont;
		seg_mp = dupb(*lso_mp);

		if (seg_mp == NULL) {
			freemsgchain(seg_head);
			return (NULL);
		}

		seg_tail->b_cont = seg_mp;
		seg_tail = seg_mp;
	}

	/*
	 * We've walked enough LSO mblks that we can now satisfy the
	 * remaining seg_len. At this point we need to jump back to
	 * determine if we have arrived at case (A) or (B).
	 */

	/* Just to be paranoid that we didn't underflow. */
	ASSERT3U(seg_len, <, IP_MAXPACKET);
	ASSERT3U(seg_len, >, 0);
	goto last_mblk;
}

/*
 * Perform software segmentation of a single LSO message. Take an LSO
 * message as input and return head/tail pointers as output. This
 * function should not be invoked directly but instead through
 * mac_hw_emul().
 *
 * The resulting chain is comprised of multiple (nsegs) MSS sized
 * segments. Each segment will consist of two or more mblks joined by
 * b_cont: a header and one or more data mblks. The header mblk is
 * allocated anew for each message. The first segment's header is used
 * as a template for the rest with adjustments made for things such as
 * ID, sequence, length, TCP flags, etc. The data mblks reference into
 * the existing LSO mblk (passed in as omp) by way of dupb(). Their
 * b_rptr/b_wptr values are adjusted to reference only the fraction of
 * the LSO message they are responsible for. At the successful
 * completion of this function the original mblk (omp) is freed,
 * leaving the newely created segment chain as the only remaining
 * reference to the data.
 *
 * The types used to point into and manipulate packet headers currently
 * assume alignment which we don't set up here in our `msgpullup`s. This
 * will be a problem for non-x86 architectures, but also results in our
 * output packets taking slower paths through IP.
 * One difficulty is that when packets are tunneled and in a single
 * mblk, 4B-alignment of outer and inner (non-Eth) headers are in direct
 * tension with one another. Offsetting the outermost layers by 2
 * ensures the inner TCP etc. cannot be 4B aligned, and vice-versa.
 */
static void
mac_sw_lso(mblk_t *omp, mac_emul_t emul, mblk_t **head, mblk_t **tail,
    uint_t *count, const mac_emul_ctx_t *ctx)
{
	uint32_t ocsum_flags, ocsum_start, ocsum_stuff;
	uint32_t mss;
	uint32_t oehlen, oiphlen, otcphlen, ohdrslen, opktlen;
	uint32_t odatalen, oleft;
	uint_t nsegs, seg;
	int len;

	const void *oiph;
	const tcph_t *otcph;
	ipha_t *niph;
	tcph_t *ntcph;
	uint16_t ip_id;
	uint32_t tcp_seq, tcp_sum, otcp_sum;

	boolean_t is_v6 = B_FALSE;
	ip6_t *niph6;

	mblk_t *odatamp;
	mblk_t *seg_chain, *prev_nhdrmp, *next_nhdrmp, *nhdrmp, *ndatamp;
	mblk_t *tmptail;

	const uint32_t encap_len = ctx->encap_len;
	const boolean_t is_tun = ctx->outer_info->meoi_tuntype != METT_NONE;
	const mac_ether_offload_info_t *ulp_info = is_tun ?
	    ctx->inner_info : ctx->outer_info;

	ASSERT3P(head, !=, NULL);
	ASSERT3P(tail, !=, NULL);
	ASSERT3P(count, !=, NULL);
	ASSERT3U((DB_CKSUMFLAGS(omp) & HW_LSO), !=, 0);
	ASSERT(encap_len == 0 || ctx->outer_info->meoi_tuntype != METT_NONE);

	/* Assume we are dealing with a single LSO message. */
	ASSERT3P(omp->b_next, ==, NULL);

	opktlen = ctx->outer_info->meoi_len;
	oehlen = ulp_info->meoi_l2hlen;
	oiphlen = ulp_info->meoi_l3hlen;
	otcphlen = ulp_info->meoi_l4hlen;
	ohdrslen = oehlen + oiphlen + otcphlen;

	if (encap_len > opktlen) {
		mac_drop_pkt(omp, "encap longer than packet");
		goto fail;
	}

	/* Performing LSO requires that we successfully read fully up to L4 */
	if (!mac_meoi_is_full(ulp_info)) {
		mac_drop_pkt(omp, "unable to fully parse packet to L4");
		goto fail;
	}

	/* mac_hw_emul() must have filled out tuninfo if one was specified */
	if (is_tun && !mac_tun_meoi_is_full(ctx->outer_info)) {
		mac_drop_pkt(omp, "tunneled packet has incomplete tuninfo");
		goto fail;
	}

	if (ulp_info->meoi_l3proto != ETHERTYPE_IP &&
	    ulp_info->meoi_l3proto != ETHERTYPE_IPV6) {
		mac_drop_pkt(omp, "LSO'd packet has non-IP L3 header: %x",
		    ulp_info->meoi_l3proto);
		goto fail;
	}

	if (ulp_info->meoi_l4proto != IPPROTO_TCP) {
		mac_drop_pkt(omp, "LSO unsupported protocol: %x",
		    ulp_info->meoi_l4proto);
		goto fail;
	}

	is_v6 = ulp_info->meoi_l3proto == ETHERTYPE_IPV6;

	mss = DB_LSOMSS(omp);
	if (mss == 0) {
		mac_drop_pkt(omp, "packet misconfigured for LSO (MSS == 0)");
		goto fail;
	}
	ASSERT3U(opktlen, <=, IP_MAXPACKET + encap_len + oehlen);

	/*
	 * Pullup all encapsulation and innermost headers here, if these are not
	 * contiguous. While we only require that the innermost L3/L4 headers
	 * are contiguous at this stage, we'd need to perform at least one
	 * pullup later to safely modify outer lengths/checksums. In particular,
	 * the IP header is used for the benefit of DTrace SDTs, and the TCP
	 * header is actively read.
	 *
	 * Most clients (IP, viona) will setup well-behaved mblks. This small
	 * pullup should only practically happen when mac_add_vlan_tag is in
	 * play, which prepends a new mblk in front containing the amended
	 * Ethernet header, or the encapsulation is pushed on as a separate
	 * mblk. This causes at most one more (header-sized) copy.
	 */
	const size_t hdr_len_reqd = encap_len + ohdrslen;
	if (MBLKL(omp) < hdr_len_reqd) {
		mblk_t *tmp = msgpullup(omp, hdr_len_reqd);
		if (tmp == NULL) {
			mac_drop_pkt(omp, "failed to pull up");
			goto fail;
		}
		mac_hcksum_clone(omp, tmp);
		freemsg(omp);
		omp = tmp;
	}

	const uint32_t l3_off = encap_len + oehlen;
	const uint32_t l4_off = l3_off + oiphlen;
	uint32_t data_off = l4_off + otcphlen;
	oiph = (void *)(omp->b_rptr + l3_off);
	otcph = (tcph_t *)(omp->b_rptr + l4_off);

	if (otcph->th_flags[0] & (TH_SYN | TH_RST | TH_URG)) {
		mac_drop_pkt(omp, "LSO packet has SYN|RST|URG set");
		goto fail;
	}

	len = MBLKL(omp);

	/*
	 * Either we have data in the current mblk or it's just the headers.
	 * Record the start of the TCP data.
	 */
	if (len > data_off) {
		odatamp = omp;
	} else {
		ASSERT3U(len, ==, data_off);
		odatamp = omp->b_cont;
		data_off = 0;
	}

	/* Make sure we still have enough data. */
	odatalen = opktlen - ohdrslen - encap_len;
	ASSERT3U(msgsize(odatamp), >=, odatalen);

	/*
	 * If a MAC negotiated LSO then it must negotiate both
	 * HCKSUM_IPHDRCKSUM and either HCKSUM_INET_FULL_V4 or
	 * HCKSUM_INET_PARTIAL; because both the IP and TCP headers
	 * change during LSO segmentation (only the 3 fields of the
	 * pseudo header checksum don't change: src, dst, proto). Thus
	 * we would expect these flags (HCK_IPV4_HDRCKSUM |
	 * HCK_PARTIALCKSUM | HCK_FULLCKSUM) to be set and for this
	 * function to emulate those checksums in software. However,
	 * that assumes a world where we only expose LSO if the
	 * underlying hardware exposes LSO. Moving forward the plan is
	 * to assume LSO in the upper layers and have MAC perform
	 * software LSO when the underlying provider doesn't support
	 * it. In such a world, if the provider doesn't support LSO
	 * but does support hardware checksum offload, then we could
	 * simply perform the segmentation and allow the hardware to
	 * calculate the checksums. To the hardware it's just another
	 * chain of non-LSO packets.
	 */
	ASSERT3S(DB_TYPE(omp), ==, M_DATA);
	ocsum_flags = DB_CKSUMFLAGS(omp);
	ASSERT3U(ocsum_flags & (is_tun ? (HCK_INNER_PARTIAL | HCK_INNER_FULL) :
	    (HCK_PARTIALCKSUM | HCK_FULLCKSUM)), !=, 0);

	/*
	 * If hardware only provides partial checksum then software
	 * must supply the pseudo-header checksum. In the case of LSO
	 * we leave the TCP length at zero to be filled in by
	 * hardware. This function must handle two scenarios.
	 *
	 * 1. Being called by a MAC client on the Rx path to segment
	 *    an LSO packet and calculate the checksum.
	 *
	 * 2. Being called by a MAC provider to segment an LSO packet.
	 *    In this case the LSO segmentation is performed in
	 *    software (by this routine) but the MAC provider should
	 *    still calculate the TCP/IP checksums in hardware.
	 *
	 *  To elaborate on the second case: we cannot have the
	 *  scenario where IP sends LSO packets but the underlying HW
	 *  doesn't support checksum offload -- because in that case
	 *  TCP/IP would calculate the checksum in software (for the
	 *  LSO packet) but then MAC would segment the packet and have
	 *  to redo all the checksum work. So IP should never do LSO
	 *  if HW doesn't support both IP and TCP checksum.
	 */
	const boolean_t tcp_csum_partial = is_tun ?
	    (ocsum_flags & HCK_INNER_PARTIAL) != 0 :
	    (ocsum_flags & HCK_PARTIALCKSUM) != 0;

	if (!is_tun && tcp_csum_partial) {
		ocsum_start = (uint32_t)DB_CKSUMSTART(omp);
		ocsum_stuff = (uint32_t)DB_CKSUMSTUFF(omp);
	}

	/*
	 * Subtract one to account for the case where the data length
	 * is evenly divisble by the MSS. Add one to account for the
	 * fact that the division will always result in one less
	 * segment than needed.
	 */
	nsegs = ((odatalen - 1) / mss) + 1;
	if (nsegs < 2) {
		mac_drop_pkt(omp, "LSO not enough segs: %u", nsegs);
		goto fail;
	}

	DTRACE_PROBE7(sw__lso__start, mblk_t *, omp, uint32_t, encap_len,
	    void_ip_t *, oiph, __dtrace_tcp_tcph_t *, otcph, uint_t, odatalen,
	    uint_t, mss, uint_t, nsegs);

	seg_chain = NULL;
	tmptail = seg_chain;
	oleft = odatalen;

	for (uint_t i = 0; i < nsegs; i++) {
		boolean_t last_seg = (i + 1) == nsegs;
		uint32_t seg_len;

		/*
		 * If we fail to allocate, then drop the partially
		 * allocated chain as well as the LSO packet. Let the
		 * sender deal with the fallout.
		 */
		if ((nhdrmp = allocb(hdr_len_reqd, 0)) == NULL) {
			freemsgchain(seg_chain);
			mac_drop_pkt(omp, "failed to alloc segment header");
			goto fail;
		}
		ASSERT3P(nhdrmp->b_cont, ==, NULL);

		/* Copy over the header stack. */
		bcopy(omp->b_rptr, nhdrmp->b_rptr, hdr_len_reqd);
		nhdrmp->b_wptr += hdr_len_reqd;

		if (seg_chain == NULL) {
			seg_chain = nhdrmp;
		} else {
			ASSERT3P(tmptail, !=, NULL);
			tmptail->b_next = nhdrmp;
		}

		tmptail = nhdrmp;

		/*
		 * Calculate this segment's length. It's either the MSS
		 * or whatever remains for the last segment.
		 */
		seg_len = last_seg ? oleft : mss;
		ASSERT3U(seg_len, <=, mss);
		ndatamp = build_data_seg(&odatamp, &data_off, seg_len);

		if (ndatamp == NULL) {
			freemsgchain(seg_chain);
			mac_drop_pkt(omp, "LSO failed to segment data");
			goto fail;
		}

		/* Attach data mblk to header mblk. */
		nhdrmp->b_cont = ndatamp;
		DB_CKSUMFLAGS(ndatamp) &= ~HW_LSO;
		ASSERT3U(seg_len, <=, oleft);
		oleft -= seg_len;

		/*
		 * Setup partial checksum offsets for non-tunneled packets.
		 * mac_sw_cksum will figure precise offsets out for tunneled
		 * packets, as we may have two partial checksums (thus need to
		 * rely upon parsing from MEOI).
		 */
		if (!is_tun && tcp_csum_partial) {
			DB_CKSUMSTART(nhdrmp) = ocsum_start;
			DB_CKSUMEND(nhdrmp) = oiphlen + otcphlen + seg_len;
			DB_CKSUMSTUFF(nhdrmp) = ocsum_stuff;
		}

		/* Fixup lengths/idents in outer headers */
		if (is_tun) {
			uint32_t diff = odatalen - seg_len;

			switch (ctx->outer_info->meoi_l3proto) {
			case ETHERTYPE_IP: {
				ipha_t *tun_ip4h = (ipha_t *)(nhdrmp->b_rptr +
				    ctx->outer_info->meoi_l2hlen);
				tun_ip4h->ipha_length = htons(
				    ntohs(tun_ip4h->ipha_length) - diff);
				tun_ip4h->ipha_ident = htons(
				    ntohs(tun_ip4h->ipha_ident) + i);
				/*
				 * The NIC used for making offload determination
				 * would have filled the V4 csum when doing LSO.
				 * However, it may be unable to fill this and
				 * also perform, e.g., inner csum offload on a
				 * normal send. This is cheap enough compared to
				 * e.g. full outer cksum to proactively fill in
				 * here.
				 */
				tun_ip4h->ipha_hdr_checksum = 0;
				tun_ip4h->ipha_hdr_checksum =
				    (uint16_t)ip_csum_hdr(tun_ip4h);
				break;
			}
			case ETHERTYPE_IPV6: {
				ip6_t *tun_ip6h = (ip6_t *)(nhdrmp->b_rptr +
				    ctx->outer_info->meoi_l2hlen);
				tun_ip6h->ip6_plen = htons(
				    ntohs(tun_ip6h->ip6_plen) - diff);
				break;
			}
			default:
				break;
			}

			switch (ctx->outer_info->meoi_tuntype) {
			case METT_GENEVE:
			case METT_VXLAN: {
				udpha_t *tun_udph = (udpha_t *)(nhdrmp->b_rptr +
				    ctx->outer_info->meoi_l2hlen +
				    ctx->outer_info->meoi_l3hlen);
				tun_udph->uha_length = htons(
				    ntohs(tun_udph->uha_length) - diff);

				/*
				 * If the control plane for the tunnel requires
				 * an outer UDP checksum (e.g., cautious use of
				 * IPv6 + UDP in spite of RFC 6935/6936), then
				 * we need to recompute those checksums if they
				 * have been filled in.
				 */
				if (tun_udph->uha_checksum != 0) {
					emul |= MAC_HWCKSUM_EMUL;
					ocsum_flags |= HCK_FULLCKSUM;
				}
				break;
			}
			default:
				break;
			}
		}
	}

	/* We should have consumed entire LSO msg. */
	ASSERT3S(oleft, ==, 0);
	ASSERT3P(odatamp, ==, NULL);

	/*
	 * All seg data mblks are referenced by the header mblks, null
	 * out this pointer to catch any bad derefs.
	 */
	ndatamp = NULL;

	/*
	 * Set headers and checksum for first segment.
	 */
	nhdrmp = seg_chain;
	ASSERT3U(msgsize(nhdrmp->b_cont), ==, mss);

	if (is_v6) {
		niph6 = (ip6_t *)(nhdrmp->b_rptr + oehlen + encap_len);
		niph6->ip6_plen = htons(
		    (oiphlen - IPV6_HDR_LEN) + otcphlen + mss);
	} else {
		niph = (ipha_t *)(nhdrmp->b_rptr + oehlen + encap_len);
		niph->ipha_length = htons(oiphlen + otcphlen + mss);

		/*
		 * If the v4 checksum was filled, we won't have a v4 offload
		 * flag. We can't write zero checksums without inserting said
		 * flag, but our output frames won't necessarily be rechecked by
		 * the caller! As a compromise, we need to force emulation to
		 * uphold the same contracts the packet already agreed to.
		 */
		if (niph->ipha_hdr_checksum != 0) {
			emul |= MAC_IPCKSUM_EMUL;
			ocsum_flags |= is_tun ?
			    HCK_INNER_V4CKSUM : HCK_IPV4_HDRCKSUM;
		}
		niph->ipha_hdr_checksum = 0;
		ip_id = ntohs(niph->ipha_ident);
	}

	ntcph = (tcph_t *)(nhdrmp->b_rptr + oehlen + oiphlen + encap_len);
	tcp_seq = BE32_TO_U32(ntcph->th_seq);
	tcp_seq += mss;

	/*
	 * The first segment shouldn't:
	 *
	 *	o indicate end of data transmission (FIN),
	 *	o indicate immediate handling of the data (PUSH).
	 */
	ntcph->th_flags[0] &= ~(TH_FIN | TH_PUSH);
	DB_CKSUMFLAGS(nhdrmp) = (uint16_t)(ocsum_flags & ~HW_LSO);

	/*
	 * If the underlying HW provides partial checksum, then make
	 * sure to correct the pseudo header checksum before calling
	 * mac_sw_cksum(). The native TCP stack doesn't include the
	 * length field in the pseudo header when LSO is in play -- so
	 * we need to calculate it here.
	 */
	if (tcp_csum_partial) {
		tcp_sum = BE16_TO_U16(ntcph->th_sum);
		otcp_sum = tcp_sum;
		tcp_sum += mss + otcphlen;
		tcp_sum = (tcp_sum >> 16) + (tcp_sum & 0xFFFF);
		U16_TO_BE16(tcp_sum, ntcph->th_sum);
	}

	if ((ocsum_flags & HCK_TX_FLAGS) && (emul & MAC_HWCKSUM_EMULS)) {
		next_nhdrmp = nhdrmp->b_next;
		nhdrmp->b_next = NULL;
		nhdrmp = mac_sw_cksum(nhdrmp, emul, ctx);
		nhdrmp->b_next = next_nhdrmp;
		next_nhdrmp = NULL;

		/*
		 * We may have freed the nhdrmp argument during
		 * checksum emulation, make sure that seg_chain
		 * references a valid mblk.
		 */
		seg_chain = nhdrmp;
	}

	ASSERT3P(nhdrmp, !=, NULL);

	seg = 1;
	DTRACE_PROBE5(sw__lso__seg, mblk_t *, nhdrmp, void_ip_t *,
	    (is_v6 ? (void *)niph6 : (void *)niph),
	    __dtrace_tcp_tcph_t *, ntcph, uint_t, mss, int_t, seg);
	seg++;

	/* There better be at least 2 segs. */
	ASSERT3P(nhdrmp->b_next, !=, NULL);
	prev_nhdrmp = nhdrmp;
	nhdrmp = nhdrmp->b_next;

	/*
	 * Now adjust the headers of the middle segments. For each
	 * header we need to adjust the following.
	 *
	 *	o IP ID
	 *	o IP length
	 *	o TCP sequence
	 *	o TCP flags
	 *	o cksum flags
	 *	o cksum values (if MAC_HWCKSUM_EMUL is set)
	 */
	for (; seg < nsegs; seg++) {
		/*
		 * We use seg_chain as a reference to the first seg
		 * header mblk -- this first header is a template for
		 * the rest of the segments. This copy will include
		 * the now updated checksum values from the first
		 * header. We must reset these checksum values to
		 * their original to make sure we produce the correct
		 * value.
		 */
		ASSERT3U(msgsize(nhdrmp->b_cont), ==, mss);
		if (is_v6) {
			niph6 = (ip6_t *)(nhdrmp->b_rptr + oehlen + encap_len);
			niph6->ip6_plen = htons(
			    (oiphlen - IPV6_HDR_LEN) + otcphlen + mss);
		} else {
			niph = (ipha_t *)(nhdrmp->b_rptr + oehlen + encap_len);
			niph->ipha_ident = htons(++ip_id);
			niph->ipha_length = htons(oiphlen + otcphlen + mss);
			niph->ipha_hdr_checksum = 0;
		}
		ntcph = (tcph_t *)(nhdrmp->b_rptr + oehlen + oiphlen +
		    encap_len);
		U32_TO_BE32(tcp_seq, ntcph->th_seq);
		tcp_seq += mss;
		/*
		 * Just like the first segment, the middle segments
		 * shouldn't have these flags set.
		 */
		ntcph->th_flags[0] &= ~(TH_FIN | TH_PUSH);
		DB_CKSUMFLAGS(nhdrmp) = (uint16_t)(ocsum_flags & ~HW_LSO);

		/* First and middle segs have same pseudo-header checksum. */
		if (tcp_csum_partial)
			U16_TO_BE16(tcp_sum, ntcph->th_sum);

		if ((ocsum_flags & HCK_TX_FLAGS) &&
		    (emul & MAC_HWCKSUM_EMULS)) {
			next_nhdrmp = nhdrmp->b_next;
			nhdrmp->b_next = NULL;
			nhdrmp = mac_sw_cksum(nhdrmp, emul, ctx);
			nhdrmp->b_next = next_nhdrmp;
			next_nhdrmp = NULL;
			/* We may have freed the original nhdrmp. */
			prev_nhdrmp->b_next = nhdrmp;
		}

		DTRACE_PROBE5(sw__lso__seg, mblk_t *, nhdrmp, void_ip_t *,
		    (is_v6 ? (void *)niph6 : (void *)niph),
		    __dtrace_tcp_tcph_t *, ntcph, uint_t, mss, uint_t, seg);

		ASSERT3P(nhdrmp->b_next, !=, NULL);
		prev_nhdrmp = nhdrmp;
		nhdrmp = nhdrmp->b_next;
	}

	/* Make sure we are on the last segment. */
	ASSERT3U(seg, ==, nsegs);
	ASSERT3P(nhdrmp->b_next, ==, NULL);

	/*
	 * Now we set the last segment header. The difference being
	 * that FIN/PSH/RST flags are allowed.
	 */
	len = msgsize(nhdrmp->b_cont);
	ASSERT3S(len, >, 0);
	if (is_v6) {
		niph6 = (ip6_t *)(nhdrmp->b_rptr + oehlen + encap_len);
		niph6->ip6_plen = htons(
		    (oiphlen - IPV6_HDR_LEN) + otcphlen + len);
	} else {
		niph = (ipha_t *)(nhdrmp->b_rptr + oehlen + encap_len);
		niph->ipha_ident = htons(++ip_id);
		niph->ipha_length = htons(oiphlen + otcphlen + len);
		niph->ipha_hdr_checksum = 0;
	}
	ntcph = (tcph_t *)(nhdrmp->b_rptr + oehlen + oiphlen + encap_len);
	U32_TO_BE32(tcp_seq, ntcph->th_seq);

	DB_CKSUMFLAGS(nhdrmp) = (uint16_t)(ocsum_flags & ~HW_LSO);
	if (tcp_csum_partial) {
		tcp_sum = otcp_sum;
		tcp_sum += len + otcphlen;
		tcp_sum = (tcp_sum >> 16) + (tcp_sum & 0xFFFF);
		U16_TO_BE16(tcp_sum, ntcph->th_sum);
	}

	if ((ocsum_flags & HCK_TX_FLAGS) && (emul & MAC_HWCKSUM_EMULS)) {
		/* This should be the last mblk. */
		ASSERT3P(nhdrmp->b_next, ==, NULL);
		nhdrmp = mac_sw_cksum(nhdrmp, emul, ctx);
		prev_nhdrmp->b_next = nhdrmp;
	}

	DTRACE_PROBE5(sw__lso__seg, mblk_t *, nhdrmp, void_ip_t *,
	    (is_v6 ? (void *)niph6 : (void *)niph),
	    __dtrace_tcp_tcph_t *, ntcph, uint_t, len, uint_t, seg);

	/*
	 * Free the reference to the original LSO message as it is
	 * being replaced by seg_chain.
	 */
	freemsg(omp);
	*head = seg_chain;
	*tail = nhdrmp;
	*count = nsegs;
	return;

fail:
	*head = NULL;
	*tail = NULL;
	*count = 0;
}

/*
 * Emulate various hardware offload features in software. Take a chain
 * of packets as input and emulate the hardware features specified in
 * 'emul'. The resulting chain's head pointer replaces the 'mp_chain'
 * pointer given as input, and its tail pointer is written to
 * '*otail'. The number of packets in the new chain is written to
 * '*ocount'. The 'otail' and 'ocount' arguments are optional and thus
 * may be NULL. The 'mp_chain' argument may point to a NULL chain; in
 * which case 'mp_chain' will simply stay a NULL chain.
 *
 * While unlikely, it is technically possible that this function could
 * receive a non-NULL chain as input and return a NULL chain as output
 * ('*mp_chain' and '*otail' would be NULL and '*ocount' would be
 * zero). This could happen if all the packets in the chain are
 * dropped or if we fail to allocate new mblks. In this case, there is
 * nothing for the caller to free. In any event, the caller shouldn't
 * assume that '*mp_chain' is non-NULL on return.
 *
 * This function was written with three main use cases in mind.
 *
 * 1. To emulate hardware offloads when traveling mac-loopback (two
 *    clients on the same mac). This is wired up in mac_tx_send().
 *
 * 2. To provide hardware offloads to the client when the underlying
 *    provider cannot. This is currently wired up in mac_tx() but we
 *    still only negotiate offloads when the underlying provider
 *    supports them.
 *
 * 3. To emulate real hardware in simnet.
 */
void
mac_hw_emul(mblk_t **mp_chain, mblk_t **otail, uint_t *ocount, mac_emul_t emul)
{
	mblk_t *head = NULL, *tail = NULL;
	uint_t count = 0;

	ASSERT3S(~(MAC_HWCKSUM_EMULS | MAC_LSO_EMUL) & emul, ==, 0);
	ASSERT3P(mp_chain, !=, NULL);

	for (mblk_t *mp = *mp_chain; mp != NULL; ) {
		mblk_t *tmp, *next, *tmphead, *tmptail;
		mblk_t *inner_frame = mp;
		size_t inner_frame_offset = 0;
		uint32_t flags;
		uint_t len, l2len;

		mac_ether_offload_info_t outer_info, inner_info;
		mac_ether_offload_info_t *ulp_info = NULL;
		uint32_t encap_len = 0;

		/* Perform LSO/checksum one message at a time. */
		next = mp->b_next;
		mp->b_next = NULL;

		/*
		 * Parse failure can occur in several cases we need to support,
		 * e.g., v4 checksum offload on ICMP packets, or tunneled
		 * ARP/ICMP. We require, at a minimum, that encapsulation parses
		 * successfully and that we can read L2 on the innermost frame.
		 */
		mac_ether_offload_info(mp, &outer_info, &inner_info);

		/*
		 * Compute offset to inner packet if tunneled.
		 * Enforce that each layer is contiguous and not split
		 * over b_cont boundaries.
		 */
		if (outer_info.meoi_tuntype != METT_NONE) {
			if (!mac_tun_meoi_is_full(&outer_info)) {
				mac_drop_pkt(mp, "tunnel headers unparseable");
				goto nextpkt;
			}
			const size_t sizes[3] = {
				outer_info.meoi_l2hlen,
				outer_info.meoi_l3hlen,
				outer_info.meoi_l4hlen + outer_info.meoi_tunhlen
			};
			encap_len = sizes[0] + sizes[1] + sizes[2];

			for (uint_t i = 0; i < 3; ++i) {
				const size_t cur_l = MBLKL(inner_frame);

				if (cur_l - inner_frame_offset < sizes[i]) {
					mac_drop_pkt(mp,
					    "packet tunnel layer split over "
					    "mblk_t boundary");
					goto nextpkt;
				}

				inner_frame_offset += sizes[i];
				if (inner_frame_offset == cur_l) {
					inner_frame_offset = 0;
					inner_frame = mp->b_cont;
				}

				/*
				 * Ensure both subsequent tunnel layers *and*
				 * inner frame have available bytes for reading.
				 */
				if (inner_frame == NULL) {
					mac_drop_pkt(mp,
					    "packet tunnel layer truncated");
					goto nextpkt;
				}
			}
			ulp_info = &inner_info;
		} else {
			ulp_info = &outer_info;
		}

		if ((ulp_info->meoi_flags & MEOI_L2INFO_SET) == 0) {
			mac_drop_pkt(mp, "innermost ethernet unparsable");
			goto nextpkt;
		}
		l2len = ulp_info->meoi_l2hlen;

		len = MBLKL(inner_frame);

		/*
		 * For our sanity the first mblk should contain at
		 * least the full L2 header.
		 */
		if (len < (l2len + inner_frame_offset)) {
			mac_drop_pkt(mp, "packet too short (A): %u", len);
			goto nextpkt;
		}

		/*
		 * If the first mblk is solely the L2 header, then
		 * there better be more data.
		 */
		if ((len == (l2len + inner_frame_offset) &&
		    mp->b_cont == NULL)) {
			mac_drop_pkt(mp, "packet too short (C): %u", len);
			goto nextpkt;
		}

		DTRACE_PROBE2(mac__emul, mblk_t *, mp, mac_emul_t, emul);

		/*
		 * We use DB_CKSUMFLAGS (instead of mac_hcksum_get())
		 * because we don't want to mask-out the LSO flag.
		 */
		flags = DB_CKSUMFLAGS(mp);

		const mac_emul_ctx_t ctx = {
			.encap_len = encap_len,
			.outer_info = &outer_info,
			.inner_info = &inner_info,
		};

		if ((flags & HW_LSO) && (emul & MAC_LSO_EMUL)) {
			uint_t tmpcount = 0;

			/*
			 * LSO fix-up handles checksum emulation
			 * inline (if requested). It also frees mp.
			 */
			mac_sw_lso(mp, emul, &tmphead, &tmptail, &tmpcount,
			    &ctx);
			if (tmphead == NULL) {
				/* mac_sw_lso() freed the mp. */
				goto nextpkt;
			}
			count += tmpcount;
		} else if ((flags & HCK_TX_FLAGS) &&
		    (emul & MAC_HWCKSUM_EMULS)) {
			tmp = mac_sw_cksum(mp, emul, &ctx);
			if (tmp == NULL) {
				/* mac_sw_cksum() freed the mp. */
				goto nextpkt;
			}
			tmphead = tmp;
			tmptail = tmp;
			count++;
		} else {
			/* There is nothing to emulate. */
			tmp = mp;
			tmphead = tmp;
			tmptail = tmp;
			count++;
		}

		/*
		 * The tmp mblk chain is either the start of the new
		 * chain or added to the tail of the new chain.
		 */
		if (head == NULL) {
			head = tmphead;
			tail = tmptail;
		} else {
			/* Attach the new mblk to the end of the new chain. */
			tail->b_next = tmphead;
			tail = tmptail;
		}

nextpkt:
		mp = next;
	}

	*mp_chain = head;

	if (otail != NULL)
		*otail = tail;

	if (ocount != NULL)
		*ocount = count;
}

/*
 * Add VLAN tag to the specified mblk.
 */
mblk_t *
mac_add_vlan_tag(mblk_t *mp, uint_t pri, uint16_t vid)
{
	mblk_t *hmp;
	struct ether_vlan_header *evhp;
	struct ether_header *ehp;
	mac_ether_offload_info_t meoi;

	ASSERT(pri != 0 || vid != 0);

	/*
	 * Allocate an mblk for the new tagged ethernet header,
	 * and copy the MAC addresses and ethertype from the
	 * original header.
	 */

	hmp = allocb(sizeof (struct ether_vlan_header), BPRI_MED);
	if (hmp == NULL) {
		freemsg(mp);
		return (NULL);
	}

	evhp = (struct ether_vlan_header *)hmp->b_rptr;
	ehp = (struct ether_header *)mp->b_rptr;

	bcopy(ehp, evhp, (ETHERADDRL * 2));
	evhp->ether_type = ehp->ether_type;
	evhp->ether_tpid = htons(ETHERTYPE_VLAN);

	/*
	 * Copy over any existing header length state, fixing up any L2 info
	 * which has already been filled in. Note that inner_info is
	 * unchanged and copied verbatim.
	 */
	if (mac_ether_any_set_pktinfo(mp)) {
		mac_ether_offload_info(mp, &meoi, NULL);
		hmp->b_datap->db_meoi = mp->b_datap->db_meoi;
		VERIFY3U(meoi.meoi_flags & MEOI_L2INFO_SET, !=, 0);
		meoi.meoi_flags |= MEOI_VLAN_TAGGED;
		meoi.meoi_l2hlen += VLAN_TAGSZ;
		meoi.meoi_len += VLAN_TAGSZ;
		mac_ether_set_pktinfo(hmp, &meoi, NULL);
	}

	hmp->b_wptr += sizeof (struct ether_vlan_header);
	mp->b_rptr += sizeof (struct ether_header);

	/*
	 * Free the original message if it's now empty. Link the
	 * rest of messages to the header message.
	 */
	mac_hcksum_clone(mp, hmp);
	if (MBLKL(mp) == 0) {
		hmp->b_cont = mp->b_cont;
		freeb(mp);
	} else {
		hmp->b_cont = mp;
	}
	ASSERT(MBLKL(hmp) >= sizeof (struct ether_vlan_header));

	/*
	 * Initialize the new TCI (Tag Control Information).
	 */
	evhp->ether_tci = htons(VLAN_TCI(pri, 0, vid));

	return (hmp);
}

/*
 * Adds a VLAN tag with the specified VID and priority to each mblk of
 * the specified chain.
 */
mblk_t *
mac_add_vlan_tag_chain(mblk_t *mp_chain, uint_t pri, uint16_t vid)
{
	mblk_t *next_mp, **prev, *mp;

	mp = mp_chain;
	prev = &mp_chain;

	while (mp != NULL) {
		next_mp = mp->b_next;
		mp->b_next = NULL;
		if ((mp = mac_add_vlan_tag(mp, pri, vid)) == NULL) {
			freemsgchain(next_mp);
			break;
		}
		*prev = mp;
		prev = &mp->b_next;
		mp = mp->b_next = next_mp;
	}

	return (mp_chain);
}

/*
 * Strip VLAN tag
 */
mblk_t *
mac_strip_vlan_tag(mblk_t *mp)
{
	mblk_t *newmp;
	struct ether_vlan_header *evhp;

	evhp = (struct ether_vlan_header *)mp->b_rptr;
	if (ntohs(evhp->ether_tpid) == ETHERTYPE_VLAN) {
		ASSERT(MBLKL(mp) >= sizeof (struct ether_vlan_header));

		if (DB_REF(mp) > 1) {
			newmp = copymsg(mp);
			if (newmp == NULL)
				return (NULL);
			freemsg(mp);
			mp = newmp;
		}

		evhp = (struct ether_vlan_header *)mp->b_rptr;

		ovbcopy(mp->b_rptr, mp->b_rptr + VLAN_TAGSZ, 2 * ETHERADDRL);
		mp->b_rptr += VLAN_TAGSZ;
	}
	return (mp);
}

/*
 * Strip VLAN tag from each mblk of the chain.
 */
mblk_t *
mac_strip_vlan_tag_chain(mblk_t *mp_chain)
{
	mblk_t *mp, *next_mp, **prev;

	mp = mp_chain;
	prev = &mp_chain;

	while (mp != NULL) {
		next_mp = mp->b_next;
		mp->b_next = NULL;
		if ((mp = mac_strip_vlan_tag(mp)) == NULL) {
			freemsgchain(next_mp);
			break;
		}
		*prev = mp;
		prev = &mp->b_next;
		mp = mp->b_next = next_mp;
	}

	return (mp_chain);
}

/*
 * Default callback function. Used when the datapath is not yet initialized.
 */
/* ARGSUSED */
void
mac_rx_def(void *arg, mac_resource_handle_t resource, mblk_t *mp_chain,
    boolean_t loopback)
{
	freemsgchain(mp_chain);
}

/*
 * Determines the IPv6 header length accounting for all the optional IPv6
 * headers (hop-by-hop, destination, routing and fragment). The header length
 * and next header value (a transport header) is captured.
 *
 * Returns B_FALSE if all the IP headers are not in the same mblk otherwise
 * returns B_TRUE.
 */
boolean_t
mac_ip_hdr_length_v6(ip6_t *ip6h, uint8_t *endptr, uint16_t *hdr_length,
    uint8_t *next_hdr, ip6_frag_t **fragp)
{
	uint16_t length;
	uint_t	ehdrlen;
	uint8_t *whereptr;
	uint8_t *nexthdrp;
	ip6_dest_t *desthdr;
	ip6_rthdr_t *rthdr;
	ip6_frag_t *fraghdr;

	if (((uchar_t *)ip6h + IPV6_HDR_LEN) > endptr)
		return (B_FALSE);
	ASSERT(IPH_HDR_VERSION(ip6h) == IPV6_VERSION);
	length = IPV6_HDR_LEN;
	whereptr = ((uint8_t *)&ip6h[1]); /* point to next hdr */

	if (fragp != NULL)
		*fragp = NULL;

	nexthdrp = &ip6h->ip6_nxt;
	while (whereptr < endptr) {
		/* Is there enough left for len + nexthdr? */
		if (whereptr + MIN_EHDR_LEN > endptr)
			break;

		switch (*nexthdrp) {
		case IPPROTO_HOPOPTS:
		case IPPROTO_DSTOPTS:
			/* Assumes the headers are identical for hbh and dst */
			desthdr = (ip6_dest_t *)whereptr;
			ehdrlen = 8 * (desthdr->ip6d_len + 1);
			if ((uchar_t *)desthdr +  ehdrlen > endptr)
				return (B_FALSE);
			nexthdrp = &desthdr->ip6d_nxt;
			break;
		case IPPROTO_ROUTING:
			rthdr = (ip6_rthdr_t *)whereptr;
			ehdrlen =  8 * (rthdr->ip6r_len + 1);
			if ((uchar_t *)rthdr +  ehdrlen > endptr)
				return (B_FALSE);
			nexthdrp = &rthdr->ip6r_nxt;
			break;
		case IPPROTO_FRAGMENT:
			fraghdr = (ip6_frag_t *)whereptr;
			ehdrlen = sizeof (ip6_frag_t);
			if ((uchar_t *)&fraghdr[1] > endptr)
				return (B_FALSE);
			nexthdrp = &fraghdr->ip6f_nxt;
			if (fragp != NULL)
				*fragp = fraghdr;
			break;
		case IPPROTO_NONE:
			/* No next header means we're finished */
		default:
			*hdr_length = length;
			*next_hdr = *nexthdrp;
			return (B_TRUE);
		}
		length += ehdrlen;
		whereptr += ehdrlen;
		*hdr_length = length;
		*next_hdr = *nexthdrp;
	}
	switch (*nexthdrp) {
	case IPPROTO_HOPOPTS:
	case IPPROTO_DSTOPTS:
	case IPPROTO_ROUTING:
	case IPPROTO_FRAGMENT:
		/*
		 * If any know extension headers are still to be processed,
		 * the packet's malformed (or at least all the IP header(s) are
		 * not in the same mblk - and that should never happen.
		 */
		return (B_FALSE);

	default:
		/*
		 * If we get here, we know that all of the IP headers were in
		 * the same mblk, even if the ULP header is in the next mblk.
		 */
		*hdr_length = length;
		*next_hdr = *nexthdrp;
		return (B_TRUE);
	}
}

/*
 * The following set of routines are there to take care of interrupt
 * re-targeting for legacy (fixed) interrupts. Some older versions
 * of the popular NICs like e1000g do not support MSI-X interrupts
 * and they reserve fixed interrupts for RX/TX rings. To re-target
 * these interrupts, PCITOOL ioctls need to be used.
 */
typedef struct mac_dladm_intr {
	int	ino;
	int	cpu_id;
	char	driver_path[MAXPATHLEN];
	char	nexus_path[MAXPATHLEN];
} mac_dladm_intr_t;

/* Bind the interrupt to cpu_num */
static int
mac_set_intr(ldi_handle_t lh, processorid_t cpu_num, int oldcpuid, int ino)
{
	pcitool_intr_set_t	iset;
	int			err;

	iset.old_cpu = oldcpuid;
	iset.ino = ino;
	iset.cpu_id = cpu_num;
	iset.user_version = PCITOOL_VERSION;
	err = ldi_ioctl(lh, PCITOOL_DEVICE_SET_INTR, (intptr_t)&iset, FKIOCTL,
	    kcred, NULL);

	return (err);
}

/*
 * Search interrupt information. iget is filled in with the info to search
 */
static boolean_t
mac_search_intrinfo(pcitool_intr_get_t *iget_p, mac_dladm_intr_t *dln)
{
	int	i;
	char	driver_path[2 * MAXPATHLEN];

	for (i = 0; i < iget_p->num_devs; i++) {
		(void) strlcpy(driver_path, iget_p->dev[i].path, MAXPATHLEN);
		(void) snprintf(&driver_path[strlen(driver_path)], MAXPATHLEN,
		    ":%s%d", iget_p->dev[i].driver_name,
		    iget_p->dev[i].dev_inst);
		/* Match the device path for the device path */
		if (strcmp(driver_path, dln->driver_path) == 0) {
			dln->ino = iget_p->ino;
			dln->cpu_id = iget_p->cpu_id;
			return (B_TRUE);
		}
	}
	return (B_FALSE);
}

/*
 * Get information about ino, i.e. if this is the interrupt for our
 * device and where it is bound etc.
 */
static boolean_t
mac_get_single_intr(ldi_handle_t lh, int oldcpuid, int ino,
    mac_dladm_intr_t *dln)
{
	pcitool_intr_get_t	*iget_p;
	int			ipsz;
	int			nipsz;
	int			err;
	uint8_t			inum;

	/*
	 * Check if SLEEP is OK, i.e if could come here in response to
	 * changing the fanout due to some callback from the driver, say
	 * link speed changes.
	 */
	ipsz = PCITOOL_IGET_SIZE(0);
	iget_p = kmem_zalloc(ipsz, KM_SLEEP);

	iget_p->num_devs_ret = 0;
	iget_p->user_version = PCITOOL_VERSION;
	iget_p->cpu_id = oldcpuid;
	iget_p->ino = ino;

	err = ldi_ioctl(lh, PCITOOL_DEVICE_GET_INTR, (intptr_t)iget_p,
	    FKIOCTL, kcred, NULL);
	if (err != 0) {
		kmem_free(iget_p, ipsz);
		return (B_FALSE);
	}
	if (iget_p->num_devs == 0) {
		kmem_free(iget_p, ipsz);
		return (B_FALSE);
	}
	inum = iget_p->num_devs;
	if (iget_p->num_devs_ret < iget_p->num_devs) {
		/* Reallocate */
		nipsz = PCITOOL_IGET_SIZE(iget_p->num_devs);

		kmem_free(iget_p, ipsz);
		ipsz = nipsz;
		iget_p = kmem_zalloc(ipsz, KM_SLEEP);

		iget_p->num_devs_ret = inum;
		iget_p->cpu_id = oldcpuid;
		iget_p->ino = ino;
		iget_p->user_version = PCITOOL_VERSION;
		err = ldi_ioctl(lh, PCITOOL_DEVICE_GET_INTR, (intptr_t)iget_p,
		    FKIOCTL, kcred, NULL);
		if (err != 0) {
			kmem_free(iget_p, ipsz);
			return (B_FALSE);
		}
		/* defensive */
		if (iget_p->num_devs != iget_p->num_devs_ret) {
			kmem_free(iget_p, ipsz);
			return (B_FALSE);
		}
	}

	if (mac_search_intrinfo(iget_p, dln)) {
		kmem_free(iget_p, ipsz);
		return (B_TRUE);
	}
	kmem_free(iget_p, ipsz);
	return (B_FALSE);
}

/*
 * Get the interrupts and check each one to see if it is for our device.
 */
static int
mac_validate_intr(ldi_handle_t lh, mac_dladm_intr_t *dln, processorid_t cpuid)
{
	pcitool_intr_info_t	intr_info;
	int			err;
	int			ino;
	int			oldcpuid;

	err = ldi_ioctl(lh, PCITOOL_SYSTEM_INTR_INFO, (intptr_t)&intr_info,
	    FKIOCTL, kcred, NULL);
	if (err != 0)
		return (-1);

	for (oldcpuid = 0; oldcpuid < intr_info.num_cpu; oldcpuid++) {
		for (ino = 0; ino < intr_info.num_intr; ino++) {
			if (mac_get_single_intr(lh, oldcpuid, ino, dln)) {
				if (dln->cpu_id == cpuid)
					return (0);
				return (1);
			}
		}
	}
	return (-1);
}

/*
 * Obtain the nexus parent node info. for mdip.
 */
static dev_info_t *
mac_get_nexus_node(dev_info_t *mdip, mac_dladm_intr_t *dln)
{
	struct dev_info		*tdip = (struct dev_info *)mdip;
	struct ddi_minor_data	*minordata;
	dev_info_t		*pdip;
	char			pathname[MAXPATHLEN];

	while (tdip != NULL) {
		/*
		 * The netboot code could call this function while walking the
		 * device tree so we need to use ndi_devi_tryenter() here to
		 * avoid deadlock.
		 */
		if (ndi_devi_tryenter((dev_info_t *)tdip) == 0)
			break;

		for (minordata = tdip->devi_minor; minordata != NULL;
		    minordata = minordata->next) {
			if (strncmp(minordata->ddm_node_type, DDI_NT_INTRCTL,
			    strlen(DDI_NT_INTRCTL)) == 0) {
				pdip = minordata->dip;
				(void) ddi_pathname(pdip, pathname);
				(void) snprintf(dln->nexus_path, MAXPATHLEN,
				    "/devices%s:intr", pathname);
				(void) ddi_pathname_minor(minordata, pathname);
				ndi_devi_exit((dev_info_t *)tdip);
				return (pdip);
			}
		}
		ndi_devi_exit((dev_info_t *)tdip);
		tdip = tdip->devi_parent;
	}
	return (NULL);
}

/*
 * For a primary MAC client, if the user has set a list or CPUs or
 * we have obtained it implicitly, we try to retarget the interrupt
 * for that device on one of the CPUs in the list.
 * We assign the interrupt to the same CPU as the poll thread.
 */
static boolean_t
mac_check_interrupt_binding(dev_info_t *mdip, int32_t cpuid)
{
	ldi_handle_t		lh = NULL;
	ldi_ident_t		li = NULL;
	int			err;
	int			ret;
	mac_dladm_intr_t	dln;
	dev_info_t		*dip;
	struct ddi_minor_data	*minordata;

	dln.nexus_path[0] = '\0';
	dln.driver_path[0] = '\0';

	minordata = ((struct dev_info *)mdip)->devi_minor;
	while (minordata != NULL) {
		if (minordata->type == DDM_MINOR)
			break;
		minordata = minordata->next;
	}
	if (minordata == NULL)
		return (B_FALSE);

	(void) ddi_pathname_minor(minordata, dln.driver_path);

	dip = mac_get_nexus_node(mdip, &dln);
	/* defensive */
	if (dip == NULL)
		return (B_FALSE);

	err = ldi_ident_from_major(ddi_driver_major(dip), &li);
	if (err != 0)
		return (B_FALSE);

	err = ldi_open_by_name(dln.nexus_path, FREAD|FWRITE, kcred, &lh, li);
	if (err != 0)
		return (B_FALSE);

	ret = mac_validate_intr(lh, &dln, cpuid);
	if (ret < 0) {
		(void) ldi_close(lh, FREAD|FWRITE, kcred);
		return (B_FALSE);
	}
	/* cmn_note? */
	if (ret != 0)
		if ((err = (mac_set_intr(lh, cpuid, dln.cpu_id, dln.ino)))
		    != 0) {
			(void) ldi_close(lh, FREAD|FWRITE, kcred);
			return (B_FALSE);
		}
	(void) ldi_close(lh, FREAD|FWRITE, kcred);
	return (B_TRUE);
}

void
mac_client_set_intr_cpu(void *arg, mac_client_handle_t mch, int32_t cpuid)
{
	dev_info_t		*mdip = (dev_info_t *)arg;
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_resource_props_t	*mrp;
	mac_perim_handle_t	mph;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_soft_ring_set_t	*rx_srs;
	mac_cpus_t		*srs_cpu;

	if (!mac_check_interrupt_binding(mdip, cpuid))
		cpuid = -1;
	mac_perim_enter_by_mh((mac_handle_t)mcip->mci_mip, &mph);
	mrp = MCIP_RESOURCE_PROPS(mcip);
	mrp->mrp_rx_intr_cpu = cpuid;
	if (flent != NULL && flent->fe_rx_srs_cnt == 2) {
		rx_srs = flent->fe_rx_srs[1];
		srs_cpu = &rx_srs->srs_cpu;
		srs_cpu->mc_rx_intr_cpu = cpuid;
	}
	mac_perim_exit(mph);
}

int32_t
mac_client_intr_cpu(mac_client_handle_t mch)
{
	mac_client_impl_t	*mcip = (mac_client_impl_t *)mch;
	mac_cpus_t		*srs_cpu;
	mac_soft_ring_set_t	*rx_srs;
	flow_entry_t		*flent = mcip->mci_flent;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	mac_ring_t		*ring;
	mac_intr_t		*mintr;

	/*
	 * Check if we need to retarget the interrupt. We do this only
	 * for the primary MAC client. We do this if we have the only
	 * exclusive ring in the group.
	 */
	if (mac_is_primary_client(mcip) && flent->fe_rx_srs_cnt == 2) {
		rx_srs = flent->fe_rx_srs[1];
		srs_cpu = &rx_srs->srs_cpu;
		ring = rx_srs->srs_ring;
		mintr = &ring->mr_info.mri_intr;
		/*
		 * If ddi_handle is present or the poll CPU is
		 * already bound to the interrupt CPU, return -1.
		 */
		if (mintr->mi_ddi_handle != NULL ||
		    ((mrp->mrp_ncpus != 0) &&
		    (mrp->mrp_rx_intr_cpu == srs_cpu->mc_rx_pollid))) {
			return (-1);
		}
		return (srs_cpu->mc_rx_pollid);
	}
	return (-1);
}

void *
mac_get_devinfo(mac_handle_t mh)
{
	mac_impl_t	*mip = (mac_impl_t *)mh;

	return ((void *)mip->mi_dip);
}

#define	PKT_HASH_2BYTES(x) ((x)[0] ^ (x)[1])
#define	PKT_HASH_4BYTES(x) ((x)[0] ^ (x)[1] ^ (x)[2] ^ (x)[3])
#define	PKT_HASH_MAC(x) ((x)[0] ^ (x)[1] ^ (x)[2] ^ (x)[3] ^ (x)[4] ^ (x)[5])

uint64_t
mac_pkt_hash(uint_t media, mblk_t *mp, uint8_t policy, boolean_t is_outbound)
{
	struct ether_header *ehp;
	uint64_t hash = 0;
	uint16_t sap;
	uint_t skip_len;
	uint8_t proto;
	boolean_t ip_fragmented;

	/*
	 * We may want to have one of these per MAC type plugin in the
	 * future. For now supports only ethernet.
	 */
	if (media != DL_ETHER)
		return (0L);

	/* for now we support only outbound packets */
	ASSERT(is_outbound);
	ASSERT(IS_P2ALIGNED(mp->b_rptr, sizeof (uint16_t)));
	ASSERT(MBLKL(mp) >= sizeof (struct ether_header));

	/* compute L2 hash */

	ehp = (struct ether_header *)mp->b_rptr;

	if ((policy & MAC_PKT_HASH_L2) != 0) {
		uchar_t *mac_src = ehp->ether_shost.ether_addr_octet;
		uchar_t *mac_dst = ehp->ether_dhost.ether_addr_octet;
		hash = PKT_HASH_MAC(mac_src) ^ PKT_HASH_MAC(mac_dst);
		policy &= ~MAC_PKT_HASH_L2;
	}

	if (policy == 0)
		goto done;

	/* skip ethernet header */

	sap = ntohs(ehp->ether_type);
	if (sap == ETHERTYPE_VLAN) {
		struct ether_vlan_header *evhp;
		mblk_t *newmp = NULL;

		skip_len = sizeof (struct ether_vlan_header);
		if (MBLKL(mp) < skip_len) {
			/* the vlan tag is the payload, pull up first */
			newmp = msgpullup(mp, -1);
			if ((newmp == NULL) || (MBLKL(newmp) < skip_len)) {
				goto done;
			}
			evhp = (struct ether_vlan_header *)newmp->b_rptr;
		} else {
			evhp = (struct ether_vlan_header *)mp->b_rptr;
		}

		sap = ntohs(evhp->ether_type);
		freemsg(newmp);
	} else {
		skip_len = sizeof (struct ether_header);
	}

	/* if ethernet header is in its own mblk, skip it */
	if (MBLKL(mp) <= skip_len) {
		skip_len -= MBLKL(mp);
		mp = mp->b_cont;
		if (mp == NULL)
			goto done;
	}

	sap = (sap < ETHERTYPE_802_MIN) ? 0 : sap;

	/* compute IP src/dst addresses hash and skip IPv{4,6} header */

	switch (sap) {
	case ETHERTYPE_IP: {
		ipha_t *iphp;

		/*
		 * If the header is not aligned or the header doesn't fit
		 * in the mblk, bail now. Note that this may cause packets
		 * reordering.
		 */
		iphp = (ipha_t *)(mp->b_rptr + skip_len);
		if (((unsigned char *)iphp + sizeof (ipha_t) > mp->b_wptr) ||
		    !OK_32PTR((char *)iphp))
			goto done;

		proto = iphp->ipha_protocol;
		skip_len += IPH_HDR_LENGTH(iphp);

		/* Check if the packet is fragmented. */
		ip_fragmented = ntohs(iphp->ipha_fragment_offset_and_flags) &
		    IPH_OFFSET;

		/*
		 * For fragmented packets, use addresses in addition to
		 * the frag_id to generate the hash inorder to get
		 * better distribution.
		 */
		if (ip_fragmented || (policy & MAC_PKT_HASH_L3) != 0) {
			uint8_t *ip_src = (uint8_t *)&(iphp->ipha_src);
			uint8_t *ip_dst = (uint8_t *)&(iphp->ipha_dst);

			hash ^= (PKT_HASH_4BYTES(ip_src) ^
			    PKT_HASH_4BYTES(ip_dst));
			policy &= ~MAC_PKT_HASH_L3;
		}

		if (ip_fragmented) {
			uint8_t *identp = (uint8_t *)&iphp->ipha_ident;
			hash ^= PKT_HASH_2BYTES(identp);
			goto done;
		}
		break;
	}
	case ETHERTYPE_IPV6: {
		ip6_t *ip6hp;
		ip6_frag_t *frag = NULL;
		uint16_t hdr_length;

		/*
		 * If the header is not aligned or the header doesn't fit
		 * in the mblk, bail now. Note that this may cause packets
		 * reordering.
		 */

		ip6hp = (ip6_t *)(mp->b_rptr + skip_len);
		if (((unsigned char *)ip6hp + IPV6_HDR_LEN > mp->b_wptr) ||
		    !OK_32PTR((char *)ip6hp))
			goto done;

		if (!mac_ip_hdr_length_v6(ip6hp, mp->b_wptr, &hdr_length,
		    &proto, &frag))
			goto done;
		skip_len += hdr_length;

		/*
		 * For fragmented packets, use addresses in addition to
		 * the frag_id to generate the hash inorder to get
		 * better distribution.
		 */
		if (frag != NULL || (policy & MAC_PKT_HASH_L3) != 0) {
			uint8_t *ip_src = &(ip6hp->ip6_src.s6_addr8[12]);
			uint8_t *ip_dst = &(ip6hp->ip6_dst.s6_addr8[12]);

			hash ^= (PKT_HASH_4BYTES(ip_src) ^
			    PKT_HASH_4BYTES(ip_dst));
			policy &= ~MAC_PKT_HASH_L3;
		}

		if (frag != NULL) {
			uint8_t *identp = (uint8_t *)&frag->ip6f_ident;
			hash ^= PKT_HASH_4BYTES(identp);
			goto done;
		}
		break;
	}
	default:
		goto done;
	}

	if (policy == 0)
		goto done;

	/* if ip header is in its own mblk, skip it */
	if (MBLKL(mp) <= skip_len) {
		skip_len -= MBLKL(mp);
		mp = mp->b_cont;
		if (mp == NULL)
			goto done;
	}

	/* parse ULP header */
again:
	switch (proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_ESP:
	case IPPROTO_SCTP:
		/*
		 * These Internet Protocols are intentionally designed
		 * for hashing from the git-go.  Port numbers are in the first
		 * word for transports, SPI is first for ESP.
		 */
		if (mp->b_rptr + skip_len + 4 > mp->b_wptr)
			goto done;
		hash ^= PKT_HASH_4BYTES((mp->b_rptr + skip_len));
		break;

	case IPPROTO_AH: {
		ah_t *ah = (ah_t *)(mp->b_rptr + skip_len);
		uint_t ah_length = AH_TOTAL_LEN(ah);

		if ((unsigned char *)ah + sizeof (ah_t) > mp->b_wptr)
			goto done;

		proto = ah->ah_nexthdr;
		skip_len += ah_length;

		/* if AH header is in its own mblk, skip it */
		if (MBLKL(mp) <= skip_len) {
			skip_len -= MBLKL(mp);
			mp = mp->b_cont;
			if (mp == NULL)
				goto done;
		}

		goto again;
	}
	}

done:
	return (hash);
}
