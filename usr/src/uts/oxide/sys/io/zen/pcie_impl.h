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

/*
 * Structures, prototypes, enumerations, and constants common across
 * microarchitectures and used in the implementation of PCIe on the Oxide
 * platform.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/mutex.h>
#include <sys/bitext.h>
#include <sys/platform_detect.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/dxio_impl.h>
#include <sys/io/zen/hotplug_impl.h>
#include <sys/io/zen/pcie_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_dxio_fw_engine zen_dxio_fw_engine_t;
typedef struct zen_mpio_ask_port zen_mpio_ask_port_t;
typedef struct zen_mpio_ubm_extra zen_mpio_ubm_extra_t;

/*
 * The current maximum number of ports that can be attached to any
 * PCIe core in our supported Zen microarchitectures.
 */
#define	ZEN_PCIE_CORE_MAX_PORTS		9

typedef struct zen_pcie_port zen_pcie_port_t;
typedef struct zen_pcie_core zen_pcie_core_t;

typedef struct zen_pcie_port_info {
	uint8_t zppi_dev;
	uint8_t zppi_func;
} zen_pcie_port_info_t;

typedef struct zen_pcie_core_info {
	const char	*zpci_name;
	uint16_t	zpci_dxio_start;
	uint16_t	zpci_dxio_end;
	uint16_t	zpci_phy_start;
	uint16_t	zpci_phy_end;
} zen_pcie_core_info_t;

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
 * In order to aid PCIe debugging, core and port registers are captured at
 * various stages of PCIe programming and initialization and stored in the
 * corresponding zen_pcie_port_t and zen_pcie_core_t structures. This is costly
 * in both space and time, and is only done on DEBUG kernels.
 */
enum {
	ZPCS_PRE_DXIO_INIT,
	ZPCS_PRE_HOTPLUG,
	ZPCS_POST_HOTPLUG,
};
#define	ZPCS_MAX_STAGES	15

typedef struct zen_pcie_reg_dbg {
	const char		*zprd_name;
	smn_reg_def_t		zprd_def;
	uint32_t		zprd_val[ZPCS_MAX_STAGES];
	hrtime_t		zprd_ts[ZPCS_MAX_STAGES];
} zen_pcie_reg_dbg_t;

typedef struct zen_pcie_dbg {
	uint32_t		zpd_last_stage;
	size_t			zpd_nregs;
	zen_pcie_reg_dbg_t	zpd_regs[];
} zen_pcie_dbg_t;

#define	ZEN_PCIE_DBG_SIZE(_nr)	\
	(sizeof (zen_pcie_dbg_t) + (_nr) * sizeof (zen_pcie_reg_dbg_t))

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
	 * The index of this port in its corresponding PCIe core.
	 * This is used as the port number in SMN and strap addressing.
	 */
	uint8_t			zpp_portno;

	/*
	 * The PCIe device number from the BDF naming the bridge on this port.
	 */
	uint8_t			zpp_device;

	/*
	 * The PCIe function number from the BDF naming the bridge on this port.
	 */
	uint8_t			zpp_func;

	zen_hotplug_type_t	zpp_hp_type;
	uint16_t		zpp_hp_slotno;
	uint32_t		zpp_hp_mpio_mask;

	union {
		zen_dxio_fw_engine_t		*zpp_dxio_engine;
		struct {
			zen_mpio_ask_port_t	*zpp_ask_port;
			zen_mpio_ubm_extra_t	*zpp_ubm_extra;
		};
	};

	zen_pcie_core_t		*zpp_core;
	void			*zpp_uarch_pcie_port;

	/*
	 * PCIe port registers captured at various stages.
	 */
	zen_pcie_dbg_t		*zpp_dbg;
};

struct zen_pcie_core {
	zen_pcie_core_flag_t	zpc_flags;

	uint8_t			zpc_coreno;

	uint8_t			zpc_nports;
	zen_pcie_port_t		zpc_ports[ZEN_PCIE_CORE_MAX_PORTS];

	/*
	 * The SDP Unit ID for the first port in this core. Within each core,
	 * ports and units increment sequentially.
	 */
	uint8_t			zpc_sdp_unit;

	/*
	 * PCIe core registers captured at various stages.
	 */
	zen_pcie_dbg_t		*zpc_dbg;

	/*
	 * Lane start and end constants, both physical and logical (DXIO).  Note
	 * that the concept of DXIO lanes is common across microarchitectures,
	 * even though the way one deals with the DXIO crossbar changes.  While
	 * one might interface with the crossbar via RPCs send to the SMU on
	 * Milan but via RPCs sent to MPIO on Genoa and Turin, all work in
	 * operation of DXIO lanes.
	 *
	 * These lane numbers are inclusive.
	 */
	uint16_t		zpc_dxio_lane_start;
	uint16_t		zpc_dxio_lane_end;
	uint16_t		zpc_phys_lane_start;
	uint16_t		zpc_phys_lane_end;

	kmutex_t		zpc_strap_lock;

	zen_ioms_t		*zpc_ioms;
	void			*zpc_uarch_pcie_core;
};

#define	PCIE_NODEMATCH_ANY	0xFFFFFFFF
#define	PCIE_NBIOMATCH_ANY	0xFF
#define	PCIE_COREMATCH_ANY	0xFF
#define	PCIE_PORTMATCH_ANY	0xFF

typedef struct zen_pcie_strap_setting {
	uint32_t		strap_reg;
	uint32_t		strap_data;
	oxide_board_t		strap_boardmatch;
	uint32_t		strap_nodematch;
	uint8_t			strap_nbiomatch;
	uint8_t			strap_corematch;
	uint8_t			strap_portmatch;
} zen_pcie_strap_setting_t;


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PCIE_IMPL_H */
