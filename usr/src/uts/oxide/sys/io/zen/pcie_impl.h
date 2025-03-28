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
 * Copyright 2025 Oxide Computer Company
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
#include <sys/io/zen/hotplug.h>
#include <sys/io/zen/pcie.h>
#include <sys/io/zen/oxio.h>

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
typedef struct zen_ubm_hfc zen_ubm_hfc_t;
typedef struct zen_ubm_dfc zen_ubm_dfc_t;

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

/*
 * The maximum number of internal PCIe ports found on an IOHC. There is
 * generally one of these for each nBIF present.
 */
#define	ZEN_IOHC_MAX_NBIFS		4

/*
 * This structure tells us, for a single IOHC, the PCIe devices and functions
 * where the internal nBIF ports are found.
 */
typedef struct zen_iohc_nbif_ports {
	const uint8_t			zinp_count;
	const zen_pcie_port_info_t	zinp_ports[ZEN_IOHC_MAX_NBIFS];
} zen_iohc_nbif_ports_t;


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
 * in both space and time, and is only done on DEBUG kernels. They do not
 * control any software behaviour other than in mdb.
 */
typedef enum zen_pcie_config_stage {
	ZPCS_PRE_INIT,
	ZPCS_SM_START,
	ZPCS_SM_MAPPED,
	ZPCS_SM_MAPPED_POST,
	ZPCS_SM_CONFIGURED,
	ZPCS_SM_CONFIGURED_POST,
	ZPCS_SM_PERST,
	ZPCS_SM_PERST_POST,
	ZPCS_SM_DONE,
	ZPCS_PRE_HOTPLUG,
	ZPCS_POST_HOTPLUG,
	ZPCS_USER_DIRECTED,
	ZPCS_NUM_STAGES
} zen_pcie_config_stage_t;

typedef struct zen_pcie_reg_dbg {
	const char		*zprd_name;
	smn_reg_def_t		zprd_def;
	uint32_t		zprd_val[ZPCS_NUM_STAGES];
	hrtime_t		zprd_ts[ZPCS_NUM_STAGES];
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

	/*
	 * All PCIe ports will have a corresponding OXIO engine that they were
	 * derived from. We cache a pointer to the corresponding structure that
	 * we pass to AMD firmware. If the port corresponds to a UBM based
	 * engine, then its corresponding HFC and DFC will be filled in.
	 */
	const oxio_engine_t	*zpp_oxio;
	union {
		zen_dxio_fw_engine_t		*zpp_dxio_engine;
		zen_mpio_ask_port_t		*zpp_ask_port;
	};
	const zen_ubm_hfc_t	*zpp_hfc;
	const zen_ubm_dfc_t	*zpp_dfc;

	zen_pcie_core_t		*zpp_core;

	/*
	 * The following represents the synthesized slot information for this.
	 */
	uint16_t		zpp_slotno;

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
};

/*
 * Straps can be matched on a combination of board identifier, IO die, DF node
 * ID, NBIO/IOMS number, PCIe core number (root complex number;
 * zen_pcie_core_t.zpc_coreno), and PCIe port number
 * (zen_pcie_port_t.zpp_portno).
 *
 * The board sentinel value is 0 and may be omitted.  The others require nonzero
 * sentinels as 0 is a valid index for all of them.
 *
 * The sentinel values of 0xFF here cannot match any real NBIO, core, or port:
 * this value is well above the architectural limits.
 *
 * The core and port filters are meaningful only if the corresponding strap
 * exists at the corresponding level.
 *
 * The node ID, which incorporates both socket and die number is 8 bits and in
 * principle it could be 0xFF, so we use 32 bits there instead: AMD have
 * reserved another 8 bits that are likely to be used in future families so we
 * expand to 32 bits.
 */
typedef struct zen_pcie_strap_setting {
	uint32_t		strap_reg;
	uint32_t		strap_data;
	oxide_board_t		strap_boardmatch;
	uint32_t		strap_nodematch;
	uint8_t			strap_iomsmatch;
	uint8_t			strap_corematch;
	uint8_t			strap_portmatch;
} zen_pcie_strap_setting_t;

#define	PCIE_NODEMATCH_ANY	0xFFFFFFFF
#define	PCIE_IOMSMATCH_ANY	0xFF
#define	PCIE_COREMATCH_ANY	0xFF
#define	PCIE_PORTMATCH_ANY	0xFF

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PCIE_IMPL_H */
