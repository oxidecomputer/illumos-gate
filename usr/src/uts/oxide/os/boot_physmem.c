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
 * Copyright 2023 Oxide Computer Co.
 */

#include <sys/boot_debug.h>
#include <sys/boot_physmem.h>
#include <sys/mach_mmu.h>
#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/promif.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <vm/kboot_mmu.h>

static struct memlist_pool ebml_pool;
static struct bsys_mem *bsys_memp;
static struct bsys_mem eballoc_mem;

/*
 * One more than the highest physical address that may contain usable RAM.
 * This is not guaranteed; it may be excluded by a hole.
 */
static paddr_t max_phys = LOADER_PHYSLIMIT;

/*
 * some allocator statistics
 */
static ulong_t total_eb_alloc_scratch = 0;
static ulong_t total_eb_alloc_kernel = 0;

paddr_t
eb_phys_alloc(size_t size, size_t align)
{
	static paddr_t next_phys = 0;
	paddr_t	pa = -(paddr_t)1;
	paddr_t	start;
	paddr_t	end;
	struct memlist *ml = eballoc_mem.physinstalled;

	size = P2ROUNDUP(size, align);
	for (; ml; ml = ml->ml_next) {
		start = P2ROUNDUP(ml->ml_address, align);
		end = P2ALIGN(ml->ml_address + ml->ml_size, align);
		if (start < next_phys)
			start = P2ROUNDUP(next_phys, align);

		if (end <= start)
			continue;
		if (end - start < size)
			continue;

		if (start < pa)
			pa = start;
	}
	if (pa != -(paddr_t)1) {
		next_phys = pa + size;
		return (pa);
	}
	bop_panic("eb_phys_alloc(0x%" PRIx64 ", 0x%" PRIx64
	    ") Out of memory\n", size, align);
	/*NOTREACHED*/
}

/*
 * Allocate and map memory. The size is always rounded up to a multiple
 * of base pagesize.
 */
caddr_t
eb_alloc(caddr_t virthint, size_t size, size_t align)
{
	paddr_t a = align;	/* same type as pa for masking */
	uint_t pgsize;
	paddr_t pa;
	uintptr_t va;
	ssize_t s;		/* the aligned size */
	uint_t level;
	x86pte_t pte_flags = PT_WRITABLE;
	boolean_t is_kernel = (virthint != 0);

	if (is_kernel) {
		pte_flags |= PT_GLOBAL;
	}

	if (a < MMU_PAGESIZE)
		a = MMU_PAGESIZE;
	else if (!ISP2(a))
		prom_panic("eb_alloc() incorrect alignment");
	size = P2ROUNDUP(size, MMU_PAGESIZE);

	/*
	 * Use the next aligned virtual address if we weren't given one.
	 */
	if (virthint == NULL) {
		virthint = (caddr_t)kbm_valloc(size, a);
		total_eb_alloc_scratch += size;
	} else {
		total_eb_alloc_kernel += size;
	}

	/*
	 * allocate the physical memory
	 */
	pa = eb_phys_alloc(size, a);

	KBM_DBGMSG("bsys_alloc: alloc sz %lx pa %lx for va %p...",
	    size, pa, virthint);

	/*
	 * Add the mappings to the page tables, try large pages first.
	 */
	va = (uintptr_t)virthint;
	s = size;
	level = 1;
	pgsize = TWO_MEG;
	if ((a & (pgsize - 1)) == 0) {
		while (IS_P2ALIGNED(pa, pgsize) && IS_P2ALIGNED(va, pgsize) &&
		    s >= pgsize) {
			kbm_map(va, pa, level, pte_flags);
			va += pgsize;
			pa += pgsize;
			s -= pgsize;
		}
	}

	/*
	 * Map remaining pages use small mappings
	 */
	level = 0;
	pgsize = MMU_PAGESIZE;
	while (s > 0) {
		kbm_map(va, pa, level, pte_flags);
		va += pgsize;
		pa += pgsize;
		s -= pgsize;
	}

	bzero(virthint, size);

	KBM_DBGMSG("done (%lx @ %p)\n", size, virthint);

	return (virthint);
}

static void
add_range(memlist_t **target, uint64_t base, uint64_t size)
{
	int err;
	void *page;

	ASSERT(target != NULL);

	err = xmemlist_add_span(&ebml_pool, base, size, target,
	    MEML_FL_RELAXED);
	if (err == MEML_SPANOP_EALLOC) {
		page = eb_alloc_page();
		xmemlist_free_block(&ebml_pool, page, MMU_PAGESIZE);
		err = xmemlist_add_span(&ebml_pool, base, size, target,
		    MEML_FL_RELAXED);
	}

	if (err != MEML_SPANOP_OK) {
		bop_panic("xmemlist_add_span() failed with "
		    "unexpected error %d\n", err);
	}
}

static void
remove_range(memlist_t **target, uint64_t base, uint64_t size)
{
	int err;
	void *page;

	ASSERT(target != NULL);

	err = xmemlist_delete_span(&ebml_pool, base, size, target,
	    MEML_FL_RELAXED);
	if (err == MEML_SPANOP_EALLOC) {
		page = eb_alloc_page();
		xmemlist_free_block(&ebml_pool, page, MMU_PAGESIZE);
		err = xmemlist_delete_span(&ebml_pool, base, size, target,
		    MEML_FL_RELAXED);
	}

	if (err != MEML_SPANOP_OK) {
		bop_panic("xmemlist_delete_span() failed with "
		    "unexpected error %d\n", err);
	}
}

void
eb_physmem_reserve_range(uint64_t addr, uint64_t size,
    eb_physmem_reservation_t ebpr)
{
	uint64_t end = addr + size;

	addr = P2ALIGN(addr, MMU_PAGESIZE);
	size = P2ROUNDUP(end, MMU_PAGESIZE) - addr;

	switch (ebpr) {
	case EBPR_NOT_RAM:
		add_range(&bsys_memp->rsvdmem, addr, size);
		remove_range(&bsys_memp->physinstalled, addr, size);
		/*FALLTHROUGH*/
	case EBPR_NO_ALLOC:
		add_range(&eballoc_mem.rsvdmem, addr, size);
		remove_range(&eballoc_mem.physinstalled, addr, size);
		break;
	default:
		bop_panic("bogus physmem reservation type %d\n", (int)ebpr);
	}
}

void
eb_physmem_reserve(const memlist_t *mlp, eb_physmem_reservation_t ebpr)
{
	for (; mlp != NULL; mlp = mlp->ml_next) {
		eb_physmem_reserve_range(mlp->ml_address, mlp->ml_size, ebpr);
	}
}

/*
 * Extend *rampp to include the range up to addr that does not overlap with any
 * of the reserved regions in rsvdp.  Note that this is distinct from
 * unreserving the region, which states categorically that the region contains
 * usable RAM.
 */
static void
maybe_extend_ram(memlist_t **rampp, const memlist_t *rsvdp, paddr_t addr)
{
	const memlist_t *mlp;
	paddr_t last;

	last = max_phys;

	for (mlp = rsvdp; mlp != NULL; mlp = mlp->ml_next) {
		/*
		 * These lists are sorted, so if we have found a
		 * reserved region starting beyond the new higher end
		 * address we are done.
		 */
		if (mlp->ml_address >= addr)
			break;

		/*
		 * There shouldn't be any zero-size regions in any of
		 * these lists, but if there is a zero-size reserved
		 * region, ignore it.
		 */
		if (mlp->ml_size == 0)
			continue;

		if (mlp->ml_address > last)
			add_range(rampp, last, mlp->ml_address - last);

		last = mlp->ml_address + mlp->ml_size;
	}

	if (addr > last)
		add_range(rampp, last, addr - last);
}

void
eb_physmem_set_max(paddr_t addr)
{
	ASSERT(addr >= LOADER_PHYSLIMIT);

	/*
	 * Shrinking is simply the same as reserving everything above what we
	 * previously thought was RAM.  Growing requires that we add regions
	 * above the old max that have not already been reserved.
	 */
	if (addr < max_phys) {
		eb_physmem_reserve_range(addr, max_phys - addr, EBPR_NOT_RAM);
	} else if (addr > max_phys) {
		maybe_extend_ram(&bsys_memp->physinstalled, bsys_memp->rsvdmem,
		    addr);
		maybe_extend_ram(&eballoc_mem.physinstalled,
		    eballoc_mem.rsvdmem, addr);
	}

	max_phys = addr;
}

void
eb_physmem_init(struct bsys_mem *bmp)
{
	uint64_t rsp;
	void *mlpage;
	memlist_t *mlp;
	memlist_t bsml_usable;

	bsml_usable.ml_address = 0x600000UL;	/* sync with Mapfile.amd64 */
	__asm__ __volatile__("movq %%rsp, %0" : "=r" (rsp) : :);
	rsp = P2ALIGN(rsp - 8 * MMU_PAGESIZE, MMU_PAGESIZE);
	bsml_usable.ml_size = rsp - bsml_usable.ml_address;
	bsml_usable.ml_next = NULL;
	bsml_usable.ml_prev = NULL;

	eballoc_mem.physinstalled = &bsml_usable;

	/*
	 * The allocator is now usable, and we've already set up the MMU, so
	 * allocate ourselves a page for our real memlists and fill in a
	 * skeleton for each.
	 */
	ebml_pool.mp_flags = MEMLP_FL_EARLYBOOT;
	mlpage = eb_alloc_page();
	KBM_DBG(mlpage);
	xmemlist_free_block(&ebml_pool, mlpage, MMU_PAGESIZE);

	mlp = xmemlist_get_one(&ebml_pool);
	KBM_DBG(mlp);
	ASSERT(mlp != NULL);
	mlp->ml_address = bsml_usable.ml_address;
	mlp->ml_size = LOADER_PHYSLIMIT - mlp->ml_address;
	mlp->ml_next = mlp->ml_prev = NULL;
	eballoc_mem.physinstalled = mlp;
	eballoc_mem.rsvdmem = eballoc_mem.pcimem = NULL;

	mlp = xmemlist_get_one(&ebml_pool);
	KBM_DBG(mlp);
	ASSERT(mlp != NULL);
	mlp->ml_address = 0;
	mlp->ml_size = LOADER_PHYSLIMIT;
	mlp->ml_next = mlp->ml_prev = NULL;

	bmp->physinstalled = mlp;
	bmp->rsvdmem = bmp->pcimem = NULL;
	bsys_memp = bmp;

	/*
	 * Let's review:
	 *
	 * - ebml_pool has been populated
	 * - bsys_memp has been populated with our initial understanding of what
	 *   addresses contain RAM
	 * - eballoc_mem has been populated with our initial understanding of
	 *   what addresses are safe to allocate during boot
	 *
	 * From here on out, the eb_physmem_reserve_XX and eb_physmem_set_max
	 * functions will work, maintaining in bsys_memp (which our caller will
	 * pass into startup code) and eballoc_mem (which is private to our
	 * allocator) sorted non-empty lists of usable and reserved physical
	 * address space.  In startup(), the former will be used to populate
	 * the real physical memory map and create page_ts for RAM.
	 *
	 * Both the available and reserved lists, for both these applications,
	 * should contain only memlist_ts from ebml_pool.  During startup(),
	 * after the RAM list is copied and the earlyboot allocator disabled,
	 * this pool's page(s) and mappings will be deleted.
	 *
	 * All that's left is to reserve the pagetables and stack from the
	 * earlyboot allocator; we cheated a bit by telling it that RAM starts
	 * above the kernel so we needn't reserve that.  We must also reserve
	 * the page containing the reset vector for MP startup; however, we
	 * need to consult boot properties to locate it and setting them up is
	 * the next thing our caller is going to do, so we leave it out here.
	 */
	eb_physmem_reserve_range(rsp, LOADER_PHYSLIMIT - rsp, EBPR_NO_ALLOC);
}

void
eb_physmem_fini(void)
{
	eballoc_mem.physinstalled = eballoc_mem.rsvdmem = NULL;
	KBM_DBG(total_eb_alloc_scratch);
	KBM_DBG(total_eb_alloc_kernel);
}
