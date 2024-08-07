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

#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/ddi.h>
#include <sys/ddidmareq.h>
#include <sys/plat/pci_prd.h>

#include <sys/io/zen/ccx.h>

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
typedef struct zen_iodie zen_iodie_t;
typedef struct zen_soc zen_soc_t;
typedef struct zen_fabric zen_fabric_t;

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


struct memlist;

/* Walker callback function type */
typedef int (*zen_ioms_cb_f)(zen_ioms_t *, void *);

/*
 * Walks IOMSes and applies a callback.
 */
extern int zen_walk_ioms(zen_ioms_cb_f, void *);

typedef enum zen_ioms_flag {
	ZEN_IOMS_F_HAS_FCH	= 1 << 0,
	ZEN_IOMS_F_HAS_WAFL	= 1 << 1
} zen_ioms_flag_t;

/*
 * Returns the set of flags set on the given IOMS.
 */
extern zen_ioms_flag_t zen_ioms_flags(const zen_ioms_t *const);

/*
 * Returns a pointer to the IO die the given IOMS is connected to.
 */
extern zen_iodie_t *zen_ioms_iodie(const zen_ioms_t *const);

typedef enum zen_iodie_flag {
	ZEN_IODIE_F_PRIMARY	= 1 << 0
} zen_iodie_flag_t;

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
 * Retrieve the base physical address of the PCIe ECAM region.
 */
extern uint64_t zen_fabric_ecam_base(void);

/*
 * Memlist subsumption.
 */
extern struct memlist *zen_fabric_pci_subsume(uint32_t, pci_prd_rsrc_t);
extern struct memlist *zen_fabric_gen_subsume(zen_ioms_t *, zen_ioms_rsrc_t);

/*
 * Accessors for IOMS registers.
 */
extern smn_reg_t zen_ioms_reg(const zen_ioms_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t zen_ioms_read(zen_ioms_t *, const smn_reg_t);
extern void zen_ioms_write(zen_ioms_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for IO die registers.
 */
extern smn_reg_t zen_iodie_reg(const zen_iodie_t *const,
    const smn_reg_def_t, const uint16_t);
extern uint32_t zen_iodie_read(zen_iodie_t *, const smn_reg_t);
extern void zen_iodie_write(zen_iodie_t *, const smn_reg_t, const uint32_t);

/*
 * Externally visible operations called from common code.
 */
typedef struct zen_fabric_ops {
	void		(*zfo_fabric_init)(void);
	void		(*zfo_enable_nmi)(void);
	void		(*zfo_nmi_eoi)(void);

	/*
	 * The following functions are uarch-specific and are called from
	 * zen_fabric_topo_init() to initialize the fabric topology.
	 */
	void		(*zfo_topo_init)(zen_fabric_t *);
	void		(*zfo_soc_init)(zen_soc_t *, zen_iodie_t *);
	void		(*zfo_ccx_init)(zen_ccd_t *, zen_ccx_t *, uint32_t);
	void		(*zfo_ioms_init)(zen_ioms_t *);
} zen_fabric_ops_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_H */
