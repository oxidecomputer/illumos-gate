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

#ifndef _LIBISPI_H
#define	_LIBISPI_H

/*
 * This is a private library for interacting with SPI devices for illumos. No
 * compatibility guarantees should be assumed. As discussed below we expect the
 * API to change over time.
 *
 * SPI devices, unlike other classes of storage devices that we often deal with
 * (ATA, NVMe, SCSI, etc.) are not particularly standardized in any way. There
 * are some defacto standards, but the specifics for a lot of these devices end
 * up changing substantially. That is, while many devices have the same basic
 * read and write command, what kinds of erase are available, what the sector
 * size of such erases are, what the opcodes of those are, etc.
 *
 * While JEDEC eventually added a specification for obtaining some of the basic
 * information about a device, which gives us some information about these
 * devices, which is in the form of JESD216 -- Serial Flash Discoverable
 * Parameters (SFDP). Of course, this doesn't tell us quite everything, but if
 * supported by the device then we will use it. The contents of this have
 * shifted over time and contain various revisions. When we have this knowledge,
 * we will attempt to use it where possible. The amount of information that we
 * get still helps with some things around erases, but also is incomplete as it
 * doesn't tell us what the command is for basic 1-1-1 reads and writes.
 *
 * The set of SPI NOR devices we expect we will have to support will increase
 * over time; however, our expectations are that most will support basic SFDP so
 * using that as a base will make sense. In the interim though, we're making the
 * following assumptions about SPI chips:
 *
 *   o They support a basic Page Program at 0x02
 *   o They support a basic Read at 0x03
 *   o They support a Write Disable at 0x04
 *   o They support a Read Status Register at 0x05
 *   o They support a Write Enable at 0x06
 *   o They support a full Chip Erase at at 0x60 or 0xc7
 *
 * While most of the basic support we expect to be more universal than not, we
 * know that 'Chip Erase' will not be supported on multi-die based devices and
 * this is where we expect to leverage more of the features of SFDP. While the
 * use of the JEDEC READ ID command is appealing, vendors do not put their JEDEC
 * bank into the actual command. This makes it hard to guarantee that.
 *
 * Currently a consumer uses this library by first getting a library handle via
 * 'ispi_init()' and then sets a device to operate on with 'ispi_set_dev()'. At
 * this point, a client is required to set information about the device via
 * 'ispi_set_size()' as right now we do not have any discovery methods available
 * to get information about the SPI device.
 *
 * In the future if we support auto-discovery of features through SFDP then we'd
 * want to offer up to the client a bunch of options for discovering information
 * and allow them to select which methods to use to fill in information both for
 * us and for them to get more there. Eventually this could evolve into more
 * built-in knowledge for specific chips.
 */

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ispi_err {
	ISPI_ERR_OK	= 0,
	/*
	 * Indicates that a file descriptor argument is not valid. Generally,
	 * this means that it refers to an invalid value (e.g. less than 0).
	 */
	ISPI_ERR_BAD_FD,
	/*
	 * Indicates that an attempt was made to set up a device; however, one
	 * is currently set.
	 */
	ISPI_ERR_DEVICE_EXISTS,
	/*
	 * Indicates that the requested sector size is invalid because it is not
	 * a multiple of 64 KiB.
	 */
	ISPI_ERR_SIZE_NOT_64K_ALIGNED,
	/*
	 * Indicates that the requested sector size is beyond the device's
	 * addressing capabilities. Note, at this time 4-byte addressing is not
	 * supported.
	 */
	ISPI_ERR_SIZE_BEYOND_DEV_ADDR,
	/*
	 * The device size is unknown.
	 */
	ISPI_ERR_SIZE_UNKNOWN,
	/*
	 * No device has been set.
	 */
	ISPI_ERR_NO_DEVICE,
	/*
	 * I/O request would exceed known device size, induces overflow, etc.
	 */
	ISPI_ERR_IO_BAD_OFFSET,
	/*
	 * An error occurred trying to perform the system I/O via a SPIDEV
	 * ioctl. Additional information is available in the system error.
	 */
	ISPI_ERR_SYSTEM_SPIDEV,
	/*
	 * Indicates that a bad timeout type was used.
	 */
	ISPI_ERR_BAD_TIMEOUT,
	/*
	 * Indicates that we hit a timeout while waiting for an I/O to complete.
	 */
	ISPI_ERR_IO_TIMED_OUT
} ispi_err_t;

typedef struct ispi ispi_t;

extern ispi_t *ispi_init(void);
extern void ispi_fini(ispi_t *);

extern ispi_err_t ispi_err(ispi_t *);
extern int32_t ispi_syserr(ispi_t *);
extern const char *ispi_errmsg(ispi_t *);

extern boolean_t ispi_set_dev(ispi_t *, int);

/*
 * This is used to get and set the amount of time that we're willing to poll for
 * a write/erase operation to complete. Note, different operations have
 * different amounts of time. By default, for a chip bulk erase we'll poll for
 * up to 5 minutes; however, for other operations like a normal page program
 * that is currently defaulting to 500 ms. The values here are all in ms as we
 * may likely sleep between checks.
 */
typedef enum {
	ISPI_TIME_CHIP_ERASE,
	ISPI_TIME_PROGRAM
} ispi_timeout_t;

extern boolean_t ispi_get_timeout(ispi_t *, ispi_timeout_t, uint32_t *);
extern boolean_t ispi_set_timeout(ispi_t *, ispi_timeout_t, uint32_t);

/*
 * Set the size of the flash in bytes. This may be initially discovered through
 * ispi_chip_discover(). However, it can also be set manually. The byte size
 * must be a multiple of 64 KiB sectors when set.
 */
extern boolean_t ispi_set_size(ispi_t *, uint64_t);
extern boolean_t ispi_get_size(ispi_t *, uint64_t *);

/*
 * Read a given range of bytes form a SPI device at a specified byte offset.
 */
extern boolean_t ispi_read(ispi_t *, uint64_t, uint64_t, uint8_t *);

/*
 * Write a given range of bytes from a SPI device at a specified byte offset.
 * Note, this takes care of handling pages and related. It does not handle
 * erasing.
 */
extern boolean_t ispi_write(ispi_t *, uint64_t, uint64_t, const uint8_t *);

/*
 * This erases the entire chip. Currently it assumes single die, but the intent
 * is that this would be usable for multiple dies as well. The write enable will
 * be set and cleared as part of this.
 */
extern boolean_t ispi_chip_erase(ispi_t *);

#ifdef __cplusplus
}
#endif

#endif /* _LIBISPI_H */
