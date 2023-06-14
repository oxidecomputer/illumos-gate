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
#include <sys/va_list.h>
#include <sys/boot_data.h>
#include <sys/kernel_ipcc.h>
#include <sys/boot_image_ops.h>

#include "oxide_boot.h"
#include "zen_umc.h"

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

int
_init(void)
{
	return (mod_install(&oxide_boot_modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&oxide_boot_modlinkage));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&oxide_boot_modlinkage, mi));
}

char *
oxide_format_sum(char *buf, size_t buflen, const uint8_t *sum)
{
	(void) snprintf(buf, buflen,
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x",
	    sum[0], sum[1], sum[2], sum[3],
	    sum[4], sum[5], sum[6], sum[7],
	    sum[8], sum[9], sum[10], sum[11],
	    sum[12], sum[13], sum[14], sum[15],
	    sum[16], sum[17], sum[18], sum[19],
	    sum[20], sum[21], sum[22], sum[23],
	    sum[24], sum[25], sum[26], sum[27],
	    sum[28], sum[29], sum[30], sum[31]);

	return (buf);
}

static void
oxide_dump_sum(const char *name, const uint8_t *sum)
{
	char buf[OXBOOT_CSUMBUF_SHA256];

	oxide_boot_note("    %s: %s", name,
	    oxide_format_sum(buf, sizeof (buf), sum));
}

bool
oxide_boot_ramdisk_create(oxide_boot_t *oxb, uint64_t size)
{
	int r;
	bool ok = false;
	ldi_handle_t ctlh = NULL;

	/*
	 * Round the size up to be a whole number of pages.
	 */
	size = P2ROUNDUP(size, PAGESIZE);

	mutex_enter(&oxb->oxb_mutex);
	if (oxb->oxb_rd_disk != 0) {
		goto bail;
	}

	oxide_boot_debug("opening ramdisk control device");
	if ((r = ldi_open_by_name("/devices/pseudo/ramdisk@1024:ctl",
	    FEXCL | FREAD | FWRITE, kcred, &ctlh, oxb->oxb_li)) != 0) {
		oxide_boot_warn("control device open failure %d", r);
		goto bail;
	}

	struct rd_ioctl ri;
	bzero(&ri, sizeof (ri));
	(void) snprintf(ri.ri_name, sizeof (ri.ri_name), OXBOOT_RAMDISK_NAME);
	ri.ri_size = size;

	oxide_boot_debug("creating ramdisk of size %lu", size);
	if ((r = ldi_ioctl(ctlh, RD_CREATE_DISK, (intptr_t)&ri,
	    FWRITE | FKIOCTL, kcred, NULL)) != 0) {
		oxide_boot_warn("ramdisk create failure %d", r);
		goto bail;
	}

	oxb->oxb_ramdisk_path = kmem_asprintf(
	    "/devices/pseudo/ramdisk@1024:%s", OXBOOT_RAMDISK_NAME);
	oxb->oxb_ramdisk_size = size;
	oxb->oxb_ramdisk_data_size = 0;

	oxide_boot_debug("opening ramdisk device: %s", oxb->oxb_ramdisk_path);
	if ((r = ldi_open_by_name(oxb->oxb_ramdisk_path, FREAD | FWRITE,
	    kcred, &oxb->oxb_rd_disk, oxb->oxb_li)) != 0) {
		oxide_boot_warn("ramdisk open failure %d", r);
		goto bail;
	}

	ok = true;

bail:
	if (ctlh != NULL) {
		VERIFY0(ldi_close(ctlh, FEXCL | FREAD | FWRITE, kcred));
	}
	mutex_exit(&oxb->oxb_mutex);
	return (ok);
}

static bool
oxide_boot_write_iov(oxide_boot_t *oxb, iovec_t *iov, uint_t niov,
    uint64_t offset)
{
	ASSERT(MUTEX_HELD(&oxb->oxb_mutex));

	size_t len = 0;
	for (uint_t i = 0; i < niov; i++) {
		if (iov[i].iov_len >= SIZE_MAX - len) {
			/*
			 * This would overflow.
			 */
			oxide_boot_warn(
			    "write to ramdisk (offset %lu) iovec too large",
			    offset);
			return (false);
		}
		len += iov[i].iov_len;
	}

	/*
	 * Record the extent of the written data so that we can confirm the
	 * image was not larger than its stated size.
	 */
	oxb->oxb_ramdisk_data_size =
	    MAX(oxb->oxb_ramdisk_data_size, offset + len);

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
		oxide_boot_warn(
		    "write to ramdisk (offset %lu size %lu) failed %d",
		    offset, len, r);
		return (false);
	}

	if (uio.uio_resid != 0) {
		oxide_boot_warn("write to ramdisk (offset %lu) was short",
		    offset);
		return (false);
	}

	return (true);
}


static bool
oxide_boot_write_block(oxide_boot_t *oxb, uint8_t *block, size_t len,
    size_t offset)
{
	ASSERT(MUTEX_HELD(&oxb->oxb_mutex));

	const uint_t niov = 1;
	iovec_t iov = {
		.iov_base = (caddr_t)block,
		.iov_len = len,
	};

	return (oxide_boot_write_iov(oxb, &iov, niov, offset));
}

static bool
oxide_boot_ramdisk_append_cb(void *arg, uint8_t *buf, size_t len)
{
	oxide_boot_t *oxb = arg;

	ASSERT(MUTEX_HELD(&oxb->oxb_mutex));

	/* Write out any full blocks. */
	while (len + oxb->oxb_acc >= DEV_BSIZE) {
		size_t n = DEV_BSIZE - oxb->oxb_acc;

		bcopy(buf, &oxb->oxb_block[oxb->oxb_acc], n);

		len -= n;
		buf += n;

		if (!oxide_boot_write_block(oxb, oxb->oxb_block, DEV_BSIZE,
		    oxb->oxb_opos)) {
			return (false);
		}

		oxb->oxb_opos += DEV_BSIZE;
		oxb->oxb_acc = 0;
	}

	/*
	 * Accumulate any remaining data so that it can be prepended to the
	 * next block.
	 */
	if (len > 0) {
		bcopy(buf, &oxb->oxb_block[oxb->oxb_acc], len);
		oxb->oxb_acc += len;
	}

	return (true);
}

/*
 * Write data to the ramdisk image at a specific offset. This is used for
 * writing data which may be out of order, such as that received via the
 * network boot protocol. It is not suitable for compressed streams as blocks
 * expand to different sizes.
 */
bool
oxide_boot_ramdisk_write_iov_offset(oxide_boot_t *oxb, iovec_t *iov, uint_t
    niov, uint64_t offset)
{
	bool ret;

	VERIFY(!oxb->oxb_compressed);

	mutex_enter(&oxb->oxb_mutex);
	ret = oxide_boot_write_iov(oxb, iov, niov, offset);
	mutex_exit(&oxb->oxb_mutex);

	return (ret);
}

/*
 * This function appends data to the ramdisk image at the current offset. For
 * an uncompressed image, the data are passed directly to
 * oxide_boot_ramdisk_append_cb, otherwise the byte sequence is passed to the
 * stream decompressor which will call the same function one or more times with
 * uncompressed data to be written.
 */
bool
oxide_boot_ramdisk_write_append(oxide_boot_t *oxb, uint8_t *buf, size_t len)
{
	size_t opos;

	mutex_enter(&oxb->oxb_mutex);

	opos = oxb->oxb_opos;

	if (!oxb->oxb_compressed) {
		bool ret = oxide_boot_ramdisk_append_cb(oxb, buf, len);
		mutex_exit(&oxb->oxb_mutex);
		return (ret);
	}

	int err = z_uncompress_stream(oxb->oxb_zstream, buf, len,
	    oxide_boot_ramdisk_append_cb, oxb);

	mutex_exit(&oxb->oxb_mutex);

	switch (err) {
	case Z_STREAM_END:
		oxide_boot_debug("end of compression stream");
		/* FALLTHROUGH */
	case Z_OK:
		return (true);
	case Z_BUF_ERROR:
		oxide_boot_warn("failed ramdisk write at offset 0x%lx", opos);
		break;
	default:
		oxide_boot_warn("failed decompression: %s", z_strerror(err));
		break;
	}

	return (false);
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
oxide_boot_ramdisk_write_flush(oxide_boot_t *oxb)
{
	bool ret = true;

	mutex_enter(&oxb->oxb_mutex);
	if (oxb->oxb_acc > 0) {
		ret = oxide_boot_write_block(oxb, oxb->oxb_block, oxb->oxb_acc,
		    oxb->oxb_opos);
		oxb->oxb_opos += oxb->oxb_acc;
		oxb->oxb_acc = 0;
	}
	mutex_exit(&oxb->oxb_mutex);

	return (ret);
}

bool
oxide_boot_ramdisk_set_len(oxide_boot_t *oxb, uint64_t len)
{
	mutex_enter(&oxb->oxb_mutex);

	if (len < oxb->oxb_ramdisk_data_size) {
		oxide_boot_warn("image size %lu < written size %lu",
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
	int c = bcmp(csum, oxb->oxb_csum_want, len);
	mutex_exit(&oxb->oxb_mutex);

	return (c == 0);
}

bool
oxide_boot_set_compressed(oxide_boot_t *oxb)
{
	bool ret;

	mutex_enter(&oxb->oxb_mutex);
	if (z_uncompress_stream_init(&oxb->oxb_zstream) != Z_OK) {
		oxide_boot_warn("Could not initialise stream decompressor");
		ret = false;
	} else {
		oxb->oxb_compressed = true;
		ret = true;
	}
	mutex_exit(&oxb->oxb_mutex);
	return (ret);
}

bool
oxide_boot_disk_read(ldi_handle_t lh, uint64_t offset, uint8_t *buf, size_t len)
{
	iovec_t iov = {
		.iov_base = (int8_t *)buf,
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
		oxide_boot_warn(
		    "read from disk (offset %lu size %lu) failed %d",
		    offset, len, r);
		return (false);
	}

	if (uio.uio_resid != 0) {
		oxide_boot_warn("read from disk (offset %lu) was short",
		    offset);
		return (false);
	}

	return (true);
}

static bool
oxide_boot_ramdisk_check(oxide_boot_t *oxb)
{
	crypto_context_t cc;
	crypto_mechanism_t cm;

	int r;

	if (oxb->oxb_rd_disk == NULL) {
		return (false);
	}

	bzero(&cm, sizeof (cm));
	if ((cm.cm_type = crypto_mech2id(SUN_CKM_SHA256)) ==
	    CRYPTO_MECH_INVALID) {
		return (false);
	}

	if ((r = crypto_digest_init(&cm, &cc, NULL)) != CRYPTO_SUCCESS) {
		oxide_boot_warn("crypto_digest_init() failed %d", r);
	}

	uint8_t *buf = kmem_alloc(PAGESIZE, KM_SLEEP);
	size_t rem = oxb->oxb_ramdisk_data_size;
	size_t pos = 0;
	for (;;) {
		size_t sz = MIN(rem, PAGESIZE);
		if (sz == 0) {
			break;
		}

		if (!oxide_boot_disk_read(oxb->oxb_rd_disk, pos, buf, sz)) {
			oxide_boot_warn("ramdisk read failed");
			goto bail;
		}

		crypto_data_t cd = {
			.cd_format = CRYPTO_DATA_RAW,
			.cd_length = sz,
			.cd_raw = {
				.iov_base = (int8_t *)buf,
				.iov_len = sz,
			},
		};
		if ((r = crypto_digest_update(cc, &cd, 0) != CRYPTO_SUCCESS)) {
			oxide_boot_warn("crypto digest update failed %d", r);
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
	if ((r = crypto_digest_final(cc, &cd, 0)) != CRYPTO_SUCCESS) {
		oxide_boot_warn("crypto digest final failed %d", r);
		goto bail;
	}

	kmem_free(buf, PAGESIZE);

	if (bcmp(oxb->oxb_csum_want, oxb->oxb_csum_have,
	    OXBOOT_CSUMLEN_SHA256) != 0) {
		oxide_boot_warn("checksum mismatch");
		oxide_dump_sum("want", oxb->oxb_csum_want);
		oxide_dump_sum("have", oxb->oxb_csum_have);

		/*
		 * Do not call crypto_cancel_ctx() after crypto_digest_final()!
		 */
		return (false);
	}

	oxide_boot_debug("checksum ok!");
	return (true);

bail:
	kmem_free(buf, PAGESIZE);
	crypto_cancel_ctx(cc);
	return (false);
}

static void
oxide_boot_fini(oxide_boot_t *oxb)
{
	if (oxb->oxb_ramdisk_path != NULL) {
		strfree(oxb->oxb_ramdisk_path);
	}
	if (oxb->oxb_ramdisk_dataset != NULL) {
		strfree(oxb->oxb_ramdisk_dataset);
	}
	if (oxb->oxb_rd_disk != NULL) {
		VERIFY0(ldi_close(oxb->oxb_rd_disk, FREAD | FWRITE, kcred));
	}
	ldi_ident_release(oxb->oxb_li);
	mutex_destroy(&oxb->oxb_mutex);
	kmem_free(oxb, sizeof (*oxb));
}

static void __NORETURN
oxide_boot_fail(ipcc_host_boot_failure_t reason, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	oxide_boot_vwarn(fmt, va);
	va_end(va);

	va_start(va, fmt);
	(void) kernel_ipcc_bootfailv(reason, fmt, va);
	va_end(va);

	va_start(va, fmt);
	vpanic(fmt, va);
	/* vpanic() does not return */
}

static int
just_attach_this(dev_info_t *dip, void *arg)
{
	const char *nodetarget = arg;

	if (ddi_node_name(dip) == NULL ||
	    strcmp(ddi_node_name(dip), nodetarget) != 0) {
		goto skip;
	}

	char *path = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
	(void) ddi_pathname(dip, path);
	printf(" * attempting to attach: %s...\n", path);
	kmem_free(path, MAXPATHLEN);

	if (i_ddi_attach_node_hierarchy(dip) != DDI_SUCCESS) {
		printf("could not!\n");
	}

	printf("ok!\n");

skip:
	return (DDI_WALK_CONTINUE);
}

static void
oxide_boot_locate(void)
{
	int err;

	oxide_boot_note("Starting Oxide boot (DRAM test edition!)");

	/*
	 * XXX In the DRAM test image we're not going to do any of the usual
	 * stuff.  We'll start up and just attempt to attach zen_umc so that we
	 * can get the information out of it.  By never returning from this
	 * function, we can prevent the OS from attempting to mount a root file
	 * system, which we will not have on the test bench.
	 */

	const char *modtarget = "drv/zen_umc";
	const char *symtarg = "zen_umc";
	modctl_t *module = NULL;
	zen_umc_t **umcp = NULL;

	/*
	 * First, attempt to load the module...
	 */
modagain:
	delay(1 * drv_usectohz(MICROSEC));

	printf(" * loading module \"%s\"...\n", modtarget);
	if (modload(NULL, modtarget) == -1) {
		printf("could not!\n");
		goto modagain;
	}

	printf(" * holding module \"%s\"...\n", modtarget);
	if ((module = mod_hold_by_name(modtarget)) == NULL) {
		printf("could not!\n");
		goto modagain;
	}

symagain:
	delay(1 * drv_usectohz(MICROSEC));

	printf(" * locating \"%s\" symbol from module \"%s\"...\n", symtarg,
	    modtarget);
	if ((umcp = (void *)modlookup_by_modctl(module, symtarg)) == NULL) {
		printf("could not!\n");
		goto symagain;
	}

lookagain:
	delay(1 * drv_usectohz(MICROSEC));

	/*
	 * Now that it is loaded, we need to attach things.
	 */
	printf(" * attaching amdzen...\n");
	if (i_ddi_attach_pseudo_node("amdzen") == NULL) {
		printf("could not!\n");
	}

	printf(" * attaching amdzen stubs...\n");
	if (i_ddi_attach_hw_nodes("amdzen_stub") != DDI_SUCCESS) {
		printf("could not!\n");
	}

	printf(" * attaching zen_umc nodes...\n");
	if (i_ddi_attach_hw_nodes("zen_umc") != DDI_SUCCESS) {
		printf("could not!\n");
	}

	/*ddi_walk_devs(ddi_root_node(), just_attach_this, (void *)"zen_umc");*/

	printf(" * zen_umc = %p\n", *umcp);
	if (*umcp == NULL) {
		printf("could not!\n");
		goto lookagain;
	}

	/*
	 * Attempt to fish out the information we want...
	 */
	zen_umc_t *umc = *umcp;
	for (uint_t c = 0; c <= 7; c++) {
		const char *chan_map[] = {
		    "A", /* 0 */
		    "B", /* 1 */
		    "D", /* 2 */
		    "C", /* 3 */
		    "H", /* 4 */
		    "G", /* 5 */
		    "E", /* 6 */
		    "F", /* 7 */
		};
		const uint32_t want_raw = (1 << 9) | (1 << 12) | (1 << 31);

		zen_umc_chan_t *chan = &umc->umc_dfs[0].zud_chan[c];

		printf("channel %s (%u) umccfg_raw = %x\n",
		    chan_map[c], c,
		    chan->chan_umccfg_raw);

		for (uint_t d = 0; d <= 1; d++) {
			printf("channel %s (%u) dimm %u ud_flags = %x\n",
			    chan_map[c], c, d,
			    chan->chan_dimms[d].ud_flags);
			printf("channel %s (%u) dimm %u ud_dimm_size = %lx\n",
			    chan_map[c], c, d,
			    chan->chan_dimms[d].ud_dimm_size);
		}
	}

	/*
	 * Throw in the detected installed memory size in bytes for good
	 * measure:
	 */
	uint64_t membytes = physinstalled * PAGESIZE;
	printf("physmem bytes = %lu\n", membytes);

	printf("\n");

	goto lookagain;

#if 0
	oxide_boot_t *oxb = kmem_zalloc(sizeof (*oxb), KM_SLEEP);
	oxide_boot_debug("oxb=%p", oxb);
	mutex_init(&oxb->oxb_mutex, NULL, MUTEX_DRIVER, NULL);
	err = ldi_ident_from_mod(&oxide_boot_modlinkage, &oxb->oxb_li);
	if (err != 0) {
		oxide_boot_fail(IPCC_BOOTFAIL_GENERAL,
		    "could not get LDI identity, error %d", err);
	}

	/*
	 * Load the hash of the ramdisk that matches the bits in the phase1
	 * archive.
	 */
	intptr_t fd;
	if ((fd = kobj_open("/boot_image_csum")) == -1 ||
	    kobj_read(fd, (int8_t *)&oxb->oxb_csum_want,
	    OXBOOT_CSUMLEN_SHA256, 0) != OXBOOT_CSUMLEN_SHA256) {
		oxide_boot_fail(IPCC_BOOTFAIL_GENERAL,
		    "could not read /boot_image_csum");
	}
	kobj_close(fd);
	oxide_dump_sum("Phase 1 wants", oxb->oxb_csum_want);

	/*
	 * The checksum only appears in the boot archive, which will be
	 * released after the root pool is mounted.  Preserve the checksum for
	 * diagnostic purposes.
	 */
	(void) e_ddi_prop_update_byte_array(DDI_DEV_T_NONE, ddi_root_node(),
	    OXBOOT_DEVPROP_IMAGE_CHECKSUM, oxb->oxb_csum_want,
	    OXBOOT_CSUMLEN_SHA256);

	/*
	 * During early-boot communication with the SP, the desired phase 2
	 * image source will have been set as a boot property. The value will
	 * be one of:
	 *
	 *   - sp	Retrieve from the service processor.
	 *   - net	Network boot - this is used during development.
	 *   - disk:NN	M.2 device in slot NN.
	 */
	bool success = false;
	char *bootdev;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, BTPROP_NAME_BOOT_SOURCE, &bootdev) !=
	    DDI_SUCCESS) {
		oxide_boot_fail(IPCC_BOOTFAIL_NOPHASE2,
		    "No phase2 image source was specified");
	}

	if (strcmp(bootdev, "sp") == 0) {
		success = oxide_boot_sp(oxb);
	} else if (strcmp(bootdev, "net") == 0) {
		success = oxide_boot_net(oxb);
	} else if (strncmp(bootdev, "disk:", 5) == 0) {
		u_longlong_t slot;

		if (ddi_strtoull(bootdev + 5, NULL, 10, &slot) == 0 &&
		    slot < UINT16_MAX) {
			success = oxide_boot_disk(oxb, (int)slot);
		}
	}

	mutex_enter(&oxb->oxb_mutex);
	if (oxb->oxb_compressed)
		z_uncompress_stream_fini(oxb->oxb_zstream);
	mutex_exit(&oxb->oxb_mutex);

	if (!success) {
		oxide_boot_fail(IPCC_BOOTFAIL_NOPHASE2,
		    "Could not find a valid phase2 image on %s", bootdev);
	}

	ddi_prop_free(bootdev);

	oxide_boot_debug("ramdisk data size = %lu",
	    oxb->oxb_ramdisk_data_size);
	if (oxb->oxb_ramdisk_dataset == NULL) {
		oxide_boot_fail(IPCC_BOOTFAIL_HEADER,
		    "no dataset name was specified");
	}

	if (!oxide_boot_ramdisk_check(oxb)) {
		char want[OXBOOT_CSUMBUF_SHA256];
		char have[OXBOOT_CSUMBUF_SHA256];

		oxide_boot_fail(IPCC_BOOTFAIL_INTEGRITY,
		    "boot image integrity failure want %s got %s",
		    oxide_format_sum(want, sizeof (want), oxb->oxb_csum_want),
		    oxide_format_sum(have, sizeof (have), oxb->oxb_csum_have));
	}

	/*
	 * Tell the system to import the ramdisk device as a ZFS pool, and to
	 * ignore any device names or IDs found in the pool label.
	 */
	(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
	    ddi_root_node(), "fstype", "zfs");
	(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
	    ddi_root_node(), "zfs-bootfs",
	    oxb->oxb_ramdisk_dataset);
	(void) e_ddi_prop_update_string(DDI_DEV_T_NONE,
	    ddi_root_node(), "zfs-rootdisk-path",
	    oxb->oxb_ramdisk_path);

	oxide_boot_fini(oxb);
#endif
}

boot_image_ops_t _boot_image_ops = {
	.bimo_version =				BOOT_IMAGE_OPS_VERSION,
	.bimo_locate =				oxide_boot_locate,
};
