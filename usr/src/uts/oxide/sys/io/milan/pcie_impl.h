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
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_PCIE_IMPL_H
#define	_SYS_IO_MILAN_PCIE_IMPL_H

#include <sys/types.h>
#include <sys/io/milan/fabric.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each of the normal PCIe cores is an RC8x16: up to 8 ports across 16 lanes.
 * The PCIe core that shares lanes with WAFL is an RC2x2.
 */
#define	MILAN_PCIE_CORE_MAX_PORTS	8
#define	MILAN_PCIE_CORE_WAFL_NPORTS	2

typedef enum milan_pcie_port_flag {
	/*
	 * Indicates that there is a corresponding zen_dxio_engine_t associated
	 * with this port and bridge.
	 */
	MILAN_PCIE_PORT_F_MAPPED	= 1 << 0,
	/*
	 * Indicates that this port's bridge has been hidden from visibility.
	 * When a port is not used, the associated brige is hidden.
	 */
	MILAN_PCIE_PORT_F_BRIDGE_HIDDEN	= 1 << 1,
	/*
	 * This port is hotplug-capable, and the associated bridge is being
	 * used for hotplug shenanigans. This means the bridge's slot state and
	 * controls are actually meaningful.
	 */
	MILAN_PCIE_PORT_F_HOTPLUG	= 1 << 2
} milan_pcie_port_flag_t;

typedef enum milan_pcie_core_flag {
	/*
	 * This is used to indicate that at least one engine and its associated
	 * port have been defined within this core.
	 */
	MILAN_PCIE_CORE_F_USED		= 1 << 0,
	/*
	 * This indicates that at least one engine mapped to this core is
	 * considered hotpluggable. This is importnat for making sure that we
	 * deal with the visibility of PCIe devices correctly.
	 */
	MILAN_PCIE_CORE_F_HAS_HOTPLUG	= 1 << 1,
} milan_pcie_core_flag_t;

struct milan_pcie_port {
	milan_pcie_port_flag_t		mpp_flags;
	uint8_t				mpp_portno;
	uint8_t				mpp_device;
	uint8_t				mpp_func;
	zen_dxio_engine_t		*mpp_engine;
	smu_hotplug_type_t		mpp_hp_type;
	uint16_t			mpp_hp_slotno;
	uint32_t			mpp_hp_smu_mask;
	milan_pcie_core_t		*mpp_core;
};

struct milan_pcie_core {
	milan_pcie_core_flag_t	mpc_flags;
	uint8_t			mpc_coreno;
	uint8_t			mpc_sdp_unit;
	uint8_t			mpc_sdp_port;
	uint8_t			mpc_nports;
	uint16_t		mpc_dxio_lane_start;
	uint16_t		mpc_dxio_lane_end;
	uint16_t		mpc_phys_lane_start;
	uint16_t		mpc_phys_lane_end;
	kmutex_t		mpc_strap_lock;
	milan_pcie_port_t	mpc_ports[MILAN_PCIE_CORE_MAX_PORTS];
	milan_ioms_t		*mpc_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_PCIE_IMPL_H */
