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
 * Copyright 2021 Oxide Computer Company
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
 * At this time we only support the use of MSI interrupts because that is what
 * the driver and scripts default to using in P4 Studio. With MSIs, Tofino
 * expects at most two interrupts; however, only one is required.
 */

#include <sys/types.h>
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
#include <sys/tofino.h>

#include <sys/tofino.h>
#include <sys/tofino_impl.h>
#include <sys/tofino_pkt.h>
#include "tofino_regs.h"

#define	TOFINO_MAX_INSTANCE	16

/*
 * Make a copy of this value to make it accesible to (k)mdb on both live systems
 * and core dumps.
 */
const int tofino_max_instance = TOFINO_MAX_INSTANCE;

static void		*tofino_state;
static id_space_t	*tofino_minors;

void
tofino_vlog(dev_info_t *dip, int level, const char *fmt, va_list args)
{
	char buf[128];

	(void) vsnprintf(buf, sizeof (buf), fmt, args);
	dev_err(dip, level, buf);
}

static void
tofino_log(tofino_t *tf, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	tofino_vlog(tf->tf_dip, CE_NOTE, fmt, args);
	va_end(args);
}

static void
tofino_err(tofino_t *tf, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	tofino_vlog(tf->tf_dip, CE_WARN, fmt, args);
	va_end(args);
}

/*
 * Get the tofino_t associated with this dev_t.
 */
static tofino_t *
tofino_get_device(dev_t dev)
{
	tofino_open_t *top;

	ASSERT(tofino_state != NULL);
	top = ddi_get_soft_state(tofino_state, dev);
	if (top != NULL)
		return (top->to_device);

	return (NULL);
}

static int
tofino_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	tofino_open_t *top;
	tofino_t *tf;
	minor_t minor;
	int instance = getminor(*devp);

	if (otyp != OTYP_CHR)
		return (EINVAL);
	if (instance >= TOFINO_MAX_INSTANCE)
		return (ENXIO);
	if ((tf = tofino_get_device(instance)) == NULL)
		return (ENXIO);

	minor = id_alloc_nosleep(tofino_minors);
	if (minor == -1) {
		/* All minors are busy */
		return (EBUSY);
	}

	if (ddi_soft_state_zalloc(tofino_state, minor) != DDI_SUCCESS) {
		id_free(tofino_minors, minor);
		return (ENOMEM);
	}

	*devp = makedevice(getmajor(*devp), minor);
	top = ddi_get_soft_state(tofino_state, minor);
	mutex_init(&top->to_mutex, NULL, MUTEX_DEFAULT, NULL);
	top->to_device = tf;

	return (0);
}

typedef struct {
	caddr_t va;
	uintptr_t dma_addr;
	size_t size;
} bf_dma_bus_map_t;

static ddi_dma_attr_t dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version	*/
	0x0000000000000000ull,		/* dma_attr_addr_lo	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x00000000FFFFFFFFull,		/* dma_attr_count_max	*/
	0x0000000000200000ull,		/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_maxxfer	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_seg		*/
	1,				/* dma_attr_sgllen	*/
	1,				/* dma_attr_granular	*/
	DDI_DMA_FLAGERR			/* dma_attr_flags */
};

static void
tofino_dma_page_teardown(tofino_dma_page_t *tdp) {
	if (tdp->td_va != 0) {
		if (ddi_dma_unbind_handle(tdp->td_dma_hdl) != 0) {
			cmn_err(CE_WARN, "error unbinding dma hdl");
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
	ddi_dma_cookie_t dma_cookie;
	ddi_umem_cookie_t um_cookie;
	tofino_dma_page_t *tdp;
	uint_t cnt;
	int err;

	// XXX: When mapping the register space into a user process we want to
	// ddi_umem_lock() it into place, and that lock should be released up as
	// part of closing the device.  However, I'm seeing the address space
	// get torn down before the file is closed - even though closeall() is
	// called before relvm().  Until I get that sorted out we need to leave
	// this unlocked in the kernel, relying on the user-level memlock to pin
	// this.
#if 0
	const int lock_flags = DDI_UMEMLOCK_READ | DDI_UMEMLOCK_WRITE;
	if (ddi_umem_lock(va, sz, lock_flags, &um_cookie) != 0) {
		cmn_err(CE_WARN, "lock failed");
		return NULL;
	}
#else
	um_cookie = NULL;
#endif

	if ((err = ddi_dma_alloc_handle(tf->tf_dip, &dma_attr, DDI_DMA_DONTWAIT,
	    NULL, &dma_hdl)) != 0) {
		cmn_err(CE_WARN, "alloc_handle failed: %d", err);
		goto fail1;
	}

	if ((err = ddi_dma_addr_bind_handle(dma_hdl, curproc->p_as,
	    va, sz, dma_flags, DDI_DMA_DONTWAIT, NULL,
	    &dma_cookie, &cnt)) != 0) {
		cmn_err(CE_WARN, "bind_handle failed: %d", err);
		goto fail2;
	}

	tdp = kmem_alloc(sizeof (*tdp), KM_SLEEP);
	tdp->td_va = va;
	tdp->td_refcnt = 0;
	tdp->td_dma_addr = dma_cookie.dmac_laddress;
	tdp->td_umem_cookie = um_cookie;
	tdp->td_dma_hdl = dma_hdl;
	tdp->td_dma_cookie = dma_cookie;
	tdp->td_next = NULL;

	return tdp;

fail2:
	ddi_dma_free_handle(&dma_hdl);

fail1:
	if (um_cookie != NULL)
		ddi_umem_unlock(um_cookie);

	return NULL;
}

/*
 * Copy in a bf_dma_bus_map_t structure from the userspace daemon.
 * Verify that the structure describes a range of memory that is both aligned
 * and sized to be mapped by large pages.
 */
static int
tofino_dma_copyin(intptr_t arg, int mode, bf_dma_bus_map_t *dbm)
{
	if (ddi_copyin((void *)(uintptr_t)arg, dbm, sizeof(bf_dma_bus_map_t),
	    mode) != 0) {
		return EFAULT;
	}

	// We expect/require the daemon to use only large pages.
	if ((dbm->size != TF_DMA_PGSIZE) ||
	    (((uintptr_t)dbm->va & TF_DMA_PGMASK) != 0)) {
		return EINVAL;
	}

	return 0;
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
		return error;

	mutex_enter(&top->to_mutex);
	for (tdp = top->to_pages; tdp != NULL; tdp = tdp->td_next) {
		if (tdp->td_va == dbm.va)
			break;
	}

	if (tdp == NULL) {
		tdp = tofino_dma_page_setup(top, dbm.va, dbm.size);
		if (tdp == NULL) {
			mutex_exit(&top->to_mutex);
			return EFAULT;
		}
		tdp->td_next = top->to_pages;
		top->to_pages = tdp;
	}

	tdp->td_refcnt++;
	dbm.dma_addr = tdp->td_dma_addr;
	mutex_exit(&top->to_mutex);

	if (ddi_copyout(&dbm, (void *)arg, sizeof (dbm), mode) != 0) {
		return EFAULT;
	}

	return 0;
}

/*
 * Respond to the daemon's request to clean up a DMA-capable range of its
 * address space.
 */
static int
tofino_dma_teardown(tofino_open_t *top, intptr_t arg, int mode)
{
	bf_dma_bus_map_t dbm;
	tofino_dma_page_t *tdp, *lastdp;
	int error;

	if ((error = tofino_dma_copyin(arg, mode, &dbm)) != 0)
		return error;

	mutex_enter(&top->to_mutex);
	lastdp = NULL;
	for (tdp = top->to_pages; tdp != NULL; tdp = tdp->td_next) {
		if (tdp->td_va == dbm.va)
			break;
		lastdp = tdp;
	}

	if (tdp != NULL) {
		if (--tdp->td_refcnt == 0) {
			if (lastdp == NULL) {
				top->to_pages = tdp->td_next;
			} else {
				lastdp->td_next = tdp->td_next;
			}
			tofino_dma_page_teardown(tdp);
			kmem_free(tdp, sizeof (*tdp));
		}
	}
	mutex_exit(&top->to_mutex);

	return (tdp == NULL) ? ENOENT : 0;
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
	int max, rc = 0;

	if ((top = ddi_get_soft_state(tofino_state, dev)) == NULL)
		return (ENXIO);
	tf = top->to_device;

	max = MIN(TOFINO_MAX_MSI_INTRS, uio->uio_resid / sizeof (uint32_t));
	mutex_enter(&top->to_mutex);
	for (int i = 0; i < max; i++) {
		uint32_t cnt = tf->tf_intr_cnt[i];

		if (cnt != top->to_intr_read[i]) {
			fired[i] = 1;
			top->to_intr_read[i] = cnt;
		}

	}
	mutex_exit(&top->to_mutex);

	if (uiomove(fired, max * sizeof (uint32_t), UIO_READ, uio)) {
		rc = EFAULT;
	}

	return (rc);
}

static int
tofino_chpoll(dev_t dev, short events, int anyyet, short *reventsp,
    struct pollhead **phpp)
{
	tofino_open_t *top;
	tofino_t *tf;

	if ((top = ddi_get_soft_state(tofino_state, dev)) == NULL)
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
	int minor = getminor(dev);
	tofino_open_t *top;
	tofino_t *tf;
	int rval = 0;

	top = ddi_get_soft_state(tofino_state, minor);
	ASSERT(top != NULL);
	tf = top->to_device;

	switch (cmd) {
	case BF_IOCMAPDMAADDR:
		return tofino_dma_setup(top, arg, mode);

	case BF_IOCUNMAPDMAADDR:
		return tofino_dma_teardown(top, arg, mode);

	case BF_TBUS_MSIX_INDEX:
		cmn_err(CE_NOTE, "tofino_ioctl(BF_TBUS_MSIX_INDEX)");
		return (ENOTTY);

	case BF_GET_INTR_MODE:
		if (ddi_copyout(&imode, (void *)arg, sizeof(imode), mode))
			return (EFAULT);

		return (0);

	case BF_PKT_INIT:
		mutex_enter(&tf->tf_mutex);
		if (tf->tf_pkt_state != NULL)
			tfpkt_fini(tf);
		rval = tfpkt_init(tf);
		mutex_exit(&tf->tf_mutex);
		return (rval);
	}

	cmn_err(CE_NOTE, "tofino_ioctl(%d) - unrecognized command", cmd);
	return (ENOTTY);
}

#define BAR0 1 // reg index 1

static struct devmap_callback_ctl tfmap_ops = {
	DEVMAP_OPS_REV,	/* devmap_ops version number */
	NULL,		/* devmap_ops map routine */
	NULL,		/* devmap_ops access routine */
	NULL,		/* devmap_ops dup routine */
	NULL,		/* devmap_ops unmap routine  */
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
	off_t regsize;

	bzero(&da, sizeof (da));
	da.devacc_attr_version = DDI_DEVICE_ATTR_V1;
	da.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	da.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	da.devacc_attr_access = DDI_DEFAULT_ACC;

	if ((top = ddi_get_soft_state(tofino_state, getminor(dev))) == NULL)
		return (ENODEV);

	tf = top->to_device;
	if ((err = ddi_dev_regsize(tf->tf_dip, BAR0, &regsize)) != DDI_SUCCESS)
		return (EINVAL);
	if (off >= regsize)
		return (EINVAL);

	len = ptob(btopr(len));
	if (off + len < regsize) {
		length = len;
	} else {
		length = regsize - off;
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
	tofino_dma_page_t *tdp, *nextdp;
	int cnt = 0;

	top = ddi_get_soft_state(tofino_state, minor);
	ASSERT(top != NULL);

	for (tdp = top->to_pages; tdp != NULL; tdp = nextdp) {
		nextdp = tdp->td_next;
		tofino_dma_page_teardown(tdp);
		kmem_free(tdp, sizeof (*tdp));
		cnt++;
	}

	id_free(tofino_minors, minor);
	ddi_soft_state_free(tofino_state, minor);

	return (0);
}

static uint_t
tofino_intr(caddr_t arg, caddr_t arg2)
{
	tofino_t *tf = (tofino_t *)arg;
	int intr_no = (int)(uintptr_t)arg2;
	uint32_t s0, s1, s2;

	if (tf->tf_dip == NULL)
		return (DDI_INTR_UNCLAIMED);

	if (intr_no >= TOFINO_MAX_MSI_INTRS)
		return (DDI_INTR_UNCLAIMED);

	pollwakeup(&tf->tf_pollhead, POLLRDNORM);

	/*
	 * We are only interested in the three status registers related to
	 * packet transfer.  Copy them and clear them.
	 * XXX: we should really let the softint reset the triggers, so we don't
	 * end up catching a needless interrupt while the softint is still
	 * iterating over the status registers.
	 */
	if (tf->tf_gen == TOFINO_G_TF1) {
		s0 = tofino_read_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT0);
		s1 = tofino_read_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT1);
		s2 = tofino_read_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT2);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT0, s0);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT1, s1);
		tofino_write_reg(tf->tf_dip, TF_REG_TBUS_INT_STAT2, s2);
	} else {
		s0 = tofino_read_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT0);
		s1 = tofino_read_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT1);
		s2 = tofino_read_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT2);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT0, s0);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT1, s1);
		tofino_write_reg(tf->tf_dip, TF2_REG_TBUS_INT_STAT2, s2);
	}

	atomic_inc_32(&tf->tf_intr_cnt[intr_no]);
	if (tf->tf_pkt_state != NULL)
		ddi_intr_trigger_softint(tf->tf_pkt_state->tfp_softint, NULL);

	return (DDI_INTR_CLAIMED);
}

static boolean_t
tofino_asic_identify(tofino_t *tf)
{
	uint16_t venid = pci_config_get16(tf->tf_cfgspace, PCI_CONF_VENID);
	uint16_t devid = pci_config_get16(tf->tf_cfgspace, PCI_CONF_DEVID);

	switch (devid) {
	case TOFINO_DEVID_TF1_A0:
	case TOFINO_DEVID_TF1_B0:
		tf->tf_gen = TOFINO_G_TF1;
		break;
	case TOFINO_DEVID_TF2_A0:
	case TOFINO_DEVID_TF2_A00:
	case TOFINO_DEVID_TF2_B0:
		tf->tf_gen = TOFINO_G_TF2;
		break;
	default:
		tofino_err(tf, "Unable to map %x,%x to a known tofino model",
		    venid, devid);
		return (B_FALSE);
	}

	return (B_TRUE);
}

uint32_t
tofino_read_reg(dev_info_t *dip, size_t offset)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	ddi_acc_handle_t hdl = tf->tf_regs_hdls[0];
	caddr_t base = tf->tf_regs_bases[0];

	if (offset > tf->tf_regs_lens[0]) {
		dev_err(dip, CE_WARN, "regs offset %lx out of range", offset);
		return (0);
	}
	return (ddi_get32(hdl, (uint32_t *)(base + offset)));
}

void
tofino_write_reg(dev_info_t *dip, size_t offset, uint32_t val)
{
	tofino_t *tf = ddi_get_driver_private(dip);
	ddi_acc_handle_t hdl = tf->tf_regs_hdls[0];
	caddr_t base = tf->tf_regs_bases[0];

	if (offset > tf->tf_regs_lens[0]) {
		dev_err(dip, CE_WARN, "regs offset %lx out of range", offset);
		return;
	}

	ddi_put32(hdl, (uint32_t *)(base + offset), val);
}

/*
 * There are three 64-bit BARs in the device. We should map all of them.
 */
static boolean_t
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
			tofino_err(tf, "failed to get register set size for "
			    "regs[%u]", i + 1);
			return (B_FALSE);
		}

		ret = ddi_regs_map_setup(tf->tf_dip, regno, &base, 0, memsize,
		    &da, &hdl);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "failed to map register set %u: %d",
			    i, ret);
			return (B_FALSE);
		}

		tf->tf_regs_lens[i] = memsize;
		tf->tf_regs_bases[i] = base;
		tf->tf_regs_hdls[i] = hdl;
	}

	return (B_TRUE);
}

/*
 * See the theory statement for a bit more on why MSIs are required here.
 */
static boolean_t
tofino_intr_init(tofino_t *tf)
{
	const int intr_type = DDI_INTR_TYPE_MSI;
	int ret, types, avail, count;

	ret = ddi_intr_get_supported_types(tf->tf_dip, &types);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "failed to get supported interrupt types: %d",
		    ret);
		return (B_FALSE);
	}

	if ((types & DDI_INTR_TYPE_MSI) == 0) {
		tofino_err(tf, "missing required MSI support, found types %d",
		    types);
		return (B_FALSE);
	}

	/* Get number of interrupts */
	ret = ddi_intr_get_nintrs(tf->tf_dip, intr_type, &count);
	if ((ret != DDI_SUCCESS) || (count == 0)) {
		tofino_err(tf, "ddi_intr_get_nintrs() failure.  "
		    "ret: %d, count: %d", ret, count);
		return (B_FALSE);
	}

	/* Get number of available interrupts */
	ret = ddi_intr_get_navail(tf->tf_dip, intr_type, &avail);
	if ((ret != DDI_SUCCESS) || (avail == 0)) {
		tofino_err(tf, "ddi_intr_get_navail() failure, "
		    "ret: %d, avail: %d\n", ret, avail);
		return (B_FALSE);
	}

	ret = ddi_intr_alloc(tf->tf_dip, tf->tf_intrs, intr_type,
	    0, TOFINO_MAX_MSI_INTRS, &tf->tf_nintrs, DDI_INTR_ALLOC_NORMAL);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "failed to allocate interrupts: %d", ret);
		return (B_FALSE);
	}

	/*
	 * Mark interrupts as attached for clean up later, before we try to get
	 * interrupt priority or related bits for enabling.
	 */
	tf->tf_attach |= TOFINO_A_INTR_ALLOC;

	ret = ddi_intr_get_cap(tf->tf_intrs[0], &tf->tf_intr_cap);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "failed to get interrupt caps: %d", ret);
		return (B_FALSE);
	}

	ret = ddi_intr_get_pri(tf->tf_intrs[0], &tf->tf_intr_pri);
	if (ret != DDI_SUCCESS) {
		tofino_err(tf, "failed to get interrupt pri: %d", ret);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
tofino_intr_handlers_add(tofino_t *tf)
{
	tofino_log(tf, "adding %d tofino interrupt handlers", tf->tf_nintrs);
	for (int i = 0; i < tf->tf_nintrs; i++) {
		int ret = ddi_intr_add_handler(tf->tf_intrs[i], tofino_intr,
		    tf, (void *)(uintptr_t)i);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "failed to add interrupt handler %d: %d",
			    i, ret);
			for (i--; i >= 0; i--) {
				(void) ddi_intr_remove_handler(tf->tf_intrs[i]);
			}
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

static void
tofino_intr_handlers_rem(tofino_t *tf)
{
	tofino_log(tf, "removing tofino interrupt handlers");
	for (int i = 0; i < tf->tf_nintrs; i++) {
		int ret = ddi_intr_remove_handler(tf->tf_intrs[i]);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "failed to remove interrupt handler "
			    "%d: %d", i, ret);
		}
	}
}

static boolean_t
tofino_intr_enable(tofino_t *tf)
{
	tofino_log(tf, "enabling tofino interrupts");
	if ((tf->tf_intr_cap & DDI_INTR_FLAG_BLOCK) != 0) {
		int ret;

		ret = ddi_intr_block_enable(tf->tf_intrs, tf->tf_nintrs);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "failed to block enable interrupts: %d",
			    ret);
			return (B_FALSE);
		}
	} else {
		for (int i = 0; i < tf->tf_nintrs; i++) {
			int ret = ddi_intr_enable(tf->tf_intrs[i]);
			if (ret != DDI_SUCCESS) {
				tofino_err(tf, "failed to enable interrupt %d: "
				    "%d", i, ret);
				for (i--; i >= 0; i--) {
					(void) ddi_intr_disable(
					    tf->tf_intrs[i]);
				}
				return (B_FALSE);
			}
		}
	}

	return (B_TRUE);
}

static void
tofino_intr_disable(tofino_t *tf)
{
	tofino_log(tf, "disabling tofino interrupts");

	if ((tf->tf_intr_cap & DDI_INTR_FLAG_BLOCK) != 0) {
		int ret;

		ret = ddi_intr_block_disable(tf->tf_intrs, tf->tf_nintrs);
		if (ret != DDI_SUCCESS) {
			tofino_err(tf, "failed to block disable interrupts: %d",
			    ret);
		}
	} else {
		for (int i = 0; i < tf->tf_nintrs; i++) {
			int ret = ddi_intr_disable(tf->tf_intrs[i]);
			if (ret != DDI_SUCCESS) {
				tofino_err(tf, "failed to disable interrupt "
				    "%d: %d", i, ret);
			}
		}
	}
}

static boolean_t
tofino_minor_create(tofino_t *tf)
{
	tofino_open_t *top;
	minor_t m = (minor_t)ddi_get_instance(tf->tf_dip);

	if (ddi_create_minor_node(tf->tf_dip, "tofino", S_IFCHR, m, DDI_PSEUDO,
	    0) != DDI_SUCCESS) {
		tofino_err(tf, "failed to create minor nodes");
		return (B_FALSE);
	}

	if (ddi_soft_state_zalloc(tofino_state, m) == DDI_FAILURE) {
		ddi_remove_minor_node(tf->tf_dip, NULL);
		return (B_FALSE);
	}
	top = ddi_get_soft_state(tofino_state, m);
	mutex_init(&top->to_mutex, NULL, MUTEX_DRIVER, NULL);
	top->to_device = tf;
	top->to_pages = NULL;

	return (B_TRUE);
}

static void
tofino_cleanup(tofino_t *tf)
{
	if (tf->tf_pkt_state != NULL)
		tfpkt_fini(tf);

	if ((tf->tf_attach & TOFINO_A_MINOR) != 0) {
		minor_t m = (minor_t)ddi_get_instance(tf->tf_dip);
		ddi_remove_minor_node(tf->tf_dip, NULL);
		ddi_soft_state_free(tofino_state, m);
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
				tofino_err(tf, "failed to free interrupt %d: "
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

	dev_err(dip, CE_NOTE, "attaching tofino driver");
	if (ddi_get_driver_private(dip) != NULL) {
		dev_err(dip, CE_WARN, "reattaching the driver");
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	if (instance > TOFINO_MAX_INSTANCE) {
		dev_err(dip, CE_WARN, "invalid instance: %d", instance);
		return (DDI_FAILURE);
	}

	tf = kmem_zalloc(sizeof (*tf), KM_SLEEP);
	tf->tf_dip = dip;
	tf->tf_instance = instance;
	ddi_set_driver_private(dip, tf);

	mutex_init(&tf->tf_mutex, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(tf->tf_intr_pri));

	if (pci_config_setup(dip, &tf->tf_cfgspace) != DDI_SUCCESS) {
		tofino_err(tf, "failed to set up pci config space");
		goto cleanup;
	}

	if (!tofino_asic_identify(tf)) {
		goto cleanup;
	}

	if (!tofino_regs_map(tf)) {
		goto cleanup;
	}

	if (!tofino_intr_init(tf)) {
		goto cleanup;
	}

	if (!tofino_intr_handlers_add(tf)) {
		goto cleanup;
	}
	tf->tf_attach |= TOFINO_A_INTR_HANDLERS;

	if (!tofino_intr_enable(tf)) {
		goto cleanup;
	}
	tf->tf_attach |= TOFINO_A_INTR_ENABLE;

	if (!tofino_minor_create(tf)) {
		goto cleanup;
	}
	tf->tf_attach |= TOFINO_A_MINOR;

	ddi_report_dev(dip);
	tofino_log(tf, "%s(): tofino driver attached", __func__);
	return (DDI_SUCCESS);

cleanup:
	tofino_cleanup(tf);
	return (DDI_FAILURE);
}

static int
tofino_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	tofino_open_t *top;
	tofino_t *tf;
	minor_t minor;

	minor = getminor((dev_t)arg);
	if ((top = ddi_get_soft_state(tofino_state, minor)) == NULL)
		return (DDI_FAILURE);

	if ((tf = top->to_device) == NULL)
		return (DDI_FAILURE);


	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*resultp = (void *)tf->tf_dip;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)(uintptr_t)tf->tf_instance;
		break;

	default:
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
tofino_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	tofino_t *tf;

	if (cmd != DDI_DETACH) {
		goto fail1;
	}

	tf = ddi_get_driver_private(dip);
	if (tf == NULL) {
		dev_err(dip, CE_WARN, "asked to detach but no private data");
		goto fail1;
	}

	mutex_enter(&tf->tf_mutex);
	if (tf->tf_pkt_state != NULL &&
	    tf->tf_pkt_state->tfp_pkt_hdlr != NULL) {
		dev_err(dip, CE_WARN, "asked to detach with active handler");
		goto fail2;
	}

	ddi_set_driver_private(dip, NULL);
	tofino_cleanup(tf);

	dev_err(dip, CE_NOTE, "detached");
	return (DDI_SUCCESS);

fail2:
	mutex_exit(&tf->tf_mutex);

fail1:
	return (DDI_FAILURE);
}

static struct cb_ops tofino_cb_ops = {
	.cb_open = tofino_open,
	.cb_close = tofino_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = tofino_read,
	.cb_write = nodev,
	.cb_ioctl = tofino_ioctl,
	.cb_devmap = tofino_devmap,
	.cb_mmap = nodev,
	.cb_segmap = nodev,
	.cb_chpoll = tofino_chpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_flag = D_MP | D_DEVMAP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops tofino_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = tofino_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = tofino_attach,
	.devo_detach = tofino_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_supported,
	.devo_cb_ops = &tofino_cb_ops
};

static struct modldrv tofino_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Tofino ASIC Driver",
	.drv_dev_ops = &tofino_dev_ops
};

static struct modlinkage tofino_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &tofino_modldrv, NULL }
};

int
_init(void)
{
	static const char buildtime[] = "Built " __DATE__ " at " __TIME__ ;
	int err;

	cmn_err(CE_NOTE, "tofino:%s() - %s", __func__, buildtime);
	err = ddi_soft_state_init(&tofino_state, sizeof (tofino_open_t), 0);
	if (err == 0) {
		tofino_minors = id_space_create("tofino_minors",
		    TOFINO_MAX_INSTANCE + 1, UINT16_MAX);

		err =  mod_install(&tofino_modlinkage);
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

	cmn_err(CE_NOTE, "tofino _fini()");
	ddi_soft_state_fini(&tofino_state);
	id_space_destroy(tofino_minors);
	tofino_state = NULL;
	return (0);
}

int
tofino_pkt_register(int dev_id, tofino_pkt_cookie_t *cookiep, void *arg,
    tofino_rx_handler_t rx, tofino_cmp_handler_t cmp)
{
	tofino_t *tf;
	tfpkt_t *tfp;
	int rval = 0;

	tf = tofino_get_device(dev_id);
	if (tf == NULL) {
		cmn_err(CE_NOTE, "%s() no state for %d", __func__, dev_id);
		return (ENXIO);
	}

	mutex_enter(&tf->tf_mutex);
	tfp = tf->tf_pkt_state;
	if (tfp == NULL) {
		rval = ENXIO;
	} else if (tfp->tfp_pkt_hdlr != NULL) {
		/* someone else is already handling the packets */
		rval = EBUSY;
	} else {
		void *p = (void *)tfpkt_reg_handler(tfp, rx, cmp, arg);
		ASSERT(p != NULL);
		*cookiep = p;
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}

int
tofino_pkt_unregister(int dev_id, tofino_pkt_cookie_t cookie)
{
	tofino_t *tf;
	tfpkt_t *tfp;
	int rval = 0;

	tf = tofino_get_device(dev_id);
	if (tf == NULL) {
		cmn_err(CE_NOTE, "%s() no state for %d", __func__, dev_id);
		return (ENXIO);
	}

	mutex_enter(&tf->tf_mutex);
	tfp = tf->tf_pkt_state;
	if (cookie != tfp->tfp_pkt_hdlr) {
		/* the cookie doesn't match the current handler */
		rval = EINVAL;
	} else {
		rval = tfpkt_unreg_handler(tfp, tfp->tfp_pkt_hdlr);
	}
	mutex_exit(&tf->tf_mutex);

	return (rval);
}
