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
 * Copyright 2025 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_FABRIC_IMPL_H
#define	_SYS_IO_MILAN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/x86_archext.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/milan/ccx_impl.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/nbif_impl.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/io/zen/dxio_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This defines what the maximum number of SoCs that are supported in Milan (and
 * Rome).
 */
#define	MILAN_FABRIC_MAX_SOCS		2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC. Since
 * Rome this has been 1. Previously on Naples this was 4. Because we do not work
 * on Naples based platforms, this is kept low (unlike the more general amdzen
 * nexus driver).
 */
#define	MILAN_IODIE_PER_SOC		1

/*
 * This is the number of IOHC instances that we know are supposed to exist per
 * die.
 */
#define	MILAN_IOHC_PER_IODIE		4

/*
 * This is the number of IOHC instances that we know are supposed to exist per
 * NBIO.
 */
#define	MILAN_IOHC_PER_NBIO		1

/*
 * Convenience macro to convert an IOHC number to the corresponding NBIO.
 * This is an identity mapping in Milan.
 */
#define	MILAN_NBIO_NUM(num)		((num) / MILAN_IOHC_PER_NBIO)

/*
 * The maximum number of PCIe cores in an IOHC. The IOHC has up to three cores,
 * but only the one with the bonus links has the third (core number 2).
 */
#define	MILAN_IOHC_MAX_PCIE_CORES	3
#define	MILAN_IOHC_BONUS_PCIE_CORENO	2

/*
 * The IOHC instance with the bonus PCIe core.
 */
#define	MILAN_NBIO_BONUS_IOHC		0

/*
 * There are supposed to be 23 digital power management (DPM) weights provided
 * by each Milan SMU.  Note that older processor families may have fewer, and
 * Naples also has more SMUs.
 */
#define	MILAN_MAX_DPM_WEIGHTS	23

/*
 * Milan uarch-specific initialization data for consumption by common Zen code.
 */
extern const zen_iohc_nbif_ports_t milan_pcie_int_ports[MILAN_IOHC_PER_IODIE];

/*
 * The Milan uarch-specific hooks for initial fabric topology initialization.
 */
extern uint8_t milan_fabric_ioms_nbio_num(uint8_t);
extern bool milan_fabric_smu_pptable_init(zen_fabric_t *, void *, size_t *);
extern void milan_fabric_smu_misc_init(zen_iodie_t *);
extern void milan_fabric_nbio_init(zen_nbio_t *);
extern void milan_fabric_ioms_init(zen_ioms_t *);
extern void milan_fabric_ioms_pcie_init(zen_ioms_t *);

/*
 * Milan uarch-specific initialization data for consumption by common Zen code.
 */
extern const uint8_t milan_nbif_nfunc[];
extern const zen_nbif_info_t
    milan_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS];

/*
 * These are the initialization points for the Milan Data Fabric, Northbridges,
 * PCIe, and related.
 */
extern void milan_fabric_init_tom(zen_ioms_t *, uint64_t, uint64_t, uint64_t);
extern void milan_fabric_disable_vga(zen_ioms_t *);
extern void milan_fabric_iohc_pci_ids(zen_ioms_t *);
extern void milan_fabric_pcie_refclk(zen_ioms_t *);
extern void milan_fabric_set_pci_to(zen_ioms_t *, uint16_t, uint16_t);
extern void milan_fabric_iohc_features(zen_ioms_t *);
extern void milan_fabric_nbio_features(zen_nbio_t *);
extern void milan_fabric_iohc_bus_num(zen_ioms_t *, uint8_t);
extern void milan_fabric_iohc_fch_link(zen_ioms_t *, bool);
extern void milan_fabric_iohc_arbitration(zen_ioms_t *);
extern void milan_fabric_nbio_arbitration(zen_nbio_t *);
extern void milan_fabric_nbif_arbitration(zen_nbif_t *);
extern void milan_fabric_sdp_control(zen_ioms_t *);
extern void milan_fabric_nbio_sdp_control(zen_nbio_t *);
extern void milan_fabric_nbif_syshub_dma(zen_nbif_t *);
extern void milan_fabric_iohc_clock_gating(zen_ioms_t *);
extern void milan_fabric_nbio_clock_gating(zen_nbio_t *);
extern void milan_fabric_nbif_clock_gating(zen_nbif_t *);
extern void milan_fabric_ioapic_clock_gating(zen_ioms_t *);
extern void milan_fabric_ioapic(zen_ioms_t *);
extern void milan_fabric_nbif_init(zen_nbif_t *);
extern void milan_fabric_nbif_dev_straps(zen_nbif_t *);
extern void milan_fabric_nbif_bridges(zen_ioms_t *);
extern void milan_fabric_pcie(zen_fabric_t *);
extern void milan_fabric_init_pcie_core(zen_pcie_core_t *);
extern void milan_fabric_init_bridge(zen_pcie_port_t *);
extern bool milan_fabric_pcie_port_is_trained(const zen_pcie_port_t *);
extern void milan_fabric_hide_bridge(zen_pcie_port_t *);
extern void milan_fabric_unhide_bridge(zen_pcie_port_t *);

extern void milan_smu_hotplug_port_data_init(zen_pcie_port_t *,
    zen_hotplug_table_t *);
extern bool milan_fabric_hotplug_smu_init(zen_iodie_t *);
extern void milan_fabric_hotplug_core_init(zen_pcie_core_t *);
extern void milan_fabric_hotplug_port_init(zen_pcie_port_t *);
extern void milan_fabric_hotplug_port_unblock_training(zen_pcie_port_t *);
extern bool milan_fabric_set_hotplug_flags(zen_iodie_t *);
extern bool milan_fabric_hotplug_start(zen_iodie_t *);

extern bool milan_fabric_pcie_hotplug_init(zen_fabric_t *);

extern void milan_iohc_enable_nmi(zen_ioms_t *);
extern void milan_iohc_nmi_eoi(zen_ioms_t *);

extern smn_reg_t milan_pcie_port_reg(const zen_pcie_port_t *const,
    const smn_reg_def_t);
extern smn_reg_t milan_pcie_core_reg(const zen_pcie_core_t *const,
    const smn_reg_def_t);
extern const zen_pcie_core_info_t *milan_pcie_core_info(const uint8_t, const
    uint8_t);
extern const zen_pcie_port_info_t *milan_pcie_port_info(const uint8_t, const
    uint8_t);
extern void milan_pcie_dbg_signal(void);

extern uint8_t milan_tile_smu_hp_id(const oxio_engine_t *);

/*
 * OXIO translations that are still Milan specific or rely on what are currently
 * Milan-specific data structures ala the smu_hotplug_table_t.
 */
extern const oxio_engine_t *milan_dxio_to_engine(zen_iodie_t *,
    const zen_dxio_fw_engine_t *);
extern void oxio_port_to_smu_hp(zen_pcie_port_t *, smu_hotplug_table_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_FABRIC_IMPL_H */
