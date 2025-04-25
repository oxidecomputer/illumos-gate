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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _USBSER_USBFTDI_USBFTDI_H
#define	_USBSER_USBFTDI_USBFTDI_H

/*
 * USB FTDI definitions
 */

#include <sys/types.h>
#include <sys/dditypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	UFTDI_MAX_PORTS			4

/*
 * MINOR NUMBERS
 *
 * Give the least significant byte to ugen(4D) for minor numbering.  The
 * remainder of the minor number will be used to determine our instance number.
 */
#define	UFTDI_MINOR_UGEN_BITS_MASK	(0xFF)
#define	UFTDI_MINOR_INST_MASK		(~0xFF)
#define	UFTDI_MINOR_TO_INST(mm)		((mm) >> 8)
/*
 * This is the count of minor numbers it is possible for ugen to track, and
 * must match UFTDI_MINOR_UGEN_BITS_MASK:
 */
#define	UFTDI_MAX_MINORS		256

typedef enum uftdi_state {
	UFTDI_ST_ATTACHING = 0,
	UFTDI_ST_CLOSED,
	UFTDI_ST_OPENING,
	UFTDI_ST_OPEN,
	UFTDI_ST_CLOSING,
} uftdi_state_t;

typedef enum uftdi_ugen_state {
	UFTDI_UGEN_ST_CLOSED = 0,
	UFTDI_UGEN_ST_OPENING,
	UFTDI_UGEN_ST_OPEN,
	UFTDI_UGEN_ST_CLOSING,
} uftdi_ugen_state_t;

typedef enum uftdi_flags {
	UFTDI_FL_USB_CONNECTED =	(1 << 0),
	UFTDI_FL_DETACHING =		(1 << 1),
} uftdi_flags_t;

typedef enum uftdi_modem_control {
	UFTDI_MODEM_RTS =		(1 << 0),
	UFTDI_MODEM_DTR =		(1 << 1),
} uftdi_modem_control_t;

typedef enum utfdi_device_type {
	UFTDI_DEVICE_UNKNOWN = 0,
	UFTDI_DEVICE_OLD,
	UFTDI_DEVICE_FT232A,
	UFTDI_DEVICE_FT232B,
	UFTDI_DEVICE_FT232R,
	UFTDI_DEVICE_FT232H,
	UFTDI_DEVICE_FT2232C,
	UFTDI_DEVICE_FT2232H,
	UFTDI_DEVICE_FT4232H,
	UFTDI_DEVICE_FTX,
} utfdi_device_type_t;

typedef enum uftdi_setup {
	UFTDI_SETUP_USB_ATTACH =	(1 << 0),
	UFTDI_SETUP_MUTEX =		(1 << 1),
	UFTDI_SETUP_SERDEV =		(1 << 2),
} uftdi_setup_t;

typedef enum uftdi_pipe_state {
	UFTDI_PIPE_CLOSED = 0,
	UFTDI_PIPE_IDLE,
	UFTDI_PIPE_BUSY,
} uftdi_pipe_state_t;

typedef struct uftdi_regs {
	uint16_t			ur_baud;
	uint16_t			ur_data;
	uint16_t			ur_timer;
	uint16_t			ur_flowval;
	uint8_t				ur_flowproto;
} uftdi_regs_t;

typedef struct uftdi_speed_params {
	uint16_t			usp_baud;
	uint16_t			usp_timer;
} uftdi_speed_params_t;

typedef struct uftdi_pipe {
	uftdi_pipe_state_t		up_state;
	usb_pipe_handle_t		up_pipe;
	size_t				up_bufsz;
} uftdi_pipe_t;

typedef struct uftdi_if uftdi_if_t;

/*
 * Per-device state:
 */
typedef struct uftdi {
	kmutex_t			uf_mutex;
	kcondvar_t			uf_cv;

	dev_info_t			*uf_dip;

	uftdi_setup_t			uf_setup;
	uftdi_flags_t			uf_flags;

	uftdi_ugen_state_t		uf_ugen_state;
	usb_ugen_hdl_t			uf_ugen;
	bool				uf_ugen_minor_open[UFTDI_MAX_MINORS];

	uint16_t			uf_device_version;
	utfdi_device_type_t		uf_device_type;

	uint_t				uf_nif;
	uftdi_if_t			*uf_if[UFTDI_MAX_PORTS];

	/*
	 * To modify USB device state, you must uftdi_usb_change_start() to
	 * install the current thread as USB device state owner.
	 * XXX
	 */
	kthread_t			*uf_usb_thread;
	usb_client_dev_data_t		*uf_usb_dev;
} uftdi_t;

typedef struct uftdi_if_stats {
	kstat_named_t			uis_program_fail;
	kstat_named_t			uis_allocb_fail;
	kstat_named_t			uis_in_error;
	kstat_named_t			uis_rx_fail;
	kstat_named_t			uis_out_error;
	kstat_named_t			uis_tx_fail;
	kstat_named_t			uis_tx_overlap;
	kstat_named_t			uis_tx_max_size;
	kstat_named_t			uis_tx_max_count;
} uftdi_if_stats_t;

#define	UFTDI_STAT_INIT(ui, stat)	\
    kstat_named_init(&(ui)->ui_stats.uis_##stat, #stat, KSTAT_DATA_UINT64)
#define	UFTDI_STAT_INCR(ui, stat)	\
    atomic_inc_64(&(ui)->ui_stats.uis_##stat.value.ui64)

struct uftdi_if {
	uftdi_t				*ui_parent;

	serdev_handle_t			*ui_serdev;

	uftdi_state_t			ui_state;

	/*
	 * FTDI port number, as passed in control messages, and other device
	 * identification information:
	 */
	uint8_t				ui_port;

	uint_t				ui_usb_if;
	uftdi_pipe_t			ui_pipe_in;
	uftdi_pipe_t			ui_pipe_out;

	mblk_t				*ui_rx_mp;
	mblk_t				*ui_tx_mp;

	/*
	 * Cached values of parameters sent to, and status received from, the
	 * device:
	 */
	uftdi_regs_t			ui_last_regs;
	uftdi_modem_control_t		ui_last_mctl;
	uint8_t				ui_last_msr; /* Modem Status Register */
	uint8_t				ui_last_lsr; /* Line Status Register */
	uint8_t				ui_last_rxerr; /* LSR RX errors */

	kstat_t				*ui_kstat;
	uftdi_if_stats_t		ui_stats;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _USBSER_USBFTDI_USBFTDI_H */
