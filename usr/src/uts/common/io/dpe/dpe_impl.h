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

#ifndef _IO_DPE_DPE_IMPL_H
#define	_IO_DPE_DPE_IMPL_H

/*
 * DPE framework internal types.
 */

#include <sys/types.h>
#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/thread.h>
#include <sys/taskq_impl.h>
#include <sys/esunddi.h>

#include "dpe.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	/*
	 * The provider is currently registered: its driver is attached and
	 * `dp_ops`/`dp_private` are usable.  Cleared when the provider
	 * driver detaches (dpe_provider_unregister()).
	 */
	DPE_PROV_F_VALID	= 1 << 0,
	/*
	 * The provider's device node is being removed (DDI unbind callback
	 * has fired).  The entry is off the providers list and awaiting
	 * final teardown.
	 */
	DPE_PROV_F_REMOVED	= 1 << 1,
} dpe_prov_flags_t;

/*
 * Internal representation of a registered provider.  A `dpe_prov_t` is
 * allocated and initialized by the framework when a backend first calls
 * dpe_provider_register() and lives until the provider's device node is
 * unbound: the provider driver detaching merely marks it invalid (see
 * dpe.c for the full lifecycle).  All mutable state is protected by the
 * global `dpe_lock`.
 */
struct dpe_prov {
	list_node_t		dp_node;	/* on dpe_providers list */

	/*
	 * Identity / capabilities, captured at first registration.
	 * `dp_profiles` is a framework-owned copy. `dp_path` is the
	 * provider's /devices path, used to revive (re-attach) it on
	 * demand.  `dp_ops`/`dp_private` reference provider module memory
	 * and are refreshed on every re-registration and NULLed while the
	 * provider is not valid.
	 */
	char			dp_name[DPE_PROVIDER_NAME_MAX];
	dev_info_t		*dp_dip;
	char			*dp_path;
	dpe_profile_info_t	*dp_profiles;
	size_t			dp_nprofiles;
	const dpe_ops_t		*dp_ops;
	void			*dp_private;

	taskq_ent_t		dp_tqent;
	ddi_unbind_callback_t	dp_cb;
	dpe_prov_flags_t	dp_flags;
	kcondvar_t		dp_cv;

	/*
	 * The consumer context holding the exclusive claim on this
	 * provider, or NULL if unclaimed.  Only one context may be bound
	 * to the provider at a time.  While claimed, the context also
	 * holds the provider's device node (see dpe_open()), pinning it
	 * attached.  `dp_cv` is signaled when the claim is dropped.
	 */
	dpe_ctx_t		*dp_owner;
};

/*
 * Consumer context.  Returned to callers as an opaque pointer.
 * Created by dpe_open(), destroyed by dpe_close().  Bound for its
 * lifetime to the specific provider selected at open time and to the
 * profile requested by the consumer.
 */
struct dpe_ctx {
	dpe_prov_t		*dc_prov;
	dpe_profile_info_t	dc_profile;
	bool			dc_derived_context;
};

/*
 * Internal representation of a registered consumer.  A `dpe_consumer_t` is
 * allocated and initialized by the framework when a consumer calls
 * dpe_consumer_register().  The identity fields are immutable for the life
 * of the registration; the callback dispatch state is protected by the
 * global `dpe_lock`, with `dc_cv` signaled when the notify task completes.
 *
 * Notifications are coalesced: at most one notify task is outstanding per
 * consumer (`dc_dispatched`, dispatched via the embedded `dc_tqent` so
 * dispatch never allocates or sleeps).  A notification arriving while the
 * task is outstanding sets `dc_renotify`, which the task consumes by
 * invoking the callback again before retiring.  `dc_thread` identifies the
 * thread currently executing the callback.
 */
struct dpe_consumer {
	list_node_t	dc_node;	/* on dpe_consumers list */

	dpe_profile_t	dc_profile;
	dpe_avail_cb_f	dc_cb;
	void		*dc_arg;

	taskq_ent_t	dc_tqent;
	bool		dc_dispatched;	/* notify task outstanding */
	bool		dc_renotify;	/* notified while task outstanding */
	bool		dc_done;	/* callback returned DPE_CB_DONE */
	kthread_t	*dc_thread;	/* thread running the callback */
	kcondvar_t	dc_cv;
};

#ifdef __cplusplus
}
#endif

#endif /* _IO_DPE_DPE_IMPL_H */
