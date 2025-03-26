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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2019 Joyent, Inc.
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _SYS_PATTR_H
#define	_SYS_PATTR_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Attribute types and structures.
 */
#define	PATTR_DSTADDRSAP	0x1	/* destination physical address+SAP */
#define	PATTR_SRCADDRSAP	0x2	/* source physical address+SAP */
#define	PATTR_HCKSUM		0x3	/* hardware checksum attribute */
#define	PATTR_ZCOPY		0x4	/* zerocopy attribute */

/*
 * Structure shared by {source,destination} physical address+SAP attributes.
 */
typedef struct pattr_addr_s {
	uint8_t	addr_is_group;	/* address is broadcast or multicast */
	uint8_t	addr_len;	/* length of address */
	uint8_t	addr[1];	/* address */
} pattr_addr_t;

/*
 * Structure used for Hardware Checksum attribute.
 */

typedef struct pattr_hcksum_s {
	uint32_t	hcksum_start_offset;
	uint32_t	hcksum_stuff_offset;
	uint32_t	hcksum_end_offset;
	union {
		uint64_t value;
		uint16_t inet_cksum; /* to store H/W computed cksum value */
	} hcksum_cksum_val;
	uint32_t	hcksum_flags;
} pattr_hcksum_t;

/*
 * Values for hcksum_flags
 */
#define	HCK_IPV4_HDRCKSUM	0x0001	/* On Transmit: Compute IP header */
					/* checksum in hardware. */

#define	HCK_IPV4_HDRCKSUM_OK	0x0001	/* On Receive: IP header checksum */
					/* was verified by h/w and is */
					/* correct. */

#define	HCK_PARTIALCKSUM	0x0002	/* On Transmit: Compute partial 1's */
					/* complement checksum based on */
					/* start, stuff and end offsets. */
					/* On Receive : Partial checksum */
					/* computed and attached. */

#define	HCK_FULLCKSUM		0x0004	/* On Transmit: Compute full(in case */
					/* of TCP/UDP, full is pseudo-header */
					/* + header + payload) checksum for */
					/* this packet. */
					/* On Receive : Full checksum  */
					/* computed in h/w and is attached */

#define	HCK_FULLCKSUM_OK	0x0008	/* On Transmit: N/A */
					/* On Receive: Full checksum status */
					/* If set, implies full checksum */
					/* computation was successful */
					/* i.e. checksum was correct. */
					/* If it is not set, IP will also */
					/* check the attached h/w computed */
					/* checksum value to determine if */
					/* checksum was bad */

#define	HCK_INNER_V4CKSUM	0x0020	/* On Transmit: Compute inner IPv4 */
					/* header checksum in hardware. */

#define	HCK_INNER_V4CKSUM_OK	0x0040	/* On Receive: inner IPv4 header */
					/* checksum was verified by h/w */

#define	HCK_INNER_PARTIAL	0x0080	/* On Transmit: Compute partial 1's */
					/* complement checksum for inner */
					/* frame TCP/UDP */

#define	HCK_INNER_FULL		0x0100	/* On Transmit: Compute full checksum */
					/* for this packet's inner TCP/UDP */
					/* layer. */
					/* On Receive: N/A */

#define	HCK_INNER_FULL_OK	0x0200	/* On Transmit: N/A */
					/* On Receive: L4 checksum status */
					/* If set, implies full checksum */
					/* computation was successful */
					/* i.e. checksum was correct on */
					/* inner TCP/UDP layer. */
					/* If it is not set, IP will also */
					/* check the attached h/w computed */
					/* checksum value to determine if */
					/* checksum was bad */

#define	HCK_FLAGS		(HCK_IPV4_HDRCKSUM | HCK_PARTIALCKSUM |	\
				HCK_FULLCKSUM | HCK_FULLCKSUM_OK | \
				HCK_INNER_V4CKSUM | HCK_INNER_V4CKSUM_OK | \
				HCK_INNER_PARTIAL | HCK_INNER_FULL | \
				HCK_INNER_FULL_OK)
#define	HCK_TX_FLAGS		(HCK_IPV4_HDRCKSUM | HCK_PARTIALCKSUM | \
				HCK_FULLCKSUM | HCK_INNER_V4CKSUM | \
				HCK_INNER_PARTIAL | HCK_INNER_FULL)
#define	HCK_OUTER_TX_FLAGS	(HCK_IPV4_HDRCKSUM | HCK_PARTIALCKSUM | \
				HCK_FULLCKSUM)
#define	HCK_INNER_TX_FLAGS	(HCK_INNER_V4CKSUM | HCK_INNER_PARTIAL | \
				HCK_INNER_FULL)
/*
 * Extended hardware offloading flags that also use hcksum_flags
 */
#define	HW_LSO			0x0010	/* On Transmit: hardware does LSO */
					/* On Receive: N/A */

#define	HW_LSO_FLAGS		HW_LSO	/* All LSO flags, currently only one */

/*
 * Structure used for zerocopy attribute.
 */
typedef struct pattr_zcopy_s {
	uint_t zcopy_flags;
} pattr_zcopy_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PATTR_H */
