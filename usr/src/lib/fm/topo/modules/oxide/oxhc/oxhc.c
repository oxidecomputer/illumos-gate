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
 * This is the core of the Oxide hardware chassis enumeration.
 *
 * This is a partner in crime to the XML file that corresponds to a given
 * hardware platform. The XML file gives the basic structure and asks for us to
 * enumerate various ranges. It then will come back and fill in static
 * information like labels where it can. It is our responsibility to figure out
 * and bridge this to dynamic information whether that's from the SP, other
 * modules like disks, etc.
 *
 * Right now, each board is mapped to a series of  oxch_enum_t entries which
 * contains function pointers and logic for creating items and contains
 * additional information like CPNs and the function pointers for processing as
 * described above.
 */

#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/debug.h>
#include <topo_zen.h>
#include <topo_dimm.h>
#include <topo_ufm.h>

#include "oxhc.h"

static const topo_pgroup_info_t oxhc_io_pgroup = {
	TOPO_PGROUP_IO,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t oxhc_sensor_pgroup = {
	TOPO_PGROUP_FACILITY,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const topo_pgroup_info_t oxhc_remote_mgs_pgroup = {
	TOPO_PGROUP_REMOTE_MGS,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

static const oxhc_slot_info_t oxhc_slots_gimlet[] = {
	{ OXHC_SLOT_CEM, 0, 9, "215-0000085" },
	{ OXHC_SLOT_DIMM, 10, 25, "215-0000086" },
	{ OXHC_SLOT_M2, 26, 27, "215-0000072" },
	{ OXHC_SLOT_TEMP, 28, 33, "215-0000092" }
};

static const oxhc_port_info_t oxhc_ports_gimlet[] = {
	{ OXHC_PORT_EXAMAX_4X8, 0, 2, "215-0000082" },
	{ OXHC_PORT_PWRBLADE, 3, 3, "215-0000114" }
};

static bool
topo_oxhc_slot_type(const oxhc_t *oxhc, topo_instance_t inst,
    oxhc_slot_type_t *typep)
{
	for (size_t i = 0; i < oxhc->oxhc_nslots; i++) {
		if (inst >= oxhc->oxhc_slots[i].osi_min &&
		    inst <= oxhc->oxhc_slots[i].osi_max) {
			*typep = oxhc->oxhc_slots[i].osi_type;
			return (true);
		}
	}

	return (false);
}

/*
 * Create our authority information for the system. While we inherit basic
 * information from our parent, we override most of it with the information from
 * IPCC.
 */
nvlist_t *
topo_oxhc_auth(topo_mod_t *mod, const oxhc_t *oxhc, const oxhc_enum_t *oe,
    tnode_t *pnode)
{
	nvlist_t *auth;
	int err = 0;

	auth = topo_mod_auth(mod, pnode);
	if (auth == NULL) {
		return (NULL);
	}

	if ((oe->oe_flags & OXHC_ENUM_F_MAKE_AUTH) == 0) {
		return (auth);
	}

	err |= nvlist_add_string(auth, FM_FMRI_AUTH_PRODUCT, oxhc->oxhc_pn);
	err |= nvlist_add_string(auth, FM_FMRI_AUTH_PRODUCT_SN, oxhc->oxhc_sn);
	err |= nvlist_add_string(auth, FM_FMRI_AUTH_CHASSIS,  oxhc->oxhc_sn);

	if (err != 0) {
		nvlist_free(auth);
		(void) topo_mod_seterrno(mod, EMOD_NVL_INVAL);
		return (NULL);
	}

	return (auth);
}

int
topo_oxhc_tn_create(topo_mod_t *mod, tnode_t *pn, tnode_t **tnp,
    const char *name, topo_instance_t inst, nvlist_t *auth, const char *part,
    const char *rev, const char *serial, topo_oxhc_tn_flags_t flags,
    const char *label)
{
	int ret;
	nvlist_t *fmri = NULL, *fru;
	tnode_t *tn, *fmri_parent;

	if (tnp != NULL)
		*tnp = NULL;

	if ((flags & TOPO_OXHC_TN_F_NO_FMRI_PARENT) != 0) {
		fmri_parent = NULL;
	} else {
		fmri_parent = pn;
	}

	fmri = topo_mod_hcfmri(mod, fmri_parent, FM_HC_SCHEME_VERSION, name,
	    inst, NULL, auth, part, rev, serial);
	if (fmri == NULL) {
		topo_mod_dprintf(mod, "failed to create fmri for %s[%" PRIu64
		    "]: %s\n", name, inst, topo_mod_errmsg(mod));
		ret = -1;
		goto out;
	}

	tn = topo_node_bind(mod, pn, name, inst, fmri);
	if (tn == NULL) {
		topo_mod_dprintf(mod, "failed to bind fmri for %s[%" PRIu64
		    "]: %s\n", name, inst, topo_mod_errmsg(mod));
		ret = -1;
		goto out;
	}

	topo_pgroup_hcset(tn, auth);

	if ((flags & TOPO_OXHC_TN_F_FRU_SELF) != 0) {
		fru = fmri;
	} else {
		fru = NULL;
	}

	if (topo_node_fru_set(tn, fru, 0, &ret) != 0) {
		topo_mod_dprintf(mod, "failed to set FRU: %s\n",
		    topo_strerror(ret));
		ret = topo_mod_seterrno(mod, ret);
		goto out;
	}

	if ((flags & TOPO_OXHC_TN_F_SET_LABEL) != 0 &&
	    topo_node_label_set(tn, label, &ret) != 0) {
		topo_mod_dprintf(mod, "failed to set FRU: %s\n",
		    topo_strerror(ret));
		ret = topo_mod_seterrno(mod, ret);
		goto out;
	}

	if (tnp != NULL)
		*tnp = tn;
	ret = 0;
out:
	nvlist_free(fmri);
	return (ret);
}

/*
 * This is used to create a remote sensor that has data available in MGS. Right
 * now we just assume all sensors are remote threshold sensors. This should be
 * pulled out when we have discrete sensors we need to support.
 */
bool
topo_oxhc_mgs_sensor(topo_mod_t *mod, tnode_t *pn, const char *fname,
    uint32_t type, uint32_t unit, ipcc_sensor_id_t mgsid)
{
	tnode_t *fac;
	const char *agents[] = { TOPO_PROP_MGS_AGENT };
	uint_t nagents = 1;

	if ((fac = topo_node_facbind(mod, pn, fname, TOPO_FAC_TYPE_SENSOR)) ==
	    NULL) {
		topo_mod_dprintf(mod, "failed to create sensor %s facility for "
		    "%s[%" PRIu64 "]: %s\n", fname, topo_node_name(pn),
		    topo_node_instance(pn), topo_mod_errmsg(mod));
		return (false);
	}

	if (topo_create_props(mod, fac, TOPO_PROP_IMMUTABLE,
	    &oxhc_sensor_pgroup,
	    TOPO_SENSOR_CLASS, TOPO_TYPE_STRING, TOPO_SENSOR_CLASS_THRESHOLD,
	    TOPO_FACILITY_TYPE, TOPO_TYPE_UINT32, type,
	    TOPO_SENSOR_UNITS, TOPO_TYPE_UINT32, unit,
	    TOPO_PROP_REMOTE_AGENTS, TOPO_TYPE_STRING_ARRAY, agents, nagents,
	    NULL) != 0) {
		topo_mod_dprintf(mod, "failed to create %s properties for "
		    "facility %s on %s[%" PRIu64 "]: %s\n",
		    oxhc_sensor_pgroup.tpi_name, fname, topo_node_name(pn),
		    topo_node_instance(pn), topo_mod_errmsg(mod));
		goto err;
	}

	if (topo_create_props(mod, fac, TOPO_PROP_IMMUTABLE,
	    &oxhc_remote_mgs_pgroup,
	    TOPO_PROP_MGS_SENSOR, TOPO_TYPE_UINT32, mgsid,
	    NULL) != 0) {
		topo_mod_dprintf(mod, "failed to create %s properties for "
		    "facility %s on %s[%" PRIu64 "]: %s\n",
		    oxhc_remote_mgs_pgroup.tpi_name, fname,
		    topo_node_name(pn), topo_node_instance(pn),
		    topo_mod_errmsg(mod));
		goto err;
	}

	return (true);

err:
	topo_node_unbind(fac);
	return (false);
}

/*
 * This is the common initial enumeration entry point for a node in the tree.
 */
static int
topo_oxhc_enum_range(topo_mod_t *mod, const oxhc_t *oxhc, const oxhc_enum_t *oe,
    tnode_t *pn, tnode_t *tn, topo_instance_t min, topo_instance_t max)
{
	nvlist_t *auth;

	auth = topo_oxhc_auth(mod, oxhc, oe, pn);
	if (auth == NULL) {
		topo_mod_dprintf(mod, "failed to get auth data: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	for (topo_instance_t i = min; i <= max; i++) {
		const char *part = oe->oe_cpn, *rev = NULL, *serial = NULL;
		topo_oxhc_tn_flags_t flags = 0;

		/*
		 * When we're a child of hc we can't use it in our attempt to
		 * construct an FMRI as that will fail at this point in time.
		 */
		if (strcmp(topo_node_name(pn), "hc") == 0) {
			flags |= TOPO_OXHC_TN_F_NO_FMRI_PARENT;
		}

		if ((oe->oe_flags & OXHC_ENUM_F_USE_IPCC_SN) != 0) {
			serial = oxhc->oxhc_sn;
		}

		if ((oe->oe_flags & OXHC_ENUM_F_USE_IPCC_PN) != 0) {
			part = oxhc->oxhc_pn;
		}

		if ((oe->oe_flags & OXHC_ENUM_F_USE_IPCC_REV) != 0) {
			rev = oxhc->oxhc_revstr;
		}

		if ((oe->oe_flags & OXHC_ENUM_F_FRU_SELF) != 0) {
			flags |= TOPO_OXHC_TN_F_FRU_SELF;
		}

		if (topo_oxhc_tn_create(mod, pn, NULL, oe->oe_name, i, auth,
		    part, rev, serial, flags, NULL) != 0) {
			nvlist_free(auth);
			return (-1);
		}

		/*
		 * This is where we should go through and set the ASRU for these
		 * items if appropriate.
		 */
	}

	nvlist_free(auth);
	return (0);
}

/*
 * All slots are enumerated at once on the system as one continuous range, but
 * are different parts and are made up of different types. We have two blocks
 * from 0-9 for CEM and 10-25 for DIMMs. These need to match the topology map.
 * During the initial range enumeration we just create them. We'll fill in
 * specific slot properties in the post-enumeration phase as we go to create
 * children.
 */
static int
topo_oxhc_enum_range_slot(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	/*
	 * When we add support for a second system board, then these verify
	 * statements should go away and be folded into the board-specific data.
	 */
	VERIFY3U(min, ==, 0);
	VERIFY3U(max, ==, 33);

	for (size_t i = 0; i < oxhc->oxhc_nslots; i++) {
		const oxhc_slot_info_t *slot = &oxhc->oxhc_slots[i];
		int ret;
		oxhc_enum_t tmp;

		tmp = *oe;
		tmp.oe_cpn = slot->osi_cpn;

		ret = topo_oxhc_enum_range(mod, oxhc, &tmp, pn, tn,
		    slot->osi_min, slot->osi_max);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

/*
 * This enumerates basic information about a PCIe device child. Our primary
 * concerns here right now are to just get the basic I/O property group
 * populated as well as any UFMs. There may well be multiple functions here,
 * which means the use of a single di_node_t doesn't give us the most accurate
 * information.  The attempt here is to have something that's a little bit
 * better than nothing. In general, we only expect this to be used for the T6.
 * We should probably come up with something better for this over time.
 */
int
topo_oxhc_enum_pcie(topo_mod_t *mod, tnode_t *tn, di_node_t di)
{
	topo_ufm_devinfo_t tud;
	char *path = NULL;
	int ret = -1, inst;
	const char *drv;
	const char *ppaths[1];
	nvlist_t *fmri = NULL;

	path = di_devfs_path(di);
	if (path == NULL) {
		topo_mod_dprintf(mod, "failed to get /devices path for %s%d: "
		    "%s", di_driver_name(di), di_instance(di),
		    strerror(errno));
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	tud.tud_method = TOPO_UFM_M_DEVINFO;
	tud.tud_path = path;
	if (topo_mod_load(mod, TOPO_MOD_UFM, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "failed to load %s module: %s",
		    TOPO_MOD_UFM, topo_mod_errmsg(mod));
		goto out;
	}

	if (topo_mod_enumerate(mod, tn, TOPO_MOD_UFM, UFM, 0, 0,
	    &tud) != 0) {
		goto out;
	}

	inst = di_instance(di);
	drv = di_driver_name(di);
	ppaths[0] = path;

	/*
	 * If we don't have a driver or instance, just proceed.
	 */
	if (inst == -1 || drv == NULL) {
		ret = 0;
		goto out;
	}

	if ((fmri = topo_mod_modfmri(mod, FM_MOD_SCHEME_VERSION, drv)) ==
	    NULL) {
		topo_mod_dprintf(mod, "failed to create mod FMRI for driver "
		    "%s: %s", drv, topo_mod_errmsg(mod));
		goto out;
	}

	if (topo_create_props(mod, tn, TOPO_PROP_IMMUTABLE,
	    &oxhc_io_pgroup,
	    TOPO_IO_INSTANCE, TOPO_TYPE_UINT32, inst,
	    TOPO_IO_DRIVER, TOPO_TYPE_STRING, drv,
	    TOPO_IO_MODULE, TOPO_TYPE_FMRI, fmri,
	    TOPO_IO_DEV_PATH, TOPO_TYPE_STRING, path,
	    TOPO_IO_PHYS_PATH, TOPO_TYPE_STRING_ARRAY, ppaths, 1,
	    NULL) != 0) {
		topo_mod_dprintf(mod, "failed to create I/O properties on "
		    "%s[%" PRIu64 "]: %s", topo_node_name(tn),
		    topo_node_instance(tn), topo_mod_errmsg(mod));
		goto out;
	}

	ret = 0;
out:
	nvlist_free(fmri);
	di_devfs_path_free(path);
	return (ret);
}

di_node_t
topo_oxhc_slot_to_devi(topo_mod_t *mod, uint32_t slot)
{
	di_node_t root;

	if ((root = topo_mod_devinfo(mod)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "failed to get devinfo tree");
		return (DI_NODE_NIL);
	}

	for (di_node_t di = di_drv_first_node("pcieb", root); di != DI_NODE_NIL;
	    di = di_drv_next_node(di)) {
		int *slots;

		if (di_prop_lookup_ints(DDI_DEV_T_ANY, di, "physical-slot#",
		    &slots) != 1) {
			continue;
		}

		if ((uint32_t)slots[0] != slot)
			continue;

		return (di);
	}

	return (DI_NODE_NIL);
}

static int
topo_oxhc_enum_nvme(topo_mod_t *mod, tnode_t *tn, di_node_t child)
{
	int err;

	if (topo_prop_set_string(tn, TOPO_PGROUP_BINDING, TOPO_BINDING_DRIVER,
	    TOPO_PROP_IMMUTABLE, di_driver_name(child), &err) != 0) {
		topo_mod_dprintf(mod, "failed to set driver property on %s: %s",
		    topo_node_name(tn), topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	if (topo_mod_load(mod, DISK, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "failed to load disk enum: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	/*
	 * The disk enumerator expects that if we're not a PCIe function that
	 * we've created the range for it, so do so here.
	 */
	if (topo_node_range_create(mod, tn, NVME, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create disk range: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	(void) topo_mod_enumerate(mod, tn, DISK, NVME, 0, 0, NULL);
	return (0);
}


/*
 * We have an unknown entity in the U.2 slot or possibly in the CEM itself. We
 * want to have a best effort of representing that something is here. As such we
 * create a board and then an IC under it.
 */
static int
topo_oxhc_enum_unknown_pcie(topo_mod_t *mod, tnode_t *tn, di_node_t di)
{
	tnode_t *board, *ic;
	nvlist_t *auth = NULL;
	int ret = -1;

	auth = topo_mod_auth(mod, tn);
	if (auth == NULL) {
		topo_mod_dprintf(mod, "failed to get auth data for %s[%" PRIu64
		    "]: %s\n", topo_node_name(tn), topo_node_instance(tn),
		    topo_mod_errmsg(mod));
		goto out;
	}

	if (topo_node_range_create(mod, tn, BOARD, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create board range: %s\n",
		    topo_mod_errmsg(mod));
		goto out;
	}

	if (topo_oxhc_tn_create(mod, tn, &board, BOARD, 0, auth, NULL, NULL,
	    NULL, TOPO_OXHC_TN_F_FRU_SELF | TOPO_OXHC_TN_F_SET_LABEL, NULL) !=
	    0) {
		goto out;
	}

	if (topo_node_range_create(mod, board, IC, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create board range: %s\n",
		    topo_mod_errmsg(mod));
		goto out;
	}

	if (topo_oxhc_tn_create(mod, board, &ic, IC, 0, auth, NULL, NULL,
	    NULL, 0, NULL) != 0) {
		goto out;
	}

	ret = topo_oxhc_enum_pcie(mod, ic, di);
out:
	nvlist_free(auth);
	return (ret);
}

/*
 * This is the follow-up enumeration case for bays, slots, and ports that are
 * mechanically PCIe root ports. We have statically assigned a slot number for
 * the devices here. We cannot just invoke the disk enumerator for a few
 * reasons:
 *
 * 1) We do not have the binding information in a form that it wants it in. We
 * need to take the PCIe slot number and transform it back into a /devices path
 * to set the parent device path.
 *
 * 2) We may not actually have a disk plugged in. Long live, K.2!
 *
 * XXX We need to come back here and add methods around device population.
 */
static int
topo_oxhc_enum_pcie_child(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	const char *tname = topo_node_name(tn);
	uint32_t slot;
	int ret, err;
	di_node_t bridge;
	char *path;
	const char *dname;
	di_node_t child;

	topo_mod_dprintf(mod, "post-processing %s[%" PRIu64 "]\n", tname,
	    topo_node_instance(tn));
	if (topo_prop_get_uint32(tn, TOPO_PGROUP_BINDING, TOPO_BINDING_SLOT,
	    &slot, &err) != 0) {
		topo_mod_dprintf(mod, "failed to get slot number from %s: %s",
		    tname, topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	if ((bridge = topo_oxhc_slot_to_devi(mod, slot)) == DI_NODE_NIL) {
		/*
		 * If we didn't find anything, that's OK. It may not be present.
		 * Our methods will help fill that in later.
		 */
		topo_mod_dprintf(mod, "failed to map %s[%" PRIu64 "] to a "
		    "pcieb instance\n", tname, topo_node_instance(tn));
		return (0);
	}

	path = di_devfs_path(bridge);
	if (path == NULL) {
		topo_mod_dprintf(mod, "failed to get /devices path for %s%d: "
		    "%s", di_driver_name(bridge), di_instance(bridge),
		    strerror(errno));
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	ret = topo_prop_set_string(tn, TOPO_PGROUP_BINDING,
	    TOPO_BINDING_PARENT_DEV, TOPO_PROP_IMMUTABLE, path, &err);
	di_devfs_path_free(path);
	if (ret != 0) {
		topo_mod_dprintf(mod, "failed to set devfs path: %s",
		    topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	/*
	 * Look at the child and before we ask the disk enumerator to do
	 * something we should see if it's an NVMe device, otherwise we
	 * will want to do a different enumeration path.
	 */
	child = di_child_node(bridge);
	if (child == DI_NODE_NIL) {
		return (0);
	}

	dname = di_driver_name(child);
	if (dname != NULL && strcmp(dname, NVME) == 0) {
		return (topo_oxhc_enum_nvme(mod, tn, child));
	} else {

		return (topo_oxhc_enum_unknown_pcie(mod, tn, child));
	}
}

/*
 * We have found a slot that refers to a temp sensor board. Check to see if we
 * have an IPCC entry for this based on its refdes property in the oxide
 * property group. If it does, create a board and then ask the IC bits to fill
 * it in. We will be looking for our refdes/U1 as a proxy for whether or not
 * anything is there.
 */
static int
topo_oxhc_enum_temp_board(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	int err, ret;
	const char *tname = topo_node_name(tn);
	char *slot_refdes = NULL;
	char ipcc[IPCC_INVENTORY_NAMELEN];
	libipcc_inv_t *inv;
	libipcc_inv_status_t status;
	nvlist_t *auth = NULL;
	tnode_t *board;

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

	if (snprintf(ipcc, sizeof (ipcc), "%s/U1", slot_refdes) >=
	    sizeof (ipcc)) {
		topo_mod_dprintf(mod, "constructing expected temp sensor "
		    "refdes for %s[%" PRIu64 "] based on found refdes '%s' "
		    "is larger than the IPCC inventory name length", tname,
		    topo_node_instance(tn), slot_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if ((inv = topo_oxhc_inventory_find(oxhc, ipcc)) == NULL) {
		topo_mod_dprintf(mod, "failed to find IPCC inventory entry %s",
		    ipcc);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	/*
	 * If there's a device present then go ahead and create the board
	 * entity. We treat the idea of an I/O error in getting this as
	 * generally there being a board present as something did more than just
	 * NAK us over i2c. The actual IC will not be enumerated in that case.
	 */

	status = libipcc_inv_status(inv);
	if (status != LIBIPCC_INV_STATUS_SUCCESS &&
	    status != LIBIPCC_INV_STATUS_IO_ERROR) {
		topo_mod_dprintf(mod, "%s device is not present, skipping "
		    "board creation", ipcc);
		ret = 0;
		goto out;
	}

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
	 * The FRU for the temp sensor board is itself. Inherit the label from
	 * our parent which will name the temp sensor according to the silk.
	 */
	if (topo_oxhc_tn_create(mod, tn, &board, BOARD, min, auth,
	    "913-0000011", NULL, NULL,
	    TOPO_OXHC_TN_F_FRU_SELF | TOPO_OXHC_TN_F_SET_LABEL, NULL) != 0) {
		ret = -1;
		goto out;
	}

	ret = topo_oxhc_enum_ic_temp(mod, oxhc, board, slot_refdes);
out:
	nvlist_free(auth);
	topo_mod_strfree(mod, slot_refdes);
	return (ret);
}

/*
 * This indicates that we've found a CEM slot that should have a sharkfin. We
 * will look for an IPCC entry of the form JXXX/U7/ID. This will tell us what
 * board we actually have.
 */
static int
topo_oxhc_enum_sharkfin(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	int err, ret;
	const char *tname = topo_node_name(tn);
	char *slot_refdes = NULL, *part = NULL, *serial = NULL;
	char ipcc[IPCC_INVENTORY_NAMELEN], rev[16];
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

	if (snprintf(ipcc, sizeof (ipcc), "%s/U7/ID", slot_refdes) >=
	    sizeof (ipcc)) {
		topo_mod_dprintf(mod, "constructing expected temp sensor "
		    "refdes for %s[%" PRIu64 "] based on found refdes '%s' "
		    "is larger than the IPCC inventory name length", tname,
		    topo_node_instance(tn), slot_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if ((inv = topo_oxhc_inventory_find(oxhc, ipcc)) == NULL) {
		topo_mod_dprintf(mod, "failed to find IPCC inventory entry %s",
		    ipcc);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	/*
	 * If we don't have valid ID information then we should not create a
	 * sharkfin. This is slightly different from the temp sensor board only
	 * because the temp sensor board does not have a FRU ID ROM.
	 */
	if (!topo_oxhc_inventory_bcopy(inv, IPCC_INVENTORY_T_VPDID, &vpd,
	    sizeof (vpd), sizeof (vpd))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", ipcc);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if ((part = topo_mod_clean_strn(mod, (const char *)vpd.vpdid_pn,
	    sizeof (vpd.vpdid_pn))) == NULL ||
	    (serial = topo_mod_clean_strn(mod, (const char *)vpd.vpdid_sn,
	    sizeof (vpd.vpdid_sn))) == NULL) {
		topo_mod_dprintf(mod, "failed to clean up strings for %s\n",
		    ipcc);
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
	 * The FRU for the sharkfin is itself. Inherit the label from
	 * our parent which will name the temp sensor according to the silk.
	 */
	if (topo_oxhc_tn_create(mod, tn, &board, BOARD, min, auth,
	    part, rev, serial, TOPO_OXHC_TN_F_FRU_SELF |
	    TOPO_OXHC_TN_F_SET_LABEL, NULL) != 0) {
		ret = -1;
		goto out;
	}

	ret = topo_oxhc_enum_ic_sharkfin(mod, oxhc, board, slot_refdes,
	    vpd.vpdid_rev);
out:
	topo_mod_strfree(mod, slot_refdes);
	topo_mod_strfree(mod, part);
	topo_mod_strfree(mod, serial);
	nvlist_free(auth);
	return (ret);
}

/*
 * Check to see what IPCC information we have for a given DIMM slot based on the
 * refdes. Eventually this should be combined with the memory controller
 * information. We generally just pass this to the common topo dimm module.
 */
static int
topo_oxhc_enum_dimm(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	int ret, err;
	char *slot_refdes = NULL;
	libipcc_inv_t *inv;
	ipcc_inv_ddr4_t ddr4;
	topo_dimm_t dimm;
	tnode_t *dtn;
	ipcc_sensor_id_t temp;

	topo_mod_dprintf(mod, "post-processing %s[%" PRIu64 "]\n",
	    topo_node_name(tn), topo_node_instance(tn));

	if (topo_prop_get_string(tn, TOPO_PGROUP_OXHC, TOPO_PGROUP_OXHC_REFDES,
	    &slot_refdes, &err) != 0) {
		topo_mod_dprintf(mod, "%s[%" PRIu64 "] missing required refdes "
		    "property: %s, cannot enumerate further",
		    topo_node_name(tn), topo_node_instance(tn),
		    topo_strerror(err));
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	if ((inv = topo_oxhc_inventory_find(oxhc, slot_refdes)) == NULL) {
		topo_mod_dprintf(mod, "failed to find IPCC inventory entry %s",
		    slot_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	/*
	 * If we can't get IPCC information on a DIMM, it's definitely not
	 * there. Though we should really cross reference presence with the zen
	 * UMC information when available.
	 */
	if (!topo_oxhc_inventory_bcopy(inv, IPCC_INVENTORY_T_DDR4, &ddr4,
	    sizeof (ddr4), offsetof(ipcc_inv_ddr4_t, ddr4_temp))) {
		topo_mod_dprintf(mod, "IPCC information for %s is not "
		    "copyable\n", slot_refdes);
		ret = topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		goto out;
	}

	/*
	 * We have a DIMM. Ask the common DIMM module enumeration to take care
	 * of this.
	 */
	(void) memset(&dimm, 0, sizeof (dimm));
	dimm.td_nspd = ARRAY_SIZE(ddr4.ddr4_spd);
	dimm.td_spd = ddr4.ddr4_spd;

	if (topo_mod_load(mod, DIMM, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "failed to load DIMM enum: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	if ((ret = topo_mod_enumerate(mod, tn, DIMM, DIMM, 0, 0, &dimm)) != 0) {
		goto out;
	}

	/*
	 * Attempt to create a temperature sensor for this DIMM if we can. If we
	 * fail because we can't actually find the data about the sensor because
	 * the SP didn't provide that, we consider that fine and just a case
	 * that we shouldn't create a sensor for.
	 */
	if ((dtn = topo_node_lookup(tn, DIMM, 0)) == NULL) {
		topo_mod_dprintf(mod, "failed to find DIMM under %s[%" PRIu64
		    "]\n", topo_node_name(tn), topo_node_instance(tn));
		ret = topo_mod_seterrno(mod, EMOD_NODE_NOENT);
		goto out;
	}

	if (!topo_oxhc_inventory_bcopyoff(inv, &temp, sizeof (temp),
	    offsetof(ipcc_inv_ddr4_t, ddr4_temp))) {
		ret = 0;
		goto out;
	}

	if (!topo_oxhc_mgs_sensor(mod, dtn, "temp", TOPO_SENSOR_TYPE_TEMP,
	    TOPO_SENSOR_UNITS_DEGREES_C, temp)) {
		ret = -1;
		goto out;
	}

	ret = 0;
out:
	topo_mod_strfree(mod, slot_refdes);
	return (ret);
}

/*
 * This is our second pass for slots. Because we have three different slots
 * types, what we do depends on which range we're in.
 */
static int
topo_oxhc_enum_slot(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	oxhc_slot_type_t slot;
	topo_instance_t inst = topo_node_instance(tn);

	if (!topo_oxhc_slot_type(oxhc, inst, &slot)) {
		topo_mod_dprintf(mod, "failed to map %s[%" PRId64 "] to a "
		    "known slot type", topo_node_name(tn), inst);
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	switch (slot) {
	case OXHC_SLOT_M2:
		return (topo_oxhc_enum_pcie_child(mod, oxhc, oe, pn, tn, min,
		    max));
	case OXHC_SLOT_TEMP:
		return (topo_oxhc_enum_temp_board(mod, oxhc, oe, pn, tn, min,
		    max));
	case OXHC_SLOT_CEM:
		return (topo_oxhc_enum_sharkfin(mod, oxhc, oe, pn, tn, min,
		    max));
	case OXHC_SLOT_DIMM:
		return (topo_oxhc_enum_dimm(mod, oxhc, oe, pn, tn, min,
		    max));
	default:
		break;
	}

	return (0);
}

static int
topo_oxhc_enum_cpu(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	int ret;
	topo_zen_chip_t chip;

	if (topo_mod_load(mod, TOPO_MOD_ZEN, TOPO_VERSION) == NULL) {
		topo_mod_dprintf(mod, "failed to load module %s: %s\n",
		    TOPO_MOD_ZEN, topo_mod_errmsg(mod));
		return (-1);
	}

	if (topo_node_range_create(mod, tn, CHIP, min, max) != 0) {
		topo_mod_dprintf(mod, "failed to create %s range: %s\n", CHIP,
		    topo_mod_errmsg(mod));
		return (-1);
	}

	/*
	 * If we ever support more than one processor on an Oxide platform then
	 * the mapping to the socket number from AMD's perspective should happen
	 * in the topo map.
	 */
	chip.tzc_sockid = 0;

	ret = topo_mod_enumerate(mod, tn, TOPO_MOD_ZEN, CHIP, min, max, &chip);
	if (ret != 0) {
		topo_mod_dprintf(mod, "failed to enum %s: %s\n", CHIP,
		    topo_mod_errmsg(mod));
	}
	return (ret);
}

static int
topo_oxhc_enum_range_port(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	/*
	 * When we add support for a second system board, then these verify
	 * statements should go away and be folded into the board-specific data.
	 */
	VERIFY3U(min, ==, 0);
	VERIFY3U(max, ==, 3);

	for (size_t i = 0; i < oxhc->oxhc_nports; i++) {
		const oxhc_port_info_t *port = &oxhc->oxhc_ports[i];
		int ret;
		oxhc_enum_t tmp;

		tmp = *oe;
		tmp.oe_cpn = port->opi_cpn;

		ret = topo_oxhc_enum_range(mod, oxhc, &tmp, pn, tn,
		    port->opi_min, port->opi_max);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

/*
 * The only entity which may have children is the connection to Sidecar, which
 * on Gimlet is port 0. This corresponds to the hotplug bridge pcie19.
 */
static int
topo_oxhc_enum_gimlet_port(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	di_node_t bridge, child;
	tnode_t *ic;
	nvlist_t *auth;

	if (topo_node_instance(tn) != 0) {
		return (0);
	}

	if ((bridge = topo_oxhc_slot_to_devi(mod, 19)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "failed to locate slot19 for Sidecar");
		return (topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM));
	}

	child = di_child_node(bridge);
	if (child == DI_NODE_NIL) {
		return (0);
	}

	/*
	 * We have a node here, so create a generic IC node at the far end. We
	 * don't do much more because we don't know what else is actually there.
	 */
	if (topo_node_range_create(mod, tn, IC, 0, 0) != 0) {
		topo_mod_dprintf(mod, "failed to create IC range: %s\n",
		    topo_mod_errmsg(mod));
		return (-1);
	}

	auth = topo_oxhc_auth(mod, oxhc, oe, tn);
	if (auth == NULL) {
		topo_mod_dprintf(mod, "failed to get auth data for %s[%" PRIu64
		    "]: %s\n", topo_node_name(tn), topo_node_instance(tn),
		    topo_mod_errmsg(mod));
		return (-1);
	}

	if (topo_oxhc_tn_create(mod, tn, &ic, IC, 0, auth, NULL, NULL, NULL,
	    TOPO_OXHC_TN_F_FRU_SELF, NULL) != 0) {
		nvlist_free(auth);
		return (-1);
	}

	nvlist_free(auth);

	return (topo_oxhc_enum_pcie(mod, ic, child));
}


static int
topo_oxhc_enum_gimlet_board(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	return (topo_oxhc_enum_ic_gimlet(mod, oxhc, tn));
}

/*
 * Data enumeration table. In particular, this module is the main enumeration
 * method for most of the chassis, motherboard, various, ports, etc. The
 * following table directs how we process these items and what we require.
 */
static const oxhc_enum_t oxhc_enum_gimlet[] = {
	{ .oe_name = CHASSIS, .oe_parent = "hc", .oe_cpn = "992-0000015",
	    .oe_flags = OXHC_ENUM_F_USE_IPCC_SN | OXHC_ENUM_F_MAKE_AUTH |
	    OXHC_ENUM_F_FRU_SELF, .oe_range_enum = topo_oxhc_enum_range },
	{ .oe_name = BAY, .oe_parent = CHASSIS,
	    .oe_flags = OXHC_ENUM_F_MULTI_RANGE,
	    .oe_range_enum = topo_oxhc_enum_range,
	    .oe_post_enum = topo_oxhc_enum_pcie_child },
	{ .oe_name = SYSTEMBOARD, .oe_parent = CHASSIS,
	    .oe_flags = OXHC_ENUM_F_USE_IPCC_SN |
	    OXHC_ENUM_F_USE_IPCC_PN | OXHC_ENUM_F_USE_IPCC_REV |
	    OXHC_ENUM_F_FRU_SELF,
	    .oe_range_enum = topo_oxhc_enum_range,
	    .oe_post_enum = topo_oxhc_enum_gimlet_board },
	{ .oe_name = SOCKET, .oe_parent = SYSTEMBOARD, .oe_cpn = "215-0000014",
	    .oe_range_enum = topo_oxhc_enum_range,
	    .oe_post_enum = topo_oxhc_enum_cpu },
	{ .oe_name = SLOT, .oe_parent = SYSTEMBOARD,
	    .oe_flags = OXHC_ENUM_F_MULTI_RANGE,
	    .oe_range_enum = topo_oxhc_enum_range_slot,
	    .oe_post_enum = topo_oxhc_enum_slot },
	{ .oe_name = PORT, .oe_parent = SYSTEMBOARD,
	    .oe_flags = OXHC_ENUM_F_MULTI_RANGE,
	    .oe_range_enum = topo_oxhc_enum_range_port,
	    .oe_post_enum = topo_oxhc_enum_gimlet_port },
	/*
	 * Because the fan tray is a removable component it only implements the
	 * enum range entry point and then will enumerate everything else under
	 * itself. It does not rely upon any static properties in the map for
	 * its nodes. This is why we have no oe_post_enum function.
	 */
	{ .oe_name = FANTRAY, .oe_parent = CHASSIS,
	    .oe_flags = OXHC_ENUM_F_FRU_SELF,
	    .oe_range_enum = topo_oxhc_enum_gimlet_fan_tray },
};

typedef struct {
	const char *oem_pn;
	const oxhc_enum_t *oem_enum;
	size_t oem_nenum;
	const oxhc_slot_info_t *oem_slots;
	size_t oem_nslots;
	const oxhc_port_info_t *oem_ports;
	size_t oem_nports;
} oxhc_enum_map_t;

static const oxhc_enum_map_t oxhc_enum_map[] = {
	{ "913-0000019", oxhc_enum_gimlet, ARRAY_SIZE(oxhc_enum_gimlet),
	    oxhc_slots_gimlet, ARRAY_SIZE(oxhc_slots_gimlet),
	    oxhc_ports_gimlet, ARRAY_SIZE(oxhc_ports_gimlet) }
};

/*
 * This is our module's primary enumerator entry point. All types that we
 * declare and handle ourselves enter this function. In general, this is driven
 * by the corresponding topology map and this means that we are called
 * potentially twice by the XML processing logic.
 *
 * 1) The first time we will be called is when we are being asked to enumerate a
 * range declaration. The range declarations give us a number of different
 * entries that we can possibly process and will ask us to create as many as we
 * believe make sense. In our maps we generally have a fairly static set, so we
 * just use that.
 *
 * During this first phase, there is one gotcha. We cannot actually set
 * properties in advance to be used here. This is why the oxhc_enum_t contains
 * information about things like MPNs and other information that we want to use
 * for these items.
 *
 * When we are called during this phase our tnode_t will generally be our parent
 * as our node doesn't exist yet.
 *
 * 2) There is a second phase where we can be called into to take action. This
 * occurs if there are XML <node> entries that are used to declare information
 * about the node. The most common use case here is to decorate specific nodes
 * with properties and property groups. When we are called this time, our
 * instance tnode_t point directly to the node itself and not to the parent.
 */
static int
topo_oxhc_enum(topo_mod_t *mod, tnode_t *pnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *modarg, void *data)
{
	oxhc_t *oxhc = topo_mod_getspecific(mod);
	const char *pname;
	tnode_t *tn = NULL;
	bool post = false, range;

	topo_mod_dprintf(mod, "asked to enum %s [%" PRIu64 ", %" PRIu64 "] on "
	    "%s%" PRIu64 "\n", name, min, max, topo_node_name(pnode),
	    topo_node_instance(pnode));

	range = min != max;

	/*
	 * Look for whether we are in the case where we've been asked to come
	 * back over our specific node. In this case the range's min/max will
	 * stay the same, but our node will have our own name. This means that
	 * we can't really have children as a parent right his moment.
	 */
	pname = topo_node_name(pnode);
	if (strcmp(pname, name) == 0) {
		tn = pnode;
		pnode = topo_node_parent(tn);
		pname = topo_node_name(pnode);
		post = true;
	}

	for (size_t i = 0; i < oxhc->oxhc_nenum; i++) {
		const oxhc_enum_t *oe = &oxhc->oxhc_enum[i];
		if (strcmp(oe->oe_name, name) != 0 ||
		    strcmp(oe->oe_parent, pname) != 0) {
			continue;
		}

		if (range && !post &&
		    (oe->oe_flags & OXHC_ENUM_F_MULTI_RANGE) == 0) {
			topo_mod_dprintf(mod, "multi-instance range "
			    "enumeration not supported");
			return (topo_mod_seterrno(mod, EMOD_NODE_RANGE));
		}

		if (post) {
			if (oe->oe_post_enum == NULL) {
				topo_mod_dprintf(mod, "skipping post-enum: no "
				    "processing function");
				return (0);
			}
			return (oe->oe_post_enum(mod, oxhc, oe, pnode, tn, min,
			    max));
		} else {
			/*
			 * While there are cases that we might get called into
			 * post-enumeration just because of how we've
			 * constructed the topo map even if we don't need to do
			 * anything (but we want to make sure it doesn't go to
			 * some other module), we pretty much always expect to
			 * have something for initial enumeration right now.
			 */
			if (oe->oe_range_enum == NULL) {
				topo_mod_dprintf(mod, "missing initial "
				    "enumeration function!");
				return (-1);
			}

			return (oe->oe_range_enum(mod, oxhc, oe, pnode, tn, min,
			    max));
		}
	}

	topo_mod_dprintf(mod, "component %s unknown", name);
	return (-1);
}

static const topo_modops_t oxhc_ops = {
	topo_oxhc_enum, NULL
};

static topo_modinfo_t oxhc_mod = {
	"Oxide Hardware Chassis Enumerator", FM_FMRI_SCHEME_HC, 1, &oxhc_ops
};

static void
topo_oxhc_cleanup(topo_mod_t *mod, oxhc_t *oxhc)
{
	if (oxhc == NULL) {
		return;
	}

	topo_oxhc_inventory_fini(mod, oxhc);
	topo_mod_strfree(mod, oxhc->oxhc_pn);
	topo_mod_strfree(mod, oxhc->oxhc_sn);

	topo_mod_free(mod, oxhc, sizeof (oxhc_t));
}

static int
topo_oxhc_init(topo_mod_t *mod, oxhc_t *oxhc)
{
	libipcc_handle_t *lih;
	libipcc_err_t lerr;
	int32_t syserr;
	libipcc_ident_t *ident = NULL;
	char errmsg[LIBIPCC_ERR_LEN];
	int ret = -1;

	if (!libipcc_init(&lih, &lerr, &syserr, errmsg, sizeof (errmsg))) {
		topo_mod_dprintf(mod, "failed to initialize libipcc: "
		    "%s: %s (libipcc: 0x%x, sys: %d)\n",
		    errmsg, libipcc_strerror(lerr), lerr, syserr);
		return (-1);
	}

	if (!libipcc_ident(lih, &ident)) {
		topo_oxhc_libipcc_error(mod, lih, "failed to retrieve ident");
		goto out;
	}

	/*
	 * The IPCC kernel driver has guaranteed that these strings are NULL
	 * terminated, but not really anything else, so we clean them up.
	 */
	oxhc->oxhc_pn = topo_mod_clean_str(mod,
	    (char *)libipcc_ident_model(ident));
	oxhc->oxhc_sn = topo_mod_clean_str(mod,
	    (char *)libipcc_ident_serial(ident));
	oxhc->oxhc_rev = libipcc_ident_rev(ident);

	if (oxhc->oxhc_pn == NULL || oxhc->oxhc_sn == NULL) {
		(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		topo_mod_dprintf(mod, "failed to clean up pn and sn strings: "
		    "%s\n", topo_mod_errmsg(mod));
		goto out;
	}

	if (snprintf(oxhc->oxhc_revstr, sizeof (oxhc->oxhc_revstr), "%u",
	    oxhc->oxhc_rev) >= sizeof (oxhc->oxhc_revstr)) {
		topo_mod_dprintf(mod, "failed to construct revision buffer due "
		    "to overflow!");
		goto out;
	}

	/*
	 * With identity information understood, determine which enumeration
	 * rules to use.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(oxhc_enum_map); i++) {
		if (strcmp(oxhc_enum_map[i].oem_pn, oxhc->oxhc_pn) == 0) {
			oxhc->oxhc_enum = oxhc_enum_map[i].oem_enum;
			oxhc->oxhc_nenum = oxhc_enum_map[i].oem_nenum;
			oxhc->oxhc_slots = oxhc_enum_map[i].oem_slots;
			oxhc->oxhc_nslots = oxhc_enum_map[i].oem_nslots;
			oxhc->oxhc_ports = oxhc_enum_map[i].oem_ports;
			oxhc->oxhc_nports = oxhc_enum_map[i].oem_nports;
			break;
		}
	}

	if (oxhc->oxhc_enum == NULL || oxhc->oxhc_nenum == 0) {
		topo_mod_dprintf(mod, "failed to get topo enum entries for pn "
		    "%s\n", oxhc->oxhc_pn);
		goto out;
	}

	if (topo_oxhc_inventory_init(mod, lih, oxhc) != 0) {
		goto out;
	}

	/*
	 * XXX This is where we should grab the memory controller snapshot for
	 * later.
	 */

	ret = 0;

out:
	libipcc_ident_free(ident);
	libipcc_fini(lih);

	return (ret);
}

int
_topo_init(topo_mod_t *mod, topo_version_t version)
{
	oxhc_t *oxhc = NULL;

	if (getenv("TOPOOXHCDEBUG") != NULL) {
		topo_mod_setdebug(mod);
	}

	topo_mod_dprintf(mod, "module initializing.\n");

	oxhc = topo_mod_zalloc(mod, sizeof (oxhc_t));
	if (oxhc == NULL) {
		topo_mod_dprintf(mod, "failed to allocate oxhc_t: %s\n",
		    topo_strerror(EMOD_NOMEM));
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	if (topo_oxhc_init(mod, oxhc) != 0) {
		topo_oxhc_cleanup(mod, oxhc);
		return (-1);
	}


	if (topo_mod_register(mod, &oxhc_mod, TOPO_VERSION) != 0) {
		topo_oxhc_cleanup(mod, oxhc);
		return (-1);
	}

	topo_mod_setspecific(mod, oxhc);

	return (0);
}

void
_topo_fini(topo_mod_t *mod)
{
	oxhc_t *oxhc;

	if ((oxhc = topo_mod_getspecific(mod)) == NULL) {
		return;
	}

	topo_mod_setspecific(mod, NULL);
	topo_oxhc_cleanup(mod, oxhc);
	topo_mod_unregister(mod);
}
