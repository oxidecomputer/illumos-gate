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

#include <sys/devops.h>		/* used by dev_ops */
#include <sys/conf.h>		/* used by dev_ops,cb_ops */
#include <sys/modctl.h>		/* for _init,_info,_fini,mod_* */
#include <sys/ddi.h>		/* used by all entry points */
#include <sys/sunddi.h>		/* used by all entry points */
#include <sys/ddifm.h>		/* various fm definitions */
#include <sys/fm/io/ddi.h>
#include <sys/fm/protocol.h>
#include <sys/cmn_err.h>	/* used for debug outputs */
#include <sys/types.h>		/* used by prop_op, ddi_prop_op */

#include <sys/file.h>		/* used by open, close */
#include <sys/errno.h>		/* used by open,close,read,write */
#include <sys/open.h>		/* used by open,close,read,write */
#include <sys/cred.h>		/* used by open,close,read */
#include <sys/uio.h>		/* used by read */
#include <sys/stat.h>		/* defines S_IFCHR */
#include <sys/poll.h>
#include <sys/id_space.h>
#include <sys/stddef.h>
#include <sys/proc.h>
#include <sys/atomic.h>

#include <sys/byteorder.h>	/* for ntohs, ntohl, htons, htonl */
#include <sys/sysmacros.h>
#include <sys/mkdev.h>
#include <sys/sdt.h>

#include <sys/tpm.h>

#include "tpm_tis.h"
#include "tpm_ddi.h"

#define	TPM_RC_FAILURE		((uint32_t)0x00000101)
#define	TPM2_RC_CANCELED	((uint32_t)0x00000909)
#define	TSS_CANCELED		((uint32_t)0x00000016)

extern bool tpm_debug;

void
tpm_dbg(const tpm_t *tpm, int level, const char *fmt, ...)
{
	if (!tpm_debug) {
		return;
	}

	va_list	ap;

	va_start(ap, fmt);
	if (tpm != NULL && tpm->tpm_dip != NULL) {
		vdev_err(tpm->tpm_dip, level, fmt, ap);
	} else {
		vcmn_err(level, fmt, ap);
	}
	va_end(ap);
}

int
tpm_cancel(tpm_client_t *c)
{
	tpm_t *tpm = c->tpmc_tpm;
	int ret;

	/* We should't be called from the tpm service thread either. */
	VERIFY3P(curthread, !=, tpm->tpm_thread);
	VERIFY(MUTEX_HELD(&c->tpmc_lock));

	switch (c->tpmc_state) {
	case TPM_CLIENT_IDLE:
		return (0);
	case TPM_CLIENT_CMD_RECEPTION:
	case TPM_CLIENT_CMD_COMPLETION:
		tpm_client_reset(c);
		return (0);
	case TPM_CLIENT_CMD_DISPATCH:
		mutex_enter(&tpm->tpm_lock);
		if (list_link_active(&c->tpmc_node)) {
			/*
			 * If we're still on the pending list, the tpm thread
			 * has not started processing our request. We can
			 * merely remove ourself from the list and reset.
			 */
			list_remove(&tpm->tpm_pending, c);
			mutex_exit(&tpm->tpm_lock);

			/* Release reference from list */
			tpm_client_refrele(c);

			tpm_client_reset(c);
			return (0);
		}

		/*
		 * The tpm service thread has pulled us off the list, but
		 * since we were able to acquire tpmc_lock, it has
		 * not been able to transition the client to
		 * TPM_CLIENT_CMD_EXECUTION -- there is a small window
		 * between the service thread dropping tpm_lock after
		 * retrieving the next client and acquiring the client's
		 * tpmc_lock where we could cancel. In this situation,
		 * we just tell the tpm service thread to cancel.
		 *
		 * Note that since we need to wait for acknowledgement
		 * (by the tpm service thread clearing tpm_thr_cancelreq
		 * and signaling us), we leave the switch statement
		 * with tpm_lock held.
		 */
		tpm->tpm_thr_cancelreq = true;
		break;
	case TPM_CLIENT_CMD_EXECUTION:

		/* The tpm thread is busy, so we have to signal it */
		mutex_enter(&tpm->tpm_lock);
		tpm->tpm_thr_cancelreq = true;
		cv_signal(&tpm->tpm_thr_cv);

		break;
	default:
		cmn_err(CE_PANIC, "unexpected tpm connection state 0x%x",
		    c->tpmc_state);
	}

	mutex_exit(&c->tpmc_lock);

	while (tpm->tpm_thr_cancelreq) {
		ret = cv_wait_sig(&tpm->tpm_thr_cv, &tpm->tpm_lock);

		if (ret == 0) {
			mutex_exit(&tpm->tpm_lock);
			return (SET_ERROR(EINTR));
		}
	}
	mutex_exit(&tpm->tpm_lock);

	mutex_enter(&c->tpmc_lock);
	tpm_client_reset(c);
	return (0);
}

void
tpm_dispatch_cmd(tpm_client_t *c)
{
	tpm_t *tpm = c->tpmc_tpm;

	VERIFY(MUTEX_HELD(&c->tpmc_lock));
	VERIFY3S(c->tpmc_state, ==, TPM_CLIENT_CMD_RECEPTION);

	c->tpmc_state = TPM_CLIENT_CMD_DISPATCH;

	mutex_enter(&tpm->tpm_lock);
	tpm_client_refhold(c);			/* ref for svc thread */
	list_insert_tail(&tpm->tpm_pending, c);
	cv_signal(&tpm->tpm_thr_cv);
	mutex_exit(&tpm->tpm_lock);
}

/*
 * A wrapper for the internal client that will dispatch the request,
 * block waiting for the response, and return it.
 */
int
tpm_exec_internal(tpm_client_t *c)
{
	tpm_t *tpm = c->tpmc_tpm;
	int ret;

	ASSERT(MUTEX_HELD(&c->tpmc_lock));
	ASSERT(c->tpmc_iskernel);
	ASSERT3U(tpm_cmdlen(&c->tpmc_cmd), <=, sizeof (c->tpmc_cmd.tcmd_buf));
	ASSERT3P(tpm->tpm_thread, !=, NULL);

	/*
	 * Unlike userland where we might have multiple calls to write(2)
	 * to assemble a full command, the assumption for kernel
	 * consumers is that they will take the client's tpmc_lock and
	 * hold it while assembling and executing the command. As such, we
	 * just unilaterally set the state to TPM_CLIENT_CMD_RECEPTION to
	 * satisify tpm_dispatch_cmd()s expectations even though we don't
	 * use it with kernel consumers.
	 */
	c->tpmc_state = TPM_CLIENT_CMD_RECEPTION;

	/*
	 * We also assume the tpm_cmd_*() functions were used to construct
	 * the command to execute, so we can assume the size from the
	 * header is correct.
	 */
	c->tpmc_bufused = tpm_cmdlen(&c->tpmc_cmd);

	tpm_dispatch_cmd(c);

	while (c->tpmc_state != TPM_CLIENT_CMD_COMPLETION) {
		cv_wait(&c->tpmc_cv, &c->tpmc_lock);
	}

	ret = c->tpmc_cmdresult;
	if (ret != 0) {
		tpm_client_reset(c);
	}

	return (ret);
}

/*
 * Transmit the command to the TPM. This should only be used by the
 * tpm exec thread.
 */
int
tpm_exec_client(tpm_client_t *c)
{
	tpm_t *tpm = c->tpmc_tpm;
	tpm_cmd_t *cmd = &c->tpmc_cmd;
	int ret = 0;

	VERIFY(MUTEX_HELD(&c->tpmc_lock));

	/* We should have the full command, and should be a valid size. */
	VERIFY3U(c->tpmc_bufused, >=, TPM_HEADER_SIZE);
	VERIFY3U(c->tpmc_bufused, ==, tpm_cmdlen(cmd));

	c->tpmc_state = TPM_CLIENT_CMD_EXECUTION;

	mutex_exit(&c->tpmc_lock);

	ret = tpm_exec_cmd(tpm, c, cmd);

	mutex_enter(&c->tpmc_lock);

	c->tpmc_cmdresult = ret;
	c->tpmc_state = TPM_CLIENT_CMD_COMPLETION;

	switch (ret) {
	case 0:
		/*
		 * If we succeeded, the amount of output will be in the
		 * returned header.
		 */
		c->tpmc_bufused = tpm_cmdlen(cmd);
		c->tpmc_bufread = 0;
		break;
	case ECANCELED:
	default:
		c->tpmc_bufused = 0;
		c->tpmc_bufread = 0;
		break;
	}

	return (ret);
}

/*
 * Transmit the given command to the TPM. Should only be called
 * by the service thread or during attach.
 */
int
tpm_exec_cmd(tpm_t *tpm, tpm_client_t *c, tpm_cmd_t *cmd)
{
	uint32_t	cc;
	uint8_t		loc;
	int		ret = 0;

	/*
	 * If we're called without a client, it should be during
	 * attach and we're gathering our initial information from
	 * the tpm.
	 */
	IMPLY((c == NULL), tpm->tpm_thread == NULL);

	loc = (c != NULL) ? c->tpmc_locality : DEFAULT_LOCALITY;

	/*
	 * Stash the command code being run. The result overwrites cmd
	 * and we may want it in case of failure to generate the fma
	 * event.
	 */
	cc = tpm_cc(cmd);

	DTRACE_PROBE2(cmd__exec, tpm_client_t *, c, tpm_cmd_t *, cmd);

	switch (tpm->tpm_iftype) {
	case TPM_IF_TIS:
	case TPM_IF_FIFO:
		ret = tis_exec_cmd(tpm, loc, cmd);
		break;
	case TPM_IF_CRB:
		ret = crb_exec_cmd(tpm, loc, cmd);
		break;
	default:
		dev_err(tpm->tpm_dip, CE_PANIC, "%s: invalid iftype %d",
		    __func__, tpm->tpm_iftype);
	}

	DTRACE_PROBE3(cmd__cmd, tpm_client_t *, c, uint32_t, ret,
	    tpm_cmd_t *, &c->tpmc_cmd);

	/*
	 * If the TPM ever returns TPM_RC_FAILURE, it's dead at least
	 * until it's been reset which means a reboot. Mark it as failed.
	 */
	if (tpm_cmd_rc(cmd) == TPM_RC_FAILURE) {
		uint64_t ena = fm_ena_generate(0, FM_ENA_FMT1);

		ddi_fm_ereport_post(tpm->tpm_dip,
		    DDI_FM_DEVICE "." DDI_FM_DEVICE_INTERN_UNCORR, ena,
		    DDI_SLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0,
		    "tpm_interface", DATA_TYPE_STRING,
		    tpm_iftype_str(tpm->tpm_iftype),
		    "locality", DATA_TYPE_UINT8, loc,
		    "command", DATA_TYPE_UINT32, cc,
		    "detailed error message", DATA_TYPE_STRING,
		    "TPM returned TPM_RC_FAILURE",
		    NULL);

		ddi_fm_service_impact(tpm->tpm_dip, DDI_SERVICE_LOST);
		return (SET_ERROR(EIO));
	}

	return (ret);
}

/*
 * Get the next client to process, blocking if no clients are waiting.
 * Returns the next client to process or NULL if the service thread should
 * exit.
 *
 * Note that a refhold is placed on any client that's been enqueued, so
 * if a client is returned, it is already refheld.
 */
static tpm_client_t *
tpm_get_next_client(tpm_t *tpm)
{
	tpm_client_t *c = NULL;

	/* We should only be invoked on the tpm service thread */
	VERIFY3P(curthread, ==, tpm->tpm_thread);

	mutex_enter(&tpm->tpm_lock);

again:
	while (!tpm->tpm_thr_quit && list_is_empty(&tpm->tpm_pending)) {
		cv_wait(&tpm->tpm_thr_cv, &tpm->tpm_lock);
	}

	if (!tpm->tpm_thr_quit) {
		c = list_remove_head(&tpm->tpm_pending);
		if (c == NULL) {
			/* spurious wakeup, go back to waiting */
			goto again;
		}
	}

	mutex_exit(&tpm->tpm_lock);
	return (c);
}

static bool
tpm_client_is_canceled(tpm_t *tpm, tpm_client_t *c __maybe_unused)
{
	bool canceled = false;

	ASSERT(MUTEX_HELD(&c->tpmc_lock));
	ASSERT3S(c->tpmc_state, ==, TPM_CLIENT_CMD_DISPATCH);

	mutex_enter(&tpm->tpm_lock);
	if (tpm->tpm_thr_cancelreq) {
		/*
		 * Ack the receipt of the cancel by clearing the flag.
		 * See the corresponding logic in tpm_cancel().
		 */
		tpm->tpm_thr_cancelreq = false;
		cv_signal(&tpm->tpm_thr_cv);
		canceled = true;
	}
	mutex_exit(&tpm->tpm_lock);

	return (canceled);
}

void
tpm_exec_thread(void *arg)
{
	tpm_t *tpm = arg;

	for (;;) {
		tpm_client_t	*c = NULL;
		int		ret = 0;
		tpm_duration_t	dur;

		c = tpm_get_next_client(tpm);
		if (c == NULL) {
			VERIFY(tpm->tpm_thr_quit);
			return;
		}

		mutex_enter(&c->tpmc_lock);

		/*
		 * After pulling the next client off the list of requests,
		 * we have to drop tpm_lock so that we can acquire the
		 * client's lock and then re-acquire the tpm_lock. During this
		 * small window where the service thread does not hold any
		 * locks, a client could cancel, so we have to check
		 * once we've re-acquired our locks in the proper order.
		 */
		if (tpm_client_is_canceled(tpm, c)) {
			mutex_exit(&c->tpmc_lock);
			tpm_client_refrele(c);
			continue;
		}

		/*
		 * We need the duration type in case we're cancelled.
		 */
		dur = tpm_get_duration_type(tpm, &c->tpmc_cmd);

		ret = tpm_exec_client(c);
		mutex_exit(&c->tpmc_lock);

		/*
		 * If the request has been cancelled by the caller (either
		 * explicitly via ioctl() or by closing their fd), or we're
		 * in the process of quitting, we want to abort the running
		 * command on the TPM and clean up before we proceed.
		 */
		if (ret == ECANCELED) {
			mutex_enter(&tpm->tpm_lock);

			VERIFY(tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit);
			tpm->tpm_thr_cancelreq = false;
			cv_signal(&tpm->tpm_thr_cv);

			mutex_exit(&tpm->tpm_lock);

			switch (tpm->tpm_iftype) {
			case TPM_IF_TIS:
			case TPM_IF_FIFO:
				tis_cancel_cmd(tpm, dur);
				break;
			case TPM_IF_CRB:
				crb_cancel_cmd(tpm, dur);
				break;
			}
		} else {
			cv_signal(&c->tpmc_cv);

			pollwakeup(&c->tpmc_pollhead, POLLIN|POLLRDNORM);
		}

		tpm_client_refrele(c);
	}
}

/*
 * Wait up to timeout ticks for cond(tpm) to be true. This should be used
 * for conditions where there's no potential concern about the timing used.
 * Basically anything except waiting for a command to complete.
 *
 * If 'intr' is set, this indicates a condition whose completion is
 * signaled by an interrupt.
 */
int
tpm_wait(tpm_t *tpm, bool (*cond)(tpm_t *, bool, clock_t, const char *),
    clock_t timeout, bool intr, const char *func)
{
	clock_t deadline, now;

	ASSERT(MUTEX_HELD(&tpm->tpm_lock));

	deadline = ddi_get_lbolt() + timeout;

	/*
	 * If interrupts are not enabled, we treat it like the conditions
	 * where completion is not signaled by an interrupt.
	 */
	if (tpm->tpm_wait != TPM_WAIT_INTR) {
		intr = false;
	}

	while ((now = ddi_get_lbolt()) <= deadline &&
	    !tpm->tpm_thr_cancelreq && !tpm->tpm_thr_quit) {
		/*
		 * If we're expecting an interrupt to signal completion,
		 * we wait the entire timeout value and let the interrupt
		 * handler cv_signal() us. Otherwise, we have to check
		 * periodically.
		 */
		clock_t to = intr ? deadline : now + tpm->tpm_poll_interval;

		if (cond(tpm, false, timeout, func)) {
			return (0);
		}

		(void) cv_timedwait(&tpm->tpm_thr_cv, &tpm->tpm_lock, to);
	}

	if (tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit) {
		return (SET_ERROR(ECANCELED));
	}

	/* Check one final time */
	if (!cond(tpm, true, timeout, func)) {
		return (SET_ERROR(ETIME));
	}

	return (0);
}

/*
 * Wait for command in buf to complete execution. `done` is a transport
 * (TIS/FIFO/CRB) specific callback to determine if the command has
 * completed.
 *
 * Commands can have both an expected duration as well as a timeout,
 * as well as potentially caring about TPM_WAIT_POLL, so the semantics
 * are a bit different than tpm_wait().
 */
int
tpm_wait_cmd(tpm_t *tpm, const tpm_cmd_t *cmd,
    bool (*done)(tpm_t *, bool, uint32_t, clock_t, const char *),
    const char *func)
{
	clock_t exp_done, deadline, now, to, dur;
	uint32_t cc = tpm_cc(cmd);

	VERIFY(tpm_can_access(tpm));
	VERIFY(MUTEX_HELD(&tpm->tpm_lock));

	now = ddi_get_lbolt();

	/*
	 * Commands can have both an expected duration as well as a timeout.
	 * The difference being that the expection duration is how long the
	 * command should take to execute (but can take longer), while
	 * exceeding the timeout means something's gone wrong, and the
	 * request should be abandoned.
	 *
	 * If the command has an expected duration, we wait the expected
	 * amount of time and use the supplied callback (done) to check if
	 * the command has completed. If interrupts are enabled, we may
	 * check sooner if the TPM triggers an interrupt. While executing
	 * a command, the TPM should only trigger an interrupt when the
	 * command is complete, however even if it triggers an interrupt for
	 * another reason, we'll just determine the command is not yet
	 * complete and go back to waiting.
	 *
	 * The exception to this behavior is if the wait mode is
	 * TPM_WAIT_POLLONCE.  In this instance, we check exactly one time --
	 * after the command timeout.
	 */
	to = tpm_get_timeout(tpm, cmd);
	deadline = now + to;

	dur = tpm_get_duration(tpm, cmd);
	exp_done = (tpm->tpm_wait != TPM_WAIT_POLLONCE) ? now + to : dur;

	VERIFY3S(exp_done, <=, deadline);

	/*
	 * Wait for the expected command duration, or until we are
	 * interrupted due to cancellation or receiving a 'command done'
	 * interrupt.
	 */
	while ((now = ddi_get_lbolt()) <= exp_done &&
	    !tpm->tpm_thr_cancelreq && !tpm->tpm_thr_quit) {
		clock_t to;

		if (tpm->tpm_wait == TPM_WAIT_POLL) {
			to = now + tpm->tpm_poll_interval;
		} else {
			to = exp_done;
		}

		(void) cv_timedwait(&tpm->tpm_thr_cv, &tpm->tpm_lock, to);

		if (tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit) {
			return (ECANCELED);
		}

		/*
		 * We either received an interrupt or reached the expected
		 * command duration, check if the command is finished.
		 */
		if (done(tpm, false, cc, to, func)) {
			return (0);
		}
	}

	/*
	 * Command is taking longer than expected, either start periodically
	 * polling (if allowed), or wait until the timeout is reached
	 * (and check again).
	 */
	while ((now = ddi_get_lbolt()) <= deadline &&
	    !tpm->tpm_thr_cancelreq && !tpm->tpm_thr_quit) {
		clock_t when = 0;

		switch (tpm->tpm_wait) {
		case TPM_WAIT_POLLONCE:
		case TPM_WAIT_INTR:
			when = deadline;
			break;
		case TPM_WAIT_POLL:
			when = ddi_get_lbolt() + tpm->tpm_timeout_poll;
			break;
		}

		(void) cv_timedwait(&tpm->tpm_thr_cv, &tpm->tpm_lock, when);
		if (tpm->tpm_thr_cancelreq || tpm->tpm_thr_quit) {
			return (ECANCELED);
		}

		if (tpm->tpm_wait == TPM_WAIT_POLLONCE) {
			continue;
		}

		if (done(tpm, false, cc, to, __func__)) {
			return (0);
		}
	}

	if (!done(tpm, true, cc, to, func)) {
		return (SET_ERROR(ETIME));
	}

	return (0);
}

tpm_duration_t
tpm_get_duration_type(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	uint32_t cc = tpm_cc(cmd);

	if (cc < TPM12_ORDINAL_MAX) {
		return (tpm12_get_duration_type(tpm, cmd));
	}
	return (tpm20_get_duration_type(tpm, cmd));
}

clock_t
tpm_get_duration(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	tpm_duration_t dur;

	dur = tpm_get_duration_type(tpm, cmd);
	return (tpm->tpm_duration[dur]);
}

clock_t
tpm_get_timeout(tpm_t *tpm, const tpm_cmd_t *cmd)
{
	uint32_t cc = tpm_cc(cmd);

	if (cc < TPM12_ORDINAL_MAX) {
		return (tpm12_get_timeout(tpm, cc));
	}

	return (tpm20_get_timeout(tpm, cmd));
}

/*
 * TPM accessor functions
 */

void *
tpm_reg_addr(const tpm_t *tpm, int8_t locality, unsigned long offset)
{
	VERIFY3U(offset, <=, TPM_OFFSET_MAX);
	VERIFY3S(locality, >=, 0);
	VERIFY3S(locality, <=, tpm->tpm_n_locality);

	/*
	 * Each locality uses a block of 0x1000 addresses starting at the
	 * base address. E.g., locality 0 registers are at
	 * [tpm->tpm_addr + 0, tpm->tpm_addr + 0x1fff] and
	 * locality 1 registers are at
	 * [tpm->tpm_addr + 0x1000, tpm->tpm->tpm_addr + 0x1fff] and so on.
	 *
	 * With in each locality (except locality 4), the layout of the
	 * registers is identical (i.e. the offsets from the starting address
	 * of each block are the same). Locality 4 is rather special and
	 * appears to be intended for the system firmware and not the
	 * running OS, so we don't use it.
	 */
	offset += 0x1000 * locality;
	return (tpm->tpm_addr + offset);
}

uint8_t
tpm_get8_loc(tpm_t *tpm, int8_t locality, unsigned long offset)
{
	uint8_t *addr = tpm_reg_addr(tpm, locality, offset);

	return (ddi_get8(tpm->tpm_handle, addr));
}

uint8_t
tpm_get8(tpm_t *tpm, unsigned long offset)
{
	return (tpm_get8_loc(tpm, tpm->tpm_locality, offset));
}

uint32_t
tpm_get32_loc(tpm_t *tpm, int8_t locality, unsigned long offset)
{
	uint32_t *addr = tpm_reg_addr(tpm, locality, offset);
	return (ddi_get32(tpm->tpm_handle, addr));
}

uint32_t
tpm_get32(tpm_t *tpm, unsigned long offset)
{
	return (tpm_get32_loc(tpm, tpm->tpm_locality, offset));
}

uint64_t
tpm_get64_loc(tpm_t *tpm, int8_t locality, unsigned long offset)
{
	uint64_t *addr = tpm_reg_addr(tpm, locality, offset);
	return (ddi_get64(tpm->tpm_handle, addr));
}

uint64_t
tpm_get64(tpm_t *tpm, unsigned long offset)
{
	return (tpm_get64_loc(tpm, tpm->tpm_locality, offset));
}

void
tpm_put8_loc(tpm_t *tpm, int8_t locality, unsigned long offset, uint8_t value)
{
	uint8_t *addr = tpm_reg_addr(tpm, locality, offset);
	ddi_put8(tpm->tpm_handle, addr, value);
}

void
tpm_put8(tpm_t *tpm, unsigned long offset, uint8_t value)
{
	tpm_put8_loc(tpm, tpm->tpm_locality, offset, value);
}

void
tpm_put32_loc(tpm_t *tpm, int8_t locality, unsigned long offset, uint32_t value)
{
	uint32_t *addr = tpm_reg_addr(tpm, locality, offset);
	ddi_put32(tpm->tpm_handle, addr, value);
}

void
tpm_put32(tpm_t *tpm, unsigned long offset, uint32_t value)
{
	tpm_put32_loc(tpm, tpm->tpm_locality, offset, value);
}

/*
 * From TCG TPM Vendor ID Registry Family 1.2 and 2.0
 * Version 1.06 Revision 0.94
 */
static struct {
	uint16_t	vid;
	const char	*vstr;
} vid_tbl[] = {
	{ 0x1022, "AMD" },
	{ 0x6688, "Ant" },
	{ 0x1114, "Atmel" },
	{ 0x14E4, "Broadcom" },
	{ 0xC5C0, "Cisco" },
	{ 0x232B, "FlySlice Technologies" },
	{ 0x232A, "Fuzhou Rockchip" },
	{ 0x6666, "Google" },
	{ 0x103C, "HPI" },
	{ 0x1590, "HPE" },
	{ 0x8888, "Huawei" },
	{ 0x1014, "IBM" },
	{ 0x15D1, "Infineon" },
	{ 0x8086, "Intel" },
	{ 0x17AA, "Lenovo" },
	{ 0x1414, "Microsoft" },
	{ 0x100B, "National Semi" },
	{ 0x1B4E, "Nationz" },
	{ 0x1050, "Nuvoton Technology nee Winbind" },
	{ 0x1011, "Qualcomm" },
	{ 0x144D, "Samsung" },
	{ 0x19FA, "Sinosun" },
	{ 0x1055, "SMSC" },
	{ 0x025E, "Solidigm" },
	{ 0x104A, "STMicroelectronics" },
	{ 0x104C, "Texas Instruments" },

	/* This isn't in the registry, but from observation */
	{ 0x0ec2, "Amazon" },
};

const char *
tpm_hwvend_str(uint16_t vid)
{
	for (uint_t i = 0; i < ARRAY_SIZE(vid_tbl); i++) {
		if (vid_tbl[i].vid == vid) {
			return (vid_tbl[i].vstr);
		}
	}

	return ("Unknown");
}
