/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Portions Copyright 2010 Robert Milkowski
 *
 * Copyright 2017 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2019 Joyent, Inc.
 * Copyright 2026 Oxide Computer Company
 */

/*
 * ZFS volume emulation driver.
 *
 * Makes a DMU object look like a volume of arbitrary size, up to 2^64 bytes.
 * Volumes are accessed through the symbolic links named:
 *
 * /dev/zvol/dsk/<pool_name>/<dataset_name>
 * /dev/zvol/rdsk/<pool_name>/<dataset_name>
 *
 * These links are created by the /dev filesystem (sdev_zvolops.c).
 * Volumes are persistent through reboot.  No user command needs to be
 * run before opening and using a device.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/dmu_traverse.h>
#include <sys/dnode.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dkio.h>
#include <sys/efi_partition.h>
#include <sys/byteorder.h>
#include <sys/pathname.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/crc32.h>
#include <sys/dirent.h>
#include <sys/policy.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/mkdev.h>
#include <sys/zil.h>
#include <sys/refcount.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_rlock.h>
#include <sys/vdev_impl.h>
#include <sys/zvol.h>
#include <sys/dumphdr.h>
#include <sys/zil_impl.h>
#include <sys/dbuf.h>
#include <sys/dmu_tx.h>
#include <sys/zfeature.h>
#include <sys/zio_checksum.h>
#include <sys/zil_impl.h>
#include <sys/smt.h>
#include <sys/dkioc_free_util.h>
#include <sys/zfs_rlock.h>

#include "zfs_namecheck.h"

void *zfsdev_state;
static char *zvol_tag = "zvol_tag";

#define	ZVOL_DUMPSIZE		"dumpsize"

/*
 * This lock protects the zfsdev_state structure from being modified
 * while it's being used, e.g. an open that comes in before a create
 * finishes.  It also protects temporary opens of the dataset so that,
 * e.g., an open doesn't get a spurious EBUSY.
 */
kmutex_t zfsdev_state_lock;
static uint32_t zvol_minors;

/*
 * zvol specific flags
 * Some of the flags indicate attributes of the zvol and others
 * are used to describe state. In the future, we may want to separate
 * these to avoid confusion.
 *
 * Attributes:
 * ZVOL_RDONLY -	readonly zvol
 * ZVOL_EXCL -		exclusion open
 * ZVOL_WCE -		write cache enabled
 * ZVOL_DUMPIFIED -	zvol has been converted to a dump device
 * ZVOL_RAW -		zvol was created as a raw volume
 *
 * Zvol States:
 * ZVOL_ZERO_STARTED -	Set when the zvol zero thread starts (typically on
 *			the first open) and remains set until the next first
 *			open. This state is consumed by ioctl administrative
 *			commands, which rely on the zero thread’s status.
 *			To ensure accurate status reporting, those commands
 *			wait for this state to be reached.
 * ZVOL_PREALLOCED -	Indicates that dump and raw volumes have completed
 *			preallocation. For dump devices, this means blocks
 *			have been allocated but not zeroed. For raw devices,
 *			blocks are allocated and either written or trimmed.
 */
enum zio_flags {
	ZVOL_RDONLY		= 1 << 0,
	ZVOL_EXCL		= 1 << 1,
	ZVOL_WCE		= 1 << 2,
	ZVOL_DUMPIFIED		= 1 << 3,
	ZVOL_RAW		= 1 << 4,
	ZVOL_ZERO_STARTED	= 1 << 5,
	/* Used by both dump or raw zvols to indicate preallocation finished */
	ZVOL_PREALLOCED		= 1 << 6
};

/*
 * For raw volumes we must keep the device open while the initialization
 * is running. Track this extra open reference as the last element
 * in the zv_open_count array.
 */
#define	OTYP_INITIALIZING	OTYPCNT

/*
 * The in-core state of each volume.
 */
typedef struct zvol_state {
	char		zv_name[MAXPATHLEN]; /* pool/dd name */
	uint64_t	zv_volsize;	/* amount of space we advertise */
	uint64_t	zv_volblocksize; /* volume block size */
	minor_t		zv_minor;	/* minor number */
	uint8_t		zv_min_bs;	/* minimum addressable block shift */
	enum zio_flags	zv_flags;	/* readonly, dumpified, etc. */
	objset_t	*zv_objset;	/* objset handle */
	uint32_t	zv_open_count[OTYPCNT + 1];	/* open counts */
	uint32_t	zv_total_opens;	/* total open count */
	zilog_t		*zv_zilog;	/* ZIL handle */
	dva_t		*zv_dvas;	/* block -> dva mapping for dump/raw */
	size_t		zv_ndvas;	/* number of dvas allocated */
	rangelock_t	zv_rangelock;
	dnode_t		*zv_dn;		/* dnode hold */
	/* set to interrupt initialization */
	boolean_t	zv_zero_exit_wanted;
	/*
	 * zv_state_lock protects the dva mapping, flags, zero thread, and
	 * open counts.
	 */
	kmutex_t	zv_state_lock;
	kcondvar_t	zv_state_cv;
	kthread_t	*zv_zero_thread;
	int		zv_zero_error;
	uint64_t	zv_zero_off;
} zvol_state_t;

/*
 * zvol maximum transfer in one DMU tx.
 */
int zvol_maxphys = DMU_MAX_ACCESS/2;

/*
 * Toggle unmap functionality.
 */
boolean_t zvol_unmap_enabled = B_TRUE;

/*
 * If true, unmaps requested as synchronous are executed synchronously,
 * otherwise all unmaps are asynchronous.
 */
boolean_t zvol_unmap_sync_enabled = B_FALSE;

extern int zfs_set_prop_nvlist(const char *, zprop_source_t,
    nvlist_t *, nvlist_t *);
static int zvol_remove_zv(zvol_state_t *);
static int zvol_get_data(void *arg, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio);
static int zvol_dumpify(zvol_state_t *zv);
static int zvol_dump_fini(zvol_state_t *zv);
static int zvol_dump_init(zvol_state_t *zv, boolean_t resize);
static int zvol_prealloc(zvol_state_t *zv);
static int zvol_open_impl(zvol_state_t *zv, int flag, int otyp);
static void zvol_close_impl(zvol_state_t *zv, int otyp);

static void
zvol_size_changed(zvol_state_t *zv, uint64_t volsize)
{
	dev_t dev = makedevice(ddi_driver_major(zfs_dip), zv->zv_minor);

	zv->zv_volsize = volsize;
	VERIFY(ddi_prop_update_int64(dev, zfs_dip,
	    "Size", volsize) == DDI_SUCCESS);
	VERIFY(ddi_prop_update_int64(dev, zfs_dip,
	    "Nblocks", lbtodb(volsize)) == DDI_SUCCESS);

	/* Notify specfs to invalidate the cached size */
	spec_size_invalidate(dev, VBLK);
	spec_size_invalidate(dev, VCHR);
}

static uint64_t
zvol_num_blocks(zvol_state_t *zv)
{
	return (zv->zv_volsize / zv->zv_volblocksize);
}

int
zvol_check_volsize(uint64_t volsize, uint64_t blocksize)
{
	if (volsize == 0)
		return (SET_ERROR(EINVAL));

	if (volsize % blocksize != 0)
		return (SET_ERROR(EINVAL));

#ifdef _ILP32
	if (volsize - 1 > SPEC_MAXOFFSET_T)
		return (SET_ERROR(EOVERFLOW));
#endif
	return (0);
}

int
zvol_check_volblocksize(uint64_t volblocksize)
{
	if (volblocksize < SPA_MINBLOCKSIZE ||
	    volblocksize > SPA_OLD_MAXBLOCKSIZE ||
	    !ISP2(volblocksize))
		return (SET_ERROR(EDOM));

	return (0);
}

int
zvol_get_stats(objset_t *os, nvlist_t *nv)
{
	int error;
	dmu_object_info_t doi;
	uint64_t val;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &val);
	if (error)
		return (error);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLSIZE, val);

	error = zap_lookup(os, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_RAWVOL), 8, 1, &val);
	if (error == ENOENT) {
		val = 0;
		error = 0;
	} else if (error) {
		return (error);
	}
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_RAWVOL, val);

	error = dmu_object_info(os, ZVOL_OBJ, &doi);

	if (error == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLBLOCKSIZE,
		    doi.doi_data_block_size);
	}

	return (error);
}

static zvol_state_t *
zvol_minor_lookup(const char *name)
{
	minor_t minor;
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	for (minor = 1; minor <= ZFSDEV_MAX_MINOR; minor++) {
		zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
		if (zv == NULL)
			continue;
		if (strcmp(zv->zv_name, name) == 0)
			return (zv);
	}

	return (NULL);
}

/* extent mapping arg */
struct maparg {
	zvol_state_t	*ma_zv;
	uint64_t	ma_blks;
};

/*ARGSUSED*/
static int
zvol_map_block(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	zvol_state_t *zv = arg;

	if (bp == NULL || BP_IS_HOLE(bp) ||
	    zb->zb_object != ZVOL_OBJ || zb->zb_level != 0)
		return (0);

	VERIFY(!BP_IS_EMBEDDED(bp));

	/* Abort immediately if we have encountered gang blocks */
	if (BP_IS_GANG(bp))
		return (SET_ERROR(EFRAGS));

	VERIFY3U(zb->zb_blkid, <, zvol_num_blocks(zv));
	zv->zv_dvas[zb->zb_blkid] = bp->blk_dva[0];

	return (0);
}

static void
zvol_free_dvas(zvol_state_t *zv)
{
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	if (zv->zv_dvas != NULL) {
		/*
		 * Note, ndvas may differ from zvol_num_blocks() if the volume
		 * size was changed (see zvol_size_changed()).
		 */
		kmem_free(zv->zv_dvas, zv->zv_ndvas * sizeof (dva_t));
		zv->zv_dvas = NULL;
		zv->zv_ndvas = 0;
	}
}

static int
zvol_get_dvas(zvol_state_t *zv)
{
	objset_t *os = zv->zv_objset;
	int		err;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	VERIFY(zv->zv_flags & ZVOL_PREALLOCED);
	zvol_free_dvas(zv);

	/* commit any in-flight changes before traversing the dataset */
	txg_wait_synced(dmu_objset_pool(os), 0);
	zv->zv_ndvas = zvol_num_blocks(zv);
	zv->zv_dvas = kmem_zalloc(zv->zv_ndvas * sizeof (dva_t), KM_SLEEP);
	err = traverse_dataset(dmu_objset_ds(os), 0,
	    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA, zvol_map_block, zv);
	if (err == 0) {
		/* make sure we filled in all dvas */
		for (uint64_t i = 0; i < zvol_num_blocks(zv); i++) {
			if (DVA_IS_EMPTY(&zv->zv_dvas[i])) {
				err = EIO;
				break;
			}
		}

	}
	if (err != 0) {
		zvol_free_dvas(zv);
		return (err);
	}

	return (0);
}

/* ARGSUSED */
void
zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;
	nvlist_t *nvprops = zct->zct_props;
	int error;
	uint64_t volblocksize, volsize, rawvol;

	VERIFY(nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) == 0);
	if (nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &volblocksize) != 0)
		volblocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);
	if (nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_RAWVOL), &rawvol) != 0) {
		rawvol = 0;
	}

	/*
	 * These properties must be removed from the list so the generic
	 * property setting step won't apply to them.
	 */
	VERIFY(nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE)) == 0);
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE));
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_RAWVOL));

	error = dmu_object_claim(os, ZVOL_OBJ, DMU_OT_ZVOL, volblocksize,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_create_claim(os, ZVOL_ZAP_OBJ, DMU_OT_ZVOL_PROP,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize, tx);
	ASSERT(error == 0);

	if (rawvol) {
		error = zap_update(os, ZVOL_ZAP_OBJ,
		    zfs_prop_to_name(ZFS_PROP_RAWVOL), 8, 1, &rawvol, tx);
		ASSERT(error == 0);
	}
}

/*
 * Replay a TX_TRUNCATE ZIL transaction if asked.  TX_TRUNCATE is how we
 * implement DKIOCFREE/free-long-range.
 */
static int
zvol_replay_truncate(void *arg1, void *arg2, boolean_t byteswap)
{
	zvol_state_t *zv = arg1;
	lr_truncate_t *lr = arg2;
	uint64_t offset, length;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	return (dmu_free_long_range(zv->zv_objset, ZVOL_OBJ, offset, length));
}

/*
 * Replay a TX_WRITE ZIL transaction that didn't get committed
 * after a system failure
 */
/* ARGSUSED */
static int
zvol_replay_write(void *arg1, void *arg2, boolean_t byteswap)
{
	zvol_state_t *zv = arg1;
	lr_write_t *lr = arg2;
	objset_t *os = zv->zv_objset;
	char *data = (char *)(lr + 1);	/* data follows lr_write_t */
	uint64_t offset, length;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	/* If it's a dmu_sync() block, write the whole block */
	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		uint64_t blocksize = BP_GET_LSIZE(&lr->lr_blkptr);
		if (length < blocksize) {
			offset -= offset % blocksize;
			length = blocksize;
		}
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, length);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		dmu_write(os, ZVOL_OBJ, offset, length, data, tx);
		dmu_tx_commit(tx);
	}

	return (error);
}

/* ARGSUSED */
static int
zvol_replay_err(void *arg1, void *arg2, boolean_t byteswap)
{
	return (SET_ERROR(ENOTSUP));
}

/*
 * Callback vectors for replaying records.
 * Only TX_WRITE and TX_TRUNCATE are needed for zvol.
 */
zil_replay_func_t *zvol_replay_vector[TX_MAX_TYPE] = {
	zvol_replay_err,	/* 0 no such transaction type */
	zvol_replay_err,	/* TX_CREATE */
	zvol_replay_err,	/* TX_MKDIR */
	zvol_replay_err,	/* TX_MKXATTR */
	zvol_replay_err,	/* TX_SYMLINK */
	zvol_replay_err,	/* TX_REMOVE */
	zvol_replay_err,	/* TX_RMDIR */
	zvol_replay_err,	/* TX_LINK */
	zvol_replay_err,	/* TX_RENAME */
	zvol_replay_write,	/* TX_WRITE */
	zvol_replay_truncate,	/* TX_TRUNCATE */
	zvol_replay_err,	/* TX_SETATTR */
	zvol_replay_err,	/* TX_ACL */
	zvol_replay_err,	/* TX_CREATE_ACL */
	zvol_replay_err,	/* TX_CREATE_ATTR */
	zvol_replay_err,	/* TX_CREATE_ACL_ATTR */
	zvol_replay_err,	/* TX_MKDIR_ACL */
	zvol_replay_err,	/* TX_MKDIR_ATTR */
	zvol_replay_err,	/* TX_MKDIR_ACL_ATTR */
	zvol_replay_err,	/* TX_WRITE2 */
};

int
zvol_name2minor(const char *name, minor_t *minor)
{
	zvol_state_t *zv;

	mutex_enter(&zfsdev_state_lock);
	zv = zvol_minor_lookup(name);
	if (minor && zv)
		*minor = zv->zv_minor;
	mutex_exit(&zfsdev_state_lock);
	return (zv ? 0 : -1);
}

/*
 * Create a minor node (plus a whole lot more) for the specified volume.
 */
int
zvol_create_minor(const char *name)
{
	zfs_soft_state_t *zs;
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t doi;
	minor_t minor = 0;
	char chrbuf[30], blkbuf[30];
	int error;

	mutex_enter(&zfsdev_state_lock);

	if (zvol_minor_lookup(name) != NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(EEXIST));
	}

	/* lie and say we're read-only */
	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, B_TRUE, FTAG, &os);

	if (error) {
		mutex_exit(&zfsdev_state_lock);
		return (error);
	}

	if ((minor = zfsdev_minor_alloc()) == 0) {
		dmu_objset_disown(os, 1, FTAG);
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(ENXIO));
	}

	if (ddi_soft_state_zalloc(zfsdev_state, minor) != DDI_SUCCESS) {
		dmu_objset_disown(os, 1, FTAG);
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(EAGAIN));
	}
	(void) ddi_prop_update_string(minor, zfs_dip, ZVOL_PROP_NAME,
	    (char *)name);

	(void) snprintf(chrbuf, sizeof (chrbuf), "%u,raw", minor);

	if (ddi_create_minor_node(zfs_dip, chrbuf, S_IFCHR,
	    minor, DDI_PSEUDO, 0) == DDI_FAILURE) {
		ddi_soft_state_free(zfsdev_state, minor);
		dmu_objset_disown(os, 1, FTAG);
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(EAGAIN));
	}

	(void) snprintf(blkbuf, sizeof (blkbuf), "%u", minor);

	if (ddi_create_minor_node(zfs_dip, blkbuf, S_IFBLK,
	    minor, DDI_PSEUDO, 0) == DDI_FAILURE) {
		ddi_remove_minor_node(zfs_dip, chrbuf);
		ddi_soft_state_free(zfsdev_state, minor);
		dmu_objset_disown(os, 1, FTAG);
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(EAGAIN));
	}

	zs = ddi_get_soft_state(zfsdev_state, minor);
	zs->zss_type = ZSST_ZVOL;
	zv = zs->zss_data = kmem_zalloc(sizeof (zvol_state_t), KM_SLEEP);
	(void) strlcpy(zv->zv_name, name, MAXPATHLEN);
	/*
	 * Volumes all get the standard block shift with the exception of
	 * raw volumes. See zvol_first_open() where we override the default
	 * to match what the underlying device advertises.
	 */
	zv->zv_min_bs = DEV_BSHIFT;
	zv->zv_minor = minor;
	zv->zv_objset = os;

	if (dmu_objset_is_snapshot(os) || !spa_writeable(dmu_objset_spa(os)))
		zv->zv_flags |= ZVOL_RDONLY;
	rangelock_init(&zv->zv_rangelock, NULL, NULL);
	mutex_init(&zv->zv_state_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zv->zv_state_cv, NULL, CV_DEFAULT, NULL);

	/* get and cache the blocksize */
	error = dmu_object_info(os, ZVOL_OBJ, &doi);
	ASSERT(error == 0);
	zv->zv_volblocksize = doi.doi_data_block_size;

	if (spa_writeable(dmu_objset_spa(os))) {
		if (zil_replay_disable)
			zil_destroy(dmu_objset_zil(os), B_FALSE);
		else
			zil_replay(os, zv, zvol_replay_vector);
	}
	dmu_objset_disown(os, 1, FTAG);
	zv->zv_objset = NULL;

	zvol_minors++;

	mutex_exit(&zfsdev_state_lock);

	return (0);
}

/*
 * Remove minor node for the specified volume.
 */
static int
zvol_remove_zv(zvol_state_t *zv)
{
	char nmbuf[20];
	minor_t minor = zv->zv_minor;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));
	if (zv->zv_total_opens != 0)
		return (SET_ERROR(EBUSY));

	/* zvol_last_close() should have cleaned these up already */
	ASSERT(zv->zv_objset == NULL);
	ASSERT(zv->zv_dvas == NULL);

	(void) snprintf(nmbuf, sizeof (nmbuf), "%u,raw", minor);
	ddi_remove_minor_node(zfs_dip, nmbuf);

	(void) snprintf(nmbuf, sizeof (nmbuf), "%u", minor);
	ddi_remove_minor_node(zfs_dip, nmbuf);

	cv_destroy(&zv->zv_state_cv);
	mutex_destroy(&zv->zv_state_lock);
	rangelock_fini(&zv->zv_rangelock);

	kmem_free(zv, sizeof (zvol_state_t));

	ddi_soft_state_free(zfsdev_state, minor);

	zvol_minors--;
	return (0);
}

int
zvol_remove_minor(const char *name)
{
	zvol_state_t *zv;
	int rc;

	mutex_enter(&zfsdev_state_lock);
	if ((zv = zvol_minor_lookup(name)) == NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(ENXIO));
	}
	rc = zvol_remove_zv(zv);
	mutex_exit(&zfsdev_state_lock);
	return (rc);
}

int
zvol_first_open(zvol_state_t *zv, boolean_t rdonly)
{
	objset_t *os;
	uint64_t volsize;
	int error;
	uint64_t readonly;
	boolean_t ro;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	ro = (rdonly || (strchr(zv->zv_name, '@') != NULL));
	error = dmu_objset_own(zv->zv_name, DMU_OST_ZVOL, ro, B_TRUE, zv, &os);
	if (error)
		return (error);

	zv->zv_objset = os;
	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error) {
		ASSERT0(error);
		dmu_objset_disown(os, 1, zv);
		return (error);
	}

	error = dnode_hold(os, ZVOL_OBJ, zvol_tag, &zv->zv_dn);
	if (error) {
		dmu_objset_disown(os, 1, zv);
		return (error);
	}

	zvol_size_changed(zv, volsize);

	uint64_t rawvol;
	error = zap_lookup(os, ZVOL_ZAP_OBJ, zfs_prop_to_name(ZFS_PROP_RAWVOL),
	    8, 1, &rawvol);
	if (error == 0 && rawvol) {
		zv->zv_flags |= ZVOL_RAW;
		/*
		 * Since raw zvols issue i/o directly to the underlying disks,
		 * we cannot accept i/o's smaller than the underlying disks can.
		 */
		zv->zv_min_bs = dmu_objset_spa(os)->spa_max_ashift;

		error = zvol_prealloc(zv);
		if (error) {
			dnode_rele(zv->zv_dn, zvol_tag);
			zv->zv_dn = NULL;
			dmu_objset_disown(os, 1, zv);
			return (error);
		}
	}

	zv->zv_zilog = zil_open(os, zvol_get_data);

	VERIFY(dsl_prop_get_integer(zv->zv_name, "readonly", &readonly,
	    NULL) == 0);
	if (readonly || dmu_objset_is_snapshot(os) ||
	    !spa_writeable(dmu_objset_spa(os)))
		zv->zv_flags |= ZVOL_RDONLY;
	else
		zv->zv_flags &= ~ZVOL_RDONLY;

	return (0);
}

void
zvol_last_close(zvol_state_t *zv)
{
	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	if (zv->zv_flags & ZVOL_RAW) {
		zvol_free_dvas(zv);
		zv->zv_flags &= ~ZVOL_RAW;
	}

	dnode_rele(zv->zv_dn, zvol_tag);
	zv->zv_dn = NULL;

	/*
	 * Evict cached data
	 */
	if (dsl_dataset_is_dirty(dmu_objset_ds(zv->zv_objset)) &&
	    !(zv->zv_flags & ZVOL_RDONLY))
		txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);
	dmu_objset_evict_dbufs(zv->zv_objset);

	dmu_objset_disown(zv->zv_objset, 1, zv);
	zv->zv_objset = NULL;
}

static uint64_t
zvol_get_initialized_offset(objset_t *os)
{
	dmu_object_info_t doi;
	VERIFY0(dmu_object_info(os, ZVOL_OBJ, &doi));
	if (doi.doi_fill_count == 0) {
		return (0);
	} else {
		return (doi.doi_max_offset);
	}
}

static int
zvol_zero(zvol_state_t *zv)
{
	objset_t *os = zv->zv_objset;
	dmu_tx_t *tx;
	uint64_t resid, bytes_zeroed = 0;
	int error = 0;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	resid = zv->zv_volsize;
	VERIFY3U(resid, >, 0);

	zv->zv_zero_off = zvol_get_initialized_offset(os);
	zfs_dbgmsg("zv %p initializing from offset %llu to %llu",
	    zv, zv->zv_zero_off, resid);

	VERIFY3U(resid, >=, zv->zv_zero_off);
	resid -= zv->zv_zero_off;
	while (resid != 0 && !zv->zv_zero_exit_wanted) {
		uint64_t bytes = MIN(resid, SPA_OLD_MAXBLOCKSIZE);

		mutex_exit(&zv->zv_state_lock);

		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, ZVOL_OBJ, zv->zv_zero_off, bytes);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			mutex_enter(&zv->zv_state_lock);
			break;
		}
		dmu_zero(os, ZVOL_OBJ, zv->zv_zero_off, bytes,
		    (zv->zv_flags & ZVOL_DUMPIFIED), tx);

		bytes_zeroed += bytes;
		resid -= bytes;
		dmu_tx_commit(tx);

		mutex_enter(&zv->zv_state_lock);
		zv->zv_zero_off += bytes;
	}
	if (bytes_zeroed > 0) {
		txg_wait_synced(dmu_objset_pool(os), 0);

		if (zv->zv_zero_exit_wanted) {
			zfs_dbgmsg("zvol_zero shutting down: zv %p, flags %d, "
			    "resid %llu, off %llu, bytes_zeroed %llu", zv,
			    zv->zv_flags, resid, zv->zv_zero_off, bytes_zeroed);
			error = EINTR;
		}
	} else {
		zfs_dbgmsg("zvol_zero complete zv %p, "
		    "flags %d, resid %llu, bytes_zeroed %llu, opens %u", zv,
		    zv->zv_flags, resid, bytes_zeroed, zv->zv_total_opens);
	}

	if (error == 0) {
		VERIFY0(resid);
		zv->zv_flags |= ZVOL_PREALLOCED;
	}

	return (error);
}

static void
zvol_zero_thread(void *arg)
{
	zvol_state_t *zv = arg;

	mutex_enter(&zv->zv_state_lock);

	/*
	 * Now that the zero thread has started, let the consumers
	 * know that might rely on it running.
	 */
	zv->zv_flags |= ZVOL_ZERO_STARTED;
	cv_broadcast(&zv->zv_state_cv);

	int error = zvol_zero(zv);
	if (error == 0) {
		error = zvol_get_dvas(zv);
	}

	zfs_dbgmsg("zvol_zero done: zv %p, flags %d, opens %u, err %d",
	    zv, zv->zv_flags, zv->zv_total_opens, error);

	zv->zv_zero_error = error;
	zv->zv_zero_exit_wanted = B_FALSE;
	zv->zv_zero_thread = NULL;
	cv_broadcast(&zv->zv_state_cv);
	mutex_exit(&zv->zv_state_lock);

	zvol_close_impl(zv, OTYP_INITIALIZING);
	thread_exit();
}

/*
 * zvol_prealloc starts initialization of raw and dump devices. Both volume
 * types require allocation of the underlying pool blocks. Raw volumes also
 * require that preallocated blocks be zeroed, either by issuing trims or by
 * writing zeros.
 *
 * Since this process may take some time, most of the work is performed in
 * the background by the zvol_zero_thread. The initialization proceeds through
 * the following states:
 *
 * zvol_prealloc --> zvol_zero_thread
 *	|--> ZVOL_ZERO_STARTED (zvol_zero_thread started)
 *		|--> blocks allocated (dmu_zero)
 *			|--> ZVOL_PREALLOCED (zvol_zero completes successfully)
 *				|--> zv_dvas != NULL (block mapping exists)
 *					|--> zv_zero_thread == NULL (complete)
 *
 * Once the process completes, consumers should check zv_zero_error to
 * determine whether initialization succeeded.
 *
 * Dump volumes start initialization and wait for completion. Because dump
 * devices do not zero blocks, zvol_zero completes quickly.
 *
 * Raw volumes start initialization on first open. Opens will succeed even
 * while initialization is in progress. The strategy routine checks the
 * states listed above and returns appropriate errors so that consumers may
 * retry or fail their I/O as needed.
 */
static int
zvol_prealloc(zvol_state_t *zv)
{
	objset_t *os = zv->zv_objset;
	uint64_t refd, avail, usedobjs, availobjs;
	uint64_t volsize = zv->zv_volsize;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	zv->zv_zero_off = zvol_get_initialized_offset(os);

	/* Check the space usage before attempting to allocate the space */
	dmu_objset_space(os, &refd, &avail, &usedobjs, &availobjs);
	if (avail < (volsize - zv->zv_zero_off)) {
		zfs_dbgmsg("zvol_prealloc ENOSPC avail %llu, size %llu, "
		    "offset %llu", avail, volsize, zv->zv_zero_off);
		return (SET_ERROR(ENOSPC));
	}

	if (zv->zv_zero_thread == NULL) {
		/*
		 * We are getting ready to initialize the raw volume so
		 * clear the ZVOL_PREALLOCED flag to prevent any I/Os from
		 * progressing. We also reset the ZVOL_ZERO_STARTED to
		 * ensure that the administrative interface gets accurate
		 * information about the initialization. Lastly, we need
		 * to keep an open reference to the objset so we increment
		 * the open count using a special open type. This will
		 * ensure that we don't disown the objset when the device
		 * is closed. This count will be decremented when the
		 * initialization completes.
		 */
		zv->zv_flags &= ~(ZVOL_ZERO_STARTED | ZVOL_PREALLOCED);
		VERIFY0(zv->zv_open_count[OTYP_INITIALIZING]);
		zv->zv_total_opens++;
		zv->zv_open_count[OTYP_INITIALIZING]++;

		zv->zv_zero_thread = thread_create(NULL, 0,
		    zvol_zero_thread, zv, 0, &p0, TS_RUN, maxclsyspri);
	}
	return (0);
}

static int
zvol_update_volsize(objset_t *os, uint64_t volsize)
{
	dmu_tx_t *tx;
	int error;
	uint64_t txg;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}
	txg = dmu_tx_get_txg(tx);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1,
	    &volsize, tx);
	dmu_tx_commit(tx);

	txg_wait_synced(dmu_objset_pool(os), txg);

	if (error == 0)
		error = dmu_free_long_range(os,
		    ZVOL_OBJ, volsize, DMU_OBJECT_END);
	return (error);
}

void
zvol_remove_minors(const char *name)
{
	zvol_state_t *zv;
	char *namebuf;
	minor_t minor;

	namebuf = kmem_zalloc(strlen(name) + 2, KM_SLEEP);
	(void) strncpy(namebuf, name, strlen(name));
	(void) strcat(namebuf, "/");
	mutex_enter(&zfsdev_state_lock);
	for (minor = 1; minor <= ZFSDEV_MAX_MINOR; minor++) {

		zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
		if (zv == NULL)
			continue;
		if (strncmp(namebuf, zv->zv_name, strlen(namebuf)) == 0)
			(void) zvol_remove_zv(zv);
	}
	kmem_free(namebuf, strlen(name) + 2);

	mutex_exit(&zfsdev_state_lock);
}

static int
zvol_update_live_volsize(zvol_state_t *zv, uint64_t volsize)
{
	uint64_t old_volsize = 0ULL;
	int error = 0;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	/*
	 * Reinitialize the dump area to the new size. If we
	 * failed to resize the dump area then restore it back to
	 * its original size.  We must set the new volsize prior
	 * to calling dumpvp_resize() to ensure that the device's
	 * size(9P) is visible by the dump subsystem.
	 */
	old_volsize = zv->zv_volsize;
	zvol_size_changed(zv, volsize);

	if (zv->zv_flags & ZVOL_DUMPIFIED) {
		if ((error = zvol_dumpify(zv)) != 0 ||
		    (error = dumpvp_resize()) != 0) {
			int dumpify_error;

			(void) zvol_update_volsize(zv->zv_objset, old_volsize);
			zvol_size_changed(zv, old_volsize);
			dumpify_error = zvol_dumpify(zv);
			error = dumpify_error ? dumpify_error : error;
		}
	}

	/*
	 * Generate a LUN expansion event.
	 */
	if (error == 0) {
		char *physpath = kmem_zalloc(MAXPATHLEN, KM_SLEEP);

		(void) snprintf(physpath, MAXPATHLEN, "%s%u", ZVOL_PSEUDO_DEV,
		    zv->zv_minor);

		zfs_post_dle_sysevent(physpath);
		kmem_free(physpath, MAXPATHLEN);
	}
	return (error);
}

int
zvol_set_volsize(const char *name, uint64_t volsize)
{
	zvol_state_t *zv = NULL;
	int error;
	uint64_t readonly;

	/*
	 * Create the minor device. If the device already exists, then
	 * just ignore that error.
	 */
	error = zvol_create_minor(name);
	if (error != 0 && error != EEXIST)
		return (error);

	mutex_enter(&zfsdev_state_lock);
	zv = zvol_minor_lookup(name);

	if (zv == NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(ENOENT));
	}

	/*
	 * Multiple OTYP_LYR opens are treated independently (each
	 * incrementing zv_total_opens).
	 */
	error = zvol_open_impl(zv, FWRITE, OTYP_LYR);
	if (error) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(error));
	}

	if (error = zvol_check_volsize(volsize, zv->zv_volblocksize) != 0)
		goto out;

	if (zv->zv_flags & ZVOL_RAW) {
		error = SET_ERROR(ERANGE);
		goto out;
	}

	error = zvol_update_volsize(zv->zv_objset, volsize);

	if (error == 0)
		error = zvol_update_live_volsize(zv, volsize);
out:
	zvol_close_impl(zv, OTYP_LYR);
	mutex_exit(&zfsdev_state_lock);
	return (error);
}

static int
zvol_open_impl(zvol_state_t *zv, int flag, int otyp)
{
	int err = 0;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_total_opens == 0)
		err = zvol_first_open(zv, !(flag & FWRITE));
	if (err) {
		mutex_exit(&zv->zv_state_lock);
		return (err);
	}

	if ((flag & FWRITE) && (zv->zv_flags & ZVOL_RDONLY)) {
		err = SET_ERROR(EROFS);
		goto out;
	}
	if (zv->zv_flags & ZVOL_EXCL) {
		err = SET_ERROR(EBUSY);
		goto out;
	}
	if (flag & FEXCL) {
		if (zv->zv_total_opens != 0) {
			err = SET_ERROR(EBUSY);
			goto out;
		}
		zv->zv_flags |= ZVOL_EXCL;
	}

	if (zv->zv_open_count[otyp] == 0 || otyp == OTYP_LYR) {
		zv->zv_open_count[otyp]++;
		zv->zv_total_opens++;
	}
	mutex_exit(&zv->zv_state_lock);

	return (err);
out:
	if (zv->zv_total_opens == 0)
		zvol_last_close(zv);
	mutex_exit(&zv->zv_state_lock);
	return (err);
}

/*ARGSUSED*/
int
zvol_open(dev_t dev, int flag, int otyp, cred_t *cr)
{
	mutex_enter(&zfsdev_state_lock);

	zvol_state_t *zv = zfsdev_get_soft_state(getminor(dev), ZSST_ZVOL);
	if (zv == NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(ENXIO));
	}

	int err = zvol_open_impl(zv, flag, otyp);

	mutex_exit(&zfsdev_state_lock);
	return (err);
}

static void
zvol_close_impl(zvol_state_t *zv, int otyp)
{
	mutex_enter(&zv->zv_state_lock);
	if (zv->zv_flags & ZVOL_EXCL) {
		ASSERT(zv->zv_total_opens == 1);
		zv->zv_flags &= ~ZVOL_EXCL;
	}

	/*
	 * If the open count is zero, this is a spurious close.
	 * That indicates a bug in the kernel / DDI framework.
	 */
	ASSERT(zv->zv_open_count[otyp] != 0);
	ASSERT(zv->zv_total_opens != 0);

	/*
	 * You may get multiple opens, but only one close.
	 */
	zv->zv_open_count[otyp]--;
	zv->zv_total_opens--;
	VERIFY3S(zv->zv_total_opens, >=, 0);

	if (zv->zv_total_opens == 0) {
		zvol_last_close(zv);
	}
	mutex_exit(&zv->zv_state_lock);
}

/*ARGSUSED*/
int
zvol_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	int error = 0;

	mutex_enter(&zfsdev_state_lock);

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(ENXIO));
	}
	zvol_close_impl(zv, otyp);
	mutex_exit(&zfsdev_state_lock);
	return (error);
}

/* ARGSUSED */
static void
zvol_get_done(zgd_t *zgd, int error)
{
	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	rangelock_exit(zgd->zgd_lr);

	kmem_free(zgd, sizeof (zgd_t));
}

/*
 * Get data to generate a TX_WRITE intent log record.
 */
static int
zvol_get_data(void *arg, lr_write_t *lr, char *buf, struct lwb *lwb, zio_t *zio)
{
	zvol_state_t *zv = arg;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;	/* length of user data */
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3P(zio, !=, NULL);
	ASSERT3U(size, !=, 0);

	zgd = kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_lwb = lwb;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_lr = rangelock_enter(&zv->zv_rangelock, offset, size,
		    RL_READER);
		error = dmu_read_by_dnode(zv->zv_dn, offset, size, buf,
		    DMU_READ_NO_PREFETCH);
	} else { /* indirect write */
		/*
		 * Have to lock the whole block to ensure when it's written out
		 * and its checksum is being calculated that no one can change
		 * the data. Contrarily to zfs_get_data we need not re-check
		 * blocksize after we get the lock because it cannot be changed.
		 */
		size = zv->zv_volblocksize;
		offset = P2ALIGN(offset, size);
		zgd->zgd_lr = rangelock_enter(&zv->zv_rangelock, offset, size,
		    RL_READER);
		error = dmu_buf_hold_by_dnode(zv->zv_dn, offset, zgd, &db,
		    DMU_READ_NO_PREFETCH);
		if (error == 0) {
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zvol_get_done, zgd);

			if (error == 0)
				return (0);
		}
	}

	zvol_get_done(zgd, error);

	return (error);
}

/*
 * zvol_log_write() handles synchronous writes using TX_WRITE ZIL transactions.
 *
 * We store data in the log buffers if it's small enough.
 * Otherwise we will later flush the data out via dmu_sync().
 */
ssize_t zvol_immediate_write_sz = 32768;

static void
zvol_log_write(zvol_state_t *zv, dmu_tx_t *tx, offset_t off, ssize_t resid,
    boolean_t commit)
{
	uint32_t blocksize = zv->zv_volblocksize;
	zilog_t *zilog = zv->zv_zilog;
	itx_wr_state_t write_state;

	if (zil_replaying(zilog, tx))
		return;

	if (zilog->zl_logbias == ZFS_LOGBIAS_THROUGHPUT) {
		write_state = WR_INDIRECT;
	} else if (!spa_has_slogs(zilog->zl_spa) &&
	    resid >= blocksize && blocksize > zvol_immediate_write_sz) {
		write_state = WR_INDIRECT;
	} else if (commit) {
		write_state = WR_COPIED;
	} else {
		write_state = WR_NEED_COPY;
	}

	while (resid) {
		itx_t *itx;
		lr_write_t *lr;
		itx_wr_state_t wr_state = write_state;
		ssize_t len = resid;

		if (wr_state == WR_COPIED && resid > ZIL_MAX_COPIED_DATA)
			wr_state = WR_NEED_COPY;
		else if (wr_state == WR_INDIRECT)
			len = MIN(blocksize - P2PHASE(off, blocksize), resid);

		itx = zil_itx_create(TX_WRITE, sizeof (*lr) +
		    (wr_state == WR_COPIED ? len : 0));
		lr = (lr_write_t *)&itx->itx_lr;
		if (wr_state == WR_COPIED && dmu_read_by_dnode(zv->zv_dn,
		    off, len, lr + 1, DMU_READ_NO_PREFETCH) != 0) {
			zil_itx_destroy(itx);
			itx = zil_itx_create(TX_WRITE, sizeof (*lr));
			lr = (lr_write_t *)&itx->itx_lr;
			wr_state = WR_NEED_COPY;
		}

		itx->itx_wr_state = wr_state;
		lr->lr_foid = ZVOL_OBJ;
		lr->lr_offset = off;
		lr->lr_length = len;
		lr->lr_blkoff = 0;
		BP_ZERO(&lr->lr_blkptr);

		itx->itx_private = zv;

		zil_itx_assign(zilog, itx, tx);

		off += len;
		resid -= len;
	}
}

static int
zvol_dumpio_vdev(vdev_t *vd, caddr_t addr, uint64_t offset,
    uint64_t origoffset, uint64_t size, boolean_t doread, boolean_t isdump)
{
	if (doread && !vdev_readable(vd))
		return (SET_ERROR(EIO));
	if (!doread && !vdev_writeable(vd))
		return (SET_ERROR(EIO));
	if (vd->vdev_ops->vdev_op_dumpio == NULL)
		return (SET_ERROR(EIO));

	return (vd->vdev_ops->vdev_op_dumpio(vd, addr, size,
	    offset, origoffset, doread, isdump));
}

static int
zvol_dumpio(zvol_state_t *zv, caddr_t addr, uint64_t vol_offset, uint64_t size,
    boolean_t doread, boolean_t isdump)
{
	spa_t *spa = dmu_objset_spa(zv->zv_objset);

	ASSERT(zv->zv_flags & ZVOL_PREALLOCED);

	/* Must be sector aligned, and not straddle a block boundary. */
	if (P2PHASE(vol_offset, DEV_BSIZE) || P2PHASE(size, DEV_BSIZE) ||
	    P2BOUNDARY(vol_offset, size, zv->zv_volblocksize)) {
		return (SET_ERROR(EINVAL));
	}
	VERIFY3U(size, <=, zv->zv_volblocksize);
	VERIFY3U(vol_offset / zv->zv_volblocksize, <, zv->zv_ndvas);

	/* Locate the extent this belongs to */
	dva_t *dva = &zv->zv_dvas[vol_offset / zv->zv_volblocksize];
	uint64_t dva_offset = vol_offset % zv->zv_volblocksize;

	if (!ddi_in_panic())
		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));
	VERIFY3P(vd, !=, NULL);

	int error = zvol_dumpio_vdev(vd, addr, DVA_GET_OFFSET(dva) + dva_offset,
	    DVA_GET_OFFSET(dva), size, doread, isdump);

	if (!ddi_in_panic())
		spa_config_exit(spa, SCL_STATE, FTAG);

	return (error);
}

static int
zvol_rawio_vdev(vdev_t *vd, buf_t *bp, uint64_t offset, uint64_t size)
{
	if ((bp->b_flags & B_READ) && !vdev_readable(vd))
		return (SET_ERROR(EIO));
	if (!(bp->b_flags & B_READ) && !vdev_writeable(vd))
		return (SET_ERROR(EIO));
	if (vd->vdev_ops->vdev_op_rawio == NULL)
		return (SET_ERROR(EIO));

	return (vd->vdev_ops->vdev_op_rawio(vd, bp, size, offset));
}

static int
zvol_rawio(zvol_state_t *zv, buf_t *bp, uint64_t vol_offset, uint64_t size)
{
	spa_t *spa = dmu_objset_spa(zv->zv_objset);
	int error;

	/*
	 * Opening a raw volume for the first time triggers several
	 * initialization tasks: preallocating blocks, zeroing them, and
	 * building the DVA mapping. This process occurs asynchronously,
	 * allowing the 'open' call to succeed while initialization continues
	 * in the background. The most time-consuming phase--marked by the
	 * ZVOL_PREALLOCED flag-—involves allocating and zeroing blocks via
	 * writes or trims. While preallocation is a one-time operation for
	 * the lifetime of the zvol, the DVA mapping must be rebuilt as part
	 * every initial open.
	 *
	 * If a consumer opens a raw volume and attempts I/O before
	 * initialization is complete, the system must manage the request
	 * based on the current phase of initialization. If the ZVOL_PREALLOCED
	 * flag is not yet set, the system returns EINPROGRESS. However, if
	 * I/O is attempted after preallocation completes but before the DVA
	 * mapping phase finishes, the application will block.
	 *
	 * We choose to block rather than return an error to prevent a race
	 * condition where the mapping is destroyed. If the system returned an
	 * error, the application might close its file descriptor, triggering
	 * the destruction of the DVA mapping. This would create a cycle: the
	 * application opens the volume, triggers an asynchronous DVA map
	 * build, receives an error on I/O, and closes the descriptor-—
	 * effectively canceling the mapping process before it can finish.
	 *
	 * Note: The ZVOL_PREALLOCED flag is set once during the first open
	 * and is only cleared on the next first open, making a lockless
	 * read safe. This also avoids the cost of acquiring the state mutex
	 * on every I/O.
	 */
	if (!(zv->zv_flags & ZVOL_PREALLOCED)) {
		return (SET_ERROR(EINPROGRESS));
	} else {
		if (zv->zv_zero_thread != NULL) {
			mutex_enter(&zv->zv_state_lock);
			while (zv->zv_zero_thread != NULL) {
				if (!cv_wait_sig(&zv->zv_state_cv,
				    &zv->zv_state_lock)) {
					mutex_exit(&zv->zv_state_lock);
					return (SET_ERROR(EINTR));
				}
			}
			mutex_exit(&zv->zv_state_lock);
		}
		if (zv->zv_zero_error != 0) {
			return (SET_ERROR(zv->zv_zero_error));
		}
	}

	VERIFY3P(zv->zv_dvas, !=, NULL);
	VERIFY3U(vol_offset / zv->zv_volblocksize, <, zv->zv_ndvas);

	/* Must be sector aligned, and not stradle a block boundary. */
	if (P2PHASE(vol_offset, DEV_BSIZE) || P2PHASE(size, DEV_BSIZE) ||
	    P2BOUNDARY(vol_offset, size, zv->zv_volblocksize)) {
		return (SET_ERROR(EINVAL));
	}
	VERIFY3U(size, <=, zv->zv_volblocksize);

	/* Locate the extent this belongs to */
	dva_t *dva = &zv->zv_dvas[vol_offset / zv->zv_volblocksize];
	uint64_t dva_offset = vol_offset % zv->zv_volblocksize;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));
	VERIFY3P(vd, !=, NULL);

	error = zvol_rawio_vdev(vd, bp, DVA_GET_OFFSET(dva) + dva_offset,
	    size);

	spa_config_exit(spa, SCL_STATE, FTAG);

	return (error);
}

static int
zvol_raw_strategy(zvol_state_t *zv, buf_t *bp)
{
	ASSERT(zv->zv_flags & ZVOL_RAW);
	size_t bp_offset = 0;
	size_t resid = bp->b_bcount;
	uint64_t off = ldbtob(bp->b_blkno);
	uint64_t volsize = zv->zv_volsize;
	int error = 0;

	smt_begin_unsafe();

	buf_t child_bp;
	bioinit(&child_bp);
	while (resid != 0 && off < volsize) {
		size_t size = MIN(resid, zvol_maxphys);
		size = MIN(size, P2END(off, zv->zv_volblocksize) - off);

		bioclone(bp, bp_offset, size,
		    0, 0, NULL, &child_bp, KM_SLEEP);

		error = zvol_rawio(zv, &child_bp, off, size);
		if (error)
			break;

		biowait(&child_bp);
		off += size;
		resid -= size;
		bp_offset += size;
	}
	biofini(&child_bp);

	bp->b_resid = resid;
	if (bp->b_resid == bp->b_bcount)
		bioerror(bp, off > volsize ? EINVAL : error);

	biodone(bp);
	smt_end_unsafe();
	return (0);
}

int
zvol_strategy(buf_t *bp)
{
	zfs_soft_state_t *zs = NULL;
	zvol_state_t *zv;
	uint64_t off, volsize;
	size_t resid;
	caddr_t addr;
	objset_t *os;
	int error = 0;
	boolean_t doread = !!(bp->b_flags & B_READ);
	boolean_t is_dumpified;
	boolean_t commit;

	if (getminor(bp->b_edev) == 0) {
		error = SET_ERROR(EINVAL);
	} else {
		zs = ddi_get_soft_state(zfsdev_state, getminor(bp->b_edev));
		if (zs == NULL)
			error = SET_ERROR(ENXIO);
		else if (zs->zss_type != ZSST_ZVOL)
			error = SET_ERROR(EINVAL);
	}

	if (error) {
		bioerror(bp, error);
		biodone(bp);
		return (0);
	}

	zv = zs->zss_data;

	if (!(bp->b_flags & B_READ) && (zv->zv_flags & ZVOL_RDONLY)) {
		bioerror(bp, EROFS);
		biodone(bp);
		return (0);
	}

	off = ldbtob(bp->b_blkno);
	volsize = zv->zv_volsize;

	os = zv->zv_objset;
	ASSERT(os != NULL);

	resid = bp->b_bcount;
	if (resid > 0 && off >= volsize) {
		bioerror(bp, EIO);
		biodone(bp);
		return (0);
	}

	if (zv->zv_flags & ZVOL_RAW) {
		return (zvol_raw_strategy(zv, bp));
	}

	is_dumpified = zv->zv_flags & ZVOL_DUMPIFIED;
	bp_mapin(bp);
	addr = bp->b_un.b_addr;

	commit = ((!(bp->b_flags & B_ASYNC) &&
	    !(zv->zv_flags & ZVOL_WCE)) ||
	    zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS) &&
	    !doread && !is_dumpified;

	smt_begin_unsafe();

	/*
	 * There must be no buffer changes when doing a dmu_sync() because
	 * we can't change the data whilst calculating the checksum.
	 */
	locked_range_t *lr = rangelock_enter(&zv->zv_rangelock, off, resid,
	    doread ? RL_READER : RL_WRITER);

	while (resid != 0 && off < volsize) {
		size_t size = MIN(resid, zvol_maxphys);
		if (is_dumpified) {
			ASSERT3P(zv->zv_dvas, !=, NULL);
			ASSERT(zv->zv_flags & ZVOL_PREALLOCED);
			size = MIN(size, P2END(off, zv->zv_volblocksize) - off);
			error = zvol_dumpio(zv, addr, off, size,
			    doread, B_FALSE);
		} else if (doread) {
			error = dmu_read(os, ZVOL_OBJ, off, size, addr,
			    DMU_READ_PREFETCH);
		} else {
			dmu_tx_t *tx = dmu_tx_create(os);
			dmu_tx_hold_write(tx, ZVOL_OBJ, off, size);
			error = dmu_tx_assign(tx, TXG_WAIT);
			if (error) {
				dmu_tx_abort(tx);
			} else {
				dmu_write(os, ZVOL_OBJ, off, size, addr, tx);
				zvol_log_write(zv, tx, off, size, commit);
				dmu_tx_commit(tx);
			}
		}
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
		off += size;
		addr += size;
		resid -= size;
	}
	rangelock_exit(lr);

	if ((bp->b_resid = resid) == bp->b_bcount)
		bioerror(bp, off > volsize ? EINVAL : error);

	if (commit)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);
	biodone(bp);

	smt_end_unsafe();

	return (0);
}

/*
 * Set the buffer count to the zvol maximum transfer.
 * Using our own routine instead of the default minphys()
 * means that for larger writes we write bigger buffers on X86
 * (128K instead of 56K) and flush the disk write cache less often
 * (every zvol_maxphys - currently 1MB) instead of minphys (currently
 * 56K on X86 and 128K on sparc).
 */
void
zvol_minphys(struct buf *bp)
{
	if (bp->b_bcount > zvol_maxphys)
		bp->b_bcount = zvol_maxphys;
}

int
zvol_dump(dev_t dev, caddr_t addr, daddr_t blkno, int nblocks)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	int error = 0;
	uint64_t size;
	uint64_t boff;
	uint64_t resid;

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL)
		return (SET_ERROR(ENXIO));

	if ((zv->zv_flags & ZVOL_DUMPIFIED) == 0)
		return (SET_ERROR(EINVAL));

	boff = ldbtob(blkno);
	resid = ldbtob(nblocks);

	VERIFY3U(boff + resid, <=, zv->zv_volsize);

	while (resid) {
		size = MIN(resid, P2END(boff, zv->zv_volblocksize) - boff);
		error = zvol_dumpio(zv, addr, boff, size, B_FALSE, B_TRUE);
		if (error)
			break;
		boff += size;
		addr += size;
		resid -= size;
	}

	return (error);
}

/*ARGSUSED*/
int
zvol_aread(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	minor_t minor = getminor(dev);
	uio_t *uio = aio->aio_uio;
	zvol_state_t *zv;
	uint64_t volsize;
	int error = 0;

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL)
		return (SET_ERROR(ENXIO));

	volsize = zv->zv_volsize;
	if (uio->uio_resid > 0 &&
	    (uio->uio_loffset < 0 || uio->uio_loffset >= volsize))
		return (SET_ERROR(EINVAL));

	if (zv->zv_flags & (ZVOL_DUMPIFIED | ZVOL_RAW)) {
		error = aphysio(zvol_strategy, anocancel, dev, B_READ,
		    zvol_minphys, aio);
		return (error);
	} else {
		return (SET_ERROR(ENOTSUP));
	}
}


/*ARGSUSED*/
int
zvol_read(dev_t dev, uio_t *uio, cred_t *cr)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	uint64_t volsize;
	int error = 0;

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL)
		return (SET_ERROR(ENXIO));

	volsize = zv->zv_volsize;
	if (uio->uio_resid > 0 &&
	    (uio->uio_loffset < 0 || uio->uio_loffset >= volsize))
		return (SET_ERROR(EIO));

	if (zv->zv_flags & (ZVOL_DUMPIFIED | ZVOL_RAW)) {
		error = physio(zvol_strategy, NULL, dev, B_READ,
		    zvol_minphys, uio);
		return (error);
	}

	smt_begin_unsafe();

	locked_range_t *lr = rangelock_enter(&zv->zv_rangelock,
	    uio->uio_loffset, uio->uio_resid, RL_READER);
	while (uio->uio_resid > 0 && uio->uio_loffset < volsize) {
		uint64_t bytes = MIN(uio->uio_resid, DMU_MAX_ACCESS >> 1);

		/* don't read past the end */
		if (bytes > volsize - uio->uio_loffset)
			bytes = volsize - uio->uio_loffset;

		error =  dmu_read_uio(zv->zv_objset, ZVOL_OBJ, uio, bytes);
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
	}
	rangelock_exit(lr);

	smt_end_unsafe();

	return (error);
}

/*ARGSUSED*/
int
zvol_awrite(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	minor_t minor = getminor(dev);
	uio_t *uio = aio->aio_uio;
	zvol_state_t *zv;
	uint64_t volsize;
	int error = 0;
	boolean_t commit;

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL)
		return (SET_ERROR(ENXIO));

	volsize = zv->zv_volsize;
	if (uio->uio_resid > 0 &&
	    (uio->uio_loffset < 0 || uio->uio_loffset >= volsize))
		return (SET_ERROR(EINVAL));

	if (zv->zv_flags & (ZVOL_DUMPIFIED | ZVOL_RAW)) {
		error = aphysio(zvol_strategy, anocancel, dev, B_WRITE,
		    zvol_minphys, aio);
		return (error);
	} else {
		return (SET_ERROR(ENOTSUP));
	}
}

/*ARGSUSED*/
int
zvol_write(dev_t dev, uio_t *uio, cred_t *cr)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	uint64_t volsize;
	int error = 0;
	boolean_t commit;

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL)
		return (SET_ERROR(ENXIO));

	volsize = zv->zv_volsize;
	if (uio->uio_resid > 0 &&
	    (uio->uio_loffset < 0 || uio->uio_loffset >= volsize))
		return (SET_ERROR(EIO));

	if (zv->zv_flags & (ZVOL_DUMPIFIED | ZVOL_RAW)) {
		error = physio(zvol_strategy, NULL, dev, B_WRITE,
		    zvol_minphys, uio);
		return (error);
	}

	smt_begin_unsafe();

	commit = !(zv->zv_flags & ZVOL_WCE) ||
	    zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;

	locked_range_t *lr = rangelock_enter(&zv->zv_rangelock,
	    uio->uio_loffset, uio->uio_resid, RL_WRITER);
	while (uio->uio_resid > 0 && uio->uio_loffset < volsize) {
		uint64_t bytes = MIN(uio->uio_resid, DMU_MAX_ACCESS >> 1);
		uint64_t off = uio->uio_loffset;
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

		if (bytes > volsize - off)	/* don't write past the end */
			bytes = volsize - off;

		dmu_tx_hold_write_by_dnode(tx, zv->zv_dn, off, bytes);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_write_uio_dnode(zv->zv_dn, uio, bytes, tx);
		if (error == 0)
			zvol_log_write(zv, tx, off, bytes, commit);
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	rangelock_exit(lr);

	if (commit)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	smt_end_unsafe();

	return (error);
}

int
zvol_getefi(void *arg, int flag, uint64_t vs, uint8_t bs)
{
	struct uuid uuid = EFI_RESERVED;
	efi_gpe_t gpe = { 0 };
	uint32_t crc;
	dk_efi_t efi;
	int length;
	char *ptr;

	if (ddi_copyin(arg, &efi, sizeof (dk_efi_t), flag))
		return (SET_ERROR(EFAULT));
	ptr = (char *)(uintptr_t)efi.dki_data_64;
	length = efi.dki_length;
	/*
	 * Some clients may attempt to request a PMBR for the
	 * zvol.  Currently this interface will return EINVAL to
	 * such requests.  These requests could be supported by
	 * adding a check for lba == 0 and consing up an appropriate
	 * PMBR.
	 */
	if (efi.dki_lba < 1 || efi.dki_lba > 2 || length <= 0)
		return (SET_ERROR(EINVAL));

	gpe.efi_gpe_StartingLBA = LE_64(34ULL);
	gpe.efi_gpe_EndingLBA = LE_64((vs >> bs) - 1);
	UUID_LE_CONVERT(gpe.efi_gpe_PartitionTypeGUID, uuid);

	if (efi.dki_lba == 1) {
		efi_gpt_t gpt = { 0 };

		gpt.efi_gpt_Signature = LE_64(EFI_SIGNATURE);
		gpt.efi_gpt_Revision = LE_32(EFI_VERSION_CURRENT);
		gpt.efi_gpt_HeaderSize = LE_32(EFI_HEADER_SIZE);
		gpt.efi_gpt_MyLBA = LE_64(1ULL);
		gpt.efi_gpt_FirstUsableLBA = LE_64(34ULL);
		gpt.efi_gpt_LastUsableLBA = LE_64((vs >> bs) - 1);
		gpt.efi_gpt_PartitionEntryLBA = LE_64(2ULL);
		gpt.efi_gpt_NumberOfPartitionEntries = LE_32(1);
		gpt.efi_gpt_SizeOfPartitionEntry =
		    LE_32(sizeof (efi_gpe_t));
		CRC32(crc, &gpe, sizeof (gpe), -1U, crc32_table);
		gpt.efi_gpt_PartitionEntryArrayCRC32 = LE_32(~crc);
		CRC32(crc, &gpt, EFI_HEADER_SIZE, -1U, crc32_table);
		gpt.efi_gpt_HeaderCRC32 = LE_32(~crc);
		if (ddi_copyout(&gpt, ptr, MIN(sizeof (gpt), length),
		    flag))
			return (SET_ERROR(EFAULT));
		ptr += sizeof (gpt);
		length -= sizeof (gpt);
	}
	if (length > 0 && ddi_copyout(&gpe, ptr, MIN(sizeof (gpe),
	    length), flag))
		return (SET_ERROR(EFAULT));
	return (0);
}

/*
 * BEGIN entry points to allow external callers access to the volume.
 */
/*
 * Return the volume parameters needed for access from an external caller.
 * These values are invariant as long as the volume is held open.
 */
int
zvol_get_volume_params(minor_t minor, uint64_t *blksize,
    uint64_t *max_xfer_len, void **minor_hdl, void **objset_hdl, void **zil_hdl,
    void **rl_hdl, void **dnode_hdl)
{
	zvol_state_t *zv;

	zv = zfsdev_get_soft_state(minor, ZSST_ZVOL);
	if (zv == NULL)
		return (SET_ERROR(ENXIO));
	if (zv->zv_flags & ZVOL_DUMPIFIED)
		return (SET_ERROR(ENXIO));

	ASSERT(blksize && max_xfer_len && minor_hdl &&
	    objset_hdl && zil_hdl && rl_hdl && dnode_hdl);

	*blksize = zv->zv_volblocksize;
	*max_xfer_len = (uint64_t)zvol_maxphys;
	*minor_hdl = zv;
	*objset_hdl = zv->zv_objset;
	*zil_hdl = zv->zv_zilog;
	*rl_hdl = &zv->zv_rangelock;
	*dnode_hdl = zv->zv_dn;
	return (0);
}

/*
 * Return the current volume size to an external caller.
 * The size can change while the volume is open.
 */
uint64_t
zvol_get_volume_size(void *minor_hdl)
{
	zvol_state_t *zv = minor_hdl;

	return (zv->zv_volsize);
}

/*
 * Return the current WCE setting to an external caller.
 * The WCE setting can change while the volume is open.
 */
int
zvol_get_volume_wce(void *minor_hdl)
{
	zvol_state_t *zv = minor_hdl;

	return ((zv->zv_flags & ZVOL_WCE) ? 1 : 0);
}

/*
 * Entry point for external callers to zvol_log_write
 */
void
zvol_log_write_minor(void *minor_hdl, dmu_tx_t *tx, offset_t off, ssize_t resid,
    boolean_t commit)
{
	zvol_state_t *zv = minor_hdl;

	zvol_log_write(zv, tx, off, resid, commit);
}
/*
 * END entry points to allow external callers access to the volume.
 */

/*
 * Log a DKIOCFREE/free-long-range to the ZIL with TX_TRUNCATE.
 */
static void
zvol_log_truncate(zvol_state_t *zv, dmu_tx_t *tx, uint64_t off, uint64_t len)
{
	itx_t *itx;
	lr_truncate_t *lr;
	zilog_t *zilog = zv->zv_zilog;

	if (zil_replaying(zilog, tx))
		return;

	itx = zil_itx_create(TX_TRUNCATE, sizeof (*lr));
	lr = (lr_truncate_t *)&itx->itx_lr;
	lr->lr_foid = ZVOL_OBJ;
	lr->lr_offset = off;
	lr->lr_length = len;

	zil_itx_assign(zilog, itx, tx);
}

/*
 * Dirtbag ioctls to support mkfs(8) for UFS filesystems.  See dkio(4I).
 * Also a dirtbag dkio ioctl for unmap/free-block functionality.
 */
/*ARGSUSED*/
int
zvol_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr, int *rvalp)
{
	zvol_state_t *zv;
	struct dk_callback *dkc;
	int i, error = 0;
	locked_range_t *lr;

	mutex_enter(&zfsdev_state_lock);

	zv = zfsdev_get_soft_state(getminor(dev), ZSST_ZVOL);

	if (zv == NULL) {
		mutex_exit(&zfsdev_state_lock);
		return (SET_ERROR(ENXIO));
	}
	ASSERT(zv->zv_total_opens > 0);

	switch (cmd) {

	case DKIOCRAWVOLSTATUS:
	{
		dk_rawvol_status_t drs;

		int error = 0;
		if (!(zv->zv_flags & ZVOL_RAW)) {
			mutex_exit(&zfsdev_state_lock);
			return (SET_ERROR(ENOTSUP));
		}

		mutex_enter(&zv->zv_state_lock);

		bzero(&drs, sizeof (drs));
		drs.drs_vers = 1;

		/*
		 * The first open of a raw volume will always start
		 * the zero thread. Once the thread starts to run it
		 * will set the ZVOL_ZERO_STARTED flag and that flag will
		 * remain set even after the zvol zero thread has exited.
		 * We want to wait till the zvol zero thread  has had a
		 * chance to run before we try to get its status.
		 */
		while (!(zv->zv_flags & ZVOL_ZERO_STARTED)) {
			if (cv_wait_sig(&zv->zv_state_cv,
			    &zv->zv_state_lock) == 0) {
				error = SET_ERROR(EINTR);
				goto out;
			}
		}
		drs.drs_zoff = zv->zv_zero_off;
		drs.drs_len = zv->zv_volsize;

		if (zv->zv_zero_thread == NULL) {
			drs.drs_status = zv->zv_zero_error;
		} else {
			drs.drs_status = EINPROGRESS;
		}
out:
		mutex_exit(&zv->zv_state_lock);
		mutex_exit(&zfsdev_state_lock);

		if (ddi_copyout(&drs, (void *)arg, sizeof (drs), flag))
			error = SET_ERROR(EFAULT);
		return (error);
	}

	case DKIOCRAWVOLSTOP:
	{
		if (!(zv->zv_flags & ZVOL_RAW)) {
			mutex_exit(&zfsdev_state_lock);
			return (SET_ERROR(ENOTSUP));
		}

		mutex_enter(&zv->zv_state_lock);

		/*
		 * The first open of a raw volume will always start
		 * the zero thread. Once the thread starts to run it
		 * will set the ZVOL_ZERO_STARTED flag and that flag will
		 * remain set even after the zvol zero thread has exited.
		 * Make sure that the zero thread has started so that
		 * we can signal it to stop.
		 */
		while (!(zv->zv_flags & ZVOL_ZERO_STARTED)) {
			if (cv_wait_sig(&zv->zv_state_cv,
			    &zv->zv_state_lock) == 0) {
				mutex_exit(&zv->zv_state_lock);
				mutex_exit(&zfsdev_state_lock);
				return (SET_ERROR(EINTR));
			}
		}

		zv->zv_zero_exit_wanted = B_TRUE;
		while (zv->zv_zero_thread != NULL) {
			if (cv_wait_sig(&zv->zv_state_cv,
			    &zv->zv_state_lock) == 0) {
				mutex_exit(&zv->zv_state_lock);
				mutex_exit(&zfsdev_state_lock);
				return (SET_ERROR(EINTR));
			}
		}
		mutex_exit(&zv->zv_state_lock);
		mutex_exit(&zfsdev_state_lock);
		return (0);
	}

	case DKIOCINFO:
	{
		struct dk_cinfo dki;

		bzero(&dki, sizeof (dki));
		(void) strcpy(dki.dki_cname, "zvol");
		(void) strcpy(dki.dki_dname, "zvol");
		dki.dki_ctype = DKC_UNKNOWN;
		dki.dki_unit = getminor(dev);
		dki.dki_maxtransfer =
		    1 << (SPA_OLD_MAXBLOCKSHIFT - zv->zv_min_bs);
		mutex_exit(&zfsdev_state_lock);
		if (ddi_copyout(&dki, (void *)arg, sizeof (dki), flag))
			error = SET_ERROR(EFAULT);
		return (error);
	}

	case DKIOCGMEDIAINFO:
	{
		struct dk_minfo dkm;

		bzero(&dkm, sizeof (dkm));
		dkm.dki_lbsize = 1U << zv->zv_min_bs;
		dkm.dki_capacity = zv->zv_volsize >> zv->zv_min_bs;
		dkm.dki_media_type = DK_UNKNOWN;
		mutex_exit(&zfsdev_state_lock);
		if (ddi_copyout(&dkm, (void *)arg, sizeof (dkm), flag))
			error = SET_ERROR(EFAULT);
		return (error);
	}

	case DKIOCGMEDIAINFOEXT:
	{
		struct dk_minfo_ext dkmext;
		size_t len;

		bzero(&dkmext, sizeof (dkmext));
		dkmext.dki_lbsize = 1U << zv->zv_min_bs;
		if (zv->zv_flags & ZVOL_RAW) {
			dkmext.dki_pbsize = dkmext.dki_lbsize;
		} else {
			dkmext.dki_pbsize = zv->zv_volblocksize;
		}
		dkmext.dki_capacity = zv->zv_volsize >> zv->zv_min_bs;
		dkmext.dki_media_type = DK_UNKNOWN;
		mutex_exit(&zfsdev_state_lock);

		switch (ddi_model_convert_from(flag & FMODELS)) {
		case DDI_MODEL_ILP32:
			len = sizeof (struct dk_minfo_ext32);
			break;
		default:
			len = sizeof (struct dk_minfo_ext);
			break;
		}

		if (ddi_copyout(&dkmext, (void *)arg, len, flag))
			error = SET_ERROR(EFAULT);
		return (error);
	}

	case DKIOCGETEFI:
	{
		uint64_t vs = zv->zv_volsize;
		uint8_t bs = zv->zv_min_bs;

		mutex_exit(&zfsdev_state_lock);
		error = zvol_getefi((void *)arg, flag, vs, bs);
		return (error);
	}

	case DKIOCFLUSHWRITECACHE:
		dkc = (struct dk_callback *)arg;
		mutex_exit(&zfsdev_state_lock);

		smt_begin_unsafe();

		zil_commit(zv->zv_zilog, ZVOL_OBJ);
		if ((flag & FKIOCTL) && dkc != NULL && dkc->dkc_callback) {
			(*dkc->dkc_callback)(dkc->dkc_cookie, error);
			error = 0;
		}

		smt_end_unsafe();

		return (error);

	case DKIOCGETWCE:
	{
		int wce = (zv->zv_flags & ZVOL_WCE) ? 1 : 0;
		if (ddi_copyout(&wce, (void *)arg, sizeof (int),
		    flag))
			error = SET_ERROR(EFAULT);
		break;
	}
	case DKIOCSETWCE:
	{
		int wce;
		if (ddi_copyin((void *)arg, &wce, sizeof (int),
		    flag)) {
			error = SET_ERROR(EFAULT);
			break;
		}
		if (wce) {
			zv->zv_flags |= ZVOL_WCE;
			mutex_exit(&zfsdev_state_lock);
		} else {
			zv->zv_flags &= ~ZVOL_WCE;
			mutex_exit(&zfsdev_state_lock);
			smt_begin_unsafe();
			zil_commit(zv->zv_zilog, ZVOL_OBJ);
			smt_end_unsafe();
		}
		return (0);
	}

	case DKIOCGGEOM:
	case DKIOCGVTOC:
		/*
		 * commands using these (like prtvtoc) expect ENOTSUP
		 * since we're emulating an EFI label
		 */
		error = SET_ERROR(ENOTSUP);
		break;

	case DKIOCDUMPINIT:
		lr = rangelock_enter(&zv->zv_rangelock, 0, zv->zv_volsize,
		    RL_WRITER);
		error = zvol_dumpify(zv);
		rangelock_exit(lr);
		break;

	case DKIOCDUMPFINI:
		if (!(zv->zv_flags & ZVOL_DUMPIFIED))
			break;
		lr = rangelock_enter(&zv->zv_rangelock, 0, zv->zv_volsize,
		    RL_WRITER);
		error = zvol_dump_fini(zv);
		rangelock_exit(lr);
		break;

	case DKIOCFREE:
	{
		dkioc_free_list_t *dfl;
		dmu_tx_t *tx;

		if (!zvol_unmap_enabled || zv->zv_flags & ZVOL_RAW) {
			mutex_exit(&zfsdev_state_lock);
			return (SET_ERROR(ENOTSUP));
		}

		if (!(flag & FKIOCTL)) {
			error = dfl_copyin((void *)arg, &dfl, flag, KM_SLEEP);
			if (error != 0)
				break;
		} else {
			dfl = (dkioc_free_list_t *)arg;
			ASSERT3U(dfl->dfl_num_exts, <=, DFL_COPYIN_MAX_EXTS);
			if (dfl->dfl_num_exts > DFL_COPYIN_MAX_EXTS) {
				error = SET_ERROR(EINVAL);
				break;
			}
		}

		mutex_exit(&zfsdev_state_lock);

		smt_begin_unsafe();

		for (int i = 0; i < dfl->dfl_num_exts; i++) {
			uint64_t start = dfl->dfl_exts[i].dfle_start,
			    length = dfl->dfl_exts[i].dfle_length,
			    end = start + length;

			/*
			 * Apply Postel's Law to length-checking.  If they
			 * overshoot, just blank out until the end, if there's
			 * a need to blank out anything.
			 */
			if (start >= zv->zv_volsize)
				continue;	/* No need to do anything... */
			if (end > zv->zv_volsize) {
				end = DMU_OBJECT_END;
				length = end - start;
			}

			lr = rangelock_enter(&zv->zv_rangelock, start, length,
			    RL_WRITER);
			tx = dmu_tx_create(zv->zv_objset);
			error = dmu_tx_assign(tx, TXG_WAIT);
			if (error != 0) {
				dmu_tx_abort(tx);
			} else {
				zvol_log_truncate(zv, tx, start, length);
				dmu_tx_commit(tx);
				error = dmu_free_long_range(zv->zv_objset,
				    ZVOL_OBJ, start, length);
			}

			rangelock_exit(lr);

			if (error != 0)
				break;
		}

		/*
		 * If the write-cache is disabled, 'sync' property
		 * is set to 'always', or if the caller is asking for
		 * a synchronous free, commit this operation to the zil.
		 * This will sync any previous uncommitted writes to the
		 * zvol object.
		 * Can be overridden by the zvol_unmap_sync_enabled tunable.
		 */
		if ((error == 0) && zvol_unmap_sync_enabled &&
		    (!(zv->zv_flags & ZVOL_WCE) ||
		    (zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS) ||
		    (dfl->dfl_flags & DF_WAIT_SYNC))) {
			zil_commit(zv->zv_zilog, ZVOL_OBJ);
		}

		if (!(flag & FKIOCTL))
			dfl_free(dfl);

		smt_end_unsafe();

		return (error);
	}

	case DKIOC_CANFREE:
		i = zvol_unmap_enabled ? 1 : 0;
		if (zv->zv_flags & ZVOL_RAW)
			i = 0;
		if (ddi_copyout(&i, (void *)arg, sizeof (int), flag) != 0) {
			error = EFAULT;
		} else {
			error = 0;
		}
		break;

	default:
		error = SET_ERROR(ENOTTY);
		break;

	}
	mutex_exit(&zfsdev_state_lock);
	return (error);
}

int
zvol_busy(void)
{
	return (zvol_minors != 0);
}

void
zvol_init(void)
{
	VERIFY(ddi_soft_state_init(&zfsdev_state, sizeof (zfs_soft_state_t),
	    1) == 0);
	mutex_init(&zfsdev_state_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
zvol_fini(void)
{
	mutex_destroy(&zfsdev_state_lock);
	ddi_soft_state_fini(&zfsdev_state);
}

/*ARGSUSED*/
static int
zfs_mvdev_dump_feature_check(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (spa_feature_is_active(spa, SPA_FEATURE_MULTI_VDEV_CRASH_DUMP))
		return (1);
	return (0);
}

/*ARGSUSED*/
static void
zfs_mvdev_dump_activate_feature_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	spa_feature_incr(spa, SPA_FEATURE_MULTI_VDEV_CRASH_DUMP, tx);
}

int
zvol_raw_volume_init(objset_t *os, nvlist_t *nvprops)
{
	dmu_tx_t *tx;
	int error;
	spa_t *spa = dmu_objset_spa(os);
	uint64_t version = spa_version(spa);
	nvlist_t *nv = NULL;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	/*
	 * If MULTI_VDEV_CRASH_DUMP is active, use the NOPARITY checksum
	 * function.  Otherwise, use the old default -- OFF.
	 */
	uint64_t checksum = spa_feature_is_active(spa,
	    SPA_FEATURE_MULTI_VDEV_CRASH_DUMP) ? ZIO_CHECKSUM_NOPARITY :
	    ZIO_CHECKSUM_OFF;

	nv = fnvlist_alloc();
	fnvlist_add_uint64(nv, zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 0);
	fnvlist_add_uint64(nv, zfs_prop_to_name(ZFS_PROP_COMPRESSION),
	    ZIO_COMPRESS_OFF);
	fnvlist_add_uint64(nv, zfs_prop_to_name(ZFS_PROP_CHECKSUM),
	    checksum);
	if (version >= SPA_VERSION_DEDUP) {
		fnvlist_add_uint64(nv, zfs_prop_to_name(ZFS_PROP_DEDUP),
		    ZIO_CHECKSUM_OFF);
	}

	char osname[ZFS_MAX_DATASET_NAME_LEN];
	dmu_objset_name(os, osname);
	error = zfs_set_prop_nvlist(osname, ZPROP_SRC_LOCAL,
	    nv, NULL);

	/*
	 * Remove overridden properties from the nvlist so the standard
	 * property-handling logic does not attempt to set them.
	 */
	if (error == 0 && nvprops != NULL) {
		nvlist_remove_all(nvprops,
		    zfs_prop_to_name(ZFS_PROP_REFRESERVATION));
		nvlist_remove_all(nvprops,
		    zfs_prop_to_name(ZFS_PROP_COMPRESSION));
		nvlist_remove_all(nvprops, zfs_prop_to_name(ZFS_PROP_CHECKSUM));
		if (version >= SPA_VERSION_DEDUP) {
			nvlist_remove_all(nvprops,
			    zfs_prop_to_name(ZFS_PROP_DEDUP));
		}
	}
	nvlist_free(nv);
	return (error);
}

static int
zvol_dump_init(zvol_state_t *zv, boolean_t resize)
{
	dmu_tx_t *tx;
	int error = 0;
	objset_t *os = zv->zv_objset;
	spa_t *spa = dmu_objset_spa(os);
	vdev_t *vd = spa->spa_root_vdev;
	uint64_t version = spa_version(spa);
	uint64_t checksum, compress, refresrv, vbs, dedup;

	ASSERT(vd->vdev_ops == &vdev_root_ops);

	error = dmu_free_long_range(os, ZVOL_OBJ, 0,
	    DMU_OBJECT_END);
	if (error != 0)
		return (error);
	/* wait for dmu_free_long_range to actually free the blocks */
	txg_wait_synced(dmu_objset_pool(os), 0);


	/*
	 * If the pool on which the dump device is being initialized has more
	 * than one child vdev, check that the MULTI_VDEV_CRASH_DUMP feature is
	 * enabled.  If so, bump that feature's counter to indicate that the
	 * feature is active. We also check the vdev type to handle the
	 * following case:
	 *   # zpool create test raidz disk1 disk2 disk3
	 *   Now have spa_root_vdev->vdev_children == 1 (the raidz vdev),
	 *   the raidz vdev itself has 3 children.
	 */
	if (vd->vdev_children > 1 || vd->vdev_ops == &vdev_raidz_ops) {
		if (!spa_feature_is_enabled(spa,
		    SPA_FEATURE_MULTI_VDEV_CRASH_DUMP))
			return (SET_ERROR(ENOTSUP));
		(void) dsl_sync_task(spa_name(spa),
		    zfs_mvdev_dump_feature_check,
		    zfs_mvdev_dump_activate_feature_sync, NULL,
		    2, ZFS_SPACE_CHECK_RESERVED);
	}

	if (!resize) {
		error = dsl_prop_get_integer(zv->zv_name,
		    zfs_prop_to_name(ZFS_PROP_COMPRESSION), &compress, NULL);
		if (error == 0) {
			error = dsl_prop_get_integer(zv->zv_name,
			    zfs_prop_to_name(ZFS_PROP_CHECKSUM), &checksum,
			    NULL);
		}
		if (error == 0) {
			error = dsl_prop_get_integer(zv->zv_name,
			    zfs_prop_to_name(ZFS_PROP_REFRESERVATION),
			    &refresrv, NULL);
		}
		if (error == 0) {
			error = dsl_prop_get_integer(zv->zv_name,
			    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &vbs,
			    NULL);
		}
		if (version >= SPA_VERSION_DEDUP && error == 0) {
			error = dsl_prop_get_integer(zv->zv_name,
			    zfs_prop_to_name(ZFS_PROP_DEDUP), &dedup, NULL);
		}
	}
	if (error != 0)
		return (error);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	dmu_tx_hold_bonus(tx, ZVOL_OBJ);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		return (error);
	}

	/*
	 * If we are resizing the dump device then we only need to
	 * update the refreservation to match the newly updated
	 * zvolsize. Otherwise, we save off the original state of the
	 * zvol so that we can restore them if the zvol is ever undumpified.
	 */
	if (resize) {
		uint64_t volsize;
		error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
		if (error == 0) {
			error = zap_update(os, ZVOL_ZAP_OBJ,
			    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 8, 1,
			    &volsize, tx);
		}
	} else {
		error = zap_update(os, ZVOL_ZAP_OBJ,
		    zfs_prop_to_name(ZFS_PROP_COMPRESSION), 8, 1,
		    &compress, tx);
		if (error == 0) {
			error = zap_update(os, ZVOL_ZAP_OBJ,
			    zfs_prop_to_name(ZFS_PROP_CHECKSUM), 8, 1,
			    &checksum, tx);
		}
		if (error == 0) {
			error = zap_update(os, ZVOL_ZAP_OBJ,
			    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 8, 1,
			    &refresrv, tx);
		}
		if (error == 0) {
			error = zap_update(os, ZVOL_ZAP_OBJ,
			    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), 8, 1,
			    &vbs, tx);
		}
		if (error == 0) {
			error = dmu_object_set_blocksize(
			    os, ZVOL_OBJ, SPA_OLD_MAXBLOCKSIZE, 0, tx);
		}
		if (version >= SPA_VERSION_DEDUP && error == 0) {
			error = zap_update(os, ZVOL_ZAP_OBJ,
			    zfs_prop_to_name(ZFS_PROP_DEDUP), 8, 1,
			    &dedup, tx);
		}
	}
	dmu_tx_commit(tx);
	/*
	 * We only need to update the zvol's property if we are initializing
	 * the dump area for the first time.
	 */
	if (!resize) {
		return (zvol_raw_volume_init(zv->zv_objset, NULL));
	}
	return (0);
}

static int
zvol_dumpify(zvol_state_t *zv)
{
	int error = 0;
	uint64_t dumpsize = 0;
	dmu_tx_t *tx;
	objset_t *os = zv->zv_objset;

	if (zv->zv_flags & ZVOL_RDONLY)
		return (SET_ERROR(EROFS));

	if (os->os_encrypted || zv->zv_flags & ZVOL_RAW)
		return (SET_ERROR(ENOTSUP));

	if (zap_lookup(zv->zv_objset, ZVOL_ZAP_OBJ, ZVOL_DUMPSIZE,
	    8, 1, &dumpsize) != 0 || dumpsize != zv->zv_volsize) {
		boolean_t resize = (dumpsize > 0);

		if ((error = zvol_dump_init(zv, resize)) != 0) {
			(void) zvol_dump_fini(zv);
			return (error);
		}
		zv->zv_volblocksize = SPA_OLD_MAXBLOCKSIZE;
	}
	zv->zv_flags |= ZVOL_DUMPIFIED;

	mutex_enter(&zv->zv_state_lock);
	error = zvol_prealloc(zv);
	if (error == 0)  {
		while (zv->zv_zero_thread != NULL) {
			cv_wait(&zv->zv_state_cv, &zv->zv_state_lock);
		}
		error = zv->zv_zero_error;
	}
	mutex_exit(&zv->zv_state_lock);
	if (error) {
		(void) zvol_dump_fini(zv);
		return (error);
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		(void) zvol_dump_fini(zv);
		return (error);
	}

	error = zap_update(os, ZVOL_ZAP_OBJ, ZVOL_DUMPSIZE, 8, 1,
	    &zv->zv_volsize, tx);
	dmu_tx_commit(tx);

	if (error) {
		(void) zvol_dump_fini(zv);
		return (error);
	}

	txg_wait_synced(dmu_objset_pool(os), 0);
	return (0);
}

static int
zvol_dump_fini(zvol_state_t *zv)
{
	dmu_tx_t *tx;
	objset_t *os = zv->zv_objset;
	nvlist_t *nv;
	int error = 0;
	uint64_t checksum, compress, refresrv, vbs, dedup;
	uint64_t version = spa_version(dmu_objset_spa(os));

	/*
	 * Attempt to restore the zvol back to its pre-dumpified state.
	 * This is a best-effort attempt as it's possible that not all
	 * of these properties were initialized during the dumpify process
	 * (i.e. error during zvol_dump_init).
	 */

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}
	(void) zap_remove(os, ZVOL_ZAP_OBJ, ZVOL_DUMPSIZE, tx);
	dmu_tx_commit(tx);

	(void) zap_lookup(os, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_CHECKSUM), 8, 1, &checksum);
	(void) zap_lookup(os, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_COMPRESSION), 8, 1, &compress);
	(void) zap_lookup(os, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 8, 1, &refresrv);
	(void) zap_lookup(os, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), 8, 1, &vbs);

	nv = fnvlist_alloc();
	(void) nvlist_add_uint64(nv,
	    zfs_prop_to_name(ZFS_PROP_CHECKSUM), checksum);
	(void) nvlist_add_uint64(nv,
	    zfs_prop_to_name(ZFS_PROP_COMPRESSION), compress);
	(void) nvlist_add_uint64(nv,
	    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), refresrv);
	if (version >= SPA_VERSION_DEDUP &&
	    zap_lookup(os, ZVOL_ZAP_OBJ, zfs_prop_to_name(ZFS_PROP_DEDUP),
	    8, 1, &dedup) == 0) {
		(void) nvlist_add_uint64(nv,
		    zfs_prop_to_name(ZFS_PROP_DEDUP), dedup);
	}
	(void) zfs_set_prop_nvlist(zv->zv_name, ZPROP_SRC_LOCAL,
	    nv, NULL);
	nvlist_free(nv);

	mutex_enter(&zv->zv_state_lock);
	zvol_free_dvas(zv);
	zv->zv_flags &= ~ZVOL_DUMPIFIED;
	mutex_exit(&zv->zv_state_lock);

	(void) dmu_free_long_range(os, ZVOL_OBJ, 0, DMU_OBJECT_END);
	/* wait for dmu_free_long_range to actually free the blocks */
	txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, ZVOL_OBJ);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	if (dmu_object_set_blocksize(os, ZVOL_OBJ, vbs, 0, tx) == 0)
		zv->zv_volblocksize = vbs;
	dmu_tx_commit(tx);

	return (0);
}
