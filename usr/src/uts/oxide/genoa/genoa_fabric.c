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
 * Various routines and things to access, initialize, understand, and manage
 * Genoa's I/O fabric. This consists of both the data fabric and the
 * northbridges.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/pcie.h>
#include <sys/io/genoa/pcie_impl.h>

#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>

/*
 * How many PCIe cores does this IOMS instance have?
 * If it's an IOHUB that has a bonus core then it will have the maximum
 * number, otherwise one fewer.
 */
uint8_t
genoa_ioms_n_pcie_cores(const uint8_t iomsno)
{
	if (GENOA_IOMS_IOHUB_NUM(iomsno) == GENOA_NBIO_BONUS_IOHUB)
		return (GENOA_IOMS_MAX_PCIE_CORES);
	return (GENOA_IOMS_MAX_PCIE_CORES - 1);
}

/*
 * How many PCIe ports does this core instance have?
 * The bonus cores have a lower number of ports than the others.
 * Not all ports are necessarily enabled, and ports that are disabled may have
 * their associated bridges hidden; this is used to compute the locations of
 * register blocks that pertain to the port that may exist.
 */
uint8_t
genoa_pcie_core_n_ports(const uint8_t pcno)
{
	if (pcno == GENOA_IOMS_BONUS_PCIE_CORENO)
		return (GENOA_PCIE_CORE_BONUS_PORTS);
	return (GENOA_PCIE_CORE_MAX_PORTS);
}

/*
 * Convenience functions for accessing SMN registers pertaining to a bridge.
 * These are candidates for making public if/when other code needs to manipulate
 * bridges.  There are some tradeoffs here: we don't need any of these
 * functions; callers could instead look up registers themselves, retrieve the
 * iodie by chasing back-pointers, and call zen_smn_{read,write}32()
 * themselves.  Indeed, they still can, and if there are many register accesses
 * to be made in code that materially affects performance, that is likely to be
 * preferable.  However, it has a major drawback: it requires each caller to get
 * the ordered set of instance numbers correct when constructing the register,
 * and there is little or nothing that can be done to help them.  Most of the
 * register accessors will blow up if the instance numbers are obviously out of
 * range, but there is little we can do to prevent them being given out of
 * order, for example.  Constructing incompatible struct types for each instance
 * level seems impractical.  So instead we isolate those calculations here and
 * allow callers to treat each bridge's (or other object's) collections of
 * pertinent registers opaquely.  This is probably closest to what we
 * conceptually want this to look like anyway; callers should be focused on
 * controlling the device, not on the mechanics of how to do so.  Nevertheless,
 * we do not foreclose on arbitrary SMN access if that's useful.
 *
 * We provide similar collections of functions below for other entities we
 * model in the fabric.
 */

smn_reg_t
genoa_pcie_port_reg(const zen_pcie_port_t *const port,
    const smn_reg_def_t def)
{
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_PORT:
		reg = genoa_pcie_port_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe port",
		    def.srd_unit);
	}

	return (reg);
}

smn_reg_t
genoa_pcie_core_reg(const zen_pcie_core_t *const pc, const smn_reg_def_t def)
{
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_CORE:
		reg = genoa_pcie_core_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe RC",
		    def.srd_unit);
	}

	return (reg);
}

/*
 * This is the main place where we basically do everything that we need to do to
 * get the PCIe engine up and running.
 */
void
genoa_fabric_init(zen_fabric_t *fabric)
{
	/*
	 * These register debugging facilities are costly in both space and
	 * time, and are enabled only on DEBUG kernels.
	 */
	zen_pcie_populate_dbg(fabric, GPCS_PRE_INIT, ZEN_IODIE_MATCH_ANY);
}
