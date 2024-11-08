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

#ifndef _SYS_IO_ZEN_RUBY_DXIO_DATA_H
#define	_SYS_IO_ZEN_RUBY_DXIO_DATA_H

#include <sys/types.h>
#include <sys/stdint.h>

/*
 * Declarations for data used by the MPIO firmware to configure the DXIO
 * crossbar on Ruby.
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
typedef struct zen_mpio_ask_port zen_mpio_ask_port_t;
typedef struct zen_mpio_ubm_hfc_port zen_mpio_ubm_hfc_port_t;

/*
 * Ruby port definitions and length.
 */
extern const zen_mpio_ask_port_t ruby_mpio_pcie_s0[];
extern size_t RUBY_MPIO_PCIE_S0_LEN;

/*
 * Ruby UBM data.  Not used by Oxide hardware.
 */
extern const zen_mpio_ubm_hfc_port_t ruby_mpio_hfc_ports[];
extern const size_t RUBY_MPIO_UBM_HFC_DESCR_NPORTS;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_RUBY_DXIO_DATA_H */
