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

#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <strings.h>
#include <sys/ipcc.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/debug.h>
#include <topo_zen.h>

typedef struct oxhc oxhc_t;
typedef struct oxhc_enum oxhc_enum_t;
typedef int (*oxhc_enum_f)(topo_mod_t *, const oxhc_t *, const oxhc_enum_t *,
    tnode_t *, tnode_t *, topo_instance_t, topo_instance_t);

/*
 * Enumeration flags and structure definition. This is used to drive the
 * processing of various topology nodes.
 *
 * XXX We still need flags and logic for ASRU construction.
 */
typedef enum {
	/*
	 * This flag is used to indicate that we are okay operating on a range
	 * of instances. This should only happen during the range enumeration
	 * phase, not during the post-creation enumeration phase.
	 */
	OXHC_ENUM_F_MULTI_RANGE		= 1 << 0,
	/*
	 * When enumerating information for this node, use the IPCC
	 * identification information for various pieces of information in the
	 * FMRI.
	 */
	OXHC_ENUM_F_USE_IPCC_SN		= 1 << 1,
	OXHC_ENUM_F_USE_IPCC_PN_AS_PART	= 1 << 2,
	/*
	 * This is a note that we need to manually construct the auth field as
	 * opposed to simply inheriting it. This is basically always the case
	 * for our initial node.
	 */
	OXHC_ENUM_F_MAKE_AUTH		= 1 << 3,
	/*
	 * This indicates that we should set a FRU to ourselves. Otherwise we
	 * will attempt to inherit the FRU from our parent.
	 */
	OXHC_ENUM_F_FRU_SELF		= 1 << 4
} oxhc_enum_flags_t;

struct oxhc_enum {
	const char *oe_name;
	const char *oe_parent;
	oxhc_enum_flags_t oe_flags;
	const char *oe_cpn;
	oxhc_enum_f oe_range_enum;
	oxhc_enum_f oe_post_enum;
};

/*
 * Misc. data that we want to keep around during the module's lifetime.
 */
typedef struct oxhc {
	char *oxhc_pn;
	char *oxhc_sn;
	char oxhc_rev[32];
	const oxhc_enum_t *oxhc_enum;
	size_t oxhc_nenum;
} oxhc_t;

/*
 * Create our authority information for the system. While we inherit basic
 * information from our parent, we override most of it with the information from
 * IPCC.
 */
static nvlist_t *
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
		nvlist_t *fmri;
		int ret, err;
		tnode_t *parent = pn;
		const char *part = oe->oe_cpn, *rev = NULL, *serial = NULL;

		/*
		 * When we're a child of hc we can't use it in our attempt to
		 * construct an FMRI as that will fail at this point in time.
		 */
		if (strcmp(topo_node_name(pn), "hc") == 0) {
			parent = NULL;
		}

		if ((oe->oe_flags & OXHC_ENUM_F_USE_IPCC_SN) != 0) {
			serial = oxhc->oxhc_sn;
		}

		if ((oe->oe_flags & OXHC_ENUM_F_USE_IPCC_PN_AS_PART) != 0) {
			serial = oxhc->oxhc_pn;
		}

		fmri = topo_mod_hcfmri(mod, parent, FM_HC_SCHEME_VERSION,
		    oe->oe_name, i, NULL, auth, part, rev, serial);
		if (fmri == NULL) {
			topo_mod_dprintf(mod, "failed to create fmri for %s[%"
			    PRIu64 "]: %s\n", oe->oe_name, i,
			    topo_mod_errmsg(mod));
			nvlist_free(auth);
			return (-1);
		}

		tn = topo_node_bind(mod, pn, oe->oe_name, i, fmri);
		if (tn == NULL) {
			topo_mod_dprintf(mod, "failed to bind fmri for %s[%"
			    PRIu64 "]: %s\n", oe->oe_name, i,
			    topo_mod_errmsg(mod));
			nvlist_free(fmri);
			nvlist_free(auth);
			return (-1);
		}

		topo_pgroup_hcset(tn, auth);

		if ((oe->oe_flags & OXHC_ENUM_F_FRU_SELF) != 0) {
			ret = topo_node_fru_set(tn, fmri, 0, &err);
		} else {
			ret = topo_node_fru_set(tn, NULL, 0, &err);
		}
		nvlist_free(fmri);

		if (ret != 0) {
			topo_mod_dprintf(mod, "failed to set FMRI: %s\n",
			    topo_mod_errmsg(mod));
			nvlist_free(auth);
			return (topo_mod_seterrno(mod, err));
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
	oxhc_enum_t tmp;
	int ret;

	VERIFY3U(min, ==, 0);
	VERIFY3U(max, ==, 27);

	tmp = *oe;
	tmp.oe_cpn = "215-0000085";
	ret = topo_oxhc_enum_range(mod, oxhc, &tmp, pn, tn, 0, 9);
	if (ret != 0) {
		return (ret);
	}

	tmp = *oe;
	tmp.oe_cpn = "215-0000086";
	ret = topo_oxhc_enum_range(mod, oxhc, &tmp, pn, tn, 10, 25);
	if (ret != 0) {
		return (ret);
	}

	tmp = *oe;
	tmp.oe_cpn = "215-0000072";
	return (topo_oxhc_enum_range(mod, oxhc, &tmp, pn, tn, 26, 27));
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
	int err;
	di_node_t root;

	topo_mod_dprintf(mod, "post-processing %s[%" PRIu64 "]\n", tname,
	    topo_node_instance(tn));
	if (topo_prop_get_uint32(tn, TOPO_PGROUP_BINDING, TOPO_BINDING_SLOT,
	    &slot, &err) != 0) {
		topo_mod_dprintf(mod, "failed to get slot number from %s: %s",
		    tname, topo_strerror(err));
		return (topo_mod_seterrno(mod, err));
	}

	if ((root = topo_mod_devinfo(mod)) == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "failed to get devinfo tree");
		return (-1);
	}

	/*
	 * We need to search our pcieb instances until we find the one that
	 * actually has a slot that matches what we want.
	 */
	for (di_node_t di = di_drv_first_node("pcieb", root); di != DI_NODE_NIL;
	    di = di_drv_next_node(di)) {
		int *slots, ret;
		char *path;
		const char *dname;
		di_node_t child;

		if (di_prop_lookup_ints(DDI_DEV_T_ANY, di, "physical-slot#",
		    &slots) != 1) {
			continue;
		}

		if ((uint32_t)slots[0] != slot)
			continue;

		path = di_devfs_path(di);
		if (path == NULL) {
			topo_mod_dprintf(mod, "failed to get /devices path for "
			    "%s%d: %s", di_driver_name(di), di_instance(di),
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
		child = di_child_node(di);
		if (child == DI_NODE_NIL) {
			return (0);
		}

		dname = di_driver_name(child);
		if (dname != NULL && strcmp(dname, NVME) == 0) {
			return (topo_oxhc_enum_nvme(mod, tn, child));
		} else {
			/*
			 * XXX We should figure out what to do in this case.
			 * Right now the PCI enumerator expects to have a PCI
			 * Bus to be enumerated under a root port, which isn't
			 * really right to create here.
			 */
			return (0);
		}
	}

	/*
	 * If we didn't find anything, that's OK. It may not be present. Our
	 * methods will help fill that in later.
	 */
	topo_mod_dprintf(mod, "failed to map %s[%" PRIu64 "] to a pcieb "
	    "instance\n", tname, topo_node_instance(tn));
	return (0);
}
/*
 * This is our second pass for slots. Because we have three different slots
 * types, what we do depends on which range we're in.
 *
 * XXX Do something for stuff other than M.2.
 */
static int
topo_oxhc_enum_slot(topo_mod_t *mod, const oxhc_t *oxhc,
    const oxhc_enum_t *oe, tnode_t *pn, tnode_t *tn, topo_instance_t min,
    topo_instance_t max)
{
	topo_instance_t inst = topo_node_instance(tn);

	if (inst >= 26 && inst <= 27) {
		return (topo_oxhc_enum_pcie_child(mod, oxhc, oe, pn, tn, min,
		    max));
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

/*
 * Data enumeration table. In particular, this module is the main enumeration
 * method for most of the chassis, motherboard, various, ports, etc. The
 * following table directs how we process these items and what we require.
 */
const oxhc_enum_t oxhc_enum_gimlet[] = {
	{ .oe_name = CHASSIS, .oe_parent = "hc", .oe_cpn = "992-0000015",
	    .oe_flags = OXHC_ENUM_F_USE_IPCC_SN | OXHC_ENUM_F_MAKE_AUTH |
	    OXHC_ENUM_F_FRU_SELF, .oe_range_enum = topo_oxhc_enum_range },
	{ .oe_name = BAY, .oe_parent = CHASSIS,
	    .oe_flags = OXHC_ENUM_F_MULTI_RANGE,
	    .oe_range_enum = topo_oxhc_enum_range,
	    .oe_post_enum = topo_oxhc_enum_pcie_child },
	{ .oe_name = PORT, .oe_parent = CHASSIS,
	    .oe_flags = OXHC_ENUM_F_MULTI_RANGE,
	    .oe_range_enum = topo_oxhc_enum_range  },
	{ .oe_name = SYSTEMBOARD, .oe_parent = CHASSIS,
	    .oe_flags = OXHC_ENUM_F_USE_IPCC_SN |
	    OXHC_ENUM_F_USE_IPCC_PN_AS_PART | OXHC_ENUM_F_FRU_SELF,
	    .oe_range_enum = topo_oxhc_enum_range },
	{ .oe_name = SOCKET, .oe_parent = SYSTEMBOARD, .oe_cpn = "215-0000014",
	    .oe_range_enum = topo_oxhc_enum_range,
	    .oe_post_enum = topo_oxhc_enum_cpu },
	{ .oe_name = SLOT, .oe_parent = SYSTEMBOARD,
	    .oe_flags = OXHC_ENUM_F_MULTI_RANGE,
	    .oe_range_enum = topo_oxhc_enum_range_slot,
	    .oe_post_enum = topo_oxhc_enum_slot },
};

typedef struct {
	const char *oem_pn;
	const oxhc_enum_t *oem_enum;
	size_t oem_nenum;
} oxhc_enum_map_t;

static const oxhc_enum_map_t oxhc_enum_map[] = {
	{ "913-0000019", oxhc_enum_gimlet, ARRAY_SIZE(oxhc_enum_gimlet) }
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

	topo_mod_strfree(mod, oxhc->oxhc_pn);
	topo_mod_strfree(mod, oxhc->oxhc_sn);

	topo_mod_free(mod, oxhc, sizeof (oxhc_t));
}

static int
topo_oxhc_init(topo_mod_t *mod, oxhc_t *oxhc)
{
	int fd = -1, ret = -1;
	ipcc_ident_t ident;

	fd = open(IPCC_DEV, O_RDWR);
	if (fd < 0) {
		topo_mod_dprintf(mod, "failed to open %s: %s\n", IPCC_DEV,
		    strerror(errno));
		goto out;
	}

	if (ioctl(fd, IPCC_IDENT, &ident) != 0) {
		topo_mod_dprintf(mod, "failed to get ident via IPCC: %s\n",
		    strerror(errno));
		goto out;
	}

	/*
	 * The IPCC kernel driver has guaranteed that these strings are NULL
	 * terminated, but not really anything else.
	 */
	oxhc->oxhc_pn = topo_mod_clean_str(mod, (char *)ident.ii_model);
	oxhc->oxhc_sn = topo_mod_clean_str(mod, (char *)ident.ii_serial);
	if (oxhc->oxhc_pn == NULL || oxhc->oxhc_sn == NULL) {
		(void) topo_mod_seterrno(mod, EMOD_UKNOWN_ENUM);
		topo_mod_dprintf(mod, "failed to cleanup pn and sn strings: "
		    "%s\n", topo_mod_errmsg(mod));
		goto out;
	}

	if (snprintf(oxhc->oxhc_rev, sizeof (oxhc->oxhc_rev), "%u",
	    ident.ii_rev) >= sizeof (oxhc->oxhc_rev)) {
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
			break;
		}
	}

	if (oxhc->oxhc_enum == NULL || oxhc->oxhc_nenum == 0) {
		topo_mod_dprintf(mod, "failed to get topo enum entries for pn "
		    "%s\n", oxhc->oxhc_pn);
		goto out;
	}

	/*
	 * XXX When we have IPCC data for inventory from the SP, this is where
	 * we should grab it.
	 */

	/*
	 * XXX This is where we should grab the memory controller snapshot for
	 * later.
	 */

	ret = 0;
out:
	if (fd >= 0) {
		(void) close(fd);
	}

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
