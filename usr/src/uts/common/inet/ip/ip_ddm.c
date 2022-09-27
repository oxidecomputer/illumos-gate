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
 * Copyright 2023 Oxide Computer Company
 */

/*
 * This file contains implementation functions for delay driven multipath.
 */

#include <sys/zone.h>
#include <sys/ethernet.h>
#include <sys/mac.h>
#include <inet/ddm.h>
#include <inet/ip6.h>
#include <inet/ip_ire.h>
#include <inet/ip_ndp.h>
#include <inet/ip_if.h>
#include <inet/ipclassifier.h>
#include <netinet/ip6.h>

/* TODO make all of the below tunable */
#define	DDM_MAX_RADIX 8
#define	DDM_AGEING_STEP 5
#define	DDM_MAX_DELAY 100000 /* 100 ms */

void
ddm_send_ack(const ip6_t *ip6h, const ip6_ddm_t *ddh, const ip_recv_attr_t *ira)
{
	/*
	 * Bail on multcast packets, need to determine what a good source
	 * address for these is.
	 */
	if (IN6_IS_ADDR_MULTICAST(&ip6h->ip6_dst)) {
		return;
	}

	/* TODO(ry) Handle VLAN header. */
	/* Allocate and link up message blocks. */
	mblk_t *mp = allocb(
	    sizeof (struct ether_header) + sizeof (ip6_t) + ddm_total_len(ddh),
	    BPRI_HI);

	/* TODO(ry) Handle VLAN header. */
	/* Create the ethernet header. */
	struct ether_header *ack_eth = (struct ether_header *)mp->b_wptr;
	bcopy(
	    ira->ira_mhip->mhi_saddr,
	    ack_eth->ether_dhost.ether_addr_octet,
	    ETHERADDRL);
	bcopy(
	    ira->ira_mhip->mhi_daddr,
	    ack_eth->ether_shost.ether_addr_octet,
	    ETHERADDRL);
	ack_eth->ether_type = htons(ETHERTYPE_IPV6);
	mp->b_wptr = (unsigned char *)&ack_eth[1];

	/* Create the ipv6 header. */
	ip6_t *ack_ip6 = (ip6_t *)mp->b_wptr;
	ack_ip6->ip6_vcf = ip6h->ip6_vcf;
	ack_ip6->ip6_plen = htons(ddm_total_len(ddh));
	ack_ip6->ip6_nxt = IPPROTO_DDM;
	ack_ip6->ip6_hlim = ddm_element_count(ddh);
	ack_ip6->ip6_src = ip6h->ip6_dst;
	ack_ip6->ip6_dst = ip6h->ip6_src;
	mp->b_wptr = (unsigned char *)&ack_ip6[1];

	/* Create the ddm extension header. */
	ip6_ddm_t *ack_ddh = (ip6_ddm_t *)mp->b_wptr;
	*ack_ddh = *ddh;
	ack_ddh->ddm_next_header = IPPROTO_NONE;
	ddm_set_ack(ack_ddh);
	/* Add elements, an ack includes all the received elements. */
	ddm_element_t *src = (ddm_element_t *)&ddh[1];
	ddm_element_t *dst = (ddm_element_t *)&ack_ddh[1];
	memcpy(dst, src, ddm_elements_len(ddh));
	mp->b_wptr = (unsigned char *)&dst[ddm_element_count(ddh)];

	/* Set up transmit attributes. */
	ip_xmit_attr_t ixa;
	bzero(&ixa, sizeof (ixa));
	ixa.ixa_ifindex = ira->ira_rifindex;
	ixa.ixa_ipst = ira->ira_rill->ill_ipst;
	ixa.ixa_flags = IXAF_BASIC_SIMPLE_V6;
	ixa.ixa_flags &= ~IXAF_VERIFY_SOURCE;
	ixa.ixa_xmit_hint = ira->ira_xmit_hint;

	/* Send out the ack. */
	putnext(ira->ira_rill->ill_wq, mp);
}

uint32_t
ddm_ts_now()
{
	uint64_t value = gethrtime();
	value /= 1000;   /* to microseconds */
	value %= DDM_MAX_TS; /* roll over */
	return (value);
}

/*
 * It's assumed we are no further than 16 seconds apart. If that's where we are,
 * ddm timestamps are the least of our problems.
 */
static uint32_t
ts_diff(uint32_t now, uint32_t before)
{
	uint32_t value;
	if (before > now) {
		value = before - now;
	} else {
		value = now - before;
	}
	return (value > DDM_MAX_DELAY ? DDM_MAX_DELAY : value);
}

void
ddm_update(
    const ip6_t *dst,
    const ill_t *ill,
    uint32_t ifindex,
    uint32_t timestamp)
{
	/*
	 * Look up routing table entry.
	 *
	 * While it's tempting to constrain this lookup
	 * to routes that are defined on the ill from
	 * whence the update packet came there are
	 * situations where we don't want this. For
	 * example when the route has no ill because the
	 * source address is tied to a loopback device.
	 */
	ire_t *ire = ire_ftable_lookup_v6(
	    &dst->ip6_src,
	    NULL,		/* TODO mask */
	    NULL,		/* TODO gateway */
	    0,			/* TODO type */
	    NULL,		/* ill */
	    ALL_ZONES,		/* TODO zone */
	    NULL,		/* TODO tsl */
	    0,			/* flags */
	    0,			/* TODO xmit_hint */
	    ill->ill_ipst,
	    NULL		/* TODO generationop */);

	if (!ire) {
		DTRACE_PROBE1(ddm__update__no__route,
		    in_addr_t *, &dst->ip6_dst);
		return;
	}

	DTRACE_PROBE2(ddm__update_timestamp,
	    in_addr_t *, &dst->ip6_dst,
	    uint32_t, ifindex);

	/* Update routing table entry delay measurement. */
	uint32_t now = ddm_ts_now();

	mutex_enter(&ire->ire_lock);
	ire->ire_delay = ts_diff(now, timestamp);
	mutex_exit(&ire->ire_lock);

	ire_refrele(ire);
}

/*
 * This is the xorshift* random number generator.
 * https://en.wikipedia.org/wiki/Xorshift
 *
 * This is a very hot path and the idea here is to use as light weight of an RNG
 * as possible.
 */
uint64_t
ddm_rnd_next(uint64_t x)
{
	/*
	 * Zero is a stable state that will stay zero forever, which is not
	 * great for a random number.
	 */
	if (x == 0) {
		x = 1;
	}
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	return (x * 0x2545F4914F6CDD1DULL);
}

/*
 * Iterate through the chain and select the entry with the smallest delay
 * probabalistically. The probability is the inverse relative proability of a
 * given path. For example if there are two delays of 70 and 30 microseconds,
 * the 70 microsecond path is taken with probability 0.3 and the 30 microsecond
 * path is taken with probability 0.7.
 */
ire_t *
ddm_select_prob(const ire_t *chain)
{
	rw_enter(&chain->ire_bucket->irb_lock, RW_READER);

	ire_t *ire = chain->ire_bucket->irb_ire;
	ire_t *chosen = NULL;
	uint32_t n = chain->ire_bucket->irb_ire_cnt;
	/* Delays are 24 bits, this should not overflow. */
	uint64_t sum = 0;
	size_t i = 0;
	uint64_t pm[DDM_MAX_RADIX];

	n = n < DDM_MAX_RADIX ? n : DDM_MAX_RADIX;

	/* Lock all the candidate ires up front. */
	for (i = 0; i < n; i++) {
		mutex_enter(&ire->ire_lock);
		ire = ire->ire_next;
	}
	ire = chain->ire_bucket->irb_ire;

	/* Sum up all the delays. */
	for (i = 0; i < n; i++) {
		/*
		 * If we have no delay data on this path, take it in order to
		 * measure.
		 */
		if (ire->ire_delay == 0) {
			chosen = ire;
			ire = chain->ire_bucket->irb_ire;
			goto done;
		}
		/*
		 * XXX: start with smallest delay, should not be needed as the
		 * probability pick mechanism should be complete, but that is
		 * apparently not the case right now as there seem to be caeses
		 * where we do not pick based on probability and thus there are
		 * holes.
		 */
		if (chosen == NULL || ire->ire_delay < chosen->ire_delay) {
			chosen = ire;
		}
		sum += ire->ire_delay;
		ire = ire->ire_next;
	}
	ire = chain->ire_bucket->irb_ire;

	/*
	 * Create a probability map.
	 *
	 * The structure of this map is the following for N entries.
	 *
	 * pm[0] -> 0..P(0)
	 * pm[1] -> pm[0]..pm[0]+P(1)
	 * ...
	 * pm[n] -> pm[n-1]..pm[n-1]+P(n)
	 *
	 * Where P(i) is the relative probability of choosing route i which
	 * comes from the equation.
	 *
	 *	P(i) = delay_i / delay_sum
	 *
	 * P(i) is defined over 0..(1<<64) as a domain.
	 *
	 */
	for (i = 0; i < n; i++) {
		uint64_t q = 0xffffffffffffffffULL / sum;
		q *= ire->ire_delay;
		/* inverse probability */
		q = 0xffffffffffffffffULL - q;
		if (i > 0) {
			q += pm[i-1];
			/* saturating add */
			if (q < pm[i-1]) {
				q = -1;
			}
		}
		pm[i] = q;
		ire = ire->ire_next;
	}
	ire = chain->ire_bucket->irb_ire;

	/* Choose an ire. */
	uint64_t *p = &chain->ire_bucket->ddm_rnd;
	*p = ddm_rnd_next(*p);
	for (i = 0; i < n; i++) {
		if (*p <= pm[i]) {
			chosen = ire;
			break;
		}
		ire = ire->ire_next;
	}
	ire = chain->ire_bucket->irb_ire;
	ASSERT(chosen != NULL);

done:
	/* Unlock all the candidate ires. */
	for (i = 0; i < n; i++) {
		mutex_exit(&ire->ire_lock);
		ire = ire->ire_next;
	}

	rw_exit(&chain->ire_bucket->irb_lock);

	DTRACE_PROBE3(ddm__route__select,
	    char *, chosen->ire_ill->ill_name,
	    in6_addr_t *, &chosen->ire_u.ire6_u.ire6_addr,
	    in6_addr_t *, &chosen->ire_u.ire6_u.ire6_gateway_addr);

	ire_refhold(chosen);
	return (chosen);
}

/*
 * Iterate through the chain and select the entry with the smallest delay.
 */
ire_t *
ddm_select_simple(const ire_t *chain)
{
	rw_enter(&chain->ire_bucket->irb_lock, RW_READER);

	ire_t *ire = chain->ire_bucket->irb_ire;
	ire_t *chosen = NULL;

	while (ire != NULL) {
		mutex_enter(&ire->ire_lock);

		/*
		 * Introduce delay ageing. Have delays age each time we consider
		 * them. This way routes we are not sending packets down don't
		 * get ignored completely. Their delay will continuously fall
		 * each time they are passed on for route selection. Eventually
		 * the delay will fall below other candidates and force
		 * selection. If the route still has large delay this will bump
		 * the link delay back up. This has the nice property of
		 * checking back on links every so often at a frequency that is
		 * relative to observed delays.
		 *
		 * TODO: This is just a starting point. It's likely better
		 * accomplished by simply recording the time at which a delay
		 * measured and subtracting from that time here to age
		 * continuously.
		 */
		if (ire->ire_delay > 0) {
			ire->ire_delay -= DDM_AGEING_STEP;
		}

		if (chosen == NULL || ire->ire_delay < chosen->ire_delay) {
			if (chosen != NULL) {
				mutex_exit(&chosen->ire_lock);
			}
			chosen = ire;
		} else {
			mutex_exit(&ire->ire_lock);
		}

		ire = ire->ire_next;
	}
	mutex_exit(&chosen->ire_lock);

	rw_exit(&chain->ire_bucket->irb_lock);

	DTRACE_PROBE3(ddm__route__select,
	    char *, chosen->ire_ill->ill_name,
	    in6_addr_t *, &chosen->ire_u.ire6_u.ire6_addr,
	    in6_addr_t *, &chosen->ire_u.ire6_u.ire6_gateway_addr);

	ire_refhold(chosen);
	return (chosen);
}

static void
ddm_xmit_ipp_enable(conn_t *connp, const ip_xmit_attr_t *ixa)
{
	connp->conn_xmit_ipp.ipp_fields |= IPPF_DDMHDR;
	/* TODO(ry): ensure free */
	connp->conn_xmit_ipp.ipp_ddmhdr = kmem_alloc(8, KM_NOSLEEP);
	connp->conn_xmit_ipp.ipp_ddmhdrlen = 8;
}

void
ddm_xmit_ipp(conn_t *connp, const ip_xmit_attr_t *ixa)
{
	/*
	 * If this is IPv6, the underlying interface has ddm enabled and the
	 * destination is off-link, set the ddm ipp field so we:
	 *
	 *   1. Calculate header lengths propertly in ip_total_hdrs_len_v6.
	 *   2. Have the ddm header filled in from ip_build_hdrs_v6.
	 *
	 */
	if (!(ixa->ixa_flags & IXAF_IS_IPV4) &&
	    (ixa->ixa_ire != NULL) &&
	    (ixa->ixa_ire->ire_ill != NULL) &&
	    (ixa->ixa_ire->ire_ill->ill_ipif != NULL) &&
	    (ixa->ixa_ire->ire_ill->ill_ipif->ipif_flags & IPIF_DDM) != 0 &&
	    (ixa->ixa_ire->ire_type & IRE_ONLINK) == 0) {
		ddm_xmit_ipp_enable(connp, ixa);
	}
}

int
ddm_set_element(const conn_t *connp, uchar_t *p, mblk_t *mp)
{
	const ip_pkt_t *ipp = &connp->conn_xmit_ipp;
	ip6_ddm_t *ddm;
	uint_t off = 0;

	if ((connp->conn_xmit_ipp.ipp_fields & IPPF_DDMHDR) == 0) {
		return (0);
	}

	/* TODO(ry) verify order */
	if (ipp->ipp_fields & IPPF_HOPOPTS) {
		off += ipp->ipp_hopoptslen;
	}
	if (ipp->ipp_fields & IPPF_RTHDRDSTOPTS) {
		off += ipp->ipp_rthdrdstoptslen;
	}
	if (ipp->ipp_fields & IPPF_RTHDR) {
		off += ipp->ipp_rthdrlen;
	}
	if (ipp->ipp_fields & IPPF_DSTOPTS) {
		off += ipp->ipp_dstoptslen;
	}
	if (ipp->ipp_fields & IPPF_FRAGHDR) {
		off += ipp->ipp_fraghdrlen;
	}

	if ((p + off + 8) > mp->b_wptr) {
		return (EMSGSIZE);
	}
	ddm = (ip6_ddm_t *)(p + off);
	ddm_element_t *dde = (ddm_element_t *)&ddm[1];
	*dde = ddm_ts_now() << 8;

	return (0);
}
