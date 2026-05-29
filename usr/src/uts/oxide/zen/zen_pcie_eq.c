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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * Retrieval and programming of PCIe link equalisation (EQ) state. The register
 * access itself is microarchitecture-specific and is provided by the
 * zfo_pcie_port_eq and zfo_pcie_port_set_preset_mask fabric operations; a
 * microarchitecture that does not implement them causes these interfaces to
 * report ENOTSUP.
 */

#include <sys/types.h>
#include <sys/errno.h>

#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/platform_impl.h>

int
zen_pcie_eq_by_bdf(uint8_t bus, uint8_t dev, uint8_t func,
    pcie_link_speed_t speed, uint32_t nlanes, pcie_eq_t *eq)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_pcie_port_t *port;
	int ret;

	/*
	 * Resolve the bridge first so that an address that names no port is
	 * reported as such (ENXIO), even on a microarchitecture that does not
	 * implement EQ retrieval at all (ENOTSUP).
	 */
	port = zen_fabric_find_pcie_port_by_bdf(bus, dev, func);
	if (port == NULL)
		return (ENXIO);

	if (ops->zfo_pcie_port_eq == NULL)
		return (ENOTSUP);

	/*
	 * The EQ read is a multi-register sequence using the core-shared
	 * lane selector, so serialise it against any other EQ operation on the
	 * same core.
	 */
	mutex_enter(&port->zpp_core->zpc_eq_lock);
	ret = ops->zfo_pcie_port_eq(port, speed, nlanes, eq);
	mutex_exit(&port->zpp_core->zpc_eq_lock);

	/*
	 * Across AMD Zen parts an all-zero preset mask is not "no presets" but
	 * "mask not in use, consider every preset". Expose that effective
	 * behaviour explicitly, for both the current mask and the mask
	 * captured at the last link-up, so that consumers need not interpret
	 * the raw mask values themselves.
	 */
	if (ret == 0) {
		if (eq->peq_mask == 0)
			eq->peq_flags |= PCIE_EQ_F_ALL_PRESETS;
		if ((eq->peq_flags & PCIE_EQ_F_LINKUP_VALID) != 0 &&
		    eq->peq_mask_linkup == 0) {
			eq->peq_flags |= PCIE_EQ_F_LINKUP_ALL_PRESETS;
		}
	}

	return (ret);
}

int
zen_pcie_set_preset_mask_by_bdf(uint8_t bus, uint8_t dev, uint8_t func,
    pcie_link_speed_t speed, uint32_t mask)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_pcie_port_t *port;
	int ret;

	port = zen_fabric_find_pcie_port_by_bdf(bus, dev, func);
	if (port == NULL)
		return (ENXIO);

	if (ops->zfo_pcie_port_set_preset_mask == NULL)
		return (ENOTSUP);

	mutex_enter(&port->zpp_core->zpc_eq_lock);
	ret = ops->zfo_pcie_port_set_preset_mask(port, speed, mask);
	mutex_exit(&port->zpp_core->zpc_eq_lock);

	return (ret);
}
