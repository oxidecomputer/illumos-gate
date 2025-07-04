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

#ifndef _SYS_IO_GENOA_FABRIC_IMPL_H
#define	_SYS_IO_GENOA_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/x86_archext.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/oxio.h>
#include <sys/io/genoa/ccx_impl.h>
#include <sys/io/genoa/nbif_impl.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This defines what the maximum number of SoCs that are supported in Genoa.
 */
#define	GENOA_MAX_SOCS			2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC.
 */
#define	GENOA_IODIE_PER_SOC		1

/*
 * This is the number of NBIO instances that we know are supposed to exist per
 * die.
 */
#define	GENOA_NBIO_PER_IODIE		2

/*
 * This is the number of IOHC instances that we know are supposed to exist per
 * NBIO.
 */
#define	GENOA_IOHC_PER_NBIO		2

/*
 * This is the number of IOHC instances that we know are supposed to exist per
 * die.
 */
#define	GENOA_IOHC_PER_IODIE	(GENOA_IOHC_PER_NBIO * GENOA_NBIO_PER_IODIE)

/*
 * Each NBIO has 4 x16 PCIe Gen5 cores, split across two IOHUBs. Additionally,
 * each NBIO has a bonus x4 PCIe Gen3 core linked to the first IOHUB. This all
 * means that the first IOHUB in each NBIO has three cores while the second has
 * two.
 */
#define	GENOA_IOHC_MAX_PCIE_CORES	3
#define	GENOA_NBIO_BONUS_IOHUB		0
#define	GENOA_IOHC_BONUS_PCIE_CORENO	2

/*
 * Convenience macro to convert an IOHC number to the corresponding relative
 * IOHUB, and to the containing NBIO. IOMS and IOHUBs have a 1:1 mapping on
 * Genoa.
 */
#define	GENOA_IOHC_IOHUB_NUM(num)	((num) % GENOA_IOHC_PER_NBIO)
#define	GENOA_NBIO_NUM(num)		((num) / GENOA_IOHC_PER_NBIO)

/*
 * The Genoa uarch-specific hooks for initial fabric topology initialization.
 */
extern uint8_t genoa_fabric_ioms_nbio_num(uint8_t);
extern bool genoa_fabric_smu_pptable_init(zen_fabric_t *, void *, size_t *);
extern void genoa_fabric_nbio_init(zen_nbio_t *);
extern void genoa_fabric_ioms_init(zen_ioms_t *);

/*
 * Genoa uarch-specific initialization data for consumption by common Zen code.
 */
extern const uint8_t genoa_nbif_nfunc[];
extern const zen_nbif_info_t
    genoa_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS];
extern const zen_iohc_nbif_ports_t genoa_pcie_int_ports[GENOA_IOHC_PER_IODIE];

/*
 * These are the initialization points for the Genoa Data Fabric, Northbridges,
 * PCIe, and related.
 */
extern void genoa_fabric_init_tom(zen_ioms_t *, uint64_t, uint64_t, uint64_t);
extern void genoa_fabric_disable_vga(zen_ioms_t *);
extern void genoa_fabric_pcie_refclk(zen_ioms_t *);
extern void genoa_fabric_set_pci_to(zen_ioms_t *, uint16_t, uint16_t);
extern void genoa_fabric_iohc_features(zen_ioms_t *);
extern void genoa_fabric_nbio_features(zen_nbio_t *);
extern void genoa_fabric_iohc_bus_num(zen_ioms_t *, uint8_t);
extern void genoa_fabric_iohc_fch_link(zen_ioms_t *, bool);
extern void genoa_fabric_iohc_arbitration(zen_ioms_t *);
extern void genoa_fabric_nbio_arbitration(zen_nbio_t *);
extern void genoa_fabric_nbif_arbitration(zen_nbif_t *);
extern void genoa_fabric_nbif_syshub_dma(zen_nbif_t *);
extern void genoa_fabric_iohc_clock_gating(zen_ioms_t *);
extern void genoa_fabric_nbio_clock_gating(zen_nbio_t *);
extern void genoa_fabric_nbif_clock_gating(zen_nbif_t *);
extern void genoa_fabric_ioapic_clock_gating(zen_ioms_t *);
extern void genoa_fabric_ioapic(zen_ioms_t *);
extern void genoa_fabric_nbif_init(zen_nbif_t *);
extern void genoa_fabric_nbif_dev_straps(zen_nbif_t *);
extern void genoa_fabric_nbif_bridges(zen_ioms_t *);
extern uint8_t genoa_fabric_hotplug_tile_id(const oxio_engine_t *);
extern void genoa_fabric_hotplug_core_init(zen_pcie_core_t *);
extern void genoa_fabric_hotplug_port_init(zen_pcie_port_t *);
extern void genoa_fabric_hotplug_port_unblock_training(zen_pcie_port_t *);
extern bool genoa_fabric_hotplug_start(zen_iodie_t *);
extern void genoa_fabric_pcie(zen_fabric_t *);
extern void genoa_fabric_hide_bridge(zen_pcie_port_t *);
extern void genoa_fabric_unhide_bridge(zen_pcie_port_t *);
extern void genoa_fabric_init_pcie_port(zen_pcie_port_t *);
extern void genoa_fabric_init_pcie_port_after_reconfig(zen_pcie_port_t *);
extern void genoa_fabric_init_pcie_straps(zen_pcie_core_t *);
extern void genoa_fabric_init_bridge(zen_pcie_port_t *);
extern void genoa_fabric_ioms_iohc_disable_unused_pcie_bridges(zen_ioms_t *);
extern void genoa_fabric_init_pcie_core(zen_pcie_core_t *);
extern void genoa_iohc_enable_nmi(zen_ioms_t *);
extern void genoa_iohc_nmi_eoi(zen_ioms_t *);

extern smn_reg_t genoa_pcie_port_reg(const zen_pcie_port_t *const,
    const smn_reg_def_t);
extern smn_reg_t genoa_pcie_core_reg(const zen_pcie_core_t *const,
    const smn_reg_def_t);
extern const zen_pcie_core_info_t *genoa_pcie_core_info(const uint8_t,
    const uint8_t);
extern const zen_pcie_port_info_t *genoa_pcie_port_info(const uint8_t,
    const uint8_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_FABRIC_IMPL_H */
