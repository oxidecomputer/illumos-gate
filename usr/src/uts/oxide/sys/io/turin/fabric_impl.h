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

#ifndef _SYS_IO_TURIN_FABRIC_IMPL_H
#define	_SYS_IO_TURIN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/x86_archext.h>
#include <sys/io/zen/mpio_impl.h>
#include <sys/io/zen/oxio.h>
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
 * This is the number of IOHC (and IO[MS] / IOHUB[MS]) instances that we know
 * are supposed to exist per NBIO.
 */
#define	TURIN_IOHC_PER_NBIO		4

/*
 * This is the number of IOHC (and IO[MS]) instances that we know are supposed
 * to exist per die.
 */
#define	TURIN_IOHC_PER_IODIE	(TURIN_IOHC_PER_NBIO * TURIN_NBIO_PER_IODIE)

/*
 * Each NBIO has 4 x16 PCIe Gen5 cores, one on each of four IOHUBs.
 * Additionally, NBIO0/IOHUB2 (IOMS2) has a bonus x8 PCIe Gen3 core (PCIe5) and
 * there is also an extra bonus core (PCIe6) on all the larger IOHC types.
 * PCIe6 is unused everywhere on Turin, so we pretend that it does not exist,
 * which is why TURIN_IOHC_MAX_PCIE_CORES is 2, not 3.
 */
#define	TURIN_IOHC_MAX_PCIE_CORES		2
#define	TURIN_NBIO_BONUS_IOHC			1
#define	TURIN_IOHC_BONUS_PCIE_CORENO	1
#define	TURIN_PCIE6_CORE_BONUS_PORTS	3
#define	TURIN_IOHC_BONUS_PCIE6_CORENO	2

/*
 * Convenience macro to convert an IOMS number to the corresponding NBIO.
 */
#define	TURIN_NBIO_NUM(num)		((num) / TURIN_IOHC_PER_NBIO)

/*
 * Convenience macro to convert an absolute IOHC index (within an IO die) into
 * an NBIO-relative IOHUB number. Each NBIO contains four logical IOHC
 * instances, two large and two small. The large ones have the even numbered
 * IOHUBs. The theory statement in turin_fabric.c has some more details on
 * IOHC mappings.
 */
#define	TURIN_IOHC_IOHUB_NUM(num)	\
	(((num) / TURIN_IOHC_PER_NBIO) + (((num) & 1) ? 2 : 0))

/*
 * The Turin uarch-specific hooks for initial fabric topology initialization.
 */
extern uint8_t turin_fabric_ioms_nbio_num(uint8_t);
extern bool turin_fabric_smu_pptable_init(zen_fabric_t *, void *, size_t *);
extern void turin_fabric_smu_pptable_post(zen_iodie_t *);
extern void turin_fabric_nbio_init(zen_nbio_t *);
extern void turin_fabric_ioms_init(zen_ioms_t *);

/*
 * Turin uarch-specific initialization data for consumption by common Zen code.
 */
extern const uint8_t turin_nbif_nfunc[];
extern const zen_nbif_info_t
    turin_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS];
extern const zen_iohc_nbif_ports_t turin_pcie_int_ports[TURIN_IOHC_PER_IODIE];

/*
 * These are the initialization points for the Genoa Data Fabric, Northbridges,
 * PCIe, and related.
 */
extern uint8_t turin_fabric_physaddr_size(void);
extern void turin_fabric_init_tom(zen_ioms_t *, uint64_t, uint64_t, uint64_t);
extern void turin_fabric_disable_vga(zen_ioms_t *);
extern void turin_fabric_pcie_refclk(zen_ioms_t *);
extern void turin_fabric_set_pci_to(zen_ioms_t *, uint16_t, uint16_t);
extern void turin_fabric_iohc_features(zen_ioms_t *);
extern void turin_fabric_nbio_features(zen_nbio_t *);
extern void turin_fabric_iohc_bus_num(zen_ioms_t *, uint8_t);
extern void turin_fabric_iohc_fch_link(zen_ioms_t *, bool);
extern void turin_fabric_iohc_arbitration(zen_ioms_t *);
extern void turin_fabric_nbio_arbitration(zen_nbio_t *);
extern void turin_fabric_nbif_arbitration(zen_nbif_t *);
extern void turin_fabric_nbif_syshub_dma(zen_nbif_t *);
extern void turin_fabric_iohc_clock_gating(zen_ioms_t *);
extern void turin_fabric_nbio_clock_gating(zen_nbio_t *);
extern void turin_fabric_nbif_clock_gating(zen_nbif_t *);
extern void turin_fabric_ioapic_clock_gating(zen_ioms_t *);
extern void turin_fabric_ioapic(zen_ioms_t *);
extern void turin_fabric_nbif_init(zen_nbif_t *);
extern void turin_fabric_nbif_dev_straps(zen_nbif_t *);
extern void turin_fabric_nbif_bridges(zen_ioms_t *);
extern uint8_t turin_fabric_hotplug_tile_id(const oxio_engine_t *);
extern void turin_fabric_hotplug_core_init(zen_pcie_core_t *);
extern void turin_fabric_hotplug_port_init(zen_pcie_port_t *);
extern void turin_fabric_hotplug_port_unblock_training(zen_pcie_port_t *);
extern bool turin_fabric_hotplug_start(zen_iodie_t *);
extern void turin_fabric_pcie(zen_fabric_t *);
extern void turin_fabric_hide_bridge(zen_pcie_port_t *);
extern void turin_fabric_unhide_bridge(zen_pcie_port_t *);
extern void turin_fabric_init_pcie_port(zen_pcie_port_t *);
extern void turin_fabric_init_pcie_port_after_reconfig(zen_pcie_port_t *);
extern void turin_fabric_init_bridge(zen_pcie_port_t *);
extern void turin_fabric_init_pcie_straps(zen_pcie_core_t *);
extern void turin_fabric_ioms_iohc_disable_unused_pcie_bridges(zen_ioms_t *);
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
extern void turin_pcie_dbg_signal(void);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_FABRIC_IMPL_H */
