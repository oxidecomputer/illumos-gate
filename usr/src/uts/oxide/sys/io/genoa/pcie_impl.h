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

#ifndef _SYS_IO_GENOA_PCIE_IMPL_H
#define	_SYS_IO_GENOA_PCIE_IMPL_H

#include <sys/types.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/genoa/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each of the normal PCIe cores is an RC9x16: up to 9 ports across 16 lanes.
 * Each bonus PCIe core is an RC4x4.
 */
#define	GENOA_PCIE_CORE_MAX_PORTS	9
#define	GENOA_PCIE_CORE_BONUS_PORTS	4

/*
 * This is the SDP unit ID for PCIe core 0 in each IOMS.
 */
#define	GENOA_PCIE_CORE0_UNITID		16

extern const zen_pcie_reg_dbg_t genoa_pcie_core_dbg_regs[];
extern const zen_pcie_reg_dbg_t genoa_pcie_port_dbg_regs[];
extern const size_t genoa_pcie_core_dbg_nregs;
extern const size_t genoa_pcie_port_dbg_nregs;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_PCIE_IMPL_H */
