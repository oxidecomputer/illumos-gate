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

#ifndef _SYS_IO_GENOA_MPIO_IMPL_H
#define	_SYS_IO_GENOA_MPIO_IMPL_H

#include <sys/io/genoa/mpio.h>

/*
 * Types, definitions, function prototypes, etc, specific to the Genoa
 * implementation of MPIO support.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_mpio_global_config zen_mpio_global_config_t;

extern void genoa_set_mpio_global_config(zen_mpio_global_config_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_MPIO_IMPL_H */
