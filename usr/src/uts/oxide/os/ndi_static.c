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
 * Static child management for nexus drivers.
 *
 * Several nexus drivers have no way to discover their children from the
 * hardware and they have instead a table of the children that can exist under
 * it. The nexus creates device nodes from that table when asked to configure
 * its children. This file provides a shared implementation of the common
 * logic that creates and binds nodes, names them on initialisation and tears
 * them down again when required.
 *
 * A nexus calls ndis_init() with a table of ndis_child_def_t pointers, a format
 * for unit addresses and an optional set of hooks, and keeps the opaque
 * `ndis_t` that it returns in its soft state. The bus_config, bus_unconfig and
 * bus_ctl entry points of the nexus then call the corresponding functions here
 * with that handle, and ndis_fini() releases it.
 *
 * Child Naming
 *
 * Every child has a node name and a 32-bit unit address, rendered in either
 * hex or decimal according to the nexus. Each (node name, unit address) pair
 * must be unique within an instance. When the DDI initialises a child with
 * (DDI_CTLOPS_INITCHILD) we look up the table entry recorded on the node and
 * set its address from that entry.
 *
 * Node Lifecycle
 *
 * Nodes are created on demand during bus configuration. If a node for a table
 * entry already exists, configuration of that entry succeeds without doing
 * anything. A node that cannot be bound to a driver is still created so that a
 * later configuration pass, for example after a driver has been added, can
 * bind it.
 *
 * Child nodes are self-identifying and persistent. A bus unconfig operation
 * detaches the children but leaves their nodes in place unless NDI_DEVI_REMOVE
 * is requested. When it is, the DDI's own implementation would remove the
 * nodes itself from within ndi_busop_bus_unconfig(), which gives the nexus no
 * opportunity to release the parent private data and resources. We therefore
 * do not pass NDI_DEVI_REMOVE on to the DDI. We allow the DDI to detach and
 * uninit the children, and then we remove the node ourselves. This depends on
 * the nodes being persistent. The DDI removes a node that is not persistent
 * as soon as it has uninitialised it, whether or not NDI_DEVI_REMOVE was
 * passed.
 *
 * Parent Private Data
 *
 * Every node created by this code has an ndis_child_t as its parent private
 * data. It also provides private storage for the nexus when requested. The
 * nexus must not call ddi_set_parent_data() on its children.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
#include <sys/kmem.h>
#include <sys/ndi_static.h>

/*
 * Per-instance state, allocated by ndis_init().
 */
struct ndis {
	dev_info_t			*ns_dip;
	const char			*ns_desc;
	ndis_ua_fmt_t			ns_ua_fmt;
	const ndis_child_def_t *const	*ns_children;
	size_t				ns_nchildren;
	const ndis_ops_t		*ns_ops;
	void				*ns_arg;
	size_t				ns_privsz;
};

/*
 * The parent private data for a child node. The nexus' private storage, if
 * any, follows the structure.
 */
typedef struct ndis_child {
	ndis_t			*nc_ndis;
	const ndis_child_def_t	*nc_def;
} ndis_child_t;

/* A unit address is at most eight hex digits or ten decimal digits */
#define	NDIS_UA_BUFLEN	11

ndis_t *
ndis_init(dev_info_t *dip, const char *desc, ndis_ua_fmt_t fmt,
    const ndis_child_def_t *const *children, size_t nchildren,
    const ndis_ops_t *ops, void *arg, size_t privsz)
{
	ndis_t *ns;

	VERIFY3P(dip, !=, NULL);
	VERIFY3P(children, !=, NULL);
	VERIFY3U(nchildren, !=, 0);
	VERIFY(fmt == NDIS_UA_HEX || fmt == NDIS_UA_DEC);

	ns = kmem_zalloc(sizeof (*ns), KM_SLEEP);
	ns->ns_dip = dip;
	ns->ns_desc = (desc != NULL) ? desc : ddi_driver_name(dip);
	ns->ns_ua_fmt = fmt;
	ns->ns_children = children;
	ns->ns_nchildren = nchildren;
	ns->ns_ops = ops;
	ns->ns_arg = arg;
	ns->ns_privsz = privsz;

	return (ns);
}

void
ndis_fini(ndis_t *ns)
{
	kmem_free(ns, sizeof (*ns));
}

static void
ndis_format_ua(const ndis_t *ns, const ndis_child_def_t *def, char *buf,
    size_t buflen)
{
	int len;

	ASSERT3U(buflen, >=, NDIS_UA_BUFLEN);

	switch (ns->ns_ua_fmt) {
	case NDIS_UA_HEX:
		len = snprintf(buf, buflen, "%x", def->ncd_unit_addr);
		break;
	case NDIS_UA_DEC:
		len = snprintf(buf, buflen, "%u", def->ncd_unit_addr);
		break;
	default:
		panic("ndis_t %p has invalid unit address format %d", ns,
		    ns->ns_ua_fmt);
	}

	VERIFY3S(len, >, 0);
	VERIFY3U(len, <, buflen);
}

static size_t
ndis_child_size(const ndis_t *ns)
{
	return (sizeof (ndis_child_t) + ns->ns_privsz);
}

static const ndis_child_def_t *
ndis_lookup_def(const ndis_t *ns, const char *devname)
{
	const ndis_child_def_t *def = NULL;
	char *dup, *name, *addr;
	size_t duplen;

	/*
	 * i_ddi_parse_name() splits the copy up in place by writing NULs into
	 * it. The length of the allocation is therefore recorded first and
	 * used to free the copy. strfree() would compute a shorter length.
	 */
	dup = ddi_strdup(devname, KM_SLEEP);
	duplen = strlen(dup) + 1;
	i_ddi_parse_name(dup, &name, &addr, NULL);

	if (name == NULL || addr == NULL) {
		kmem_free(dup, duplen);
		return (NULL);
	}

	for (size_t i = 0; i < ns->ns_nchildren; i++) {
		char ua[NDIS_UA_BUFLEN];

		if (strcmp(ns->ns_children[i]->ncd_nodename, name) != 0)
			continue;

		ndis_format_ua(ns, ns->ns_children[i], ua, sizeof (ua));
		if (strcmp(ua, addr) == 0) {
			def = ns->ns_children[i];
			break;
		}
	}

	kmem_free(dup, duplen);
	return (def);
}

static ndis_child_t *
ndis_child_lookup(const ndis_t *ns, dev_info_t *cdip)
{
	ndis_child_t *nc;

	if (ddi_get_parent(cdip) != ns->ns_dip)
		return (NULL);

	nc = ddi_get_parent_data(cdip);
	if (nc == NULL || nc->nc_ndis != ns)
		return (NULL);

	return (nc);
}

dev_info_t *
ndis_child_dip(const ndis_t *ns, const ndis_child_def_t *def)
{
	ASSERT(DEVI_BUSY_OWNED(ns->ns_dip));

	for (dev_info_t *cdip = ddi_get_child(ns->ns_dip); cdip != NULL;
	    cdip = ddi_get_next_sibling(cdip)) {
		const ndis_child_t *nc = ndis_child_lookup(ns, cdip);

		if (nc != NULL && nc->nc_def == def)
			return (cdip);
	}

	return (NULL);
}

const ndis_child_def_t *
ndis_child_def(dev_info_t *cdip)
{
	const ndis_child_t *nc = ddi_get_parent_data(cdip);

	VERIFY3P(nc, !=, NULL);
	return (nc->nc_def);
}

void *
ndis_child_private(dev_info_t *cdip)
{
	ndis_child_t *nc = ddi_get_parent_data(cdip);

	VERIFY3P(nc, !=, NULL);
	VERIFY3U(nc->nc_ndis->ns_privsz, !=, 0);
	return (nc + 1);
}

static bool
ndis_child_usable(const ndis_t *ns, const ndis_child_def_t *def)
{
	if (ns->ns_ops == NULL || ns->ns_ops->nso_usable == NULL)
		return (true);

	return (ns->ns_ops->nso_usable(ns->ns_arg, def));
}

/*
 * Create the node for a table entry if it does not already exist.
 */
static int
ndis_config_child(ndis_t *ns, const ndis_child_def_t *def)
{
	dev_info_t *pdip = ns->ns_dip;
	dev_info_t *cdip;
	ndis_child_t *nc;

	ASSERT(DEVI_BUSY_OWNED(pdip));

	if (ndis_child_dip(ns, def) != NULL)
		return (NDI_SUCCESS);

	if (!ndis_child_usable(ns, def))
		return (NDI_FAILURE);

	/*
	 * The node must be persistent so that the DDI does not remove it
	 * before ndis_bus_unconfig() can, see the theory statement.
	 */
	ndi_devi_alloc_sleep(pdip, def->ncd_nodename, (pnode_t)DEVI_SID_NODEID,
	    &cdip);

	nc = kmem_zalloc(ndis_child_size(ns), KM_SLEEP);
	nc->nc_ndis = ns;
	nc->nc_def = def;
	ddi_set_parent_data(cdip, nc);

	if (ns->ns_ops != NULL && ns->ns_ops->nso_init_child != NULL) {
		int ret = ns->ns_ops->nso_init_child(ns->ns_arg, def, cdip);

		if (ret != NDI_SUCCESS) {
			ddi_set_parent_data(cdip, NULL);
			kmem_free(nc, ndis_child_size(ns));
			(void) ndi_devi_free(cdip);
			return (ret);
		}
	}

	/*
	 * Binding can fail if there is no driver for the child yet. The node is
	 * kept so that a later configuration pass can bind it.
	 */
	(void) ndi_devi_bind_driver(cdip, 0);

	return (NDI_SUCCESS);
}

/*
 * Remove a node that we created. The node must already have been detached and
 * uninitialised by the DDI.
 */
static int
ndis_remove_child(ndis_t *ns, dev_info_t *cdip)
{
	ndis_child_t *nc = ndis_child_lookup(ns, cdip);

	ASSERT(DEVI_BUSY_OWNED(ns->ns_dip));
	VERIFY3P(nc, !=, NULL);

	/*
	 * ndi_devi_free() fails for nodes that are still initialised. Check
	 * first so that the nexus' resources are not released from under an
	 * initialised node.
	 */
	if (i_ddi_node_state(cdip) >= DS_INITIALIZED) {
		dev_err(ns->ns_dip, CE_WARN,
		    "cannot remove child %s%d which is still initialised",
		    ddi_node_name(cdip), ddi_get_instance(cdip));
		return (NDI_FAILURE);
	}

	if (ns->ns_ops != NULL && ns->ns_ops->nso_fini_child != NULL)
		ns->ns_ops->nso_fini_child(ns->ns_arg, nc->nc_def, cdip);

	ddi_set_parent_data(cdip, NULL);
	kmem_free(nc, ndis_child_size(ns));
	VERIFY3S(ndi_devi_free(cdip), ==, NDI_SUCCESS);

	return (NDI_SUCCESS);
}

int
ndis_bus_config(ndis_t *ns, uint_t flags, ddi_bus_config_op_t op, void *arg,
    dev_info_t **childp)
{
	dev_info_t *pdip = ns->ns_dip;
	int ret;

	switch (op) {
	case BUS_CONFIG_ONE:
	case BUS_CONFIG_ALL:
	case BUS_CONFIG_DRIVER:
		break;
	default:
		return (NDI_FAILURE);
	}

	ndi_devi_enter(pdip);

	if (op == BUS_CONFIG_ONE) {
		const ndis_child_def_t *def;

		VERIFY3P(arg, !=, NULL);
		def = ndis_lookup_def(ns, (const char *)arg);
		if (def == NULL) {
			ndi_devi_exit(pdip);
			return (NDI_EINVAL);
		}

		ret = ndis_config_child(ns, def);
	} else {
		/*
		 * BUS_CONFIG_DRIVER asks for the children bound to one major
		 * number. We cannot know a child's major number before its node
		 * exists, so create them all. The DDI then attaches only the
		 * matching ones.
		 */
		for (size_t i = 0; i < ns->ns_nchildren; i++)
			(void) ndis_config_child(ns, ns->ns_children[i]);
		ret = NDI_SUCCESS;
	}

	ndi_devi_exit(pdip);

	if (ret != NDI_SUCCESS)
		return (ret);

	flags |= NDI_ONLINE_ATTACH;
	return (ndi_busop_bus_config(pdip, flags, op, arg, childp, 0));
}

int
ndis_bus_unconfig(ndis_t *ns, uint_t flags, ddi_bus_config_op_t op, void *arg)
{
	dev_info_t *pdip = ns->ns_dip;
	const bool remove = (flags & NDI_DEVI_REMOVE) != 0;
	int ret;

	switch (op) {
	case BUS_UNCONFIG_ONE:
	case BUS_UNCONFIG_ALL:
	case BUS_UNCONFIG_DRIVER:
		break;
	default:
		return (NDI_FAILURE);
	}

	ndi_devi_enter(pdip);

	/*
	 * Have the DDI detach and uninitialise the children. Node removal is
	 * kept for ourselves, see the theory statement.
	 */
	flags = (flags & ~NDI_DEVI_REMOVE) | NDI_UNCONFIG;
	ret = ndi_busop_bus_unconfig(pdip, flags, op, arg);
	if (ret != NDI_SUCCESS || !remove) {
		ndi_devi_exit(pdip);
		return (ret);
	}

	if (op == BUS_UNCONFIG_ONE) {
		const ndis_child_def_t *def;
		dev_info_t *cdip;

		VERIFY3P(arg, !=, NULL);
		def = ndis_lookup_def(ns, (const char *)arg);
		if (def == NULL) {
			ndi_devi_exit(pdip);
			return (NDI_EINVAL);
		}

		/* There is nothing to do for a child that was never created */
		cdip = ndis_child_dip(ns, def);
		if (cdip != NULL)
			ret = ndis_remove_child(ns, cdip);
	} else {
		major_t major = DDI_MAJOR_T_NONE;

		if (op == BUS_UNCONFIG_DRIVER)
			major = (major_t)(uintptr_t)arg;

		for (size_t i = 0; i < ns->ns_nchildren; i++) {
			dev_info_t *cdip = ndis_child_dip(ns,
			    ns->ns_children[i]);

			if (cdip == NULL)
				continue;

			/*
			 * The children that the DDI has unconfigured are in
			 * the DS_BOUND state. A child is not unbound from its
			 * driver until ndi_devi_free() is called for it, so
			 * the major number is still valid here.
			 */
			if (major != DDI_MAJOR_T_NONE &&
			    major != ddi_driver_major(cdip)) {
				continue;
			}

			(void) ndis_remove_child(ns, cdip);
		}
	}

	ndi_devi_exit(pdip);
	return (ret);
}

int
ndis_bus_ctl(ndis_t *ns, dev_info_t *rdip, ddi_ctl_enum_t ctlop, void *arg,
    void *result)
{
	dev_info_t *dip = ns->ns_dip;
	dev_info_t *cdip;
	const ndis_child_t *nc;

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		VERIFY3P(rdip, !=, NULL);
		cmn_err(CE_CONT, "?%s: %s@%s, %s%d\n", ns->ns_desc,
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    ddi_driver_name(rdip), ddi_get_instance(rdip));
		return (DDI_SUCCESS);
	case DDI_CTLOPS_INITCHILD: {
		char ua[NDIS_UA_BUFLEN];

		cdip = arg;
		VERIFY3P(cdip, !=, NULL);

		/*
		 * The DDI only initialises nodes that are bound, and we created
		 * every node under this nexus with parent private data before
		 * binding it. A node without one was not created by us.
		 */
		nc = ndis_child_lookup(ns, cdip);
		if (nc == NULL) {
			dev_err(dip, CE_WARN,
			    "!not initialising unknown child %s",
			    ddi_node_name(cdip));
			return (DDI_FAILURE);
		}

		ndis_format_ua(ns, nc->nc_def, ua, sizeof (ua));
		ddi_set_name_addr(cdip, ua);
		return (DDI_SUCCESS);
	}
	case DDI_CTLOPS_UNINITCHILD:
		cdip = arg;
		VERIFY3P(cdip, !=, NULL);
		ddi_set_name_addr(cdip, NULL);
		return (DDI_SUCCESS);
	default:
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}
}
