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
 * Copyright 2024 Oxide Computer Co.
 */

#ifndef _SYS_BOOT_PHYSMEM_H
#define	_SYS_BOOT_PHYSMEM_H

#include <sys/bootconf.h>
#include <sys/machparam.h>
#include <sys/memlist.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * See kboot_mmu.c:kbm_init().
 */
#define	LOADER_PHYSLIMIT	0x80000000UL

extern void eb_physmem_init(struct bsys_mem *);
extern void eb_physmem_fini(void);

extern paddr_t eb_phys_bound_usable(paddr_t);
extern paddr_t eb_phys_alloc(size_t, size_t);
extern caddr_t eb_alloc(caddr_t, size_t, size_t);

#define	eb_alloc_page()	\
	eb_alloc(NULL, MMU_PAGESIZE, MMU_PAGESIZE)

typedef enum eb_physmem_reservation {
	EBPR_NOT_RAM,
	EBPR_NO_ALLOC
} eb_physmem_reservation_t;

extern void eb_physmem_set_max(paddr_t);
extern void eb_physmem_reserve(const memlist_t *, eb_physmem_reservation_t);
extern void eb_physmem_reserve_range(uint64_t, uint64_t,
    eb_physmem_reservation_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_BOOT_PHYSMEM_H */
