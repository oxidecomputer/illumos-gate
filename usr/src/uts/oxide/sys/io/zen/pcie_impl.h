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

#ifndef	_SYS_IO_ZEN_PCIE_IMPL_H
#define	_SYS_IO_ZEN_PCIE_IMPL_H

/*
 * Definitions that allow us to access the PCIe fabric.
 */

#include <sys/memlist.h>
#include <sys/plat/pci_prd.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/firmware_impl.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Maximums for the numbers of cores and WAFL ports across
 * the microarchitecture family.
 */
#define	ZEN_PCIE_CORE_MAX_PORTS		8
#define	ZEN_PCIE_CORE_WAFL_NPORTS	2

typedef enum zen_pcie_port_flag {
	/*
	 * Indicates that there is a corresponding engine associated
	 * with this port and bridge.
	 */
	ZEN_PCIE_PORT_F_MAPPED	= 1 << 0,
	/*
	 * Indicates that this port's bridge is not used, and has
	 * therefore been hidden from visibility.
	 */
	ZEN_PCIE_PORT_F_BRIDGE_HIDDEN	= 1 << 1,
	/*
	 * This port is hotplug-capable, and the associated bridge is being
	 * used for hotplug shenanigans. This means the bridge's slot state and
	 * controls are actually meaningful.
	 */
	ZEN_PCIE_PORT_F_HOTPLUG	= 1 << 2
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

typedef enum zen_dxio_engine_type {
	ZEN_FW_ENGINE_UNUSED	= 0x00,
	ZEN_FW_ENGINE_PCIE	= 0x01,
	ZEN_FW_ENGINE_USB	= 0x02,
	ZEN_FW_ENGINE_SATA	= 0x03,
	ZEN_FW_ENGINE_ETH	= 0x10
} zen_dxio_engine_type_t;

#ifdef	NADA_DEBUG
extern const milan_pcie_reg_dbg_t milan_pcie_core_dbg_regs[];
extern const size_t milan_pcie_core_dbg_nregs;
extern const milan_pcie_reg_dbg_t milan_pcie_port_dbg_regs[];
extern const size_t milan_pcie_port_dbg_nregs;
#endif	/* DEBUG */

struct zen_pcie_port {
	zen_pcie_port_flag_t	zpp_flags;
	uint8_t			zpp_portno;
	uint8_t			zpp_device;
	uint8_t			zpp_func;
	void			*zpp_engine;
	zen_fw_hotplug_type_t	zpp_hp_type;
	uint16_t		zpp_hp_slotno;
	uint32_t		zpp_hp_smu_mask;
	void			*zpp_dbg;
	zen_pcie_core_t		*zpp_core;
};

struct zen_pcie_core {
	zen_pcie_core_flag_t	zpc_flags;
	uint8_t			zpc_coreno;
	uint8_t			zpc_sdp_unit;
	uint8_t			zpc_sdp_port;
	uint8_t			zpc_nports;
	uint16_t		zpc_dxio_lane_start;
	uint16_t		zpc_dxio_lane_end;
	uint16_t		zpc_phys_lane_start;
	uint16_t		zpc_phys_lane_end;
	kmutex_t		zpc_strap_lock;
	void			*zpc_dbg;
	zen_pcie_port_t		zpc_ports[ZEN_PCIE_CORE_MAX_PORTS];
	zen_ioms_t		*zpc_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PCIE_IMPL_H */
