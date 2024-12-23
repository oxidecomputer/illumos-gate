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

/*
 * Type and function declarations for interacting with MPIO, the post-Milan AMD
 * Zen "MicroProcessor for IO", which is the component that handles tasks
 * including driving the DXIO crossbar to train PCIe lanes, etc.
 */

#ifndef	_SYS_IO_ZEN_MPIO_H
#define	_SYS_IO_ZEN_MPIO_H

#include <sys/stdbool.h>
#include <sys/io/zen/fabric_limits.h>
#include <sys/io/zen/oxio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;
typedef struct zen_mpio_ask_port zen_mpio_ask_port_t;
typedef struct zen_mpio_ask zen_mpio_ask_t;
typedef struct zen_mpio_ubm_hfc_port zen_mpio_ubm_hfc_port_t;
typedef struct zen_mpio_ext_attrs zen_mpio_ext_attrs_t;


/*
 * We size the maximum number of ports in the ask roughly based on the SP5
 * design and I/O die constraints as a rough swag. P0 and G3 can each support up
 * to 16 PCIe devices, while the remaining 6 groups cans upport up to 8-9
 * devices and P4/P5 can support up to 4 devices. That gives us 88 devices. We
 * currently require this to be a page size which can only fit up to 78 devices.
 */
#define	ZEN_MPIO_ASK_MAX_PORTS	78

typedef struct zen_mpio_config {
	zen_mpio_ask_t			*zmc_ask;
	zen_mpio_ext_attrs_t		*zmc_ext_attrs;
	uint64_t			zmc_ask_pa;
	uint64_t			zmc_ext_attrs_pa;
	uint32_t			zmc_ask_nports;
	uint32_t			zmc_ask_alloc_len;
	uint32_t			zmc_ext_attrs_alloc_len;
	uint32_t			zmc_ext_attrs_len;
} zen_mpio_config_t;

/*
 * Discovered and Synthesized Information for a given UBM DFC.
 */
typedef struct zen_ubm_dfc {
	const zen_mpio_ask_port_t	*zud_ask;
	uint16_t			zud_slot;
} zen_ubm_dfc_t;

typedef struct zen_ubm_hfc {
	const oxio_engine_t		*zuh_oxio;
	const zen_mpio_ubm_hfc_port_t	*zuh_hfc;
	uint32_t			zuh_num;
	uint32_t			zuh_ndfcs;
	zen_ubm_dfc_t			zuh_dfcs[ZEN_MAX_UBM_DFC_PER_HFC];
} zen_ubm_hfc_t;

typedef struct zen_ubm_config {
	zen_mpio_ubm_hfc_port_t		*zuc_hfc_ports;
	uint64_t			zuc_hfc_ports_pa;
	uint32_t			zuc_hfc_nports;
	uint32_t			zuc_hfc_ports_alloc_len;
	/*
	 * These members allow us to map the global UBM config back to the
	 * corresponding per-I/O die information.
	 */
	uint32_t			zuc_die_idx[ZEN_FABRIC_MAX_IO_DIES];
	uint32_t			zuc_die_nports[ZEN_FABRIC_MAX_IO_DIES];
	/*
	 * These structures allow us to map a given UBM HFC and DFC
	 * configuration information back to the corresponding oxio engine.
	 */
	zen_ubm_hfc_t			zuc_hfc[ZEN_MAX_UBM_HFC];
} zen_ubm_config_t;

/*
 * Retrieves and reports the MPIO firmware version.
 */
extern bool zen_mpio_get_fw_version(zen_iodie_t *iodie);
extern void zen_mpio_report_fw_version(const zen_iodie_t *iodie);

/*
 * Initialize MPIO-level PCIe components: this trains links, maps bridges, and
 * so on.
 */
extern void zen_mpio_pcie_init(zen_fabric_t *);

extern bool zen_mpio_write_pcie_strap(zen_pcie_core_t *, uint32_t, uint32_t);
extern uint32_t zen_mpio_ubm_idx(const zen_iodie_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IO_ZEN_MPIO_H */
