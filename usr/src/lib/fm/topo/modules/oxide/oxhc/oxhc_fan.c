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
 * Logic to create and manage the fan-tray in Gimlet and Cosmo. The two are
 * designed the same, but have different part numbers.
 *
 * The Gimlet fan tray consists of a single enclosure with three fans each. Each
 * of the fans in turn has two rotors, which are independent and can fail. This
 * looks roughly like:
 *			Label
 *   fan-tray
 *     fan=0		Fan 0 (West)
 *       rotor=0	Southwest
 *       rotor=1	Northwest
 *     fan=1		Fan 1 (Center)
 *       rotor=0	South
 *       rotor=1	North
 *     fan=2		Fan 2 (East)
 *       rotor=0	Southeast
 *       rotor=1	Northeast
 *     board=0
 *       ic=0		U1
 *
 * When enumerating the fan tray things are a little more nuanced because of the
 * fact that the whole tray may be missing so we don't want to use the normal
 * node range enumeration and the topology map here if they don't exist.
 */

#include <string.h>

#include "oxhc.h"

#define	OXHC_MAX_ROTORS	2
#define	OXHC_MAX_FANS	3

typedef struct {
	/*
	 * The RefDes upon which we'll find the VPD for the fan tray.
	 */
	const char *ft_vpd;
	/*
	 * The RefDes that has the actual VPD information for the tray overall.
	 */
	const char *ft_refdes;
	/*
	 * The CPN of this platform's fan tray.
	 */
	const char *ft_cpn;
	/*
	 * The RefDes of the Fan Controller.
	 */
	const char *ft_ctrl;
	/*
	 * The number of fans present in the tray.
	 */
	uint32_t ft_nfans;
	/*
	 * These two tables are used for the fans labeling in the tree and
	 * relate to the compass rose usage. The first table is the label for
	 * the fan itself, the second is used as part of the rotor's label and
	 * combined with the actual rotor information contained in the of_labels
	 * member of the oxhc_fan_t.
	 */
	const char *ft_labels[OXHC_MAX_FANS];
	const char *ft_dirs[OXHC_MAX_FANS];
	/*
	 * The relationship between the sensor entries and the fans.
	 */
	uint32_t ft_sensors[OXHC_MAX_FANS];
} fan_tray_info_t;

static const fan_tray_info_t gimlet_tray_info = {
	.ft_vpd = "J180",
	.ft_refdes = "J180/ID",
	.ft_cpn = "991-0000084",
	.ft_ctrl = "U321",
	.ft_nfans = 3,
	.ft_labels = { "West", "Center", "East" },
	.ft_dirs = { "west", "", "east" },
	/*
	 * We enumerate fans from West to East, the sensors are from East to
	 * West. This gives the starting index for a sensor for those three
	 * entries.
	 */
	.ft_sensors = { 4, 2, 0 }
};

static const fan_tray_info_t cosmo_tray_info = {
	.ft_vpd = "J34",
	.ft_refdes = "J34/ID",
	.ft_cpn = "991-0000151",
	.ft_ctrl = "U58",
	.ft_nfans = 3,
	.ft_labels = { "West", "Center", "East" },
	.ft_dirs = { "west", "", "east" },
	/* The same direction reversal happens in Cosmo as well. */
	.ft_sensors = { 4, 2, 0 }
};

typedef struct oxhc_fan {
	const char *of_cpn;
	uint32_t of_nrotors;
	const char *of_labels[OXHC_MAX_ROTORS];
} oxhc_fan_t;

static const oxhc_fan_t oxhc_fans[] = {
	{ "991-0000094", 2, { "South", "North" } },
	{ "418-0000005", 2, { "South", "North" } }
};

static int
topo_oxhc_enum_fan(topo_mod_t *mod, const ipcc_inv_vpdid_t *fan_vpd,
    tnode_t *tray, topo_instance_t inst, nvlist_t *auth, const char *loc,
    const char *dir, const ipcc_sensor_id_t *sensors, uint32_t sensor_base)
{
	int ret;
	char *fan_pn = NULL, *fan_sn = NULL, fan_rev[16];
	char label[64];
	static const oxhc_fan_t *fan_info;
	tnode_t *fan, *rotor;

	if ((fan_pn = topo_mod_clean_strn(mod, (const char *)fan_vpd->vpdid_pn,
	    sizeof (fan_vpd->vpdid_pn))) == NULL ||
	    (fan_sn = topo_mod_clean_strn(mod, (const char *)fan_vpd->vpdid_sn,
	    sizeof (fan_vpd->vpdid_sn))) == NULL) {
		topo_mod_dprintf(mod, "failed to clean up fan %" PRIu64
		    " strings\n", inst);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	(void) snprintf(fan_rev, sizeof (fan_rev), "%u", fan_vpd->vpdid_rev);
	(void) snprintf(label, sizeof (label), "Fan %" PRIu64 " (%s)", inst,
	    loc);

	if ((ret = topo_oxhc_tn_create(mod, tray, &fan, FAN, inst, auth, fan_pn,
	    fan_rev, fan_sn, TOPO_OXHC_TN_F_SET_LABEL, label)) == -1) {
		goto out;
	}

	/*
	 * IPCC gives us enough information to create the FAN. At this point go
	 * back and create information about the rotors if we can determine more
	 * part-specific information here.
	 */
	fan_info = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(oxhc_fans); i++) {
		if (strcmp(fan_pn, oxhc_fans[i].of_cpn) == 0) {
			fan_info = &oxhc_fans[i];
			break;
		}
	}

	if (fan_info == NULL) {
		topo_mod_dprintf(mod, "no additional rotor information "
		    "available for fan %s:%s:%s\n", fan_pn, fan_rev, fan_sn);
		ret = 0;
		goto out;
	}

	if ((ret = topo_node_range_create(mod, fan, ROTOR, 0,
	    fan_info->of_nrotors - 1)) != 0) {
		topo_mod_dprintf(mod, "failed to create fan rotor range: %s\n",
		    topo_mod_errmsg(mod));
		goto out;
	}

	for (uint32_t i = 0; i < fan_info->of_nrotors; i++) {
		uint32_t sidx = sensor_base + i;
		(void) snprintf(label, sizeof (label), "%s%s",
		    fan_info->of_labels[i], dir);

		if ((ret = topo_oxhc_tn_create(mod, fan, &rotor, ROTOR, i, auth,
		    NULL, NULL, NULL, TOPO_OXHC_TN_F_SET_LABEL, label)) == -1) {
			goto out;
		}

		if (sensors[sidx] != UINT32_MAX && !topo_oxhc_mgs_sensor(mod,
		    rotor, "tach", TOPO_SENSOR_TYPE_FAN, TOPO_SENSOR_UNITS_RPM,
		    sensors[sidx])) {
			ret = -1;
			goto out;
		}
	}

	ret = 0;
out:
	topo_mod_strfree(mod, fan_pn);
	topo_mod_strfree(mod, fan_sn);
	return (ret);
}

static int
topo_oxhc_enum_fan_tray(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max, const fan_tray_info_t *info)
{
	int ret;
	libipcc_inv_t *inv, *sense_inv;
	ipcc_inv_fantray_t tray;
	ipcc_inv_max31790_t max31790;
	const ipcc_inv_vpdid_t *tinv, *binv;
	char *tray_pn = NULL, *tray_sn = NULL, tray_rev[16];
	char *board_pn = NULL, *board_sn = NULL, board_rev[16];
	tnode_t *tray_tn, *board_tn;
	nvlist_t *auth = NULL;

	if ((inv = topo_oxhc_inventory_find(oxhc, info->ft_refdes)) == NULL) {
		topo_mod_dprintf(mod, "failed to find IPCC inventory entry "
		    "%s\n", info->ft_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if (!topo_oxhc_inventory_bcopy(inv, IPCC_INVENTORY_T_FANTRAY, &tray,
	    sizeof (tray), sizeof (tray))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", info->ft_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	/*
	 * This means that we got enough information for a fan tray itself,
	 * which is great. First, we want to go through and create the fan tray
	 * itself. We're going to check that we actually know the fan tray's
	 * CPN. If it's different, we probably shouldn't continue as it means
	 * that things are likely different.
	 */
	tinv = &tray.ft_id;
	if ((tray_pn = topo_mod_clean_strn(mod, (const char *)tinv->vpdid_pn,
	    sizeof (tinv->vpdid_pn))) == NULL ||
	    (tray_sn = topo_mod_clean_strn(mod, (const char *)tinv->vpdid_sn,
	    sizeof (tinv->vpdid_sn))) == NULL) {
		topo_mod_dprintf(mod, "failed to clean up fan tray strings\n");
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	(void) snprintf(tray_rev, sizeof (tray_rev), "%u", tinv->vpdid_rev);

	if (strcmp(tray_pn, info->ft_cpn) != 0) {
		topo_mod_dprintf(mod, "found unexpected CPN for fan tray: %s, "
		    "not creating\n", tray_pn);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if ((ret = topo_oxhc_tn_create(mod, pn, &tray_tn, FANTRAY, min, auth,
	    tray_pn, tray_rev, tray_sn, TOPO_OXHC_TN_F_FRU_SELF |
	    TOPO_OXHC_TN_F_SET_LABEL, "Fan Tray")) == -1) {
		ret = -1;
		goto out;
	}

	auth = topo_oxhc_auth(mod, oxhc, oe, pn);
	if (auth == NULL) {
		topo_mod_dprintf(mod, "failed to get auth data for %s[%" PRIu64
		    "]: %s\n", topo_node_name(pn), topo_node_instance(pn),
		    topo_mod_errmsg(mod));
		ret = -1;
		goto out;
	}

	if ((ret = topo_node_range_create(mod, tray_tn, FAN, 0,
	    ARRAY_SIZE(tray.ft_fans) - 1)) != 0) {
		topo_mod_dprintf(mod, "failed to create fan range: %s\n",
		    topo_mod_errmsg(mod));
		goto out;
	}

	if ((ret = topo_node_range_create(mod, tray_tn, BOARD, 0, 0)) != 0) {
		topo_mod_dprintf(mod, "failed to create fan range: %s\n",
		    topo_mod_errmsg(mod));
		goto out;
	}

	/*
	 * Check to see if we have sensors available for the fans. This may not
	 * exist in all version of the software so we treat the lack of it as a
	 * non-failure and set the sensor IDs to UINT32_MAX, which the fan
	 * enum will take as a cue not to do anything here.
	 */
	sense_inv = topo_oxhc_inventory_find(oxhc, info->ft_ctrl);
	if (sense_inv == NULL || !topo_oxhc_inventory_bcopy(sense_inv,
	    IPCC_INVENTORY_T_MAX31790, &max31790, sizeof (max31790),
	    sizeof (max31790))) {
		(void) memset(&max31790, 0xff, sizeof (max31790));
	}

	for (size_t i = 0; i < ARRAY_SIZE(tray.ft_fans); i++) {
		const ipcc_inv_vpdid_t *fan_vpd = &tray.ft_fans[i];

		if ((ret = topo_oxhc_enum_fan(mod, fan_vpd, tray_tn, i, auth,
		    info->ft_labels[i], info->ft_dirs[i],
		    max31790.max_tach, info->ft_sensors[i])) != 0) {
			goto out;
		}
	}

	binv = &tray.ft_board;
	if ((board_pn = topo_mod_clean_strn(mod, (const char *)binv->vpdid_pn,
	    sizeof (tinv->vpdid_pn))) == NULL ||
	    (board_sn = topo_mod_clean_strn(mod, (const char *)binv->vpdid_sn,
	    sizeof (tinv->vpdid_sn))) == NULL) {
		topo_mod_dprintf(mod, "failed to clean up fan tray strings\n");
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}
	(void) snprintf(board_rev, sizeof (board_rev), "%u", binv->vpdid_rev);

	if ((ret = topo_oxhc_tn_create(mod, tray_tn, &board_tn, BOARD, min,
	    auth, board_pn, board_rev, board_sn, 0, NULL)) == -1) {
		goto out;
	}

	ret = topo_oxhc_enum_ic(mod, oxhc, board_tn, info->ft_vpd,
	    binv->vpdid_rev, oxhc_ic_fanvpd, oxhc_ic_fanvpd_nents);

out:
	nvlist_free(auth);
	topo_mod_strfree(mod, board_pn);
	topo_mod_strfree(mod, board_sn);
	topo_mod_strfree(mod, tray_pn);
	topo_mod_strfree(mod, tray_sn);
	return (ret);
}

int
topo_oxhc_enum_gimlet_fan_tray(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	return (topo_oxhc_enum_fan_tray(mod, oxhc, oe, pn, tn, min, max,
	    &gimlet_tray_info));
}

int
topo_oxhc_enum_cosmo_fan_tray(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	return (topo_oxhc_enum_fan_tray(mod, oxhc, oe, pn, tn, min, max,
	    &cosmo_tray_info));
}
