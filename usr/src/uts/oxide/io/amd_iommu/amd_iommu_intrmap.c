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
 *
 * Our principal concern here is interrupts that are remapped through the
 * IOMMU; let us assume this case in the following discussion.
 *
 * The IOMMU remaps interrupts by referring to an "Interrupt Remapping Table"
 * for the device ID of each device (PCIe function using MSI/MSI-X, whether a
 * bridge or endpoint, or peripheral interrupting through an IOAPIC).  When a
 * device generates an interrupt, the IOMMU looks that device up in its Device
 * Table (DT), using the device's DeviceID as the DT index; for PCIe functions,
 * the DeviceID is the function's BDF.  For devices interrupting through the
 * IOAPIC, the DeviceID refers to the IOAPIC itself.
 *
 * The corresponding Device Table Entry (DTE) has settable bits that control how
 * interrupts are handled, based on their type; for example, Fixed interrupts
 * are handled differently than INITs or SIPIs, NMIs, LINT*, etc; these may be
 * independently suppressed, redirected, and so on.  SMIs can be subjected to
 * passing through filters programmed in the IOMMU, that may similarly drop
 * or log them.
 *
 * When a device interrupts, the device's DTE must have been programmed with a
 * pointer to an Interrupt Remapping Table (IRT), which has entries for each
 * interrupt that the device can generate; to locate the IRT entry (IRTE) for a
 * given interrupt, the source device is programmed to encode the IRTE index in
 * the interrupt message it generates; the IOMMU intercepts this, extracts the
 * index, and looks the corresponding entry up in the IRT.  The IRTE defines how
 * and where to deliver the interrupt.
 *
 * The manner in which the IRTE index is encoded differs based on whether the
 * interrupt arrives from the IOAPIC or from an MSI/MSI-X message.  In the
 * latter case, the index is in the low 11 bits of the MSI data and the MSI
 * address is ignored; in the former case, the low 11 bits of the low half of
 * the IOAPIC's redirection table entry for the source device are set to the
 * IRTE index.
 *
 * For simplicity, we give each device its own IRT, even though it is unlikely
 * to ever generate anywhere near the full number of distinct interrupts we
 * provide room for.
 *
 * Within an IRT, allocation of IRTEs is carefully managed to anticipate the
 * maximum needs of a device; in particular, we allocate contiguous spans of
 * entries, where the starting index of such spans is aligned to a power of 2,
 * to accommodate the possibility of a device that generates multiple vectors
 * per e.g., PCI function.
 *
 * To understand this, consider a PCI function that generates Message Signaled
 * Interrupts (MSIs).  The function only has a single data/address pair in the
 * MSI capability, but may generate up to 32 distinct interrupt vectors.  It
 * does this by asking the host to allocate a vector that is aligned to a small
 * power of 2, determined as a function of the maximum number of vectors it can
 * generate, which is advertised via the "Multi Message Capable" field of its
 * Message Control register.  The host then writes the resulting vector into the
 * MSI data register; to generate distinct vectors, the device itself will set
 * the low bits of the data value it writes in the interrupt message.
 *
 * So for example, a function that can generate up to three distinct vectors
 * will request a block of four vectors (the next power of two greater than or
 * equal to three), where the lowest  vector number is a multiple of four.  The
 * first vector will be written to the vector field of the MSI data register,
 * and as the function generates interrupts, it will set the low bits of the
 * vector number itself to signal distinct interrupts.  Note that this does not
 * apply to MSI-X, which exposes a per-function table of per-interrupt data;
 * MSI-X allocates interrupts one at a time.
 *
 * In the IOMMU case, these vectors become IRTE indices, so each can be remapped
 * independently.  But to allow the mechanism to work, we are forced to honor
 * the size and alignment criteria requested by the device.  We use a bitmap to
 * track allocated blocks of vectors.
 *
 * Apix cooperates in this scheme and requests allocation of multiple vectors at
 * a time, but note that it only frees them one at a time.
 *
 * IRTEs and Cookies
 * -----------------
 * apix also allows us to give it an opaque "cookie" for each allocated
 * interrupt, that it passes back to us when it makes a callback.  We use that
 * to track the metadata about the IRTE associated with an interrupt, notably
 * which IRT it comes from, and its index; cookies are allocated from an array
 * that is embedded in the `amd_iommu_intr_remap_tbl_t` structure that points to
 * the actual IRT (as consumed by the IOMMU), the IRTE allocation bitmap, and so
 * on.
 *
 * Cookies are not 1:1 with IRTEs, however.  The system allows us to "rebind"
 * an interrupt, such as when we move handling it from one CPU to another.  apix
 * handles most of the details for us, and follows a general sequence of
 * allocating a new IRTE, setting it up, enabling it, and making the device
 * refer to it, so that the old entry is no longer actively in use and could be
 * reclaimed.  But an IRTE is only freed via e.g. `amd_iommu_intrmap_free_entry`
 * when tearing down the interrupt, by which time we presume apix has arranged
 * things so that the interrupt that the entry being freed corresponds to is
 * inactive.  Thus, we don't need to atomically update an IRTE in place.
 *
 * But, apix reuses the array of private data during this procedure, including
 * while the "old" interrupt entry is still in use (e.g., `RemapEn` is set); we
 * detect this by noting whether an entry is already in use when allocating.
 * The upshot is that allocating an entry requires that we take care to note
 * whether the entry was already in use; if so, we are rebinding, and we will
 * eventually need to deallocate the "old" entry, but when, and where?
 *
 * To handle this case, when we detect that we are rebinding (i.e., the cookie
 * for the interrupt in question is marked as allocated), we cache the current
 * IRTE index as the "old" index but do not immediately free it.  If we find
 * that the interrupt already has an old cached index during allocation, then we
 * deduce that that must be from a previous allocation _for this interrupt_, and
 * references to it were subsequently removed by apix.  Thus, it cannot be in
 * use, and is therefore safe to reap and reinitialize.  We also check for a
 * cached entry in the free operation, when the interrupt is no longer in use at
 * all, and similarly deallocate there.
 *
 * In the worst case, this could mean that up to $2n$ IRTE entries are allocated
 * for $n$ interrupts in the system, but we have sized the number of entries in
 * IRTs to be large, and believe the available number is more than adequate for
 * any conceivable configuration we might ship.
 *
 * Also note that, while a single cookie may refer to a maximum of 2 IRTEs, a
 * single IRTE will never be associated with more than one cookie, and the
 * sizes of the cookie array and IRT are the same.  Thus, when we have
 * successfully allocated an IRTE for an interrupt, we are guaranteed that a
 * cookie for it will be available.
 *
 * Lifecycle of IRTEs and Cookies
 * ------------------------------
 * 0. All IRTEs and Cookies start out unallocated; allocates are made as a
 *    result of requrests from apix, as it allocates interrupts.
 *
 * 1. IRTEs are allocated by `amd_iommu_intrmap_alloc_entry`, called from apix.
 *    Apix may request that an entire block of IRTEs be allocated at once.
 *    For each IRTE allocated, a coresponding cookie will be similarly allocated
 *    that describes the IRTE, and will be given to apix.
 *
 * 2. Once allocated, an interrupt will be remapped by a call to
 *    `amd_iommu_intrmap_map_entry`, which will write the ITRE, enable it by
 *    setting the RemapEn, and flush any state the IOMMUs may have cached about
 *    it by issuing `INVALIDATE_INTERRUPT_TABLE` commands.
 *
 * 3. `amd_iommu_intrmap_record_msi` or `amd_iommu_intrmap_record_rdr` will
 *    generate the appropriate data to program into either an MSI data/adress
 *    pair (or MSI-X table entry), or IOAPIC Interrupt Redirect Table; apix will
 *    do the actual write so that the device actually interrupts.
 *
 * 4. An interrupt may be rebound: for example, `intrd(8)` can redistribute
 *    interrupts between CPUs.  apix does most of the work by allocating a new
 *    interrupt, mapping it, and then enabling it in the device and freeing the
 *    old interrupt.  However, when doing so, it _reuses the existing cookie for
 *    the interrupt_.
 *
 *    If during allocation we notice that apix passes us a pointer to a cookie
 *    that is currently in-use, we know that the interrupt is being rebound, and
 *    take note of the current IRTE index.  If we have already noted an even
 *    earlier entry, it must be from an older rebind that cannot be active
 *    anymore, so we free it.
 *
 *    Once the new IRTE is mapped, we will again issue an
 *    `INVALIDATE_INTERRUPT_TABLE` command to clear cached IOMMU state.
 *
 * 5. When apix is done with an interrupt and tears it down, it will invoke
 *    `amd_iommu_intrmap_free_entry`: we take this as an assurance from apix
 *    that the interrupt is no longer in use by a device.  We use the cookie
 *    to find the interrupt's IRTE index, as well as a potentially unreaped
 *    old index (if the interrupt was rebound), and we free both, by clearing
 *    the IRTE and marking them free in the IRTE allocation bitmap.  The cookie
 *    is returned to the freelist.  We finish by issuing an
 *    `INVALIDATE_INTERRUPT_TABLE` command.
 *
 * Graphically, it looks something like this:
 *
 *        +------+
 *   +--->| Free |
 *   |    +--+---+
 *   |   alloc_entry
 *   |       |
 *   |       v
 *   |  +---------+
 *   |  |Allocated|
 *   |  +----+----+
 *   |   map_entry
 *   |       |
 *   |       v
 *   | +-------------+
 *   | |   Mapped    |
 *   | |(invalidates)|
 *   | +-----+-------+
 *   |     record
 *   |       |
 *   |       v              +---------+         +---------+
 *   |   +--------+         | Has old |         |  Free   |
 *   |   | In Use +-rebind->| index?  +--yes--->|old index|
 *   |   +-+----+-+         +----+----+         +----+----+
 *   |     |    ^                | no                |
 *   |     |    |                v                   |
 *   |     |    |           +---------+              |
 *   |     |    |           |Save old |<-------------+
 *   |     |    |           |  index  |
 *   |     |    |           +----+----+
 *   |     |    |                |
 *   |     |    |                v
 *   |     |    |        +---------------+
 *   |     |    |        | New allocated,|
 *   |     |    +--------+  mapped and   |
 *   |     |             |   recorded    |
 *   |     |             | (invalidates) |
 *   |     |             +---------------+
 *   |  apix tears
 *   |    down
 *   |     |
 *   |     v
 *   | +----------+
 *   | |  Device  |
 *   | | disabled |
 *   | +----+-----+
 *   |  free_entry
 *   | (invalidates)
 *   |      |
 *   +------+
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

static void *INTRMAP_DISABLE = (void *)UINTPTR_MAX;
static const uint16_t INTRMAP_NOENT = UINT16_MAX;

typedef enum amd_iommu_intrmap_state {
	AMD_IOMMU_INTRMAP_DISABLED,
	AMD_IOMMU_INTRMAP_ENABLED,
} amd_iommu_intrmap_state_t;

static amd_iommu_intrmap_state_t amd_iommu_intrmap_state =
    AMD_IOMMU_INTRMAP_ENABLED;

/*
 * A tuneable flag to disable interrupt remapping, that can be
 * set in early boot options.
 */
bool amd_iommu_disable_intrmap = false;

bool
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
intrmap_is_free(ulong_t *map, size_t offset, int count)
{
	for (size_t k = offset; k < offset + count; k++) {
		if (BT_TEST(map, k))
			return (false);
	}
	return (true);
}

static void
intrmap_set(ulong_t *map, size_t offset, int count)
{
	for (size_t k = offset; k < offset + count; k++) {
		BT_SET(map, k);
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
	BT_CLEAR(irt->aiirt_alloc_map, index);
}

static void
amd_iommu_alloc_intrmap_tbl_entry(void **intrp, uint16_t device_id,
    uint16_t align, int count)
{
	extern amd_iommu_segment_t *zen_iommu_segment;
	amd_iommu_intr_cookie_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;
	size_t index;

	ASSERT3P(intrp, !=, NULL);
	ASSERT(ISP2(align));
	ASSERT3U(0, <, align);
	ASSERT3U(align, <=, 32);

	mutex_enter(&zen_iommu_segment->ais_lock);
	irt = amd_iommu_intr_remap_tbl_find(zen_iommu_segment, device_id);
	mutex_exit(&zen_iommu_segment->ais_lock);
	VERIFY3P(irt, !=, NULL);
	mutex_enter(&irt->aiirt_alloc_lock);
	for (index = 0; (index + count) <= AMD_IOMMU_MAX_INTR; index += align) {
		if (intrmap_is_free(irt->aiirt_alloc_map, index, count)) {
			intrmap_set(irt->aiirt_alloc_map, index, count);
			break;
		}
	}
	VERIFY3U(index + count, <=, AMD_IOMMU_MAX_INTR);
	for (size_t k = 0; k < count; k++) {
		if (intrp[k] == NULL || intrp[k] == INTRMAP_DISABLE) {
			intr = list_remove_head(&irt->aiirt_free_cookies);
			VERIFY3P(intr, !=, NULL);
			intr->aiic_irte_oldindex = INTRMAP_NOENT;
			intr->aiic_irte_index = INTRMAP_NOENT;
			intr->aiic_intrmap_tbl = irt;
			intrp[k] = intr;
		}
		intr = intrp[k];
		VERIFY3P(intr->aiic_intrmap_tbl, ==, irt);
		VERIFY3U(intr->aiic_irte_index, ==, INTRMAP_NOENT);
		intr->aiic_irte_index = index + k;
	}
	mutex_exit(&irt->aiirt_alloc_lock);
}

/*
 * Reap the "old" IRTE slot for this interrupt, if any.
 */
static void
intrmap_reap_old(void *private)
{
	amd_iommu_intr_cookie_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;

	ASSERT3P(private, !=, NULL);
	if (private == INTRMAP_DISABLE)
		return;
	intr = private;
	irt = intr->aiic_intrmap_tbl;
	VERIFY3P(irt, !=, NULL);
	mutex_enter(&irt->aiirt_alloc_lock);
	intrmap_free_entry(irt, intr->aiic_irte_oldindex);
	intr->aiic_irte_oldindex = intr->aiic_irte_index;
	intr->aiic_irte_index = INTRMAP_NOENT;
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
	/*
	 * See the theory statement details about the alignment argument value
	 * why we modify it for MSIs.
	 */
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
 * private information about the interrupt itself.
 */
static void
amd_iommu_intrmap_map_entry(void *private, void *data, uint16_t type, int count)
{
	amd_iommu_intr_cookie_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;
	amd_iommu_irte_t *irte;
	uint32_t dest;
	uint8_t vector;


	if (amd_iommu_intrmap_disabled() || private == INTRMAP_DISABLE)
		return;

	intr = private;
	VERIFY3P(intr, !=, NULL);
	irt = intr->aiic_intrmap_tbl;
	VERIFY3P(irt, !=, NULL);
	VERIFY3P(irt->aiirt_intr_tbl, !=, NULL);
	VERIFY3U(intr->aiic_irte_index, <, AMD_IOMMU_MAX_INTR);
	VERIFY3S(0, <, count);
	VERIFY3U(count, <, AMD_IOMMU_MAX_INTR);
	VERIFY3U(intr->aiic_irte_index, <=, AMD_IOMMU_MAX_INTR - count);
	irte = &irt->aiirt_intr_tbl[intr->aiic_irte_index];
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
		irte->aiie_sup_io_pf = 0;
		irte->aiie_rq_eoi = 0;
		irte->aiie_int_type = MSI_DATA_DELIVERY_FIXED;
		irte->aiie_dm = MSI_ADDR_DM_PHYSICAL;
		irte->aiie_guest_mode = 0;
		irte->aiie_dest_lo = bitx32(dest, 23, 0);
		irte->aiie_dest_hi = bitx32(dest, 31, 24);
		irte->aiie_vector = vector++;
		membar_producer();
		irte->aiie_remap_en = 1;
	}
	membar_producer();
	amd_iommu_invalidate_intr_tbl_segment(segment, irt->aiirt_device_id);
	/*
	 * If adding the IRTE caused an event log entry after the invalidation,
	 * the entry will be in the event log configured for the IOMMU that
	 * handles the corresponding device.  However, given that we currently
	 * poll logs, scan them all to pick up any loose events we haven't yet
	 * seen.
	 */
	(void) amd_iommu_process_segment_event_logs(segment);
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
	amd_iommu_intr_cookie_t *intr;
	amd_iommu_intr_remap_tbl_t *irt;
	amd_iommu_segment_t *segment;

	if (amd_iommu_intrmap_disabled() || privatep == NULL ||
	    *privatep == INTRMAP_DISABLE || *privatep == NULL)
		return;

	intr = *privatep;
	VERIFY3P(intr, !=, NULL);
	irt = intr->aiic_intrmap_tbl;
	VERIFY3P(irt, !=, NULL);
	mutex_enter(&irt->aiirt_alloc_lock);
	segment = irt->aiirt_segment;
	VERIFY3P(segment, !=, NULL);
	intrmap_free_entry(irt, intr->aiic_irte_index);
	intrmap_free_entry(irt, intr->aiic_irte_oldindex);
	*privatep = INTRMAP_DISABLE;
	amd_iommu_invalidate_intr_tbl_segment(segment, irt->aiirt_device_id);
	list_insert_head(&irt->aiirt_free_cookies, intr);
	mutex_exit(&irt->aiirt_alloc_lock);
}

static void
amd_iommu_record_rdt_passthru(void *private __unused, ioapic_rdt_t *rdt)
{
	/*
	 * In the passthru case, the IOAPIC will deliver the interrupt directly
	 * to the destination LAPIC, and so we are limited to the format it
	 * uses, with an 8 bit destination APIC ID.
	 */
	rdt->ir_hi = bitx32(rdt->ir_hi, 7, 0) << APIC_ID_BIT_OFFSET;
}

static void
amd_iommu_record_rdt_intrmap(void *private, ioapic_rdt_t *rdt)
{
	VERIFY3P(private, !=, NULL);
	VERIFY3P(rdt, !=, NULL);
	amd_iommu_intr_cookie_t *intr = private;

	/*
	 * When remapping through the IOMMU, the low 11 bits of the redirection
	 * table entry format are redefined to hold the IRTE index for the
	 * interrupt.  Note that the index itself will be smaller than what fits
	 * into 11 bits, but the IOMMU expects an 11-bit field; the high bits
	 * will be zero.
	 */
	rdt->ir_lo = bitset32(rdt->ir_lo, 10, 0, intr->aiic_irte_index);
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
amd_iommu_record_msi_passthru(void *private __unused, msi_regs_t *regs)
{
	regs->mr_addr = MSI_ADDR_HDR |
	    (MSI_ADDR_RH_FIXED << MSI_ADDR_RH_SHIFT) |
	    (MSI_ADDR_DM_PHYSICAL << MSI_ADDR_DM_SHIFT) |
	    (regs->mr_addr << MSI_ADDR_DEST_SHIFT);
	regs->mr_data = (MSI_DATA_TM_EDGE << MSI_DATA_TM_SHIFT) |
	    regs->mr_data;
}

/*
 * When remapping an interrupt sent via MSI, the IOMMU derives the index for the
 * interrupt remapping table entry to use for that interrupt from the low 11
 * bits of the PCI MSI data word.  The MSI address does not participate in the
 * IRT remapping protocol.  By convention, we format it to look like a normal
 * MSI address.  Note that details about the interrupt itself, such as
 * destination mode and so on, are programmed in the IRTE for this interrupt,
 * not the MSI data/address pairs.
 */
static void
amd_iommu_record_msi_intrmap(void *private, msi_regs_t *regs)
{
	ASSERT3P(regs, !=, NULL);
	VERIFY3P(private, !=, NULL);
	amd_iommu_intr_cookie_t *intr = private;
	regs->mr_addr = MSI_ADDR_HDR;
	regs->mr_data = intr->aiic_irte_index;
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
