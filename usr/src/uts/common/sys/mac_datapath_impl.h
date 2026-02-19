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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef	_MAC_DATAPATH_IMPL_H
#define	_MAC_DATAPATH_IMPL_H

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

#define	likely(x)	__builtin_expect((x), 1)
#define	unlikely(x)	__builtin_expect((x), 0)

typedef struct {
	mblk_t		*mpl_head;
	mblk_t		*mpl_tail;
	uint32_t	mpl_count;
	size_t		mpl_size;
} mac_pkt_list_t;

__attribute__((always_inline))
inline size_t
mp_len(const mblk_t *mp)
{
	return ((mp->b_cont == NULL) ? MBLKL(mp) : msgdsize(mp));
}

__attribute__((always_inline))
inline bool
mac_pkt_list_is_empty(const mac_pkt_list_t *list)
{
	const bool out = list->mpl_head == NULL;
	ASSERT3B(out, ==, list->mpl_tail == NULL);
	ASSERT3B(out, ==, list->mpl_count == 0);
	/*
	 * list_empty => size == 0.
	 * One-way condition to keep bw_ctl out of this for now.
	 */
	ASSERT3B(!out, ||, (list->mpl_size == 0));
	return (out);
}

__attribute__((always_inline))
inline void
mac_pkt_list_extend(mac_pkt_list_t *src, mac_pkt_list_t *dst)
{
	if (mac_pkt_list_is_empty(src)) {
		return;
	}

	if (!mac_pkt_list_is_empty(dst)) {
		ASSERT3P(dst->mpl_tail->b_next, ==, NULL);
		dst->mpl_tail->b_next = src->mpl_head;
	} else {
		dst->mpl_head = src->mpl_head;
	}
	dst->mpl_tail = src->mpl_tail;
	dst->mpl_count += src->mpl_count;
	dst->mpl_size += src->mpl_size;

	src->mpl_head = NULL;
	src->mpl_tail = NULL;
	src->mpl_count = 0;
	src->mpl_size = 0;
}

__attribute__((always_inline))
inline void
mac_pkt_list_append_sz(mac_pkt_list_t *dst, mblk_t *mp, const size_t sz)
{
	ASSERT3P(mp, !=, NULL);
	ASSERT3U(sz, ==, mp_len(mp));

	if (!mac_pkt_list_is_empty(dst)) {
		ASSERT3P(dst->mpl_tail->b_next, ==, NULL);
		dst->mpl_tail->b_next = mp;
	} else {
		dst->mpl_head = mp;
	}
	dst->mpl_tail = mp;
	dst->mpl_size += sz;
	dst->mpl_count++;
}

__attribute__((always_inline))
inline void
mac_pkt_list_append(mac_pkt_list_t *dst, mblk_t *mp)
{
	mac_pkt_list_append_sz(dst, mp, mp_len(mp));
}

/*
 * Methods for reading parts of outermost MEOI facts in the domain covered by
 * `mac_standardise_pkts`.
 */
__attribute__((always_inline))
static inline ssize_t
meoi_fast_l2hlen(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L2INFO_SET) != 0) ? db->p_l2hlen: -1) :
	    (((db->t_flags & MEOI_L2INFO_SET) != 0) ? db->t_l2hlen: -1));
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
	    (((db->p_flags & MEOI_L2INFO_SET) != 0) ? db->p_l3proto: -1) :
	    (((db->t_flags & MEOI_L2INFO_SET) != 0) ? db->t_l3proto: -1));
}

static inline ssize_t
meoi_fast_l3hlen(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L3INFO_SET) != 0) ? db->p_l3hlen: -1) :
	    (((db->t_flags & MEOI_L3INFO_SET) != 0) ? db->t_l3hlen: -1));
}

static inline int16_t
meoi_fast_l4proto(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L3INFO_SET) != 0) ? db->p_l4proto: -1) :
	    (((db->t_flags & MEOI_L3INFO_SET) != 0) ? IPPROTO_UDP: -1));
}

static inline ssize_t
meoi_fast_l4hlen(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & MEOI_L4INFO_SET) != 0) ? db->p_l4hlen: -1) :
	    (((db->t_flags & MEOI_L4INFO_SET) != 0) ? 8: -1));
}

static inline ssize_t
meoi_fast_l4off(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	const mac_ether_offload_flags_t flags =
	    (MEOI_L2INFO_SET | MEOI_L3INFO_SET);
	return ((db->t_tuntype == 0) ?
	    (((db->p_flags & flags) == flags) ?
	    db->p_l2hlen + db->p_l3hlen: -1) :
	    (((db->t_flags & flags) == flags) ? db->t_l2hlen +
	    db->t_l3hlen: -1));
}

static inline bool
meoi_fast_fragmented(const mblk_t *mp)
{
	const packed_meoi_t *db = &mp->b_datap->db_meoi.pktinfo;
	return ((((db->t_tuntype == 0) ?
	    db->p_flags : db->t_flags) & 0x30) != 0);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _MAC_DATAPATH_IMPL_H */
