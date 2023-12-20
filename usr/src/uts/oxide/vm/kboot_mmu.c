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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2018 Joyent, Inc.
 * Copyright 2023 Oxide Computer Co.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/debug.h>
#include <sys/bootconf.h>
#include <sys/bootsvcs.h>
#include <sys/bootinfo.h>
#include <sys/boot_physmem.h>
#include <sys/mman.h>
#include <sys/cmn_err.h>
#include <sys/param.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/promif.h>
#include <sys/kobj.h>
#include <sys/mach_mmu.h>
#include <sys/boot_debug.h>
#include <vm/kboot_mmu.h>
#include <vm/hat_pte.h>
#include <vm/hat_i86.h>
#include <vm/seg_kmem.h>
#include <sys/sysmacros.h>

static const uint_t SHIFT_AMT[] = { 12, 21, 30, 39 };
uint_t ptes_per_table = 512;		/* consumed by shm */
static const uint_t PTE_SIZE = 8;
static const uint_t TOP_LEVEL = 3;

/*
 * The minimum number of contiguous pagetables for which the loader is required
 * to have allocated space beginning at the root.
 */
static const uint_t MIN_PAGETABLES = 15;

/*
 * Physical address of the root pagetable, as set up by the loader.
 */
static paddr_t top_page_table = 0;

/*
 * window is a page VA that we will map onto pagetables to modify them.  The
 * VA of the PTE for this page is in pte_to_window.
 */
static caddr_t window;
static x86pte_t *pte_to_window;

static void install_ptable(x86pte_t *, void *, paddr_t);

/*
 * XXX delete this; it's consumed by _kobj_boot which we don't need and by
 * startup.c which we control.  It's also constant; 32-bit is dead dead dead.
 */
uint_t kbm_nucleus_size = TWO_MEG;

#define	BOOT_SHIFT(l)	(SHIFT_AMT[l])
#define	BOOT_SZ(l)	((size_t)1 << BOOT_SHIFT(l))
#define	BOOT_OFFSET(l)	(BOOT_SZ(l) - 1)
#define	BOOT_MASK(l)	(~BOOT_OFFSET(l))

/*
 * BTS: oxide boot-time memory management and loader interface
 *
 * This discussion is intended to be read in concert with the one above
 * fakebop.c:_start() as together they really describe our universe and how we
 * plan to move on into more generic code via krtld.
 *
 * The contract with the loader and/or the amd64 architecture require that the
 * following conditions be satisfied when we are called:
 *
 * - %cr3 points to a valid set of pagetables
 * - that set of pagetables is stored exclusively in identity-mapped pages
 * - the page referenced by %cr3 has the lowest address among those pages
 * - at least 16 contiguous pages in that region are usable as pagetables (some
 *   discoverable number of which are already in use)
 * - the stack on which we are called is identity-mapped and contains at least
 *   8 pages
 * - virtual address 0 is unmapped
 * - the PTEs for the kernel nucleus pages have bit 11 (0x800) set, and no
 *   other PTEs have bit 11 set (this bit is not used by the HAT)
 *
 * Taken together, this guarantees us that we can walk the pagetable without a
 * bootstrap PTE for mapping in pagetable pages and in so doing, classify every
 * PTE definitively as one of the following:
 *
 * - kernel nucleus
 * - bootstrap stack
 * - bootstrap pagetable
 * - something else
 *
 * We're going to keep the first three and toss the rest.  In the process, we
 * will also be able to determine conclusively which of the 16 identity-mapped
 * pagetable pages are in use, and from that either (or both):
 *
 * - a page containing level 0 PTEs at the bottom of the VA space exists
 * - there are free pages we can use to create and link in such a pagetable
 *
 * If we wished, we could at this point relocate the kernel's pagetables
 * entirely; however, there would appear to be little advantage to doing so
 * because we are going to need them to be below the 4 GiB boundary anyway to
 * allow for MP startup.  Having the loader do MP startup is a possibility, but
 * it only moves OS code out of the kernel and into some other body of software
 * and requires defining additional interfaces (specifically, what is the state
 * of the APs at the time control is passed?).  These decisions could be
 * revisited later without changing most of this logic at all.
 *
 * One other thing we might later choose to do: that "something else" category
 * probably -- and could be defined to -- includes the kernel's ELF image.
 * Identifying and preserving it would allow some of the krtld code to be
 * simplified, and would eliminate the need to include the kernel in the boot
 * ramdisk image.  Speaking of the ramdisk image, it's probably going to be
 * found somewhere among the miscellany, but we don't need it; krtld will map
 * it in later anyway, so if such a mapping exists now it can be destroyed.
 *
 * Finally, all that groveling around will allow us to create/discover the
 * location of a PTE that maps the lowest virtual page not containing NULL,
 * which we will use as the bootstrap pagetable mapping window with which
 * arbitrary mappings can be created (i.e., pagetables can be created on pages
 * that are not already mapped).
 *
 * This seems like a lot of rigamarole, and it is, but it allows the loader to
 * be very general in its behaviour.  It also allows the loader (and the
 * pagetables and stack) to be located anywhere in the bottom half of the
 * 32-bit physical address space.  This contract is much simpler, more
 * explicit, and yet also dramatically smaller and more self-discoverable than
 * that among Multiboot loaders, dboot, and the i86pc kernel.
 *
 * That essentially covers the virtual side; what of the physical?  In general,
 * most of the very early code for dealing with the physical address space and
 * physical allocations lives in fakebop.c.  We describe it here because it is
 * fairly tightly bound to the previous discussion of the VA space at this
 * moment in time.
 *
 * We assume in the absence of evidence to the contrary that LOADER_PHYSLIMIT
 * is the lowest physical address guaranteed not to be used by any part of the
 * loader we might need to preserve, and that every physical address below it
 * addresses RAM.  That is true on every implementation we know about; it is
 * the lowest address AMD normally allows for the start of the low MMIO window,
 * but it could also be that we are on an extraordinarily tiny machine.  Thus 2
 * GiB is also the absolute minimum amount of RAM this machine type can
 * support.
 *
 * As noted above, the loader itself and its data, including the pagetables
 * we're currently using, is located within that range so that it can be built
 * as a small code model static executable.  To leave ourselves as much space
 * as possible, it locates itself at the top of that range or as nearly so as
 * is practical given the limitations of the amd64 architecture and PSP boot
 * mechanism; below it is the stack on which we are currently executing.
 * Therefore we are free to use all the physical address space below our stack
 * for bootstrap memory.  The bootstrap allocator will allocate these pages
 * from the bottom while our stack grows downward.
 *
 * Thus, at this moment, the physical space looks like this:
 *
 * +------------------------+ 0x7fff_ffff
 * | loader pages including |
 * | our active pagetables  |
 * +------------------------+ %cr3
 * | loader pages and stack |
 * +------------------------+ %rsp
 * | more stack space we're |
 * |  sure to need shortly  |
 * +------------------------+ %rsp - 8 pages, rounded down
 * |   and more free pages  |
 * +------------------------+ somewhere + something
 * |          APOB          |
 * +------------------------+ somewhere
 * |  delicious free pages  |
 * +------------------------+ 0x0600_0000 (from Mapfile.amd64)
 * |   kernel nucleus data  |
 * +------------------------+ 0x0400_0000 (from Mapfile.amd64)
 * |   kernel nucleus text  |
 * +------------------------+ 0x0200_0000 (from Mapfile.amd64)
 * |  free pages we want to |
 * |  save for special uses |
 * +------------------------+ 0
 *
 * We'll bootstrap the allocator by setting up a single region from the top of
 * the kernel nucleus up to the bottom of our current stack less a healthy
 * margin for further growth.  We'll use this only for as long as it takes to
 * figure out where RAM really is, but it's necessary that anything we allocate
 * while doing so end up in the eventual RAM region; the allocator will keep
 * those allocations safe but if they really weren't usable bad things are sure
 * to happen.  In practice this means that all the physical address space
 * between the base of the kernel nucleus load address and roughly a MiB or so
 * beyond the end of it must:
 *
 * - truly reference writable RAM;
 * - not contain anything other than the kernel that we're ever going to need;
 * - not be claimed by something else (*cough* firmware *cough*)
 *
 * Once we've allocated enough memory to get through the APOB handling and
 * memlist generation, we'll replace this bootstrap range in both
 * bootops.boot_mem and the earlyboot allocator's memlist.  The two are not,
 * in general, the same: boot_mem describes how the kernel should handle the
 * physical address space after the entire VM subsystem is set up and the boot
 * pages freed, while the allocator's view of the system must be more
 * circumspect to ensure that it doesn't hand out a chunk of memory we're
 * using, or expecting to need, for something else during the boot process.  If
 * we want to keep something, we must map it above KERNELBASE prior to finishing
 * startup() or it will go away forever.
 *
 * To that end, the fakebop code offers interfaces for various early subsystems
 * to declare that a region of physical memory shouldn't be allocated from
 * during boot, or isn't usable RAM at all (i.e., it's MMIO, or reserved by
 * hardware, or claimed by firmware for its own nefarious purposes).  The
 * mechanism for declaring holes, both in boot-time allocable RAM and in the
 * physical address space more generally, is fairly flexible and allows for
 * implementation- and SOC-specific reservations.  So for example we could
 * decide we don't need the APOB on some SOC or at all, or accommodate multiple
 * APOBs, or handle an APOB above the 2 GiB boundary, etc.  Similarly, the
 * ramdisk could reside in this region, or anything else that is RAM but must
 * not be allocated from until we've had a chance to make use of it.
 *
 * Finally, then: the APOB.  As discussed in the BTS in fakebop.c:_start(), we
 * need to be able to contact the SP to find it, and only then can we figure
 * out reliably where RAM is (we can undoubtedly ask NBIO, but that wouldn't
 * tell us about any regions that PSP or SMU firmware thinks it's entitled to
 * use, which means anything we might put there is in danger of being clobbered
 * unpredictably -- RICHMOND-16 all over again!  The good news is that we do
 * control where the APOB itself goes; we supply the address we want for it in
 * the boot flash headers.  So we can define the interface between "things that
 * came before us" and this code simply to require that the APOB be located
 * somewhere sufficiently far above the kernel nucleus.  As part of probing the
 * physical space, boot code will also protect the APOB from being clobbered
 * during boot.  For now, we can safely ignore it.  Like the rest of RAM used
 * by the boot code, the APOB will eventually be freed unless we choose to
 * remap it above KERNELBASE.
 *
 * Until we've found how how much RAM we really have and where it is, we'll
 * tell anyone who asks that this bootstrap region is all we have; right now
 * that's no one but any consumer of bootops has access to it.  Once all this
 * is done, this code is really nothing but a newspaper held above your head in
 * the rain: it's no hat, but it'll do until you can find one.  The actual
 * allocation of RAM, based on firm knowledge of the machine state, is all
 * hidden behind bootops -- BOP_ALLOC() -- on the other side of the wall, along
 * with other uses of the physical address space needed to access devices.
 */
static paddr_t
find_free_pagetable(paddr_t first, paddr_t last, uint64_t used)
{
	uint8_t idx;

	if (~used == 0)
		goto fail;

	for (idx = 0;
	    idx < 64 && (first + (idx << MMU_PAGESHIFT)) <= last; idx++) {
		if ((used & (1UL << idx)) == 0)
			return (first + (idx << MMU_PAGESHIFT));
	}

fail:
	bop_panic("kbm_init: no pagetable pages available: "
	    "base %lx last %lx used %lx", first, last, used);
}

/*
 * XXX MMU_PAGEMASK and friends assume all the bits that aren't masked off are
 * part of the physical address, but on amd64 the top 12 bits are flag bits.
 * We don't want them, in case someone has set them.  We also have only 47 bits
 * of valid identity-mappable VA, which limits the PA bits in an initial boot
 * PTE to these.
 */
#define	IPTE_PAMASK	0x7ffffffff000UL

void
kbm_init(void)
{
	paddr_t loader_pt_base = getcr3() & IPTE_PAMASK;
	paddr_t loader_pt_last = loader_pt_base;
	uint64_t loader_pt_pages_used = 0;
	uint64_t idx;
	paddr_t l2_pa, l1_pa, l0_pa;
	x86pte_t *ptep;
	x86pte_t pteval;
	uint_t i, j, k;
	extern void hat_boot_kdi_init(void);

	top_page_table = loader_pt_base;
	KBM_DBG(top_page_table);

	window = (caddr_t)MMU_PAGESIZE;
	KBM_DBG(window);

	pte_to_window = NULL;
	if ((ptep = find_pte((uint64_t)window, NULL, 0, 1)) != NULL) {
		pte_to_window = ptep;
		KBM_DBG(pte_to_window);
		return;
	}

	KBM_DBG(loader_pt_base);

#define	PT_USED(_pa) do {	\
	idx = (_pa - loader_pt_base) >> MMU_PAGESHIFT;		\
	if (idx < 64)						\
		loader_pt_pages_used |= (1UL << idx);		\
} while (0)

	PT_USED(top_page_table);

	/*
	 * The loader doesn't set PT_USER in PTPs, but the htable code later on
	 * wants it set.  For convenience we set it here for all PTPs.  XXX
	 * consider either changing PT_PTPBITS or making the loader set this
	 * (creates a flag day).
	 */
	for (i = 0; i < ptes_per_table; i++) {
		pteval = get_pteval(top_page_table, i);
		if ((pteval & PT_VALID) == 0) {
			continue;
		}
		pteval |= PT_USER;
		set_pteval(top_page_table, i, 3, pteval);
		l2_pa = pteval & IPTE_PAMASK;
		PT_USED(l2_pa);
		if (l2_pa > loader_pt_last)
			loader_pt_last = l2_pa;

		for (j = 0; j < ptes_per_table; j++) {
			pteval = get_pteval(l2_pa, j);
			if ((pteval & PT_VALID) == 0 ||
			    (pteval & PT_PAGESIZE) != 0) {
				continue;
			}
			pteval |= PT_USER;
			set_pteval(l2_pa, j, 2, pteval);
			l1_pa = pteval & IPTE_PAMASK;
			PT_USED(l1_pa);
			if (l1_pa > loader_pt_last)
				loader_pt_last = l1_pa;

			for (k = 0; k < ptes_per_table; k++) {
				pteval = get_pteval(l1_pa, k);
				if ((pteval & PT_VALID) == 0 ||
				    (pteval & PT_PAGESIZE) != 0) {
					continue;
				}
				pteval |= PT_USER;
				set_pteval(l1_pa, k, 1, pteval);
				l0_pa = pteval & IPTE_PAMASK;
				PT_USED(l0_pa);
				if (l0_pa > loader_pt_last)
					loader_pt_last = l0_pa;
			}
		}
	}

	loader_pt_last = MAX(loader_pt_last,
	    loader_pt_base + (MIN_PAGETABLES - 1) * MMU_PAGESIZE);

	/*
	 * The highest pagetable supplied by the loader is at loader_pt_last,
	 * and loader_pt_pages_used is a bitmap of the first 64 of them.  We
	 * need at most 2 free pages to build our pagetables: levels 0 and 1.
	 * We are guaranteed that the level 2 pagetable already exists because
	 * it is impossible to identity-map anything without it and the
	 * pagetables are themselves mapped in that manner.
	 */
	if ((ptep = find_pte((uint64_t)window, NULL, 1, 1)) == NULL) {
		ptep = find_pte((uint64_t)window, NULL, 2, 1);
		ASSERT(ptep != NULL);

		l1_pa = find_free_pagetable(loader_pt_base, loader_pt_last,
		    loader_pt_pages_used);
		PT_USED(l1_pa);
		KBM_DBG(l1_pa);
		install_ptable(ptep, (void *)l1_pa, l1_pa);
	}

	ptep = find_pte((uint64_t)window, NULL, 1, 1);
	ASSERT(ptep != NULL);

	l0_pa = find_free_pagetable(loader_pt_base, loader_pt_last,
	    loader_pt_pages_used);
	PT_USED(l0_pa);
	KBM_DBG(l0_pa);
	install_ptable(ptep, (void *)l0_pa, l0_pa);

#undef	PT_USED

	pte_to_window = find_pte((uint64_t)window, NULL, 0, 1);
	KBM_DBG(pte_to_window);
	ASSERT(pte_to_window != NULL);

	/*
	 * Set up the earlyboot KDI interface for accessing physical memory.
	 * It does not in fact need nor use any part of the HAT.
	 */
	hat_boot_kdi_init();
}

/*
 * Change the addressable page table window to point at a given page.  If we
 * are still doing initialisation, all pagetable pages are known to be
 * identity-mapped so we do no mapping and return the PA as the VA.
 */
/*ARGSUSED*/
void *
kbm_remap_window(paddr_t physaddr, int writeable)
{
	x86pte_t pt_bits = PT_NOCONSIST | PT_VALID | PT_WRITABLE;

	KBM_DBG(physaddr);

	if (pte_to_window == NULL)
		return ((void *)(uintptr_t)physaddr);

	*pte_to_window = physaddr | pt_bits;
	mmu_invlpg(window);

	KBM_DBG(window);
	return (window);
}

/*
 * Add a mapping for the physical page at the given virtual address.
 */
void
kbm_map(uintptr_t va, paddr_t pa, uint_t level, x86pte_t flags)
{
	x86pte_t *ptep;
	paddr_t pte_physaddr;
	x86pte_t pteval;

	if (khat_running)
		panic("kbm_map() called too late");

	KBM_DBGMSG("kbm_map(%lx, %lx, %x, %lx)\n", va, pa, level, flags);

	pteval = pa | PT_NOCONSIST | PT_VALID | flags;
	if (level >= 1)
		pteval |= PT_PAGESIZE;

	/*
	 * Find the pte that will map this address. This creates any
	 * missing intermediate level page tables.
	 */
	ptep = find_pte(va, &pte_physaddr, level, 0);
	if (ptep == NULL)
		bop_panic("kbm_map: find_pte returned NULL");

	*ptep = pteval;
	mmu_invlpg((caddr_t)va);
}

/*
 * Probe the boot time page tables to find the first mapping
 * including va (or higher) and return non-zero if one is found.
 * va is updated to the starting address and len to the pagesize.
 * pp will be set to point to the 1st page_t of the mapped page(s).
 *
 * Note that if va is in the middle of a large page, the returned va
 * will be less than what was asked for.
 */
int
kbm_probe(uintptr_t *va, size_t *len, pfn_t *pfn, uint_t *prot)
{
	uintptr_t	probe_va;
	x86pte_t	*ptep;
	paddr_t		pte_physaddr;
	x86pte_t	pte_val;
	level_t		l;

	if (khat_running)
		panic("kbm_probe() called too late");
	*len = 0;
	*pfn = PFN_INVALID;
	*prot = 0;
	probe_va = *va;
restart_new_va:
	l = TOP_LEVEL;
	for (;;) {
		if (IN_VA_HOLE(probe_va))
			probe_va = mmu.hole_end;

		if (IN_HYPERVISOR_VA(probe_va))
			return (0);

		/*
		 * If we don't have a valid PTP/PTE at this level
		 * then we can bump VA by this level's pagesize and try again.
		 * When the probe_va wraps around, we are done.
		 */
		ptep = find_pte(probe_va, &pte_physaddr, l, 1);
		if (ptep == NULL)
			bop_panic("kbm_probe: find_pte returned NULL");

		pte_val = *ptep;
		if (!PTE_ISVALID(pte_val)) {
			probe_va = (probe_va & BOOT_MASK(l)) + BOOT_SZ(l);
			if (probe_va <= *va)
				return (0);
			goto restart_new_va;
		}

		/*
		 * If this entry is a pointer to a lower level page table
		 * go down to it.
		 */
		if (!PTE_ISPAGE(pte_val, l)) {
			ASSERT(l > 0);
			--l;
			continue;
		}

		/*
		 * We found a boot level page table entry
		 */
		*len = BOOT_SZ(l);
		*va = probe_va & ~(*len - 1);
		*pfn = PTE2PFN(pte_val, l);


		*prot = PROT_READ | PROT_EXEC;
		if (PTE_GET(pte_val, PT_WRITABLE))
			*prot |= PROT_WRITE;

		if (PTE_GET(pte_val, mmu.pt_nx))
			*prot &= ~PROT_EXEC;

		return (1);
	}
}


/*
 * Destroy a boot loader page table 4K mapping.
 */
void
kbm_unmap(uintptr_t va)
{
	KBM_DBGMSG("kbm_unmap(%lx)\n", va);

	if (khat_running) {
		panic("kbm_unmap() called too late");
	} else {
		x86pte_t *ptep;
		level_t	level = 0;
		uint_t  probe_only = 1;

		ptep = find_pte(va, NULL, level, probe_only);
		if (ptep == NULL)
			return;

		*ptep = 0;
		mmu_invlpg((caddr_t)va);
	}
}


/*
 * Change a boot loader page table 4K mapping.
 * Returns the pfn of the old mapping.
 */
pfn_t
kbm_remap(uintptr_t va, pfn_t pfn)
{
	x86pte_t *ptep;
	level_t	level = 0;
	uint_t  probe_only = 1;
	x86pte_t pte_val = pfn_to_pa(pfn) | PT_WRITABLE |
	    PT_NOCONSIST | PT_VALID;
	x86pte_t old_pte;

	if (khat_running)
		panic("kbm_remap() called too late");
	ptep = find_pte(va, NULL, level, probe_only);
	if (ptep == NULL)
		bop_panic("kbm_remap: find_pte returned NULL");

	old_pte = *ptep;
	*((x86pte_t *)ptep) = pte_val;
	mmu_invlpg((caddr_t)va);

	if (!(old_pte & PT_VALID) || old_pte == -1)
		return (PFN_INVALID);
	return (mmu_btop(old_pte));
}


/*
 * Change a boot loader page table 4K mapping to read only.
 */
void
kbm_read_only(uintptr_t va, paddr_t pa)
{
	x86pte_t pte_val = pa | PT_NOCONSIST | PT_REF | PT_MOD | PT_VALID;

	x86pte_t *ptep;
	level_t	level = 0;

	ptep = find_pte(va, NULL, level, 0);
	if (ptep == NULL)
		bop_panic("kbm_read_only: find_pte returned NULL");

	*ptep = pte_val;
	mmu_invlpg((caddr_t)va);
}

/*
 * Allocate virtual address space from the imaginary earlyboot arena.  These
 * mappings will be torn down automatically in startup.c when we call
 * clear_boot_mappings().  The address returned is not mapped; the caller is
 * responsible for setting up a mapping via kbm_map() etc.
 */
uintptr_t
kbm_valloc(size_t size, paddr_t align)
{
	/*
	 * Next available virtual address to allocate.  Do not allocate page 0.
	 */
	static uintptr_t next_virt = (uintptr_t)(MMU_PAGESIZE * 2);
	uintptr_t rv;

	KBM_DBGMSG("kbm_valloc: sz %lx align %lx", size, align);

	next_virt = P2ROUNDUP(next_virt, (uintptr_t)align);
	rv = next_virt;
	next_virt += size;

	KBM_DBGMSG(" = %lx\n", rv);

	return (rv);
}

/*
 * interfaces for kernel debugger to access physical memory
 */
static x86pte_t save_pte;

void *
kbm_push(paddr_t pa)
{
	static int first_time = 1;

	if (first_time) {
		first_time = 0;
		return (window);
	}

	save_pte = *pte_to_window;

	return (kbm_remap_window(pa, 0));
}

void
kbm_pop(void)
{
	*pte_to_window = save_pte;
	mmu_invlpg(window);
}

x86pte_t
get_pteval(paddr_t table, uint_t index)
{
	void *table_ptr = kbm_remap_window(table, 0);

	return (((x86pte_t *)table_ptr)[index]);
}

void
set_pteval(paddr_t table, uint_t index, uint_t level, x86pte_t pteval)
{
	void *table_ptr = kbm_remap_window(table, 0);

	((x86pte_t *)table_ptr)[index] = pteval;
}

static void
install_ptable(x86pte_t *pteval, void *table_ptr, paddr_t new_table)
{
	bzero(table_ptr, MMU_PAGESIZE);
	*pteval = new_table | PT_VALID | PT_REF | PT_USER | PT_WRITABLE;
}

paddr_t
make_ptable(x86pte_t *pteval, uint_t level)
{
	paddr_t new_table;
	void *table_ptr;

	new_table = eb_phys_alloc(MMU_PAGESIZE, MMU_PAGESIZE);
	table_ptr = kbm_remap_window(new_table, 1);
	install_ptable(pteval, table_ptr, new_table);

	return (new_table);
}

x86pte_t *
map_pte(paddr_t table, uint_t index)
{
	void *table_ptr = kbm_remap_window(table, 0);
	return ((x86pte_t *)((caddr_t)table_ptr + index * PTE_SIZE));
}

/*
 * Return the index corresponding to a virt address at a given page table level.
 */
static uint_t
vatoindex(uint64_t va, uint_t level)
{
	return ((va >> BOOT_SHIFT(level)) & (ptes_per_table - 1));
}

/*
 * Return a pointer to the page table entry that maps a virtual address.
 * If there is no page table and probe_only is not set, one is created.
 */
x86pte_t *
find_pte(uint64_t va, paddr_t *pa, uint_t level, uint_t probe_only)
{
	uint_t l;
	uint_t index;
	paddr_t table;
	x86pte_t *pp;

	if (pa)
		*pa = 0;

	/*
	 * Walk down the page tables creating any needed intermediate tables.
	 */
	table = top_page_table;
	for (l = TOP_LEVEL; l != level; --l) {
		uint64_t pteval;
		paddr_t new_table;

		index = vatoindex(va, l);
		pteval = get_pteval(table, index);

		/*
		 * Life is easy if we find the pagetable.  We just use it.
		 * However we must be sure this is not a large page; this
		 * simplified MMU code doesn't know how to demote such a
		 * mapping, and we must panic if we are asked for a PTE from a
		 * level L pagetable but there is a large mapping covering the
		 * requested VA at any level above L.
		 */
		if (pteval & PT_VALID) {
			if ((pteval & PT_PAGESIZE) != 0) {
				if (probe_only)
					return (NULL);

				bop_panic("find_pte() encountered large page "
				    "PTE %lx at level %d while looking for %lx "
				    "at level %d",
				    pteval, l, va, level);
			}

			table = pteval & MMU_PAGEMASK;
			continue;
		}

		if (probe_only)
			return (NULL);

		new_table = make_ptable(&pteval, l);
		set_pteval(table, index, l, pteval);

		table = new_table;
	}

	/*
	 * Return a pointer into the current pagetable.
	 */
	index = vatoindex(va, l);
	if (pa)
		*pa = table + index * PTE_SIZE;
	pp = map_pte(table, index);

	return (pp);
}

uint64_t
kbm_map_ramdisk(uint64_t start, uint64_t end)
{
	uint64_t vstart, vend, size, off, p, pagesize;
	uint_t level = 0;

	if ((start & BOOT_OFFSET(1)) == 0)
		level = 1;

	pagesize = BOOT_SZ(level);

	size = P2ROUNDUP(end - P2ALIGN(start, pagesize), pagesize);
	vstart = kbm_valloc(size, pagesize);
	vend = vstart + size;

	off = start - P2ALIGN(start, pagesize);
	start -= off;

	KBM_DBGMSG("ramdisk: start %lx end %lx base %lx vend %lx off %lx\n",
	    start, end, vstart, vend, off);

	for (p = 0; vstart + p < vend; p += pagesize) {
		KBM_DBGMSG("mapping ramdisk: %lx -> %lx\n",
		    vstart + p, start + p);
		kbm_map(vstart + p, start + p, level, 0);
	}


	return (vstart + off);
}

#ifdef DEBUG
/*
 * Dump out the contents of page tables, assuming that they are all identity
 * mapped; this will panic otherwise so use with extreme caution.  It's really
 * useful only when we are first probing what the loader has done.
 */
void
dump_tables(void)
{
	uint_t save_index[4];	/* for recursion */
	char *save_table[4];	/* for recursion */
	uint_t	l;
	uint64_t va;
	uint64_t pgsize;
	int index;
	int i;
	x86pte_t pteval;
	char *table;
	static char *tablist = "\t\t\t";
	char *tabs = tablist + 3 - TOP_LEVEL;
	uint64_t pa, pa1;

	eb_printf("Pagetables:\n");
	table = (char *)(uintptr_t)top_page_table;
	l = TOP_LEVEL;
	va = 0;
	for (index = 0; index < ptes_per_table; ++index) {
		pgsize = 1UL << BOOT_SHIFT(l);
		pteval = ((x86pte_t *)table)[index];
		if (pteval == 0)
			goto next_entry;

		eb_printf("%s %p[0x%x] = %" PRIx64 ", va=%" PRIx64,
		    tabs + l, (void *)table, index, (uint64_t)pteval, va);
		pa = pteval & MMU_PAGEMASK;
		eb_printf(" physaddr=%lx\n", pa);

		if ((l > 2 || (l > 0 && (pteval & PT_PAGESIZE) == 0))) {
			save_table[l] = table;
			save_index[l] = index;
			--l;
			index = -1;
			table = (char *)(uintptr_t)(pteval & MMU_PAGEMASK);
			goto recursion;
		}

		/*
		 * shorten dump for consecutive mappings
		 */
		for (i = 1; index + i < ptes_per_table; ++i) {
			pteval = ((x86pte_t *)table)[index + i];
			if (pteval == 0)
				break;
			pa1 = pteval & MMU_PAGEMASK;
			if (pa1 != pa + i * pgsize)
				break;
		}
		if (i > 2) {
			eb_printf("%s...\n", tabs + l);
			va += pgsize * (i - 2);
			index += i - 2;
		}
next_entry:
		va += pgsize;
		if (l == 3 && index == 255)	/* VA hole */
			va = 0xffff800000000000UL;
recursion:
		;
	}
	if (l < TOP_LEVEL) {
		++l;
		index = save_index[l];
		table = save_table[l];
		goto recursion;
	}
}
#endif
