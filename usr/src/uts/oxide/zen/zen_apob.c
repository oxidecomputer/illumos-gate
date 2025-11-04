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

typedef struct zen_apob_cf {
	const apob_group_t kiacf_group;
	const uint32_t kiacf_type;
} zen_apob_cf_t;

/*
 * These are the APOB groups and types which need to be the same in a pair of
 * APOBs for them to be considered materially the same in terms of memory
 * training data. If the current boot APOB is materially the same as the one
 * that is stored in the SP then we do not need to replace it.
 *
 * Note that these entries are for Turin, the only platform that currently
 * supports eMCR. If a future architecture needs to validate different types
 * then some or all of what follows will need to be relocated to
 * platform-specific code. For now it's optimistically placed within the
 * common zen code.
 */
static zen_apob_cf_t apobcf[] = {
	{ APOB_GROUP_GENERAL, APOB_GENERAL_TYPE_CFG_INFO },
	{ APOB_GROUP_GENERAL, APOB_GENERAL_TYPE_S3REPLAY_BUF_INFO },
	{ APOB_GROUP_GENERAL, APOB_GENERAL_TYPE_CFG_DATA },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_GEN_CONFIG_INFO },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_SOC_INIT_CFG },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP0 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP1 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP2 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP3 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP4 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP5 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP6 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP7 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP8 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP9 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP10 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_MOP11 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR0 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR1 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR2 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR3 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR4 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR5 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR6 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR7 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR8 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_S3_DDR9 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB0 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB1 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB2 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB3 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB4 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB5 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB6 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB7 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB8 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB9 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB10 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_SMB11 },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_MBIST_RES_INFO },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_APCB_BOOT_INFO },
	{ APOB_GROUP_MEMORY, APOB_MEMORY_TYPE_PMU_TRAIN_FAIL },
};

static bool
zen_apob_changed(apob_hdl_t *old, apob_hdl_t *new)
{
	apob_entry_hdl_t **new_entries, **old_entries;
	size_t allocsize, new_count, old_count;
	void *buf = NULL;
	/* Assume changed unless we can prove otherwise */
	bool ret = true;

	allocsize = sizeof (apob_entry_hdl_t *) * APOB_MAX_ENTRIES * 2;
	buf = kmem_zalloc(allocsize, KM_NOSLEEP);
	if (buf == NULL) {
		cmn_err(CE_WARN, "eMCR: failed to allocate memory for "
		    "APOB comparison, assuming changed");
		goto out;
	}
	new_entries = (apob_entry_hdl_t **)buf;
	old_entries = new_entries + APOB_MAX_ENTRIES;

	for (size_t i = 0; i < ARRAY_SIZE(apobcf); i++) {
		zen_apob_cf_t *cf = &apobcf[i];

		new_count = APOB_MAX_ENTRIES;
		if (!apob_gather(new, cf->kiacf_group, cf->kiacf_type,
		    new_entries, &new_count)) {
			cmn_err(CE_WARN,
			    "APOB new Group %u Type %u failed to gather: %s",
			    cf->kiacf_group, cf->kiacf_type,
			    apob_errmsg(new));
			goto out;
		}
		old_count = APOB_MAX_ENTRIES;
		if (!apob_gather(old, cf->kiacf_group, cf->kiacf_type,
		    old_entries, &old_count)) {
			cmn_err(CE_WARN,
			    "APOB old Group %u Type %u failed to gather: %s",
			    cf->kiacf_group, cf->kiacf_type,
			    apob_errmsg(old));
			goto out;
		}
		if (old_count != new_count) {
			cmn_err(CE_NOTE, "APOB Group %u Type %u "
			    "Old count 0x%zx != New count 0x%zx",
			    cf->kiacf_group, cf->kiacf_type,
			    old_count, new_count);
			goto out;
		}

		/*
		 * Now that we know that the APOBs from the current boot and
		 * from flash have the same number of entries for this group
		 * and type, check that they have the same content and are in
		 * the same order. Swapped entries are treated as a material
		 * difference.
		 */
		for (size_t j = 0; j < new_count; j++) {
			if (bcmp(apob_entry_hmac(old_entries[j]),
			    apob_entry_hmac(new_entries[j]),
			    APOB_HMAC_LEN) != 0) {
				cmn_err(CE_NOTE, "APOB Group %u Type %u "
				    "Entry 0x%zx hash mismatch",
				    cf->kiacf_group, cf->kiacf_type, j);
				goto out;
			}
		}
	}
	ret = false;

out:
	if (buf != NULL)
		kmem_free(buf, allocsize);
	return (ret);
}

/*
 * Transmit the APOB data to the SP so that it can be cached and used for eMCR
 * on subsequent boots.
 */
void
zen_apob_sp_transmit(void)
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
	 * directly access its data and size in order to compare with the
	 * existing stored version, and to save it if required.
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

	/*
	 * It is not clear what circumstances would cause the PSP to unset this
	 * bit in the output block. We've only ever seen it set to 1 on Turin
	 * even if the PSP image is not configured to support eMCR.
	 * Unsurprisingly it's always set to 0 on Milan. Regardless, if it is
	 * clear then we will honour the request and not save the APOB.
	 */
	if (!cfg->agci_param.agcp_apob_restore) {
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
		case APOB_EVENT_PMIC_RT_ERROR:
			cmn_err(CE_NOTE,
			    "eMCR: Fatal event 0x%x detected, not saving APOB",
			    event->aev_info);
			save_apob = false;
			break;
		}
		if (!save_apob)
			break;
	}

	if (save_apob) {
		apob_hdl_t *apob_old;

		apob_old = kernel_ipcc_apobread();
		if (apob_old != NULL && !zen_apob_changed(apob_old, apob_hdl))
			save_apob = false;
		kernel_ipcc_apobfree(apob_old);
	}

	if (save_apob) {
		err = kernel_ipcc_apobwrite(apob_hdl);
		if (err == 0) {
			cmn_err(CE_NOTE,
			    "eMCR: Successfully transmitted APOB data to SP");
		} else {
			cmn_err(CE_WARN,
			    "eMCR: Failed to send APOB data to SP, error 0x%x",
			    err);
		}
	} else if ((err = kernel_ipcc_apobwrite(NULL)) != 0) {
		cmn_err(CE_WARN, "eMCR: Failed to inform SP that there"
		    " is no APOB update, err 0x%x", err);
	} else {
		cmn_err(CE_NOTE, "eMCR: No APOB update required");
	}

out:
	kmem_free(apob_hdl, alloclen);
}
