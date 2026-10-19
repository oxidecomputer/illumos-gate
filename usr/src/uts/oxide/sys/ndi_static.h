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

#ifndef _SYS_NDI_STATIC_H
#define	_SYS_NDI_STATIC_H

/*
 * Support for nexus drivers whose children are described by a static table
 * rather than discovered by probing hardware. See the theory statement in
 * uts/oxide/os/ndi_static.c.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How a child's integer unit address is rendered in its device name.
 */
typedef enum ndis_ua_fmt {
	NDIS_UA_HEX,
	NDIS_UA_DEC
} ndis_ua_fmt_t;

/*
 * A description of one potential child. The pair (node name, unit address)
 * must be unique within a nexus instance. ncd_private is for the nexus'
 * own use.
 */
typedef struct ndis_child_def {
	const char	*ncd_nodename;
	uint32_t	ncd_unit_addr;
	const void	*ncd_private;
} ndis_child_def_t;

/*
 * Hooks supplied by the nexus. All are optional.
 *
 * nso_usable	Whether the child may exist on this instance of the nexus.
 *		A child that is not usable is skipped during configuration.
 * nso_init_child
 *		Called with a newly allocated, unbound child node. The nexus
 *		sets properties and claims resources here. On failure the node
 *		is freed again and nso_fini_child is not called.
 * nso_fini_child
 *		Called before a child node is freed, to release whatever
 *		nso_init_child set up.
 */
typedef struct ndis_ops {
	bool	(*nso_usable)(void *, const ndis_child_def_t *);
	int	(*nso_init_child)(void *, const ndis_child_def_t *,
	    dev_info_t *);
	void	(*nso_fini_child)(void *, const ndis_child_def_t *,
	    dev_info_t *);
} ndis_ops_t;

/*
 * Opaque per-instance state, returned by ndis_init() and released by
 * ndis_fini().
 */
typedef struct ndis ndis_t;

extern ndis_t *ndis_init(dev_info_t *, const char *, ndis_ua_fmt_t,
    const ndis_child_def_t *const *, size_t, const ndis_ops_t *, void *,
    size_t);
extern void ndis_fini(ndis_t *);

/*
 * Implementations of the corresponding bus_ops(9S) entry points. The nexus
 * calls these from its own entry points with the ndis_t that ndis_init()
 * returned.
 */
extern int ndis_bus_config(ndis_t *, uint_t, ddi_bus_config_op_t, void *,
    dev_info_t **);
extern int ndis_bus_unconfig(ndis_t *, uint_t, ddi_bus_config_op_t, void *);
extern int ndis_bus_ctl(ndis_t *, dev_info_t *, ddi_ctl_enum_t, void *,
    void *);

/*
 * Accessors for children created through this interface.
 */
extern const ndis_child_def_t *ndis_child_def(dev_info_t *);
extern void *ndis_child_private(dev_info_t *);
extern dev_info_t *ndis_child_dip(const ndis_t *, const ndis_child_def_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_NDI_STATIC_H */
