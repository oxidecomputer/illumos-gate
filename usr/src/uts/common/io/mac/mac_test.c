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
 * Copyright 2024 Ryan Zezeski
 */

/*
 * A test module for various mac routines.
 */
#include <inet/ip.h>
#include <inet/ip_impl.h>
#include <inet/tcp.h>
#include <inet/udp_impl.h>
#include <sys/dlpi.h>
#include <sys/ethernet.h>
#include <sys/ktest.h>
#include <sys/mac_client.h>
#include <sys/mac_provider.h>
#include <sys/mac_impl.h>
#include <sys/pattr.h>
#include <sys/sdt.h>
#include <sys/strsun.h>
#include <sys/vxlan.h>

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

typedef boolean_t (*mac_sw_cksum_ipv4_t)(mblk_t *, uint32_t, ipha_t *,
    const char **);
typedef int (*mac_partial_tun_info_t)(const mblk_t *, size_t,
    mac_ether_offload_info_t *);

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

void
mac_sw_cksum_ipv4_tcp_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	mac_sw_cksum_ipv4_t mac_sw_cksum_ipv4 = NULL;
	tcpha_t *tcp;
	ipha_t *ip;
	struct ether_header *eh;
	mblk_t *mp = NULL;
	char *msg = "...when it's not your turn";
	size_t msglen = strlen(msg) + 1;
	size_t mplen;
	const char *err = "";
	uint32_t sum;
	size_t ehsz = sizeof (*eh);
	size_t ipsz = sizeof (*ip);
	size_t tcpsz = sizeof (*tcp);

	if (ktest_hold_mod("mac", &hdl) != 0) {
		KT_ERROR(ctx, "failed to hold 'mac' module");
		return;
	}

	if (ktest_get_fn(hdl, "mac_sw_cksum_ipv4",
	    (void **)&mac_sw_cksum_ipv4) != 0) {
		KT_ERROR(ctx, "failed to resolve symbol %s`%s", "mac",
		    "mac_sw_cksum_ipv4");
		goto cleanup;
	}

	mplen = ehsz + ipsz + tcpsz + msglen;
	mp = allocb(mplen, 0);
	KT_EASSERT3P(mp, !=, NULL, ctx);
	mp->b_wptr = mp->b_rptr + mplen;
	tcp = mt_tcp_basic_hdr(mp, ehsz + ipsz, 2002, 2008, 1, 166, 0, 32000);
	ip = mt_ipv4_simple_hdr(mp, ehsz, tcpsz + msglen, 410, IPPROTO_TCP,
	    "192.168.2.4", "192.168.2.5", B_FALSE);
	eh = mt_ether_hdr(mp, 0, "f2:35:c2:72:26:57", "92:ce:5a:29:46:9d",
	    ETHERTYPE_IP);

	bcopy(msg, mp->b_rptr + ehsz + ipsz + tcpsz, msglen);

	/*
	 * It's important that we calculate the reference checksum
	 * first, because mac_sw_cksum_ipv4() populates the checksum
	 * field.
	 */
	sum = mt_pseudo_sum(IPPROTO_TCP, ip);
	sum = mt_rfc1071_sum(sum, (uint16_t *)(mp->b_rptr + ehsz + ipsz),
	    tcpsz + msglen);

	/*
	 * The internet checksum can never be 0xFFFF, as that would
	 * indicate an input of all zeros.
	 */
	KT_ASSERT3UG(sum, !=, 0xFFFF, ctx, cleanup);
	KT_ASSERTG(mac_sw_cksum_ipv4(mp, ehsz, ip, &err), ctx, cleanup);
	KT_ASSERT3UG(tcp->tha_sum, !=, 0xFFFF, ctx, cleanup);
	KT_ASSERT3UG(sum, ==, tcp->tha_sum, ctx, cleanup);
	KT_PASS(ctx);

cleanup:
	if (hdl != NULL) {
		ktest_release_mod(hdl);
	}

	if (mp != NULL) {
		freeb(mp);
	}
}

/*
 * Verify that an unexpected IP protocol results in the expect
 * failure.
 */
void
mac_sw_cksum_ipv4_bad_proto_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	mac_sw_cksum_ipv4_t mac_sw_cksum_ipv4 = NULL;
	tcpha_t *tcp;
	ipha_t *ip;
	struct ether_header *eh;
	mblk_t *mp = NULL;
	char *msg = "...when it's not your turn";
	size_t msglen = strlen(msg) + 1;
	size_t mplen;
	const char *err = "";
	size_t ehsz = sizeof (*eh);
	size_t ipsz = sizeof (*ip);
	size_t tcpsz = sizeof (*tcp);

	if (ktest_hold_mod("mac", &hdl) != 0) {
		KT_ERROR(ctx, "failed to hold 'mac' module");
		return;
	}

	if (ktest_get_fn(hdl, "mac_sw_cksum_ipv4",
	    (void **)&mac_sw_cksum_ipv4) != 0) {
		KT_ERROR(ctx, "failed to resolve symbol mac`mac_sw_cksum_ipv4");
		goto cleanup;
	}

	mplen = ehsz + ipsz + tcpsz + msglen;
	mp = allocb(mplen, 0);
	KT_EASSERT3P(mp, !=, NULL, ctx);
	mp->b_wptr = mp->b_rptr + mplen;
	tcp = mt_tcp_basic_hdr(mp, ehsz + ipsz, 2002, 2008, 1, 166, 0, 32000);
	ip = mt_ipv4_simple_hdr(mp, ehsz, tcpsz + msglen, 410, IPPROTO_ENCAP,
	    "192.168.2.4", "192.168.2.5", B_FALSE);
	eh = mt_ether_hdr(mp, 0, "f2:35:c2:72:26:57", "92:ce:5a:29:46:9d",
	    ETHERTYPE_IP);
	bcopy(msg, mp->b_rptr + ehsz + ipsz + tcpsz, msglen);
	KT_ASSERT0G(mac_sw_cksum_ipv4(mp, ehsz, ip, &err), ctx, cleanup);
	KT_PASS(ctx);

cleanup:
	if (hdl != NULL) {
		ktest_release_mod(hdl);
	}

	if (mp != NULL) {
		freeb(mp);
	}
}

typedef struct snoop_pkt_record_hdr {
	uint32_t	spr_orig_len;
	uint32_t	spr_include_len;
	uint32_t	spr_record_len;
	uint32_t	spr_cumulative_drops;
	uint32_t	spr_ts_secs;
	uint32_t	spr_ts_micros;
} snoop_pkt_record_hdr_t;

typedef struct snoop_pkt {
	uchar_t *sp_bytes;
	uint16_t sp_len;
} snoop_pkt_t;

typedef struct snoop_iter {
	uchar_t *sic_input;	/* beginning of stream */
	uintptr_t sic_end;	/* end of stream */
	uchar_t *sic_pos;	/* current position in stream */
	uint_t sic_pkt_num;	/* current packet number, 1-based */
	snoop_pkt_record_hdr_t *sic_pkt_hdr; /* current packet record header */
} snoop_iter_t;

#define	PAST_END(itr, len)	\
	(((uintptr_t)(itr)->sic_pos + len) > itr->sic_end)

/*
 * Get the next packet in the snoop stream iterator returned by
 * mt_snoop_iter_get(). A copy of the packet is returned via the pkt
 * pointer. The caller provides the snoop_pkt_t, and this function
 * allocates a new buffer inside it to hold a copy of the packet's
 * bytes. It is the responsibility of the caller to free the copy. It
 * is recommended the caller make use of desballoc(9F) along with the
 * snoop_pkt_free() callback. When all the packets in the stream have
 * been read all subsequent calls to this function will set sp_bytes
 * to NULL and sp_len to 0.
 *
 * The caller may optionally specify an rhdr argument in order to
 * receive a pointer to the packet record header (unlike the packet
 * bytes this is a pointer into the stream, not a copy).
 */
static int
mt_snoop_iter_next(ktest_ctx_hdl_t *ctx, snoop_iter_t *itr, snoop_pkt_t *pkt,
    snoop_pkt_record_hdr_t **rhdr)
{
	uchar_t *pkt_start;

	/*
	 * We've read exactly the number of bytes expected, this is
	 * the end.
	 */
	if ((uintptr_t)(itr->sic_pos) == itr->sic_end) {
		pkt->sp_bytes = NULL;
		pkt->sp_len = 0;

		if (rhdr != NULL)
			*rhdr = NULL;

		return (0);
	}

	/*
	 * A corrupted record or truncated stream could point us past
	 * the end of the stream.
	 */
	if (PAST_END(itr, sizeof (snoop_pkt_record_hdr_t))) {
		KT_ERROR(ctx, "record corrupted or stream truncated, read past "
		    "end of stream for record header #%d: 0x%p + %u > 0x%p",
		    itr->sic_pkt_num, itr->sic_pos,
		    sizeof (snoop_pkt_record_hdr_t), itr->sic_end);
		return (EIO);
	}

	itr->sic_pkt_hdr = (snoop_pkt_record_hdr_t *)itr->sic_pos;
	pkt_start = itr->sic_pos + sizeof (snoop_pkt_record_hdr_t);

	/*
	 * A corrupted record or truncated stream could point us past
	 * the end of the stream.
	 */
	if (PAST_END(itr, ntohl(itr->sic_pkt_hdr->spr_record_len))) {
		KT_ERROR(ctx, "record corrupted or stream truncated, read past "
		    "end of stream for record #%d: 0x%p + %u > 0x%p",
		    itr->sic_pkt_num, itr->sic_pos,
		    ntohl(itr->sic_pkt_hdr->spr_record_len), itr->sic_end);
		return (EIO);
	}

	pkt->sp_len = ntohl(itr->sic_pkt_hdr->spr_include_len);
	pkt->sp_bytes = kmem_zalloc(pkt->sp_len, KM_SLEEP);
	bcopy(pkt_start, pkt->sp_bytes, pkt->sp_len);
	itr->sic_pos += ntohl(itr->sic_pkt_hdr->spr_record_len);
	itr->sic_pkt_num++;

	if (rhdr != NULL) {
		*rhdr = itr->sic_pkt_hdr;
	}

	return (0);
}

/*
 * Parse a snoop data stream (RFC 1761) provided by input and return
 * a packet iterator to be used by mt_snoop_iter_next().
 */
static int
mt_snoop_iter_get(ktest_ctx_hdl_t *ctx, uchar_t *input, const uint_t input_len,
    snoop_iter_t **itr_out)
{
	const uchar_t id[8] = { 's', 'n', 'o', 'o', 'p', '\0', '\0', '\0' };
	uint32_t version;
	uint32_t datalink;
	snoop_iter_t *itr;

	*itr_out = NULL;

	if (input_len < 16) {
		KT_ERROR(ctx, "snoop stream truncated at file header: %u < %u ",
		    input_len, 16);
		return (ENOBUFS);
	}

	if (memcmp(input, &id, sizeof (id)) != 0) {
		KT_ERROR(ctx, "snoop stream malformed identification: %x %x %x "
		    "%x %x %x %x %x", input[0], input[1], input[2], input[3],
		    input[4], input[5], input[6], input[7]);
		return (EINVAL);
	}

	itr = kmem_zalloc(sizeof (*itr), KM_SLEEP);
	itr->sic_input = input;
	itr->sic_end = (uintptr_t)input + input_len;
	itr->sic_pos = input + sizeof (id);
	itr->sic_pkt_num = 1;
	itr->sic_pkt_hdr = NULL;
	version = ntohl(*(uint32_t *)itr->sic_pos);

	if (version != 2) {
		KT_ERROR(ctx, "snoop stream bad version: %u != %u", version, 2);
		return (EINVAL);
	}

	itr->sic_pos += sizeof (version);
	datalink = ntohl(*(uint32_t *)itr->sic_pos);

	/* We expect only Ethernet. */
	if (datalink != DL_ETHER) {
		KT_ERROR(ctx, "snoop stream bad datalink type: %u != %u",
		    datalink, DL_ETHER);
		kmem_free(itr, sizeof (*itr));
		return (EINVAL);
	}

	itr->sic_pos += sizeof (datalink);
	*itr_out = itr;
	return (0);
}

static void
snoop_pkt_free(snoop_pkt_t *pkt)
{
	kmem_free(pkt->sp_bytes, pkt->sp_len);
}

/*
 * Verify mac_sw_cksum_ipv4() against an arbitrary TCP stream read
 * from the snoop capture given as input. In order to verify the
 * checksum all TCP/IPv4 packets must be captured in full. The snoop
 * capture may contain non-TCP/IPv4 packets, which will be skipped
 * over. If not a single TCP/IPv4 packet is found, the test will
 * report an error.
 */
void
mac_sw_cksum_ipv4_snoop_test(ktest_ctx_hdl_t *ctx)
{
	ddi_modhandle_t hdl = NULL;
	mac_sw_cksum_ipv4_t mac_sw_cksum_ipv4 = NULL;
	uchar_t *bytes;
	size_t num_bytes = 0;
	uint_t pkt_num = 0;
	tcpha_t *tcp;
	ipha_t *ip;
	struct ether_header *eh;
	mblk_t *mp = NULL;
	const char *err = "";
	uint32_t csum;
	size_t ehsz, ipsz, tcpsz, msglen;
	snoop_iter_t *itr = NULL;
	snoop_pkt_record_hdr_t *hdr = NULL;
	boolean_t at_least_one = B_FALSE;
	snoop_pkt_t pkt;
	int ret;

	if (ktest_hold_mod("mac", &hdl) != 0) {
		KT_ERROR(ctx, "failed to hold 'mac' module");
		return;
	}

	if (ktest_get_fn(hdl, "mac_sw_cksum_ipv4",
	    (void **)&mac_sw_cksum_ipv4) != 0) {
		KT_ERROR(ctx, "failed to resolve symbol mac`mac_sw_cksum_ipv4");
		goto cleanup;
	}

	ktest_get_input(ctx, &bytes, &num_bytes);
	ret = mt_snoop_iter_get(ctx, bytes, num_bytes, &itr);
	if (ret != 0) {
		/* mt_snoop_iter_get() already set error context. */
		goto cleanup;
	}

	bzero(&pkt, sizeof (pkt));

	while ((ret = mt_snoop_iter_next(ctx, itr, &pkt, &hdr)) == 0) {
		frtn_t frtn;

		if (pkt.sp_len == 0) {
			break;
		}

		pkt_num++;

		/*
		 * Prepend the packet record number to any
		 * fail/skip/error message so the user knows which
		 * record in the snoop stream to inspect.
		 */
		ktest_msg_prepend(ctx, "pkt #%u: ", pkt_num);

		/* IPv4 only */
		if (hdr->spr_include_len < (sizeof (*eh) + sizeof (*ip))) {
			continue;
		}

		/* fully recorded packets only */
		if (hdr->spr_include_len != hdr->spr_orig_len) {
			continue;
		}

		frtn.free_func = snoop_pkt_free;
		frtn.free_arg = (caddr_t)&pkt;
		mp = desballoc(pkt.sp_bytes, pkt.sp_len, 0, &frtn);
		KT_EASSERT3PG(mp, !=, NULL, ctx, cleanup);
		mp->b_wptr += pkt.sp_len;
		eh = (struct ether_header *)mp->b_rptr;
		ehsz = sizeof (*eh);

		/* IPv4 only */
		if (ntohs(eh->ether_type) != ETHERTYPE_IP) {
			freeb(mp);
			mp = NULL;
			continue;
		}

		ip = (ipha_t *)(mp->b_rptr + ehsz);
		ipsz = sizeof (*ip);

		if (ip->ipha_protocol == IPPROTO_TCP) {
			tcp = (tcpha_t *)(mp->b_rptr + sizeof (*eh) +
			    sizeof (*ip));
			tcpsz = TCP_HDR_LENGTH(tcp);
			msglen = ntohs(ip->ipha_length) - (ipsz + tcpsz);

			/* Let's make sure we don't run off into space. */
			if ((tcpsz + msglen) > (pkt.sp_len - (ehsz + ipsz))) {
				KT_ERROR(ctx, "(tcpsz=%lu + msglen=%lu) > "
				    "(pkt_len=%lu - (ehsz=%lu + ipsz=%lu))",
				    tcpsz, msglen, pkt.sp_len, ehsz, ipsz);
				goto cleanup;
			}

			/*
			 * As we are reading a snoop input stream we
			 * need to make sure to zero out any existing
			 * checksum.
			 */
			tcp->tha_sum = 0;
			csum = mt_pseudo_sum(IPPROTO_TCP, ip);
			csum = mt_rfc1071_sum(csum,
			    (uint16_t *)(mp->b_rptr + ehsz + ipsz),
			    tcpsz + msglen);
		} else {
			freeb(mp);
			mp = NULL;
			continue;
		}

		/*
		 * The internet checksum can never be 0xFFFF, as that
		 * would indicate an input of all zeros.
		 */
		KT_ASSERT3UG(csum, !=, 0xFFFF, ctx, cleanup);
		KT_ASSERTG(mac_sw_cksum_ipv4(mp, ehsz, ip, &err), ctx, cleanup);
		KT_ASSERT3UG(tcp->tha_sum, !=, 0xFFFF, ctx, cleanup);
		KT_ASSERT3UG(tcp->tha_sum, ==, csum, ctx, cleanup);
		at_least_one = B_TRUE;
		freeb(mp);
		mp = NULL;

		/*
		 * Clear the prepended message for the iterator call
		 * as it already includes the current record number
		 * (and pkt_num is not incremented, thus incorrect,
		 * until after a successful call).
		 */
		ktest_msg_clear(ctx);
	}

	if (ret != 0) {
		/* mt_snoop_next() already set error context. */
		goto cleanup;
	}

	if (at_least_one) {
		KT_PASS(ctx);
	} else {
		ktest_msg_clear(ctx);
		KT_ERROR(ctx, "at least one TCP/IPv4 packet expected");
	}

cleanup:
	if (hdl != NULL) {
		ktest_release_mod(hdl);
	}

	if (mp != NULL) {
		freeb(mp);
	}

	if (itr != NULL) {
		kmem_free(itr, sizeof (*itr));
	}
}

typedef struct meoi_test_params {
	mblk_t				*mtp_mp;
	mac_ether_offload_info_t	mtp_partial;
	mac_ether_offload_info_t	mtp_results;
	uint_t				mtp_offset;
	boolean_t			mtp_is_err;
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

static int
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
		return (-1);
	}

	nvlist_t *results;
	if (nvlist_lookup_nvlist(params, "results", &results) != 0) {
		KT_ERROR(ctx, "Input missing results field");
		nvlist_free(params);
		return (-1);
	}

	if (test_partial) {
		nvlist_t *partial;
		if (nvlist_lookup_nvlist(params, "partial", &partial) != 0) {
			KT_ERROR(ctx, "Input missing partial field");
			nvlist_free(params);
			return (-1);
		} else {
			nvlist_to_meoi(partial, &mtp->mtp_partial);
		}

		(void) nvlist_lookup_uint32(params, "offset", &mtp->mtp_offset);
	}

	mtp->mtp_mp = alloc_split_pkt(ctx, params, "pkt_bytes");
	if (mtp->mtp_mp == NULL) {
		nvlist_free(params);
		return (-1);
	}

	nvlist_to_meoi(results, &mtp->mtp_results);
	mtp->mtp_is_err = nvlist_lookup_boolean(results, "is_err") == 0;

	nvlist_free(params);
	return (0);
}

void
mac_ether_offload_info_test(ktest_ctx_hdl_t *ctx)
{
	meoi_test_params_t mtp = { 0 };

	if (meoi_test_parse_input(ctx, &mtp, B_FALSE) != 0) {
		return;
	}

	mac_ether_offload_info_t result;
	const boolean_t is_err =
	    mac_ether_offload_info(mtp.mtp_mp, &result, NULL) != 0;

	KT_ASSERTG(is_err == mtp.mtp_is_err, ctx, done);
	const mac_ether_offload_info_t *expect = &mtp.mtp_results;
	KT_ASSERT3UG(result.meoi_flags, ==, expect->meoi_flags, ctx, done);
	KT_ASSERT3UG(result.meoi_l2hlen, ==, expect->meoi_l2hlen, ctx, done);
	KT_ASSERT3UG(result.meoi_l3proto, ==, expect->meoi_l3proto, ctx, done);
	KT_ASSERT3UG(result.meoi_l3hlen, ==, expect->meoi_l3hlen, ctx, done);
	KT_ASSERT3UG(result.meoi_l4proto, ==, expect->meoi_l4proto, ctx, done);
	KT_ASSERT3UG(result.meoi_l4hlen, ==, expect->meoi_l4hlen, ctx, done);

	KT_PASS(ctx);

done:
	freemsg(mtp.mtp_mp);
}

void
mac_partial_offload_info_test(ktest_ctx_hdl_t *ctx)
{
	meoi_test_params_t mtp = { 0 };

	if (meoi_test_parse_input(ctx, &mtp, B_TRUE) != 0) {
		return;
	}

	mac_ether_offload_info_t *result = &mtp.mtp_partial;
	const boolean_t is_err =
	    mac_partial_offload_info(mtp.mtp_mp, mtp.mtp_offset, result) != 0;

	KT_ASSERTG(is_err == mtp.mtp_is_err, ctx, done);
	const mac_ether_offload_info_t *expect = &mtp.mtp_results;
	KT_ASSERT3UG(result->meoi_flags, ==, expect->meoi_flags, ctx, done);
	KT_ASSERT3UG(result->meoi_l2hlen, ==, expect->meoi_l2hlen, ctx, done);
	KT_ASSERT3UG(result->meoi_l3proto, ==, expect->meoi_l3proto, ctx, done);
	KT_ASSERT3UG(result->meoi_l3hlen, ==, expect->meoi_l3hlen, ctx, done);
	KT_ASSERT3UG(result->meoi_l4proto, ==, expect->meoi_l4proto, ctx, done);
	KT_ASSERT3UG(result->meoi_l4hlen, ==, expect->meoi_l4hlen, ctx, done);

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

static int
ether_parse_input(ktest_ctx_hdl_t *ctx, ether_test_params_t *etp)
{
	uchar_t *bytes;
	size_t num_bytes = 0;

	ktest_get_input(ctx, &bytes, &num_bytes);
	bzero(etp, sizeof (*etp));

	nvlist_t *params = NULL;
	if (nvlist_unpack((char *)bytes, num_bytes, &params, KM_SLEEP) != 0) {
		KT_ERROR(ctx, "Invalid nvlist input");
		return (-1);
	}

	etp->etp_mp = alloc_split_pkt(ctx, params, "pkt_bytes");
	if (etp->etp_mp == NULL) {
		nvlist_free(params);
		return (-1);
	}

	if (nvlist_lookup_uint32(params, "tci", &etp->etp_tci) != 0) {
		KT_ERROR(ctx, "Input missing tci field");
		nvlist_free(params);
		return (-1);
	}

	uchar_t *dstaddr;
	uint_t dstaddr_sz;
	if (nvlist_lookup_byte_array(params, "dstaddr", &dstaddr,
	    &dstaddr_sz) != 0) {
		KT_ERROR(ctx, "Input missing dstaddr field");
		nvlist_free(params);
		return (-1);
	} else if (dstaddr_sz != ETHERADDRL) {
		KT_ERROR(ctx, "bad dstaddr size %u != %u", dstaddr_sz,
		    ETHERADDRL);
		nvlist_free(params);
		return (-1);
	}
	bcopy(dstaddr, &etp->etp_dstaddr, ETHERADDRL);

	etp->etp_is_err = nvlist_lookup_boolean(params, "is_err") == 0;

	nvlist_free(params);
	return (0);
}

void
mac_ether_l2_info_test(ktest_ctx_hdl_t *ctx)
{
	ether_test_params_t etp = { 0 };

	if (ether_parse_input(ctx, &etp) != 0) {
		return;
	}

	uint8_t dstaddr[ETHERADDRL];
	uint32_t vlan_tci = 0;
	const boolean_t is_err =
	    mac_ether_l2_info(etp.etp_mp, dstaddr, &vlan_tci) != 0;

	KT_ASSERTG(is_err == etp.etp_is_err, ctx, done);
	KT_ASSERTG(bcmp(dstaddr, etp.etp_dstaddr, ETHERADDRL) == 0, ctx,
	    done);
	KT_ASSERT3UG(vlan_tci, ==, etp.etp_tci, ctx, done);

	KT_PASS(ctx);

done:
	freemsg(etp.etp_mp);
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
	/* Assumption -- we'll be tunnelling Ethernet in these tests */
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
		err = mac_ether_clear_pktinfo(curr);
		KT_ASSERT3SG(err, ==, 0, ctx, cleanup);
		err = mac_ether_push_tun(curr, tuntype);
		KT_ASSERT3SG(err, ==, 0, ctx, cleanup);
		err = mac_ether_offload_info(curr, &outer_info, &inner_info);
		KT_ASSERT3SG(err, ==, 0, ctx, cleanup);

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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_LSO_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_LSO_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_LSO_EMUL);
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

	mac_hw_emul(&mp, NULL, NULL, MAC_HWCKSUM_EMUL | MAC_LSO_EMUL);
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

static struct modlmisc mac_test_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "mac ktest module"
};

static struct modlinkage mac_test_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &mac_test_modlmisc, NULL }
};

int
_init()
{
	int ret;
	ktest_module_hdl_t *km = NULL;
	ktest_suite_hdl_t *ks = NULL;

	VERIFY0(ktest_create_module("mac", &km));
	VERIFY0(ktest_add_suite(km, "checksum", &ks));
	VERIFY0(ktest_add_test(ks, "mac_sw_cksum_ipv4_tcp_test",
	    mac_sw_cksum_ipv4_tcp_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "mac_sw_cksum_ipv4_bad_proto_test",
	    mac_sw_cksum_ipv4_bad_proto_test, KTEST_FLAG_NONE));
	VERIFY0(ktest_add_test(ks, "mac_sw_cksum_ipv4_snoop_test",
	    mac_sw_cksum_ipv4_snoop_test, KTEST_FLAG_INPUT));
	VERIFY0(ktest_add_test(ks, "mac_sw_cksum_tun_ipv4_test",
	    mac_sw_cksum_tun_ipv4_test, KTEST_FLAG_NONE));

	VERIFY0(ktest_add_suite(km, "lso", &ks));
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

	if ((ret = ktest_register_module(km)) != 0) {
		ktest_free_module(km);
		return (ret);
	}

	if ((ret = mod_install(&mac_test_modlinkage)) != 0) {
		ktest_unregister_module("mac");
		return (ret);
	}

	return (0);
}

int
_fini(void)
{
	ktest_unregister_module("mac");
	return (mod_remove(&mac_test_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&mac_test_modlinkage, modinfop));
}
