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
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */
/*
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2018 Joyent, Inc.  All rights reserved.
 * Copyright (c) 2014, 2015 by Delphix. All rights reserved.
 * Copyright 2022 Tintri by DDN, Inc. All rights reserved.
 */

/*
 * VM - Hardware Address Translation management for i386 and amd64
 *
 * Implementation of the interfaces described in <common/vm/hat.h>
 *
 * Nearly all the details of how the hardware is managed should not be
 * visible outside this layer except for misc. machine specific functions
 * that work in conjunction with this code.
 *
 * Routines used only inside of i86pc/vm start with hati_ for HAT Internal.
 */

/*
 * amd64 HAT Design
 *
 * ----------
 * Background
 * ----------
 *
 * On x86, the address space is shared between a user process and the kernel.
 * This is different from SPARC. Conventionally, the kernel lives at the top of
 * the address space and the user process gets to enjoy the rest of it. If you
 * look at the image of the address map in uts/i86pc/os/startup.c, you'll get a
 * rough sense of how the address space is laid out and used.
 *
 * Every unique address space is represented by an instance of a HAT structure
 * called a 'hat_t'. In addition to a hat_t structure for each process, there is
 * also one that is used for the kernel (kas.a_hat), and each CPU ultimately
 * also has a HAT.
 *
 * Each HAT contains a pointer to its root page table. This root page table is
 * what we call an L3 page table in illumos and Intel calls the PML4. It is the
 * physical address of the L3 table that we place in the %cr3 register which the
 * processor uses.
 *
 * Each of the many layers of the page table is represented by a structure
 * called an htable_t. The htable_t manages a set of 512 8-byte entries. The
 * number of entries in a given page table is constant across all different
 * level page tables. Note, this is only true on amd64. This has not always been
 * the case on x86.
 *
 * Each entry in a page table, generally referred to as a PTE, may refer to
 * another page table or a memory location, depending on the level of the page
 * table and the use of large pages. Importantly, the top-level L3 page table
 * (PML4) only supports linking to further page tables. This is also true on
 * systems which support a 5th level page table (which we do not currently
 * support).
 *
 * Historically, on x86, when a process was running on CPU, the root of the page
 * table was inserted into %cr3 on each CPU on which it was currently running.
 * When processes would switch (by calling hat_switch()), then the value in %cr3
 * on that CPU would change to that of the new HAT. While this behavior is still
 * maintained in the xpv kernel, this is not what is done today.
 *
 * -------------------
 * Per-CPU Page Tables
 * -------------------
 *
 * Throughout the system the 64-bit kernel has a notion of what it calls a
 * per-CPU page table or PCP. The notion of a per-CPU page table was originally
 * introduced as part of the original work to support x86 PAE. On the 64-bit
 * kernel, it was originally used for 32-bit processes running on the 64-bit
 * kernel. The rationale behind this was that each 32-bit process could have all
 * of its memory represented in a single L2 page table as each L2 page table
 * entry represents 1 GbE of memory.
 *
 * Following on from this, the idea was that given that all of the L3 page table
 * entries for 32-bit processes are basically going to be identical with the
 * exception of the first entry in the page table, why not share those page
 * table entries. This gave rise to the idea of a per-CPU page table.
 *
 * The way this works is that we have a member in the machcpu_t called the
 * mcpu_hat_info. That structure contains two different 4k pages: one that
 * represents the L3 page table and one that represents an L2 page table. When
 * the CPU starts up, the L3 page table entries are copied in from the kernel's
 * page table. The L3 kernel entries do not change throughout the lifetime of
 * the kernel. The kernel portion of these L3 pages for each CPU have the same
 * records, meaning that they point to the same L2 page tables and thus see a
 * consistent view of the world.
 *
 * When a 32-bit process is loaded into this world, we copy the 32-bit process's
 * four top-level page table entries into the CPU's L2 page table and then set
 * the CPU's first L3 page table entry to point to the CPU's L2 page.
 * Specifically, in hat_pcp_update(), we're copying from the process's
 * HAT_COPIED_32 HAT into the page tables specific to this CPU.
 *
 * As part of the implementation of kernel page table isolation, this was also
 * extended to 64-bit processes. When a 64-bit process runs, we'll copy their L3
 * PTEs across into the current CPU's L3 page table. (As we can't do the
 * first-L3-entry trick for 64-bit processes, ->hci_pcp_l2ptes is unused in this
 * case.)
 *
 * The use of per-CPU page tables has a lot of implementation ramifications. A
 * HAT that runs a user process will be flagged with the HAT_COPIED flag to
 * indicate that it is using the per-CPU page table functionality. In tandem
 * with the HAT, the top-level htable_t will be flagged with the HTABLE_COPIED
 * flag. If the HAT represents a 32-bit process, then we will also set the
 * HAT_COPIED_32 flag on that hat_t.
 *
 * These two flags work together. The top-level htable_t when using per-CPU page
 * tables is 'virtual'. We never allocate a ptable for this htable_t (i.e.
 * ht->ht_pfn is PFN_INVALID).  Instead, when we need to modify a PTE in an
 * HTABLE_COPIED ptable, x86pte_access_pagetable() will redirect any accesses to
 * ht_hat->hat_copied_ptes.
 *
 * Of course, such a modification won't actually modify the HAT_PCP page tables
 * that were copied from the HAT_COPIED htable. When we change the top level
 * page table entries (L2 PTEs for a 32-bit process and L3 PTEs for a 64-bit
 * process), we need to make sure to trigger hat_pcp_update() on all CPUs that
 * are currently tied to this HAT (including the current CPU).
 *
 * To do this, PCP piggy-backs on TLB invalidation, specifically via the
 * hat_tlb_inval() path from link_ptp() and unlink_ptp().
 *
 * (Importantly, in all such cases, when this is in operation, the top-level
 * entry should not be able to refer to an actual page table entry that can be
 * changed and consolidated into a large page. If large page consolidation is
 * required here, then there will be much that needs to be reconsidered.)
 *
 * -----------------------------------------------
 * Kernel Page Table Isolation and the Per-CPU HAT
 * -----------------------------------------------
 *
 * All Intel CPUs that support speculative execution and paging are subject to a
 * series of bugs that have been termed 'Meltdown'. These exploits allow a user
 * process to read kernel memory through cache side channels and speculative
 * execution. To mitigate this on vulnerable CPUs, we need to use a technique
 * called kernel page table isolation. What this requires is that we have two
 * different page table roots. When executing in kernel mode, we will use a %cr3
 * value that has both the user and kernel pages. However when executing in user
 * mode, we will need to have a %cr3 that has all of the user pages; however,
 * only a subset of the kernel pages required to operate.
 *
 * These kernel pages that we need mapped are:
 *
 *   o Kernel Text that allows us to switch between the cr3 values.
 *   o The current global descriptor table (GDT)
 *   o The current interrupt descriptor table (IDT)
 *   o The current task switching state (TSS)
 *   o The current local descriptor table (LDT)
 *   o Stacks and scratch space used by the interrupt handlers
 *
 * For more information on the stack switching techniques, construction of the
 * trampolines, and more, please see i86pc/ml/kpti_trampolines.s. The most
 * important part of these mappings are the following two constraints:
 *
 *   o The mappings are all per-CPU (except for read-only text)
 *   o The mappings are static. They are all established before the CPU is
 *     started (with the exception of the boot CPU).
 *
 * To facilitate the kernel page table isolation we employ our per-CPU
 * page tables discussed in the previous section and add the notion of a per-CPU
 * HAT. Fundamentally we have a second page table root. There is both a kernel
 * page table (hci_pcp_l3ptes), and a user L3 page table (hci_user_l3ptes).
 * Both will have the user page table entries copied into them, the same way
 * that we discussed in the section 'Per-CPU Page Tables'.
 *
 * The complex part of this is how do we construct the set of kernel mappings
 * that should be present when running with the user page table. To answer that,
 * we add the notion of a per-CPU HAT. This HAT functions like a normal HAT,
 * except that it's not really associated with an address space the same way
 * that other HATs are.
 *
 * This HAT lives off of the 'struct hat_cpu_info' which is a member of the
 * machcpu in the member hci_user_hat. We use this per-CPU HAT to create the set
 * of kernel mappings that should be present on this CPU. The kernel mappings
 * are added to the per-CPU HAT through the function hati_cpu_punchin(). Once a
 * mapping has been punched in, it may not be punched out. The reason that we
 * opt to leverage a HAT structure is that it knows how to allocate and manage
 * all of the lower level page tables as required.
 *
 * Because all of the mappings are present at the beginning of time for this CPU
 * and none of the mappings are in the kernel pageable segment, we don't have to
 * worry about faulting on these HAT structures and thus the notion of the
 * current HAT that we're using is always the appropriate HAT for the process
 * (usually a user HAT or the kernel's HAT).
 *
 * A further constraint we place on the system with these per-CPU HATs is that
 * they are not subject to htable_steal(). Because each CPU will have a rather
 * fixed number of page tables, the same way that we don't steal from the
 * kernel's HAT, it was determined that we should not steal from this HAT due to
 * the complications involved and somewhat criminal nature of htable_steal().
 *
 * The per-CPU HAT is initialized in hat_pcp_setup() which is called as part of
 * onlining the CPU, but before the CPU is actually started. The per-CPU HAT is
 * removed in hat_pcp_teardown() which is called when a CPU is being offlined to
 * be removed from the system (which is different from what psradm usually
 * does).
 *
 * Finally, once the CPU has been onlined, the set of mappings in the per-CPU
 * HAT must not change. The HAT related functions that we call are not meant to
 * be called when we're switching between processes. For example, it is quite
 * possible that if they were, they would try to grab an htable mutex which
 * another thread might have. One needs to treat hat_switch() as though they
 * were above LOCK_LEVEL and therefore _must not_ block under any circumstance.
 */

#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/disp.h>
#include <sys/shm.h>
#include <sys/sysmacros.h>
#include <sys/machparam.h>
#include <sys/vmem.h>
#include <sys/vmsystm.h>
#include <sys/promif.h>
#include <sys/var.h>
#include <sys/x86_archext.h>
#include <sys/atomic.h>
#include <sys/bitmap.h>
#include <sys/controlregs.h>
#include <sys/bootconf.h>
#include <sys/bootsvcs.h>
#include <sys/bootinfo.h>
#include <sys/archsystm.h>

#include <vm/seg_kmem.h>
#include <vm/hat_i86.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <vm/seg_kp.h>
#include <vm/seg_kpm.h>
#include <vm/vm_dep.h>
#include <vm/kboot_mmu.h>
#include <vm/seg_spt.h>

#include <sys/cmn_err.h>

/*
 * Basic parameters for hat operation.
 */
struct hat_mmu_info mmu;

/*
 * The page that is the kernel's top level pagetable.
 *
 * For 32 bit PAE support on i86pc, the kernel hat will use the 1st 4 entries
 * on this 4K page for its top level page table. The remaining groups of
 * 4 entries are used for per processor copies of user PCP pagetables for
 * running threads.  See hat_switch() and reload_pae32() for details.
 *
 * pcp_page[0..3] - level==2 PTEs for kernel HAT
 * pcp_page[4..7] - level==2 PTEs for user thread on cpu 0
 * pcp_page[8..11]  - level==2 PTE for user thread on cpu 1
 * etc...
 *
 * On the 64-bit kernel, this is the normal root of the page table and there is
 * nothing special about it when used for other CPUs.
 */
static x86pte_t *pcp_page;

/*
 * forward declaration of internal utility routines
 */
static x86pte_t hati_update_pte(htable_t *ht, uint_t entry, x86pte_t expected,
	x86pte_t new);

/*
 * The kernel address space exists in all non-HAT_COPIED HATs. To implement this
 * the kernel reserves a fixed number of entries in the topmost level(s) of page
 * tables. The values are setup during startup and then copied to every user hat
 * created by hat_alloc(). This means that kernelbase must be:
 *
 *	  4Meg aligned for 32 bit kernels
 *	512Gig aligned for x86_64 64 bit kernel
 *
 * The hat_kernel_range_ts describe what needs to be copied from kernel hat
 * to each user hat.
 */
typedef struct hat_kernel_range {
	level_t		hkr_level;
	uintptr_t	hkr_start_va;
	uintptr_t	hkr_end_va;	/* zero means to end of memory */
} hat_kernel_range_t;
#define	NUM_KERNEL_RANGE 2
static hat_kernel_range_t kernel_ranges[NUM_KERNEL_RANGE];
static int num_kernel_ranges;

uint_t use_boot_reserve = 1;	/* cleared after early boot process */
uint_t can_steal_post_boot = 0;	/* set late in boot to enable stealing */

/*
 * enable_1gpg: controls 1g page support for user applications.
 * By default, 1g pages are exported to user applications. enable_1gpg can
 * be set to 0 to not export.
 */
int	enable_1gpg = 1;

/*
 * AMD shanghai processors provide better management of 1gb ptes in its tlb.
 * By default, 1g page support will be disabled for pre-shanghai AMD
 * processors that don't have optimal tlb support for the 1g page size.
 * chk_optimal_1gtlb can be set to 0 to force 1g page support on sub-optimal
 * processors.
 */
int	chk_optimal_1gtlb = 1;


#ifdef DEBUG
uint_t	map1gcnt;
#endif


/*
 * A cpuset for all cpus. This is used for kernel address cross calls, since
 * the kernel addresses apply to all cpus.
 */
cpuset_t khat_cpuset;

/*
 * management stuff for hat structures
 */
kmutex_t	hat_list_lock;
kcondvar_t	hat_list_cv;
kmem_cache_t	*hat_cache;
kmem_cache_t	*hat_hash_cache;
kmem_cache_t	*hat32_hash_cache;

/*
 * Simple statistics
 */
struct hatstats hatstat;

/*
 * Some earlier hypervisor versions do not emulate cmpxchg of PTEs
 * correctly.  For such hypervisors we must set PT_USER for kernel
 * entries ourselves (normally the emulation would set PT_USER for
 * kernel entries and PT_USER|PT_GLOBAL for user entries).  pt_kern is
 * thus set appropriately.  Note that dboot/kbm is OK, as only the full
 * HAT uses cmpxchg() and the other paths (hypercall etc.) were never
 * incorrect.
 */
int pt_kern;

extern pfn_t memseg_get_start(struct memseg *);

#define	PP_GETRM(pp, rmmask)    (pp->p_nrm & rmmask)
#define	PP_ISMOD(pp)		PP_GETRM(pp, P_MOD)
#define	PP_ISREF(pp)		PP_GETRM(pp, P_REF)
#define	PP_ISRO(pp)		PP_GETRM(pp, P_RO)

#define	PP_SETRM(pp, rm)	atomic_orb(&(pp->p_nrm), rm)
#define	PP_SETMOD(pp)		PP_SETRM(pp, P_MOD)
#define	PP_SETREF(pp)		PP_SETRM(pp, P_REF)
#define	PP_SETRO(pp)		PP_SETRM(pp, P_RO)

#define	PP_CLRRM(pp, rm)	atomic_andb(&(pp->p_nrm), ~(rm))
#define	PP_CLRMOD(pp)   	PP_CLRRM(pp, P_MOD)
#define	PP_CLRREF(pp)   	PP_CLRRM(pp, P_REF)
#define	PP_CLRRO(pp)    	PP_CLRRM(pp, P_RO)
#define	PP_CLRALL(pp)		PP_CLRRM(pp, P_MOD | P_REF | P_RO)

/*
 * kmem cache constructor for struct hat
 */
/*ARGSUSED*/
static int
hati_constructor(void *buf, void *handle, int kmflags)
{
	hat_t	*hat = buf;

	mutex_init(&hat->hat_mutex, NULL, MUTEX_DEFAULT, NULL);
	bzero(hat->hat_pages_mapped,
	    sizeof (pgcnt_t) * (mmu.max_page_level + 1));
	hat->hat_ism_pgcnt = 0;
	hat->hat_stats = 0;
	hat->hat_flags = 0;
	CPUSET_ZERO(hat->hat_cpus);
	hat->hat_htable = NULL;
	hat->hat_ht_hash = NULL;
	return (0);
}

/*
 * Put it at the start of the global list of all hats (used by stealing)
 *
 * kas.a_hat is not in the list but is instead used to find the
 * first and last items in the list.
 *
 * - kas.a_hat->hat_next points to the start of the user hats.
 *   The list ends where hat->hat_next == NULL
 *
 * - kas.a_hat->hat_prev points to the last of the user hats.
 *   The list begins where hat->hat_prev == NULL
 */
static void
hat_list_append(hat_t *hat)
{
	mutex_enter(&hat_list_lock);
	hat->hat_prev = NULL;
	hat->hat_next = kas.a_hat->hat_next;
	if (hat->hat_next)
		hat->hat_next->hat_prev = hat;
	else
		kas.a_hat->hat_prev = hat;
	kas.a_hat->hat_next = hat;
	mutex_exit(&hat_list_lock);
}

/*
 * Allocate a hat structure for as. We also create the top level
 * htable and initialize it to contain the kernel hat entries.
 */
hat_t *
hat_alloc(struct as *as)
{
	hat_t			*hat;
	htable_t		*ht;	/* top level htable */
	uint_t			use_copied;
	uint_t			r;
	hat_kernel_range_t	*rp;
	uintptr_t		va;
	uintptr_t		eva;
	uint_t			start;
	uint_t			cnt;
	htable_t		*src;
	boolean_t		use_hat32_cache;

	/*
	 * Once we start creating user process HATs we can enable
	 * the htable_steal() code.
	 */
	if (can_steal_post_boot == 0)
		can_steal_post_boot = 1;

	ASSERT(AS_WRITE_HELD(as));
	hat = kmem_cache_alloc(hat_cache, KM_SLEEP);
	hat->hat_as = as;
	mutex_init(&hat->hat_mutex, NULL, MUTEX_DEFAULT, NULL);
	ASSERT(hat->hat_flags == 0);

	/*
	 * All processes use HAT_COPIED on the 64-bit kernel if KPTI is
	 * turned on.
	 */
	if (ttoproc(curthread)->p_model == DATAMODEL_ILP32) {
		use_copied = 1;
		hat->hat_max_level = mmu.max_level32;
		hat->hat_num_copied = mmu.num_copied_ents32;
		use_hat32_cache = B_TRUE;
		hat->hat_flags |= HAT_COPIED_32;
		HATSTAT_INC(hs_hat_copied32);
	} else if (kpti_enable == 1) {
		use_copied = 1;
		hat->hat_max_level = mmu.max_level;
		hat->hat_num_copied = mmu.num_copied_ents;
		use_hat32_cache = B_FALSE;
		HATSTAT_INC(hs_hat_copied64);
	} else {
		use_copied = 0;
		use_hat32_cache = B_FALSE;
		hat->hat_max_level = mmu.max_level;
		hat->hat_num_copied = 0;
		hat->hat_flags = 0;
		HATSTAT_INC(hs_hat_normal64);
	}

	if (use_copied) {
		hat->hat_flags |= HAT_COPIED;
		bzero(hat->hat_copied_ptes, sizeof (hat->hat_copied_ptes));
	}

	/*
	 * Allocate the htable hash. For 32-bit PCP processes we use the
	 * hat32_hash_cache. However, for 64-bit PCP processes we do not as the
	 * number of entries that they have to handle is closer to
	 * hat_hash_cache in count (though there will be more wastage when we
	 * have more DRAM in the system and thus push down the user address
	 * range).
	 */
	if (use_hat32_cache) {
		hat->hat_num_hash = mmu.hat32_hash_cnt;
		hat->hat_ht_hash = kmem_cache_alloc(hat32_hash_cache, KM_SLEEP);
	} else {
		hat->hat_num_hash = mmu.hash_cnt;
		hat->hat_ht_hash = kmem_cache_alloc(hat_hash_cache, KM_SLEEP);
	}
	bzero(hat->hat_ht_hash, hat->hat_num_hash * sizeof (htable_t *));

	/*
	 * Initialize Kernel HAT entries at the top of the top level page
	 * tables for the new hat.
	 */
	hat->hat_htable = NULL;
	hat->hat_ht_cached = NULL;
	XPV_DISALLOW_MIGRATE();
	ht = htable_create(hat, (uintptr_t)0, TOP_LEVEL(hat), NULL);
	hat->hat_htable = ht;

	if (hat->hat_flags & HAT_COPIED)
		goto init_done;

	for (r = 0; r < num_kernel_ranges; ++r) {
		rp = &kernel_ranges[r];
		for (va = rp->hkr_start_va; va != rp->hkr_end_va;
		    va += cnt * LEVEL_SIZE(rp->hkr_level)) {

			if (rp->hkr_level == TOP_LEVEL(hat))
				ht = hat->hat_htable;
			else
				ht = htable_create(hat, va, rp->hkr_level,
				    NULL);

			start = htable_va2entry(va, ht);
			cnt = HTABLE_NUM_PTES(ht) - start;
			eva = va +
			    ((uintptr_t)cnt << LEVEL_SHIFT(rp->hkr_level));
			if (rp->hkr_end_va != 0 &&
			    (eva > rp->hkr_end_va || eva == 0))
				cnt = htable_va2entry(rp->hkr_end_va, ht) -
				    start;

			src = htable_lookup(kas.a_hat, va, rp->hkr_level);
			ASSERT(src != NULL);
			x86pte_copy(src, ht, start, cnt);
			htable_release(src);
		}
	}

init_done:

	XPV_ALLOW_MIGRATE();

	hat_list_append(hat);

	return (hat);
}

/*
 * Cons up a HAT for a CPU. This represents the user mappings. This will have
 * various kernel pages punched into it manually. Importantly, this hat is
 * ineligible for stealing. We really don't want to deal with this ever
 * faulting and figuring out that this is happening, much like we don't with
 * kas.
 */
static hat_t *
hat_cpu_alloc(cpu_t *cpu)
{
	hat_t *hat;
	htable_t *ht;

	hat = kmem_cache_alloc(hat_cache, KM_SLEEP);
	hat->hat_as = NULL;
	mutex_init(&hat->hat_mutex, NULL, MUTEX_DEFAULT, NULL);
	hat->hat_max_level = mmu.max_level;
	hat->hat_num_copied = 0;
	hat->hat_flags = HAT_PCP;

	hat->hat_num_hash = mmu.hash_cnt;
	hat->hat_ht_hash = kmem_cache_alloc(hat_hash_cache, KM_SLEEP);
	bzero(hat->hat_ht_hash, hat->hat_num_hash * sizeof (htable_t *));

	hat->hat_next = hat->hat_prev = NULL;

	/*
	 * Because this HAT will only ever be used by the current CPU, we'll go
	 * ahead and set the CPUSET up to only point to the CPU in question.
	 */
	CPUSET_ADD(hat->hat_cpus, cpu->cpu_id);

	hat->hat_htable = NULL;
	hat->hat_ht_cached = NULL;
	ht = htable_create(hat, (uintptr_t)0, TOP_LEVEL(hat), NULL);
	hat->hat_htable = ht;

	hat_list_append(hat);

	return (hat);
}

/*
 * process has finished executing but as has not been cleaned up yet.
 */
/*ARGSUSED*/
void
hat_free_start(hat_t *hat)
{
	ASSERT(AS_WRITE_HELD(hat->hat_as));

	/*
	 * If the hat is currently a stealing victim, wait for the stealing
	 * to finish.  Once we mark it as HAT_FREEING, htable_steal()
	 * won't look at its pagetables anymore.
	 */
	mutex_enter(&hat_list_lock);
	while (hat->hat_flags & HAT_VICTIM)
		cv_wait(&hat_list_cv, &hat_list_lock);
	hat->hat_flags |= HAT_FREEING;
	mutex_exit(&hat_list_lock);
}

/*
 * An address space is being destroyed, so we destroy the associated hat.
 */
void
hat_free_end(hat_t *hat)
{
	kmem_cache_t *cache;

	ASSERT(hat->hat_flags & HAT_FREEING);

	/*
	 * must not be running on the given hat
	 */
	ASSERT(CPU->cpu_current_hat != hat);

	/*
	 * Remove it from the list of HATs
	 */
	mutex_enter(&hat_list_lock);
	if (hat->hat_prev)
		hat->hat_prev->hat_next = hat->hat_next;
	else
		kas.a_hat->hat_next = hat->hat_next;
	if (hat->hat_next)
		hat->hat_next->hat_prev = hat->hat_prev;
	else
		kas.a_hat->hat_prev = hat->hat_prev;
	mutex_exit(&hat_list_lock);
	hat->hat_next = hat->hat_prev = NULL;

	/*
	 * Make a pass through the htables freeing them all up.
	 */
	htable_purge_hat(hat);

	/*
	 * Decide which kmem cache the hash table came from, then free it.
	 */
	if (hat->hat_flags & HAT_COPIED) {
		if (hat->hat_flags & HAT_COPIED_32) {
			cache = hat32_hash_cache;
		} else {
			cache = hat_hash_cache;
		}
	} else {
		cache = hat_hash_cache;
	}
	kmem_cache_free(cache, hat->hat_ht_hash);
	hat->hat_ht_hash = NULL;

	hat->hat_flags = 0;
	hat->hat_max_level = 0;
	hat->hat_num_copied = 0;
	kmem_cache_free(hat_cache, hat);
}

/*
 * round kernelbase down to a supported value to use for _userlimit
 *
 * userlimit must be aligned down to an entry in the top level htable.
 * The one exception is for 32 bit HAT's running PAE.
 */
uintptr_t
hat_kernelbase(uintptr_t va)
{
	if (IN_VA_HOLE(va))
		panic("_userlimit %p will fall in VA hole\n", (void *)va);
	return (va);
}

/*
 *
 */
static void
set_max_page_level()
{
	level_t lvl;

	if (is_x86_feature(x86_featureset, X86FSET_1GPG)) {
		lvl = 2;
		if (chk_optimal_1gtlb &&
		    cpuid_opteron_erratum(CPU, 6671130)) {
			lvl = 1;
		}
		if (plat_mnode_xcheck(LEVEL_SIZE(2) >>
		    LEVEL_SHIFT(0))) {
			lvl = 1;
		}
	} else {
		lvl = 1;
	}

	mmu.max_page_level = lvl;

	if ((lvl == 2) && (enable_1gpg == 0))
		mmu.umax_page_level = 1;
	else
		mmu.umax_page_level = lvl;
}

/*
 * Determine the number of slots that are in used in the top-most level page
 * table for user memory. This is based on _userlimit. In effect this is similar
 * to htable_va2entry, but without the convenience of having an htable.
 */
void
mmu_calc_user_slots(void)
{
	uint_t ent, nptes;
	uintptr_t shift;

	nptes = mmu.top_level_count;
	shift = _userlimit >> mmu.level_shift[mmu.max_level];
	ent = shift & (nptes - 1);

	/*
	 * Ent tells us the slot that the page for _userlimit would fit in. We
	 * need to add one to this to cover the total number of entries.
	 */
	mmu.top_level_uslots = ent + 1;

	/*
	 * When running 32-bit compatability processes on a 64-bit kernel, we
	 * will only need to use one slot.
	 */
	mmu.top_level_uslots32 = 1;

	/*
	 * Record the number of PCP page table entries that we'll need to copy
	 * around. For 64-bit processes this is the number of user slots. For
	 * 32-bit proceses, this is 4 1 GiB pages.
	 */
	mmu.num_copied_ents = mmu.top_level_uslots;
	mmu.num_copied_ents32 = 4;
}

/*
 * Initialize hat data structures based on processor MMU information.
 */
void
mmu_init(void)
{
	uint_t max_htables;
	uint_t pa_bits;
	uint_t va_bits;
	int i;

	/*
	 * If CPU enabled the page table global bit, use it for the kernel
	 * This is bit 7 in CR4 (PGE - Page Global Enable).
	 */
	if (is_x86_feature(x86_featureset, X86FSET_PGE) &&
	    (getcr4() & CR4_PGE) != 0)
		mmu.pt_global = PT_GLOBAL;

	/*
	 * The 64-bit x86 kernel has split user/kernel page tables. As such we
	 * cannot have the global bit set. The simplest way for us to deal with
	 * this is to just say that pt_global is zero, so the global bit isn't
	 * present.
	 */
	if (kpti_enable == 1)
		mmu.pt_global = 0;

	/*
	 * Detect NX and PAE usage.
	 */
	mmu.pae_hat = 1;
	mmu.pt_nx = PT_NX;

	/*
	 * Use CPU info to set various MMU parameters
	 */
	cpuid_get_addrsize(CPU, &pa_bits, &va_bits);

	/*
	 * Check if 5 level paging is on, we dont support that (yet).
	 * AMD64 processors that support 5 level paging report
	 * the number of va bits for 5 level paging even if
	 * not in 5 level paging mode.  So we need
	 * to adjust va_bits to max for 4 level paging if not in 5 level mode.
	 */
	if ((getcr4() & CR4_LA57) != 0)
		panic("5 Level paging enabled but not yet supported");
	else if (va_bits > MMU_MAX4LEVELVABITS)
		va_bits = MMU_MAX4LEVELVABITS;

	if (va_bits < sizeof (void *) * NBBY) {
		mmu.hole_start = (1ul << (va_bits - 1));
		mmu.hole_end = 0ul - mmu.hole_start - 1;
	} else {
		mmu.hole_end = 0;
		mmu.hole_start = mmu.hole_end - 1;
	}
#if defined(OPTERON_ERRATUM_121)
	/*
	 * If erratum 121 has already been detected at this time, hole_start
	 * contains the value to be subtracted from mmu.hole_start.
	 */
	ASSERT(hole_start == 0 || opteron_erratum_121 != 0);
	hole_start = mmu.hole_start - hole_start;
#else
	hole_start = mmu.hole_start;
#endif
	hole_end = mmu.hole_end;

	mmu.highest_pfn = mmu_btop((1ull << pa_bits) - 1);
	if (mmu.pae_hat == 0 && pa_bits > 32)
		mmu.highest_pfn = PFN_4G - 1;

	if (mmu.pae_hat) {
		mmu.pte_size = 8;	/* 8 byte PTEs */
		mmu.pte_size_shift = 3;
	} else {
		mmu.pte_size = 4;	/* 4 byte PTEs */
		mmu.pte_size_shift = 2;
	}

	if (mmu.pae_hat && !is_x86_feature(x86_featureset, X86FSET_PAE))
		panic("Processor does not support PAE");

	if (!is_x86_feature(x86_featureset, X86FSET_CX8))
		panic("Processor does not support cmpxchg8b instruction");

	mmu.num_level = 4;
	mmu.max_level = 3;
	mmu.ptes_per_table = 512;
	mmu.top_level_count = 512;

	/*
	 * 32-bit processes only use 1 GB ptes.
	 */
	mmu.max_level32 = 2;

	mmu.level_shift[0] = 12;
	mmu.level_shift[1] = 21;
	mmu.level_shift[2] = 30;
	mmu.level_shift[3] = 39;

	for (i = 0; i < mmu.num_level; ++i) {
		mmu.level_size[i] = 1UL << mmu.level_shift[i];
		mmu.level_offset[i] = mmu.level_size[i] - 1;
		mmu.level_mask[i] = ~mmu.level_offset[i];
	}

	set_max_page_level();
	mmu_calc_user_slots();

	mmu_page_sizes = mmu.max_page_level + 1;
	mmu_exported_page_sizes = mmu.umax_page_level + 1;

	/* restrict legacy applications from using pagesizes 1g and above */
	mmu_legacy_page_sizes =
	    (mmu_exported_page_sizes > 2) ? 2 : mmu_exported_page_sizes;


	for (i = 0; i <= mmu.max_page_level; ++i) {
		mmu.pte_bits[i] = PT_VALID | pt_kern;
		if (i > 0)
			mmu.pte_bits[i] |= PT_PAGESIZE;
	}

	/*
	 * NOTE Legacy 32 bit PAE mode only has the P_VALID bit at top level.
	 */
	for (i = 1; i < mmu.num_level; ++i)
		mmu.ptp_bits[i] = PT_PTPBITS;

	/*
	 * Compute how many hash table entries to have per process for htables.
	 * We start with 1 page's worth of entries.
	 *
	 * If physical memory is small, reduce the amount need to cover it.
	 */
	max_htables = physmax / mmu.ptes_per_table;
	mmu.hash_cnt = MMU_PAGESIZE / sizeof (htable_t *);
	while (mmu.hash_cnt > 16 && mmu.hash_cnt >= max_htables)
		mmu.hash_cnt >>= 1;
	mmu.hat32_hash_cnt = mmu.hash_cnt;

	/*
	 * If running in 64 bits and physical memory is large,
	 * increase the size of the cache to cover all of memory for
	 * a 64 bit process.
	 */
#define	HASH_MAX_LENGTH 4
	while (mmu.hash_cnt * HASH_MAX_LENGTH < max_htables)
		mmu.hash_cnt <<= 1;
}


/*
 * initialize hat data structures
 */
void
hat_init()
{
	cv_init(&hat_list_cv, NULL, CV_DEFAULT, NULL);

	/*
	 * initialize kmem caches
	 */
	htable_init();
	hment_init();

	hat_cache = kmem_cache_create("hat_t",
	    sizeof (hat_t), 0, hati_constructor, NULL, NULL,
	    NULL, 0, 0);

	hat_hash_cache = kmem_cache_create("HatHash",
	    mmu.hash_cnt * sizeof (htable_t *), 0, NULL, NULL, NULL,
	    NULL, 0, 0);

	/*
	 * 32-bit PCP hats can use a smaller hash table size on large memory
	 * machines
	 */
	if (mmu.hash_cnt == mmu.hat32_hash_cnt) {
		hat32_hash_cache = hat_hash_cache;
	} else {
		hat32_hash_cache = kmem_cache_create("Hat32Hash",
		    mmu.hat32_hash_cnt * sizeof (htable_t *), 0, NULL, NULL,
		    NULL, NULL, 0, 0);
	}

	/*
	 * Set up the kernel's hat
	 */
	AS_LOCK_ENTER(&kas, RW_WRITER);
	kas.a_hat = kmem_cache_alloc(hat_cache, KM_NOSLEEP);
	mutex_init(&kas.a_hat->hat_mutex, NULL, MUTEX_DEFAULT, NULL);
	kas.a_hat->hat_as = &kas;
	kas.a_hat->hat_flags = 0;
	AS_LOCK_EXIT(&kas);

	CPUSET_ZERO(khat_cpuset);
	CPUSET_ADD(khat_cpuset, CPU->cpu_id);

	/*
	 * The kernel HAT doesn't use PCP regardless of architectures.
	 */
	ASSERT3U(mmu.max_level, >, 0);
	kas.a_hat->hat_max_level = mmu.max_level;
	kas.a_hat->hat_num_copied = 0;

	/*
	 * The kernel hat's next pointer serves as the head of the hat list .
	 * The kernel hat's prev pointer tracks the last hat on the list for
	 * htable_steal() to use.
	 */
	kas.a_hat->hat_next = NULL;
	kas.a_hat->hat_prev = NULL;

	/*
	 * Allocate an htable hash bucket for the kernel
	 * XX64 - tune for 64 bit procs
	 */
	kas.a_hat->hat_num_hash = mmu.hash_cnt;
	kas.a_hat->hat_ht_hash = kmem_cache_alloc(hat_hash_cache, KM_NOSLEEP);
	bzero(kas.a_hat->hat_ht_hash, mmu.hash_cnt * sizeof (htable_t *));

	/*
	 * zero out the top level and cached htable pointers
	 */
	kas.a_hat->hat_ht_cached = NULL;
	kas.a_hat->hat_htable = NULL;

	/*
	 * Pre-allocate hrm_hashtab before enabling the collection of
	 * refmod statistics.  Allocating on the fly would mean us
	 * running the risk of suffering recursive mutex enters or
	 * deadlocks.
	 */
	hrm_hashtab = kmem_zalloc(HRM_HASHSIZE * sizeof (struct hrmstat *),
	    KM_SLEEP);
}


extern void kpti_tramp_start();
extern void kpti_tramp_end();

extern void kdi_isr_start();
extern void kdi_isr_end();

extern gate_desc_t kdi_idt[NIDT];

/*
 * Prepare per-CPU pagetables for all processes on the 64 bit kernel.
 *
 * Each CPU has a set of 2 pagetables that are reused for any 32 bit
 * process it runs. They are the top level pagetable, hci_pcp_l3ptes, and
 * the next to top level table for the bottom 512 Gig, hci_pcp_l2ptes.
 */
/*ARGSUSED*/
static void
hat_pcp_setup(struct cpu *cpu)
{
	struct hat_cpu_info *hci = cpu->cpu_hat_info;
	uintptr_t va;
	size_t len;

	/*
	 * allocate the level==2 page table for the bottom most
	 * 512Gig of address space (this is where 32 bit apps live)
	 */
	ASSERT(hci != NULL);
	hci->hci_pcp_l2ptes = kmem_zalloc(MMU_PAGESIZE, KM_SLEEP);

	/*
	 * Allocate a top level pagetable and copy the kernel's
	 * entries into it. Then link in hci_pcp_l2ptes in the 1st entry.
	 */
	hci->hci_pcp_l3ptes = kmem_zalloc(MMU_PAGESIZE, KM_SLEEP);
	hci->hci_pcp_l3pfn =
	    hat_getpfnum(kas.a_hat, (caddr_t)hci->hci_pcp_l3ptes);
	ASSERT3U(hci->hci_pcp_l3pfn, !=, PFN_INVALID);
	bcopy(pcp_page, hci->hci_pcp_l3ptes, MMU_PAGESIZE);

	hci->hci_pcp_l2pfn =
	    hat_getpfnum(kas.a_hat, (caddr_t)hci->hci_pcp_l2ptes);
	ASSERT3U(hci->hci_pcp_l2pfn, !=, PFN_INVALID);

	/*
	 * Now go through and allocate the user version of these structures.
	 * Unlike with the kernel version, we allocate a hat to represent the
	 * top-level page table as that will make it much simpler when we need
	 * to patch through user entries.
	 */
	hci->hci_user_hat = hat_cpu_alloc(cpu);
	hci->hci_user_l3pfn = hci->hci_user_hat->hat_htable->ht_pfn;
	ASSERT3U(hci->hci_user_l3pfn, !=, PFN_INVALID);
	hci->hci_user_l3ptes =
	    (x86pte_t *)hat_kpm_mapin_pfn(hci->hci_user_l3pfn);

	/* Skip the rest of this if KPTI is switched off at boot. */
	if (kpti_enable != 1)
		return;

	/*
	 * OK, now that we have this we need to go through and punch the normal
	 * holes in the CPU's hat for this. At this point we'll punch in the
	 * following:
	 *
	 *   o GDT
	 *   o IDT
	 *   o LDT
	 *   o Trampoline Code
	 *   o machcpu KPTI page
	 *   o kmdb ISR code page (just trampolines)
	 *
	 * If this is cpu0, then we also can initialize the following because
	 * they'll have already been allocated.
	 *
	 *   o TSS for CPU 0
	 *   o Double Fault for CPU 0
	 *
	 * The following items have yet to be allocated and have not been
	 * punched in yet. They will be punched in later:
	 *
	 *   o TSS (mach_cpucontext_alloc_tables())
	 *   o Double Fault Stack (mach_cpucontext_alloc_tables())
	 */
	hati_cpu_punchin(cpu, (uintptr_t)cpu->cpu_gdt, PROT_READ);
	hati_cpu_punchin(cpu, (uintptr_t)cpu->cpu_idt, PROT_READ);

	/*
	 * As the KDI IDT is only active during kmdb sessions (including single
	 * stepping), typically we don't actually need this punched in (we
	 * consider the routines that switch to the user cr3 to be toxic).  But
	 * if we ever accidentally end up on the user cr3 while on this IDT,
	 * we'd prefer not to triple fault.
	 */
	hati_cpu_punchin(cpu, (uintptr_t)&kdi_idt, PROT_READ);

	VERIFY0((uintptr_t)&kpti_tramp_start % MMU_PAGESIZE);
	VERIFY0((uintptr_t)&kpti_tramp_end % MMU_PAGESIZE);
	for (va = (uintptr_t)&kpti_tramp_start;
	    va < (uintptr_t)&kpti_tramp_end; va += MMU_PAGESIZE) {
		hati_cpu_punchin(cpu, va, PROT_READ | PROT_EXEC);
	}

	VERIFY3U(((uintptr_t)cpu->cpu_m.mcpu_ldt) % MMU_PAGESIZE, ==, 0);
	for (va = (uintptr_t)cpu->cpu_m.mcpu_ldt, len = LDT_CPU_SIZE;
	    len >= MMU_PAGESIZE; va += MMU_PAGESIZE, len -= MMU_PAGESIZE) {
		hati_cpu_punchin(cpu, va, PROT_READ);
	}

	/* mcpu_pad2 is the start of the page containing the kpti_frames. */
	hati_cpu_punchin(cpu, (uintptr_t)&cpu->cpu_m.mcpu_pad2[0],
	    PROT_READ | PROT_WRITE);

	if (cpu == &cpus[0]) {
		/*
		 * CPU0 uses a global for its double fault stack to deal with
		 * the chicken and egg problem. We need to punch it into its
		 * user HAT.
		 */
		extern char dblfault_stack0[];

		hati_cpu_punchin(cpu, (uintptr_t)cpu->cpu_m.mcpu_tss,
		    PROT_READ);

		for (va = (uintptr_t)dblfault_stack0,
		    len = DEFAULTSTKSZ; len >= MMU_PAGESIZE;
		    va += MMU_PAGESIZE, len -= MMU_PAGESIZE) {
			hati_cpu_punchin(cpu, va, PROT_READ | PROT_WRITE);
		}
	}

	VERIFY0((uintptr_t)&kdi_isr_start % MMU_PAGESIZE);
	VERIFY0((uintptr_t)&kdi_isr_end % MMU_PAGESIZE);
	for (va = (uintptr_t)&kdi_isr_start;
	    va < (uintptr_t)&kdi_isr_end; va += MMU_PAGESIZE) {
		hati_cpu_punchin(cpu, va, PROT_READ | PROT_EXEC);
	}
}

/*ARGSUSED*/
static void
hat_pcp_teardown(cpu_t *cpu)
{
	struct hat_cpu_info *hci;

	if ((hci = cpu->cpu_hat_info) == NULL)
		return;
	if (hci->hci_pcp_l2ptes != NULL)
		kmem_free(hci->hci_pcp_l2ptes, MMU_PAGESIZE);
	if (hci->hci_pcp_l3ptes != NULL)
		kmem_free(hci->hci_pcp_l3ptes, MMU_PAGESIZE);
	if (hci->hci_user_hat != NULL) {
		hat_free_start(hci->hci_user_hat);
		hat_free_end(hci->hci_user_hat);
	}
}

#define	NEXT_HKR(r, l, s, e) {			\
	kernel_ranges[r].hkr_level = l;		\
	kernel_ranges[r].hkr_start_va = s;	\
	kernel_ranges[r].hkr_end_va = e;	\
	++r;					\
}

/*
 * Finish filling in the kernel hat.
 * Pre fill in all top level kernel page table entries for the kernel's
 * part of the address range.  From this point on we can't use any new
 * kernel large pages if they need PTE's at max_level
 *
 * create the kmap mappings.
 */
void
hat_init_finish(void)
{
	size_t		size;
	uint_t		r = 0;
	uintptr_t	va;
	hat_kernel_range_t *rp;


	/*
	 * We are now effectively running on the kernel hat.
	 * Clearing use_boot_reserve shuts off using the pre-allocated boot
	 * reserve for all HAT allocations.  From here on, the reserves are
	 * only used when avoiding recursion in kmem_alloc().
	 */
	use_boot_reserve = 0;
	htable_adjust_reserve();

	/*
	 * User HATs are initialized with copies of all kernel mappings in
	 * higher level page tables. Ensure that those entries exist.
	 */
	NEXT_HKR(r, 3, kernelbase, 0);

	num_kernel_ranges = r;

	/*
	 * Create all the kernel pagetables that will have entries
	 * shared to user HATs.
	 */
	for (r = 0; r < num_kernel_ranges; ++r) {
		rp = &kernel_ranges[r];
		for (va = rp->hkr_start_va; va != rp->hkr_end_va;
		    va += LEVEL_SIZE(rp->hkr_level)) {
			htable_t *ht;

			if (IN_HYPERVISOR_VA(va))
				continue;

			/* can/must skip if a page mapping already exists */
			if (rp->hkr_level <= mmu.max_page_level &&
			    (ht = htable_getpage(kas.a_hat, va, NULL)) !=
			    NULL) {
				htable_release(ht);
				continue;
			}

			(void) htable_create(kas.a_hat, va, rp->hkr_level - 1,
			    NULL);
		}
	}

	/*
	 * 32 bit PAE metal kernels use only 4 of the 512 entries in the
	 * page holding the top level pagetable. We use the remainder for
	 * the "per CPU" page tables for PCP processes.
	 * Map the top level kernel pagetable into the kernel to make
	 * it easy to use bcopy access these tables.
	 *
	 * PAE is required for the 64-bit kernel which uses this as well to
	 * perform the per-CPU pagetables. See the big theory statement.
	 */
	if (mmu.pae_hat) {
		pcp_page = vmem_alloc(heap_arena, MMU_PAGESIZE, VM_SLEEP);
		hat_devload(kas.a_hat, (caddr_t)pcp_page, MMU_PAGESIZE,
		    kas.a_hat->hat_htable->ht_pfn,
		    PROT_WRITE | PROT_READ | HAT_NOSYNC | HAT_UNORDERED_OK,
		    HAT_LOAD | HAT_LOAD_NOCONSIST);
	}
	hat_pcp_setup(CPU);

	/*
	 * Create kmap (cached mappings of kernel PTEs)
	 * for 32 bit we map from segmap_start .. ekernelheap
	 * for 64 bit we map from segmap_start .. segmap_start + segmapsize;
	 */
	size = segmapsize;
	hat_kmap_init((uintptr_t)segmap_start, size);

	ASSERT3U(kas.a_hat->hat_htable->ht_pfn, !=, PFN_INVALID);
	ASSERT3U(kpti_safe_cr3, ==,
	    MAKECR3(kas.a_hat->hat_htable->ht_pfn, PCID_KERNEL));
}

/*
 * Update the PCP data on the CPU cpu to the one on the hat. If this is a 32-bit
 * process, then we must update the L2 pages and then the L3. If this is a
 * 64-bit process then we must update the L3 entries.
 */
static void
hat_pcp_update(cpu_t *cpu, const hat_t *hat)
{
	ASSERT3U(hat->hat_flags & HAT_COPIED, !=, 0);

	if ((hat->hat_flags & HAT_COPIED_32) != 0) {
		const x86pte_t *l2src;
		x86pte_t *l2dst, *l3ptes, *l3uptes;
		/*
		 * This is a 32-bit process. To set this up, we need to do the
		 * following:
		 *
		 *  - Copy the 4 L2 PTEs into the dedicated L2 table
		 *  - Zero the user L3 PTEs in the user and kernel page table
		 *  - Set the first L3 PTE to point to the CPU L2 table
		 */
		l2src = hat->hat_copied_ptes;
		l2dst = cpu->cpu_hat_info->hci_pcp_l2ptes;
		l3ptes = cpu->cpu_hat_info->hci_pcp_l3ptes;
		l3uptes = cpu->cpu_hat_info->hci_user_l3ptes;

		l2dst[0] = l2src[0];
		l2dst[1] = l2src[1];
		l2dst[2] = l2src[2];
		l2dst[3] = l2src[3];

		/*
		 * Make sure to use the mmu to get the number of slots. The
		 * number of PLP entries that this has will always be less as
		 * it's a 32-bit process.
		 */
		bzero(l3ptes, sizeof (x86pte_t) * mmu.top_level_uslots);
		l3ptes[0] = MAKEPTP(cpu->cpu_hat_info->hci_pcp_l2pfn, 2);
		bzero(l3uptes, sizeof (x86pte_t) * mmu.top_level_uslots);
		l3uptes[0] = MAKEPTP(cpu->cpu_hat_info->hci_pcp_l2pfn, 2);
	} else {
		/*
		 * This is a 64-bit process. To set this up, we need to do the
		 * following:
		 *
		 *  - Zero the 4 L2 PTEs in the CPU structure for safety
		 *  - Copy over the new user L3 PTEs into the kernel page table
		 *  - Copy over the new user L3 PTEs into the user page table
		 */
		ASSERT3S(kpti_enable, ==, 1);
		bzero(cpu->cpu_hat_info->hci_pcp_l2ptes, sizeof (x86pte_t) * 4);
		bcopy(hat->hat_copied_ptes, cpu->cpu_hat_info->hci_pcp_l3ptes,
		    sizeof (x86pte_t) * mmu.top_level_uslots);
		bcopy(hat->hat_copied_ptes, cpu->cpu_hat_info->hci_user_l3ptes,
		    sizeof (x86pte_t) * mmu.top_level_uslots);
	}
}

static void
reset_kpti(struct kpti_frame *fr, uint64_t kcr3, uint64_t ucr3)
{
	ASSERT3U(fr->kf_tr_flag, ==, 0);
#if DEBUG
	if (fr->kf_kernel_cr3 != 0) {
		ASSERT3U(fr->kf_lower_redzone, ==, 0xdeadbeefdeadbeef);
		ASSERT3U(fr->kf_middle_redzone, ==, 0xdeadbeefdeadbeef);
		ASSERT3U(fr->kf_upper_redzone, ==, 0xdeadbeefdeadbeef);
	}
#endif

	bzero(fr, offsetof(struct kpti_frame, kf_kernel_cr3));
	bzero(&fr->kf_unused, sizeof (struct kpti_frame) -
	    offsetof(struct kpti_frame, kf_unused));

	fr->kf_kernel_cr3 = kcr3;
	fr->kf_user_cr3 = ucr3;
	fr->kf_tr_ret_rsp = (uintptr_t)&fr->kf_tr_rsp;

	fr->kf_lower_redzone = 0xdeadbeefdeadbeef;
	fr->kf_middle_redzone = 0xdeadbeefdeadbeef;
	fr->kf_upper_redzone = 0xdeadbeefdeadbeef;
}

/*
 * Switch to a new active hat, maintaining bit masks to track active CPUs.
 *
 * With KPTI, all our HATs except kas should be using PCP.  Thus, to switch
 * HATs, we need to copy over the new user PTEs, then set our trampoline context
 * as appropriate.
 *
 * If lacking PCID, we then load our new cr3, which will flush the TLB: we may
 * have established userspace TLB entries via kernel accesses, and these are no
 * longer valid.  We have to do this eagerly, as we just deleted this CPU from
 * ->hat_cpus, so would no longer see any TLB shootdowns.
 *
 * With PCID enabled, things get a little more complicated.  We would like to
 * keep TLB context around when entering and exiting the kernel, and to do this,
 * we partition the TLB into two different spaces:
 *
 * PCID_KERNEL is defined as zero, and used both by kas and all other address
 * spaces while in the kernel (post-trampoline).
 *
 * PCID_USER is used while in userspace.  Therefore, userspace cannot use any
 * lingering PCID_KERNEL entries to kernel addresses it should not be able to
 * read.
 *
 * The trampoline cr3s are set not to invalidate on a mov to %cr3. This means if
 * we take a journey through the kernel without switching HATs, we have some
 * hope of keeping our TLB state around.
 *
 * On a hat switch, rather than deal with any necessary flushes on the way out
 * of the trampolines, we do them upfront here. If we're switching from kas, we
 * shouldn't need any invalidation.
 *
 * Otherwise, we can have stale userspace entries for both PCID_USER (what
 * happened before we move onto the kcr3) and PCID_KERNEL (any subsequent
 * userspace accesses such as ddi_copyin()).  Since setcr3() won't do these
 * flushes on its own in PCIDE, we'll do a non-flushing load and then
 * invalidate everything.
 */
void
hat_switch(hat_t *hat)
{
	cpu_t *cpu = CPU;
	hat_t *old = cpu->cpu_current_hat;

	/*
	 * set up this information first, so we don't miss any cross calls
	 */
	if (old != NULL) {
		if (old == hat)
			return;
		if (old != kas.a_hat)
			CPUSET_ATOMIC_DEL(old->hat_cpus, cpu->cpu_id);
	}

	/*
	 * Add this CPU to the active set for this HAT.
	 */
	if (hat != kas.a_hat) {
		CPUSET_ATOMIC_ADD(hat->hat_cpus, cpu->cpu_id);
	}
	cpu->cpu_current_hat = hat;

	struct hat_cpu_info *info = cpu->cpu_m.mcpu_hat_info;
	uint64_t pcide = getcr4() & CR4_PCIDE;
	uint64_t kcr3, ucr3;
	pfn_t tl_kpfn;
	ulong_t	flag;

	EQUIV(kpti_enable, !mmu.pt_global);

	if (hat->hat_flags & HAT_COPIED) {
		hat_pcp_update(cpu, hat);
		tl_kpfn = info->hci_pcp_l3pfn;
	} else {
		IMPLY(kpti_enable, hat == kas.a_hat);
		tl_kpfn = hat->hat_htable->ht_pfn;
	}

	if (pcide) {
		ASSERT(kpti_enable);

		kcr3 = MAKECR3(tl_kpfn, PCID_KERNEL) | CR3_NOINVL_BIT;
		ucr3 = MAKECR3(info->hci_user_l3pfn, PCID_USER) |
		    CR3_NOINVL_BIT;

		setcr3(kcr3);
		if (old != kas.a_hat)
			mmu_flush_tlb(FLUSH_TLB_ALL, NULL);
	} else {
		kcr3 = MAKECR3(tl_kpfn, PCID_NONE);
		ucr3 = kpti_enable ?
		    MAKECR3(info->hci_user_l3pfn, PCID_NONE) :
		    0;

		setcr3(kcr3);
	}

	/*
	 * We will already be taking shootdowns for our new HAT, and as KPTI
	 * invpcid emulation needs to use kf_user_cr3, make sure we don't get
	 * any cross calls while we're inconsistent.  Note that it's harmless to
	 * have a *stale* kf_user_cr3 (we just did a FLUSH_TLB_ALL), but a
	 * *zero* kf_user_cr3 is not going to go very well.
	 */
	if (pcide)
		flag = intr_clear();

	reset_kpti(&cpu->cpu_m.mcpu_kpti, kcr3, ucr3);
	reset_kpti(&cpu->cpu_m.mcpu_kpti_flt, kcr3, ucr3);
	reset_kpti(&cpu->cpu_m.mcpu_kpti_dbg, kcr3, ucr3);

	if (pcide)
		intr_restore(flag);

	ASSERT(cpu == CPU);
}

/*
 * Utility to return a valid x86pte_t from protections, pfn, and level number
 */
static x86pte_t
hati_mkpte(pfn_t pfn, uint_t attr, level_t level, uint_t flags)
{
	x86pte_t	pte;
	uint_t		cache_attr = attr & HAT_ORDER_MASK;

	pte = MAKEPTE(pfn, level);

	if (attr & PROT_WRITE)
		PTE_SET(pte, PT_WRITABLE);

	if (attr & PROT_USER)
		PTE_SET(pte, PT_USER);

	if (!(attr & PROT_EXEC))
		PTE_SET(pte, mmu.pt_nx);

	/*
	 * Set the software bits used track ref/mod sync's and hments.
	 * If not using REF/MOD, set them to avoid h/w rewriting PTEs.
	 */
	if (flags & HAT_LOAD_NOCONSIST)
		PTE_SET(pte, PT_NOCONSIST | PT_REF | PT_MOD);
	else if (attr & HAT_NOSYNC)
		PTE_SET(pte, PT_NOSYNC | PT_REF | PT_MOD);

	/*
	 * Set the caching attributes in the PTE. The combination
	 * of attributes are poorly defined, so we pay attention
	 * to them in the given order.
	 *
	 * The test for HAT_STRICTORDER is different because it's defined
	 * as "0" - which was a stupid thing to do, but is too late to change!
	 */
	if (cache_attr == HAT_STRICTORDER) {
		PTE_SET(pte, PT_NOCACHE);
	/*LINTED [Lint hates empty ifs, but it's the obvious way to do this] */
	} else if (cache_attr & (HAT_UNORDERED_OK | HAT_STORECACHING_OK)) {
		/* nothing to set */;
	} else if (cache_attr & (HAT_MERGING_OK | HAT_LOADCACHING_OK)) {
		PTE_SET(pte, PT_NOCACHE);
		if (is_x86_feature(x86_featureset, X86FSET_PAT))
			PTE_SET(pte, (level == 0) ? PT_PAT_4K : PT_PAT_LARGE);
		else
			PTE_SET(pte, PT_WRITETHRU);
	} else {
		panic("hati_mkpte(): bad caching attributes: %x\n", cache_attr);
	}

	return (pte);
}

/*
 * Duplicate address translations of the parent to the child.
 * This function really isn't used anymore.
 */
/*ARGSUSED*/
int
hat_dup(hat_t *old, hat_t *new, caddr_t addr, size_t len, uint_t flag)
{
	ASSERT((uintptr_t)addr < kernelbase);
	ASSERT(new != kas.a_hat);
	ASSERT(old != kas.a_hat);
	return (0);
}

/*
 * Allocate any hat resources required for a process being swapped in.
 */
/*ARGSUSED*/
void
hat_swapin(hat_t *hat)
{
	/* do nothing - we let everything fault back in */
}

/*
 * Unload all translations associated with an address space of a process
 * that is being swapped out.
 */
void
hat_swapout(hat_t *hat)
{
	uintptr_t	vaddr = (uintptr_t)0;
	uintptr_t	eaddr = _userlimit;
	htable_t	*ht = NULL;
	level_t		l;

	XPV_DISALLOW_MIGRATE();
	/*
	 * We can't just call hat_unload(hat, 0, _userlimit...)  here, because
	 * seg_spt and shared pagetables can't be swapped out.
	 * Take a look at segspt_shmswapout() - it's a big no-op.
	 *
	 * Instead we'll walk through all the address space and unload
	 * any mappings which we are sure are not shared, not locked.
	 */
	ASSERT(IS_PAGEALIGNED(vaddr));
	ASSERT(IS_PAGEALIGNED(eaddr));
	ASSERT(AS_LOCK_HELD(hat->hat_as));
	if ((uintptr_t)hat->hat_as->a_userlimit < eaddr)
		eaddr = (uintptr_t)hat->hat_as->a_userlimit;

	while (vaddr < eaddr) {
		(void) htable_walk(hat, &ht, &vaddr, eaddr);
		if (ht == NULL)
			break;

		ASSERT(!IN_VA_HOLE(vaddr));

		/*
		 * If the page table is shared skip its entire range.
		 */
		l = ht->ht_level;
		if (ht->ht_flags & HTABLE_SHARED_PFN) {
			vaddr = ht->ht_vaddr + LEVEL_SIZE(l + 1);
			htable_release(ht);
			ht = NULL;
			continue;
		}

		/*
		 * If the page table has no locked entries, unload this one.
		 */
		if (ht->ht_lock_cnt == 0)
			hat_unload(hat, (caddr_t)vaddr, LEVEL_SIZE(l),
			    HAT_UNLOAD_UNMAP);

		/*
		 * If we have a level 0 page table with locked entries,
		 * skip the entire page table, otherwise skip just one entry.
		 */
		if (ht->ht_lock_cnt > 0 && l == 0)
			vaddr = ht->ht_vaddr + LEVEL_SIZE(1);
		else
			vaddr += LEVEL_SIZE(l);
	}
	if (ht)
		htable_release(ht);

	/*
	 * We're in swapout because the system is low on memory, so
	 * go back and flush all the htables off the cached list.
	 */
	htable_purge_hat(hat);
	XPV_ALLOW_MIGRATE();
}

/*
 * returns number of bytes that have valid mappings in hat.
 */
size_t
hat_get_mapped_size(hat_t *hat)
{
	size_t total = 0;
	int l;

	for (l = 0; l <= mmu.max_page_level; l++)
		total += (hat->hat_pages_mapped[l] << LEVEL_SHIFT(l));
	total += hat->hat_ism_pgcnt;

	return (total);
}

/*
 * enable/disable collection of stats for hat.
 */
int
hat_stats_enable(hat_t *hat)
{
	atomic_inc_32(&hat->hat_stats);
	return (1);
}

void
hat_stats_disable(hat_t *hat)
{
	atomic_dec_32(&hat->hat_stats);
}

/*
 * Utility to sync the ref/mod bits from a page table entry to the page_t
 * We must be holding the mapping list lock when this is called.
 */
static void
hati_sync_pte_to_page(page_t *pp, x86pte_t pte, level_t level)
{
	uint_t	rm = 0;
	pgcnt_t	pgcnt;

	if (PTE_GET(pte, PT_SOFTWARE) >= PT_NOSYNC)
		return;

	if (PTE_GET(pte, PT_REF))
		rm |= P_REF;

	if (PTE_GET(pte, PT_MOD))
		rm |= P_MOD;

	if (rm == 0)
		return;

	/*
	 * sync to all constituent pages of a large page
	 */
	ASSERT(x86_hm_held(pp));
	pgcnt = page_get_pagecnt(level);
	ASSERT(IS_P2ALIGNED(pp->p_pagenum, pgcnt));
	for (; pgcnt > 0; --pgcnt) {
		/*
		 * hat_page_demote() can't decrease
		 * pszc below this mapping size
		 * since this large mapping existed after we
		 * took mlist lock.
		 */
		ASSERT(pp->p_szc >= level);
		hat_page_setattr(pp, rm);
		++pp;
	}
}

/*
 * This the set of PTE bits for PFN, permissions and caching
 * that are allowed to change on a HAT_LOAD_REMAP
 */
#define	PT_REMAP_BITS							\
	(PT_PADDR | PT_NX | PT_WRITABLE | PT_WRITETHRU |		\
	PT_NOCACHE | PT_PAT_4K | PT_PAT_LARGE | PT_IGNORE | PT_REF | PT_MOD)

#define	REMAPASSERT(EX)	if (!(EX)) panic("hati_pte_map: " #EX)
/*
 * Do the low-level work to get a mapping entered into a HAT's pagetables
 * and in the mapping list of the associated page_t.
 */
static int
hati_pte_map(
	htable_t	*ht,
	uint_t		entry,
	page_t		*pp,
	x86pte_t	pte,
	int		flags,
	void		*pte_ptr)
{
	hat_t		*hat = ht->ht_hat;
	x86pte_t	old_pte;
	level_t		l = ht->ht_level;
	hment_t		*hm;
	uint_t		is_consist;
	uint_t		is_locked;
	int		rv = 0;

	/*
	 * Is this a consistent (ie. need mapping list lock) mapping?
	 */
	is_consist = (pp != NULL && (flags & HAT_LOAD_NOCONSIST) == 0);

	/*
	 * Track locked mapping count in the htable.  Do this first,
	 * as we track locking even if there already is a mapping present.
	 */
	is_locked = (flags & HAT_LOAD_LOCK) != 0 && hat != kas.a_hat;
	if (is_locked)
		HTABLE_LOCK_INC(ht);

	/*
	 * Acquire the page's mapping list lock and get an hment to use.
	 * Note that hment_prepare() might return NULL.
	 */
	if (is_consist) {
		x86_hm_enter(pp);
		hm = hment_prepare(ht, entry, pp);
	}

	/*
	 * Set the new pte, retrieving the old one at the same time.
	 */
	old_pte = x86pte_set(ht, entry, pte, pte_ptr);

	/*
	 * Did we get a large page / page table collision?
	 */
	if (old_pte == LPAGE_ERROR) {
		if (is_locked)
			HTABLE_LOCK_DEC(ht);
		rv = -1;
		goto done;
	}

	/*
	 * If the mapping didn't change there is nothing more to do.
	 */
	if (PTE_EQUIV(pte, old_pte))
		goto done;

	/*
	 * Install a new mapping in the page's mapping list
	 */
	if (!PTE_ISVALID(old_pte)) {
		if (is_consist) {
			hment_assign(ht, entry, pp, hm);
			x86_hm_exit(pp);
		} else {
			ASSERT(flags & HAT_LOAD_NOCONSIST);
		}
		if (ht->ht_flags & HTABLE_COPIED) {
			cpu_t *cpu = CPU;
			hat_pcp_update(cpu, hat);
		}
		HTABLE_INC(ht->ht_valid_cnt);
		PGCNT_INC(hat, l);
		return (rv);
	}

	/*
	 * Remap's are more complicated:
	 *  - HAT_LOAD_REMAP must be specified if changing the pfn.
	 *    We also require that NOCONSIST be specified.
	 *  - Otherwise only permission or caching bits may change.
	 */
	if (!PTE_ISPAGE(old_pte, l))
		panic("non-null/page mapping pte=" FMT_PTE, old_pte);

	if (PTE2PFN(old_pte, l) != PTE2PFN(pte, l)) {
		REMAPASSERT(flags & HAT_LOAD_REMAP);
		REMAPASSERT(flags & HAT_LOAD_NOCONSIST);
		REMAPASSERT(PTE_GET(old_pte, PT_SOFTWARE) >= PT_NOCONSIST);
		REMAPASSERT(pf_is_memory(PTE2PFN(old_pte, l)) ==
		    pf_is_memory(PTE2PFN(pte, l)));
		REMAPASSERT(!is_consist);
	}

	/*
	 * We only let remaps change the certain bits in the PTE.
	 */
	if (PTE_GET(old_pte, ~PT_REMAP_BITS) != PTE_GET(pte, ~PT_REMAP_BITS))
		panic("remap bits changed: old_pte="FMT_PTE", pte="FMT_PTE"\n",
		    old_pte, pte);

	/*
	 * We don't create any mapping list entries on a remap, so release
	 * any allocated hment after we drop the mapping list lock.
	 */
done:
	if (is_consist) {
		x86_hm_exit(pp);
		if (hm != NULL)
			hment_free(hm);
	}
	return (rv);
}

/*
 * Internal routine to load a single page table entry. This only fails if
 * we attempt to overwrite a page table link with a large page.
 */
static int
hati_load_common(
	hat_t		*hat,
	uintptr_t	va,
	page_t		*pp,
	uint_t		attr,
	uint_t		flags,
	level_t		level,
	pfn_t		pfn)
{
	htable_t	*ht;
	uint_t		entry;
	x86pte_t	pte;
	int		rv = 0;

	/*
	 * The number 16 is arbitrary and here to catch a recursion problem
	 * early before we blow out the kernel stack.
	 */
	++curthread->t_hatdepth;
	ASSERT(curthread->t_hatdepth < 16);

	ASSERT(hat == kas.a_hat || (hat->hat_flags & HAT_PCP) != 0 ||
	    AS_LOCK_HELD(hat->hat_as));

	if (flags & HAT_LOAD_SHARE)
		hat->hat_flags |= HAT_SHARED;

	/*
	 * Find the page table that maps this page if it already exists.
	 */
	ht = htable_lookup(hat, va, level);

	/*
	 * We must have HAT_LOAD_NOCONSIST if page_t is NULL.
	 */
	if (pp == NULL)
		flags |= HAT_LOAD_NOCONSIST;

	if (ht == NULL) {
		ht = htable_create(hat, va, level, NULL);
		ASSERT(ht != NULL);
	}
	/*
	 * htable_va2entry checks this condition as well, but it won't include
	 * much useful info in the panic. So we do it in advance here to include
	 * all the context.
	 */
	if (ht->ht_vaddr > va || va > HTABLE_LAST_PAGE(ht)) {
		panic("hati_load_common: bad htable: va=%p, last page=%p, "
		    "ht->ht_vaddr=%p, ht->ht_level=%d", (void *)va,
		    (void *)HTABLE_LAST_PAGE(ht), (void *)ht->ht_vaddr,
		    (int)ht->ht_level);
	}
	entry = htable_va2entry(va, ht);

	/*
	 * a bunch of paranoid error checking
	 */
	ASSERT(ht->ht_busy > 0);
	ASSERT(ht->ht_level == level);

	/*
	 * construct the new PTE
	 */
	if (hat == kas.a_hat)
		attr &= ~PROT_USER;
	pte = hati_mkpte(pfn, attr, level, flags);
	if (hat == kas.a_hat && va >= kernelbase)
		PTE_SET(pte, mmu.pt_global);

	/*
	 * establish the mapping
	 */
	rv = hati_pte_map(ht, entry, pp, pte, flags, NULL);

	/*
	 * release the htable and any reserves
	 */
	htable_release(ht);
	--curthread->t_hatdepth;
	return (rv);
}

/*
 * special case of hat_memload to deal with some kernel addrs for performance
 */
static void
hat_kmap_load(
	caddr_t		addr,
	page_t		*pp,
	uint_t		attr,
	uint_t		flags)
{
	uintptr_t	va = (uintptr_t)addr;
	x86pte_t	pte;
	pfn_t		pfn = page_pptonum(pp);
	pgcnt_t		pg_off = mmu_btop(va - mmu.kmap_addr);
	htable_t	*ht;
	uint_t		entry;
	void		*pte_ptr;

	/*
	 * construct the requested PTE
	 */
	attr &= ~PROT_USER;
	attr |= HAT_STORECACHING_OK;
	pte = hati_mkpte(pfn, attr, 0, flags);
	PTE_SET(pte, mmu.pt_global);

	/*
	 * Figure out the pte_ptr and htable and use common code to finish up
	 */
	if (mmu.pae_hat)
		pte_ptr = mmu.kmap_ptes + pg_off;
	else
		pte_ptr = (x86pte32_t *)mmu.kmap_ptes + pg_off;
	ht = mmu.kmap_htables[(va - mmu.kmap_htables[0]->ht_vaddr) >>
	    LEVEL_SHIFT(1)];
	entry = htable_va2entry(va, ht);
	++curthread->t_hatdepth;
	ASSERT(curthread->t_hatdepth < 16);
	(void) hati_pte_map(ht, entry, pp, pte, flags, pte_ptr);
	--curthread->t_hatdepth;
}

/*
 * hat_memload() - load a translation to the given page struct
 *
 * Flags for hat_memload/hat_devload/hat_*attr.
 *
 *	HAT_LOAD	Default flags to load a translation to the page.
 *
 *	HAT_LOAD_LOCK	Lock down mapping resources; hat_map(), hat_memload(),
 *			and hat_devload().
 *
 *	HAT_LOAD_NOCONSIST Do not add mapping to page_t mapping list.
 *			sets PT_NOCONSIST
 *
 *	HAT_LOAD_SHARE	A flag to hat_memload() to indicate h/w page tables
 *			that map some user pages (not kas) is shared by more
 *			than one process (eg. ISM).
 *
 *	HAT_LOAD_REMAP	Reload a valid pte with a different page frame.
 *
 *	HAT_NO_KALLOC	Do not kmem_alloc while creating the mapping; at this
 *			point, it's setting up mapping to allocate internal
 *			hat layer data structures.  This flag forces hat layer
 *			to tap its reserves in order to prevent infinite
 *			recursion.
 *
 * The following is a protection attribute (like PROT_READ, etc.)
 *
 *	HAT_NOSYNC	set PT_NOSYNC - this mapping's ref/mod bits
 *			are never cleared.
 *
 * Installing new valid PTE's and creation of the mapping list
 * entry are controlled under the same lock. It's derived from the
 * page_t being mapped.
 */
static uint_t supported_memload_flags =
	HAT_LOAD | HAT_LOAD_LOCK | HAT_LOAD_ADV | HAT_LOAD_NOCONSIST |
	HAT_LOAD_SHARE | HAT_NO_KALLOC | HAT_LOAD_REMAP | HAT_LOAD_TEXT;

void
hat_memload(
	hat_t		*hat,
	caddr_t		addr,
	page_t		*pp,
	uint_t		attr,
	uint_t		flags)
{
	uintptr_t	va = (uintptr_t)addr;
	level_t		level = 0;
	pfn_t		pfn = page_pptonum(pp);

	XPV_DISALLOW_MIGRATE();
	ASSERT(IS_PAGEALIGNED(va));
	ASSERT(hat == kas.a_hat || va < _userlimit);
	ASSERT(hat == kas.a_hat || AS_LOCK_HELD(hat->hat_as));
	ASSERT((flags & supported_memload_flags) == flags);

	ASSERT(!IN_VA_HOLE(va));
	ASSERT(!PP_ISFREE(pp));

	/*
	 * kernel address special case for performance.
	 */
	if (mmu.kmap_addr <= va && va < mmu.kmap_eaddr) {
		ASSERT(hat == kas.a_hat);
		hat_kmap_load(addr, pp, attr, flags);
		XPV_ALLOW_MIGRATE();
		return;
	}

	/*
	 * This is used for memory with normal caching enabled, so
	 * always set HAT_STORECACHING_OK.
	 */
	attr |= HAT_STORECACHING_OK;
	if (hati_load_common(hat, va, pp, attr, flags, level, pfn) != 0)
		panic("unexpected hati_load_common() failure");
	XPV_ALLOW_MIGRATE();
}

/* ARGSUSED */
void
hat_memload_region(struct hat *hat, caddr_t addr, struct page *pp,
    uint_t attr, uint_t flags, hat_region_cookie_t rcookie)
{
	hat_memload(hat, addr, pp, attr, flags);
}

/*
 * Load the given array of page structs using large pages when possible
 */
void
hat_memload_array(
	hat_t		*hat,
	caddr_t		addr,
	size_t		len,
	page_t		**pages,
	uint_t		attr,
	uint_t		flags)
{
	uintptr_t	va = (uintptr_t)addr;
	uintptr_t	eaddr = va + len;
	level_t		level;
	size_t		pgsize;
	pgcnt_t		pgindx = 0;
	pfn_t		pfn;
	pgcnt_t		i;

	XPV_DISALLOW_MIGRATE();
	ASSERT(IS_PAGEALIGNED(va));
	ASSERT(hat == kas.a_hat || va + len <= _userlimit);
	ASSERT(hat == kas.a_hat || AS_LOCK_HELD(hat->hat_as));
	ASSERT((flags & supported_memload_flags) == flags);

	/*
	 * memload is used for memory with full caching enabled, so
	 * set HAT_STORECACHING_OK.
	 */
	attr |= HAT_STORECACHING_OK;

	/*
	 * handle all pages using largest possible pagesize
	 */
	while (va < eaddr) {
		/*
		 * decide what level mapping to use (ie. pagesize)
		 */
		pfn = page_pptonum(pages[pgindx]);
		for (level = mmu.max_page_level; ; --level) {
			pgsize = LEVEL_SIZE(level);
			if (level == 0)
				break;

			if (!IS_P2ALIGNED(va, pgsize) ||
			    (eaddr - va) < pgsize ||
			    !IS_P2ALIGNED(pfn_to_pa(pfn), pgsize))
				continue;

			/*
			 * To use a large mapping of this size, all the
			 * pages we are passed must be sequential subpages
			 * of the large page.
			 * hat_page_demote() can't change p_szc because
			 * all pages are locked.
			 */
			if (pages[pgindx]->p_szc >= level) {
				for (i = 0; i < mmu_btop(pgsize); ++i) {
					if (pfn + i !=
					    page_pptonum(pages[pgindx + i]))
						break;
					ASSERT(pages[pgindx + i]->p_szc >=
					    level);
					ASSERT(pages[pgindx] + i ==
					    pages[pgindx + i]);
				}
				if (i == mmu_btop(pgsize)) {
#ifdef DEBUG
					if (level == 2)
						map1gcnt++;
#endif
					break;
				}
			}
		}

		/*
		 * Load this page mapping. If the load fails, try a smaller
		 * pagesize.
		 */
		ASSERT(!IN_VA_HOLE(va));
		while (hati_load_common(hat, va, pages[pgindx], attr,
		    flags, level, pfn) != 0) {
			if (level == 0)
				panic("unexpected hati_load_common() failure");
			--level;
			pgsize = LEVEL_SIZE(level);
		}

		/*
		 * move to next page
		 */
		va += pgsize;
		pgindx += mmu_btop(pgsize);
	}
	XPV_ALLOW_MIGRATE();
}

/* ARGSUSED */
void
hat_memload_array_region(struct hat *hat, caddr_t addr, size_t len,
    struct page **pps, uint_t attr, uint_t flags,
    hat_region_cookie_t rcookie)
{
	hat_memload_array(hat, addr, len, pps, attr, flags);
}

/*
 * void hat_devload(hat, addr, len, pf, attr, flags)
 *	load/lock the given page frame number
 *
 * Advisory ordering attributes. Apply only to device mappings.
 *
 * HAT_STRICTORDER: the CPU must issue the references in order, as the
 *	programmer specified.  This is the default.
 * HAT_UNORDERED_OK: the CPU may reorder the references (this is all kinds
 *	of reordering; store or load with store or load).
 * HAT_MERGING_OK: merging and batching: the CPU may merge individual stores
 *	to consecutive locations (for example, turn two consecutive byte
 *	stores into one halfword store), and it may batch individual loads
 *	(for example, turn two consecutive byte loads into one halfword load).
 *	This also implies re-ordering.
 * HAT_LOADCACHING_OK: the CPU may cache the data it fetches and reuse it
 *	until another store occurs.  The default is to fetch new data
 *	on every load.  This also implies merging.
 * HAT_STORECACHING_OK: the CPU may keep the data in the cache and push it to
 *	the device (perhaps with other data) at a later time.  The default is
 *	to push the data right away.  This also implies load caching.
 *
 * Equivalent of hat_memload(), but can be used for device memory where
 * there are no page_t's and we support additional flags (write merging, etc).
 * Note that we can have large page mappings with this interface.
 */
int supported_devload_flags = HAT_LOAD | HAT_LOAD_LOCK |
	HAT_LOAD_NOCONSIST | HAT_STRICTORDER | HAT_UNORDERED_OK |
	HAT_MERGING_OK | HAT_LOADCACHING_OK | HAT_STORECACHING_OK;

void
hat_devload(
	hat_t		*hat,
	caddr_t		addr,
	size_t		len,
	pfn_t		pfn,
	uint_t		attr,
	int		flags)
{
	uintptr_t	va = ALIGN2PAGE(addr);
	uintptr_t	eva = va + len;
	level_t		level;
	size_t		pgsize;
	page_t		*pp;
	int		f;	/* per PTE copy of flags  - maybe modified */
	uint_t		a;	/* per PTE copy of attr */

	XPV_DISALLOW_MIGRATE();
	ASSERT(IS_PAGEALIGNED(va));
	ASSERT(hat == kas.a_hat || eva <= _userlimit);
	ASSERT(hat == kas.a_hat || AS_LOCK_HELD(hat->hat_as));
	ASSERT((flags & supported_devload_flags) == flags);

	/*
	 * handle all pages
	 */
	while (va < eva) {

		/*
		 * decide what level mapping to use (ie. pagesize)
		 */
		for (level = mmu.max_page_level; ; --level) {
			pgsize = LEVEL_SIZE(level);
			if (level == 0)
				break;
			if (IS_P2ALIGNED(va, pgsize) &&
			    (eva - va) >= pgsize &&
			    IS_P2ALIGNED(pfn, mmu_btop(pgsize))) {
#ifdef DEBUG
				if (level == 2)
					map1gcnt++;
#endif
				break;
			}
		}

		/*
		 * If this is just memory then allow caching (this happens
		 * for the nucleus pages) - though HAT_PLAT_NOCACHE can be used
		 * to override that. If we don't have a page_t then make sure
		 * NOCONSIST is set.
		 */
		a = attr;
		f = flags;
		if (!pf_is_memory(pfn))
			f |= HAT_LOAD_NOCONSIST;
		else if (!(a & HAT_PLAT_NOCACHE))
			a |= HAT_STORECACHING_OK;

		if (f & HAT_LOAD_NOCONSIST)
			pp = NULL;
		else
			pp = page_numtopp_nolock(pfn);

		/*
		 * Check to make sure we are really trying to map a valid
		 * memory page. The caller wishing to intentionally map
		 * free memory pages will have passed the HAT_LOAD_NOCONSIST
		 * flag, then pp will be NULL.
		 */
		if (pp != NULL) {
			if (PP_ISFREE(pp)) {
				panic("hat_devload: loading "
				    "a mapping to free page %p", (void *)pp);
			}

			if (!PAGE_LOCKED(pp) && !PP_ISNORELOC(pp)) {
				panic("hat_devload: loading a mapping "
				    "to an unlocked page %p",
				    (void *)pp);
			}
		}

		/*
		 * load this page mapping
		 */
		ASSERT(!IN_VA_HOLE(va));
		while (hati_load_common(hat, va, pp, a, f, level, pfn) != 0) {
			if (level == 0)
				panic("unexpected hati_load_common() failure");
			--level;
			pgsize = LEVEL_SIZE(level);
		}

		/*
		 * move to next page
		 */
		va += pgsize;
		pfn += mmu_btop(pgsize);
	}
	XPV_ALLOW_MIGRATE();
}

/*
 * void hat_unlock(hat, addr, len)
 *	unlock the mappings to a given range of addresses
 *
 * Locks are tracked by ht_lock_cnt in the htable.
 */
void
hat_unlock(hat_t *hat, caddr_t addr, size_t len)
{
	uintptr_t	vaddr = (uintptr_t)addr;
	uintptr_t	eaddr = vaddr + len;
	htable_t	*ht = NULL;

	/*
	 * kernel entries are always locked, we don't track lock counts
	 */
	ASSERT(hat == kas.a_hat || eaddr <= _userlimit);
	ASSERT(IS_PAGEALIGNED(vaddr));
	ASSERT(IS_PAGEALIGNED(eaddr));
	if (hat == kas.a_hat)
		return;
	if (eaddr > _userlimit)
		panic("hat_unlock() address out of range - above _userlimit");

	XPV_DISALLOW_MIGRATE();
	ASSERT(AS_LOCK_HELD(hat->hat_as));
	while (vaddr < eaddr) {
		(void) htable_walk(hat, &ht, &vaddr, eaddr);
		if (ht == NULL)
			break;

		ASSERT(!IN_VA_HOLE(vaddr));

		if (ht->ht_lock_cnt < 1)
			panic("hat_unlock(): lock_cnt < 1, "
			    "htable=%p, vaddr=%p\n", (void *)ht, (void *)vaddr);
		HTABLE_LOCK_DEC(ht);

		vaddr += LEVEL_SIZE(ht->ht_level);
	}
	if (ht)
		htable_release(ht);
	XPV_ALLOW_MIGRATE();
}

/* ARGSUSED */
void
hat_unlock_region(struct hat *hat, caddr_t addr, size_t len,
    hat_region_cookie_t rcookie)
{
	panic("No shared region support on x86");
}

/*
 * Cross call service routine to demap a range of virtual
 * pages on the current CPU or flush all mappings in TLB.
 */
static int
hati_demap_func(xc_arg_t a1, xc_arg_t a2, xc_arg_t a3)
{
	_NOTE(ARGUNUSED(a3));
	hat_t		*hat = (hat_t *)a1;
	tlb_range_t	*range = (tlb_range_t *)a2;

	/*
	 * If the target hat isn't the kernel and this CPU isn't operating
	 * in the target hat, we can ignore the cross call.
	 */
	if (hat != kas.a_hat && hat != CPU->cpu_current_hat)
		return (0);

	if (range->tr_va != DEMAP_ALL_ADDR) {
		mmu_flush_tlb(FLUSH_TLB_RANGE, range);
		return (0);
	}

	/*
	 * We are flushing all of userspace.
	 *
	 * When using PCP, we first need to update this CPU's idea of the PCP
	 * PTEs.
	 */
	if (hat->hat_flags & HAT_COPIED) {
		hat_pcp_update(CPU, hat);
	}

	mmu_flush_tlb(FLUSH_TLB_NONGLOBAL, NULL);
	return (0);
}

#define	TLBIDLE_CPU_HALTED	(0x1UL)
#define	TLBIDLE_INVAL_ALL	(0x2UL)
#define	CAS_TLB_INFO(cpu, old, new)	\
	atomic_cas_ulong((ulong_t *)&(cpu)->cpu_m.mcpu_tlb_info, (old), (new))

/*
 * Record that a CPU is going idle
 */
void
tlb_going_idle(void)
{
	atomic_or_ulong((ulong_t *)&CPU->cpu_m.mcpu_tlb_info,
	    TLBIDLE_CPU_HALTED);
}

/*
 * Service a delayed TLB flush if coming out of being idle.
 * It will be called from cpu idle notification with interrupt disabled.
 */
void
tlb_service(void)
{
	ulong_t tlb_info;
	ulong_t found;

	/*
	 * We only have to do something if coming out of being idle.
	 */
	tlb_info = CPU->cpu_m.mcpu_tlb_info;
	if (tlb_info & TLBIDLE_CPU_HALTED) {
		ASSERT(CPU->cpu_current_hat == kas.a_hat);

		/*
		 * Atomic clear and fetch of old state.
		 */
		while ((found = CAS_TLB_INFO(CPU, tlb_info, 0)) != tlb_info) {
			ASSERT(found & TLBIDLE_CPU_HALTED);
			tlb_info = found;
			SMT_PAUSE();
		}
		if (tlb_info & TLBIDLE_INVAL_ALL)
			mmu_flush_tlb(FLUSH_TLB_ALL, NULL);
	}
}

/*
 * Internal routine to do cross calls to invalidate a range of pages on
 * all CPUs using a given hat.
 */
void
hat_tlb_inval_range(hat_t *hat, tlb_range_t *in_range)
{
	extern int	flushes_require_xcalls;	/* from mp_startup.c */
	cpuset_t	justme;
	cpuset_t	cpus_to_shootdown;
	tlb_range_t	range = *in_range;
	cpuset_t	check_cpus;
	cpu_t		*cpup;
	int		c;

	/*
	 * If the hat is being destroyed, there are no more users, so
	 * demap need not do anything.
	 */
	if (hat->hat_flags & HAT_FREEING)
		return;

	/*
	 * If demapping from a shared pagetable, we best demap the
	 * entire set of user TLBs, since we don't know what addresses
	 * these were shared at.
	 */
	if (hat->hat_flags & HAT_SHARED) {
		hat = kas.a_hat;
		range.tr_va = DEMAP_ALL_ADDR;
	}

	/*
	 * if not running with multiple CPUs, don't use cross calls
	 */
	if (panicstr || !flushes_require_xcalls) {
		(void) hati_demap_func((xc_arg_t)hat, (xc_arg_t)&range, 0);
		return;
	}


	/*
	 * Determine CPUs to shootdown. Kernel changes always do all CPUs.
	 * Otherwise it's just CPUs currently executing in this hat.
	 */
	kpreempt_disable();
	CPUSET_ONLY(justme, CPU->cpu_id);
	if (hat == kas.a_hat)
		cpus_to_shootdown = khat_cpuset;
	else
		cpus_to_shootdown = hat->hat_cpus;

	/*
	 * If any CPUs in the set are idle, just request a delayed flush
	 * and avoid waking them up.
	 */
	check_cpus = cpus_to_shootdown;
	for (c = 0; c < NCPU && !CPUSET_ISNULL(check_cpus); ++c) {
		ulong_t tlb_info;

		if (!CPU_IN_SET(check_cpus, c))
			continue;
		CPUSET_DEL(check_cpus, c);
		cpup = cpu[c];
		if (cpup == NULL)
			continue;

		tlb_info = cpup->cpu_m.mcpu_tlb_info;
		while (tlb_info == TLBIDLE_CPU_HALTED) {
			(void) CAS_TLB_INFO(cpup, TLBIDLE_CPU_HALTED,
			    TLBIDLE_CPU_HALTED | TLBIDLE_INVAL_ALL);
			SMT_PAUSE();
			tlb_info = cpup->cpu_m.mcpu_tlb_info;
		}
		if (tlb_info == (TLBIDLE_CPU_HALTED | TLBIDLE_INVAL_ALL)) {
			HATSTAT_INC(hs_tlb_inval_delayed);
			CPUSET_DEL(cpus_to_shootdown, c);
		}
	}

	if (CPUSET_ISNULL(cpus_to_shootdown) ||
	    CPUSET_ISEQUAL(cpus_to_shootdown, justme)) {
		(void) hati_demap_func((xc_arg_t)hat, (xc_arg_t)&range, 0);
	} else {
		CPUSET_ADD(cpus_to_shootdown, CPU->cpu_id);
		xc_call((xc_arg_t)hat, (xc_arg_t)&range, 0,
		    CPUSET2BV(cpus_to_shootdown), hati_demap_func);
	}
	kpreempt_enable();
}

void
hat_tlb_inval(hat_t *hat, uintptr_t va)
{
	/*
	 * Create range for a single page.
	 */
	tlb_range_t range;
	range.tr_va = va;
	range.tr_cnt = 1; /* one page */
	range.tr_level = MIN_PAGE_LEVEL; /* pages are MMU_PAGESIZE */

	hat_tlb_inval_range(hat, &range);
}

/*
 * Interior routine for HAT_UNLOADs from hat_unload_callback(),
 * hat_kmap_unload() OR from hat_steal() code.  This routine doesn't
 * handle releasing of the htables.
 */
void
hat_pte_unmap(
	htable_t	*ht,
	uint_t		entry,
	uint_t		flags,
	x86pte_t	old_pte,
	void		*pte_ptr,
	boolean_t	tlb)
{
	hat_t		*hat = ht->ht_hat;
	hment_t		*hm = NULL;
	page_t		*pp = NULL;
	level_t		l = ht->ht_level;
	pfn_t		pfn;

	/*
	 * We always track the locking counts, even if nothing is unmapped
	 */
	if ((flags & HAT_UNLOAD_UNLOCK) != 0 && hat != kas.a_hat) {
		ASSERT(ht->ht_lock_cnt > 0);
		HTABLE_LOCK_DEC(ht);
	}

	/*
	 * Figure out which page's mapping list lock to acquire using the PFN
	 * passed in "old" PTE. We then attempt to invalidate the PTE.
	 * If another thread, probably a hat_pageunload, has asynchronously
	 * unmapped/remapped this address we'll loop here.
	 */
	ASSERT(ht->ht_busy > 0);
	while (PTE_ISVALID(old_pte)) {
		pfn = PTE2PFN(old_pte, l);
		if (PTE_GET(old_pte, PT_SOFTWARE) >= PT_NOCONSIST) {
			pp = NULL;
		} else {
			pp = page_numtopp_nolock(pfn);
			if (pp == NULL) {
				panic("no page_t, not NOCONSIST: old_pte="
				    FMT_PTE " ht=%lx entry=0x%x pte_ptr=%lx",
				    old_pte, (uintptr_t)ht, entry,
				    (uintptr_t)pte_ptr);
			}
			x86_hm_enter(pp);
		}

		old_pte = x86pte_inval(ht, entry, old_pte, pte_ptr, tlb);

		/*
		 * If the page hadn't changed we've unmapped it and can proceed
		 */
		if (PTE_ISVALID(old_pte) && PTE2PFN(old_pte, l) == pfn)
			break;

		/*
		 * Otherwise, we'll have to retry with the current old_pte.
		 * Drop the hment lock, since the pfn may have changed.
		 */
		if (pp != NULL) {
			x86_hm_exit(pp);
			pp = NULL;
		} else {
			ASSERT(PTE_GET(old_pte, PT_SOFTWARE) >= PT_NOCONSIST);
		}
	}

	/*
	 * If the old mapping wasn't valid, there's nothing more to do
	 */
	if (!PTE_ISVALID(old_pte)) {
		if (pp != NULL)
			x86_hm_exit(pp);
		return;
	}

	/*
	 * Take care of syncing any MOD/REF bits and removing the hment.
	 */
	if (pp != NULL) {
		if (!(flags & HAT_UNLOAD_NOSYNC))
			hati_sync_pte_to_page(pp, old_pte, l);
		hm = hment_remove(pp, ht, entry);
		x86_hm_exit(pp);
		if (hm != NULL)
			hment_free(hm);
	}

	/*
	 * Handle book keeping in the htable and hat
	 */
	ASSERT(ht->ht_valid_cnt > 0);
	HTABLE_DEC(ht->ht_valid_cnt);
	PGCNT_DEC(hat, l);
}

/*
 * very cheap unload implementation to special case some kernel addresses
 */
static void
hat_kmap_unload(caddr_t addr, size_t len, uint_t flags)
{
	uintptr_t	va = (uintptr_t)addr;
	uintptr_t	eva = va + len;
	pgcnt_t		pg_index;
	htable_t	*ht;
	uint_t		entry;
	x86pte_t	*pte_ptr;
	x86pte_t	old_pte;

	for (; va < eva; va += MMU_PAGESIZE) {
		/*
		 * Get the PTE
		 */
		pg_index = mmu_btop(va - mmu.kmap_addr);
		pte_ptr = PT_INDEX_PTR(mmu.kmap_ptes, pg_index);
		old_pte = GET_PTE(pte_ptr);

		/*
		 * get the htable / entry
		 */
		ht = mmu.kmap_htables[(va - mmu.kmap_htables[0]->ht_vaddr)
		    >> LEVEL_SHIFT(1)];
		entry = htable_va2entry(va, ht);

		/*
		 * use mostly common code to unmap it.
		 */
		hat_pte_unmap(ht, entry, flags, old_pte, pte_ptr, B_TRUE);
	}
}


/*
 * unload a range of virtual address space (no callback)
 */
void
hat_unload(hat_t *hat, caddr_t addr, size_t len, uint_t flags)
{
	uintptr_t va = (uintptr_t)addr;

	XPV_DISALLOW_MIGRATE();
	ASSERT(hat == kas.a_hat || va + len <= _userlimit);

	/*
	 * special case for performance.
	 */
	if (mmu.kmap_addr <= va && va < mmu.kmap_eaddr) {
		ASSERT(hat == kas.a_hat);
		hat_kmap_unload(addr, len, flags);
	} else {
		hat_unload_callback(hat, addr, len, flags, NULL);
	}
	XPV_ALLOW_MIGRATE();
}

/*
 * Invalidate the TLB, and perform the callback to the upper level VM system,
 * for the specified ranges of contiguous pages.
 */
static void
handle_ranges(hat_t *hat, hat_callback_t *cb, uint_t cnt, tlb_range_t *range)
{
	while (cnt > 0) {
		--cnt;
		hat_tlb_inval_range(hat, &range[cnt]);

		if (cb != NULL) {
			cb->hcb_start_addr = (caddr_t)range[cnt].tr_va;
			cb->hcb_end_addr = cb->hcb_start_addr;
			cb->hcb_end_addr += range[cnt].tr_cnt <<
			    LEVEL_SHIFT(range[cnt].tr_level);
			cb->hcb_function(cb);
		}
	}
}

/*
 * Unload a given range of addresses (has optional callback)
 *
 * Flags:
 * define	HAT_UNLOAD		0x00
 * define	HAT_UNLOAD_NOSYNC	0x02
 * define	HAT_UNLOAD_UNLOCK	0x04
 * define	HAT_UNLOAD_OTHER	0x08 - not used
 * define	HAT_UNLOAD_UNMAP	0x10 - same as HAT_UNLOAD
 */
#define	MAX_UNLOAD_CNT (8)
void
hat_unload_callback(
	hat_t		*hat,
	caddr_t		addr,
	size_t		len,
	uint_t		flags,
	hat_callback_t	*cb)
{
	uintptr_t	vaddr = (uintptr_t)addr;
	uintptr_t	eaddr = vaddr + len;
	htable_t	*ht = NULL;
	uint_t		entry;
	uintptr_t	contig_va = (uintptr_t)-1L;
	tlb_range_t	r[MAX_UNLOAD_CNT];
	uint_t		r_cnt = 0;
	x86pte_t	old_pte;

	XPV_DISALLOW_MIGRATE();
	ASSERT(hat == kas.a_hat || eaddr <= _userlimit);
	ASSERT(IS_PAGEALIGNED(vaddr));
	ASSERT(IS_PAGEALIGNED(eaddr));

	/*
	 * Special case a single page being unloaded for speed. This happens
	 * quite frequently, COW faults after a fork() for example.
	 */
	if (cb == NULL && len == MMU_PAGESIZE) {
		ht = htable_getpte(hat, vaddr, &entry, &old_pte, 0);
		if (ht != NULL) {
			if (PTE_ISVALID(old_pte)) {
				hat_pte_unmap(ht, entry, flags, old_pte,
				    NULL, B_TRUE);
			}
			htable_release(ht);
		}
		XPV_ALLOW_MIGRATE();
		return;
	}

	while (vaddr < eaddr) {
		old_pte = htable_walk(hat, &ht, &vaddr, eaddr);
		if (ht == NULL)
			break;

		ASSERT(!IN_VA_HOLE(vaddr));

		if (vaddr < (uintptr_t)addr)
			panic("hat_unload_callback(): unmap inside large page");

		/*
		 * We'll do the call backs for contiguous ranges
		 */
		if (vaddr != contig_va ||
		    (r_cnt > 0 && r[r_cnt - 1].tr_level != ht->ht_level)) {
			if (r_cnt == MAX_UNLOAD_CNT) {
				handle_ranges(hat, cb, r_cnt, r);
				r_cnt = 0;
			}
			r[r_cnt].tr_va = vaddr;
			r[r_cnt].tr_cnt = 0;
			r[r_cnt].tr_level = ht->ht_level;
			++r_cnt;
		}

		/*
		 * Unload one mapping (for a single page) from the page tables.
		 * Note that we do not remove the mapping from the TLB yet,
		 * as indicated by the tlb=FALSE argument to hat_pte_unmap().
		 * handle_ranges() will clear the TLB entries with one call to
		 * hat_tlb_inval_range() per contiguous range.  This is
		 * safe because the page can not be reused until the
		 * callback is made (or we return).
		 */
		entry = htable_va2entry(vaddr, ht);
		hat_pte_unmap(ht, entry, flags, old_pte, NULL, B_FALSE);
		ASSERT(ht->ht_level <= mmu.max_page_level);
		vaddr += LEVEL_SIZE(ht->ht_level);
		contig_va = vaddr;
		++r[r_cnt - 1].tr_cnt;
	}
	if (ht)
		htable_release(ht);

	/*
	 * handle last range for callbacks
	 */
	if (r_cnt > 0)
		handle_ranges(hat, cb, r_cnt, r);
	XPV_ALLOW_MIGRATE();
}

/*
 * Invalidate a virtual address translation on a slave CPU during
 * panic() dumps.
 */
void
hat_flush_range(hat_t *hat, caddr_t va, size_t size)
{
	ssize_t sz;
	caddr_t endva = va + size;

	while (va < endva) {
		sz = hat_getpagesize(hat, va);
		if (sz < 0) {
			mmu_flush_tlb(FLUSH_TLB_ALL, NULL);
			break;
		}
		mmu_flush_tlb_kpage((uintptr_t)va);
		va += sz;
	}
}

/*
 * synchronize mapping with software data structures
 *
 * This interface is currently only used by the working set monitor
 * driver.
 */
/*ARGSUSED*/
void
hat_sync(hat_t *hat, caddr_t addr, size_t len, uint_t flags)
{
	uintptr_t	vaddr = (uintptr_t)addr;
	uintptr_t	eaddr = vaddr + len;
	htable_t	*ht = NULL;
	uint_t		entry;
	x86pte_t	pte;
	x86pte_t	save_pte;
	x86pte_t	new;
	page_t		*pp;

	ASSERT(!IN_VA_HOLE(vaddr));
	ASSERT(IS_PAGEALIGNED(vaddr));
	ASSERT(IS_PAGEALIGNED(eaddr));
	ASSERT(hat == kas.a_hat || eaddr <= _userlimit);

	XPV_DISALLOW_MIGRATE();
	for (; vaddr < eaddr; vaddr += LEVEL_SIZE(ht->ht_level)) {
try_again:
		pte = htable_walk(hat, &ht, &vaddr, eaddr);
		if (ht == NULL)
			break;
		entry = htable_va2entry(vaddr, ht);

		if (PTE_GET(pte, PT_SOFTWARE) >= PT_NOSYNC ||
		    PTE_GET(pte, PT_REF | PT_MOD) == 0)
			continue;

		/*
		 * We need to acquire the mapping list lock to protect
		 * against hat_pageunload(), hat_unload(), etc.
		 */
		pp = page_numtopp_nolock(PTE2PFN(pte, ht->ht_level));
		if (pp == NULL)
			break;
		x86_hm_enter(pp);
		save_pte = pte;
		pte = x86pte_get(ht, entry);
		if (pte != save_pte) {
			x86_hm_exit(pp);
			goto try_again;
		}
		if (PTE_GET(pte, PT_SOFTWARE) >= PT_NOSYNC ||
		    PTE_GET(pte, PT_REF | PT_MOD) == 0) {
			x86_hm_exit(pp);
			continue;
		}

		/*
		 * Need to clear ref or mod bits. We may compete with
		 * hardware updating the R/M bits and have to try again.
		 */
		if (flags == HAT_SYNC_ZERORM) {
			new = pte;
			PTE_CLR(new, PT_REF | PT_MOD);
			pte = hati_update_pte(ht, entry, pte, new);
			if (pte != 0) {
				x86_hm_exit(pp);
				goto try_again;
			}
		} else {
			/*
			 * sync the PTE to the page_t
			 */
			hati_sync_pte_to_page(pp, save_pte, ht->ht_level);
		}
		x86_hm_exit(pp);
	}
	if (ht)
		htable_release(ht);
	XPV_ALLOW_MIGRATE();
}

/*
 * void	hat_map(hat, addr, len, flags)
 */
/*ARGSUSED*/
void
hat_map(hat_t *hat, caddr_t addr, size_t len, uint_t flags)
{
	/* does nothing */
}

/*
 * uint_t hat_getattr(hat, addr, *attr)
 *	returns attr for <hat,addr> in *attr.  returns 0 if there was a
 *	mapping and *attr is valid, nonzero if there was no mapping and
 *	*attr is not valid.
 */
uint_t
hat_getattr(hat_t *hat, caddr_t addr, uint_t *attr)
{
	uintptr_t	vaddr = ALIGN2PAGE(addr);
	htable_t	*ht = NULL;
	x86pte_t	pte;

	ASSERT(hat == kas.a_hat || vaddr <= _userlimit);

	if (IN_VA_HOLE(vaddr))
		return ((uint_t)-1);

	ht = htable_getpte(hat, vaddr, NULL, &pte, mmu.max_page_level);
	if (ht == NULL)
		return ((uint_t)-1);

	if (!PTE_ISVALID(pte) || !PTE_ISPAGE(pte, ht->ht_level)) {
		htable_release(ht);
		return ((uint_t)-1);
	}

	*attr = PROT_READ;
	if (PTE_GET(pte, PT_WRITABLE))
		*attr |= PROT_WRITE;
	if (PTE_GET(pte, PT_USER))
		*attr |= PROT_USER;
	if (!PTE_GET(pte, mmu.pt_nx))
		*attr |= PROT_EXEC;
	if (PTE_GET(pte, PT_SOFTWARE) >= PT_NOSYNC)
		*attr |= HAT_NOSYNC;
	htable_release(ht);
	return (0);
}

/*
 * hat_updateattr() applies the given attribute change to an existing mapping
 */
#define	HAT_LOAD_ATTR		1
#define	HAT_SET_ATTR		2
#define	HAT_CLR_ATTR		3

static void
hat_updateattr(hat_t *hat, caddr_t addr, size_t len, uint_t attr, int what)
{
	uintptr_t	vaddr = (uintptr_t)addr;
	uintptr_t	eaddr = (uintptr_t)addr + len;
	htable_t	*ht = NULL;
	uint_t		entry;
	x86pte_t	oldpte, newpte;
	page_t		*pp;

	XPV_DISALLOW_MIGRATE();
	ASSERT(IS_PAGEALIGNED(vaddr));
	ASSERT(IS_PAGEALIGNED(eaddr));
	ASSERT(hat == kas.a_hat || AS_LOCK_HELD(hat->hat_as));
	for (; vaddr < eaddr; vaddr += LEVEL_SIZE(ht->ht_level)) {
try_again:
		oldpte = htable_walk(hat, &ht, &vaddr, eaddr);
		if (ht == NULL)
			break;
		if (PTE_GET(oldpte, PT_SOFTWARE) >= PT_NOCONSIST)
			continue;

		pp = page_numtopp_nolock(PTE2PFN(oldpte, ht->ht_level));
		if (pp == NULL)
			continue;
		x86_hm_enter(pp);

		newpte = oldpte;
		/*
		 * We found a page table entry in the desired range,
		 * figure out the new attributes.
		 */
		if (what == HAT_SET_ATTR || what == HAT_LOAD_ATTR) {
			if ((attr & PROT_WRITE) &&
			    !PTE_GET(oldpte, PT_WRITABLE))
				newpte |= PT_WRITABLE;

			if ((attr & HAT_NOSYNC) &&
			    PTE_GET(oldpte, PT_SOFTWARE) < PT_NOSYNC)
				newpte |= PT_NOSYNC;

			if ((attr & PROT_EXEC) && PTE_GET(oldpte, mmu.pt_nx))
				newpte &= ~mmu.pt_nx;
		}

		if (what == HAT_LOAD_ATTR) {
			if (!(attr & PROT_WRITE) &&
			    PTE_GET(oldpte, PT_WRITABLE))
				newpte &= ~PT_WRITABLE;

			if (!(attr & HAT_NOSYNC) &&
			    PTE_GET(oldpte, PT_SOFTWARE) >= PT_NOSYNC)
				newpte &= ~PT_SOFTWARE;

			if (!(attr & PROT_EXEC) && !PTE_GET(oldpte, mmu.pt_nx))
				newpte |= mmu.pt_nx;
		}

		if (what == HAT_CLR_ATTR) {
			if ((attr & PROT_WRITE) && PTE_GET(oldpte, PT_WRITABLE))
				newpte &= ~PT_WRITABLE;

			if ((attr & HAT_NOSYNC) &&
			    PTE_GET(oldpte, PT_SOFTWARE) >= PT_NOSYNC)
				newpte &= ~PT_SOFTWARE;

			if ((attr & PROT_EXEC) && !PTE_GET(oldpte, mmu.pt_nx))
				newpte |= mmu.pt_nx;
		}

		/*
		 * Ensure NOSYNC/NOCONSIST mappings have REF and MOD set.
		 * x86pte_set() depends on this.
		 */
		if (PTE_GET(newpte, PT_SOFTWARE) >= PT_NOSYNC)
			newpte |= PT_REF | PT_MOD;

		/*
		 * what about PROT_READ or others? this code only handles:
		 * EXEC, WRITE, NOSYNC
		 */

		/*
		 * If new PTE really changed, update the table.
		 */
		if (newpte != oldpte) {
			entry = htable_va2entry(vaddr, ht);
			oldpte = hati_update_pte(ht, entry, oldpte, newpte);
			if (oldpte != 0) {
				x86_hm_exit(pp);
				goto try_again;
			}
		}
		x86_hm_exit(pp);
	}
	if (ht)
		htable_release(ht);
	XPV_ALLOW_MIGRATE();
}

/*
 * Various wrappers for hat_updateattr()
 */
void
hat_setattr(hat_t *hat, caddr_t addr, size_t len, uint_t attr)
{
	ASSERT(hat == kas.a_hat || (uintptr_t)addr + len <= _userlimit);
	hat_updateattr(hat, addr, len, attr, HAT_SET_ATTR);
}

void
hat_clrattr(hat_t *hat, caddr_t addr, size_t len, uint_t attr)
{
	ASSERT(hat == kas.a_hat || (uintptr_t)addr + len <= _userlimit);
	hat_updateattr(hat, addr, len, attr, HAT_CLR_ATTR);
}

void
hat_chgattr(hat_t *hat, caddr_t addr, size_t len, uint_t attr)
{
	ASSERT(hat == kas.a_hat || (uintptr_t)addr + len <= _userlimit);
	hat_updateattr(hat, addr, len, attr, HAT_LOAD_ATTR);
}

void
hat_chgprot(hat_t *hat, caddr_t addr, size_t len, uint_t vprot)
{
	ASSERT(hat == kas.a_hat || (uintptr_t)addr + len <= _userlimit);
	hat_updateattr(hat, addr, len, vprot & HAT_PROT_MASK, HAT_LOAD_ATTR);
}

/*
 * size_t hat_getpagesize(hat, addr)
 *	returns pagesize in bytes for <hat, addr>. returns -1 of there is
 *	no mapping. This is an advisory call.
 */
ssize_t
hat_getpagesize(hat_t *hat, caddr_t addr)
{
	uintptr_t	vaddr = ALIGN2PAGE(addr);
	htable_t	*ht;
	size_t		pagesize;

	ASSERT(hat == kas.a_hat || vaddr <= _userlimit);
	if (IN_VA_HOLE(vaddr))
		return (-1);
	ht = htable_getpage(hat, vaddr, NULL);
	if (ht == NULL)
		return (-1);
	pagesize = LEVEL_SIZE(ht->ht_level);
	htable_release(ht);
	return (pagesize);
}



/*
 * pfn_t hat_getpfnum(hat, addr)
 *	returns pfn for <hat, addr> or PFN_INVALID if mapping is invalid.
 */
pfn_t
hat_getpfnum(hat_t *hat, caddr_t addr)
{
	uintptr_t	vaddr = ALIGN2PAGE(addr);
	htable_t	*ht;
	uint_t		entry;
	pfn_t		pfn = PFN_INVALID;

	ASSERT(hat == kas.a_hat || vaddr <= _userlimit);
	if (khat_running == 0)
		return (PFN_INVALID);

	if (IN_VA_HOLE(vaddr))
		return (PFN_INVALID);

	XPV_DISALLOW_MIGRATE();
	/*
	 * A very common use of hat_getpfnum() is from the DDI for kernel pages.
	 * Use the kmap_ptes (which also covers the 32 bit heap) to speed
	 * this up.
	 */
	if (mmu.kmap_addr <= vaddr && vaddr < mmu.kmap_eaddr) {
		x86pte_t pte;
		pgcnt_t pg_index;

		pg_index = mmu_btop(vaddr - mmu.kmap_addr);
		pte = GET_PTE(PT_INDEX_PTR(mmu.kmap_ptes, pg_index));
		if (PTE_ISVALID(pte))
			/*LINTED [use of constant 0 causes a lint warning] */
			pfn = PTE2PFN(pte, 0);
		XPV_ALLOW_MIGRATE();
		return (pfn);
	}

	ht = htable_getpage(hat, vaddr, &entry);
	if (ht == NULL) {
		XPV_ALLOW_MIGRATE();
		return (PFN_INVALID);
	}
	ASSERT(vaddr >= ht->ht_vaddr);
	ASSERT(vaddr <= HTABLE_LAST_PAGE(ht));
	pfn = PTE2PFN(x86pte_get(ht, entry), ht->ht_level);
	if (ht->ht_level > 0)
		pfn += mmu_btop(vaddr & LEVEL_OFFSET(ht->ht_level));
	htable_release(ht);
	XPV_ALLOW_MIGRATE();
	return (pfn);
}

/*
 * int hat_probe(hat, addr)
 *	return 0 if no valid mapping is present.  Faster version
 *	of hat_getattr in certain architectures.
 */
int
hat_probe(hat_t *hat, caddr_t addr)
{
	uintptr_t	vaddr = ALIGN2PAGE(addr);
	uint_t		entry;
	htable_t	*ht;
	pgcnt_t		pg_off;

	ASSERT(hat == kas.a_hat || vaddr <= _userlimit);
	ASSERT(hat == kas.a_hat || AS_LOCK_HELD(hat->hat_as));
	if (IN_VA_HOLE(vaddr))
		return (0);

	/*
	 * Most common use of hat_probe is from segmap. We special case it
	 * for performance.
	 */
	if (mmu.kmap_addr <= vaddr && vaddr < mmu.kmap_eaddr) {
		pg_off = mmu_btop(vaddr - mmu.kmap_addr);
		if (mmu.pae_hat)
			return (PTE_ISVALID(mmu.kmap_ptes[pg_off]));
		else
			return (PTE_ISVALID(
			    ((x86pte32_t *)mmu.kmap_ptes)[pg_off]));
	}

	ht = htable_getpage(hat, vaddr, &entry);
	htable_release(ht);
	return (ht != NULL);
}

/*
 * Find out if the segment for hat_share()/hat_unshare() is DISM or locked ISM.
 */
static int
is_it_dism(hat_t *hat, caddr_t va)
{
	struct seg *seg;
	struct shm_data *shmd;
	struct spt_data *sptd;

	seg = as_findseg(hat->hat_as, va, 0);
	ASSERT(seg != NULL);
	ASSERT(seg->s_base <= va);
	shmd = (struct shm_data *)seg->s_data;
	ASSERT(shmd != NULL);
	sptd = (struct spt_data *)shmd->shm_sptseg->s_data;
	ASSERT(sptd != NULL);
	if (sptd->spt_flags & SHM_PAGEABLE)
		return (1);
	return (0);
}

/*
 * Simple implementation of ISM. hat_share() is similar to hat_memload_array(),
 * except that we use the ism_hat's existing mappings to determine the pages
 * and protections to use for this hat. If we find a full properly aligned
 * and sized pagetable, we will attempt to share the pagetable itself.
 */
/*ARGSUSED*/
int
hat_share(
	hat_t		*hat,
	caddr_t		addr,
	hat_t		*ism_hat,
	caddr_t		src_addr,
	size_t		len,	/* almost useless value, see below.. */
	uint_t		ismszc)
{
	uintptr_t	vaddr_start = (uintptr_t)addr;
	uintptr_t	vaddr;
	uintptr_t	eaddr = vaddr_start + len;
	uintptr_t	ism_addr_start = (uintptr_t)src_addr;
	uintptr_t	ism_addr = ism_addr_start;
	uintptr_t	e_ism_addr = ism_addr + len;
	htable_t	*ism_ht = NULL;
	htable_t	*ht;
	x86pte_t	pte;
	page_t		*pp;
	pfn_t		pfn;
	level_t		l;
	pgcnt_t		pgcnt;
	uint_t		prot;
	int		is_dism;
	int		flags;

	/*
	 * We might be asked to share an empty DISM hat by as_dup()
	 */
	ASSERT(hat != kas.a_hat);
	ASSERT(eaddr <= _userlimit);
	if (!(ism_hat->hat_flags & HAT_SHARED)) {
		ASSERT(hat_get_mapped_size(ism_hat) == 0);
		return (0);
	}
	XPV_DISALLOW_MIGRATE();

	/*
	 * The SPT segment driver often passes us a size larger than there are
	 * valid mappings. That's because it rounds the segment size up to a
	 * large pagesize, even if the actual memory mapped by ism_hat is less.
	 */
	ASSERT(IS_PAGEALIGNED(vaddr_start));
	ASSERT(IS_PAGEALIGNED(ism_addr_start));
	ASSERT(ism_hat->hat_flags & HAT_SHARED);
	is_dism = is_it_dism(hat, addr);
	while (ism_addr < e_ism_addr) {
		/*
		 * use htable_walk to get the next valid ISM mapping
		 */
		pte = htable_walk(ism_hat, &ism_ht, &ism_addr, e_ism_addr);
		if (ism_ht == NULL)
			break;

		/*
		 * First check to see if we already share the page table.
		 */
		l = ism_ht->ht_level;
		vaddr = vaddr_start + (ism_addr - ism_addr_start);
		ht = htable_lookup(hat, vaddr, l);
		if (ht != NULL) {
			if (ht->ht_flags & HTABLE_SHARED_PFN)
				goto shared;
			htable_release(ht);
			goto not_shared;
		}

		/*
		 * Can't ever share top table.
		 */
		if (l == mmu.max_level)
			goto not_shared;

		/*
		 * Avoid level mismatches later due to DISM faults.
		 */
		if (is_dism && l > 0)
			goto not_shared;

		/*
		 * addresses and lengths must align
		 * table must be fully populated
		 * no lower level page tables
		 */
		if (ism_addr != ism_ht->ht_vaddr ||
		    (vaddr & LEVEL_OFFSET(l + 1)) != 0)
			goto not_shared;

		/*
		 * The range of address space must cover a full table.
		 */
		if (e_ism_addr - ism_addr < LEVEL_SIZE(l + 1))
			goto not_shared;

		/*
		 * All entries in the ISM page table must be leaf PTEs.
		 */
		if (l > 0) {
			int e;

			/*
			 * We know the 0th is from htable_walk() above.
			 */
			for (e = 1; e < HTABLE_NUM_PTES(ism_ht); ++e) {
				x86pte_t pte;
				pte = x86pte_get(ism_ht, e);
				if (!PTE_ISPAGE(pte, l))
					goto not_shared;
			}
		}

		/*
		 * share the page table
		 */
		ht = htable_create(hat, vaddr, l, ism_ht);
shared:
		ASSERT(ht->ht_flags & HTABLE_SHARED_PFN);
		ASSERT(ht->ht_shares == ism_ht);
		hat->hat_ism_pgcnt +=
		    (ism_ht->ht_valid_cnt - ht->ht_valid_cnt) <<
		    (LEVEL_SHIFT(ht->ht_level) - MMU_PAGESHIFT);
		ht->ht_valid_cnt = ism_ht->ht_valid_cnt;
		htable_release(ht);
		ism_addr = ism_ht->ht_vaddr + LEVEL_SIZE(l + 1);
		htable_release(ism_ht);
		ism_ht = NULL;
		continue;

not_shared:
		/*
		 * Unable to share the page table. Instead we will
		 * create new mappings from the values in the ISM mappings.
		 * Figure out what level size mappings to use;
		 */
		for (l = ism_ht->ht_level; l > 0; --l) {
			if (LEVEL_SIZE(l) <= eaddr - vaddr &&
			    (vaddr & LEVEL_OFFSET(l)) == 0)
				break;
		}

		/*
		 * The ISM mapping might be larger than the share area,
		 * be careful to truncate it if needed.
		 */
		if (eaddr - vaddr >= LEVEL_SIZE(ism_ht->ht_level)) {
			pgcnt = mmu_btop(LEVEL_SIZE(ism_ht->ht_level));
		} else {
			pgcnt = mmu_btop(eaddr - vaddr);
			l = 0;
		}

		pfn = PTE2PFN(pte, ism_ht->ht_level);
		ASSERT(pfn != PFN_INVALID);
		while (pgcnt > 0) {
			/*
			 * Make a new pte for the PFN for this level.
			 * Copy protections for the pte from the ISM pte.
			 */
			pp = page_numtopp_nolock(pfn);
			ASSERT(pp != NULL);

			prot = PROT_USER | PROT_READ | HAT_UNORDERED_OK;
			if (PTE_GET(pte, PT_WRITABLE))
				prot |= PROT_WRITE;
			if (!PTE_GET(pte, PT_NX))
				prot |= PROT_EXEC;

			flags = HAT_LOAD;
			if (!is_dism)
				flags |= HAT_LOAD_LOCK | HAT_LOAD_NOCONSIST;
			while (hati_load_common(hat, vaddr, pp, prot, flags,
			    l, pfn) != 0) {
				if (l == 0)
					panic("hati_load_common() failure");
				--l;
			}

			vaddr += LEVEL_SIZE(l);
			ism_addr += LEVEL_SIZE(l);
			pfn += mmu_btop(LEVEL_SIZE(l));
			pgcnt -= mmu_btop(LEVEL_SIZE(l));
		}
	}
	if (ism_ht != NULL)
		htable_release(ism_ht);
	XPV_ALLOW_MIGRATE();
	return (0);
}


/*
 * hat_unshare() is similar to hat_unload_callback(), but
 * we have to look for empty shared pagetables. Note that
 * hat_unshare() is always invoked against an entire segment.
 */
/*ARGSUSED*/
void
hat_unshare(hat_t *hat, caddr_t addr, size_t len, uint_t ismszc)
{
	uint64_t	vaddr = (uintptr_t)addr;
	uintptr_t	eaddr = vaddr + len;
	htable_t	*ht = NULL;
	uint_t		need_demaps = 0;
	int		flags = HAT_UNLOAD_UNMAP;
	level_t		l;

	ASSERT(hat != kas.a_hat);
	ASSERT(eaddr <= _userlimit);
	ASSERT(IS_PAGEALIGNED(vaddr));
	ASSERT(IS_PAGEALIGNED(eaddr));
	XPV_DISALLOW_MIGRATE();

	/*
	 * First go through and remove any shared pagetables.
	 *
	 * Note that it's ok to delay the TLB shootdown till the entire range is
	 * finished, because if hat_pageunload() were to unload a shared
	 * pagetable page, its hat_tlb_inval() will do a global TLB invalidate.
	 */
	l = mmu.max_page_level;
	if (l == mmu.max_level)
		--l;
	for (; l >= 0; --l) {
		for (vaddr = (uintptr_t)addr; vaddr < eaddr;
		    vaddr = (vaddr & LEVEL_MASK(l + 1)) + LEVEL_SIZE(l + 1)) {
			ASSERT(!IN_VA_HOLE(vaddr));
			/*
			 * find a pagetable that maps the current address
			 */
			ht = htable_lookup(hat, vaddr, l);
			if (ht == NULL)
				continue;
			if (ht->ht_flags & HTABLE_SHARED_PFN) {
				/*
				 * clear page count, set valid_cnt to 0,
				 * let htable_release() finish the job
				 */
				hat->hat_ism_pgcnt -= ht->ht_valid_cnt <<
				    (LEVEL_SHIFT(ht->ht_level) - MMU_PAGESHIFT);
				ht->ht_valid_cnt = 0;
				need_demaps = 1;
			}
			htable_release(ht);
		}
	}

	/*
	 * flush the TLBs - since we're probably dealing with MANY mappings
	 * we just do a full invalidation.
	 */
	if (!(hat->hat_flags & HAT_FREEING) && need_demaps)
		hat_tlb_inval(hat, DEMAP_ALL_ADDR);

	/*
	 * Now go back and clean up any unaligned mappings that
	 * couldn't share pagetables.
	 */
	if (!is_it_dism(hat, addr))
		flags |= HAT_UNLOAD_UNLOCK;
	hat_unload(hat, addr, len, flags);
	XPV_ALLOW_MIGRATE();
}


/*
 * hat_reserve() does nothing
 */
/*ARGSUSED*/
void
hat_reserve(struct as *as, caddr_t addr, size_t len)
{
}


/*
 * Called when all mappings to a page should have write permission removed.
 * Mostly stolen from hat_pagesync()
 */
static void
hati_page_clrwrt(struct page *pp)
{
	hment_t		*hm = NULL;
	htable_t	*ht;
	uint_t		entry;
	x86pte_t	old;
	x86pte_t	new;
	uint_t		pszc = 0;

	XPV_DISALLOW_MIGRATE();
next_size:
	/*
	 * walk thru the mapping list clearing write permission
	 */
	x86_hm_enter(pp);
	while ((hm = hment_walk(pp, &ht, &entry, hm)) != NULL) {
		if (ht->ht_level < pszc)
			continue;
		old = x86pte_get(ht, entry);

		for (;;) {
			/*
			 * Is this mapping of interest?
			 */
			if (PTE2PFN(old, ht->ht_level) != pp->p_pagenum ||
			    PTE_GET(old, PT_WRITABLE) == 0)
				break;

			/*
			 * Clear ref/mod writable bits. This requires cross
			 * calls to ensure any executing TLBs see cleared bits.
			 */
			new = old;
			PTE_CLR(new, PT_REF | PT_MOD | PT_WRITABLE);
			old = hati_update_pte(ht, entry, old, new);
			if (old != 0)
				continue;

			break;
		}
	}
	x86_hm_exit(pp);
	while (pszc < pp->p_szc) {
		page_t *tpp;
		pszc++;
		tpp = PP_GROUPLEADER(pp, pszc);
		if (pp != tpp) {
			pp = tpp;
			goto next_size;
		}
	}
	XPV_ALLOW_MIGRATE();
}

/*
 * void hat_page_setattr(pp, flag)
 * void hat_page_clrattr(pp, flag)
 *	used to set/clr ref/mod bits.
 */
void
hat_page_setattr(struct page *pp, uint_t flag)
{
	vnode_t		*vp = pp->p_vnode;
	kmutex_t	*vphm = NULL;
	page_t		**listp;
	int		noshuffle;

	noshuffle = flag & P_NSH;
	flag &= ~P_NSH;

	if (PP_GETRM(pp, flag) == flag)
		return;

	if ((flag & P_MOD) != 0 && vp != NULL && IS_VMODSORT(vp) &&
	    !noshuffle) {
		vphm = page_vnode_mutex(vp);
		mutex_enter(vphm);
	}

	PP_SETRM(pp, flag);

	if (vphm != NULL) {

		/*
		 * Some File Systems examine v_pages for NULL w/o
		 * grabbing the vphm mutex. Must not let it become NULL when
		 * pp is the only page on the list.
		 */
		if (pp->p_vpnext != pp) {
			page_vpsub(&vp->v_pages, pp);
			if (vp->v_pages != NULL)
				listp = &vp->v_pages->p_vpprev->p_vpnext;
			else
				listp = &vp->v_pages;
			page_vpadd(listp, pp);
		}
		mutex_exit(vphm);
	}
}

void
hat_page_clrattr(struct page *pp, uint_t flag)
{
	vnode_t		*vp = pp->p_vnode;
	ASSERT(!(flag & ~(P_MOD | P_REF | P_RO)));

	/*
	 * Caller is expected to hold page's io lock for VMODSORT to work
	 * correctly with pvn_vplist_dirty() and pvn_getdirty() when mod
	 * bit is cleared.
	 * We don't have assert to avoid tripping some existing third party
	 * code. The dirty page is moved back to top of the v_page list
	 * after IO is done in pvn_write_done().
	 */
	PP_CLRRM(pp, flag);

	if ((flag & P_MOD) != 0 && vp != NULL && IS_VMODSORT(vp)) {

		/*
		 * VMODSORT works by removing write permissions and getting
		 * a fault when a page is made dirty. At this point
		 * we need to remove write permission from all mappings
		 * to this page.
		 */
		hati_page_clrwrt(pp);
	}
}

/*
 *	If flag is specified, returns 0 if attribute is disabled
 *	and non zero if enabled.  If flag specifes multiple attributes
 *	then returns 0 if ALL attributes are disabled.  This is an advisory
 *	call.
 */
uint_t
hat_page_getattr(struct page *pp, uint_t flag)
{
	return (PP_GETRM(pp, flag));
}


/*
 * common code used by hat_pageunload() and hment_steal()
 */
hment_t *
hati_page_unmap(page_t *pp, htable_t *ht, uint_t entry)
{
	x86pte_t old_pte;
	pfn_t pfn = pp->p_pagenum;
	hment_t *hm;

	/*
	 * We need to acquire a hold on the htable in order to
	 * do the invalidate. We know the htable must exist, since
	 * unmap's don't release the htable until after removing any
	 * hment. Having x86_hm_enter() keeps that from proceeding.
	 */
	htable_acquire(ht);

	/*
	 * Invalidate the PTE and remove the hment.
	 */
	old_pte = x86pte_inval(ht, entry, 0, NULL, B_TRUE);
	if (PTE2PFN(old_pte, ht->ht_level) != pfn) {
		panic("x86pte_inval() failure found PTE = " FMT_PTE
		    " pfn being unmapped is %lx ht=0x%lx entry=0x%x",
		    old_pte, pfn, (uintptr_t)ht, entry);
	}

	/*
	 * Clean up all the htable information for this mapping
	 */
	ASSERT(ht->ht_valid_cnt > 0);
	HTABLE_DEC(ht->ht_valid_cnt);
	PGCNT_DEC(ht->ht_hat, ht->ht_level);

	/*
	 * sync ref/mod bits to the page_t
	 */
	if (PTE_GET(old_pte, PT_SOFTWARE) < PT_NOSYNC)
		hati_sync_pte_to_page(pp, old_pte, ht->ht_level);

	/*
	 * Remove the mapping list entry for this page.
	 */
	hm = hment_remove(pp, ht, entry);

	/*
	 * drop the mapping list lock so that we might free the
	 * hment and htable.
	 */
	x86_hm_exit(pp);
	htable_release(ht);
	return (hm);
}

extern int	vpm_enable;
/*
 * Unload all translations to a page. If the page is a subpage of a large
 * page, the large page mappings are also removed.
 *
 * The forceflags are unused.
 */

/*ARGSUSED*/
static int
hati_pageunload(struct page *pp, uint_t pg_szcd, uint_t forceflag)
{
	page_t		*cur_pp = pp;
	hment_t		*hm;
	hment_t		*prev;
	htable_t	*ht;
	uint_t		entry;
	level_t		level;

	XPV_DISALLOW_MIGRATE();

	/*
	 * prevent recursion due to kmem_free()
	 */
	++curthread->t_hatdepth;
	ASSERT(curthread->t_hatdepth < 16);

	/*
	 * clear the vpm ref.
	 */
	if (vpm_enable) {
		pp->p_vpmref = 0;
	}
	/*
	 * The loop with next_size handles pages with multiple pagesize mappings
	 */
next_size:
	for (;;) {

		/*
		 * Get a mapping list entry
		 */
		x86_hm_enter(cur_pp);
		for (prev = NULL; ; prev = hm) {
			hm = hment_walk(cur_pp, &ht, &entry, prev);
			if (hm == NULL) {
				x86_hm_exit(cur_pp);

				/*
				 * If not part of a larger page, we're done.
				 */
				if (cur_pp->p_szc <= pg_szcd) {
					ASSERT(curthread->t_hatdepth > 0);
					--curthread->t_hatdepth;
					XPV_ALLOW_MIGRATE();
					return (0);
				}

				/*
				 * Else check the next larger page size.
				 * hat_page_demote() may decrease p_szc
				 * but that's ok we'll just take an extra
				 * trip discover there're no larger mappings
				 * and return.
				 */
				++pg_szcd;
				cur_pp = PP_GROUPLEADER(cur_pp, pg_szcd);
				goto next_size;
			}

			/*
			 * If this mapping size matches, remove it.
			 */
			level = ht->ht_level;
			if (level == pg_szcd)
				break;
		}

		/*
		 * Remove the mapping list entry for this page.
		 * Note this does the x86_hm_exit() for us.
		 */
		hm = hati_page_unmap(cur_pp, ht, entry);
		if (hm != NULL)
			hment_free(hm);
	}
}

int
hat_pageunload(struct page *pp, uint_t forceflag)
{
	ASSERT(PAGE_EXCL(pp));
	return (hati_pageunload(pp, 0, forceflag));
}

/*
 * Unload all large mappings to pp and reduce by 1 p_szc field of every large
 * page level that included pp.
 *
 * pp must be locked EXCL. Even though no other constituent pages are locked
 * it's legal to unload large mappings to pp because all constituent pages of
 * large locked mappings have to be locked SHARED.  therefore if we have EXCL
 * lock on one of constituent pages none of the large mappings to pp are
 * locked.
 *
 * Change (always decrease) p_szc field starting from the last constituent
 * page and ending with root constituent page so that root's pszc always shows
 * the area where hat_page_demote() may be active.
 *
 * This mechanism is only used for file system pages where it's not always
 * possible to get EXCL locks on all constituent pages to demote the size code
 * (as is done for anonymous or kernel large pages).
 */
void
hat_page_demote(page_t *pp)
{
	uint_t		pszc;
	uint_t		rszc;
	uint_t		szc;
	page_t		*rootpp;
	page_t		*firstpp;
	page_t		*lastpp;
	pgcnt_t		pgcnt;

	ASSERT(PAGE_EXCL(pp));
	ASSERT(!PP_ISFREE(pp));
	ASSERT(page_szc_lock_assert(pp));

	if (pp->p_szc == 0)
		return;

	rootpp = PP_GROUPLEADER(pp, 1);
	(void) hati_pageunload(rootpp, 1, HAT_FORCE_PGUNLOAD);

	/*
	 * all large mappings to pp are gone
	 * and no new can be setup since pp is locked exclusively.
	 *
	 * Lock the root to make sure there's only one hat_page_demote()
	 * outstanding within the area of this root's pszc.
	 *
	 * Second potential hat_page_demote() is already eliminated by upper
	 * VM layer via page_szc_lock() but we don't rely on it and use our
	 * own locking (so that upper layer locking can be changed without
	 * assumptions that hat depends on upper layer VM to prevent multiple
	 * hat_page_demote() to be issued simultaneously to the same large
	 * page).
	 */
again:
	pszc = pp->p_szc;
	if (pszc == 0)
		return;
	rootpp = PP_GROUPLEADER(pp, pszc);
	x86_hm_enter(rootpp);
	/*
	 * If root's p_szc is different from pszc we raced with another
	 * hat_page_demote().  Drop the lock and try to find the root again.
	 * If root's p_szc is greater than pszc previous hat_page_demote() is
	 * not done yet.  Take and release mlist lock of root's root to wait
	 * for previous hat_page_demote() to complete.
	 */
	if ((rszc = rootpp->p_szc) != pszc) {
		x86_hm_exit(rootpp);
		if (rszc > pszc) {
			/* p_szc of a locked non free page can't increase */
			ASSERT(pp != rootpp);

			rootpp = PP_GROUPLEADER(rootpp, rszc);
			x86_hm_enter(rootpp);
			x86_hm_exit(rootpp);
		}
		goto again;
	}
	ASSERT(pp->p_szc == pszc);

	/*
	 * Decrement by 1 p_szc of every constituent page of a region that
	 * covered pp. For example if original szc is 3 it gets changed to 2
	 * everywhere except in region 2 that covered pp. Region 2 that
	 * covered pp gets demoted to 1 everywhere except in region 1 that
	 * covered pp. The region 1 that covered pp is demoted to region
	 * 0. It's done this way because from region 3 we removed level 3
	 * mappings, from region 2 that covered pp we removed level 2 mappings
	 * and from region 1 that covered pp we removed level 1 mappings.  All
	 * changes are done from from high pfn's to low pfn's so that roots
	 * are changed last allowing one to know the largest region where
	 * hat_page_demote() is stil active by only looking at the root page.
	 *
	 * This algorithm is implemented in 2 while loops. First loop changes
	 * p_szc of pages to the right of pp's level 1 region and second
	 * loop changes p_szc of pages of level 1 region that covers pp
	 * and all pages to the left of level 1 region that covers pp.
	 * In the first loop p_szc keeps dropping with every iteration
	 * and in the second loop it keeps increasing with every iteration.
	 *
	 * First loop description: Demote pages to the right of pp outside of
	 * level 1 region that covers pp.  In every iteration of the while
	 * loop below find the last page of szc region and the first page of
	 * (szc - 1) region that is immediately to the right of (szc - 1)
	 * region that covers pp.  From last such page to first such page
	 * change every page's szc to szc - 1. Decrement szc and continue
	 * looping until szc is 1. If pp belongs to the last (szc - 1) region
	 * of szc region skip to the next iteration.
	 */
	szc = pszc;
	while (szc > 1) {
		lastpp = PP_GROUPLEADER(pp, szc);
		pgcnt = page_get_pagecnt(szc);
		lastpp += pgcnt - 1;
		firstpp = PP_GROUPLEADER(pp, (szc - 1));
		pgcnt = page_get_pagecnt(szc - 1);
		if (lastpp - firstpp < pgcnt) {
			szc--;
			continue;
		}
		firstpp += pgcnt;
		while (lastpp != firstpp) {
			ASSERT(lastpp->p_szc == pszc);
			lastpp->p_szc = szc - 1;
			lastpp--;
		}
		firstpp->p_szc = szc - 1;
		szc--;
	}

	/*
	 * Second loop description:
	 * First iteration changes p_szc to 0 of every
	 * page of level 1 region that covers pp.
	 * Subsequent iterations find last page of szc region
	 * immediately to the left of szc region that covered pp
	 * and first page of (szc + 1) region that covers pp.
	 * From last to first page change p_szc of every page to szc.
	 * Increment szc and continue looping until szc is pszc.
	 * If pp belongs to the fist szc region of (szc + 1) region
	 * skip to the next iteration.
	 *
	 */
	szc = 0;
	while (szc < pszc) {
		firstpp = PP_GROUPLEADER(pp, (szc + 1));
		if (szc == 0) {
			pgcnt = page_get_pagecnt(1);
			lastpp = firstpp + (pgcnt - 1);
		} else {
			lastpp = PP_GROUPLEADER(pp, szc);
			if (firstpp == lastpp) {
				szc++;
				continue;
			}
			lastpp--;
			pgcnt = page_get_pagecnt(szc);
		}
		while (lastpp != firstpp) {
			ASSERT(lastpp->p_szc == pszc);
			lastpp->p_szc = szc;
			lastpp--;
		}
		firstpp->p_szc = szc;
		if (firstpp == rootpp)
			break;
		szc++;
	}
	x86_hm_exit(rootpp);
}

/*
 * get hw stats from hardware into page struct and reset hw stats
 * returns attributes of page
 * Flags for hat_pagesync, hat_getstat, hat_sync
 *
 * define	HAT_SYNC_ZERORM		0x01
 *
 * Additional flags for hat_pagesync
 *
 * define	HAT_SYNC_STOPON_REF	0x02
 * define	HAT_SYNC_STOPON_MOD	0x04
 * define	HAT_SYNC_STOPON_RM	0x06
 * define	HAT_SYNC_STOPON_SHARED	0x08
 */
uint_t
hat_pagesync(struct page *pp, uint_t flags)
{
	hment_t		*hm = NULL;
	htable_t	*ht;
	uint_t		entry;
	x86pte_t	old, save_old;
	x86pte_t	new;
	uchar_t		nrmbits = P_REF|P_MOD|P_RO;
	extern ulong_t	po_share;
	page_t		*save_pp = pp;
	uint_t		pszc = 0;

	ASSERT(PAGE_LOCKED(pp) || panicstr);

	if (PP_ISRO(pp) && (flags & HAT_SYNC_STOPON_MOD))
		return (pp->p_nrm & nrmbits);

	if ((flags & HAT_SYNC_ZERORM) == 0) {

		if ((flags & HAT_SYNC_STOPON_REF) != 0 && PP_ISREF(pp))
			return (pp->p_nrm & nrmbits);

		if ((flags & HAT_SYNC_STOPON_MOD) != 0 && PP_ISMOD(pp))
			return (pp->p_nrm & nrmbits);

		if ((flags & HAT_SYNC_STOPON_SHARED) != 0 &&
		    hat_page_getshare(pp) > po_share) {
			if (PP_ISRO(pp))
				PP_SETREF(pp);
			return (pp->p_nrm & nrmbits);
		}
	}

	XPV_DISALLOW_MIGRATE();
next_size:
	/*
	 * walk thru the mapping list syncing (and clearing) ref/mod bits.
	 */
	x86_hm_enter(pp);
	while ((hm = hment_walk(pp, &ht, &entry, hm)) != NULL) {
		if (ht->ht_level < pszc)
			continue;
		old = x86pte_get(ht, entry);
try_again:

		ASSERT(PTE2PFN(old, ht->ht_level) == pp->p_pagenum);

		if (PTE_GET(old, PT_REF | PT_MOD) == 0)
			continue;

		save_old = old;
		if ((flags & HAT_SYNC_ZERORM) != 0) {

			/*
			 * Need to clear ref or mod bits. Need to demap
			 * to make sure any executing TLBs see cleared bits.
			 */
			new = old;
			PTE_CLR(new, PT_REF | PT_MOD);
			old = hati_update_pte(ht, entry, old, new);
			if (old != 0)
				goto try_again;

			old = save_old;
		}

		/*
		 * Sync the PTE
		 */
		if (!(flags & HAT_SYNC_ZERORM) &&
		    PTE_GET(old, PT_SOFTWARE) <= PT_NOSYNC)
			hati_sync_pte_to_page(pp, old, ht->ht_level);

		/*
		 * can stop short if we found a ref'd or mod'd page
		 */
		if (((flags & HAT_SYNC_STOPON_MOD) && PP_ISMOD(save_pp)) ||
		    ((flags & HAT_SYNC_STOPON_REF) && PP_ISREF(save_pp))) {
			x86_hm_exit(pp);
			goto done;
		}
	}
	x86_hm_exit(pp);
	while (pszc < pp->p_szc) {
		page_t *tpp;
		pszc++;
		tpp = PP_GROUPLEADER(pp, pszc);
		if (pp != tpp) {
			pp = tpp;
			goto next_size;
		}
	}
done:
	XPV_ALLOW_MIGRATE();
	return (save_pp->p_nrm & nrmbits);
}

/*
 * returns approx number of mappings to this pp.  A return of 0 implies
 * there are no mappings to the page.
 */
ulong_t
hat_page_getshare(page_t *pp)
{
	uint_t cnt;
	cnt = hment_mapcnt(pp);
	if (vpm_enable && pp->p_vpmref) {
		cnt += 1;
	}
	return (cnt);
}

/*
 * Return 1 the number of mappings exceeds sh_thresh. Return 0
 * otherwise.
 */
int
hat_page_checkshare(page_t *pp, ulong_t sh_thresh)
{
	return (hat_page_getshare(pp) > sh_thresh);
}

/*
 * hat_softlock isn't supported anymore
 */
/*ARGSUSED*/
faultcode_t
hat_softlock(
	hat_t *hat,
	caddr_t addr,
	size_t *len,
	struct page **page_array,
	uint_t flags)
{
	return (FC_NOSUPPORT);
}



/*
 * Routine to expose supported HAT features to platform independent code.
 */
/*ARGSUSED*/
int
hat_supported(enum hat_features feature, void *arg)
{
	switch (feature) {

	case HAT_SHARED_PT:	/* this is really ISM */
		return (1);

	case HAT_DYNAMIC_ISM_UNMAP:
		return (0);

	case HAT_VMODSORT:
		return (1);

	case HAT_SHARED_REGIONS:
		return (0);

	default:
		panic("hat_supported() - unknown feature");
	}
	return (0);
}

/*
 * Called when a thread is exiting and has been switched to the kernel AS
 */
void
hat_thread_exit(kthread_t *thd)
{
	ASSERT(thd->t_procp->p_as == &kas);
	XPV_DISALLOW_MIGRATE();
	hat_switch(thd->t_procp->p_as->a_hat);
	XPV_ALLOW_MIGRATE();
}

/*
 * Setup the given brand new hat structure as the new HAT on this cpu's mmu.
 */
/*ARGSUSED*/
void
hat_setup(hat_t *hat, int flags)
{
	XPV_DISALLOW_MIGRATE();
	kpreempt_disable();

	hat_switch(hat);

	kpreempt_enable();
	XPV_ALLOW_MIGRATE();
}

/*
 * Prepare for a CPU private mapping for the given address.
 *
 * The address can only be used from a single CPU and can be remapped
 * using hat_mempte_remap().  Return the address of the PTE.
 *
 * We do the htable_create() if necessary and increment the valid count so
 * the htable can't disappear.  We also hat_devload() the page table into
 * kernel so that the PTE is quickly accessed.
 */
hat_mempte_t
hat_mempte_setup(caddr_t addr)
{
	uintptr_t	va = (uintptr_t)addr;
	htable_t	*ht;
	uint_t		entry;
	x86pte_t	oldpte;
	hat_mempte_t	p;

	ASSERT(IS_PAGEALIGNED(va));
	ASSERT(!IN_VA_HOLE(va));
	++curthread->t_hatdepth;
	XPV_DISALLOW_MIGRATE();
	ht = htable_getpte(kas.a_hat, va, &entry, &oldpte, 0);
	if (ht == NULL) {
		ht = htable_create(kas.a_hat, va, 0, NULL);
		entry = htable_va2entry(va, ht);
		ASSERT(ht->ht_level == 0);
		oldpte = x86pte_get(ht, entry);
	}
	if (PTE_ISVALID(oldpte))
		panic("hat_mempte_setup(): address already mapped"
		    "ht=%p, entry=%d, pte=" FMT_PTE, (void *)ht, entry, oldpte);

	/*
	 * increment ht_valid_cnt so that the pagetable can't disappear
	 */
	HTABLE_INC(ht->ht_valid_cnt);

	/*
	 * return the PTE physical address to the caller.
	 */
	htable_release(ht);
	XPV_ALLOW_MIGRATE();
	p = PT_INDEX_PHYSADDR(pfn_to_pa(ht->ht_pfn), entry);
	--curthread->t_hatdepth;
	return (p);
}

/*
 * Release a CPU private mapping for the given address.
 * We decrement the htable valid count so it might be destroyed.
 */
/*ARGSUSED1*/
void
hat_mempte_release(caddr_t addr, hat_mempte_t pte_pa)
{
	htable_t	*ht;

	XPV_DISALLOW_MIGRATE();
	/*
	 * invalidate any left over mapping and decrement the htable valid count
	 */
	{
		x86pte_t *pteptr;

		pteptr = x86pte_mapin(mmu_btop(pte_pa),
		    (pte_pa & MMU_PAGEOFFSET) >> mmu.pte_size_shift, NULL);
		if (mmu.pae_hat)
			*pteptr = 0;
		else
			*(x86pte32_t *)pteptr = 0;
		mmu_flush_tlb_kpage((uintptr_t)addr);
		x86pte_mapout();
	}

	ht = htable_getpte(kas.a_hat, ALIGN2PAGE(addr), NULL, NULL, 0);
	if (ht == NULL)
		panic("hat_mempte_release(): invalid address");
	ASSERT(ht->ht_level == 0);
	HTABLE_DEC(ht->ht_valid_cnt);
	htable_release(ht);
	XPV_ALLOW_MIGRATE();
}

/*
 * Apply a temporary CPU private mapping to a page. We flush the TLB only
 * on this CPU, so this ought to have been called with preemption disabled.
 */
void
hat_mempte_remap(
	pfn_t		pfn,
	caddr_t		addr,
	hat_mempte_t	pte_pa,
	uint_t		attr,
	uint_t		flags)
{
	uintptr_t	va = (uintptr_t)addr;
	x86pte_t	pte;

	/*
	 * Remap the given PTE to the new page's PFN. Invalidate only
	 * on this CPU.
	 */
#ifdef DEBUG
	htable_t	*ht;
	uint_t		entry;

	ASSERT(IS_PAGEALIGNED(va));
	ASSERT(!IN_VA_HOLE(va));
	ht = htable_getpte(kas.a_hat, va, &entry, NULL, 0);
	ASSERT(ht != NULL);
	ASSERT(ht->ht_level == 0);
	ASSERT(ht->ht_valid_cnt > 0);
	ASSERT(ht->ht_pfn == mmu_btop(pte_pa));
	htable_release(ht);
#endif
	XPV_DISALLOW_MIGRATE();
	pte = hati_mkpte(pfn, attr, 0, flags);
	{
		x86pte_t *pteptr;

		pteptr = x86pte_mapin(mmu_btop(pte_pa),
		    (pte_pa & MMU_PAGEOFFSET) >> mmu.pte_size_shift, NULL);
		if (mmu.pae_hat)
			*(x86pte_t *)pteptr = pte;
		else
			*(x86pte32_t *)pteptr = (x86pte32_t)pte;
		mmu_flush_tlb_kpage((uintptr_t)addr);
		x86pte_mapout();
	}
	XPV_ALLOW_MIGRATE();
}



/*
 * Hat locking functions
 * XXX - these two functions are currently being used by hatstats
 *	they can be removed by using a per-as mutex for hatstats.
 */
void
hat_enter(hat_t *hat)
{
	mutex_enter(&hat->hat_mutex);
}

void
hat_exit(hat_t *hat)
{
	mutex_exit(&hat->hat_mutex);
}

/*
 * HAT part of cpu initialization.
 */
void
hat_cpu_online(struct cpu *cpup)
{
	if (cpup != CPU) {
		x86pte_cpu_init(cpup);
		hat_pcp_setup(cpup);
	}
	CPUSET_ATOMIC_ADD(khat_cpuset, cpup->cpu_id);
}

/*
 * HAT part of cpu deletion.
 * (currently, we only call this after the cpu is safely passivated.)
 */
void
hat_cpu_offline(struct cpu *cpup)
{
	ASSERT(cpup != CPU);

	CPUSET_ATOMIC_DEL(khat_cpuset, cpup->cpu_id);
	hat_pcp_teardown(cpup);
	x86pte_cpu_fini(cpup);
}

/*
 * Function called after all CPUs are brought online.
 * Used to remove low address boot mappings.
 */
void
clear_boot_mappings(uintptr_t low, uintptr_t high)
{
	uintptr_t vaddr = low;
	htable_t *ht = NULL;
	level_t level;
	uint_t entry;
	x86pte_t pte;

	/*
	 * On 1st CPU we can unload the prom mappings, basically we blow away
	 * all virtual mappings under _userlimit.
	 */
	while (vaddr < high) {
		pte = htable_walk(kas.a_hat, &ht, &vaddr, high);
		if (ht == NULL)
			break;

		level = ht->ht_level;
		entry = htable_va2entry(vaddr, ht);
		ASSERT(level <= mmu.max_page_level);
		ASSERT(PTE_ISPAGE(pte, level));

		/*
		 * Unload the mapping from the page tables.
		 */
		(void) x86pte_inval(ht, entry, 0, NULL, B_TRUE);
		ASSERT(ht->ht_valid_cnt > 0);
		HTABLE_DEC(ht->ht_valid_cnt);
		PGCNT_DEC(ht->ht_hat, ht->ht_level);

		vaddr += LEVEL_SIZE(ht->ht_level);
	}
	if (ht)
		htable_release(ht);
}

/*
 * Atomically update a new translation for a single page.  If the
 * currently installed PTE doesn't match the value we expect to find,
 * it's not updated and we return the PTE we found.
 *
 * If activating nosync or NOWRITE and the page was modified we need to sync
 * with the page_t. Also sync with page_t if clearing ref/mod bits.
 */
static x86pte_t
hati_update_pte(htable_t *ht, uint_t entry, x86pte_t expected, x86pte_t new)
{
	page_t		*pp;
	uint_t		rm = 0;
	x86pte_t	replaced;

	if (PTE_GET(expected, PT_SOFTWARE) < PT_NOSYNC &&
	    PTE_GET(expected, PT_MOD | PT_REF) &&
	    (PTE_GET(new, PT_NOSYNC) || !PTE_GET(new, PT_WRITABLE) ||
	    !PTE_GET(new, PT_MOD | PT_REF))) {

		ASSERT(!pfn_is_foreign(PTE2PFN(expected, ht->ht_level)));
		pp = page_numtopp_nolock(PTE2PFN(expected, ht->ht_level));
		ASSERT(pp != NULL);
		if (PTE_GET(expected, PT_MOD))
			rm |= P_MOD;
		if (PTE_GET(expected, PT_REF))
			rm |= P_REF;
		PTE_CLR(new, PT_MOD | PT_REF);
	}

	replaced = x86pte_update(ht, entry, expected, new);
	if (replaced != expected)
		return (replaced);

	if (rm) {
		/*
		 * sync to all constituent pages of a large page
		 */
		pgcnt_t pgcnt = page_get_pagecnt(ht->ht_level);
		ASSERT(IS_P2ALIGNED(pp->p_pagenum, pgcnt));
		while (pgcnt-- > 0) {
			/*
			 * hat_page_demote() can't decrease
			 * pszc below this mapping size
			 * since large mapping existed after we
			 * took mlist lock.
			 */
			ASSERT(pp->p_szc >= ht->ht_level);
			hat_page_setattr(pp, rm);
			++pp;
		}
	}

	return (0);
}

/* ARGSUSED */
void
hat_join_srd(struct hat *hat, vnode_t *evp)
{
}

/* ARGSUSED */
hat_region_cookie_t
hat_join_region(struct hat *hat,
    caddr_t r_saddr,
    size_t r_size,
    void *r_obj,
    u_offset_t r_objoff,
    uchar_t r_perm,
    uchar_t r_pgszc,
    hat_rgn_cb_func_t r_cb_function,
    uint_t flags)
{
	panic("No shared region support on x86");
	return (HAT_INVALID_REGION_COOKIE);
}

/* ARGSUSED */
void
hat_leave_region(struct hat *hat, hat_region_cookie_t rcookie, uint_t flags)
{
	panic("No shared region support on x86");
}

/* ARGSUSED */
void
hat_dup_region(struct hat *hat, hat_region_cookie_t rcookie)
{
	panic("No shared region support on x86");
}


/*
 * Kernel Physical Mapping (kpm) facility
 *
 * Most of the routines needed to support segkpm are almost no-ops on the
 * x86 platform.  We map in the entire segment when it is created and leave
 * it mapped in, so there is no additional work required to set up and tear
 * down individual mappings.  All of these routines were created to support
 * SPARC platforms that have to avoid aliasing in their virtually indexed
 * caches.
 *
 * Most of the routines have sanity checks in them (e.g. verifying that the
 * passed-in page is locked).  We don't actually care about most of these
 * checks on x86, but we leave them in place to identify problems in the
 * upper levels.
 */

/*
 * Map in a locked page and return the vaddr.
 */
/*ARGSUSED*/
caddr_t
hat_kpm_mapin(struct page *pp, struct kpme *kpme)
{
	caddr_t		vaddr;

#ifdef DEBUG
	if (kpm_enable == 0) {
		cmn_err(CE_WARN, "hat_kpm_mapin: kpm_enable not set\n");
		return ((caddr_t)NULL);
	}

	if (pp == NULL || PAGE_LOCKED(pp) == 0) {
		cmn_err(CE_WARN, "hat_kpm_mapin: pp zero or not locked\n");
		return ((caddr_t)NULL);
	}
#endif

	vaddr = hat_kpm_page2va(pp, 1);

	return (vaddr);
}

/*
 * Mapout a locked page.
 */
/*ARGSUSED*/
void
hat_kpm_mapout(struct page *pp, struct kpme *kpme, caddr_t vaddr)
{
#ifdef DEBUG
	if (kpm_enable == 0) {
		cmn_err(CE_WARN, "hat_kpm_mapout: kpm_enable not set\n");
		return;
	}

	if (IS_KPM_ADDR(vaddr) == 0) {
		cmn_err(CE_WARN, "hat_kpm_mapout: no kpm address\n");
		return;
	}

	if (pp == NULL || PAGE_LOCKED(pp) == 0) {
		cmn_err(CE_WARN, "hat_kpm_mapout: page zero or not locked\n");
		return;
	}
#endif
}

/*
 * hat_kpm_mapin_pfn is used to obtain a kpm mapping for physical
 * memory addresses that are not described by a page_t.  It can
 * also be used for normal pages that are not locked, but beware
 * this is dangerous - no locking is performed, so the identity of
 * the page could change.  hat_kpm_mapin_pfn is not supported when
 * vac_colors > 1, because the chosen va depends on the page identity,
 * which could change.
 * The caller must only pass pfn's for valid physical addresses; violation
 * of this rule will cause panic.
 */
caddr_t
hat_kpm_mapin_pfn(pfn_t pfn)
{
	caddr_t paddr, vaddr;

	if (kpm_enable == 0)
		return ((caddr_t)NULL);

	paddr = (caddr_t)ptob(pfn);
	vaddr = (uintptr_t)kpm_vbase + paddr;

	return ((caddr_t)vaddr);
}

/*ARGSUSED*/
void
hat_kpm_mapout_pfn(pfn_t pfn)
{
	/* empty */
}

/*
 * Return the kpm virtual address for a specific pfn
 */
caddr_t
hat_kpm_pfn2va(pfn_t pfn)
{
	uintptr_t vaddr = (uintptr_t)kpm_vbase + mmu_ptob(pfn);

	ASSERT(!pfn_is_foreign(pfn));
	return ((caddr_t)vaddr);
}

/*
 * Return the kpm virtual address for the page at pp.
 */
/*ARGSUSED*/
caddr_t
hat_kpm_page2va(struct page *pp, int checkswap)
{
	return (hat_kpm_pfn2va(pp->p_pagenum));
}

/*
 * Return the page frame number for the kpm virtual address vaddr.
 */
pfn_t
hat_kpm_va2pfn(caddr_t vaddr)
{
	pfn_t		pfn;

	ASSERT(IS_KPM_ADDR(vaddr));

	pfn = (pfn_t)btop(vaddr - kpm_vbase);

	return (pfn);
}


/*
 * Return the page for the kpm virtual address vaddr.
 */
page_t *
hat_kpm_vaddr2page(caddr_t vaddr)
{
	pfn_t		pfn;

	ASSERT(IS_KPM_ADDR(vaddr));

	pfn = hat_kpm_va2pfn(vaddr);

	return (page_numtopp_nolock(pfn));
}

/*
 * hat_kpm_fault is called from segkpm_fault when we take a page fault on a
 * KPM page.  This should never happen on x86
 */
int
hat_kpm_fault(hat_t *hat, caddr_t vaddr)
{
	panic("pagefault in seg_kpm.  hat: 0x%p  vaddr: 0x%p",
	    (void *)hat, (void *)vaddr);

	return (0);
}

/*ARGSUSED*/
void
hat_kpm_mseghash_clear(int nentries)
{}

/*ARGSUSED*/
void
hat_kpm_mseghash_update(pgcnt_t inx, struct memseg *msp)
{}

void
hat_kpm_addmem_mseg_update(struct memseg *msp, pgcnt_t nkpmpgs,
    offset_t kpm_pages_off)
{
	_NOTE(ARGUNUSED(nkpmpgs, kpm_pages_off));
	pfn_t base, end;

	/*
	 * kphysm_add_memory_dynamic() does not set nkpmpgs
	 * when page_t memory is externally allocated.  That
	 * code must properly calculate nkpmpgs in all cases
	 * if nkpmpgs needs to be used at some point.
	 */

	/*
	 * The meta (page_t) pages for dynamically added memory are allocated
	 * either from the incoming memory itself or from existing memory.
	 * In the former case the base of the incoming pages will be different
	 * than the base of the dynamic segment so call memseg_get_start() to
	 * get the actual base of the incoming memory for each case.
	 */

	base = memseg_get_start(msp);
	end = msp->pages_end;

	hat_devload(kas.a_hat, kpm_vbase + mmu_ptob(base),
	    mmu_ptob(end - base), base, PROT_READ | PROT_WRITE,
	    HAT_LOAD | HAT_LOAD_LOCK | HAT_LOAD_NOCONSIST);
}

void
hat_kpm_addmem_mseg_insert(struct memseg *msp)
{
	_NOTE(ARGUNUSED(msp));
}

void
hat_kpm_addmem_memsegs_update(struct memseg *msp)
{
	_NOTE(ARGUNUSED(msp));
}

/*
 * Return end of metadata for an already setup memseg.
 * X86 platforms don't need per-page meta data to support kpm.
 */
caddr_t
hat_kpm_mseg_reuse(struct memseg *msp)
{
	return ((caddr_t)msp->epages);
}

void
hat_kpm_delmem_mseg_update(struct memseg *msp, struct memseg **mspp)
{
	_NOTE(ARGUNUSED(msp, mspp));
	ASSERT(0);
}

void
hat_kpm_split_mseg_update(struct memseg *msp, struct memseg **mspp,
    struct memseg *lo, struct memseg *mid, struct memseg *hi)
{
	_NOTE(ARGUNUSED(msp, mspp, lo, mid, hi));
	ASSERT(0);
}

/*
 * Walk the memsegs chain, applying func to each memseg span.
 */
void
hat_kpm_walk(void (*func)(void *, void *, size_t), void *arg)
{
	pfn_t	pbase, pend;
	void	*base;
	size_t	size;
	struct memseg *msp;

	for (msp = memsegs; msp; msp = msp->next) {
		pbase = msp->pages_base;
		pend = msp->pages_end;
		base = ptob(pbase) + kpm_vbase;
		size = ptob(pend - pbase);
		func(arg, base, size);
	}
}

/*
 * Helper function to punch in a mapping that we need with the specified
 * attributes.
 */
void
hati_cpu_punchin(cpu_t *cpu, uintptr_t va, uint_t attrs)
{
	int ret;
	pfn_t pfn;
	hat_t *cpu_hat = cpu->cpu_hat_info->hci_user_hat;

	ASSERT3S(kpti_enable, ==, 1);
	ASSERT3P(cpu_hat, !=, NULL);
	ASSERT3U(cpu_hat->hat_flags & HAT_PCP, ==, HAT_PCP);
	ASSERT3U(va & MMU_PAGEOFFSET, ==, 0);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)va);
	VERIFY3U(pfn, !=, PFN_INVALID);

	/*
	 * We purposefully don't try to find the page_t. This means that this
	 * will be marked PT_NOCONSIST; however, given that this is pretty much
	 * a static mapping that we're using we should be relatively OK.
	 */
	attrs |= HAT_STORECACHING_OK;
	ret = hati_load_common(cpu_hat, va, NULL, attrs, 0, 0, pfn);
	VERIFY3S(ret, ==, 0);
}
