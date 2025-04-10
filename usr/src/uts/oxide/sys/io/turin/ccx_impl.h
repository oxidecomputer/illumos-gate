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
 * Copyright 2024 Oxide Computer Company
 */

#ifndef	_SYS_IO_TURIN_CCX_IMPL_H
#define	_SYS_IO_TURIN_CCX_IMPL_H

/*
 * This file includes constants, type definitions, and prototypes that are
 * specific to Turin and used in the CCX implementation.
 */

#include <sys/io/zen/ccx_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern void turin_ccx_physmem_init(void);

extern void turin_thread_feature_init(void);
extern void turin_thread_uc_init(void);
extern void turin_core_ls_init(void);
extern void turin_core_ic_init(void);
extern void turin_core_dc_init(void);
extern void turin_core_tw_init(void);
extern void turin_core_l2_init(void);
extern void turin_core_undoc_init(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_CCX_IMPL_H */
