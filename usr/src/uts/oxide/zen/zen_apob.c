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
 * Copyright 2025 Oxide Computer Company
 */

/*
 * Abstract support for the APOB, so that code common across the Oxide
 * architecture can work with it, without a direct dependency on a
 * specific microarchitecture.
 */

#include <sys/types.h>
#include <sys/boot_data.h>
#include <sys/boot_debug.h>
#include <sys/boot_physmem.h>
#include <sys/debug.h>
#include <sys/apob.h>
#include <sys/kapob.h>
#include <sys/sysmacros.h>
#include <sys/kernel_ipcc.h>
#include <sys/stdbool.h>

#include <sys/amdzen/bdat.h>
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

		if (smp->asmm_holes[i].asmmh_type == APOB_MEM_HOLE_TYPE_BDAT) {
			/*
			 * Save the BDAT address as a property for the bdat_prd
			 * module to find.
			 */
			bt_set_prop_u64(BTPROP_NAME_BDAT_START, start);
			bt_set_prop_u64(BTPROP_NAME_BDAT_END, end);
		}
	}
}

void
zen_apob_preserve(void)
{
	const size_t alloclen = apob_handle_size();
	apob_hdl_t *apob_hdl;
	const apob_gen_cfg_info_t *cfg;
	const apob_gen_event_log_t *elog;
	uint16_t limit;
	size_t len;
	int err;

	/*
	 * We take a clone of the kernel's APOB handle here so that we can
	 * directly access its data and size for transmission to the SP via
	 * IPCC if we decide to save it.
	 */
	apob_hdl = kmem_zalloc(alloclen, KM_SLEEP);
	if (!kapob_clone_handle(apob_hdl, NULL)) {
		cmn_err(CE_WARN,
		    "eMCR: Failed to acquire clone of KAPOB handle");
		goto out;
	}

	cfg = apob_find(apob_hdl,
	    APOB_GROUP_GENERAL, APOB_GENERAL_TYPE_CFG_INFO, 0, &len);
	if (cfg == NULL) {
		cmn_err(CE_NOTE,
		    "APOB general configuration: %s (errno = %d)",
		    apob_errmsg(apob_hdl), apob_errno(apob_hdl));
		goto out;
	}
	if (len < sizeof (*cfg)) {
		cmn_err(CE_NOTE, "APOB general configuration area too small "
		    "(0x%lx < 0x%lx bytes)", len, sizeof (*cfg));
		goto out;
	}

	if (!cfg->agci_apob_restore) {
		cmn_err(CE_NOTE, "eMCR: restoration disabled in APOB");
		goto out;
	}

	elog = apob_find(apob_hdl,
	    APOB_GROUP_GENERAL, APOB_GENERAL_TYPE_EVENT_LOG, 0, &len);

	limit = MIN(elog->agevl_count, ARRAY_SIZE(elog->agevl_events));

	bool save_apob = true;
	for (uint16_t i = 0; i < limit; i++) {
		const apob_event_t *event = &elog->agevl_events[i];

		if (event->aev_info == ABL_EVENT_PMU_MBIST) {
			cmn_err(CE_NOTE,
			    "eMCR: PMU MBIST enabled, not saving APOB");
			save_apob = false;
			break;
		}

		if (event->aev_class != APOB_EVC_FATAL)
			continue;

		switch (event->aev_info) {
		case APOB_EVENT_TRAIN_ERROR:
		case APOB_EVENT_MEMTEST_ERROR:
		case APOB_EVENT_MEM_RRW_ERROR:
		case APOB_EVENT_PCMIC_RT_ERROR:
			cmn_err(CE_NOTE,
			    "eMCR: Fatal event 0x%x detected, not saving APOB",
			    event->aev_info);
			save_apob = false;
			break;
		}
		if (!save_apob)
			break;
	}

	// XXX - we should do something to determine if an update is required
	// rather than writing to flash on every boot. AGESA compares HMACs of
	// selected entries between the old and new.

	if (!save_apob) {
		const uint8_t disable[] = "!APOB-DISABLE";
		err = kernel_ipcc_apob(disable, sizeof (disable));
	} else {
		err = kernel_ipcc_apob(apob_get_raw(apob_hdl),
		    apob_get_len(apob_hdl));
	}

	if (err == 0) {
		if (save_apob) {
			cmn_err(CE_NOTE,
			    "eMCR: Successfully transmitted APOB data to SP");
		}
	} else {
		cmn_err(CE_WARN,
		    "eMCR: Failed to send APOB data to SP, error %d", err);
	}

out:
	kmem_free(apob_hdl, alloclen);
}
