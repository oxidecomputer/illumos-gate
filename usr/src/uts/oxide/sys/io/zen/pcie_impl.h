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

#ifndef	_SYS_IO_ZEN_PCIE_IMPL_H
#define	_SYS_IO_ZEN_PCIE_IMPL_H

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/mutex.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/pcie.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The current maximum number of ports that can be attached to any
 * PCIe core in our supported Zen microarchitectures.
 */
#define	ZEN_PCIE_CORE_MAX_PORTS		8

typedef int (*zen_pcie_core_cb_f)(zen_pcie_core_t *, void *);
typedef int (*zen_pcie_port_cb_f)(zen_pcie_port_t *, void *);

extern zen_pcie_core_t *zen_fabric_find_pcie_core_by_lanes(zen_iodie_t *,
    uint16_t, uint16_t);

typedef enum zen_pcie_port_flag {
	/*
	 * Indicates that there is a corresponding zen_dxio_engine_t associated
	 * with this port and bridge.
	 */
	ZEN_PCIE_PORT_F_MAPPED		= 1 << 0,

	/*
	 * Indicates that this port's bridge has been hidden from visibility.
	 * When a port is not used, the associated brige is hidden.
	 */
	ZEN_PCIE_PORT_F_BRIDGE_HIDDEN	= 1 << 1,

	/*
	 * This port is hotplug-capable, and the associated bridge is being
	 * used for hotplug shenanigans. This means the bridge's slot state and
	 * controls are actually meaningful.
	 */
	ZEN_PCIE_PORT_F_HOTPLUG		= 1 << 2
} zen_pcie_port_flag_t;

typedef enum zen_pcie_core_flag {
	/*
	 * This is used to indicate that at least one engine and its associated
	 * port have been defined within this core.
	 */
	ZEN_PCIE_CORE_F_USED		= 1 << 0,

	/*
	 * This indicates that at least one engine mapped to this core is
	 * considered hotpluggable. This is important for making sure that we
	 * deal with the visibility of PCIe devices correctly.
	 */
	ZEN_PCIE_CORE_F_HAS_HOTPLUG	= 1 << 1,
} zen_pcie_core_flag_t;

/*
 * A PCIe port attached to a PCIe core.
 */
struct zen_pcie_port {
	/*
	 * Software-defined flags for the current port.  These are
	 * neither hardware defined, nor architecturally specific.
	 */
	zen_pcie_port_flag_t	zpp_flags;

	/*
	 * The port number of this port.
	 */
	uint8_t			zpp_portno;

	/*
	 * A device number associated with this port.
	 */
	uint8_t			zpp_device;

	/*
	 * The function associated with this port.
	 */
	uint8_t			zpp_func;

	/*
	 * The core that this port is attached to.
	 */
	zen_pcie_core_t		*zpp_core;

	/*
	 * A pointer to the microarchitecturally specific data for
	 * this PCIe port.
	 */
	void			*zpp_uarch_pcie_port;
};

struct zen_pcie_core {
	/*
	 * Software-defined flags for this core.
	 */
	zen_pcie_core_flag_t	zpc_flags;

	/*
	 * The core number of this PCIe core.
	 */
	uint8_t			zpc_coreno;

	/*
	 * The ports that are attached to this core.
	 */
	zen_pcie_port_t		zpc_ports[ZEN_PCIE_CORE_MAX_PORTS];

	/*
	 * The actual number of ports attached to this core.
	 */
	uint8_t			zpc_nports;

	/*
	 * Lane start and end constants, both physical and logical
	 * (DXIO).  Note that the DXIO lanes are common across micro
	 * architectures, even though the way one deals with the DXIO
	 * crossbar changes (e.g., via the SMU or via MPIO).
	 *
	 * These lane numbers are inclusive.
	 */
	uint16_t		zpc_dxio_lane_start;
	uint16_t		zpc_dxio_lane_end;
	uint16_t		zpc_phys_lane_start;
	uint16_t		zpc_phys_lane_end;

	/*
	 * A mutex for updating straps.
	 */
	kmutex_t		zpc_strap_lock;

	/*
	 * The IOMS this core is attached to.
	 */
	zen_ioms_t		*zpc_ioms;

	/*
	 * A pointer to the microarchitecturally specific data for
	 * this PCIe core.
	 */
	void			*zpc_uarch_pcie_core;
};


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PCIE_IMPL_H */
