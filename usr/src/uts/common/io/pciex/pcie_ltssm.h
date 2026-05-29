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

#ifndef _IO_PCIE_PCIE_LTSSM_H
#define	_IO_PCIE_PCIE_LTSSM_H

/*
 * This is the private, illumos-specific data model used to convey PCIe LTSSM
 * (Link Training and Status State Machine) state up from the platform to its
 * consumers (the pcieb ioctl interface). This header is not installed; see
 * pcieb_ioctl.h for the related ioctl ABI.
 */

#include <sys/stdint.h>
#include <sys/time.h>
#include <sys/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A single observation of the LTSSM. This captures the raw platform-specific
 * encoding (ple_raw), the common state and sub-state that it maps to (ple_state
 * and ple_substate, a pcie_ltssm_state_t and pcie_ltssm_substate_t), and a
 * human-readable name for the platform's (potentially finer-grained) state.
 * These structures are used as part of a driver ioctl ABI, so all fields are of
 * fixed width.
 */
#define	PCIE_LTSSM_NAMELEN	40

typedef struct pcie_ltssm_entry {
	uint32_t	ple_state;	/* pcie_ltssm_state_t */
	uint32_t	ple_substate;	/* pcie_ltssm_substate_t */
	uint32_t	ple_raw;	/* raw platform-specific encoding */
	char		ple_name[PCIE_LTSSM_NAMELEN];
} pcie_ltssm_entry_t;

/*
 * A single capture of a link's LTSSM: the current state and a number of recent
 * historic states (pls_history[0] being the most recent). pls_time holds the
 * hrtime at which the capture was taken: for an event-driven capture the time
 * of the event, and for a live capture the time the state was read. A capture
 * is only valid when PCIE_LTSSM_SNAP_F_VALID is set in pls_flags.
 */
#define	PCIE_LTSSM_HISTORY_MAX	24

typedef struct pcie_ltssm_snapshot {
	uint32_t		pls_flags;
	uint32_t		pls_nhistory;
	hrtime_t		pls_time;
	pcie_ltssm_entry_t	pls_current;
	pcie_ltssm_entry_t	pls_history[PCIE_LTSSM_HISTORY_MAX];
} pcie_ltssm_snapshot_t;

typedef enum pcie_ltssm_snap_flag {
	PCIE_LTSSM_SNAP_F_VALID	= 1 << 0
} pcie_ltssm_snap_flag_t;

/*
 * The kinds of LTSSM capture that may be requested for a link: the current live
 * state, or the state captured the last time the link was seen to come up or go
 * down. This is the snapshot selector passed to the retrieval interface, which
 * returns the single requested pcie_ltssm_snapshot_t.
 */
typedef enum pcie_ltssm_snap {
	PCIE_LTSSM_SNAP_LIVE = 0,
	PCIE_LTSSM_SNAP_LINK_UP,
	PCIE_LTSSM_SNAP_LINK_DOWN,
	PCIE_LTSSM_NSNAP
} pcie_ltssm_snap_t;

#ifdef __cplusplus
}
#endif

#endif /* _IO_PCIE_PCIE_LTSSM_H */
