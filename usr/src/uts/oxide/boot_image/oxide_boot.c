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
 * Oxide Image Boot.  Fetches a ramdisk image from various sources and
 * configures the system to boot from it.
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
#include <sys/kobj.h>
#include <sys/boot_image_ops.h>

#include "oxide_boot.h"

/*
 * Linkage structures
 */
static struct modlmisc oxide_boot_modlmisc = {
	.misc_modops =				&mod_miscops,
	.misc_linkinfo =			"boot_image",
};

static struct modlinkage oxide_boot_modlinkage = {
	.ml_rev =				MODREV_1,
	.ml_linkage =				{ &oxide_boot_modlmisc, NULL },
};

int oxide_boot_allow_unload = 0;

int
_init(void)
{
	return (mod_install(&oxide_boot_modlinkage));
}

int
_fini(void)
{
	if (oxide_boot_allow_unload == 0) {
		return (EBUSY);
	}

	return (mod_remove(&oxide_boot_modlinkage));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&oxide_boot_modlinkage, mi));
}

static void
oxide_dump_sum(const char *name, const uint8_t *sum)
{
	printf("    %s: "
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x\n",
	    name,
	    sum[0], sum[1], sum[2], sum[3],
	    sum[4], sum[5], sum[6], sum[7],
	    sum[8], sum[9], sum[10], sum[11],
	    sum[12], sum[13], sum[14], sum[15],
	    sum[16], sum[17], sum[18], sum[19],
	    sum[20], sum[21], sum[22], sum[23],
	    sum[24], sum[25], sum[26], sum[27],
	    sum[28], sum[29], sum[30], sum[31]);
}

bool
oxide_boot_ramdisk_create(oxide_boot_t *oxb, uint64_t size)
{
	int r;
	bool ok = false;
	ldi_handle_t ctlh = 0;

	/*
	 * Round the size up to be a whole number of pages.
	 */
	size = P2ROUNDUP(size, PAGESIZE);

	mutex_enter(&oxb->oxb_mutex);
	if (oxb->oxb_rd_disk != 0) {
		goto bail;
	}

	printf("opening ramdisk control device\n");
	if ((r = ldi_open_by_name("/devices/pseudo/ramdisk@1024:ctl",
	    FEXCL | FREAD | FWRITE, kcred, &ctlh, oxb->oxb_li)) != 0) {
		printf("control device open failure %d\n", r);
		goto bail;
	}

	int rv;
	struct rd_ioctl ri;
	bzero(&ri, sizeof (ri));
	(void) snprintf(ri.ri_name, sizeof (ri.ri_name), OXBOOT_RAMDISK_NAME);
	ri.ri_size = size;

	printf("creating ramdisk of size %lu\n", size);
	if ((r = ldi_ioctl(ctlh, RD_CREATE_DISK, (intptr_t)&ri,
	    FWRITE | FKIOCTL, kcred, &rv)) != 0) {
		printf("ramdisk create failure %d\n", r);
		goto bail;
	}

	oxb->oxb_ramdisk_path = kmem_asprintf(
	    "/devices/pseudo/ramdisk@1024:%s", OXBOOT_RAMDISK_NAME);
	oxb->oxb_ramdisk_size = size;
	oxb->oxb_ramdisk_data_size = 0;

	printf("opening ramdisk device: %s\n", oxb->oxb_ramdisk_path);
	if ((r = ldi_open_by_name(oxb->oxb_ramdisk_path, FREAD | FWRITE,
	    kcred, &oxb->oxb_rd_disk, oxb->oxb_li)) != 0) {
		printf("ramdisk open failure %d\n", r);
		goto bail;
	}

	ok = true;

bail:
	if (ctlh != 0) {
		(void) ldi_close(ctlh, FEXCL | FREAD | FWRITE, kcred);
	}
	mutex_exit(&oxb->oxb_mutex);
	return (ok);
}

bool
oxide_boot_ramdisk_write(oxide_boot_t *oxb, iovec_t *iov, uint_t niov,
    uint64_t offset)
{
	size_t len = 0;
	for (uint_t i = 0; i < niov; i++) {
		len += iov[i].iov_len;
	}

	/*
	 * Record the extent of the written data so that we can confirm the
	 * image was not larger than its stated size.
	 */
	mutex_enter(&oxb->oxb_mutex);
	oxb->oxb_ramdisk_data_size =
	    MAX(oxb->oxb_ramdisk_data_size, offset + len);
	mutex_exit(&oxb->oxb_mutex);

	/*
	 * Write the data to the ramdisk.
	 */
	uio_t uio = {
		.uio_iovcnt = niov,
		.uio_iov = iov,
		.uio_loffset = offset,
		.uio_segflg = UIO_SYSSPACE,
		.uio_resid = len,
	};

	int r;
	if ((r = ldi_write(oxb->oxb_rd_disk, &uio, kcred)) != 0) {
		printf("write to ramdisk (offset %lu size %lu) failed %d\n",
		    offset, len, r);
		return (false);
	}

	if (uio.uio_resid != 0) {
		printf("write to ramdisk (offset %lu) was short\n", offset);
		return (false);
	}

	return (true);
}

bool
oxide_boot_ramdisk_set_dataset(oxide_boot_t *oxb, const char *name)
{
	mutex_enter(&oxb->oxb_mutex);
	if (oxb->oxb_ramdisk_dataset != NULL) {
		strfree(oxb->oxb_ramdisk_dataset);
	}
	oxb->oxb_ramdisk_dataset = strdup(name);
	mutex_exit(&oxb->oxb_mutex);
	return (true);
}

bool
oxide_boot_ramdisk_set_len(oxide_boot_t *oxb, uint64_t len)
{
	mutex_enter(&oxb->oxb_mutex);
	if (len < oxb->oxb_ramdisk_data_size) {
		printf("image size %lu < written size %lu\n",
		    len, oxb->oxb_ramdisk_data_size);
		mutex_exit(&oxb->oxb_mutex);
		return (false);
	}

	oxb->oxb_ramdisk_data_size = len;
	mutex_exit(&oxb->oxb_mutex);
	return (true);
}

bool
oxide_boot_ramdisk_set_csum(oxide_boot_t *oxb, uint8_t *csum, size_t len)
{
	if (len != OXBOOT_CSUMLEN_SHA256) {
		return (false);
	}

	oxide_dump_sum("in image", csum);

	mutex_enter(&oxb->oxb_mutex);
	int c = bcmp(csum, oxb->oxb_csum_want, MIN(len, OXBOOT_CSUMLEN_SHA256));
	mutex_exit(&oxb->oxb_mutex);

	return (c == 0);
}

bool
oxide_boot_disk_read(ldi_handle_t lh, uint64_t offset, char *buf, size_t len)
{
	iovec_t iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	uio_t uio = {
		.uio_iov = &iov,
		.uio_iovcnt = 1,
		.uio_loffset = offset,
		.uio_segflg = UIO_SYSSPACE,
		.uio_resid = len,
	};

	int r;
	if ((r = ldi_read(lh, &uio, kcred)) != 0) {
		printf("read from disk (offset %lu size %lu) failed %d\n",
		    offset, len, r);
		return (false);
	}

	if (uio.uio_resid != 0) {
		printf("read from disk (offset %lu) was short\n", offset);
		return (false);
	}

	return (true);
}

static bool
oxide_boot_ramdisk_check(oxide_boot_t *oxb)
{
	bool ok = false;
	int r;

	if (oxb->oxb_rd_disk == 0) {
		return (false);
	}

	bzero(&oxb->oxb_mechanism, sizeof (oxb->oxb_mechanism));
	if ((oxb->oxb_mechanism.cm_type = crypto_mech2id(SUN_CKM_SHA256)) ==
	    CRYPTO_MECH_INVALID) {
		return (false);
	}

	if ((r = crypto_digest_init(&oxb->oxb_mechanism, &oxb->oxb_crypto,
	    NULL)) != CRYPTO_SUCCESS) {
		printf("crypto_digest_init() failed %d\n", r);
	}

	char *buf = kmem_alloc(PAGESIZE, KM_SLEEP);
	size_t rem = oxb->oxb_ramdisk_data_size;
	size_t pos = 0;
	for (;;) {
		size_t sz = MIN(rem, PAGESIZE);
		if (sz == 0) {
			break;
		}

		if (!oxide_boot_disk_read(oxb->oxb_rd_disk, pos, buf, sz)) {
			printf("ramdisk read failed\n");
			goto bail;
		}

		crypto_data_t cd = {
			.cd_format = CRYPTO_DATA_RAW,
			.cd_length = sz,
			.cd_raw = {
				.iov_base = buf,
				.iov_len = sz,
			},
		};
		if (crypto_digest_update(oxb->oxb_crypto, &cd, 0) !=
		    CRYPTO_SUCCESS) {
			goto bail;
		}

		rem -= sz;
		pos += sz;
	}

	crypto_data_t cd = {
		.cd_format = CRYPTO_DATA_RAW,
		.cd_length = OXBOOT_CSUMLEN_SHA256,
		.cd_raw = {
			.iov_base = (void *)oxb->oxb_csum_have,
			.iov_len = OXBOOT_CSUMLEN_SHA256,
		},
	};
	if (crypto_digest_final(oxb->oxb_crypto, &cd, 0) != CRYPTO_SUCCESS) {
		goto bail;
	}

	if (bcmp(oxb->oxb_csum_want, oxb->oxb_csum_have,
	    OXBOOT_CSUMLEN_SHA256) != 0) {
		printf("checksum mismatch\n");
		oxide_dump_sum("want", oxb->oxb_csum_want);
		oxide_dump_sum("have", oxb->oxb_csum_have);
		goto bail;
	}

	printf("checksum ok!\n");
	ok = true;

bail:
	if (!ok) {
		crypto_cancel_ctx(oxb->oxb_crypto);
	}
	return (ok);
}

void
oxide_boot_fini(oxide_boot_t *oxb)
{
	if (oxb->oxb_ramdisk_path != NULL) {
		strfree(oxb->oxb_ramdisk_path);
	}
	if (oxb->oxb_ramdisk_dataset != NULL) {
		strfree(oxb->oxb_ramdisk_dataset);
	}
	ldi_ident_release(oxb->oxb_li);
	mutex_destroy(&oxb->oxb_mutex);
	kmem_free(oxb, sizeof (*oxb));
}

static void
oxide_boot_locate(void)
{
	int r = -1;

	printf("in oxide_boot!\n");

	oxide_boot_t *oxb = kmem_zalloc(sizeof (*oxb), KM_SLEEP);
	mutex_init(&oxb->oxb_mutex, NULL, MUTEX_DRIVER, NULL);
	if (ldi_ident_from_mod(&oxide_boot_modlinkage, &oxb->oxb_li) != 0) {
		panic("could not get LDI identity");
	}

	/*
	 * Load the hash of the ramdisk that matches the bits in the cpio
	 * archive.
	 */
	intptr_t fd;
	uint8_t csum[OXBOOT_CSUMLEN_SHA256];
	if ((fd = kobj_open("/boot_image_csum")) == -1 ||
	    kobj_read(fd, (int8_t *)&oxb->oxb_csum_want,
	    OXBOOT_CSUMLEN_SHA256, 0) != OXBOOT_CSUMLEN_SHA256) {
		panic("could not read /boot_image_csum");
	}
	(void) kobj_close(fd);
	oxide_dump_sum("cpio wants", oxb->oxb_csum_want);

	/*
	 * XXX We need to pick the source based on an interaction with the SP.
	 */
	if (oxide_boot_disk(oxb)) {
		(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
		    ddi_root_node(), "oxide-boot-source", "disk");
	} else if (oxide_boot_net(oxb)) {
		(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
		    ddi_root_node(), "oxide-boot-source", "net");
	} else {
		panic("no source was able to locate a boot image");
	}

	printf("ramdisk data size = %lu\n", oxb->oxb_ramdisk_data_size);
	if (oxb->oxb_ramdisk_dataset == NULL) {
		panic("missing dataset name");
	}

	if (!oxide_boot_ramdisk_check(oxb)) {
		panic("boot image integrity failure");
	}

	/*
	 * Tell the system to import the ramdisk device as a ZFS pool, and to
	 * ignore any device names or IDs found in the pool label.
	 */
	r = 0;
	(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
	    ddi_root_node(), "fstype", "zfs");
	(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
	    ddi_root_node(), "zfs-bootfs-name",
	    oxb->oxb_ramdisk_dataset);
	(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
	    ddi_root_node(), "zfs-ramdisk-path",
	    oxb->oxb_ramdisk_path);

	oxide_boot_fini(oxb);
}

boot_image_ops_t _boot_image_ops = {
	.bimo_version =				BOOT_IMAGE_OPS_VERSION,
	.bimo_locate =				oxide_boot_locate,
};
