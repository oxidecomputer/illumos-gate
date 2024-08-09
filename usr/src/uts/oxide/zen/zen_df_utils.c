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
 * Utility functions for accessing the Zen fabric in a
 * microarchitecture-independent manner.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_impl.h>
#include <io/amdzen/amdzen.h>
#include <sys/amdzen/df.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform_impl.h>

#include <sys/io/zen/df_utils.h>


static void
zen_df_mech1_write32(const uint8_t dfno, const df_reg_def_t reg,
    const uint32_t val)
{
	VERIFY(df_reg_valid(oxide_zen_platform_consts()->zpc_df_rev, reg));
	VERIFY3U(reg.drd_reg, <, 0x100);
	outl(PCI_CONFADD, PCI_CADDR1(AMDZEN_DF_BUSNO,
	    (AMDZEN_DF_FIRST_DEVICE + dfno), reg.drd_func, reg.drd_reg));
	outl(PCI_CONFDATA, val);
}

void
zen_df_mech1_indirect_bcast_write32(const uint8_t dfno, const df_reg_def_t reg,
    const uint32_t val)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	df_reg_def_t ficaa, ficad;
	uint32_t fval = 0;

	VERIFY(df_reg_valid(df_rev, reg));
	switch (df_rev) {
	case DF_REV_3:
		ficaa = DF_FICAA_V2;
		ficad = DF_FICAD_LO_V2;
		fval = DF_FICAA_V2_SET_REG(fval, reg.drd_reg >>
		    DF_FICAA_REG_SHIFT);
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		ficaa = DF_FICAA_V4;
		ficad = DF_FICAD_LO_V4;
		fval = DF_FICAA_V4_SET_REG(fval, reg.drd_reg >>
		    DF_FICAA_REG_SHIFT);
		break;
	default:
		panic("Unsupported DF revision %d", df_rev);
	}

	fval = DF_FICAA_V2_SET_TARG_INST(fval, 0);
	fval = DF_FICAA_V2_SET_FUNC(fval, reg.drd_func);
	fval = DF_FICAA_V2_SET_INST(fval, 0);
	fval = DF_FICAA_V2_SET_64B(fval, 0);

	zen_df_mech1_write32(dfno, ficaa, fval);
	zen_df_mech1_write32(dfno, ficad, val);
}

uint32_t
zen_df_early_read32(const df_reg_def_t def)
{
	VERIFY(df_reg_valid(oxide_zen_platform_consts()->zpc_df_rev, def));
	return (pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE,
	    def.drd_func, def.drd_reg));
}

uint32_t
zen_df_bcast_read32(const zen_iodie_t *iodie, const df_reg_def_t def)
{
	VERIFY(df_reg_valid(iodie->zi_df_rev, def));
	return (pci_getl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, def.drd_func,
	    def.drd_reg));
}

void
zen_df_bcast_write32(const zen_iodie_t *iodie, const df_reg_def_t def,
    const uint32_t val)
{
	VERIFY(df_reg_valid(iodie->zi_df_rev, def));
	pci_putl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, def.drd_func,
	    def.drd_reg, val);
}

uint32_t
zen_df_read32(zen_iodie_t *iodie, const uint8_t inst, const df_reg_def_t def)
{
	const df_rev_t df_rev = iodie->zi_df_rev;
	df_reg_def_t ficaa, ficad;
	uint32_t val = 0;

	VERIFY(df_reg_valid(df_rev, def));
	switch (df_rev) {
	case DF_REV_3:
		ficaa = DF_FICAA_V2;
		ficad = DF_FICAD_LO_V2;
		val = DF_FICAA_V2_SET_REG(val, def.drd_reg >>
		    DF_FICAA_REG_SHIFT);
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		ficaa = DF_FICAA_V4;
		ficad = DF_FICAD_LO_V4;
		val = DF_FICAA_V4_SET_REG(val, def.drd_reg >>
		    DF_FICAA_REG_SHIFT);
		break;
	default:
		panic("Unsupported DF revision %d", df_rev);
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
