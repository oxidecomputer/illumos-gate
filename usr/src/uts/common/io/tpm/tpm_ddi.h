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
 * Copyright 2025 RackTop Systems, Inc.
 */

#ifndef	_TPM_DDI_H
#define	_TPM_DDI_H

#include <stdbool.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/crypto/spi.h>
#include <sys/debug.h>
#include <sys/ksynch.h>
#include <sys/list.h>
#include <sys/byteorder.h>
#include <sys/refhash.h>
#include "tpm_tis.h"

/* Duration index is SHORT, MEDIUM, LONG, UNDEFINED */
#define	TPM_DURATION_MAX_IDX	3

/*
 * IO buffer size: this seems sufficient, but feel free to modify
 * This should be at minimum 765
 */
#define	TPM_IO_BUF_SIZE		4096
#define	TPM_IO_TIMEOUT		10000000

/*
 * The byte offsets of various fields in a TPM command. These are the same
 * for TPM1.2 and TPM2.0. The header size is also the same.
 */
#define	TPM_HEADER_SIZE			10

#define	TPM_TAG_OFFSET			0
#define	TPM_PARAMSIZE_OFFSET		2
#define	TPM_RETURN_OFFSET		6
#define	TPM_COMMAND_CODE_OFFSET		6

#define	TPM12_ORDINAL_MAX	243
#define	TPM_LOCALITY_MAX	4
#define	TPM_OFFSET_MAX		0x0fff

#define	DEFAULT_LOCALITY	0

/*
 * TPM interface methods. TPM_IF_TIS and TPM_IF_FIFO are effectively
 * identical except that TPM_IF_FIFO supports the TPM_INTF_CAPABILITY_x
 * registers.
 */
typedef enum tpm_if_t {
	TPM_IF_TIS,	/* TPM 1.2 and TPM 2.0 */
	TPM_IF_FIFO,	/* TPM 2.0 only */
	TPM_IF_CRB,	/* TPM 2.0 only */
} tpm_if_t;

typedef enum tpm_family {
	TPM_FAMILY_1_2,
	TPM_FAMILY_2_0,
} tpm_family_t;

/* From section 6.5.2.5.1 */
typedef enum tpm_tis_state {
	TPMT_ST_IDLE,
	TPMT_ST_READY,
	TPMT_ST_CMD_RECEPTION,
	TPMT_ST_CMD_EXECUTION,
	TPMT_ST_CMD_COMPLETION,
	TPMT_ST_MAX			/* Must be last */
} tpm_tis_state_t;

typedef enum tpm_tis_xfer_size {
	TPM_TIS_XFER_LEGACY = 0,
	TPM_TIS_XFER_8,
	TPM_TIS_XFER_32,
	TPM_TIS_XFER_64,
} tpm_tis_xfer_size_t;

typedef enum tpm_duration {
	TPM_SHORT,
	TPM_MEDIUM,
	TPM_LONG,
	TPM_UNDEFINED,
	TPM_DURATION_MAX	/* Must be last */
} tpm_duration_t;

/* TIS/FIFO specific data */
typedef struct tpm_tis {
	tpm_tis_state_t		ttis_state;		/* RW */
	tpm_tis_xfer_size_t	ttis_xfer_size;		/* WO */
	uint32_t		ttis_intr;
	bool			ttis_has_sts_valid_int;	/* WO */
	bool			ttis_has_cmd_ready_int;	/* WO */
} tpm_tis_t;

/*
 * From PC-Client-Specific-Platform-TPM-Profile 6.5.3.8
 *
 * Note that while the diagram does include a TPM_Init state, the system
 * firmware should always transition the TPM out of that state long before the
 * kernel ever has a chance to access the TPM.
 */
typedef enum tpm_crb_state {
	TCRB_ST_IDLE,
	TCRB_ST_READY,
	TCRB_ST_CMD_RECEPTION,
	TCRB_ST_CMD_EXECUTION,
	TCRB_ST_CMD_COMPLETION,
	TCRB_ST_MAX			/* Must be last */
} tpm_crb_state_t;

/* CRB Interface specific data, protected by tpm_t.tpm_lock */
typedef struct tpm_crb {
	tpm_crb_state_t		tcrb_state;		/* RW */

	uint64_t		tcrb_cmd_off[TPM_LOCALITY_MAX];	/* WO */
	uint64_t		tcrb_resp_off[TPM_LOCALITY_MAX]; /* WO */
	uint32_t		tcrb_cmd_size[TPM_LOCALITY_MAX]; /* WO */
	uint32_t		tcrb_resp_size[TPM_LOCALITY_MAX]; /* WO */
	bool			tcrb_idle_bypass;	/* WO */
} tpm_crb_t;

typedef struct tpm_cmd {
	uint8_t		tcmd_buf[TPM_IO_BUF_SIZE];
} tpm_cmd_t;

/*
 * The TPM can be operated with or without interrupts. Without interrupts
 * enabled, one must write to a register, and then poll periodically (up to
 * a timeout value) for the TPM to set or clear a bit in a register. Using
 * interrupts avoids the need to poll.
 *
 * We offer three modes of waiting for command completion:
 *
 * TPM_WAIT_POLL	Poll every tpm_timeout_poll ms for the desired status.
 *			Fail the request if not complete within the desired
 *			timeout amount. If tpm_timeout_poll is larger than the
 *			the desired timeout, only wait for the desired timeout
 *			amount before checking the status.
 *
 * TPM_WAIT_INTR	Use an interrupt (when supported by the TPM module) to
 *			signal the completion of the request. If the condition
 *			does not support being signaled by an interrupt, poll
 *			instead.
 *
 * TPM_WAIT_POLLONCE	Always wait the full timeout amount before checking
 *			the status of the request.
 *
 * For TPM1.2 devices the default is TPM_WAIT_POLL (to match the historic
 * behavior of the TPM driver). For TPM2.0 devices, it is possible the TPM
 * device can process requests much faster than the timeouts specified by the
 * standard (e.g. software TPMs aka fTPMs that run on the host processor at a
 * special privilege level). As such, the default for TPM2.0 devices is
 * TPM_WAIT_INTR.
 *
 * However, it is currently unknown how vulnerable TPM devices are to
 * timing attacks. At the same time, it is also possible that a given TPM
 * implementation may legitimately be able to process commands faster than
 * the maximum timeouts allowed by the spec without being vulnerable to
 * timing attacks.
 *
 * Since evaluating every TPM model is not realistic, instead we offer
 * an escape hatch. Enabling TPM_WAIT_POLLONCE via the tpm.conf device
 * configuration file will force each request to wait the full timeout amount.
 */
typedef enum tpm_wait {
	TPM_WAIT_POLL,
	TPM_WAIT_INTR,
	TPM_WAIT_POLLONCE,
} tpm_wait_t;

typedef enum tpm_attach_seq {
	TPM_ATTACH_FM =		0,
	TPM_ATTACH_REGS,
	TPM_ATTACH_INTR_ALLOC,
	TPM_ATTACH_INTR_HDLRS,
	TPM_ATTACH_SYNC,
	TPM_ATTACH_ICLIENT,
	TPM_ATTACH_DEV_INIT,
	TPM_ATTACH_THREAD,
	TPM_ATTACH_MINOR_NODE,
	TPM_ATTACH_KCF,
	TPM_ATTACH_END			/* should always be last */
} tpm_attach_seq_t;
#define	TPM_ATTACH_NUM_ENTRIES	(TPM_ATTACH_END)

typedef struct tpm tpm_t;
typedef struct tpm_client tpm_client_t;

struct tpm {
	dev_info_t		*tpm_dip;
	int			tpm_instance;
	ddi_acc_handle_t	tpm_handle;

	kmutex_t		tpm_suspend_lock;
	kcondvar_t		tpm_suspend_cv;
	bool			tpm_suspended;

	tpm_attach_seq_t	tpm_seq;

	kmutex_t		tpm_lock;
	uint8_t			*tpm_addr;	/* TPM mapped address */
				/*
				 * TPM client count does not include the
				 * internal client.
				 */
	uint_t			tpm_client_count;	/* RW */
	uint_t			tpm_client_max;		/* RW */

	ddi_intr_handle_t	*tpm_harray;		/* WO */
	int			tpm_nintr;		/* WO */
	uint_t			tpm_intr_pri;		/* WO */
	tpm_wait_t		tpm_wait;		/* WO */
	bool			tpm_use_interrupts;	/* WO */

	kthread_t		*tpm_thread;		/* WO */
	kcondvar_t		tpm_thr_cv;
	bool			tpm_thr_quit;		/* RW */
	bool			tpm_thr_cancelreq;	/* RW */
	list_t			tpm_pending;		/* RW */
	tpm_client_t		*tpm_active;		/* RW */

	tpm_family_t		tpm_family;		/* WO */
	tpm_if_t		tpm_iftype;		/* WO */
	union {
		tpm_tis_t	tpmu_tis;
		tpm_crb_t	tpmu_crb;
	} tpm_u;
	uint16_t		tpm_vid;		/* WO */
	uint16_t		tpm_did;		/* WO */
	uint8_t			tpm_rid;		/* WO */

	uint32_t		tpm_fw_major;		/* WO */
	uint32_t		tpm_fw_minor;		/* WO */

	tpm_cmd_t		tpm_cmd;		/* during exec */
	int8_t			tpm_locality;	/* locality during cmd exec */
	uint8_t			tpm_n_locality;

	clock_t			tpm_timeout_a;		/* WO */
	clock_t			tpm_timeout_b;		/* WO */
	clock_t			tpm_timeout_c;		/* WO */
	clock_t			tpm_timeout_d;		/* WO */
	clock_t			tpm_timeout_poll;	/* WO */
	clock_t			tpm20_timeout_create;	/* WO */
	clock_t			tpm20_timeout_default;	/* WO */
	clock_t			tpm_duration[TPM_DURATION_MAX]; /* WO */
	clock_t			tpm_poll_interval;	/* WO */

	ddi_intr_handle_t	tpm_isr;

	crypto_kcf_provider_handle_t	tpm_n_prov;

	tpm_client_t		*tpm_internal_client;
	int			tpm_fm_capabilities;
	ddi_device_acc_attr_t	tpm_acc_attr;
};

typedef enum tpm_mode {
	TPM_MODE_RDONLY =	0,
	TPM_MODE_WRITE =	(1 << 0),
	TPM_MODE_NONBLOCK =	(1 << 1),
} tpm_mode_t;

/*
 * A client normally cycles through these states in the order they are listed.
 * However, errors will cancel any pending operations and reset the client
 * state back to TPM_CLIENT_IDLE.
 */
typedef enum tpm_client_state {
	TPM_CLIENT_IDLE,		/* No command in progress */
	TPM_CLIENT_CMD_RECEPTION,	/* Reading command from client */
	TPM_CLIENT_CMD_DISPATCH,	/* Command has been dispatched */
	TPM_CLIENT_CMD_EXECUTION,	/* Command is running on TPM */
	TPM_CLIENT_CMD_COMPLETION,	/* Write command to client */
} tpm_client_state_t;

struct tpm_client {
	refhash_link_t		tpmc_reflink;
	list_node_t		tpmc_node;

	kmutex_t		tpmc_lock;
	kcondvar_t		tpmc_cv;
	tpm_t			*tpmc_tpm;		/* Write once (WO) */
	int			tpmc_minor;		/* WO */
	tpm_mode_t		tpmc_mode;		/* WO */
	tpm_client_state_t	tpmc_state;		/* RW */
	pollhead_t		tpmc_pollhead;		/* RW */
	tpm_cmd_t		tpmc_cmd;		/* RW */
	uint32_t		tpmc_bufused;		/* RW */
	uint32_t		tpmc_bufread;		/* RW */
	int			tpmc_instance;		/* WO */
	int8_t			tpmc_locality;		/* RW */
	int			tpmc_cmdresult;		/* RW */
	bool			tpmc_closing;		/* WO */
	bool			tpmc_iskernel;		/* WO */
};

static inline bool
tpm_client_nonblock(const tpm_client_t *c)
{
	return ((c->tpmc_mode & TPM_MODE_NONBLOCK) != 0 ? true : false);
}

/*
 * We can access the TPM device (read/write registers) if we haven't
 * started the TPM thread yet or if we're executing on the TPM thread.
 */
static inline bool
tpm_can_access(const tpm_t *tpm)
{
	return (tpm->tpm_thread == NULL || curthread == tpm->tpm_thread);
}

uint32_t tpm_cc(const tpm_cmd_t *);
uint32_t tpm_cmdlen(const tpm_cmd_t *);
uint16_t tpm_tag(const tpm_cmd_t *);
uint16_t tpm_getbuf16(const tpm_cmd_t *, uint32_t);
uint32_t tpm_getbuf32(const tpm_cmd_t *, uint32_t);
uint16_t tpm_cmd_sess(const tpm_cmd_t *);
uint32_t tpm_cmd_rc(const tpm_cmd_t *);
void tpm_cmd_getbuf(const tpm_cmd_t *, uint32_t, uint32_t, void *);
void tpm_cmd_init(tpm_cmd_t *, uint32_t, uint16_t);
void tpm_cmd_resp(tpm_cmd_t *, uint32_t, uint16_t);
void tpm_cmd_put8(tpm_cmd_t *, uint8_t);
void tpm_cmd_put16(tpm_cmd_t *, uint16_t);
void tpm_cmd_put32(tpm_cmd_t *, uint32_t);
void tpm_cmd_copy(tpm_cmd_t *, const void *, uint32_t);

/*
 * Some operations do not generate an interrupt on completion.
 * For those, we want to translate TPM_WAIT_INTR to TPM_WAIT_POLL.
 */
static inline tpm_wait_t
tpm_wait_nointr(const tpm_t *tpm)
{
	if (tpm->tpm_wait == TPM_WAIT_INTR)
		return (TPM_WAIT_POLL);
	return (tpm->tpm_wait);
}

extern bool tpm_debug;

void *tpm_reg_addr(const tpm_t *, int8_t, unsigned long);
uint8_t tpm_get8(tpm_t *, unsigned long);
uint8_t tpm_get8_loc(tpm_t *, int8_t, unsigned long);
uint32_t tpm_get32(tpm_t *, unsigned long);
uint32_t tpm_get32_loc(tpm_t *, int8_t, unsigned long);
uint64_t tpm_get64(tpm_t *, unsigned long);
void tpm_put8(tpm_t *, unsigned long, uint8_t);
void tpm_put32(tpm_t *, unsigned long, uint32_t);
void tpm_put32_loc(tpm_t *, int8_t, unsigned long, uint32_t);

int tpm_wait(tpm_t *, bool (*)(tpm_t *, bool, clock_t, const char *), clock_t,
    bool, const char *);
int tpm_wait_cmd(tpm_t *, const tpm_cmd_t *,
    bool(*)(tpm_t *, bool, uint32_t, clock_t, const char *), const char *);

tpm_duration_t tpm_get_duration_type(tpm_t *, const tpm_cmd_t *);
clock_t tpm_get_duration(tpm_t *, const tpm_cmd_t *);
clock_t tpm_get_timeout(tpm_t *, const tpm_cmd_t *);

void tpm_client_refhold(tpm_client_t *);
void tpm_client_refrele(tpm_client_t *);
void tpm_client_reset(tpm_client_t *);

int tpm_cancel(tpm_client_t *);
void tpm_dbg(const tpm_t *, int, const char *, ...);
void tpm_dispatch_cmd(tpm_client_t *);
void tpm_exec_thread(void *);

void tpm_int_newcmd(tpm_client_t *, uint16_t, uint32_t);
void tpm_int_put8(tpm_client_t *, uint8_t);
void tpm_int_put16(tpm_client_t *, uint16_t);
void tpm_int_put32(tpm_client_t *, uint32_t);
void tpm_int_copy(tpm_client_t *, const void *, size_t);
uint32_t tpm_int_rc(tpm_client_t *);

int tpm_exec_client(tpm_client_t *);
int tpm_exec_internal(tpm_client_t *);
int tpm_exec_cmd(tpm_t *, tpm_client_t *, tpm_cmd_t *);

size_t tpm_uio_size(const uio_t *);

const char *tpm_iftype_str(tpm_if_t);
const char *tpm_hwvend_str(uint16_t);

int tpm12_seed_random(tpm_client_t *, uchar_t *, size_t);
int tpm12_generate_random(tpm_client_t *, uchar_t *, size_t);
bool tpm12_init(tpm_t *);
tpm_duration_t tpm12_get_duration_type(tpm_t *, const tpm_cmd_t *);
clock_t tpm12_get_timeout(tpm_t *, uint32_t);

bool tpm20_init(tpm_t *);
int tpm20_seed_random(tpm_client_t *, uchar_t *, size_t);
int tpm20_generate_random(tpm_client_t *, uchar_t *, size_t);
tpm_duration_t tpm20_get_duration_type(tpm_t *, const tpm_cmd_t *);
clock_t tpm20_get_timeout(tpm_t *, const tpm_cmd_t *);

bool tpm_tis_init(tpm_t *);
int tis_exec_cmd(tpm_t *, uint8_t, tpm_cmd_t *);
void tis_cancel_cmd(tpm_t *, tpm_duration_t);
void tpm_tis_intr_mgmt(tpm_t *, bool);
uint_t tpm_tis_intr(caddr_t, caddr_t);

bool crb_init(tpm_t *);
int crb_exec_cmd(tpm_t *, uint8_t, tpm_cmd_t *);
void crb_cancel_cmd(tpm_t *, tpm_duration_t);
void crb_intr_mgmt(tpm_t *, bool);
uint_t crb_intr(caddr_t, caddr_t);

void tpm_ereport_timeout(tpm_t *, uint16_t, clock_t, const char *);
void tpm_ereport_timeout_cmd(tpm_t *, clock_t, const char *);
void tpm_ereport_short_read(tpm_t *, uint32_t, uint32_t, uint32_t);

int tpm_kcf_register(tpm_t *);
int tpm_kcf_unregister(tpm_t *);

#endif	/* _TPM_DDI_H */
