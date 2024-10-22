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

#ifndef _SYS_IO_TURIN_FABRIC_IMPL_H
#define	_SYS_IO_TURIN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/x86_archext.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/turin/ccx_impl.h>
#include <sys/io/turin/nbif_impl.h>
#include <sys/io/turin/pcie_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This defines what the maximum number of SoCs that are supported in Turin.
 */
#define	TURIN_MAX_SOCS			2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC.
 */
#define	TURIN_IODIE_PER_SOC		1

/*
 * This is the number of NBIO instances that we know are supposed to exist per
 * die.
 */
#define	TURIN_NBIO_PER_IODIE		2

/*
 * This is the number of IO[MS] (IOHUB[MS]) instances that we know are supposed
 * to exist per NBIO.
 */
#define	TURIN_IOMS_PER_NBIO		4

/*
 * This is the number of IO[MS] instances that we know are supposed to exist per
 * die.
 */
#define	TURIN_IOMS_PER_IODIE	(TURIN_IOMS_PER_NBIO * TURIN_NBIO_PER_IODIE)

/*
 * Each NBIO has 4 x16 PCIe Gen5 cores, one on each of four IOHUBs.
 * Additionally, NBIO0/IOHUB2 (IOMS2) has a bonus x8 PCIe Gen3 core.
 * This all means that most IOHUBs across both NBIOs have one core, while
 * NBIO0/IOHUB2 has two.
 */
#define	TURIN_IOMS_MAX_PCIE_CORES	2
#define	TURIN_NBIO_BONUS_IOMS		2
#define	TURIN_IOMS_BONUS_PCIE_CORENO	1

/*
 * Convenience macro to convert an IOMS number to the corresponding NBIO.
 */
#define	TURIN_NBIO_NUM(num)		((num) / TURIN_IOMS_PER_NBIO)

/*
 * Convenience macro to to convert an absolute IOMS index into a relative one
 * within an NBIO.
 */
#define	TURIN_NBIO_IOMS_NUM(num)	((num) % TURIN_IOMS_PER_NBIO)

/*
 * The Turin uarch-specific hooks for initial fabric topology initialization.
 */
extern void turin_fabric_ioms_init(zen_ioms_t *);

/*
 * Turin uarch-specific initialization data for consumption by common Zen code.
 */
extern const uint8_t turin_nbif_nfunc[];
extern const zen_nbif_info_t
    turin_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS];

/*
 * These are the initialization points for the Genoa Data Fabric, Northbridges,
 * PCIe, and related.
 */
extern void turin_fabric_init_tom(zen_ioms_t *, uint64_t, uint64_t, uint64_t);
extern void turin_fabric_disable_vga(zen_ioms_t *);
extern void turin_fabric_pcie_refclk(zen_ioms_t *);
extern void turin_fabric_set_pci_to(zen_ioms_t *, uint16_t, uint16_t);
extern void turin_fabric_iohc_features(zen_ioms_t *);
extern void turin_fabric_iohc_bus_num(zen_ioms_t *, uint8_t);
extern void turin_fabric_iohc_fch_link(zen_ioms_t *, bool);
extern void turin_fabric_iohc_arbitration(zen_ioms_t *);
extern void turin_fabric_nbif_arbitration(zen_nbif_t *);
extern void turin_fabric_nbif_syshub_dma(zen_nbif_t *);
extern void turin_fabric_ioapic(zen_ioms_t *);
extern void turin_fabric_nbif_dev_straps(zen_nbif_t *);
extern void turin_fabric_nbif_bridges(zen_ioms_t *);
extern void turin_fabric_pcie(zen_fabric_t *);
extern void turin_fabric_unhide_bridges(zen_pcie_port_t *);
extern void turin_fabric_init_smn_port_state(zen_pcie_port_t *);
extern void turin_fabric_init_bridges(zen_pcie_port_t *);
extern void turin_fabric_init_pcie_core(zen_pcie_core_t *);

extern void turin_iohc_enable_nmi(zen_ioms_t *);
extern void turin_iohc_nmi_eoi(zen_ioms_t *);

extern smn_reg_t turin_pcie_port_reg(const zen_pcie_port_t *const,
    const smn_reg_def_t);
extern smn_reg_t turin_pcie_core_reg(const zen_pcie_core_t *const,
    const smn_reg_def_t);
extern const zen_pcie_core_info_t *turin_pcie_core_info(const uint8_t, const
    uint8_t);
extern const zen_pcie_port_info_t *turin_pcie_port_info(const uint8_t, const
    uint8_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_FABRIC_IMPL_H */
