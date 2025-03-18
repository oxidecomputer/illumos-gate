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
 * This implements the interfaces required to get the BIOS Data ACPI Table
 * (BDAT) resources via ACPI.
 *
 * Note that this is just a stub implementation for now and always reports that
 * no BDAT was found.
 */

#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/acpica.h>
#include <sys/plat/bdat_prd.h>

bool
bdat_prd_mem_present(bdat_prd_mem_rsrc_t rtype,
    const bdat_prd_mem_select_t *rsel, size_t *rsize)
{
	*rsize = 0;
	return (false);
}

bdat_prd_errno_t
bdat_prd_mem_read(bdat_prd_mem_rsrc_t rtype,
    const bdat_prd_mem_select_t *rsel, void *rsrc, size_t rsize)
{
	return (BPE_NOBDAT);
}

static struct modlmisc bdat_prd_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "BDAT Resource Discovery"
};

static struct modlinkage bdat_prd_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &bdat_prd_modlmisc, NULL }
};

static int
bdat_prd_acpi_init(void)
{
	ACPI_STATUS status;
	ACPI_TABLE_BDAT *tbl;

	if (ACPI_FAILURE(status = acpica_init())) {
		cmn_err(CE_WARN,
		    "?bdat_prd: failed to initialize acpica subsystem (%u)",
		    status);
		return (ENOTSUP);
	}

	status = AcpiGetTable(ACPI_SIG_BDAT, 0, (ACPI_TABLE_HEADER **)&tbl);
	if (ACPI_FAILURE(status)) {
		return (0);
	}

	return (0);
}

int
_init(void)
{
	int ret;

	if ((ret = bdat_prd_acpi_init()) != 0) {
		return (ret);
	}

	return (mod_install(&bdat_prd_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&bdat_prd_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&bdat_prd_modlinkage));
}
