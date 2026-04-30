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
 * PSM for IOMMU.
 */

#include <sys/stdint.h>
#include <sys/dditypes.h>
#include <sys/debug.h>
#include <sys/sunddi.h>
#include <sys/iommu.h>

/*
 * Generic entries for when no IOMMU PSM exists.
 */
static int
psm_iommu_null_int(void)
{
	return (DDI_SUCCESS);
}

static void
psm_iommu_null_void(void)
{
}

static void
psm_iommu_null_args(uint64_t a __unused, uint64_t b __unused)
{
}

static void
psm_iommu_null_dip(dev_info_t *dip __unused)
{
}

static psm_iommu_ops_t psm_iommu_null_ops = {
	.pio_quiesce = psm_iommu_null_int,
	.pio_unquiesce = psm_iommu_null_int,
	.pio_init = psm_iommu_null_void,
	.pio_dev_init = psm_iommu_null_dip,
	.pio_dev_fini = psm_iommu_null_dip,
	.pio_startup = psm_iommu_null_void,
	.pio_physmem_update = psm_iommu_null_args,
};

/*
 * Entry points for the platform-specific IOMMU driver.  These are filled in
 * an implementation's `psm_iommu_linkage` function, which is currently called
 * from the platform root nexus driver.
 */
static const psm_iommu_ops_t *psm_iommu_ops = &psm_iommu_null_ops;

int
psm_iommu_quiesce(void)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	if (psm_iommu_ops->pio_quiesce == NULL)
		return (psm_iommu_null_int());
	return (psm_iommu_ops->pio_quiesce());
}

int
psm_iommu_unquiesce(void)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	if (psm_iommu_ops->pio_unquiesce == NULL)
		return (psm_iommu_null_int());
	return (psm_iommu_ops->pio_unquiesce());
}

void
psm_iommu_init(void)
{
	VERIFY3P(psm_iommu_ops, ==, &psm_iommu_null_ops);
	psm_iommu_ops = psm_iommu_linkage();
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_init, !=, NULL);
	psm_iommu_ops->pio_init();
}

void
psm_iommu_dev_init(dev_info_t *dip)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_dev_init, !=, NULL);
	psm_iommu_ops->pio_dev_init(dip);
}

void
psm_iommu_dev_fini(dev_info_t *dip)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_dev_fini, !=, NULL);
	psm_iommu_ops->pio_dev_fini(dip);
}

void
psm_iommu_startup(void)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_startup, !=, NULL);
	psm_iommu_ops->pio_startup();
}

void
psm_iommu_physmem_update(uint64_t addr, uint64_t size)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_physmem_update, !=, NULL);
	psm_iommu_ops->pio_physmem_update(addr, size);
}
