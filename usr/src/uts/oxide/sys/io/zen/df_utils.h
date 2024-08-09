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

/*
 * Routines for reading and writing DF registers at various points.
 */

#ifndef	_ZEN_DF_UTILS_H
#define	_ZEN_DF_UTILS_H

#include <sys/amdzen/df.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This allows writing to a DF register before PCI config space is setup by
 * making use of CFC/CF8 I/O port-based access. Note the given register offset
 * is not limited to 8-bits as the write is performed as a FICAA/FICAD indirect
 * broadcast write.
 *
 * This may only be used before the end of zen_fabric_topo_init (which will
 * disable I/O port-based PCI config space access). After that point MMIO-based
 * access should be used exclusively for PCI config space.
 */
extern void zen_df_mech1_indirect_bcast_write32(const uint8_t,
    const df_reg_def_t, const uint32_t);

/*
 * The following can only be used after pcie_cfgspace_init() has been called.
 */

/*
 * This is used early in boot when we're trying to bootstrap the system so we
 * can construct our fabric data structure. This always reads against the first
 * data fabric instance which is required to be present. Furthermore, we expect
 * any registers we read with this to be the same across all DFs.
 */
extern uint32_t zen_df_early_read32(const df_reg_def_t);

/*
 * The following can only be used after early fabric init.
 */

/*
 * Broadcast reads & writes are allowed to use PCIe configuration space directly
 * to access the register. Because we are not using the indirect registers,
 * there is no locking being used as the purpose of zi_df_ficaa_lock is just to
 * ensure there's only one use of it at any given time. Note it's still up to
 * the caller to ensure any logical locking is taken care of to ensure
 * consistency across the fabric.
 */
extern uint32_t zen_df_bcast_read32(const zen_iodie_t *, const df_reg_def_t);
extern void zen_df_bcast_write32(const zen_iodie_t *, const df_reg_def_t,
    const uint32_t);

extern uint32_t zen_df_read32(zen_iodie_t *, const uint8_t, const df_reg_def_t);

#ifdef	__cplusplus
}
#endif

#endif /* _ZEN_DF_UTILS_H */
