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
 * Copyright 2024 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_FABRIC_IMPL_H
#define	_SYS_IO_MILAN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/x86_archext.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/milan/ccx_impl.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/nbif_impl.h>
#include <sys/io/milan/pcie_impl.h>
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
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	MILAN_IOMS_PER_IODIE		4

/*
 * The maximum number of PCIe cores in an NBIO IOMS. The IOMS has up to three
 * cores, but only the one with the bonus links has core number 2.
 */
#define	MILAN_IOMS_MAX_PCIE_CORES	3
#define	MILAN_IOMS_BONUS_PCIE_CORENO	2

/*
 * The IOMS instance with the bonus PCIe core.
 */
#define	MILAN_NBIO_BONUS_IOMS	0

/*
 * There are supposed to be 23 digital power management (DPM) weights provided
 * by each Milan SMU.  Note that older processor families may have fewer, and
 * Naples also has more SMUs.
 */
#define	MILAN_MAX_DPM_WEIGHTS	23

typedef struct milan_fabric milan_fabric_t;
typedef struct milan_ioms milan_ioms_t;
typedef struct milan_iodie milan_iodie_t;
typedef struct milan_soc milan_soc_t;

struct milan_ioms {
	milan_pcie_core_t	mio_pcie_cores[MILAN_IOMS_MAX_PCIE_CORES];
};

struct milan_iodie {
	milan_dxio_sm_state_t	mi_state;
	uint64_t		mi_dpm_weights[MILAN_MAX_DPM_WEIGHTS];
	milan_ioms_t		mi_ioms[MILAN_IOMS_PER_IODIE];
};

struct milan_soc {
	milan_iodie_t		ms_iodies[MILAN_IODIE_PER_SOC];
};

struct milan_fabric {
	milan_hotplug_t	mf_hotplug;
	milan_soc_t	mf_socs[MILAN_FABRIC_MAX_SOCS];
};

/*
 * The Milan uarch-specific hooks for initial fabric topology initialization.
 */
extern void milan_fabric_topo_init(zen_fabric_t *);
extern void milan_fabric_soc_init(zen_soc_t *);
extern void milan_fabric_iodie_init(zen_iodie_t *);
extern void milan_fabric_smu_misc_init(zen_iodie_t *);
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
extern void milan_fabric_iohc_bus_num(zen_ioms_t *, uint8_t);
extern void milan_fabric_iohc_fch_link(zen_ioms_t *, bool);
extern void milan_fabric_iohc_arbitration(zen_ioms_t *);
extern void milan_fabric_nbif_arbitration(zen_nbif_t *);
extern void milan_fabric_sdp_control(zen_ioms_t *);
extern void milan_fabric_nbif_syshub_dma(zen_nbif_t *);
extern void milan_fabric_ioapic(zen_ioms_t *);
extern void milan_fabric_nbif_dev_straps(zen_nbif_t *);
extern void milan_fabric_nbif_bridges(zen_ioms_t *);
extern void milan_fabric_pcie(zen_fabric_t *);

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

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_FABRIC_IMPL_H */
