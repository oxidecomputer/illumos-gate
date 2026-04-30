/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Oxide Computer Company
 */

/*
 * Basic AMD IOMMU entry points.
 */

#include <sys/amd_iommu.h>
#include <sys/stdint.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/iommu.h>
#include <sys/ddidmareq.h>
#include <sys/ddi_subrdefs.h>
#include <sys/pcie_impl.h>
#include <sys/amdzen/iommu.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>
#include <sys/stdbool.h>
#include <sys/apic.h>
#include <sys/spl.h>

#include "amd_iommu_cmd.h"
#include "amd_iommu_impl.h"
#include "amd_iommu_intrmap.h"

amd_iommu_segment_t *zen_iommu_segment;

const ddi_dma_attr_t amd_iommu_dma_attr = {
	.dma_attr_version = DMA_ATTR_V0,
	.dma_attr_addr_lo = 0,
	.dma_attr_addr_hi = ~(uint64_t)0,
	.dma_attr_count_max = ~(uint32_t)0,
	.dma_attr_align = 4096,
	.dma_attr_burstsizes = 1,
	.dma_attr_minxfer = 64,
	.dma_attr_maxxfer = ~(uint32_t)0,
	.dma_attr_seg = ~(uint32_t)0,
	.dma_attr_sgllen = 1,
	.dma_attr_granular = 64,
	.dma_attr_flags = 0,
};

/*
 * Initializes per-segment state.  Generally, this means the device table and
 * command and log buffers.
 */
amd_iommu_segment_t *
amd_iommu_segment_alloc(int segno)
{
	const bool sleeping_ok = true;
	amd_iommu_segment_t *segment;
	amd_iommu_dev_tbl_t *devtbl;
	amd_iommu_dte_t *dte;
	pfn_t pfn;
	uint_t spl = ipltospl(ddi_intr_get_hilevel_pri());
	void *lock_pri = (void *)(uintptr_t)spl;

	segment = kmem_zalloc(sizeof (*segment), KM_SLEEP);

	/*
	 * The primary resources shared across a segment are the device table,
	 * and associated per-device interrupt remapping tables.  Event logs,
	 * command buffers, and so on, are per-IOMMU resources, and allocated as
	 * IOMMUs are discovered and assigned to the segment.
	 */
	devtbl = kmem_zalloc(sizeof (*devtbl), KM_SLEEP);
	devtbl->aidt_size = AMD_IOMMU_DEV_TBL_SIZE;
	devtbl->aidt_dev_tbl = contig_alloc(AMD_IOMMU_DEV_TBL_SIZE,
	    &amd_iommu_dma_attr, AMD_IOMMU_DEV_TBL_ALIGN, sleeping_ok);
	bzero(devtbl->aidt_dev_tbl, AMD_IOMMU_DEV_TBL_SIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)devtbl->aidt_dev_tbl);
	devtbl->aidt_phys_addr = mmu_ptob((uint64_t)pfn);
	segment->ais_segment = segno;
	segment->ais_dev_tbl = devtbl;

	/*
	 * Initialize entries.
	 */
	for (size_t k = 0; k < 65536; k++) {
		dte = &devtbl->aidt_dev_tbl[k];
		dte->aide_intr_map_valid = 1;
		dte->aide_int_ctl = AIIC_FWD_NOREMAP;
	}

	/*
	 * Initialize the interrupt remapping tables lock.
	 */
	mutex_init(&segment->ais_intr_remap_tbls_lock, NULL, MUTEX_DRIVER,
	    lock_pri);

	return (segment);
}

static smn_reg_t
amd_iommu_mmio_smn_reg(const zen_ioms_t *const ioms, const smn_reg_def_t def)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	ASSERT3P(ioms, !=, NULL);
	ASSERT3P(ops, !=, NULL);
	ASSERT3P(ops->zfo_iommu_mmio_reg, !=, NULL);
	return (ops->zfo_iommu_mmio_reg(ioms, def));
}

/*
 * Verifies that the IOMMU architecturally supports the features we need for
 * proper operation on Oxide hardware.
 */
static void
amd_iommu_verify_support(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_extfeat_t extfeat = amd_iommu_read_extfeat(iommu);
	VERIFY3U(extfeat.aimr_extfeat_inv_all_sup, ==, 1);
	VERIFY3U(extfeat.aimr_extfeat_x2apic_sup, ==, 1);
	VERIFY3U(extfeat.aimr_extfeat_gvapic_sup, ==, 1);
	VERIFY3U(extfeat.aimr_extfeat_smi_filter_sup, ==, 1);
}

amd_iommu_t *
amd_iommu_segment_iommu_alloc(zen_ioms_t *ioms, amd_iommu_segment_t *segment)
{
	const bool sleeping_ok = true;
	amd_iommu_t *iommu;
	amd_iommu_dev_tbl_t *devtbl;
	amd_iommu_cmd_buf_t *cmdbuf;
	amd_iommu_event_log_t *evtlog;
	pfn_t pfn;
	smn_reg_t reg;
	uint64_t pa;
	uint32_t val;
	uint_t spl = ipltospl(ddi_intr_get_hilevel_pri());
	void *lock_pri = (void *)(uintptr_t)spl;

	ASSERT3P(ioms, !=, NULL);
	ASSERT3P(segment, !=, NULL);

	iommu = kmem_zalloc(sizeof (*iommu), KM_SLEEP);

	/* Allocate and initialize the command buffer. */
	cmdbuf = kmem_zalloc(sizeof (*cmdbuf), KM_SLEEP);
	cmdbuf->aicb_iommu = iommu;
	cmdbuf->aicb_size = AMD_IOMMU_CMD_BUF_SIZE;
	cmdbuf->aicb_cmd_buf = contig_alloc(AMD_IOMMU_CMD_BUF_SIZE,
	    &amd_iommu_dma_attr, AMD_IOMMU_CMD_BUF_ALIGN, sleeping_ok);
	bzero(cmdbuf->aicb_cmd_buf, AMD_IOMMU_CMD_BUF_SIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)cmdbuf->aicb_cmd_buf);
	cmdbuf->aicb_phys_addr = mmu_ptob((uint64_t)pfn);
	mutex_init(&cmdbuf->aicb_cmd_lock, NULL, MUTEX_DRIVER, lock_pri);
	iommu->ai_cmd_buf = cmdbuf;

	/* Allocate and initialize the event log. */
	evtlog = kmem_zalloc(sizeof (*evtlog), KM_SLEEP);
	evtlog->aiel_iommu = iommu;
	evtlog->aiel_size = AMD_IOMMU_EVENT_LOG_SIZE;
	evtlog->aiel_evt_log = contig_alloc(AMD_IOMMU_EVENT_LOG_SIZE,
	    &amd_iommu_dma_attr, AMD_IOMMU_EVENT_LOG_ALIGN, sleeping_ok);
	bzero(evtlog->aiel_evt_log, AMD_IOMMU_EVENT_LOG_SIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)evtlog->aiel_evt_log);
	evtlog->aiel_phys_addr = mmu_ptob((uint64_t)pfn);
	iommu->ai_evt_log = evtlog;

	/* Link this to the segment, and point to the segment and IOMS. */
	iommu->ai_segment = segment;
	iommu->ai_zen_ioms = ioms;
	iommu->ai_next = segment->ais_iommus;
	segment->ais_iommus = iommu;
	++segment->ais_niommus;

	/*
	 * Verify that the IOMMU we have discovered supports all of the
	 * necessary features for our use case.
	 */
	amd_iommu_verify_support(iommu);

	/*
	 * Configure the IOMMU hardware, but do not enable it yet.
	 */

	/* First, set the device table address. */
	devtbl = segment->ais_dev_tbl;
	ASSERT3P(devtbl, !=, NULL);
	pa = devtbl->aidt_phys_addr;
	VERIFY3U(pa, !=, 0);

	reg = amd_iommu_mmio_smn_reg(ioms, D_IOMMU_MMIO_DEVTBL_BASE_1);
	val = IOMMU_MMIO_DEVTBL_BASE_1_SET_BASE_HI(0, bitx64(pa, 51, 32));
	zen_ioms_write(ioms, reg, val);

	reg = amd_iommu_mmio_smn_reg(ioms, D_IOMMU_MMIO_DEVTBL_BASE_0);
	val = IOMMU_MMIO_DEVTBL_BASE_0_SET_BASE_LO(0, bitx64(pa, 31, 12));
	val = IOMMU_MMIO_DEVTBL_BASE_0_SET_DEV_TBL_SIZE(val,
	    IOMMU_MMIO_DEVTBL_SIZE_2MIB);
	zen_ioms_write(ioms, reg, val);

	/* Set the command buffer address. */
	pa = cmdbuf->aicb_phys_addr;
	reg = amd_iommu_mmio_smn_reg(ioms, D_IOMMU_MMIO_CMD_BASE_1);
	val = IOMMU_MMIO_CMD_BASE_1_SET_BASE_HI(0, bitx64(pa, 51, 32));
	val = IOMMU_MMIO_CMD_BASE_1_SET_COM_LEN(val,
	    IOMMU_MMIO_CMD_SIZE_512KIB);
	zen_ioms_write(ioms, reg, val);

	reg = amd_iommu_mmio_smn_reg(ioms, D_IOMMU_MMIO_CMD_BASE_0);
	val = IOMMU_MMIO_CMD_BASE_0_SET_BASE_LO(0, bitx64(pa, 31, 12));
	zen_ioms_write(ioms, reg, val);

	/* Set the event log address. */
	pa = evtlog->aiel_phys_addr;
	reg = amd_iommu_mmio_smn_reg(ioms, D_IOMMU_MMIO_EVENT_BASE_1);
	val = IOMMU_MMIO_EVENT_BASE_1_SET_BASE_HI(0, bitx64(pa, 51, 32));
	val = IOMMU_MMIO_EVENT_BASE_1_SET_EVENT_LEN(val,
	    IOMMU_MMIO_EVENT_SIZE_512KIB);
	zen_ioms_write(ioms, reg, val);

	reg = amd_iommu_mmio_smn_reg(ioms, D_IOMMU_MMIO_EVENT_BASE_0);
	val = IOMMU_MMIO_EVENT_BASE_0_SET_BASE_LO(0, bitx64(pa, 31, 12));
	zen_ioms_write(ioms, reg, val);

	return (iommu);
}

/*
 * Reads an unsigned 64-bit integer from a lo/hi pair of SMN registers.
 * The registers are given in little-endian order, and read from low to
 * high.
 */
static void
amd_iommu_smn_read_reg64(amd_iommu_t *iommu, void *dst,
    const smn_reg_def_t regs[2])
{
	zen_ioms_t *ioms;
	uint64_t val;
	uint32_t lo, hi;
	smn_reg_t reg;

	VERIFY3P(iommu, !=, NULL);
	ioms = iommu->ai_zen_ioms;
	VERIFY3P(ioms, !=, NULL);

	reg = amd_iommu_mmio_smn_reg(ioms, regs[0]);
	lo = zen_ioms_read(ioms, reg);
	reg = amd_iommu_mmio_smn_reg(ioms, regs[1]);
	hi = zen_ioms_read(ioms, reg);
	val = ((uint64_t)hi << 32) | (uint64_t)lo;
	bcopy(&val, dst, sizeof (uint64_t));
}

/*
 * Writes an unsigned 64-bit integer to a lo/hi pair of SMN registers.
 * The registers are given in little-endian order, and written high first
 * and then low.
 */
static void
amd_iommu_smn_write_reg64(amd_iommu_t *iommu, void *src,
    const smn_reg_def_t regs[2])
{
	zen_ioms_t *ioms;
	uint32_t lo, hi;
	smn_reg_t reg;
	uint64_t value;

	VERIFY3P(iommu, !=, NULL);
	ioms = iommu->ai_zen_ioms;
	VERIFY3P(ioms, !=, NULL);
	VERIFY3P(src, !=, NULL);
	memcpy(&value, src, sizeof (uint64_t));

	hi = (value >> 32) & UINT32_MAX;
	lo = value & UINT32_MAX;

	reg = amd_iommu_mmio_smn_reg(ioms, regs[1]);
	zen_ioms_write(ioms, reg, hi);
	reg = amd_iommu_mmio_smn_reg(ioms, regs[0]);
	zen_ioms_write(ioms, reg, lo);
}

static amd_iommu_intr_remap_tbl_t *
amd_iommu_intr_remap_tbl_alloc(amd_iommu_segment_t *segment, uint16_t device_id)
{
	const bool sleeping_ok = true;
	amd_iommu_intr_remap_tbl_t *irt;
	amd_iommu_dev_tbl_t *devtbl;
	amd_iommu_dte_t *dte;
	uint64_t pa;
	pfn_t pfn;
	uint_t spl = ipltospl(ddi_intr_get_hilevel_pri());
	void *lock_pri = (void *)(uintptr_t)spl;

	ASSERT3P(segment, !=, NULL);

	irt = kmem_zalloc(sizeof (*irt), KM_SLEEP);
	mutex_init(&irt->aiirt_alloc_lock, NULL, MUTEX_DRIVER, lock_pri);
	irt->aiirt_device_id = device_id;
	irt->aiirt_intr_tbl = contig_alloc(AMD_IOMMU_INTR_REMAP_TBL_SIZE,
	    &amd_iommu_dma_attr, AMD_IOMMU_INTR_REMAP_TBL_ALIGN, sleeping_ok);
	bzero(irt->aiirt_intr_tbl, AMD_IOMMU_INTR_REMAP_TBL_SIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)irt->aiirt_intr_tbl);
	pa = mmu_ptob((uint64_t)pfn);
	irt->aiirt_phys_addr = pa;
	irt->aiirt_segment = segment;
	irt->aiirt_next = segment->ais_intr_remap_tbls;
	irt->aiirt_private = amd_iommu_intrmap_intr_private_alloc();
	segment->ais_intr_remap_tbls = irt;
	++segment->ais_intr_remap_ntbls;

	/*
	 * Now that the IRT has been created, make sure that the DTE for the
	 * given device_id refers to it.  We set the table base and length,
	 * and mark it valid.
	 */
	devtbl = segment->ais_dev_tbl;
	VERIFY3P(devtbl, !=, NULL);
	dte = &devtbl->aidt_dev_tbl[device_id];
	VERIFY3U(dte->aide_int_ctl, !=, AIIC_FWD_REMAP);
	dte->aide_intr_tbl_lo = bitx64(pa, 31, 6);
	dte->aide_intr_tbl_hi = bitx64(pa, 51, 32);
	dte->aide_intr_tbl_len = AMD_IOMMU_MAX_INTR_LOG2;
	dte->aide_int_ctl = AIIC_FWD_REMAP;
	membar_producer();
	dte->aide_intr_map_valid = 1;
	membar_producer();

	return (irt);
}

/*
 * Finds or allocates an interrupt remapping table for the given DeviceID.
 */
amd_iommu_intr_remap_tbl_t *
amd_iommu_intr_remap_tbl_find(amd_iommu_segment_t *segment, uint16_t device_id)
{
	VERIFY3P(segment, !=, NULL);
	for (amd_iommu_intr_remap_tbl_t *irt = segment->ais_intr_remap_tbls;
	    irt != NULL;
	    irt = irt->aiirt_next) {
		if (irt->aiirt_device_id == device_id)
			return (irt);
	}
	return (NULL);
}

static int
amd_iommu_ioms_cb(zen_ioms_t *ioms, void *arg)
{
	ASSERT3P(ioms, !=, NULL);
	if (ioms->zio_iohctype != ZEN_IOHCT_LARGE)
		return (0);
	ASSERT3P(arg, !=, NULL);
	amd_iommu_segment_t *segment = arg;
	amd_iommu_t *iommu = amd_iommu_segment_iommu_alloc(ioms, segment);
	VERIFY3P(iommu, !=, NULL);

	return (0);
}

static const smn_reg_def_t amd_iommu_extfeat_smn_regs[2] = {
    D_IOMMU_MMIO_EFR_0,
    D_IOMMU_MMIO_EFR_1,
};

amd_iommu_mmio_reg_extfeat_t
amd_iommu_read_extfeat(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_extfeat_t extfeat;
	amd_iommu_smn_read_reg64(iommu, &extfeat, amd_iommu_extfeat_smn_regs);
	return (extfeat);
}

static const smn_reg_def_t amd_iommu_ctl_smn_regs[2] = {
    D_IOMMU_MMIO_CTL_0,
    D_IOMMU_MMIO_CTL_1,
};

amd_iommu_mmio_reg_ctl_t
amd_iommu_read_ctl(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_ctl_t ctl;
	amd_iommu_smn_read_reg64(iommu, &ctl, amd_iommu_ctl_smn_regs);
	return (ctl);
}

void
amd_iommu_write_ctl(amd_iommu_t *iommu, amd_iommu_mmio_reg_ctl_t ctl)
{
	amd_iommu_smn_write_reg64(iommu, &ctl, amd_iommu_ctl_smn_regs);
}

static const smn_reg_def_t amd_iommu_status_smn_regs[2] = {
    D_IOMMU_MMIO_STATUS_0,
    D_IOMMU_MMIO_STATUS_1,
};

amd_iommu_mmio_reg_status_t
amd_iommu_read_status(amd_iommu_t *iommu)
{
	amd_iommu_mmio_reg_status_t status;
	amd_iommu_smn_read_reg64(iommu, &status, amd_iommu_status_smn_regs);
	return (status);
}

void
amd_iommu_write_status(amd_iommu_t *iommu,
    amd_iommu_mmio_reg_status_t status)
{
	amd_iommu_smn_write_reg64(iommu, &status, amd_iommu_status_smn_regs);
}

static const smn_reg_def_t amd_iommu_cmd_buf_head_regs[2] = {
    D_IOMMU_MMIO_CMD_BUF_HDPTR_0,
    D_IOMMU_MMIO_CMD_BUF_HDPTR_1,
};

size_t
amd_iommu_read_cmd_buf_head(amd_iommu_cmd_buf_t *cmdbuf)
{
	VERIFY3P(cmdbuf, !=, NULL);
	amd_iommu_t *iommu = cmdbuf->aicb_iommu;
	uint64_t offset;
	amd_iommu_smn_read_reg64(iommu, &offset, amd_iommu_cmd_buf_head_regs);
	size_t index = offset >> 4;
	ASSERT3U(index, <, AMD_IOMMU_CMD_BUF_NENTS);
	return (index);
}

/*
 * Writes the command buffer doorbell register.
 *
 * Note that the upper 32-bits of the doorbell are reserved and read-only,
 * so we only write the lower 32-bits.
 */
void
amd_iommu_write_cmd_buf_tail(amd_iommu_cmd_buf_t *cmdbuf, size_t index)
{
	VERIFY3P(cmdbuf, !=, NULL);
	amd_iommu_t *iommu = cmdbuf->aicb_iommu;
	VERIFY3P(iommu, !=, NULL);
	zen_ioms_t *ioms = iommu->ai_zen_ioms;
	VERIFY3P(ioms, !=, NULL);
	ASSERT3U(index, <, AMD_IOMMU_CMD_BUF_NENTS);
	uint64_t offset = index << 4;
	smn_reg_t reg = amd_iommu_mmio_smn_reg(ioms,
	    D_IOMMU_MMIO_CMD_BUF_TAILPTR_0);
	zen_ioms_write(ioms, reg, offset);
}

/*
 * Early initialization mostly allocates space for the device table.
 */
static void
amd_iommu_init(void)
{
	amd_iommu_t *iommus = NULL;
	zen_iommu_segment = amd_iommu_segment_alloc(0);
	(void) zen_walk_ioms(amd_iommu_ioms_cb, zen_iommu_segment);

	/*
	 * Set up the control registers for each IOMMU so that interrupt
	 * remapping is set up, to generate interrupts using the x2APIC (giving
	 * the full 32-bit APIC ID), to use the SMI filters (which we leave as
	 * all zeros, this suppressing device-initiated SMIs), and enabling the
	 * command buffer.  None of this takes effect until the IOMMU is
	 * actually enabled.
	 *
	 * When walking the IOMMUs, the callback pushes the discovered IOMMUs
	 * onto the segment's list in reverse order, so reverse the list to have
	 * them in the forward order.  This is not strictly necessary.
	 */
	for (amd_iommu_t *next = NULL, *iommu = zen_iommu_segment->ais_iommus;
	    iommu != NULL;
	    iommu = next) {
		amd_iommu_mmio_reg_ctl_t ctl = amd_iommu_read_ctl(iommu);
		VERIFY3U(ctl.aimr_ctl_en, ==, 0);
		ctl.aimr_ctl_cmd_buf_en = 1;
		ctl.aimr_ctl_ga_en = 1;
		ctl.aimr_ctl_smi_filter_en = 1;
		ctl.aimr_ctl_x2apic_en = 1;
		ctl.aimr_ctl_x2apic_int_en = 1;
		amd_iommu_write_ctl(iommu, ctl);

		next = iommu->ai_next;
		iommu->ai_next = iommus;
		iommus = iommu;
	}
	zen_iommu_segment->ais_iommus = iommus;
}

/*
 * IOMMU-specific device initialization.  When we discover a bridge, either with
 * a device, or hot-pluggable, we allocate an IRT for it and record it here.  We
 * do this now, rather than at interrupt allocation time, because interrupt
 * allocation happens with interrupts disabled and while holding a mutex: if we
 * were to block (e.g., in a `KM_SLEEP`-style allocation) then we could
 * deadlock.  Note that we only call this for PCIe devices; IOAPICs are handled
 * explicitly from `init`.
 */
static void
amd_iommu_dev_init(dev_info_t *dip)
{
	uint16_t device_id;

	VERIFY3P(dip, !=, NULL);
	VERIFY3P(PCIE_DIP2BUS(dip), !=, NULL);
	device_id = PCIE_DIP2BUS(dip)->bus_bdf;
	VERIFY3U(device_id, !=, PCIE_INVALID_BDF);
	mutex_enter(&zen_iommu_segment->ais_intr_remap_tbls_lock);
	if (amd_iommu_intr_remap_tbl_find(zen_iommu_segment, device_id) == NULL)
		amd_iommu_intr_remap_tbl_alloc(zen_iommu_segment, device_id);
	mutex_exit(&zen_iommu_segment->ais_intr_remap_tbls_lock);
}
static void
amd_iommu_startup(void)
{
	for (amd_iommu_t *iommu = zen_iommu_segment->ais_iommus;
	    iommu != NULL;
	    iommu = iommu->ai_next) {
		amd_iommu_mmio_reg_ctl_t ctl = amd_iommu_read_ctl(iommu);
		ctl.aimr_ctl_en = 1;
		amd_iommu_write_ctl(iommu, ctl);
	}
	for (int i = 0; i < MAX_IO_APIC; i++) {
		uint16_t device_id = apic_io_iommu_devid[i];
		if (device_id == 0)
			break;
		amd_iommu_intr_remap_tbl_alloc(zen_iommu_segment, device_id);
	}
	amd_iommu_invalidate_all_segment(zen_iommu_segment);
	amd_iommu_intrmap_os_startup();
}

static void
amd_iommu_physmem_update(uint64_t addr, uint64_t size)
{
	(void) addr;
	(void) size;
}

static int
amd_iommu_quiesce(void)
{
	return (DDI_SUCCESS);
}

static int
amd_iommu_unquiesce(void)
{
	return (DDI_SUCCESS);
}

static psm_iommu_ops_t amd_iommu_ops = {
	.pio_init = amd_iommu_init,
	.pio_dev_init = amd_iommu_dev_init,
	.pio_startup = amd_iommu_startup,
	.pio_physmem_update = amd_iommu_physmem_update,
	.pio_quiesce = amd_iommu_quiesce,
	.pio_unquiesce = amd_iommu_unquiesce,
};

void
psm_iommu_linkage(void)
{
	psm_iommu_ops = &amd_iommu_ops;
}
