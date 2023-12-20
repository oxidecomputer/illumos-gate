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
 * This file provides a means of accessing PCIe extended configuration space
 * over memory mapped I/O. Traditionally this was always accessed over the
 * various I/O ports; however, we instead opt to leverage facilities in the CPU
 * to set up memory-mapped I/O. To do this we basically do an initial mapping
 * that we use prior to VM in whatever VA space that we can get. After which,
 * we will unmap that and leverage addresses from the device arena once that has
 * been set up.
 *
 * Configuration space is accessed by constructing an address that has the
 * bits arranged in the following pattern to indicate what the bus, device,
 * function, and register is:
 *
 *	bus[7:0]	addr[27:20]
 *	dev[4:0]	addr[19:15]
 *	func[2:0]	addr[14:12]
 *	reg[11:0]	addr[11:0]
 *
 * The CPU does not generally support 64-bit accesses, which means that a 64-bit
 * access requires us to write the lower 32-bits followed by the upper 32-bits.
 */

#include <sys/machparam.h>
#include <vm/kboot_mmu.h>
#include <vm/as.h>
#include <vm/hat.h>
#include <sys/mman.h>
#include <sys/bootconf.h>
#include <sys/spl.h>
#include <sys/boot_debug.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pcie_impl.h>
#include <sys/pci_cfgacc.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/machsystm.h>
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/fabric.h>
#include <genoa/genoa_physaddrs.h>

/*
 * XXX This section contains variables that the rest of the system expects to
 * have. Their needs should be re-evaluated as we go.
 */
int pci_bios_maxbus = 0xff;

/*
 * These function pointers are entry points that the system has historically
 * assumed to exist. While we only have a single implementation, for now we need
 * to keep the indirect functions.
 *
 * XXX Can we go back and make the arguments for the bdfr unsigned?
 */
uint8_t (*pci_getb_func)(int bus, int dev, int func, int reg);
uint16_t (*pci_getw_func)(int bus, int dev, int func, int reg);
uint32_t (*pci_getl_func)(int bus, int dev, int func, int reg);
void (*pci_putb_func)(int bus, int dev, int func, int reg, uint8_t val);
void (*pci_putw_func)(int bus, int dev, int func, int reg, uint16_t val);
void (*pci_putl_func)(int bus, int dev, int func, int reg, uint32_t val);
extern void (*pci_cfgacc_acc_p)(pci_cfgacc_req_t *req);

/*
 * The pci_cfgacc_req
 */

/*
 * This contains the base virtual address for PCIe configuration space.
 */
static uintptr_t pcie_cfgspace_vaddr;

static boolean_t
pcie_access_check(int bus, int dev, int func, int reg, size_t len)
{
	if (bus < 0 || bus >= PCI_MAX_BUS_NUM) {
		return (B_FALSE);
	}

	if (dev < 0 || dev >= PCI_MAX_DEVICES) {
		return (B_FALSE);
	}

	/*
	 * Due to the advent of ARIs we want to make sure that we're not overly
	 * stringent here. ARIs retool how the bits are used for the device and
	 * function. This means that if dev == 0, allow func to be up to 0xff.
	 */
	if (func < 0 || (dev != 0 && func >= PCI_MAX_FUNCTIONS) ||
	    (dev == 0 && func >= PCIE_ARI_MAX_FUNCTIONS)) {
		return (B_FALSE);
	}

	/*
	 * Technically the maximum register is determined by the parent. At this
	 * point we have no way of knowing what is PCI or PCIe and will rely on
	 * mmio to solve this for us.
	 */
	if (reg < 0 || reg >= PCIE_CONF_HDR_SIZE) {
		return (B_FALSE);
	}

	if (!IS_P2ALIGNED(reg, len)) {
#ifdef	DEBUG
		/*
		 * While there are legitimate reasons we might try to access
		 * nonexistent devices and functions, misaligned accesses are at
		 * least strongly suggestive of kernel bugs.  Let's see what
		 * this finds.
		 */
		cmn_err(CE_WARN, "misaligned PCI config space access at "
		    "%x/%x/%x reg 0x%x len %lu\n", bus, dev, func, reg, len);
#endif
		return (B_FALSE);
	}

	return (B_TRUE);
}

static uintptr_t
pcie_bdfr_to_addr(int bus, int dev, int func, int reg)
{
	uintptr_t bdfr = PCIE_CADDR_ECAM(bus, dev, func, reg);

	return (bdfr + pcie_cfgspace_vaddr);
}

/*
 * Each of our access functions uses inline assembly to perform the direct
 * access to memory-mapped config space.  This is necessary to guarantee that
 * the value to be stored into config space is in %rax or the value to be read
 * from config space will be placed in %rax.  AMD publication 56255 rev. 3.03
 * sec. 2.1.4.1 imposes three requirements for memory-mapped (ECAM) config space
 * accesses:
 *
 * 1. "MMIO configuration space accesses must use the uncacheable (UC) memory
 *    type."
 * 2. "Instructions used to read MMIO configuration space are required to take
 *    the following form:
 *        mov eax/ax/al, any_address_mode;
 *    Instructions used to write MMIO configuration space are required to take
 *    the following form:
 *        mov any_address_mode, eax/ax/al;
 *    No other source/target registers may be used other than eax/ax/al."
 * 3. "In addition, all such accesses are required not to cross any naturally
 *    aligned DW boundary."
 *
 * "Access to MMIO configuration space registers that do not meet these
 * requirements result in undefined behavior."
 *
 * These requirements, or substantially identical phrasings of them, have been
 * carried into all known subsequent PPRs, including those for Rome, Milan, and
 * Genoa processor families.
 *
 * The first of these is guaranteed here by our device mapping (in
 * pcie_cfgspace_{init,remap}() and by hat_devload()) and in the KDI by
 * kdi_prw(); see the comment there for additional details.
 *
 * The second is guaranteed by our use of inline assembly with the "a"
 * constraint: if we are storing to config space, we force gcc to first load
 * from our source buffer into the A register the value to be stored into config
 * space; if we are loading from config space, we force gcc to perform that load
 * using the A register as a target, then store the contents to our destination
 * buffer.
 *
 * The third constraint is guaranteed by pcie_access_check(), except with
 * respect to 64-bit accesses which are not currently used.  Our check is
 * actually slightly more strict than AMD requires: we enforce natural
 * alignment.  This guarantees we satisfy the constraint, but it would also be
 * legal to read a 16-bit quantity at offset 1 from the start of a
 * 4-byte-aligned region.  We don't allow that because it's very unlikely to be
 * useful or correct.
 *
 * The write (store to cfg space) variants may need the store inline assembly to
 * be volatile because the output is not used in the function and we cannot be
 * certain the compiler won't move or eliminate the store.  The read variants
 * return the output so they don't have this problem.
 */

uint8_t
pcie_cfgspace_read_uint8(int bus, int dev, int func, int reg)
{
	volatile uint8_t *u8p;
	uint8_t rv;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (rv))) {
		return (PCI_EINVAL8);
	}

	u8p = (uint8_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__("movb	%1, %0\n" : "=a" (rv) : "m" (*u8p) :);

	return (rv);
}

void
pcie_cfgspace_write_uint8(int bus, int dev, int func, int reg, uint8_t val)
{
	volatile uint8_t *u8p;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (val))) {
		return;
	}

	u8p = (uint8_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__ __volatile__("movb	%1, %0\n" : "=m" (*u8p) : "a" (val) :);
}

uint16_t
pcie_cfgspace_read_uint16(int bus, int dev, int func, int reg)
{
	volatile uint16_t *u16p;
	uint16_t rv;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (rv))) {
		return (PCI_EINVAL16);
	}

	u16p = (uint16_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__("movw	%1, %0\n" : "=a" (rv) : "m" (*u16p) :);

	return (rv);
}

void
pcie_cfgspace_write_uint16(int bus, int dev, int func, int reg, uint16_t val)
{
	volatile uint16_t *u16p;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (val))) {
		return;
	}

	u16p = (uint16_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__ __volatile__("movw	%1, %0\n" : "=m" (*u16p) : "a" (val) :);
}

uint32_t
pcie_cfgspace_read_uint32(int bus, int dev, int func, int reg)
{
	volatile uint32_t *u32p;
	uint32_t rv;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (rv))) {
		return (PCI_EINVAL32);
	}

	u32p = (uint32_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__("movl	%1, %0\n" : "=a" (rv) : "m" (*u32p) :);

	return (rv);
}

void
pcie_cfgspace_write_uint32(int bus, int dev, int func, int reg, uint32_t val)
{
	volatile uint32_t *u32p;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (val))) {
		return;
	}

	u32p = (uint32_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__ __volatile__("movl	%1, %0\n" : "=m" (*u32p) : "a" (val) :);
}

/*
 * XXX Historically only 32-bit accesses were done to configuration space.
 */
uint64_t
pcie_cfgspace_read_uint64(int bus, int dev, int func, int reg)
{
	volatile uint64_t *u64p;
	uint64_t rv;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (rv))) {
		return (PCI_EINVAL64);
	}

	u64p = (uint64_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__("movq	%1, %0\n" : "=a" (rv) : "m" (*u64p) :);

	return (rv);
}

void
pcie_cfgspace_write_uint64(int bus, int dev, int func, int reg, uint64_t val)
{
	volatile uint64_t *u64p;

	if (!pcie_access_check(bus, dev, func, reg, sizeof (val))) {
		return;
	}

	u64p = (uint64_t *)pcie_bdfr_to_addr(bus, dev, func, reg);
	__asm__ __volatile__("movq	%1, %0\n" : "=m" (*u64p) : "a" (val) :);
}

/*
 * The following function is a stub that is expected to exist due to
 * support for older platforms from the old pci_cfgacc_x86.c. Because we don't
 * have the old systems with a broken AMD ECS, we can just make these simple.
 */
void
pci_cfgacc_add_workaround(uint16_t bdf, uchar_t secbus, uchar_t subbus)
{
}

/*
 * This is an entry point that expects accesses in a different pattern from the
 * traditional function pointers used above.
 */
void
pcie_cfgspace_acc(pci_cfgacc_req_t *req)
{
	int bus, dev, func, reg;

	bus = PCI_CFGACC_BUS(req);
	dev = PCI_CFGACC_DEV(req);
	func = PCI_CFGACC_FUNC(req);
	reg = req->offset;

	switch (req->size) {
	case PCI_CFG_SIZE_BYTE:
		if (req->write) {
			pcie_cfgspace_write_uint8(bus, dev, func, reg,
			    VAL8(req));
		} else {
			VAL8(req) = pcie_cfgspace_read_uint8(bus, dev, func,
			    reg);
		}
		break;
	case PCI_CFG_SIZE_WORD:
		if (req->write) {
			pcie_cfgspace_write_uint16(bus, dev, func, reg,
			    VAL16(req));
		} else {
			VAL16(req) = pcie_cfgspace_read_uint16(bus, dev, func,
			    reg);
		}
		break;
	case PCI_CFG_SIZE_DWORD:
		if (req->write) {
			pcie_cfgspace_write_uint32(bus, dev, func, reg,
			    VAL32(req));
		} else {
			VAL32(req) = pcie_cfgspace_read_uint32(bus, dev, func,
			    reg);
		}
		break;
	case PCI_CFG_SIZE_QWORD:
		if (req->write) {
			pcie_cfgspace_write_uint64(bus, dev, func, reg,
			    VAL64(req));
		} else {
			VAL64(req) = pcie_cfgspace_read_uint64(bus, dev, func,
			    reg);
		}
		break;
	default:
		if (!req->write) {
			VAL64(req) = PCI_EINVAL64;
		}
		break;
	}
}

void
pcie_cfgspace_init(void)
{
	uint64_t ecam_base = genoa_fabric_ecam_base();

	/*
	 * This ensures that the boot CPU will be programmed with everything
	 * needed to access PCIe configuration space.
	 */
	genoa_ccx_mmio_init(ecam_base, B_TRUE);

	/*
	 * This is a temporary VA range that we'll use during bootstrapping.
	 * Once we get vmem set up and the device arena allocated, this will be
	 * remapped to a final address.
	 */
	pcie_cfgspace_vaddr = kbm_valloc(PCIE_CFGSPACE_SIZE,
	    PCIE_CFGSPACE_ALIGN);
	KBM_DBGMSG("PCIe configuration space mapped at 0x%lx\n",
	    pcie_cfgspace_vaddr);

	for (uintptr_t offset = 0; offset < PCIE_CFGSPACE_SIZE;
	    offset += PCIE_CFGSPACE_ALIGN) {
		kbm_map(pcie_cfgspace_vaddr + offset, ecam_base + offset, 1,
		    PT_WRITABLE | PT_NOCACHE);
	}

	pci_getb_func = pcie_cfgspace_read_uint8;
	pci_getw_func = pcie_cfgspace_read_uint16;
	pci_getl_func = pcie_cfgspace_read_uint32;
	pci_putb_func = pcie_cfgspace_write_uint8;
	pci_putw_func = pcie_cfgspace_write_uint16;
	pci_putl_func = pcie_cfgspace_write_uint32;
	pci_cfgacc_acc_p = pcie_cfgspace_acc;

	/*
	 * XXX Now that config space is mapped we need to come back and actually
	 * do things like configure completion timeouts and related.
	 */
}

/*
 * This is called once the device arena has been set up. We don't bother
 * unmapping the original bootstrap address range because it will just be torn
 * down when we tear down that hat.
 */
void
pcie_cfgspace_remap(void)
{
	uint64_t ecam_base = genoa_fabric_ecam_base();
	void *new_va = device_arena_alloc(PCIE_CFGSPACE_SIZE, VM_SLEEP);
	pfn_t pfn = mmu_btop(ecam_base);

	hat_devload(kas.a_hat, new_va, PCIE_CFGSPACE_SIZE, pfn,
	    PROT_READ | PROT_WRITE | HAT_STRICTORDER,
	    HAT_LOAD_LOCK | HAT_LOAD_NOCONSIST);
	pcie_cfgspace_vaddr = (uintptr_t)new_va;
}
