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
 * Tracking of the "target" AMD processor family for the oxide mdb modules.
 *
 * Several decoders are processor-family specific: the APOB layout (e.g. the
 * maximum number of memory channels) and the PCIe LTSSM state decode. On a live
 * system or a dump the family is determined automatically from the kernel's
 * recorded chiprev; when that is not possible (a raw APOB, or a dump from a
 * different system) the user sets it explicitly with ::target. Consumers read
 * the current family with oxide_mdb_target_family().
 */

#include <mdb/mdb_modapi.h>
#include <sys/sysmacros.h>
#include <sys/x86_archext.h>
#include <strings.h>

#include "apob_mod.h"
#include "target.h"

static x86_processor_family_t oxide_mdb_target_pf = X86_PF_UNKNOWN;
static const char *oxide_mdb_target_name = "<unknown>";

/*
 * The processor families we can decode for, mapping the name accepted and
 * displayed by ::target to its x86_processor_family_t. More than one family can
 * share a name (e.g. Turin and dense Turin); the first match wins when
 * translating a name to a family.
 */
static const struct {
	const char		*otf_name;
	x86_processor_family_t	otf_pf;
} oxide_mdb_target_families[] = {
	{ "Milan",	X86_PF_AMD_MILAN },
	{ "Genoa",	X86_PF_AMD_GENOA },
	{ "Turin",	X86_PF_AMD_TURIN },
	{ "Turin",	X86_PF_AMD_DENSE_TURIN }
};

x86_processor_family_t
oxide_mdb_target_family(void)
{
	return (oxide_mdb_target_pf);
}

void
oxide_mdb_set_target_family(x86_processor_family_t pf)
{
	oxide_mdb_target_pf = pf;
	oxide_mdb_target_name = "<unknown>";

	for (size_t i = 0; i < ARRAY_SIZE(oxide_mdb_target_families); i++) {
		if (oxide_mdb_target_families[i].otf_pf == pf) {
			oxide_mdb_target_name =
			    oxide_mdb_target_families[i].otf_name;
			break;
		}
	}

	/*
	 * Keep the APOB decoder's family-specific state (e.g. the memory
	 * channel map) in step with the target.
	 */
	apob_set_target(pf);
}

static const char *target_help =
"Some decoders are specific to the target's processor family: APOB structures\n"
"(e.g. the maximum number of memory channels) and the PCIe LTSSM state\n"
"decode. On a live system or a dump the family is determined automatically;\n"
"when that is not possible (a raw APOB, or a dump from a different system)\n"
"this command sets an override.\n"
"The following families are currently supported:\n"
"\n"
"  - Milan\n"
"  - Genoa\n"
"  - Turin\n"
"\n"
"Passing no argument will print the current target.\n";

void
oxide_mdb_target_dcmd_help(void)
{
	mdb_printf(target_help);
}

int
oxide_mdb_target_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	const char *target;

	if (argc == 0) {
		mdb_printf("%s\n", oxide_mdb_target_name);
		return (DCMD_OK);
	}

	if (argc != 1 || argv[0].a_type != MDB_TYPE_STRING ||
	    (flags & DCMD_ADDRSPEC)) {
		return (DCMD_USAGE);
	}

	target = argv[0].a_un.a_str;
	for (size_t i = 0; i < ARRAY_SIZE(oxide_mdb_target_families); i++) {
		if (strcasecmp(target,
		    oxide_mdb_target_families[i].otf_name) == 0) {
			oxide_mdb_set_target_family(
			    oxide_mdb_target_families[i].otf_pf);
			return (DCMD_OK);
		}
	}

	mdb_warn("Unknown target family '%s'\n", target);
	return (DCMD_USAGE);
}
