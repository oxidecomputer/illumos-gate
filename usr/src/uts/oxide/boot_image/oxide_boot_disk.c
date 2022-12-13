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
 * Oxide Image Boot: Disk image source.  Fetches a ramdisk image from a local
 * NVMe SSD in the server sled.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/sunldi.h>
#include <sys/sysmacros.h>

#include "oxide_boot.h"

typedef struct oxide_boot_disk_find_m2 {
	int ofm_want_slot;
	char ofm_physpath[MAXPATHLEN];
} oxide_boot_disk_find_m2_t;

static int
oxide_boot_disk_find_m2(dev_info_t *dip, void *arg)
{
	oxide_boot_disk_find_m2_t *ofm = arg;

	if (i_ddi_devi_class(dip) == NULL ||
	    strcmp(i_ddi_devi_class(dip), ESC_DISK) != 0) {
		/*
		 * We do not think that this is a disk.
		 */
		return (DDI_WALK_CONTINUE);
	}

	/*
	 * The disk we are looking for is an NVMe device.  The actual block
	 * device interface is provided by "blkdev", which will be a child of
	 * the "nvme" driver, which will then be attached to a PCIe bridge with
	 * a particular physical slot number.
	 */
	dev_info_t *p;
	const char *n;
	int slot;
	if ((p = ddi_get_parent(dip)) == NULL ||
	    (n = ddi_driver_name(p)) == NULL ||
	    strcmp(n, "nvme") != 0 ||
	    (p = ddi_get_parent(p)) == NULL ||
	    (n = ddi_driver_name(p)) == NULL ||
	    strcmp(n, "pcieb") != 0 ||
	    (slot = ddi_prop_get_int(DDI_DEV_T_ANY, p, DDI_PROP_DONTPASS,
	    "physical-slot#", -1)) == -1) {
		/*
		 * This is definitely not the right device.
		 */
		return (DDI_WALK_CONTINUE);
	}

	if (slot != ofm->ofm_want_slot) {
		/*
		 * This device is the right shape, but not the specific slot we
		 * want.
		 */
		printf("    %s%d (slot %d)\n", ddi_driver_name(dip),
		    ddi_get_instance(dip), slot);
		return (DDI_WALK_CONTINUE);
	}

	/*
	 * Locate the minor for slice 0 on this disk.  The disk will have been
	 * formatted such that s0 is set aside to hold the boot image by
	 * upstack software.
	 */
	for (struct ddi_minor_data *md = DEVI(dip)->devi_minor; md != NULL;
	    md = md->next) {
		if (md->ddm_spec_type != S_IFBLK ||
		    strcmp(md->ddm_name, "a") != 0) {
			continue;
		}

		if (ofm->ofm_physpath[0] == '\0') {
			(void) ddi_pathname_minor(md, ofm->ofm_physpath);
			printf("    %s (slot %d!)\n", ofm->ofm_physpath, slot);

			/*
			 * We have found the right disk, so the walk can
			 * terminate here.
			 */
			return (DDI_WALK_TERMINATE);
		}
	}

	return (DDI_WALK_CONTINUE);
}

#define	OXBOOT_DISK_DATASET_SIZE	128

#define	OXBOOT_DISK_VERSION		2

#define	OXBOOT_DISK_MAGIC		0x1DEB0075

/*
 * This header occupies the first 4K block in the slice.  Changes to the header
 * contents require bumps to the version and coordination with other software
 * that produces or inspects images.
 *
 * XXX We should eventually have a digest specifically for the header as well.
 */

#define	ODH_FLAG_COMPRESSED		0x1

typedef struct oxide_boot_disk_header {
	uint32_t odh_magic;
	uint32_t odh_version;

	uint64_t odh_flags;
	uint64_t odh_data_size;
	uint64_t odh_image_size;
	uint64_t odh_target_size;

	uint8_t odh_sha256[OXBOOT_CSUMLEN_SHA256];

	char odh_dataset[OXBOOT_DISK_DATASET_SIZE];
} __packed oxide_boot_disk_header_t;

bool
oxide_boot_disk(oxide_boot_t *oxb, int slot)
{
	bool ok = false;
	int r;
	ldi_handle_t lh = NULL;
	uint8_t *buf = NULL;

	oxide_boot_disk_find_m2_t ofm = { .ofm_want_slot = slot };

	printf("TRYING: boot disk (slot %d)\n", slot);

	/*
	 * First, force everything which can attach to do so.  The device class
	 * is not derived until at least one minor mode is created, so we
	 * cannot walk the device tree looking for a device class of
	 * ESC_DISK until everything is attached.
	 */
	printf("attaching stuff...\n");
	(void) ndi_devi_config(ddi_root_node(), NDI_CONFIG | NDI_DEVI_PERSIST |
	    NDI_NO_EVENT | NDI_DRV_CONF_REPROBE);

	/*
	 * We need to find the M.2 device that we want to boot.  It will be
	 * attached under the bridge for the physical slot specified by the
	 * caller.
	 */
	printf("NVMe boot devices:\n");
	ddi_walk_devs(ddi_root_node(), oxide_boot_disk_find_m2, &ofm);
	printf("\n");

	if (ofm.ofm_physpath[0] == '\0') {
		printf("did not find the M.2 device in slot %d!\n", slot);
		return (false);
	}

	printf("found M.2 device (slot %d), @ %s\n", slot, ofm.ofm_physpath);

	/*
	 * Open the M.2 device:
	 */
	char fp[MAXPATHLEN];
	if (snprintf(fp, sizeof (fp), "/devices%s", ofm.ofm_physpath) >=
	    sizeof (fp)) {
		printf("path construction failure!\n");
		return (false);
	}

	printf("opening M.2 device\n");
	if ((r = ldi_open_by_name(fp, FREAD, kcred, &lh, oxb->oxb_li)) != 0) {
		printf("M.2 open failure\n");
		goto out;
	}

	buf = kmem_zalloc(PAGESIZE, KM_SLEEP);

	if (!oxide_boot_disk_read(lh, 0, buf, PAGESIZE)) {
		printf("could not read header from disk\n");
		goto out;
	}

	oxide_boot_disk_header_t odh;
	bcopy(buf, &odh, sizeof (odh));

	if (odh.odh_magic != OXBOOT_DISK_MAGIC ||
	    odh.odh_version != OXBOOT_DISK_VERSION ||
	    odh.odh_image_size > OXBOOT_MAX_IMAGE_SIZE ||
	    odh.odh_image_size < PAGESIZE ||
	    odh.odh_image_size > odh.odh_target_size ||
	    odh.odh_dataset[OXBOOT_DISK_DATASET_SIZE - 1] != '\0') {
		printf("invalid disk header\n");
		goto out;
	}

	if (!oxide_boot_ramdisk_set_csum(oxb, odh.odh_sha256,
	    OXBOOT_CSUMLEN_SHA256)) {
		printf("checksum does not match cpio\n");
		goto out;
	}

	if (!oxide_boot_ramdisk_create(oxb, odh.odh_target_size)) {
		printf("could not configure ramdisk\n");
		goto out;
	}

	if ((odh.odh_flags & ODH_FLAG_COMPRESSED) != 0) {
		if (!oxide_boot_set_compressed(oxb)) {
			printf("could not initialise decompression");
			goto out;
		}
	}

	size_t rem = odh.odh_data_size;
	size_t pos = 0;
	for (;;) {
		size_t sz = MIN(PAGESIZE, rem);
		if (sz == 0) {
			break;
		}

		if (!oxide_boot_disk_read(lh, PAGESIZE + pos, buf, PAGESIZE)) {
			printf("could not read from disk\n");
			goto out;
		}


		if (!oxide_boot_ramdisk_write_append(oxb, buf, sz)) {
			printf("could not write to ramdisk\n");
			goto out;
		}

		rem -= sz;
		pos += sz;
	}

	if (!oxide_boot_ramdisk_write_flush(oxb) ||
	    !oxide_boot_ramdisk_set_len(oxb, odh.odh_image_size) ||
	    !oxide_boot_ramdisk_set_dataset(oxb, odh.odh_dataset)) {
		printf("could not set ramdisk metadata\n");
		goto out;
	}

	ok = true;

out:
	if (buf != NULL) {
		kmem_free(buf, PAGESIZE);
	}
	if (lh != NULL) {
		printf("closing M.2\n");
		if ((r = ldi_close(lh, FREAD | FWRITE, kcred)) != 0) {
			printf("M.2 close failure %d\n", r);
		}
	}

	return (ok);
}
