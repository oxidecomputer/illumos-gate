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

#ifndef _SYS_KAPOB_H
#define	_SYS_KAPOB_H

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/sunddi.h>
#include <sys/apob.h>

/*
 * Kernel APOB parsing functionality, machine-specific.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef	_KERNEL
extern void kapob_eb_init(uint64_t, uint64_t);
extern void kapob_preserve(void);
extern const void *kapob_find(const apob_group_t, const uint32_t,
    const uint32_t, size_t *, int *);
extern bool kapob_clone_handle(apob_hdl_t *, ddi_umem_cookie_t *);
#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_KAPOB_H */
