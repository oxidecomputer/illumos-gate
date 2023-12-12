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

#ifndef _SYS_IO_GENOA_FABRIC_H
#define	_SYS_IO_GENOA_FABRIC_H

/*
 * Definitions that allow us to access the Genoa fabric. This consists of the
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
struct genoa_ioms;
struct genoa_iodie;
struct genoa_soc;
struct genoa_fabric;

typedef struct genoa_ioms genoa_ioms_t;
typedef struct genoa_iodie genoa_iodie_t;
typedef struct genoa_soc genoa_soc_t;
typedef struct genoa_fabric genoa_fabric_t;

typedef enum genoa_ioms_flag {
	GENOA_IOMS_F_HAS_FCH	= 1 << 0,
	GENOA_IOMS_F_HAS_WAFL	= 1 << 1
} genoa_ioms_flag_t;

typedef enum genoa_iodie_flag {
	GENOA_IODIE_F_PRIMARY	= 1 << 0
} genoa_iodie_flag_t;

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
extern void genoa_fabric_topo_init(void);

/*
 * Retrieve the base physical address of the PCIe ECAM region.
 */
extern uint64_t genoa_fabric_ecam_base(void);

/*
 * This is the primary initialization point for the Genoa Data Fabric,
 * Northbridges, PCIe, and related.
 */
extern void genoa_fabric_init(void);

extern struct memlist *genoa_fabric_pci_subsume(uint32_t, pci_prd_rsrc_t);
extern struct memlist *genoa_fabric_gen_subsume(genoa_ioms_t *, ioms_rsrc_t);

/* Walker callback function types */
typedef int (*genoa_iodie_cb_f)(genoa_iodie_t *, void *);
typedef int (*genoa_ioms_cb_f)(genoa_ioms_t *, void *);

extern int genoa_walk_iodie(genoa_iodie_cb_f, void *);
extern int genoa_walk_ioms(genoa_ioms_cb_f, void *);

extern genoa_ioms_flag_t genoa_ioms_flags(const genoa_ioms_t *const);
extern genoa_iodie_t *genoa_ioms_iodie(const genoa_ioms_t *const);
extern smn_reg_t genoa_ioms_reg(const genoa_ioms_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t genoa_ioms_read(genoa_ioms_t *, const smn_reg_t);
extern void genoa_ioms_write(genoa_ioms_t *, const smn_reg_t, const uint32_t);

extern genoa_iodie_flag_t genoa_iodie_flags(const genoa_iodie_t *const);
extern uint8_t genoa_iodie_node_id(const genoa_iodie_t *const);
extern smn_reg_t genoa_iodie_reg(const genoa_iodie_t *const,
    const smn_reg_def_t, const uint16_t);
extern uint32_t genoa_iodie_read(genoa_iodie_t *, const smn_reg_t);
extern void genoa_iodie_write(genoa_iodie_t *, const smn_reg_t, const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_FABRIC_H */
