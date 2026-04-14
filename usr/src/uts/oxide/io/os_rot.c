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
 * Oxide OS RoT Driver
 *
 *
 * The AMD PSP RoT provides measurements of AMD firmware components as well as
 * the reset x86 image included in the boot ROM (aka our Phase 1).  We then make
 * use of the PSP's DICE Protection Environment (DPE) capabilities, via the DPE
 * framework, to further extend trust the chain by adding our Phase 2
 * measurement.
 *
 * The driver exposes an attestation API that allows consumers to:
 *   - Retrieve the attestation certificate chain.
 *   - Sign a caller-provided nonce using a derived attestation key stored in
 *     the PSP.
 *
 * Measurements included in leaf certificate:
 *  - Phase 2 boot image as measured on boot.  The boot_image module computes
 *    a SHA2-384 digest of the image as it is loaded (alongside the SHA2-256
 *    checksum used for image verification) and publishes it as a root node
 *    property. That digest is extended directly into the DPE context here.
 *
 * Measurements in non-leaf certificates:
 *  - The second-last certificate (Subject: DPE_ALIAS_*) includes the SHA-384
 *    measurement of the Phase 1 boot image (as included in the boot ROM).
 *  - The other intermediate certificates include measurements of various
 *    AMD firmware components.  See Table 5. "DPE Certificate Chain", pub 68086,
 *    Rev. 1.00.
 *
 * Attestation:
 *  - An attestation key is deterministically derived within the PSP and
 *    certified by chain rooted to a trusted AMD root CA.  The public half is
 *    returned as part of the leaf certificate.  An attestation then is a
 *    signature over the SHA2-384 digest of the caller-provided nonce.  A random
 *    nonce should be used to validate freshness.
 *
 * Appraising:
 *  - An appraiser may validate an attestation by extracting the public half of
 *    the attestation signing key from the leaf certificate and using it to
 *    verify the attestation (signature) with the nonce.
 *  - The certificate chain must also validated against a trusted AMD root
 *    (e.g., AMD Root CA R4).
 *  - With a trusted certificate chain and validated signature, the measurements
 *    themselves can be appraised.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/stddef.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/policy.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/boot_data.h>
#include <sys/platform_detect.h>
#include <sys/sha2.h>

#include <boot_image/oxide_boot.h>
#include <dpe/dpe.h>

#include "os_rot.h"

#define	OS_ROT_MINOR_NAME		"os_rot"
#define	OS_ROT_MINOR_NUM		0


/*
 * DPE label used to derive the attestation key.
 */
static const uint8_t OS_ROT_LABEL[4] = {'O', 'X', 'A', 'T'};

/*
 * Name of the DPE provider we rely on.  Providers advertising the same
 * profile are not necessarily interchangeable, so we only accept this one.
 */
#define	OS_ROT_DPE_PROVIDER		"psp_dpe"

/*
 * The DPE provider driver attaches asynchronously relative to us (e.g.
 * psp_dpe sits below the psp nexus, whose node is enumerated from a taskq
 * during boot), so we register with the DPE framework as a consumer of our
 * profile and complete initialization from its availability callback.
 * Until then ioctls fail with OS_ROT_E_NO_PROVIDER.
 */
typedef struct os_rot {
	/*
	 * Initialized on attach.
	 */
	dev_info_t			*osr_dip;
	dpe_consumer_t			*osr_dpe_consumer;
	uint8_t				osr_phase2_tag[OS_ROT_TYPE_SIZE];
	kmutex_t			osr_lock;

	/*
	 * Initialized once we're notified of a matching DPE provider & profile.
	 * See os_rot_dpe_avail().
	 */
	dpe_ctx_t			*osr_dpe_ctx;
	dpe_profile_info_t		osr_info;
} os_rot_t;

static os_rot_t os_rot_data;


/*
 * Returns the DPE context if initialization has completed or NULL if no
 * suitable provider has been matched yet.
 */
static dpe_ctx_t *
os_rot_dpe_ctx(os_rot_t *osr)
{
	dpe_ctx_t *ctx;

	mutex_enter(&osr->osr_lock);
	ctx = osr->osr_dpe_ctx;
	mutex_exit(&osr->osr_lock);

	return (ctx);
}

/*
 * The DPE profile we use expresses measurements as SHA2-384 digests, and the
 * boot_image module publishes a SHA2-384 measurement of the phase 2 image.
 */
CTASSERT(OXBOOT_CSUMLEN_SHA384 == OS_ROT_HASH_SIZE);

/*
 * Read the phase 2 boot image measurement from the root devinfo node, extend
 * the DPE context with it, then establish the attestation key / leaf cert
 * for the resulting context.
 */
static bool
os_rot_measure_phase2(os_rot_t *osr)
{
	uint8_t *hash = NULL;
	uint_t hash_len = 0;
	dpe_error_t dpe_err;
	int ret;

	ASSERT(MUTEX_HELD(&osr->osr_lock));

	ret = ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, OXBOOT_DEVPROP_IMAGE_SHA384,
	    &hash, &hash_len);
	if (ret != DDI_PROP_SUCCESS) {
		dev_err(osr->osr_dip, CE_WARN, "!failed to read phase 2 "
		    "measurement from root node property %s: %d",
		    OXBOOT_DEVPROP_IMAGE_SHA384, ret);
		return (false);
	}

	if (hash_len != OS_ROT_HASH_SIZE) {
		dev_err(osr->osr_dip, CE_WARN, "!unexpected measurement "
		    "length %u (expected %u)", hash_len, OS_ROT_HASH_SIZE);
		ddi_prop_free(hash);
		return (false);
	}

	dpe_err = dpe_derive_context(osr->osr_dpe_ctx, osr->osr_phase2_tag,
	    sizeof (osr->osr_phase2_tag), hash, hash_len, NULL);
	ddi_prop_free(hash);
	if (dpe_err.dpe_error != DPE_E_OK) {
		dev_err(osr->osr_dip, CE_WARN, "!failed to extend DPE context "
		    "with phase 2 measurement: %d (provider: %d)",
		    dpe_err.dpe_error, dpe_err.dpe_prov);
		return (false);
	}

	/*
	 * Generate the attestation key pair and certificate for the current
	 * DPE context.  The key is derived deterministically from the label
	 * and context state, so CertifyKey and Sign with the same label will
	 * use the same key.  The returned leaf certificate is included in the
	 * DPE cert chain subsequently returned by GetCertChain so we don't
	 * actually care to save the cert or public key.
	 */
	dpe_err = dpe_certify_key(osr->osr_dpe_ctx, OS_ROT_LABEL,
	    sizeof (OS_ROT_LABEL), NULL, 0, NULL, NULL);
	if (dpe_err.dpe_error != DPE_E_OK) {
		dev_err(osr->osr_dip, CE_WARN, "!failed to derive attestation "
		    "key and certificate: %d (provider: %d)",
		    dpe_err.dpe_error, dpe_err.dpe_prov);
		return (false);
	}

	return (true);
}

/*
 * DPE provider availability callback, invoked from taskq context by the DPE
 * framework whenever a provider supporting our profile may be available.
 * Notifications are level-triggered and carry no provider identity, so we
 * probe by opening the provider we want by name: acquire a context,
 * validate the profile parameters, and extend the context with the phase 2
 * measurement, returning DPE_CB_DONE to stop further notifications.  On any
 * failure we simply remain without a DPE context: ioctls keep returning
 * `OS_ROT_E_NO_PROVIDER` and a subsequent provider registration will invoke us
 * again.
 */
static dpe_cb_res_t
os_rot_dpe_avail(void *arg)
{
	os_rot_t *osr = arg;
	dpe_profile_info_t info;
	dpe_error_t dpe_err;

	mutex_enter(&osr->osr_lock);

	if (osr->osr_dpe_ctx != NULL) {
		mutex_exit(&osr->osr_lock);
		return (DPE_CB_DONE);
	}

	dpe_err = dpe_open(DPE_PROFILE_IROT_P384_SHA384, OS_ROT_DPE_PROVIDER,
	    &osr->osr_dpe_ctx);
	if (dpe_err.dpe_error != DPE_E_OK) {
		dev_err(osr->osr_dip, CE_WARN, "!dpe_open failed: %d "
		    "(provider: %d)", dpe_err.dpe_error, dpe_err.dpe_prov);
		goto fail;
	}

	dpe_err = dpe_get_profile_info(osr->osr_dpe_ctx, &info);
	if (dpe_err.dpe_error != DPE_E_OK) {
		dev_err(osr->osr_dip, CE_WARN, "!dpe_get_profile_info "
		    "failed: %d (provider: %d)", dpe_err.dpe_error,
		    dpe_err.dpe_prov);
		goto fail_dpe;
	}

	if ((sizeof (OS_ROT_LABEL) != info.dpi_label_size) ||
	    (OS_ROT_HASH_SIZE != info.dpi_hash_size) ||
	    (OS_ROT_SIG_SIZE != info.dpi_sig_size) ||
	    (OS_ROT_TYPE_SIZE != info.dpi_type_size)) {
		dev_err(osr->osr_dip, CE_WARN, "!unexpected profile param "
		    "sizes");
		goto fail_dpe;
	}

	/*
	 * Extend the DPE context with the phase 2 boot image measurement and
	 * establish the attestation leaf key/cert.
	 */
	if (!os_rot_measure_phase2(osr))
		goto fail_dpe;

	osr->osr_info = info;
	mutex_exit(&osr->osr_lock);

	dev_err(osr->osr_dip, CE_CONT, "?found DPE provider: %s\n",
	    OS_ROT_DPE_PROVIDER);
	return (DPE_CB_DONE);

fail_dpe:
	(void) dpe_close(osr->osr_dpe_ctx);
	osr->osr_dpe_ctx = NULL;
fail:
	mutex_exit(&osr->osr_lock);
	return (DPE_CB_CONTINUE);
}

static int
os_rot_ioctl_get_certs(os_rot_t *osr, intptr_t arg, int mode)
{
	os_rot_certs_t hdr;
	uint8_t *chain = NULL;
	uint32_t chain_size = 0;
	uint32_t have = 0;
	dpe_ctx_t *ctx;
	dpe_error_t dpe_err;
	int ret = 0;

	if (ddi_copyin((void *)arg, &hdr, sizeof (hdr), mode & FKIOCTL) != 0)
		return (EFAULT);

	hdr.osrc_error = OS_ROT_E_OK;

	if ((ctx = os_rot_dpe_ctx(osr)) == NULL) {
		hdr.osrc_error = OS_ROT_E_NO_PROVIDER;
		if (ddi_copyout(&hdr, (void *)arg, sizeof (hdr),
		    mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		return (0);
	}

	dpe_err = dpe_get_cert_chain_size(ctx, &chain_size);
	if (dpe_err.dpe_error != DPE_E_OK) {
		dev_err(osr->osr_dip, CE_WARN, "!failed to get DPE certificate "
		    "chain size: %d (provider: %d)",
		    dpe_err.dpe_error, dpe_err.dpe_prov);
		hdr.osrc_error = OS_ROT_E_DPE;
		if (ddi_copyout(&hdr, (void *)arg, sizeof (hdr),
		    mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		return (0);
	}

	/*
	 * If the caller passed 0, just return the required size.
	 */
	if (hdr.osrc_chain_size == 0) {
		hdr.osrc_chain_size = chain_size;
		if (ddi_copyout(&hdr, (void *)arg, sizeof (hdr),
		    mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		return (0);
	}

	if (hdr.osrc_chain_size < chain_size) {
		hdr.osrc_error = OS_ROT_E_SIZE;
		hdr.osrc_chain_size = chain_size;
		if (ddi_copyout(&hdr, (void *)arg, sizeof (hdr),
		    mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		return (0);
	}

	chain = kmem_zalloc(chain_size, KM_SLEEP);

	while (have < chain_size) {
		uint32_t got = 0;

		dpe_err = dpe_get_cert_chain(ctx, have,
		    chain_size - have, chain + have, &got);
		if (dpe_err.dpe_error != DPE_E_OK) {
			dev_err(osr->osr_dip, CE_WARN, "!failed to get DPE "
			    "certificate chain (off: %u req_sz: %u): %d "
			    "(provider: %d)", have, chain_size - have,
			    dpe_err.dpe_error, dpe_err.dpe_prov);
			hdr.osrc_error = OS_ROT_E_DPE;
			if (ddi_copyout(&hdr, (void *)arg, sizeof (hdr),
			    mode & FKIOCTL) != 0) {
				ret = EFAULT;
			}
			goto out;
		}
		if (got == 0) {
			ret = EIO;
			goto out;
		}
		have += got;
	}

	hdr.osrc_chain_size = chain_size;
	if (ddi_copyout(&hdr, (void *)arg, sizeof (hdr),
	    mode & FKIOCTL) != 0) {
		ret = EFAULT;
		goto out;
	}

	intptr_t data_arg = arg + offsetof(os_rot_certs_t, osrc_chain);
	if (ddi_copyout(chain, (void *)data_arg, chain_size,
	    mode & FKIOCTL) != 0) {
		ret = EFAULT;
	}

out:
	kmem_free(chain, chain_size);
	return (ret);
}

static int
os_rot_ioctl_attest(os_rot_t *osr, intptr_t arg, int mode)
{
	os_rot_attest_t req;
	dpe_ctx_t *ctx;
	dpe_error_t dpe_err;
	SHA384_CTX sha;
	uint8_t digest[OS_ROT_HASH_SIZE];

	if (ddi_copyin((void *)arg, &req, sizeof (req), mode & FKIOCTL) != 0)
		return (EFAULT);

	req.osra_error = OS_ROT_E_OK;

	if ((ctx = os_rot_dpe_ctx(osr)) == NULL) {
		req.osra_error = OS_ROT_E_NO_PROVIDER;
		if (ddi_copyout(&req, (void *)arg, sizeof (req),
		    mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		return (0);
	}

	/*
	 * The attestation is a signature over the SHA2-384 digest of the
	 * concatenation of the caller-provided nonce and the current set of
	 * measurements as recorded in our local measurement log. But we don't
	 * currently maintain any additional measurements so it's just the nonce
	 * for now.
	 */
	SHA2Init(SHA384, &sha);
	SHA2Update(&sha, req.osra_nonce, sizeof (req.osra_nonce));
	SHA2Final(digest, &sha);

	/*
	 * Sign the digest using the DPE-derived attestation key.
	 */
	dpe_err = dpe_sign(ctx, OS_ROT_LABEL, sizeof (OS_ROT_LABEL), digest,
	    sizeof (digest), req.osra_sig, OS_ROT_SIG_SIZE);
	if (dpe_err.dpe_error != DPE_E_OK) {
		dev_err(osr->osr_dip, CE_WARN, "!failed to sign digest: %d "
		    "(provider: %d)", dpe_err.dpe_error, dpe_err.dpe_prov);
		req.osra_error = OS_ROT_E_DPE;
	}

	if (ddi_copyout(&req, (void *)arg, sizeof (req), mode & FKIOCTL) != 0)
		return (EFAULT);

	return (0);
}

static int
os_rot_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	os_rot_t *osr = &os_rot_data;

	if (crgetzoneid(credp) != GLOBAL_ZONEID)
		return (EPERM);

	switch (cmd) {
	case OS_ROT_IOC_GET_CERTS:
		return (os_rot_ioctl_get_certs(osr, arg, mode));
	case OS_ROT_IOC_ATTEST:
		return (os_rot_ioctl_attest(osr, arg, mode));
	default:
		return (ENOTTY);
	}
}

static int
os_rot_open(dev_t *devp, int flags, int otype, cred_t *credp)
{
	if (crgetzoneid(credp) != GLOBAL_ZONEID ||
	    secpolicy_sys_config(credp, B_FALSE) != 0) {
		return (EPERM);
	}

	if (getminor(*devp) != OS_ROT_MINOR_NUM)
		return (ENXIO);

	if ((flags & (FNDELAY | FNONBLOCK | FEXCL)) != 0)
		return (EINVAL);

	if (otype != OTYP_CHR)
		return (EINVAL);

	return (0);
}

/* ARGSUSED */
static int
os_rot_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	if (getminor(dev) != OS_ROT_MINOR_NUM)
		return (ENXIO);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	return (0);
}

static int
os_rot_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	os_rot_t *osr = &os_rot_data;
	char *bootdev = NULL;
	bool is_ramdisk;
	int ret;

	if (cmd == DDI_RESUME)
		return (DDI_SUCCESS);
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (osr->osr_dip != NULL) {
		dev_err(dip, CE_WARN, "!os_rot already attached");
		return (DDI_FAILURE);
	}

	osr->osr_dip = dip;
	mutex_init(&osr->osr_lock, NULL, MUTEX_DRIVER, NULL);

	/*
	 * If we booted from a ramdisk then we don't have a real phase 2 image.
	 * But we can still record the checksum of the ramdisk used as the root
	 * filesystem itself (using a separate tag).
	 */
	if ((ret = ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, BTPROP_NAME_BOOT_SOURCE, &bootdev)) !=
	    DDI_SUCCESS || bootdev == NULL) {
		dev_err(dip, CE_WARN, "!failed to read boot source: %d", ret);
		goto fail;
	}
	is_ramdisk = strcmp(bootdev, "ramdisk") == 0;
	ddi_prop_free(bootdev);
	bootdev = NULL;

	bcopy(is_ramdisk ? OS_ROT_TYPE_OXRD : OS_ROT_TYPE_OXP2,
	    osr->osr_phase2_tag, OS_ROT_TYPE_SIZE);

	if (ddi_create_minor_node(dip, OS_ROT_MINOR_NAME, S_IFCHR,
	    OS_ROT_MINOR_NUM, DDI_PSEUDO, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!failed to create minor node");
		goto fail;
	}

	/*
	 * Register interest in DPE providers supporting our profile.  The
	 * framework invokes os_rot_dpe_avail() as they become available;
	 * once OS_ROT_DPE_PROVIDER shows up, the callback completes the DPE
	 * initialization.  Until then ioctls fail with
	 * OS_ROT_E_NO_PROVIDER.  Passing OS_ROT_DPE_PROVIDER as the hint
	 * lets the framework try to load and attach the provider driver if
	 * nothing else will (e.g. we're being reloaded on an open of
	 * /dev/os_rot after both drivers were explicitly unloaded).
	 */
	ret = dpe_consumer_register(DPE_PROFILE_IROT_P384_SHA384,
	    OS_ROT_DPE_PROVIDER, os_rot_dpe_avail, osr,
	    &osr->osr_dpe_consumer);
	if (ret != 0) {
		dev_err(dip, CE_WARN, "!failed to register as a DPE "
		    "consumer: %d", ret);
		ddi_remove_minor_node(dip, NULL);
		goto fail;
	}

	ddi_report_dev(dip);
	return (DDI_SUCCESS);

fail:
	mutex_destroy(&osr->osr_lock);
	osr->osr_dip = NULL;
	return (DDI_FAILURE);
}

static int
os_rot_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	os_rot_t *osr = &os_rot_data;

	if (cmd == DDI_SUSPEND)
		return (DDI_SUCCESS);
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	(void) ddi_remove_minor_node(dip, NULL);

	/*
	 * Unregistering blocks until any in-flight availability callback has
	 * completed, after which nothing else can touch `osr_dpe_ctx`.
	 */
	if (osr->osr_dpe_consumer != NULL) {
		dpe_consumer_unregister(osr->osr_dpe_consumer);
		osr->osr_dpe_consumer = NULL;
	}

	if (osr->osr_dpe_ctx != NULL) {
		(void) dpe_close(osr->osr_dpe_ctx);
		osr->osr_dpe_ctx = NULL;
	}

	mutex_destroy(&osr->osr_lock);
	osr->osr_dip = NULL;

	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
os_rot_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
    void **resultp)
{
	os_rot_t *osr = &os_rot_data;
	minor_t minor;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		minor = getminor((dev_t)arg);
		if (minor != OS_ROT_MINOR_NUM)
			return (DDI_FAILURE);
		*resultp = (void *)osr->osr_dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		minor = getminor((dev_t)arg);
		if (minor != OS_ROT_MINOR_NUM)
			return (DDI_FAILURE);
		*resultp = (void *)(uintptr_t)ddi_get_instance(osr->osr_dip);
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static struct cb_ops os_rot_cb_ops = {
	.cb_open = os_rot_open,
	.cb_close = os_rot_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = os_rot_ioctl,
	.cb_devmap = nodev,
	.cb_mmap = nodev,
	.cb_segmap = nodev,
	.cb_chpoll = nochpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_flag = D_MP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops os_rot_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = os_rot_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = os_rot_attach,
	.devo_detach = os_rot_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_needed,
	.devo_cb_ops = &os_rot_cb_ops
};

static struct modldrv os_rot_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Oxide OS RoT Driver",
	.drv_dev_ops = &os_rot_dev_ops
};

static struct modlinkage os_rot_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &os_rot_modldrv, NULL }
};

int
_init(void)
{
	ASSERT3P(oxide_board_data, !=, NULL);
	if (!oxide_board_data->obd_measure_root) {
		cmn_err(CE_WARN, "!phase 2 root filesystem measurement is "
		    "disabled on this platform");
		return (ENOTSUP);
	}

	return (mod_install(&os_rot_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&os_rot_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&os_rot_modlinkage));
}
