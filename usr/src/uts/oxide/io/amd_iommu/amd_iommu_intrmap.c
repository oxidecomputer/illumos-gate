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
 * Functions and data specific to the Interrupt Remapping feature of the AMD
 * IOMMU, for use on systems with LAPIC ID numbers >254.  This allows us to
 * direct device interrupts to CPUs named by those higher-valued LAPIC IDs,
 * presuming use of the x2APIC.
 */

#include <sys/amd_iommu.h>
#include <sys/apic.h>
#include <sys/iommu.h>
#include <sys/smp_impldefs.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/stdbool.h>
#include <sys/sysmacros.h>
#include <vm/hat_i86.h>
#include <sys/pci_cfgacc.h>
#include <sys/pcie_impl.h>

#include "amd_iommu_impl.h"
#include "amd_iommu_intrmap.h"
#include "amd_iommu_cmd.h"

static void *INTRMAP_DISABLE = (void *)~(uintptr_t)0ULL;
static const uint16_t INTRMAP_NOENT = UINT16_MAX;

/*
 * Private state for an interrupt.  This is what is allocated for a given
 * interrupt.
 */
typedef struct amd_iommu_intrmap_intr_private amd_iommu_intrmap_intr_private_t;
typedef struct amd_iommu_intrmap_intr_private {
	amd_iommu_intrmap_intr_private_t *aiiip_next;
	amd_iommu_intr_remap_tbl_t *aiiip_intrmap_tbl;
	uint16_t aiiip_irte_index;
	uint16_t aiiip_irte_oldindex;
} amd_iommu_intrmap_intr_private_t;

void *
amd_iommu_intrmap_intr_private_alloc(void)
{
	amd_iommu_intrmap_intr_private_t *intrs, *intr, *next;
	size_t i;

	intrs = kmem_zalloc(
	    sizeof (amd_iommu_intrmap_intr_private_t) * AMD_IOMMU_MAX_INTR,
	    KM_SLEEP);

	next = NULL;
	i = AMD_IOMMU_MAX_INTR;
	while (i-- > 0) {
		intr = intrs + i;
		intr->aiiip_next = next;
		next = intr;
	}

	return (intrs);
}

typedef enum amd_iommu_intrmap_state {
	AMD_IOMMU_INTRMAP_DISABLED,
	AMD_IOMMU_INTRMAP_ENABLED,
	AMD_IOMMU_INTRMAP_SET_UP,
} amd_iommu_intrmap_state_t;

static amd_iommu_intrmap_state_t amd_iommu_intrmap_state =
    AMD_IOMMU_INTRMAP_ENABLED;

/*
 * A tuneable flag to disable interrupt remapping, that can be
 * set in early boot options.
 */
bool amd_iommu_disable_intrmap = false;

static bool
amd_iommu_intrmap_disabled(void)
{
	return (amd_iommu_disable_intrmap ||
	    amd_iommu_intrmap_state == AMD_IOMMU_INTRMAP_DISABLED);
}

/*
 * Set up interrupt remapping on systems that support >255 CPUs.  Note, we only
 * do this on systems that use the x2APIC; otherwise, there is no real point to
 * using it.
 */
static int
amd_iommu_intrmap_init(int apic_mode)
{
	if (apic_mode != LOCAL_X2APIC)
		return (DDI_FAILURE);

	if (amd_iommu_intrmap_disabled()) {
		amd_iommu_intrmap_state = AMD_IOMMU_INTRMAP_DISABLED;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static void
amd_iommu_intrmap_enable(int suppress_broadcast_eoi __unused)
{
	amd_iommu_intrmap_state = AMD_IOMMU_INTRMAP_ENABLED;
}

static bool
intrmap_is_free(uint32_t *map, size_t offset, int count)
{
	for (size_t k = offset; k < offset + count; k++) {
		size_t i = k / 32;
		size_t o = k % 32;
		if (bitx32(map[i], o, o) != 0)
			return (false);
	}
	return (true);
}

static void
intrmap_set(uint32_t *map, size_t offset, int count, bool v)
{
	for (size_t k = offset; k < offset + count; k++) {
		size_t i = k / 32;
		size_t o = k % 32;
		map[i] = bitset32(map[i], o, o, v);
	}
}

static void
intrmap_free_entry(amd_iommu_intr_remap_tbl_t *irt, uint16_t index)
{
	amd_iommu_irte_t *irte;

	if (index == INTRMAP_NOENT)
		return;

	VERIFY3P(irt, !=, NULL);
	irte = irt->aiirt_intr_tbl + index;
	irte->aiie_remap_en = 0;
	membar_producer();
	bzero(irte, sizeof (*irte));
	intrmap_set(irt->aiirt_alloc_map, index, 1, false);
}

static void
amd_iommu_alloc_intrmap_tbl_entry(void **intrp, uint16_t device_id,
    uint16_t align, int count)
{
	extern amd_iommu_segment_t *zen_iommu_segment;
	amd_iommu_intrmap_intr_private_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;
	size_t index;

	ASSERT3P(intrp, !=, NULL);
	ASSERT(ISP2(align));
	ASSERT3U(0, <, align);
	ASSERT3U(align, <=, 32);

	mutex_enter(&zen_iommu_segment->ais_intr_remap_tbls_lock);
	irt = amd_iommu_intr_remap_tbl_find(zen_iommu_segment, device_id);
	mutex_exit(&zen_iommu_segment->ais_intr_remap_tbls_lock);
	ASSERT3P(irt, !=, NULL);
	mutex_enter(&irt->aiirt_alloc_lock);
	for (index = 0; (index + count) <= AMD_IOMMU_MAX_INTR; index += align) {
		if (intrmap_is_free(irt->aiirt_alloc_map, index, count)) {
			intrmap_set(irt->aiirt_alloc_map, index, count, true);
			break;
		}
	}
	VERIFY3U(index + count, <=, AMD_IOMMU_MAX_INTR);
	for (size_t k = 0; k < count; k++) {
		if (intrp[k] == NULL || intrp[k] == INTRMAP_DISABLE) {
			intr = irt->aiirt_private;
			irt->aiirt_private = intr->aiiip_next;
			intr->aiiip_irte_oldindex = INTRMAP_NOENT;
			intr->aiiip_irte_index = INTRMAP_NOENT;
			intr->aiiip_intrmap_tbl = irt;
			intrp[k] = intr;
		}
		intr = intrp[k];
		VERIFY3P(intr->aiiip_intrmap_tbl, ==, irt);
		VERIFY3U(intr->aiiip_irte_index, ==, INTRMAP_NOENT);
		intr->aiiip_irte_index = index + k;
	}
	mutex_exit(&irt->aiirt_alloc_lock);
}

/*
 * Reap the "old" IRTE slot for this interrupt, if any.  It is possible to
 * rebind a given interrupt, while the interrupt is live and executing; apix
 * handles most of the details for us, and follows a general sequence of
 * allocating a new IRTE, setting it up, enabling it, making the device refer
 * to it; an IRTE is only freed via e.g. `amd_iommu_intrmap_free_entry` when
 * tearing down the interrupt, by which time it is inactive.  Thus, we don't
 * need to atomically update an IRTE in place; it is too large to do so in a
 * single operation without introducing a new 128-bit atomic primitive, and the
 * destination fields are spread across too many words in the entry with
 * smaller writes otherwise.  Furthermore, even with a 128-bit primitive (such
 * as `cmpxchg16b`), we have no guarantee that the IOMMU fetches at the same
 * granularity or obeys the same coherence protocol required for such.
 *
 * But, apix reuses the array of private data during this procedure.  So, while
 * allocating an entry, we must take care to note whether the entry was already
 * in use; if so, we are rebinding, and we'll need to deallocate the "old"
 * entry, but where?
 *
 * To handle this case, we cache the "old" IRTE index but do not immediately
 * free it, as it is still in use.  Instead, if during allocation we find that
 * the interrupt already has an old cached index, then we observe that that must
 * be cached from a previous allocation for this interrupt, and references to it
 * were subsequently removed, so it cannot be in use, and is therefore safe to
 * reinitialize, and we reap it then.  In addition, we also check for a cached
 * entry in the free operation, when the interrupt is no longer in use at all,
 * and similarly deallocate it then if found.
 *
 * In the worst case, this could mean that up to `2n` IRTE entries are allocated
 * for `n` interrupts in the system, but we have sized the number of interrupts
 * in IRTEs to be large and believe the available number is more than adequate
 * for any conceivable configuration we might ship.
 */
static void
intrmap_reap_old(void *private)
{
	amd_iommu_intrmap_intr_private_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;

	ASSERT3P(private, !=, NULL);
	if (private == INTRMAP_DISABLE)
		return;
	intr = private;
	irt = intr->aiiip_intrmap_tbl;
	VERIFY3P(irt, !=, NULL);
	mutex_enter(&irt->aiirt_alloc_lock);
	intrmap_free_entry(irt, intr->aiiip_irte_oldindex);
	intr->aiiip_irte_oldindex = intr->aiiip_irte_index;
	intr->aiiip_irte_index = INTRMAP_NOENT;
	mutex_exit(&irt->aiirt_alloc_lock);
}

static void
amd_iommu_intrmap_alloc_entry(void **privatep, dev_info_t *dip,
    uint16_t type, int count, uchar_t ioapic_index)
{
	uint16_t device_id, align;

	VERIFY3P(privatep, !=, NULL);
	for (int k = 0; k < count; k++) {
		if (privatep[k] == NULL)
			privatep[k] = INTRMAP_DISABLE;
		intrmap_reap_old(privatep[k]);
	}
	if (amd_iommu_intrmap_disabled())
		return;
	align = 1;
	if (type == DDI_INTR_TYPE_MSI) {
		ASSERT(ISP2(count));
		align = count;
	}
	if (DDI_INTR_IS_MSI_OR_MSIX(type)) {
		VERIFY3P(dip, !=, NULL);
		VERIFY3P(PCIE_DIP2BUS(dip), !=, NULL);
		device_id = PCIE_DIP2BUS(dip)->bus_bdf;
	} else {
		device_id = apic_io_iommu_devid[ioapic_index];
	}
	amd_iommu_alloc_intrmap_tbl_entry(privatep, device_id, align, count);
}

/*
 * Creates a remapping table entry from the given information, including
 * private information about the interrupt itself.  Note, we can handle
 * remapping of interrupts routed through an IOAPIC or MSI/MSI-X only; in the
 * Oxide architecture, there is no legacy 8259A-based PIC.
 */
static void
amd_iommu_intrmap_map_entry(void *private, void *data, uint16_t type, int count)
{
	amd_iommu_intrmap_intr_private_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;
	amd_iommu_irte_t *irte;
	uint32_t dest;
	uint8_t vector;


	if (amd_iommu_intrmap_disabled() || private == INTRMAP_DISABLE)
		return;

	intr = private;
	VERIFY3P(intr, !=, NULL);
	irt = intr->aiiip_intrmap_tbl;
	VERIFY3P(irt, !=, NULL);
	VERIFY3P(irt->aiirt_intr_tbl, !=, NULL);
	VERIFY3U(intr->aiiip_irte_index, <, AMD_IOMMU_MAX_INTR);
	VERIFY3S(0, <, count);
	VERIFY3U(count, <, AMD_IOMMU_MAX_INTR);
	VERIFY3U(intr->aiiip_irte_index, <=, AMD_IOMMU_MAX_INTR - count);
	irte = &irt->aiirt_intr_tbl[intr->aiiip_irte_index];
	amd_iommu_segment_t *segment = irt->aiirt_segment;
	ASSERT3P(segment, !=, NULL);

	/*
	 * We differentiate between MSI/MSI-X and the IOAPIC only for extracting
	 * the fields we program into the IRTE.
	 */
	if (DDI_INTR_IS_MSI_OR_MSIX(type)) {
		msi_regs_t *msi = data;
		dest = msi->mr_addr;
		vector = bitx32(msi->mr_data, 7, 0);
	} else {
		ioapic_rdt_t *ioapic = data;
		dest = ioapic->ir_hi;
		vector = RDT_VECTOR(ioapic->ir_lo);
	}
	for (int i = 0; i < count; i++, irte++) {
		VERIFY0(irte->aiie_remap_en);
		irte->aiie_sup_io_pf = 1;
		irte->aiie_rq_eoi = 0;
		irte->aiie_int_type = MSI_DATA_DELIVERY_FIXED;
		irte->aiie_dm = MSI_ADDR_DM_PHYSICAL;
		irte->aiie_guest_mode = 0;
		irte->aiie_dest_0_23 = bitx32(dest, 23, 0);
		irte->aiie_dest_24_31 = bitx32(dest, 31, 24);
		irte->aiie_vector = vector++;
		membar_producer();
		irte->aiie_remap_en = 1;
	}
	membar_producer();
	amd_iommu_invalidate_all_segment(segment);
}

/*
 * Frees an IRTE entry.  Note that at this point, the entry must be unreferenced
 * and disabled.  Note also that there are potentially two entries that we must
 * free: as mentioned in the comments about `intrmap_reap_old`, if a vector was
 * _rebound_ while in use, then we cached its old IRTE index at the time of
 * allocating for the rebind and must be freed here in addition to the live
 * entry.
 */
static void
amd_iommu_intrmap_free_entry(void **privatep)
{
	amd_iommu_intrmap_intr_private_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;
	amd_iommu_segment_t *segment;

	if (amd_iommu_intrmap_disabled() || privatep == NULL ||
	    *privatep == INTRMAP_DISABLE)
		return;

	intr = *privatep;
	VERIFY3P(intr, !=, NULL);
	irt = intr->aiiip_intrmap_tbl;
	VERIFY3P(irt, !=, NULL);
	mutex_enter(&irt->aiirt_alloc_lock);
	segment = irt->aiirt_segment;
	VERIFY3P(segment, !=, NULL);
	intrmap_free_entry(irt, intr->aiiip_irte_index);
	intrmap_free_entry(irt, intr->aiiip_irte_oldindex);
	*privatep = INTRMAP_DISABLE;
	amd_iommu_invalidate_all_segment(segment);
	intr->aiiip_next = irt->aiirt_private;
	irt->aiirt_private = intr;
	mutex_exit(&irt->aiirt_alloc_lock);
}

static void
amd_iommu_record_rdt_passthru(void *private __unused, ioapic_rdt_t *rdt)
{
	rdt->ir_hi = bitx32(rdt->ir_hi, 7, 0) << APIC_ID_BIT_OFFSET;
}

static void
amd_iommu_record_rdt_intrmap(void *private, ioapic_rdt_t *rdt)
{
	VERIFY3P(private, !=, NULL);
	VERIFY3P(rdt, !=, NULL);
	amd_iommu_intrmap_intr_private_t *intr = private;
	rdt->ir_lo = bitset32(rdt->ir_lo, 10, 0, intr->aiiip_irte_index);
	rdt->ir_hi = 0;
}

static void
amd_iommu_record_rdt(void *private, ioapic_rdt_t *rdt)
{
	if (amd_iommu_intrmap_disabled() || private == INTRMAP_DISABLE) {
		amd_iommu_record_rdt_passthru(private, rdt);
		return;
	}

	amd_iommu_record_rdt_intrmap(private, rdt);
}

static void
amd_iommu_record_msi_passthru(void *private, msi_regs_t *regs)
{
	(void) private;
	regs->mr_addr = MSI_ADDR_HDR |
	    (MSI_ADDR_RH_FIXED << MSI_ADDR_RH_SHIFT) |
	    (MSI_ADDR_DM_PHYSICAL << MSI_ADDR_DM_SHIFT) |
	    (regs->mr_addr << MSI_ADDR_DEST_SHIFT);
	regs->mr_data = (MSI_DATA_TM_EDGE << MSI_DATA_TM_SHIFT) |
	    regs->mr_data;
}

/*
 * When remapping an interrupt sent via MSI, the IOMMU derives the index for the
 * interrupt remapping table entry to use for that interrupt from the the low 11
 * bits of the PCI MSI data word.  The MSI address does not participate in the
 * IRT remapping protocol.  By convention, we format it to look like a normal
 * MSI address.
 */
static void
amd_iommu_record_msi_intrmap(void *private, msi_regs_t *regs)
{
	ASSERT3P(regs, !=, NULL);
	VERIFY3P(private, !=, NULL);
	amd_iommu_intrmap_intr_private_t *intr = private;
	regs->mr_addr = MSI_ADDR_HDR;
	regs->mr_data = intr->aiiip_irte_index;
}

static void
amd_iommu_record_msi(void *private, msi_regs_t *regs)
{
	if (amd_iommu_intrmap_disabled() || private == INTRMAP_DISABLE) {
		amd_iommu_record_msi_passthru(private, regs);
		return;
	}

	amd_iommu_record_msi_intrmap(private, regs);
}

struct apic_intrmap_ops amd_iommu_intrmap_ops = {
	.apic_intrmap_init = amd_iommu_intrmap_init,
	.apic_intrmap_enable = amd_iommu_intrmap_enable,
	.apic_intrmap_alloc_entry = amd_iommu_intrmap_alloc_entry,
	.apic_intrmap_map_entry = amd_iommu_intrmap_map_entry,
	.apic_intrmap_free_entry = amd_iommu_intrmap_free_entry,
	.apic_intrmap_record_rdt = amd_iommu_record_rdt,
	.apic_intrmap_record_msi = amd_iommu_record_msi,
};

void
amd_iommu_intrmap_os_startup(void)
{
	psm_vt_ops = &amd_iommu_intrmap_ops;
}
