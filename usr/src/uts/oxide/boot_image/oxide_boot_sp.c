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
 * Oxide Image Boot: SP image source. Fetches a ramdisk image from the local
 * service processor using IPCC.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/kernel_ipcc.h>

#include "oxide_boot.h"

/*
 * The image hash is used as part of the protocol for retrieving image
 * fragments from the SP. If the checksum algorithm used for phase 2 images
 * changes, then protocol changes will also be necessary. Check that the
 * hash length used in the protocol matches.
 */
CTASSERT(IPCC_IMAGE_HASHLEN == OXBOOT_CSUMLEN_SHA256);

/*
 * The protocol used between this module and MGS, via the SP, consists of a
 * header block and then data which comprises the image itself. For now, MGS
 * just sends the raw header from the phase 2 disk image, and so the
 * definitions here match those in oxide_boot_disk.c - this will change in the
 * future to incorporate more data that the system needs (such as the identity
 * of the real phase 2 hash which should be subsequently fetched and
 * installed).
 */
#define	OXBOOT_SP_VERSION_1		1
#define	OXBOOT_SP_VERSION		OXBOOT_SP_VERSION_1

#define	OXBOOT_SP_MAGIC			0x1DEB0075
#define	OXBOOT_SP_HEADER_SIZE		0x1000

typedef struct oxide_boot_sp_header {
	uint32_t obsh_magic;
	uint32_t obsh_version;

	uint64_t obsh_image_size;
	uint64_t obsh_target_size;

	uint8_t obsh_sha256[OXBOOT_CSUMLEN_SHA256];

	char obsh_dataset[OXBOOT_DATASET_LEN];
} oxide_boot_sp_header_t;

static bool
oxide_boot_sp_fail(ipcc_host_boot_failure_t reason, const char *fmt, ...)
{
	va_list va;

	/*
	 * We're heading out of this module, release the channel, and release
	 * it now so that we can call kernel_ipcc_bootfailv().
	 */
	kernel_ipcc_release();

	va_start(va, fmt);
	(void) vprintf(fmt, va);
	va_end(va);
	va_start(va, fmt);
	(void) kernel_ipcc_bootfailv(reason, fmt, va);
	va_end(va);

	return (false);
}

static bool
oxide_boot_sp_write_block(oxide_boot_t *oxb, uint8_t *block, size_t len,
    size_t pos)
{
	iovec_t iov = {
		.iov_base = (caddr_t)block,
		.iov_len = len,
	};
	if (!oxide_boot_ramdisk_write(oxb, &iov, 1, pos))
		return (false);
	return (true);
}

bool
oxide_boot_sp(oxide_boot_t *oxb)
{
	oxide_boot_sp_header_t obsh = { 0, };
	uint8_t *data;
	size_t datal;
	int err;

	printf("TRYING: boot sp\n");

	/*
	 * Retrieving a phase 2 image from the SP involves transferring a
	 * large number of data blocks. Rather than continuously acquiring
	 * and releasing the IPC channel we acquire it once, and then issue
	 * multiple requests. This also allows us to use pointers into the ipcc
	 * protocol buffers directly, removing the need to copy each data
	 * chunk.
	 *
	 * The channel is released again at the end of this function, or by
	 * oxide_boot_sp_fail() when returning an error.
	 */
	err = kernel_ipcc_acquire();
	if (err != 0) {
		(void) kernel_ipcc_bootfail(IPCC_BOOTFAIL_NOPHASE2,
		    "failed to acquire ipcc channel, err %d", err);
		return (false);
	}

	/* Retrieve the image header */
	err = kernel_ipcc_imageblock(oxb->oxb_csum_want, 0, &data, &datal);
	if (err	!= 0) {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_NOPHASE2,
		    "failed to read phase2 header block from SP, err %d", err));
	}

	if (datal < sizeof (obsh)) {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_NOPHASE2,
		    "first block too small for disk header, got 0x%lx", datal));
	}

	bcopy(data, &obsh, sizeof (obsh));
	if (obsh.obsh_magic != OXBOOT_SP_MAGIC ||
	    obsh.obsh_version != OXBOOT_SP_VERSION ||
	    obsh.obsh_image_size > OXBOOT_MAX_IMAGE_SIZE ||
	    obsh.obsh_image_size < PAGESIZE ||
	    obsh.obsh_image_size > obsh.obsh_target_size ||
	    obsh.obsh_dataset[OXBOOT_DATASET_LEN - 1] != '\0') {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_HEADER,
		    "invalid disk header"));
	}

	if (!oxide_boot_ramdisk_set_csum(oxb, obsh.obsh_sha256,
	    OXBOOT_CSUMLEN_SHA256)) {
		char want[OXBOOT_CSUMLEN_SHA256 * 2 + 1];
		char got[OXBOOT_CSUMLEN_SHA256 * 2 + 1];

		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_INTEGRITY,
		    "checksum does not match cpio want %s got %s",
		    oxide_format_sum(want, sizeof (want), oxb->oxb_csum_want),
		    oxide_format_sum(got, sizeof (got), obsh.obsh_sha256)));
	}

	printf("received offer from SP -- "
	    "size 0x%lx data size 0x%lx dataset %s\n",
	    obsh.obsh_target_size, obsh.obsh_image_size, obsh.obsh_dataset);

	if (!oxide_boot_ramdisk_create(oxb, obsh.obsh_target_size)) {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_GENERAL,
		    "could not configure ramdisk"));
	}

	uint64_t start = gethrtime();
	size_t rem = obsh.obsh_image_size;
	size_t ipos = OXBOOT_SP_HEADER_SIZE;
	size_t opos = 0;
	uint8_t loop = 0;

	/*
	 * The phase2 transfer protocol between the host and the SP consists
	 * of the host issuing requests for a particular image, identified
	 * by the hash, at a specified offset, and the SP will return as much
	 * data as possible in response. The practical limit there is the
	 * amount of data that will fit in the UDP packets used between MGS
	 * and the SP, which means that the SP will typically return just
	 * under 1000 bytes.
	 * The simplest approach here would be to just write the received
	 * data directly to the correct offset in the ramdisk. However,
	 * although the ramdisk device accepts writes to arbitrary offsets, it
	 * does not appear to put the data where one might expect if the
	 * offsets are not aligned to DEV_BSIZE. This feels like a bug in
	 * the ramdisk driver in that it should either deal with unaligned
	 * writes properly or reject them; TBD.
	 * To work around this for now, while keeping the transfer speed as
	 * high as possible, data are accumulated and written to the ramdisk
	 * in chunks aligned to DEV_BSIZE.
	 */
	uint8_t block[DEV_BSIZE];
	size_t acc = 0;

	while (rem > 0) {
		err = kernel_ipcc_imageblock(oxb->oxb_csum_want,
		    ipos, &data, &datal);
		if (err != 0 || datal == 0) {
			/*
			 * The SP will return a data length of 0 if it is
			 * unable to retrieve the requested block from MGS.
			 */
			return (oxide_boot_sp_fail(IPCC_BOOTFAIL_GENERAL,
			    "failed to read offset 0x%lx from SP, "
			    "err=%d, len=0x%lx", ipos, err, datal));
		}

		if (datal > rem) {
			return (oxide_boot_sp_fail(IPCC_BOOTFAIL_GENERAL,
			    "too much data returned for offset 0x%lx, "
			    "len=0x%lx expected <= 0x%lx", ipos, datal, rem));
		}

		ipos += datal;
		rem -= datal;

		/* Write out any full blocks. */
		while (datal + acc >= DEV_BSIZE) {
			size_t n = DEV_BSIZE - acc;

			bcopy(data, &block[acc], n);
			datal -= n;
			data += n;

			if (!oxide_boot_sp_write_block(oxb, block, DEV_BSIZE,
			    opos)) {
				return (oxide_boot_sp_fail(
				    IPCC_BOOTFAIL_RAMDISK,
				    "failed ramdisk write at offset 0x%lx",
				    opos));
			}

			opos += DEV_BSIZE;
			acc = 0;
		}

		/* Accumulate any remaining data to prepend to next block. */
		acc = datal;
		if (acc > 0)
			bcopy(data, block, acc);

		/* Report progress periodically. */
		if (++loop == 0) {
			uint64_t secs = (gethrtime() - start) / SEC2NSEC(1);
			uint_t pct = 100UL * opos / obsh.obsh_image_size;
			uint64_t bw = 0;

			if (secs > 0)
				bw = (opos / secs) / 1024;

			printf("\r received %016lx / %016lx (%3u%%) "
			    "%luKiB/s                \r",
			    opos, obsh.obsh_image_size, pct, bw);
		}
	}

	/* Write final block. */
	if (acc > 0) {
		if (!oxide_boot_sp_write_block(oxb, block, acc, opos)) {
			return (oxide_boot_sp_fail(IPCC_BOOTFAIL_RAMDISK,
			    "failed final ramdisk write at offset 0x%lx",
			    opos));
		}
	}

	uint64_t secs = (gethrtime() - start) / SEC2NSEC(1);
	/*
	 * Print a final status message showing the total transfer time and
	 * average transfer rate. Trailing whitespace is added to completely
	 * overwrite the last periodic status message which is still on the
	 * current line.
	 */
	printf("transfer finished after %lu seconds, %luKiB/s"
	    "                        \n",
	    secs, secs > 0 ? (obsh.obsh_image_size / secs) / 1024 : 0);

	if (!oxide_boot_ramdisk_set_len(oxb, obsh.obsh_image_size) ||
	    !oxide_boot_ramdisk_set_dataset(oxb, obsh.obsh_dataset)) {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_RAMDISK,
		    "could not set ramdisk metadata"));
	}

	kernel_ipcc_release();
	return (true);
}
