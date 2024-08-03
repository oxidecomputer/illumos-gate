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
#include <sys/pci_impl.h>

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/df.h>
#include <sys/io/zen/df_utils.h>

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
