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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef	_MAC_DATAPATH_IMPL_H
#define	_MAC_DATAPATH_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/stream.h>

typedef struct {
	mblk_t		*mpl_head;
	mblk_t		*mpl_tail;
	uint32_t	mpl_count;
	size_t		mpl_size;
} mac_pkt_list_t;

inline bool
mac_pkt_list_is_empty(const mac_pkt_list_t *list) {
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

inline void
mac_pkt_list_append(mac_pkt_list_t *src, mac_pkt_list_t *dst) {
	if (mac_pkt_list_is_empty(src)) {
		return;
	}

	dst->mpl_count += src->mpl_count;
	dst->mpl_size += src->mpl_size;
	if (!mac_pkt_list_is_empty(dst)) {
		dst->mpl_tail->b_next = src->mpl_head;
	} else {
		dst->mpl_head = src->mpl_head;
	}
	dst->mpl_tail = src->mpl_tail;
}

#ifdef	__cplusplus
}
#endif

#endif	/* _MAC_DATAPATH_IMPL_H */
