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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014 by Delphix. All rights reserved.
 * Copyright 2018 Joyent, Inc.
 * Copyright 2023 Oxide Computer Co.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/bitmap.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/mman.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/disp.h>
#include <sys/vmem.h>
#include <sys/vmsystm.h>
#include <sys/promif.h>
#include <sys/var.h>
#include <sys/x86_archext.h>
#include <sys/archsystm.h>
#include <sys/bootconf.h>
#include <sys/dumphdr.h>
#include <vm/seg_kmem.h>
#include <vm/seg_kpm.h>
#include <vm/hat.h>
#include <vm/hat_i86.h>
#include <sys/cmn_err.h>
#include <sys/panic.h>
#include <sys/bootinfo.h>
#include <vm/kboot_mmu.h>

static void x86pte_zero(htable_t *dest, uint_t entry, uint_t count);

kmem_cache_t *htable_cache;

/*
 * The variable htable_reserve_amount, rather than HTABLE_RESERVE_AMOUNT,
 * is used in order to facilitate testing of the htable_steal() code.
 * By resetting htable_reserve_amount to a lower value, we can force
 * stealing to occur.  The reserve amount is a guess to get us through boot.
 */
#define	HTABLE_RESERVE_AMOUNT	(200)
uint_t htable_reserve_amount = HTABLE_RESERVE_AMOUNT;
kmutex_t htable_reserve_mutex;
uint_t htable_reserve_cnt;
htable_t *htable_reserve_pool;

/*
 * Used to hand test htable_steal().
 */
#ifdef DEBUG
ulong_t force_steal = 0;
ulong_t ptable_cnt = 0;
#endif

/*
 * This variable is so that we can tune this via /etc/system
 * Any value works, but a power of two <= mmu.ptes_per_table is best.
 */
uint_t htable_steal_passes = 8;

/*
 * mutex stuff for access to htable hash
 */
#define	NUM_HTABLE_MUTEX 128
kmutex_t htable_mutex[NUM_HTABLE_MUTEX];
#define	HTABLE_MUTEX_HASH(h) ((h) & (NUM_HTABLE_MUTEX - 1))

#define	HTABLE_ENTER(h)	mutex_enter(&htable_mutex[HTABLE_MUTEX_HASH(h)]);
#define	HTABLE_EXIT(h)	mutex_exit(&htable_mutex[HTABLE_MUTEX_HASH(h)]);

/*
 * forward declarations
 */
static void link_ptp(htable_t *higher, htable_t *new, uintptr_t vaddr);
static void unlink_ptp(htable_t *higher, htable_t *old, uintptr_t vaddr);
static void htable_free(htable_t *ht);
static x86pte_t *x86pte_access_pagetable(htable_t *ht, uint_t index);
static void x86pte_release_pagetable(htable_t *ht);
static x86pte_t x86pte_cas(htable_t *ht, uint_t entry, x86pte_t old,
	x86pte_t new);

/*
 * A counter to track if we are stealing or reaping htables. When non-zero
 * htable_free() will directly free htables (either to the reserve or kmem)
 * instead of putting them in a hat's htable cache.
 */
uint32_t htable_dont_cache = 0;

/*
 * Track the number of active pagetables, so we can know how many to reap
 */
static uint32_t active_ptables = 0;

/*
 * Allocate a memory page for a hardware page table.
 *
 * A wrapper around page_get_physical(), with some extra checks.
 */
static pfn_t
ptable_alloc(uintptr_t seed)
{
	pfn_t pfn;
	page_t *pp;

	pfn = PFN_INVALID;

	/*
	 * The first check is to see if there is memory in the system. If we
	 * drop to throttlefree, then fail the ptable_alloc() and let the
	 * stealing code kick in. Note that we have to do this test here,
	 * since the test in page_create_throttle() would let the NOSLEEP
	 * allocation go through and deplete the page reserves.
	 *
	 * The !NOMEMWAIT() lets pageout, fsflush, etc. skip this check.
	 */
	if (!NOMEMWAIT() && freemem <= throttlefree + 1)
		return (PFN_INVALID);

#ifdef DEBUG
	/*
	 * This code makes htable_steal() easier to test. By setting
	 * force_steal we force pagetable allocations to fall
	 * into the stealing code. Roughly 1 in ever "force_steal"
	 * page table allocations will fail.
	 */
	if (proc_pageout != NULL && force_steal > 1 &&
	    ++ptable_cnt > force_steal) {
		ptable_cnt = 0;
		return (PFN_INVALID);
	}
#endif /* DEBUG */

	pp = page_get_physical(seed);
	if (pp == NULL)
		return (PFN_INVALID);
	ASSERT(PAGE_SHARED(pp));
	pfn = pp->p_pagenum;
	if (pfn == PFN_INVALID)
		panic("ptable_alloc(): Invalid PFN!!");
	atomic_inc_32(&active_ptables);
	HATSTAT_INC(hs_ptable_allocs);
	return (pfn);
}

/*
 * Free an htable's associated page table page.  See the comments
 * for ptable_alloc().
 */
static void
ptable_free(pfn_t pfn)
{
	page_t *pp = page_numtopp_nolock(pfn);

	/*
	 * need to destroy the page used for the pagetable
	 */
	ASSERT(pfn != PFN_INVALID);
	HATSTAT_INC(hs_ptable_frees);
	atomic_dec_32(&active_ptables);
	if (pp == NULL)
		panic("ptable_free(): no page for pfn!");
	ASSERT(PAGE_SHARED(pp));
	ASSERT(pfn == pp->p_pagenum);
	ASSERT(!IN_XPV_PANIC());

	/*
	 * Get an exclusive lock, might have to wait for a kmem reader.
	 */
	if (!page_tryupgrade(pp)) {
		u_offset_t off = pp->p_offset;
		page_unlock(pp);
		pp = page_lookup(&kvp, off, SE_EXCL);
		if (pp == NULL)
			panic("page not found");
	}
	page_hashout(pp, NULL);
	page_free(pp, 1);
	page_unresv(1);
}

/*
 * Put one htable on the reserve list.
 */
static void
htable_put_reserve(htable_t *ht)
{
	ht->ht_hat = NULL;		/* no longer tied to a hat */
	ASSERT(ht->ht_pfn == PFN_INVALID);
	HATSTAT_INC(hs_htable_rputs);
	mutex_enter(&htable_reserve_mutex);
	ht->ht_next = htable_reserve_pool;
	htable_reserve_pool = ht;
	++htable_reserve_cnt;
	mutex_exit(&htable_reserve_mutex);
}

/*
 * Take one htable from the reserve.
 */
static htable_t *
htable_get_reserve(void)
{
	htable_t *ht = NULL;

	mutex_enter(&htable_reserve_mutex);
	if (htable_reserve_cnt != 0) {
		ht = htable_reserve_pool;
		ASSERT(ht != NULL);
		ASSERT(ht->ht_pfn == PFN_INVALID);
		htable_reserve_pool = ht->ht_next;
		--htable_reserve_cnt;
		HATSTAT_INC(hs_htable_rgets);
	}
	mutex_exit(&htable_reserve_mutex);
	return (ht);
}

/*
 * Allocate initial htables and put them on the reserve list
 */
void
htable_initial_reserve(uint_t count)
{
	htable_t *ht;

	count += HTABLE_RESERVE_AMOUNT;
	while (count > 0) {
		ht = kmem_cache_alloc(htable_cache, KM_NOSLEEP);
		ASSERT(ht != NULL);

		ASSERT(use_boot_reserve);
		ht->ht_pfn = PFN_INVALID;
		htable_put_reserve(ht);
		--count;
	}
}

/*
 * Readjust the reserves after a thread finishes using them.
 */
void
htable_adjust_reserve()
{
	htable_t *ht;

	/*
	 * Free any excess htables in the reserve list
	 */
	while (htable_reserve_cnt > htable_reserve_amount &&
	    !USE_HAT_RESERVES()) {
		ht = htable_get_reserve();
		if (ht == NULL)
			return;
		ASSERT(ht->ht_pfn == PFN_INVALID);
		kmem_cache_free(htable_cache, ht);
	}
}

/*
 * Search the active htables for one to steal. Start at a different hash
 * bucket every time to help spread the pain of stealing
 */
static void
htable_steal_active(hat_t *hat, uint_t cnt, uint_t threshold,
    uint_t *stolen, htable_t **list)
{
	static uint_t	h_seed = 0;
	htable_t	*higher, *ht;
	uint_t		h, e, h_start;
	uintptr_t	va;
	x86pte_t	pte;

	h = h_start = h_seed++ % hat->hat_num_hash;
	do {
		higher = NULL;
		HTABLE_ENTER(h);
		for (ht = hat->hat_ht_hash[h]; ht; ht = ht->ht_next) {

			/*
			 * Can we rule out reaping?
			 */
			if (ht->ht_busy != 0 ||
			    (ht->ht_flags & HTABLE_SHARED_PFN) ||
			    ht->ht_level > 0 || ht->ht_valid_cnt > threshold ||
			    ht->ht_lock_cnt != 0)
				continue;

			/*
			 * Increment busy so the htable can't disappear. We
			 * drop the htable mutex to avoid deadlocks with
			 * hat_pageunload() and the hment mutex while we
			 * call hat_pte_unmap()
			 */
			++ht->ht_busy;
			HTABLE_EXIT(h);

			/*
			 * Try stealing.
			 * - unload and invalidate all PTEs
			 */
			for (e = 0, va = ht->ht_vaddr;
			    e < HTABLE_NUM_PTES(ht) && ht->ht_valid_cnt > 0 &&
			    ht->ht_busy == 1 && ht->ht_lock_cnt == 0;
			    ++e, va += MMU_PAGESIZE) {
				pte = x86pte_get(ht, e);
				if (!PTE_ISVALID(pte))
					continue;
				hat_pte_unmap(ht, e, HAT_UNLOAD, pte, NULL,
				    B_TRUE);
			}

			/*
			 * Reacquire htable lock. If we didn't remove all
			 * mappings in the table, or another thread added a new
			 * mapping behind us, give up on this table.
			 */
			HTABLE_ENTER(h);
			if (ht->ht_busy != 1 || ht->ht_valid_cnt != 0 ||
			    ht->ht_lock_cnt != 0) {
				--ht->ht_busy;
				continue;
			}

			/*
			 * Steal it and unlink the page table.
			 */
			higher = ht->ht_parent;
			unlink_ptp(higher, ht, ht->ht_vaddr);

			/*
			 * remove from the hash list
			 */
			if (ht->ht_next)
				ht->ht_next->ht_prev = ht->ht_prev;

			if (ht->ht_prev) {
				ht->ht_prev->ht_next = ht->ht_next;
			} else {
				ASSERT(hat->hat_ht_hash[h] == ht);
				hat->hat_ht_hash[h] = ht->ht_next;
			}

			/*
			 * Break to outer loop to release the
			 * higher (ht_parent) pagetable. This
			 * spreads out the pain caused by
			 * pagefaults.
			 */
			ht->ht_next = *list;
			*list = ht;
			++*stolen;
			break;
		}
		HTABLE_EXIT(h);
		if (higher != NULL)
			htable_release(higher);
		if (++h == hat->hat_num_hash)
			h = 0;
	} while (*stolen < cnt && h != h_start);
}

/*
 * Move hat to the end of the kas list
 */
static void
move_victim(hat_t *hat)
{
	ASSERT(MUTEX_HELD(&hat_list_lock));

	/* unlink victim hat */
	if (hat->hat_prev)
		hat->hat_prev->hat_next = hat->hat_next;
	else
		kas.a_hat->hat_next = hat->hat_next;

	if (hat->hat_next)
		hat->hat_next->hat_prev = hat->hat_prev;
	else
		kas.a_hat->hat_prev = hat->hat_prev;
	/* relink at end of hat list */
	hat->hat_next = NULL;
	hat->hat_prev = kas.a_hat->hat_prev;
	if (hat->hat_prev)
		hat->hat_prev->hat_next = hat;
	else
		kas.a_hat->hat_next = hat;

	kas.a_hat->hat_prev = hat;
}

/*
 * This routine steals htables from user processes.  Called by htable_reap
 * (reap=TRUE) or htable_alloc (reap=FALSE).
 */
static htable_t *
htable_steal(uint_t cnt, boolean_t reap)
{
	hat_t		*hat = kas.a_hat;	/* list starts with khat */
	htable_t	*list = NULL;
	htable_t	*ht;
	uint_t		stolen = 0;
	uint_t		pass, passes;
	uint_t		threshold;

	/*
	 * Limit htable_steal_passes to something reasonable
	 */
	if (htable_steal_passes == 0)
		htable_steal_passes = 1;
	if (htable_steal_passes > mmu.ptes_per_table)
		htable_steal_passes = mmu.ptes_per_table;

	/*
	 * If we're stealing merely as part of kmem reaping (versus stealing
	 * to assure forward progress), we don't want to actually steal any
	 * active htables.  (Stealing active htables merely to give memory
	 * back to the system can inadvertently kick off an htable crime wave
	 * as active processes repeatedly steal htables from one another,
	 * plummeting the system into a kind of HAT lawlessness that can
	 * become so violent as to impede the one thing that can end it:  the
	 * freeing of memory via ARC reclaim and other means.)  So if we're
	 * reaping, we limit ourselves to the first pass that steals cached
	 * htables that aren't in use -- which gives memory back, but averts
	 * the entire breakdown of social order.
	 */
	passes = reap ? 0 : htable_steal_passes;

	/*
	 * Loop through all user hats. The 1st pass takes cached htables that
	 * aren't in use. The later passes steal by removing mappings, too.
	 */
	atomic_inc_32(&htable_dont_cache);
	for (pass = 0; pass <= passes && stolen < cnt; ++pass) {
		threshold = pass * mmu.ptes_per_table / htable_steal_passes;

		mutex_enter(&hat_list_lock);

		/* skip the first hat (kernel) */
		hat = kas.a_hat->hat_next;
		for (;;) {
			/*
			 * Skip any hat that is already being stolen from.
			 *
			 * We skip SHARED hats, as these are dummy
			 * hats that host ISM shared page tables.
			 *
			 * We also skip if HAT_FREEING because hat_pte_unmap()
			 * won't zero out the PTE's. That would lead to hitting
			 * stale PTEs either here or under hat_unload() when we
			 * steal and unload the same page table in competing
			 * threads.
			 *
			 * We skip HATs that belong to CPUs, to make our lives
			 * simpler.
			 */
			while (hat != NULL && (hat->hat_flags &
			    (HAT_VICTIM | HAT_SHARED | HAT_FREEING |
			    HAT_PCP)) != 0) {
				hat = hat->hat_next;
			}

			if (hat == NULL)
				break;

			/*
			 * Mark the HAT as a stealing victim so that it is
			 * not freed from under us, e.g. in as_free()
			 */
			hat->hat_flags |= HAT_VICTIM;
			mutex_exit(&hat_list_lock);

			/*
			 * Take any htables from the hat's cached "free" list.
			 */
			hat_enter(hat);
			while ((ht = hat->hat_ht_cached) != NULL &&
			    stolen < cnt) {
				hat->hat_ht_cached = ht->ht_next;
				ht->ht_next = list;
				list = ht;
				++stolen;
			}
			hat_exit(hat);

			/*
			 * Don't steal active htables on first pass.
			 */
			if (pass != 0 && (stolen < cnt))
				htable_steal_active(hat, cnt, threshold,
				    &stolen, &list);

			/*
			 * do synchronous teardown for the reap case so that
			 * we can forget hat; at this time, hat is
			 * guaranteed to be around because HAT_VICTIM is set
			 * (see htable_free() for similar code)
			 */
			for (ht = list; (ht) && (reap); ht = ht->ht_next) {
				if (ht->ht_hat == NULL)
					continue;
				ASSERT(ht->ht_hat == hat);
				/*
				 * forget the hat
				 */
				ht->ht_hat = NULL;
			}

			mutex_enter(&hat_list_lock);

			/*
			 * Are we finished?
			 */
			if (stolen == cnt) {
				/*
				 * Try to spread the pain of stealing,
				 * move victim HAT to the end of the HAT list.
				 */
				if (pass >= 1 && cnt == 1 &&
				    kas.a_hat->hat_prev != hat)
					move_victim(hat);
				/*
				 * We are finished
				 */
			}

			/*
			 * Clear the victim flag, hat can go away now (once
			 * the lock is dropped)
			 */
			if (hat->hat_flags & HAT_VICTIM) {
				ASSERT(hat != kas.a_hat);
				hat->hat_flags &= ~HAT_VICTIM;
				cv_broadcast(&hat_list_cv);
			}

			/* move on to the next hat */
			hat = hat->hat_next;
		}

		mutex_exit(&hat_list_lock);

	}
	ASSERT(!MUTEX_HELD(&hat_list_lock));

	atomic_dec_32(&htable_dont_cache);
	return (list);
}

/*
 * This is invoked from kmem when the system is low on memory.  We try
 * to free hments, htables, and ptables to improve the memory situation.
 */
/*ARGSUSED*/
static void
htable_reap(void *handle)
{
	uint_t		reap_cnt;
	htable_t	*list;
	htable_t	*ht;

	HATSTAT_INC(hs_reap_attempts);
	if (!can_steal_post_boot)
		return;

	/*
	 * Try to reap 5% of the page tables bounded by a maximum of
	 * 5% of physmem and a minimum of 10.
	 */
	reap_cnt = MAX(MIN(physmem / 20, active_ptables / 20), 10);

	/*
	 * Note: htable_dont_cache should be set at the time of
	 * invoking htable_free()
	 */
	atomic_inc_32(&htable_dont_cache);
	/*
	 * Let htable_steal() do the work, we just call htable_free()
	 */
	XPV_DISALLOW_MIGRATE();
	list = htable_steal(reap_cnt, B_TRUE);
	XPV_ALLOW_MIGRATE();
	while ((ht = list) != NULL) {
		list = ht->ht_next;
		HATSTAT_INC(hs_reaped);
		htable_free(ht);
	}
	atomic_dec_32(&htable_dont_cache);

	/*
	 * Free up excess reserves
	 */
	htable_adjust_reserve();
	hment_adjust_reserve();
}

/*
 * Allocate an htable, stealing one or using the reserve if necessary
 */
static htable_t *
htable_alloc(
	hat_t		*hat,
	uintptr_t	vaddr,
	level_t		level,
	htable_t	*shared)
{
	htable_t	*ht = NULL;
	uint_t		is_copied;
	uint_t		is_bare = 0;
	uint_t		need_to_zero = 1;
	int		kmflags = (can_steal_post_boot ? KM_NOSLEEP : KM_SLEEP);

	if (level < 0 || level > TOP_LEVEL(hat))
		panic("htable_alloc(): level %d out of range\n", level);

	is_copied = (hat->hat_flags & HAT_COPIED) &&
	    level == hat->hat_max_level;
	if (is_copied || shared != NULL)
		is_bare = 1;

	/*
	 * First reuse a cached htable from the hat_ht_cached field, this
	 * avoids unnecessary trips through kmem/page allocators.
	 */
	if (hat->hat_ht_cached != NULL && !is_bare) {
		hat_enter(hat);
		ht = hat->hat_ht_cached;
		if (ht != NULL) {
			hat->hat_ht_cached = ht->ht_next;
			need_to_zero = 0;
			/* XX64 ASSERT() they're all zero somehow */
			ASSERT(ht->ht_pfn != PFN_INVALID);
		}
		hat_exit(hat);
	}

	if (ht == NULL) {
		/*
		 * Allocate an htable, possibly refilling the reserves.
		 */
		if (USE_HAT_RESERVES()) {
			ht = htable_get_reserve();
		} else {
			/*
			 * Donate successful htable allocations to the reserve.
			 */
			for (;;) {
				ht = kmem_cache_alloc(htable_cache, kmflags);
				if (ht == NULL)
					break;
				ht->ht_pfn = PFN_INVALID;
				if (USE_HAT_RESERVES() ||
				    htable_reserve_cnt >= htable_reserve_amount)
					break;
				htable_put_reserve(ht);
			}
		}

		/*
		 * allocate a page for the hardware page table if needed
		 */
		if (ht != NULL && !is_bare) {
			ht->ht_hat = hat;
			ht->ht_pfn = ptable_alloc((uintptr_t)ht);
			if (ht->ht_pfn == PFN_INVALID) {
				if (USE_HAT_RESERVES())
					htable_put_reserve(ht);
				else
					kmem_cache_free(htable_cache, ht);
				ht = NULL;
			}
		}
	}

	/*
	 * If allocations failed, kick off a kmem_reap() and resort to
	 * htable steal(). We may spin here if the system is very low on
	 * memory. If the kernel itself has consumed all memory and kmem_reap()
	 * can't free up anything, then we'll really get stuck here.
	 * That should only happen in a system where the administrator has
	 * misconfigured VM parameters via /etc/system.
	 */
	while (ht == NULL && can_steal_post_boot) {
		kmem_reap();
		ht = htable_steal(1, B_FALSE);
		HATSTAT_INC(hs_steals);

		/*
		 * If we stole for a bare htable, release the pagetable page.
		 */
		if (ht != NULL) {
			if (is_bare) {
				ptable_free(ht->ht_pfn);
				ht->ht_pfn = PFN_INVALID;
			}
		}
	}

	/*
	 * All attempts to allocate or steal failed. This should only happen
	 * if we run out of memory during boot, due perhaps to a huge
	 * boot_archive. At this point there's no way to continue.
	 */
	if (ht == NULL)
		panic("htable_alloc(): couldn't steal\n");

	/*
	 * Shared page tables have all entries locked and entries may not
	 * be added or deleted.
	 */
	ht->ht_flags = 0;
	if (shared != NULL) {
		ASSERT(shared->ht_valid_cnt > 0);
		ht->ht_flags |= HTABLE_SHARED_PFN;
		ht->ht_pfn = shared->ht_pfn;
		ht->ht_lock_cnt = 0;
		ht->ht_valid_cnt = 0;		/* updated in hat_share() */
		ht->ht_shares = shared;
		need_to_zero = 0;
	} else {
		ht->ht_shares = NULL;
		ht->ht_lock_cnt = 0;
		ht->ht_valid_cnt = 0;
	}

	/*
	 * setup flags, etc. for copied page tables.
	 */
	if (is_copied) {
		ht->ht_flags |= HTABLE_COPIED;
		ASSERT(ht->ht_pfn == PFN_INVALID);
		need_to_zero = 0;
	}

	/*
	 * fill in the htable
	 */
	ht->ht_hat = hat;
	ht->ht_parent = NULL;
	ht->ht_vaddr = vaddr;
	ht->ht_level = level;
	ht->ht_busy = 1;
	ht->ht_next = NULL;
	ht->ht_prev = NULL;

	/*
	 * Zero out any freshly allocated page table
	 */
	if (need_to_zero)
		x86pte_zero(ht, 0, mmu.ptes_per_table);

	return (ht);
}

/*
 * Free up an htable, either to a hat's cached list, the reserves or
 * back to kmem.
 */
static void
htable_free(htable_t *ht)
{
	hat_t *hat = ht->ht_hat;

	/*
	 * If the process isn't exiting, cache the free htable in the hat
	 * structure. We always do this for the boot time reserve. We don't
	 * do this if the hat is exiting or we are stealing/reaping htables.
	 */
	if (hat != NULL &&
	    !(ht->ht_flags & HTABLE_SHARED_PFN) &&
	    (use_boot_reserve ||
	    (!(hat->hat_flags & HAT_FREEING) && !htable_dont_cache))) {
		ASSERT((ht->ht_flags & HTABLE_COPIED) == 0);
		ASSERT(ht->ht_pfn != PFN_INVALID);
		hat_enter(hat);
		ht->ht_next = hat->hat_ht_cached;
		hat->hat_ht_cached = ht;
		hat_exit(hat);
		return;
	}

	/*
	 * If we have a hardware page table, free it.
	 * We don't free page tables that are accessed by sharing.
	 */
	if (ht->ht_flags & HTABLE_SHARED_PFN) {
		ASSERT(ht->ht_pfn != PFN_INVALID);
	} else if (!(ht->ht_flags & HTABLE_COPIED)) {
		ptable_free(ht->ht_pfn);
	}
	ht->ht_pfn = PFN_INVALID;

	/*
	 * Free it or put into reserves.
	 */
	if (USE_HAT_RESERVES() || htable_reserve_cnt < htable_reserve_amount) {
		htable_put_reserve(ht);
	} else {
		kmem_cache_free(htable_cache, ht);
		htable_adjust_reserve();
	}
}


/*
 * This is called when a hat is being destroyed or swapped out. We reap all
 * the remaining htables in the hat cache. If destroying all left over
 * htables are also destroyed.
 *
 * We also don't need to invalidate any of the PTPs nor do any demapping.
 */
void
htable_purge_hat(hat_t *hat)
{
	htable_t *ht;
	int h;

	/*
	 * Purge the htable cache if just reaping.
	 */
	if (!(hat->hat_flags & HAT_FREEING)) {
		atomic_inc_32(&htable_dont_cache);
		for (;;) {
			hat_enter(hat);
			ht = hat->hat_ht_cached;
			if (ht == NULL) {
				hat_exit(hat);
				break;
			}
			hat->hat_ht_cached = ht->ht_next;
			hat_exit(hat);
			htable_free(ht);
		}
		atomic_dec_32(&htable_dont_cache);
		return;
	}

	/*
	 * if freeing, no locking is needed
	 */
	while ((ht = hat->hat_ht_cached) != NULL) {
		hat->hat_ht_cached = ht->ht_next;
		htable_free(ht);
	}

	/*
	 * walk thru the htable hash table and free all the htables in it.
	 */
	for (h = 0; h < hat->hat_num_hash; ++h) {
		while ((ht = hat->hat_ht_hash[h]) != NULL) {
			if (ht->ht_next)
				ht->ht_next->ht_prev = ht->ht_prev;

			if (ht->ht_prev) {
				ht->ht_prev->ht_next = ht->ht_next;
			} else {
				ASSERT(hat->hat_ht_hash[h] == ht);
				hat->hat_ht_hash[h] = ht->ht_next;
			}
			htable_free(ht);
		}
	}
}

/*
 * Unlink an entry for a table at vaddr and level out of the existing table
 * one level higher. We are always holding the HASH_ENTER() when doing this.
 */
static void
unlink_ptp(htable_t *higher, htable_t *old, uintptr_t vaddr)
{
	uint_t		entry = htable_va2entry(vaddr, higher);
	x86pte_t	expect = MAKEPTP(old->ht_pfn, old->ht_level);
	x86pte_t	found;
	hat_t		*hat = old->ht_hat;

	ASSERT(higher->ht_busy > 0);
	ASSERT(higher->ht_valid_cnt > 0);
	ASSERT(old->ht_valid_cnt == 0);
	found = x86pte_cas(higher, entry, expect, 0);
	if (found != expect)
		panic("Bad PTP found=" FMT_PTE ", expected=" FMT_PTE,
		    found, expect);

	/*
	 * When a top level PTE changes for a copied htable, we must trigger a
	 * hat_pcp_update() on all HAT CPUs.
	 *
	 * If we don't need do do that, then we still have to INVLPG against an
	 * address covered by the inner page table, as the latest processors
	 * have TLB-like caches for non-leaf page table entries.
	 */
	if (!(hat->hat_flags & HAT_FREEING)) {
		hat_tlb_inval(hat, (higher->ht_flags & HTABLE_COPIED) ?
		    DEMAP_ALL_ADDR : old->ht_vaddr);
	}

	HTABLE_DEC(higher->ht_valid_cnt);
}

/*
 * Link an entry for a new table at vaddr and level into the existing table
 * one level higher. We are always holding the HASH_ENTER() when doing this.
 */
static void
link_ptp(htable_t *higher, htable_t *new, uintptr_t vaddr)
{
	uint_t		entry = htable_va2entry(vaddr, higher);
	x86pte_t	newptp = MAKEPTP(new->ht_pfn, new->ht_level);
	x86pte_t	found;

	ASSERT(higher->ht_busy > 0);

	ASSERT(new->ht_level != mmu.max_level);

	HTABLE_INC(higher->ht_valid_cnt);

	found = x86pte_cas(higher, entry, 0, newptp);
	if ((found & ~PT_REF) != 0)
		panic("HAT: ptp not 0, found=" FMT_PTE, found);

	/*
	 * When a top level PTE changes for a copied htable, we must trigger a
	 * hat_pcp_update() on all HAT CPUs.
	 *
	 * We also need to do this for the kernel hat on PAE 32 bit kernel.
	 */
	if ((higher->ht_flags & HTABLE_COPIED) != 0)
		hat_tlb_inval(higher->ht_hat, DEMAP_ALL_ADDR);
}

/*
 * Release of hold on an htable. If this is the last use and the pagetable
 * is empty we may want to free it, then recursively look at the pagetable
 * above it. The recursion is handled by the outer while() loop.
 *
 * On the metal, during process exit, we don't bother unlinking the tables from
 * upper level pagetables. They are instead handled in bulk by hat_free_end().
 * We can't do this on the hypervisor as we need the page table to be
 * implicitly unpinnned before it goes to the free page lists. This can't
 * happen unless we fully unlink it from the page table hierarchy.
 */
void
htable_release(htable_t *ht)
{
	uint_t		hashval;
	htable_t	*shared;
	htable_t	*higher;
	hat_t		*hat;
	uintptr_t	va;
	level_t		level;

	while (ht != NULL) {
		shared = NULL;
		for (;;) {
			hat = ht->ht_hat;
			va = ht->ht_vaddr;
			level = ht->ht_level;
			hashval = HTABLE_HASH(hat, va, level);

			/*
			 * The common case is that this isn't the last use of
			 * an htable so we don't want to free the htable.
			 */
			HTABLE_ENTER(hashval);
			ASSERT(ht->ht_valid_cnt >= 0);
			ASSERT(ht->ht_busy > 0);
			if (ht->ht_valid_cnt > 0)
				break;
			if (ht->ht_busy > 1)
				break;
			ASSERT(ht->ht_lock_cnt == 0);

			/*
			 * we always release empty shared htables
			 */
			if (!(ht->ht_flags & HTABLE_SHARED_PFN)) {

				/*
				 * don't release if in address space tear down
				 */
				if (hat->hat_flags & HAT_FREEING)
					break;

				/*
				 * At and above max_page_level, free if it's for
				 * a boot-time kernel mapping below kernelbase.
				 */
				if (level >= mmu.max_page_level &&
				    (hat != kas.a_hat || va >= kernelbase))
					break;
			}

			/*
			 * Remember if we destroy an htable that shares its PFN
			 * from elsewhere.
			 */
			if (ht->ht_flags & HTABLE_SHARED_PFN) {
				ASSERT(shared == NULL);
				shared = ht->ht_shares;
				HATSTAT_INC(hs_htable_unshared);
			}

			/*
			 * Handle release of a table and freeing the htable_t.
			 * Unlink it from the table higher (ie. ht_parent).
			 */
			higher = ht->ht_parent;
			ASSERT(higher != NULL);

			/*
			 * Unlink the pagetable.
			 */
			unlink_ptp(higher, ht, va);

			/*
			 * remove this htable from its hash list
			 */
			if (ht->ht_next)
				ht->ht_next->ht_prev = ht->ht_prev;

			if (ht->ht_prev) {
				ht->ht_prev->ht_next = ht->ht_next;
			} else {
				ASSERT(hat->hat_ht_hash[hashval] == ht);
				hat->hat_ht_hash[hashval] = ht->ht_next;
			}
			HTABLE_EXIT(hashval);
			htable_free(ht);
			ht = higher;
		}

		ASSERT(ht->ht_busy >= 1);
		--ht->ht_busy;
		HTABLE_EXIT(hashval);

		/*
		 * If we released a shared htable, do a release on the htable
		 * from which it shared
		 */
		ht = shared;
	}
}

/*
 * Find the htable for the pagetable at the given level for the given address.
 * If found acquires a hold that eventually needs to be htable_release()d
 */
htable_t *
htable_lookup(hat_t *hat, uintptr_t vaddr, level_t level)
{
	uintptr_t	base;
	uint_t		hashval;
	htable_t	*ht = NULL;

	ASSERT(level >= 0);
	ASSERT(level <= TOP_LEVEL(hat));

	if (level == TOP_LEVEL(hat)) {
		/*
		 * 32 bit address spaces on 64 bit kernels need to check
		 * for overflow of the 32 bit address space
		 */
		if ((hat->hat_flags & HAT_COPIED_32) &&
		    vaddr >= ((uint64_t)1 << 32))
			return (NULL);
		base = 0;
	} else {
		base = vaddr & LEVEL_MASK(level + 1);
	}

	hashval = HTABLE_HASH(hat, base, level);
	HTABLE_ENTER(hashval);
	for (ht = hat->hat_ht_hash[hashval]; ht; ht = ht->ht_next) {
		if (ht->ht_hat == hat &&
		    ht->ht_vaddr == base &&
		    ht->ht_level == level)
			break;
	}
	if (ht)
		++ht->ht_busy;

	HTABLE_EXIT(hashval);
	return (ht);
}

/*
 * Acquires a hold on a known htable (from a locked hment entry).
 */
void
htable_acquire(htable_t *ht)
{
	hat_t		*hat = ht->ht_hat;
	level_t		level = ht->ht_level;
	uintptr_t	base = ht->ht_vaddr;
	uint_t		hashval = HTABLE_HASH(hat, base, level);

	HTABLE_ENTER(hashval);
#ifdef DEBUG
	/*
	 * make sure the htable is there
	 */
	{
		htable_t	*h;

		for (h = hat->hat_ht_hash[hashval];
		    h && h != ht;
		    h = h->ht_next)
			;
		ASSERT(h == ht);
	}
#endif /* DEBUG */
	++ht->ht_busy;
	HTABLE_EXIT(hashval);
}

/*
 * Find the htable for the pagetable at the given level for the given address.
 * If found acquires a hold that eventually needs to be htable_release()d
 * If not found the table is created.
 *
 * Since we can't hold a hash table mutex during allocation, we have to
 * drop it and redo the search on a create. Then we may have to free the newly
 * allocated htable if another thread raced in and created it ahead of us.
 */
htable_t *
htable_create(
	hat_t		*hat,
	uintptr_t	vaddr,
	level_t		level,
	htable_t	*shared)
{
	uint_t		h;
	level_t		l;
	uintptr_t	base;
	htable_t	*ht;
	htable_t	*higher = NULL;
	htable_t	*new = NULL;

	if (level < 0 || level > TOP_LEVEL(hat))
		panic("htable_create(): level %d out of range\n", level);

	ht = NULL;
	/*
	 * Create the page tables in top down order.
	 */
	for (l = TOP_LEVEL(hat); l >= level; --l) {
		new = NULL;
		if (l == TOP_LEVEL(hat))
			base = 0;
		else
			base = vaddr & LEVEL_MASK(l + 1);

		h = HTABLE_HASH(hat, base, l);
try_again:
		/*
		 * look up the htable at this level
		 */
		HTABLE_ENTER(h);
		if (l == TOP_LEVEL(hat)) {
			ht = hat->hat_htable;
		} else {
			for (ht = hat->hat_ht_hash[h]; ht; ht = ht->ht_next) {
				ASSERT(ht->ht_hat == hat);
				if (ht->ht_vaddr == base &&
				    ht->ht_level == l)
					break;
			}
		}

		/*
		 * if we found the htable, increment its busy cnt
		 * and if we had allocated a new htable, free it.
		 */
		if (ht != NULL) {
			/*
			 * If we find a pre-existing shared table, it must
			 * share from the same place.
			 */
			if (l == level && shared && ht->ht_shares &&
			    ht->ht_shares != shared) {
				panic("htable shared from wrong place "
				    "found htable=%p shared=%p",
				    (void *)ht, (void *)shared);
			}
			++ht->ht_busy;
			HTABLE_EXIT(h);
			if (new)
				htable_free(new);
			if (higher != NULL)
				htable_release(higher);
			higher = ht;

		/*
		 * if we didn't find it on the first search
		 * allocate a new one and search again
		 */
		} else if (new == NULL) {
			HTABLE_EXIT(h);
			new = htable_alloc(hat, base, l,
			    l == level ? shared : NULL);
			goto try_again;

		/*
		 * 2nd search and still not there, use "new" table
		 * Link new table into higher, when not at top level.
		 */
		} else {
			ht = new;
			if (higher != NULL) {
				link_ptp(higher, ht, base);
				ht->ht_parent = higher;
			}
			ht->ht_next = hat->hat_ht_hash[h];
			ASSERT(ht->ht_prev == NULL);
			if (hat->hat_ht_hash[h])
				hat->hat_ht_hash[h]->ht_prev = ht;
			hat->hat_ht_hash[h] = ht;
			HTABLE_EXIT(h);

			/*
			 * Note we don't do htable_release(higher).
			 * That happens recursively when "new" is removed by
			 * htable_release() or htable_steal().
			 */
			higher = ht;

			/*
			 * If we just created a new shared page table we
			 * increment the shared htable's busy count, so that
			 * it can't be the victim of a steal even if it's empty.
			 */
			if (l == level && shared) {
				(void) htable_lookup(shared->ht_hat,
				    shared->ht_vaddr, shared->ht_level);
				HATSTAT_INC(hs_htable_shared);
			}
		}
	}

	return (ht);
}

/*
 * Inherit initial pagetables from the boot program. On the 64-bit
 * hypervisor we also temporarily mark the p_index field of page table
 * pages, so we know not to try making them writable in seg_kpm.
 */
void
htable_attach(
	hat_t *hat,
	uintptr_t base,
	level_t level,
	htable_t *parent,
	pfn_t pfn)
{
	htable_t	*ht;
	uint_t		h;
	uint_t		i;
	x86pte_t	pte;
	x86pte_t	*ptep;
	page_t		*pp;
	extern page_t	*boot_claim_page(pfn_t);

	ht = htable_get_reserve();
	if (level == mmu.max_level)
		kas.a_hat->hat_htable = ht;
	ht->ht_hat = hat;
	ht->ht_parent = parent;
	ht->ht_vaddr = base;
	ht->ht_level = level;
	ht->ht_busy = 1;
	ht->ht_next = NULL;
	ht->ht_prev = NULL;
	ht->ht_flags = 0;
	ht->ht_pfn = pfn;
	ht->ht_lock_cnt = 0;
	ht->ht_valid_cnt = 0;
	if (parent != NULL)
		++parent->ht_busy;

	h = HTABLE_HASH(hat, base, level);
	HTABLE_ENTER(h);
	ht->ht_next = hat->hat_ht_hash[h];
	ASSERT(ht->ht_prev == NULL);
	if (hat->hat_ht_hash[h])
		hat->hat_ht_hash[h]->ht_prev = ht;
	hat->hat_ht_hash[h] = ht;
	HTABLE_EXIT(h);

	/*
	 * make sure the page table physical page is not FREE
	 */
	if (page_resv(1, KM_NOSLEEP) == 0)
		panic("page_resv() failed in ptable alloc");

	pp = boot_claim_page(pfn);
	ASSERT(pp != NULL);

	/*
	 * Page table pages that were allocated by dboot or
	 * in very early startup didn't go through boot_mapin()
	 * and so won't have vnode/offsets. Fix that here.
	 */
	if (pp->p_vnode == NULL) {
		/* match offset calculation in page_get_physical() */
		u_offset_t offset = (uintptr_t)ht;
		if (offset > kernelbase)
			offset -= kernelbase;
		offset <<= MMU_PAGESHIFT;
		offset += mmu.hole_start;	/* something in VA hole */
		ASSERT(page_exists(&kvp, offset) == NULL);
		(void) page_hashin(pp, &kvp, offset, NULL);
	}
	page_downgrade(pp);

	/*
	 * Count valid mappings and recursively attach lower level pagetables.
	 * We also need to count this mapping in the hat; this code is part of
	 * building up the kernel hat and *page* mappings need to be counted
	 * within the hat.  Note that this is distinct from the htable count of
	 * valid PTEs, which includes PTPs as well as pages.  If we don't count
	 * these mappings, htable_walk() below may decide to overlook large or
	 * huge page mappings and confuse callers.
	 */
	ptep = kbm_remap_window(pfn_to_pa(pfn), 0);
	for (i = 0; i < HTABLE_NUM_PTES(ht); ++i) {
		if (mmu.pae_hat)
			pte = ptep[i];
		else
			pte = ((x86pte32_t *)ptep)[i];
		if (!IN_HYPERVISOR_VA(base) && PTE_ISVALID(pte)) {
			++ht->ht_valid_cnt;
			if (PTE_ISPAGE(pte, level)) {
				PGCNT_INC(hat, level)
			} else {
				htable_attach(hat, base, level - 1,
				    ht, PTE2PFN(pte, level));
				ptep = kbm_remap_window(pfn_to_pa(pfn), 0);
			}
		}
		base += LEVEL_SIZE(level);
		if (base == mmu.hole_start)
			base = (mmu.hole_end + MMU_PAGEOFFSET) & MMU_PAGEMASK;
	}

	/*
	 * As long as all the mappings we had were below kernel base
	 * we can release the htable.
	 */
	if (base < kernelbase)
		htable_release(ht);
}

/*
 * Walk through a given htable looking for the first valid entry.  This
 * routine takes both a starting and ending address.  The starting address
 * is required to be within the htable provided by the caller, but there is
 * no such restriction on the ending address.
 *
 * If the routine finds a valid entry in the htable (at or beyond the
 * starting address), the PTE (and its address) will be returned.
 * This PTE may correspond to either a page or a pagetable - it is the
 * caller's responsibility to determine which.  If no valid entry is
 * found, 0 (and invalid PTE) and the next unexamined address will be
 * returned.
 *
 * The loop has been carefully coded for optimization.
 */
static x86pte_t
htable_scan(htable_t *ht, uintptr_t *vap, uintptr_t eaddr)
{
	uint_t e;
	x86pte_t found_pte = (x86pte_t)0;
	caddr_t pte_ptr;
	caddr_t end_pte_ptr;
	int l = ht->ht_level;
	uintptr_t va = *vap & LEVEL_MASK(l);
	size_t pgsize = LEVEL_SIZE(l);

	ASSERT(va >= ht->ht_vaddr);
	ASSERT(va <= HTABLE_LAST_PAGE(ht));

	/*
	 * Compute the starting index and ending virtual address
	 */
	e = htable_va2entry(va, ht);

	/*
	 * The following page table scan code knows that the valid
	 * bit of a PTE is in the lowest byte AND that x86 is little endian!!
	 */
	pte_ptr = (caddr_t)x86pte_access_pagetable(ht, 0);
	end_pte_ptr = (caddr_t)PT_INDEX_PTR(pte_ptr, HTABLE_NUM_PTES(ht));
	pte_ptr = (caddr_t)PT_INDEX_PTR((x86pte_t *)pte_ptr, e);
	while (!PTE_ISVALID(*pte_ptr)) {
		va += pgsize;
		if (va >= eaddr)
			break;
		pte_ptr += mmu.pte_size;
		ASSERT(pte_ptr <= end_pte_ptr);
		if (pte_ptr == end_pte_ptr)
			break;
	}

	/*
	 * if we found a valid PTE, load the entire PTE
	 */
	if (va < eaddr && pte_ptr != end_pte_ptr)
		found_pte = GET_PTE((x86pte_t *)pte_ptr);
	x86pte_release_pagetable(ht);

	/*
	 * deal with VA hole on amd64
	 */
	if (l == mmu.max_level && va >= mmu.hole_start && va <= mmu.hole_end)
		va = mmu.hole_end + va - mmu.hole_start;

	*vap = va;
	return (found_pte);
}

/*
 * Find the address and htable for the first populated translation at or
 * above the given virtual address.  The caller may also specify an upper
 * limit to the address range to search.  Uses level information to quickly
 * skip unpopulated sections of virtual address spaces.
 *
 * If not found returns NULL. When found, returns the htable and virt addr
 * and has a hold on the htable.
 */
x86pte_t
htable_walk(
	struct hat *hat,
	htable_t **htp,
	uintptr_t *vaddr,
	uintptr_t eaddr)
{
	uintptr_t va = *vaddr;
	htable_t *ht;
	htable_t *prev = *htp;
	level_t l;
	level_t max_mapped_level;
	x86pte_t pte;

	ASSERT(eaddr > va);

	/*
	 * If this is a user address, then we know we need not look beyond
	 * kernelbase.
	 */
	ASSERT(hat == kas.a_hat || eaddr <= kernelbase ||
	    eaddr == HTABLE_WALK_TO_END);
	if (hat != kas.a_hat && eaddr == HTABLE_WALK_TO_END)
		eaddr = kernelbase;

	/*
	 * If we're coming in with a previous page table, search it first
	 * without doing an htable_lookup(), this should be frequent.
	 */
	if (prev) {
		ASSERT(prev->ht_busy > 0);
		ASSERT(prev->ht_vaddr <= va);
		l = prev->ht_level;
		if (va <= HTABLE_LAST_PAGE(prev)) {
			pte = htable_scan(prev, &va, eaddr);

			if (PTE_ISPAGE(pte, l)) {
				*vaddr = va;
				*htp = prev;
				return (pte);
			}
		}

		/*
		 * We found nothing in the htable provided by the caller,
		 * so fall through and do the full search
		 */
		htable_release(prev);
	}

	/*
	 * Find the level of the largest pagesize used by this HAT.
	 */
	if (hat->hat_ism_pgcnt > 0) {
		max_mapped_level = mmu.umax_page_level;
	} else {
		max_mapped_level = 0;
		for (l = 1; l <= mmu.max_page_level; ++l)
			if (hat->hat_pages_mapped[l] != 0)
				max_mapped_level = l;
	}

	while (va < eaddr && va >= *vaddr) {
		/*
		 *  Find lowest table with any entry for given address.
		 */
		for (l = 0; l <= TOP_LEVEL(hat); ++l) {
			ht = htable_lookup(hat, va, l);
			if (ht != NULL) {
				pte = htable_scan(ht, &va, eaddr);
				if (PTE_ISPAGE(pte, l)) {
					VERIFY(!IN_VA_HOLE(va));
					*vaddr = va;
					*htp = ht;
					return (pte);
				}
				htable_release(ht);
				break;
			}

			/*
			 * No htable at this level for the address. If there
			 * is no larger page size that could cover it, we can
			 * skip right to the start of the next page table.
			 */
			ASSERT(l < TOP_LEVEL(hat));
			if (l >= max_mapped_level) {
				va = NEXT_ENTRY_VA(va, l + 1);
				if (va >= eaddr)
					break;
			}
		}
	}

	*vaddr = 0;
	*htp = NULL;
	return (0);
}

/*
 * Find the htable and page table entry index of the given virtual address
 * with pagesize at or below given level.
 * If not found returns NULL. When found, returns the htable, sets
 * entry, and has a hold on the htable.
 */
htable_t *
htable_getpte(
	struct hat *hat,
	uintptr_t vaddr,
	uint_t *entry,
	x86pte_t *pte,
	level_t level)
{
	htable_t	*ht;
	level_t		l;
	uint_t		e;

	ASSERT(level <= mmu.max_page_level);

	for (l = 0; l <= level; ++l) {
		ht = htable_lookup(hat, vaddr, l);
		if (ht == NULL)
			continue;
		e = htable_va2entry(vaddr, ht);
		if (entry != NULL)
			*entry = e;
		if (pte != NULL)
			*pte = x86pte_get(ht, e);
		return (ht);
	}
	return (NULL);
}

/*
 * Find the htable and page table entry index of the given virtual address.
 * There must be a valid page mapped at the given address.
 * If not found returns NULL. When found, returns the htable, sets
 * entry, and has a hold on the htable.
 */
htable_t *
htable_getpage(struct hat *hat, uintptr_t vaddr, uint_t *entry)
{
	htable_t	*ht;
	uint_t		e;
	x86pte_t	pte;

	ht = htable_getpte(hat, vaddr, &e, &pte, mmu.max_page_level);
	if (ht == NULL)
		return (NULL);

	if (entry)
		*entry = e;

	if (PTE_ISPAGE(pte, ht->ht_level))
		return (ht);
	htable_release(ht);
	return (NULL);
}


void
htable_init()
{
	/*
	 * To save on kernel VA usage, we avoid debug information in 32 bit
	 * kernels.
	 */
	int	kmem_flags = KMC_NOHASH;

	/*
	 * initialize kmem caches
	 */
	htable_cache = kmem_cache_create("htable_t",
	    sizeof (htable_t), 0, NULL, NULL,
	    htable_reap, NULL, hat_memload_arena, kmem_flags);
}

/*
 * get the pte index for the virtual address in the given htable's pagetable
 */
uint_t
htable_va2entry(uintptr_t va, htable_t *ht)
{
	level_t	l = ht->ht_level;

	ASSERT(va >= ht->ht_vaddr);
	ASSERT(va <= HTABLE_LAST_PAGE(ht));
	return ((va >> LEVEL_SHIFT(l)) & (HTABLE_NUM_PTES(ht) - 1));
}

/*
 * Given an htable and the index of a pte in it, return the virtual address
 * of the page.
 */
uintptr_t
htable_e2va(htable_t *ht, uint_t entry)
{
	level_t	l = ht->ht_level;
	uintptr_t va;

	ASSERT(entry < HTABLE_NUM_PTES(ht));
	va = ht->ht_vaddr + ((uintptr_t)entry << LEVEL_SHIFT(l));

	/*
	 * Need to skip over any VA hole in top level table
	 */
	if (ht->ht_level == mmu.max_level && va >= mmu.hole_start)
		va += ((mmu.hole_end - mmu.hole_start) + 1);

	return (va);
}

/*
 * The code uses compare and swap instructions to read/write PTE's to
 * avoid atomicity problems, since PTEs can be 8 bytes on 32 bit systems.
 * will naturally be atomic.
 *
 * The combination of using kpreempt_disable()/_enable() and the hci_mutex
 * are used to ensure that an interrupt won't overwrite a temporary mapping
 * while it's in use. If an interrupt thread tries to access a PTE, it will
 * yield briefly back to the pinned thread which holds the cpu's hci_mutex.
 */
void
x86pte_cpu_init(cpu_t *cpu)
{
	struct hat_cpu_info *hci;

	hci = kmem_zalloc(sizeof (*hci), KM_SLEEP);
	mutex_init(&hci->hci_mutex, NULL, MUTEX_DEFAULT, NULL);
	cpu->cpu_hat_info = hci;
}

void
x86pte_cpu_fini(cpu_t *cpu)
{
	struct hat_cpu_info *hci = cpu->cpu_hat_info;

	kmem_free(hci, sizeof (*hci));
	cpu->cpu_hat_info = NULL;
}

/*
 * Disable preemption and establish a mapping to the pagetable with the
 * given pfn. This is optimized for there case where it's the same
 * pfn as we last used referenced from this CPU.
 */
static x86pte_t *
x86pte_access_pagetable(htable_t *ht, uint_t index)
{
	/*
	 * HTABLE_COPIED pagetables are contained in the hat_t
	 */
	if (ht->ht_flags & HTABLE_COPIED) {
		ASSERT3U(index, <, ht->ht_hat->hat_num_copied);
		return (PT_INDEX_PTR(ht->ht_hat->hat_copied_ptes, index));
	}
	return (x86pte_mapin(ht->ht_pfn, index, ht));
}

/*
 * map the given pfn into the page table window.
 */
/*ARGSUSED*/
x86pte_t *
x86pte_mapin(pfn_t pfn, uint_t index, htable_t *ht)
{
	x86pte_t *pteptr;
	x86pte_t pte = 0;
	x86pte_t newpte;
	int x;

	ASSERT(pfn != PFN_INVALID);

	if (!khat_running) {
		caddr_t va = kbm_remap_window(pfn_to_pa(pfn), 1);
		return (PT_INDEX_PTR(va, index));
	}

	/*
	 * If kpm is available, use it.
	 */
	if (kpm_vbase)
		return (PT_INDEX_PTR(hat_kpm_pfn2va(pfn), index));

	/*
	 * Disable preemption and grab the CPU's hci_mutex
	 */
	kpreempt_disable();

	ASSERT(CPU->cpu_hat_info != NULL);
	ASSERT(!(getcr4() & CR4_PCIDE));

	mutex_enter(&CPU->cpu_hat_info->hci_mutex);
	x = PWIN_TABLE(CPU->cpu_id);
	pteptr = (x86pte_t *)PWIN_PTE_VA(x);
	if (mmu.pae_hat)
		pte = *pteptr;
	else
		pte = *(x86pte32_t *)pteptr;

	newpte = MAKEPTE(pfn, 0) | mmu.pt_global | mmu.pt_nx;

	/*
	 * For hardware we can use a writable mapping.
	 */
	newpte |= PT_WRITABLE;

	if (!PTE_EQUIV(newpte, pte)) {
		XPV_ALLOW_PAGETABLE_UPDATES();
		if (mmu.pae_hat)
			*pteptr = newpte;
		else
			*(x86pte32_t *)pteptr = newpte;
		XPV_DISALLOW_PAGETABLE_UPDATES();
		mmu_flush_tlb_kpage((uintptr_t)PWIN_VA(x));
	}
	return (PT_INDEX_PTR(PWIN_VA(x), index));
}

/*
 * Release access to a page table.
 */
static void
x86pte_release_pagetable(htable_t *ht)
{
	if (ht->ht_flags & HTABLE_COPIED)
		return;

	x86pte_mapout();
}

void
x86pte_mapout(void)
{
	if (kpm_vbase != NULL || !khat_running)
		return;

	/*
	 * Drop the CPU's hci_mutex and restore preemption.
	 */
	mutex_exit(&CPU->cpu_hat_info->hci_mutex);
	kpreempt_enable();
}

/*
 * Atomic retrieval of a pagetable entry
 */
x86pte_t
x86pte_get(htable_t *ht, uint_t entry)
{
	x86pte_t	pte;
	x86pte_t	*ptep;

	/*
	 * Be careful that loading PAE entries in 32 bit kernel is atomic.
	 */
	ASSERT(entry < mmu.ptes_per_table);
	ptep = x86pte_access_pagetable(ht, entry);
	pte = GET_PTE(ptep);
	x86pte_release_pagetable(ht);
	return (pte);
}

/*
 * Atomic unconditional set of a page table entry, it returns the previous
 * value. For pre-existing mappings if the PFN changes, then we don't care
 * about the old pte's REF / MOD bits. If the PFN remains the same, we leave
 * the MOD/REF bits unchanged.
 *
 * If asked to overwrite a link to a lower page table with a large page
 * mapping, this routine returns the special value of LPAGE_ERROR. This
 * allows the upper HAT layers to retry with a smaller mapping size.
 */
x86pte_t
x86pte_set(htable_t *ht, uint_t entry, x86pte_t new, void *ptr)
{
	x86pte_t	old;
	x86pte_t	prev;
	x86pte_t	*ptep;
	level_t		l = ht->ht_level;
	x86pte_t	pfn_mask = (l != 0) ? PT_PADDR_LGPG : PT_PADDR;
	x86pte_t	n;
	uintptr_t	addr = htable_e2va(ht, entry);
	hat_t		*hat = ht->ht_hat;

	ASSERT(new != 0); /* don't use to invalidate a PTE, see x86pte_update */
	ASSERT(!(ht->ht_flags & HTABLE_SHARED_PFN));
	if (ptr == NULL)
		ptep = x86pte_access_pagetable(ht, entry);
	else
		ptep = ptr;

	/*
	 * Install the new PTE. If remapping the same PFN, then
	 * copy existing REF/MOD bits to new mapping.
	 */
	do {
		prev = GET_PTE(ptep);
		n = new;
		if (PTE_ISVALID(n) && (prev & pfn_mask) == (new & pfn_mask))
			n |= prev & (PT_REF | PT_MOD);

		/*
		 * Another thread may have installed this mapping already,
		 * flush the local TLB and be done.
		 */
		if (prev == n) {
			old = new;
			mmu_flush_tlb_page(addr);
			goto done;
		}

		/*
		 * Detect if we have a collision of installing a large
		 * page mapping where there already is a lower page table.
		 */
		if (l > 0 && (prev & PT_VALID) && !(prev & PT_PAGESIZE)) {
			old = LPAGE_ERROR;
			goto done;
		}

		XPV_ALLOW_PAGETABLE_UPDATES();
		old = CAS_PTE(ptep, prev, n);
		XPV_DISALLOW_PAGETABLE_UPDATES();
	} while (old != prev);

	/*
	 * Do a TLB demap if needed, ie. the old pte was valid.
	 *
	 * Note that a stale TLB writeback to the PTE here either can't happen
	 * or doesn't matter. The PFN can only change for NOSYNC|NOCONSIST
	 * mappings, but they were created with REF and MOD already set, so
	 * no stale writeback will happen.
	 *
	 * Segmap is the only place where remaps happen on the same pfn and for
	 * that we want to preserve the stale REF/MOD bits.
	 */
	if (old & PT_REF)
		hat_tlb_inval(hat, addr);

done:
	if (ptr == NULL)
		x86pte_release_pagetable(ht);
	return (old);
}

/*
 * Atomic compare and swap of a page table entry. No TLB invalidates are done.
 * This is used for links between pagetables of different levels.
 * Note we always create these links with dirty/access set, so they should
 * never change.
 */
x86pte_t
x86pte_cas(htable_t *ht, uint_t entry, x86pte_t old, x86pte_t new)
{
	x86pte_t	pte;
	x86pte_t	*ptep;

	ptep = x86pte_access_pagetable(ht, entry);
	XPV_ALLOW_PAGETABLE_UPDATES();
	pte = CAS_PTE(ptep, old, new);
	XPV_DISALLOW_PAGETABLE_UPDATES();
	x86pte_release_pagetable(ht);
	return (pte);
}

/*
 * Invalidate a page table entry as long as it currently maps something that
 * matches the value determined by expect.
 *
 * If tlb is set, also invalidates any TLB entries.
 *
 * Returns the previous value of the PTE.
 */
x86pte_t
x86pte_inval(
	htable_t *ht,
	uint_t entry,
	x86pte_t expect,
	x86pte_t *pte_ptr,
	boolean_t tlb)
{
	x86pte_t	*ptep;
	x86pte_t	oldpte;
	x86pte_t	found;

	ASSERT(!(ht->ht_flags & HTABLE_SHARED_PFN));
	ASSERT(ht->ht_level <= mmu.max_page_level);

	if (pte_ptr != NULL)
		ptep = pte_ptr;
	else
		ptep = x86pte_access_pagetable(ht, entry);

	/*
	 * Note that the loop is needed to handle changes due to h/w updating
	 * of PT_MOD/PT_REF.
	 */
	do {
		oldpte = GET_PTE(ptep);
		if (expect != 0 && (oldpte & PT_PADDR) != (expect & PT_PADDR))
			goto done;
		XPV_ALLOW_PAGETABLE_UPDATES();
		found = CAS_PTE(ptep, oldpte, 0);
		XPV_DISALLOW_PAGETABLE_UPDATES();
	} while (found != oldpte);
	if (tlb && (oldpte & (PT_REF | PT_MOD)))
		hat_tlb_inval(ht->ht_hat, htable_e2va(ht, entry));

done:
	if (pte_ptr == NULL)
		x86pte_release_pagetable(ht);
	return (oldpte);
}

/*
 * Change a page table entry af it currently matches the value in expect.
 */
x86pte_t
x86pte_update(
	htable_t *ht,
	uint_t entry,
	x86pte_t expect,
	x86pte_t new)
{
	x86pte_t	*ptep;
	x86pte_t	found;

	ASSERT(new != 0);
	ASSERT(!(ht->ht_flags & HTABLE_SHARED_PFN));
	ASSERT(ht->ht_level <= mmu.max_page_level);

	ptep = x86pte_access_pagetable(ht, entry);
	XPV_ALLOW_PAGETABLE_UPDATES();
	found = CAS_PTE(ptep, expect, new);
	XPV_DISALLOW_PAGETABLE_UPDATES();
	if (found == expect) {
		hat_tlb_inval(ht->ht_hat, htable_e2va(ht, entry));

		/*
		 * When removing write permission *and* clearing the
		 * MOD bit, check if a write happened via a stale
		 * TLB entry before the TLB shootdown finished.
		 *
		 * If it did happen, simply re-enable write permission and
		 * act like the original CAS failed.
		 */
		if ((expect & (PT_WRITABLE | PT_MOD)) == PT_WRITABLE &&
		    (new & (PT_WRITABLE | PT_MOD)) == 0 &&
		    (GET_PTE(ptep) & PT_MOD) != 0) {
			do {
				found = GET_PTE(ptep);
				XPV_ALLOW_PAGETABLE_UPDATES();
				found =
				    CAS_PTE(ptep, found, found | PT_WRITABLE);
				XPV_DISALLOW_PAGETABLE_UPDATES();
			} while ((found & PT_WRITABLE) == 0);
		}
	}
	x86pte_release_pagetable(ht);
	return (found);
}

/*
 * Copy page tables - this is just a little more complicated than the
 * previous routines. Note that it's also not atomic! It also is never
 * used for HTABLE_COPIED pagetables.
 */
void
x86pte_copy(htable_t *src, htable_t *dest, uint_t entry, uint_t count)
{
	caddr_t	src_va;
	caddr_t dst_va;
	size_t size;
	x86pte_t *pteptr;
	x86pte_t pte;

	ASSERT(khat_running);
	ASSERT(!(dest->ht_flags & HTABLE_COPIED));
	ASSERT(!(src->ht_flags & HTABLE_COPIED));
	ASSERT(!(src->ht_flags & HTABLE_SHARED_PFN));
	ASSERT(!(dest->ht_flags & HTABLE_SHARED_PFN));

	/*
	 * Acquire access to the CPU pagetable windows for the dest and source.
	 */
	dst_va = (caddr_t)x86pte_access_pagetable(dest, entry);
	if (kpm_vbase) {
		src_va = (caddr_t)
		    PT_INDEX_PTR(hat_kpm_pfn2va(src->ht_pfn), entry);
	} else {
		uint_t x = PWIN_SRC(CPU->cpu_id);

		ASSERT(!(getcr4() & CR4_PCIDE));

		/*
		 * Finish defining the src pagetable mapping
		 */
		src_va = (caddr_t)PT_INDEX_PTR(PWIN_VA(x), entry);
		pte = MAKEPTE(src->ht_pfn, 0) | mmu.pt_global | mmu.pt_nx;
		pteptr = (x86pte_t *)PWIN_PTE_VA(x);
		if (mmu.pae_hat)
			*pteptr = pte;
		else
			*(x86pte32_t *)pteptr = pte;
		mmu_flush_tlb_kpage((uintptr_t)PWIN_VA(x));
	}

	/*
	 * now do the copy
	 */
	size = count << mmu.pte_size_shift;
	bcopy(src_va, dst_va, size);

	x86pte_release_pagetable(dest);
}

/*
 * Zero page table entries - Note this doesn't use atomic stores!
 */
static void
x86pte_zero(htable_t *dest, uint_t entry, uint_t count)
{
	caddr_t dst_va;
	size_t size;

	/*
	 * Map in the page table to be zeroed.
	 */
	ASSERT(!(dest->ht_flags & HTABLE_SHARED_PFN));
	ASSERT(!(dest->ht_flags & HTABLE_COPIED));

	dst_va = (caddr_t)x86pte_access_pagetable(dest, entry);

	size = count << mmu.pte_size_shift;
	ASSERT(size > BLOCKZEROALIGN);
	block_zero_no_xmm(dst_va, size);

	x86pte_release_pagetable(dest);
}

/*
 * Called to ensure that all pagetables are in the system dump
 */
void
hat_dump(void)
{
	hat_t *hat;
	uint_t h;
	htable_t *ht;

	/*
	 * Dump all page tables
	 */
	for (hat = kas.a_hat; hat != NULL; hat = hat->hat_next) {
		for (h = 0; h < hat->hat_num_hash; ++h) {
			for (ht = hat->hat_ht_hash[h]; ht; ht = ht->ht_next) {
				if ((ht->ht_flags & HTABLE_COPIED) == 0)
					dump_page(ht->ht_pfn);
			}
		}
	}
}
