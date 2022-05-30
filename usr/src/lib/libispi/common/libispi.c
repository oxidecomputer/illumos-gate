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

/*
 * Routines to interact with SPI devices.
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/sysmacros.h>
#include <sys/spi.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <libispi.h>

/*
 *  The primary sector size of a SPI device. This is generally expected to be 64
 *  KiB.
 */
#define	SPI_SECTOR_SIZE	0x10000

/*
 * This is the maximum addressable range of a 3-byte and 4-byte SPI device.
 */
#define	SPI_MAX_LEN_3B	0x1000000UL
#define	SPI_MAX_LEN_4B	0x100000000UL

/*
 * Maximum read and write sizes. Note, this is also the alignment that we will
 * require for doing the read/write. In particular, while writes can be up to
 * the 256 bytes listed below, they cannot exceed a 256-byte page in many cases
 * so if we're not writing something aligned, then we're in trouble.
 */
#define	SPI_MAX_IO	256

/*
 * Common SPI commands we're using right now that aren't being discovered.
 */
#define	SPI_CMD_PROGRAM		0x02
#define	SPI_CMD_READ		0x03
#define	SPI_CMD_WRITE_DISABLE	0x04
#define	SPI_CMD_READ_STATUS	0x05
#define	SPI_CMD_READ_STATUS_WIP		0x01
#define	SPI_CMD_WRITE_ENABLE	0x06
#define	SPI_CMD_CHIP_ERASE	0xc7

/*
 * Default timeout values that we use in the library. These are in milliseconds.
 */
#define	ISPI_DEF_TO_CHIP_ERASE	(5 * 1000 * 60)	/* 5s in ms */
#define	ISPI_DEF_TO_PROGRAM	(500)		/* 500ms */

struct ispi {
	int ispi_fd;
	ispi_err_t ispi_err;
	int32_t ispi_syserr;
	char ispi_errmsg[1024];
	uint64_t ispi_chip_size;
	uint32_t ispi_timeouts[2];
};

ispi_err_t
ispi_err(ispi_t *ispi)
{
	return (ispi->ispi_err);
}

int32_t
ispi_syserr(ispi_t *ispi)
{
	return (ispi->ispi_syserr);
}

const char *
ispi_errmsg(ispi_t *ispi)
{
	return (ispi->ispi_errmsg);
}

static boolean_t ispi_error(ispi_t *, ispi_err_t, int32_t, const char *,
    ...)  __PRINTFLIKE(4);

static boolean_t
ispi_error(ispi_t *ispi, ispi_err_t err, int32_t sys, const char *fmt, ...)
{
	va_list ap;

	ispi->ispi_err = err;
	ispi->ispi_syserr = sys;
	va_start(ap, fmt);
	(void) vsnprintf(ispi->ispi_errmsg, sizeof (ispi->ispi_errmsg), fmt,
	    ap);
	va_end(ap);
	return (B_FALSE);
}

static boolean_t
ispi_success(ispi_t *ispi)
{
	ispi->ispi_err = ISPI_ERR_OK;
	ispi->ispi_syserr = 0;
	ispi->ispi_errmsg[0] = '\0';
	return (B_TRUE);
}

boolean_t
ispi_get_timeout(ispi_t *ispi, ispi_timeout_t type, uint32_t *valp)
{
	if (type >= ARRAY_SIZE(ispi->ispi_timeouts)) {
		return (ispi_error(ispi, ISPI_ERR_BAD_TIMEOUT, 0, "invalid "
		    "timeout type: 0x%x", type));
	}

	*valp = ispi->ispi_timeouts[type];
	return (ispi_success(ispi));
}

boolean_t
ispi_set_timeout(ispi_t *ispi, ispi_timeout_t type, uint32_t val)
{
	if (type >= ARRAY_SIZE(ispi->ispi_timeouts)) {
		return (ispi_error(ispi, ISPI_ERR_BAD_TIMEOUT, 0, "invalid "
		    "timeout type: 0x%x", type));
	}

	ispi->ispi_timeouts[type] = val;
	return (ispi_success(ispi));
}

boolean_t
ispi_set_dev(ispi_t *ispi, int fd)
{
	if (fd < 0) {
		return (ispi_error(ispi, ISPI_ERR_BAD_FD, 0, "cannot set "
		    "device to an invalid fd: %d", fd));
	}

	if (ispi->ispi_fd != -1) {
		return (ispi_error(ispi, ISPI_ERR_DEVICE_EXISTS, 0, "SPI device "
		    "already exists, fd %d", ispi->ispi_fd));
	}

	ispi->ispi_fd = fd;

	return (ispi_success(ispi));
}

boolean_t
ispi_get_size(ispi_t *ispi, uint64_t *sizep)
{
	if (ispi->ispi_chip_size == UINT64_MAX) {
		return (ispi_error(ispi, ISPI_ERR_SIZE_UNKNOWN, 0, "SPI chip "
		    "size is unknown"));
	}

	*sizep = ispi->ispi_chip_size;
	return (ispi_success(ispi));
}

boolean_t
ispi_set_size(ispi_t *ispi, uint64_t size)
{
	if ((size % SPI_SECTOR_SIZE) != 0) {
		return (ispi_error(ispi, ISPI_ERR_SIZE_NOT_64K_ALIGNED, 0,
		    "device size 0x%" PRIx64 "is not a multiple of 64 KiB",
		    size));
	}

	/*
	 * In the future, we'll need to check if we have 4-byte addressing
	 * capabilities here and use the 4-byte version instead.
	 */
	if (size > SPI_MAX_LEN_3B) {
		return (ispi_error(ispi, ISPI_ERR_SIZE_BEYOND_DEV_ADDR, 0,
		    "device size 0x%" PRIx64 "is beyond 3-bye addressable "
		    "range (0x%lx)", size, SPI_MAX_LEN_3B));
	}

	ispi->ispi_chip_size = size;
	return (ispi_success(ispi));
}

static boolean_t
ispi_read_status(ispi_t *ispi, uint8_t *statusp)
{
	int ret;
	uint8_t cmd = SPI_CMD_READ_STATUS;

	*statusp = 0;
	spidev_transfer_t poll_xfers[2] = { {
		.tx_buf = &cmd,
		.len = sizeof (cmd),
		.delay_usec = 0,
		.deassert_cs = 0,
	}, {
		.rx_buf = statusp,
		.len = sizeof (*statusp),
		.delay_usec = 0,
		.deassert_cs = 0,
	} };

	spidev_transaction_t poll = { poll_xfers,
	    ARRAY_SIZE(poll_xfers) };

	ret = ioctl(ispi->ispi_fd, SPIDEV_TRANSACTION, &poll);
	if (ret != 0) {
		int e = errno;
		return (ispi_error(ispi, ISPI_ERR_SYSTEM_SPIDEV, e,
		    "failed to perform Read Status Register (0x%x): %s",
		    SPI_CMD_READ_STATUS, strerror(e)));
	}

	return (B_TRUE);
}
typedef enum {
	ISPI_POLL_SUCCESS,
	ISPI_POLL_ERROR,
	ISPI_POLL_TIMEOUT
} ispi_poll_t;

static ispi_poll_t
ispi_status_poll(ispi_t *ispi, ispi_timeout_t to)
{
	hrtime_t start = gethrtime();
	hrtime_t max_time = MSEC2NSEC(ispi->ispi_timeouts[to]);

	for (;;) {
		hrtime_t now;
		struct timespec to;
		uint8_t status;

		if (!ispi_read_status(ispi, &status)) {
			return (ISPI_POLL_ERROR);
		}

		/*
		 * Right now we're using the Status Register rather than the
		 * preferred Flag Status Register. This makes it hard to know
		 * about errors and success. This is dependent on getting out of
		 * the least common denominator aspect of device management.
		 */
		if ((status & SPI_CMD_READ_STATUS_WIP) == 0) {
			return (ISPI_POLL_SUCCESS);
		}

		now = gethrtime();
		if (now - start > max_time) {
			return (ISPI_POLL_TIMEOUT);
		}

		to.tv_sec = 0;
		to.tv_nsec = 1000000;
		(void) nanosleep(&to, NULL);
	}
}

static boolean_t
ispi_check_io(ispi_t *ispi, uint64_t offset, uint64_t len)
{
	if (ispi->ispi_fd < 0) {
		return (ispi_error(ispi, ISPI_ERR_NO_DEVICE, 0, "no SPI device "
		    "set"));
	}

	if (ispi->ispi_chip_size == UINT64_MAX) {
		return (ispi_error(ispi, ISPI_ERR_SIZE_UNKNOWN, 0, "SPI chip "
		    "size is unknown"));
	}

	if (offset + len < MAX(offset, len)) {
		return (ispi_error(ispi, ISPI_ERR_IO_BAD_OFFSET, 0,
		    "combination of offset (0x%" PRIx64 ") and len (%" PRIu64
		    ") would overflow", offset, len));
	}

	if (offset >= ispi->ispi_chip_size || len > ispi->ispi_chip_size ||
	    offset + len > ispi->ispi_chip_size) {
		return (ispi_error(ispi, ISPI_ERR_IO_BAD_OFFSET, 0,
		    "combination of offset (0x%" PRIx64 ") and len (%" PRIu64
		    ") execeed the chip size (0x%" PRIx64, offset, len,
		    ispi->ispi_chip_size));
	}

	return (B_TRUE);
}

static uint32_t
ispi_io_length(uint64_t offset, uint64_t len)
{
	uint32_t sec_rem, io_min;

	/*
	 * We have two considerations for the I/O size here we allow in one go.
	 * The first bit is taking the amount that the user wants to read with
	 * the maximum I/O size (256 bytes) and taking the lesser of those.
	 *
	 * Next, we must consider where we are in the I/O size 256-byte sector
	 * region. Because writes can't span this boundary, we have to futher
	 * constrain this.
	 */
	io_min = MIN(len, SPI_MAX_IO);
	sec_rem = SPI_MAX_IO - (offset % SPI_MAX_IO);

	return (MIN(io_min, sec_rem));
}

/*
 * This currently performs a basic SPI read per the constraints that we lay out
 * in the hader file. In the future this should select opcodes automatically
 * based on read mode that's set and set up the device, e.g. 3-byte, 4-byte,
 * qspi, etc.
 */
boolean_t
ispi_read(ispi_t *ispi, uint64_t offset, uint64_t len, uint8_t *buf)
{
	uint64_t nread = 0;

	if (!ispi_check_io(ispi, offset, len)) {
		return (B_FALSE);
	}

	while (len > 0) {
		int ret;
		uint32_t toread = ispi_io_length(offset, len);
		uint8_t readbuf[4];

		readbuf[0] = SPI_CMD_READ;
		readbuf[1] = (uint8_t)((offset >> 16) & 0xff);
		readbuf[2] = (uint8_t)((offset >> 8) & 0xff);
		readbuf[3] = (uint8_t)(offset & 0xff);

		spidev_transfer_t xfers[2] = { {
			.tx_buf = readbuf,
			.rx_buf = NULL,
			.len = ARRAY_SIZE(readbuf),
			.delay_usec = 0,
			.deassert_cs = 0,
		}, {
			.tx_buf = NULL,
			.rx_buf = buf + nread,
			.len = toread,
			.delay_usec = 0,
			.deassert_cs = 1,
		} };

		spidev_transaction_t xact = { xfers, ARRAY_SIZE(xfers) };
		ret = ioctl(ispi->ispi_fd, SPIDEV_TRANSACTION, &xact);
		if (ret != 0) {
			int e = errno;
			return (ispi_error(ispi, ISPI_ERR_SYSTEM_SPIDEV, e,
			    "failed to perform read transaction command 0x%x, "
			    "offset: 0x%" PRIx64 ", length: %u: %s", readbuf[0],
			    offset, toread, strerror(e)));
		}

		nread += toread;
		offset += toread;
		len -= toread;
	}

	return (ispi_success(ispi));
}

/*
 * While we'd prefer to inline the write enable as part of the internal
 * transactions, we are doing this as a separate transaction right now.
 */
static boolean_t
ispi_write_enable(ispi_t *ispi)
{
	int ret;
	const uint8_t wren_cmd = SPI_CMD_WRITE_ENABLE;

	spidev_transfer_t wren_xfer[1] = { {
		.tx_buf = &wren_cmd,
		.len = sizeof (wren_cmd),
		.delay_usec = 0,
		.deassert_cs = 0,
	} };

	spidev_transaction_t wren_txn = { wren_xfer, ARRAY_SIZE(wren_xfer) };
	ret = ioctl(ispi->ispi_fd, SPIDEV_TRANSACTION, &wren_txn);
	if (ret != 0) {
		int e = errno;
		return (ispi_error(ispi, ISPI_ERR_SYSTEM_SPIDEV, e,
		    "failed to perform Write Enable (0x%x) operation: %s",
		    SPI_CMD_WRITE_ENABLE, strerror(e)));
	}

	return (B_TRUE);
}

/*
 * Counterpart to ispi_read() with all the same caveats.
 */
boolean_t
ispi_write(ispi_t *ispi, uint64_t offset, uint64_t len, const uint8_t *buf)
{
	uint64_t nwrite = 0;

	if (!ispi_check_io(ispi, offset, len)) {
		return (B_FALSE);
	}

	while (len > 0) {
		int ret;
		uint32_t towrite = ispi_io_length(offset, len);
		const uint8_t wren_cmd = SPI_CMD_WRITE_ENABLE;
		uint8_t writebuf[4];
		ispi_poll_t pret;

		writebuf[0] = SPI_CMD_PROGRAM;
		writebuf[1] = (uint8_t)((offset >> 16) & 0xff);
		writebuf[2] = (uint8_t)((offset >> 8) & 0xff);
		writebuf[3] = (uint8_t)(offset & 0xff);

		spidev_transfer_t write_xfers[3] = { {
			.tx_buf = &wren_cmd,
			.len = sizeof (wren_cmd),
			.delay_usec = 0,
			.deassert_cs = 1,
		}, {
			.tx_buf = writebuf,
			.len = ARRAY_SIZE(writebuf),
			.delay_usec = 0,
			.deassert_cs = 0,
		}, {
			.tx_buf = buf + nwrite,
			.len = towrite,
			.delay_usec = 0,
			.deassert_cs = 0,
		} };

		spidev_transaction_t write_txn = { write_xfers,
		    ARRAY_SIZE(write_xfers) };
		ret = ioctl(ispi->ispi_fd, SPIDEV_TRANSACTION, &write_txn);
		if (ret != 0) {
			int e = errno;
			return (ispi_error(ispi, ISPI_ERR_SYSTEM_SPIDEV, e,
			    "failed to perform Write Enable (0x%x) and Program "
			    "Page (0x%x) operation at offset 0x%" PRIx64 ", "
			    "length %u bytes: %s", SPI_CMD_WRITE_ENABLE,
			    SPI_CMD_PROGRAM, offset, towrite, strerror(e)));
		}

		pret = ispi_status_poll(ispi, ISPI_TIME_PROGRAM);
		switch (pret) {
		case ISPI_POLL_SUCCESS:
			break;
		case ISPI_POLL_ERROR:
			return (B_FALSE);
		case ISPI_POLL_TIMEOUT:
			return (ispi_error(ispi, ISPI_ERR_IO_TIMED_OUT, 0,
			    "timed out waiting after %u ms for program page "
			    "operation to finish at offset 0x%" PRIx64 ", "
			    "length: %u",
			    ispi->ispi_timeouts[ISPI_TIME_CHIP_ERASE], offset,
			    towrite));
		default:
			abort();
		}

		nwrite += towrite;
		offset += towrite;
		len -= towrite;
	}

	return (ispi_success(ispi));
}

/*
 * Our general sequence for performing a chip erase is to first issue a Write
 * Enable command followed by the bulk erase. After that we will spin on the
 * status register to see if has completed or not.
 */
boolean_t
ispi_chip_erase(ispi_t *ispi)
{
	int ret;
	const uint8_t bulk_erase = SPI_CMD_CHIP_ERASE;
	ispi_poll_t pret;

	if (ispi->ispi_fd < 0) {
		return (ispi_error(ispi, ISPI_ERR_NO_DEVICE, 0, "no SPI device "
		    "set"));
	}

	if (!ispi_write_enable(ispi)) {
		return (B_FALSE);
	}

	spidev_transfer_t erase_xfers[1] = { {
		.tx_buf = &bulk_erase,
		.rx_buf = NULL,
		.len = sizeof (bulk_erase),
		.delay_usec = 0,
		.deassert_cs = 0,
	} };

	spidev_transaction_t erase = { erase_xfers, ARRAY_SIZE(erase_xfers) };
	ret = ioctl(ispi->ispi_fd, SPIDEV_TRANSACTION, &erase);
	if (ret != 0) {
		int e = errno;
		return (ispi_error(ispi, ISPI_ERR_SYSTEM_SPIDEV, e,
		    "failed to perform Write Enable (0x%x) and Bulk Erase "
		    "(0x%x) operations: %s", SPI_CMD_WRITE_ENABLE,
		    SPI_CMD_CHIP_ERASE, strerror(e)));
	}

	pret = ispi_status_poll(ispi, ISPI_TIME_CHIP_ERASE);
	switch (pret) {
	case ISPI_POLL_SUCCESS:
		return (B_TRUE);
	case ISPI_POLL_ERROR:
		return (B_FALSE);
	case ISPI_POLL_TIMEOUT:
		return (ispi_error(ispi, ISPI_ERR_IO_TIMED_OUT, 0,
		    "timed out waiting for bulk erase to complete after %u ms",
		    ispi->ispi_timeouts[ISPI_TIME_CHIP_ERASE]));
	default:
		abort();
	}
}

ispi_t *
ispi_init(void)
{
	ispi_t *ispi;

	ispi = calloc(1, sizeof (ispi_t));
	if (ispi == NULL) {
		return (NULL);
	}

	ispi->ispi_fd = -1;
	ispi->ispi_syserr = ISPI_ERR_OK;
	ispi->ispi_chip_size = UINT64_MAX;
	ispi->ispi_timeouts[ISPI_TIME_CHIP_ERASE] = ISPI_DEF_TO_CHIP_ERASE;
	ispi->ispi_timeouts[ISPI_TIME_PROGRAM] = ISPI_DEF_TO_PROGRAM;

	return (ispi);
}

void
ispi_fini(ispi_t *ispi)
{
	if (ispi == NULL) {
		return;
	}

	free(ispi);
}
