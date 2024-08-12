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

#ifndef _ZEN_KMDB_IMPL_H
#define	_ZEN_KMDB_IMPL_H

#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/df.h>
#include <io/amdzen/amdzen_client.h>

/*
 * We don't really know how many I/O dies there are in advance;
 * however, the theoretical max is 8 (2P Naples with 4 dies);
 * however, on the Oxide architecture there'll only ever be 2.
 */
#define	MAX_IO_DIES	2
#define	MAX_COMPS	256


/*
 * We define just enough of the board data to be able to identify what kind of
 * system we're running on.
 *
 * Must be kept in sync with the definitions in oxide/sys/platform_detect.h
 */
typedef struct mdb_oxide_board_cpuinfo {
	x86_chiprev_t			obc_chiprev;
} mdb_oxide_board_cpuinfo_t;
typedef struct mdb_oxide_board_data {
	mdb_oxide_board_cpuinfo_t	obd_cpuinfo;
} mdb_oxide_board_data_t;

/*
 * Represents a specific DF Component.
 */
typedef struct df_comp {
	/*
	 * InstanceID -- a unique identifier within a node for accessing
	 * per-instance component registers.
	 *
	 * Rome through Milan unfortunately use a discontinuous scheme hence
	 * why we require this to be explicitly provided.
	 */
	const uint16_t dc_inst;

	/*
	 * Component name.
	 */
	const char *dc_name;

	/*
	 * Number of supported DRAM rules for this component.
	 */
	const uint_t dc_ndram;

	/*
	 * Whether this component is a valid destination for routing or
	 * mapping rules -- in essence: can it have a FabricID?
	 */
	const boolean_t dc_invalid_dest;
} df_comp_t;

/*
 * Fixed and dynamically discovered properties of the DF on the current system.
 */
typedef struct df_props {
	/*
	 * The major DF revision -- determines register definitions we'll use.
	 */
	const df_rev_t dfp_rev;

	/*
	 * The maximum number of PCI Bus configuration address maps.
	 */
	const uint_t dfp_max_cfgmap;

	/*
	 * The default instance to use for DRAM & I/O ports when not specified.
	 */
	const uint16_t dfp_dram_io_inst;

	/*
	 * The default instance to use for MMIO & PCI buses when not specified.
	 */
	const uint16_t dfp_mmio_pci_inst;

	/*
	 * The list of components that we know about on this system.
	 */
	const df_comp_t *dfp_comps;
	const size_t dfp_comps_count;

	/*
	 * Mapping of channel interleave values to human-readable names.
	 */
	const char **dfp_chan_ileaves;
	const size_t dfp_chan_ileaves_count;

	/*
	 * The number of UMC instances on this system.
	 */
	const size_t dfp_umc_count;
	/*
	 * Mapping of UMC instance to channel name.
	 */
	const char **dfp_umc_chan_map;
	/*
	 * Order to iterate through UMC instances in output (board order).
	 */
	const uint8_t *dfp_umc_order;

	/*
	 * The rest of the fields are dynamically discovered and cached
	 * in df_props_init().
	 */

	/*
	 * Lookup table for ComponentID to an InstanceID (per-IO die).
	 *
	 * On first glance it would seem like we could simply hardcode these
	 * using the mapping provided in the PPRs.  However, that assumes a
	 * system with all components present and enabled.  In practise though
	 * something like, e.g., some DIMM slots being empty could mean the
	 * corresponding UMCs are disabled thus throwing off the mapping.
	 * Instead, we dynamically read DF::FabricBlockInstanceInformation3 for
	 * each instance to fill this in.
	 *
	 * Besides disabled components, some are also just never valid mapping
	 * or routing targets (e.g. TCDXs, CAKEs).
	 */
	uint16_t dfp_comp_map[MAX_IO_DIES][MAX_COMPS];

	/*
	 * The information necessary to (de)composing Fabric/Node/Component IDs.
	 */
	df_fabric_decomp_t dfp_decomp;
} df_props_t;

extern df_props_t df_props_genoa;
extern df_props_t df_props_milan;
extern df_props_t df_props_turin;

#endif /* _ZEN_KMDB_IMPL_H */
