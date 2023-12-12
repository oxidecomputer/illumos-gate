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
#include <sys/io/genoa/dxio_impl.h>
#include <sys/io/genoa/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each of the normal PCIe cores is an RC8x16: up to 8 ports across 16 lanes.
 * The PCIe core that shares lanes with WAFL is an RC2x2.
 */
#define	GENOA_PCIE_CORE_MAX_PORTS	8
#define	GENOA_PCIE_CORE_WAFL_NPORTS	2

typedef enum genoa_pcie_port_flag {
	/*
	 * Indicates that there is a corresponding zen_dxio_engine_t associated
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
	MPCS_PRE_DXIO_INIT,
	MPCS_DXIO_SM_START,
	MPCS_DXIO_SM_MAPPED,
	MPCS_DXIO_SM_MAPPED_RESUME,
	MPCS_DXIO_SM_CONFIGURED,
	MPCS_DXIO_SM_CONFIGURED_RESUME,
	MPCS_DXIO_SM_PERST,
	MPCS_DXIO_SM_PERST_RESUME,
	MPCS_DXIO_SM_DONE,
	MPCS_PRE_HOTPLUG,
	MPCS_POST_HOTPLUG,
	MPCS_USER_DIRECTED,
	MPCS_NUM_STAGES
} genoa_pcie_config_stage_t;

typedef struct genoa_pcie_reg_dbg {
	const char		*mprd_name;
	smn_reg_def_t		mprd_def;
	uint32_t		mprd_val[MPCS_NUM_STAGES];
	hrtime_t		mprd_ts[MPCS_NUM_STAGES];
} genoa_pcie_reg_dbg_t;

typedef struct genoa_pcie_dbg {
	genoa_pcie_config_stage_t	mpd_last_stage;
	uint32_t			mpd_nregs;
	genoa_pcie_reg_dbg_t		mpd_regs[];
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
	genoa_pcie_port_flag_t		mpp_flags;
	uint8_t				mpp_portno;
	uint8_t				mpp_device;
	uint8_t				mpp_func;
	zen_dxio_engine_t		*mpp_engine;
	smu_hotplug_type_t		mpp_hp_type;
	uint16_t			mpp_hp_slotno;
	uint32_t			mpp_hp_smu_mask;
	genoa_pcie_dbg_t		*mpp_dbg;
	genoa_pcie_core_t		*mpp_core;
};

struct genoa_pcie_core {
	genoa_pcie_core_flag_t	mpc_flags;
	uint8_t			mpc_coreno;
	uint8_t			mpc_sdp_unit;
	uint8_t			mpc_sdp_port;
	uint8_t			mpc_nports;
	uint16_t		mpc_dxio_lane_start;
	uint16_t		mpc_dxio_lane_end;
	uint16_t		mpc_phys_lane_start;
	uint16_t		mpc_phys_lane_end;
	kmutex_t		mpc_strap_lock;
	genoa_pcie_dbg_t	*mpc_dbg;
	genoa_pcie_port_t	mpc_ports[GENOA_PCIE_CORE_MAX_PORTS];
	genoa_ioms_t		*mpc_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_PCIE_IMPL_H */
