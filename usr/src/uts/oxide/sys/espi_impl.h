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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _SYS_ESPI_IMPL_H
#define	_SYS_ESPI_IMPL_H

/*
 * Declarations for the Oxide eSPI polling kernel implementation.
 */

#include <sys/stdbool.h>
#include <sys/types.h>
#include <sys/espi.h>
#include <sys/amdzen/mmioreg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This value is used by the implementation to represent an invalid register
 * read.
 */
#define	ESPI_CFG_INVAL32	UINT32_MAX

extern int espi_init(mmio_reg_block_t);
extern uint32_t espi_intstatus(mmio_reg_block_t);
extern int espi_acquire(mmio_reg_block_t);
extern void espi_release(mmio_reg_block_t);
extern uint32_t espi_get_configuration(mmio_reg_block_t, uint16_t);
extern int espi_set_configuration(mmio_reg_block_t, uint16_t, uint32_t);

extern bool espi_oob_readable(mmio_reg_block_t);
extern bool espi_oob_writable(mmio_reg_block_t);
extern void espi_oob_flush(mmio_reg_block_t);
extern int espi_oob_tx(mmio_reg_block_t, uint8_t *, size_t *);
extern int espi_oob_rx(mmio_reg_block_t, uint8_t *, size_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ESPI_IMPL_H */
