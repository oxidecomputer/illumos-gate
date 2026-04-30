/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
 * Entry points for the platform-specific IOMMU driver.  These are filled in
 * by that driver's module's `_init` function, and cleared by its `_fini`.
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
psm_iommu_null_dev_init(dev_info_t *dip __unused)
{
}

static psm_iommu_ops_t psm_iommu_null_ops = {
	.pio_quiesce = psm_iommu_null_int,
	.pio_unquiesce = psm_iommu_null_int,
	.pio_init = psm_iommu_null_void,
	.pio_dev_init = psm_iommu_null_dev_init,
	.pio_startup = psm_iommu_null_void,
	.pio_physmem_update = psm_iommu_null_args,
};

psm_iommu_ops_t *psm_iommu_ops = &psm_iommu_null_ops;

int
psm_iommu_quiesce(void)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_quiesce, !=, NULL);
	return (psm_iommu_ops->pio_quiesce());
}

int
psm_iommu_unquiesce(void)
{
	VERIFY3P(psm_iommu_ops, !=, NULL);
	VERIFY3P(psm_iommu_ops->pio_unquiesce, !=, NULL);
	return (psm_iommu_ops->pio_unquiesce());
}

void
psm_iommu_init(void)
{
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
