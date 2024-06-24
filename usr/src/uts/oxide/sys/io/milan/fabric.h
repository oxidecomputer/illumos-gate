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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_FABRIC_H
#define	_SYS_IO_MILAN_FABRIC_H

/*
 * Definitions that allow us to access the Milan fabric. This consists of the
 * data fabric, northbridges, SMN, and more.
 */

#include <sys/memlist.h>
#include <sys/plat/pci_prd.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/fabric.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is an entry point for early boot that is used after we have PCIe
 * configuration space set up so we can load up all the information about the
 * actual system itself.
 */
extern void milan_fabric_topo_init(void);

/*
 * Retrieve the base physical address of the PCIe ECAM region.
 */
extern uint64_t milan_fabric_ecam_base(void);

/*
 * This is the primary initialization point for the Milan Data Fabric,
 * Northbridges, PCIe, and related.
 */
extern void milan_fabric_init(void);

extern struct memlist *milan_fabric_pci_subsume(uint32_t, pci_prd_rsrc_t);
extern struct memlist *milan_fabric_gen_subsume(zen_ioms_t *, ioms_rsrc_t);

extern zen_ioms_flag_t milan_ioms_flags(const zen_ioms_t *const);
extern zen_iodie_t *milan_ioms_iodie(const zen_ioms_t *const);
extern smn_reg_t milan_ioms_reg(const zen_ioms_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t milan_ioms_read(zen_ioms_t *, const smn_reg_t);
extern void milan_ioms_write(zen_ioms_t *, const smn_reg_t, const uint32_t);

extern zen_iodie_flag_t milan_iodie_flags(const zen_iodie_t *const);
extern uint8_t milan_iodie_node_id(const zen_iodie_t *const);
extern smn_reg_t milan_iodie_reg(const zen_iodie_t *const,
    const smn_reg_def_t, const uint16_t);
extern uint32_t milan_iodie_read(zen_iodie_t *, const smn_reg_t);
extern void milan_iodie_write(zen_iodie_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_FABRIC_H */
