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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2021 Oxide Computer Co.
 */

#ifndef	_KBOOT_MMU_H
#define	_KBOOT_MMU_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Kernel boot-time interfaces for handling MMU mappings before the HAT proper
 * is running (i.e. before khat_running is set).
 */

#include <sys/mach_mmu.h>
#include <sys/bootconf.h>

extern void kbm_init(void);

/*
 * Interface to remap the page table window, also used by HAT during init.
 */
extern void *kbm_remap_window(paddr_t physaddr, int writeable);

/*
 * Find the next mapping at or above VA, if found returns non-zero and sets:
 * - va : virtual address
 * - pfn : pfn of real address
 * - size : pagesize of the mapping
 * - prot : protections
 */
extern int kbm_probe(uintptr_t *va, size_t *len, pfn_t *pfn, uint_t *prot);

/*
 * Add a new mapping
 */
extern void kbm_map(uintptr_t va, paddr_t pa, uint_t level, x86pte_t flags);

/*
 * unmap a single 4K page at VA
 */
extern void kbm_unmap(uintptr_t va);

/*
 * Remap a single 4K page at VA (always PROT_READ|PROT_WRITE).
 * Returns the pfn of the old mapping.
 */
extern pfn_t kbm_remap(uintptr_t va, pfn_t pfn);

/*
 * Make a page mapping read only
 */
extern void kbm_read_only(uintptr_t va, paddr_t pa);

/*
 * interface for kmdb to map a physical page, stack is only 1 deep
 */
extern void *kbm_push(paddr_t pa);
extern void kbm_pop(void);

/*
 * interface to get virtual address space during early boot; mappings created
 * from these addresses will be torn down when the hat is set up later
 */
extern uintptr_t kbm_valloc(size_t, paddr_t);

/*
 * The size of memory mapped for the initial kernel nucleus text
 * and data regions setup by the boot loader. needed for startup
 */
extern uint_t kbm_nucleus_size;

#ifdef	__cplusplus
}
#endif

#endif	/* _KBOOT_MMU_H */
