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
 * Copyright 2023 Oxide Computer Company
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
#include <sys/dumphdr.h>
#include <sys/dumpadm.h>
#include <sys/sysmacros.h>

#include "oxide_boot.h"

typedef struct oxide_boot_disk_find_m2 {
	int ofm_want_slot;
	uint_t ofm_want_slice;
	char ofm_physpath[MAXPATHLEN];
} oxide_boot_disk_find_m2_t;

static int
oxide_boot_disk_find_m2(dev_info_t *dip, void *arg)
{
	oxide_boot_disk_find_m2_t *ofm = arg;
	char slicec;

	/*
	 * Slices 0 to 6 use 'a' to 'f' for their minor name - beyond that
	 * things are more complicated.
	 */
	VERIFY3U(ofm->ofm_want_slice, <=, 6);
	slicec = ofm->ofm_want_slice + 'a';

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
		oxide_boot_debug("    %s%d (slot %d)", ddi_driver_name(dip),
		    ddi_get_instance(dip), slot);
		return (DDI_WALK_CONTINUE);
	}

	/*
	 * Locate the minor for the requested slice on this disk.
	 */
	for (struct ddi_minor_data *md = DEVI(dip)->devi_minor; md != NULL;
	    md = md->next) {
		if (md->ddm_spec_type != S_IFBLK ||
		    md->ddm_name[0] != slicec || md->ddm_name[1] != '\0') {
			continue;
		}

		if (ofm->ofm_physpath[0] == '\0') {
			(void) ddi_pathname_minor(md, ofm->ofm_physpath);
			oxide_boot_debug("    %s (slot %d!)",
			    ofm->ofm_physpath, slot);

			/*
			 * We have found the right disk and slice, so the walk
			 * can terminate here.
			 */
			return (DDI_WALK_TERMINATE);
		}
	}

	return (DDI_WALK_CONTINUE);
}

#define	OXBOOT_DISK_DATASET_SIZE	128
#define	OXBOOT_DISK_IMAGENAME_SIZE	128

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
	char odh_imagename[OXBOOT_DISK_IMAGENAME_SIZE];
} __packed oxide_boot_disk_header_t;

static bool
oxide_boot_disk_slice(oxide_boot_t *oxb, int slot, uint_t slice)
{
	oxide_boot_disk_find_m2_t *ofm;
	char *fp = NULL;
	bool ok = false;
	int r;
	ldi_handle_t lh = NULL;
	uint8_t *buf = NULL;

	oxide_boot_note("TRYING: boot disk (slot %d, slice %u)", slot, slice);

	ofm = kmem_zalloc(sizeof (*ofm), KM_SLEEP);
	ofm->ofm_want_slot = slot;
	ofm->ofm_want_slice = slice;

	/*
	 * We need to find the M.2 device that we want to boot.  It will be
	 * attached under the bridge for the physical slot specified by the
	 * caller.
	 */
	oxide_boot_debug("NVMe boot devices:");
	ddi_walk_devs(ddi_root_node(), oxide_boot_disk_find_m2, ofm);

	if (ofm->ofm_physpath[0] == '\0') {
		oxide_boot_warn("did not find the M.2 device in slot %d!",
		    slot);
		goto out;
	}

	oxide_boot_note("found M.2 device (slot %d, slice %u), @ %s",
	    slot, slice, ofm->ofm_physpath);

	/*
	 * Open the M.2 device:
	 */
	fp = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (snprintf(fp, MAXPATHLEN, "/devices%s", ofm->ofm_physpath) >=
	    MAXPATHLEN) {
		oxide_boot_warn("path construction failure!");
		goto out;
	}

	oxide_boot_debug("opening M.2 device");
	if ((r = ldi_open_by_name(fp, FREAD, kcred, &lh, oxb->oxb_li)) != 0) {
		oxide_boot_warn("M.2 open failure");
		goto out;
	}

	buf = kmem_zalloc(PAGESIZE, KM_SLEEP);

	if (!oxide_boot_disk_read(lh, 0, buf, PAGESIZE)) {
		oxide_boot_warn("could not read header from disk");
		goto out;
	}

	oxide_boot_disk_header_t odh;
	bcopy(buf, &odh, sizeof (odh));

	if (odh.odh_magic != OXBOOT_DISK_MAGIC ||
	    odh.odh_version != OXBOOT_DISK_VERSION ||
	    odh.odh_image_size > OXBOOT_MAX_IMAGE_SIZE ||
	    odh.odh_image_size < PAGESIZE ||
	    odh.odh_image_size > odh.odh_target_size ||
	    odh.odh_dataset[OXBOOT_DISK_DATASET_SIZE - 1] != '\0' ||
	    odh.odh_imagename[OXBOOT_DISK_IMAGENAME_SIZE - 1] != '\0') {
		oxide_boot_warn("invalid disk header");
		goto out;
	}

	if (!oxide_boot_ramdisk_set_csum(oxb, odh.odh_sha256,
	    OXBOOT_CSUMLEN_SHA256)) {
		oxide_boot_warn("checksum does not match phase1");
		goto out;
	}

	oxide_boot_note("attempting boot from image name '%s'",
	    *odh.odh_imagename == '\0' ? "<none>" : odh.odh_imagename);

	if (!oxide_boot_ramdisk_create(oxb, odh.odh_target_size)) {
		oxide_boot_warn("could not configure ramdisk");
		goto out;
	}

	if ((odh.odh_flags & ODH_FLAG_COMPRESSED) != 0) {
		if (!oxide_boot_set_compressed(oxb)) {
			oxide_boot_warn("could not initialise decompression");
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
			oxide_boot_warn("could not read from disk");
			goto out;
		}


		if (!oxide_boot_ramdisk_write_append(oxb, buf, sz)) {
			oxide_boot_warn("could not write to ramdisk");
			goto out;
		}

		rem -= sz;
		pos += sz;
	}

	if (!oxide_boot_ramdisk_write_flush(oxb) ||
	    !oxide_boot_ramdisk_set_len(oxb, odh.odh_image_size) ||
	    !oxide_boot_ramdisk_set_dataset(oxb, odh.odh_dataset)) {
		oxide_boot_warn("could not set ramdisk metadata");
		goto out;
	}

	ok = true;

	if (*odh.odh_imagename != '\0') {
		(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
		    ddi_root_node(), OXBOOT_DEVPROP_IMAGE_NAME,
		    odh.odh_imagename);
	}

out:
	kmem_free(ofm, sizeof (*ofm));
	if (fp != NULL) {
		kmem_free(fp, MAXPATHLEN);
	}
	if (buf != NULL) {
		kmem_free(buf, PAGESIZE);
	}
	if (lh != NULL) {
		oxide_boot_debug("closing M.2");
		if ((r = ldi_close(lh, FREAD | FWRITE, kcred)) != 0) {
			oxide_boot_warn("M.2 close failure %d", r);
		}
	}

	return (ok);
}

static void
oxide_boot_disk_dump(oxide_boot_t *oxb, int slot, uint_t slice)
{
	oxide_boot_disk_find_m2_t *ofm;
	char *dumpdev = NULL;
	vnode_t *vp;
	int ret;

	oxide_boot_note("SEEKING: dump device (slot %d, slice %u)",
	    slot, slice);

	ofm = kmem_zalloc(sizeof (*ofm), KM_SLEEP);
	ofm->ofm_want_slot = slot;
	ofm->ofm_want_slice = slice;

	ddi_walk_devs(ddi_root_node(), oxide_boot_disk_find_m2, ofm);

	if (ofm->ofm_physpath[0] == '\0') {
		oxide_boot_warn("did not find a dump device in slot %d!",
		    slot);
		goto out;
	}

	oxide_boot_note("found dump device (slot %d, slice %u), @ %s",
	    slot, slice, ofm->ofm_physpath);

	dumpdev = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (snprintf(dumpdev, MAXPATHLEN, "/devices%s", ofm->ofm_physpath) >=
	    MAXPATHLEN) {
		oxide_boot_warn("dump device path construction failure!");
		goto out;
	}

	if ((ret = ldi_vp_from_name(dumpdev, &vp)) != 0) {
		oxide_boot_warn("dump device vnode lookup failure, errno %d",
		    ret);
		goto out;
	}

	mutex_enter(&dump_lock);

	if ((ret = dumpinit(vp, dumpdev, 0)) != 0) {
		oxide_boot_warn("dump device setup failure, errno %d", ret);
	} else {
		oxide_boot_note("successfully configured dump device");
		dump_conflags = DUMP_CURPROC;
	}

	mutex_exit(&dump_lock);

	VN_RELE(vp);

out:
	kmem_free(ofm, sizeof (*ofm));
	if (dumpdev != NULL)
		kmem_free(dumpdev, MAXPATHLEN);
}

bool
oxide_boot_disk(oxide_boot_t *oxb, int slot)
{
	/*
	 * First, force everything which can attach to do so.  The device class
	 * is not derived until at least one minor mode is created, so we
	 * cannot walk the device tree looking for a device class of
	 * ESC_DISK until everything is attached.
	 */
	oxide_boot_debug("attaching stuff...");
	(void) ndi_devi_config(ddi_root_node(), NDI_CONFIG | NDI_DEVI_PERSIST |
	    NDI_NO_EVENT | NDI_DRV_CONF_REPROBE);

	/*
	 * The disk will have been formatted by upstack software such that
	 * slices 0 and 1 are set aside to hold boot images. We try these
	 * slices in order to try to find the image we want.
	 */
	for (uint_t slice = OXBOOT_SLICE_MIN; slice <= OXBOOT_SLICE_MAX;
	    slice++) {
		if (oxide_boot_disk_slice(oxb, slot, slice)) {
			(void) e_ddi_prop_update_int(DDI_DEV_T_NONE,
			    ddi_root_node(), OXBOOT_DEVPROP_DISK_SLICE, slice);
			/*
			 * Attempt to set up a dump device on the selected
			 * boot disk now.
			 */
			oxide_boot_disk_dump(oxb, slot, OXBOOT_SLICE_DUMP);
			return (true);
		}
	}

	return (false);
}
