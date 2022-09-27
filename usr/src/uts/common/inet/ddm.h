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
 * The ddm protocol embeds hop-by-hop time stamp information in IPv6 extension
 * headers. The ddm extension header has a fixed 4-byte portion that is always
 * present, followed by a variable sized list of elements. There may be between
 * 0 and 15 elements in a single ddm extension header. Ddm over greater than 15
 * hops is not currently supported. If the need arises the 15 element limit per
 * ddm extension header will not change, rather extension headers must be
 * chained. This is to keep in line with the recommendations of RFC 6564 for
 * IPv6 extension headers.
 *
 *           0               0               1               2               3
 *           0               8               6               4               2
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     0x00 |  Next Header  | Header Length |    Version    |A|  Reserved   |
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     0x04 |     0.Id      |           0.Timestamp                         |
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     0x08 |     1.Id      |           1.Timestamp                         |
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *          |     ...       |                ...                            |
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *          |     ...       |                ...                            |
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * (N+1)<<2 |     N.Id      |           N.Timestamp                         :
 *          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Fixed header fields have the following semantics:
 *
 *   Next Header:   IANA IP protocol number of the next header.
 *
 *   Header Length: Length of the ddm header and all elements in 8-octet units
 *                  not including the leading Next Header byte. Follows
 *                  convention established in RFC 6564.
 *
 *   Version:       Version of the ddm protocol.
 *
 *   A:             Acknowledgement bit. A value of 1 indicates this is an
 *                  acknowledgement, 0 otherwise.
 *
 *   Reserved:      Reserved for future use.
 *
 * Element fields have the following semantics
 *
 *   Id:        Identifier for the node that produced this element.
 *
 *   Timestamp: Time this element was produced. This is an opaque 24-bit value
 *              that is only meaningful to the producer of the time stamp.
 */

#ifndef	_INET_DDM_H
#define	_INET_DDM_H

#include <sys/stream.h>
#include <inet/ip.h>

/* maximum timestamp size */
#define	DDM_MAX_TS (1<<24)

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * True if the ddm header is an acknowledgement.
 */
static __GNU_INLINE boolean_t
ddm_is_ack(const ip6_ddm_t *ddh)
{
	return ((ddh->ddm_reserved & 1) != 0);
}

/*
 * Set the ddm header acknowledgement bit.
 */
static __GNU_INLINE void
ddm_set_ack(ip6_ddm_t *ddh)
{
	ddh->ddm_reserved |= 1;
}

/*
 * Return the total length of the ddm header, including time stamp elements.
 */
static __GNU_INLINE uint16_t
ddm_total_len(const ip6_ddm_t *ddh)
{
	/* Ddm header length field + 1 for the leading 8 bits (RFC 6564). */
	return (ddh->ddm_length + 1);
}

/*
 * Return the length of the time stamp elements in bytes.
 */
static __GNU_INLINE uint8_t
ddm_elements_len(const ip6_ddm_t *ddh)
{
	return (ddh->ddm_length - 3);
}

/*
 * Return the total number of time stamp elements.
 */
static __GNU_INLINE uint8_t
ddm_element_count(const ip6_ddm_t *ddh)
{
	/*
	 * Subtract out the ddm header and divide by 4 (ddm elements are 4 bytes
	 * wide).
	 */
	return (ddm_elements_len(ddh) >> 2);
}

/*
 * First 8 bits are an origin host id, last 24 bits are a time stamp. Timestamp
 * is only meaningful to the host that generated it.
 */
typedef uint32_t ddm_element_t;

#ifdef _KERNEL

/*
 * Update the ddm delay tracking table.
 */
extern void ddm_update(const ip6_t *, const ill_t *, uint32_t, uint32_t);

/*
 * Select an ire to use from an ire chain based on delay measurements.
 */
extern ire_t *ddm_select_simple(const ire_t *);

/*
 * Select an ire to use from an ire chain based on delay measurements based on a
 * probability function.
 */
extern ire_t *ddm_select_prob(const ire_t *);

/*
 * Get the current ddm time stamp.
 */
extern uint32_t ddm_ts_now();

/*
 * If the connection and ixa imply that packets being sent on this connection
 * should have ddm extension headers, set the appropriate fields to make this
 * happen in conn_t`conn_xmit_ipp.
 */
extern void ddm_xmit_ipp(conn_t *, const ip_xmit_attr_t *);

/*
 * Send a ddm acknowledgement packet for the given ipv6/ddm header pair. The ira
 * is used to send the ack out the interface the packet being acknowledged
 * arrived on.
 */
extern void ddm_send_ack(const ip6_t *, const ip6_ddm_t *,
    const ip_recv_attr_t *);

/*
 * Set the time stamp field of the ddm element in the given message block.
 */
extern int ddm_set_element(const conn_t *, uchar_t *, mblk_t *);

#endif /* _KERNEL */

/*
 * Extract node id from an ddm element.
 */
static __GNU_INLINE uint8_t
ddm_element_id(ddm_element_t e)
{
	return ((uint8_t)e);
}

/*
 * Extract 24 bit time stamp from a ddm element.
 */
static __GNU_INLINE uint32_t
ddm_element_timestamp(ddm_element_t e)
{
	return (e >> 8);
}


#ifdef	__cplusplus
}
#endif

#endif /* _INET_DDM_H */
