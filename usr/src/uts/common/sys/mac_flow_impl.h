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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef	_MAC_FLOW_IMPL_H
#define	_MAC_FLOW_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/time.h>
#include <sys/ksynch.h>
#include <sys/mac_flow.h>
#include <sys/stream.h>
#include <sys/sdt.h>
#include <sys/ethernet.h>
#include <net/if.h>
#include <sys/stdbool.h>
#include <sys/mac_datapath_impl.h>

#define	MAX_MAC_RX_SRS	 (MAX_RINGS_PER_GROUP + 1)

/*
 * Macros to increment/decrement the reference count on a flow_entry_t.
 */
#define	FLOW_REFHOLD(flent) {					\
	DTRACE_PROBE1(flow_refhold, flow_entry_t *, (flent));	\
	mutex_enter(&(flent)->fe_lock);				\
	(flent)->fe_refcnt++;					\
	mutex_exit(&(flent)->fe_lock);				\
}

/*
 * Data paths must not attempt to use a flow entry if it is marked INCIPIENT
 * or QUIESCE. In the former case the set up is not yet complete and the
 * data path could stumble on inconsistent data structures. In the latter
 * case a control operation is waiting for quiescence so that it can
 * change callbacks or other structures without the use of locks.
 */
#define	FLOW_TRY_REFHOLD(flent, err) {				\
	DTRACE_PROBE1(flow_refhold, flow_entry_t *, (flent));	\
	(err) = 0;						\
	mutex_enter(&(flent)->fe_lock);				\
	if ((flent)->fe_flags & (FE_INCIPIENT | FE_QUIESCE | FE_CONDEMNED | \
	    FE_UF_NO_DATAPATH | FE_MC_NO_DATAPATH))			\
		(err) = -1;					\
	else							\
		(flent)->fe_refcnt++;				\
	mutex_exit(&(flent)->fe_lock);				\
}

#define	FLOW_REFRELE(flent) {					\
	DTRACE_PROBE1(flow_refrele, flow_entry_t *, (flent));	\
	mutex_enter(&(flent)->fe_lock);				\
	ASSERT3U((flent)->fe_refcnt, !=, 0);			\
	(flent)->fe_refcnt--;					\
	if ((flent)->fe_flags & FE_WAITER) {			\
		ASSERT((flent)->fe_refcnt != 0);		\
		cv_signal(&(flent)->fe_cv);			\
		mutex_exit(&(flent)->fe_lock);			\
	} else if ((flent)->fe_refcnt == 0) {			\
		mac_flow_destroy(flent);			\
	} else {						\
		mutex_exit(&(flent)->fe_lock);			\
	}							\
}

#define	FLOW_USER_REFHOLD(flent) {			\
	mutex_enter(&(flent)->fe_lock);			\
	(flent)->fe_user_refcnt++;			\
	mutex_exit(&(flent)->fe_lock);			\
}

#define	FLOW_USER_REFRELE(flent) {			\
	mutex_enter(&(flent)->fe_lock);			\
	ASSERT3U((flent)->fe_user_refcnt, !=, 0);	\
	if (--(flent)->fe_user_refcnt == 0 &&		\
	    ((flent)->fe_flags & FE_WAITER))		\
		cv_signal(&(flent)->fe_cv);		\
	mutex_exit(&(flent)->fe_lock);			\
}

#define	FLOW_FINAL_REFRELE(flent) {			\
	ASSERT3U(flent->fe_refcnt, ==, 1);		\
	ASSERT3U(flent->fe_user_refcnt, ==, 0);		\
	FLOW_REFRELE(flent);				\
}

/*
 * Mark or unmark the flent with a bit flag
 */
#define	FLOW_MARK(flent, flag) {		\
	mutex_enter(&(flent)->fe_lock);		\
	(flent)->fe_flags |= flag;		\
	mutex_exit(&(flent)->fe_lock);		\
}

#define	FLOW_UNMARK(flent, flag) {		\
	mutex_enter(&(flent)->fe_lock);		\
	(flent)->fe_flags &= ~flag;		\
	mutex_exit(&(flent)->fe_lock);		\
}

#define	FLENT_TO_MIP(flent)			\
	(flent->fe_mbg != NULL ? mac_bcast_grp_mip(flent->fe_mbg) :	\
	((mac_client_impl_t *)flent->fe_mcip)->mci_mip)

/* Convert a bandwidth expressed in bps to a number of bytes per tick. */
#define	FLOW_BYTES_PER_TICK(bps)	(((bps) >> 3) / hz)

/*
 * Given an underlying range and a priority level, obtain the minimum for the
 * new range.
 */
#define	FLOW_MIN_PRIORITY(min, max, pri)	\
	((min) + ((((max) - (min)) / MRP_PRIORITY_LEVELS) * (pri)))

/*
 * Given an underlying range and a minimum level (base), obtain the maximum
 * for the new range.
 */
#define	FLOW_MAX_PRIORITY(min, max, base)	\
	((base) + (((max) - (min)) / MRP_PRIORITY_LEVELS))

/*
 * Given an underlying range and a priority level, get the absolute
 * priority value. For now there are just 3 values, high, low and
 * medium  so we can just return max, min or min + (max - min) / 2.
 * If there are more than three we need to change this computation.
 */
#define	FLOW_PRIORITY(min, max, pri)		\
	(pri) == MPL_HIGH ? (max) :	\
	(pri) == MPL_LOW ? (min) :	\
	((min) + (((max) - (min)) / 2))

#define	MAC_FLOW_TAB_SIZE		500

typedef struct flow_entry_s		flow_entry_t;
typedef struct flow_tree_s		flow_tree_t;
typedef struct flow_tab_s		flow_tab_t;
typedef struct flow_state_s		flow_state_t;
struct mac_impl_s;
struct mac_client_impl_s;
struct mac_soft_ring_set_s;

/*
 * Classification flags used to lookup the flow.
 */
#define	FLOW_INBOUND		0x01
#define	FLOW_OUTBOUND		0x02
/* Don't compare VID when classifying the packets, see mac_rx_classify() */
#define	FLOW_IGNORE_VLAN	0x04

/* Generic flow client function signature */
typedef void		(*flow_fn_t)(void *, void *, mblk_t *, boolean_t);

/* Flow state */
typedef enum {
	FLOW_DRIVER_UPCALL,
	FLOW_USER_REF
} mac_flow_state_t;

/* Matches a flow_entry_t using the extracted flow_state_t info */
typedef boolean_t	(*flow_match_fn_t)(flow_tab_t *, flow_entry_t *,
			    flow_state_t *);
typedef bool		(*flow_match_fn2_t)(void *, mblk_t *);

/* fe_flags */
#define	FE_QUIESCE		0x01	/* Quiesce the flow */
#define	FE_WAITER		0x02	/* Flow has a waiter */
#define	FE_FLOW_TAB		0x04	/* Flow is in the flow tab list */
#define	FE_G_FLOW_HASH		0x08	/* Flow is in the global flow hash */
#define	FE_INCIPIENT		0x10	/* Being setup */
#define	FE_CONDEMNED		0x20	/* Being deleted */
#define	FE_UF_NO_DATAPATH	0x40	/* No datapath setup for User flow */
#define	FE_MC_NO_DATAPATH	0x80	/* No datapath setup for mac client */

/* fe_type */
#define	FLOW_PRIMARY_MAC	0x01	/* NIC primary MAC address */
#define	FLOW_VNIC_MAC		0x02	/* VNIC flow */
#define	FLOW_MCAST		0x04	/* Multicast (and broadcast) */
#define	FLOW_OTHER		0x08	/* Other flows configured */
#define	FLOW_USER		0x10	/* User defined flow */
#define	FLOW_VNIC		FLOW_VNIC_MAC
#define	FLOW_NO_STATS		0x20	/* Don't create stats for the flow */

/*
 * Shared Bandwidth control counters between the soft ring set and its
 * associated soft rings. In case the flow associated with NIC/VNIC
 * has a group of Rx rings assigned to it, we have the same
 * number of soft ring sets as we have the Rx ring in the group
 * and each individual SRS (and its soft rings) decide when to
 * poll their Rx ring independently. But if there is a B/W limit
 * associated with the NIC/VNIC, then the B/W control counter is
 * shared across all the SRS in the group and their associated
 * soft rings.
 *
 * Bandwidth controls cause all affected SRSes (packet queues) to obey a shared
 * policing/shaping criteria:
 *  - Total queue occupancy beyond `mac_bw_drop_threshold` will lead to packet
 *    drops. (Policing)
 *  - All queues can, amongst themselves, admit at most `mac_bw_limit` bytes
 *    to their softrings per system tick. (Shaping)
 * The policing threshold is set today at 2 * mac_bw_limit.
 *
 * There is generally a many-to-1 mapping between SRSes and mac_bw_ctl. The Rx
 * path's software classifier and SRSes for hardware rings will necessarily
 * share a control, as will any logical SRSes for subflows reachable by several
 * classifier paths. In the Tx path, nested bandwidth limits on subflows with
 * hardware resources will cause a control to be shared.
 */
typedef struct mac_bw_ctl_s {
	kmutex_t	mac_bw_lock;
	uint8_t		mac_bw_state;
	size_t		mac_bw_sz;	/* Bytes enqueued in controlled SRSes */
	size_t		mac_bw_limit;	/* Max bytes to process per tick */
	size_t		mac_bw_used;	/* Bytes processed in current tick */
	size_t		mac_bw_drop_threshold; /* Max queue length */
	hrtime_t	mac_bw_curr_time;

	/* stats */
	uint64_t	mac_bw_drop_bytes;
	uint64_t	mac_bw_polled;
	uint64_t	mac_bw_intr;
} mac_bw_ctl_t;

/* mac_bw_state */
#define	BW_ENABLED	0x01
#define	BW_ENFORCED	0x02

typedef enum {
	MF_TYPE_MAC,	/* Flows created by MAC. */
	MF_TYPE_CLIENT,	/* Flows created by MAC clients. */
} mac_flow_type_t;

typedef enum {
	MFA_TYPE_DELIVER,
	MFA_TYPE_DROP,
	MFA_TYPE_DELEGATE,
} mac_flow_action_type_t;

typedef enum {
	MFM_NONE,
	MFM_SAP,
	MFM_IPPROTO,

	MFM_L2_DST,
	MFM_L2_SRC,
	MFM_L2_VID,

	MFM_L3_DST,
	MFM_L3_SRC,
	MFM_L3_REMOTE,
	MFM_L3_LOCAL,

	MFM_L4_DST,
	MFM_L4_SRC,
	MFM_L4_REMOTE,
	MFM_L4_LOCAL,

	MFM_ALL,
	MFM_ANY,
	MFM_ARBITRARY,

	/*
	 * TODO(ky): This is probably just an egregious hack to get *some*
	 * functionality...
	 */
	MFM_SUBFLOW,
} mac_flow_match_type_t;

typedef enum {
	MFC_NOFRAG = (1 << 0),
	MFC_UNICAST = (1 << 1),
} mac_flow_match_condition_t;

typedef struct mac_flow_match_list_s mac_flow_match_list_t;

typedef struct {
	flow_match_fn2_t	mfma_match;
	void			*mfma_arg;
} mac_flow_match_arbitrary_t;

typedef struct {
	in6_addr_t	mfml_addr;
	uint8_t		mfml_prefixlen: 5;
	uint8_t		mfml_v4: 1;
	uint8_t		mfml_unused: 2;
} mac_flow_match_l3_t;

typedef struct {
	mac_flow_match_type_t		mfm_type;
	mac_flow_match_condition_t	mfm_cond;
	union {
		uint16_t	mfm_sap;
		uint16_t	mfm_vid;
		uint8_t		mfm_ipproto;
		mac_flow_match_list_t		*mfm_list;
		mac_flow_match_arbitrary_t	mfm_arbitrary;
		uint8_t		mfm_l2addr[ETHERADDRL];
		mac_flow_match_l3_t	*mfm_l3addr;
		uint16_t	mfm_l4addr;
	} arg;
} mac_flow_match_t;

struct mac_flow_match_list_s {
	size_t			mfml_size;
	mac_flow_match_t	mfml_match[];
};

typedef struct {
	/*
	 * Every layer in the flow tree needs to keep two lists of packets:
	 *  - packets which have been taken by this layer, but which are
	 *    eligible to be picked by a child flow entry.
	 *  - packets which have been picked up by a child node and the action
	 *    is, quite definitively, to drop them off here. these should NOT
	 *    undergo any further processing.
	 */
	mac_pkt_list_t	ftp_avail;
	mac_pkt_list_t	ftp_deli;
} flow_tree_pkt_set_t;

typedef struct {
	flow_entry_t		*ften_flent;
	mac_flow_match_t	ften_match;
	bool			ften_descend;
	uint16_t		ften_skip;
} flow_tree_enter_node_t;

typedef struct {
	mac_flow_action_type_t	ftex_do;
	bool			ftex_ascend;
	union {
		/* MFA_TYPE_DELIVER | MFA_TYPE_DROP_DELEGATE */
		struct mac_soft_ring_set_s	*ftex_srs;
		/* MFA_TYPE_DROP */
		flow_entry_t	*ftex_flent; /* kept for stats */
	} arg;
} flow_tree_exit_node_t;

typedef union {
	flow_tree_enter_node_t enter;
	flow_tree_exit_node_t exit;
} flow_tree_baked_node_t;

typedef struct {
	mac_bw_ctl_t	*ftbr_bw;
	size_t		ftbr_size;
} flow_tree_bw_refund_t;

typedef struct {
	flow_tree_baked_node_t	*ftb_subtree;	/* len = 2 * ftb_len */
	uint16_t		ftb_depth;
	uint16_t		ftb_len;
	uint32_t		ftb_bw_count;
	flow_tree_pkt_set_t	*ftb_chains;	/* len = ftb_depth */
	flow_tree_bw_refund_t	*ftb_bw_refund;	/* len = ftb_depth */
} flow_tree_baked_t;

struct flow_entry_s {					/* Protected by */
	struct flow_entry_s	*fe_next;		/* ft_lock */

	datalink_id_t		fe_link_id;		/* WO */

	/*
	 * TODO(ky): each of the below resource_props accounts for 15KiB. Each!
	 *  This can't be replicated if we have many flents!!
	 *  Esp since fe_resource_props is the default.
	 */

	/* Properties as specified for this flow */
	mac_resource_props_t	fe_resource_props;	/* SL */

	/* Properties actually effective at run time for this flow */
	mac_resource_props_t	fe_effective_props;	/* SL */

	kmutex_t		fe_lock;
	char			fe_flow_name[MAXFLOWNAMELEN];	/* fe_lock */
	flow_desc_t		fe_flow_desc;		/* fe_lock */
	kcondvar_t		fe_cv;			/* fe_lock */
	/*
	 * Initial flow ref is 1 on creation. A thread that lookups the
	 * flent typically by a mac_flow_lookup() dynamically holds a ref.
	 * If the ref is 1, it means there arent' any upcalls from the driver
	 * or downcalls from the stack using this flent. Structures pointing
	 * to the flent or flent inserted in lists don't count towards this
	 * refcnt. Instead they are tracked using fe_flags. Only a control
	 * thread doing a teardown operation deletes the flent, after waiting
	 * for upcalls to finish synchronously. The fe_refcnt tracks
	 * the number of upcall refs
	 */
	uint32_t		fe_refcnt;		/* fe_lock */

	/*
	 * This tracks lookups done using the global hash list for user
	 * generated flows. This refcnt only protects the flent itself
	 * from disappearing and helps walkers to read the flent info such
	 * as flow spec. However the flent may be quiesced and the SRS could
	 * be deleted. The fe_user_refcnt tracks the number of global flow
	 * has refs.
	 */
	uint32_t		fe_user_refcnt;		/* fe_lock */
	uint_t			fe_flags;		/* fe_lock */

	/*
	 * Function/args to invoke for delivering matching packets
	 * Only the function ff_fn may be changed dynamically and atomically.
	 * The ff_arg1 and ff_arg2 are set at creation time and may not
	 * be changed.
	 */
	flow_fn_t		fe_cb_fn;		/* fe_lock */
	void			*fe_cb_arg1;		/* fe_lock */
	void			*fe_cb_arg2;		/* fe_lock */

	/*
	 * Flows can be tied to physical rings and/or MAC clients.
	 * When this is the case, we have softring sets which serve as valid
	 * entrypoints for packet delivery. These will be:
	 *  - an SRS for the software classifier for the MAC client.
	 *  - an SRS for each ring bound to this flow.
	 * fe_rx_srs contains a list of all such softring sets. These will be
	 * complete SRSes where packet delivery processing can occur.
	 *
	 * ?? TODO(ky) what of fe_tx_srs?
	 */
	void			*fe_client_cookie;	/* WO */
	void			*fe_rx_ring_group;	/* SL */
							/* fe_lock */
	struct mac_soft_ring_set_s	*fe_rx_srs[MAX_MAC_RX_SRS];
	uint16_t			fe_rx_srs_cnt;		/* fe_lock */
	void			*fe_tx_ring_group;
	struct mac_soft_ring_set_s	*fe_tx_srs;	/* WO */

	/*
	 * This is a unicast flow, and is a mac_client_impl_t
	 */
	void			*fe_mcip;		/* WO */

	/*
	 * Used by mci_flent_list of mac_client_impl_t to track flows sharing
	 * the same mac_client_impl_t.
	 */
	struct flow_entry_s	*fe_client_next;

	/*
	 * This is a broadcast or multicast flow and is a mac_bcast_grp_t
	 */
	void			*fe_mbg;		/* WO */
	uint_t			fe_type;		/* WO */

	/*
	 * BW control info.
	 */
	mac_bw_ctl_t		fe_tx_bw;
	mac_bw_ctl_t		fe_rx_bw;

	/*
	 * Used by flow table lookup code
	 */
	flow_match_fn_t		fe_match;

	/*
	 * Used by mac_flow_remove().
	 */
	int			fe_index;
	flow_tab_t		*fe_flow_tab;

	kstat_t			*fe_ksp;
	kstat_t			*fe_misc_stat_ksp;

	boolean_t		fe_desc_logged;
	uint64_t		fe_nic_speed;

	/*
	 * Used in flow tree construction.
	 */

	/* used to generate flow_fn_t entries for each softring */
	flow_action_t fe_action;

	/* differentiate flows created by MAC / clients / flowadm */
	mac_flow_type_t fe_owner_type;

	/* TODO(ky): swap out so this becomes the Real Thing */
	mac_flow_match_t fe_match2;

	/*
	 * Stats relating to bytes and packets *matching this flow entry
	 * explicitly*, modified when no matching SRS exists or to preserve
	 * counters from a condemned SRS.
	 *
	 * Modified/read atomically.
	 */
	uint64_t	fe_match_pkts_in;
	uint64_t	fe_match_bytes_in;
	uint64_t	fe_match_pkts_out;
	uint64_t	fe_match_bytes_out;

	/*
	 * Stats relating to bytes and packets *which this flow action has been
	 * used on*, modified when no matching SRS exists or to preserve
	 * counters from a condemned SRS.
	 *
	 * Modified/read atomically.
	 */
	uint64_t	fe_act_pkts_in;
	uint64_t	fe_act_bytes_in;
	uint64_t	fe_act_pkts_out;
	uint64_t	fe_act_bytes_out;
};

typedef struct flow_tree_s {
	flow_entry_t	*ft_flent;
	flow_tree_t	*ft_parent;
	flow_tree_t	*ft_sibling;
	flow_tree_t	*ft_child;

	/*
	 * If set to a value other than MFM_NONE, then use this matcher in place
	 * of that in ft_flent.
	 */
	mac_flow_match_t ft_match_override;
} flow_tree_t;

/*
 * Various structures used by the flows framework for keeping track
 * of packet state information.
 */

/* Layer 2 */
typedef struct flow_l2info_s {
	uchar_t		*l2_start;
	uint8_t		*l2_daddr;
	uint16_t	l2_vid;
	uint32_t	l2_sap;
	uint_t		l2_hdrsize;
} flow_l2info_t;

/* Layer 3 */
typedef struct flow_l3info_s {
	uchar_t		*l3_start;
	uint8_t		l3_protocol;
	uint8_t		l3_version;
	boolean_t	l3_dst_or_src;
	uint_t		l3_hdrsize;
	boolean_t	l3_fragmented;
} flow_l3info_t;

/* Layer 4 */
typedef struct flow_l4info_s {
	uchar_t		*l4_start;
	uint16_t	l4_src_port;
	uint16_t	l4_dst_port;
	uint16_t	l4_hash_port;
} flow_l4info_t;

/*
 * Combined state structure.
 * Holds flow direction and an mblk_t pointer.
 */
struct flow_state_s {
	uint_t		fs_flags;
	mblk_t		*fs_mp;
	flow_l2info_t	fs_l2info;
	flow_l3info_t	fs_l3info;
	flow_l4info_t	fs_l4info;
};

/*
 * Flow ops vector.
 * There are two groups of functions. The ones ending with _fe are
 * called when a flow is being added. The others (hash, accept) are
 * called at flow lookup time.
 */
#define	FLOW_MAX_ACCEPT	16
typedef struct flow_ops_s {
	/*
	 * fo_accept_fe():
	 * Validates the contents of the flow and checks whether
	 * it's compatible with the flow table. sets the fe_match
	 * function of the flow.
	 */
	int		(*fo_accept_fe)(flow_tab_t *, flow_entry_t *);
	/*
	 * fo_hash_fe():
	 * Generates a hash index to the flow table. This function
	 * must use the same algorithm as fo_hash(), which is used
	 * by the flow lookup code path.
	 */
	uint32_t	(*fo_hash_fe)(flow_tab_t *, flow_entry_t *);
	/*
	 * fo_match_fe():
	 * This is used for finding identical flows.
	 */
	boolean_t	(*fo_match_fe)(flow_tab_t *, flow_entry_t *,
			    flow_entry_t *);
	/*
	 * fo_insert_fe():
	 * Used for inserting a flow to a flow chain.
	 * Protocols that have special ordering requirements would
	 * need to implement this. For those that don't,
	 * flow_generic_insert_fe() may be used.
	 */
	int		(*fo_insert_fe)(flow_tab_t *, flow_entry_t **,
			    flow_entry_t *);

	/*
	 * Calculates the flow hash index based on the accumulated
	 * state in flow_state_t. Must use the same algorithm as
	 * fo_hash_fe().
	 */
	uint32_t	(*fo_hash)(flow_tab_t *, flow_state_t *);

	/*
	 * Array of accept fuctions.
	 * Each function in the array will accumulate enough state
	 * (header length, protocol) to allow the next function to
	 * proceed. We support up to FLOW_MAX_ACCEPT functions which
	 * should be sufficient for all practical purposes.
	 */
	int		(*fo_accept[FLOW_MAX_ACCEPT])(flow_tab_t *,
			    flow_state_t *);
} flow_ops_t;

/*
 * Generic flow table.
 */
struct flow_tab_s {
	krwlock_t		ft_lock;
	/*
	 * Contains a list of functions (described above)
	 * specific to this table type.
	 */
	flow_ops_t		ft_ops;

	/*
	 * Indicates what types of flows are supported.
	 */
	flow_mask_t		ft_mask;

	/*
	 * An array of flow_entry_t * of size ft_size.
	 * Each element is the beginning of a hash chain.
	 */
	flow_entry_t		**ft_table;
	uint_t			ft_size;

	/*
	 * The number of flows inserted into ft_table.
	 */
	uint_t			ft_flow_count;
	struct mac_impl_s	*ft_mip;
	struct mac_client_impl_s	*ft_mcip;
};

/*
 * This is used for describing what type of flow table can be created.
 * mac_flow.c contains a list of these structures.
 */
typedef struct flow_tab_info_s {
	flow_ops_t		*fti_ops;
	flow_mask_t		fti_mask;
	uint_t			fti_size;
} flow_tab_info_t;

#define	FLOW_TAB_EMPTY(ft)	((ft) == NULL || (ft)->ft_flow_count == 0)


#define	MCIP_STAT_UPDATE(m, s, c) {					\
	((mac_client_impl_t *)(m))->mci_misc_stat.mms_##s		\
	+= ((uint64_t)(c));						\
}

#define	SRS_RX_STAT_UPDATE(m, s, c)  {					\
	((mac_soft_ring_set_t *)(m))->srs_data.rx.sr_stat.mrs_##s	\
	+= ((uint64_t)(c));						\
}

#define	SRS_TX_STAT_UPDATE(m, s, c)  {					\
	((mac_soft_ring_set_t *)(m))->srs_data.tx.st_stat.mts_##s	\
	+= ((uint64_t)(c));						\
}

#define	SRS_TX_STATS_UPDATE(m, s) {					\
	SRS_TX_STAT_UPDATE((m), opackets, (s)->mts_opackets);		\
	SRS_TX_STAT_UPDATE((m), obytes, (s)->mts_obytes);		\
	SRS_TX_STAT_UPDATE((m), oerrors, (s)->mts_oerrors);		\
}

#define	SOFTRING_TX_STAT_UPDATE(m, s, c)  {				\
	((mac_soft_ring_t *)(m))->s_st_stat.mts_##s += ((uint64_t)(c));	\
}

#define	SOFTRING_TX_STATS_UPDATE(m, s) {				\
	SOFTRING_TX_STAT_UPDATE((m), opackets, (s)->mts_opackets);	\
	SOFTRING_TX_STAT_UPDATE((m), obytes, (s)->mts_obytes);		\
	SOFTRING_TX_STAT_UPDATE((m), oerrors, (s)->mts_oerrors);	\
}

extern void	mac_flow_init();
extern void	mac_flow_fini();
extern int	mac_flow_create(flow_desc_t *, mac_resource_props_t *,
		    char *, void *, uint_t, flow_entry_t **);

extern int	mac_flow_add(flow_tab_t *, flow_entry_t *);
extern int	mac_flow_add_subflow(mac_client_handle_t, flow_entry_t *,
		    boolean_t);
extern int	mac_flow_hash_add(flow_entry_t *);
extern int	mac_flow_lookup_byname(char *, flow_entry_t **);
extern int	mac_flow_lookup(flow_tab_t *, mblk_t *, uint_t,
		    flow_entry_t **);

extern int	mac_flow_walk(flow_tab_t *, int (*)(flow_entry_t *, void *),
		    void *);

extern int	mac_flow_walk_nolock(flow_tab_t *,
		    int (*)(flow_entry_t *, void *), void *);

extern void	mac_flow_modify(flow_tab_t *, flow_entry_t *,
		    mac_resource_props_t *);

extern void	*mac_flow_get_client_cookie(flow_entry_t *);

extern uint32_t	mac_flow_modify_props(flow_entry_t *, mac_resource_props_t *);

extern int	mac_flow_update(flow_tab_t *, flow_entry_t *, flow_desc_t *);
extern void	mac_flow_get_desc(flow_entry_t *, flow_desc_t *);
extern void	mac_flow_set_desc(flow_entry_t *, flow_desc_t *);

extern void	mac_flow_remove(flow_tab_t *, flow_entry_t *, boolean_t);
extern void	mac_flow_hash_remove(flow_entry_t *);
extern void	mac_flow_wait(flow_entry_t *, mac_flow_state_t);
extern void	mac_flow_quiesce(flow_entry_t *);
extern void	mac_flow_restart(flow_entry_t *);
extern void	mac_flow_cleanup(flow_entry_t *);
extern void	mac_flow_destroy(flow_entry_t *);

extern void	mac_flow_tab_create(flow_ops_t *, flow_mask_t, uint_t,
		    struct mac_impl_s *, flow_tab_t **);
extern void	mac_flow_l2tab_create(struct mac_impl_s *, flow_tab_t **);
extern void	mac_flow_tab_destroy(flow_tab_t *);
extern void	mac_flow_drop(void *, void *, mblk_t *);
extern void	flow_stat_destroy(flow_entry_t *);

extern void mac_flow_match_destroy(mac_flow_match_t *);
extern mac_flow_match_list_t *mac_flow_match_list_create(const size_t);
extern mac_flow_match_t mac_flow_clone_match(const mac_flow_match_t *);
extern void mac_flow_match_list_remove(mac_flow_match_t *, const size_t);

extern flow_tree_t *mac_flow_tree_node_create(flow_entry_t *);
extern void mac_flow_tree_node_destroy(flow_tree_t *);
extern void mac_flow_tree_destroy(flow_tree_t *);

extern bool mac_flow_action_validate(const flow_action_t *);

inline mac_flow_action_type_t
mac_flow_action_type(const flow_action_t *ac)
{
	ASSERT3P(ac, !=, NULL);
	if ((ac->fa_flags & MFA_FLAGS_ACTION) == 0) {
		return (MFA_TYPE_DELEGATE);
	}
	return ((ac->fa_direct_rx_fn == NULL) ?
	    MFA_TYPE_DROP : MFA_TYPE_DELIVER);
}

inline bool
mac_bw_ctl_is_enabled(const mac_bw_ctl_t *bw)
{
	ASSERT(MUTEX_HELD(&bw->mac_bw_lock));
	return ((bw->mac_bw_state & BW_ENABLED) != 0);
}

inline bool
mac_bw_ctl_is_enforced(const mac_bw_ctl_t *bw)
{
	ASSERT(MUTEX_HELD(&bw->mac_bw_lock));
	return ((bw->mac_bw_state & BW_ENFORCED) != 0);
}

inline void
mac_bw_ctls_lock(mac_bw_ctl_t **list, const size_t list_len)
{
	for (size_t i = 0; i < list_len; i++) {
		mutex_enter(&list[i]->mac_bw_lock);
	}
}

inline void
mac_bw_ctls_unlock(mac_bw_ctl_t **list, const size_t list_len)
{
	for (size_t i = 0; i < list_len; i++) {
		mutex_exit(&list[i]->mac_bw_lock);
	}
}

#ifdef	__cplusplus
}
#endif

#endif	/* _MAC_FLOW_IMPL_H */
