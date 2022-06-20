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

#ifndef _SERDEV_H
#define	_SERDEV_H

#include <sys/dditypes.h>
#include <sys/stdbool.h>
#include <sys/stream.h>
#include <sys/termios.h>

/*
 * SERDEV: A GENERIC SERIAL PORT DRIVER FRAMEWORK
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct serdev_handle serdev_handle_t;
typedef struct serdev_params serdev_params_t;

typedef enum serdev_parity {
	SERDEV_PARITY_NONE,
	SERDEV_PARITY_ODD,
	SERDEV_PARITY_EVEN,
} serdev_parity_t;

typedef enum serdev_error {
	SERDEV_ERROR_FRAMING =	(1 << 0),
	SERDEV_ERROR_PARITY =	(1 << 1),
	SERDEV_ERROR_OVERRUN =	(1 << 2),
	SERDEV_ERROR_BREAK =	(1 << 3),
} serdev_error_t;

#define	SERDEV_OPS_VERSION_1	1

typedef struct serdev_ops {
	uint_t			srdo_version;
	int			(*srdo_open)(void *);
	int			(*srdo_close)(void *);
	void			(*srdo_rx)(void *);
	void			(*srdo_tx)(void *, mblk_t *);
	int			(*srdo_flush_rx)(void *);
	int			(*srdo_flush_tx)(void *);
	int			(*srdo_drain)(void *, hrtime_t);
	int			(*srdo_break)(void *, bool);
	int			(*srdo_params_set)(void *, serdev_params_t *);
	int			(*srdo_modem_set)(void *, uint_t, uint_t);
	int			(*srdo_modem_get)(void *, uint_t, uint_t *);
} serdev_ops_t;

extern int serdev_mod_init(struct dev_ops *);
extern void serdev_mod_fini(struct dev_ops *);

extern serdev_handle_t *serdev_handle_alloc(void *, uint_t,
    const serdev_ops_t *, int);
extern void serdev_handle_free(serdev_handle_t *);

extern int serdev_handle_detach(serdev_handle_t *);
extern int serdev_handle_attach(dev_info_t *, serdev_handle_t *);

extern void serdev_handle_report_status(serdev_handle_t *);
extern void serdev_handle_report_rx(serdev_handle_t *);
extern void serdev_handle_rx(serdev_handle_t *, mblk_t *);
extern void serdev_handle_report_tx(serdev_handle_t *);

extern bool serdev_handle_running_tx(serdev_handle_t *);
extern bool serdev_handle_running_rx(serdev_handle_t *);


/*
 * Routines to extract specific parameters from the parameter object:
 */
extern speed_t serdev_params_baudrate(serdev_params_t *);
extern uint_t serdev_params_stop_bits(serdev_params_t *);
extern serdev_parity_t serdev_params_parity(serdev_params_t *);
extern uint_t serdev_params_char_size(serdev_params_t *);
extern bool serdev_params_hard_flow_inbound(serdev_params_t *);
extern bool serdev_params_hard_flow_outbound(serdev_params_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SERDEV_H */
