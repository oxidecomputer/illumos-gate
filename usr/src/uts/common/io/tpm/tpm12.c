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
 * Copyright 2022 Jason King
 * Copyright 2025 RackTop Systems, Inc.
 */

#include <sys/byteorder.h>
#include <sys/crypto/api.h>

#include "tpm_ddi.h"
#include "tpm_tis.h"

/*
 * In order to test the 'millisecond bug', we test if DURATIONS and TIMEOUTS
 * are unreasonably low...such as 10 milliseconds (TPM isn't that fast).
 * and 400 milliseconds for long duration
 */
#define	TEN_MILLISECONDS		10000	/* 10 milliseconds */
#define	FOUR_HUNDRED_MILLISECONDS	400000	/* 4 hundred milliseconds */

/*
 * Historically, only one connection has been allowed to TPM1.2 devices,
 * with tssd (or equivalent) arbitrating access between multiple clients.
 */
#define	TPM12_CLIENT_MAX	1

#define	TPM_TAG_RQU_COMMAND	((uint16_t)0x00c1)

/* The TPM1.2 Commands we are using */
#define		TPM_ORD_GetCapability		0x00000065u
#define		TPM_ORD_ContinueSelfTest	0x00000053u
#define		TPM_ORD_GetRandom		0x00000046u
#define		TPM_ORD_StirRandom		0x00000047u

#define		TPM_CAP_PROPERTY		0x00000005u
#define		TPM_CAP_PROP_TIS_TIMEOUT	0x00000115u
#define		TPM_CAP_PROP_DURATION		0x00000120u

#define		TPM_CAP_VERSION_VAL		0x0000001au

/* The maximum amount of bytes allowed for TPM_ORD_StirRandom */
#define	TPM12_SEED_MAX		255

/*
 * This is to address some TPMs that does not report the correct duration
 * and timeouts.  In our experience with the production TPMs, we encountered
 * time errors such as GetCapability command from TPM reporting the timeout
 * and durations in milliseconds rather than microseconds.  Some other TPMs
 * report the value 0's
 *
 * Short Duration is based on section 11.3.4 of TIS specifiation, that
 * TPM_GetCapability (short duration) commands should not be longer than 750ms
 * and that section 11.3.7 states that TPM_ContinueSelfTest (medium duration)
 * should not be longer than 1 second.
 */
#define	DEFAULT_SHORT_DURATION	750000
#define	DEFAULT_MEDIUM_DURATION	1000000
#define	DEFAULT_LONG_DURATION	300000000
#define	DEFAULT_TIMEOUT_A	750000
#define	DEFAULT_TIMEOUT_B	2000000
#define	DEFAULT_TIMEOUT_C	750000
#define	DEFAULT_TIMEOUT_D	750000

/*
 * TPM input/output buffer offsets
 */

typedef enum {
	TPM_CAP_RESPSIZE_OFFSET = 10,
	TPM_CAP_RESP_OFFSET = 14,
} TPM_CAP_RET_OFFSET_T;

typedef enum {
	TPM_CAP_TIMEOUT_A_OFFSET = 14,
	TPM_CAP_TIMEOUT_B_OFFSET = 18,
	TPM_CAP_TIMEOUT_C_OFFSET = 22,
	TPM_CAP_TIMEOUT_D_OFFSET = 26,
} TPM_CAP_TIMEOUT_OFFSET_T;

typedef enum {
	TPM_CAP_DUR_SHORT_OFFSET = 14,
	TPM_CAP_DUR_MEDIUM_OFFSET = 18,
	TPM_CAP_DUR_LONG_OFFSET = 22,
} TPM_CAP_DURATION_OFFSET_T;

#define	TPM_CAP_VERSION_INFO_OFFSET	14
#define	TPM_CAP_VERSION_INFO_SIZE	15

typedef struct {
	uint16_t	tpmcap_tag;
	uint8_t		tpmcap_major;
	uint8_t		tpmcap_minor;
	uint8_t		tpmcap_rev_major;
	uint8_t		tpmcap_rev_minor;
	uint16_t	tpmcap_spec_level;
	uint8_t		tpmcap_errata_level;
	uint8_t		tpmcap_vendorid[5];
} __packed tpm12_vers_info_t;

#define	TPM_CAP_VERSION_INFO_MAJOR	(TPM_CAP_VERSION_INFO_OFFSET + 2)
#define	TPM_CAP_VERSION_INFO_MINOR	(TPM_CAP_VERSION_INFO_OFFSET + 3)
#define	TPM_CAP_VERSION_INFO_REVMAJOR	(TPM_CAP_VERSION_INFO_OFFSET + 4)
#define	TPM_CAP_VERSION_INFO_REVMINOR	(TPM_CAP_VERSION_INFO_OFFSET + 5)
#define	TPM_CAP_VERSION_INFO_SPEC	(TPM_CAP_VERSION_INFO_OFFSET + 6)
#define	TPM_CAP_VERSION_INFO_ERRATA	(TPM_CAP_VERSION_INFO_OFFSET + 8)
#define	TPM_CAP_VERSION_INFO_VENDORID	(TPM_CAP_VERSION_INFO_OFFSET + 9)

/* TSC Ordinals */
static const tpm_duration_t tpm12_ords_duration[TPM_ORDINAL_MAX] = {
	TPM_UNDEFINED,		/* 0 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 5 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 10 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_LONG,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_SHORT,		/* 20 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_SHORT,		/* 25 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,		/* 30 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 35 */
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 40 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 45 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_LONG,
	TPM_MEDIUM,		/* 50 */
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 55 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 60 */
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,		/* 65 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 70 */
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 75 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_LONG,		/* 80 */
	TPM_UNDEFINED,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 85 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 90 */
	TPM_LONG,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 95 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 100 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 105 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 110 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 115 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_LONG,		/* 120 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_SHORT,		/* 125 */
	TPM_SHORT,
	TPM_LONG,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,		/* 130 */
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_UNDEFINED,		/* 135 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 140 */
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 145 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 150 */
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 155 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 160 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 165 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_LONG,		/* 170 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 175 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_MEDIUM,		/* 180 */
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,		/* 185 */
	TPM_SHORT,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 190 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 195 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 200 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_SHORT,		/* 205 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_MEDIUM,		/* 210 */
	TPM_UNDEFINED,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_MEDIUM,
	TPM_UNDEFINED,		/* 215 */
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,
	TPM_SHORT,		/* 220 */
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_SHORT,
	TPM_UNDEFINED,		/* 225 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 230 */
	TPM_LONG,
	TPM_MEDIUM,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,		/* 235 */
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_UNDEFINED,
	TPM_SHORT,		/* 240 */
	TPM_UNDEFINED,
	TPM_MEDIUM,
};

/*
 * Get the actual timeouts supported by the TPM by issuing TPM_GetCapability
 * with the subcommand TPM_CAP_PROP_TIS_TIMEOUT
 * TPM_GetCapability (TPM Main Part 3 Rev. 94, pg.38)
 */
static int
tpm12_get_timeouts(tpm_t *tpm)
{
	tpm_cmd_t *cmd = &tpm->tpm_cmd;
	int ret;
	uint32_t timeout;   /* in milliseconds */
	uint32_t len;

	tpm_cmd_init(cmd, TPM_ORD_GetCapability, TPM_TAG_RQU_COMMAND);
	tpm_cmd_put32(cmd, TPM_CAP_PROPERTY);
	tpm_cmd_put32(cmd, sizeof (uint32_t));
	tpm_cmd_put32(cmd, TPM_CAP_PROP_TIS_TIMEOUT);

	ret = tpm_exec_cmd(tpm, NULL, cmd);
	if (ret != 0) {
		/* XXX: ereport? */
		dev_err(tpm->tpm_dip, CE_WARN, "%s: command failed: %d",
		    __func__, ret);
		return (ret);
	}

	/*
	 * Get the length of the returned buffer
	 * Make sure that there are 4 timeout values returned
	 * length of the capability response is stored in data[10-13]
	 * Also the TPM is in network byte order
	 */
	len = tpm_cmdlen(cmd);
	if (len != 4 * sizeof (uint32_t)) {
		dev_err(tpm->tpm_dip, CE_WARN, "%s: incorrect capability "
		    "response size: expected %d received %u", __func__,
		    (int)(4 * sizeof (uint32_t)), len);
		return (EIO);
	}

	/* Get the four timeout's: a,b,c,d (they are 4 bytes long each) */
	timeout = tpm_getbuf32(cmd, TPM_CAP_TIMEOUT_A_OFFSET);
	if (timeout == 0) {
		timeout = DEFAULT_TIMEOUT_A;
	} else if (timeout < TEN_MILLISECONDS) {
		/* timeout is in millisecond range (should be microseconds) */
		timeout *= 1000;
	}
	tpm->tpm_timeout_a = drv_usectohz(timeout);

	timeout = tpm_getbuf32(cmd, TPM_CAP_TIMEOUT_B_OFFSET);
	if (timeout == 0) {
		timeout = DEFAULT_TIMEOUT_B;
	} else if (timeout < TEN_MILLISECONDS) {
		/* timeout is in millisecond range (should be microseconds) */
		timeout *= 1000;
	}
	tpm->tpm_timeout_b = drv_usectohz(timeout);

	timeout = tpm_getbuf32(cmd, TPM_CAP_TIMEOUT_C_OFFSET);
	if (timeout == 0) {
		timeout = DEFAULT_TIMEOUT_C;
	} else if (timeout < TEN_MILLISECONDS) {
		/* timeout is in millisecond range (should be microseconds) */
		timeout *= 1000;
	}
	tpm->tpm_timeout_c = drv_usectohz(timeout);

	timeout = tpm_getbuf32(cmd, TPM_CAP_TIMEOUT_D_OFFSET);
	if (timeout == 0) {
		timeout = DEFAULT_TIMEOUT_D;
	} else if (timeout < TEN_MILLISECONDS) {
		/* timeout is in millisecond range (should be microseconds) */
		timeout *= 1000;
	}
	tpm->tpm_timeout_d = drv_usectohz(timeout);

	return (0);
}

/*
 * Get the actual timeouts supported by the TPM by issuing TPM_GetCapability
 * with the subcommand TPM_CAP_PROP_DURATION
 * TPM_GetCapability (TPM Main Part 3 Rev. 94, pg.38)
 */
static int
tpm12_get_duration(tpm_t *tpm)
{
	tpm_cmd_t *cmd = &tpm->tpm_cmd;
	int ret;
	uint32_t duration;
	uint32_t len;

	tpm_cmd_init(cmd, TPM_ORD_GetCapability, TPM_TAG_RQU_COMMAND);
	tpm_cmd_put32(cmd, sizeof (uint32_t));
	tpm_cmd_put32(cmd, TPM_CAP_PROP_DURATION);

	ret = tpm_exec_cmd(tpm, NULL, cmd);
	if (ret != 0) {
		dev_err(tpm->tpm_dip, CE_WARN, "%s: command failed: %d",
		    __func__, ret);
		return (EIO);
	}

	/*
	 * Get the length of the returned buffer
	 * Make sure that there are 3 duration values (S,M,L: in that order)
	 * length of the capability response is stored in data[10-13]
	 * Also the TPM is in network byte order
	 */
	len = tpm_getbuf32(cmd, TPM_CAP_RESPSIZE_OFFSET);
	if (len != 3 * sizeof (uint32_t)) {
		dev_err(tpm->tpm_dip, CE_WARN, "%s: incorrect capability "
		    "response size: expected %d received %u", __func__,
		    (int)(3 * sizeof (uint32_t)), len);
		return (EIO);
	}

	duration = tpm_getbuf32(cmd, TPM_CAP_DUR_SHORT_OFFSET);
	if (duration == 0) {
		duration = DEFAULT_SHORT_DURATION;
	} else if (duration < TEN_MILLISECONDS) {
		duration *= 1000;
	}
	tpm->tpm_duration[TPM_SHORT] = drv_usectohz(duration);

	duration = tpm_getbuf32(cmd, TPM_CAP_DUR_MEDIUM_OFFSET);
	if (duration == 0) {
		duration = DEFAULT_MEDIUM_DURATION;
	} else if (duration < TEN_MILLISECONDS) {
		duration *= 1000;
	}
	tpm->tpm_duration[TPM_MEDIUM] = drv_usectohz(duration);

	duration = tpm_getbuf32(cmd, TPM_CAP_DUR_LONG_OFFSET);
	if (duration == 0) {
		duration = DEFAULT_LONG_DURATION;
	} else if (duration < FOUR_HUNDRED_MILLISECONDS) {
		duration *= 1000;
	}
	tpm->tpm_duration[TPM_LONG] = drv_usectohz(duration);

	/* Just make the undefined duration be the same as the LONG */
	tpm->tpm_duration[TPM_UNDEFINED] = tpm->tpm_duration[TPM_LONG];

	return (0);
}

static int
tpm12_get_version(tpm_t *tpm, tpm12_vers_info_t *vp)
{
	tpm_cmd_t *cmd = &tpm->tpm_cmd;
	int ret;
	uint32_t len;

	tpm_cmd_init(cmd, TPM_ORD_GetCapability, TPM_TAG_RQU_COMMAND);
	tpm_cmd_put32(cmd, TPM_CAP_VERSION_VAL);
	tpm_cmd_put32(cmd, 0); /* Sub cap size */

	ret = tpm_exec_cmd(tpm, NULL, cmd);
	if (ret != DDI_SUCCESS) {
		dev_err(tpm->tpm_dip, CE_WARN, "%s: command failed: %d",
		    __func__, ret);
		return (ret);
	}

	/*
	 * Get the length of the returned buffer.
	 */
	len = tpm_getbuf32(cmd, TPM_CAP_RESPSIZE_OFFSET);
	if (len < TPM_CAP_VERSION_INFO_SIZE) {
		dev_err(tpm->tpm_dip, CE_WARN,
		    "%s: unexpected response length; expected %d actual %d",
		    __func__, TPM_CAP_VERSION_INFO_SIZE, len);
		return (EIO);
	}

	tpm_cmd_getbuf(cmd, TPM_CAP_VERSION_INFO_OFFSET,
	    TPM_CAP_VERSION_INFO_SIZE, vp);
	vp->tpmcap_vendorid[4] = '\0';

	dev_err(tpm->tpm_dip, CE_NOTE,
	    "!TPM Version %d.%d Revision %d.%d SpecLevel %d, Errata Rev %d "
	    "VendorId '%s'", vp->tpmcap_major, vp->tpmcap_minor,
	    vp->tpmcap_rev_major, vp->tpmcap_rev_minor, vp->tpmcap_spec_level,
	    vp->tpmcap_errata_level, vp->tpmcap_vendorid);

	return (0);
}

tpm_duration_t
tpm12_get_duration_type(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	uint32_t ordinal = tpm_cc(cmd);

	if (ordinal >= TPM_ORDINAL_MAX) {
		return (TPM_UNDEFINED);
	}

	return (tpm12_ords_duration[ordinal]);
}

clock_t
tpm12_get_timeout(tpm_t *tpm, uint32_t cmd)
{
	VERIFY3U(cmd, <, TPM_ORDINAL_MAX);

	return (tpm->tpm_duration[tpm12_ords_duration[cmd]]);
}

/*
 * To prevent the TPM from complaining that certain functions are not tested
 * we run this command when the driver attaches.
 * For details see Section 4.2 of TPM Main Part 3 Command Specification
 */
static int
tpm12_continue_selftest(tpm_t *tpm)
{
	tpm_cmd_t *cmd = &tpm->tpm_cmd;
	int ret;

	tpm_cmd_init(cmd, TPM_ORD_ContinueSelfTest, TPM_TAG_RQU_COMMAND);
	ret = tpm_exec_cmd(tpm, NULL, cmd);

	if (ret != DDI_SUCCESS) {
		dev_err(tpm->tpm_dip, CE_WARN, "%s: command timed out",
		    __func__);
		return (ret);
	}

	return (DDI_SUCCESS);
}

int
tpm12_seed_random(tpm_client_t *c, uchar_t *buf, size_t buflen)
{
	tpm_cmd_t *cmd = &c->tpmc_cmd;
	int ret;

	if (buflen == 0 || buflen > TPM12_SEED_MAX || buf == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	mutex_enter(&c->tpmc_lock);
	VERIFY3S(c->tpmc_state, ==, TPM_CLIENT_IDLE);

	tpm_cmd_init(cmd, TPM_ORD_StirRandom, TPM_TAG_RQU_COMMAND);
	tpm_cmd_put32(cmd, buflen);
	tpm_cmd_copy(cmd, buf, buflen);

	ret = tpm_exec_internal(c);
	/* Timeout reached */
	if (ret != 0) {
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		tpm_dbg(c->tpmc_tpm, CE_WARN, "!%s failed", __func__);
		return (CRYPTO_FAILED);
	}

	tpm_client_reset(c);
	mutex_exit(&c->tpmc_lock);
	return (CRYPTO_SUCCESS);
}

#define	RNDHDR_SIZE	(TPM_HEADER_SIZE + sizeof (uint32_t))

int
tpm12_generate_random(tpm_client_t *c, uchar_t *buf, size_t buflen)
{
	tpm_cmd_t	*cmd = &c->tpmc_cmd;
	uint32_t	amt;
	int		ret;

	if (buflen == 0 || buf == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	mutex_enter(&c->tpmc_lock);
	VERIFY3S(c->tpmc_state, ==, TPM_CLIENT_IDLE);

	tpm_cmd_init(cmd, TPM_ORD_GetRandom, TPM_TAG_RQU_COMMAND);
	tpm_cmd_put32(cmd, buflen);
	ret = tpm_exec_internal(c);

	if (ret != 0) {
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);

		switch (ret) {
		case ETIME:
			return (CRYPTO_BUSY);
		default:
			return (CRYPTO_FAILED);
		}
	}

	amt = tpm_getbuf32(cmd, TPM_HEADER_SIZE);
	if (amt < buflen) {
		tpm_client_reset(c);
		mutex_exit(&c->tpmc_lock);
		return (CRYPTO_FAILED);
	}

	tpm_cmd_getbuf(cmd, TPM_HEADER_SIZE + sizeof (uint32_t), buflen, buf);
	tpm_client_reset(c);
	mutex_exit(&c->tpmc_lock);

	/* XXX: Do we need to check the header for an error? */
	return (CRYPTO_SUCCESS);
}

/*
 * Initialize TPM1.2 device
 * 1. Find out supported interrupt capabilities
 * 2. Set up interrupt handler if supported (some BIOSes don't support
 * interrupts for TPMS, in which case we set up polling)
 * 3. Determine timeouts and commands duration
 */
bool
tpm12_init(tpm_t *tpm)
{
	tpm12_vers_info_t vers_info = { 0 };
	uint32_t intf_caps;
	int ret;

	/* For legacy TPM1.2 devices, we only support a single client */
	tpm->tpm_client_max = 1;

	/*
	 * Temporarily set up timeouts before we get the real timeouts
	 * by issuing TPM_CAP commands (but to issue TPM_CAP commands,
	 * you need TIMEOUTs defined...chicken and egg problem here.
	 * TPM timeouts: Convert the milliseconds to clock cycles
	 */
	tpm->tpm_timeout_a = drv_usectohz(TIS_TIMEOUT_A);
	tpm->tpm_timeout_b = drv_usectohz(TIS_TIMEOUT_B);
	tpm->tpm_timeout_c = drv_usectohz(TIS_TIMEOUT_C);
	tpm->tpm_timeout_d = drv_usectohz(TIS_TIMEOUT_D);
	/*
	 * Do the same with the duration (real duration will be filled out
	 * when we call TPM_GetCapability to get the duration values from
	 * the TPM itself).
	 */
	tpm->tpm_duration[TPM_SHORT] = drv_usectohz(TPM_DEFAULT_DURATION);
	tpm->tpm_duration[TPM_MEDIUM] = drv_usectohz(TPM_DEFAULT_DURATION);
	tpm->tpm_duration[TPM_LONG] = drv_usectohz(TPM_DEFAULT_DURATION);
	tpm->tpm_duration[TPM_UNDEFINED] = drv_usectohz(TPM_DEFAULT_DURATION);

	/* Find out supported capabilities */
	intf_caps = tpm_get32(tpm, TPM_INTF_CAP);

	if ((intf_caps & ~(TPM_INTF_MASK)) != 0) {
		dev_err(tpm->tpm_dip, CE_WARN, "%s: bad intf_caps value 0x%0x",
		    __func__, intf_caps);
		return (false);
	}

	/* These two interrupts are mandatory */
	if (!(intf_caps & TPM_INTF_INT_LOCALITY_CHANGE_INT)) {
		dev_err(tpm->tpm_dip, CE_WARN,
		    "%s: mandatory capability locality change interrupt "
		    "not supported", __func__);
		return (false);
	}
	if (!(intf_caps & TPM_INTF_INT_DATA_AVAIL_INT)) {
		dev_err(tpm->tpm_dip, CE_WARN,
		    "%s: mandatory capability data available interrupt "
		    "not supported.", __func__);
		return (false);
	}

	tpm->tpm_timeout_poll = drv_usectohz(TPM_POLLING_TIMEOUT);
	tpm->tpm_use_interrupts = false;

	/* Get the real timeouts from the TPM */
	ret = tpm12_get_timeouts(tpm);
	if (ret != DDI_SUCCESS) {
		return (false);
	}

	ret = tpm12_get_duration(tpm);
	if (ret != DDI_SUCCESS) {
		return (false);
	}

	/* This gets the TPM version information */
	ret = tpm12_get_version(tpm, &vers_info);
	if (ret != DDI_SUCCESS) {
		return (false);
	}


	/* XXX: Cleanup */
	char buf[32];

	(void) snprintf(buf, sizeof (buf), "%d.%d",
	    vers_info.tpmcap_major,
	    vers_info.tpmcap_minor);
	(void) ddi_prop_update_string(DDI_DEV_T_NONE, tpm->tpm_dip,
	    "tpm-version", buf);

	tpm->tpm_fw_major = vers_info.tpmcap_rev_major;
	tpm->tpm_fw_minor = vers_info.tpmcap_rev_minor;

	(void) snprintf(buf, sizeof (buf), "%d.%d",
	    vers_info.tpmcap_rev_major,
	    vers_info.tpmcap_rev_minor);
	(void) ddi_prop_update_string(DDI_DEV_T_NONE, tpm->tpm_dip,
	    "tpm-revision", buf);

	(void) ddi_prop_update_int(DDI_DEV_T_NONE, tpm->tpm_dip,
	    "tpm-speclevel", ntohs(vers_info.tpmcap_spec_level));
	(void) ddi_prop_update_int(DDI_DEV_T_NONE, tpm->tpm_dip,
	    "tpm-errata-revision", vers_info.tpmcap_errata_level);

	/*
	 * Unless the TPM completes the test of its commands,
	 * it can return an error when the untested commands are called
	 */
	ret = tpm12_continue_selftest(tpm);

	return ((ret == 0) ? true : false);
}
