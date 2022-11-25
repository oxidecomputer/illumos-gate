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

#include <sys/boot_debug.h>
#include <sys/clock.h>
#include <sys/prom_debug.h>
#include <sys/cmn_err.h>
#include <sys/dw_apb_uart.h>
#include <sys/file.h>
#include <sys/ipcc_proto.h>
#include <sys/kernel_ipcc.h>
#include <sys/psm_defs.h>
#include <sys/stdbool.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/systm.h>
#include <sys/uart.h>
#include <vm/kboot_mmu.h>
#include <sys/io/fch/gpio.h>
#include <sys/io/fch/iomux.h>
#include <sys/io/fch/uart.h>
#include <sys/archsystm.h>
#include <sys/cpu.h>

/*
 * This is correct for the current Gimlet platform, in the future this may
 * need to be determined dynamically.
 */
#define	SP_AGPIO	139

static ipcc_ops_t kernel_ipcc_ops;
static ipcc_init_t ipcc_init = IPCC_INIT_UNSET;

/*
 * Functions for using IPCC from the kernel, driving the UART directly using the
 * polling functions in dw_apb_uart.c
 */

typedef struct {
	dw_apb_uart_t		kid_uart;
	mmio_reg_block_t	kid_gpio_block;
	mmio_reg_t		kid_gpio_reg;
} kernel_ipcc_data_t;

static kernel_ipcc_data_t kernel_ipcc_data;

static bool
eb_ipcc_readable(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (dw_apb_uart_readable(&data->kid_uart));
}

static bool
eb_ipcc_writable(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (dw_apb_uart_writable(&data->kid_uart));
}

static bool
eb_ipcc_readintr(void *arg)
{
	kernel_ipcc_data_t *data = arg;
	const uint32_t gpio = mmio_reg_read(data->kid_gpio_reg);

	return (FCH_GPIO_STD_GET_INPUT(gpio) == FCH_GPIO_STD_INPUT_VAL_LOW);
}

static void
eb_ipcc_pause(uint64_t delay_ms)
{
	hrtime_t delay_ns = MSEC2NSEC(delay_ms);
	extern int gethrtime_hires;

	if (gethrtime_hires) {
		/* The TSC is calibrated, we can use drv_usecwait() */
		drv_usecwait(NSEC2USEC(delay_ns));
	} else {
		/*
		 * The TSC has not yet been calibrated so assume its frequency
		 * is 2GHz (2 ticks per nanosecond). This is approximately
		 * correct for Gimlet and should be the right order of magnitude
		 * for future platforms. This delay does not have be accurate
		 * and is only used very early in boot.
		 */
		hrtime_t start = tsc_read();
		while (tsc_read() < start + (delay_ns << 1))
			SMT_PAUSE();
	}
}

static int
eb_ipcc_poll(void *arg, ipcc_pollevent_t ev, ipcc_pollevent_t *revp,
    uint64_t timeout_ms)
{
	uint64_t elapsed = 0;
	ipcc_pollevent_t rev = 0;

	for (;;) {
		if ((ev & IPCC_INTR) != 0 && eb_ipcc_readintr(arg))
			rev |= IPCC_INTR;
		if ((ev & IPCC_POLLIN) != 0 && eb_ipcc_readable(arg))
			rev |= IPCC_POLLIN;
		if ((ev & IPCC_POLLOUT) != 0 && eb_ipcc_writable(arg))
			rev |= IPCC_POLLOUT;
		if (rev != 0)
			break;

		eb_ipcc_pause(10);
		elapsed += 10;
		if (timeout_ms > 0 && elapsed >= timeout_ms)
			return (ETIMEDOUT);
	}

	*revp = rev;
	return (0);
}

static void
eb_ipcc_flush(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	dw_apb_uart_flush(&data->kid_uart);
}

static int
eb_ipcc_read(void *arg, uint8_t *buf, size_t *len)
{
	kernel_ipcc_data_t *data = arg;

	ASSERT3U(*len, >, 0);
	*buf = dw_apb_uart_rx_one(&data->kid_uart);
	*len = 1;
	return (0);
}

static int
eb_ipcc_write(void *arg, uint8_t *buf, size_t *len)
{
	kernel_ipcc_data_t *data = arg;

	dw_apb_uart_tx(&data->kid_uart, buf, *len);
	return (0);
}

static void
eb_ipcc_log(void *arg __unused, ipcc_log_type_t type __maybe_unused,
    const char *fmt, ...)
{
	va_list ap;

#ifndef DEBUG
	/*
	 * In a non-DEBUG kernel the hexdump messages are not logged to the
	 * console.
	 */
	if (type == IPCC_LOG_HEX)
		return;
#endif

	va_start(ap, fmt);
	eb_vprintf(fmt, ap);
	eb_printf("\n");
	va_end(ap);
}

static void
eb_ipcc_init(void)
{
	kernel_ipcc_data_t *data = &kernel_ipcc_data;

	DBG_MSG("kernel_ipcc_init(EARLYBOOT)\n");

	if (dw_apb_uart_init(&data->kid_uart, DAP_1,
	    3000000, AD_8BITS, AP_NONE, AS_1BIT) != 0) {
		bop_panic("Could not initialize SP/Host UART");
	}

	bzero(&kernel_ipcc_ops, sizeof (kernel_ipcc_ops));
	kernel_ipcc_ops.io_poll = eb_ipcc_poll;
	kernel_ipcc_ops.io_flush = eb_ipcc_flush;
	kernel_ipcc_ops.io_read = eb_ipcc_read;
	kernel_ipcc_ops.io_write = eb_ipcc_write;
	kernel_ipcc_ops.io_log = eb_ipcc_log;

	/*
	 * XXX - this is correct for Gimlet but will need to be factored out
	 *	 for boards that aren't Gimlet and/or processors that aren't
	 *	 Milan.
	 * AGPIO139 is the interrupt line from the SP that signals when it
	 * has information for us. Set up the GPIO parameters and then
	 * configure the pinmux to make it active on the pad.
	 */
	data->kid_gpio_block = fch_gpio_mmio_block();
	data->kid_gpio_reg = FCH_GPIO_STD_MMIO(data->kid_gpio_block,
	    SP_AGPIO);
	uint32_t gpio = mmio_reg_read(data->kid_gpio_reg);
	gpio = FCH_GPIO_STD_SET_OUTPUT_EN(gpio, 0);
	gpio = FCH_GPIO_STD_SET_PD_EN(gpio, 0);
	gpio = FCH_GPIO_STD_SET_PU_EN(gpio, 0);
	gpio = FCH_GPIO_STD_SET_TRIG(gpio, FCH_GPIO_STD_TRIG_LEVEL);
	gpio = FCH_GPIO_STD_SET_LEVEL(gpio, FCH_GPIO_STD_LEVEL_ACT_LOW);
	gpio = FCH_GPIO_STD_SET_INT_EN(gpio, 0);
	mmio_reg_write(data->kid_gpio_reg, gpio);
	gpio = mmio_reg_read(data->kid_gpio_reg);

	eb_printf("Configured AGPIO%d: %x (input is %s)\n", SP_AGPIO, gpio,
	    FCH_GPIO_STD_GET_INPUT(gpio) == FCH_GPIO_STD_INPUT_VAL_HIGH ?
	    "high" : "low");

	const mmio_reg_block_t block = fch_iomux_mmio_block();
	FCH_IOMUX_PINMUX_SET_MMIO(block, 139, GPIO139);
	mmio_reg_block_unmap(block);
}

static void
ebi_ipcc_init(void)
{
	DBG_MSG("kernel_ipcc_init(ENABLE_INTERRUPT)\n");
	kernel_ipcc_ops.io_readintr = eb_ipcc_readintr;
}

/*
 * Functions used for IPCC in mid boot, after KVM has been initialised but
 * before the STREAMS subsystem and UART drivers are loaded. These are also
 * used for system panics and some other messages if the path via LDI is
 * unavailable.
 */

static void
mb_ipcc_log(void *arg __unused, ipcc_log_type_t type __maybe_unused,
    const char *fmt, ...)
{
	va_list ap;

#ifndef DEBUG
	/*
	 * In a non-DEBUG kernel the hexdump messages are not logged to the
	 * console.
	 */
	if (type == IPCC_LOG_HEX)
		return;
#endif

	va_start(ap, fmt);
	vcmn_err(CE_CONT, fmt, ap);
	va_end(ap);
}

static void
mb_ipcc_init(void)
{
	kernel_ipcc_data_t *data = &kernel_ipcc_data;

	DBG_MSG("kernel_ipcc_init(KVMAVAIL)\n");

	/*
	 * The UART is re-initialised to move the register MMIO mappings out
	 * of the boot pages.
	 */
	if (dw_apb_uart_init(&data->kid_uart, DAP_1,
	    3000000, AD_8BITS, AP_NONE, AS_1BIT) != 0) {
		bop_panic("Could not re-initialize SP/Host UART");
	}

	/*
	 * Similarly the GPIO MMIO block and register.
	 */
	mmio_reg_block_unmap(data->kid_gpio_block);
	data->kid_gpio_block = fch_gpio_mmio_block();
	data->kid_gpio_reg = FCH_GPIO_STD_MMIO(data->kid_gpio_block, SP_AGPIO);

	/*
	 * Switch to the cmn_err()-based logger.
	 */
	kernel_ipcc_ops.io_log = mb_ipcc_log;

	/*
	 * At this stage of boot, the genunix module has been loaded and it is
	 * safe to use things like mutex_enter/exit(). Switch the ipcc_proto
	 * module to multithreaded mode. Note that we must still be
	 * single-threaded at this point to avoid racing with any calls in
	 * progress; this is verified in ipcc_begin_multithreaded().
	 */
	ipcc_begin_multithreaded();
}

/*
 * Entry points
 */

void
kernel_ipcc_init(ipcc_init_t stage)
{
	switch (stage) {
	case IPCC_INIT_EARLYBOOT:
		VERIFY3U(ipcc_init, ==, IPCC_INIT_UNSET);
		eb_ipcc_init();
		break;
	case IPCC_INIT_ENABLE_INTERRUPT:
		VERIFY3U(ipcc_init, ==, IPCC_INIT_EARLYBOOT);
		ebi_ipcc_init();
		break;
	case IPCC_INIT_KVMAVAIL:
		VERIFY3U(ipcc_init, ==, IPCC_INIT_ENABLE_INTERRUPT);
		mb_ipcc_init();
		break;
	case IPCC_INIT_DEVTREE:
		VERIFY3U(ipcc_init, ==, IPCC_INIT_KVMAVAIL);
		DBG_MSG("kernel_ipcc_init(DEVTREE)\n");
		break;
	default:
		break;
	}

	ipcc_init = stage;
}

/*
 * These functions always drive the UART directly via kernel_ipcc_ops. They
 * are called when the system is in a state where interrupts may not be
 * available or we may be single-threaded.
 * This function configures things to give us the best chance of success in
 * sending a final message.
 */
static void
kernel_ipcc_prepare_gasp(void)
{
	/*
	 * We're sending a final message, don't look at or try to deal with any
	 * asserted interrupt.
	 */
	kernel_ipcc_ops.io_readintr = NULL;
	/*
	 * We may be at a high SPL in which case logging can deadlock if we're
	 * also single-threaded (as we are in at least the reboot and panic
	 * cases).
	 */
	kernel_ipcc_ops.io_log = NULL;
	/*
	 * The UART may not be configured as we require. For example, if we are
	 * multi-user then the `dwu` driver may have disabled RTS; reset
	 * things.
	 */
	dw_apb_reset_mcr(&kernel_ipcc_data.kid_uart);
}

void
kernel_ipcc_reboot(void)
{
	kernel_ipcc_prepare_gasp();
	(void) ipcc_reboot(&kernel_ipcc_ops, &kernel_ipcc_data);
}

void
kernel_ipcc_poweroff(void)
{
	kernel_ipcc_prepare_gasp();
	(void) ipcc_poweroff(&kernel_ipcc_ops, &kernel_ipcc_data);
}

/*
 * The following interfaces are intended only for use during early boot,
 * before the device tree is available. They drive the UART directly via
 * kernel_ipcc_ops. It is an error to call these functions too late, once
 * ipcc_init >= IPCC_INIT_DEVTREE.
 */

int
kernel_ipcc_ident(ipcc_ident_t *ident)
{
	VERIFY3U(ipcc_init, <, IPCC_INIT_DEVTREE);
	return (ipcc_ident(&kernel_ipcc_ops, &kernel_ipcc_data, ident));
}

int
kernel_ipcc_bsu(uint8_t *bsu)
{
	VERIFY3U(ipcc_init, <, IPCC_INIT_DEVTREE);
	return (ipcc_bsu(&kernel_ipcc_ops, &kernel_ipcc_data, bsu));
}

int
kernel_ipcc_status(uint64_t *status, uint64_t *debug)
{
	VERIFY3U(ipcc_init, <, IPCC_INIT_DEVTREE);
	return (ipcc_status(&kernel_ipcc_ops, &kernel_ipcc_data, status,
	    debug));
}

int
kernel_ipcc_ackstart(void)
{
	VERIFY3U(ipcc_init, <, IPCC_INIT_DEVTREE);
	return (ipcc_ackstart(&kernel_ipcc_ops, &kernel_ipcc_data));
}
