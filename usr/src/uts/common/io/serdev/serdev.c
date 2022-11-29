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
 * Copyright 2022 Oxide Computer Company
 */

#include "serdev_impl.h"

void *serdev_state;


static void
serdev_bufcall_cb_write(void *arg)
{
	serdev_t *srd = arg;

	mutex_enter(&srd->srd_mutex);
	if (srd->srd_bufcalls[SERDEV_BUFCALL_WRITE] != 0) {
		srd->srd_bufcalls[SERDEV_BUFCALL_WRITE] = 0;
		if (srd->srd_state != SERDEV_ST_CLOSING) {
			qenable(srd->srd_tty.t_writeq);
		}
	}
	mutex_exit(&srd->srd_mutex);
}

static void
serdev_bufcall_cb_read(void *arg)
{
	serdev_t *srd = arg;

	mutex_enter(&srd->srd_mutex);
	if (srd->srd_bufcalls[SERDEV_BUFCALL_READ] != 0) {
		srd->srd_bufcalls[SERDEV_BUFCALL_READ] = 0;
		if (srd->srd_state != SERDEV_ST_CLOSING) {
			qenable(srd->srd_tty.t_readq);
		}
	}
	mutex_exit(&srd->srd_mutex);
}

typedef struct {
	queue_t *q;
	void (*cb)(void *);
} serdev_bufcall_which_t;

static void
serdev_bufcall_which(serdev_t *srd, serdev_bufcall_t which,
    serdev_bufcall_which_t *w)
{
	switch (which) {
	case SERDEV_BUFCALL_WRITE:
		w->cb = serdev_bufcall_cb_write;
		VERIFY((w->q = srd->srd_tty.t_writeq) != NULL);
		return;
	case SERDEV_BUFCALL_READ:
		w->cb = serdev_bufcall_cb_read;
		VERIFY((w->q = srd->srd_tty.t_readq) != NULL);
		return;
	}

	panic("unexpected bufcall type %u", which);
}

static void
serdev_bufcall_cancel(serdev_t *srd, serdev_bufcall_t which)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	serdev_bufcall_which_t w;
	serdev_bufcall_which(srd, which, &w);

	bufcall_id_t oldid = srd->srd_bufcalls[which];
	srd->srd_bufcalls[which] = 0;

	mutex_exit(&srd->srd_mutex);
	qunbufcall(w.q, oldid);
	mutex_enter(&srd->srd_mutex);
}

static void
serdev_bufcall_schedule(serdev_t *srd, size_t sz, serdev_bufcall_t which)
{
	VERIFY(which == SERDEV_BUFCALL_WRITE || which == SERDEV_BUFCALL_READ);

	mutex_enter(&srd->srd_mutex);

	serdev_bufcall_cancel(srd, which);
	if (srd->srd_bufcalls[which] != 0) {
		/*
		 * Another thread must have ducked in and rescheduled the call
		 * while we were cancelling the old call.
		 */
		mutex_exit(&srd->srd_mutex);
		return;
	}

	serdev_bufcall_which_t w;
	serdev_bufcall_which(srd, which, &w);

	srd->srd_bufcalls[which] = qbufcall(w.q, sz, BPRI_HI, w.cb, srd);
	mutex_exit(&srd->srd_mutex);
}

static void
serdev_flow_out_stop(serdev_t *srd, serdev_stop_tx_t why)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));
	VERIFY(why == SERDEV_STOP_TX_USER ||
	    why == SERDEV_STOP_TX_CTS ||
	    why == SERDEV_STOP_TX_DELAY ||
	    why == SERDEV_STOP_TX_BREAK);

	/*
	 * Record this stop action as one of the reasons we have requested no
	 * more input:
	 */
	srd->srd_stop_tx_why |= why;

	if (srd->srd_flags & SERDEV_FL_TX_STOPPED) {
		/*
		 * We were already stopped.
		 */
		return;
	}

	srd->srd_flags |= SERDEV_FL_TX_STOPPED;
}

static void
serdev_flow_out_start(serdev_t *srd, serdev_stop_tx_t why)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));
	VERIFY(why == SERDEV_STOP_TX_USER ||
	    why == SERDEV_STOP_TX_CTS ||
	    why == SERDEV_STOP_TX_DELAY ||
	    why == SERDEV_STOP_TX_BREAK);

	/*
	 * Clear this stop action:
	 */
	srd->srd_stop_tx_why &= ~why;

	if (!(srd->srd_flags & SERDEV_FL_TX_STOPPED) ||
	    srd->srd_stop_tx_why != 0) {
		/*
		 * We were already moving, or we have other reasons to be
		 * stopped.
		 */
		return;
	}

	srd->srd_flags &= ~SERDEV_FL_TX_STOPPED;
	if (srd->srd_tty.t_writeq != NULL) {
		qenable(srd->srd_tty.t_writeq);
	}

	if (srd->srd_flags & SERDEV_FL_TX_ACTIVE) {
		/*
		 * The driver may have stopped trying to feed data to the
		 * device if they observed in the past that we were flow
		 * controlled.  Kick them to make sure they're moving again.
		 */
		mutex_exit(&srd->srd_mutex);
		srd->srd_ops.srdo_tx(srd->srd_private, NULL);
		mutex_enter(&srd->srd_mutex);
	}
}

static void
serdev_flow_in_stop(serdev_t *srd, serdev_stop_rx_t why)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));
	VERIFY(why == SERDEV_STOP_RX_USER || why == SERDEV_STOP_RX_STREAMS);

	/*
	 * Record this stop action as one of the reasons we have requested no
	 * more input:
	 */
	srd->srd_stop_rx_why |= why;

	if (srd->srd_flags & SERDEV_FL_RX_STOPPED) {
		/*
		 * We were already stopped.
		 */
		return;
	}

	/*
	 * Schedule a flow control action on the taskq:
	 */
	srd->srd_flags |= SERDEV_FL_RX_STOPPED;
}

static void
serdev_flow_in_start(serdev_t *srd, serdev_stop_rx_t why)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));
	VERIFY(why == SERDEV_STOP_RX_USER || why == SERDEV_STOP_RX_STREAMS);

	/*
	 * Clear this stop action:
	 */
	srd->srd_stop_rx_why &= ~why;

	if (!(srd->srd_flags & SERDEV_FL_RX_STOPPED) ||
	    srd->srd_stop_rx_why != 0) {
		/*
		 * We were already moving, or we have other reasons to be
		 * stopped.
		 */
		return;
	}

	/*
	 * Restart the read queue:
	 */
	srd->srd_flags &= ~SERDEV_FL_RX_STOPPED;
	if (why != SERDEV_STOP_RX_STREAMS && srd->srd_tty.t_readq != NULL) {
		/*
		 * Only trigger the read queue service routine if there is
		 * still a read queue, and if we are not being called from
		 * inside its service routine.  Enabling the queue from inside
		 * the service routine could lead to it being scheduled over
		 * and over forever.
		 */
		qenable(srd->srd_tty.t_readq);
	}
}

static void
serdev_rx_transform_break(serdev_t *srd, mblk_t *mp)
{
	tty_common_t *t = &srd->srd_tty;

	/*
	 * The device passes us two bytes: an error value and a character.  The
	 * error value is private to serdev, and we may pass the data on to the
	 * line discipline.
	 */
	uchar_t error = *mp->b_rptr++;
	uchar_t data = *mp->b_rptr;
	bool framing = (error & SERDEV_ERROR_FRAMING) != 0;
	bool brk = (error & SERDEV_ERROR_BREAK) != 0;
	bool parity = (error & SERDEV_ERROR_PARITY) != 0;

	/*
	 * Determine if we need to transform the message, or merely pass it on
	 * to the line discipline as an M_BREAK with a suspect character.
	 */
	if ((framing || brk) && data == 0) {
		/*
		 * This would seem to be a real serial break condition.  Signal
		 * that condition by passing on an M_BREAK with no data.
		 */
		mp->b_rptr++;
	} else if (!(t->t_iflag & INPCK) && parity) {
		/*
		 * This is a parity error.  As per termio(7I), the INPCK flag
		 * enables the reporting of parity errors.  If it is not set,
		 * we just pass the data on.
		 */
		DB_TYPE(mp) = M_DATA;
	}
}

static int
serdev_rx_transform_data(serdev_t *srd, mblk_t **mpp)
{
	mblk_t *mp = *mpp;

	/*
	 * As described in termio(7I), there is a specific condition we need to
	 * account for (as do the other serial port drivers).  If parity is
	 * enabled, and we are using 8-bit characters, and both IGNPAR and
	 * ISTRIP are clear, we must represent the value 0377 as a pair of 0377
	 * values to avoid ambiguity.
	 *
	 * If these exact conditions are not met, we need not adjust the data.
	 */
	mutex_enter(&srd->srd_mutex);
	tty_common_t *t = &srd->srd_tty;
	if (!(t->t_cflag & PARENB) ||
	    !(t->t_iflag & PARMRK) ||
	    (t->t_cflag & CSIZE) != CS8 ||
	    (t->t_iflag & IGNPAR) ||
	    (t->t_iflag & ISTRIP)) {
		mutex_exit(&srd->srd_mutex);
		return (0);
	}
	mutex_exit(&srd->srd_mutex);

	VERIFY3P(mp->b_next, ==, NULL);
	VERIFY3P(mp->b_cont, ==, NULL);

	/*
	 * First, perform a scan to see if we need to adjust the data
	 * at all.
	 */
	uint_t found = 0;
	for (uchar_t *p = mp->b_rptr; p < mp->b_wptr; p++) {
		if (*p == 0377) {
			found++;
			break;
		}
	}

	if (found == 0) {
		/*
		 * If a 0377 byte does not appear, no transformation is
		 * required.
		 */
		return (0);
	}

	mblk_t *newmp;
	if ((newmp = allocb(MBLKL(mp) + found, BPRI_HI)) == NULL) {
		/*
		 * If we could not allocate, do not touch the existing mblk.
		 * Request a restart of the read service routine once there
		 * is free memory.
		 */
		serdev_bufcall_schedule(srd, MBLKL(mp) + found,
		    SERDEV_BUFCALL_READ);
		return (ENOMEM);
	}

	/*
	 * Copy and transform the data:
	 */
	for (uchar_t *p = mp->b_rptr; p < mp->b_wptr; p++) {
		if (*p == 0377) {
			*newmp->b_wptr++ = 0377;
		}
		*newmp->b_wptr++ = *p;
	}

	/*
	 * Swap out the old mblk for the new one.
	 */
	freemsg(mp);
	*mpp = newmp;
	return (0);
}

static void
serdev_taskq_new_status(serdev_t *srd, uint_t status)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	tty_common_t *t = &srd->srd_tty;

	/*
	 * Record the most recent status we received for debugging purposes.
	 */
	srd->srd_last_modem_status = status;

	if ((t->t_cflag & CRTSCTS) && !(status & TIOCM_CTS)) {
		/*
		 * Inbound flow control is presently enabled and Clear To Send
		 * (CTS) is not asserted.  We need to propagate that back
		 * through the stream as flow control.
		 */
		serdev_flow_out_stop(srd, SERDEV_STOP_TX_CTS);
	} else {
		/*
		 * Otherwise, we are either explicitly allowed to send or are
		 * ignoring the hardware flow control signals.
		 */
		serdev_flow_out_start(srd, SERDEV_STOP_TX_CTS);
	}

	if ((status & TIOCM_CD) || (t->t_flags & TS_SOFTCAR)) {
		/*
		 * Either the modem reports carrier detection, or we are using
		 * the soft carrier mode ("ignore-cd") for this line.
		 */
		if (!(srd->srd_flags & SERDEV_FL_CARRIER_DETECT)) {
			srd->srd_flags |= SERDEV_FL_CARRIER_DETECT;
			if (t->t_readq != NULL) {
				/*
				 * Wake the read queue to send M_UNHANGUP.
				 */
				qenable(t->t_readq);
			}
		}
	} else if (srd->srd_flags & SERDEV_FL_CARRIER_DETECT) {
		/*
		 * The previously detected carrier is now gone.
		 */
		if (!(t->t_cflag & CLOCAL)) {
			/*
			 * This is not a local line so we drop DTR to cause the
			 * modem to hang up.
			 */
			mutex_exit(&srd->srd_mutex);
			(void) srd->srd_ops.srdo_modem_set(srd->srd_private,
			    TIOCM_DTR, 0);
			mutex_enter(&srd->srd_mutex);
		}

		/*
		 * We set the CARRIER_LOSS flag so that we are sure to send an
		 * M_HANGUP even if the carrier comes back very quickly.
		 */
		srd->srd_flags &= ~SERDEV_FL_CARRIER_DETECT;
		srd->srd_flags |= SERDEV_FL_CARRIER_LOSS;
		if (t->t_readq != NULL) {
			/*
			 * Wake the read queue to send M_HANGUP.
			 */
			qenable(t->t_readq);
		}
	}

	/*
	 * Wake anybody that was waiting on a new status value.
	 */
	cv_broadcast(&srd->srd_cv);
}

static void
serdev_taskq(void *arg)
{
	serdev_t *srd = arg;

	mutex_enter(&srd->srd_mutex);
	VERIFY0(srd->srd_flags & SERDEV_FL_TASK_RUNNING);
	srd->srd_flags |= SERDEV_FL_TASK_RUNNING;

	while (srd->srd_flags & SERDEV_FL_TASK_REQUESTED) {
		srd->srd_flags &= ~SERDEV_FL_TASK_REQUESTED;

		if (srd->srd_flags & SERDEV_FL_NEED_STATUS) {
			srd->srd_flags &= ~SERDEV_FL_NEED_STATUS;

			/*
			 * Call into the device driver to get updated modem
			 * status.
			 */
			uint_t status;
			mutex_exit(&srd->srd_mutex);
			int r = srd->srd_ops.srdo_modem_get(srd->srd_private,
			    UINT_MAX, &status);
			mutex_enter(&srd->srd_mutex);
			if (r == 0) {
				serdev_taskq_new_status(srd, status);
			}
		}
	}

	srd->srd_flags &= ~SERDEV_FL_TASK_RUNNING;
	cv_broadcast(&srd->srd_cv);
	mutex_exit(&srd->srd_mutex);
}

void
serdev_taskq_dispatch(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	if (srd->srd_state == SERDEV_ST_CLOSED) {
		return;
	}

	if (!(srd->srd_flags & SERDEV_FL_TASK_REQUESTED)) {
		srd->srd_flags |= SERDEV_FL_TASK_REQUESTED;
		taskq_dispatch_ent(srd->srd_taskq, serdev_taskq,
		    srd, 0, &srd->srd_task);
	}
}

static void
serdev_teardown(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	/*
	 * Before we try to tear down resources at the instance level, the port
	 * must be completely closed.
	 */
	VERIFY3U(srd->srd_state, ==, SERDEV_ST_CLOSED);

	if (srd->srd_setup & SERDEV_SETUP_MINOR_NODES) {
		/*
		 * Because we have a device node per port, we can just remove
		 * all of our minor nodes at once.
		 */
		ddi_remove_minor_node(srd->srd_dip, NULL);

		srd->srd_setup &= ~SERDEV_SETUP_MINOR_NODES;
	}

	/*
	 * Make sure we did not forget to tear anything down:
	 */
	VERIFY0(srd->srd_setup);

	mutex_exit(&srd->srd_mutex);

	taskq_destroy(srd->srd_taskq);
	cv_destroy(&srd->srd_cv);
	mutex_destroy(&srd->srd_mutex);

	ddi_set_driver_private(srd->srd_dip, NULL);
	ddi_soft_state_free(serdev_state, ddi_get_instance(srd->srd_dip));
}

static int
serdev_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int inst = ddi_get_instance(dip);
	serdev_handle_t *srdh;

	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if ((srdh = ddi_get_parent_data(dip)) == NULL) {
		return (DDI_FAILURE);
	}

	if (ddi_soft_state_zalloc(serdev_state, inst) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "unable to allocate soft state");
		return (DDI_FAILURE);
	}

	serdev_t *srd = ddi_get_soft_state(serdev_state, inst);
	srd->srd_dip = dip;
	srd->srd_private = srdh->srdh_private;
	srd->srd_ops = srdh->srdh_ops;
	srd->srd_ignore_cd = srdh->srdh_ignore_cd;
	ddi_set_driver_private(dip, srd);

	srd->srd_taskq = taskq_create_instance("serdev", inst, 1, minclsyspri,
	    0, 0, 0);
	mutex_init(&srd->srd_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&srd->srd_cv, NULL, CV_DEFAULT, NULL);

	/*
	 * Create the minor nodes for this serial port.  Each port on a
	 * multiport device will end up with a separate serdev device node, so
	 * we can use a static minor name for the two nodes we need to create:
	 */
	if (ddi_create_minor_node(dip, "0", S_IFCHR,
	    SERDEV_MINOR_TTY(inst), DDI_NT_SERIAL, 0) != DDI_SUCCESS ||
	    ddi_create_minor_node(dip, "0,cu", S_IFCHR,
	    SERDEV_MINOR_DIALOUT(inst), DDI_NT_SERIAL_DO, 0) != DDI_SUCCESS) {
		mutex_enter(&srd->srd_mutex);
		serdev_teardown(srd);
		return (DDI_FAILURE);
	}
	srd->srd_setup |= SERDEV_SETUP_MINOR_NODES;

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

static int
serdev_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	serdev_t *srd = ddi_get_driver_private(dip);

	if (cmd == DDI_SUSPEND) {
		/*
		 * Parent device handles suspend.
		 */
		return (DDI_SUCCESS);
	} else if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	mutex_enter(&srd->srd_mutex);

	if (srd->srd_state != SERDEV_ST_CLOSED) {
		/*
		 * Do not allow detach until we have fully closed the port.
		 */
		mutex_exit(&srd->srd_mutex);
		return (DDI_FAILURE);
	}

	serdev_teardown(srd);

	return (DDI_SUCCESS);
}

static int
serdev_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **rp)
{
	serdev_t *srd;
	int inst = SERDEV_MINOR_TO_INST(getminor((dev_t)arg));

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if ((srd = ddi_get_soft_state(serdev_state, inst)) == NULL) {
			return (DDI_FAILURE);
		}
		*rp = srd->srd_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*rp = (void *)(intptr_t)inst;
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

static serdev_open_mode_t
serdev_dev_mode(dev_t dev)
{
	switch (getminor(dev) & SERDEV_MINOR_MODE_MASK) {
	case SERDEV_MINOR_MODE_TTY:
		return (SERDEV_OM_TTY);
	case SERDEV_MINOR_MODE_DIALOUT:
		return (SERDEV_OM_DIALOUT);
	default:
		panic("unexpected dev");
	}
}

static int
serdev_ttymodes_cflag(void)
{
	struct termios *termios;
	uint_t len;

	/*
	 * If we can't find the property, we use some extremely basic defaults:
	 */
	int cflag = B9600 | CS8 | CREAD;

	/*
	 * Get default tty settings from the global devinfo property:
	 */
	if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, ddi_root_node(),
	    0, "ttymodes", (uchar_t **)&termios, &len) == DDI_PROP_SUCCESS) {
		if (len == sizeof (*termios)) {
			cflag = termios->c_cflag;
		}
		ddi_prop_free(termios);
	}

	return (cflag);
}

static void
serdev_tx_hold_cancel(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	if (srd->srd_timeout != 0) {
		/*
		 * Cancel the existing timeout.
		 */
		timeout_id_t old = srd->srd_timeout;
		srd->srd_timeout = 0;

		mutex_exit(&srd->srd_mutex);
		VERIFY0(untimeout(old));
		mutex_enter(&srd->srd_mutex);
	}
}

static void
serdev_timeout(void *arg)
{
	serdev_t *srd = arg;

	mutex_enter(&srd->srd_mutex);
	if (srd->srd_timeout == 0) {
		/*
		 * This timeout was cancelled either for rescheduling or during
		 * teardown.
		 */
		mutex_exit(&srd->srd_mutex);
		return;
	}

	/*
	 * When the timeout expires, we are no longed stopped for an M_DELAY
	 * request.
	 */
	serdev_flow_out_start(srd, SERDEV_STOP_TX_DELAY);

	/*
	 * If we are delayed due to a timed break, clear it now.
	 */
	if (srd->srd_break == SERDEV_BREAK_TIMED) {
		mutex_exit(&srd->srd_mutex);
		(void) srd->srd_ops.srdo_break(srd->srd_private, false);
		mutex_enter(&srd->srd_mutex);

		srd->srd_break = SERDEV_BREAK_NONE;
		serdev_flow_out_start(srd, SERDEV_STOP_TX_BREAK);
	}

	srd->srd_timeout = 0;
	mutex_exit(&srd->srd_mutex);
}

static void
serdev_tx_hold(serdev_t *srd, clock_t ticks)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	serdev_tx_hold_cancel(srd);

	VERIFY0(srd->srd_timeout);
	srd->srd_timeout = timeout(serdev_timeout, srd, ticks);
}

static int
serdev_tx_start_delay(serdev_t *srd, clock_t ticks)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	serdev_flow_out_stop(srd, SERDEV_STOP_TX_DELAY);
	serdev_tx_hold(srd, ticks);
	return (0);
}

static int
serdev_tx_start_break(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	mutex_exit(&srd->srd_mutex);
	int r = srd->srd_ops.srdo_break(srd->srd_private, false);
	mutex_enter(&srd->srd_mutex);

	if (r != 0) {
		return (r);
	}

	/*
	 * If we were successful, hold transmission for at least a quarter
	 * second.  Break will be cleared when the timeout expires.
	 */
	serdev_flow_out_stop(srd, SERDEV_STOP_TX_BREAK);
	serdev_tx_hold(srd, drv_usectohz(250000));

	/*
	 * If the user has used one of the untimed break ioctls, the timed
	 * break will effectively cancel it when it expires.  The untimed break
	 * ioctls (TIOCSBRK, TIOCCBRK) are not especially well considered;
	 * using both timed breaks and untimed breaks on the same line is a
	 * recipe for peril.
	 */
	srd->srd_break = SERDEV_BREAK_TIMED;

	return (0);
}

static int
serdev_configure(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	if ((srd->srd_tty.t_cflag & (CIBAUD | CIBAUDEXT)) != 0) {
		/*
		 * We do not support programming a different input and output
		 * baud rate.  As per termio(7I), impossible speed changes are
		 * ignored.  By clearing these bits, the input speed is
		 * determined by the output speed:
		 */
		srd->srd_tty.t_cflag &= ~(CIBAUD | CIBAUDEXT);
	}

	/*
	 * Assemble parameters in an opaque structure that we pass to the
	 * device driver.  Drivers will make use of the parameters they are
	 * able to understand and ignore those that they do not.
	 */
	serdev_params_t srpp = {
		.srpp_baudrate = ttycommon_ospeed(&srd->srd_tty),
		.srpp_stop_bits = (srd->srd_tty.t_cflag & CSTOPB) ? 2 : 1,
		.srpp_parity = !(srd->srd_tty.t_cflag & PARENB) ?
		    SERDEV_PARITY_NONE :
		    (srd->srd_tty.t_cflag & PARODD) ?
		    SERDEV_PARITY_ODD : SERDEV_PARITY_EVEN,
		.srpp_char_size = ttycommon_char_size(&srd->srd_tty),
		.srpp_hard_flow_inbound =
		    (srd->srd_tty.t_cflag & CRTSXOFF) != 0,
		.srpp_hard_flow_outbound =
		    (srd->srd_tty.t_cflag & CRTSCTS) != 0,
	};

	/*
	 * In addition to programming parameters, we need to update the modem
	 * status bits.
	 */
	uint_t mask = TIOCM_DTR | TIOCM_RTS;
	uint_t control = TIOCM_DTR | TIOCM_RTS;
	if (srpp.srpp_baudrate == B0) {
		/*
		 * As per termio(7P), a baud rate of zero means we need to
		 * switch off the DTR signal.  This is intended to cause the
		 * modem to disconnect, if it supports that.
		 */
		control &= ~TIOCM_DTR;
	}
	if (srd->srd_tty.t_cflag & CRTSXOFF) {
		/*
		 * Inbound hardware flow control is enabled.
		 */
		if (srd->srd_flags & SERDEV_FL_RX_STOPPED) {
			/*
			 * Drop the RTS signal if we do not want any more data.
			 */
			control &= ~TIOCM_RTS;
		}
	}

	mutex_exit(&srd->srd_mutex);
	int r;
	if ((r = srd->srd_ops.srdo_params_set(srd->srd_private, &srpp)) == 0) {
		r = srd->srd_ops.srdo_modem_set(srd->srd_private, mask,
		    control);
	}
	mutex_enter(&srd->srd_mutex);

	return (r);
}

static void
serdev_state_change_unchecked(serdev_t *srd, serdev_state_t newstate)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	srd->srd_state = newstate;
	cv_broadcast(&srd->srd_cv);
}

static void
serdev_state_change(serdev_t *srd, serdev_state_t oldstate,
    serdev_state_t newstate)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	VERIFY3U(srd->srd_state, ==, oldstate);
	serdev_state_change_unchecked(srd, newstate);
}

/*
 * Wait for a state change on this instance.  Returns true if we made it to a
 * terminal state, or false if we were interrupted and should return EINTR to
 * the user.
 */
static bool
serdev_wait(serdev_t *srd)
{
	if (cv_wait_sig(&srd->srd_cv, &srd->srd_mutex) == 0) {
		/*
		 * Interrupted by signal.
		 */
		return (false);
	}

	return (true);
}

/*
 * Wait for a state change on this instance.  Returns true if we made it to a
 * terminal state, or false if we were interrupted and should return EINTR to
 * the user.
 */
static bool
serdev_wait_deadline(serdev_t *srd, hrtime_t deadline)
{
	if (cv_timedwait_sig_hrtime(&srd->srd_cv, &srd->srd_mutex,
	    deadline) <= 0) {
		/*
		 * Interrupted by signal or ran out of time.
		 */
		return (false);
	}

	return (true);
}

static void
serdev_open_teardown(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	serdev_state_change_unchecked(srd, SERDEV_ST_CLOSING);

	/*
	 * Ensure all of our deferred execution mechanisms have come to rest:
	 */
	serdev_tx_hold_cancel(srd);
	while (srd->srd_flags & SERDEV_FL_TASK_RUNNING) {
		srd->srd_flags &= ~SERDEV_FL_TASK_REQUESTED;
		cv_wait(&srd->srd_cv, &srd->srd_mutex);
	}

	if (srd->srd_setup & SERDEV_SETUP_OPEN_DEVICE) {
		bool clear_break = srd->srd_break != SERDEV_BREAK_NONE;

		mutex_exit(&srd->srd_mutex);
		if (clear_break) {
			(void) srd->srd_ops.srdo_break(srd->srd_private, false);
		}
		(void) srd->srd_ops.srdo_close(srd->srd_private);
		mutex_enter(&srd->srd_mutex);

		srd->srd_break = SERDEV_BREAK_NONE;
		srd->srd_setup &= ~SERDEV_SETUP_OPEN_DEVICE;
	}

	ttycommon_close(&srd->srd_tty);

	for (uint_t i = 0; i < SERDEV_NBUFCALLS; i++) {
		VERIFY3S(srd->srd_bufcalls[i], ==, 0);
	}
	VERIFY3S(srd->srd_timeout, ==, 0);

	srd->srd_flags &= ~SERDEV_FL_CARRIER_DETECT;
	srd->srd_flags &= ~SERDEV_FL_CARRIER_LOSS;
	srd->srd_flags &= ~SERDEV_FL_OFF_HOOK;

	srd->srd_open_mode = SERDEV_OM_NONE;
	serdev_state_change(srd, SERDEV_ST_CLOSING, SERDEV_ST_CLOSED);
}

static void
serdev_open_release(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	VERIFY3P(srd->srd_opener, ==, curthread);
	srd->srd_opener = NULL;
}

static void
serdev_open_takeover(serdev_t *srd, serdev_open_mode_t open_mode)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	/*
	 * We can only take over the port if it is in the carrier wait state.
	 */
	serdev_state_change(srd, SERDEV_ST_CARRIER_WAIT, SERDEV_ST_OPENING);

	/*
	 * Only an outbound open can take over, and it must be from a pending
	 * inbound open.
	 */
	VERIFY3U(srd->srd_open_mode, ==, SERDEV_OM_TTY);
	VERIFY3U(open_mode, ==, SERDEV_OM_DIALOUT);
	srd->srd_open_mode = open_mode;

	VERIFY(srd->srd_opener != NULL);
	VERIFY(srd->srd_opener != curthread);
	srd->srd_opener = curthread;
}

static void
serdev_open_start(serdev_t *srd, serdev_open_mode_t open_mode)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	/*
	 * We can only open the port if it is in the closed state.
	 */
	serdev_state_change(srd, SERDEV_ST_CLOSED, SERDEV_ST_OPENING);

	VERIFY3U(srd->srd_open_mode, ==, SERDEV_OM_NONE);
	srd->srd_open_mode = open_mode;

	VERIFY0(srd->srd_opener);
	srd->srd_opener = curthread;
}

/*
 * Wait for a carrier to be detected for this line.  If a carrier is detected,
 * we'll return 0.  Otherwise, any required cleanup will be performed and an
 * error number will be returned.
 *
 * While sleeping waiting for modem status changes, we will be in the
 * SERDEV_ST_CARRIER_WAIT state.  In this wait state, it's possible for another
 * higher priority open to swoop in and steal the line from us -- if so we'll
 * return failure without doing any cleanup because the line will belong to
 * another opener.
 */
static int
serdev_wait_for_carrier(serdev_t *srd)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));

	VERIFY3U(srd->srd_state, ==, SERDEV_ST_OPENING);
	VERIFY3P(srd->srd_opener, ==, curthread);

	for (;;) {
		uint_t status;

		/*
		 * Fetch the current carrier detect status from the device
		 * driver:
		 */
		mutex_exit(&srd->srd_mutex);
		int r = srd->srd_ops.srdo_modem_get(srd->srd_private,
		    TIOCM_CD, &status);
		mutex_enter(&srd->srd_mutex);

		if (r != 0) {
			/*
			 * We could not fetch the status.
			 */
			serdev_open_release(srd);
			serdev_open_teardown(srd);
			return (r);
		}

		if (status & TIOCM_CD) {
			/*
			 * Carrier detected!
			 */
			srd->srd_flags |= SERDEV_FL_CARRIER_DETECT;
			return (0);
		} else {
			srd->srd_flags &= ~SERDEV_FL_CARRIER_DETECT;
		}

		/*
		 * We transition into the CARRIER_WAIT state only across this
		 * sleep, as whenever we drop the mutex in this state another
		 * opening thread may take over and we have to check on
		 * reacquisition.
		 */
		serdev_state_change(srd, SERDEV_ST_OPENING,
		    SERDEV_ST_CARRIER_WAIT);

		bool signalled = !serdev_wait(srd);

		if (srd->srd_opener != curthread) {
			/*
			 * Another open has taken the device away from us.  We
			 * need to re-enter the state machine to determine what
			 * to do next.  As the device state no longer belongs
			 * to us, we do not update the state on the way out.
			 */
			if (signalled) {
				/*
				 * We were apparently also interrupted by a
				 * signal.  Because another open took over, we
				 * have no clean-up to do.
				 */
				return (EINTR);
			} else {
				return (EBUSY);
			}
		}

		serdev_state_change(srd, SERDEV_ST_CARRIER_WAIT,
		    SERDEV_ST_OPENING);

		if (srd->srd_flags & SERDEV_FL_CARRIER_DETECT) {
			/*
			 * The modem status changed while we were asleep, and a
			 * carrier is now detected!
			 */
			serdev_open_release(srd);
			return (0);
		} else if (signalled) {
			/*
			 * Because we are still in charge, we need to do a full
			 * tear-down.
			 */
			serdev_open_release(srd);
			serdev_open_teardown(srd);
			return (EINTR);
		}
	}
}

int
serdev_open_finish(serdev_t *srd, queue_t *rq, queue_t *wq, bool noblock)
{
	VERIFY(MUTEX_HELD(&srd->srd_mutex));
	VERIFY3P(srd->srd_opener, ==, curthread);
	VERIFY3U(srd->srd_state, ==, SERDEV_ST_OPENING);

	/*
	 * Switch on the Data Terminal Ready (DTR) signal for this line:
	 */
	mutex_exit(&srd->srd_mutex);
	(void) srd->srd_ops.srdo_modem_set(srd->srd_private,
	    TIOCM_DTR, TIOCM_DTR);
	mutex_enter(&srd->srd_mutex);

	if (srd->srd_ignore_cd) {
		/*
		 * If we are ignoring carrier detect for this device, set the
		 * soft carrier flag on the tty:
		 */
		srd->srd_tty.t_flags |= TS_SOFTCAR;
	}

	/*
	 * If this is not a soft-carrier or local line, and it is a blocking
	 * open(), and this is an inbound/tty minor node, then we need to block
	 * waiting for carrier detection.
	 */
	if (!(srd->srd_tty.t_flags & TS_SOFTCAR) &&
	    !(srd->srd_tty.t_cflag & CLOCAL) &&
	    !noblock &&
	    srd->srd_open_mode == SERDEV_OM_TTY) {
		int r;

		if ((r = serdev_wait_for_carrier(srd)) != 0) {
			/*
			 * If we could not wait for carrier we must abort this
			 * open.  Any required cleanup has been done already.
			 */
			mutex_exit(&srd->srd_mutex);
			return (r);
		}
	}

	/*
	 * Set up the tty STREAMS and enable our put and service procedures:
	 */
	srd->srd_tty.t_readq = rq;
	rq->q_ptr = srd;
	srd->srd_tty.t_writeq = wq;
	wq->q_ptr = srd;
	qprocson(rq);
	srd->srd_setup |= SERDEV_SETUP_STREAMS;

	serdev_open_release(srd);

	serdev_state_change(srd, SERDEV_ST_OPENING, SERDEV_ST_OPEN);

	/*
	 * Ensure we request a full status update at least once up front, even
	 * if the driver never ends up pushing a status update.
	 */
	srd->srd_flags |= SERDEV_FL_NEED_STATUS;
	serdev_taskq_dispatch(srd);

	mutex_exit(&srd->srd_mutex);
	return (0);
}

int
serdev_open(queue_t *rq, dev_t *dev, int flag, int sflag, cred_t *cr)
{
	int inst = SERDEV_MINOR_TO_INST(getminor(*dev));
	serdev_open_mode_t open_mode = serdev_dev_mode(*dev);
	int default_cflag = serdev_ttymodes_cflag();
	bool noblock = (flag & (FNDELAY | FNONBLOCK)) != 0;
	serdev_t *srd;

	if ((srd = ddi_get_soft_state(serdev_state, inst)) == NULL) {
		return (ENXIO);
	}
	mutex_enter(&srd->srd_mutex);

again:
	switch (srd->srd_state) {
	case SERDEV_ST_CLOSED:
		/*
		 * The device is completely closed.  As the first of
		 * potentially several competing opens, we must advance the
		 * state machine to a point where we are completely open or
		 * holding for a detected carrier.
		 */
		serdev_open_start(srd, open_mode);
		ttycommon_init(&srd->srd_tty);
		srd->srd_tty.t_cflag = default_cflag;

		/*
		 * Attempt to open the actual device.
		 */
		mutex_exit(&srd->srd_mutex);
		int r = srd->srd_ops.srdo_open(srd->srd_private);
		mutex_enter(&srd->srd_mutex);
		if (r != 0) {
			serdev_open_release(srd);
			serdev_open_teardown(srd);
			mutex_exit(&srd->srd_mutex);
			return (r);
		}
		srd->srd_setup |= SERDEV_SETUP_OPEN_DEVICE;

		/*
		 * Set parameters and modem status on the device.
		 */
		if ((r = serdev_configure(srd)) != 0) {
			/*
			 * If we could not configure the device we need to close
			 * it again.
			 */
			serdev_open_release(srd);
			serdev_open_teardown(srd);
			mutex_exit(&srd->srd_mutex);
			return (r);
		}

		return (serdev_open_finish(srd, rq, WR(rq), noblock));

	case SERDEV_ST_OPENING:
	case SERDEV_ST_CLOSING_DRAINING:
	case SERDEV_ST_CLOSING:
		/*
		 * We need to wait for the first open to either complete the
		 * process or reach the point where they are waiting on carrier
		 * detection.
		 */
		if (!serdev_wait(srd)) {
			mutex_exit(&srd->srd_mutex);
			return (EINTR);
		}
		goto again;

	case SERDEV_ST_CARRIER_WAIT:
		/*
		 * There is an existing inbound (tty) open that is waiting for
		 * carrier detect.  If our open is for dialout, we can take
		 * over the device.
		 */
		if (open_mode == SERDEV_OM_DIALOUT) {
			/*
			 * Take over the device!  The thread that put the port
			 * in the carrier wait state will check and find we
			 * have changed the open state, at which time it will
			 * re-enter the open state machine from the top.
			 */
			serdev_open_takeover(srd, open_mode);
			return (serdev_open_finish(srd, rq, WR(rq), noblock));
		}

		/*
		 * Otherwise, wait for the first open to complete or reach the
		 * point where they are waiting on carrier detection.
		 */
		if (!serdev_wait(srd)) {
			mutex_exit(&srd->srd_mutex);
			return (EINTR);
		}
		goto again;

	case SERDEV_ST_OPEN:
		/*
		 * The port is already open by somebody else.  Determine if we
		 * can open it a second time, or if we need to return a
		 * failure.
		 */
		if (open_mode != srd->srd_open_mode ||
		    ((srd->srd_tty.t_flags & TS_XCLUDE) &&
		    secpolicy_excl_open(cr) != 0)) {
			/*
			 * Either the mode we are using here is not the same as
			 * the mode for the existing open, or the port is open
			 * for exclusive use.
			 */
			mutex_exit(&srd->srd_mutex);
			return (EBUSY);
		}

		/*
		 * The port is already open so there is no more setup to do for
		 * a second open.
		 */
		mutex_exit(&srd->srd_mutex);
		return (0);
	}

	panic("unexpected state %d", srd->srd_state);
}

static int
serdev_close(queue_t *rq, int flag, cred_t *cr)
{
	serdev_t *srd = rq->q_ptr;
	queue_t *wq = WR(rq);

	mutex_enter(&srd->srd_mutex);
	bool hangup = (srd->srd_tty.t_cflag & HUPCL) != 0;

	/*
	 * In the CLOSING_DRAINING state we will no longer accept new messages
	 * into the STREAMS write queue, but we will continue to process
	 * anything already submitted.
	 */
	serdev_state_change(srd, SERDEV_ST_OPEN, SERDEV_ST_CLOSING_DRAINING);

	/*
	 * If the user started an untimed break before closing, we assume that
	 * they don't care about draining any remaining data in the write queue
	 * or the device buffers.  After all, once we're closing there is no
	 * way to use ioctl() to disable the break condition.
	 *
	 * If a timed break is in effect, we will wait for it to clear
	 * naturally.
	 */
	if (srd->srd_break == SERDEV_BREAK_USER) {
		mutex_exit(&srd->srd_mutex);
		goto flush;
	}

	/*
	 * Close could occur in two contexts: an explicit call to close(2),
	 * where a process still exists and can be interrupted with a signal;
	 * or implicitly during exit(2) handling, where we are no longer able
	 * to be interrupted.  Set a deadline to ensure we do not end up
	 * waiting forever.
	 */
	hrtime_t deadline = gethrtime() + SEC2NSEC(5); /* XXX */

	/*
	 * Draining is a multi-step process.  First, we operate the device as
	 * normal until the write queue for the stream has no more messages to
	 * process.
	 */
	serdev_flow_out_start(srd, SERDEV_STOP_TX_USER);
	srd->srd_flags |= SERDEV_FL_NEED_DRAIN;
	qenable(wq);
	while (srd->srd_flags & SERDEV_FL_NEED_DRAIN) {
		if (!serdev_wait_deadline(srd, deadline)) {
			/*
			 * We were interrupted or we ran out of time.
			 */
			srd->srd_flags &= ~SERDEV_FL_NEED_DRAIN;
			mutex_exit(&srd->srd_mutex);
			goto flush;
		}
	}

	mutex_exit(&srd->srd_mutex);

	/*
	 * Next, try to drain whatever is in the driver or the buffers in the
	 * device.
	 */
	(void) srd->srd_ops.srdo_drain(srd->srd_private, deadline);

	/*
	 * Finally, flush and reset the hardware in case anything was left.
	 */
flush:
	(void) srd->srd_ops.srdo_flush_tx(srd->srd_private);
	(void) srd->srd_ops.srdo_flush_rx(srd->srd_private);

	if (hangup) {
		/*
		 * Drop DTR and RTS to try to hang up the modem.
		 */
		(void) srd->srd_ops.srdo_modem_set(srd->srd_private,
		    TIOCM_DTR | TIOCM_RTS, 0);
	}

	mutex_enter(&srd->srd_mutex);
	serdev_state_change(srd, SERDEV_ST_CLOSING_DRAINING, SERDEV_ST_CLOSING);

	/*
	 * Cancel any pending bufcalls.  This must be done before qprocsoff().
	 */
	serdev_bufcall_cancel(srd, SERDEV_BUFCALL_WRITE);
	serdev_bufcall_cancel(srd, SERDEV_BUFCALL_READ);

	/*
	 * Disable put and service procedures for our streams.  This will block
	 * until they are no longer running, so we must not hold the lock here.
	 */
	mutex_exit(&srd->srd_mutex);
	qprocsoff(rq);
	flushq(rq, FLUSHALL);
	flushq(wq, FLUSHALL);

	mutex_enter(&srd->srd_mutex);
	rq->q_ptr = NULL;
	wq->q_ptr = NULL;
	srd->srd_tty.t_writeq = NULL;
	srd->srd_tty.t_readq = NULL;
	srd->srd_setup &= ~SERDEV_SETUP_STREAMS;

	serdev_open_teardown(srd);
	mutex_exit(&srd->srd_mutex);
	return (0);
}

static void
serdev_ioctl(serdev_t *srd, mblk_t *mp)
{
	/*
	 * This function is run from the write queue service routine.  The
	 * queues remain available until at least after qprocsoff() returns
	 * during cleanup.
	 */
	VERIFY(MUTEX_NOT_HELD(&srd->srd_mutex));

	tty_common_t *t = &srd->srd_tty;
	queue_t *q = t->t_writeq;
	struct iocblk *ioc = (struct iocblk *)mp->b_rptr;
	mblk_t *data;

	/*
	 * XXX Explain why we do this here.
	 */
	ttycommon_iocpending_discard(t);

	switch (ioc->ioc_cmd) {
	case CONSOPENPOLLEDIO:
	case CONSCLOSEPOLLEDIO:
	case CONSSETABORTENABLE:
	case CONSGETABORTENABLE:
		/*
		 * We do not support polled console I/O.
		 */
		miocnak(q, mp, 0, EINVAL);
		return;

	case TIOCSILOOP:
	case TIOCCILOOP:
		/*
		 * We do not support loopback testing.
		 */
		miocnak(q, mp, 0, EINVAL);
		return;

	case TIOCMGET:
	case TIOCMBIC:
	case TIOCMBIS:
	case TIOCMSET:
	case TCSBRK:
		/*
		 * We handle these ourselves without help from
		 * ttycommon_ioctl().
		 */
		goto ours;

	default:
		/*
		 * Try the tty common ioctl code.
		 */
		break;
	}

	int error;
	size_t failsz = ttycommon_ioctl(t, q, mp, &error);
	if (failsz != 0) {
		/*
		 * For the ioctl() commands that read data back to the user,
		 * ttycommon_ioctl() may need to allocate a buffer for the
		 * reply.  If there was not enough memory to do that, the tty
		 * code will have put the ioctl message in the pending slot and
		 * we will schedule another attempt once memory becomes
		 * available.
		 */
		serdev_bufcall_schedule(srd, failsz, SERDEV_BUFCALL_WRITE);
		return;
	}

	if (error != 0) {
		if (error < 0) {
			/*
			 * The tty common code did not understand this ioctl
			 * and it is not one of the ones we are handling on our
			 * own.
			 */
			error = EINVAL;
		}
		miocnak(q, mp, 0, error);
		return;
	}

ours:
	switch (ioc->ioc_cmd) {
	case TCSETS:
	case TCSETSW:
	case TCSETSF:
	case TCSETA:
	case TCSETAW:
	case TCSETAF:
		/*
		 * The tty common code has already flushed our read side
		 * STREAMS queue for the F command variants.  For both F and W
		 * variants we need to ensure the driver has transmitted
		 * everything that came before this call.
		 */
		if (ioc->ioc_cmd != TCSETS && ioc->ioc_cmd != TCSETA) {
			(void) srd->srd_ops.srdo_flush_tx(srd->srd_private);
		}

		/*
		 * Re-program the serial line based on the updated tty flags:
		 */
		mutex_enter(&srd->srd_mutex);
		error = serdev_configure(srd);
		mutex_exit(&srd->srd_mutex);
		qreply(q, mp);
		return;

	case TCSBRK:
		if ((error = miocpullup(mp, sizeof (int))) != 0) {
			miocnak(q, mp, 0, error);
			return;
		}

		if (*(int *)mp->b_cont->b_rptr == 0) {
			/*
			 * XXX Initiate a timed break.
			 */
			miocnak(q, mp, 0, EIO);
			return;
		}

		/*
		 * Otherwise, we just need to wait for outbound data flush to
		 * occur.
		 */
		if ((error =
		    srd->srd_ops.srdo_drain(srd->srd_private, -1)) != 0) {
			miocnak(q, mp, 0, error);
		} else {
			miocack(q, mp, 0, 0);
		}
		return;

	case TIOCMGET:
		/*
		 * Get all modem control status bits.
		 */
		if ((data = allocb(sizeof (int), BPRI_HI)) == NULL) {
			/*
			 * If we could not allocate, stash this ioctl in the
			 * pending slot and request another attempt.
			 */
			ttycommon_iocpending_set(t, mp);
			serdev_bufcall_schedule(srd, sizeof (int),
			    SERDEV_BUFCALL_WRITE);
			return;
		}

		*(uint_t *)mp->b_rptr = 0;
		if (srd->srd_ops.srdo_modem_get(srd->srd_private,
		    UINT_MAX, (uint_t *)mp->b_rptr) != 0) {
			miocnak(q, mp, 0, EIO);
			return;
		}

		if (ioc->ioc_count == TRANSPARENT) {
			mcopyout(mp, NULL, sizeof (int), NULL, data);
		} else {
			mioc2ack(mp, data, sizeof (int), 0);
		}
		qreply(q, mp);
		return;

	case TIOCMBIC:
	case TIOCMBIS:
	case TIOCMSET:
		/*
		 * XXX
		 */
		miocnak(q, mp, 0, EIO);
		return;

	default:
		qreply(q, mp);
		return;
	}
}

static void
serdev_iocdata(serdev_t *srd, mblk_t *mp)
{
	VERIFY(MUTEX_NOT_HELD(&srd->srd_mutex));

	queue_t *q = srd->srd_tty.t_writeq;
	struct copyresp *cp = (struct copyresp *)mp->b_rptr;

	if (cp->cp_rval != 0) {
		freemsg(mp);
		return;
	}

	switch (cp->cp_cmd) {
	case TIOCMGET:
		miocack(q, mp, 0, 0);
		return;

	case TIOCMBIC:
	case TIOCMBIS:
	case TIOCMSET:
		/*
		 * XXX
		 */
		miocnak(q, mp, 0, EINVAL);
		return;

	/*
	 * XXX
	 */
	default:
		miocnak(q, mp, 0, EINVAL);
		return;
	}
}

int
serdev_wput(queue_t *q, mblk_t *mp)
{
	serdev_t *srd = q->q_ptr;

	VERIFY(MUTEX_NOT_HELD(&srd->srd_mutex));

	switch (DB_TYPE(mp)) {
	case M_STOP:
		mutex_enter(&srd->srd_mutex);
		serdev_flow_out_stop(srd, SERDEV_STOP_TX_USER);
		mutex_exit(&srd->srd_mutex);
		break;

	case M_START:
		mutex_enter(&srd->srd_mutex);
		serdev_flow_out_start(srd, SERDEV_STOP_TX_USER);
		mutex_exit(&srd->srd_mutex);
		break;

	case M_STOPI:
		mutex_enter(&srd->srd_mutex);
		serdev_flow_in_stop(srd, SERDEV_STOP_RX_USER);
		mutex_exit(&srd->srd_mutex);
		break;

	case M_STARTI:
		mutex_enter(&srd->srd_mutex);
		serdev_flow_in_start(srd, SERDEV_STOP_RX_USER);
		mutex_exit(&srd->srd_mutex);
		break;

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			/*
			 * Have the device driver flush anything else:
			 */
			(void) srd->srd_ops.srdo_flush_rx(srd->srd_private);

			*mp->b_rptr &= ~FLUSHW;
		}
		if (*mp->b_rptr & FLUSHR) {
			/*
			 * Flush any data we are holding:
			 */
			flushq(RD(q), FLUSHDATA);

			/*
			 * Have the device driver flush anything else:
			 */
			(void) srd->srd_ops.srdo_flush_rx(srd->srd_private);

			/*
			 * Pass the flush message back up the stream.
			 */
			qreply(q, mp);
			return (0);
		}
		break;

	case M_IOCDATA:
	case M_IOCTL:
	case M_BREAK:
	case M_DELAY:
	case M_DATA:
		/*
		 * Only push messages onto the write queue if the port is not
		 * closing down.
		 */
		mutex_enter(&srd->srd_mutex);
		bool open = (srd->srd_state == SERDEV_ST_OPEN);
		mutex_exit(&srd->srd_mutex);
		if (!open) {
			break;
		}
		if (putq(q, mp) != 1) {
			/*
			 * XXX record error?
			 */
			break;
		}
		return (0);

	default:
		break;
	}

	freemsg(mp);
	return (0);
}

int
serdev_wsrv(queue_t *q)
{
	serdev_t *srd = q->q_ptr;
	bool try_pending_ioctl = true;

	VERIFY(MUTEX_NOT_HELD(&srd->srd_mutex));

	for (;;) {
		mblk_t *mp = NULL;

		/*
		 * XXX Explain this properly.
		 * If we have a pending ioctl() we should try to service that
		 * first, but only once per service routine activation.
		 */
		if (try_pending_ioctl) {
			tty_common_t *t = &srd->srd_tty;
			if ((mp = ttycommon_iocpending_take(t)) != NULL) {
				try_pending_ioctl = false;
			}
		}

		if (mp == NULL) {
			/*
			 * Otherwise, try to pull messages from the write
			 * queue.
			 */
			mp = getq(q);
		}

		if (mp == NULL) {
			/*
			 * Nothing left to do!
			 */
			mutex_enter(&srd->srd_mutex);
			if (srd->srd_flags & SERDEV_FL_NEED_DRAIN) {
				/*
				 * We are asleep in serdev_close() waiting for
				 * the queue to drain.
				 */
				srd->srd_flags &= ~SERDEV_FL_NEED_DRAIN;
				cv_broadcast(&srd->srd_cv);
			}
			mutex_exit(&srd->srd_mutex);
			return (0);
		}

		switch (DB_TYPE(mp)) {
		case M_STOP:
		case M_START:
		case M_STOPI:
		case M_STARTI:
		case M_FLUSH:
			/*
			 * These high priority messages should have been
			 * processed by serdev_wput().
			 */
			panic("unexpected high priority message %p", mp);
			break;

		case M_IOCDATA:
			serdev_iocdata(srd, mp);
			continue;

		case M_IOCTL:
			serdev_ioctl(srd, mp);
			continue;

		case M_DELAY:
		case M_BREAK:
		case M_DATA:
			/*
			 * Process these below if we are not on hold.
			 */
			break;

		default:
			/*
			 * Unrecognised messages must be freed.
			 */
			freemsg(mp);
			continue;
		}

		/*
		 * If we receive a normal priority message that is not an ioctl
		 * request, we can try a pending ioctl again next turn.
		 */
		try_pending_ioctl = true;

		mutex_enter(&srd->srd_mutex);
		if ((srd->srd_flags & SERDEV_FL_TX_STOPPED) ||
		    (srd->srd_flags & SERDEV_FL_TX_ACTIVE)) {
			mutex_exit(&srd->srd_mutex);

			/*
			 * We are not sending right now; put it back on the
			 * queue.
			 */
			if (putbq(q, mp) != 1) {
				/*
				 * XXX report failure?
				 */
				freemsg(mp);
			}

			return (0);
		}

		switch (DB_TYPE(mp)) {
		case M_DELAY:
			(void) serdev_tx_start_delay(srd,
			    (clock_t)(*(uchar_t *)mp->b_rptr + 6));
			break;

		case M_BREAK:
			(void) serdev_tx_start_break(srd);
			break;

		case M_DATA:
			/*
			 * Push the data into the driver.  The driver keeps the
			 * message so we do not need to free it here.
			 */
			srd->srd_flags |= SERDEV_FL_TX_ACTIVE;
			mutex_exit(&srd->srd_mutex);
			srd->srd_ops.srdo_tx(srd->srd_private, mp);
			continue;
		}

		mutex_exit(&srd->srd_mutex);
		freemsg(mp);
	}
}

int
serdev_rsrv(queue_t *q)
{
	serdev_t *srd = q->q_ptr;
	tty_common_t *t = &srd->srd_tty;

	mutex_enter(&srd->srd_mutex);
	/*
	 * First, determine if we need to push up a change in our hangup state.
	 */
hangup:
	if (srd->srd_flags & SERDEV_FL_OFF_HOOK) {
		if (!(t->t_cflag & CLOCAL)) {
			/*
			 * This is not a local line, so we may need to update
			 * the hangup state.
			 */
			if ((srd->srd_flags & SERDEV_FL_CARRIER_LOSS) ||
			    !(srd->srd_flags & SERDEV_FL_CARRIER_DETECT)) {
				mutex_exit(&srd->srd_mutex);
				int r = putnextctl(q, M_HANGUP);
				mutex_enter(&srd->srd_mutex);

				if (r == 1) {
					srd->srd_flags &=
					    ~SERDEV_FL_CARRIER_LOSS;
					srd->srd_flags &= ~SERDEV_FL_OFF_HOOK;
					goto hangup;
				} else {
					/*
					 * XXX sched bufcall READ
					 */
				}
			}
		}
	} else {
		if ((t->t_cflag & CLOCAL) ||
		    (srd->srd_flags & SERDEV_FL_CARRIER_DETECT)) {
			/*
			 * Either this is a local line, or we have detected
			 * carrier.
			 */
			mutex_exit(&srd->srd_mutex);
			int r = putnextctl(q, M_UNHANGUP);
			mutex_enter(&srd->srd_mutex);

			if (r == 1) {
				srd->srd_flags |= SERDEV_FL_OFF_HOOK;
			} else {
				/*
				 * XXX sched bufcall READ
				 */
			}
		}
	}

	/*
	 * When our read side service routine is called, we are no longer
	 * blocked because of flow control on the stream.  We may still be
	 * blocked for other reasons.
	 */
	serdev_flow_in_start(srd, SERDEV_STOP_RX_STREAMS);

	if (srd->srd_flags & SERDEV_FL_RX_STOPPED) {
		/*
		 * We will not service the queue until we are allowed to do so.
		 */
		mutex_exit(&srd->srd_mutex);
		return (0);
	}
	mutex_exit(&srd->srd_mutex);

	for (;;) {
		mblk_t *mp;

		if (!canputnext(q)) {
			/*
			 * Record the fact that we are stopped because of flow
			 * control on the stream.
			 */
			goto park;
		}

		if ((mp = getq(q)) == NULL) {
			/*
			 * If we do not have any queued data to process, ask
			 * for more data from the device.
			 */
			srd->srd_ops.srdo_rx(srd->srd_private);
			return (0);
		}
		VERIFY3P(mp->b_cont, ==, NULL);

		switch (DB_TYPE(mp)) {
		case M_DATA:
			/*
			 * Correctly received data from the device.
			 */
			if (serdev_rx_transform_data(srd, &mp) == 0) {
				putnext(q, mp);
			} else {
				/*
				 * We needed to allocate to transform the
				 * message but were unable.  Put it back for
				 * next time.
				 */
				if (putbq(q, mp) != 1) {
					/*
					 * XXX record failure?
					 */
					freemsg(mp);
				}

				goto park;
			}
			break;

		case M_BREAK:
			/*
			 * Framing and parity errors.
			 */
			if (MBLKL(mp) != 2) {
				/*
				 * We expect a particular format for errors
				 * from the device and this is not well-formed.
				 */
				freemsg(mp);
			} else {
				serdev_rx_transform_break(srd, mp);
				putnext(q, mp);
			}
			break;

		default:
			/*
			 * We don't expect any other kinds of messages from
			 * devices.
			 * XXX
			 */
			freemsg(mp);
			break;
		}
	}

park:
	mutex_enter(&srd->srd_mutex);
	serdev_flow_in_stop(srd, SERDEV_STOP_RX_STREAMS);
	mutex_exit(&srd->srd_mutex);
	return (0);
}

static struct module_info serdev_modinfo = {
	.mi_idnum =		0,
	.mi_idname =		"serdev",
	.mi_minpsz =		0,
	.mi_maxpsz =		INFPSZ,
	.mi_hiwat =		128 * 1024,
	.mi_lowat =		4 * 1024,
};

static struct qinit serdev_rinit = {
	.qi_putp =		putq,		/* XXX ? */
	.qi_srvp =		serdev_rsrv,
	.qi_qopen =		serdev_open,
	.qi_qclose =		serdev_close,
	.qi_minfo =		&serdev_modinfo,
};

static struct qinit serdev_winit = {
	.qi_putp =		serdev_wput,
	.qi_srvp =		serdev_wsrv,
	.qi_minfo =		&serdev_modinfo,
};

static struct streamtab serdev_stream_info = {
	.st_rdinit =		&serdev_rinit,
	.st_wrinit =		&serdev_winit,
};

static struct cb_ops serdev_cb_ops = {
	.cb_rev =		CB_REV,
	.cb_flag =		D_64BIT | D_NEW | D_MP | D_HOTPLUG,

	/*
	 * XXX This is a STREAMS device, which means most of the entry points
	 * are not used here.
	 */
	.cb_str =		&serdev_stream_info,
	.cb_open =		nulldev,
	.cb_close =		nulldev,
	.cb_strategy =		nodev,
	.cb_print =		nodev,
	.cb_dump =		nodev,
	.cb_read =		nodev,
	.cb_write =		nodev,
	.cb_ioctl =		nodev,
	.cb_devmap =		nodev,
	.cb_mmap =		nodev,
	.cb_segmap =		nodev,
	.cb_chpoll =		nochpoll,
	.cb_prop_op =		ddi_prop_op,
	.cb_aread =		nodev,
	.cb_awrite =		nodev,
};

static struct dev_ops serdev_dev_ops = {
	.devo_rev =		DEVO_REV,

	.devo_getinfo =		serdev_getinfo,
	.devo_attach =		serdev_attach,
	.devo_detach =		serdev_detach,
	.devo_cb_ops =		&serdev_cb_ops,

	.devo_identify =	nulldev,
	.devo_probe =		nulldev,
	.devo_reset =		nodev,
	.devo_quiesce =		ddi_quiesce_not_needed,
};

static struct modldrv serdev_modldrv = {
	.drv_modops =		&mod_driverops,
	.drv_linkinfo =		"generic serial device",
	.drv_dev_ops =		&serdev_dev_ops,
};

static struct modlinkage serdev_modlinkage = {
	.ml_rev =		MODREV_1,
	.ml_linkage =		{ &serdev_modldrv, NULL, },
};

int
_init(void)
{
	int r;

	if ((r = ddi_soft_state_init(&serdev_state, sizeof (serdev_t), 0)) !=
	    DDI_SUCCESS) {
		return (r);
	}

	if ((r = mod_install(&serdev_modlinkage)) != DDI_SUCCESS) {
		ddi_soft_state_fini(&serdev_state);
	}

	return (r);
}

int
_fini(void)
{
	int r;

	if ((r = mod_remove(&serdev_modlinkage)) == DDI_SUCCESS) {
		ddi_soft_state_fini(&serdev_state);
	}
	return (r);
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&serdev_modlinkage, mi));
}
