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

#ifndef	_SYS_IO_ZEN_FABRIC_H
#define	_SYS_IO_ZEN_FABRIC_H

/*
 * Definitions for types, functions and constants used in managing Zen IO and
 * data fabrics common across microarchitectures.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/plat/pci_prd.h>
#include <sys/io/zen/smn.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_ioms zen_ioms_t;
typedef struct zen_nbif zen_nbif_t;
typedef struct zen_iodie zen_iodie_t;
typedef struct zen_soc zen_soc_t;
typedef struct zen_fabric zen_fabric_t;
struct memlist;
typedef struct zen_pcie_core zen_pcie_core_t;
typedef struct zen_pcie_port zen_pcie_port_t;
typedef struct zen_pcie_dbg zen_pcie_dbg_t;
typedef struct zen_pcie_reg_dbg zen_pcie_reg_dbg_t;
typedef struct zen_pcie_strap_setting zen_pcie_strap_setting_t;

/*
 * Generic resource types that can be routed via an IOMS.
 */
typedef enum zen_ioms_rsrc {
	ZIR_NONE,
	ZIR_PCI_LEGACY,
	ZIR_PCI_MMIO,
	ZIR_PCI_PREFETCH,
	ZIR_PCI_BUS,
	ZIR_GEN_LEGACY,
	ZIR_GEN_MMIO
} zen_ioms_rsrc_t;

/*
 * Walks IOMSes and applies a callback.  While most walkers are hidden as part
 * of an implementation, this is called from common code.  A callback that
 * returns a non-zero value terminates the walk.
 *
 */
typedef int (*zen_ioms_cb_f)(zen_ioms_t *, void *);
extern int zen_walk_ioms(zen_ioms_cb_f, void *);

typedef enum zen_ioms_flag {
	ZEN_IOMS_F_HAS_FCH	= 1 << 0,
	ZEN_IOMS_F_HAS_BONUS	= 1 << 1,
	ZEN_IOMS_F_HAS_NBIF	= 1 << 2
} zen_ioms_flag_t;

typedef enum zen_iodie_flag {
	ZEN_IODIE_F_PRIMARY	= 1 << 0
} zen_iodie_flag_t;

/*
 * Some platforms have more than one type of IOHC with differences in
 * connectivity, downstream components, available register instances or
 * even register offsets. Turin is the first platform that has this split and
 * one of its IOHC kinds is larger than the other, hence the naming below.
 */
typedef enum {
	ZEN_IOHCT_LARGE		= 0,
	ZEN_IOHCT_SMALL
} zen_iohc_type_t;

/*
 * Returns the set of flags set on the given IOMS.
 */
extern zen_ioms_flag_t zen_ioms_flags(const zen_ioms_t *const);

/*
 * Returns a pointer to the IO die the given IOMS is connected to.
 */
extern zen_iodie_t *zen_ioms_iodie(const zen_ioms_t *const);

/*
 * Returns the node ID associated corresponding to this die.
 */
extern uint8_t zen_iodie_node_id(const zen_iodie_t *const);

/*
 * Returns the set of flags set on the given IO die.
 */
extern zen_iodie_flag_t zen_iodie_flags(const zen_iodie_t *const);

/*
 * The entry point for early boot which intializes the general fabric
 * topology.
 */
extern void zen_fabric_topo_init(void);

/*
 * Called from startup() to initialize the fabric up to getting PCIe ready.
 */
extern void zen_fabric_init(void);

/*
 * Retrieve the base physical address of the PCIe ECAM region.
 */
extern uint64_t zen_fabric_ecam_base(void);

/*
 * Given a PCI resource type and a PCI bus number, transfers unallocated
 * resources of that type from an IOMS root port to PCI, returning a memlist
 * with the transferred resources.  Returns NULL if no resources are available.
 * For things that are not PCI, use zen_fabric_gen_subsume, instead.
 */
extern struct memlist *zen_fabric_pci_subsume(uint32_t, pci_prd_rsrc_t);

/*
 * Given an IOMS instance and a resource type, transfers available resources of
 * that type to and returns a new memlist.  Returns NULL if no such resources
 * are available.  This is intended for things that are not PCI, such as legacy
 * IO and MMIO spaces; for PCI use zen_fabric_pci_subsume.
 */
extern struct memlist *zen_fabric_gen_subsume(zen_ioms_t *, zen_ioms_rsrc_t);

/*
 * Enable the NMI functionality in the IOHC to allow external devices (i.e., the
 * SP) to signal an NMI via the dedicated NMI_SYNCFLOOD_L pin.
 */
extern void zen_fabric_enable_nmi(void);

/*
 * If an NMI originated from the IOHC, clear it and indicate EOI to receive
 * subsequent NMIs.
 */
extern void zen_fabric_nmi_eoi(void);

/*
 * Copies the brand string into the given output buffer.  The argument and
 * return value semantics match those of snprintf(9F).
 */
extern size_t zen_fabric_thread_get_brandstr(const zen_thread_t *, char *,
    size_t);

/*
 * No-op routine for platforms that do not support DPM weights.
 */
extern void zen_fabric_thread_get_dpm_weights_noop(const zen_thread_t *,
    const uint64_t **, uint32_t *);

/*
 * Read and write PCIe core and port registers.
 */
extern uint32_t zen_pcie_core_read(zen_pcie_core_t *, const smn_reg_t);
extern void zen_pcie_core_write(zen_pcie_core_t *, const smn_reg_t,
    const uint32_t);
extern uint32_t zen_pcie_port_read(zen_pcie_port_t *, const smn_reg_t);
extern void zen_pcie_port_write(zen_pcie_port_t *, const smn_reg_t,
    const uint32_t);

#define	ZEN_IODIE_MATCH_ANY	UINT8_MAX
extern void zen_pcie_populate_dbg(zen_fabric_t *, uint32_t, uint8_t);

extern bool zen_fabric_pcie_strap_matches(const zen_pcie_core_t *, uint8_t,
    const zen_pcie_strap_setting_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_H */
