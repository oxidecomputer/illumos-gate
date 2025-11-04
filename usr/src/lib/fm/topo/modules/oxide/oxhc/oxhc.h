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

#ifndef _OXHC_H
#define	_OXHC_H

/*
 * This is an implementation-specific, private header file for the oxhc module.
 */

#include <stdbool.h>
#include <sys/fm/protocol.h>
#include <fm/topo_mod.h>
#include <fm/topo_hc.h>
#include <libipcc.h>
#include <sys/ipcc_inventory.h>

#include "oxhc_ic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * oxhc-specific property groups that are used internally.
 */
#define	TOPO_PGROUP_OXHC		"oxhc"
#define	TOPO_PGROUP_OXHC_REFDES		"refdes"
#define	TOPO_PGROUP_OXHC_MFGCODE	"mfg-code"
#define	TOPO_PGROUP_OXHC_MFGNAME	"mfg-name"

/*
 * MGS remote sensor and property group.
 */
#define	TOPO_PROP_MGS_AGENT	"mgs"
#define	TOPO_PGROUP_REMOTE_MGS	"remote-mgs"
#define	TOPO_PROP_MGS_SENSOR	"mgs-sensor-id"

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

/*
 * Our systems often have a number of different kinds of slots. The types of
 * slots and the corresponding instance numbers will vary based upon the board.
 */
typedef enum oxhc_slot {
	OXHC_SLOT_DIMM,
	OXHC_SLOT_CEM,
	OXHC_SLOT_M2,
	OXHC_SLOT_TEMP,
	OXHC_SLOT_MCIO
} oxhc_slot_type_t;

typedef struct oxhc_slot_info {
	oxhc_slot_type_t osi_type;
	topo_instance_t osi_min;
	topo_instance_t osi_max;
	const char *osi_cpn;
} oxhc_slot_info_t;

typedef enum oxhc_port {
	OXHC_PORT_EXAMAX_4X8,
	OXHC_PORT_PWRBLADE
} oxhc_port_type_t;

typedef struct oxhc_port_info {
	oxhc_port_type_t opi_type;
	topo_instance_t opi_min;
	topo_instance_t opi_max;
	const char *opi_cpn;
} oxhc_port_info_t;

typedef struct oxhc_dimm_info {
	uint8_t di_spd[1024];
	uint32_t di_nspd;
	ipcc_sensor_id_t di_temp[2];
	uint32_t di_ntemp;
} oxhc_dimm_info_t;

typedef int (*oxhc_dimm_info_f)(topo_mod_t *, libipcc_inv_t *inv, const char *,
    oxhc_dimm_info_t *);

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
	const oxhc_port_info_t *oxhc_ports;
	size_t oxhc_nports;
	uint32_t oxhc_ninv;
	libipcc_inv_t **oxhc_inv;
	oxhc_dimm_info_f oxhc_dimm_info;
	const char *oxhc_dram;
} oxhc_t;

/*
 * Common topo node and facility creation.
 */
extern nvlist_t *topo_oxhc_auth(topo_mod_t *, const oxhc_t *,
    const oxhc_enum_t *, tnode_t *);

typedef enum {
	TOPO_OXHC_TN_F_FRU_SELF		= 1 << 0,
	TOPO_OXHC_TN_F_SET_LABEL	= 1 << 1,
	TOPO_OXHC_TN_F_NO_FMRI_PARENT	= 1 << 2
} topo_oxhc_tn_flags_t;
extern int topo_oxhc_tn_create(topo_mod_t *, tnode_t *, tnode_t **,
    const char *, topo_instance_t, nvlist_t *, const char *, const char *,
    const char *, topo_oxhc_tn_flags_t, const char *);

extern bool topo_oxhc_mgs_sensor(topo_mod_t *, tnode_t *, const char *,
    uint32_t, uint32_t, ipcc_sensor_id_t);

/*
 * Miscellaneous utility functions.
 */
extern void topo_oxhc_libipcc_error(topo_mod_t *, libipcc_handle_t *,
    const char *);

/*
 * Inventory related setup.
 */
extern int topo_oxhc_inventory_init(topo_mod_t *, libipcc_handle_t *, oxhc_t *);
extern void topo_oxhc_inventory_fini(topo_mod_t *, oxhc_t *);
extern libipcc_inv_t *topo_oxhc_inventory_find(const oxhc_t *,
    const char *, ipcc_inv_type_t);
extern bool topo_oxhc_inventory_bcopy(libipcc_inv_t *, ipcc_inv_type_t,
    void *, size_t, size_t);
extern bool topo_oxhc_inventory_bcopyoff(libipcc_inv_t *, void *, size_t,
    size_t);

/*
 * IC related functions.
 */
extern int topo_oxhc_enum_ic(topo_mod_t *, const oxhc_t *, tnode_t *,
    const char *, uint32_t, const oxhc_ic_board_t *, size_t);

/*
 * Fan related functions.
 */
extern int topo_oxhc_enum_gimlet_fan_tray(topo_mod_t *, const oxhc_t *,
    const oxhc_enum_t *, tnode_t *, tnode_t *, topo_instance_t,
    topo_instance_t);
extern int topo_oxhc_enum_cosmo_fan_tray(topo_mod_t *, const oxhc_t *,
    const oxhc_enum_t *, tnode_t *, tnode_t *, topo_instance_t,
    topo_instance_t);

/*
 * Sharkfin related functions.
 */
extern int topo_oxhc_enum_sharkfin(topo_mod_t *, const oxhc_t *,
    const oxhc_enum_t *, tnode_t *, tnode_t *, topo_instance_t,
    topo_instance_t);

/*
 * Miscellaneous enumeration functions.
 */
extern di_node_t topo_oxhc_slot_to_devi(topo_mod_t *, uint32_t);
extern int topo_oxhc_enum_pcie(topo_mod_t *, tnode_t *, di_node_t);

/*
 * Property groups.
 */
extern const topo_pgroup_info_t oxhc_pgroup;
extern const topo_pgroup_info_t oxhc_storage_pgroup;

#ifdef __cplusplus
}
#endif

#endif /* _OXHC_H */
