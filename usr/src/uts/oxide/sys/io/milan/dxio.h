/*
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

/*
 * Utility declarations for interacting with DXIO on Milan, specifically.
 */

#ifndef	_SYS_IO_MILAN_DXIO_H
#define	_SYS_IO_MILAN_DXIO_H

#include <sys/stdbool.h>

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;

/*
 * Retrieves and reports the DXIO firmware version from the SMU.
 */
extern bool milan_get_dxio_fw_version(zen_iodie_t *iodie);
extern void milan_report_dxio_fw_version(const zen_iodie_t *iodie);

#endif	/* _SYS_IO_MILAN_DXIO_H */
