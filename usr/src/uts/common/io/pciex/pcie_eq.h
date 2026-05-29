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

#ifndef _IO_PCIE_PCIE_EQ_H
#define	_IO_PCIE_PCIE_EQ_H

/*
 * This is the private, illumos-specific data model used to convey PCIe link
 * equalisation (EQ) state up from the platform to its consumers (the pcieb
 * ioctl interface). This header is not installed; see pcieb_ioctl.h for the
 * related ioctl ABI.
 *
 * Equalisation applies at the 8.0 GT/s signalling rate and above (PCIe
 * generation 3 onwards); there is nothing to report for slower links. The
 * generations a given platform can actually report on are up to its
 * microarchitecture.
 */

#include <sys/stdint.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A preset search mask is a bitmask of the transmitter presets (P0 through P9)
 * that the link is permitted to consider during equalisation; bit i selects
 * preset Pi. The meaning of particular mask values is implementation-defined:
 * some platforms (AMD among them) treat an all-zero mask as "consider every
 * preset" rather than none. Rather than have consumers reason about such cases
 * from the raw value, a platform that is considering every preset says so via
 * PCIE_EQ_F_ALL_PRESETS (see pcie_eq_flags_t below).
 */
#define	PCIE_EQ_NPRESETS	10

/*
 * The maximum number of lanes for which we report per-lane equalisation data.
 * This matches the widest PCIe link (x16).
 */
#define	PCIE_EQ_MAX_LANES	16

/*
 * The equalisation settings for a single lane. Two sets of values are
 * reported. pel_best_* are the settings the link found during its equalisation
 * search, along with the figure of merit (pel_best_fom) it scored. pel_local_*
 * are the settings actually in use by the near-end (local) transmitter. The
 * two differ when the link did not end up adopting the best result it found,
 * and the representation differs depending on whether a preset or explicit
 * coefficients are in use (see PCIE_EQ_LANE_F_USE_PRESET). Per-lane data is
 * only present while the link is up. With the link down peq_nlanes is 0 and no
 * best or local values are reported. These structures are used as part of a
 * driver ioctl ABI, so all fields are of fixed width.
 */
typedef struct pcie_eq_lane {
	uint32_t	pel_lane;
	uint32_t	pel_flags;
	uint32_t	pel_best_preset;
	uint32_t	pel_best_precursor;
	uint32_t	pel_best_cursor;
	uint32_t	pel_best_postcursor;
	uint32_t	pel_best_fom;		/* figure of merit */
	uint32_t	pel_local_preset;
	uint32_t	pel_local_precursor;
	uint32_t	pel_local_cursor;
	uint32_t	pel_local_postcursor;
} pcie_eq_lane_t;

typedef enum pcie_eq_lane_flags {
	/*
	 * PCIE_EQ_LANE_F_USE_PRESET_VALID indicates whether the platform could
	 * report the local transmitter's use-preset state; not every platform
	 * can. When it is set, PCIE_EQ_LANE_F_USE_PRESET says whether the local
	 * transmitter is using a preset (pel_local_preset) rather than
	 * individual coefficients. Consumers must treat the use-preset state as
	 * unknown when VALID is clear.
	 */
	PCIE_EQ_LANE_F_USE_PRESET_VALID	= 1 << 0,
	PCIE_EQ_LANE_F_USE_PRESET	= 1 << 1
} pcie_eq_lane_flags_t;

typedef enum pcie_eq_flags {
	/*
	 * PCIE_EQ_F_ALL_PRESETS indicates that the platform's effective
	 * behaviour is to consider every preset during equalisation,
	 * irrespective of the raw value in peq_mask. It exists so that a
	 * consumer need not know how a given platform encodes "all presets"
	 * (for example, AMD uses an all-zero mask).
	 */
	PCIE_EQ_F_ALL_PRESETS		= 1 << 0,
	/*
	 * PCIE_EQ_F_LINKUP_VALID indicates that peq_mask_linkup and
	 * peq_linkup_time are populated: the platform was able to report the
	 * preset mask that was in effect when the link last came up.
	 * PCIE_EQ_F_LINKUP_ALL_PRESETS is the PCIE_EQ_F_ALL_PRESETS equivalent
	 * for that link-up mask.
	 */
	PCIE_EQ_F_LINKUP_VALID		= 1 << 1,
	PCIE_EQ_F_LINKUP_ALL_PRESETS	= 1 << 2
} pcie_eq_flags_t;

/*
 * A capture of a link's equalisation state for a single PCIe generation:
 * peq_gen is the generation the data is for (3 or greater), peq_mask is the raw
 * preset search mask for that generation, and peq_flags is a set of
 * pcie_eq_flags_t describing the capture as a whole.
 *
 * peq_mask is the mask programmed now, which governs the next equalisation.
 * When PCIE_EQ_F_LINKUP_VALID is set, peq_mask_linkup is the raw mask that was
 * in effect when the link last came up and peq_linkup_time is the hrtime of
 * that event, so that a consumer can tell a freshly programmed mask from the
 * one that actually produced the current per-lane results.
 *
 * peq_nlanes is an output: the platform fills it in with the number of lanes it
 * has reported data for, which may be fewer than the link's negotiated width if
 * the platform could not read every lane (and is 0 while the link is down). The
 * valid per-lane entries are peq_lanes[0] through peq_lanes[peq_nlanes - 1],
 * always populated in increasing lane order with pel_lane equal to the array
 * index; entries are never sparse or reordered.
 */
typedef struct pcie_eq {
	uint32_t	peq_gen;
	uint32_t	peq_mask;
	uint32_t	peq_flags;
	uint32_t	peq_mask_linkup;
	hrtime_t	peq_linkup_time;
	uint32_t	peq_nlanes;
	pcie_eq_lane_t	peq_lanes[PCIE_EQ_MAX_LANES];
} pcie_eq_t;

#ifdef __cplusplus
}
#endif

#endif /* _IO_PCIE_PCIE_EQ_H */
