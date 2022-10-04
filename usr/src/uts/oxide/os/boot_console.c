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
 * Copyright 2021 Oxide Computer Co.
 */

#include <sys/bootconf.h>
#include <sys/bootsvcs.h>
#include <sys/boot_console.h>
#include <sys/boot_debug.h>
#include <sys/kernel_ipcc.h>
#include <sys/cmn_err.h>
#include <sys/dw_apb_uart.h>
#include <sys/uart.h>

/*
 * Debugging note: If you wish to debug on the console using the loader's
 * identity mapping, set this to the UART regs base address.  This is
 * useful only very, very early -- while setting up the MMU.
 */
static void *con_uart_regs;
static struct boot_syscalls bsys;

static int
uart_getchar(void)
{
	return ((int)dw_apb_uart_rx_one(con_uart_regs));
}

static void
uart_putchar(int c)
{
	static const uint8_t CR = '\r';
	uint8_t ch = (uint8_t)(c);

	if (ch == '\n')
		dw_apb_uart_tx(con_uart_regs, &CR, 1);
	dw_apb_uart_tx(con_uart_regs, &ch, 1);
}

static int
uart_ischar(void)
{
	return ((int)dw_apb_uart_dr(con_uart_regs));
}

struct boot_syscalls *
boot_console_init(void)
{
	con_uart_regs = dw_apb_uart_init(DAP_0, 3000000,
	    AD_8BITS, AP_NONE, AS_1BIT);

	if (con_uart_regs == NULL)
		return (NULL);

	bsys.bsvc_getchar = uart_getchar;
	bsys.bsvc_putchar = uart_putchar;
	bsys.bsvc_ischar = uart_ischar;

	return (&bsys);
}

void
vbop_printf(void *_bop, const char *fmt, va_list ap)
{
	const char *cp;
	static char buffer[512];

	if (con_uart_regs == NULL)
		return;

	(void) vsnprintf(buffer, sizeof (buffer), fmt, ap);
	for (cp = buffer; *cp != '\0'; ++cp)
		uart_putchar(*cp);
}

/*PRINTFLIKE2*/
void
bop_printf(void *bop, const char *fmt, ...)
{
	va_list	ap;

	if (con_uart_regs == NULL)
		return;

	va_start(ap, fmt);
	vbop_printf(bop, fmt, ap);
	va_end(ap);
}

void
kbm_debug_printf(const char *file, int line, const char *fmt, ...)
{
	/*
	 * This use of a static is safe because we are always single-threaded
	 * when this code is running.
	 */
	static boolean_t continuation = 0;
	size_t fmtlen = strlen(fmt);
	boolean_t is_end = (fmt[fmtlen - 1] == '\n');
	va_list ap;

	if (!kbm_debug || con_uart_regs == NULL)
		return;

	if (!continuation)
		eb_printf("%s:%d: ", file, line);

	va_start(ap, fmt);
	eb_vprintf(fmt, ap);
	va_end(ap);

	continuation = !is_end;
}

/*
 * Another panic() variant; this one can be used even earlier during boot than
 * prom_panic().
 */
/*PRINTFLIKE1*/
void
bop_panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vbop_printf(NULL, fmt, ap);
	ipcc_panic_vmessage(fmt, ap);
	va_end(ap);

	kernel_ipcc_panic();

	eb_printf("\nRebooting.\n");
	kernel_ipcc_reboot();
	eb_halt();
}
