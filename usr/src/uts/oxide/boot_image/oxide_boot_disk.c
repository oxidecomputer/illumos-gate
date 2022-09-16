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
#include <net/if.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/ddi.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/ddidevmap.h>
#include <sys/mac.h>
#include <sys/mac_client.h>
#include <sys/sunldi.h>
#include <sys/ramdisk.h>
#include <sys/ethernet.h>
#include <sys/byteorder.h>
#include <sys/sysmacros.h>
#include <sys/crypto/api.h>

#include "oxide_boot.h"

typedef struct jmc_find_m2 {
	char jfm_physpath[MAXPATHLEN];
} jmc_find_m2_t;

static int
jmc_find_m2(dev_info_t *dip, void *arg)
{
	jmc_find_m2_t *jfm = arg;

	if (i_ddi_devi_class(dip) == NULL ||
	    strcmp(i_ddi_devi_class(dip), ESC_DISK) != 0) {
		/*
		 * We do not think that this is a disk.
		 */
		return (DDI_WALK_CONTINUE);
	}

	if (i_ddi_attach_node_hierarchy(dip) != DDI_SUCCESS) {
		return (DDI_WALK_CONTINUE);
	}

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
		return (DDI_WALK_CONTINUE);
	}

	/*
	 * XXX We need to choose the slot number based on the BSU from the SP.
	 */
	if (slot != 17) {
		printf("    %s%d (slot %d)\n", ddi_driver_name(dip),
		    ddi_get_instance(dip), slot);
		return (DDI_WALK_CONTINUE);
	}

	/*
	 * Locate the minor for slice 0!
	 */
	for (struct ddi_minor_data *md = DEVI(dip)->devi_minor; md != NULL;
	    md = md->next) {
		if (md->ddm_spec_type != S_IFBLK ||
		    strcmp(md->ddm_name, "a") != 0) {
			continue;
		}

		if (jfm->jfm_physpath[0] == '\0') {
			(void) ddi_pathname_minor(md, jfm->jfm_physpath);
			printf("    %s (slot %d!)\n", jfm->jfm_physpath, slot);
			return (DDI_WALK_CONTINUE);
		}
	}

	return (DDI_WALK_CONTINUE);
}

#define	JMC_DISK_DATASET_SIZE		128

#define	JMC_DISK_VERSION_1		1
#define	JMC_DISK_VERSION		JMC_DISK_VERSION_1

#define	JMC_DISK_MAGIC			0x1DEB0075

/*
 * This header occupies the first 4K block in the slice.
 * XXX Should have a digest specifically for the header as well.
 */
typedef struct jmc_disk_header {
	uint32_t jdh_magic;
	uint32_t jdh_version;

	uint64_t jdh_image_size;
	uint64_t jdh_target_size;

	uint8_t jdh_sha256[OXBOOT_CSUMLEN_SHA256];

	char jdh_dataset[JMC_DISK_DATASET_SIZE];
} __packed jmc_disk_header_t;

bool
oxide_boot_disk(oxide_boot_t *oxb)
{
	bool ok = false;
	int r;
	ldi_handle_t lh = 0;
	char *buf = NULL;

	jmc_find_m2_t jfm = { 0 };

	printf("TRYING: boot disk\n");

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
	 * attached, at least for now, under the bridge for physical slot 17.
	 */
	printf("M.2 boot devices:\n");
	ddi_walk_devs(ddi_root_node(), jmc_find_m2, &jfm);
	printf("\n");

	if (jfm.jfm_physpath[0] == '\0') {
		printf("did not find any M.2 devices!\n");
		return (-1);
	}

	printf("found M.2 device @ %s\n", jfm.jfm_physpath);

	/*
	 * XXX Open the M.2 device!
	 */
	char fp[MAXPATHLEN];
	if (snprintf(fp, sizeof (fp), "/devices%s", jfm.jfm_physpath) >=
	    sizeof (fp)) {
		printf("path construction failure!\n");
		return (-1);
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

	jmc_disk_header_t jdh;
	bcopy(buf, &jdh, sizeof (jdh));

	if (jdh.jdh_magic != JMC_DISK_MAGIC ||
	    jdh.jdh_version != JMC_DISK_VERSION ||
	    jdh.jdh_image_size > jdh.jdh_target_size ||
	    jdh.jdh_dataset[JMC_DISK_DATASET_SIZE - 1] != '\0') {
		printf("invalid disk header\n");
		goto out;
	}

	if (!oxide_boot_ramdisk_set_csum(oxb, jdh.jdh_sha256,
	    OXBOOT_CSUMLEN_SHA256)) {
		printf("checksum does not match cpio\n");
		goto out;
	}

	if (!oxide_boot_ramdisk_create(oxb, jdh.jdh_target_size)) {
		printf("could not configure ramdisk\n");
		goto out;
	}

	size_t rem = jdh.jdh_image_size;
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

		iovec_t iov = {
			.iov_base = buf,
			.iov_len = sz,
		};
		if (!oxide_boot_ramdisk_write(oxb, &iov, 1, pos)) {
			printf("could not write to ramdisk\n");
			goto out;
		}

		rem -= sz;
		pos += sz;
	}

	if (!oxide_boot_ramdisk_set_len(oxb, jdh.jdh_image_size) ||
	    !oxide_boot_ramdisk_set_dataset(oxb, jdh.jdh_dataset)) {
		printf("could not set ramdisk metadata\n");
		goto out;
	}

	ok = true;

out:
	if (buf != NULL) {
		kmem_free(buf, PAGESIZE);
	}
	if (lh != 0) {
		printf("closing M.2\n");
		if ((r = ldi_close(lh, FREAD | FWRITE, kcred)) != 0) {
			printf("M.2 close failure %d\n", r);
		}
	}

	return (ok);
}
