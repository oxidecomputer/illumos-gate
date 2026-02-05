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

#define	S_RING_NAMELEN 64

#define	MAX_SR_FANOUT	24

extern boolean_t mac_soft_ring_enable;
extern boolean_t mac_latency_optimize;

typedef struct mac_soft_ring_s mac_soft_ring_t;
typedef struct mac_soft_ring_set_s mac_soft_ring_set_t;
struct flow_entry_s;

typedef void (*mac_soft_ring_drain_func_t)(mac_soft_ring_t *);
typedef mac_tx_cookie_t (*mac_tx_func_t)(mac_soft_ring_set_t *, mblk_t *,
    uintptr_t, uint16_t, mblk_t **);


/* Tx notify callback */
typedef struct mac_tx_notify_cb_s {
	mac_cb_t		mtnf_link;	/* Linked list of callbacks */
	mac_tx_notify_t		mtnf_fn;	/* The callback function */
	void			*mtnf_arg;	/* Callback function argument */
} mac_tx_notify_cb_t;

/*
 * Flagset of immutable and slowly-varying aspects of a softring, stored in
 * `s_ring_type` as a 16-bit value.
 *
 * These identify static characteristics of how a ring should process packets.
 */
typedef enum {
	/*
	 * If set, this is a transmit softring. Packets will be directed via
	 * `mac_tx_send` to an underlying client and ring.
	 *
	 * If absent, this is a receive softring. Packets will be delivered to a
	 * client via `s_ring_rx_func`.
	 *
	 * Immutable.
	 */
	ST_RING_TX		= 0x0001,

	/*
	 * Packets may only be drained from this softring by its own worker
	 * thread, and cannot be handled inline by `mac_tx`, any SRS threads,
	 * or the interrupt context.
	 *
	 * Immutable.
	 */
	ST_RING_WORKER_ONLY	= 0x0002,

	/*
	 * This softring is known to an upstack client, which may invoke any
	 * `mac_rx_fifo_t` operations (direct polling, disable/re-enable inline
	 * delivery).
	 *
	 * `s_ring_rx_arg2` must be non-null.
	 *
	 * Mutable.
	 */
	ST_RING_POLLABLE	= 0x0010,
} mac_soft_ring_type_t;

/*
 * Flagset reflecting the current state of datapath processing for a given
 * softring, stored in `s_ring_state` as a 16-bit value.
 */
typedef enum {
	/*
	 * A thread is currently processing packets from this softring, and has
	 * relinquished its hold on `s_ring_lock` to allow new packets to be
	 * enqueued while it does so.
	 *
	 * SRS processing will always enqueue packets if set, with the
	 * expectation that whoever was draining the thread will continue to
	 * do so.
	 */
	S_RING_PROC		= 0x0001,
	/*
	 * The worker thread of this CPU has been bound to a specific CPU.
	 */
	S_RING_BOUND		= 0x0002,
	/*
	 * This softring is a TX softring and has run out of descriptors on the
	 * underlying ring/NIC.
	 *
	 * Any outbound packets will be queued until the underlying provider
	 * marks more descriptors as available via `mac_tx_ring_update`.
	 */
	S_RING_BLOCK		= 0x0004,
	/*
	 * This softring is a TX softring and is flow controlled: more than
	 * `s_ring_tx_hiwat` packets are currently enqueued.
	 *
	 * Any outbound packets will be enqueued, and drained by the softring
	 * worker. Senders will receive a cookie -- they will be informed when
	 * any cookie is no longer flow controlled if they have registered a
	 * callback via `mac_client_tx_notify`.
	 */
	S_RING_TX_HIWAT		= 0x0008,

	/*
	 * This softring is a TX softring and has returned a cookie to at least
	 * one sender who has set `MAC_TX_NO_ENQUEUE` regardless of watermark
	 * state.
	 *
	 * When the softring is drained, notify the client via its
	 * `mac_client_tx_notify` callback that it may send.
	 */
	S_RING_WAKEUP_CLIENT	= 0x0010,
	/*
	 * This RX softring is client pollable (`ST_RING_POLLABLE`) and this
	 * client has called `mac_soft_ring_intr_enable` to remove MAC's ability
	 * to deliver frames via `s_ring_rx_func`.
	 *
	 * Packets may only be delivered by client polling. The client may undo
	 * this using `mac_soft_ring_intr_disable`.
	 */
	S_RING_BLANK		= 0x0020,
	/*
	 * Inform a thread which holds `S_RING_PROC` that it should notify a
	 * client/MAC when it is done processing using `s_ring_client_cv`.
	 *
	 * This may be used to ensure that replace `s_ring_rx_func` and its
	 * arguments, by waiting until `S_RING_PROC` is unset and these data
	 * are not in use.
	 */
	S_RING_CLIENT_WAIT	= 0x0040,

	/*
	 * This softring has been signalled to stop processing any packets.
	 *
	 * The presence of this flag implies that the parent softring set has
	 * *also* been asked to quiesce. It will not enqueue any packets here.
	 */
	S_RING_QUIESCE		= 0x0100,
	/*
	 * The softring has ceased processing any enqueued/arriving packets, and
	 * is awaiting a signal alongside either `S_RING_CONDEMNED` or
	 * `S_RING_RESTART` to wake up.
	 */
	S_RING_QUIESCE_DONE	= 0x0200,
	/*
	 * This softring is marked for deletion.
	 *
	 * No further packets can be admitted into the softring, and enqueued
	 * packets must not be processed.
	 */
	S_RING_CONDEMNED	= 0x0400,
	/*
	 * The softring worker has completed any teardown in response to
	 * `S_RING_CONDEMNED`.
	 *
	 * Requires `S_RING_QUIESCE_DONE`.
	 */
	S_RING_CONDEMNED_DONE	= 0x0800,

	/*
	 * The softring has been signalled to resume processing traffic.
	 *
	 * The worker thread should unset this and any `QUIESCE` flags and
	 * resume processing packets.
	 */
	S_RING_RESTART		= 0x1000,
	/*
	 * This TX softring has packets enqueued, which the worker thread is
	 * responsible for draining.
	 */
	S_RING_ENQUEUED		= 0x2000,
} mac_soft_ring_state_t;

struct mac_soft_ring_s {
	/* Keep the most used members 64bytes cache aligned */
	kmutex_t	s_ring_lock;	/* lock before using any member */
	uint16_t	s_ring_type;	/* processing model of the sq */
	uint16_t	s_ring_state;	/* state flags and message count */
	uint32_t	s_ring_count;	/* # of mblocks in mac_soft_ring */
	size_t		s_ring_size;	/* Size of data queued */
	mblk_t		*s_ring_first;	/* first mblk chain or NULL */
	mblk_t		*s_ring_last;	/* last mblk chain or NULL */

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

	void		*s_ring_tx_arg1;
	void		*s_ring_tx_arg2;

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
	struct mac_client_impl_s *s_ring_mcip;
	kstat_t		*s_ring_ksp;

	/* Teardown, poll disable control ops */
	kcondvar_t	s_ring_client_cv; /* Client wait for control op */

	mac_soft_ring_set_t *s_ring_set;   /* The SRS this ring belongs to */
	mac_soft_ring_t	*s_ring_next;
	mac_soft_ring_t	*s_ring_prev;
	mac_soft_ring_drain_func_t s_ring_drain_func;

	mac_tx_stats_t	s_st_stat;
};

typedef void (*mac_srs_drain_proc_t)(mac_soft_ring_set_t *, uint_t);

/* Transmit side Soft Ring Set */
typedef struct mac_srs_tx_s {
	/* Members for Tx size processing */
	uint32_t		st_mode;
	mac_tx_func_t		st_func;
	/* TODO(ky) this is a `mac_client_impl_t *`. */
	void			*st_arg1;
	/* TODO(ky) this is a `mac_ring_t *`. */
	void			*st_arg2;
	mac_group_t		*st_group;	/* TX group for share */
	boolean_t		st_woken_up;

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

/* Receive side Soft Ring Set */
typedef struct mac_srs_rx_s {
	/*
	 * Upcall Function for fanout, Rx processing etc. Perhaps
	 * the same 3 members below can be used for Tx
	 * processing, but looking around, mac_rx_func_t has
	 * proliferated too much into various files at different
	 * places. I am leaving the consolidation battle for
	 * another day.
	 */
	mac_direct_rx_t		sr_func;	/* srs_lock */
	/* TODO(ky) this is a `mac_client_impl_t *`. */
	void			*sr_arg1;	/* srs_lock */
	mac_resource_handle_t	sr_arg2;	/* srs_lock */
	mac_rx_func_t		sr_lower_proc;	/* Atomically changed */
	uint32_t		sr_poll_pkt_cnt;
	uint32_t		sr_poll_thres;

	/* mblk cnt to apply flow control */
	uint32_t		sr_hiwat;
	/* mblk cnt to relieve flow control */
	uint32_t		sr_lowat;
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
	/* Ring Descriptor (WO) */
	mac_ring_t		*sr_ring;
	/* processor to bind to */
	processorid_t		sr_poll_cpuid;
	/* saved cpuid during offline */
	processorid_t		sr_poll_cpuid_save;

	/* WO */
	flow_entry_t		*sr_act_as;
} mac_srs_rx_t;

/*
 * Flagset of immutable and slowly-varying aspects of a softring set, stored in
 * `srs_type`.
 *
 * These identify mainly static characteristics (Tx/Rx, whether the SRS
 * corresponds to the entrypoint on a MAC client) as well as state on an
 * administrative timescale (fanout behaviour, bandwidth control).
 */
typedef enum {
	/*
	 * The flow entry underpinning this SRS belongs to a MAC client for
	 * a link.
	 *
	 * Immutable.
	 */
	SRST_LINK			= 0x00000001,
	/*
	 * The flow entry underpinning this SRS belongs to a classifier attached
	 * to a given MAC client.
	 *
	 * Immutable.
	 */
	SRST_FLOW			= 0x00000002,
	/*
	 * This SRS does not have any softrings assigned.
	 *
	 * Immutable.
	 */
	SRST_NO_SOFT_RINGS		= 0x00000004,
	/*
	 * This softring set is logical, and exists as part of the flowtree of a
	 * complete SRS. It is not directly visible via the flow entry's Rx/Tx
	 * SRS list.
	 *
	 * The field `srs_complete_parent` points to the SRS whose flowtree
	 * this object is contained in.
	 *
	 * Immutable.
	 */
	SRST_LOGICAL			= 0x00000008,

	/*
	 * This softring set behaves as a queue for a bandwidth limited subflow,
	 * and directs traffic to another (logical/complete) SRS `srs_give_to`
	 * every system tick.
	 *
	 * Immutable. Requires `SRST_LOGICAL` and `SRST_NO_SOFT_RINGS`.
	 */
	SRST_FORWARD			= 0x00000010,
	/*
	 * If present, this softring set is a transmit SRS. Otherwise it is a
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
	SRST_TX				= 0x00000020,
	/*
	 * Set on all Rx SRSes when the tunable `mac_latency_optimize` is
	 * `true`.
	 *
	 * If set, packets may be processed inline by any caller who arrives
	 * with more packets to enqueue if there is no existing backlog.
	 * The worker thread will share a CPU binding with the poll thread.
	 * Wakeups sent to worker threads will be instantaneous (teardown and
	 * bandwidth-controlled cases).
	 *
	 * If unset on an Rx SRS, packets may only be moved to softrings by the
	 * worker thread. `SRST_ENQUEUE` will also be set in this case.
	 *
	 * Immutable. Requires ¬`SRST_TX`.
	 */
	SRST_LATENCY_OPT		= 0x00000040,
	/*
	 * All softrings will be initialised with `ST_RING_WORKER_ONLY`.
	 *
	 * Set when `SRST_LATENCY_OPT` is disabled, or when the underlying ring
	 * requires `MAC_RING_RX_ENQUEUE` (sun4v).
	 *
	 * Immutable. Requires ¬`SRST_TX`.
	 */
	SRST_ENQUEUE			= 0x00000080,

	/*
	 * The client underlying this softring set has been assigned the default
	 * group (either due to oversubscription, or the device admits only one
	 * group).
	 *
	 * A hardware classified ring of this type will receive additional
	 * traffic when moved into full or all-multicast promiscuous mode.
	 *
	 * Mutable.
	 */
	SRST_DEFAULT_GRP		= 0x00000100,
	/*
	 * One or more elements of `srs_bw` is `BW_ENABLED`, and the queue size
	 * and egress rate of this SRS are limited accordingly.
	 *
	 * Mutable.
	 */
	SRST_BW_CONTROL			= 0x00000200,

	/*
	 * The action associated with this soft ring set (complete/logical) is
	 * configured with `MFA_FLAGS_RESOURCE`, and we will inform an upstack
	 * client of any changes to softrings (creation, deletion, CPU bind,
	 * quiesce). The client may also poll the softrings to check for
	 * packets.
	 *
	 * This implies that the client is sensitive to the CPU bindings of soft
	 * rings and/or that flows are consistently delivered to the same
	 * softring. Accordingly, packet fanout must always be flowhash-driven.
	 *
	 * Mutable.
	 */
	SRST_CLIENT_POLL		= 0x00001000,

	/*
	 * This complete SRS has had flows plumbed from IP to allow matching
	 * IPv4 packets to bypass DLS (i.e., the root SRS action).
	 *
	 * This is a vanity flag to make MAC client plumbing state clearer when
	 * debugging, and does not alter datapath behaviour.
	 *
	 * Mutable.
	 */
	SRST_DLS_BYPASS_V4		= 0x00010000,
	/*
	 * This complete SRS has had flows plumbed from IP to allow matching
	 * IPv6 packets to bypass DLS (i.e., the root SRS action).
	 *
	 * This is a vanity flag to make MAC client plumbing state clearer when
	 * debugging, and does not alter datapath behaviour.
	 *
	 * Mutable.
	 */
	SRST_DLS_BYPASS_V6		= 0x00020000,
} mac_soft_ring_set_type_t;

/*
 * Flagset reflecting the current state of datapath processing for a given
 * softring set, stored in `srs_state`.
 */
typedef enum {
	/*
	 * This Rx softring set has been temporarily prevented from processing
	 * packets.
	 *
	 * Unused.
	 */
	SRS_BLANK		= 0x00000001,
	/*
	 * This softring set's worker thread is explicitly bound to a single
	 * CPU.
	 */
	SRS_WORKER_BOUND	= 0x00000002,
	/*
	 * This complete Rx softring set's poll thread is explicitly bound to a
	 * single CPU.
	 */
	SRS_POLL_BOUND		= 0x00000004,
	/*
	 * This complete Rx softring set is created on top of (and has exclusive
	 * use of) a dedicated ring. When under sufficient load, MAC will
	 * disable interrupts and pull packets into the SRS by polling the
	 * NIC/ring, and will set `SRS_POLLING` when this is the case.
	 *
	 * This flag may be added/removed as SRSes move between
	 * hardware/software classification (e.g., if groups must be shared).
	 *
	 * TODO(ky): based on timescale, should be a type, not a state?
	 */
	SRS_POLLING_CAPAB	= 0x00000008,

	/*
	 * A thread is currently processing packets from this softring set, and
	 * has relinquished its hold on `srs_lock` to allow new packets to be
	 * enqueued while it does so.
	 *
	 * SRS processing will always enqueue packets if set, with the
	 * expectation that whoever was draining the thread will continue to
	 * do so.
	 *
	 * Requires qualification of what thread is doing the processing: either
	 * `SRS_WORKER`, `SRS_PROC_FAST`, or `SRS_POLL_PROC`.
	 */
	SRS_PROC		= 0x00000010,
	/*
	 * The Rx poll thread should request more packets from the underlying
	 * device.
	 *
	 * Requires `SRS_POLLING`.
	 */
	SRS_GET_PKTS		= 0x00000020,
	/*
	 * This Rx softring set has been moved into poll mode. Interrupts from
	 * the underlying device are disabled, and the poll thread is
	 * exclusively responsible for moving packets into the SRS.
	 *
	 * Requires `SRS_POLLING_CAPAB`.
	 */
	SRS_POLLING		= 0x00000040,

	/*
	 * The SRS worker thread currently holds `SRS_PROC`.
	 *
	 * Requires `SRS_PROC`.
	 */
	SRS_WORKER		= 0x00000100,
	/*
	 * Packets have been enqueued on this TX SRS due to either flow control
	 * or a lack of Tx descriptors on the NIC.
	 */
	SRS_ENQUEUED		= 0x00000200,
	/*
	 * `SRS_PROC` is held by the caller of `mac_rx_srs_process` (typically
	 * the interrupt context) and packets are being processed inline.
	 *
	 * Requires `SRS_PROC`.
	 */
	SRS_PROC_FAST		= 0x00000800,

	/*
	 * The Rx SRS poll thread currently holds `SRS_PROC`.
	 *
	 * Requires `SRS_PROC`.
	 */
	SRS_POLL_PROC		= 0x00001000,
	/*
	 * This Tx SRS has run out of descriptors on the underlying NIC.
	 *
	 * Any outbound packets will be queued until the underlying provider
	 * marks more descriptors as available via `mac_tx_ring_update`.
	 */
	SRS_TX_BLOCKED		= 0x00002000,
	/*
	 * This Tx SRS is flow controlled: more than `st_hiwat` packets are
	 * currently enqueued.
	 *
	 * Any outbound packets will be enqueued, and drained by the SRS
	 * worker. Senders will receive a cookie -- they will be informed when
	 * any cookie is no longer flow controlled if they have registered a
	 * callback via `mac_client_tx_notify`.
	 */
	SRS_TX_HIWAT		= 0x00004000,
	/*
	 * This Tx SRS has returned a cookie to at least one sender who has set
	 * `MAC_TX_NO_ENQUEUE` regardless of watermark state.
	 *
	 * When the SRS is drained, notify the client via its
	 * `mac_client_tx_notify` callback that it may send.
	 */
	SRS_TX_WAKEUP_CLIENT	= 0x00008000,

	/*
	 * This SRS has been signalled to stop processing any packets.
	 *
	 * Downstack entrypoints (rings, flows) which can call into this SRS
	 * should be quiesced with no remaining references such that no more
	 * packets will be enqueued while this is set.
	 *
	 * The SRS worker thread will propagate the request to any softrings.
	 */
	SRS_QUIESCE		= 0x00010000,
	/*
	 * The SRS has ceased processing any enqueued packets, the worker thread
	 * has finished quiescing any softrings and is awaiting a signal
	 * alongside either `SRS_CONDEMNED` or `SRS_RESTART` to wake up.
	 */
	SRS_QUIESCE_DONE	= 0x00020000,
	/*
	 * This SRS is marked for deletion.
	 *
	 * Downstack entrypoints (rings, flows) which can call into this SRS
	 * should be quiesced with no remaining references such that no more
	 * packets will be enqueued while this is set.
	 *
	 * The SRS worker thread will propagate the request to any softrings.
	 */
	SRS_CONDEMNED		= 0x00040000,
	/*
	 * The SRS worker has completed any teardown in response to
	 * `SRS_CONDEMNED`.
	 *
	 * Requires `SRS_CONDEMNED_DONE`.
	 */
	SRS_CONDEMNED_DONE	= 0x00080000,

	/*
	 * The SRS has been signalled to resume processing traffic.
	 *
	 * The worker thread should unset this and any `QUIESCE` flags,
	 * propagate the request to softrings and the poll thread, and
	 * resume processing packets.
	 */
	SRS_RESTART		= 0x00100000,
	/*
	 * The SRS has successfully restarted all of its softrings and poll
	 * thread, if present.
	 */
	SRS_RESTART_DONE	= 0x00200000,
	/*
	 * This Rx SRS's poll thread has quiesced in response to `SRS_QUIESCE`.
	 */
	SRS_POLL_THR_QUIESCED	= 0x00400000,
	/*
	 * This Rx SRS's poll thread has terminated in response to
	 * `SRS_CONDEMN`.
	 */
	SRS_POLL_THR_EXITED	= 0x00800000,

	/*
	 * This Rx SRS's worker thread has signalled the poll thread to resume
	 * in response to `SRS_RESTART`.
	 */
	SRS_POLL_THR_RESTART	= 0x01000000,
	/*
	 * This SRS is semi-permanently quiesced, and should not accept
	 * `SRS_RESTART` requests.
	 */
	SRS_QUIESCE_PERM	= 0x02000000,
	/*
	 * This SRS is part of the global list `mac_srs_g_list`. Its siblings
	 * are accessed via `srs_next` and `srs_prev`.
	 */
	SRS_IN_GLIST		= 0x04000000,
} mac_soft_ring_set_state_t;

#define	SRS_QUIESCED(srs)	((srs)->srs_state & SRS_QUIESCE_DONE)

/*
 * If the SRS_QUIESCE_PERM flag is set, the SRS worker thread will not be
 * able to be restarted.
 */
#define	SRS_QUIESCED_PERMANENT(srs)	((srs)->srs_state & SRS_QUIESCE_PERM)

/*
 * mac_soft_ring_set_s:
 * This is used both for Tx and Rx side. The srs_type identifies Rx or
 * Tx type.
 *
 * Note that the structure is carefully crafted, with Rx elements coming
 * first followed by Tx specific members. Future additions to this
 * structure should follow the same guidelines.
 *
 * Rx-side notes:
 * mac_rx_classify_flow_add() always creates a mac_soft_ring_set_t and fn_flow
 * points to info from it (func = srs_lower_proc, arg = soft_ring_set). On
 * interrupt path, srs_lower_proc does B/W adjustment and switch to polling mode
 * (if poll capable) and feeds the packets to soft_ring_list via choosen
 * fanout type (specified by srs_type). In poll mode, the poll thread which is
 * also a pointer can pick up the packets and feed them to various
 * soft_ring_list.
 *
 * The srs_type can either be protocol based or fanout based where fanout itelf
 * can be various types
 *
 * The polling works by turning off interrupts as soon as a packets
 * are queued on the soft ring set. Once the backlog is clear and poll
 * thread return empty handed i.e. Rx ring doesn't have anything, the
 * interrupt is turned back on. For this purpose we keep a separate
 * srs_poll_pkt_cnt counter which tracks the packets queued between SRS
 * and the soft rings as well. The counter is incremented when packets
 * are queued and decremented when SRS processes them (in case it has
 * no soft rings) or the soft ring process them. Its important that
 * in case SRS has softrings, the decrement doesn't happen till the
 * packet is processed by the soft rings since it takes very little time
 * for SRS to queue packet from SRS to soft rings and it will keep
 * bringing more packets in the system faster than soft rings can
 * process them.
 *
 * Tx side notes:
 * The srs structure acts as a serializer with a worker thread. The
 * default behavior of srs though is to act as a pass-thru. The queues
 * (srs_first, srs_last, srs_count) get used when Tx ring runs out of Tx
 * descriptors or to enforce bandwidth limits.
 *
 * When multiple Tx rings are present, the SRS state will be set to
 * SRS_FANOUT_OTH. Outgoing packets coming into mac_tx_srs_process()
 * function will be fanned out to one of the Tx side soft rings based on
 * a hint passed in mac_tx_srs_process(). Each soft ring, in turn, will
 * be associated with a distinct h/w Tx ring.
 */
struct mac_soft_ring_set_s {
	/*
	 * Common elements, common to both Rx and Tx SRS type.
	 * The following block of fields are protected by srs_lock
	 */
	kmutex_t	srs_lock;
	mac_soft_ring_set_type_t	srs_type;
	mac_soft_ring_set_state_t	srs_state;
	uint32_t	srs_count;
	mblk_t		*srs_first;	/* first mblk chain or NULL */
	mblk_t		*srs_last;	/* last mblk chain or NULL */
	kcondvar_t	srs_async;	/* cv for worker thread */
	kcondvar_t	srs_cv;		/* cv for poll thread */
	kcondvar_t	srs_quiesce_done_cv;	/* cv for removal */
	timeout_id_t	srs_tid;	/* timeout id for pending timeout */

	/*
	 * An SRS may be either _complete_ (!(srs_type & SRST_LOGICAL)), or
	 * _logical_ (srs_type & SRST_LOGICAL).
	 *
	 * Complete SRSes are valid entry points for packets, and may have the
	 * full suite of poll and/or worker threads created and bound to them.
	 * If needed, they will have a valid baked flowtree for packet delivery.
	 *
	 * Logical SRSes serve purely as lists of softrings, with bandwidth
	 * control elements if required.
	 *
	 * This field is protected by quiescence of the SRS.
	 */
	flow_tree_baked_t	srs_flowtree;

	/*
	 * List of soft rings & processing function.
	 * The following block is protected by Rx quiescence.
	 * i.e. they can be changed only after quiescing the SRS
	 * Protected by srs_lock.
	 *
	 * TODO(ky) what is the protection in Tx case?
	 */
	mac_soft_ring_t	*srs_soft_ring_head;
	mac_soft_ring_t	*srs_soft_ring_tail;
	mac_soft_ring_t	**srs_soft_rings;
	uint32_t	srs_soft_ring_count;
	uint32_t	srs_soft_ring_quiesced_count;
	uint32_t	srs_soft_ring_condemned_count;

	/*
	 * Logical SRSes which hold no actual softrings are used as queues
	 * limited by one or more bandwidth controls. These then forward onto
	 * the set of softrings held by `srs_give_to`, which may be logical or
	 * complete.
	 *
	 * This allows us to avoid creating excess softrings for BW-limited
	 * delegate Rx actions, and is used to mete out access to the underlying
	 * Tx rings for BW-limited cases.
	 */
	mac_soft_ring_set_t	*srs_give_to; /* WO */

	/*
	 * Bandwidth control related members.
	 * They are common to both Rx- and Tx-side.
	 * Following protected by srs_lock
	 */
	mac_bw_ctl_t	**srs_bw; /* WO */
	size_t		srs_bw_len; /* WO */

	/* TODO(ky): *these* should be protected by srs_lock */
	size_t		srs_size;	/* Size of packets queued in bytes */
	pri_t		srs_pri; /* srs_lock */

	mac_soft_ring_set_t	*srs_next;	/* mac_srs_g_lock */
	mac_soft_ring_set_t	*srs_prev;	/* mac_srs_g_lock */

	/* Attribute specific drain func (BW ctl vs non-BW ctl)	*/
	mac_srs_drain_proc_t	srs_drain_func;	/* srs_lock(Rx), Quiesce(tx) */

	/*
	 * If the associated ring is exclusively used by a mac client, e.g.,
	 * an aggregation, this fields is used to keep a reference to the
	 * MAC client's pseudo ring.
	 */
	mac_resource_handle_t	srs_mrh;
	/*
	 * The following blocks are write once (WO) and valid for the life
	 * of the SRS
	 */
	struct mac_client_impl_s *srs_mcip;	/* back ptr to mac client */
	struct flow_entry_s	*srs_flent;	/* back ptr to flent */

	/* Teardown, disable control ops */
	kcondvar_t	srs_client_cv;	/* Client wait for the control op */

	kthread_t	*srs_worker;	/* WO, worker thread */

	uint_t		srs_ind;	/* Round Robin indx for picking up SR */
	processorid_t	srs_worker_cpuid;	/* processor to bind to */
	processorid_t	srs_worker_cpuid_save;	/* saved cpuid during offline */
	uint_t		srs_fanout_state;

	/*
	 * Singly-linked list of logical SRSes allocated within an srs_flowtree.
	 * A complete SRS serves as the head of this list, which allows for
	 * easier walking during stats collection or quiescence.
	 */
	mac_soft_ring_set_t	*srs_logical_next;
	mac_soft_ring_set_t	*srs_complete_parent;

	/*
	 * TODO(ky) if we're doing one SRS per flent in the tree (PER RING!),
	 * then can we share this where relevant? It's like 5KiB each go.
	 */
	mac_cpus_t	srs_cpu;

	union {
		mac_srs_rx_t	rx; /* !(srs_type & SRST_TX) */
		mac_srs_tx_t	tx; /* srs_type & SRST_TX */
	} srs_data;

	kstat_t		*srs_ksp;
};

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

inline flow_entry_t *
mac_srs_rx_action_flent(mac_soft_ring_set_t *srs)
{
	ASSERT(!mac_srs_is_tx(srs));

	return ((srs->srs_data.rx.sr_act_as != NULL) ?
	    srs->srs_data.rx.sr_act_as :
	    srs->srs_flent);
}

inline flow_action_t *
mac_srs_rx_action(mac_soft_ring_set_t *srs)
{
	return (&mac_srs_rx_action_flent(srs)->fe_action);
}

/*
 * soft ring set (SRS) Tx modes
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
 * SRS fanout states
 */
typedef enum {
	SRS_FANOUT_UNINIT = 0,
	SRS_FANOUT_INIT,
	SRS_FANOUT_REINIT
} mac_srs_fanout_state_t;

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
	if ((mac_srs)->srs_type & SRST_BW_CONTROL)		\
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
 * Decrement the cumulative packet count in SRS and its
 * soft rings. If the srs_poll_pkt_cnt goes below lowat, then check
 * if if the interface was left in a polling mode and no one
 * is really processing the queue (to get the interface out
 * of poll mode). If no one is processing the queue, then
 * acquire the PROC and signal the poll thread to check the
 * interface for packets and get the interface back to interrupt
 * mode if nothing is found.
 */
__attribute__((always_inline))
inline void
mac_update_srs_count(mac_soft_ring_set_t *mac_srs, uint32_t cnt)
{
	/*
	 * Poll packet occupancy is not tracked by Tx SRSes.
	 */
	if (mac_srs_is_tx(mac_srs)) {
		return;
	}

	const bool is_logical = mac_srs_is_logical(mac_srs);

	/*
	 * The poll packet count on a logical SRS serves no real function, we
	 * want to feed this information back to control the poll thread of the
	 * complete SRS.
	 */
	mac_soft_ring_set_t *true_target = (is_logical) ?
	    mac_srs->srs_complete_parent : mac_srs;
	ASSERT3P(true_target, !=, NULL);

	mac_srs_rx_t *srs_rx = &true_target->srs_data.rx;

	const uint32_t point_value = atomic_add_32_nv(
	    &srs_rx->sr_poll_pkt_cnt, -cnt);
	if (point_value <= srs_rx->sr_poll_thres) {
		mutex_enter(&true_target->srs_lock);
		/*
		 * Re-verify count/flags.
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
extern void mac_soft_ring_destroy(mac_soft_ring_t *);

/* Rx SRS */
extern void mac_srs_free(mac_soft_ring_set_t *);
extern void mac_srs_signal(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_srs_signal_diff(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t, const mac_soft_ring_set_state_t);
extern cpu_t *mac_srs_bind(mac_soft_ring_set_t *, processorid_t);
extern void mac_rx_srs_retarget_intr(mac_soft_ring_set_t *, processorid_t);
extern void mac_tx_srs_retarget_intr(mac_soft_ring_set_t *);

extern void mac_srs_quiesce_initiate(mac_soft_ring_set_t *);
extern void mac_srs_client_poll_quiesce(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_srs_client_poll_restart(mac_soft_ring_set_t *);
extern void mac_rx_srs_quiesce(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t, const bool);
extern void mac_rx_srs_restart(mac_soft_ring_set_t *);
extern void mac_tx_srs_quiesce(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);

/* Tx SRS, Tx softring */
extern void mac_tx_srs_wakeup(mac_soft_ring_set_t *, mac_ring_handle_t);
extern void mac_tx_srs_setup(struct mac_client_impl_s *, flow_entry_t *);
extern mac_tx_func_t mac_tx_get_func(uint32_t);
extern mblk_t *mac_tx_send(mac_client_handle_t, mac_ring_handle_t, mblk_t *,
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
extern void mac_srs_adjust_subflow_bwlimit(struct mac_client_impl_s *);
extern void mac_srs_update_drv(struct mac_client_impl_s *);
extern void mac_update_srs_priority(mac_soft_ring_set_t *, pri_t);

extern int mac_soft_ring_intr_enable(void *);
extern boolean_t mac_soft_ring_intr_disable(void *);
extern mac_soft_ring_t *mac_soft_ring_create(int, clock_t, mac_soft_ring_type_t,
    pri_t, mac_client_impl_t *, mac_soft_ring_set_t *,
    processorid_t, mac_direct_rx_t, void *, mac_resource_handle_t);
extern cpu_t *mac_soft_ring_bind(mac_soft_ring_t *, processorid_t);
	extern void mac_soft_ring_unbind(mac_soft_ring_t *);
extern void mac_soft_ring_free(mac_soft_ring_t *);
extern void mac_soft_ring_signal(mac_soft_ring_t *,
    const mac_soft_ring_state_t);
extern void mac_rx_soft_ring_process(mac_soft_ring_t *, mblk_t *, mblk_t *, int,
    size_t);
extern mac_tx_cookie_t mac_tx_soft_ring_process(mac_soft_ring_t *,
    mblk_t *, uint16_t, mblk_t **);
extern void mac_srs_worker_quiesce(mac_soft_ring_set_t *);
extern void mac_srs_worker_restart(mac_soft_ring_set_t *);
extern void mac_rx_attach_flow_srs(mac_impl_t *, flow_entry_t *,
    mac_soft_ring_set_t *, mac_ring_t *, mac_classify_type_t);

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
extern void mac_rx_srs_process(void *, mac_resource_handle_t, mblk_t *,
    boolean_t);
extern void mac_srs_worker(mac_soft_ring_set_t *);
extern void mac_rx_srs_poll_ring(mac_soft_ring_set_t *);
extern void mac_tx_srs_drain(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);
extern void mac_srs_drain_forward(mac_soft_ring_set_t *,
    const mac_soft_ring_set_state_t);

extern void mac_tx_srs_restart(mac_soft_ring_set_t *);
extern void mac_rx_srs_remove(mac_soft_ring_set_t *);

/* Bandwidth control related functions */
inline bool
mac_srs_any_bw_enabled(const mac_soft_ring_set_t *srs)
{
	for (size_t i = 0; i < srs->srs_bw_len; i++) {
		if (mac_bw_ctl_is_enabled(srs->srs_bw[i])) {
			return (true);
		}
	}
	return (false);
}

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

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MAC_SOFT_RING_H */
