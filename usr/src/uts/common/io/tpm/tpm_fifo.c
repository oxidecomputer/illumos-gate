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
 * Copyright 2024 RackTop Systems, Inc.
 */

#include <sys/sysmacros.h>
#include <sys/ddifm.h>
#include <sys/fm/io/ddi.h>
#include <sys/fm/protocol.h>

#include "tpm_ddi.h"
#include "tpm_tis.h"

static void tis_release_locality(tpm_t *, uint8_t, bool);

static uint8_t
tpm_tis_get_status(tpm_t *tpm)
{
	return (tpm_get8(tpm, TPM_STS));
}

static void
tpm_tis_set_ready(tpm_t *tpm)
{
	tpm_put8(tpm, TPM_STS, TPM_STS_CMD_READY);
}

static bool
tis_burst_nonzero(tpm_t *tpm, bool final, clock_t to, const char *func)
{
	uint32_t sts = tpm_get32(tpm, TPM_STS);

	if (TPM_STS_BURSTCOUNT(sts) > 0)
		return (true);

	if (final) {
		tpm_ereport_timeout(tpm, TPM_STS, to, func);
	}

	return (false);
}

/*
 * Whenever the driver wants to write to the DATA_IO register, it needs
 * to figure out the burstcount.  This is the amount of bytes it can write
 * before having to wait for the long LPC bus cycle
 *
 * Returns: 0 if error, burst count if success
 */
static int
tpm_tis_get_burstcount(tpm_t *tpm, uint16_t *burstp)
{
	int ret;

	ASSERT(MUTEX_HELD(&tpm->tpm_lock));

	ret = tpm_wait(tpm, tis_burst_nonzero, false, tpm->tpm_timeout_d,
	    __func__);
	if (ret != 0) {
		return (ret);
	}

	*burstp = TPM_STS_BURSTCOUNT(tpm_get32(tpm, TPM_STS));
	return (0);
}

static bool
tis_is_ready(tpm_t *tpm, bool final, clock_t to, const char *func)
{
	uint8_t sts = tpm_tis_get_status(tpm);

	if ((sts & TPM_STS_CMD_READY) != 0)
		return (true);

	if (final) {
		tpm_ereport_timeout(tpm, TPM_STS, to, func);
	}

	return (false);
}

static int
tis_fifo_make_ready(tpm_t *tpm, clock_t to)
{
	int ret;
	uint8_t status;

	mutex_enter(&tpm->tpm_lock);

	if (tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit) {
		mutex_exit(&tpm->tpm_lock);
		return (SET_ERROR(ECANCELED));
	}

	status = tpm_tis_get_status(tpm);

	/* If already ready, we're done */
	if ((status & TPM_STS_CMD_READY) != 0) {
		mutex_exit(&tpm->tpm_lock);
		return (0);
	}

	/*
	 * Otherwise, request the TPM to transition to the ready state, and
	 * wait until it is.
	 */
	tpm_tis_set_ready(tpm);

	ret = tpm_wait(tpm, tis_is_ready, false, to, __func__);
	mutex_exit(&tpm->tpm_lock);

	return (ret);
}

static bool
tis_status_valid(tpm_t *tpm, bool final, clock_t to, const char *func)
{
	uint8_t sts = tpm_tis_get_status(tpm);

	if ((sts & TPM_STS_VALID) != 0) {
		return (true);
	}

	if (final) {
		tpm_ereport_timeout(tpm, TPM_STS, to, func);
	}

	return (false);
}

static int
tis_expecting_data(tpm_t *tpm, bool *expp)
{
	tpm_tis_t *tis = &tpm->tpm_u.tpmu_tis;
	int ret;
	uint8_t sts;

	ASSERT(MUTEX_HELD(&tpm->tpm_lock));

	/*
	 * Wait for stsValid to be set before checking the Expect
	 * bit.
	 */
	ret = tpm_wait(tpm, tis_status_valid, tis->ttis_has_sts_valid_int,
	    tpm->tpm_timeout_c, __func__);
	if (ret != 0) {
		return (ret);
	}

	sts = tpm_tis_get_status(tpm);
	if ((sts & TPM_STS_VALID) == 0) {
		uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

		ddi_fm_ereport_post(tpm->tpm_dip,
		    DDI_FM_DEVICE "." DDI_FM_DEVICE_INVAL_STATE, ena,
		    DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
		    "tpm_interface", DATA_TYPE_STRING,
		    tpm_iftype_str(tpm->tpm_iftype),
		    "locality", DATA_TYPE_UINT8, tpm->tpm_locality,
		    "detailed error message", DATA_TYPE_STRING,
		    "status went from valid to not valid waiting for data",
		    NULL);

		return (SET_ERROR(EIO));
	}

	*expp = (sts & TPM_STS_DATA_EXPECT) != 0 ? true : false;
	return (0);
}

static int
tis_send_data(tpm_t *tpm, uint8_t *buf, uint32_t amt)
{
	uint8_t *dest;
	uint32_t chunk;
	int ret;
	uint16_t burstcnt;
	bool expecting;

	VERIFY3U(amt, >, 0);

	mutex_enter(&tpm->tpm_lock);

	dest = tpm_reg_addr(tpm, tpm->tpm_locality, TPM_DATA_FIFO);

	/*
	 * Send the command. The TPM's burst count determines how many
	 * how many bytes to write at one time. Once we write burstcount
	 * bytes, we must wait for the TPM to report a burstcount > 0
	 * before writing more bytes.
	 */
	do {
		/*
		 * tpm_tis_get_burstcount() will check for cancellation
		 * by virtue of callign tpm_wait(), so we don't need to
		 * check again.
		 */
		ret = tpm_tis_get_burstcount(tpm, &burstcnt);
		switch (ret) {
		case 0:
			VERIFY3U(burstcnt, >, 0);
			break;
		case ETIME:
		case ECANCELED:
			mutex_exit(&tpm->tpm_lock);
			return (ret);
		default:
			dev_err(tpm->tpm_dip, CE_PANIC,
			    "unexpected return value from "
			    "tpm_tis_get_burstcount: %d", ret);
		}

		/*
		 * If tpm_tis_get_burstcount() succeeds, burstcnt should be
		 * a positive value.
		 */
		VERIFY3U(burstcnt, >, 0);

		chunk = MIN(burstcnt, amt);

		ddi_rep_put8(tpm->tpm_handle, buf, dest, chunk,
		    DDI_DEV_NO_AUTOINCR);

		buf += chunk;
		amt -= chunk;

		if (amt > 0) {
			/*
			 * Once the first byte is written to the TPM,
			 * Expect is set, and remains set until the
			 * last byte of the command has been written.
			 *
			 * Make sure if there is more data to write, that
			 * the TPM is expecting more data. We only check
			 * every burstcnt bytes as this is just a sanity
			 * check. Any data written after what the TPM
			 * believes is the last bytes of the command
			 * are ignored. If there is a disagreement between
			 * us and the TPM, we error out and abort the
			 * current command.
			 */
			ret = tis_expecting_data(tpm, &expecting);
			if (ret != 0) {
				mutex_exit(&tpm->tpm_lock);
				return (ret);
			}

			if (!expecting) {
				uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

				mutex_exit(&tpm->tpm_lock);

				ddi_fm_ereport_post(tpm->tpm_dip,
				    DDI_FM_DEVICE "." DDI_FM_DEVICE_INVAL_STATE,
				    ena, DDI_NOSLEEP,
				    FM_VERSION, DATA_TYPE_UINT8,
				    FM_EREPORT_VERS0,
				    "tpm_interface", DATA_TYPE_STRING,
				    tpm_iftype_str(tpm->tpm_iftype),
				    "locality", DATA_TYPE_UINT8,
				    tpm->tpm_locality,
				    "cmd", DATA_TYPE_UINT32, tpm->tpm_cmd,
				    "detailed error message", DATA_TYPE_STRING,
				    "TPM not expecting data with unsent data",
				    NULL);

				return (SET_ERROR(EIO));
			}
		}
	} while (amt > 0);

	/*
	 * Verify that the TPM agrees that it's received the entire
	 * command.
	 */
	ret = tis_expecting_data(tpm, &expecting);
	if (ret != 0) {
		mutex_exit(&tpm->tpm_lock);
		return (ret);
	}
	if (expecting) {
		uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

		mutex_exit(&tpm->tpm_lock);

		ddi_fm_ereport_post(tpm->tpm_dip,
		    DDI_FM_DEVICE "." DDI_FM_DEVICE_INVAL_STATE, ena,
		    DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
		    "tpm_interface", DATA_TYPE_STRING,
		    tpm_iftype_str(tpm->tpm_iftype),
		    "locality", DATA_TYPE_UINT8, tpm->tpm_locality,
		    "cmd", DATA_TYPE_UINT32, tpm->tpm_cmd,
		    "detailed error message", DATA_TYPE_STRING,
		    "TPM expecting data after request sent",
		    NULL);

		return (SET_ERROR(EIO));
	}

	mutex_exit(&tpm->tpm_lock);
	return (0);
}

static bool
tis_data_avail(tpm_t *tpm)
{
	uint8_t status = tpm_tis_get_status(tpm);

	/*
	 * Both the VALID and DATA_AVAIL bits must be set for there
	 * to actually be data available to read.
	 */
	if ((status & (TPM_STS_VALID|TPM_STS_DATA_AVAIL)) ==
	    (TPM_STS_VALID|TPM_STS_DATA_AVAIL)) {
		return (true);
	}

	return (false);
}

static bool
tis_data_avail_cmd(tpm_t *tpm, bool final, uint32_t cmd, clock_t to,
    const char *func)
{
	if (tis_data_avail(tpm)) {
		return (true);
	}

	if (final) {
		tpm_ereport_timeout_cmd(tpm, to, func);
	}

	return (false);
}

static bool
tis_more_data_avail(tpm_t *tpm, bool final, clock_t to, const char *func)
{
	if (tis_data_avail(tpm)) {
		return (true);
	}

	if (final) {
		tpm_ereport_timeout(tpm, TPM_STS, to, func);
	}

	return (false);
}

static int
tis_recv_chunk(tpm_t *tpm, uint8_t *buf, uint32_t len)
{
	uint8_t *src;
	uint32_t chunk;
	int ret;
	uint16_t burstcnt;

	ASSERT(MUTEX_HELD(&tpm->tpm_lock));

	src = tpm_reg_addr(tpm, tpm->tpm_locality, TPM_DATA_FIFO);

	while (len > 0) {
		ret = tpm_wait(tpm, tis_more_data_avail, false,
		    tpm->tpm_timeout_c, __func__);
		if (ret != 0) {
			return (ret);
		}

		/*
		 * The burst count may be dynamic, so we have to
		 * check each time.
		 */
		ret = tpm_tis_get_burstcount(tpm, &burstcnt);
		if (ret != 0) {
			return (ret);
		}

		chunk = MIN(burstcnt, len);
		ddi_rep_get8(tpm->tpm_handle, buf, src, chunk,
		    DDI_DEV_NO_AUTOINCR);

		buf += chunk;
		len -= chunk;
	}

	return (0);
}

static int
tis_recv_data(tpm_t *tpm, tpm_cmd_t *cmd)
{
	int ret;
	uint32_t expected, status;
	uint32_t cmdresult;
	bool is_retry = false;

	mutex_enter(&tpm->tpm_lock);

retry:
	bzero(cmd->tcmd_buf, sizeof (cmd->tcmd_buf));

	/* Read header */
	ret = tis_recv_chunk(tpm, cmd->tcmd_buf, TPM_HEADER_SIZE);
	if (ret != 0) {
		goto check_retry;
	}

	/* If we succeeded, we have TPM_HEADER_SIZE bytes in buf */
	cmdresult = tpm_cmd_rc(cmd);

	/* Get 'paramsize'(4 bytes)--it includes tag and paramsize */
	expected = tpm_cmdlen(cmd);
	if (expected > sizeof (cmd->tcmd_buf)) {
		uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

		ddi_fm_ereport_post(tpm->tpm_dip,
		    DDI_FM_DEVICE "." DDI_FM_DEVICE_INVAL_STATE, ena,
		    DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
		    "tpm_interface", DATA_TYPE_STRING,
		    tpm_iftype_str(tpm->tpm_iftype),
		    "locality", DATA_TYPE_UINT8, tpm->tpm_locality,
		    "cmd", DATA_TYPE_UINT32, tpm->tpm_cmd,
		    "rc", DATA_TYPE_UINT32, cmdresult,
		    "length", DATA_TYPE_UINT32, expected,
		    "detailed error message", DATA_TYPE_STRING,
		    "command returned more data than expected",
		    NULL);

		goto done;
	}

	/* Read in the rest of the data from the TPM */
	ret = tis_recv_chunk(tpm, cmd->tcmd_buf + TPM_HEADER_SIZE,
	    expected - TPM_HEADER_SIZE);
	if (ret != 0) {
		goto check_retry;
	}

	/* The TPM MUST set the state to stsValid within TIMEOUT_C */
	ret = tpm_wait(tpm, tis_status_valid, false, tpm->tpm_timeout_c,
	    __func__);

	status = tpm_tis_get_status(tpm);
	if (ret != 0) {
		uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

		ddi_fm_ereport_post(tpm->tpm_dip,
		    DDI_FM_DEVICE "." DDI_FM_DEVICE_INVAL_STATE, ena,
		    DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
		    "tpm_interface", DATA_TYPE_STRING,
		    tpm_iftype_str(tpm->tpm_iftype),
		    "locality", DATA_TYPE_UINT8, tpm->tpm_locality,
		    "TPM_STS", DATA_TYPE_UINT32, status,
		    "detailed error message", DATA_TYPE_STRING,
		    "valid status not asserted after I/O",
		    NULL);

		goto done;
	}

	/* There is still more data? */
	if (status & TPM_STS_DATA_AVAIL) {
		uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

		/* We'll note it but go ahead and return what we have */
		ddi_fm_ereport_post(tpm->tpm_dip,
		    DDI_FM_DEVICE "." DDI_FM_DEVICE_INVAL_STATE, ena,
		    DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
		    "tpm_interface", DATA_TYPE_STRING,
		    tpm_iftype_str(tpm->tpm_iftype),
		    "locality", DATA_TYPE_UINT8, tpm->tpm_locality,
		    "TPM_STS", DATA_TYPE_UINT32, status,
		    "detailed error message", DATA_TYPE_STRING,
		    "more data available after reading entire response",
		    NULL);
	}

done:
	mutex_exit(&tpm->tpm_lock);
	return (ret);

check_retry:
	if (ret != ETIME || is_retry) {
		goto done;
	}

	/* Retry reading the entire response again */
	is_retry = true;
	tpm_put8(tpm, TPM_STS, TPM_STS_RESPONSE_RETRY);
	goto retry;
}

/*
 * Checks whether the given locality is active
 * Use TPM_ACCESS register and the masks TPM_ACCESS_VALID,TPM_ACTIVE_LOCALITY
 */
static bool
tis_locality_active(tpm_t *tpm, uint8_t locality)
{
	uint8_t access_bits;

	VERIFY3U(locality, <=, TPM_LOCALITY_MAX);

	/* Just check to see if the requested locality works */
	access_bits = tpm_get8_loc(tpm, locality, TPM_ACCESS);

	access_bits &= (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID);

	if (access_bits == (TPM_ACCESS_ACTIVE_LOCALITY | TPM_ACCESS_VALID)) {
		return (true);
	} else {
		return (false);
	}
}

static bool
tis_is_locality_active(tpm_t *tpm, bool final, clock_t to, const char *func)
{
	/* tpm_wait() should call us with this held */
	VERIFY(MUTEX_HELD(&tpm->tpm_lock));

	if (tis_locality_active(tpm, tpm->tpm_locality))
		return (true);

	if (final) {
		tpm_ereport_timeout(tpm, TPM_ACCESS, to, func);
	}

	return (false);
}

static int
tis_request_locality(tpm_t *tpm, uint8_t locality)
{
	int ret;

	VERIFY3U(locality, <=, TPM_LOCALITY_MAX);

	mutex_enter(&tpm->tpm_lock);

	if (tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit) {
		mutex_exit(&tpm->tpm_lock);
		return (SET_ERROR(ECANCELED));
	}

	if (tis_locality_active(tpm, locality)) {
		tpm->tpm_locality = locality;
		mutex_exit(&tpm->tpm_lock);
		return (0);
	}

	/*
	 * Unlike CRB, where the TPM_LOC_STATE_x register can be read from
	 * any locality to determine the active locality, for TIS/FIFO we
	 * must read the TPM_ACCESS in register for a given locality to
	 * determine if it is the active locality.
	 */
	tpm->tpm_locality = locality;
	tpm_put8(tpm, TPM_ACCESS, TPM_ACCESS_REQUEST_USE);

	ret = tpm_wait(tpm, tis_is_locality_active, true, tpm->tpm_timeout_a,
	    __func__);
	mutex_exit(&tpm->tpm_lock);

	switch (ret) {
	case 0:
		break;
	case ETIME:
	case ECANCELED:
		tis_release_locality(tpm, locality, true);
		break;
	default:
		dev_err(tpm->tpm_dip, CE_PANIC,
		    "unexpected value from tpm_wait_u8: %d", ret);
	}

	return (ret);
}

static void
tis_release_locality(tpm_t *tpm, uint8_t locality, bool force)
{
	VERIFY3U(locality, <=, TPM_LOCALITY_MAX);

	mutex_enter(&tpm->tpm_lock);

	tpm->tpm_locality = locality;
	if (force ||
	    (tpm_get8(tpm, TPM_ACCESS) &
	    (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_REQUEST_PENDING | TPM_ACCESS_VALID)) {
		/*
		 * Writing 1 to active locality bit in TPM_ACCESS
		 * register reliquishes the control of the locality
		 */
		tpm_put8(tpm, TPM_ACCESS, TPM_ACCESS_ACTIVE_LOCALITY);
	}
	tpm->tpm_locality = -1;

	mutex_exit(&tpm->tpm_lock);
}

uint_t
tpm_tis_intr(caddr_t arg0, caddr_t arg1 __unused)
{
	const uint32_t mask = TPM_TIS_INT_CMD_READY |
	    TPM_TIS_INT_LOCALITY_CHANGED | TPM_TIS_INT_STATUS_VALID |
	    TPM_TIS_INT_DATA_AVAIL;

	tpm_t *tpm = (tpm_t *)arg0;
	uint32_t status;

	mutex_enter(&tpm->tpm_lock);
	status = tpm_get32(tpm, TPM_INT_STATUS);
	if ((status & mask) == 0) {
		mutex_exit(&tpm->tpm_lock);

		/* Not us */
		return (DDI_INTR_UNCLAIMED);
	}

	/* Ack the interrupt */
	tpm_put32(tpm, TPM_INT_STATUS, status);

	/*
	 * For now at least, it's enough to signal the waiting command to
	 * recheck their appropriate register.
	 */
	cv_signal(&tpm->tpm_thr_cv);
	mutex_exit(&tpm->tpm_lock);

	return (DDI_INTR_CLAIMED);
}

bool
tpm_tis_init(tpm_t *tpm)
{
	tpm_tis_t *tis = &tpm->tpm_u.tpmu_tis;
	uint32_t cap;
	uint32_t devid;
	uint8_t revid;

	VERIFY(tpm->tpm_iftype == TPM_IF_TIS || tpm->tpm_iftype == TPM_IF_FIFO);
	VERIFY(tpm_can_access(tpm));

	cap = tpm_get32(tpm, TPM_INTF_CAP);

	switch (tpm->tpm_iftype) {
	case TPM_IF_TIS:
		switch (TIS_INTF_VER_VAL(cap)) {
		case TIS_INTF_VER_VAL_1_21:
		case TIS_INTF_VER_VAL_1_3:
			tpm->tpm_family = TPM_FAMILY_1_2;
			break;
		case TIS_INTF_VER_VAL_1_3_TPM:
			tpm->tpm_family = TPM_FAMILY_2_0;
			break;
		default:
			dev_err(tpm->tpm_dip, CE_NOTE,
			    "!%s: unknown TPM interface version 0x%x", __func__,
			    TIS_INTF_VER_VAL(cap));
			return (false);
		}
		break;
	case TPM_IF_FIFO:
		VERIFY3S(tpm->tpm_family, ==, TPM_FAMILY_2_0);
		break;
	default:
		/*
		 * We should only be called if the TPM is using the TIS
		 * or FIFO interface.
		 */
		dev_err(tpm->tpm_dip, CE_PANIC, "%s: invalid interface type %d",
		    __func__, tpm->tpm_iftype);
		break;
	}

	devid = tpm_get32(tpm, TPM_DID_VID);
	revid = tpm_get8(tpm, TPM_RID);

	tpm->tpm_did = devid >> 16;
	tpm->tpm_vid = devid & 0xffff;
	tpm->tpm_rid = revid;

	tis->ttis_state = TPMT_ST_IDLE;
	tis->ttis_xfer_size = TIS_INTF_XFER_VAL(cap);

	/* Both of these are mandated by the spec */
	if ((cap & TPM_INTF_CAP_DATA_AVAIL) == 0) {
		dev_err(tpm->tpm_dip, CE_NOTE,
		    "!TPM does not support mandatory data available interrupt");
		return (false);
	}
	if ((cap & TPM_INTF_CAP_LOC_CHANGED) == 0) {
		dev_err(tpm->tpm_dip, CE_NOTE,
		    "!TPM does not support mandatory locality changed "
		    "interrupt");
		return (false);
	}

	/* These are optional */
	if ((cap & TPM_INTF_CAP_STS_VALID) != 0) {
		tis->ttis_has_sts_valid_int = true;
	}
	if ((cap & TPM_INTF_CAP_CMD_READY) != 0) {
		tis->ttis_has_cmd_ready_int = true;
	}

	switch (tpm->tpm_family) {
	case TPM_FAMILY_1_2:
		return (tpm12_init(tpm));
	case TPM_FAMILY_2_0:
		return (tpm20_init(tpm));
	}
	return (false);
}

static int
tis_start(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	int ret;

	mutex_enter(&tpm->tpm_lock);

	if (tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit) {
		mutex_exit(&tpm->tpm_lock);
		return (SET_ERROR(ECANCELED));
	}

	tpm_put8(tpm, TPM_STS, TPM_STS_GO);
	ret = tpm_wait_cmd(tpm, cmd, tis_data_avail_cmd, __func__);

	mutex_exit(&tpm->tpm_lock);

	return (ret);
}

int
tis_exec_cmd(tpm_t *tpm, uint8_t loc, tpm_cmd_t *cmd)
{
	uint32_t cmdlen;
	int ret;

	VERIFY(tpm_can_access(tpm));
	VERIFY(tpm->tpm_iftype == TPM_IF_TIS || tpm->tpm_iftype == TPM_IF_FIFO);

	cmdlen = tpm_cmdlen(cmd);
	VERIFY3U(cmdlen, >=, TPM_HEADER_SIZE);
	VERIFY3U(cmdlen, <=, sizeof (cmd->tcmd_buf));

	ret = tis_request_locality(tpm, loc);
	if (ret != 0) {
		return (ret);
	}

	/* Make sure the TPM is in the ready state */
	ret = tis_fifo_make_ready(tpm, tpm->tpm_timeout_b);
	if (ret != 0) {
		goto done;
	}

	ret = tis_send_data(tpm, cmd->tcmd_buf, cmdlen);
	if (ret != 0) {
		goto done;
	}

	ret = tis_start(tpm, cmd);
	if (ret != 0) {
		goto done;
	}

	ret = tis_recv_data(tpm, cmd);

done:
	/*
	 * If we were cancelled, we defer putting the TPM into the ready
	 * state (which will stop any current execution) and release the
	 * locality until after we've released the client so it's not
	 * blocking while waiting for the TPM to cancel the operation.
	 */
	if (ret != ECANCELED) {
		tpm_tis_set_ready(tpm);

		/*
		 * Release the locality after completion to allow a lower value
		 * locality to use the TPM.
		 */
		tis_release_locality(tpm, loc, false);
	}

	return (ret);
}

void
tis_cancel_cmd(tpm_t *tpm, tpm_duration_t dur)
{
	clock_t to;

	VERIFY(tpm_can_access(tpm));

	/* We should be called after the TPM thread has acked the cancel req */
	VERIFY(!tpm->tpm_thr_cancelreq);

	switch (dur) {
	case TPM_SHORT:
	case TPM_MEDIUM:
		to = tpm->tpm_timeout_a;
		break;
	default:
		to = tpm->tpm_timeout_b;
		break;
	}

	(void) tis_fifo_make_ready(tpm, to);
	tis_release_locality(tpm, tpm->tpm_locality, false);
}

void
tpm_tis_intr_mgmt(tpm_t *tpm, bool enable)
{
	tpm_tis_t *tis = &tpm->tpm_u.tpmu_tis;
	uint32_t mask = 0;

	VERIFY(tpm->tpm_use_interrupts);

	if (enable) {
		/* Enable global interrupts */
		mask |= TPM_INT_GLOBAL_EN;

		/*
		 * Enable locality change and data available. These are
		 * always supported.
		 */
		mask |= TPM_INT_LOCAL_CHANGE_INT_EN;
		mask |= TPM_INT_STS_DATA_AVAIL_EN;

		if (tis->ttis_has_sts_valid_int) {
			mask |= TPM_INT_STS_VALID_EN;
		}
		if (tis->ttis_has_cmd_ready_int) {
			mask |= TPM_INT_STS_DATA_AVAIL_EN;
		}
	}

	tpm_put32(tpm, TPM_INT_ENABLE, mask);
}
