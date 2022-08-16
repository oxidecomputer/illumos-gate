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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
struct milan_ioms;
struct milan_iodie;
struct milan_soc;
struct milan_fabric;

typedef struct milan_ioms milan_ioms_t;
typedef struct milan_iodie milan_iodie_t;
typedef struct milan_soc milan_soc_t;
typedef struct milan_fabric milan_fabric_t;

typedef enum milan_ioms_flag {
	MILAN_IOMS_F_HAS_FCH	= 1 << 0,
	MILAN_IOMS_F_HAS_WAFL	= 1 << 1
} milan_ioms_flag_t;

typedef enum milan_iodie_flag {
	MILAN_IODIE_F_PRIMARY	= 1 << 0
} milan_iodie_flag_t;

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
extern struct memlist *milan_fabric_gen_subsume(milan_ioms_t *, ioms_rsrc_t);

/* Walker callback function types */
typedef int (*milan_iodie_cb_f)(milan_iodie_t *, void *);
typedef int (*milan_ioms_cb_f)(milan_ioms_t *, void *);

extern int milan_walk_iodie(milan_iodie_cb_f, void *);
extern int milan_walk_ioms(milan_ioms_cb_f, void *);

extern milan_ioms_flag_t milan_ioms_flags(const milan_ioms_t *const);
extern milan_iodie_t *milan_ioms_iodie(const milan_ioms_t *const);
extern smn_reg_t milan_ioms_reg(const milan_ioms_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t milan_ioms_read(milan_ioms_t *, const smn_reg_t);
extern void milan_ioms_write(milan_ioms_t *, const smn_reg_t, const uint32_t);

extern milan_iodie_flag_t milan_iodie_flags(const milan_iodie_t *const);
extern uint8_t milan_iodie_node_id(const milan_iodie_t *const);
extern smn_reg_t milan_iodie_reg(const milan_iodie_t *const,
    const smn_reg_def_t, const uint16_t);
extern uint32_t milan_iodie_read(milan_iodie_t *, const smn_reg_t);
extern void milan_iodie_write(milan_iodie_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_FABRIC_H */
