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

#ifndef _SYS_IO_ZEN_FABRIC_H
#define	_SYS_IO_ZEN_FABRIC_H

/*
 * Definitions that allow us to access the Zen fabric.  The fabric
 * consists of the data fabric, northbridges, SMN, and more.
 *
 * These are generic system definitions shared across variants of the
 * microarchitecture, including Milan, Genoa, and Turin.
 */

#include <sys/memlist.h>
#include <sys/plat/pci_prd.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
struct zen_hotplug;
struct zen_ioms;
struct zen_iodie;
struct zen_nbif;
struct zen_nbif_func;
struct zen_soc;
struct zen_fabric;

typedef struct zen_hotplug zen_hotplug_t;
typedef struct zen_fw_engine zen_fw_engine_t;
typedef struct zen_ioms zen_ioms_t;
typedef struct zen_iodie zen_iodie_t;
typedef struct zen_nbif zen_nbif_t;
typedef struct zen_nbif_func milan_nbif_func_t;
typedef struct zen_soc zen_soc_t;
typedef struct zen_fabric zen_fabric_t;

enum milan_nbif_func_type;
typedef enum zen_nbif_func_type zen_nbif_func_type_t;

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
struct zen_pcie_core;
struct zen_pcie_port;

typedef struct zen_pcie_core zen_pcie_core_t;
typedef struct zen_pcie_port zen_pcie_port_t;

extern uint8_t zen_nbio_n_pcie_cores(const uint8_t);
extern uint8_t zen_pcie_core_n_ports(const uint8_t);

typedef enum zen_ioms_flag {
	ZEN_IOMS_F_HAS_FCH	= 1 << 0,
	ZEN_IOMS_F_HAS_WAFL	= 1 << 1,
} zen_ioms_flag_t;

typedef enum zen_iodie_flag {
	ZEN_IODIE_F_PRIMARY	= 1 << 0,
} zen_iodie_flag_t;

/*
 * Generic resource types that can be routed via an IOMS.
 */
typedef enum ioms_rsrc {
	IR_NONE,
	IR_PCI_LEGACY,
	IR_PCI_MMIO,
	IR_PCI_PREFETCH,
	IR_PCI_BUS,
	IR_GEN_LEGACY,
	IR_GEN_MMIO
} ioms_rsrc_t;

/*
 * This is an entry point used in early boot, after we have PCIe
 * configuration space set up, that loads information about the
 * actual system topology.
 */
extern void zen_fabric_topo_init(void);

/*
 * Retrieve the base physical address of the PCIe ECAM region.
 */
extern uint64_t zen_fabric_ecam_base(void);

/*
 * This is the primary initialization point for the Zen Data Fabric,
 * Northbridges, PCIe, and related.
 */
extern void zen_fabric_init(void);

extern struct memlist *zen_fabric_pci_subsume(uint32_t, pci_prd_rsrc_t);
extern struct memlist *zen_fabric_gen_subsume(zen_ioms_t *, ioms_rsrc_t);

extern zen_ioms_flag_t zen_ioms_flags(const zen_ioms_t *const);
extern zen_iodie_t *zen_ioms_iodie(const zen_ioms_t *const);
extern smn_reg_t zen_ioms_reg(const zen_ioms_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t zen_ioms_read(zen_ioms_t *, const smn_reg_t);
extern void zen_ioms_write(zen_ioms_t *, const smn_reg_t, const uint32_t);

extern zen_iodie_flag_t milan_iodie_flags(const zen_iodie_t *const);
extern uint8_t zen_iodie_node_id(const zen_iodie_t *const);
extern smn_reg_t zen_iodie_reg(const zen_iodie_t *const,
    const smn_reg_def_t, const uint16_t);
extern uint32_t zen_iodie_read(zen_iodie_t *, const smn_reg_t);
extern void zen_iodie_write(zen_iodie_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_H */
