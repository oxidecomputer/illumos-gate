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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2023 Jason King
 */

/*
 * KCF Provider for a TPM device. Currently only the RNG function of a TPM
 * is exposed to KCF. Historically, TPM1.2 KCF RNG support was only ever
 * built with special compilation flags (that were never used in illumos).
 * As such, we currently only register TPM2.0 devices with KCF.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/spi.h>

#include "tpm_ddi.h"

/*
 * CSPI information (entry points, provider info, etc.)
 */
static void tpmrng_provider_status(crypto_provider_handle_t, uint_t *);

static crypto_control_ops_t tpmrng_control_ops = {
	.provider_status = tpmrng_provider_status,
};

static int tpmrng_seed_random(crypto_provider_handle_t, crypto_session_id_t,
    uchar_t *, size_t, uint_t, uint32_t, crypto_req_handle_t);

static int tpmrng_generate_random(crypto_provider_handle_t,
    crypto_session_id_t, uchar_t *, size_t, crypto_req_handle_t);

static crypto_random_number_ops_t tpmrng_random_number_ops = {
	.seed_random =		tpmrng_seed_random,
	.generate_random =	tpmrng_generate_random,
};

static int tpmrng_ext_info(crypto_provider_handle_t,
	crypto_provider_ext_info_t *, crypto_req_handle_t);

static crypto_provider_management_ops_t tpmrng_extinfo_op = {
	.ext_info =	tpmrng_ext_info,
};

static crypto_ops_t tpmrng_crypto_ops = {
	.co_control_ops =	&tpmrng_control_ops,
	.co_random_ops =	&tpmrng_random_number_ops,
	.co_provider_ops =	&tpmrng_extinfo_op,
};

static crypto_provider_info_t tpmrng_prov_info = {
	.pi_interface_version =		CRYPTO_SPI_VERSION_2,
	.pi_provider_description =	"TPM Provider",
	.pi_provider_type =		CRYPTO_HW_PROVIDER,
	.pi_ops_vector =		&tpmrng_crypto_ops,
};

/*
 * Random number generator entry points
 */
static void
strncpy_spacepad(uchar_t *s1, char *s2, int n)
{
	int s2len = strlen(s2);

	(void) strncpy((char *)s1, s2, n);
	if (s2len < n)
		(void) memset(s1 + s2len, ' ', n - s2len);
}

static int
tpmrng_ext_info(crypto_provider_handle_t prov,
    crypto_provider_ext_info_t *ext_info,
    crypto_req_handle_t cfreq __unused)
{
	tpm_client_t	*c = (tpm_client_t *)prov;
	tpm_t		*tpm = c->tpmc_tpm;
	char		*s = "";
	char		buf[64];

	if (tpm == NULL)
		return (DDI_FAILURE);

	VERIFY0(ddi_prop_lookup_string(DDI_DEV_T_ANY, tpm->tpm_dip,
	    DDI_PROP_DONTPASS, "vendor-name", &s));
	strncpy_spacepad(ext_info->ei_manufacturerID,
	    s, sizeof (ext_info->ei_manufacturerID));

	strncpy_spacepad(ext_info->ei_model, "0",
	    sizeof (ext_info->ei_model));
	strncpy_spacepad(ext_info->ei_serial_number, "0",
	    sizeof (ext_info->ei_serial_number));

	ext_info->ei_flags = CRYPTO_EXTF_RNG | CRYPTO_EXTF_SO_PIN_LOCKED;
	ext_info->ei_max_session_count = CRYPTO_EFFECTIVELY_INFINITE;
	ext_info->ei_max_pin_len = 0;
	ext_info->ei_min_pin_len = 0;
	ext_info->ei_total_public_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_free_public_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_total_private_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_free_public_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_time[0] = 0;

	switch (tpm->tpm_family) {
	case TPM_FAMILY_1_2:
		ext_info->ei_hardware_version.cv_major = 1;
		ext_info->ei_hardware_version.cv_minor = 2;
		break;
	case TPM_FAMILY_2_0:
		ext_info->ei_hardware_version.cv_major = 2;
		ext_info->ei_hardware_version.cv_minor = 0;
		break;
	}

	ext_info->ei_firmware_version.cv_major = tpm->tpm_fw_major;
	ext_info->ei_firmware_version.cv_minor = tpm->tpm_fw_minor;

	(void) snprintf(buf, sizeof (buf), "tpmrng TPM RNG");

	strncpy_spacepad(ext_info->ei_label, buf, sizeof (ext_info->ei_label));

	return (CRYPTO_SUCCESS);
}

int
tpm_kcf_register(tpm_t *tpm)
{
	int			ret;
	char			id[64];

	ASSERT(tpm != NULL);

	(void) strlcpy(id, "Trusted Platform Module", sizeof (id));

	tpmrng_prov_info.pi_provider_description = id;
	tpmrng_prov_info.pi_provider_dev.pd_hw = tpm->tpm_dip;
	tpmrng_prov_info.pi_provider_handle = tpm->tpm_internal_client;

	ret = crypto_register_provider(&tpmrng_prov_info, &tpm->tpm_n_prov);

	if (ret != CRYPTO_SUCCESS) {
		tpm->tpm_n_prov = 0;
		return (DDI_FAILURE);
	}
	ASSERT3U(tpm->tpm_n_prov, !=, 0);

	crypto_provider_notification(tpm->tpm_n_prov, CRYPTO_PROVIDER_READY);

	if (tpm->tpm_family == TPM_FAMILY_1_2) {
		/*
		 * For unknown reasons, even when TPM1.2 devices were
		 * registered with KCF, the RNG mechanism was always disabled
		 * by default. We preserve the historical behavior for now.
		 */
		crypto_mech_name_t	*rngmech;

		rngmech = kmem_zalloc(1 * sizeof (crypto_mech_name_t),
		    KM_SLEEP);
		(void) strlcpy(rngmech[0], "random",
		    sizeof (crypto_mech_name_t));

		ret = crypto_load_dev_disabled("tpm",
		    ddi_get_instance(tpm->tpm_dip), 1, rngmech);
		if (ret != CRYPTO_SUCCESS) {
			cmn_err(CE_WARN,
			    "!crypto_load_dev_disabled failed (%d)", ret);
		}
	}

	return (DDI_SUCCESS);
}

int
tpm_kcf_unregister(tpm_t *tpm)
{
	if (tpm->tpm_n_prov != 0) {
		int ret = crypto_unregister_provider(tpm->tpm_n_prov);
		if (ret != CRYPTO_SUCCESS) {
			dev_err(tpm->tpm_dip, CE_WARN,
			    "failed to unregister from KCF");
			return (DDI_FAILURE);
		}
		tpm->tpm_n_prov = 0;
	}

	return (DDI_SUCCESS);
}

static void
tpmrng_provider_status(crypto_provider_handle_t provider __unused,
    uint_t *status)
{
	*status = CRYPTO_PROVIDER_READY;
}

static int
tpmrng_seed_random(crypto_provider_handle_t provider, crypto_session_id_t sid,
    uchar_t *buf, size_t len, uint_t entropy_est __unused,
    uint32_t flags __unused, crypto_req_handle_t req __unused)
{
	tpm_client_t	*c = (tpm_client_t *)provider;
	tpm_t		*tpm = c->tpmc_tpm;

	switch (tpm->tpm_family) {
	case TPM_FAMILY_1_2:
		return (tpm12_seed_random(c, buf, len));
	case TPM_FAMILY_2_0:
		return (tpm20_seed_random(c, buf, len));
	default:
		dev_err(tpm->tpm_dip, CE_PANIC,
		    "unknown TPM family %d", tpm->tpm_family);
		/* Make gcc happy */
		return (CRYPTO_FAILED);
	}
}

static int
tpmrng_generate_random(crypto_provider_handle_t provider,
    crypto_session_id_t sid __unused, uchar_t *buf, size_t len,
    crypto_req_handle_t req __unused)
{
	tpm_client_t	*c = (tpm_client_t *)provider;
	tpm_t		*tpm = c->tpmc_tpm;

	switch (tpm->tpm_family) {
	case TPM_FAMILY_1_2:
		return (tpm12_generate_random(c, buf, len));
	case TPM_FAMILY_2_0:
		return (tpm20_generate_random(c, buf, len));
	default:
		dev_err(tpm->tpm_dip, CE_PANIC,
		    "unknown TPM family %d", tpm->tpm_family);
		/* Make gcc happy */
		return (CRYPTO_FAILED);
	}
}
