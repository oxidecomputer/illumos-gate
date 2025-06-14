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

#ifndef _OXHC_IC_H
#define	_OXHC_IC_H

/*
 * This header file contains information to define IC topo related features.
 */

#ifdef __cplusplus
extern "C" {
#endif

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
 * need. Names are stored separately from the actual sensor definition here as
 * we often have the same set of devices that are used, but all have different
 * labels. A given sensor's name is used to reflect semantic information about
 * what the sensor is for. For a single temperature sensor for a device this may
 * be something simple like "temp". For a power controller we want not only the
 * rail name, but also what it is as we often have more than one sensor of a
 * given type (e.g. a Vin, Vout, Vout_min, Vout_max, etc.).
 */
typedef struct {
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
	const oxhc_ic_sensor_t *ic_sensors;
	size_t ic_nsensors;
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
	const char **ib_labels;
	size_t ib_nlabels;
} oxhc_ic_board_t;

/*
 * Various predefined sets of IC structures and functions.
 */
extern const oxhc_ic_board_t oxhc_ic_gimlet_main[];
extern const size_t oxhci_ic_gimlet_main_nents;

extern const oxhc_ic_board_t oxhc_ic_cosmo_main[];
extern const size_t oxhc_ic_cosmo_main_nents;

extern const oxhc_ic_board_t oxhc_ic_temp_board[];
extern const size_t oxhc_ic_temp_board_nents;

extern const oxhc_ic_board_t oxhc_ic_fanvpd[];
extern const size_t oxhc_ic_fanvpd_nents;

extern const oxhc_ic_board_t oxhc_ic_sharkfin_gimlet[];
extern const size_t oxhc_ic_sharkfin_gimlet_nents;

extern const oxhc_ic_board_t oxhc_ic_sharkfin_cosmo[];
extern const size_t oxhc_ic_sharkfin_cosmo_nents;

#ifdef __cplusplus
}
#endif

#endif /* _OXHC_IC_H */
