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
 * Copyright 2024 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_CCX_IMPL_H
#define	_SYS_IO_MILAN_CCX_IMPL_H

/*
 * This file includes constants, type definitions, and prototypes that are
 * specific to Milan and used in the CCX implementation.
 */

#include <sys/types.h>

#include <sys/io/zen/ccx_impl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void milan_fabric_thread_get_dpm_weights(const zen_thread_t *,
    const uint64_t **, uint32_t *);

extern void milan_thread_feature_init(void);
extern void milan_thread_uc_init(void);
extern void milan_core_ls_init(void);
extern void milan_core_ic_init(void);
extern void milan_core_dc_init(void);
extern void milan_core_de_init(void);
extern void milan_core_l2_init(void);
extern void milan_ccx_l3_init(void);
extern void milan_core_undoc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_CCX_IMPL_H */
