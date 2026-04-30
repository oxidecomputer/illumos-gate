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

#ifndef _IOMMU_H
#define	_IOMMU_H

#include <sys/dditypes.h>
#include <sys/stdint.h>

/*
 * Provide prototypes for stub functions that initialize IOMMU functionality.
 * These, in turn, are implemented in a per-architecture manner.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The platform specific operations that manipulate the IOMMU on a given
 * platform/architecture.
 */
typedef struct psm_iommu_ops {
	/*
	 * Performs any platform-specific initialization required to use an
	 * IOMMU, but does not start it yet.
	 */
	void (*pio_init)(void);

	/*
	 * Performs device-specific IOMMU initialization and tear down for the
	 * given device; for example, allocating and freeing memory for an
	 * interrupt remapping table.
	 */
	void (*pio_dev_init)(dev_info_t *);
	void (*pio_dev_fini)(dev_info_t *);

	/*
	 * Actually enable and start the IOMMU.
	 */
	void (*pio_startup)(void);

	/*
	 * Called when memory is added to a system, for example to update device
	 * page tables that use direct or identity mappings to host physical
	 * memory.
	 */
	void (*pio_physmem_update)(uint64_t, uint64_t);

	/*
	 * Quiesce and unquiesce the IOMMU for e.g., entering a low-power mode.
	 * Optional.
	 */
	int (*pio_quiesce)(void);
	int (*pio_unquiesce)(void);
} psm_iommu_ops_t;

extern void psm_iommu_init(void);
extern void psm_iommu_dev_init(dev_info_t *);
extern void psm_iommu_dev_fini(dev_info_t *);
extern void psm_iommu_startup(void);
extern void psm_iommu_physmem_update(uint64_t, uint64_t);
extern int psm_iommu_quiesce(void);
extern int psm_iommu_unquiesce(void);

extern const psm_iommu_ops_t *psm_iommu_linkage(void);

#ifdef __cplusplus
}
#endif

#endif /* _IOMMU_H */
