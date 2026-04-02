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

#ifndef	_MAC_DATAPATH_IMPL_H
#define	_MAC_DATAPATH_IMPL_H

/*
 * Methods and datatypes involved in packet processing and classification in
 * MAC's datapath.
 *
 * These rely on various internals, and may only be valid for use within certain
 * portions of the datapath.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/debug.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/mac_provider.h>
#include <netinet/udp.h>

typedef struct {
	mblk_t		*mpl_head;
	mblk_t		*mpl_tail;
	uint32_t	mpl_count;
	size_t		mpl_size;
} mac_pkt_list_t;

static inline __ALWAYS_INLINE size_t
mp_len(const mblk_t *mp)
{
	return ((mp->b_cont == NULL) ? MBLKL(mp) : msgdsize(mp));
}

static inline __ALWAYS_INLINE bool
mac_pkt_list_is_empty(const mac_pkt_list_t *list)
{
	const bool empty = list->mpl_head == NULL;
	ASSERT3B(empty, ==, list->mpl_tail == NULL);
	ASSERT3B(empty, ==, list->mpl_count == 0);
	/*
	 * One-way implication to leave bandwidth mode out of consideration.
	 */
	IMPLY(empty, list->mpl_size == 0);
	return (empty);
}

static inline __ALWAYS_INLINE void
mac_pkt_list_append_list(mac_pkt_list_t *into, mac_pkt_list_t *from)
{
	if (mac_pkt_list_is_empty(from)) {
		return;
	}

	if (!mac_pkt_list_is_empty(into)) {
		ASSERT3P(into->mpl_tail->b_next, ==, NULL);
		into->mpl_tail->b_next = from->mpl_head;
	} else {
		into->mpl_head = from->mpl_head;
	}
	into->mpl_tail = from->mpl_tail;
	into->mpl_count += from->mpl_count;
	into->mpl_size += from->mpl_size;

	from->mpl_head = NULL;
	from->mpl_tail = NULL;
	from->mpl_count = 0;
	from->mpl_size = 0;
}

static inline __ALWAYS_INLINE void
mac_pkt_list_append_pkt(mac_pkt_list_t *into, mblk_t *mp, const size_t sz)
{
	ASSERT3P(mp, !=, NULL);
	ASSERT3U(sz, ==, mp_len(mp));

	if (!mac_pkt_list_is_empty(into)) {
		ASSERT3P(into->mpl_tail->b_next, ==, NULL);
		into->mpl_tail->b_next = mp;
	} else {
		into->mpl_head = mp;
	}
	into->mpl_tail = mp;
	into->mpl_size += sz;
	into->mpl_count++;
}

static inline __ALWAYS_INLINE void
mac_pkt_list_append(mac_pkt_list_t *into, mblk_t *mp)
{
	mac_pkt_list_append_pkt(into, mp, mp_len(mp));
}

/*
 * Methods for reading parts of outermost MEOI facts in the domain covered by
 * `mac_standardise_pkts`.
 *
 * These should only be called when we can guarantee that MEOI is stored within
 * mp's dblk_t using mac_ether_set_pktinfo(), and are intended purely for
 * mac_sched.c's use in packet classification. In this context we want to avoid
 * the overhead of calling into mac_ether_offload_info per-(packet, matcher)
 * combination.
 */
static inline __ALWAYS_INLINE ssize_t
meoi_fast_l2hlen(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L2INFO_SET) != 0) ? db->p_l2hlen : -1) :
	    (((db->t_flags & MEOI_L2INFO_SET) != 0) ? db->t_l2hlen : -1));
}

static inline bool
meoi_fast_is_vlan(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    ((db->p_flags & MEOI_VLAN_TAGGED) != 0) :
	    ((db->t_flags & MEOI_VLAN_TAGGED) != 0));
}

static inline int32_t
meoi_fast_l3proto(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L2INFO_SET) != 0) ? db->p_l3proto : -1) :
	    (((db->t_flags & MEOI_L2INFO_SET) != 0) ? db->t_l3proto : -1));
}

static inline ssize_t
meoi_fast_l3hlen(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L3INFO_SET) != 0) ? db->p_l3hlen : -1) :
	    (((db->t_flags & MEOI_L3INFO_SET) != 0) ? db->t_l3hlen : -1));
}

static inline int16_t
meoi_fast_l4proto(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L3INFO_SET) != 0) ? db->p_l4proto : -1) :
	    (((db->t_flags & MEOI_L3INFO_SET) != 0) ? IPPROTO_UDP : -1));
}

static inline ssize_t
meoi_fast_l4hlen(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L4INFO_SET) != 0) ? db->p_l4hlen : -1) :
	    (((db->t_flags & MEOI_L4INFO_SET) != 0) ? sizeof (struct udphdr) :
	    -1));
}

static inline ssize_t
meoi_fast_l4off(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	const mac_ether_offload_flags_t flags =
	    (MEOI_L2INFO_SET | MEOI_L3INFO_SET);
	if (db->t_tuntype == 0) {
		if ((db->p_flags & flags) == flags) {
			return (db->p_l2hlen + db->p_l3hlen);
		} else {
			return (-1);
		}
	} else {
		if ((db->t_flags & flags) == flags) {
			return (db->t_l2hlen + db->t_l3hlen);
		} else {
			return (-1);
		}
	}
}

static inline bool
meoi_fast_fragmented(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	/*
	 * p_flags will omit `MEOI_TUNINFO_SET`, and t_flags will omit
	 * `MEOI_L4INFO_SET` from the packed representation. Shift by one
	 * to account for this.
	 */
	const mac_ether_offload_flags_t flags =
	    (MEOI_L3_FRAG_MORE | MEOI_L3_FRAG_OFFSET) >> 1;
	return ((((db->t_tuntype == 0) ?
	    db->p_flags : db->t_flags) & flags) != 0);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _MAC_DATAPATH_IMPL_H */
