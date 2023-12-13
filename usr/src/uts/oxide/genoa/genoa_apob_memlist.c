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
#include <sys/memlist.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <vm/kboot_mmu.h>

#include "genoa_apob.h"
#include "genoa_physaddrs.h"

void
genoa_apob_reserve_phys(void)
{
	uint32_t i;
	size_t sysmap_len;
	int err;
	const genoa_apob_sysmap_t *smp;
	const uint_t MAX_APOB_HOLES = ARRAY_SIZE(smp->gasm_holes);
	uint32_t apob_hole_count;
	paddr_t max_paddr;
	paddr_t start, end;

	apob_hole_count = 0;
	max_paddr = LOADER_PHYSLIMIT;
	err = 0;
	sysmap_len = 0;

	smp = genoa_apob_find(GENOA_APOB_GROUP_FABRIC, 9, 0, &sysmap_len, &err);
	/*
	 * XXX(cross): Should we do something (early return?) in these
	 * failure cases?  This wouuld cause a semantic difference, as
	 * `eb_physmem_set_max(max_paddr)` is the same as
	 * `eb_physmem_set_max(LOAD_PHYSLIMIT)` in the failure case(s),
	 * and we would not be calling that.  However, we could call
	 * `eb_physmem_set_max` twice, or perhaps it's already set.
	 */
	if (err != 0) {
		eb_printf("couldn't find APOB system memory map "
		    "(errno = %d); using bootstrap RAM only\n", err);
	} else if (sysmap_len < sizeof (*smp)) {
		eb_printf("APOB system memory map too small "
		    "(0x%lx < 0x%lx bytes); using bootstrap RAM only\n",
		    sysmap_len, sizeof (*smp));
	} else if (smp->gasm_hole_count > MAX_APOB_HOLES) {
		eb_printf("APOB system memory map has too many holes "
		    "(0x%x > 0x%x allowed); using bootstrap RAM only\n",
		    smp->gasm_hole_count, MAX_APOB_HOLES);
	} else {
		apob_hole_count = smp->gasm_hole_count;
		max_paddr = P2ALIGN(smp->gasm_high_phys, MMU_PAGESIZE);
	}

	KBM_DBG(apob_hole_count);
	KBM_DBG(max_paddr);

	eb_physmem_set_max(max_paddr);

	for (i = 0; i < apob_hole_count; i++) {
		KBM_DBGMSG("APOB: RAM hole @ %lx size %lx\n",
		    smp->gasm_holes[i].gasmrh_base,
		    smp->gasm_holes[i].gasmrh_size);
		start = P2ALIGN(smp->gasm_holes[i].gasmrh_base, MMU_PAGESIZE);
		end = P2ROUNDUP(smp->gasm_holes[i].gasmrh_base +
		    smp->gasm_holes[i].gasmrh_size, MMU_PAGESIZE);

		eb_physmem_reserve_range(start, end - start, EBPR_NOT_RAM);
	}
}
