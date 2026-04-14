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

/*
 * Kernel DPE (DICE Protection Environment) Framework
 *
 * This module implements a generic framework for managing DPE providers
 * and dispatching DPE operations on behalf of in-kernel consumers.  See
 * dpe.h for the user-facing API.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/debug.h>
#include <sys/disp.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/modctl.h>
#include <sys/sysmacros.h>
#include <sys/taskq.h>

#include "dpe.h"
#include "dpe_impl.h"

/*
 * Global lists of registered providers and consumers, both protected by
 * `dpe_lock`.  The provider list is walked by dpe_open() to match a
 * consumer's requested profile to an available provider.  The consumer
 * list is walked by dpe_provider_register() to notify registered consumers
 * of a newly available provider.
 */
static kmutex_t dpe_lock;
static list_t dpe_providers;
static list_t dpe_consumers;

/*
 * Taskq used to deliver consumer notifications.  Notifications are coalesced
 * with a single outstanding event per-consumer.
 */
static taskq_t *dpe_notify_taskq;

/*
 * If the given provider advertises support for `profile`, return true and
 * (optionally) set `infop` to point at the provider's metadata entry for
 * the profile.  The pointer is valid for the lifetime of the provider's
 * registration.
 *
 * Note: a provider's profile list is immutable after registration, so this
 * function must be called with the global `dpe_lock` held rather than the
 * per-provider lock.
 */
static bool
dpe_prov_supports(const dpe_prov_t *prov, dpe_profile_t profile,
    const dpe_profile_info_t **infop)
{
	ASSERT(MUTEX_HELD(&dpe_lock));
	for (size_t i = 0; i < prov->dp_nprofiles; i++) {
		if (prov->dp_profiles[i].dpi_profile == profile) {
			if (infop != NULL)
				*infop = &prov->dp_profiles[i];
			return (true);
		}
	}
	return (false);
}

static void
dpe_consumer_notify_task(void *arg)
{
	dpe_consumer_t *dcon = arg;
	dpe_cb_res_t res;

	mutex_enter(&dpe_lock);
	do {
		dcon->dc_renotify = false;

		/*
		 * The consumer may have unregistered or completed (via
		 * DPE_CB_DONE) since this notification was dispatched; if so
		 * deliver nothing further.
		 */
		if (!list_link_active(&dcon->dc_node) || dcon->dc_done)
			break;

		dcon->dc_thread = curthread;
		mutex_exit(&dpe_lock);
		res = dcon->dc_cb(dcon->dc_arg);
		mutex_enter(&dpe_lock);
		dcon->dc_thread = NULL;

		if (res == DPE_CB_DONE)
			dcon->dc_done = true;
	} while (dcon->dc_renotify);

	dcon->dc_dispatched = false;
	cv_broadcast(&dcon->dc_cv);
	mutex_exit(&dpe_lock);
}

/*
 * Notify a consumer that a provider supporting its desired profile may be
 * available.  Callbacks run in taskq context so that a provider registering
 * from its attach(9e) never runs consumer code (which will typically call back
 * into the framework) in that context.
 *
 * Notifications are coalesced: at most one notify task is outstanding per
 * consumer, with additional notifications folded into `dc_renotify` and
 * consumed by the running task.
 */
static void
dpe_consumer_notify(dpe_consumer_t *dcon)
{
	ASSERT(MUTEX_HELD(&dpe_lock));

	/*
	 * An unregistered consumer would have been removed from the list,
	 * so if it's still present it must be active.
	 */
	ASSERT(list_link_active(&dcon->dc_node));

	if (dcon->dc_done)
		return;

	if (dcon->dc_dispatched) {
		dcon->dc_renotify = true;
		return;
	}

	dcon->dc_dispatched = true;
	taskq_dispatch_ent(dpe_notify_taskq, dpe_consumer_notify_task, dcon,
	    TQ_NOSLEEP, &dcon->dc_tqent);
}

int
dpe_consumer_register(dpe_profile_t profile, const char *provider_hint,
    dpe_avail_cb_f cb, void *arg, dpe_consumer_t **dconp)
{
	dpe_consumer_t *dcon;
	dpe_prov_t *prov;
	bool hint_present = false, found_provider = false;

	if (cb == NULL || dconp == NULL)
		return (EINVAL);

	if (provider_hint != NULL) {
		size_t hint_len = strnlen(provider_hint, DPE_PROVIDER_NAME_MAX);
		if (hint_len >= DPE_PROVIDER_NAME_MAX || hint_len == 0)
			return (EINVAL);
	}

	dcon = kmem_zalloc(sizeof (*dcon), KM_SLEEP);
	dcon->dc_profile = profile;
	dcon->dc_cb = cb;
	dcon->dc_arg = arg;
	cv_init(&dcon->dc_cv, NULL, CV_DRIVER, NULL);

	mutex_enter(&dpe_lock);
	list_insert_tail(&dpe_consumers, dcon);

	/*
	 * If a matching provider is already registered, notify the consumer
	 * right away.
	 */
	for (prov = list_head(&dpe_providers); prov != NULL;
	    prov = list_next(&dpe_providers, prov)) {
		if (dpe_prov_supports(prov, profile, NULL))
			found_provider = true;
		if (provider_hint != NULL &&
		    strcmp(prov->dp_name, provider_hint) == 0) {
			hint_present = true;
		}
	}
	if (found_provider)
		dpe_consumer_notify(dcon);
	mutex_exit(&dpe_lock);

	/*
	 * If the hinted provider has yet to register, try a best effort attempt
	 * to get its driver loaded and attached.  This specifically is for the
	 * case where the consumer gets attached after both consumer and
	 * provider were detached post-initial boot-time configuration.  In that
	 * scenario, there's nothing that would otherwise trigger the provider
	 * to attach.
	 *
	 * This must be called without `dpe_lock` held: attaching the driver
	 * runs its attach(9e), which will typically call
	 * dpe_provider_register().
	 */
	if (provider_hint != NULL && !hint_present) {
		major_t major = ddi_name_to_major(provider_hint);
		if (major != DDI_MAJOR_T_NONE) {
			if (ddi_hold_installed_driver(major) != NULL)
				ddi_rele_driver(major);
		}
	}

	*dconp = dcon;
	return (0);
}

void
dpe_consumer_unregister(dpe_consumer_t *dcon)
{
	if (dcon == NULL)
		return;

	mutex_enter(&dpe_lock);

	/*
	 * Unregistering from the availability callback itself would deadlock
	 * below as we waited on our own invocation to complete.
	 */
	VERIFY3P(dcon->dc_thread, !=, curthread);

	list_remove(&dpe_consumers, dcon);
	while (dcon->dc_dispatched)
		cv_wait(&dcon->dc_cv, &dpe_lock);
	mutex_exit(&dpe_lock);

	cv_destroy(&dcon->dc_cv);
	kmem_free(dcon, sizeof (*dcon));
}

int
dpe_provider_register(const dpe_provider_t *desc, dpe_prov_t **provp)
{
	dpe_prov_t *prov;
	dpe_consumer_t *dcon;
	size_t prov_name_len;

	if (desc == NULL || desc->dpp_profiles == NULL ||
	    desc->dpp_nprofiles == 0 || desc->dpp_ops == NULL ||
	    provp == NULL) {
		return (EINVAL);
	}

	prov_name_len = strnlen(desc->dpp_name, DPE_PROVIDER_NAME_MAX);
	if (prov_name_len >= DPE_PROVIDER_NAME_MAX || prov_name_len == 0) {
		return (EINVAL);
	}

	prov = kmem_zalloc(sizeof (*prov), KM_SLEEP);
	CTASSERT(sizeof (prov->dp_name) == sizeof (desc->dpp_name));
	bcopy(desc->dpp_name, prov->dp_name, prov_name_len);
	prov->dp_profiles = desc->dpp_profiles;
	prov->dp_nprofiles = desc->dpp_nprofiles;
	prov->dp_ops = desc->dpp_ops;
	prov->dp_private = desc->dpp_private;
	mutex_init(&prov->dp_lock, NULL, MUTEX_DRIVER, NULL);
	prov->dp_in_use = false;

	mutex_enter(&dpe_lock);
	list_insert_tail(&dpe_providers, prov);

	/*
	 * Notify any consumers waiting on a provider supporting one of our
	 * profiles.
	 */
	for (dcon = list_head(&dpe_consumers); dcon != NULL;
	    dcon = list_next(&dpe_consumers, dcon)) {
		if (dpe_prov_supports(prov, dcon->dc_profile, NULL))
			dpe_consumer_notify(dcon);
	}
	mutex_exit(&dpe_lock);

	*provp = prov;
	return (0);
}

int
dpe_provider_unregister(dpe_prov_t *prov)
{
	if (prov == NULL)
		return (EINVAL);

	mutex_enter(&dpe_lock);
	mutex_enter(&prov->dp_lock);
	if (prov->dp_in_use) {
		mutex_exit(&prov->dp_lock);
		mutex_exit(&dpe_lock);
		return (EBUSY);
	}
	mutex_exit(&prov->dp_lock);

	list_remove(&dpe_providers, prov);
	mutex_exit(&dpe_lock);

	mutex_destroy(&prov->dp_lock);
	kmem_free(prov, sizeof (*prov));
	return (0);
}

dpe_error_t
dpe_open(dpe_profile_t profile, const char *provider_name, dpe_ctx_t **ctxp)
{
	dpe_prov_t *prov, *chosen = NULL;
	const dpe_profile_info_t *chosen_info = NULL;
	dpe_ctx_t *ctx;
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };
	bool matched_prov = false, matched_profile = false;

	if (ctxp == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	mutex_enter(&dpe_lock);
	for (prov = list_head(&dpe_providers); prov != NULL;
	    prov = list_next(&dpe_providers, prov)) {
		const dpe_profile_info_t *info;

		if (provider_name != NULL &&
		    strcmp(prov->dp_name, provider_name) != 0)
			continue;

		matched_prov = true;

		if (!dpe_prov_supports(prov, profile, &info))
			continue;

		matched_profile = true;

		mutex_enter(&prov->dp_lock);
		if (!prov->dp_in_use) {
			prov->dp_in_use = true;
			mutex_exit(&prov->dp_lock);
			chosen = prov;
			chosen_info = info;
			break;
		}
		mutex_exit(&prov->dp_lock);
	}
	mutex_exit(&dpe_lock);

	if (chosen == NULL) {
		if (provider_name != NULL && !matched_prov)
			err.dpe_error = DPE_E_NO_SUCH_PROVIDER;
		else if (matched_profile)
			err.dpe_error = DPE_E_BUSY;
		else
			err.dpe_error = DPE_E_PROFILE_UNSUPPORTED;
		return (err);
	}

	ctx = kmem_zalloc(sizeof (*ctx), KM_SLEEP);
	ctx->dc_prov = chosen;
	ctx->dc_profile = *chosen_info;
	ctx->dc_derived_context = false;

	*ctxp = ctx;
	return (err);
}

dpe_error_t
dpe_close(dpe_ctx_t *ctx)
{
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	if (ctx->dc_derived_context) {
		err = dpe_destroy_context(ctx);
		if (err.dpe_error != DPE_E_OK)
			return (err);
	}

	mutex_enter(&ctx->dc_prov->dp_lock);
	ctx->dc_prov->dp_in_use = false;
	mutex_exit(&ctx->dc_prov->dp_lock);

	kmem_free(ctx, sizeof (*ctx));
	return (err);
}

dpe_error_t
dpe_get_profile_info(dpe_ctx_t *ctx, dpe_profile_info_t *info)
{
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL || info == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	*info = ctx->dc_profile;
	return (err);
}

dpe_error_t
dpe_derive_context(dpe_ctx_t *ctx, const uint8_t *type, size_t type_size,
    const uint8_t *hash, size_t hash_size, const dpe_derive_opts_t *opts)
{
	const dpe_profile_info_t *profile;
	const dpe_derive_opts_t zero_opts = { 0 };
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL || type == NULL || hash == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	profile = &ctx->dc_profile;

	if (type_size != profile->dpi_type_size) {
		err.dpe_error = DPE_E_BAD_TYPE_LEN;
		return (err);
	}
	if (hash_size != profile->dpi_hash_size) {
		err.dpe_error = DPE_E_BAD_HASH_LEN;
		return (err);
	}

	err.dpe_prov = ctx->dc_prov->dp_ops->dop_derive_context(
	    ctx->dc_prov->dp_private, type, hash, opts ? opts : &zero_opts);
	if (err.dpe_prov != DPE_PROV_E_OK)
		err.dpe_error = DPE_E_PROVIDER;

	/*
	 * Note we've successfully derived a context to cleanup on close.
	 */
	if (err.dpe_error == DPE_E_OK)
		ctx->dc_derived_context = true;

	return (err);
}

dpe_error_t
dpe_certify_key(dpe_ctx_t *ctx, const uint8_t *label, size_t label_size,
    uint8_t *pubkey, size_t pubkey_size, uint8_t *cert, uint32_t *cert_size)
{
	const dpe_profile_info_t *profile;
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL || label == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	profile = &ctx->dc_profile;

	if (label_size != profile->dpi_label_size) {
		err.dpe_error = DPE_E_BAD_LABEL_LEN;
		return (err);
	}
	if ((pubkey != NULL && pubkey_size != profile->dpi_key_size) ||
	    (pubkey == NULL && pubkey_size != 0)) {
		err.dpe_error = DPE_E_BAD_KEY_LEN;
		return (err);
	}
	if (cert != NULL && cert_size == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}
	if (cert_size != NULL && *cert_size != profile->dpi_max_cert_size) {
		err.dpe_error = DPE_E_BAD_CERT_LEN;
		return (err);
	}

	err.dpe_prov = ctx->dc_prov->dp_ops->dop_certify_key(
	    ctx->dc_prov->dp_private, label, pubkey, cert, cert_size);
	if (err.dpe_prov != DPE_PROV_E_OK)
		err.dpe_error = DPE_E_PROVIDER;
	return (err);
}

dpe_error_t
dpe_sign(dpe_ctx_t *ctx, const uint8_t *label, size_t label_size,
    const uint8_t *data, size_t data_size, uint8_t *sig, size_t sig_size)
{
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL || label == NULL || data == NULL || sig == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	if (label_size != ctx->dc_profile.dpi_label_size) {
		err.dpe_error = DPE_E_BAD_LABEL_LEN;
		return (err);
	}
	if (data_size != ctx->dc_profile.dpi_hash_size) {
		err.dpe_error = DPE_E_BAD_HASH_LEN;
		return (err);
	}
	if (sig_size != ctx->dc_profile.dpi_sig_size) {
		err.dpe_error = DPE_E_BAD_SIG_LEN;
		return (err);
	}

	err.dpe_prov = ctx->dc_prov->dp_ops->dop_sign(
	    ctx->dc_prov->dp_private, label, data, sig);
	if (err.dpe_prov != DPE_PROV_E_OK)
		err.dpe_error = DPE_E_PROVIDER;
	return (err);
}

dpe_error_t
dpe_get_cert_chain_size(dpe_ctx_t *ctx, uint32_t *sizep)
{
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL || sizep == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	err.dpe_prov = ctx->dc_prov->dp_ops->dop_get_cert_chain_size(
	    ctx->dc_prov->dp_private, sizep);
	if (err.dpe_prov != DPE_PROV_E_OK)
		err.dpe_error = DPE_E_PROVIDER;
	return (err);
}

dpe_error_t
dpe_get_cert_chain(dpe_ctx_t *ctx, uint32_t offset, uint32_t req_sz,
    uint8_t *out, uint32_t *got)
{
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL || out == NULL || got == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	err.dpe_prov = ctx->dc_prov->dp_ops->dop_get_cert_chain(
	    ctx->dc_prov->dp_private, offset, req_sz, out, got);
	if (err.dpe_prov != DPE_PROV_E_OK)
		err.dpe_error = DPE_E_PROVIDER;
	return (err);
}

dpe_error_t
dpe_destroy_context(dpe_ctx_t *ctx)
{
	dpe_error_t err = { .dpe_error = DPE_E_OK, .dpe_prov = DPE_PROV_E_OK };

	if (ctx == NULL) {
		err.dpe_error = DPE_E_BAD_ARG;
		return (err);
	}

	err.dpe_prov = ctx->dc_prov->dp_ops->dop_destroy_context(
	    ctx->dc_prov->dp_private);
	if (err.dpe_prov != DPE_PROV_E_OK)
		err.dpe_error = DPE_E_PROVIDER;

	if (err.dpe_error == DPE_E_OK)
		ctx->dc_derived_context = false;

	return (err);
}

static struct modlmisc dpe_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "DICE Protection Environment Framework"
};

static struct modlinkage dpe_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &dpe_modlmisc, NULL }
};

int
_init(void)
{
	int ret;

	mutex_init(&dpe_lock, NULL, MUTEX_DRIVER, NULL);
	list_create(&dpe_providers, sizeof (dpe_prov_t),
	    offsetof(dpe_prov_t, dp_node));
	list_create(&dpe_consumers, sizeof (dpe_consumer_t),
	    offsetof(dpe_consumer_t, dc_node));
	dpe_notify_taskq = taskq_create("dpe_notify", 1, minclsyspri, 1, 1,
	    TASKQ_PREPOPULATE);

	if ((ret = mod_install(&dpe_modlinkage)) != 0) {
		taskq_destroy(dpe_notify_taskq);
		list_destroy(&dpe_consumers);
		list_destroy(&dpe_providers);
		mutex_destroy(&dpe_lock);
	}
	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&dpe_modlinkage, modinfop));
}

int
_fini(void)
{
	int ret;

	mutex_enter(&dpe_lock);
	if (!list_is_empty(&dpe_providers) || !list_is_empty(&dpe_consumers)) {
		mutex_exit(&dpe_lock);
		return (EBUSY);
	}
	mutex_exit(&dpe_lock);

	if ((ret = mod_remove(&dpe_modlinkage)) != 0)
		return (ret);

	taskq_destroy(dpe_notify_taskq);
	list_destroy(&dpe_consumers);
	list_destroy(&dpe_providers);
	mutex_destroy(&dpe_lock);
	return (0);
}
