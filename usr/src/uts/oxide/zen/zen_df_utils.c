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

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_impl.h>

/*
 * pci_impl.h and memlist_impl.h conflict.
 */
#ifdef memlist_insert
#undef memlist_insert
#endif

#ifdef memlist_find
#undef memlist_find
#endif

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/df.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>

#include <zen/df_utils.h>


/*
 * Like PCI_CADDR1, but allows for access to extended configuration space.
 * If DF::CoreMasterAccessCtrl[EnableCf8ExtCfg] is enabled, the usually
 * reserved bits 27:24 are set to bits 11:8 of the register offset.
 */

#define	PCI_CADDR1_EXT(b, d, f, r) \
		PCI_CADDR1(b, d, f, r) | ((((r) >> 8) & 0xf) << 24)

void
zen_df_mech1_write32(const df_reg_def_t reg, uint32_t val)
{
	outl(PCI_CONFADD, PCI_CADDR1_EXT(AMDZEN_DF_BUSNO,
	    AMDZEN_DF_FIRST_DEVICE, reg.drd_func, reg.drd_reg));
	outl(PCI_CONFDATA, val);
}

uint32_t
zen_df_early_read32(const df_reg_def_t def)
{
	ASSERT0(def.drd_reg & oxide_zen_platform_consts()->zpc_df_rev);
	return (pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE,
	    def.drd_func, def.drd_reg));
}

uint32_t
zen_df_bcast_read32(zen_iodie_t *iodie, const df_reg_def_t def)
{
	ASSERT0(def.drd_reg & oxide_zen_platform_consts()->zpc_df_rev);
	return (pci_getl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, def.drd_func,
	    def.drd_reg));
}

void
zen_df_bcast_write32(zen_iodie_t *iodie, const df_reg_def_t def,
    uint32_t val)
{
	ASSERT0(def.drd_reg & oxide_zen_platform_consts()->zpc_df_rev);
	pci_putl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, def.drd_func,
	    def.drd_reg, val);
}

uint32_t
zen_df_read32(zen_iodie_t *iodie, uint8_t inst, const df_reg_def_t def)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	df_reg_def_t ficaa, ficad;
	uint32_t val = 0;

	ASSERT3U(def.drd_gens & df_rev, ==, df_rev);
	switch (df_rev) {
	case DF_REV_3:
		ficaa = DF_FICAA_V2;
		ficad = DF_FICAD_LO_V2;
		val = DF_FICAA_V2_SET_REG(val, def.drd_reg >> 2);
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		ficaa = DF_FICAA_V4;
		ficad = DF_FICAD_LO_V4;
		val = DF_FICAA_V4_SET_REG(val, def.drd_reg >> 2);
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		return (-1);
	}

	val = DF_FICAA_V2_SET_TARG_INST(val, 1);
	val = DF_FICAA_V2_SET_FUNC(val, def.drd_func);
	val = DF_FICAA_V2_SET_INST(val, inst);
	val = DF_FICAA_V2_SET_64B(val, 0);

	mutex_enter(&iodie->zi_df_ficaa_lock);
	pci_putl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, ficaa.drd_func,
	    ficaa.drd_reg, val);
	val = pci_getl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, ficad.drd_func,
	    ficad.drd_reg);
	mutex_exit(&iodie->zi_df_ficaa_lock);

	return (val);
}
