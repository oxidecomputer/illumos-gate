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
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/va_list.h>
#include <sys/kernel_ipcc.h>

#include "oxide_boot.h"
#include "oxide_boot_sp.h"

/*
 * The image hash is used as part of the protocol for retrieving image
 * fragments from the SP. If the checksum algorithm used for phase 2 images
 * changes, then protocol changes will also be necessary. Check that the
 * hash length used in the protocol matches.
 */
CTASSERT(IPCC_IMAGE_HASHLEN == OXBOOT_CSUMLEN_SHA256);

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
	oxide_boot_vwarn(fmt, va);
	va_end(va);
	va_start(va, fmt);
	(void) kernel_ipcc_bootfailv(reason, fmt, va);
	va_end(va);

	return (false);
}

bool
oxide_boot_sp(oxide_boot_t *oxb)
{
	oxide_boot_sp_header_t obsh = { 0 };
	uint8_t *data;
	size_t datal;
	int err;

	oxide_boot_note("TRYING: boot sp");

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
	err = kernel_ipcc_acquire(IPCC_CHAN_QUIET);
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
	    obsh.obsh_dataset[OXBOOT_SP_DATASET_LEN - 1] != '\0' ||
	    obsh.obsh_imagename[OXBOOT_SP_IMAGENAME_LEN - 1] != '\0') {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_HEADER,
		    "invalid disk header"));
	}

	if (!oxide_boot_ramdisk_set_csum(oxb, obsh.obsh_sha256,
	    OXBOOT_CSUMLEN_SHA256)) {
		char want[OXBOOT_CSUMLEN_SHA256 * 2 + 1];
		char got[OXBOOT_CSUMLEN_SHA256 * 2 + 1];

		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_INTEGRITY,
		    "checksum does not match phase1, want %s got %s",
		    oxide_format_sum(want, sizeof (want), oxb->oxb_csum_want),
		    oxide_format_sum(got, sizeof (got), obsh.obsh_sha256)));
	}

	oxide_boot_note("received offer from SP -- ");
	oxide_boot_note("    v%u flags 0x%lx",
	    obsh.obsh_version, obsh.obsh_flags);
	oxide_boot_note(
	    "    data size 0x%lx image size 0x%lx target size 0x%lx",
	    obsh.obsh_data_size, obsh.obsh_image_size, obsh.obsh_target_size);
	oxide_boot_note("    dataset %s", obsh.obsh_dataset);
	oxide_boot_note(" image name %s",
	    *obsh.obsh_imagename == '\0' ? "<none>" : obsh.obsh_imagename);

	if (!oxide_boot_ramdisk_create(oxb, obsh.obsh_target_size)) {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_GENERAL,
		    "could not configure ramdisk"));
	}

	if ((obsh.obsh_flags & OBSH_FLAG_COMPRESSED) != 0) {
		if (!oxide_boot_set_compressed(oxb)) {
			return (oxide_boot_sp_fail(IPCC_BOOTFAIL_GENERAL,
			    "could not initialise decompression"));
		}
	}

	uint64_t start = gethrtime();
	size_t rem = obsh.obsh_data_size;
	size_t ipos = OXBOOT_SP_HEADER_SIZE;
	uint8_t loop = 0;

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

		if (!oxide_boot_ramdisk_write_append(oxb, data, datal)) {
			return (oxide_boot_sp_fail(IPCC_BOOTFAIL_RAMDISK,
			    "failed ramdisk write at offset 0x%lx",
			    oxb->oxb_opos));
		}

		/* Report progress periodically. */
		if (++loop == 0) {
			uint64_t secs = (gethrtime() - start) / SEC2NSEC(1);
			uint_t pct = 100UL * ipos / obsh.obsh_data_size;
			uint64_t bw = 0;

			if (secs > 0)
				bw = (ipos / secs) / 1024;

			printf("\r received %016lx / %016lx (%3u%%) "
			    "%luKiB/s                \r",
			    ipos, obsh.obsh_data_size, pct, bw);
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
	    secs, secs > 0 ? (obsh.obsh_data_size / secs) / 1024 : 0);

	if (!oxide_boot_ramdisk_write_flush(oxb) ||
	    !oxide_boot_ramdisk_set_len(oxb, obsh.obsh_image_size) ||
	    !oxide_boot_ramdisk_set_dataset(oxb, obsh.obsh_dataset)) {
		return (oxide_boot_sp_fail(IPCC_BOOTFAIL_RAMDISK,
		    "could not set ramdisk metadata"));
	}

	if (*obsh.obsh_imagename != '\0') {
		(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
		    ddi_root_node(), OXBOOT_DEVPROP_IMAGE_NAME,
		    obsh.obsh_imagename);
	}

	kernel_ipcc_release();
	return (true);
}
