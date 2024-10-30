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
 * Copyright 2024 Oxide Computer Company
 */

/*
 * Abstract support for the APOB, so that code common across the Oxide
 * architecture can work with it, without a direct dependency on a
 * specific microarchitecture.
 */

#include <sys/types.h>
#include <sys/boot_debug.h>
#include <sys/boot_physmem.h>
#include <sys/debug.h>
#include <sys/apob.h>
#include <sys/kapob.h>
#include <sys/sysmacros.h>

#include <sys/io/zen/apob.h>
#include <sys/io/zen/platform_impl.h>

/*
 * The APOB is set up by the PSP and in particular, contains a system memory map
 * that describes the end of DRAM along with any holes in the physical address
 * space. We grab those details here and update our view of the physical memory
 * space accordingly.
 */
void
zen_apob_reserve_phys(void)
{
	const zen_platform_consts_t *zpc = oxide_zen_platform_consts();
	const apob_sys_mem_map_t *smp;
	int err = 0;
	size_t sysmap_len = 0;
	paddr_t max_paddr;
	uint32_t apob_hole_count;

	/*
	 * Our base assumption is we only have bootstrap RAM and no holes.
	 */
	max_paddr = LOADER_PHYSLIMIT;
	apob_hole_count = 0;

	smp = kapob_find(APOB_GROUP_FABRIC,
	    APOB_FABRIC_TYPE_SYS_MEM_MAP, 0, &sysmap_len, &err);
	if (err != 0) {
		eb_printf("couldn't find APOB system memory map "
		    "(errno = %d); using bootstrap RAM only\n", err);
	} else if (sysmap_len < sizeof (*smp)) {
		eb_printf("APOB system memory map too small "
		    "(0x%lx < 0x%lx bytes); using bootstrap RAM only\n",
		    sysmap_len, sizeof (*smp));
	} else if ((sysmap_len - sizeof (*smp)) < (smp->asmm_hole_count *
	    sizeof (apob_sys_mem_map_hole_t))) {
		eb_printf("APOB system memory map truncated? %u holes but only "
		    "0x%lx bytes worth of entries; using bootstrap RAM only\n",
		    smp->asmm_hole_count, sysmap_len - sizeof (*smp));
	} else if (smp->asmm_hole_count > zpc->zpc_max_apob_mem_map_holes) {
		eb_printf("APOB system memory map has too many holes "
		    "(0x%x > 0x%x allowed); using bootstrap RAM only\n",
		    smp->asmm_hole_count, zpc->zpc_max_apob_mem_map_holes);
	} else {
		apob_hole_count = smp->asmm_hole_count;
		max_paddr = P2ALIGN(smp->asmm_high_phys, MMU_PAGESIZE);
	}

	KBM_DBG(apob_hole_count);
	KBM_DBG(max_paddr);

	eb_physmem_set_max(max_paddr);

	for (uint32_t i = 0; i < apob_hole_count; i++) {
		paddr_t start, end;
		KBM_DBGMSG("APOB: RAM hole @ %lx size %lx\n",
		    smp->asmm_holes[i].asmmh_base,
		    smp->asmm_holes[i].asmmh_size);
		start = P2ALIGN(smp->asmm_holes[i].asmmh_base, MMU_PAGESIZE);
		end = P2ROUNDUP(smp->asmm_holes[i].asmmh_base +
		    smp->asmm_holes[i].asmmh_size, MMU_PAGESIZE);

		eb_physmem_reserve_range(start, end - start, EBPR_NOT_RAM);
	}
}
