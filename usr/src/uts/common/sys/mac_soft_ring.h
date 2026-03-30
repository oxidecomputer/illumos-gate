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
 * Copyright 2017 Joyent, Inc.
 * Copyright 2026 Oxide Computer Company
 */

#ifndef	_SYS_MAC_SOFT_RING_H
#define	_SYS_MAC_SOFT_RING_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/stddef.h>
#include <sys/types.h>
#include <sys/cpuvar.h>
#include <sys/cpupart.h>
#include <sys/processor.h>
#include <sys/stream.h>
#include <sys/squeue.h>
#include <sys/dlpi.h>
#include <sys/mac_impl.h>
#include <sys/mac_stat.h>
#include <sys/atomic.h>

#define	S_RING_NAMELEN	64

#define	MAX_SR_FANOUT	24

extern boolean_t mac_soft_ring_enable;
extern boolean_t mac_latency_optimize;

typedef struct mac_soft_ring_s mac_soft_ring_t;
typedef struct mac_soft_ring_set_s mac_soft_ring_set_t;

/* Tx notify callback */
typedef struct mac_tx_notify_cb_s {
	mac_cb_t		mtnf_link;	/* Linked list of callbacks */
	mac_tx_notify_t		mtnf_fn;	/* The callback function */
	void			*mtnf_arg;	/* Callback function argument */
} mac_tx_notify_cb_t;

/*
 * Flagset of immutable and datapath-altered aspects of a softring.
 *
 * Flags prefixed by `ST_` identify static characteristics of how a ring should
 * process packets, whereas those prefixed `S_RING` reflect the current state
 * of datapath processing.
 *
 * Gaps in flag allocation correspond to former flag definitions (such that
 * existing flags mapped to their historic values). New flags can be placed in
 * these gaps without issue. See issue 17920.
 */
typedef enum {
	/*
	 * Packets may only be drained from this softring by its own worker
	 * thread, and cannot be handled inline by the SRS or its caller.
	 *
	 * Immutable.
	 */
	ST_RING_WORKER_ONLY	= 1 << 0,
	/*
	 * This Rx softring is known to an upstack client, which may invoke any
	 * `mac_rx_fifo_t` operations (direct polling, disable/re-enable inline
	 * delivery).
	 *
	 * `s_ring_rx_arg2` must be non-null.
	 *
	 * Mutable. Requires !`ST_RING_TX`.
	 */
	ST_RING_POLLABLE	= 1 << 1,
	/*
	 * If set, this is a transmit softring. Packets will be directed via
	 * `mac_tx_send` to an underlying provider's ring.
	 *
	 * If absent, this is a receive softring. Packets will be delivered to a
	 * client via `s_ring_rx_func`.
	 *
	 * Immutable.
	 */
	ST_RING_TX		= 1 << 6,
	/*
	 * A thread is currently processing packets from this softring, and has
	 * relinquished its hold on `s_ring_lock` to allow more packets to be
	 * enqueued while it does so.
	 *
	 * Rx/Tx process methods will always enqueue packets if set, with the
	 * expectation that whoever is draining the thread will continue to
	 * do so.
	 */
	S_RING_PROC		= 1 << 16,
	/*
	 * The worker thread of this softring has been bound to a specific CPU.
	 */
	S_RING_BOUND		= 1 << 17,
	/*
	 * This softring is a TX softring and has run out of descriptors on the
	 * underlying ring/NIC.
	 *
	 * Any outbound packets will be queued until the underlying provider
	 * marks more descriptors as available via `mac_tx_ring_update`.
	 */
	S_RING_BLOCK		= 1 << 18,
	/*
	 * This softring is a TX softring and is flow controlled: more than
	 * `s_ring_tx_hiwat` packets are currently enqueued.
	 *
	 * Any outbound packets will be enqueued, and drained by the softring
	 * worker. Senders will receive a cookie -- they will be informed when
	 * any cookie is no longer flow controlled if they have registered a
	 * callback via `mac_client_tx_notify`.
	 */
	S_RING_TX_HIWAT		= 1 << 19,
	/*
	 * This softring is a TX softring and has returned a cookie to at least
	 * one sender who has set `MAC_TX_NO_ENQUEUE` regardless of watermark
	 * state.
	 *
	 * When the softring is drained, notify the client via its
	 * `mac_client_tx_notify` callback that it may send.
	 */
	S_RING_WAKEUP_CLIENT	= 1 << 20,
	/*
	 * This RX softring is client pollable and its client has called
	 * `mac_soft_ring_intr_disble` to stop MAC from delivering frames via
	 * `s_ring_rx_func`.
	 *
	 * Packets may _only_ be delivered by client polling. The client may
	 * undo this using `mac_soft_ring_intr_enable`.
	 */
	S_RING_BLANK		= 1 << 21,
	/*
	 * Request the thread processing packets to notify a waiting client when
	 * it is safe to alter the `s_ring_rx_func` callback and its arguments.
	 */
	S_RING_CLIENT_WAIT	= 1 << 22,
	/*
	 * This softring is marked for deletion.
	 *
	 * No further packets can be admitted into the softring, and enqueued
	 * packets must not be processed.
	 */
	S_RING_CONDEMNED	= 1 << 24,
	/*
	 * The softring worker has completed any teardown in response to
	 * `S_RING_CONDEMNED`.
	 *
	 * Requires `S_RING_QUIESCE_DONE`.
	 */
	S_RING_CONDEMNED_DONE	= 1 << 25,
	/*
	 * This softring has been signalled to stop processing any packets.
	 *
	 * The presence of this flag implies that the parent SRS has
	 * *also* been asked to quiesce. It will not enqueue any packets here.
	 */
	S_RING_QUIESCE		= 1 << 26,
	/*
	 * The softring has ceased processing any enqueued/arriving packets, and
	 * is awaiting a signal of either `S_RING_CONDEMNED` or `S_RING_RESTART`
	 * to wake up.
	 */
	S_RING_QUIESCE_DONE	= 1 << 27,
	/*
	 * The softring has been signalled to resume processing traffic.
	 *
	 * The worker thread should unset this and any `QUIESCE` flags and
	 * resume processing packets.
	 */
	S_RING_RESTART		= 1 << 28,
	/*
	 * This TX softring has packets enqueued, which the worker thread is
	 * responsible for draining.
	 */
	S_RING_ENQUEUED		= 1 << 29,
} mac_soft_ring_state_t;

/*
 * Used to verify whether a given value is allowed to be used as the
 * `type` of a softring during creation.
 */
#define	SR_STATE	0xffff0000

/*
 * A MAC soft ring.
 *
 * This represents a queue of packets which have underwent any rate control
 * or fanout, and are ready to be passed up to a client or handed down to the
 * NIC.
 */
struct mac_soft_ring_s {
	/* Keep the most used members 64bytes cache aligned */
	kmutex_t	s_ring_lock;	/* lock before using any member */
	mac_soft_ring_state_t	s_ring_state;	/* processing model and state */
	uint32_t	s_ring_count;	/* # of mblocks in mac_soft_ring */
	size_t		s_ring_size;	/* Size of data queued */
	mblk_t		*s_ring_first;	/* first mblk chain or NULL */
	mblk_t		*s_ring_last;	/* last mblk chain or NULL */

	/* Protected by s_ring_lock + !S_RING_PROC */
	mac_direct_rx_t		s_ring_rx_func;
	void			*s_ring_rx_arg1;
	mac_resource_handle_t	s_ring_rx_arg2;

	/* 64B */

	/*
	 * Threshold after which packets get dropped.
	 * Is always greater than s_ring_tx_hiwat
	 */
	uint32_t	s_ring_tx_max_q_cnt;
	/* # of mblocks after which to apply flow control */
	uint32_t	s_ring_tx_hiwat;
	/* # of mblocks after which to relieve flow control */
	uint32_t	s_ring_tx_lowat;
	boolean_t	s_ring_tx_woken_up;
	uint32_t	s_ring_hiwat_cnt;	/* times blocked for Tx descs */

	/* Arguments for `mac_tx_send`, called by `mac_tx_soft_ring_drain` */
	mac_client_impl_t	*s_ring_tx_arg1;
	mac_ring_t		*s_ring_tx_arg2;

	/* Tx notify callback */
	mac_cb_info_t	s_ring_notify_cb_info;		/* cb list info */
	mac_cb_t	*s_ring_notify_cb_list;		/* The cb list */

	clock_t		s_ring_awaken;	/* time async thread was awakened */

	kthread_t	*s_ring_run;	/* Current thread processing sq */
	processorid_t	s_ring_cpuid;	/* processor to bind to */
	processorid_t	s_ring_cpuid_save;	/* saved cpuid during offline */
	kcondvar_t	s_ring_async;	/* async thread blocks on */
	clock_t		s_ring_wait;	/* lbolts to wait after a fill() */
	timeout_id_t	s_ring_tid;	/* timer id of pending timeout() */
	kthread_t	*s_ring_worker;	/* kernel thread id */
	char		s_ring_name[S_RING_NAMELEN + 1];
	uint64_t	s_ring_total_inpkt;
	uint64_t	s_ring_total_rbytes;
	uint64_t	s_ring_drops;
	mac_client_impl_t *s_ring_mcip;
	kstat_t		*s_ring_ksp;

	/* Teardown, poll disable control ops */
	kcondvar_t	s_ring_client_cv; /* Client wait for control op */

	mac_soft_ring_set_t *s_ring_set;   /* The SRS this ring belongs to */
	mac_soft_ring_t	*s_ring_next;
	mac_soft_ring_t	*s_ring_prev;

	mac_tx_stats_t	s_st_stat;
};

/*
 * Soft ring set (SRS) Tx modes.
 *
 * There are seven modes of operation on the Tx side. These modes get set
 * in mac_tx_srs_setup(). Except for the experimental TX_SERIALIZE mode,
 * none of the other modes are user configurable. They get selected by
 * the system depending upon whether the link (or flow) has multiple Tx
 * rings or a bandwidth configured, or if the link is an aggr, etc.
 *
 * When the Tx SRS is operating in aggr mode (st_mode) or if there are
 * multiple Tx rings owned by Tx SRS, then each Tx ring (pseudo or
 * otherwise) will have a soft ring associated with it. These soft rings
 * are stored in srs_soft_rings[] array.
 *
 * Additionally in the case of aggr, there is the st_soft_rings[] array
 * in the mac_srs_tx_t structure. This array is used to store the same
 * set of soft rings that are present in srs_tx_soft_rings[] array but
 * in a different manner. The soft ring associated with the pseudo Tx
 * ring is saved at mr_index (of the pseudo ring) in st_soft_rings[]
 * array. This helps in quickly getting the soft ring associated with the
 * Tx ring when aggr_find_tx_ring() returns the pseudo Tx ring that is to
 * be used for transmit.
 */
typedef enum {
	SRS_TX_DEFAULT = 0,
	SRS_TX_SERIALIZE,
	SRS_TX_FANOUT,
	SRS_TX_BW,
	SRS_TX_BW_FANOUT,
	SRS_TX_AGGR,
	SRS_TX_BW_AGGR
} mac_tx_srs_mode_t;

/*
 * Members specific to transmit SRSes.
 */
typedef struct {
	mac_tx_srs_mode_t	st_mode;
	mac_client_impl_t	*st_arg1;
	mac_ring_t		*st_arg2;

	mac_group_t	*st_group;	/* TX group for share */
	boolean_t	st_woken_up;

	/*
	 * st_max_q_cnt is the queue depth threshold to limit
	 * outstanding packets on the Tx SRS. Once the limit
	 * is reached, Tx SRS will drop packets until the
	 * limit goes below the threshold.
	 */
	uint32_t	st_max_q_cnt;	/* max. outstanding packets */
	/*
	 * st_hiwat is used Tx serializer and bandwidth mode.
	 * This is the queue depth threshold upto which
	 * packets will get buffered with no flow-control
	 * back pressure applied to the caller. Once this
	 * threshold is reached, back pressure will be
	 * applied to the caller of mac_tx() (mac_tx() starts
	 * returning a cookie to indicate a blocked SRS).
	 * st_hiwat should always be lesser than or equal to
	 * st_max_q_cnt.
	 */
	uint32_t	st_hiwat;	/* mblk cnt to apply flow control */
	uint32_t	st_lowat;	/* mblk cnt to relieve flow control */
	uint32_t	st_hiwat_cnt; /* times blocked for Tx descs */
	mac_tx_stats_t	st_stat;

	mac_capab_aggr_t	st_capab_aggr;

	/*
	 * st_soft_rings is used as an array to store aggr Tx soft
	 * rings. When aggr_find_tx_ring() returns a pseudo ring,
	 * the associated soft ring has to be found. st_soft_rings
	 * array stores the soft ring associated with a pseudo Tx
	 * ring and it can be accessed using the pseudo ring
	 * index (mr_index). Note that the ring index is unique
	 * for each ring in a group.
	 */
	mac_soft_ring_t **st_soft_rings;
} mac_srs_tx_t;

/*
 * Methods for moving packets from receipt at a MAC provider onto the correct
 * Rx SRS.
 */
typedef enum {
	/*
	 * Packets will be moved onto this SRS using `mac_rx_srs_process`.
	 */
	MRSLP_PROCESS,
	/*
	 * Packets will be moved onto this SRS using `mac_hwrings_rx_process`.
	 *
	 * This should only occur for Rx rings when sun4v is in use,
	 */
	MRSLP_HWRINGS,
} mac_rx_srs_lower_proc_t;

/*
 * Members specific to receive SRSes.
 */
typedef struct {
	/*
	 * Upcall function for Rx processing when `SRST_NO_SOFT_RINGS` is set.
	 * Rx softring callbacks for non-bypass traffic should use the same
	 * function and initial argument.
	 * Argument 2 of `sr_func` would be a client-provided handle, but is
	 * always `NULL` in this context as SRSes themselves cannot be used as
	 * part of client polling.
	 *
	 * Protected by srs_lock + !SRS_PROC.
	 */
	mac_direct_rx_t		sr_func;
	void			*sr_arg1;

	mac_rx_srs_lower_proc_t	sr_lower_proc;	/* Atomically changed */
	mac_ring_t		*sr_ring;	/* Ring Descriptor (WO) */

	/*
	 * mblk count under which we should signal the poll thread to request
	 * more packets.
	 */
	uint32_t		sr_poll_thres;
	/* mblk count to apply flow control. */
	uint32_t		sr_hiwat;
	/* mblk count to relieve flow control. */
	uint32_t		sr_lowat;
	/* Target flent to use for an action spec, if `srs_flent` is delegate */
	flow_entry_t		*sr_act_as;	/* WO */

	/* 64B */

	/*
	 * Number of mblks enqueued in this complete SRS, its logical SRSes,
	 * and all of their softrings. Always 0 for a logical SRS.
	 */
	uint32_t		sr_poll_pkt_cnt; /* Atomically updated */
	/* Round Robin index for hashing into softrings */
	uint32_t		sr_ind; /* SRS_PROC */
	mac_rx_stats_t		sr_stat;

	/* Times polling was enabled */
	uint32_t		sr_poll_on;
	/* Times polling was enabled by worker thread */
	uint32_t		sr_worker_poll_on;
	/* Times polling was disabled */
	uint32_t		sr_poll_off;
	/* Poll thread signalled count */
	uint32_t		sr_poll_thr_sig;
	/* Poll thread busy */
	uint32_t		sr_poll_thr_busy;
	/* SRS drains, stays in poll mode but doesn't poll */
	uint32_t		sr_poll_drain_no_poll;
	/*
	 * SRS has nothing to do and no packets in H/W but
	 * there is a backlog in softrings. SRS stays in
	 * poll mode but doesn't do polling.
	 */
	uint32_t		sr_poll_no_poll;
	/* Active polling restarted */
	uint32_t		sr_below_hiwat;
	/* Found packets in last poll so try and poll again */
	uint32_t		sr_poll_again;
	/*
	 * Packets in queue but poll thread not allowed to process so
	 * signal the worker thread.
	 */
	uint32_t		sr_poll_sig_worker;
	/*
	 * Poll thread has nothing to do and H/W has nothing so
	 * reenable the interrupts.
	 */
	uint32_t		sr_poll_intr_enable;
	/*
	 * Poll thread has nothing to do and worker thread was already
	 * running so it can decide to reenable interrupt or poll again.
	 */
	uint32_t		sr_poll_goto_sleep;
	/* Worker thread goes back to draining the queue */
	uint32_t		sr_drain_again;
	/* More Packets in queue so signal the poll thread to drain */
	uint32_t		sr_drain_poll_sig;
	/* More Packets in queue so signal the worker thread to drain */
	uint32_t		sr_drain_worker_sig;
	/* Poll thread is already running so worker has nothing to do */
	uint32_t		sr_drain_poll_running;
	/* We have packets already queued so keep polling */
	uint32_t		sr_drain_keep_polling;
	/* Drain is done and interrupts are reenabled */
	uint32_t		sr_drain_finish_intr;
	/* Polling thread needs to schedule worker wakeup */
	uint32_t		sr_poll_worker_wakeup;

	/* WO, poll thread */
	kthread_t		*sr_poll_thr;

	/* processor to bind to */
	processorid_t		sr_poll_cpuid;
	/* saved cpuid during offline */
	processorid_t		sr_poll_cpuid_save;
} mac_srs_rx_t;

/*
 * Flagset of immutable and slowly-varying aspects of a softring set (SRS).
 *
 * These identify mainly static characteristics (Tx/Rx, whether the SRS
 * corresponds to the entrypoint on a MAC client) as well as state on an
 * administrative timescale (fanout behaviour, bandwidth control).
 *
 * See the commentary on `mac_soft_ring_state_t` for commentary on gaps in the
 * numbering of flags for this type.
 */
enum mac_soft_ring_set_type {
	/*
	 * The flow entry underpinning this SRS belongs to a MAC client for
	 * a link.
	 *
	 * Immutable.
	 */
	SRST_LINK			= 1 << 0,
	/*
	 * The flow entry underpinning this SRS belongs to a flow classifier
	 * attached to a given MAC client.
	 *
	 * Immutable.
	 */
	SRST_FLOW			= 1 << 1,
	/*
	 * This SRS does not have any softrings assigned.
	 *
	 * A Tx SRS has no rings and will send packets directly to the NIC,
	 * and an Rx SRS will handle packets inline via `sr_func`.
	 *
	 * Mutable for Tx SRSes.
	 */
	SRST_NO_SOFT_RINGS		= 1 << 2,
	/*
	 * Set on all Rx SRSes when the tunable `mac_latency_optimize` is
	 * `true`.
	 *
	 * If set, packets may be processed inline by any caller who arrives
	 * with more packets to enqueue if there is no existing backlog.
	 * The worker thread will share a CPU binding with the poll thread.
	 * Wakeups sent to worker threads will be instantaneous (loopback,
	 * teardown, and bandwidth-controlled cases).
	 *
	 * If unset on an Rx SRS, packets may only be moved to softrings by the
	 * worker thread. `SRST_ENQUEUE` will also be set in this case.
	 *
	 * Immutable. Requires !`SRST_TX`.
	 */
	SRST_LATENCY_OPT		= 1 << 3,
	/*
	 * This SRS is logical, and exists as part of the flowtree of a
	 * complete SRS. It is not directly visible via the flow entry's Rx/Tx
	 * SRS list.
	 *
	 * The field `srs_complete_parent` points to the SRS whose flowtree
	 * this object is contained in.
	 *
	 * Immutable.
	 */
	SRST_LOGICAL			= 1 << 4,
	/*
	 * This SRS behaves as a queue for a bandwidth limited subflow,
	 * and directs traffic to another logical/complete SRS, `srs_give_to`,
	 * every system tick.
	 *
	 * Immutable. Requires `SRST_LOGICAL` and `SRST_NO_SOFT_RINGS`.
	 */
	SRST_FORWARD			= 1 << 5,
	/*
	 * All softrings will be initialised with `ST_RING_WORKER_ONLY`.
	 *
	 * Set when `SRST_LATENCY_OPT` is disabled, or when the underlying ring
	 * requires `MAC_RING_RX_ENQUEUE` (sun4v).
	 *
	 * Immutable. Requires !`SRST_TX`.
	 */
	SRST_ENQUEUE			= 1 << 6,
	/*
	 * The SRS's client is placed on the default group (either due to
	 * oversubscription, or the device admits only one group).
	 *
	 * A hardware classified ring of this type will receive additional
	 * traffic when moved into full or all-multicast promiscuous mode.
	 *
	 * Mutable. Requires !`SRST_TX`.
	 */
	SRST_DEFAULT_GRP		= 1 << 7,
	/*
	 * If present, this SRS is a transmit SRS. Otherwise it is a
	 * receive SRS.
	 *
	 * Transmit SRSes use softrings as mappings to underlying Tx rings
	 * from the hardware.
	 *
	 * Tx/Rx specific data in `srs_data` are gated on this flag, as are the
	 * choice of drain functions, enqueue behaviours, etc.
	 *
	 * Immutable.
	 */
	SRST_TX				= 1 << 8,
	/*
	 * One or more elements of `srs_bw` is `BW_ENABLED`, and the queue size
	 * and egress rate of this SRS are limited accordingly.
	 *
	 * Mutable.
	 */
	SRST_BW_CONTROL			= 1 << 9,
	/*
	 * The action associated with this SRS (complete/logical) is configured
	 * with `MFA_FLAGS_RESOURCE`, and we will inform an upstack client of
	 * any changes to softrings (creation, deletion, CPU bind, quiesce). The
	 * client may also poll the softrings to check for packets.
	 *
	 * This implies that the client is sensitive to the CPU bindings of soft
	 * rings and/or that flows are consistently delivered to the same
	 * softring. Accordingly, packet fanout must always be flowhash-driven.
	 *
	 * Mutable under quiescence, if a flow action is changed on an
	 * established SRS. Requires !`SRST_TX`.
	 */
	SRST_CLIENT_POLL		= 1 << 10,
	/*
	 * This complete SRS has had flows plumbed from IP to allow matching
	 * IPv4 packets to bypass DLS (i.e., the root SRS action).
	 *
	 * This is a vanity flag to make MAC client plumbing state clearer when
	 * debugging, and does not alter datapath behaviour.
	 *
	 * Mutable under quiescence. Requires !`SRST_TX`.
	 */
	SRST_DLS_BYPASS_V4		= 1 << 12,
	/*
	 * This complete SRS has had flows plumbed from IP to allow matching
	 * IPv6 packets to bypass DLS (i.e., the root SRS action).
	 *
	 * This is a vanity flag to make MAC client plumbing state clearer when
	 * debugging, and does not alter datapath behaviour.
	 *
	 * Mutable under quiescence. Requires !`SRST_TX`.
	 */
	SRST_DLS_BYPASS_V6		= 1 << 13,
};

/*
 * Flagset reflecting the current state of datapath processing for a given SRS.
 *
 * See the commentary on `mac_soft_ring_state_t` for commentary on gaps in the
 * numbering of flags for this type.
 */
typedef enum {
	/*
	 * This SRS's worker thread is explicitly bound to a single CPU.
	 */
	SRS_WORKER_BOUND	= 1 << 1,
	/*
	 * This Rx SRS's poll thread is explicitly bound to a single CPU.
	 */
	SRS_POLL_BOUND		= 1 << 2,
	/*
	 * This complete Rx SRS is created on top of (and has exclusive
	 * use of) a dedicated ring. When under sufficient load, MAC will
	 * disable interrupts and pull packets into the SRS by polling the
	 * NIC/ring, and will set `SRS_POLLING` when this is the case.
	 *
	 * This flag may be added/removed as SRSes move between
	 * hardware/software classification (e.g., if groups must be shared).
	 */
	SRS_POLLING_CAPAB	= 1 << 3,
	/*
	 * A thread is currently processing packets from this SRS, and
	 * has relinquished its hold on `srs_lock` to allow more packets to be
	 * enqueued while it does so.
	 *
	 * SRS processing will always enqueue packets if set, with the
	 * expectation that whoever is draining the thread will continue to
	 * do so.
	 *
	 * Requires qualification of what thread is doing the processing: either
	 * `SRS_WORKER`, `SRS_PROC_FAST`, or `SRS_POLL_PROC`.
	 */
	SRS_PROC		= 1 << 4,
	/*
	 * The Rx poll thread should request more packets from the underlying
	 * device.
	 *
	 * Requires `SRS_POLLING`.
	 */
	SRS_GET_PKTS		= 1 << 5,
	/*
	 * This Rx SRS has been moved into poll mode. Interrupts from
	 * the underlying device are disabled, and the poll thread is
	 * exclusively responsible for moving packets into the SRS.
	 *
	 * Requires `SRS_POLLING_CAPAB`.
	 */
	SRS_POLLING		= 1 << 6,
	/*
	 * The SRS worker thread currently holds `SRS_PROC`.
	 *
	 * Requires `SRS_PROC`.
	 */
	SRS_WORKER		= 1 << 8,
	/*
	 * Packets have been enqueued on this TX SRS due to either flow control
	 * or a lack of Tx descriptors on the NIC.
	 */
	SRS_ENQUEUED		= 1 << 9,
	/*
	 * `SRS_PROC` is held by the caller of `mac_rx_srs_process` (typically
	 * the interrupt context) and packets are being processed inline.
	 *
	 * Requires `SRS_PROC`.
	 */
	SRS_PROC_FAST		= 1 << 11,
	/*
	 * The Rx SRS poll thread currently holds `SRS_PROC`.
	 *
	 * Requires `SRS_PROC`.
	 */
	SRS_POLL_PROC		= 1 << 12,
	/*
	 * This Tx SRS has run out of descriptors on the underlying NIC.
	 *
	 * Any outbound packets will be queued until the underlying provider
	 * marks more descriptors as available via `mac_tx_ring_update`.
	 */
	SRS_TX_BLOCKED		= 1 << 13,
	/*
	 * This Tx SRS is flow controlled: more than `st_hiwat` packets are
	 * currently enqueued.
	 *
	 * Any outbound packets will be enqueued, and drained by the SRS
	 * worker. Senders will receive a cookie -- they will be informed when
	 * any cookie is no longer flow controlled if they have registered a
	 * callback via `mac_client_tx_notify`.
	 */
	SRS_TX_HIWAT		= 1 << 14,
	/*
	 * This Tx SRS has returned a cookie to at least one sender who has set
	 * `MAC_TX_NO_ENQUEUE` regardless of watermark state.
	 *
	 * When the SRS is drained, notify the client via its
	 * `mac_client_tx_notify` callback that it may send.
	 */
	SRS_TX_WAKEUP_CLIENT	= 1 << 15,
	/*
	 * This SRS has been signalled to stop processing any packets.
	 *
	 * Downstack entrypoints (rings, flows) which can call into this SRS
	 * should be quiesced such that no more packets will be enqueued while
	 * this is set.
	 *
	 * The SRS worker thread will propagate the request to any softrings.
	 */
	SRS_QUIESCE		= 1 << 18,
	/*
	 * The SRS has ceased processing any enqueued packets, the worker thread
	 * has finished quiescing any softrings and is awaiting a signal
	 * of either `SRS_CONDEMNED` or `SRS_RESTART` to wake up.
	 */
	SRS_QUIESCE_DONE	= 1 << 19,
	/*
	 * This SRS is marked for deletion.
	 *
	 * Downstack entrypoints (rings, flows) which can call into this SRS
	 * should be quiesced such that no more packets will be enqueued while
	 * this is set.
	 *
	 * The SRS worker thread will propagate the request to any softrings.
	 */
	SRS_CONDEMNED		= 1 << 20,
	/*
	 * The SRS worker has completed any teardown in response to
	 * `SRS_CONDEMNED`.
	 *
	 * Requires `SRS_CONDEMNED_DONE`.
	 */
	SRS_CONDEMNED_DONE	= 1 << 21,
	/*
	 * This Rx SRS's poll thread has quiesced in response to `SRS_QUIESCE`.
	 */
	SRS_POLL_THR_QUIESCED	= 1 << 22,
	/*
	 * The SRS has been signalled to resume processing traffic.
	 *
	 * The worker thread should unset this and any `QUIESCE` flags,
	 * propagate the request to softrings and the poll thread, and
	 * resume processing packets.
	 */
	SRS_RESTART		= 1 << 23,
	/*
	 * The SRS has successfully restarted all of its softrings and poll
	 * thread, if present.
	 */
	SRS_RESTART_DONE	= 1 << 24,
	/*
	 * This Rx SRS's worker thread has signalled the poll thread to resume
	 * in response to `SRS_RESTART`.
	 */
	SRS_POLL_THR_RESTART	= 1 << 25,
	/*
	 * This SRS is part of the global list `mac_srs_g_list`. Its siblings
	 * are accessed via `srs_next` and `srs_prev`.
	 */
	SRS_IN_GLIST		= 1 << 26,
	/*
	 * This Rx SRS's poll thread has terminated in response to
	 * `SRS_CONDEMN`.
	 */
	SRS_POLL_THR_EXITED	= 1 << 27,
	/*
	 * This SRS is semi-permanently quiesced, and should not accept
	 * `SRS_RESTART` requests.
	 */
	SRS_QUIESCE_PERM	= 1 << 28,
} mac_soft_ring_set_state_t;

/*
 * SRS fanout states.
 *
 * These are set during SRS initialisation and by the flow CPU init methods to
 * indicate whether any work is needing done to adjust the softrings.
 */
typedef enum {
	/*
	 * This is a new SRS. Softrings have not yet been created.
	 */
	SRS_FANOUT_UNINIT = 0,
	/*
	 * The SRS's bindings and fanout count match the underlying CPU spec.
	 */
	SRS_FANOUT_INIT,
	/*
	 * CPU count and/or bindings have changed and the SRS needs to be
	 * modified accordingly.
	 */
	SRS_FANOUT_REINIT
} mac_srs_fanout_state_t;

#define	SRS_QUIESCED(srs)	((srs)->srs_state & SRS_QUIESCE_DONE)

/*
 * If the SRS_QUIESCE_PERM flag is set, the SRS worker thread will not be
 * able to be restarted.
 */
#define	SRS_QUIESCED_PERMANENT(srs)	((srs)->srs_state & SRS_QUIESCE_PERM)

/*
 * Methods for draining packets enqueued on a softring set. These vary according
 * to how any SRS is configured, and may be specialised to remove conditionals
 * around how packets must be processed.
 */
typedef enum {
	/*
	 * Sentinel value used to ensure that a valid drain function is always
	 * configured.
	 */
	MDSP_UNSPEC = 0,
	MDSP_TX,
	MDSP_RX,
	MDSP_RX_SUBTREE,
	MDSP_RX_SUBTREE_BW,
	MDSP_RX_BW,
	MDSP_RX_BW_SUBTREE,
	MDSP_RX_BW_SUBTREE_BW,
	MDSP_FORWARD,
} mac_srs_drain_proc_t;

/*
 * The first-line packet queue hit once packets are received from or
 * transmitted onto a MAC provider. srs_type identifies whether an SRS
 * is transmit or receive, as well as other aspects of how packets should be
 * processed.
 *
 * An SRS may be either _complete_ (!(srs_type & SRST_LOGICAL)), or
 * _logical_ (srs_type & SRST_LOGICAL).
 *
 * Complete SRSes are valid entry points for packets, and may have the
 * full suite of poll and/or worker threads created and bound to them.
 * If needed, they will have a filled flowtree for packet delivery to any
 * subflows.
 *
 * Logical SRSes serve purely as lists of softrings or a queue ahead of another
 * SRS, with bandwidth control elements if required. Forward SRSes are a subset
 * of logical SRSes, which are responsible only for policing and shaping traffic
 * packets bound for another SRS due to bandwidth limitations.
 *
 * # Rx SRS operation
 * Softrings in an Rx SRS are responsible for parallelising the upstack
 * processing of inbound traffic, by fanning out from a single Rx ring or device
 * entrypoint.
 *
 * In the interrupt path, `sr_lower_proc` is responsible for moving packets into
 * the SRS. Bandwidth adjustment controls whether packets can be enqueued here
 * and the rate at which they are dequeued. Once dequeued, the packets are
 * fanned out across `srs_soft_rings` according to `srs_type`, and may switch to
 * polling mode (if poll capable). In poll mode, the poll thread is often
 * expected to perform fanout if the SRS is `SRST_LATENCY_OPT`.
 *
 * Polling works by turning off interrupts if packets are still queued on any
 * soft ring reachable via a complete SRS once the drain routine finishes. Once
 * the backlog is clear and a poll attempt returns no packets, i.e., the Rx ring
 * doesn't have anything, interrupts are turned back on. For this purpose we
 * keep a separate `sr_poll_pkt_cnt` counter which tracks the sum of packets
 * present in all these queues. The counter is incremented when packets
 * are queued in the SRS and decremented once they are fully processed by a
 * client (via sr_func or the soft rings) or dropped due to bandwidth policing.
 * It's important that this decrement occurs after handoff to the client, since
 * this best reflects the rate at which MAC clients are willing/able to process
 * this traffic.
 *
 * # Tx SRS operation
 * Softrings in a Tx SRS each hold a Tx ring returned from the underlying
 * device. The SRS is responsible for handling traffic from many upstack
 * callers, and fanning it out across these rings using either an optional hint
 * value or computing packets' flow hash. Devices without HW ring capabilities
 * will have no softrings and go straight to the device.
 *
 * The SRS structure acts as a serializer with a worker thread. The Tx SRS's
 * default behaviour is to act as a pass-through and either send straight to the
 * NIC or to a target softring. The queue in each layer is used when either a
 * Tx ring runs out of descriptors, or to enforce bandwidth limits.
 */
struct mac_soft_ring_set_s {
	/*
	 * Elements common to all SRS types.
	 * The following block of fields are protected by srs_lock and fill one
	 * cache line with the elements which change often in the datapath.
	 */
	kmutex_t	srs_lock;
	mac_soft_ring_set_type_t	srs_type;
	mac_soft_ring_set_state_t	srs_state;

	/*
	 * The SRS's packet queue.
	 */
	mblk_t		*srs_first;	/* first mblk chain or NULL */
	mblk_t		*srs_last;	/* last mblk chain or NULL */
	size_t		srs_size;	/* Size of packets queued in bytes */
	uint32_t	srs_count;

	kcondvar_t	srs_async;	/* cv for worker thread */
	kcondvar_t	srs_cv;		/* cv for poll thread */
	timeout_id_t	srs_tid;	/* timeout id for pending timeout */

	/*
	 * From here 'til `srs_data`, the fields of this struct are mostly
	 * static, barring changes from administrative commands.
	 */

	/*
	 * Type-specific drain function accounting for Tx vs. Rx, Bandwidth vs.
	 * non-Bandwidth, and Subtree vs. non-Subtree.
	 */
	mac_srs_drain_proc_t	srs_drain_func;	/* srs_lock(Rx), Quiesce(tx) */

	/* 64B */

	/*
	 * A structure for classifying packets received on a complete SRS and
	 * directing them to the correct logical SRSes according to any
	 * installed subflows.
	 *
	 * This field is protected by Tx/Rx quiescence.
	 */
	flow_tree_baked_t	srs_flowtree;

	/*
	 * List of soft rings.
	 * The following block can be altered only after quiescing the SRS.
	 *
	 * Counts are limited to `uint16_t` to save space, as we admit at most
	 * `MAX_SR_FANOUT` (24, Rx) or `MAX_RINGS_PER_GROUP` (128, Tx) elements.
	 */
	mac_soft_ring_t	*srs_soft_ring_head;
	mac_soft_ring_t	*srs_soft_ring_tail;
	mac_soft_ring_t	**srs_soft_rings;
	uint16_t	srs_soft_ring_count;
	uint16_t	srs_soft_ring_quiesced_count;
	uint16_t	srs_soft_ring_condemned_count;

	kcondvar_t	srs_quiesce_done_cv;	/* cv for removal */

	/*
	 * Forward-type SRSes are used as queues limited by one or more
	 * bandwidth controls. These then fanout onto the set of softrings held
	 * by `srs_give_to`, which may be logical or complete.
	 *
	 * This allows us to avoid creating any softrings for BW-limited
	 * delegate Rx actions, and is used to mete out access to the underlying
	 * Tx rings for BW-limited cases.
	 */
	mac_soft_ring_set_t	*srs_give_to; /* WO */

	/*
	 * Bandwidth control related members.
	 */
	mac_bw_ctl_t	**srs_bw; /* WO */
	size_t		srs_bw_len; /* WO */

	/*
	 * Global doubly-linked list of all SRSes.
	 */
	mac_soft_ring_set_t	*srs_next;	/* mac_srs_g_lock */
	mac_soft_ring_set_t	*srs_prev;	/* mac_srs_g_lock */

	/*
	 * If the associated ring is exclusively used by a mac client, e.g.,
	 * an aggregation, this fields is used to keep a reference to the
	 * MAC client's pseudo ring.
	 */
	mac_resource_handle_t	srs_mrh;
	/*
	 * The following blocks are write once (WO) and valid for the life
	 * of the SRS.
	 */
	mac_client_impl_t	*srs_mcip;	/* back ptr to mac client */
	flow_entry_t		*srs_flent;	/* back ptr to flent */

	kthread_t	*srs_worker;	/* WO, worker thread */

	processorid_t	srs_worker_cpuid;	/* processor to bind to */
	processorid_t	srs_worker_cpuid_save;	/* saved cpuid during offline */
	mac_srs_fanout_state_t	srs_fanout_state;

	/*
	 * Priority assignment for poll/worker threads for this SRS and its
	 * softrings.
	 */
	pri_t		srs_pri; /* srs_lock */

	/*
	 * Singly-linked list of logical SRSes allocated within an srs_flowtree.
	 * A complete SRS serves as the head of this list, which allows for
	 * easier walking during stats collection or quiescence.
	 */
	mac_soft_ring_set_t	*srs_logical_next;
	mac_soft_ring_set_t	*srs_complete_parent;

	/*
	 * We want to setup cache-line alignment for `mac_srs_rx_t` and
	 * `mac_srs_tx_t` such that they can reason about placing immutable
	 * members together regardless of this struct's layout.
	 *
	 * We assert this property holds below.
	 */
	uint8_t			srs_pad[24];

	union {
		mac_srs_rx_t	rx; /* !(srs_type & SRST_TX) */
		mac_srs_tx_t	tx; /* srs_type & SRST_TX */
	} srs_data;

	/*
	 * Stats relating to bytes and packets *matching this SRS explicitly*,
	 * even if another SRS is doing the processing (e.g., non-BW delegate
	 * actions).
	 *
	 * Stats for a given flent will sum up all Tx/Rx counts by walking the
	 * SRSes in a client. Stats per *action* are instead accumulated over
	 * all softrings.
	 *
	 * Modified/read atomically.
	 */
	uint64_t	srs_match_pkts;
	uint64_t	srs_match_bytes;

	/*
	 * TODO(ky): This struct is around 5kiB. We need to be smarter around
	 * refcounting this member now that we have *many* SRSes per client.
	 */
	mac_cpus_t	srs_cpu;

	kstat_t		*srs_ksp;
};

#ifdef _KERNEL
CTASSERT((offsetof(mac_soft_ring_set_t, srs_data) % 64) == 0);
#endif

inline bool
mac_srs_is_tx(const mac_soft_ring_set_t *srs)
{
	return ((srs->srs_type & SRST_TX) != 0);
}

inline bool
mac_srs_is_logical(const mac_soft_ring_set_t *srs)
{
	return ((srs->srs_type & SRST_LOGICAL) != 0);
}

inline bool
mac_srs_is_latency_opt(const mac_soft_ring_set_t *srs)
{
	return ((srs->srs_type & SRST_LATENCY_OPT) != 0);
}

inline bool
mac_srs_is_bw_controlled(const mac_soft_ring_set_t *srs)
{
	return ((srs->srs_type & SRST_BW_CONTROL) != 0);
}

/*
 * Return the flow entry which specifies the action that packets should
 * be subject to on this Rx SRS (either directly or via some layers of
 * delegation).
 *
 * This flow entry's action is guaranteed to have `MFA_FLAGS_ACTION` set.
 */
inline flow_entry_t *
mac_srs_rx_action_flent(mac_soft_ring_set_t *srs)
{
	ASSERT(!mac_srs_is_tx(srs));

	return ((srs->srs_data.rx.sr_act_as != NULL) ?
	    srs->srs_data.rx.sr_act_as :
	    srs->srs_flent);
}

/*
 * Return the action that packets should be subject to on this Rx SRS (either
 * directly or via some layers of delegation).
 *
 * This action is guaranteed to have `MFA_FLAGS_ACTION` set.
 */
inline flow_action_t *
mac_srs_rx_action(mac_soft_ring_set_t *srs)
{
	return (&mac_srs_rx_action_flent(srs)->fe_action);
}

/*
 * Structure for dls statistics
 */
struct dls_kstats {
	kstat_named_t	dlss_soft_ring_pkt_drop;
};

extern struct dls_kstats dls_kstat;

#define	DLS_BUMP_STAT(x, y)	(dls_kstat.x.value.ui32 += y)

/* Turn dynamic polling off */
#define	MAC_SRS_POLLING_OFF(mac_srs) {					\
	ASSERT(MUTEX_HELD(&(mac_srs)->srs_lock));			\
	if (((mac_srs)->srs_state & (SRS_POLLING_CAPAB|SRS_POLLING)) == \
	    (SRS_POLLING_CAPAB|SRS_POLLING)) {				\
		(mac_srs)->srs_state &= ~SRS_POLLING;			\
		(void) mac_hwring_enable_intr((mac_ring_handle_t)	\
		    (mac_srs)->srs_data.rx.sr_ring);			\
		(mac_srs)->srs_data.rx.sr_poll_off++;			\
		DTRACE_PROBE1(mac__poll__off, mac_soft_ring_set_t *,	\
			(mac_srs));					\
	}								\
}

#define	MAC_COUNT_CHAIN(mac_srs, head, tail, cnt, sz)	{	\
	mblk_t		*tmp;					\
	boolean_t	bw_ctl = B_FALSE;			\
								\
	ASSERT((head) != NULL);					\
	cnt = 0;						\
	sz = 0;							\
	if (mac_srs_is_bw_controlled((mac_srs)))		\
		bw_ctl = B_TRUE;				\
	tmp = tail = (head);					\
	if ((head)->b_next == NULL) {				\
		cnt = 1;					\
		if (bw_ctl)					\
			sz += mp_len(head);			\
	} else {						\
		while (tmp != NULL) {				\
			tail = tmp;				\
			cnt++;					\
			if (bw_ctl)				\
				sz += mp_len(tmp);		\
			tmp = tmp->b_next;			\
		}						\
	}							\
}

/*
 * Decrement the cumulative packet count in SRS and its softrings. If the
 * `sr_poll_pkt_cnt` goes below lowat, then check if the interface was left in a
 * polling mode and no one is really processing the queue (to get the interface
 * out of poll mode). If no one is processing the queue, then acquire
 * `SRS_PROC` and signal the poll thread to check the interface for packets and
 * get the interface back to interrupt mode if nothing is found.
 */
static inline __attribute__((always_inline)) void
mac_update_srs_count(mac_soft_ring_set_t *mac_srs, uint32_t cnt)
{
	/*
	 * Poll packet occupancy is not tracked by Tx SRSes.
	 */
	if (mac_srs_is_tx(mac_srs)) {
		return;
	}

	/*
	 * The poll packet count on a logical SRS serves no real function. We
	 * want to feed this information back to control the poll thread of its
	 * complete SRS.
	 */
	mac_soft_ring_set_t *true_target = mac_srs_is_logical(mac_srs) ?
	    mac_srs->srs_complete_parent : mac_srs;
	ASSERT3P(true_target, !=, NULL);
	ASSERT(!mac_srs_is_tx(true_target));

	mac_srs_rx_t *srs_rx = &true_target->srs_data.rx;

	/*
	 * Update the count of queued packets, but only attempt to lock the SRS
	 * and signal the poll thread when necessary.
	 *
	 * We know that at least one thread will send the signal even with
	 * several competing softrings and the poll thread itself. The only case
	 * in which we would back off from doing so is if `sr_poll_pkt_cnt`
	 * is increased past the threshold. If this occurs, the poll thread
	 * *is/was active*, and will either poll again of its own accord or will
	 * be alerted to do so once those packets are dequeued.
	 */
	const uint32_t point_value =
	    atomic_add_32_nv(&srs_rx->sr_poll_pkt_cnt, -cnt);
	if (point_value <= srs_rx->sr_poll_thres) {
		mutex_enter(&true_target->srs_lock);
		/*
		 * Check that the poll condition still holds, and whether
		 * the SRS is in in poll mode (with the poll thread asleep_.
		 */
		if ((srs_rx->sr_poll_pkt_cnt <= srs_rx->sr_poll_thres) &&
		    ((true_target->srs_state &
		    (SRS_POLLING|SRS_PROC|SRS_GET_PKTS)) == SRS_POLLING)) {
			true_target->srs_state |= (SRS_PROC|SRS_GET_PKTS);
			cv_signal(&true_target->srs_cv);
			srs_rx->sr_below_hiwat++;
		}
		mutex_exit(&true_target->srs_lock);
	}
}

#define	MAC_TX_SOFT_RINGS(mac_srs) (mac_srs_is_tx((mac_srs)) &&		\
	(mac_srs)->srs_soft_ring_count >= 1)

/* Soft ring flags for teardown */
#define	SRS_POLL_THR_OWNER	(SRS_PROC | SRS_POLLING | SRS_GET_PKTS)
#define	SRS_PAUSE		(SRS_CONDEMNED | SRS_QUIESCE)
#define	S_RING_PAUSE		(S_RING_CONDEMNED | S_RING_QUIESCE)

/* Soft rings */
extern void mac_soft_ring_init(void);
extern void mac_soft_ring_finish(void);
extern void mac_fanout_setup(mac_client_impl_t *, flow_entry_t *,
    mac_resource_props_t *, cpupart_t *);

extern void mac_soft_ring_worker_wakeup(mac_soft_ring_t *);
extern mblk_t *mac_soft_ring_poll(mac_soft_ring_t *, size_t);

extern void mac_rx_soft_ring_drain(mac_soft_ring_t *);

/* SRS */
extern void mac_srs_free(mac_soft_ring_set_t *);
extern void mac_srs_quiesce_wait_one(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_srs_signal(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_srs_signal_diff(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t, const mac_soft_ring_set_state_t);
extern void mac_srs_signal_client(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);

extern void mac_rx_srs_retarget_intr(mac_soft_ring_set_t *, processorid_t);
extern void mac_tx_srs_retarget_intr(mac_soft_ring_set_t *);

extern void mac_rx_srs_quiesce(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_rx_srs_restart(mac_soft_ring_set_t *);
extern void mac_tx_srs_quiesce(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t, const bool);
extern void mac_tx_srs_restart(mac_soft_ring_set_t *);
extern void mac_rx_srs_remove(mac_soft_ring_set_t *);

/* Tx SRS, Tx softring */
extern void mac_tx_srs_wakeup(mac_soft_ring_set_t *, mac_ring_handle_t);
extern mblk_t *mac_tx_send(mac_client_impl_t *, mac_ring_t *, mblk_t *,
    mac_tx_stats_t *);
extern boolean_t mac_tx_srs_ring_present(mac_soft_ring_set_t *, mac_ring_t *);
extern mac_soft_ring_t *mac_tx_srs_get_soft_ring(mac_soft_ring_set_t *,
    mac_ring_t *);
extern void mac_tx_srs_add_ring(mac_soft_ring_set_t *, mac_ring_t *);
extern void mac_tx_srs_del_ring(mac_soft_ring_set_t *, mac_ring_t *);
extern mac_tx_cookie_t mac_tx_srs_no_desc(mac_soft_ring_set_t *, mblk_t *,
    uint16_t, mblk_t **);

/* Subflow specific stuff */
extern void mac_srs_update_bwlimit(flow_entry_t *, mac_resource_props_t *);
extern void mac_update_srs_priority(mac_soft_ring_set_t *, pri_t);

/* Flowtree walkers */
extern void mac_tx_srs_walk_flowtree_bw(mac_soft_ring_set_t *,
    flow_tree_pkt_set_t *, const uintptr_t);
extern void mac_tx_srs_walk_flowtree_stat(mac_soft_ring_set_t *,
    flow_tree_pkt_set_t *, const bool);

/* Resource callbacks for clients */
extern int mac_soft_ring_intr_enable(void *);
extern boolean_t mac_soft_ring_intr_disable(void *);
extern cpu_t *mac_soft_ring_bind(mac_soft_ring_t *, processorid_t);
extern void mac_soft_ring_unbind(mac_soft_ring_t *);

extern mac_soft_ring_t *mac_soft_ring_create_rx(int, clock_t, pri_t,
    mac_client_impl_t *, mac_soft_ring_set_t *, processorid_t, mac_direct_rx_t,
    void *);
extern mac_soft_ring_t *mac_soft_ring_create_tx(int, clock_t,
    const mac_soft_ring_state_t, pri_t, mac_client_impl_t *,
    mac_soft_ring_set_t *, processorid_t, mac_ring_t *);
extern void mac_soft_ring_free(mac_soft_ring_t *);
extern void mac_soft_ring_signal(mac_soft_ring_t *,
    const mac_soft_ring_state_t);
extern void mac_rx_soft_ring_process(mac_soft_ring_t *, mblk_t *, mblk_t *, int,
    size_t);
extern mac_tx_cookie_t mac_tx_soft_ring_process(mac_soft_ring_t *,
    mblk_t *, uint16_t, mblk_t **);
extern void mac_srs_worker_quiesce(mac_soft_ring_set_t *);
extern void mac_srs_worker_restart(mac_soft_ring_set_t *);

extern void mac_rx_srs_process(void *, mac_resource_handle_t, mblk_t *,
    boolean_t);
extern void mac_hwrings_rx_process(void *, mac_resource_handle_t, mblk_t *,
    boolean_t);
extern void mac_srs_worker(mac_soft_ring_set_t *);
extern void mac_rx_srs_poll_ring(mac_soft_ring_set_t *);

/* Bandwidth control related functions */
inline bool
mac_srs_any_bw_enforced(const mac_soft_ring_set_t *srs)
{
	for (size_t i = 0; i < srs->srs_bw_len; i++) {
		if (mac_bw_ctl_is_enforced(srs->srs_bw[i])) {
			return (true);
		}
	}
	return (false);
}

inline bool
mac_srs_any_bw_zeroed(const mac_soft_ring_set_t *srs)
{
	for (size_t i = 0; i < srs->srs_bw_len; i++) {
		if (srs->srs_bw[i]->mac_bw_limit == 0) {
			return (true);
		}
	}
	return (false);
}

inline void
mac_srs_bw_lock(const mac_soft_ring_set_t *srs)
{
	mac_bw_ctls_lock(srs->srs_bw, srs->srs_bw_len);
}

inline void
mac_srs_bw_unlock(const mac_soft_ring_set_t *srs)
{
	mac_bw_ctls_unlock(srs->srs_bw, srs->srs_bw_len);
}

/*
 * Static dispatch for all SRS drain functions.
 */
extern void mac_rx_srs_drain_bw(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_rx_srs_drain_bw_subtree(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_rx_srs_drain_bw_subtree_bw(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_rx_srs_drain(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_rx_srs_drain_subtree(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_rx_srs_drain_subtree_bw(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_tx_srs_drain(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_srs_drain_forward(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);

static inline __attribute__((always_inline)) void
mac_srs_drain(mac_soft_ring_set_t *srs, const mac_soft_ring_set_state_t owner)
{
	ASSERT(MUTEX_HELD(&srs->srs_lock));
	switch (srs->srs_drain_func) {
		case MDSP_TX:
			mac_tx_srs_drain(srs, owner);
			break;
		case MDSP_RX:
			mac_rx_srs_drain(srs, owner);
			break;
		case MDSP_RX_BW:
			mac_rx_srs_drain_bw(srs, owner);
			break;
		case MDSP_RX_SUBTREE:
			mac_rx_srs_drain_subtree(srs, owner);
			break;
		case MDSP_RX_SUBTREE_BW:
			mac_rx_srs_drain_subtree_bw(srs, owner);
			break;
		case MDSP_RX_BW_SUBTREE:
			mac_rx_srs_drain_bw_subtree(srs, owner);
			break;
		case MDSP_RX_BW_SUBTREE_BW:
			mac_rx_srs_drain_bw_subtree_bw(srs, owner);
			break;
		case MDSP_FORWARD:
			mac_srs_drain_forward(srs, owner);
			break;
		default:
			panic("Illegal drain func %d for SRS.",
			    srs->srs_drain_func);
			break;
	}
}

inline void
mac_srs_drain_rx_complete(mac_soft_ring_set_t *srs,
    const mac_soft_ring_set_state_t owner)
{
	ASSERT(MUTEX_HELD(&srs->srs_lock));
	ASSERT(!mac_srs_is_tx(srs));
	ASSERT(!mac_srs_is_logical(srs));
	switch (srs->srs_drain_func) {
		case MDSP_RX:
			mac_rx_srs_drain(srs, owner);
			break;
		case MDSP_RX_BW:
			mac_rx_srs_drain_bw(srs, owner);
			break;
		case MDSP_RX_SUBTREE:
			mac_rx_srs_drain_subtree(srs, owner);
			break;
		case MDSP_RX_SUBTREE_BW:
			mac_rx_srs_drain_subtree_bw(srs, owner);
			break;
		case MDSP_RX_BW_SUBTREE:
			mac_rx_srs_drain_bw_subtree(srs, owner);
			break;
		case MDSP_RX_BW_SUBTREE_BW:
			mac_rx_srs_drain_bw_subtree_bw(srs, owner);
			break;
		default:
			panic("Illegal drain func %d for Receive SRS.",
			    srs->srs_drain_func);
			break;
	}
}

/*
 * Static dispatch for all Tx SRS transmit functions.
 */
extern mac_tx_cookie_t mac_tx_single_ring_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
extern mac_tx_cookie_t mac_tx_serializer_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
extern mac_tx_cookie_t mac_tx_fanout_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
extern mac_tx_cookie_t mac_tx_bw_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);
extern mac_tx_cookie_t mac_tx_aggr_mode(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);

inline mac_tx_cookie_t
mac_srs_send_tx_complete(mac_soft_ring_set_t *srs, mblk_t *mp,
    uintptr_t hint, uint16_t flags, mblk_t **retmp)
{
	ASSERT(!MUTEX_HELD(&srs->srs_lock));
	ASSERT(mac_srs_is_tx(srs));
	ASSERT(!mac_srs_is_logical(srs));

	const mac_srs_tx_t *srs_tx = &srs->srs_data.tx;
	mac_tx_cookie_t out = 0;
	switch (srs_tx->st_mode) {
		case SRS_TX_DEFAULT:
			out = mac_tx_single_ring_mode(srs, mp, hint, flags,
			    retmp);
			break;
		case SRS_TX_SERIALIZE:
			out = mac_tx_serializer_mode(srs, mp, hint, flags,
			    retmp);
			break;
		case SRS_TX_FANOUT:
			out = mac_tx_fanout_mode(srs, mp, hint, flags, retmp);
			break;
		case SRS_TX_AGGR:
			out = mac_tx_aggr_mode(srs, mp, hint, flags, retmp);
			break;
		case SRS_TX_BW:
		case SRS_TX_BW_FANOUT:
		case SRS_TX_BW_AGGR:
			out = mac_tx_bw_mode(srs, mp, hint, flags, retmp);
			break;
		default:
			panic("Illegal tx func %d for Transmit SRS.",
			    srs_tx->st_mode);
			break;
	}

	return (out);
}

inline mac_rx_func_t
mac_srs_lower_proc(const mac_rx_srs_lower_proc_t proc)
{
	switch (proc) {
	case MRSLP_PROCESS:
		return (mac_rx_srs_process);
	case MRSLP_HWRINGS:
		return (mac_hwrings_rx_process);
	default:
		panic("No lower proc defined for %d.", proc);
	}
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MAC_SOFT_RING_H */
