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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * This implements the interfaces required to access SMN registers that need
 * special handling, such as those restricted due to CPU security policy.
 * This is effectively a thin veneer around the platform-specific code and
 * related pieces of unix.
 */

#include <sys/errno.h>
#include <sys/plat/amdzen.h>
#include <sys/modctl.h>
#include <sys/io/zen/fabric.h>

int
amdzen_plat_smn_read(uint8_t nodeid, const smn_reg_t reg, uint32_t *val)
{
	if (SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_CORE &&
	    SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_PORT) {
		return (ESRCH);
	}

	*val = zen_read_iodie_pcie_reg(nodeid, reg);
	return (0);
}

int
amdzen_plat_smn_write(uint8_t nodeid, const smn_reg_t reg, const uint32_t val)
{
	if (SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_CORE &&
	    SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_PORT) {
		return (ESRCH);
	}
	zen_write_iodie_pcie_reg(nodeid, reg, val);
	return (0);
}

static struct modlmisc amdzen_plat_modlmisc_oxide = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "Oxide AMD Zen Platform Driver"
};

static struct modlinkage amdzen_plat_modlinkage_oxide = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &amdzen_plat_modlmisc_oxide, NULL }
};

int
_init(void)
{
	return (mod_install(&amdzen_plat_modlinkage_oxide));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&amdzen_plat_modlinkage_oxide, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&amdzen_plat_modlinkage_oxide));
}
