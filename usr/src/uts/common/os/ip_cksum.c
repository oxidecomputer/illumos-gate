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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2021 Joyent, Inc.
 * Copyright 2022 Garrett D'Amore
 * Copyright 2025 Oxide Computer Company
 */
/* Copyright (c) 1990 Mentat Inc. */

#include <sys/types.h>
#include <sys/inttypes.h>
#include <sys/systm.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/debug.h>
#include <sys/ddi.h>
#include <sys/vtrace.h>
#include <inet/sctp_crc32.h>
#include <inet/ip.h>
#include <inet/ip6.h>

extern unsigned int ip_ocsum(ushort_t *, int, unsigned int);

/*
 * Checksum routine for Internet Protocol family headers.
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 */

#define	mp_len(mp) ((mp)->b_wptr - (mp)->b_rptr)

/*
 * Even/Odd checks. Usually it is performed on pointers but may be
 * used on integers as well. uintptr_t is long enough to hold both
 * integer and pointer.
 */
#define	is_odd(p) (((uintptr_t)(p) & 0x1) != 0)
#define	is_even(p) (!is_odd(p))


#ifdef ZC_TEST
/*
 * Disable the TCP s/w cksum.
 * XXX - This is just a hack for testing purpose. Don't use it for
 * anything else!
 */
int noswcksum = 0;
#endif
/*
 * Note: this does not ones-complement the result since it is used
 * when computing partial checksums.
 * For nonSTRUIO_IP mblks, assumes mp->b_rptr+offset is 16 bit aligned.
 * For STRUIO_IP mblks, assumes mp->b_datap->db_struiobase is 16 bit aligned.
 *
 * Note: for STRUIO_IP special mblks some data may have been previously
 *	 checksumed, this routine will handle additional data prefixed within
 *	 an mblk or b_cont (chained) mblk(s). This routine will also handle
 *	 suffixed b_cont mblk(s) and data suffixed within an mblk.
 */
unsigned int
ip_cksum(mblk_t *mp, int offset, uint_t sum)
{
	ushort_t *w;
	ssize_t	mlen;
	int pmlen;
	mblk_t *pmp;
	dblk_t *dp = mp->b_datap;
	ushort_t psum = 0;

#ifdef ZC_TEST
	if (noswcksum)
		return (0xffff);
#endif
	ASSERT(dp);

	if (mp->b_cont == NULL) {
		/*
		 * May be fast-path, only one mblk.
		 */
		w = (ushort_t *)(mp->b_rptr + offset);
		if (dp->db_struioflag & STRUIO_IP) {
			/*
			 * Checksum any data not already done by
			 * the caller and add in any partial checksum.
			 */
			if ((offset > dp->db_cksumstart) ||
			    mp->b_wptr != (uchar_t *)(mp->b_rptr +
			    dp->db_cksumend)) {
				/*
				 * Mblk data pointers aren't inclusive
				 * of uio data, so disregard checksum.
				 *
				 * not using all of data in dblk make sure
				 * not use to use the precalculated checksum
				 * in this case.
				 */
				dp->db_struioflag &= ~STRUIO_IP;
				goto norm;
			}
			ASSERT(mp->b_wptr == (mp->b_rptr + dp->db_cksumend));
			psum = *(ushort_t *)dp->db_struioun.data;
			if ((mlen = dp->db_cksumstart - offset) < 0)
				mlen = 0;
			if (is_odd(mlen))
				goto slow;
			if (mlen && dp->db_cksumstart != dp->db_cksumstuff &&
			    dp->db_cksumend != dp->db_cksumstuff) {
				/*
				 * There is prefix data to do and some uio
				 * data has already been checksumed and there
				 * is more uio data to do, so do the prefix
				 * data first, then do the remainder of the
				 * uio data.
				 */
				sum = ip_ocsum(w, mlen >> 1, sum);
				w = (ushort_t *)(mp->b_rptr +
				    dp->db_cksumstuff);
				if (is_odd(w)) {
					pmp = mp;
					goto slow1;
				}
				mlen = dp->db_cksumend - dp->db_cksumstuff;
			} else if (dp->db_cksumend != dp->db_cksumstuff) {
				/*
				 * There may be uio data to do, if there is
				 * prefix data to do then add in all of the
				 * uio data (if any) to do, else just do any
				 * uio data.
				 */
				if (mlen)
					mlen += dp->db_cksumend
					    - dp->db_cksumstuff;
				else {
					w = (ushort_t *)(mp->b_rptr +
					    dp->db_cksumstuff);
					if (is_odd(w))
						goto slow;
					mlen = dp->db_cksumend
					    - dp->db_cksumstuff;
				}
			} else if (mlen == 0)
				return (psum);

			if (is_odd(mlen))
				goto slow;
			sum += psum;
		} else {
			/*
			 * Checksum all data not already done by the caller.
			 */
		norm:
			mlen = mp->b_wptr - (uchar_t *)w;
			if (is_odd(mlen))
				goto slow;
		}
		ASSERT(is_even(w));
		ASSERT(is_even(mlen));
		return (ip_ocsum(w, mlen >> 1, sum));
	}
	if (dp->db_struioflag & STRUIO_IP)
		psum = *(ushort_t *)dp->db_struioun.data;
slow:
	DTRACE_PROBE(ip_cksum_slow);
	pmp = 0;
slow1:
	mlen = 0;
	pmlen = 0;
	for (; ; ) {
		/*
		 * Each trip around loop adds in word(s) from one mbuf segment
		 * (except for when pmp == mp, then its two partial trips).
		 */
		w = (ushort_t *)(mp->b_rptr + offset);
		if (pmp) {
			/*
			 * This is the second trip around for this mblk.
			 */
			pmp = 0;
			mlen = 0;
			goto douio;
		} else if (dp->db_struioflag & STRUIO_IP) {
			/*
			 * Checksum any data not already done by the
			 * caller and add in any partial checksum.
			 */
			if ((offset > dp->db_cksumstart) ||
			    mp->b_wptr != (uchar_t *)(mp->b_rptr +
			    dp->db_cksumend)) {
				/*
				 * Mblk data pointers aren't inclusive
				 * of uio data, so disregard checksum.
				 *
				 * not using all of data in dblk make sure
				 * not use to use the precalculated checksum
				 * in this case.
				 */
				dp->db_struioflag &= ~STRUIO_IP;
				goto snorm;
			}
			ASSERT(mp->b_wptr == (mp->b_rptr + dp->db_cksumend));
			if ((mlen = dp->db_cksumstart - offset) < 0)
				mlen = 0;
			if (mlen && dp->db_cksumstart != dp->db_cksumstuff) {
				/*
				 * There is prefix data too do and some
				 * uio data has already been checksumed,
				 * so do the prefix data only this trip.
				 */
				pmp = mp;
			} else {
				/*
				 * Add in any partial cksum (if any) and
				 * do the remainder of the uio data.
				 */
				int odd;
			douio:
				odd = is_odd(dp->db_cksumstuff -
				    dp->db_cksumstart);
				if (pmlen == -1) {
					/*
					 * Previous mlen was odd, so swap
					 * the partial checksum bytes.
					 */
					sum += ((psum << 8) & 0xffff)
					    | (psum >> 8);
					if (odd)
						pmlen = 0;
				} else {
					sum += psum;
					if (odd)
						pmlen = -1;
				}
				if (dp->db_cksumend != dp->db_cksumstuff) {
					/*
					 * If prefix data to do and then all
					 * the uio data nees to be checksumed,
					 * else just do any uio data.
					 */
					if (mlen)
						mlen += dp->db_cksumend
						    - dp->db_cksumstuff;
					else {
						w = (ushort_t *)(mp->b_rptr +
						    dp->db_cksumstuff);
						mlen = dp->db_cksumend -
						    dp->db_cksumstuff;
					}
				}
			}
		} else {
			/*
			 * Checksum all of the mblk data.
			 */
		snorm:
			mlen = mp->b_wptr - (uchar_t *)w;
		}

		mp = mp->b_cont;
		if (mlen > 0 && pmlen == -1) {
			/*
			 * There is a byte left from the last
			 * segment; add it into the checksum.
			 * Don't have to worry about a carry-
			 * out here because we make sure that
			 * high part of (32 bit) sum is small
			 * below.
			 */
#ifdef _LITTLE_ENDIAN
			sum += *(uchar_t *)w << 8;
#else
			sum += *(uchar_t *)w;
#endif
			w = (ushort_t *)((char *)w + 1);
			mlen--;
			pmlen = 0;
		}
		if (mlen > 0) {
			if (is_even(w)) {
				sum = ip_ocsum(w, mlen>>1, sum);
				w += mlen>>1;
				/*
				 * If we had an odd number of bytes,
				 * then the last byte goes in the high
				 * part of the sum, and we take the
				 * first byte to the low part of the sum
				 * the next time around the loop.
				 */
				if (is_odd(mlen)) {
#ifdef _LITTLE_ENDIAN
					sum += *(uchar_t *)w;
#else
					sum += *(uchar_t *)w << 8;
#endif
					pmlen = -1;
				}
			} else {
				ushort_t swsum;
#ifdef _LITTLE_ENDIAN
				sum += *(uchar_t *)w;
#else
				sum += *(uchar_t *)w << 8;
#endif
				mlen--;
				w = (ushort_t *)(1 + (uintptr_t)w);

				/* Do a separate checksum and copy operation */
				swsum = ip_ocsum(w, mlen>>1, 0);
				sum += ((swsum << 8) & 0xffff) | (swsum >> 8);
				w += mlen>>1;
				/*
				 * If we had an even number of bytes,
				 * then the last byte goes in the low
				 * part of the sum.  Otherwise we had an
				 * odd number of bytes and we take the first
				 * byte to the low part of the sum the
				 * next time around the loop.
				 */
				if (is_odd(mlen)) {
#ifdef _LITTLE_ENDIAN
					sum += *(uchar_t *)w << 8;
#else
					sum += *(uchar_t *)w;
#endif
				}
				else
					pmlen = -1;
			}
		}
		/*
		 * Locate the next block with some data.
		 * If there is a word split across a boundary we
		 * will wrap to the top with mlen == -1 and
		 * then add it in shifted appropriately.
		 */
		offset = 0;
		if (! pmp) {
			for (; ; ) {
				if (mp == 0) {
					goto done;
				}
				if (mp_len(mp))
					break;
				mp = mp->b_cont;
			}
			dp = mp->b_datap;
			if (dp->db_struioflag & STRUIO_IP)
				psum = *(ushort_t *)dp->db_struioun.data;
		} else
			mp = pmp;
	}
done:
	/*
	 * Add together high and low parts of sum
	 * and carry to get cksum.
	 * Have to be careful to not drop the last
	 * carry here.
	 */
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	TRACE_3(TR_FAC_IP, TR_IP_CKSUM_END,
	    "ip_cksum_end:(%S) type %d (%X)", "ip_cksum", 1, sum);
	return (sum);
}

uint32_t
sctp_cksum(mblk_t *mp, int offset)
{
	uint32_t crc32;
	uchar_t *p = NULL;

	crc32 = 0xFFFFFFFF;
	p = mp->b_rptr + offset;
	crc32 = sctp_crc32(crc32, p, mp->b_wptr - p);
	for (mp = mp->b_cont; mp != NULL; mp = mp->b_cont) {
		crc32 = sctp_crc32(crc32, mp->b_rptr, MBLKL(mp));
	}

	/* Complement the result */
	crc32 = ~crc32;

	return (crc32);
}

/* Return the IP checksum for the IP header at "iph". */
uint16_t
ip_csum_hdr(ipha_t *ipha)
{
	uint16_t	*uph;
	uint32_t	sum;
	int		opt_len;

	opt_len = (ipha->ipha_version_and_hdr_length & 0xF) -
	    IP_SIMPLE_HDR_LENGTH_IN_WORDS;
	uph = (uint16_t *)ipha;
	sum = uph[0] + uph[1] + uph[2] + uph[3] + uph[4] +
	    uph[5] + uph[6] + uph[7] + uph[8] + uph[9];
	if (opt_len > 0) {
		do {
			sum += uph[10];
			sum += uph[11];
			uph += 2;
		} while (--opt_len);
	}
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = ~(sum + (sum >> 16)) & 0xFFFF;
	if (sum == 0xffff)
		sum = 0;
	return ((uint16_t)sum);
}

/*
 * This function takes an mblk and IPv6 header as input and returns
 * three pieces of information.
 *
 * 'hdr_length_ptr': The IPv6 header length including extension headers.
 *
 * 'nethdrpp': A pointer to the "next hedader" value, aka the
 *             transport header. This argument may be set to NULL if
 *             only the length is desired.
 *
 * return: Whether or not the header is well formed.
 *
 * This function assumes the IPv6 header along with all extensions are
 * contained solely in this mblk: i.e., there is no b_cont walking.
 */
boolean_t
ip_hdr_length_nexthdr_v6(mblk_t *mp, ip6_t *ip6h, uint16_t *hdr_length_ptr,
    uint8_t **nexthdrpp)
{
	uint16_t length;
	uint_t	ehdrlen;
	uint8_t	*nexthdrp;
	uint8_t *whereptr;
	uint8_t *endptr;
	ip6_dest_t *desthdr;
	ip6_rthdr_t *rthdr;
	ip6_frag_t *fraghdr;

	if (IPH_HDR_VERSION(ip6h) != IPV6_VERSION)
		return (B_FALSE);
	length = IPV6_HDR_LEN;
	whereptr = ((uint8_t *)&ip6h[1]); /* point to next hdr */
	endptr = mp->b_wptr;

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
			break;
		case IPPROTO_NONE:
			/* No next header means we're finished */
		default:
			*hdr_length_ptr = length;

			if (nexthdrpp != NULL)
				*nexthdrpp = nexthdrp;

			return (B_TRUE);
		}
		length += ehdrlen;
		whereptr += ehdrlen;
		*hdr_length_ptr = length;

		if (nexthdrpp != NULL)
			*nexthdrpp = nexthdrp;
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
		*hdr_length_ptr = length;

		if (nexthdrpp != NULL)
			*nexthdrpp = nexthdrp;

		return (B_TRUE);
	}
}
