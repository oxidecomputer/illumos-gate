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

#ifndef _OXHC_H
#define	_OXHC_H

/*
 * This is an implementation-specific, private header file for the oxhc module.
 */

#include <stdbool.h>
#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <sys/ipcc.h>
#include <sys/ipcc_inventory.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * oxhc-specific property groups that are used internally.
 */
#define	TOPO_PGROUP_OXHC	"oxhc"
#define	TOPO_PGROUP_OXHC_REFDES	"refdes"

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
	OXHC_ENUM_F_USE_IPCC_PN		= 1 << 2,
	OXHC_ENUM_F_USE_IPCC_REV	= 1 << 3,
	/*
	 * This is a note that we need to manually construct the auth field as
	 * opposed to simply inheriting it. This is basically always the case
	 * for our initial node.
	 */
	OXHC_ENUM_F_MAKE_AUTH		= 1 << 4,
	/*
	 * This indicates that we should set a FRU to ourselves. Otherwise we
	 * will attempt to inherit the FRU from our parent.
	 */
	OXHC_ENUM_F_FRU_SELF		= 1 << 5
} oxhc_enum_flags_t;

struct oxhc_enum {
	const char *oe_name;
	const char *oe_parent;
	oxhc_enum_flags_t oe_flags;
	const char *oe_cpn;
	oxhc_enum_f oe_range_enum;
	oxhc_enum_f oe_post_enum;
};

typedef struct oxhc_ipcc_inv {
	bool oii_valid;
	ipcc_inventory_t oii_ipcc;
} oxhc_ipcc_inv_t;

/*
 * Our systems often have a number of different kinds of slots. The types of
 * slots and the corresponding instance numbers will vary based upon the board.
 */
typedef enum oxhc_slot {
	OXHC_SLOT_DIMM,
	OXHC_SLOT_CEM,
	OXHC_SLOT_M2,
	OXHC_SLOT_TEMP
} oxhc_slot_type_t;

typedef struct oxhc_slot_info {
	oxhc_slot_type_t osi_type;
	topo_instance_t osi_min;
	topo_instance_t osi_max;
	const char *osi_cpn;
} oxhc_slot_info_t;

/*
 * Misc. data that we want to keep around during the module's lifetime.
 */
typedef struct oxhc {
	char *oxhc_pn;
	char *oxhc_sn;
	uint32_t oxhc_rev;
	char oxhc_revstr[32];
	const oxhc_enum_t *oxhc_enum;
	size_t oxhc_nenum;
	const oxhc_slot_info_t *oxhc_slots;
	size_t oxhc_nslots;
	uint32_t oxhc_ninv;
	oxhc_ipcc_inv_t *oxhc_inv;
} oxhc_t;

/*
 * Inventory related setup.
 */
extern int topo_oxhc_inventory_init(topo_mod_t *, int, oxhc_t *);
extern void topo_oxhc_inventory_fini(topo_mod_t *, oxhc_t *);
extern const ipcc_inventory_t *topo_oxhc_inventory_find(const oxhc_t *,
    const char *);
extern bool topo_oxhc_inventory_bcopy(const ipcc_inventory_t *, ipcc_inv_type_t,
    void *, size_t, size_t);

/*
 * IC related functions.
 */
extern int topo_oxhc_enum_ic_gimlet(topo_mod_t *, const oxhc_t *, tnode_t *);
extern int topo_oxhc_enum_ic_temp(topo_mod_t *, const oxhc_t *, tnode_t *,
    const char *);


#ifdef __cplusplus
}
#endif

#endif /* _OXHC_H */
