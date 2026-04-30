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
 * the platform
 */
typedef struct psm_iommu_ops {
	void (*pio_init)(void);
	void (*pio_dev_init)(dev_info_t *);
	void (*pio_startup)(void);
	void (*pio_physmem_update)(uint64_t, uint64_t);
	int (*pio_quiesce)(void);
	int (*pio_unquiesce)(void);
} psm_iommu_ops_t;

extern psm_iommu_ops_t *psm_iommu_ops;

extern void psm_iommu_linkage(void);

extern void psm_iommu_init(void);
extern void psm_iommu_dev_init(dev_info_t *);
extern void psm_iommu_startup(void);
extern void psm_iommu_physmem_update(uint64_t, uint64_t);
extern int psm_iommu_quiesce(void);
extern int psm_iommu_unquiesce(void);

#ifdef __cplusplus
}
#endif

#endif /* _IOMMU_H */
