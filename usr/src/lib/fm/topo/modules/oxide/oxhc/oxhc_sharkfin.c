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
 * Implement logic around enumerating sharkfins and disinguishing between
 * different hardware generations.
 */

#include <string.h>

#include "oxhc.h"

typedef struct {
	const char *shark_refdes;
	const char *shark_cpn;
	const oxhc_ic_board_t *shark_ics;
	const size_t *shark_nics;
} sharkfin_info_t;

const sharkfin_info_t sharkfin_info[] = {
	{
		.shark_refdes = "U7",
		.shark_cpn = "913-0000021",
		.shark_ics = oxhc_ic_sharkfin_gimlet,
		.shark_nics = &oxhc_ic_sharkfin_gimlet_nents
	}, {
		.shark_refdes = "U2",
		.shark_cpn = "913-0000026",
		.shark_ics = oxhc_ic_sharkfin_cosmo,
		.shark_nics = &oxhc_ic_sharkfin_cosmo_nents
	}
};

/*
 * Attempt to find / determine which model of sharkfin we're using.
 */
static const sharkfin_info_t *
topo_oxhc_enum_sharkfin_find(topo_mod_t *mod, tnode_t *tn, const oxhc_t *oxhc,
    const char *refdes, libipcc_inv_t **invp)
{
	for (size_t i = 0; i < ARRAY_SIZE(sharkfin_info); i++) {
		char ipcc[IPCC_INVENTORY_NAMELEN];
		libipcc_inv_t *inv;

		if (snprintf(ipcc, sizeof (ipcc), "%s/%s/ID",
		    refdes, sharkfin_info[i].shark_refdes) >= sizeof (ipcc)) {
			topo_mod_dprintf(mod, "constructing expected VPD ID "
			    "refdes for %s[%" PRIu64 "] based on found refdes "
			    "'%s' is larger than the IPCC inventory name "
			    "length", topo_node_name(tn),
			    topo_node_instance(tn), refdes);
			(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
			return (NULL);
		}

		if ((inv = topo_oxhc_inventory_find(oxhc, ipcc)) == NULL) {
			continue;
		}

		*invp = inv;
		return (&sharkfin_info[i]);
	}

	topo_mod_dprintf(mod, "failed to find VPD for %s[%" PRIu64 "], slot "
	    "refdes '%s'", topo_node_name(tn), topo_node_instance(tn), refdes);
	(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
	return (NULL);
}

/*
 * This indicates that we've found a CEM slot that should have a sharkfin.
 * Unfortuantely we don't know what kind of sharkfin we have so we are going to
 * use our base refdes and try a few different things to see where we can find
 * the VPD. Once we have that we'll be able to confirm whether this is a Gimlet
 * or Cosmo-era sharkfin.
 */
int
topo_oxhc_enum_sharkfin(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	int err, ret;
	const char *tname = topo_node_name(tn);
	char *slot_refdes = NULL, *part = NULL, *serial = NULL;
	char rev[16];
	const sharkfin_info_t *info;
	libipcc_inv_t *inv;
	ipcc_inv_vpdid_t vpd;
	tnode_t *board;
	nvlist_t *auth = NULL;

	topo_mod_dprintf(mod, "post-processing %s[%" PRIu64 "]\n", tname,
	    topo_node_instance(tn));

	if (topo_prop_get_string(tn, TOPO_PGROUP_OXHC, TOPO_PGROUP_OXHC_REFDES,
	    &slot_refdes, &err) != 0) {
		topo_mod_dprintf(mod, "%s[%" PRIu64 "] missing required refdes "
		    "property: %s, cannot enumerate further", tname,
		    topo_node_instance(tn), topo_strerror(err));
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	info = topo_oxhc_enum_sharkfin_find(mod, tn, oxhc, slot_refdes, &inv);
	if (info == NULL) {
		ret = -1;
		goto out;
	}

	/*
	 * If we don't have valid ID information then we should not create a
	 * sharkfin. This is slightly different from the temp sensor board only
	 * because the temp sensor board does not have a FRU ID ROM.
	 */
	if (!topo_oxhc_inventory_bcopy(inv, IPCC_INVENTORY_T_VPDID, &vpd,
	    sizeof (vpd), sizeof (vpd))) {
		topo_mod_dprintf(mod, "IPCC information for %s/%s is not "
		    "copyable\n", slot_refdes, info->shark_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if ((part = topo_mod_clean_strn(mod, (const char *)vpd.vpdid_pn,
	    sizeof (vpd.vpdid_pn))) == NULL ||
	    (serial = topo_mod_clean_strn(mod, (const char *)vpd.vpdid_sn,
	    sizeof (vpd.vpdid_sn))) == NULL) {
		topo_mod_dprintf(mod, "failed to clean up strings for %s/%s\n",
		    slot_refdes, info->shark_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if (strcmp(part, info->shark_cpn) != 0) {
		topo_mod_dprintf(mod, "encountered part mismatch on %s[%"
		    PRIu64 "] with slot refdes %s: found part %s, but expected "
		    "%s", tname, topo_node_instance(tn), slot_refdes, part,
		    info->shark_cpn);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	(void) snprintf(rev, sizeof (rev), "%u", vpd.vpdid_rev);

	if (topo_node_range_create(mod, tn, BOARD, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create BOARD range: %s\n",
		    topo_mod_errmsg(mod));
		ret = -1;
		goto out;
	}

	auth = topo_oxhc_auth(mod, oxhc, oe, tn);
	if (auth == NULL) {
		topo_mod_dprintf(mod, "failed to get auth data for %s[%" PRIu64
		    "]: %s\n", tname, topo_node_instance(tn),
		    topo_mod_errmsg(mod));
		ret = -1;
		goto out;
	}

	/*
	 * The FRU for the sharkfin is itself. Inherit the label from our parent
	 * which will name the sharkfin according to the silk.
	 */
	if (topo_oxhc_tn_create(mod, tn, &board, BOARD, min, auth,
	    part, rev, serial, TOPO_OXHC_TN_F_FRU_SELF |
	    TOPO_OXHC_TN_F_SET_LABEL, NULL) != 0) {
		ret = -1;
		goto out;
	}

	ret = topo_oxhc_enum_ic(mod, oxhc, board, slot_refdes, vpd.vpdid_rev,
	    info->shark_ics, *info->shark_nics);
out:
	topo_mod_strfree(mod, slot_refdes);
	topo_mod_strfree(mod, part);
	topo_mod_strfree(mod, serial);
	nvlist_free(auth);
	return (ret);
}
