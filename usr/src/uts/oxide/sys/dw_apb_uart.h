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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_DW_APB_UART_H
#define	_SYS_DW_APB_UART_H

#include <sys/stdbool.h>
#include <sys/types.h>
#include <sys/uart.h>
#include <sys/io/mmioreg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dw_apb_port {
	DAP_NONE	= 0,
	DAP_0		= 0x1000000,
	DAP_1		= 0x1000001,
	DAP_2		= 0x1000002,
	DAP_3		= 0x1000003
} dw_apb_port_t;

typedef enum dw_apb_uart_flag {
	DAUF_MAPPED	= 1 << 0,
	DAUF_INITDONE	= 1 << 1,
} dw_apb_uart_flag_t;

typedef struct {
	dw_apb_port_t		dau_port;
	uint32_t		dau_baudrate;
	async_databits_t	dau_databits;
	async_parity_t		dau_parity;
	async_stopbits_t	dau_stopbits;
	uint8_t			dau_mcr;

	mmio_reg_block_t	dau_reg_block;
	mmio_reg_t		dau_reg_thr;
	mmio_reg_t		dau_reg_rbr;
	mmio_reg_t		dau_reg_lsr;
	mmio_reg_t		dau_reg_usr;
	mmio_reg_t		dau_reg_srr;
	mmio_reg_t		dau_reg_mcr;

	dw_apb_uart_flag_t	dau_flags;
} dw_apb_uart_t;

extern void dw_apb_uart_deinit(dw_apb_uart_t * const);
extern int dw_apb_uart_init(dw_apb_uart_t * const, const dw_apb_port_t,
    const uint32_t, const async_databits_t, const async_parity_t,
    const async_stopbits_t);
extern void dw_apb_uart_flush(const dw_apb_uart_t * const);
extern size_t dw_apb_uart_rx_nb(const dw_apb_uart_t * const, uint8_t *, size_t);
extern uint8_t dw_apb_uart_rx_one(const dw_apb_uart_t * const);
extern size_t dw_apb_uart_tx_nb(const dw_apb_uart_t * const, const uint8_t *,
    size_t);
extern void dw_apb_uart_tx(const dw_apb_uart_t * const, const uint8_t *,
    size_t);
extern bool dw_apb_uart_readable(const dw_apb_uart_t * const);
extern bool dw_apb_uart_writable(const dw_apb_uart_t * const);
extern void dw_apb_reset_mcr(const dw_apb_uart_t * const);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_DW_APB_UART_H */
