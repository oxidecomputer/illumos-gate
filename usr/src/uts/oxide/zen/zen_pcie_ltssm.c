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
 * Retrieval and decoding of the PCIe LTSSM (Link Training and Status State
 * Machine) state for a PCIe port.
 *
 * The 6-bit encoding is the same across the Zen microarchitectures that the
 * Oxide platform supports (only the means of addressing the registers differs);
 * the mapping from that encoding to a name and to the common, specification
 * derived pcie_ltssm_state_t lives in zen_ltssm_decode below.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/bitext.h>
#include <sys/cpuvar.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <io/amdzen/zen_pcie_ltssm_decode.h>
#include <sys/io/zen/platform_impl.h>

static void
zen_pcie_ltssm_decode_entry(uint8_t raw, x86_processor_family_t fam,
    pcie_ltssm_entry_t *e)
{
	const char *name;
	pcie_ltssm_state_t state;
	pcie_ltssm_substate_t substate;

	e->ple_raw = raw;

	if (zen_ltssm_lookup(fam, raw, &name, &state, &substate)) {
		e->ple_state = state;
		e->ple_substate = substate;
		(void) strlcpy(e->ple_name, name, sizeof (e->ple_name));
	} else {
		e->ple_state = PCIE_LTSSM_UNKNOWN;
		e->ple_substate = PCIE_LTSSM_SS_UNKNOWN;
		(void) snprintf(e->ple_name, sizeof (e->ple_name),
		    "0x%02x", raw);
	}
}

/*
 * Decode a set of ZEN_PCIE_LC_STATE_NREGS LC_STATE register values into an
 * LTSSM snapshot. The very first value (LC_STATE0's current field) is the
 * link's current state; everything after it is history, most recent first.
 */
static void
zen_pcie_ltssm_decode_snapshot(const uint32_t *lc, hrtime_t time,
    x86_processor_family_t fam, pcie_ltssm_snapshot_t *snap)
{
	uint_t nhist = 0;

	for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
		uint8_t raw[ZEN_PCIE_LC_STATE_PER_REG];

		/*
		 * Each register holds the current state and the three preceding
		 * it, most recent first. These bit positions match the
		 * PCIE_PORT_LC_STATE_GET_{CUR,PREV1,PREV2,PREV3} macros in the
		 * per-microarchitecture headers.
		 */
		raw[0] = bitx32(lc[i], 5, 0);
		raw[1] = bitx32(lc[i], 13, 8);
		raw[2] = bitx32(lc[i], 21, 16);
		raw[3] = bitx32(lc[i], 29, 24);

		for (uint_t j = 0; j < ZEN_PCIE_LC_STATE_PER_REG; j++) {
			if (i == 0 && j == 0) {
				zen_pcie_ltssm_decode_entry(raw[j], fam,
				    &snap->pls_current);
			} else {
				zen_pcie_ltssm_decode_entry(raw[j], fam,
				    &snap->pls_history[nhist++]);
			}
		}
	}

	snap->pls_nhistory = nhist;
	snap->pls_time = time;
	snap->pls_flags = PCIE_LTSSM_SNAP_F_VALID;
}

/*
 * Read the live LC_STATE registers for a port.
 */
static int
zen_pcie_ltssm_read_live(zen_pcie_port_t *port, uint32_t *lc)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();

	for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
		char name[sizeof ("PCIEPORT::PCIE_LC_STATEn")];
		smn_reg_def_t def;

		(void) snprintf(name, sizeof (name),
		    "PCIEPORT::PCIE_LC_STATE%u", i);
		if (!zen_pcie_port_dbg_reg_by_name(name, &def))
			return (ENOTSUP);

		lc[i] = zen_pcie_port_read(port,
		    ops->zfo_pcie_port_reg(port, def));
	}

	return (0);
}

/*
 * Read a previously captured set of LC_STATE register values for a port at the
 * given stage. Returns true only if a capture is present, indicated by all of
 * the registers being found with a non-zero timestamp.
 */
static bool
zen_pcie_ltssm_read_stage(const zen_pcie_port_t *port, uint32_t stage,
    uint32_t *lc, hrtime_t *timep)
{
	hrtime_t ts = 0;

	for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
		char name[sizeof ("PCIEPORT::PCIE_LC_STATEn")];

		(void) snprintf(name, sizeof (name),
		    "PCIEPORT::PCIE_LC_STATE%u", i);
		if (!zen_pcie_port_dbg_val_by_name(port, name, stage, &lc[i],
		    i == 0 ? &ts : NULL)) {
			return (false);
		}
	}

	if (ts == 0)
		return (false);

	*timep = ts;
	return (true);
}

/*
 * Fill in the single requested LTSSM snapshot for a port. The live state is
 * read ad-hoc from the hardware and timestamped with the time of the read; the
 * link-up and link-down states come from the captures taken when the link last
 * changed state (ENOENT if no such capture has been taken).
 */
static int
zen_pcie_port_ltssm(zen_pcie_port_t *port, pcie_ltssm_snap_t snap,
    pcie_ltssm_snapshot_t *out)
{
	uint32_t lc[ZEN_PCIE_LC_STATE_NREGS];
	x86_processor_family_t fam = chiprev_family(cpuid_getchiprev(CPU));
	hrtime_t ts;
	int ret;

	bzero(out, sizeof (*out));

	switch (snap) {
	case PCIE_LTSSM_SNAP_LIVE:
		ret = zen_pcie_ltssm_read_live(port, lc);
		if (ret != 0)
			return (ret);
		zen_pcie_ltssm_decode_snapshot(lc, gethrtime(), fam, out);
		return (0);
	case PCIE_LTSSM_SNAP_LINK_UP:
		if (!zen_pcie_ltssm_read_stage(port, ZPCS_LINK_UP, lc, &ts))
			return (ENOENT);
		zen_pcie_ltssm_decode_snapshot(lc, ts, fam, out);
		return (0);
	case PCIE_LTSSM_SNAP_LINK_DOWN:
		if (!zen_pcie_ltssm_read_stage(port, ZPCS_LINK_DOWN, lc, &ts))
			return (ENOENT);
		zen_pcie_ltssm_decode_snapshot(lc, ts, fam, out);
		return (0);
	default:
		return (EINVAL);
	}
}

int
zen_pcie_ltssm_by_bdf(uint8_t bus, uint8_t dev, uint8_t func,
    pcie_ltssm_snap_t snap, pcie_ltssm_snapshot_t *out)
{
	zen_pcie_port_t *port;

	port = zen_fabric_find_pcie_port_by_bdf(bus, dev, func);
	if (port == NULL)
		return (ENXIO);

	return (zen_pcie_port_ltssm(port, snap, out));
}

int
zen_pcie_ltssm_link_event_by_bdf(uint8_t bus, uint8_t dev, uint8_t func,
    bool up)
{
	zen_pcie_port_t *port;

	port = zen_fabric_find_pcie_port_by_bdf(bus, dev, func);
	if (port == NULL)
		return (ENXIO);

	zen_pcie_port_dbg_snapshot(port, up ? ZPCS_LINK_UP : ZPCS_LINK_DOWN);

	return (0);
}
