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
 * Copyright 2022 Oxide Computer Company
 */

/*
 * This wraps up all the different pin data that we provide information about
 * and exposes it to drivers as part of a common module. While this could all be
 * part of the cpuid information we get today, the shape and format of this data
 * is basically private to the various AMD Zen-specific subsystems and drivers
 * that we have.
 */

#include <sys/x86_archext.h>
#include <sys/cpuvar.h>
#include <sys/modctl.h>
#include "amdzen_data.h"

extern const zen_gpio_pindata_t zen_gpio_sp3_data[];
extern const zen_gpio_pindata_t zen_gpio_sp5_data[];

extern const size_t zen_gpio_sp3_nents;
extern const size_t zen_gpio_sp5_nents;

const zen_gpio_pindata_t *
amdzen_data_pininfo(size_t *npins)
{
	x86_processor_family_t fam = chiprev_family(cpuid_getchiprev(CPU));
	uint32_t sock = cpuid_getsockettype(CPU);

	switch (fam) {
	case X86_PF_AMD_ROME:
	case X86_PF_AMD_MILAN:
		*npins = zen_gpio_sp3_nents;
		return (zen_gpio_sp3_data);
		break;
	case X86_PF_AMD_GENOA:
		switch (sock) {
		case X86_SOCKET_SP5:
			*npins = zen_gpio_sp5_nents;
			return (zen_gpio_sp5_data);
		default:
			break;
		}
	default:
		break;
	}

	*npins = 0;
	return (NULL);
}

static struct modlmisc zen_data_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "Zen Data Module"
};

static struct modlinkage zen_data_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &zen_data_modlmisc, NULL }
};

int
_init(void)
{
	return (mod_install(&zen_data_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&zen_data_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&zen_data_modlinkage));
}
