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
 * Copyright 2024 Oxide Computer Co.
 */

#include <sys/archsystm.h>
#include <sys/bootconf.h>
#include <sys/bootsvcs.h>
#include <sys/boot_console.h>
#include <sys/boot_debug.h>
#include <sys/stdbool.h>
#include <sys/kernel_ipcc.h>
#include <sys/cmn_err.h>
#include <sys/dw_apb_uart.h>
#include <sys/uart.h>

/*
 * Debugging note: If you wish to debug on the console using the loader's
 * identity mapping, enable the following definition.  This is useful only
 * very, very early -- while setting up the MMU.
 */
#undef	VVE_CONSOLE_DEBUG

#ifdef VVE_CONSOLE_DEBUG
#include <sys/io/fch/uart.h>
static dw_apb_uart_t con_uart = {
	.dau_reg_thr = {
		.mr_va = (caddr_t)FCH_UART_PHYS_BASE + FCH_UART_REGOFF_THR,
		.mr_size = 4,
	},
	.dau_reg_rbr = {
		.mr_va = (caddr_t)FCH_UART_PHYS_BASE + FCH_UART_REGOFF_RBR,
		.mr_size = 4,
	},
	.dau_reg_lsr = {
		.mr_va = (caddr_t)FCH_UART_PHYS_BASE + FCH_UART_REGOFF_LSR,
		.mr_size = 4,
	},
	.dau_reg_usr = {
		.mr_va = (caddr_t)FCH_UART_PHYS_BASE + FCH_UART_REGOFF_USR,
		.mr_size = 4,
	},
	.dau_reg_srr = {
		.mr_va = (caddr_t)FCH_UART_PHYS_BASE + FCH_UART_REGOFF_SRR,
		.mr_size = 4,
	},
};
static bool con_uart_init = true;
#else
static dw_apb_uart_t con_uart;
static bool con_uart_init;
#endif
static struct boot_syscalls bsys;

static int
uart_getchar(void)
{
	return ((int)dw_apb_uart_rx_one(&con_uart));
}

static void
uart_putchar(int c)
{
	static const uint8_t CR = '\r';
	uint8_t ch = (uint8_t)(c);

	if (ch == '\n')
		dw_apb_uart_tx(&con_uart, &CR, 1);
	dw_apb_uart_tx(&con_uart, &ch, 1);
}

static int
uart_ischar(void)
{
	return ((int)dw_apb_uart_readable(&con_uart));
}

struct boot_syscalls *
boot_console_init(void)
{
	if (dw_apb_uart_init(&con_uart, DAP_0, 3000000,
	    AD_8BITS, AP_NONE, AS_1BIT) != 0) {
		bop_panic("Could not initialize boot console UART");
	}

	con_uart_init = true;

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

	if (!con_uart_init)
		return;

	(void) vsnprintf(buffer, sizeof (buffer), fmt, ap);
	for (cp = buffer; *cp != '\0'; ++cp)
		uart_putchar(*cp);
}

void
bop_printf(void *bop, const char *fmt, ...)
{
	va_list	ap;

	if (!con_uart_init)
		return;

	va_start(ap, fmt);
	vbop_printf(bop, fmt, ap);
	va_end(ap);
}

void
eb_debug_printf_gated(boolean_t gate, const char *file, int line,
    const char *fmt, ...)
{
	/*
	 * This use of a static is safe because we are always single-threaded
	 * when this code is running.
	 */
	static boolean_t continuation = 0;
	size_t fmtlen = strlen(fmt);
	boolean_t is_end = (fmt[fmtlen - 1] == '\n');
	va_list ap;

	if (!gate || !con_uart_init)
		return;

	if (!continuation && file != NULL)
		bop_printf(NULL, "%s:%d: ", file, line);

	va_start(ap, fmt);
	vbop_printf(NULL, fmt, ap);
	va_end(ap);

	continuation = !is_end;
}

/*
 * Another panic() variant; this one can be used even earlier during boot than
 * prom_panic().
 */
void
bop_panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vbop_printf(NULL, fmt, ap);
	va_end(ap);
	va_start(ap, fmt);
	kipcc_panic_vmessage(fmt, ap);
	va_end(ap);

	kipcc_panic_field(IPF_CAUSE, IPCC_PANIC_EARLYBOOT);
	kernel_ipcc_panic();

	bop_printf(NULL, "\nRebooting.\n");
	reset();
}
