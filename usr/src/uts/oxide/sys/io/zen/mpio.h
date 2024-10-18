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

/*
 * Type and function declarations for interacting with MPIO, the post-Milan AMD
 * Zen "MicroProcessor for IO", which is the component that handles tasks
 * including driving the DXIO crossbar to train PCIe lanes, etc.
 */

#ifndef	_SYS_IO_ZEN_MPIO_H
#define	_SYS_IO_ZEN_MPIO_H

#include <sys/stdbool.h>

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;

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

extern bool zen_mpio_send_hotplug_table(zen_iodie_t *, uint64_t);
extern bool zen_mpio_rpc_hotplug_flags(zen_iodie_t *, uint32_t);
extern bool zen_mpio_rpc_start_hotplug(zen_iodie_t *, bool, uint32_t);
extern bool zen_mpio_rpc_set_i2c_switch_addr(zen_iodie_t *, uint32_t);

#endif	/* _SYS_IO_ZEN_MPIO_H */
