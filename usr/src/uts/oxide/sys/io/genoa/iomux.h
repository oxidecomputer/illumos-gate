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
 * Copyright 2023 Oxide Computer Company
 */

#ifndef _SYS_IO_GENOA_IOMUX_H
#define	_SYS_IO_GENOA_IOMUX_H

/*
 * This file contains constants that are specific to the Genoa implementation of
 * the I/O Mux.  That is, while <sys/amdzen/fch/iomux.h> describes the general
 * interface to the unit, the following definitions relate to what specific
 * alternate functions are and what the pins mean.
 *
 * This header should not generally have everything that exists in the I/O Mux.
 * That is what the general zen_data_sp5.c tables are for. Instead, this is here
 * to support early boot and general things we need to do before we have the
 * full I/O multiplexing driver existing and in a useable state.
 */

#include <sys/amdzen/mmioreg.h>
#include <sys/amdzen/fch.h>
#include <sys/amdzen/fch/iomux.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is a convenience macro for setting the function for a particular pin
 * using MMIO.  It uses the function values defined below and will fail to
 * compile if a definition for the requested function and pin does not exist.
 */
#define	GENOA_FCH_IOMUX_IOMUX_SET_MMIO(b, r, f)	\
	mmio_reg_write(FCH_IOMUX_IOMUX_MMIO(b, r),	\
	GENOA_FCH_IOMUX_ ## r ## _ ## f)

/*
 * IOMUX function values.
 */

#define	GENOA_FCH_IOMUX_26_PCIE_RST1_L		0
#define	GENOA_FCH_IOMUX_26_AGPIO26		1
#define	GENOA_FCH_IOMUX_26_EGPIO26_0		1

#define	GENOA_FCH_IOMUX_27_PCIE_RST3_L		0
#define	GENOA_FCH_IOMUX_27_EGPIO27		1
#define	GENOA_FCH_IOMUX_27_EGPIO26_3		1

#define	GENOA_FCH_IOMUX_129_ESPI_RSTIN_L	0
#define	GENOA_FCH_IOMUX_129_KBRST_L		1
#define	GENOA_FCH_IOMUX_129_AGPIO129		2

#define	GENOA_FCH_IOMUX_135_UART0_CTS_L		0
#define	GENOA_FCH_IOMUX_136_UART0_RXD		0
#define	GENOA_FCH_IOMUX_137_UART0_RTS_L		0
#define	GENOA_FCH_IOMUX_138_UART0_TXD		0
#define	GENOA_FCH_IOMUX_139_AGPIO139		1

#define	GENOA_FCH_IOMUX_141_UART1_RXD		0
#define	GENOA_FCH_IOMUX_142_UART1_TXD		0

#define	GENOA_FCH_RMTMUX_10_PCIE_RST1_L		0
#define	GENOA_FCH_RMTMUX_10_EGPIO26_1		1
#define	GENOA_FCH_RMTMUX_11_PCIE_RST2_L		0
#define	GENOA_FCH_RMTMUX_11_EGPIO26_2		1

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_IOMUX_H */
