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
 * Copyright 2021 Oxide Computer Company
 */

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/serdev.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>

#define	USBDRV_MAJOR_VER	2
#define	USBDRV_MINOR_VER	0
#include <sys/usb/usba.h>
#include <sys/usb/usba/usbai_private.h>

#include <sys/usb/clients/usbftdi/uftdi_reg.h>

#include "usbftdi.h"

static void uftdi_rx_start(uftdi_t *, uftdi_if_t *);
static void uftdi_tx_start(uftdi_t *, uftdi_if_t *);

void *uftdi_state;

/*
 * Baud rate and latency timer table.  There is one slot in the table for each
 * baud rate in "sys/termios.h".  Any unsupported baud rate has a zero value
 * for "usp_timer".  The values for "usp_timer" are specified in milliseconds.
 *
 * Data received by the device is retrieved through a bulk input pipe.  The
 * device batches received data to entirely fill reply messages before passing
 * them to the host.  The latency timer value determines how long the device
 * will accumulate bytes before it will give up on waiting and send a short
 * message.
 *
 * If not explicitly programmed the latency timer is generally set at 16ms,
 * meaning the device will wait up to 16ms before it will return a short batch
 * of data.  Setting the latency timer value appropriately for all applications
 * is difficult, as it represents a trade-off between CPU and USB resources
 * consumed in more frequent but shorter (or even empty) USB packets against
 * artificial latency experienced by protocols that are routinely composed of
 * messages shorter than our receive buffer size.
 *
 * An example of a protocol with short messages is XMODEM, where each 1KB data
 * block is acknowledged by a single byte message in response.  There are no
 * sliding windows or deferred acknowledgements in the protocol, so an
 * injection of 16ms of latency for each acknowledgement sets a hard cap on
 * transfer speed of (1000ms / 16ms * 1KB) or 62.5KB/s.  This is not much of a
 * problem for the classical lower baud rates, but becomes a challenge on more
 * modern and capable systems at around 1Mbaud.  To improve XMODEM performance
 * on faster links, at the expense of increased CPU and USB activity, we set
 * the latency timer to lower values for higher baud rates.
 */
uftdi_speed_params_t uftdi_params[32] = {
	[B300] =	{ .usp_baud = ftdi_8u232am_b300,     .usp_timer = 16 },
	[B600] =	{ .usp_baud = ftdi_8u232am_b600,     .usp_timer = 16 },
	[B1200] =	{ .usp_baud = ftdi_8u232am_b1200,    .usp_timer = 16 },
	[B2400] =	{ .usp_baud = ftdi_8u232am_b2400,    .usp_timer = 16 },
	[B4800] =	{ .usp_baud = ftdi_8u232am_b4800,    .usp_timer = 16 },
	[B9600] =	{ .usp_baud = ftdi_8u232am_b9600,    .usp_timer = 16 },
	[B19200] =	{ .usp_baud = ftdi_8u232am_b19200,   .usp_timer = 16 },
	[B38400] =	{ .usp_baud = ftdi_8u232am_b38400,   .usp_timer = 16 },
	[B57600] =	{ .usp_baud = ftdi_8u232am_b57600,   .usp_timer = 16 },
	[B115200] =	{ .usp_baud = ftdi_8u232am_b115200,  .usp_timer = 16 },
	[B230400] =	{ .usp_baud = ftdi_8u232am_b230400,  .usp_timer = 16 },
	[B460800] =	{ .usp_baud = ftdi_8u232am_b460800,  .usp_timer = 16 },
	[B921600] =	{ .usp_baud = ftdi_8u232am_b921600,  .usp_timer = 8 },
	[B2000000] =	{ .usp_baud = ftdi_8u232am_b2000000, .usp_timer = 1 },
	[B3000000] =	{ .usp_baud = ftdi_8u232am_b3000000, .usp_timer = 1 },
};


static size_t
uftdi_buf_size(usb_ep_data_t *ep, size_t bus_max)
{
	size_t sz;

	if ((sz = ep->ep_descr.wMaxPacketSize) == 0) {
		/*
		 * If the endpoint does not specify a maximum packet size, we
		 * default to a small size that is believed to work with older
		 * devices.
		 */
		sz = 64;
	}

	if (sz > bus_max) {
		sz = bus_max;
	}

	return (sz);
}

static bool
uftdi_pipe_hold(uftdi_t *uf, uftdi_if_t *ui, uftdi_pipe_t *up)
{
	if (uf->uf_usb_thread != NULL || ui->ui_state != UFTDI_ST_OPEN) {
		/*
		 * We are undergoing USB reconfiguration, or the port is not
		 * open, so we cannot hold the pipe now.
		 */
		return (false);
	}

	if (up->up_state == UFTDI_PIPE_IDLE) {
		up->up_state = UFTDI_PIPE_BUSY;
		return (true);
	}

	return (false);
}

static void
uftdi_pipe_release(uftdi_t *uf, uftdi_if_t *ui, uftdi_pipe_t *up)
{
	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	VERIFY3U(up->up_state, ==, UFTDI_PIPE_BUSY);

	if (up->up_pipe == 0) {
		/*
		 * uftdi_pipe_remove() was called to tear down the pipe while
		 * the pipe was in use.
		 */
		up->up_state = UFTDI_PIPE_CLOSED;
	} else {
		up->up_state = UFTDI_PIPE_IDLE;
	}

	cv_broadcast(&uf->uf_cv);
}

static void
uftdi_pipe_wait(uftdi_t *uf, uftdi_if_t *ui, uftdi_pipe_t *up)
{
	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	while (up->up_state == UFTDI_PIPE_BUSY) {
		cv_wait(&uf->uf_cv, &uf->uf_mutex);
	}
}

static void
uftdi_pipe_install(uftdi_pipe_t *up, usb_pipe_handle_t pipe, size_t buf_size)
{
	VERIFY3U(up->up_state, ==, UFTDI_PIPE_CLOSED);
	up->up_state = UFTDI_PIPE_IDLE;

	VERIFY3U(up->up_pipe, ==, 0);
	up->up_pipe = pipe;
	up->up_bufsz = buf_size;
}

static usb_pipe_handle_t
uftdi_pipe_remove(uftdi_pipe_t *up)
{
	VERIFY3U(up->up_state, !=, UFTDI_PIPE_CLOSED);
	if (up->up_state == UFTDI_PIPE_IDLE) {
		/*
		 * If the pipe is idle, mark it closed immediately.  Otherwise
		 * we want to wait until the in flight request has released it
		 * before continuing.
		 */
		up->up_state = UFTDI_PIPE_CLOSED;
	}

	VERIFY3U(up->up_pipe, !=, 0);
	usb_pipe_handle_t pipe = up->up_pipe;
	up->up_pipe = 0;
	up->up_bufsz = 0;

	return (pipe);
}

static int
uftdi_open_pipes_one(uftdi_t *uf, uftdi_if_t *ui, size_t maxb)
{
	dev_info_t *dip = uf->uf_dip;
	usb_client_dev_data_t *dev = uf->uf_usb_dev;
	usb_ep_data_t *epin, *epout;

	/*
	 * First, make sure we have access to the USB interface we expect to
	 * use here:
	 */
	if (ui->ui_usb_if >= dev->dev_curr_cfg->cfg_n_if) {
		dev_err(dip, CE_WARN, "device does not have interface %u",
		    ui->ui_usb_if);
		return (USB_FAILURE);
	}

	if ((epin = usb_lookup_ep_data(dip, dev, ui->ui_usb_if, 0, 0,
	    USB_EP_ATTR_BULK, USB_EP_DIR_IN)) == NULL ||
	    (epout = usb_lookup_ep_data(dip, dev, ui->ui_usb_if, 0, 0,
	    USB_EP_ATTR_BULK, USB_EP_DIR_OUT)) == NULL) {
		dev_err(dip, CE_WARN, "could not locate endpoints for "
		    "interface %u", ui->ui_usb_if);
		return (USB_FAILURE);
	}

	/*
	 * Open the bulk input and output pipes:
	 */
	usb_pipe_policy_t policy = {
		.pp_max_async_reqs = 2,
	};
	usb_pipe_handle_t pin = 0;
	usb_pipe_handle_t pout = 0;
	if (usb_pipe_open(dip, &epin->ep_descr, &policy, USB_FLAGS_SLEEP,
	    &pin) != USB_SUCCESS ||
	    usb_pipe_open(dip, &epout->ep_descr, &policy, USB_FLAGS_SLEEP,
	    &pout) != USB_SUCCESS) {
		if (pin != 0) {
			usb_pipe_close(dip, pin, USB_FLAGS_SLEEP, NULL, NULL);
		}
		if (pout != 0) {
			usb_pipe_close(dip, pout, USB_FLAGS_SLEEP, NULL, NULL);
		}
		return (USB_FAILURE);
	}

	mutex_enter(&uf->uf_mutex);
	uftdi_pipe_install(&ui->ui_pipe_in, pin, uftdi_buf_size(epin, maxb));
	uftdi_pipe_install(&ui->ui_pipe_out, pout, uftdi_buf_size(epout, maxb));
	mutex_exit(&uf->uf_mutex);

	return (USB_SUCCESS);
}

static int
uftdi_open_pipes(uftdi_t *uf)
{
	dev_info_t *dip = uf->uf_dip;
	size_t maxb;

	/*
	 * If we are to adjust the pipes, we must be the only USB configuration
	 * thread.
	 */
	VERIFY3P(uf->uf_usb_thread, ==, curthread);

	if (usb_pipe_get_max_bulk_transfer_size(dip, &maxb) != USB_SUCCESS) {
		return (USB_FAILURE);
	}

	int r = USB_SUCCESS;
	for (uint_t i = 0; i < uf->uf_nif; i++) {
		if (uftdi_open_pipes_one(uf, uf->uf_if[i], maxb) !=
		    USB_SUCCESS) {
			r = USB_FAILURE;
		}
	}

	return (r);
}

static void
uftdi_close_pipes(uftdi_t *uf)
{
	dev_info_t *dip = uf->uf_dip;

	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	/*
	 * If we are to adjust the pipes, we must be the only USB configuration
	 * thread.
	 */
	VERIFY3P(uf->uf_usb_thread, ==, curthread);

	for (uint_t i = 0; i < uf->uf_nif; i++) {
		uftdi_if_t *ui = uf->uf_if[i];

		usb_pipe_handle_t pin = uftdi_pipe_remove(&ui->ui_pipe_in);
		usb_pipe_handle_t pout = uftdi_pipe_remove(&ui->ui_pipe_out);

		mutex_exit(&uf->uf_mutex);
		usb_pipe_close(dip, pin, USB_FLAGS_SLEEP, NULL, NULL);
		usb_pipe_close(dip, pout, USB_FLAGS_SLEEP, NULL, NULL);
		mutex_enter(&uf->uf_mutex);

		/*
		 * If a pipe was in use while we were trying to close it down,
		 * wait for it to be released by the callback:
		 */
		uftdi_pipe_wait(uf, ui, &ui->ui_pipe_in);
		uftdi_pipe_wait(uf, ui, &ui->ui_pipe_out);
	}
}

/*
 * Send a control command to the device.
 */
static int
uftdi_send_command(uftdi_t *uf, uint8_t port, uchar_t reqno, uint16_t val,
    uint8_t hindex)
{
	VERIFY(MUTEX_NOT_HELD(&uf->uf_mutex));

	usb_ctrl_setup_t req = {
		.bmRequestType = USB_DEV_REQ_TYPE_VENDOR |
		    USB_DEV_REQ_HOST_TO_DEV,
		.bRequest = reqno,
		.wValue = val,
		.wIndex = port | ((0xff & hindex) << 8),
		.wLength = 0,
		.attrs = USB_ATTRS_NONE,
	};

	return (usb_pipe_ctrl_xfer_wait(uf->uf_usb_dev->dev_default_ph,
	    &req, NULL, NULL, NULL, 0));
}

static void
uftdi_try(bool *fail, int result)
{
	if (result != USB_SUCCESS) {
		*fail = true;
	}
}

/*
 * Try to program the registers which control the baud rate, data settings,
 * flow control, and latency timer.  We will always try to program everything,
 * even if some of the commands fail, reporting success or failure at the end
 * of the multi-step process.
 */
static bool
uftdi_program_try(uftdi_t *uf, uint8_t port, const uftdi_regs_t *ur)
{
	bool fail = false;

	uftdi_try(&fail, uftdi_send_command(uf, port,
	    FTDI_SIO_SET_BAUD_RATE, ur->ur_baud, 0));

	uftdi_try(&fail, uftdi_send_command(uf, port,
	    FTDI_SIO_SET_DATA, ur->ur_data, 0));

	uftdi_try(&fail, uftdi_send_command(uf, port,
	    FTDI_SIO_SET_FLOW_CTRL, ur->ur_flowval, ur->ur_flowproto));

	uftdi_try(&fail, uftdi_send_command(uf, port,
	    FTDI_SIO_SET_TIMER, ur->ur_timer, 0));

	return (!fail);
}

/*
 * Write the register set to the device and update the state structure.
 * If there are errors, return the device to its previous state.
 */
static int
uftdi_program(uftdi_if_t *ui, const uftdi_regs_t *ur)
{
	uftdi_t *uf = ui->ui_parent;
	VERIFY(MUTEX_NOT_HELD(&uf->uf_mutex));

	mutex_enter(&uf->uf_mutex);
	uint8_t port = ui->ui_port;
	mutex_exit(&uf->uf_mutex);

	if (!uftdi_program_try(uf, port, ur)) {
		/*
		 * If any command failed, we attempt to undo the entire state
		 * change by reprogramming the device to our original values.
		 */
		mutex_enter(&uf->uf_mutex);
		uftdi_regs_t urold = ui->ui_last_regs;
		mutex_exit(&uf->uf_mutex);

		(void) uftdi_program_try(uf, port, &urold);
		return (USB_FAILURE);
	}

	/*
	 * Save the updated values:
	 */
	mutex_enter(&uf->uf_mutex);
	ui->ui_last_regs = *ur;
	mutex_exit(&uf->uf_mutex);
	return (USB_SUCCESS);
}

static int
uftdi_set_dtr(uftdi_if_t *ui, bool on)
{
	uftdi_t *uf = ui->ui_parent;
	uint16_t mctl = on ? FTDI_SIO_SET_DTR_HIGH : FTDI_SIO_SET_DTR_LOW;

	if (uftdi_send_command(uf, ui->ui_port,
	    FTDI_SIO_MODEM_CTRL, mctl, 0) != USB_SUCCESS) {
		return (EIO);
	}

	mutex_enter(&uf->uf_mutex);
	if (on) {
		ui->ui_last_mctl |= UFTDI_MODEM_DTR;
	} else {
		ui->ui_last_mctl &= ~UFTDI_MODEM_DTR;
	}
	mutex_exit(&uf->uf_mutex);

	return (0);
}

static int
uftdi_set_rts(uftdi_if_t *ui, bool on)
{
	uftdi_t *uf = ui->ui_parent;
	uint16_t mctl = on ? FTDI_SIO_SET_RTS_HIGH : FTDI_SIO_SET_RTS_LOW;

	if (uftdi_send_command(uf, ui->ui_port,
	    FTDI_SIO_MODEM_CTRL, mctl, 0) != 0) {
		return (EIO);
	}

	mutex_enter(&uf->uf_mutex);
	if (on) {
		ui->ui_last_mctl |= UFTDI_MODEM_RTS;
	} else {
		ui->ui_last_mctl &= ~UFTDI_MODEM_RTS;
	}
	mutex_exit(&uf->uf_mutex);

	return (0);
}

static int
uftdi_reset(uftdi_if_t *ui)
{
	return (uftdi_send_command(ui->ui_parent, ui->ui_port,
	    FTDI_SIO_RESET, FTDI_SIO_RESET_SIO, 0));
}

static void
uftdi_rx_purge(uftdi_if_t *ui)
{
	(void) uftdi_send_command(ui->ui_parent, ui->ui_port,
	    FTDI_SIO_RESET, FTDI_SIO_RESET_PURGE_RX, 0);
}

static void
uftdi_tx_purge(uftdi_if_t *ui)
{
	(void) uftdi_send_command(ui->ui_parent, ui->ui_port,
	    FTDI_SIO_RESET, FTDI_SIO_RESET_PURGE_TX, 0);
}

/*
 * Is the device transmit buffer empty?
 */
static bool
uftdi_tx_empty(uftdi_t *uf, uftdi_if_t *ui)
{
	const uint8_t txempty = FTDI_LSR_STATUS_TEMT | FTDI_LSR_STATUS_THRE;

	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	return ((ui->ui_last_lsr & txempty) == txempty);
}

/*
 * Receive errors are communicated to the serdev framework through specially
 * formatted M_BREAK messages.  Each message has two bytes: a serdev_error_t
 * value, followed by a single byte of data.
 */
static bool
uftdi_rx_error(uftdi_if_t *ui, mblk_t *mp, uint8_t lsr)
{
	uftdi_t *uf = ui->ui_parent;

	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	serdev_error_t sre = 0;

	if (lsr & FTDI_LSR_STATUS_OE) {
		sre |= SERDEV_ERROR_OVERRUN;
	}

	/*
	 * If a break was detected, ignore parity and framing errors.
	 */
	if (lsr & FTDI_LSR_STATUS_BI) {
		sre |= SERDEV_ERROR_BREAK;
	} else {
		if (lsr & FTDI_LSR_STATUS_FE) {
			sre |= SERDEV_ERROR_FRAMING;
		}
		if (lsr & FTDI_LSR_STATUS_PE) {
			sre |= SERDEV_ERROR_PARITY;
		}
	}

	bool error_sent = false;

	do {
		mutex_exit(&uf->uf_mutex);
		mblk_t *brk = allocb(2, BPRI_HI);
		mutex_enter(&uf->uf_mutex);

		if (brk == NULL) {
			/*
			 * If there is no memory to allocate a block, just
			 * discard the bad data.  If we were not able to
			 * allocate any blocks at all, we will not update the
			 * cached LSR error bits value so that if the error is
			 * still asserted in future we can try again.
			 */
			break;
		}

		DB_TYPE(brk) = M_BREAK;
		*brk->b_wptr++ = (uchar_t)sre;
		if (MBLKL(mp) > 0) {
			*brk->b_wptr++ = *mp->b_rptr++;
		} else {
			/*
			 * If a break was detected we may not receive any data,
			 * just the change in the LSR value.  Insert a zero
			 * data byte so that the message is still well-formed.
			 */
			*brk->b_wptr++ = 0;
		}
		VERIFY3U(MBLKL(brk), ==, 2);

		serdev_handle_rx(ui->ui_serdev, brk);
		error_sent = true;

	} while (MBLKL(mp) > 0);

	return (error_sent);
}

static void
uftdi_pipe_in_complete(uftdi_t *uf, uftdi_if_t *ui)
{
	mutex_enter(&uf->uf_mutex);
	uftdi_pipe_release(uf, ui, &ui->ui_pipe_in);

	/*
	 * Continue receiving:
	 */
	uftdi_rx_start(uf, ui);
	mutex_exit(&uf->uf_mutex);
}

static void
uftdi_pipe_in_err(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	uftdi_if_t *ui = (uftdi_if_t *)req->bulk_client_private;

	/*
	 * If there was an error, just free the request and try again.
	 */
	usb_free_bulk_req(req);

	uftdi_pipe_in_complete(ui->ui_parent, ui);
}

/*
 * This callback fires when we have received new data from the device.
 */
static void
uftdi_pipe_in_cb(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	uftdi_if_t *ui = (uftdi_if_t *)req->bulk_client_private;
	uftdi_t *uf = ui->ui_parent;
	mblk_t *mp = req->bulk_data;

	VERIFY3U(req->bulk_completion_reason, ==, USB_CR_OK);

	if (mp == NULL || MBLKL(mp) < 2) {
		/*
		 * All data read from the input pipe should be prefixed with
		 * two bytes: the Modem Status Register (MSR) value and the
		 * Line Status Register (LSR) value.
		 *
		 * If we don't get at least those two bytes, we do not
		 * understand this message and need to discard it.  The device
		 * will also send us periodic short messages with just these
		 * two register values if no other data is received.
		 */
		goto done;
	}

	uint8_t msr = *mp->b_rptr++;
	uint8_t lsr = *mp->b_rptr++;
	uint8_t rxerr = lsr & FTDI_LSR_RX_ERR;

	mutex_enter(&uf->uf_mutex);
	if (ui->ui_last_msr != msr) {
		/*
		 * The MSR value has changed.  We need to save the updated
		 * value and report the change to the serdev framework.
		 */
		ui->ui_last_msr = msr;
		serdev_handle_report_status(ui->ui_serdev);
	}

	if (ui->ui_last_lsr != lsr) {
		/*
		 * The LSR value has changed.  We need to save the updated
		 * value.  If uftdi_serdev_drain() is waiting for the output
		 * buffer to drain, we need to wake it up so that it can check
		 * the THRE and TEMT bits.
		 */
		ui->ui_last_lsr = lsr;
		cv_broadcast(&uf->uf_cv);
	}

	/*
	 * Look for a receive-side error condition in the LSR bits:
	 */
	bool report_error = false;
	if (rxerr != 0) {
		/*
		 * There is presently a receive-side error condition.
		 */
		if (MBLKL(mp) > 0) {
			/*
			 * We received data with the error bits.  Because of
			 * the cheerfully simplistic nature of the device
			 * protocol, we cannot tell if the error applies to
			 * just this data (e.g., if it is a parity error) or
			 * not.  All we can do is pass on all the data we
			 * received, each byte marked with the detected error.
			 */
			report_error = true;
		} else if (ui->ui_last_rxerr != rxerr) {
			/*
			 * We did not receive any data, but the error bits have
			 * a different value from the last time we communicated
			 * an error to the framework.  If the error represents
			 * a break, we must try to communicate it at least once
			 * even if we were unable to do so last time.
			 */
			report_error = true;
		}
	}

	if (report_error) {
		if (uftdi_rx_error(ui, mp, lsr)) {
			/*
			 * If we were able to report the error condition to the
			 * framework, we can update our cached copy of the
			 * receive error bits.
			 */
			ui->ui_last_rxerr = rxerr;
		}
	} else if (MBLKL(mp) > 0) {
		/*
		 * We had received some data bytes.  Detach the data from the
		 * USB request and pass it to the framework.
		 */
		req->bulk_data = NULL;
		serdev_handle_rx(ui->ui_serdev, mp);
	}

	mutex_exit(&uf->uf_mutex);

done:
	usb_free_bulk_req(req);

	uftdi_pipe_in_complete(uf, ui);
}

static void
uftdi_rx_start(uftdi_t *uf, uftdi_if_t *ui)
{
	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	if (!serdev_handle_running_rx(ui->ui_serdev)) {
		/*
		 * The framework has requested that we stop receiving.
		 */
		return;
	}

	if (!uftdi_pipe_hold(uf, ui, &ui->ui_pipe_in)) {
		/*
		 * The bulk input pipe is busy.
		 */
		return;
	}

	mutex_exit(&uf->uf_mutex);

	usb_bulk_req_t *br = usb_alloc_bulk_req(uf->uf_dip,
	    ui->ui_pipe_in.up_bufsz, USB_FLAGS_SLEEP);
	br->bulk_len = ui->ui_pipe_in.up_bufsz;
	br->bulk_cb = uftdi_pipe_in_cb;
	br->bulk_exc_cb = uftdi_pipe_in_err;
	br->bulk_client_private = (usb_opaque_t)ui;
	br->bulk_attributes = USB_ATTRS_AUTOCLEARING | USB_ATTRS_SHORT_XFER_OK;

	int r = usb_pipe_bulk_xfer(ui->ui_pipe_in.up_pipe, br, 0);
	if (r != USB_SUCCESS) {
		usb_free_bulk_req(br);
	}

	mutex_enter(&uf->uf_mutex);
	if (r != USB_SUCCESS) {
		uftdi_pipe_release(uf, ui, &ui->ui_pipe_in);
	}
}

static void
uftdi_pipe_out_complete(uftdi_t *uf, uftdi_if_t *ui)
{
	mutex_enter(&uf->uf_mutex);
	uftdi_pipe_release(uf, ui, &ui->ui_pipe_out);

	/*
	 * Continue transmitting:
	 */
	uftdi_tx_start(uf, ui);
	mutex_exit(&uf->uf_mutex);
}

static void
uftdi_pipe_out_cb(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	uftdi_if_t *ui = (uftdi_if_t *)req->bulk_client_private;

	VERIFY3U(req->bulk_completion_reason, ==, USB_CR_OK);

	usb_free_bulk_req(req);

	uftdi_pipe_out_complete(ui->ui_parent, ui);
}

static void
uftdi_pipe_out_err(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	uftdi_if_t *ui = (uftdi_if_t *)req->bulk_client_private;
	uftdi_t *uf = ui->ui_parent;
	mblk_t *mp = req->bulk_data;

	if (mp != NULL && MBLKL(mp) > 0) {
		/*
		 * There was an exception sending this data.  Put it back on
		 * the front of the transmit queue so we can try to send it
		 * again.
		 */
		mutex_enter(&uf->uf_mutex);
		if (ui->ui_tx_mp != NULL) {
			linkb(mp, ui->ui_tx_mp);
		}
		ui->ui_tx_mp = mp;
		mutex_exit(&uf->uf_mutex);

		req->bulk_data = NULL;
	}

	usb_free_bulk_req(req);

	uftdi_pipe_out_complete(uf, ui);
}

static void
uftdi_tx_start(uftdi_t *uf, uftdi_if_t *ui)
{
	VERIFY(MUTEX_HELD(&uf->uf_mutex));
	VERIFY(ui->ui_state != UFTDI_ST_CLOSED);

	if (!serdev_handle_running_tx(ui->ui_serdev)) {
		/*
		 * The framework has requested we stop transmitting.
		 */
		return;
	}

	if (!uftdi_pipe_hold(uf, ui, &ui->ui_pipe_out)) {
		/*
		 * The bulk output pipe is busy.
		 */
		return;
	}

	/*
	 * Check to see if we have data left to send to the device:
	 */
	if (ui->ui_tx_mp == NULL) {
		uftdi_pipe_release(uf, ui, &ui->ui_pipe_out);

		/*
		 * Request more data from the framework, and wake anybody that
		 * was sleeping waiting for a drain condition.
		 */
		serdev_handle_report_tx(ui->ui_serdev);
		cv_broadcast(&uf->uf_cv);
		return;
	}

	mblk_t *mp;
	size_t max_size = ui->ui_pipe_out.up_bufsz;
	if (MBLKL(ui->ui_tx_mp) <= max_size) {
		/*
		 * We can pass this block on without allocating or copying, so
		 * just do that.
		 */
		mp = ui->ui_tx_mp;
		ui->ui_tx_mp = unlinkb(mp);
	} else {
		/*
		 * Try to allocate a new message of the appropriate length for
		 * the device.
		 */
		mutex_exit(&uf->uf_mutex);
		if ((mp = allocb(max_size, BPRI_HI)) == NULL) {
			/*
			 * If we cannot allocate a shorter buffer, there is
			 * nothing we can do for now.
			 */
			mutex_enter(&uf->uf_mutex);
			uftdi_pipe_release(uf, ui, &ui->ui_pipe_out);
			return;
		}

		mutex_enter(&uf->uf_mutex);
		VERIFY3S(MBLKL(ui->ui_tx_mp), >, max_size);
		bcopy(ui->ui_tx_mp->b_rptr, mp->b_wptr, max_size);
		ui->ui_tx_mp->b_rptr += max_size;
		mp->b_wptr += max_size;
		VERIFY3S(MBLKL(mp), ==, max_size);
	}

	mutex_exit(&uf->uf_mutex);

	usb_bulk_req_t *br = usb_alloc_bulk_req(uf->uf_dip, 0, USB_FLAGS_SLEEP);
	br->bulk_data = mp;
	br->bulk_len = MBLKL(mp);
	br->bulk_cb = uftdi_pipe_out_cb;
	br->bulk_exc_cb = uftdi_pipe_out_err;
	br->bulk_client_private = (usb_opaque_t)ui;
	br->bulk_attributes = USB_ATTRS_AUTOCLEARING;

	int r = usb_pipe_bulk_xfer(ui->ui_pipe_out.up_pipe, br, 0);

	if (r != USB_SUCCESS) {
		br->bulk_data = NULL;
		usb_free_bulk_req(br);
	}

	mutex_enter(&uf->uf_mutex);

	if (r != USB_SUCCESS) {
		/*
		 * If we could not send to the device, put the unsent data back
		 * at the head of the queue.
		 */
		if (ui->ui_tx_mp != NULL) {
			linkb(mp, ui->ui_tx_mp);
		}
		ui->ui_tx_mp = mp;

		uftdi_pipe_release(uf, ui, &ui->ui_pipe_out);
	}
}

static int
uftdi_regs_set_baudrate(uftdi_regs_t *ur, speed_t speed)
{
	uftdi_speed_params_t *usp;

	if (speed >= ARRAY_SIZE(uftdi_params) ||
	    (usp = &uftdi_params[speed])->usp_timer == 0) {
		return (EINVAL);
	}

	ur->ur_baud = usp->usp_baud;
	ur->ur_timer = usp->usp_timer;
	return (0);
}

static void
uftdi_regs_set_datamode(uftdi_regs_t *ur, uint_t char_size,
    serdev_parity_t parity, uint_t stop_bits)
{
	ur->ur_data = 0;

	if (char_size >= 5 && char_size <= 8) {
		ur->ur_data |= FTDI_SIO_SET_DATA_BITS(char_size);
	} else {
		ur->ur_data |= FTDI_SIO_SET_DATA_BITS(8);
	}

	switch (parity) {
	case SERDEV_PARITY_NONE:
		ur->ur_data |= FTDI_SIO_SET_DATA_PARITY_NONE;
		break;
	case SERDEV_PARITY_EVEN:
		ur->ur_data |= FTDI_SIO_SET_DATA_PARITY_EVEN;
		break;
	case SERDEV_PARITY_ODD:
		ur->ur_data |= FTDI_SIO_SET_DATA_PARITY_ODD;
		break;
	}

	if (stop_bits == 2) {
		ur->ur_data |= FTDI_SIO_SET_DATA_STOP_BITS_2;
	} else {
		VERIFY3U(stop_bits, ==, 1);
		ur->ur_data |= FTDI_SIO_SET_DATA_STOP_BITS_1;
	}
}

static void
uftdi_regs_set_flowcontrol(uftdi_regs_t *ur, bool hardware)
{
	if (hardware) {
		/*
		 * Enable hardware flow control, using the RTS/CTS signals:
		 */
		ur->ur_flowproto = FTDI_SIO_RTS_CTS_HS;
	} else {
		ur->ur_flowproto = FTDI_SIO_DISABLE_FLOW_CTRL;
	}

	/*
	 * This value is only set if we were to configure XON/XOFF style flow
	 * control:
	 */
	ur->ur_flowval = 0;
}

static bool
uftdi_usb_change_start(uftdi_t *uf, bool hotplug)
{
	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	for (;;) {
		if (hotplug && uf->uf_flags & UFTDI_FL_DETACHING) {
			return (false);
		}

		if (uf->uf_usb_thread == NULL) {
			uf->uf_usb_thread = curthread;
			return (true);
		}

		cv_wait(&uf->uf_cv, &uf->uf_mutex);
	}
}

static void
uftdi_usb_change_finish(uftdi_t *uf)
{
	VERIFY(MUTEX_HELD(&uf->uf_mutex));

	VERIFY3P(uf->uf_usb_thread, ==, curthread);
	uf->uf_usb_thread = NULL;

	cv_broadcast(&uf->uf_cv);
}

static int
uftdi_usb_disconnect(dev_info_t *dip)
{
	uftdi_t *uf = ddi_get_soft_state(uftdi_state, ddi_get_instance(dip));

	/*
	 * We need to exclude other asynchronous activity from the driver and
	 * the system.
	 */
	mutex_enter(&uf->uf_mutex);
	if (!uftdi_usb_change_start(uf, true)) {
		/*
		 * If we are detaching, just return immediately.
		 */
		mutex_exit(&uf->uf_mutex);
		return (USB_SUCCESS);
	}

	if (!(uf->uf_flags & UFTDI_FL_USB_CONNECTED)) {
		/*
		 * If we were not previously connected, there is nothing for us
		 * to do here.
		 */
		goto done;
	}
	uf->uf_flags &= ~UFTDI_FL_USB_CONNECTED;

	uftdi_close_pipes(uf);

done:
	uftdi_usb_change_finish(uf);
	mutex_exit(&uf->uf_mutex);
	return (USB_SUCCESS);
}

static int
uftdi_usb_reconnect(dev_info_t *dip)
{
	uftdi_t *uf = ddi_get_soft_state(uftdi_state, ddi_get_instance(dip));

	/*
	 * We need to exclude other asynchronous activity from the driver and
	 * the system.
	 */
	mutex_enter(&uf->uf_mutex);
	if (!uftdi_usb_change_start(uf, true)) {
		/*
		 * If we are detaching, just return immediately.
		 */
		mutex_exit(&uf->uf_mutex);
		return (USB_SUCCESS);
	}

	if (uf->uf_flags & UFTDI_FL_USB_CONNECTED) {
		/*
		 * If we were not previously disconnected, there is nothing for
		 * us to do here.
		 */
		goto done;
	}

	mutex_exit(&uf->uf_mutex);

	if (usb_check_same_device(dip, NULL, USB_LOG_L0,
	    UINT_MAX, USB_CHK_ALL, "usbftdi") != USB_SUCCESS) {
		mutex_enter(&uf->uf_mutex);
		goto done;
	}

	if (uftdi_open_pipes(uf) != USB_SUCCESS) {
		goto done;
	}

	for (uint_t i = 0; i < uf->uf_nif; i++) {
		uftdi_if_t *ui = uf->uf_if[i];

		if (ui->ui_state != UFTDI_ST_OPEN) {
			continue;
		}

		/*
		 * If we were already open for this interface, reset it and
		 * program it with the last set of register values we used.
		 */
		(void) uftdi_reset(ui);
		(void) uftdi_program(ui, &ui->ui_last_regs);
	}

	mutex_enter(&uf->uf_mutex);
	uf->uf_flags |= UFTDI_FL_USB_CONNECTED;

done:
	uftdi_usb_change_finish(uf);
	mutex_exit(&uf->uf_mutex);
	return (USB_SUCCESS);
}

usb_event_t uftdi_usb_events = {
	.disconnect_event_handler =	uftdi_usb_disconnect,
	.reconnect_event_handler =	uftdi_usb_reconnect,
};

static int
uftdi_serdev_open(void *arg)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	if (!(uf->uf_flags & UFTDI_FL_USB_CONNECTED) ||
	    ui->ui_state != UFTDI_ST_CLOSED) {
		mutex_exit(&uf->uf_mutex);
		return (EIO);
	}
	ui->ui_state = UFTDI_ST_OPENING;
	mutex_exit(&uf->uf_mutex);

	/*
	 * Reset the device.
	 */
	if (uftdi_reset(ui) != USB_SUCCESS) {
		mutex_enter(&uf->uf_mutex);
		ui->ui_state = UFTDI_ST_CLOSED;
		mutex_exit(&uf->uf_mutex);
		return (EIO);
	}

	/*
	 * Program sensible defaults; i.e., 9600 8/N/1.
	 */
	uftdi_regs_t ur;

	(void) uftdi_regs_set_baudrate(&ur, B9600);
	uftdi_regs_set_datamode(&ur, 8, SERDEV_PARITY_NONE, 1);
	uftdi_regs_set_flowcontrol(&ur, true);

	if (uftdi_program(ui, &ur) != USB_SUCCESS) {
		mutex_enter(&uf->uf_mutex);
		ui->ui_state = UFTDI_ST_CLOSED;
		mutex_exit(&uf->uf_mutex);
		return (EIO);
	}

	mutex_enter(&uf->uf_mutex);
	ui->ui_state = UFTDI_ST_OPEN;
	uftdi_rx_start(uf, ui);
	mutex_exit(&uf->uf_mutex);

	return (0);
}

static int
uftdi_serdev_close(void *arg)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	VERIFY3U(ui->ui_state, ==, UFTDI_ST_OPEN);
	ui->ui_state = UFTDI_ST_CLOSING;

	/*
	 * Wait for the pipes to be idle.
	 */
	uftdi_pipe_wait(uf, ui, &ui->ui_pipe_in);
	uftdi_pipe_wait(uf, ui, &ui->ui_pipe_out);

	/*
	 * Free any buffered data:
	 */
	mblk_t *mprx = ui->ui_rx_mp;
	ui->ui_rx_mp = NULL;
	mblk_t *mptx = ui->ui_tx_mp;
	ui->ui_tx_mp = NULL;

	mutex_exit(&uf->uf_mutex);

	freemsg(mprx);
	freemsg(mptx);

	/*
	 * Purge the on-device buffers:
	 */
	uftdi_tx_purge(ui);
	uftdi_rx_purge(ui);

	mutex_enter(&uf->uf_mutex);
	VERIFY3U(ui->ui_state, ==, UFTDI_ST_CLOSING);
	ui->ui_state = UFTDI_ST_CLOSED;
	mutex_exit(&uf->uf_mutex);

	return (0);
}

void
uftdi_serdev_rx(void *arg)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	uftdi_rx_start(uf, ui);
	mutex_exit(&uf->uf_mutex);
}

static int
uftdi_serdev_tx(void *arg, mblk_t *mp)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	if (mp != NULL) {
		/*
		 * XXX I don't think we expect overlapping transmission
		 * requests from serdev?
		 */
		VERIFY3P(ui->ui_tx_mp, ==, NULL);
		ui->ui_tx_mp = mp;
	}

	/*
	 * Whether we were given data to send or not, we need to resume
	 * transmission if we were previously stopped for flow control.
	 */
	uftdi_tx_start(uf, ui);
	mutex_exit(&uf->uf_mutex);

	return (0);
}

static int
uftdi_serdev_flush_rx(void *arg)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	mblk_t *mp = ui->ui_rx_mp;
	ui->ui_rx_mp = NULL;
	mutex_exit(&uf->uf_mutex);

	freemsg(mp);

	uftdi_rx_purge(ui);

	return (0);
}

static int
uftdi_serdev_flush_tx(void *arg)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	mblk_t *mp = ui->ui_tx_mp;
	ui->ui_tx_mp = NULL;
	mutex_exit(&uf->uf_mutex);

	freemsg(mp);

	uftdi_tx_purge(ui);

	return (0);
}

static int
uftdi_serdev_drain(void *arg, hrtime_t deadline)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	VERIFY3U(ui->ui_state, ==, UFTDI_ST_OPEN);

	/*
	 * Draining the outbound data is a two-step process. First we must
	 * ensure that all queued data has been sent to the device.  Then, we
	 * wait for the hardware transmit buffer to drain as well.
	 */
	int error = 0;
	for (;;) {
		if (ui->ui_tx_mp == NULL && uftdi_tx_empty(uf, ui)) {
			mutex_exit(&uf->uf_mutex);
			return (0);
		}

		if (error != 0) {
			/*
			 * If the timeout expired or we received a signal, we
			 * cannot wait any longer.
			 */
			mutex_exit(&uf->uf_mutex);
			return (error);
		}

		/*
		 * Wait for the deadline to expire, the status value to be
		 * updated, or for a signal.
		 */
		int r;
		if ((r = cv_timedwait_sig_hrtime(&uf->uf_cv, &uf->uf_mutex,
		    deadline)) == 0) {
			error = EINTR;
		} else if (r < 0) {
			error = ETIMEDOUT;
		}
	}
}

static int
uftdi_serdev_break(void *arg, bool on)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	mutex_enter(&uf->uf_mutex);
	uint16_t data = ui->ui_last_regs.ur_data;
	mutex_exit(&uf->uf_mutex);

	if (on) {
		data |= FTDI_SIO_SET_BREAK;
	}

	if (uftdi_send_command(uf, ui->ui_port,
	    FTDI_SIO_SET_DATA, data, 0) != USB_SUCCESS) {
		return (EIO);
	}

	return (0);
}

static int
uftdi_serdev_params_set(void *arg, serdev_params_t *p)
{
	uftdi_if_t *ui = arg;
	uftdi_regs_t ur = ui->ui_last_regs;

	uftdi_regs_set_datamode(&ur, serdev_params_char_size(p),
	    serdev_params_parity(p), serdev_params_stop_bits(p));

	uftdi_regs_set_flowcontrol(&ur, serdev_params_hard_flow_inbound(p) ||
	    serdev_params_hard_flow_outbound(p));

	if (uftdi_regs_set_baudrate(&ur, serdev_params_baudrate(p)) != 0) {
		return (EINVAL);
	}

	if (uftdi_program(ui, &ur) != USB_SUCCESS) {
		return (EIO);
	}

	return (0);
}

static int
uftdi_serdev_modem_set(void *arg, uint_t mask, uint_t val)
{
	uftdi_if_t *ui = arg;
	int erts = 0, edtr = 0;

	if (mask & TIOCM_DTR) {
		edtr = uftdi_set_dtr(ui, (val & TIOCM_DTR) != 0);
	}

	if (mask & TIOCM_RTS) {
		erts = uftdi_set_rts(ui, (val & TIOCM_RTS) != 0);
	}

	if (edtr != 0) {
		return (edtr);
	}
	return (erts);
}

static int
uftdi_serdev_modem_get(void *arg, uint_t mask, uint_t *val)
{
	uftdi_if_t *ui = arg;
	uftdi_t *uf = ui->ui_parent;

	*val = 0;

	mutex_enter(&uf->uf_mutex);
	if ((mask & TIOCM_CTS) && (ui->ui_last_msr & FTDI_MSR_STATUS_CTS)) {
		*val |= TIOCM_CTS;
	}
	if ((mask & TIOCM_DSR) && (ui->ui_last_msr & FTDI_MSR_STATUS_DSR)) {
		*val |= TIOCM_DSR;
	}
	if ((mask & TIOCM_RI) && (ui->ui_last_msr & FTDI_MSR_STATUS_RI)) {
		*val |= TIOCM_RI;
	}
	if ((mask & TIOCM_CD) && (ui->ui_last_msr & FTDI_MSR_STATUS_RLSD)) {
		*val |= TIOCM_CD;
	}
	if ((mask & TIOCM_RTS) && (ui->ui_last_mctl & UFTDI_MODEM_RTS)) {
		*val |= TIOCM_RTS;
	}
	if ((mask & TIOCM_DTR) && (ui->ui_last_mctl & UFTDI_MODEM_DTR)) {
		*val |= TIOCM_DTR;
	}
	mutex_exit(&uf->uf_mutex);

	return (0);
}

serdev_ops_t uftdi_serdev_ops = {
	.srdo_version =			SERDEV_OPS_VERSION_1,
	.srdo_open =			uftdi_serdev_open,
	.srdo_close =			uftdi_serdev_close,
	.srdo_rx =			uftdi_serdev_rx,
	.srdo_tx =			uftdi_serdev_tx,
	.srdo_flush_rx =		uftdi_serdev_flush_rx,
	.srdo_flush_tx =		uftdi_serdev_flush_tx,
	.srdo_drain =			uftdi_serdev_drain,
	.srdo_break =			uftdi_serdev_break,
	.srdo_params_set =		uftdi_serdev_params_set,
	.srdo_modem_set =		uftdi_serdev_modem_set,
	.srdo_modem_get =		uftdi_serdev_modem_get,
};

static void
uftdi_teardown(uftdi_t *uf)
{
	dev_info_t *dip = uf->uf_dip;

	VERIFY(uf->uf_flags & UFTDI_FL_DETACHING);
	VERIFY3P(uf->uf_usb_thread, ==, curthread);

	/*
	 * Clean up each per-interface structure that we allocated:
	 */
	for (uint_t i = 0; i < uf->uf_nif; i++) {
		uftdi_if_t *ui;
		if ((ui = uf->uf_if[i]) == NULL) {
			continue;
		}

		if (ui->ui_serdev != NULL) {
			serdev_handle_free(ui->ui_serdev);
		}

		kmem_free(ui, sizeof (*ui));
		uf->uf_if[i] = NULL;
	}

	if (uf->uf_setup & UFTDI_SETUP_MUTEX) {
		mutex_enter(&uf->uf_mutex);
		if (uf->uf_flags & UFTDI_FL_USB_CONNECTED) {
			uftdi_close_pipes(uf);
			uf->uf_flags &= ~UFTDI_FL_USB_CONNECTED;
		}
		mutex_exit(&uf->uf_mutex);

		mutex_destroy(&uf->uf_mutex);
		cv_destroy(&uf->uf_cv);
		uf->uf_setup &= ~UFTDI_SETUP_MUTEX;
	}

	if (uf->uf_setup & UFTDI_SETUP_USB_ATTACH) {
		usb_unregister_hotplug_cbs(dip);
		usb_client_detach(dip, uf->uf_usb_dev);
		uf->uf_usb_dev = NULL;
		uf->uf_setup &= ~UFTDI_SETUP_USB_ATTACH;
	}

	VERIFY3U(uf->uf_flags, ==, UFTDI_FL_DETACHING);
	VERIFY0(uf->uf_setup);

	ddi_soft_state_free(uftdi_state, ddi_get_instance(dip));
}

static int
uftdi_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int inst = ddi_get_instance(dip);

	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if (ddi_soft_state_zalloc(uftdi_state, inst) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "unable to allocate soft state");
		return (DDI_FAILURE);
	}

	uftdi_t *uf = ddi_get_soft_state(uftdi_state, inst);
	uf->uf_dip = dip;
	ddi_set_driver_private(dip, uf);

	/*
	 * We need to exclude hotplug callbacks until we finish attaching.
	 */
	uf->uf_usb_thread = curthread;

	if (usb_client_attach(dip, USBDRV_VERSION, 0) != USB_SUCCESS) {
		dev_err(dip, CE_WARN, "USB attach failure");
		goto bail;
	}
	uf->uf_setup |= UFTDI_SETUP_USB_ATTACH;

	if (usb_get_dev_data(dip, &uf->uf_usb_dev, USB_PARSE_LVL_IF, 0) !=
	    USB_SUCCESS) {
		dev_err(dip, CE_WARN, "USB device config failure");
		goto bail;
	}

	mutex_init(&uf->uf_mutex, NULL, MUTEX_DRIVER,
	    uf->uf_usb_dev->dev_iblock_cookie);
	cv_init(&uf->uf_cv, NULL, CV_DRIVER, NULL);
	uf->uf_setup |= UFTDI_SETUP_MUTEX;

	/*
	 * Make a best guess at what type of device this is.  For now, this is
	 * chiefly diagnostic, but as we support more (e.g., multi-port)
	 * devices and devices with more features, it will be come more
	 * important.  The logic below is a synthesis of device versioning
	 * facts found in several datasheets and drivers from other operating
	 * systems.
	 * XXX
	 */
	uf->uf_device_version = uf->uf_usb_dev->dev_descr->bcdDevice;
	uf->uf_nif = 1;
	if (uf->uf_usb_dev->dev_curr_cfg->cfg_descr.bNumInterfaces > 1) {
		/*
		 * Some models are newer devices that provide multiple ports
		 * through multiple interfaces.
		 */
		switch (uf->uf_device_version) {
		case 0x800:
			uf->uf_device_type = UFTDI_DEVICE_FT4232H;
			uf->uf_nif = 4;
			break;
		case 0x700:
			uf->uf_device_type = UFTDI_DEVICE_FT2232H;
			break;
		default:
			/*
			 * This might be an FT2232C or FT2232D; indeed, they
			 * may be otherwise indistinguishable.
			 */
			uf->uf_device_type = UFTDI_DEVICE_FT2232C;
			break;
		}
	} else if (uf->uf_device_version < 0x200) {
		uf->uf_device_type = UFTDI_DEVICE_OLD;
	} else if (uf->uf_device_version < 0x400) {
		if (uf->uf_usb_dev->dev_descr->iSerialNumber == 0) {
			/*
			 * According to various sources, FT232BM devices may
			 * have had a firmware problem that made them appear to
			 * have no serial number.
			 */
			uf->uf_device_type = UFTDI_DEVICE_FT232B;
		} else {
			uf->uf_device_type = UFTDI_DEVICE_FT232A;
		}
	} else if (uf->uf_device_version < 0x600) {
		uf->uf_device_type = UFTDI_DEVICE_FT232B;
	} else if (uf->uf_device_version < 0x900) {
		uf->uf_device_type = UFTDI_DEVICE_FT232R;
	} else if (uf->uf_device_version < 0x1000) {
		uf->uf_device_type = UFTDI_DEVICE_FT232H;
	} else {
		uf->uf_device_type = UFTDI_DEVICE_FTX;
	}

	for (uint_t i = 0; i < uf->uf_nif; i++) {
		uftdi_if_t *ui = kmem_zalloc(sizeof (*ui), KM_SLEEP);

		ui->ui_parent = uf;
		uf->uf_if[i] = ui;

		ui->ui_state = UFTDI_ST_ATTACHING;

		/*
		 * Some FTDI devices provide multiple ports on separate USB
		 * interfaces.  A survey of available information suggests
		 * ports are numbered starting at one, rather than at zero like
		 * USB interfaces.
		 */
		ui->ui_usb_if = uf->uf_usb_dev->dev_curr_if + i;
		ui->ui_port = ui->ui_usb_if + 1;

		if ((ui->ui_serdev = serdev_handle_alloc(ui, i,
		    &uftdi_serdev_ops, KM_SLEEP)) == NULL) {
			dev_err(dip, CE_WARN, "serdev allocation failure");
			goto bail;
		}
	}

	if (usb_register_hotplug_cbs(dip, uftdi_usb_disconnect,
	    uftdi_usb_reconnect) != USB_SUCCESS) {
		dev_err(dip, CE_WARN, "USB hotplug registration failure");
		goto bail;
	}

	if (uftdi_open_pipes(uf) != USB_SUCCESS) {
		dev_err(dip, CE_WARN, "pipe open failure");
		goto bail;
	}
	uf->uf_flags |= UFTDI_FL_USB_CONNECTED;

	/*
	 * We are finished configuring the USB state.  All that remains is for
	 * the serdev framework to attach and establish our device nodes.
	 */
	mutex_enter(&uf->uf_mutex);
	uftdi_usb_change_finish(uf);
	for (uint_t i = 0; i < uf->uf_nif; i++) {
		uf->uf_if[i]->ui_state = UFTDI_ST_CLOSED;
	}
	mutex_exit(&uf->uf_mutex);

	for (uint_t i = 0; i < uf->uf_nif; i++) {
		if (serdev_handle_attach(dip, uf->uf_if[i]->ui_serdev) !=
		    DDI_SUCCESS) {
			dev_err(dip, CE_WARN, "serdev attach failure");

			/*
			 * Get back control of the USB state so we can tear it
			 * down:
			 */
			mutex_enter(&uf->uf_mutex);
			uf->uf_flags |= UFTDI_FL_DETACHING;
			uftdi_usb_change_start(uf, false);
			mutex_exit(&uf->uf_mutex);

			goto bail;
		}
	}

	return (0);

bail:
	uf->uf_flags |= UFTDI_FL_DETACHING;
	uftdi_teardown(uf);
	return (DDI_FAILURE);
}

static int
uftdi_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	uftdi_t *uf = ddi_get_soft_state(uftdi_state, ddi_get_instance(dip));

	mutex_enter(&uf->uf_mutex);
	for (uint_t i = 0; i < uf->uf_nif; i++) {
		if (uf->uf_if[i]->ui_state != UFTDI_ST_CLOSED) {
			mutex_exit(&uf->uf_mutex);
			dev_err(dip, CE_WARN, "cannot detach while open");
			return (DDI_FAILURE);
		}
	}

	/*
	 * Signal to any subsequent hotplug events that they should fail
	 * immediately because we are detaching, and then wait for them to be
	 * over.
	 */
	uf->uf_flags |= UFTDI_FL_DETACHING;
	uftdi_usb_change_start(uf, false);
	mutex_exit(&uf->uf_mutex);

	for (uint_t i = 0; i < uf->uf_nif; i++) {
		if (serdev_handle_detach(uf->uf_if[i]->ui_serdev) !=
		    DDI_SUCCESS) {
			dev_err(dip, CE_WARN, "serdev detach failure");
			return (DDI_FAILURE);
		}
	}

	uftdi_teardown(uf);
	return (0);
}

static struct dev_ops uftdi_dev_ops = {
	.devo_rev =		DEVO_REV,

	.devo_attach =		uftdi_attach,
	.devo_detach =		uftdi_detach,

	.devo_getinfo =		nodev,
	.devo_identify =	nulldev,
	.devo_probe =		nulldev,
	.devo_reset =		nodev,
	.devo_quiesce =		ddi_quiesce_not_needed,
};

static struct modldrv uftdi_modldrv = {
	.drv_modops =		&mod_driverops,
	.drv_linkinfo =		"FTDI USB UART driver",
	.drv_dev_ops =		&uftdi_dev_ops,
};

static struct modlinkage uftdi_modlinkage = {
	.ml_rev =		MODREV_1,
	.ml_linkage =		{ &uftdi_modldrv, NULL },
};

int
_init(void)
{
	int r;

	if ((r = ddi_soft_state_init(&uftdi_state, sizeof (uftdi_t), 0)) != 0) {
		return (r);
	}

	if ((r = serdev_mod_init(&uftdi_dev_ops)) != 0) {
		ddi_soft_state_fini(&uftdi_state);
		return (r);
	}

	if ((r = mod_install(&uftdi_modlinkage)) != 0) {
		serdev_mod_fini(&uftdi_dev_ops);
		ddi_soft_state_fini(&uftdi_state);
	}

	return (r);
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&uftdi_modlinkage, mi));
}

int
_fini(void)
{
	int r;

	if ((r = mod_remove(&uftdi_modlinkage)) != 0) {
		return (r);
	}

	ddi_soft_state_fini(&uftdi_state);
	serdev_mod_fini(&uftdi_dev_ops);

	return (r);
}
