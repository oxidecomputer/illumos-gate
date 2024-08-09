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

#ifndef _SYS_IO_MILAN_PCIE_IMPL_H
#define	_SYS_IO_MILAN_PCIE_IMPL_H

#include <sys/types.h>
#include <sys/io/zen/pcie_impl.h>
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

/*
 * These stages of configuration are referred to in the per-port and per-RC
 * register storage structures, which provide a debugging facility to help
 * understand what both firmware and software have done to these registers over
 * time.  They do not control any software behaviour other than in mdb.  See the
 * theory statement in milan_fabric.c for the definitions of these stages.
 */
typedef enum milan_pcie_config_stage {
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
} milan_pcie_config_stage_t;

typedef struct milan_pcie_reg_dbg {
	const char		*mprd_name;
	smn_reg_def_t		mprd_def;
	uint32_t		mprd_val[MPCS_NUM_STAGES];
	hrtime_t		mprd_ts[MPCS_NUM_STAGES];
} milan_pcie_reg_dbg_t;

typedef struct milan_pcie_dbg {
	milan_pcie_config_stage_t	mpd_last_stage;
	uint32_t			mpd_nregs;
	milan_pcie_reg_dbg_t		mpd_regs[];
} milan_pcie_dbg_t;

#define	MILAN_PCIE_DBG_SIZE(_nr)	\
	(sizeof (milan_pcie_dbg_t) + (_nr) * sizeof (milan_pcie_reg_dbg_t))

#ifdef	DEBUG
extern const milan_pcie_reg_dbg_t milan_pcie_core_dbg_regs[];
extern const size_t milan_pcie_core_dbg_nregs;
extern const milan_pcie_reg_dbg_t milan_pcie_port_dbg_regs[];
extern const size_t milan_pcie_port_dbg_nregs;
#endif	/* DEBUG */

/*
 * The PCIe port data specific to the Milan microarchitecture.
 */
struct milan_pcie_port {
	zen_dxio_engine_t	*mpp_engine;
	smu_hotplug_type_t	mpp_hp_type;
	uint16_t		mpp_hp_slotno;
	uint32_t		mpp_hp_smu_mask;
	milan_pcie_dbg_t	*mpp_dbg;
};

struct milan_pcie_core {
	uint8_t			mpc_sdp_unit;
	uint8_t			mpc_sdp_port;
	milan_pcie_dbg_t	*mpc_dbg;
	milan_pcie_port_t	mpc_ports[MILAN_PCIE_CORE_MAX_PORTS];
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_PCIE_IMPL_H */
