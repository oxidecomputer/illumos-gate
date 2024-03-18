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

#ifndef _SYS_IO_GENOA_PCIE_IMPL_H
#define	_SYS_IO_GENOA_PCIE_IMPL_H

#include <sys/types.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/mpio_impl.h>
#include <sys/io/genoa/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each of the normal PCIe cores is an RC9x16: up to 9 ports across 16 lanes.
 * The PCIe core that shares lanes with WAFL is an RC4x4.
 */
#define	GENOA_PCIE_CORE_MAX_PORTS	9
#define	GENOA_PCIE_CORE_WAFL_NPORTS	4

typedef enum genoa_pcie_port_flag {
	/*
	 * Indicates that there is a corresponding zen_mpio_engine_t associated
	 * with this port and bridge.
	 */
	GENOA_PCIE_PORT_F_MAPPED	= 1 << 0,
	/*
	 * Indicates that this port's bridge has been hidden from visibility.
	 * When a port is not used, the associated brige is hidden.
	 */
	GENOA_PCIE_PORT_F_BRIDGE_HIDDEN	= 1 << 1,
	/*
	 * This port is hotplug-capable, and the associated bridge is being
	 * used for hotplug shenanigans. This means the bridge's slot state and
	 * controls are actually meaningful.
	 */
	GENOA_PCIE_PORT_F_HOTPLUG	= 1 << 2
} genoa_pcie_port_flag_t;

typedef enum genoa_pcie_core_flag {
	/*
	 * This is used to indicate that at least one engine and its associated
	 * port have been defined within this core.
	 */
	GENOA_PCIE_CORE_F_USED		= 1 << 0,
	/*
	 * This indicates that at least one engine mapped to this core is
	 * considered hotpluggable. This is importnat for making sure that we
	 * deal with the visibility of PCIe devices correctly.
	 */
	GENOA_PCIE_CORE_F_HAS_HOTPLUG	= 1 << 1,
} genoa_pcie_core_flag_t;

/*
 * These stages of configuration are referred to in the per-port and per-RC
 * register storage structures, which provide a debugging facility to help
 * understand what both firmware and software have done to these registers over
 * time.  They do not control any software behaviour other than in mdb.  See the
 * theory statement in genoa_fabric.c for the definitions of these stages.
 */
typedef enum genoa_pcie_config_stage {
	GPCS_PRE_DXIO_INIT,
	GPCS_DXIO_SM_START,
	GPCS_DXIO_SM_MAPPED,
	GPCS_DXIO_SM_MAPPED_RESUME,
	GPCS_DXIO_SM_CONFIGURED,
	GPCS_DXIO_SM_CONFIGURED_RESUME,
	GPCS_DXIO_SM_PERST,
	GPCS_DXIO_SM_PERST_RESUME,
	GPCS_DXIO_SM_DONE,
	GPCS_PRE_HOTPLUG,
	GPCS_POST_HOTPLUG,
	GPCS_USER_DIRECTED,
	GPCS_NUM_STAGES
} genoa_pcie_config_stage_t;

typedef struct genoa_pcie_reg_dbg {
	const char		*gprd_name;
	smn_reg_def_t		gprd_def;
	uint32_t		gprd_val[GPCS_NUM_STAGES];
	hrtime_t		gprd_ts[GPCS_NUM_STAGES];
} genoa_pcie_reg_dbg_t;

typedef struct genoa_pcie_dbg {
	genoa_pcie_config_stage_t	gpd_last_stage;
	uint32_t			gpd_nregs;
	genoa_pcie_reg_dbg_t		gpd_regs[];
} genoa_pcie_dbg_t;

#define	GENOA_PCIE_DBG_SIZE(_nr)	\
	(sizeof (genoa_pcie_dbg_t) + (_nr) * sizeof (genoa_pcie_reg_dbg_t))

#ifdef	DEBUG
extern const genoa_pcie_reg_dbg_t genoa_pcie_core_dbg_regs[];
extern const size_t genoa_pcie_core_dbg_nregs;
extern const genoa_pcie_reg_dbg_t genoa_pcie_port_dbg_regs[];
extern const size_t genoa_pcie_port_dbg_nregs;
#endif	/* DEBUG */

struct genoa_pcie_port {
	genoa_pcie_port_flag_t		gpp_flags;
	uint8_t				gpp_portno;
	uint8_t				gpp_device;
	uint8_t				gpp_func;
	zen_mpio_engine_t		*gpp_engine;
	smu_hotplug_type_t		gpp_hp_type;
	uint16_t			gpp_hp_slotno;
	uint32_t			gpp_hp_smu_mask;
	genoa_pcie_dbg_t		*gpp_dbg;
	genoa_pcie_core_t		*gpp_core;
};

struct genoa_pcie_core {
	genoa_pcie_core_flag_t	gpc_flags;
	uint8_t			gpc_coreno;
	uint8_t			gpc_sdp_unit;
	uint8_t			gpc_sdp_port;
	uint8_t			gpc_nports;
	uint16_t		gpc_dxio_lane_start;
	uint16_t		gpc_dxio_lane_end;
	uint16_t		gpc_phys_lane_start;
	uint16_t		gpc_phys_lane_end;
	kmutex_t		gpc_strap_lock;
	genoa_pcie_dbg_t	*gpc_dbg;
	genoa_pcie_port_t	gpc_ports[GENOA_PCIE_CORE_MAX_PORTS];
	genoa_ioms_t		*gpc_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_PCIE_IMPL_H */
