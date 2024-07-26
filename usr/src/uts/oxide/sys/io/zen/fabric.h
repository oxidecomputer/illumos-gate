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

typedef enum zen_ioms_flag {
	ZEN_IOMS_F_HAS_FCH	= 1 << 0,
	ZEN_IOMS_F_HAS_WAFL	= 1 << 1
} zen_ioms_flag_t;

typedef enum zen_iodie_flag {
	ZEN_IODIE_F_PRIMARY	= 1 << 0
} zen_iodie_flag_t;

/*
 * The entry point for early boot which intializes the general fabric
 * topology.
 */
extern void zen_fabric_topo_init(void);

/*
 * Retrieve the base physical address of the PCIe ECAM region.
 */
extern uint64_t zen_fabric_ecam_base(void);

typedef struct zen_fabric_ops {
	void		(*zfo_topo_init)(void);
} zen_fabric_ops_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_H */
