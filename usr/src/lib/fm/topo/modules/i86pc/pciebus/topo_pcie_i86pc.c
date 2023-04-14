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
 * Copyright 2023 Oxide Computer Company
 */

#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <fm/topo_method.h>

#include "topo_pcie.h"

nvlist_t *
mod_pcie_platform_auth(topo_mod_t *mod, const pcie_t *pcie, tnode_t *parent)
{
	return (topo_mod_auth(mod, parent));
}

tnode_t *
mod_pcie_platform_topo_node_decorate(topo_mod_t *mod, const pcie_t *pcie,
    const pcie_node_t *node, tnode_t *tn)
{
	return (tn);
}

bool
mod_pcie_platform_init(topo_mod_t *mod, pcie_t *pcie)
{
	return (true);
}

void
mod_pcie_platform_fini(topo_mod_t *mod, pcie_t *pcie)
{
}
