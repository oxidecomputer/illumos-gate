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

#ifndef _SERDEV_IMPL_H
#define	_SERDEV_IMPL_H

#include <sys/conf.h>
#include <sys/consdev.h>
#include <sys/ddi.h>
#include <sys/debug.h>
#include <sys/devops.h>
#include <sys/disp.h>
#include <sys/fs/dv_node.h>
#include <sys/ksynch.h>
#include <sys/param.h>
#include <sys/policy.h>
#include <sys/serdev.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/taskq.h>
#include <sys/taskq_impl.h>
#include <sys/termio.h>
#include <sys/tty.h>
#include <sys/types.h>

/*
 * SERDEV: A GENERIC SERIAL PORT DRIVER FRAMEWORK (INTERNAL HEADER)
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MINOR NUMBER ALLOCATION STRATEGY XXX
 *
 * Each instance requires two minor nodes: the inbound (tty) node, and the
 * dialout node.  We will use the formula:
 *
 *	tty =		instance * 2 + 0
 *	dialout =	instance * 2 + 1
 */
#define	SERDEV_MINOR_TO_INST(minor)	((minor) >> 1)

#define	SERDEV_MINOR_MODE_MASK		1

#define	SERDEV_MINOR_MODE_TTY		0
#define	SERDEV_MINOR_MODE_DIALOUT	1

#define	SERDEV_MINOR_TTY(inst)		\
	((((minor_t)(inst)) << 1) | SERDEV_MINOR_MODE_TTY)
#define	SERDEV_MINOR_DIALOUT(inst)	\
	((((minor_t)(inst)) << 1) | SERDEV_MINOR_MODE_DIALOUT)

typedef struct serdev serdev_t;


typedef enum serdev_stop_rx {
	SERDEV_STOP_RX_USER =		(1 << 0),
	SERDEV_STOP_RX_STREAMS =	(1 << 1),
} serdev_stop_rx_t;

typedef enum serdev_stop_tx {
	SERDEV_STOP_TX_USER =		(1 << 8),
	SERDEV_STOP_TX_CTS =		(1 << 9),
	SERDEV_STOP_TX_DELAY =		(1 << 10),
	SERDEV_STOP_TX_BREAK =		(1 << 11),
} serdev_stop_tx_t;

typedef enum serdev_break {
	SERDEV_BREAK_NONE = 0,
	SERDEV_BREAK_TIMED,
	SERDEV_BREAK_USER,
} serdev_break_t;

typedef enum serdev_open_mode {
	SERDEV_OM_NONE = 0,
	SERDEV_OM_DIALOUT,
	SERDEV_OM_TTY,
} serdev_open_mode_t;


struct serdev_handle {
	uint_t				srdh_port;
	bool				srdh_ignore_cd;
	void				*srdh_private;
	dev_info_t			*srdh_parent;
	dev_info_t			*srdh_child;
	serdev_ops_t			srdh_ops;
};

struct serdev_params {
	speed_t srpp_baudrate;
	uint_t srpp_stop_bits;
	serdev_parity_t srpp_parity;
	uint_t srpp_char_size;
	bool srpp_hard_flow_inbound;
	bool srpp_hard_flow_outbound;
};

typedef enum serdev_state {
	SERDEV_ST_CLOSED = 0,
	SERDEV_ST_OPENING,
	SERDEV_ST_CARRIER_WAIT,
	SERDEV_ST_OPEN,
	SERDEV_ST_CLOSING_DRAINING,
	SERDEV_ST_CLOSING,
} serdev_state_t;

typedef enum serdev_flags {
	/*
	 * CARRIER_DETECT is set if we should act as if we have detected a
	 * carrier, whether because the line actually has a CD signal or
	 * because we are ignoring CD for this line.
	 */
	SERDEV_FL_CARRIER_DETECT =	(1 << 0),
	/*
	 * CARRIER_LOSS is set when we detect a carrier loss and need to take
	 * action like send M_HANGUP.
	 */
	SERDEV_FL_CARRIER_LOSS =	(1 << 1),
	/*
	 * OFF_HOOK means we have sent M_UNHANGUP up the stream.  It is cleared
	 * when we have most recently sent M_HANGUP.
	 */
	SERDEV_FL_OFF_HOOK =		(1 << 2),

	SERDEV_FL_NEED_STATUS =		(1 << 3),
	SERDEV_FL_NEED_DRAIN =		(1 << 4),

	SERDEV_FL_TASK_REQUESTED =	(1 << 5),
	SERDEV_FL_TASK_RUNNING =	(1 << 6),

	SERDEV_FL_RX_STOPPED =		(1 << 7),
	SERDEV_FL_TX_STOPPED =		(1 << 8),
	SERDEV_FL_TX_ACTIVE =		(1 << 9),
} serdev_flags_t;

typedef enum serdev_setup {
	SERDEV_SETUP_MINOR_NODES =	(1 << 0),
	SERDEV_SETUP_OPEN_DEVICE =	(1 << 1),
	SERDEV_SETUP_STREAMS =		(1 << 2),
} serdev_setup_t;

typedef enum serdev_bufcall {
	SERDEV_BUFCALL_WRITE =		0,
	SERDEV_BUFCALL_READ,
} serdev_bufcall_t;

#define	SERDEV_NBUFCALLS		(SERDEV_BUFCALL_READ + 1)

struct serdev {
	kmutex_t			srd_mutex;
	kcondvar_t			srd_cv;

	dev_info_t			*srd_dip;
	serdev_setup_t			srd_setup;
	serdev_state_t			srd_state;
	serdev_flags_t			srd_flags;
	serdev_break_t			srd_break;
	bool				srd_ignore_cd;

	/*
	 * During the open process, at most one thread is in charge at a time.
	 * This thread may change, such as when an inbound open is waiting for
	 * a carrier but an outbound open takes over the serial line.
	 */
	kthread_t			*srd_opener;
	serdev_open_mode_t		srd_open_mode;

	/*
	 * We manage several types of deferred execution while the device is
	 * open.  Delays or breaks are driven by timeout(), allocation failure
	 * is retried by qbufcall(), and requests for status updates occur on a
	 * taskq.
	 */
	timeout_id_t			srd_timeout;
	bufcall_id_t			srd_bufcalls[SERDEV_NBUFCALLS];
	taskq_t				*srd_taskq;
	taskq_ent_t			srd_task;

	serdev_ops_t			srd_ops;
	void				*srd_private;

	serdev_stop_tx_t		srd_stop_tx_why;
	serdev_stop_rx_t		srd_stop_rx_why;
	uint_t				srd_last_modem_status;
	tty_common_t			srd_tty;
};

extern void serdev_taskq_dispatch(serdev_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SERDEV_IMPL_H */
