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
 * Copyright 2024 Oxide Computer Company
 */

#include <sys/types.h>
#include <sys/boot_debug.h>
#include <sys/clock.h>
#include <sys/cmn_err.h>
#include <sys/dw_apb_uart.h>
#include <sys/file.h>
#include <sys/ipcc_proto.h>
#include <sys/kernel_ipcc.h>
#include <sys/psm_defs.h>
#include <sys/reboot.h>
#include <sys/stdbool.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/systm.h>
#include <sys/uart.h>
#include <vm/kboot_mmu.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/io/fch/uart.h>
#include <sys/io/milan/iomux.h>
#include <sys/archsystm.h>
#include <sys/platform_detect.h>
#include <sys/cpu.h>

static ipcc_ops_t kernel_ipcc_ops;
static ipcc_init_t ipcc_init = IPCC_INIT_UNSET;

/*
 * This flag reduces the sleep time in eb_ipcc_poll() from 10ms down to 10us.
 * This is used specifically to reduce latency in the long running phase 2
 * image transfer. Requesting data from the SP always results in us entering
 * the poll loop since the SP has to coordinate multiple tasks, lease buffers
 * and retrieve the data over the management network.
 * Testing of that particular mechanism end-to-end has shown that this change
 * increases the transfer rate of 512 byte blocks from 40KiB/s to 128KiB/s and
 * that using a smaller delay than 10us does not further improve throughput.
 *
 * This variable must only be read or modified if the ipcc channel lock is
 * held.
 */
static bool ipcc_fastpoll;

/*
 * A static buffer into which panic data are accumulated before being sent to
 * the SP as a byte stream. This is static so that it is not necessary to
 * perform allocations while panicking, and so that it exists regardless of
 * which phase of boot the system is in when a panic occurs.
 */
static ipcc_panic_data_t ipcc_panic_buf;

/*
 * Functions for using IPCC from the kernel, driving the UART directly using the
 * polling functions in dw_apb_uart.c
 */

typedef struct {
	dw_apb_uart_t		kid_uart;
	uint32_t		kid_agpio;
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

	return (FCH_GPIO_GPIO_GET_INPUT(gpio) == FCH_GPIO_GPIO_INPUT_LOW);
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
	uint64_t elapsed = 0, uselapsed = 0;
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

		if (ipcc_fastpoll) {
			tenmicrosec();
			if (++uselapsed % 100 == 0)
				elapsed++;
		} else {
			eb_ipcc_pause(10);
			elapsed += 10;
		}
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
#ifndef DEBUG
	/*
	 * In a non-DEBUG kernel the hexdump messages are not logged to the
	 * console.
	 */
	if (type == IPCC_LOG_HEX)
		return;
#endif

	if ((boothowto & RB_VERBOSE) != 0) {
		va_list ap;

		va_start(ap, fmt);
		eb_vprintf(fmt, ap);
		va_end(ap);
	}
}

static void
eb_ipcc_init_gpio(kernel_ipcc_data_t *data)
{
	switch (oxide_board_data->obd_ipccspintr) {
	case IPCC_SPINTR_DISABLED:
		data->kid_agpio = UINT32_MAX;
		return;
	case IPCC_SPINTR_SP3_AGPIO139:
		data->kid_agpio = 139;
		break;
	default:
		bop_panic("Unknown SPINTR mode");
	}

	/*
	 * Configure the interrupt line from the SP that signals when it
	 * has information for us. The IOMUX has already been configured for
	 * us in oxide_derive_platform(); we still have to set up GPIO
	 * parameters as we'd like.
	 */
	data->kid_gpio_block = fch_gpio_mmio_block();
	data->kid_gpio_reg = FCH_GPIO_GPIO_MMIO(data->kid_gpio_block,
	    data->kid_agpio);

	uint32_t gpio = mmio_reg_read(data->kid_gpio_reg);
	gpio = FCH_GPIO_GPIO_SET_OUT_EN(gpio, 0);
	gpio = FCH_GPIO_GPIO_SET_PD_EN(gpio, 0);
	gpio = FCH_GPIO_GPIO_SET_PU_EN(gpio, 0);
	gpio = FCH_GPIO_GPIO_SET_TRIG(gpio, FCH_GPIO_GPIO_TRIG_LEVEL);
	gpio = FCH_GPIO_GPIO_SET_LEVEL(gpio, FCH_GPIO_GPIO_LEVEL_ACT_LOW);
	gpio = FCH_GPIO_GPIO_SET_INT_EN(gpio, 0);
	mmio_reg_write(data->kid_gpio_reg, gpio);

	gpio = mmio_reg_read(data->kid_gpio_reg);

	eb_debug_printf("Configured AGPIO%d: %x (input is %s)\n",
	    data->kid_agpio, gpio,
	    FCH_GPIO_GPIO_GET_INPUT(gpio) == FCH_GPIO_GPIO_INPUT_HIGH ?
	    "high" : "low");
}

static void
eb_ipcc_init(void)
{
	kernel_ipcc_data_t *data = &kernel_ipcc_data;
	oxide_ipcc_mode_t mode;

	EB_DBGMSG("kernel_ipcc_init(EARLYBOOT)\n");

	eb_ipcc_init_gpio(data);

	switch ((mode = oxide_board_data->obd_ipccmode)) {
	case IPCC_MODE_UART1:
		if (dw_apb_uart_init(&data->kid_uart, DAP_1, 3000000,
		    AD_8BITS, AP_NONE, AS_1BIT) != 0) {
			bop_panic("Could not initialize SP/Host UART");
		}

		bzero(&kernel_ipcc_ops, sizeof (kernel_ipcc_ops));
		kernel_ipcc_ops.io_poll = eb_ipcc_poll;
		kernel_ipcc_ops.io_flush = eb_ipcc_flush;
		kernel_ipcc_ops.io_read = eb_ipcc_read;
		kernel_ipcc_ops.io_write = eb_ipcc_write;
		kernel_ipcc_ops.io_log = eb_ipcc_log;
		break;
	default:
		bop_panic("Unknown IPCC mode: 0x%x", mode);
	}
}

static void
ebi_ipcc_init(void)
{
	EB_DBGMSG("kernel_ipcc_init(ENABLE_INTERRUPT)\n");
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
	oxide_ipcc_mode_t mode;

	EB_DBGMSG("kernel_ipcc_init(KVMAVAIL)\n");

	switch ((mode = oxide_board_data->obd_ipccmode)) {
	case IPCC_MODE_UART1:
		/*
		 * The UART is re-initialised to move the register MMIO
		 * mappings out of the boot pages.
		 */
		if (dw_apb_uart_reinit(&data->kid_uart) != 0)
			bop_panic("Could not re-initialize SP/Host UART");
		break;
	default:
		bop_panic("Unknown IPCC mode: 0x%x", mode);
	}

	/*
	 * Re-initialise the GPIO MMIO block and register to move the
	 * MMIO mappings out of the boot pages.
	 */
	if (data->kid_agpio != UINT32_MAX) {
		mmio_reg_block_unmap(&data->kid_gpio_block);
		data->kid_gpio_block = fch_gpio_mmio_block();
		data->kid_gpio_reg = FCH_GPIO_GPIO_MMIO(data->kid_gpio_block,
		    data->kid_agpio);
	}

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
	if (oxide_board_data->obd_ipccmode == IPCC_MODE_DISABLED)
		return;
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
		EB_DBGMSG("kernel_ipcc_init(DEVTREE)\n");
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
	 * things.  We must also disable interrupts in case someone is currently
	 * using the device or the normal driver has left interrupts enabled;
	 * otherwise, the interrupt handler will consume received data before
	 * our polled consumer gets a chance.
	 */
	if (oxide_board_data->obd_ipccmode == IPCC_MODE_UART1) {
		dw_apb_disable_intr(&kernel_ipcc_data.kid_uart);
		dw_apb_reset_mcr(&kernel_ipcc_data.kid_uart);
	}
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

void
kernel_ipcc_panic(void)
{
	ipcc_panic_buf.ipd_version = IPCC_PANIC_VERSION;

	/*
	 * A panic message is not exactly a gasp, but we are single threaded
	 * here and need to try and get the message to the SP before carrying
	 * on with system dump, reboot, as configured. We don't check the
	 * return code as we are going to carry on regardless.
	 *
	 * The SP is not expected to do anything in response to this message
	 * beyond recording the data and optionally passing it on for
	 * analysis/storage. In particular we do not expect the SP to initiate
	 * a reboot as a result of receiving a panic message; the host may
	 * still have work to do such as dumping to disk or entering kmdb for
	 * an operator to do further investigation.
	 */
	kernel_ipcc_prepare_gasp();
	(void) ipcc_panic(&kernel_ipcc_ops, &kernel_ipcc_data,
	    (uint8_t *)&ipcc_panic_buf, sizeof (ipcc_panic_buf));
}

/*
 * Utility functions that call into ipcc_proto. These are used by long running
 * multi-command operations such as the phase 2 image transfer that wish to
 * take acquire the channel over the whole operation to reduce latency and to
 * avoid having to copy data around unecessarily. Holding the channel allows
 * them to access the returned data directly.
 */

int
kernel_ipcc_acquire(void)
{
	return (ipcc_acquire_channel(&kernel_ipcc_ops, &kernel_ipcc_data));
}

void
kernel_ipcc_release(void)
{
	ipcc_release_channel(&kernel_ipcc_ops, &kernel_ipcc_data, true);
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

/*
 * These interfaces are used a little later in boot but before the root
 * filesystem is mounted.
 */

int
kernel_ipcc_bootfailv(ipcc_host_boot_failure_t reason, const char *fmt,
    va_list va)
{
	size_t bufsize;
	va_list vas;
	char *buf;
	int ret;

	va_copy(vas, va);
	bufsize = vsnprintf(NULL, 0, fmt, vas) + 1;
	va_end(vas);

	bufsize = MIN(bufsize, IPCC_BOOTFAIL_MAX_PAYLOAD);
	buf = kmem_alloc(bufsize, KM_SLEEP);
	(void) vsnprintf(buf, bufsize, fmt, va);

	ret = ipcc_bootfail(&kernel_ipcc_ops, &kernel_ipcc_data, reason,
	    (uint8_t *)buf, bufsize);

	kmem_free(buf, bufsize);
	return (ret);
}

int
kernel_ipcc_bootfail(ipcc_host_boot_failure_t reason, const char *fmt, ...)
{
	va_list va;
	int err;

	va_start(va, fmt);
	err = kernel_ipcc_bootfailv(reason, fmt, va);
	va_end(va);

	return (err);
}

int
kernel_ipcc_keylookup(uint8_t key, uint8_t *buf, size_t *bufl)
{
	ipcc_keylookup_t kl;
	int ret;

	kl.ik_key = key;
	kl.ik_buf = buf;
	kl.ik_buflen = *bufl;

	ret = ipcc_keylookup(&kernel_ipcc_ops, &kernel_ipcc_data, &kl, buf);

	if (ret != 0)
		return (ret);
	if (kl.ik_result != IPCC_KEYLOOKUP_SUCCESS)
		return (ENOENT);

	*bufl = kl.ik_datalen;

	return (0);
}

int
kernel_ipcc_imageblock(uint8_t *hash, uint64_t offset, uint8_t **data,
    size_t *datal)
{
	ipcc_ops_t nops = { 0 };
	int ret;

	/*
	 * Callers of this function must have previously acquired exclusive
	 * access to the IPCC by successfully calling kernel_ipcc_acquire().
	 */
	VERIFY(ipcc_channel_held());

	/*
	 * Enable fast polling. It is safe to modify this here as channel
	 * access has been acquired.
	 */
	ipcc_fastpoll = true;

	/*
	 * Logging is disabled for these requests to avoid spamming the console
	 * (and so that the progress meter is visible).
	 */
	nops = kernel_ipcc_ops;
	nops.io_log = NULL;

	ret = ipcc_imageblock(&nops, &kernel_ipcc_data, hash, offset,
	    data, datal);
	ipcc_fastpoll = false;

	return (ret);
}

/*
 * System Panic Reporting
 * ----------------------
 *
 * When a system panic occurs due to an explicit call to [v]panic() or due to a
 * processor trap, the kernel calls a number of functions in common, ISA and
 * MACH code. The diagram in common/os/panic.c shows this flow. The following
 * functions, all within the Oxide-specific code, are used to build up the
 * final panic information that is sent to the SP when kernel_ipcc_panic() is
 * called:
 *
 *  - die()
 *  - plat_traceback()
 *
 * Earlier in boot there are several mechanisms used for panicking, most of
 * which are explicitly called when a fatal error occurs. These paths are
 * also shown in a diagram in common/os/panic.c. The following functions within
 * Oxide-specific code collect panic information ready for sending to the SP in
 * early boot:
 *
 *  - bop_trap()
 *  - bop_traceback()
 *  - bop_panic()
 *  - prom_panic()
 *
 * The following functions are used to populate parts of ipcc_panic_buf prior
 * to calling kernel_ipcc_panic(), which sends the assembled message to the SP.
 */

void
kipcc_panic_field(ipcc_panic_field_t type, uint64_t val)
{
	switch (type) {
	case IPF_CAUSE:
		/*
		 * In the case of a nested panic, or an early boot trap that
		 * ends up calling into bop_panic(), preserve the original
		 * panic cause rather than overwriting it.
		 */
		if (ipcc_panic_buf.ipd_cause == 0)
			ipcc_panic_buf.ipd_cause = val & 0xffff;
		break;
	case IPF_ERROR:
		ipcc_panic_buf.ipd_error = val & 0xffff;
		break;
	case IPF_CPUID:
		ipcc_panic_buf.ipd_cpuid = val & 0xffffffff;
		break;
	case IPF_THREAD:
		ipcc_panic_buf.ipd_thread = val;
		break;
	case IPF_ADDR:
		ipcc_panic_buf.ipd_addr = val;
		break;
	case IPF_PC:
		ipcc_panic_buf.ipd_pc = val;
		break;
	case IPF_FP:
		ipcc_panic_buf.ipd_fp = val;
		break;
	case IPF_RP:
		ipcc_panic_buf.ipd_rp = val;
		break;
	}
}

void
kipcc_panic_vmessage(const char *fmt, va_list ap)
{
	(void) vsnprintf(ipcc_panic_buf.ipd_message,
	    sizeof (ipcc_panic_buf.ipd_message), fmt, ap);
}

void
kipcc_panic_message(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	kipcc_panic_vmessage(fmt, ap);
	va_end(ap);
}

void
kipcc_panic_stack_item(uintptr_t addr, const char *sym, off_t off)
{
	ipcc_panic_stack_t *stack;

	if (ipcc_panic_buf.ipd_stackidx >= IPCC_PANIC_STACKS)
		return;

	stack = &ipcc_panic_buf.ipd_stack[ipcc_panic_buf.ipd_stackidx++];

	stack->ips_addr = addr;
	if (sym != NULL) {
		/*
		 * The symbol does not have be NUL-terminated since it is being
		 * written to a buffer with a fixed known size. The global
		 * ipcc_panic_buf variable will have been initialised with
		 * zeros so a shorter string will end up zero padded to the
		 * size of the buffer. The most likely consumers of these data
		 * are rust programs using hubpack, which works with fixed size
		 * arrays rather than C-style strings.
		 */
		bcopy((char *)sym, stack->ips_symbol,
		    MIN(IPCC_PANIC_SYMLEN, strlen(sym)));
		stack->ips_offset = off;
	}
}

void
kipcc_panic_vdata(const char *fmt, va_list ap)
{
	size_t datalen, space;

	space = sizeof (ipcc_panic_buf.ipd_data) - ipcc_panic_buf.ipd_dataidx;
	if (space == 0)
		return;

	datalen = vsnprintf(ipcc_panic_buf.ipd_data +
	    ipcc_panic_buf.ipd_dataidx, space, fmt, ap);

	if (datalen <= space)
		ipcc_panic_buf.ipd_dataidx += datalen;
	else
		ipcc_panic_buf.ipd_dataidx = sizeof (ipcc_panic_buf.ipd_data);
}

void
kipcc_panic_data(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	kipcc_panic_vdata(fmt, ap);
	va_end(ap);
}
