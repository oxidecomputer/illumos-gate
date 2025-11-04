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

#include <sys/types.h>
#include <sys/boot_debug.h>
#include <sys/clock.h>
#include <sys/cmn_err.h>
#include <sys/crypto/api.h>
#include <sys/dw_apb_uart.h>
#include <sys/file.h>
#include <sys/ipcc_proto.h>
#include <sys/kernel_ipcc.h>
#include <sys/panic.h>
#include <sys/privregs.h>
#include <sys/psm_defs.h>
#include <sys/reboot.h>
#include <sys/stdbool.h>
#include <sys/sha2.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/systm.h>
#include <sys/uart.h>
#include <sys/espi_impl.h>
#include <vm/kboot_mmu.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/io/fch/uart.h>
#include <sys/io/fch/espi.h>
#include <sys/archsystm.h>
#include <sys/platform_detect.h>
#include <sys/cpu.h>
#include <sys/apob.h>

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
 * A small buffer used for assembling data for addition to ipcc_panic_buf
 * during a panic.
 */
static uint8_t ipcc_panic_scratch[0x100];

/*
 * Functions for using IPCC from the kernel.
 */

typedef struct {
	uint32_t		kid_agpio;
	mmio_reg_block_t	kid_gpio_block;
	mmio_reg_t		kid_gpio_reg;

	/* Only one or other of the following two will end up being used */
	dw_apb_uart_t		kid_uart;
	mmio_reg_block_t	kid_espi_block;
} kernel_ipcc_data_t;

static kernel_ipcc_data_t kernel_ipcc_data;

static bool
eb_ipcc_readintr(void *arg)
{
	if (oxide_board_data->obd_ipccspintr == IPCC_SPINTR_DISABLED)
		return (false);

	kernel_ipcc_data_t *data = arg;
	const uint32_t gpio = mmio_reg_read(data->kid_gpio_reg);

	return (FCH_GPIO_GPIO_GET_INPUT(gpio) == FCH_GPIO_GPIO_INPUT_LOW);
}

static int
eb_ipcc_poll(void *arg, ipcc_pollevent_t ev, ipcc_pollevent_t *revp,
    uint64_t timeout_ms, bool (*readable)(void *), bool (*writable)(void *))
{
	uint64_t elapsed = 0, uselapsed = 0;
	ipcc_pollevent_t rev = 0;

	for (;;) {
		if ((ev & IPCC_INTR) != 0 && eb_ipcc_readintr(arg))
			rev |= IPCC_INTR;
		if ((ev & IPCC_POLLIN) != 0 && readable(arg))
			rev |= IPCC_POLLIN;
		if ((ev & IPCC_POLLOUT) != 0 && writable(arg))
			rev |= IPCC_POLLOUT;
		if (rev != 0)
			break;

		if (ipcc_fastpoll) {
			tenmicrosec();
			if (++uselapsed % 100 == 0)
				elapsed++;
		} else {
			eb_pausems(10);
			elapsed += 10;
		}
		if (timeout_ms > 0 && elapsed >= timeout_ms)
			return (ETIMEDOUT);
	}

	*revp = rev;
	return (0);
}

/* Drive the UART directly using the polling functions in dw_apb_uart.c */

static bool
eb_ipcc_uart_readable(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (dw_apb_uart_readable(&data->kid_uart));
}

static bool
eb_ipcc_uart_writable(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (dw_apb_uart_writable(&data->kid_uart));
}

static int
eb_ipcc_uart_poll(void *arg, ipcc_pollevent_t ev, ipcc_pollevent_t *revp,
    uint64_t timeout_ms)
{
	return (eb_ipcc_poll(arg, ev, revp, timeout_ms,
	    eb_ipcc_uart_readable, eb_ipcc_uart_writable));
}

static void
eb_ipcc_uart_flush(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	dw_apb_uart_flush(&data->kid_uart);
}

static int
eb_ipcc_uart_read(void *arg, uint8_t *buf, size_t *len)
{
	kernel_ipcc_data_t *data = arg;

	ASSERT3U(*len, >, 0);
	*buf = dw_apb_uart_rx_one(&data->kid_uart);
	*len = 1;
	return (0);
}

static int
eb_ipcc_uart_write(void *arg, uint8_t *buf, size_t *len)
{
	kernel_ipcc_data_t *data = arg;

	dw_apb_uart_tx(&data->kid_uart, buf, *len);
	return (0);
}

/* Communicate with an eSPI downstream peripheral */

static bool
eb_ipcc_espi_readable(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (espi_oob_readable(data->kid_espi_block));
}

static bool
eb_ipcc_espi_writable(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (espi_oob_writable(data->kid_espi_block));
}

static int
eb_ipcc_espi_poll(void *arg, ipcc_pollevent_t ev, ipcc_pollevent_t *revp,
    uint64_t timeout_ms)
{
	return (eb_ipcc_poll(arg, ev, revp, timeout_ms,
	    eb_ipcc_espi_readable, eb_ipcc_espi_writable));
}

static void
eb_ipcc_espi_flush(void *arg __unused)
{
	kernel_ipcc_data_t *data = arg;

	espi_oob_flush(data->kid_espi_block);
}

static int
eb_ipcc_espi_read(void *arg, uint8_t *buf, size_t *len)
{
	kernel_ipcc_data_t *data = arg;

	ASSERT3U(*len, >, 0);

	return (espi_oob_rx(data->kid_espi_block, buf, len));
}

static int
eb_ipcc_espi_write(void *arg, uint8_t *buf, size_t *len)
{
	kernel_ipcc_data_t *data = arg;

	return (espi_oob_tx(data->kid_espi_block, buf, len));
}

static int
eb_ipcc_espi_open(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	return (espi_acquire(data->kid_espi_block));
}

static void
eb_ipcc_espi_close(void *arg)
{
	kernel_ipcc_data_t *data = arg;

	espi_release(data->kid_espi_block);
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
	case IPCC_SPINTR_SP5_AGPIO2:
		data->kid_agpio = 2;
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

	bzero(&kernel_ipcc_ops, sizeof (kernel_ipcc_ops));

	switch ((mode = oxide_board_data->obd_ipccmode)) {
	case IPCC_MODE_UART1:
		if (dw_apb_uart_init(&data->kid_uart, DAP_1, 3000000,
		    AD_8BITS, AP_NONE, AS_1BIT) != 0) {
			bop_panic("Could not initialize SP/Host UART");
		}

		kernel_ipcc_ops.io_poll = eb_ipcc_uart_poll;
		kernel_ipcc_ops.io_flush = eb_ipcc_uart_flush;
		kernel_ipcc_ops.io_read = eb_ipcc_uart_read;
		kernel_ipcc_ops.io_write = eb_ipcc_uart_write;
		kernel_ipcc_ops.io_log = eb_ipcc_log;
		break;
	case IPCC_MODE_ESPI0:
		data->kid_espi_block = fch_espi_mmio_block(0);

		if (espi_init(data->kid_espi_block) != 0)
			bop_panic("Cannot initialise eSPI IPCC");

		kernel_ipcc_ops.io_open = eb_ipcc_espi_open;
		kernel_ipcc_ops.io_close = eb_ipcc_espi_close;
		kernel_ipcc_ops.io_poll = eb_ipcc_espi_poll;
		kernel_ipcc_ops.io_flush = eb_ipcc_espi_flush;
		kernel_ipcc_ops.io_read = eb_ipcc_espi_read;
		kernel_ipcc_ops.io_write = eb_ipcc_espi_write;
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
mb_ipcc_log(void *arg __unused, ipcc_log_type_t type, const char *fmt, ...)
{
	va_list ap;

	if (type == IPCC_LOG_HEX) {
		/*
		 * Hexdump messages are never logged when we're in fast poll
		 * mode. If that's set then we are expecting a lot of IPCC
		 * traffic.
		 */
		if (ipcc_fastpoll)
			return;
#ifndef DEBUG
		/*
		 * In a non-DEBUG kernel the hexdump messages are not logged to
		 * the console.
		 */
		return;
#endif
	}

	int level = (type == IPCC_LOG_WARNING) ? CE_WARN : CE_CONT;

	va_start(ap, fmt);
	vcmn_err(level, fmt, ap);
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
	case IPCC_MODE_ESPI0:
		/*
		 * The eSPI register block needs to be re-initialised to move
		 * the MMIO mappings out of the boot pages.
		 */
		mmio_reg_block_unmap(&data->kid_espi_block);
		data->kid_espi_block = fch_espi_mmio_block(0);
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
	ipcc_panic_buf.ipd_hrtime = panic_hrtime;
	ipcc_panic_buf.ipd_hrestime = panic_hrestime;

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
	    (uint8_t *)&ipcc_panic_buf,
	    offsetof(ipcc_panic_data_t, ipd_items) +
	    ipcc_panic_buf.ipd_items_len);
}

/*
 * Utility functions that call into ipcc_proto. These are used by long running
 * multi-command operations such as the phase 2 image transfer that wish to
 * take acquire the channel over the whole operation to reduce latency and to
 * avoid having to copy data around unecessarily. Holding the channel allows
 * them to access the returned data directly.
 */

int
kernel_ipcc_acquire(ipcc_channel_flag_t flags)
{
	int ret;

	ret = ipcc_acquire_channel(&kernel_ipcc_ops, &kernel_ipcc_data);
	if (ret == 0 && flags != 0)
		ipcc_channel_setflags(flags);

	return (ret);
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

/*
 * Read the current APOB out from the SP over IPCC. On success this returns an
 * allocated `apob_hdl_t *` which must be freed via kernel_ipcc_apobfree()
 * once no longer required. On failure the return value is NULL.
 */
apob_hdl_t *
kernel_ipcc_apobread(void)
{
	ipcc_apob_read_t readr;
	uint64_t offset;
	uint8_t header[APOB_MIN_LEN];
	size_t alloclen, rem, len;
	uint8_t *buf = NULL;
	size_t hdl_alloclen = apob_handle_size();
	apob_hdl_t *hdl = NULL;
	int ret;

	ret = kernel_ipcc_acquire(0);
	if (ret != 0) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "Attempt to acquire IPCC channel for APOB header failed "
		    "with error %d", ret);
		goto fail;
	}

	len = sizeof (header);
	ret = ipcc_apob_read(&kernel_ipcc_ops, &kernel_ipcc_data,
	    0, header, &len, &readr);
	kernel_ipcc_release();

	if (ret != 0) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "Attempt to read APOB header failed with error %d", ret);
		goto fail;
	}

	if (readr != IPCC_APOB_READ_SUCCESS) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data,
		    IPCC_LOG_WARNING,
		    "Attempt to read APOB header was met with SP response %s",
		    ipcc_apob_read_errstr(readr));
		goto fail;
	}

	hdl = kmem_zalloc(hdl_alloclen, KM_SLEEP);
	alloclen = apob_init_handle(hdl, header, len);

	kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_DEBUG,
	    "Read APOB header, length: 0x%zx\n", alloclen);

	if (alloclen == 0) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB read: %s", apob_errmsg(hdl));
		kmem_free(hdl, hdl_alloclen);
		goto fail;
	}
	kmem_free(hdl, hdl_alloclen);

	if (alloclen > IPCC_APOB_MAX_SIZE) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB read: invalid data length (0x%zx)", alloclen);
		goto fail;
	}

	buf = kmem_alloc(alloclen, KM_NOSLEEP);
	if (buf == NULL) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB read: failed to allocate 0x%zx bytes of memory",
		    alloclen);
		goto fail;
	}

	kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_DEBUG,
	    "Reading 0x%zx bytes of APOB from the SP\n", alloclen);

	VERIFY0(kernel_ipcc_acquire(IPCC_CHAN_QUIET));
	ipcc_fastpoll = true;

	rem = alloclen;
	offset = 0;
	while (rem > 0) {
		len = MIN(rem, IPCC_APOB_MAX_PAYLOAD);

		ret = ipcc_apob_read(&kernel_ipcc_ops, &kernel_ipcc_data,
		    offset, buf + offset, &len, &readr);
		if (ret != 0) {
			kernel_ipcc_ops.io_log(&kernel_ipcc_data,
			    IPCC_LOG_WARNING,
			    "Attempt to read APOB offset 0x%lx length 0x%zx "
			    "failed with error %d", offset, len, ret);
			break;
		}

		if (readr != IPCC_APOB_READ_SUCCESS) {
			kernel_ipcc_ops.io_log(&kernel_ipcc_data,
			    IPCC_LOG_WARNING,
			    "Attempt to read APOB offset 0x%lx length 0x%zx "
			    "was met with SP response %s",
			    offset, len, ipcc_apob_read_errstr(readr));
			break;
		}

		rem -= len;
		offset += len;
	}

	ipcc_fastpoll = false;
	kernel_ipcc_release();

	if (rem > 0)
		goto fail;

	hdl = kmem_zalloc(hdl_alloclen, KM_SLEEP);
	len = apob_init_handle(hdl, buf, alloclen);
	if (len == alloclen) {
		/*
		 * buf is not freed here since it is now pointed to by the
		 * apob handle.
		 */
		return (hdl);
	}

	if (len == 0) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB read: %s", apob_errmsg(hdl));
	} else {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB read: invalid final length "
		    "(got 0x%zx, expected 0x%zx)", len, alloclen);
	}
	kmem_free(hdl, hdl_alloclen);

fail:
	if (buf != NULL)
		kmem_free(buf, alloclen);
	return (NULL);
}

void
kernel_ipcc_apobfree(apob_hdl_t *hdl)
{
	const uint8_t *raw;
	uint8_t *buf;
	size_t len;

	if (hdl == NULL)
		return;

	raw = apob_get_raw(hdl);
	buf = __DECONST(uint8_t *, raw);
	len = apob_get_len(hdl);
	kmem_free(buf, len);
	kmem_free(hdl, apob_handle_size());
}

int
kernel_ipcc_apobwrite(const apob_hdl_t *hdl)
{
	const uint8_t *buf;
	size_t bufl;
	int ret = 0;

	/*
	 * If we have no data, jump straight to the commit phase which lets the
	 * SP know that we are done in relation to APOB data for this boot.
	 */
	if (hdl == NULL)
		goto commit;

	buf = apob_get_raw(hdl);
	bufl = apob_get_len(hdl);

	/*
	 * Calculate the hash and prepare to transmit the APOB.
	 */
	uint8_t hash[SHA256_DIGEST_LENGTH];
	crypto_context_t cc;
	crypto_mechanism_t cm;

	bzero(&cm, sizeof (cm));
	cm.cm_type = crypto_mech2id(SUN_CKM_SHA256);
	if (cm.cm_type == CRYPTO_MECH_INVALID) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB hash: invalid crypto hash mechanism SHA256");
		return (EIO);
	}

	ret = crypto_digest_init(&cm, &cc, NULL);
	if (ret != CRYPTO_SUCCESS) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB hash: crypto_digest_init() failed %d", ret);
		return (EIO);
	}

	crypto_data_t cd = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_length = bufl,
		.cd_raw = {
			.iov_base = (int8_t *)buf,
			.iov_len = bufl,
		},
	};

	ret = crypto_digest_update(cc, &cd, 0);
	if (ret != CRYPTO_SUCCESS) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB hash: crypto_digest_update() failed %d", ret);
		crypto_cancel_ctx(cc);
		return (EIO);
	}

	crypto_data_t cf = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_length = sizeof (hash),
		.cd_raw = {
			.iov_base = (void *)hash,
			.iov_len = sizeof (hash),
		},
	};

	ret = crypto_digest_final(cc, &cf, 0);
	if (ret != CRYPTO_SUCCESS) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB hash: crypto_digest_final() failed %d", ret);
		crypto_cancel_ctx(cc);
		return (EIO);
	}

	kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_DEBUG,
	    "APOB length: 0x%x\n", bufl);
	kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_DEBUG,
	    "APOB SHA256: "
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x\n",
	    hash[0], hash[1], hash[2], hash[3],
	    hash[4], hash[5], hash[6], hash[7],
	    hash[8], hash[9], hash[10], hash[11],
	    hash[12], hash[13], hash[14], hash[15],
	    hash[16], hash[17], hash[18], hash[19],
	    hash[20], hash[21], hash[22], hash[23],
	    hash[24], hash[25], hash[26], hash[27],
	    hash[28], hash[29], hash[30], hash[31]);

	ipcc_apob_begin_t beginr;
	ret = ipcc_apob_begin(&kernel_ipcc_ops, &kernel_ipcc_data,
	    bufl, IPCC_APOB_ALG_SHA256, hash, sizeof (hash), &beginr);
	if (ret != 0)
		return (ret);
	if (beginr != IPCC_APOB_BEGIN_SUCCESS) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB Begin: %s", ipcc_apob_begin_errstr(beginr));
		return (EIO);
	}

	/*
	 * We want to transmit the following data quickly, without constantly
	 * acquiring and releasing the channel and with faster response
	 * polling. Acquire the channel now.
	 */
	VERIFY0(kernel_ipcc_acquire(IPCC_CHAN_QUIET));
	ipcc_fastpoll = true;

	uint64_t offset = 0;
	while (bufl > 0) {
		size_t sendlen = MIN(bufl, IPCC_APOB_MAX_PAYLOAD);
		ipcc_apob_data_t datar;

		ret = ipcc_apob_data(&kernel_ipcc_ops, &kernel_ipcc_data,
		    offset, buf + offset, sendlen, &datar);

		if (ret != 0) {
			kernel_ipcc_ops.io_log(&kernel_ipcc_data,
			    IPCC_LOG_WARNING,
			    "Attempt to send APOB offset 0x%lx length 0x%zx "
			    "failed with error %d", offset, sendlen, ret);
			break;
		}

		if (datar != IPCC_APOB_DATA_SUCCESS) {
			kernel_ipcc_ops.io_log(&kernel_ipcc_data,
			    IPCC_LOG_WARNING,
			    "Attempt to send APOB offset 0x%lx length 0x%zx "
			    "was met with SP response %s",
			    offset, sendlen, ipcc_apob_data_errstr(datar));
			ret = EIO;
			break;
		}

		bufl -= sendlen;
		offset += sendlen;
	}

	ipcc_fastpoll = false;
	kernel_ipcc_release();

	ipcc_apob_commit_t commitr;
commit:
	ret = ipcc_apob_commit(&kernel_ipcc_ops, &kernel_ipcc_data, &commitr);
	if (commitr != IPCC_APOB_COMMIT_SUCCESS) {
		kernel_ipcc_ops.io_log(&kernel_ipcc_data, IPCC_LOG_WARNING,
		    "APOB Commit: %s", ipcc_apob_commit_errstr(commitr));
		return (EIO);
	}

	return (ret);
}

int
kernel_ipcc_imageblock(uint8_t *hash, uint64_t offset, uint8_t **data,
    size_t *datal)
{
	static ipcc_ops_t nops = { 0 };
	int ret;

	if (nops.io_write == NULL) {
		nops = kernel_ipcc_ops;
		nops.io_log = NULL;
	}

	/*
	 * Callers of this function must have previously acquired exclusive
	 * access to the IPCC by successfully calling kernel_ipcc_acquire().
	 */
	VERIFY(ipcc_channel_held());

	/*
	 * Enable fast polling around the retrieval. It is safe to modify this
	 * here as channel access has been acquired.
	 */
	ipcc_fastpoll = true;
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
kipcc_panic_regs(struct regs *rp)
{
	bcopy(rp, &ipcc_panic_buf.ipd_regs, sizeof (*rp));
}

static void
ipcc_panic_add(ipcc_panic_item_t type, const uint8_t *data, uint16_t len)
{
	uint16_t avail = sizeof (ipcc_panic_buf.ipd_items) -
	    ipcc_panic_buf.ipd_items_len;
	ipcc_panic_tlvhdr_t *hdr = (ipcc_panic_tlvhdr_t *)
	    &ipcc_panic_buf.ipd_items[ipcc_panic_buf.ipd_items_len];
	const uint16_t hdrlen = sizeof (*hdr);

	if (avail < hdrlen + 1) {
		/*
		 * If we don't even have space for 1 byte of data after the
		 * header, give up on this item.
		 */
		return;
	}

	len += hdrlen;
	len = MIN(len, avail);

	hdr->ipth_type = (uint8_t)type;
	hdr->ipth_len = len;

	bcopy(data, hdr->ipth_data, len - hdrlen);
	ipcc_panic_buf.ipd_nitems++;
	ipcc_panic_buf.ipd_items_len += len;
}

void
kipcc_panic_vmessage(const char *fmt, va_list ap)
{
	int len;

	len = vsnprintf((char *)ipcc_panic_scratch, sizeof (ipcc_panic_scratch),
	    fmt, ap);
	ipcc_panic_add(IPI_MESSAGE, ipcc_panic_scratch, len);
}

void
kipcc_panic_message(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	kipcc_panic_vmessage(fmt, ap);
	va_end(ap);
}

/*
 * A stack item is encoded as:
 *	uint64_t	address
 *	uint64_t	offset
 *	uint8_t[]	symbol name (may be zero-length)
 */
void
kipcc_panic_stack_item(uintptr_t addr, const char *sym, off_t off)
{
	ipcc_panic_stackentry_t *s =
	    (ipcc_panic_stackentry_t *)ipcc_panic_scratch;
	uint16_t len = 0;

	s->ipse_addr = addr;
	s->ipse_offset = off;
	len += sizeof (*s);

	if (sym != NULL) {
		size_t symlen, cpylen;

		symlen = strlen(sym);
		cpylen = MIN(sizeof (ipcc_panic_scratch) - len, symlen);
		bcopy((char *)sym, s->ipse_symbol, cpylen);
		len += cpylen;
	}

	ipcc_panic_add(IPI_STACKENTRY, ipcc_panic_scratch, len);
}

void
kipcc_panic_vdata(const char *fmt, va_list ap)
{
	uint16_t len;

	len = vsnprintf((char *)ipcc_panic_scratch, sizeof (ipcc_panic_scratch),
	    fmt, ap);
	ipcc_panic_add(IPI_ANCIL, ipcc_panic_scratch, len);
}

void
kipcc_panic_data(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	kipcc_panic_vdata(fmt, ap);
	va_end(ap);
}
