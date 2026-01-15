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
 * In i86pc we don't currently implement support for these.
 */

#include <sys/errno.h>
#include <sys/plat/amdzen.h>
#include <sys/modctl.h>

int
amdzen_plat_smn_read(uint8_t nodeid, const smn_reg_t reg, uint32_t *val)
{
	return (ESRCH);
}

int
amdzen_plat_smn_write(uint8_t nodeid, const smn_reg_t reg, const uint32_t val)
{
	return (ESRCH);
}

static struct modlmisc amdzen_plat_modlmisc_i86pc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "i86pc AMD Zen Platform Driver"
};

static struct modlinkage amdzen_plat_modlinkage_i86pc = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &amdzen_plat_modlmisc_i86pc, NULL }
};

int
_init(void)
{
	return (mod_install(&amdzen_plat_modlinkage_i86pc));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&amdzen_plat_modlinkage_i86pc, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&amdzen_plat_modlinkage_i86pc));
}
