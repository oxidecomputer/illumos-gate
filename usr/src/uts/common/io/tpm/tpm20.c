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
 * Copyright 2023 Jason King
 * Copyright 2025 RackTop Systems, Inc.
 */

#include <sys/debug.h>
#include <sys/crypto/common.h>
#include <sys/types.h>

#include "tpm_ddi.h"
#include "tpm20.h"

bool
tpm20_init(tpm_t *tpm)
{
	/* Until TAB support is implemented, we only support 1 client. */
	tpm->tpm_client_max = 1;

	/*
	 * TPM2.0 defines explicit timeouts unlike TPM1.2 which has default
	 * timeout values that can be overridden by the TPM1.2 module. As
	 * a result, these can be statically defined.
	 *
	 * Timeouts are in milliseconds.
	 */
	tpm->tpm_timeout_a = drv_usectohz(TPM20_TIMEOUT_A * 1000);
	tpm->tpm_timeout_b = drv_usectohz(TPM20_TIMEOUT_B * 1000);
	tpm->tpm_timeout_c = drv_usectohz(TPM20_TIMEOUT_C * 1000);
	tpm->tpm_timeout_d = drv_usectohz(TPM20_TIMEOUT_D * 1000);

	tpm->tpm20_timeout_create = drv_usectohz(TPM20_TIMEOUT_CREATE * 1000);
	tpm->tpm20_timeout_default = drv_usectohz(TPM20_TIMEOUT_DEFAULT * 1000);

	tpm->tpm_timeout_poll = drv_usectohz(TPM_POLLING_TIMEOUT * 1000);

	return (true);
}

clock_t
tpm20_get_timeout(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	uint32_t cc = tpm_cc(cmd);

	switch (cc) {
	case TPM2_CC_Startup:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_SelfTest:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_GetRandom:
		return (tpm->tpm_timeout_b);
	case TPM2_CC_HashSequenceStart:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_SequenceUpdate:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_SequenceComplete:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_EventSequenceComplete:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_VerifySignature:
		return (tpm->tpm_timeout_b);
	case TPM2_CC_PCR_Extend:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_HierarchyControl:
		return (tpm->tpm_timeout_b);
	case TPM2_CC_HierarchyChangeAuth:
		return (tpm->tpm_timeout_b);
	case TPM2_CC_GetCapability:
		return (tpm->tpm_timeout_a);
	case TPM2_CC_NV_Read:
		return (tpm->tpm_timeout_b);
	case TPM2_CC_Create:
	case TPM2_CC_CreatePrimary:
	case TPM2_CC_CreateLoaded:
		return (tpm->tpm20_timeout_create);
	default:
		return (tpm->tpm20_timeout_default);
	}
}

tpm_duration_t
tpm20_get_duration_type(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	uint32_t cc = tpm_cc(cmd);

	switch (cc) {
	case TPM2_CC_Startup:
		return (TPM_SHORT);

	case TPM2_CC_SelfTest:
		/*
		 * Immediately after the buffer is the fullTest parameter.
		 * If true, a full test is done which uses the long timeout.
		 * Otherwise a short duration is used.
		 */
		if (cmd->tcmd_buf[TPM_HEADER_SIZE] != 0)
			return (TPM_LONG);
		return (TPM_SHORT);

	case TPM2_CC_GetRandom:
		return (TPM_MEDIUM);

	case TPM2_CC_HashSequenceStart:
	case TPM2_CC_SequenceUpdate:
	case TPM2_CC_SequenceComplete:
	case TPM2_CC_EventSequenceComplete:
		return (TPM_SHORT);

	case TPM2_CC_VerifySignature:
		return (TPM_MEDIUM);

	case TPM2_CC_PCR_Extend:
		return (TPM_SHORT);

	case TPM2_CC_HierarchyControl:
	case TPM2_CC_HierarchyChangeAuth:
		return (TPM_MEDIUM);

	case TPM2_CC_GetCapability:
		return (TPM_SHORT);

	case TPM2_CC_NV_Read:
		return (TPM_MEDIUM);

	default:
		return (TPM_MEDIUM);
	}

	return (TPM_MEDIUM);
}

int
tpm20_generate_random(tpm_client_t *c, uchar_t *buf, size_t len)
{
	tpm_cmd_t	*cmd = &c->tpmc_cmd;
	int		ret;
	TPM2_RC		trc;

	if (len > UINT16_MAX) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	mutex_enter(&c->tpmc_lock);
	if (c->tpmc_state != TPM_CLIENT_IDLE) {
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_BUSY);
	}

	tpm_cmd_init(cmd, TPM2_CC_GetRandom, TPM2_ST_NO_SESSIONS);
	tpm_cmd_put16(cmd, len);

	ret = tpm_exec_internal(c);
	if (ret != 0) {
		/*
		 * XXX: Can we map to better errors here?
		 * Maybe CRYPTO_BUSY for timeouts?
		 */
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_FAILED);
	}

	trc = tpm_cmd_rc(cmd);
	if (trc != TPM2_RC_SUCCESS) {
		dev_err(c->tpmc_tpm->tpm_dip, CE_NOTE,
		    "!TPM2_CC_GetRandom failed with 0x%x", trc);
		/* TODO: Maybe map TPM rc codes to CRYPTO_xxx values */
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_FAILED);
	}

	/*
	 * The response includes the fixed sized TPM header, followed by
	 * a 16-bit length, followed by the random data.
	 *
	 * Verify we have at least len bytes of random data.
	 */
	if (tpm_getbuf16(cmd, TPM_HEADER_SIZE) < len) {
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_FAILED);
	}

	/* Copy out the random data */
	tpm_cmd_getbuf(cmd, TPM_HEADER_SIZE + sizeof (uint16_t), len, buf);

	tpm_client_reset(c);
	mutex_exit(&c->tpmc_lock);
	return (CRYPTO_SUCCESS);
}

#define	TPM_STIR_MAX 128

int
tpm20_seed_random(tpm_client_t *c, uchar_t *buf, size_t len)
{
	tpm_cmd_t	*cmd = &c->tpmc_cmd;
	int		ret;
	TPM2_RC		trc;

	/* Should we just truncate instead? */
	if (len > TPM_STIR_MAX) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	mutex_enter(&c->tpmc_lock);
	if (c->tpmc_state != TPM_CLIENT_IDLE) {
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_BUSY);
	}

	tpm_cmd_init(cmd, TPM2_CC_StirRandom, TPM2_ST_NO_SESSIONS);
	tpm_cmd_put16(cmd, (uint16_t)len);
	tpm_cmd_copy(cmd, buf, len);

	ret = tpm_exec_internal(c);
	if (ret != 0) {
		/* XXX: Map to better errors? */
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_FAILED);
	}

	trc = tpm_cmd_rc(cmd);
	if (trc != TPM2_RC_SUCCESS) {
		dev_err(c->tpmc_tpm->tpm_dip, CE_CONT,
		    "!TPM2_CC_StirRandom failed with 0x%x", trc);
		/* TODO: Maybe map TPM return codes to CRYPTO_xxx values */
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_FAILED);
	}

	tpm_client_reset(c);
	mutex_exit(&c->tpmc_lock);
	return (ret);
}
