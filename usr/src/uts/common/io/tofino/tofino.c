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
 * Device driver to work with Barefoot/Intel Tofino programmable network ASICs.
 * Supports Tofino 1 and 2.
 *
 * ----------
 * Background
 * ----------
 *
 * The purpose of this driver is to provide a compatible interface for the
 * Barefoot / Intel Tofino 1 and 2 family ASICs. Most of this device is driven
 * by the "P4 Studio Software Development Environment" which runs in user land.
 * The overall user / kernel interface does not change very much allowing this
 * driver to work across several different generations of hardware with most of
 * the heavy lifting being done by the SDE.
 *
 * Ultimately, the user / kernel API is defined by that software. Because that
 * SDE generally targets Linux platforms, folks generally will be rebuilding the
 * SDE to operate here. As such while we are implementing the expected API,
 * there is not a strict requirement to match the ABI in ioctls per se since
 * those are being built; however, when it comes to what the basic character
 * device entry points do, that is entirely driven by the upstream work.
 *
 * ----------
 * Interrupts
 * ----------
 *
 * An important part of the interface between the user software and the kernel
 * is that the kernel proxies interrupt information between the two. This means
 * that the choice of which type of interrupt we use actually is important and
 * has bearing on the system. While in most traditional device drivers this
 * choice is really based upon system resource availability, that is not true
 * here.
 *
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/pci.h>
#include <sys/ksynch.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/list.h>
#include <sys/tofino.h>

#include <sys/tofino.h>
#include <sys/tofino_regs.h>
#include "tofino_impl.h"

#define	TOFINO_MAX_INSTANCE	16

/*
 * Make a copy of this value to make it accesible to (k)mdb on both live systems
 * and core dumps.
 */
const int tofino_max_instance = TOFINO_MAX_INSTANCE;

dev_info_t		*tofino_dip = NULL;
static void		*tofino_soft_state = NULL;
static id_space_t	*tofino_minors = NULL;
int			tofino_debug = 0;

/*
 * Utility function for debug logging
 */
void
tofino_dlog(tofino_t *tf, const char *fmt, ...)
{
	va_list args;

	if (tofino_debug) {
		va_start(args, fmt);
		vdev_err(tf->tf_dip, CE_NOTE, fmt, args);
		va_end(args);
	}
}

/*
 * Utility function for error logging
 */
void
tofino_err(tofino_t *tf, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vdev_err(tf->tf_dip, CE_WARN, fmt, args);
	va_end(args);
}

/*
 * Read a single 32-bit register from the device's MMIO space.  The offset is
 * provided in bytes.
 */
int
tofino_read_reg(dev_info_t *dip, size_t offset, uint32_t *val)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	ddi_acc_handle_t hdl = tf->tf_regs_hdls[0];
	caddr_t base = tf->tf_regs_bases[0];

	if (offset > tf->tf_regs_lens[0])
		return (EINVAL);
	*val = ddi_get32(hdl, (uint32_t *)(base + offset));
	return (0);
}

/*
 * Write to a single 32-bit register in the device's MMIO space.  The offset is
 * provided in bytes.
 */
int
tofino_write_reg(dev_info_t *dip, size_t offset, uint32_t val)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	ddi_acc_handle_t hdl = tf->tf_regs_hdls[0];
	caddr_t base = tf->tf_regs_bases[0];

	if (offset > tf->tf_regs_lens[0])
		return (EINVAL);

	ddi_put32(hdl, (uint32_t *)(base + offset), val);
	return (0);
}

static int
tofino_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	tofino_open_t *top;
	tofino_t *tf;
	minor_t minor;
	int instance = getminor(*devp);

	/* XXX: add support for 32-bit opens */
	if (get_udatamodel() != DATAMODEL_LP64)
		return (ENOSYS);
	if (otyp != OTYP_CHR)
		return (EINVAL);
	if (instance >= TOFINO_MAX_INSTANCE)
		return (ENXIO);
	if (tofino_dip == NULL)
		return (ENXIO);
	tf = ddi_get_driver_private(tofino_dip);

	minor = id_alloc_nosleep(tofino_minors);
	if (minor == -1) {
		/* All minors are busy */
		return (EBUSY);
	}

	if (ddi_soft_state_zalloc(tofino_soft_state, minor) != DDI_SUCCESS) {
		id_free(tofino_minors, minor);
		return (ENOMEM);
	}

	*devp = makedevice(getmajor(*devp), minor);
	top = ddi_get_soft_state(tofino_soft_state, minor);
	mutex_init(&top->to_mutex, NULL, MUTEX_DRIVER, NULL);
	top->to_device = tf;
	list_create(&top->to_pages, sizeof (tofino_dma_page_t),
	    offsetof(tofino_dma_page_t, td_list_node));

	return (0);
}

static ddi_dma_attr_t dma_attr = {
	.dma_attr_version =		DMA_ATTR_V0,
	.dma_attr_addr_lo =		0x0000000000000000ull,
	.dma_attr_addr_hi =		0xFFFFFFFFFFFFFFFFull,
	.dma_attr_count_max =		0x00000000FFFFFFFFull,
	.dma_attr_align =		0x0000000000200000ull,
	.dma_attr_burstsizes =		0x00000FFF,
	.dma_attr_minxfer =		0x00000001,
	.dma_attr_maxxfer =		0x00000000FFFFFFFFull,
	.dma_attr_seg =			0xFFFFFFFFFFFFFFFFull,
	.dma_attr_sgllen =		1,
	.dma_attr_granular =		1,
	.dma_attr_flags =		0,
};

static void
tofino_dma_page_teardown(tofino_dma_page_t *tdp)
{
	if (tdp->td_va != 0) {
		if (ddi_dma_unbind_handle(tdp->td_dma_hdl) != 0) {
			cmn_err(CE_WARN, "!error unbinding dma hdl");
		}
		ddi_dma_free_handle(&tdp->td_dma_hdl);
		if (tdp->td_umem_cookie != NULL)
			ddi_umem_unlock(tdp->td_umem_cookie);

		tdp->td_va = 0;
	}
}

static tofino_dma_page_t *
tofino_dma_page_setup(tofino_open_t *top, caddr_t va, size_t sz)
{
	tofino_t *tf = top->to_device;
	int dma_flags = DDI_DMA_RDWR | DDI_DMA_STREAMING;
	ddi_dma_handle_t dma_hdl;
	const ddi_dma_cookie_t *dma_cookie;
	ddi_umem_cookie_t um_cookie;
	tofino_dma_page_t *tdp;
	int err;

#if 0
	/*
	 * XXX: When mapping the register space into a user process we want to
	 * ddi_umem_lock() it into place, and that lock should be released as
	 * part of closing the device.  However, I'm seeing the address space
	 * get torn down before the file is closed - even though closeall() is
	 * called before relvm().  Until I get that sorted out we need to leave
	 * this unlocked in the kernel, relying on the user-level memlock to pin
	 * this.
	 */
	const int lock_flags = DDI_UMEMLOCK_READ | DDI_UMEMLOCK_WRITE;
	if (ddi_umem_lock(va, sz, lock_flags, &um_cookie) != 0) {
		cmn_err(CE_WARN, "!lock failed");
		return (NULL);
	}
#else
	um_cookie = NULL;
#endif

	if ((err = ddi_dma_alloc_handle(tf->tf_dip, &dma_attr, DDI_DMA_SLEEP,
	    NULL, &dma_hdl)) != 0) {
		cmn_err(CE_WARN, "!alloc_handle failed: %d", err);
		goto fail1;
	}

	if ((err = ddi_dma_addr_bind_handle(dma_hdl, curproc->p_as,
	    va, sz, dma_flags, DDI_DMA_DONTWAIT, NULL, NULL, NULL)) != 0) {
		cmn_err(CE_WARN, "!bind_handle failed: %d", err);
		goto fail2;
	}

	dma_cookie = ddi_dma_cookie_one(dma_hdl);
	tdp = kmem_zalloc(sizeof (*tdp), KM_SLEEP);
	tdp->td_va = va;
	tdp->td_refcnt = 0;
	tdp->td_dma_addr = dma_cookie->dmac_laddress;
	tdp->td_umem_cookie = um_cookie;
	tdp->td_dma_hdl = dma_hdl;

	return (tdp);

fail2:
	ddi_dma_free_handle(&dma_hdl);

fail1:
	if (um_cookie != NULL)
		ddi_umem_unlock(um_cookie);

	return (NULL);
}

/*
 * Copy in a bf_dma_bus_map_t structure from the userspace daemon.
 * Verify that the structure describes a range of memory that is both aligned
 * and sized to be mapped by large pages.
 *
 * XXX: the requirement that the daemon use large pages comes from Intel's
 * original Linux code.  I haven't yet come across any place where it
 * actually seems to be necessary, but I also haven't studied their driver
 * code in any detail.  It's definitely worth experimenting with allowing
 * small pages here as well.
 */
static int
tofino_dma_copyin(intptr_t arg, int mode, bf_dma_bus_map_t *dbm)
{
	if (ddi_copyin((void *)(uintptr_t)arg, dbm, sizeof (bf_dma_bus_map_t),
	    mode) != 0) {
		return (EFAULT);
	}

	/*
	 * We expect/require the daemon to use only large pages.
	 */
	if ((dbm->size != TF_DMA_PGSIZE) ||
	    (((uintptr_t)dbm->va & TF_DMA_PGMASK) != 0)) {
		return (EINVAL);
	}

	return (0);
}

static tofino_dma_page_t *
tofino_dma_page_find(tofino_open_t *top, caddr_t va)
{
	tofino_dma_page_t *tdp;

	ASSERT(MUTEX_HELD(&top->to_mutex));

	for (tdp = list_head(&top->to_pages);
	    tdp != NULL;
	    tdp = list_next(&top->to_pages, tdp)) {
		if (tdp->td_va == va)
			break;
	}

	return (tdp);
}

/*
 * Process a request from the userspace daemon to allocate a DMA-capable
 * physical page to back the given virtual address.
 */
static int
tofino_dma_setup(tofino_open_t *top, intptr_t arg, int mode)
{
	bf_dma_bus_map_t dbm;
	tofino_dma_page_t *tdp;
	int error;

	if ((error = tofino_dma_copyin(arg, mode, &dbm)) != 0)
		return (error);

	mutex_enter(&top->to_mutex);
	tdp = tofino_dma_page_find(top, dbm.va);
	if (tdp == NULL) {
		tdp = tofino_dma_page_setup(top, dbm.va, dbm.size);
		if (tdp == NULL) {
			mutex_exit(&top->to_mutex);
			return (EFAULT);
		}
		list_insert_head(&top->to_pages, tdp);
	}

	tdp->td_refcnt++;
	dbm.dma_addr = tdp->td_dma_addr;
	mutex_exit(&top->to_mutex);

	if (ddi_copyout(&dbm, (void *)arg, sizeof (dbm), mode) != 0) {
		return (EFAULT);
	}

	return (0);
}

/*
 * Respond to the daemon's request to clean up a DMA-capable range of its
 * address space.
 */
static int
tofino_dma_teardown(tofino_open_t *top, intptr_t arg, int mode)
{
	bf_dma_bus_map_t dbm;
	tofino_dma_page_t *tdp;
	int error;

	if ((error = tofino_dma_copyin(arg, mode, &dbm)) != 0)
		return (error);

	mutex_enter(&top->to_mutex);
	tdp = tofino_dma_page_find(top, dbm.va);

	if (tdp != NULL && --tdp->td_refcnt == 0) {
		list_remove(&top->to_pages, tdp);
		tofino_dma_page_teardown(tdp);
		kmem_free(tdp, sizeof (*tdp));
	}
	mutex_exit(&top->to_mutex);

	return (tdp == NULL ? ENOENT : 0);
}

/*
 * read(2) for tofino devices is used to communicate interrupt status to the
 * userspace daemon.  The reference code uses a 32-bit integer per interrupt to
 * track the interrupts which have fired since the previous read.
 */
/* ARGSUSED */
static int
tofino_read(dev_t dev, struct uio *uio, cred_t *cr)
{
	uint32_t fired[TOFINO_MAX_MSI_INTRS];
	tofino_open_t *top;
	tofino_t *tf;
	uint32_t max;

	if ((top = ddi_get_soft_state(tofino_soft_state, dev)) == NULL)
		return (ENXIO);
	tf = top->to_device;

	max = MIN(TOFINO_MAX_MSI_INTRS, uio->uio_resid / sizeof (uint32_t));
	mutex_enter(&top->to_mutex);
	for (uint32_t i = 0; i < max; i++) {
		uint32_t cnt = tf->tf_intr_cnt[i];

		if (cnt != top->to_intr_read[i]) {
			fired[i] = 1;
			top->to_intr_read[i] = cnt;
		}

	}
	mutex_exit(&top->to_mutex);

	if (uiomove(fired, max * sizeof (uint32_t), UIO_READ, uio) != 0)
		return (EFAULT);

	return (0);
}

static int
tofino_chpoll(dev_t dev, short events, int anyyet, short *reventsp,
    struct pollhead **phpp)
{
	tofino_open_t *top;
	tofino_t *tf;

	if ((top = ddi_get_soft_state(tofino_soft_state, dev)) == NULL)
		return (ENXIO);
	tf = top->to_device;

	/*
	 * The only pollable event for the tofino device is a change in the
	 * interrupt counters.
	 */
	*reventsp = 0;
	if ((events & POLLRDNORM) == 0)
		return (0);

	mutex_enter(&top->to_mutex);
	for (int i = 0; i < TOFINO_MAX_MSI_INTRS; i++) {
		if (tf->tf_intr_cnt[i] != top->to_intr_read[i]) {
			*reventsp |= POLLRDNORM;
			break;
		}
	}
	mutex_exit(&top->to_mutex);

	if ((*reventsp == 0 && !anyyet) || (events & POLLET))
		*phpp = &tf->tf_pollhead;

	return (0);
}

static int
tofino_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	const uint32_t imode = BF_INTR_MODE_MSI;
	const tofino_version_t tf_version = {
		.tofino_major = TOFINO_DRIVER_MAJOR,
		.tofino_minor = TOFINO_DRIVER_MINOR,
		.tofino_patch = TOFINO_DRIVER_PATCH,
	};

	int minor = getminor(dev);
	int resetting;
	tofino_open_t *top;
	tofino_t *tf;

	top = ddi_get_soft_state(tofino_soft_state, minor);
	ASSERT(top != NULL);

	switch (cmd) {
	case BF_IOCMAPDMAADDR:
		return (tofino_dma_setup(top, arg, mode));

	case BF_IOCUNMAPDMAADDR:
		return (tofino_dma_teardown(top, arg, mode));

	case BF_TBUS_MSIX_INDEX:
		return (ENOTTY);

	case BF_GET_INTR_MODE:
		if (ddi_copyout(&imode, (void *)arg, sizeof (imode), mode))
			return (EFAULT);

		return (0);

	case BF_PKT_INIT:
		tf = ddi_get_driver_private(tofino_dip);
		if (tf == NULL) {
			return (ENXIO);
		}

		if (ddi_copyin((void *)(uintptr_t)arg, &resetting,
		    sizeof (resetting), mode) != 0) {
			return (EFAULT);
		}

		mutex_enter(&tf->tf_mutex);
		if (resetting) {
			(void) tofino_tbus_state_update(tf, TF_TBUS_RESETTING);
		} else {
			(void) tofino_tbus_state_update(tf, TF_TBUS_RESET);
		}
		mutex_exit(&tf->tf_mutex);
		return (0);

	case BF_GET_PCI_DEVID:
		if (ddi_copyout(&tf_version, (void *)arg,
		    sizeof (tf_version), mode))
			return (EFAULT);
		return (0);

	case BF_GET_VERSION:
		tf = ddi_get_driver_private(tofino_dip);
		if (ddi_copyout(&tf_version, (void *)arg, sizeof (tf_version),
		    mode))
			return (EFAULT);
		return (0);
	}

	return (ENOTTY);
}

#define	BAR0 1	/* Register index 1 */

static struct devmap_callback_ctl tfmap_ops = {
	.devmap_rev =		DEVMAP_OPS_REV,
};

static int
tofino_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
    size_t *maplen, uint_t model)
{
	ddi_device_acc_attr_t da;
	tofino_open_t *top;
	tofino_t *tf;
	uint_t maxprot;
	int err;
	size_t length;
	off_t range_size;

	bzero(&da, sizeof (da));
	da.devacc_attr_version = DDI_DEVICE_ATTR_V1;
	da.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	da.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	da.devacc_attr_access = DDI_DEFAULT_ACC;

	if ((top = ddi_get_soft_state(tofino_soft_state, getminor(dev))) ==
	    NULL) {
		return (ENODEV);
	}

	tf = top->to_device;
	range_size = tf->tf_regs_lens[0];

	if (off >= range_size)
		return (EINVAL);

	len = ptob(btopr(len));

	/* check for overflow */
	if (off + len < off) {
		return (EINVAL);
	}
	if (off + len < range_size) {
		length = len;
	} else {
		length = range_size - off;
	}

	maxprot = PROT_ALL & ~PROT_EXEC;
	if ((err = devmap_devmem_setup(dhp, tf->tf_dip, &tfmap_ops, BAR0, off,
	    length, maxprot, IOMEM_DATA_UNCACHED, &da)) < 0) {
		return (err);
	}

	*maplen = length;
	return (0);
}

static int
tofino_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	minor_t minor = getminor(dev);
	tofino_open_t *top;
	tofino_dma_page_t *tdp;

	top = ddi_get_soft_state(tofino_soft_state, minor);
	ASSERT(top != NULL);

	while ((tdp = list_remove_tail(&top->to_pages)) != NULL) {
		tofino_dma_page_teardown(tdp);
		kmem_free(tdp, sizeof (*tdp));
	}

	id_free(tofino_minors, minor);
	list_destroy(&top->to_pages);
	mutex_destroy(&top->to_mutex);

	ddi_soft_state_free(tofino_soft_state, minor);

	return (0);
}

static uint_t
tofino_intr(caddr_t arg, caddr_t arg2)
{
	tofino_t *tf = (tofino_t *)arg;
	int intr_no = (int)(uintptr_t)arg2;
	tofino_tbus_client_t *tbc;
	uint32_t s0, s1, s2;

	if (tf->tf_dip == NULL)
		return (DDI_INTR_UNCLAIMED);

	if (intr_no >= TOFINO_MAX_MSI_INTRS)
		return (DDI_INTR_UNCLAIMED);
	atomic_inc_32(&tf->tf_intr_cnt[intr_no]);
	pollwakeup(&tf->tf_pollhead, POLLRDNORM);

	mutex_enter(&tf->tf_mutex);

	/*
	 * We are only interested in the three status registers related to
	 * packet transfer.  The registers are RW1C (i.e., cleared in a bitwise
	 * fashion), so by writing back the same value we read we clear just
	 * those bits we've already seen.
	 *
	 * XXX: we should really let the softint reset the triggers, so we don't
	 * end up catching a needless interrupt while the softint is still
	 * iterating over the status registers.
	 */
	if (tf->tf_gen == TOFINO_G_TF1) {
		tofino_read_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT0, &s0);
		tofino_read_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT1, &s1);
		tofino_read_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT2, &s2);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT0, s0);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT1, s1);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT2, s2);
	} else {
		tofino_read_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT0, &s0);
		tofino_read_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT1, &s1);
		tofino_read_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT2, &s2);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT0, s0);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT1, s1);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT2, s2);
	}

	/*
	 * If a tbus client has registered an interrupt handler, call it.
	 * Setting tbc_intr_busy across the call ensures that we won't
	 * invalidate the handler fields or have multiple calls into the handler
	 * simultaneously.
	 */
	tbc = tf->tf_tbus_client;
	if (tbc != NULL && tbc->tbc_intr != NULL && !tbc->tbc_intr_busy) {
		tbc->tbc_intr_busy = true;

		mutex_exit(&tf->tf_mutex);
		tbc->tbc_intr(tbc->tbc_intr_arg);
		mutex_enter(&tf->tf_mutex);

		ASSERT(tf->tf_tbus_client == tbc);
		ASSERT(tbc->tbc_intr_busy);
		tbc->tbc_intr_busy = false;
	}
	mutex_exit(&tf->tf_mutex);

	return (DDI_INTR_CLAIMED);
}

static int
tofino_asic_identify(tofino_t *tf)
{
	uint16_t vendid = pci_config_get16(tf->tf_cfgspace, PCI_CONF_VENID);
	uint16_t devid = pci_config_get16(tf->tf_cfgspace, PCI_CONF_DEVID);

	if (vendid == TOFINO_VENDID) {
		switch (tf->tf_devid) {
		case TOFINO_DEVID_TF1_A0:
		case TOFINO_DEVID_TF1_B0:
			tf->tf_devid = devid;
			tf->tf_gen = TOFINO_G_TF1;
			return (0);
		case TOFINO_DEVID_TF2_A0:
		case TOFINO_DEVID_TF2_A00:
		case TOFINO_DEVID_TF2_B0:
			tf->tf_devid = devid;
			tf->tf_gen = TOFINO_G_TF2;
			return (0);
		}
	}

	tofino_err(tf, "!Unable to map %x,%x to a known tofino model",
	    vendid, devid);
	return (-1);
}

/*
 * There are three 64-bit BARs in the device. We should map all of them.
 */
static int
tofino_regs_map(tofino_t *tf)
{
	ddi_device_acc_attr_t da;

	bzero(&da, sizeof (da));
	da.devacc_attr_version = DDI_DEVICE_ATTR_V1;
	da.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	da.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	da.devacc_attr_access = DDI_DEFAULT_ACC;

	for (uint_t i = 0; i < TOFINO_NBARS; i++) {
		int ret;
		off_t memsize;
		caddr_t base;
		ddi_acc_handle_t hdl;
		uint_t regno = i + 1;

		/*
		 * Entry zero into the regs[] array is device information.
		 * Registers to map start at entry 1.
		 */
		regno = i + 1;
		if (ddi_dev_regsize(tf->tf_dip, regno, &memsize) != 0) {
			tofino_err(tf, "!failed to get register set size for "
			    "regs[%u]", i + 1);
			return (-1);
		}

		ret = ddi_regs_map_setup(tf->tf_dip, regno, &base, 0, memsize,
		    &da, &hdl);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "!failed to map register set %u: %d",
			    i, ret);
			return (-1);
		}

		tf->tf_regs_lens[i] = memsize;
		tf->tf_regs_bases[i] = base;
		tf->tf_regs_hdls[i] = hdl;
	}

	return (0);
}

static int
tofino_intr_init(tofino_t *tf)
{
	const int intr_type = DDI_INTR_TYPE_MSI;
	int ret, types, avail, nintrs;

	ret = ddi_intr_get_supported_types(tf->tf_dip, &types);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "!failed to get supported interrupt types: %d",
		    ret);
		return (-1);
	}

	if ((types & DDI_INTR_TYPE_MSI) == 0) {
		tofino_err(tf, "!missing required MSI support, found types %d",
		    types);
		return (-1);
	}

	/* Get number of interrupts */
	ret = ddi_intr_get_nintrs(tf->tf_dip, intr_type, &nintrs);
	if ((ret != DDI_SUCCESS) || (nintrs == 0)) {
		tofino_err(tf, "!ddi_intr_get_nintrs() failure.  "
		    "ret: %d, nintrs: %d", ret, nintrs);
		return (-1);
	}

	/* Get number of available interrupts */
	ret = ddi_intr_get_navail(tf->tf_dip, intr_type, &avail);
	if ((ret != DDI_SUCCESS) || (avail == 0)) {
		tofino_err(tf, "!ddi_intr_get_navail() failure, "
		    "ret: %d, avail: %d\n", ret, avail);
		return (-1);
	}

	ret = ddi_intr_alloc(tf->tf_dip, tf->tf_intrs, intr_type,
	    0, TOFINO_MAX_MSI_INTRS, &tf->tf_nintrs, DDI_INTR_ALLOC_NORMAL);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "!failed to allocate interrupts: %d", ret);
		return (-1);
	}

	/*
	 * Mark interrupts as attached for clean up later, before we try to get
	 * interrupt priority or related bits for enabling.
	 */
	tf->tf_attach |= TOFINO_A_INTR_ALLOC;

	ret = ddi_intr_get_cap(tf->tf_intrs[0], &tf->tf_intr_cap);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "!failed to get interrupt caps: %d", ret);
		return (-1);
	}

	for (uint32_t i = 0; i < nintrs; i++) {
		ret = ddi_intr_get_pri(tf->tf_intrs[i], &tf->tf_intr_pri);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "!failed to get interrupt pri: %d", ret);
			return (-1);
		}

		if (tf->tf_intr_pri >= LOCK_LEVEL) {
			tf->tf_intr_pri = LOCK_LEVEL - 1;
			ret = ddi_intr_set_pri(tf->tf_intrs[i],
			    tf->tf_intr_pri);
			if (ret != DDI_SUCCESS) {
				tofino_err(tf, "!failed to set intr pri: %d",
				    ret);
				return (-1);
			}
		}
	}

	return (0);
}

static int
tofino_intr_handlers_add(tofino_t *tf)
{
	tofino_dlog(tf, "!adding %d tofino interrupt handlers", tf->tf_nintrs);
	for (int i = 0; i < tf->tf_nintrs; i++) {
		int ret = ddi_intr_add_handler(tf->tf_intrs[i], tofino_intr,
		    tf, (void *)(uintptr_t)i);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "!failed to add intr handler %d: %d",
			    i, ret);
			for (i--; i >= 0; i--) {
				(void) ddi_intr_remove_handler(tf->tf_intrs[i]);
			}
			return (-1);
		}
	}

	return (0);
}

static void
tofino_intr_handlers_rem(tofino_t *tf)
{
	tofino_dlog(tf, "!removing tofino interrupt handlers");
	for (int i = 0; i < tf->tf_nintrs; i++) {
		int ret = ddi_intr_remove_handler(tf->tf_intrs[i]);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "!failed to remove interrupt handler "
			    "%d: %d", i, ret);
		}
	}
}

static int
tofino_intr_enable(tofino_t *tf)
{
	tofino_dlog(tf, "!enabling tofino interrupts");
	if ((tf->tf_intr_cap & DDI_INTR_FLAG_BLOCK) != 0) {
		int ret;

		ret = ddi_intr_block_enable(tf->tf_intrs, tf->tf_nintrs);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "!failed to block enable interrupts: %d",
			    ret);
			return (-1);
		}
	} else {
		for (int i = 0; i < tf->tf_nintrs; i++) {
			int ret = ddi_intr_enable(tf->tf_intrs[i]);
			if (ret != DDI_SUCCESS) {
				tofino_err(tf, "!failed to enable interrupt %d:"
				    " %d", i, ret);
				for (i--; i >= 0; i--) {
					(void) ddi_intr_disable(
					    tf->tf_intrs[i]);
				}
				return (-1);
			}
		}
	}

	return (0);
}

static void
tofino_intr_disable(tofino_t *tf)
{
	tofino_dlog(tf, "!disabling tofino interrupts");

	if ((tf->tf_intr_cap & DDI_INTR_FLAG_BLOCK) != 0) {
		int ret;

		ret = ddi_intr_block_disable(tf->tf_intrs, tf->tf_nintrs);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "!failed to disable interrupts: %d",
			    ret);
		}
	} else {
		for (int i = 0; i < tf->tf_nintrs; i++) {
			int ret = ddi_intr_disable(tf->tf_intrs[i]);
			if (ret != DDI_SUCCESS) {
				tofino_err(tf, "!failed to disable interrupt "
				    "%d: %d", i, ret);
			}
		}
	}
}

static int
tofino_minor_create(tofino_t *tf)
{
	minor_t m = (minor_t)ddi_get_instance(tf->tf_dip);

	if (ddi_create_minor_node(tf->tf_dip, "tofino", S_IFCHR, m, DDI_PSEUDO,
	    0) != DDI_SUCCESS) {
		return (-1);
	}

	return (0);
}

static void
tofino_cleanup(tofino_t *tf)
{
	ASSERT(tf->tf_tbus_client == NULL);

	if ((tf->tf_attach & TOFINO_A_MINOR) != 0) {
		ddi_remove_minor_node(tf->tf_dip, "tofino");
		tf->tf_attach &= ~TOFINO_A_MINOR;
	}

	if ((tf->tf_attach & TOFINO_A_INTR_ENABLE) != 0) {
		tofino_intr_disable(tf);
		tf->tf_attach &= ~TOFINO_A_INTR_ENABLE;
	}

	if ((tf->tf_attach & TOFINO_A_INTR_HANDLERS) != 0) {
		tofino_intr_handlers_rem(tf);
		tf->tf_attach &= ~TOFINO_A_INTR_HANDLERS;
	}

	if ((tf->tf_attach & TOFINO_A_INTR_ALLOC) != 0) {
		for (int i = 0; i < tf->tf_nintrs; i++) {
			int ret;

			ret = ddi_intr_free(tf->tf_intrs[i]);
			if (ret != DDI_SUCCESS) {
				tofino_err(tf, "!failed to free interrupt %d: "
				    "%d", i, ret);
			}
		}
		tf->tf_attach &= ~TOFINO_A_INTR_ALLOC;
	}

	for (uint_t i = 0; i < TOFINO_NBARS; i++) {
		if (tf->tf_regs_hdls[i] != NULL) {
			ddi_regs_map_free(&tf->tf_regs_hdls[i]);
		}
	}

	if (tf->tf_cfgspace != NULL) {
		pci_config_teardown(&tf->tf_cfgspace);
	}

	ddi_set_driver_private(tf->tf_dip, NULL);
	mutex_destroy(&tf->tf_mutex);

	ASSERT0(tf->tf_attach);
	kmem_free(tf, sizeof (*tf));
}

static int
tofino_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	tofino_t *tf;
	int instance;

	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if (tofino_dip != NULL) {
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	if (instance > TOFINO_MAX_INSTANCE) {
		dev_err(dip, CE_WARN, "!invalid instance: %d", instance);
		return (DDI_FAILURE);
	}

	tf = kmem_zalloc(sizeof (*tf), KM_SLEEP);
	tf->tf_dip = dip;
	tf->tf_instance = instance;
	ddi_set_driver_private(dip, tf);

	if (pci_config_setup(dip, &tf->tf_cfgspace) != DDI_SUCCESS) {
		tofino_err(tf, "!failed to set up pci config space");
		goto cleanup;
	}

	if (tofino_asic_identify(tf) != 0) {
		goto cleanup;
	}

	if (tofino_regs_map(tf) != 0) {
		goto cleanup;
	}

	if (tofino_intr_init(tf) != 0) {
		goto cleanup;
	}

	mutex_init(&tf->tf_mutex, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(tf->tf_intr_pri));

	if (tofino_intr_handlers_add(tf) != 0) {
		goto cleanup;
	}
	tf->tf_attach |= TOFINO_A_INTR_HANDLERS;

	if (tofino_intr_enable(tf) != 0) {
		goto cleanup;
	}
	tf->tf_attach |= TOFINO_A_INTR_ENABLE;

	if (tofino_minor_create(tf) != 0) {
		goto cleanup;
	}
	tf->tf_attach |= TOFINO_A_MINOR;

	tofino_dip = dip;
	ddi_report_dev(dip);
	tofino_dlog(tf, "!%s(): tofino driver attached", __func__);
	return (DDI_SUCCESS);

cleanup:
	tofino_cleanup(tf);
	return (DDI_FAILURE);
}

static int
tofino_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	tofino_open_t *top;
	tofino_t *tf = NULL;
	minor_t minor;

	if (cmd != DDI_INFO_DEVT2DEVINFO && cmd != DDI_INFO_DEVT2INSTANCE)
		return (DDI_FAILURE);

	minor = getminor((dev_t)arg);
	if ((top = ddi_get_soft_state(tofino_soft_state, minor)) == NULL)
		return (DDI_FAILURE);
	if ((tf = top->to_device) == NULL)
		return (DDI_FAILURE);

	if (cmd == DDI_INFO_DEVT2DEVINFO)
		*resultp = (void *)tf->tf_dip;
	else
		*resultp = (void *)(uintptr_t)tf->tf_instance;

	return (DDI_SUCCESS);
}

static int
tofino_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	tofino_t *tf;

	if (cmd != DDI_DETACH) {
		goto fail1;
	}
	ASSERT(dip == tofino_dip);

	tf = ddi_get_driver_private(dip);
	if (tf == NULL) {
		dev_err(dip, CE_WARN, "!asked to detach but no private data");
		goto fail1;
	}

	mutex_enter(&tf->tf_mutex);
	if (tf->tf_tbus_client != NULL)
		goto fail2;

	tofino_cleanup(tf);
	tofino_dip = NULL;

	return (DDI_SUCCESS);

fail2:
	mutex_exit(&tf->tf_mutex);

fail1:
	return (DDI_FAILURE);
}

static struct cb_ops tofino_cb_ops = {
	.cb_open =			tofino_open,
	.cb_close =			tofino_close,
	.cb_strategy =			nodev,
	.cb_print =			nodev,
	.cb_dump =			nodev,
	.cb_read =			tofino_read,
	.cb_write =			nodev,
	.cb_ioctl =			tofino_ioctl,
	.cb_devmap =			tofino_devmap,
	.cb_mmap =			nodev,
	.cb_segmap =			nodev,
	.cb_chpoll =			tofino_chpoll,
	.cb_prop_op =			ddi_prop_op,
	.cb_flag =			D_MP | D_DEVMAP,
	.cb_rev =			CB_REV,
	.cb_aread =			nodev,
	.cb_awrite =			nodev
};

static struct dev_ops tofino_dev_ops = {
	.devo_rev =			DEVO_REV,
	.devo_getinfo =			tofino_getinfo,
	.devo_identify =		nulldev,
	.devo_probe =			nulldev,
	.devo_attach =			tofino_attach,
	.devo_detach =			tofino_detach,
	.devo_reset =			nodev,
	.devo_quiesce =			ddi_quiesce_not_supported,
	.devo_cb_ops =			&tofino_cb_ops
};

static struct modldrv tofino_modldrv = {
	.drv_modops =			&mod_driverops,
	.drv_linkinfo =			"Tofino ASIC Driver",
	.drv_dev_ops =			&tofino_dev_ops
};

static struct modlinkage tofino_modlinkage = {
	.ml_rev =			MODREV_1,
	.ml_linkage =			{ &tofino_modldrv, NULL },
};

int
_init(void)
{
	int err;

	err = ddi_soft_state_init(
	    &tofino_soft_state, sizeof (tofino_open_t), 0);
	if (err == 0) {
		tofino_minors = id_space_create("tofino_minors",
		    TOFINO_MAX_INSTANCE + 1, UINT16_MAX);

		err = mod_install(&tofino_modlinkage);
		if (err != 0)
			id_space_destroy(tofino_minors);
	}

	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&tofino_modlinkage, modinfop));
}

int
_fini(void)
{
	int err;

	if ((err = mod_remove(&tofino_modlinkage)) != 0)
		return (err);

	ddi_soft_state_fini(&tofino_soft_state);
	id_space_destroy(tofino_minors);
	tofino_soft_state = NULL;
	return (0);
}
