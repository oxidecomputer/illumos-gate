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

#ifndef _IO_DPE_DPE_H
#define	_IO_DPE_DPE_H

/*
 * Kernel DPE (DICE Protection Environment) Framework
 *
 * This header defines the interfaces used by backend providers to register DPE
 * implementations, and by in-kernel consumers to perform DPE operations against
 * a registered backend:
 *
 *   - A provider registers with a name, a set of supported profiles, and
 *     an ops vector.  The framework keeps a list of providers.
 *
 *   - A consumer calls dpe_open() requesting a specific profile (and optionally
 *     a specific provider).  The framework selects a registered provider that
 *     supports the requested profile and is not currently in use, returning a
 *     context handle.
 *
 *   - A consumer with no ordering relationship to its provider's driver may
 *     instead register interest in a profile via dpe_consumer_register().
 *     The framework invokes the consumer's callback once a provider
 *     supporting the profile is available, at which point the consumer can
 *     dpe_open() as usual.
 *
 *   - All DPE operations are issued through the returned context.  The
 *     framework routes each operation to the owning provider's ops.
 *
 *   - Only one consumer context may be outstanding against a given
 *     provider at a time (enforced by the framework).  Different
 *     providers may be used concurrently.
 *
 * See the DICE Protection Environment Specification for the semantics of the
 * underlying operations.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/dditypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This represents the set of errors that can be returned by the DPE framework.
 */
typedef enum {
	DPE_E_OK = 0,
	/*
	 * A provider-specific error occurred (see `dpe_prov_error_t`).
	 */
	DPE_E_PROVIDER,
	/*
	 * A required argument was NULL or otherwise invalid.
	 */
	DPE_E_BAD_ARG,
	/*
	 * No registered provider supports the requested profile.
	 */
	DPE_E_PROFILE_UNSUPPORTED,
	/*
	 * The requested provider does not exist.
	 */
	DPE_E_NO_SUCH_PROVIDER,
	/*
	 * One or more registered providers support the requested profile,
	 * but all matching providers are currently in use.
	 */
	DPE_E_BUSY,
	/*
	 * These indicate a provided buffer is not the correct size for the
	 * operation.
	 */
	DPE_E_BAD_HASH_LEN,
	DPE_E_BAD_KEY_LEN,
	DPE_E_BAD_SIG_LEN,
	DPE_E_BAD_TYPE_LEN,
	DPE_E_BAD_LABEL_LEN,
	DPE_E_BAD_CERT_LEN,
} dpe_errno_t;

/*
 * This represents the set of provider-specific errors that can be returned by
 * the DPE framework.
 */
typedef enum {
	DPE_PROV_E_OK = 0,
	/*
	 * This represents a generic error in the provider.  This should be used
	 * sparingly and not otherwise be used for errors specific to DPE.
	 */
	DPE_PROV_E_INTERNAL,
	/*
	 * The provider was asked to perform an operation it does not support.
	 */
	DPE_PROV_E_UNSUP_CMD,
	/*
	 * The provider received invalid data in a request.
	 */
	DPE_PROV_E_BAD_REQ,
	/*
	 * The provider received invalid data in response to a request.
	 */
	DPE_PROV_E_BAD_RESP,
	/*
	 * The provider timed out while submitting or processing a request.
	 */
	DPE_PROV_E_TIMEDOUT,
} dpe_prov_error_t;

/*
 * This represents a combined error code returned by the DPE framework with
 * an optional provider-augmented error.
 */
typedef struct {
	dpe_errno_t		dpe_error;
	dpe_prov_error_t	dpe_prov;
} dpe_error_t;

/*
 * DPE profile identifiers.
 */
typedef enum dpe_profile {
	DPE_PROFILE_IROT_P384_SHA384 = 1,
} dpe_profile_t;

/*
 * Maximum length of a profile name string (incl. NUL terminator).
 */
#define	DPE_PROFILE_NAME_MAX		32

/*
 * Description of a profile: algorithm sizes, name, etc.  Each provider
 * publishes one of these per profile it supports; consumers obtain a
 * copy for the active profile via dpe_get_profile_info().
 */
typedef struct dpe_profile_info {
	dpe_profile_t	dpi_profile;
	char		dpi_name[DPE_PROFILE_NAME_MAX];
	uint32_t	dpi_hash_size;	/* digest size */
	uint32_t	dpi_key_size;	/* public key size */
	uint32_t	dpi_sig_size;	/* signature size */
	uint32_t	dpi_type_size;	/* DeriveContext type tag size */
	uint32_t	dpi_label_size;	/* CertifyKey/Sign label size */
	uint32_t	dpi_max_cert_size; /* max CertifyKey cert size */
} dpe_profile_info_t;

/*
 * Flags used when deriving new contexts.  Provider support for any given flag
 * is optional.
 */
typedef enum dpe_derive_ctx_flags {
	/*
	 * If supported, indicates some basic information like the DPE version
	 * and profile should be included as additional input to the DICE
	 * computation for the derived context.
	 */
	DDCF_INCLUDE_INFO	= 1 << 0,
	/*
	 * If supported, indicates the internal DICE state of the DPE should be
	 * included as additional input to the DICE computation for the derived
	 * context.
	 */
	DDCF_INCLUDE_DICE	= 1 << 1,
} dpe_derive_ctx_flags_t;

/*
 * Options for dpe_derive_context().
 *
 * `ddo_flags' is a bitmap of optional flags (dpe_derive_ctx_flags_t); unknown
 * bits are reserved and must be zero.  Callers may pass NULL to
 * dpe_derive_context() to request default behaviour (no flags set).
 */
typedef struct dpe_derive_opts {
	dpe_derive_ctx_flags_t	ddo_flags;
} dpe_derive_opts_t;

/*
 * Backend ops vector.  Each callback receives the provider's opaque
 * private pointer (dpp_private) followed by operation-specific arguments.
 * Buffers passed in are sized according to the active profile.
 */
typedef struct dpe_ops {
	/*
	 * DeriveContext: extend the DPE measurement chain.
	 *
	 * `type` - a `dpi_type_size` byte tag.
	 * `hash` - a `dpi_hash_size` byte digest.
	 * `opts` - optional derive options.
	 */
	dpe_prov_error_t (*dop_derive_context)(void *priv, const uint8_t *type,
	    const uint8_t *hash, const dpe_derive_opts_t *opts);

	/*
	 * CertifyKey: derive an attestation key from the given label and
	 * return its public key and certificate. Invalidates any previously
	 * generated certificate for the current context.
	 *
	 * `label`  - `dpi_label_size` bytes.
	 * `pubkey` - may be NULL, otherwise receives `dpi_key_size` bytes.
	 * `cert`   - may be NULL, otherwise receives up to `dpi_max_cert_size`
	 * bytes and `cert_size` is updated with the actual size on return.
	 */
	dpe_prov_error_t (*dop_certify_key)(void *priv, const uint8_t *label,
	    uint8_t *pubkey, uint8_t *cert, uint32_t *cert_size);

	/*
	 * Sign: sign the given data with the attestation key derived from the
	 * given label and return the resulting signature.
	 *
	 * `data`  - `dpi_hash_size` bytes.
	 * `label` - `dpi_label_size` bytes.
	 * `sig`   - receives `dpi_sig_size` bytes.
	 */
	dpe_prov_error_t (*dop_sign)(void *priv, const uint8_t *label,
	    const uint8_t *data, uint8_t *sig);

	/*
	 * GetCertChainSize: return the total size in bytes of the certificate
	 * chain for the current context.
	 */
	dpe_prov_error_t (*dop_get_cert_chain_size)(void *priv,
	    uint32_t *sizep);

	/*
	 * GetCertChain: retrieve up to `req_sz` bytes of the certificate
	 * chain starting at `offset`.  On return `*got` is set to the number
	 * of bytes actually returned.  Providers may return fewer bytes
	 * than requested (in which case the caller should iterate).
	 */
	dpe_prov_error_t (*dop_get_cert_chain)(void *priv, uint32_t offset,
	    uint32_t req_sz, uint8_t *out, uint32_t *got);

	/*
	 * DestroyContext: discard the current DPE context, invalidating
	 * any previous measurements and derived keys.
	 */
	dpe_prov_error_t (*dop_destroy_context)(void *priv);
} dpe_ops_t;

/*
 * Maximum length of a provider name string (incl. NUL terminator).
 */
#define	DPE_PROVIDER_NAME_MAX		32

/*
 * Provider descriptor.  Passed (by reference) to dpe_provider_register().
 * The framework copies the name and profile array into its own provider
 * entry.  `dpp_ops` must remain valid while the provider is registered
 * (i.e. until the matching dpe_provider_unregister() in detach(9E)).
 * The framework only invokes the ops while the provider driver is attached.
 * `dpp_dip` is the registering driver's own devinfo node.
 */
typedef struct dpe_provider {
	char				dpp_name[DPE_PROVIDER_NAME_MAX];
	dev_info_t			*dpp_dip;
	const dpe_profile_info_t	*dpp_profiles;
	size_t				dpp_nprofiles;
	const dpe_ops_t			*dpp_ops;
	void				*dpp_private;
} dpe_provider_t;

/*
 * Opaque registration handle returned by dpe_provider_register().
 */
typedef struct dpe_prov dpe_prov_t;

/*
 * Opaque consumer context handle returned by dpe_open().
 */
typedef struct dpe_ctx dpe_ctx_t;

/*
 * Opaque consumer registration handle returned by dpe_consumer_register().
 */
typedef struct dpe_consumer dpe_consumer_t;

/*
 * Result of a provider availability callback: whether the consumer wants
 * further notifications.
 */
typedef enum {
	/*
	 * Keep the registration active and invoke the callback again when
	 * another matching provider registers.
	 */
	DPE_CB_CONTINUE = 0,
	/*
	 * The consumer found what it needed so deliver no further
	 * notifications.  The registration remains valid and must still be
	 * released via dpe_consumer_unregister().
	 */
	DPE_CB_DONE,
} dpe_cb_res_t;

/*
 * Provider availability callback registered via dpe_consumer_register().
 */
typedef dpe_cb_res_t (*dpe_avail_cb_f)(void *arg);

/*
 * Provider-side API.  dpe_provider_register() must be called from the
 * backend driver's attach(9E) (it fails with EAGAIN otherwise) and
 * dpe_provider_unregister() from its detach(9E), or from attach(9E) when
 * cleaning up after a failure.
 *
 * The registration itself outlives the driver: unregistering at detach
 * only deactivates it, and the framework will revive (re-attach) the
 * provider on demand when a consumer opens a context against it.  The
 * registration is torn down for good only when the provider's device node
 * is removed from the system.  A driver re-attaching simply calls
 * dpe_provider_register() again, which reactivates the existing
 * registration (with the same descriptor contents) and returns the same
 * handle.
 *
 * dpe_provider_unregister() fails with EBUSY while a consumer's claim on
 * the provider is outstanding.  An open context holds the provider's
 * device node, preventing a detach from starting.  But an in-flight
 * dpe_open() claims the provider before taking that hold, so a detach
 * racing such an open fails with EBUSY and leaves the provider attached
 * for the open to complete against.
 */
extern int dpe_provider_register(const dpe_provider_t *desc,
    dpe_prov_t **provp);
extern int dpe_provider_unregister(dpe_prov_t *prov);

/*
 * Consumer-side API.  Only one context may be outstanding against a
 * given provider at a time.  dpe_open() returns an error if no registered
 * provider supports the requested profile or if one or more do but all
 * matching providers are currently in use.  If `provider_name` is
 * non-NULL, only a provider with a matching name will be considered.
 *
 * A matching provider whose driver has detached is transparently revived:
 * dpe_open() re-attaches the provider (and any detached ancestors) via its
 * device path and may therefore block while device configuration runs.
 * The returned context holds the provider's device node until dpe_close(),
 * preventing the provider driver from detaching underneath it.
 */
extern dpe_error_t dpe_open(dpe_profile_t profile, const char *provider_name,
    dpe_ctx_t **ctxp);
extern dpe_error_t dpe_close(dpe_ctx_t *ctx);

/*
 * Register interest in providers supporting the given profile.  `cb` is
 * invoked with `arg`, from taskq context with no framework locks held,
 * whenever such a provider may be available:
 *   - upon registration, if one is already present (the callback may be
 *     invoked even before dpe_consumer_register() returns!), and
 *   - when a matching provider registers.
 *
 * Notifications are level-triggered and coalesced: at most one callback
 * invocation is outstanding per consumer, and a burst of provider
 * registrations may be delivered as a single invocation.  The callback
 * should probe for what it wants -- typically by calling dpe_open() with
 * its desired profile and provider name -- and must tolerate failure: the
 * callback carries no provider identity, and a provider may have
 * unregistered or become busy by the time the callback runs.  Returning
 * DPE_CB_CONTINUE keeps the registration active so a later provider
 * registration triggers the callback again.  Once the consumer has what it
 * needs it should return DPE_CB_DONE to stop further notifications.
 *
 * Callback invocations are serialized per consumer: the callback is never
 * invoked concurrently with itself for the same registration.
 *
 * dpe_consumer_unregister() blocks until any in-flight callback invocation
 * has completed; calling it from the callback itself is a fatal error
 * (return DPE_CB_DONE instead).  Until it returns, a caller must assume the
 * callback may be invoked.  It must be called to release the registration
 * regardless of what the callback returned.
 */
extern int dpe_consumer_register(dpe_profile_t profile, dpe_avail_cb_f cb,
    void *arg, dpe_consumer_t **dconp);
extern void dpe_consumer_unregister(dpe_consumer_t *dcon);

extern dpe_error_t dpe_get_profile_info(dpe_ctx_t *ctx,
    dpe_profile_info_t *info);

extern dpe_error_t dpe_derive_context(dpe_ctx_t *ctx, const uint8_t *type,
    size_t type_size, const uint8_t *hash, size_t hash_size,
    const dpe_derive_opts_t *opts);
extern dpe_error_t dpe_certify_key(dpe_ctx_t *ctx, const uint8_t *label,
    size_t label_size, uint8_t *pubkey, size_t pubkey_size, uint8_t *cert,
    uint32_t *cert_size);
extern dpe_error_t dpe_sign(dpe_ctx_t *ctx, const uint8_t *label,
    size_t label_size, const uint8_t *data, size_t data_size, uint8_t *sig,
    size_t sig_size);
extern dpe_error_t dpe_get_cert_chain_size(dpe_ctx_t *ctx, uint32_t *sizep);
extern dpe_error_t dpe_get_cert_chain(dpe_ctx_t *ctx, uint32_t offset,
    uint32_t req_sz, uint8_t *out, uint32_t *got);
extern dpe_error_t dpe_destroy_context(dpe_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _IO_DPE_DPE_H */
