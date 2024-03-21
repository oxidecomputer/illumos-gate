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
 * This file implements and deals with logic to enumerate various ICs
 * (integrated circuits) that exist on the boards. This combines information
 * from IPCC and static information about the board itself to flesh out topology
 * information.
 */

#include <string.h>
#include <sys/sysmacros.h>
#include <sys/bitext.h>
#include <endian.h>

#include "oxhc.h"

typedef struct oxhc_ic_hc oxhc_ic_hc_t;
typedef struct oxhc_ic_info oxhc_ic_info_t;

/*
 * This structure represents information that should be used in the construct of
 * the HC FMRI. When constructing a node, some units need to use dynamic
 * construction of the topo node's information and do string cleaning. In those
 * cases, they should set the _dyn members which will be freed upon completion.
 */
struct oxhc_ic_hc {
	const char *oih_pn;
	const char *oih_rev;
	const char *oih_serial;
	char *oih_pn_dyn;
	char *oih_rev_dyn;
	char *oih_serial_dyn;
	libipcc_inv_t *oih_inv;
};

/*
 * While constructing information for IC FMRIs, we want to distinguish between
 * internal errors like running out of memory, from bad or invalid communication
 * from the SP. Returning OXHC_IC_FMRI_DEFAULT is the equivalent to what we do
 * when such a function doesn't exist.
 *
 * A reasonable question is when should we use the default static information
 * versus when should we not. In general we would like to be in a world where
 * most identifying information is derived from the dynamic information rather
 * than us making assumptions and noting the full set of alternates that could
 * exist as part of a given revision. For most of the boards that we process we
 * will be able to get a CPN and revision which means we will know what the set
 * of items on the board could be, the primary exception is the temp sensor
 * board which doesn't have a separate FRU. That may suggest that its the board
 * that should make this determination as to whether or not to use the defaults
 * rather than the individual function as we've opted for right now.
 */
typedef enum {
	OXHC_IC_FMRI_OK,
	OXHC_IC_FMRI_ERR,
	OXHC_IC_FMRI_DEFAULT
} oxhc_ic_fmri_ret_t;

/*
 * Information about a sensor to create for an IC. For now, all sensors are
 * assumed to be MGS remote threshold sensors. This can be expanded based on
 * need. A given sensor's name is used to reflect semantic information about
 * what the sensor is for. For a single temperature sensor for a device this may
 * be something simple like "temp". For a power controller we want not only the
 * rail name, but also what it is as we often have more than one sensor of a
 * given type (e.g. a Vin, Vout, Vout_min, Vout_max, etc.).
 */
typedef struct {
	const char *is_name;
	uint32_t is_type;
	uint32_t is_unit;
	size_t is_offset;
} oxhc_ic_sensor_t;

/*
 * These function protoypes are used for determining the core pieces of the FMRI
 * to add and then the latter is a chance to decorate information on the node
 * like a UFM.
 */
typedef oxhc_ic_fmri_ret_t (*oxhc_ic_fmri_f)(topo_mod_t *,
    const oxhc_ic_info_t *, oxhc_ic_hc_t *);
typedef bool (*oxhc_ic_enum_f)(topo_mod_t *, const oxhc_ic_info_t *,
    const oxhc_ic_hc_t *, tnode_t *);

struct oxhc_ic_info {
	const char *ic_refdes;
	const char *ic_cpn;
	const char *ic_mfg;
	const char *ic_mpn;
	const char *ic_use;
	oxhc_ic_fmri_f ic_fmri;
	oxhc_ic_enum_f ic_enum;
};

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_adm1272_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_adm1272_t adm;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_ADM1272,
	    &adm, sizeof (adm), offsetof(ipcc_inv_adm1272_t, adm_temp))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if ((hc->oih_rev_dyn = topo_mod_clean_strn(mod,
	    (const char *)adm.adm_mfr_rev, sizeof (adm.adm_mfr_rev))) == NULL) {
		topo_mod_dprintf(mod, "failed to clean up strings for %s\n",
		    ic_info->ic_refdes);
		(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		return (OXHC_IC_FMRI_ERR);
	}

	hc->oih_pn = ic_info->ic_mpn;
	hc->oih_rev = hc->oih_rev_dyn;

	return (OXHC_IC_FMRI_OK);
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_bmr491_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_bmr491_t bmr;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_BRM491,
	    &bmr, sizeof (bmr), offsetof(ipcc_inv_bmr491_t, bmr_temp))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if ((hc->oih_pn_dyn = topo_mod_clean_strn(mod,
	    (const char *)bmr.bmr_mfr_model, sizeof (bmr.bmr_mfr_model))) ==
	    NULL ||
	    (hc->oih_rev_dyn = topo_mod_clean_strn(mod,
	    (const char *)bmr.bmr_mfr_rev, sizeof (bmr.bmr_mfr_rev))) ==
	    NULL ||
	    (hc->oih_serial_dyn = topo_mod_clean_strn(mod,
	    (const char *)bmr.bmr_mfr_serial, sizeof (bmr.bmr_mfr_serial))) ==
	    NULL) {
		topo_mod_dprintf(mod, "failed to clean up strings for %s\n",
		    ic_info->ic_refdes);
		(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		return (OXHC_IC_FMRI_ERR);
	}

	hc->oih_pn = hc->oih_pn_dyn;
	hc->oih_rev = hc->oih_rev_dyn;
	hc->oih_serial = hc->oih_serial_dyn;

	return (OXHC_IC_FMRI_OK);
}

static bool
topo_oxhc_ic_bmr491_enum(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    const oxhc_ic_hc_t *hc, tnode_t *tn)
{
	ipcc_inv_bmr491_t bmr;
	char *fwrev;
	tnode_t *ufm;
	topo_ufm_slot_info_t slot = { 0 };

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (true);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_BRM491,
	    &bmr, sizeof (bmr), sizeof (bmr))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (true);
	}

	fwrev = topo_mod_clean_strn(mod, (const char *)bmr.bmr_mfr_fw,
	    sizeof (bmr.bmr_mfr_fw));
	if (fwrev == NULL) {
		topo_mod_dprintf(mod, "failed to clean up firmware string for "
		    "%s\n", ic_info->ic_refdes);
		(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		return (false);
	}

	slot.usi_slotid = 0;
	slot.usi_mode = TOPO_UFM_SLOT_MODE_WO;
	slot.usi_version = fwrev;
	slot.usi_active = B_TRUE;
	slot.usi_extra = NULL;

	if (topo_node_range_create(mod, tn, UFM, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create UFM range for %s: %s\n",
		    ic_info->ic_refdes, topo_mod_errmsg(mod));
		topo_mod_strfree(mod, fwrev);
		return (false);
	}

	ufm = topo_mod_create_ufm(mod, tn, 0, "firmware", &slot);
	topo_mod_strfree(mod, fwrev);

	if (ufm == NULL) {
		topo_mod_dprintf(mod, "failed to create UFM for %s: %s\n",
		    ic_info->ic_refdes, topo_mod_errmsg(mod));
		return (false);
	}

	return (true);
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_at24csw_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_at24csw_t at24;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_AT24CSW,
	    &at24, sizeof (at24), sizeof (at24))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (topo_mod_asprintf(mod, &hc->oih_serial_dyn, "%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
	    at24.at24_serial[0], at24.at24_serial[1], at24.at24_serial[2],
	    at24.at24_serial[3], at24.at24_serial[4], at24.at24_serial[5],
	    at24.at24_serial[6], at24.at24_serial[7], at24.at24_serial[8],
	    at24.at24_serial[9], at24.at24_serial[10], at24.at24_serial[11],
	    at24.at24_serial[12], at24.at24_serial[13], at24.at24_serial[14],
	    at24.at24_serial[15]) == -1) {
		topo_mod_dprintf(mod, "failed to create AT24CSW serial "
		    "string for %s: %s\n", ic_info->ic_refdes, strerror(errno));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (OXHC_IC_FMRI_ERR);
	}

	hc->oih_pn = ic_info->ic_mpn;
	hc->oih_serial = hc->oih_serial_dyn;

	return (OXHC_IC_FMRI_OK);
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_stm32_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_stm32h7_t h7;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_STM32H7,
	    &h7, sizeof (h7), sizeof (h7))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	hc->oih_pn = ic_info->ic_mpn;

	/*
	 * These IDs come from the DBGMCU register (60.5.8) of the STM32H753
	 * reference manual. Doc ID RM0433.
	 */
	switch (h7.stm_revid) {
	case 0x1001:
		hc->oih_rev = "Z";
		break;
	case 0x1003:
		hc->oih_rev = "Y";
		break;
	case 0x2001:
		hc->oih_rev = "X";
		break;
	case 0x2003:
		hc->oih_rev = "V";
		break;
	default:
		topo_mod_dprintf(mod, "encountered unknown revision of the H7: "
		    "0x%x\n", h7.stm_revid);
		break;
	}

	if (topo_mod_asprintf(mod, &hc->oih_serial_dyn, "%08x%08x%08x",
	    h7.stm_uid[2], h7.stm_uid[1], h7.stm_uid[0]) == -1) {
		topo_mod_dprintf(mod, "failed to construct STM32H7 sn for "
		    "%s: %s\n", ic_info->ic_refdes, strerror(errno));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (OXHC_IC_FMRI_ERR);
	}
	hc->oih_serial = hc->oih_serial_dyn;

	return (OXHC_IC_FMRI_OK);
}

typedef struct {
	const char *tim_name;
	uint8_t tim_ic_id[6];
} tps546_ic_map_t;

static const tps546_ic_map_t tps_map[] = {
	/*
	 * This information comes from Table 7-90 in the Sep 2020 TPS546B24A
	 * datasheet (SLUSE15A).
	 */
	{ "TPS546B24A", { 0x54, 0x49, 0x54, 0x6d, 0x24, 0x41 } }
};

/*
 * Most of the TPS546B24A's properties are left for the manufacturer to fill in.
 * We end up mostly with an IC id and a revision.
 */
static oxhc_ic_fmri_ret_t
topo_oxhc_ic_tps546_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	uint8_t major, minor;
	ipcc_inv_tps546b24a_t tps;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_TPS546B24A,
	    &tps, sizeof (tps), offsetof(ipcc_inv_tps546b24a_t, tps_temp))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	for (size_t i = 0; i < ARRAY_SIZE(tps_map); i++) {
		if (memcmp(tps.tps_ic_id, tps_map[i].tim_ic_id,
		    sizeof (tps.tps_ic_id)) == 0) {
			hc->oih_pn = tps_map[i].tim_name;
			break;
		}
	}

	major = bitx8(tps.tps_ic_rev[1], 7, 4);
	minor = bitx8(tps.tps_ic_rev[1], 3, 0);
	if (topo_mod_asprintf(mod, &hc->oih_rev_dyn, "%u.%u.%u", major, minor,
	    tps.tps_ic_rev[0]) == -1) {
		topo_mod_dprintf(mod, "failed to create TPS revision string "
		    "for %s: %s\n", ic_info->ic_refdes, strerror(errno));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (OXHC_IC_FMRI_ERR);
	}

	hc->oih_rev = hc->oih_rev_dyn;

	return (OXHC_IC_FMRI_OK);
}

typedef struct {
	const char *ren_name;
	uint8_t ren_ic_id[4];
} renesas_ic_map_t;

static const renesas_ic_map_t ren_ic_map[] = {
	{ "ISL68224", { 0x00, 0x52, 0xd2, 0x49 } },
	{ "RAA229618", { 0x00, 0x99, 0xd2, 0x49 } }
};

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_renpow_fmri_common(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc, const uint8_t id[4], const uint8_t rev[4])
{
	for (size_t i = 0; i < ARRAY_SIZE(ren_ic_map); i++) {
		if (memcmp(id, ren_ic_map[i].ren_ic_id,
		    sizeof (ren_ic_map[i].ren_ic_id)) == 0) {
			hc->oih_pn = ren_ic_map[i].ren_name;
			break;
		}
	}

	if (hc->oih_pn == NULL) {
		topo_mod_dprintf(mod, "failed to map refdes %s id (0x%02x "
		    "0x%02x 0x%02x 0x%02x to a known Renesas IC",
		    ic_info->ic_refdes, id[0], id[1], id[2], id[3]);
	}

	if (topo_mod_asprintf(mod, &hc->oih_rev_dyn, "0x%x", rev[3]) == -1) {
		topo_mod_dprintf(mod, "failed to create Renesas revision "
		    "string for %s: %s\n", ic_info->ic_refdes, strerror(errno));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (OXHC_IC_FMRI_ERR);
	}

	hc->oih_rev = hc->oih_rev_dyn;

	return (OXHC_IC_FMRI_OK);

}

/*
 * Construct a UFM for the various Renesas power controllers. There are two
 * parts that make up the firmware version here. There is the Renesas blob
 * itself which is versioned in ic_rev[0] and then there is our own version of
 * the configuration which is in mfr_rev which is really a little endian u32.
 * The date is when this was constructed as hex-encoded ASCII which we add as
 * misc. data to the UFM.
 */
static bool
topo_oxhc_ic_renpow_enum_common(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    tnode_t *tn, const uint8_t mfr_rev[4], const uint8_t mfr_date[4],
    const uint8_t ic_rev[4])
{
	int ret;
	char fwrev[16], oxrev[16], date[32];
	tnode_t *ufm;
	topo_ufm_slot_info_t slot = { 0 };
	uint32_t rev;
	nvlist_t *extra = NULL;

	rev = mfr_rev[0];
	rev |= (uint32_t)mfr_rev[1] << 8;
	rev |= (uint32_t)mfr_rev[2] << 16;
	rev |= (uint32_t)mfr_rev[3] << 24;

	(void) snprintf(fwrev, sizeof (fwrev), "0x%x", ic_rev[0]);
	(void) snprintf(oxrev, sizeof (oxrev), "0x%x", rev);
	(void) snprintf(date, sizeof (date), "%02x%02x%02x%02x", mfr_date[3],
	    mfr_date[2], mfr_date[1], mfr_date[0]);

	if ((ret = nvlist_alloc(&extra, NV_UNIQUE_NAME, 0)) != 0) {
		topo_mod_dprintf(mod, "failed to create UFM extras NVL for "
		    "%s: %s\n", ic_info->ic_refdes, strerror(ret));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (false);
	}

	if ((ret = nvlist_add_string(extra, "build-date", date)) != 0 ||
	    (ret = nvlist_add_string(extra, "build-id", oxrev)) != 0) {
		nvlist_free(extra);
	}

	slot.usi_slotid = 0;
	slot.usi_mode = TOPO_UFM_SLOT_MODE_WO;
	slot.usi_version = fwrev;
	slot.usi_active = B_TRUE;
	slot.usi_extra = extra;

	if (topo_node_range_create(mod, tn, UFM, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create UFM range for %s: %s\n",
		    ic_info->ic_refdes, topo_mod_errmsg(mod));
		nvlist_free(extra);
		return (false);
	}

	ufm = topo_mod_create_ufm(mod, tn, 0, "firmware", &slot);
	nvlist_free(extra);

	if (ufm == NULL) {
		topo_mod_dprintf(mod, "failed to create UFM for %s: %s\n",
		    ic_info->ic_refdes, topo_mod_errmsg(mod));
		return (false);
	}

	return (true);
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_raa229618_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_raa229618_t ren;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_RAA229618,
	    &ren, sizeof (ren), sizeof (ren))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	return (topo_oxhc_ic_renpow_fmri_common(mod, ic_info, hc, ren.raa_ic_id,
	    ren.raa_ic_rev));
}

static bool
topo_oxhc_ic_raa229618_enum(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    const oxhc_ic_hc_t *hc, tnode_t *tn)
{
	ipcc_inv_raa229618_t ren;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (true);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_RAA229618,
	    &ren, sizeof (ren), offsetof(ipcc_inv_raa229618_t,
	    raa_stage_temp_max[0]))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (true);
	}

	return (topo_oxhc_ic_renpow_enum_common(mod, ic_info, tn,
	    ren.raa_mfr_rev, ren.raa_mfr_date, ren.raa_ic_rev));
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_isl68224_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_isl68224_t ren;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_ISL68224,
	    &ren, sizeof (ren), offsetof(ipcc_inv_isl68224_t,
	    isl_rail_vout[0]))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	return (topo_oxhc_ic_renpow_fmri_common(mod, ic_info, hc, ren.isl_ic_id,
	    ren.isl_ic_rev));
}

static bool
topo_oxhc_ic_isl68224_enum(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    const oxhc_ic_hc_t *hc, tnode_t *tn)
{
	ipcc_inv_isl68224_t ren;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (true);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_ISL68224,
	    &ren, sizeof (ren), offsetof(ipcc_inv_isl68224_t,
	    isl_rail_vout[0]))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (true);
	}

	return (topo_oxhc_ic_renpow_enum_common(mod, ic_info, tn,
	    ren.isl_mfr_rev, ren.isl_mfr_date, ren.isl_ic_rev));
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_ksz8463_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_ksz8463_t ksz;
	uint16_t fam, chip, rev;

	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_KSZ8463,
	    &ksz, sizeof (ksz), sizeof (ksz))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_DEFAULT);
	}

	/*
	 * The following come from Table 4-7 of the KSZ8463 datasheet (doc
	 * DS00002642A). The CIDER register has the family (0x84) in bits 15-8.
	 * The chip id is in bits 7-4 and only has definitions for values of 0x4
	 * and 0x5. The chip revision is in bits 3-1. Bit 0 is not for ID
	 * purposes and is actually an enable.
	 */
	fam = bitx16(ksz.ksz_cider, 15, 8);
	chip = bitx16(ksz.ksz_cider, 7, 4);
	rev = bitx16(ksz.ksz_cider, 3, 1);

	if (fam == 0x84 && chip == 0x4) {
		hc->oih_pn = "KSZ8463ML/FML";
	} else if (fam == 0x85 && chip == 0x5) {
		hc->oih_pn = "KSZ8463RL/FRL";
	}

	if (topo_mod_asprintf(mod, &hc->oih_rev_dyn, "%u", rev) == -1) {
		topo_mod_dprintf(mod, "failed to create KSZ revision "
		    "string for %s: %s\n", ic_info->ic_refdes, strerror(errno));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (OXHC_IC_FMRI_ERR);
	}
	hc->oih_rev = hc->oih_rev_dyn;

	return (OXHC_IC_FMRI_OK);
}

static oxhc_ic_fmri_ret_t
topo_oxhc_ic_tmp11x_fmri(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    oxhc_ic_hc_t *hc)
{
	ipcc_inv_tmp11x_t tmp;
	uint16_t rev, did, idreg;

	/*
	 * The TMP117 generally only shows up on the small temp sensor boards
	 * right now. If we can't get inventory information we may have
	 * something different or unexpected plugged in. As such we don't
	 * actually want to claim anything is here that we expect. That is why
	 * for this case we return an error. For the case where we have
	 * inventory information about the refdes but cannot get information
	 * about it, again because this is showing up on the temperature sensor
	 * board and this is the primary identifying information we opt to leave
	 * return OK so we don't populate anything from our default fields.
	 *
	 * If this ends up getting used on a different component then we'll want
	 * to change some of this logic to be based on the board.
	 */
	if (hc->oih_inv == NULL) {
		topo_mod_dprintf(mod, "missing IPCC information for %s\n",
		    ic_info->ic_refdes);
		return (OXHC_IC_FMRI_ERR);
	}

	if (!topo_oxhc_inventory_bcopy(hc->oih_inv, IPCC_INVENTORY_T_TMP117,
	    &tmp, sizeof (tmp), offsetof(ipcc_inv_tmp11x_t, tmp_temp))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ic_info->ic_refdes);
		return (OXHC_IC_FMRI_OK);
	}

	/*
	 * The TMP116/117 share a similar Device ID register which is defined in
	 * Section 7.6.11 of the TMP117 datasheet (SNOSD82C). Because the tmp11x
	 * data is all read over I2C, we get it as a byte array and we must
	 * reconstruct its intended data as a little endian value. If we don't
	 * recognize the device ID, assume this is something weird and don't
	 * populate any data.
	 */
	idreg = be16toh(tmp.tmp_id);
	rev = bitx16(idreg, 15, 12);
	did = bitx16(idreg, 11, 0);
	if (did == 0x116) {
		hc->oih_pn = "TMP116";
	} else if (did == 0x117) {
		hc->oih_pn = "TMP117";
	}

	if (topo_mod_asprintf(mod, &hc->oih_rev_dyn, "%u", rev) == -1 ||
	    topo_mod_asprintf(mod, &hc->oih_serial_dyn, "%04x%04x%04x%04x",
	    idreg, be16toh(tmp.tmp_ee3), be16toh(tmp.tmp_ee2),
	    be16toh(tmp.tmp_ee1)) == -1) {
		topo_mod_dprintf(mod, "failed to create TMP 11x "
		    "revision/serial string for %s: %s\n", ic_info->ic_refdes,
		    strerror(errno));
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		return (OXHC_IC_FMRI_ERR);
	}

	hc->oih_rev = hc->oih_rev_dyn;
	hc->oih_serial = hc->oih_serial_dyn;

	return (OXHC_IC_FMRI_OK);
}

/*
 * For the T6 IC we would like to enumerate a UFM and the I/O property group. To
 * do this we need to find the root port that refers to this on the board and
 * then look in the devinfo tree. On Gimlet, the root port is identified by
 * having the slot 'pcie16'. If we can't find a child node, that's fine, nothing
 * may be attached. If we don't find the bridge, that's quite weird and that is
 * a fatal error.
 */
static bool
topo_oxhc_ic_t6_enum(topo_mod_t *mod, const oxhc_ic_info_t *ic_info,
    const oxhc_ic_hc_t *hc, tnode_t *tn)
{
	di_node_t bridge = topo_oxhc_slot_to_devi(mod, 0x10);

	if (bridge == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "failed to find T6 bridge pcie16!");
		(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		return (false);
	}

	/*
	 * Find the instance of t6mfg or t4nex that exists if any, in the
	 * snapshot. That will the basis for which we'll try to construct an I/O
	 * property group and UFMs.
	 */
	for (di_node_t di = di_child_node(bridge); di != DI_NODE_NIL;
	    di = di_sibling_node(di)) {
		const char *drv = di_driver_name(di);
		if (drv == NULL)
			continue;

		if (strcmp(drv, "t6mfg") == 0 || strcmp(drv, "t4nex") == 0) {
			return (topo_oxhc_enum_pcie(mod, tn, di) == 0);
		}
	}

	return (true);
}

static const oxhc_ic_info_t oxhc_ic_adm1272 = {
	.ic_cpn = "221-0000076", .ic_mfg = "Analog Devices",
	.ic_mpn = "ADM1272-1ACPZ-RL", .ic_fmri = topo_oxhc_ic_adm1272_fmri
};

static const oxhc_ic_info_t oxhc_ic_ign = {
	.ic_cpn = "221-0000083", .ic_mfg = "Lattice", .ic_mpn = "ice40lp1k-qn84"
};

static const oxhc_ic_info_t oxhc_ic_bmr491 = {
	.ic_cpn = "229-0000025", .ic_mfg = "Flex", .ic_mpn = "BMR491-0203/851",
	.ic_fmri = topo_oxhc_ic_bmr491_fmri, .ic_enum = topo_oxhc_ic_bmr491_enum
};

static const oxhc_ic_info_t oxhc_ic_at24csw = {
	.ic_cpn = "225-0000016", .ic_mfg = "Microchip",
	.ic_mpn = "AT24CSW080-STUM-T", .ic_fmri = topo_oxhc_ic_at24csw_fmri
};

static const oxhc_ic_info_t oxhc_ic_stm32h7 = {
	.ic_cpn = "221-0000039", .ic_mfg = "ST", .ic_mpn = "STM32H753XIH6TR",
	.ic_fmri = topo_oxhc_ic_stm32_fmri
};

static const oxhc_ic_info_t oxhc_ic_lpc55s69 = {
	.ic_cpn = "221-0000099", .ic_mfg = "NXP", .ic_mpn = "LPC55S69JBD64K"
};

static const oxhc_ic_info_t oxhc_ic_mt25qu256 = {
	.ic_cpn = "225-0000008", .ic_mfg = "Micron",
	.ic_mpn = "MT25QU256ABA8E12-0AUT"
};

static const oxhc_ic_info_t oxhc_ic_max31790 = {
	.ic_cpn = "221-0000016", .ic_mfg = "Maxim", .ic_mpn = "MAX31790ATI+T"
};

static const oxhc_ic_info_t oxhc_ic_9dbl0455 = {
	.ic_cpn = "221-0000074", .ic_mfg = "Renesas", .ic_mpn = "9DBL0455"
};

static const oxhc_ic_info_t oxhc_ic_pca9545 = {
	.ic_cpn = "221-0000071", .ic_mfg = "NXP", .ic_mpn = "PCA9545ABS"
};

static const oxhc_ic_info_t oxhc_ic_pca9506 = {
	.ic_cpn = "221-0000077", .ic_mfg = "NXP", .ic_mpn = "PCA9506BS,118"
};

static const oxhc_ic_info_t oxhc_ic_pca9535 = {
	.ic_cpn = "221-0000139", .ic_mfg = "NXP", .ic_mpn = "PCA9535BS,118"
};

static const oxhc_ic_info_t oxhc_ic_at2526b = {
	.ic_cpn = "225-0000005", .ic_mfg = "Microchip", .ic_mpn = "AT25256B"
};

static const oxhc_ic_info_t oxhc_ic_mt25ql128 = {
	.ic_cpn = "225-0000006", .ic_mfg = "Micron",
	.ic_mpn = "MT25QL128ABB8E12-0AUT"
};

static const oxhc_ic_info_t oxhc_ic_tmp451 = {
	.ic_cpn = "221-0000086", .ic_mfg = "TI", .ic_mpn = "TMP451AQDQWRQ1"
};

static const oxhc_ic_info_t oxhc_ic_tps546b = {
	.ic_cpn = "229-0000018", .ic_mfg = "TI", .ic_mpn = "TPS546B24A",
	.ic_fmri = topo_oxhc_ic_tps546_fmri
};

static const oxhc_ic_info_t oxhc_ic_tps62913 = {
	.ic_cpn = "221-0000096", .ic_mfg = "TI", .ic_mpn = "TPS62913RPUR"
};

static const oxhc_ic_info_t oxhc_ic_lt3072 = {
	.ic_cpn = "221-0000073", .ic_mfg = "Analog Devices", .ic_mpn = "LT3072"
};

static const oxhc_ic_info_t oxhc_ic_8a34003 = {
	.ic_cpn = "229-0000021", .ic_mfg = "Renesas", .ic_mpn = "8A34003E"
};

static const oxhc_ic_info_t oxhc_ic_max17651 = {
	.ic_cpn = "221-0000150", .ic_mfg = "Maxim", .ic_mpn = "MAX17651AZT+"
};

static const oxhc_ic_info_t oxhc_ic_raa229618 = {
	.ic_cpn = "221-0000037", .ic_mfg = "Renesas", .ic_mpn = "RAA229618",
	.ic_fmri = topo_oxhc_ic_raa229618_fmri,
	.ic_enum = topo_oxhc_ic_raa229618_enum
};

static const oxhc_ic_info_t oxhc_ic_isl68224 = {
	.ic_cpn = "221-0000072", .ic_mfg = "Renesas", .ic_mpn = "ISL68224",
	.ic_fmri = topo_oxhc_ic_isl68224_fmri,
	.ic_enum = topo_oxhc_ic_isl68224_enum
};

static const oxhc_ic_info_t oxhc_ic_isl99390 = {
	.ic_cpn = "229-0000011", .ic_mfg = "Renesas", .ic_mpn = "ISL99390FRZ"
};

static const oxhc_ic_info_t oxhc_ic_tps51200 = {
	.ic_cpn = "221-0000078", .ic_mfg = "TI", .ic_mpn = "TPS51200"
};

static const oxhc_ic_info_t oxhc_ic_tps746 = {
	.ic_cpn = "229-0000017", .ic_mfg = "TI", .ic_mpn = "TPS746-Q1"
};

static const oxhc_ic_info_t oxhc_ic_lp5907 = {
	.ic_cpn = "221-0000109", .ic_mfg = "TI", .ic_mpn = "LPC5907-Q1"
};

static const oxhc_ic_info_t oxhc_ic_ksz8463 = {
	.ic_cpn = "221-0000032", .ic_mfg = "Microchip", .ic_mpn = "KSZ8463FRLI",
	.ic_fmri = topo_oxhc_ic_ksz8463_fmri
};

static const oxhc_ic_info_t oxhc_ic_vsc8552 = {
	.ic_cpn = "221-0000033", .ic_mfg = "Microchip",
	.ic_mpn = "VSC8552XKS-02"
};

static const oxhc_ic_info_t oxhc_ic_ice40seq = {
	.ic_cpn = "221-0000084", .ic_mfg = "Lattice",
	.ic_mpn = "ICE40HX8K-CT256"
};

static const oxhc_ic_info_t oxhc_ic_tmp117 = {
	.ic_cpn = "234-0000002", .ic_mfg = "TI",
	.ic_mpn = "TMP117", .ic_fmri = topo_oxhc_ic_tmp11x_fmri
};

static const oxhc_ic_info_t oxhc_ic_max5970 = {
	.ic_cpn = "221-0000070", .ic_mfg = "Maxim",
	.ic_mpn = "MAX5970ETX"
};

static const oxhc_ic_info_t oxhc_ic_t6 = {
	.ic_cpn = "221-0000024", .ic_mfg = "Chelsio",
	.ic_mpn = "T6ASIC2100", .ic_enum = topo_oxhc_ic_t6_enum
};

/*
 * This represents information about a single board that we want to deal with.
 * The ib_min_rev being left at zero means that it applies to all boards. As
 * right now we don't have anything that's been removed in a rev, we don't have
 * a max present, but that could be added. The sensors are tied to this
 * structure as the semantics of the sensors are most often specific to the
 * reference designator.
 */
typedef struct {
	const char *ib_refdes;
	const oxhc_ic_info_t *ib_info;
	uint32_t ib_min_rev;
	const oxhc_ic_sensor_t *ib_sensors;
	size_t ib_nsensors;
} oxhc_ic_board_t;

/*
 * Note, we currently don't plumb through the power readings as while there's a
 * sensor ID for it, there isn't an obvious corresponding thing it is reading.
 */
static const oxhc_ic_sensor_t oxhc_ic_bmr491_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_bmr491_t, bmr_temp)
}, {
	.is_name = "V12_SYS_A2:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_bmr491_t, bmr_vout)
}, {
	.is_name = "V12_SYS_A2:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_bmr491_t, bmr_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_vpp_sensors[] = { {
	.is_name = "VPP_ABCD:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_isl68224_t, isl_rail_vout[0])
}, {
	.is_name = "VPP_EFGH:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_isl68224_t, isl_rail_vout[1])
}, {
	.is_name = "V1P8_SP3:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_isl68224_t, isl_rail_vout[2])
}, {
	.is_name = "VPP_ABCD:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_isl68224_t, isl_rail_iout[0])
}, {
	.is_name = "VPP_EFGH:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_isl68224_t, isl_rail_iout[1])
}, {
	.is_name = "V1P8_SP3:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_isl68224_t, isl_rail_iout[2])
} };

static const oxhc_ic_sensor_t oxhc_ic_raa_vcore_sensors[] = { {
	.is_name = "VDD_VCORE:stage-max",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_stage_temp_max[0])
}, {
	.is_name = "VDD_MEM_ABCD:stage-max",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_stage_temp_max[1])
}, {
	.is_name = "VDD_VCORE:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_vout[0])
}, {
	.is_name = "VDD_MEM_ABCD:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_vout[1])
}, {
	.is_name = "VDD_VCORE:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_iout[0])
}, {
	.is_name = "VDD_MEM_ABCD:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_iout[0])
} };

static const oxhc_ic_sensor_t oxhc_ic_raa_vsoc_sensors[] = { {
	.is_name = "VDD_VSOC:stage-max",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_stage_temp_max[0])
}, {
	.is_name = "VDD_MEM_EFGH:stage-max",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_stage_temp_max[1])
}, {
	.is_name = "VDD_VSOC:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_vout[0])
}, {
	.is_name = "VDD_MEM_EFGH:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_vout[1])
}, {
	.is_name = "VDD_VSOC:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_iout[0])
}, {
	.is_name = "VDD_MEM_EFGH:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_raa229618_t, raa_rail_iout[0])
} };

static const oxhc_ic_sensor_t oxhc_ic_tps_v3p3_a2_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_temp)
}, {
	.is_name = "V3P3_SP_A2:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_vout)
}, {
	.is_name = "V3P3_SP_A2:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_tps_v3p3_a0_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_temp)
}, {
	.is_name = "V3P3_SYS_A0:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_vout)
}, {
	.is_name = "V3P3_SYS_A0:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_tps_v5p0_a2_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_temp)
}, {
	.is_name = "V5_SYS_A2:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_vout)
}, {
	.is_name = "V5_SYS_A2:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_tps_v1p8_a2_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_temp)
}, {
	.is_name = "V1P8_SYS_A2:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_vout)
}, {
	.is_name = "V1P8_SYS_A2:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_tps_t6_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_temp)
}, {
	.is_name = "V0P96_NIC_VDD_A0HP:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_vout)
}, {
	.is_name = "V0P96_NIC_VDD_A0HP:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_tps546b24a_t, tps_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_max5970_m2_sensors[] = { {
	.is_name = "V3P3_M2A_A0HP:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_vout[0])
}, {
	.is_name = "V3P3_M2B_A0HP:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_vout[1])
}, {
	.is_name = "V3P3_M2A_A0HP:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_iout[0])
}, {
	.is_name = "V3P3_M2B_A0HP:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_iout[1])
} };

static const oxhc_ic_sensor_t oxhc_ic_adm_hs_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_adm1272_t, adm_temp)
}, {
	.is_name = "V54_HS_OUTPUT:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_adm1272_t, adm_vout)
}, {
	.is_name = "V54_HS_OUTPUT:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_adm1272_t, adm_iout)
} };

static const oxhc_ic_sensor_t oxhc_ic_adm_fan_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_adm1272_t, adm_temp)
}, {
	.is_name = "V54_FAN:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_adm1272_t, adm_vout)
}, {
	.is_name = "V54_FAN:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_adm1272_t, adm_iout)
} };

/*
 * To help maintain stable IDs in the tree, items in here should generally only
 * be appended rather than reordered.
 */
static const oxhc_ic_board_t oxhc_ic_gimlet_main[] = {
	{
		.ib_refdes = "U452",
		.ib_info = &oxhc_ic_adm1272,
		.ib_sensors = oxhc_ic_adm_hs_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_adm_hs_sensors)
	},
	{
		.ib_refdes = "U419",
		.ib_info = &oxhc_ic_adm1272,
		.ib_sensors = oxhc_ic_adm_fan_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_adm_fan_sensors)
	},
	/*
	 * It is tempting to include the ignition controller's SPI flash here;
	 * however, there's no way for us to get any other information about it
	 * and therefore fault diagnosis is somewhat difficult to distinguish
	 * between the two.
	 */
	{ .ib_refdes = "U471", .ib_info = &oxhc_ic_ign },
	{
		.ib_refdes = "U431",
		.ib_info = &oxhc_ic_bmr491,
		.ib_sensors = oxhc_ic_bmr491_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_bmr491_sensors)
	},
	{ .ib_refdes = "U615", .ib_info = &oxhc_ic_at24csw },
	{ .ib_refdes = "U12", .ib_info = &oxhc_ic_stm32h7 },
	{ .ib_refdes = "U18", .ib_info = &oxhc_ic_lpc55s69 },
	/*
	 * These next two entries are SPI flashes that we should fill in through
	 * the dynamic properties in them when the SP makes them available
	 * rather than assuming what these are for the time being.
	 */
	{ .ib_refdes = "U493", .ib_info = &oxhc_ic_mt25qu256 },
	{ .ib_refdes = "U556", .ib_info = &oxhc_ic_mt25qu256 },
	{ .ib_refdes = "U321", .ib_info = &oxhc_ic_max31790 },
	{ .ib_refdes = "U377", .ib_info = &oxhc_ic_9dbl0455 },
	{ .ib_refdes = "U388", .ib_info = &oxhc_ic_9dbl0455 },
	{ .ib_refdes = "U389", .ib_info = &oxhc_ic_9dbl0455 },
	{ .ib_refdes = "U376", .ib_info = &oxhc_ic_9dbl0455 },
	{ .ib_refdes = "U336", .ib_info = &oxhc_ic_pca9545 },
	{ .ib_refdes = "U337", .ib_info = &oxhc_ic_pca9545 },
	{ .ib_refdes = "U339", .ib_info = &oxhc_ic_pca9545 },
	{ .ib_refdes = "U422", .ib_info = &oxhc_ic_pca9545 },
	{ .ib_refdes = "U438", .ib_info = &oxhc_ic_pca9506 },
	{ .ib_refdes = "U430", .ib_info = &oxhc_ic_pca9506 },
	{ .ib_refdes = "U307", .ib_info = &oxhc_ic_pca9535 },
	{ .ib_refdes = "U306", .ib_info = &oxhc_ic_pca9535 },
	{ .ib_refdes = "U305", .ib_info = &oxhc_ic_pca9535 },
	{ .ib_refdes = "U308", .ib_info = &oxhc_ic_pca9535 },
	{ .ib_refdes = "U312", .ib_info = &oxhc_ic_at2526b },
	/*
	 * When we come back and add the T6 here, we should not assume that this
	 * is actually a Micron part and instead use the information (whether
	 * via libispi or devinfo) about the device and its vpd directly.
	 */
	{ .ib_refdes = "U314", .ib_info = &oxhc_ic_mt25ql128 },
	{
		.ib_refdes = "U491",
		.ib_info = &oxhc_ic_tmp451,
		.ib_sensors = oxhc_ic_tps_t6_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_tps_t6_sensors)
	},
	{ .ib_refdes = "U565", .ib_info = &oxhc_ic_tps546b },
	{ .ib_refdes = "U612", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U360", .ib_info = &oxhc_ic_lt3072 },
	{ .ib_refdes = "U424", .ib_info = &oxhc_ic_lt3072 },
	{ .ib_refdes = "U630", .ib_info = &oxhc_ic_lt3072 },
	{ .ib_refdes = "U446", .ib_info = &oxhc_ic_8a34003 },
	{ .ib_refdes = "U425", .ib_info = &oxhc_ic_max17651, .ib_min_rev = 6 },
	{ .ib_refdes = "U638", .ib_info = &oxhc_ic_max17651, .ib_min_rev = 6 },
	{ .ib_refdes = "U639", .ib_info = &oxhc_ic_max17651, .ib_min_rev = 6 },
	{ .ib_refdes = "U640", .ib_info = &oxhc_ic_max17651, .ib_min_rev = 6 },
	{ .ib_refdes = "U642", .ib_info = &oxhc_ic_max17651, .ib_min_rev = 6 },
	{ .ib_refdes = "U643", .ib_info = &oxhc_ic_max17651, .ib_min_rev = 6 },
	{
		.ib_refdes = "U350",
		.ib_info = &oxhc_ic_raa229618,
		.ib_sensors = oxhc_ic_raa_vcore_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_raa_vcore_sensors)
	},
	/*
	 * Note, out of order refdes follow the schematic page ordering.
	 */
	{ .ib_refdes = "UP12", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP39", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP13", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP14", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP16", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP17", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP18", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP19", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP0", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP1", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP2", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP3", .ib_info = &oxhc_ic_isl99390 },
	{
		.ib_refdes = "U351",
		.ib_info = &oxhc_ic_raa229618,
		.ib_sensors = oxhc_ic_raa_vsoc_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_raa_vsoc_sensors)
	},
	{ .ib_refdes = "UP26", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP27", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP28", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP29", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP20", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP21", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP22", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP40", .ib_info = &oxhc_ic_isl99390 },
	{
		.ib_refdes = "U352",
		.ib_info = &oxhc_ic_isl68224,
		.ib_sensors = oxhc_ic_vpp_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_vpp_sensors)
	},
	{ .ib_refdes = "UP36", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP37", .ib_info = &oxhc_ic_isl99390 },
	{ .ib_refdes = "UP38", .ib_info = &oxhc_ic_isl99390 },
	{
		.ib_refdes = "U560",
		.ib_info = &oxhc_ic_tps546b,
		.ib_sensors = oxhc_ic_tps_v3p3_a0_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_tps_v3p3_a0_sensors)
	},
	{ .ib_refdes = "U432", .ib_info = &oxhc_ic_tps51200 },
	{ .ib_refdes = "U445", .ib_info = &oxhc_ic_tps51200 },
	{ .ib_refdes = "U563", .ib_info = &oxhc_ic_tps51200 },
	{ .ib_refdes = "U564", .ib_info = &oxhc_ic_tps51200 },
	{
		.ib_refdes = "U522",
		.ib_info = &oxhc_ic_tps546b,
		.ib_sensors = oxhc_ic_tps_v3p3_a2_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_tps_v3p3_a2_sensors)
	},
	{
		.ib_refdes = "U524",
		.ib_info = &oxhc_ic_tps546b,
		.ib_sensors = oxhc_ic_tps_v5p0_a2_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_tps_v5p0_a2_sensors)
	},
	{
		.ib_refdes = "U561",
		.ib_info = &oxhc_ic_tps546b,
		.ib_sensors = oxhc_ic_tps_v1p8_a2_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_tps_v1p8_a2_sensors)
	},
	{ .ib_refdes = "U489", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U488", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U483", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U486", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U490", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U494", .ib_info = &oxhc_ic_tps746 },
	{ .ib_refdes = "U497", .ib_info = &oxhc_ic_lt3072 },
	{ .ib_refdes = "U496", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U562", .ib_info = &oxhc_ic_tps62913 },
	{ .ib_refdes = "U628", .ib_info = &oxhc_ic_lp5907 },
	{ .ib_refdes = "U401", .ib_info = &oxhc_ic_ksz8463 },
	{ .ib_refdes = "U478", .ib_info = &oxhc_ic_vsc8552 },
	{ .ib_refdes = "U476", .ib_info = &oxhc_ic_ice40seq },
	{ .ib_refdes = "U477", .ib_info = &oxhc_ic_t6 },
	{
		.ib_refdes = "U275",
		.ib_info = &oxhc_ic_max5970,
		.ib_sensors = oxhc_ic_max5970_m2_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_max5970_m2_sensors)
	}
};

static const oxhc_ic_sensor_t oxhc_ic_tmp117_sensors[] = { {
	.is_name = "temp",
	.is_type = TOPO_SENSOR_TYPE_TEMP,
	.is_unit = TOPO_SENSOR_UNITS_DEGREES_C,
	.is_offset = offsetof(ipcc_inv_tmp11x_t, tmp_temp)
} };

static const oxhc_ic_board_t oxhc_ic_temp_board[] = { {
	.ib_refdes = "U1",
	.ib_info = &oxhc_ic_tmp117,
	.ib_sensors = oxhc_ic_tmp117_sensors,
	.ib_nsensors = ARRAY_SIZE(oxhc_ic_tmp117_sensors)
} };

static const oxhc_ic_sensor_t oxhc_ic_sharkfin_sensors[] = { {
	.is_name = "V12:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_vout[0])
}, {
	.is_name = "V3P3:vout",
	.is_type = TOPO_SENSOR_TYPE_VOLTAGE,
	.is_unit = TOPO_SENSOR_UNITS_VOLTS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_vout[1])
}, {
	.is_name = "V12:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_iout[0])
}, {
	.is_name = "V3P3:iout",
	.is_type = TOPO_SENSOR_TYPE_CURRENT,
	.is_unit = TOPO_SENSOR_UNITS_AMPS,
	.is_offset = offsetof(ipcc_inv_max5970_t, max_rails_iout[1])
} };

static const oxhc_ic_board_t oxhc_ic_sharkfin[] = {
	{ .ib_refdes = "U7", .ib_info = &oxhc_ic_at24csw },
	{
		.ib_refdes = "U8",
		.ib_info = &oxhc_ic_max5970,
		.ib_sensors = oxhc_ic_sharkfin_sensors,
		.ib_nsensors = ARRAY_SIZE(oxhc_ic_sharkfin_sensors)
	}
};

static const oxhc_ic_board_t oxhc_ic_fanvpd[] = {
	{ .ib_refdes = "U1", .ib_info = &oxhc_ic_at24csw },
};

static bool
topo_oxhc_ic_sensor_create(topo_mod_t *mod, oxhc_ic_hc_t *hc, tnode_t *tn,
    const oxhc_ic_sensor_t *sensor)
{
	ipcc_sensor_id_t mgs_id;

	/*
	 * Attempt to get the sensor ID. This will not exist if the data is not
	 * large enough for this. We will not fail as this may happen as we are
	 * dealing across disparate versions of software between the host and
	 * SP.
	 */
	if (!topo_oxhc_inventory_bcopyoff(hc->oih_inv, &mgs_id, sizeof (mgs_id),
	    sensor->is_offset)) {
		return (true);
	}

	return (topo_oxhc_mgs_sensor(mod, tn, sensor->is_name, sensor->is_type,
	    sensor->is_unit, mgs_id));
}

/*
 * This is our primary entry point to enumerate a single IC entry.
 */
static int
topo_oxhc_enum_ic(topo_mod_t *mod, const oxhc_t *oxhc, uint32_t rev,
    tnode_t *pnode, const oxhc_ic_board_t *ib, topo_instance_t inst,
    const char *board_ipcc)
{
	int ret;
	oxhc_ic_fmri_ret_t fmri_ret;
	nvlist_t *auth = NULL;
	tnode_t *tn;
	oxhc_ic_hc_t hc_info = { 0 };
	oxhc_ic_info_t ic_info;

	topo_mod_dprintf(mod, "creating %s[%" PRIu64 "]: %s\n", IC, inst,
	    ib->ib_refdes);

	/*
	 * Note, this creates holes in the topo space; however, this allows
	 * different revisions to have otherwise consistent entries.
	 */
	if (ib->ib_min_rev > rev) {
		return (0);
	}

	ic_info = *(ib->ib_info);
	ic_info.ic_refdes = ib->ib_refdes;

	/*
	 * If we have a specific functions, that generally implies that we would
	 * like to use IPCC information if it exists. Look for it now. If we
	 * were given a board prefix then we must take that into account when
	 * trying to construct the refdes for IPCC, otherwise we may not find
	 * anything here.
	 */
	if (ic_info.ic_fmri != NULL || ic_info.ic_enum != NULL) {
		const char *lookup;
		char buf[IPCC_INVENTORY_NAMELEN];

		if (board_ipcc != NULL) {
			(void) snprintf(buf, sizeof (buf), "%s/%s", board_ipcc,
			    ic_info.ic_refdes);
			lookup = buf;
		} else {
			lookup = ic_info.ic_refdes;
		}

		hc_info.oih_inv = topo_oxhc_inventory_find(oxhc, lookup);
	}

	fmri_ret = OXHC_IC_FMRI_DEFAULT;
	if (ic_info.ic_fmri != NULL) {
		fmri_ret = ic_info.ic_fmri(mod, &ic_info, &hc_info);
		if (fmri_ret == OXHC_IC_FMRI_ERR) {
			ret = -1;
			goto done;
		}
	}

	if (fmri_ret == OXHC_IC_FMRI_DEFAULT) {
		hc_info.oih_pn = ic_info.ic_mpn;
	}

	auth = topo_mod_auth(mod, pnode);
	if (auth == NULL) {
		topo_mod_dprintf(mod, "failed to get auth data for %s[%" PRIu64
		    "] (%s): %s\n", IC, inst, ic_info.ic_refdes,
		    topo_mod_errmsg(mod));
		ret = -1;
		goto done;
	}

	if (topo_oxhc_tn_create(mod, pnode, &tn, IC, inst, auth,
	    hc_info.oih_pn, hc_info.oih_rev, hc_info.oih_serial,
	    TOPO_OXHC_TN_F_SET_LABEL, ic_info.ic_refdes) != 0) {
		ret = -1;
		goto done;
	}

	if (ic_info.ic_enum != NULL && !ic_info.ic_enum(mod, &ic_info,
	    &hc_info, tn)) {
		ret = -1;
		goto done;
	}

	for (size_t i = 0; i < ib->ib_nsensors; i++) {
		if (!topo_oxhc_ic_sensor_create(mod, &hc_info, tn,
		    &ib->ib_sensors[i])) {
			ret = -1;
			goto done;
		}
	}

	ret = 0;
done:
	nvlist_free(auth);
	topo_mod_strfree(mod, hc_info.oih_pn_dyn);
	topo_mod_strfree(mod, hc_info.oih_rev_dyn);
	topo_mod_strfree(mod, hc_info.oih_serial_dyn);

	return (ret);
}

int
topo_oxhc_enum_ic_gimlet(topo_mod_t *mod, const oxhc_t *oxhc, tnode_t *tn)
{
	size_t ngimlet_ic = ARRAY_SIZE(oxhc_ic_gimlet_main);

	if (topo_node_range_create(mod, tn, IC, 0, ngimlet_ic - 1) != 0) {
		topo_mod_dprintf(mod, "failed to create IC range: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	for (size_t i = 0; i < ngimlet_ic; i++) {
		if (topo_oxhc_enum_ic(mod, oxhc, oxhc->oxhc_rev, tn,
		    &oxhc_ic_gimlet_main[i], i, NULL) != 0) {
			return (-1);
		}
	}

	return (0);
}

int
topo_oxhc_enum_ic_temp(topo_mod_t *mod, const oxhc_t *oxhc, tnode_t *tn,
    const char *refdes)
{
	size_t nic = ARRAY_SIZE(oxhc_ic_temp_board);

	if (topo_node_range_create(mod, tn, IC, 0, nic - 1) != 0) {
		topo_mod_dprintf(mod, "failed to create IC range: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	for (size_t i = 0; i < nic; i++) {
		if (topo_oxhc_enum_ic(mod, oxhc, 0, tn, &oxhc_ic_temp_board[i],
		    i, refdes) != 0) {
			return (-1);
		}
	}

	return (0);
}

int
topo_oxhc_enum_ic_sharkfin(topo_mod_t *mod, const oxhc_t *oxhc, tnode_t *tn,
    const char *refdes, uint32_t rev)
{
	size_t nic = ARRAY_SIZE(oxhc_ic_sharkfin);

	if (topo_node_range_create(mod, tn, IC, 0, nic - 1) != 0) {
		topo_mod_dprintf(mod, "failed to create IC range: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	for (size_t i = 0; i < nic; i++) {
		if (topo_oxhc_enum_ic(mod, oxhc, rev, tn, &oxhc_ic_sharkfin[i],
		    i, refdes) != 0) {
			return (-1);
		}
	}

	return (0);
}

int
topo_oxhc_enum_ic_fanvpd(topo_mod_t *mod, const oxhc_t *oxhc, tnode_t *tn,
    const char *refdes, uint32_t rev)
{
	size_t nic = ARRAY_SIZE(oxhc_ic_fanvpd);

	if (topo_node_range_create(mod, tn, IC, 0, nic - 1) != 0) {
		topo_mod_dprintf(mod, "failed to create IC range: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	for (size_t i = 0; i < nic; i++) {
		if (topo_oxhc_enum_ic(mod, oxhc, rev, tn, &oxhc_ic_fanvpd[i],
		    i, refdes) != 0) {
			return (-1);
		}
	}

	return (0);
}
