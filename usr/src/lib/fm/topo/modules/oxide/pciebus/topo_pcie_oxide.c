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

/*
 * This is the oxide architecture specific part of the pciebus enumeration
 * module. It provides hooks which are called at module init and fini, and
 * after each topology node is created.
 */

#include <stdbool.h>
#include <string.h>
#include <sys/debug.h>
#include <sys/types.h>
#include <sys/sysmacros.h>

#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_list.h>
#include <fm/topo_method.h>

#include "topo_pcie.h"

typedef struct {
	const char *s_name;
	topo_instance_t s_instance;
} substrate_t;

#define	SS_SIZE	2	/* Max number of additional substrate entries */
#define	SS_CLEN	2	/* Max number of each substrate's components */
static substrate_t substrate0[][SS_CLEN] = {
	{ { SLOT, 0 } },
	{ { SLOT, 0 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate1[][SS_CLEN] = {
	{ { SLOT, 1 } },
	{ { SLOT, 1 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate2[][SS_CLEN] = {
	{ { SLOT, 2 } },
	{ { SLOT, 2 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate3[][SS_CLEN] = {
	{ { SLOT, 3 } },
	{ { SLOT, 3 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate4[][SS_CLEN] = {
	{ { SLOT, 4 } },
	{ { SLOT, 4 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate5[][SS_CLEN] = {
	{ { SLOT, 5 } },
	{ { SLOT, 5 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate6[][SS_CLEN] = {
	{ { SLOT, 6 } },
	{ { SLOT, 6 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate7[][SS_CLEN] = {
	{ { SLOT, 7 } },
	{ { SLOT, 7 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate8[][SS_CLEN] = {
	{ { SLOT, 8 } },
	{ { SLOT, 8 }, { BOARD, 0 } },
	{ 0 },
};
static substrate_t substrate9[][SS_CLEN] = {
	{ { SLOT, 9 } },
	{ { SLOT, 9 }, { BOARD, 0 } },
	{ 0 },
};
static const substrate_t substrate19[][SS_CLEN]= {
	{ { PORT, 0 } },
	{ 0 },
};

typedef struct {
	uint16_t		sm_slot;
	const char		*sm_label;
	const substrate_t	(*sm_substrate)[SS_CLEN];
} slotmap_t;

static slotmap_t gimlet_slotmap[] = {
	{ 0,	"N0",		substrate0 },
	{ 1,	"N1",		substrate1 },
	{ 2,	"N2",		substrate2 },
	{ 3,	"N3",		substrate3 },
	{ 4,	"N4",		substrate4 },
	{ 5,	"N5",		substrate5 },
	{ 6,	"N6",		substrate6 },
	{ 7,	"N7",		substrate7 },
	{ 8,	"N8",		substrate8 },
	{ 9,	"N9",		substrate9 },
	{ 16,	"U477",		NULL },		/* Chip-down Chelsio T6 */
	{ 17,	"M.2 (East)",	NULL },
	{ 18,	"M.2 (West)",	NULL },
	{ 19,	"J3",		substrate19 },	/* ExaMax connector */
};

typedef struct {
	const char		*em_product;
	const slotmap_t		*em_enum;
	const size_t		em_nenum;
} enum_map_t;

static const enum_map_t enum_map[] = {
	{ "Oxide,Gimlet", gimlet_slotmap, ARRAY_SIZE(gimlet_slotmap) }
};

typedef struct {
	const enum_map_t	*mpp_map;
	const char		*mpp_pn;
	const char		*mpp_sn;
	nvlist_t		*mpp_board_fmri;
} mod_pcie_privdata_t;

/*
 * Walk up the topology tree to see if there is a node which has a populated
 * "slot" property and, if found, return the matching entry from the provided
 * slot map. If a node whose name matches 'stop' is encountered, then go no
 * further.
 */
static const slotmap_t *
map_slot(topo_mod_t *mod, tnode_t *tn, const enum_map_t *map, const char *stop)
{
	while ((tn = topo_node_parent(tn)) != NULL) {
		uint32_t slot;
		int err;

		if (stop != NULL && strcmp(stop, topo_node_name(tn)) == 0)
			break;

		if (topo_prop_get_uint32(tn, TOPO_PGROUP_PCI,
		    TOPO_PCIE_PCI_SLOT, &slot, &err) != 0) {
			if (err != ETOPO_PROP_NOENT) {
				topo_mod_dprintf(mod,
				    "decorate: could not retrieve slot: %s",
				    topo_strerror(err));
			}
			continue;
		}

		topo_mod_dprintf(mod,
		    "decorate: Fetched slot %u from %s%" PRIu64,
		    slot, topo_node_name(tn), topo_node_instance(tn));

		for (size_t i = 0; i < map->em_nenum; i++) {
			const slotmap_t *smap = &map->em_enum[i];

			if (slot == smap->sm_slot)
				return (smap);
		}
	}

	return (NULL);
}

static tnode_t *
decorate_port(mod_pcie_privdata_t *pd __unused, topo_mod_t *mod,
    const pcie_t *pcie, const enum_map_t *map, const pcie_node_t *node,
    tnode_t *tn)
{
	tnode_t *ptn;
	int err;

	/*
	 * Apply any label from the slot map table to upstream ports
	 * that have a parent with a known slot number.
	 */
	ptn = topo_node_parent(tn);
	if (ptn != NULL && strcmp(topo_node_name(ptn), "link")) {
		const slotmap_t *smap;

		smap = map_slot(mod, tn, map, NULL);

		if (smap != NULL) {
			topo_mod_dprintf(mod,
			    "decorate: mapped slot %u -> '%s'",
			    smap->sm_slot, smap->sm_label);
			(void) topo_node_label_set(tn, smap->sm_label, &err);
		}
	}

	return (tn);
}

static tnode_t *
decorate_link(mod_pcie_privdata_t *pd, topo_mod_t *mod, const pcie_t *pcie,
    const enum_map_t *map, const pcie_node_t *node, tnode_t *tn)
{
	extern const topo_pgroup_info_t pcielink_pgroup;
	/* Allow one slot for the main board */
	const nvlist_t *substrate[SS_SIZE + 1]; /* +1 for board */
	const slotmap_t *smap;
	size_t nsub = 0;
	int err;

	smap = map_slot(mod, tn, map, "link");

	if (smap == NULL)
		return (tn);

	/* All substrates include the main board */
	substrate[nsub++] = pd->mpp_board_fmri;

	if (smap->sm_substrate != NULL) {
		for (size_t i = 0; i < SS_SIZE; i++) {
			nvlist_t *sfmri;

			topo_mod_dprintf(mod, "SS entry %zu", i);

			/*
			 * Start with the main board, then add the additional
			 * substrate components to that FMRI.
			 */
			sfmri = pd->mpp_board_fmri;

			for (size_t j = 0; j < SS_CLEN; j++) {
				const substrate_t *ss =
				    &smap->sm_substrate[i][j];
				nvlist_t *nfmri;

				topo_mod_dprintf(mod, "SS entry %zu, slot %zu",
				    i, j);

				topo_mod_dprintf(mod, "SS   %s[%"PRIu64"]",
				    ss->s_name, ss->s_instance);

				if (ss->s_name == NULL)
					break;

				nfmri = topo_mod_hcfmri_extend(mod,
				    sfmri, FM_HC_SCHEME_VERSION,
				    ss->s_name, ss->s_instance);
				if (nfmri == NULL) {
					topo_mod_dprintf(mod, "Failed to create"
					    "substrate FMRI: %s",
					    topo_mod_errmsg(mod));
				}
				if (sfmri != pd->mpp_board_fmri)
					nvlist_free(sfmri);
				sfmri = nfmri;
				if (sfmri == NULL)
					break;
			}
			if (sfmri != NULL)
				substrate[nsub++] = sfmri;
		}
	}

	(void) pcie_topo_pgroup_create(mod, tn, &pcielink_pgroup);
	if (topo_prop_set_fmri_array(tn, TOPO_PCIE_PGROUP_PCIE_LINK,
	    TOPO_PCIE_LINK_SUBSTRATE, TOPO_PROP_IMMUTABLE,
	    substrate, nsub, &err) != 0) {
		topo_mod_dprintf(mod, "decorate: could not set %s/%s: %s",
		    TOPO_PCIE_PGROUP_PCIE_LINK, TOPO_PCIE_LINK_SUBSTRATE,
		    topo_strerror(err));
	}

	for (size_t i = 0; i < nsub; i++) {
		if (substrate[i] != pd->mpp_board_fmri)
			nvlist_free((nvlist_t *)substrate[i]);
	}

	return (tn);
}

/*
 * This is the main entry point for this arch-specific pciebus component. It is
 * called for every topology node that is created after the basic properties
 * are set.
 */
tnode_t *
mod_pcie_platform_topo_node_decorate(topo_mod_t *mod, const pcie_t *pcie,
    const pcie_node_t *node, tnode_t *tn)
{
	mod_pcie_privdata_t *pd;
	const enum_map_t *map;
	const char *name;

	pd = pcie_get_platdata(pcie);
	if (pd == NULL) {
		topo_mod_dprintf(mod, "decorate: no privdata\n");
		return (NULL);
	}
	map = pd->mpp_map;
	if (map == NULL) {
		topo_mod_dprintf(mod, "decorate: no map\n");
		return (tn);
	}

	name = topo_node_name(tn);

	topo_mod_dprintf(mod, "decorate: %s=%" PRIu64,
	    name, topo_node_instance(tn));

	if (strcmp(name, "port") == 0)
		return (decorate_port(pd, mod, pcie, map, node, tn));
	else if (strcmp(name, "link") == 0)
		return (decorate_link(pd, mod, pcie, map, node, tn));

	return (tn);
}

nvlist_t *
mod_pcie_platform_auth(topo_mod_t *mod, const pcie_t *pcie, tnode_t *parent)
{
	mod_pcie_privdata_t *pd;
	nvlist_t *auth;
	int err = 0;

	pd = pcie_get_platdata(pcie);
	if (pd == NULL) {
		topo_mod_dprintf(mod, "%s: no privdata\n", __func__);
		return (NULL);
	}

	auth = topo_mod_auth(mod, parent);
	if (auth == NULL)
		return (NULL);

	err |= nvlist_add_string(auth, FM_FMRI_AUTH_PRODUCT, pd->mpp_pn);
	err |= nvlist_add_string(auth, FM_FMRI_AUTH_PRODUCT_SN, pd->mpp_sn);
	err |= nvlist_add_string(auth, FM_FMRI_AUTH_CHASSIS,  pd->mpp_sn);

	if (err != 0) {
		nvlist_free(auth);
		(void) topo_mod_seterrno(mod, EMOD_NVL_INVAL);
		return (NULL);
	}

	return (auth);
}

/*
 * Create the HC scheme FMRIs that we will need to populate the link
 * substrates.
 */
static bool
mod_pcie_platform_hcfmri(topo_mod_t *mod, pcie_t *pcie,
    mod_pcie_privdata_t *pd)
{
	nvlist_t *chassis_fmri, *board_fmri;

	chassis_fmri = topo_mod_hcfmri(mod, NULL, FM_HC_SCHEME_VERSION,
	    CHASSIS, 0, NULL, NULL, NULL, NULL, NULL);
	if (chassis_fmri == NULL) {
		topo_mod_dprintf(mod, "Failed to create chassis FMRI: %s",
		    topo_mod_errmsg(mod));
		return (false);
	}

	board_fmri = topo_mod_hcfmri_extend(mod, chassis_fmri,
	    FM_HC_SCHEME_VERSION, SYSTEMBOARD, 0);
	nvlist_free(chassis_fmri);

	if (board_fmri == NULL) {
		topo_mod_dprintf(mod, "Failed to create board FMRI: %s",
		    topo_mod_errmsg(mod));
		return (false);
	}

	pd->mpp_board_fmri = board_fmri;

	return (true);
}

bool
mod_pcie_platform_init(topo_mod_t *mod, pcie_t *pcie)
{
	mod_pcie_privdata_t *pd;
	const char *product;
	char *props;
	int nprop;

	topo_mod_dprintf(mod, "%s start", __func__);

	if ((pd = topo_mod_zalloc(mod, sizeof (*pd))) == NULL)
		return (false);

	product = di_node_name(pcie->tp_devinfo);

	for (size_t i = 0; i < ARRAY_SIZE(enum_map); i++) {
		if (strcmp(enum_map[i].em_product, product) == 0) {
			pd->mpp_map = &enum_map[i];
			break;
		}
	}
	if (pd->mpp_map == NULL) {
		topo_mod_dprintf(mod, "Could not find product map for %s",
		    product);
		/* Carry on; there will just be no decoration. */
	}

	if (!mod_pcie_platform_hcfmri(mod, pcie, pd)) {
		topo_mod_dprintf(mod, "hc construction failed");
		topo_mod_free(mod, pd, sizeof (*pd));
		return (false);
	}

	nprop = di_prop_lookup_strings(DDI_DEV_T_ANY, pcie->tp_devinfo,
	    "baseboard-identifier", &props);
	if (nprop == 1)
		pd->mpp_sn = topo_mod_strdup(mod, props);

	nprop = di_prop_lookup_strings(DDI_DEV_T_ANY, pcie->tp_devinfo,
	    "baseboard-model", &props);
	if (nprop == 1)
		pd->mpp_pn = topo_mod_strdup(mod, props);

	return (pcie_set_platdata(pcie, pd));
}

void
mod_pcie_platform_fini(topo_mod_t *mod, pcie_t *pcie)
{
	mod_pcie_privdata_t *pd;

	if ((pd = pcie_get_platdata(pcie)) != NULL) {
		topo_mod_strfree(mod, (char *)pd->mpp_sn);
		topo_mod_strfree(mod, (char *)pd->mpp_pn);
		nvlist_free(pd->mpp_board_fmri);

		topo_mod_free(mod, pd, sizeof (*pd));

		(void) pcie_set_platdata(pcie, NULL);
	}
}
