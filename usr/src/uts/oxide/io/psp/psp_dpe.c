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
 * AMD PSP-backed provider for the kernel DICE Protection Environment (DPE)
 * framework.  This driver makes use of the DPE exposed by the AMD RoT within
 * the PSP via the C2P mailbox and registers those capabilities with the
 * in-kernel DPE framework to be used via the DPE consumer APIs.
 */

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/stdbit.h>
#include <sys/stdbool.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/cpuvar.h>
#include <sys/machsystm.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/psp.h>
#include <amdzen_client.h>

#include <dpe/dpe.h>

#include "psp_client.h"

typedef struct psp_dpe {
	dev_info_t		*pd_dip;
	psp_dpe_profile_t	pd_profile;
	dpe_prov_t		*pd_dpe_prov;
} psp_dpe_t;

static psp_dpe_t psp_dpe_data;

/*
 * Helper routine to set the common request fields, submit the DPE command via
 * the C2P mailbox, and validate the response.
 */
static dpe_prov_error_t
psp_dpe_run_cmd(psp_dpe_t *pd, c2p_mbox_dpe_buffer_t *dpe)
{
	int ret;
	uint32_t size;
	psp_dpe_resp_hdr_t *resp;

#define	SIZEOF_CMD_BUFFER(f) \
	(offsetof(c2p_mbox_dpe_buffer_t, c2pmdb_ ## f) + \
	sizeof (((c2p_mbox_dpe_buffer_t *)0)->c2pmdb_ ## f))

	/*
	 * Set the correct request size and stash the command specific response
	 * header pointer.
	 */
	switch (dpe->c2pmdb_cmd) {
	case PSP_DPE_CMD_GET_PROFILE:
		resp = &dpe->c2pmdb_get_profile.resp;
		size = SIZEOF_CMD_BUFFER(get_profile);
		break;
	case PSP_DPE_CMD_DERIVE_CHILD:
		resp = &dpe->c2pmdb_derive_child_irot384.resp;
		size = SIZEOF_CMD_BUFFER(derive_child_irot384);
		break;
	case PSP_DPE_CMD_CERTIFY_KEY:
		resp = &dpe->c2pmdb_certify_key_irot384.resp.pdck_resp_hdr;
		size = SIZEOF_CMD_BUFFER(certify_key_irot384);
		break;
	case PSP_DPE_CMD_SIGN:
		resp = &dpe->c2pmdb_sign_irot384.resp.pds_resp_hdr;
		size = SIZEOF_CMD_BUFFER(sign_irot384);
		break;
	case PSP_DPE_CMD_DESTROY_CONTEXT:
		resp = &dpe->c2pmdb_destroy_context.resp;
		size = SIZEOF_CMD_BUFFER(destroy_context);
		break;
	case PSP_DPE_CMD_GET_CERT_CHAIN:
		resp = &dpe->c2pmdb_get_cert_chain.resp.pdgcc_resp_hdr;
		size = SIZEOF_CMD_BUFFER(get_cert_chain);
		break;
	default:
		dev_err(pd->pd_dip, CE_WARN, "!unknown DPE command (0x%x)",
		    dpe->c2pmdb_cmd);
		return (DPE_PROV_E_UNSUP_CMD);
	}
#undef	SIZEOF_CMD_BUFFER

	dpe->c2pmdb_hdr.c2pmb_size = size;
	dpe->c2pmdb_magic = PSP_DPE_CMD_MAGIC;

	ret = psp_c_c2pmbox_cmd(C2P_MBOX_CMD_DPE_INTERFACE, &dpe->c2pmdb_hdr);
	if (ret != 0 || dpe->c2pmdb_hdr.c2pmb_status != 0) {
		dev_err(pd->pd_dip, CE_WARN, "!failed to submit DPE command "
		    "(0x%x) via C2P mailbox: %d (status = %u)", dpe->c2pmdb_cmd,
		    ret, dpe->c2pmdb_hdr.c2pmb_status);
		switch (ret) {
		case EINVAL:
			return (DPE_PROV_E_BAD_REQ);
		case ETIMEDOUT:
			return (DPE_PROV_E_TIMEDOUT);
		default:
			return (DPE_PROV_E_INTERNAL);
		}
	}

	if (resp->pdr_magic != PSP_DPE_RESP_MAGIC) {
		dev_err(pd->pd_dip, CE_WARN, "!invalid DPE command (0x%x) "
		    "response magic: 0x%08x", dpe->c2pmdb_cmd, resp->pdr_magic);
		return (DPE_PROV_E_BAD_RESP);
	}

	if (resp->pdr_status != 0) {
		dev_err(pd->pd_dip, CE_WARN, "!DPE command (0x%x) failed: "
		    "status = %u", dpe->c2pmdb_cmd, resp->pdr_status);
		return (DPE_PROV_E_INTERNAL);
	}

	return (DPE_PROV_E_OK);
}

static dpe_prov_error_t
psp_dpe_get_profile(psp_dpe_t *pd, uint32_t *profile)
{
	c2p_mbox_dpe_buffer_t buf;
	dpe_prov_error_t err;

	bzero(&buf, sizeof (c2p_mbox_dpe_buffer_t));

	/*
	 * GetProfile doesn't take any additional arguments.
	 */
	buf.c2pmdb_cmd = PSP_DPE_CMD_GET_PROFILE;

	if ((err = psp_dpe_run_cmd(pd, &buf)) != DPE_PROV_E_OK)
		return (err);

	*profile = buf.c2pmdb_get_profile.resp.pdr_profile;

	return (DPE_PROV_E_OK);
}

/*
 * DPE framework provider ops.  The framework guarantees type/hash/label/data
 * buffers are sized according to the profile info we publish below, so
 * fixed-size bcopy() into the C2P mailbox request structs is safe.
 */
static dpe_prov_error_t
psp_dpe_provop_derive_context(void *priv, const uint8_t *type,
    const uint8_t *hash, const dpe_derive_opts_t *opts)
{
	psp_dpe_t *pd = priv;
	c2p_mbox_dpe_buffer_t buf;
	psp_dpe_derive_child_irot384_req_t *req;

	VERIFY3U(pd->pd_profile, ==, PSP_DPE_PROFILE_IROT_P384_SHA384);

	bzero(&buf, sizeof (buf));
	buf.c2pmdb_cmd = PSP_DPE_CMD_DERIVE_CHILD;
	buf.c2pmdb_profile = pd->pd_profile;

	req = &buf.c2pmdb_derive_child_irot384.req;
	bcopy(type, req->pddc_type, sizeof (req->pddc_type));
	bcopy(hash, req->pddc_hash, sizeof (req->pddc_hash));
	req->pddc_include_info = (opts->ddo_flags & DDCF_INCLUDE_INFO) != 0;
	req->pddc_include_dice = (opts->ddo_flags & DDCF_INCLUDE_DICE) != 0;

	return (psp_dpe_run_cmd(pd, &buf));
}

static dpe_prov_error_t
psp_dpe_provop_certify_key(void *priv, const uint8_t *label, uint8_t *pubkey,
    uint8_t *cert, uint32_t *cert_size)
{
	psp_dpe_t *pd = priv;
	c2p_mbox_dpe_buffer_t buf;
	psp_dpe_certify_key_irot384_req_t *req;
	psp_dpe_certify_key_irot384_resp_t *resp;
	dpe_prov_error_t err = DPE_PROV_E_OK;

	VERIFY3U(pd->pd_profile, ==, PSP_DPE_PROFILE_IROT_P384_SHA384);

	bzero(&buf, sizeof (buf));
	buf.c2pmdb_cmd = PSP_DPE_CMD_CERTIFY_KEY;
	buf.c2pmdb_profile = pd->pd_profile;

	req = &buf.c2pmdb_certify_key_irot384.req;
	bcopy(label, req->pdck_label, sizeof (req->pdck_label));

	if ((err = psp_dpe_run_cmd(pd, &buf)) != DPE_PROV_E_OK)
		return (err);

	resp = &buf.c2pmdb_certify_key_irot384.resp;

	if (resp->pdck_cert_size > sizeof (resp->pdck_cert)) {
		dev_err(pd->pd_dip, CE_WARN, "!DPE CertifyKey returned "
		    "oversized certificate: 0x%x (max 0x%zx)",
		    resp->pdck_cert_size, sizeof (resp->pdck_cert));
		return (DPE_PROV_E_BAD_RESP);
	}

	if (pubkey != NULL)
		bcopy(resp->pdck_pubkey, pubkey, sizeof (resp->pdck_pubkey));
	if (cert != NULL) {
		bcopy(resp->pdck_cert, cert, resp->pdck_cert_size);
		*cert_size = resp->pdck_cert_size;
	}

	return (err);
}

static dpe_prov_error_t
psp_dpe_provop_sign(void *priv, const uint8_t *label, const uint8_t *data,
    uint8_t *sig)
{
	psp_dpe_t *pd = priv;
	c2p_mbox_dpe_buffer_t buf;
	psp_dpe_sign_irot384_req_t *req;
	psp_dpe_sign_irot384_resp_t *resp;
	dpe_prov_error_t err = DPE_PROV_E_OK;

	VERIFY3U(pd->pd_profile, ==, PSP_DPE_PROFILE_IROT_P384_SHA384);

	bzero(&buf, sizeof (buf));
	buf.c2pmdb_cmd = PSP_DPE_CMD_SIGN;
	buf.c2pmdb_profile = pd->pd_profile;

	req = &buf.c2pmdb_sign_irot384.req;
	bcopy(label, req->pds_label, sizeof (req->pds_label));
	bcopy(data, req->pds_data, sizeof (req->pds_data));

	if ((err = psp_dpe_run_cmd(pd, &buf)) == DPE_PROV_E_OK) {
		resp = &buf.c2pmdb_sign_irot384.resp;
		bcopy(resp->pds_sig, sig, sizeof (resp->pds_sig));
	}

	return (err);
}

static dpe_prov_error_t
psp_dpe_provop_get_cert_chain_size(void *priv, uint32_t *sizep)
{
	psp_dpe_t *pd = priv;
	c2p_mbox_dpe_buffer_t buf;
	psp_dpe_get_cert_chain_req_t *req;
	psp_dpe_get_cert_chain_resp_t *resp;
	dpe_prov_error_t err = DPE_PROV_E_OK;
	uint32_t size = 0;

	VERIFY3U(pd->pd_profile, ==, PSP_DPE_PROFILE_IROT_P384_SHA384);

	bzero(&buf, sizeof (buf));
	buf.c2pmdb_cmd = PSP_DPE_CMD_GET_CERT_CHAIN;
	buf.c2pmdb_profile = pd->pd_profile;

	req = &buf.c2pmdb_get_cert_chain.req;
	resp = &buf.c2pmdb_get_cert_chain.resp;

	/*
	 * The PSP does not expose the chain size directly; walk it in
	 * max-sized windows until a short response terminates the chain.
	 */
	req->pdgcc_size = PSP_DPE_IROT_P384_MAX_CERT_SIZE;
	do {
		req->pdgcc_offset = size;

		if ((err = psp_dpe_run_cmd(pd, &buf)) != DPE_PROV_E_OK) {
			dev_err(pd->pd_dip, CE_WARN, "!failed to get DPE cert "
			    "chain size");
			return (err);
		}
		if (resp->pdgcc_chain_size > PSP_DPE_IROT_P384_MAX_CERT_SIZE) {
			dev_err(pd->pd_dip, CE_WARN, "!DPE GetCertChain "
			    "returned oversized chunk: 0x%x (max 0x%x)",
			    resp->pdgcc_chain_size,
			    PSP_DPE_IROT_P384_MAX_CERT_SIZE);
			return (DPE_PROV_E_BAD_RESP);
		}
		size += resp->pdgcc_chain_size;
		if (resp->pdgcc_chain_size < PSP_DPE_IROT_P384_MAX_CERT_SIZE)
			break;
	} while (resp->pdgcc_chain_size > 0);

	*sizep = size;
	return (err);
}

static dpe_prov_error_t
psp_dpe_provop_get_cert_chain(void *priv, uint32_t offset, uint32_t req_sz,
    uint8_t *out, uint32_t *got)
{
	psp_dpe_t *pd = priv;
	c2p_mbox_dpe_buffer_t buf;
	psp_dpe_get_cert_chain_req_t *req;
	psp_dpe_get_cert_chain_resp_t *resp;
	dpe_prov_error_t err = DPE_PROV_E_OK;

	VERIFY3U(pd->pd_profile, ==, PSP_DPE_PROFILE_IROT_P384_SHA384);

	bzero(&buf, sizeof (buf));
	buf.c2pmdb_cmd = PSP_DPE_CMD_GET_CERT_CHAIN;
	buf.c2pmdb_profile = pd->pd_profile;

	req = &buf.c2pmdb_get_cert_chain.req;
	resp = &buf.c2pmdb_get_cert_chain.resp;

	req->pdgcc_offset = offset;
	req->pdgcc_size = MIN(req_sz, PSP_DPE_IROT_P384_MAX_CERT_SIZE);

	if ((err = psp_dpe_run_cmd(pd, &buf)) != DPE_PROV_E_OK) {
		dev_err(pd->pd_dip, CE_WARN, "!failed to get DPE cert chain");
		return (err);
	}

	if (resp->pdgcc_chain_size > PSP_DPE_IROT_P384_MAX_CERT_SIZE) {
		dev_err(pd->pd_dip, CE_WARN, "!DPE GetCertChain returned "
		    "oversized chunk: 0x%x (max 0x%x)", resp->pdgcc_chain_size,
		    PSP_DPE_IROT_P384_MAX_CERT_SIZE);
		return (DPE_PROV_E_BAD_RESP);
	}

	bcopy(resp->pdgcc_chain, out, MIN(resp->pdgcc_chain_size, req_sz));
	*got = resp->pdgcc_chain_size;
	return (err);
}

static dpe_prov_error_t
psp_dpe_provop_destroy_context(void *priv)
{
	psp_dpe_t *pd = priv;
	c2p_mbox_dpe_buffer_t buf;

	bzero(&buf, sizeof (buf));
	buf.c2pmdb_cmd = PSP_DPE_CMD_DESTROY_CONTEXT;
	buf.c2pmdb_profile = pd->pd_profile;

	return (psp_dpe_run_cmd(pd, &buf));
}

static const dpe_profile_info_t psp_dpe_profiles[] = {
	{
		.dpi_profile = DPE_PROFILE_IROT_P384_SHA384,
		.dpi_name = "tcg.profile.irot.p384",
		.dpi_hash_size = PSP_DPE_IROT_P384_HASH_SIZE,
		.dpi_key_size = PSP_DPE_IROT_P384_KEY_SIZE,
		.dpi_sig_size = PSP_DPE_IROT_P384_SIG_SIZE,
		.dpi_type_size = PSP_DPE_IROT_P384_TYPE_SIZE,
		.dpi_label_size = PSP_DPE_IROT_P384_LABEL_SIZE,
		.dpi_max_cert_size = PSP_DPE_IROT_P384_MAX_CERT_SIZE,
	},
};

static const dpe_ops_t psp_dpe_ops = {
	.dop_derive_context = psp_dpe_provop_derive_context,
	.dop_certify_key = psp_dpe_provop_certify_key,
	.dop_sign = psp_dpe_provop_sign,
	.dop_get_cert_chain_size = psp_dpe_provop_get_cert_chain_size,
	.dop_get_cert_chain = psp_dpe_provop_get_cert_chain,
	.dop_destroy_context = psp_dpe_provop_destroy_context,
};

static int
psp_dpe_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	psp_dpe_t *pd = &psp_dpe_data;
	uint32_t profile = PSP_DPE_PROFILE_INVALID;
	dpe_provider_t prov_desc = {
		.dpp_name = "psp_dpe",
		.dpp_profiles = psp_dpe_profiles,
		.dpp_nprofiles = ARRAY_SIZE(psp_dpe_profiles),
		.dpp_ops = &psp_dpe_ops,
	};
	int ret;

	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if (pd->pd_dip != NULL) {
		dev_err(dip, CE_WARN, "!psp_dpe is already attached to a "
		    "dev_info_t: %p", pd->pd_dip);
		return (DDI_FAILURE);
	}

	pd->pd_dip = dip;

	if (psp_dpe_get_profile(pd, &profile) != DPE_PROV_E_OK) {
		dev_err(dip, CE_WARN, "!failed to retrieve DPE profile");
		goto fail;
	}

	switch (profile) {
	case PSP_DPE_PROFILE_IROT_P384_SHA384:
		break;
	default:
		dev_err(dip, CE_WARN, "!unsupported DPE profile: 0x%x",
		    profile);
		goto fail;
	}
	pd->pd_profile = (psp_dpe_profile_t)profile;

	/*
	 * Destroy any existing context to start fresh.
	 */
	(void) psp_dpe_provop_destroy_context(pd);

	prov_desc.dpp_private = pd;
	if ((ret = dpe_provider_register(&prov_desc, &pd->pd_dpe_prov)) != 0) {
		dev_err(dip, CE_WARN, "!failed to register DPE provider: %d",
		    ret);
		goto fail;
	}

	ddi_report_dev(dip);
	return (DDI_SUCCESS);

fail:
	pd->pd_profile = PSP_DPE_PROFILE_INVALID;
	pd->pd_dip = NULL;
	return (DDI_FAILURE);
}

static int
psp_dpe_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	psp_dpe_t *pd = &psp_dpe_data;
	int ret;

	if (cmd == DDI_SUSPEND) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	if (pd->pd_dip != dip) {
		dev_err(dip, CE_WARN, "!attempt to detach with wrong dip");
		return (DDI_FAILURE);
	}

	/*
	 * Unregister from the DPE framework first: this fails with EBUSY if
	 * any consumer still holds a context against us, in which case we must
	 * not proceed.
	 */
	ret = dpe_provider_unregister(pd->pd_dpe_prov);
	if (ret == EBUSY) {
		return (DDI_FAILURE);
	} else if (ret != 0) {
		dev_err(dip, CE_WARN, "!failed to unregister DPE provider: %d",
		    ret);
		return (DDI_FAILURE);
	}
	pd->pd_dpe_prov = NULL;

	/*
	 * Destroy the context on detach.
	 */
	(void) psp_dpe_provop_destroy_context(pd);

	pd->pd_profile = PSP_DPE_PROFILE_INVALID;
	pd->pd_dip = NULL;

	return (DDI_SUCCESS);
}

static struct dev_ops psp_dpe_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = nulldev,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = psp_dpe_attach,
	.devo_detach = psp_dpe_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_needed,
};

static struct modldrv psp_dpe_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "AMD PSP DPE Driver",
	.drv_dev_ops = &psp_dpe_dev_ops
};

static struct modlinkage psp_dpe_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &psp_dpe_modldrv, NULL }
};

int
_init(void)
{
	switch (chiprev_family(cpuid_getchiprev(CPU))) {
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		break;
	default:
		cmn_err(CE_WARN, "!psp_dpe: unsupported processor family");
		return (ENOTSUP);
	}

	return (mod_install(&psp_dpe_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&psp_dpe_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&psp_dpe_modlinkage));
}
