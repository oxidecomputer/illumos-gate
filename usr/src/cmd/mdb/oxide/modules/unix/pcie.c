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
 * mdb support for PCIe-related dcmds:
 *	::ltssm
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_target.h>
#include <mdb/mdb_ctf.h>

#include <sys/sysmacros.h>
#include <io/amdzen/zen_pcie_ltssm_decode.h>

#include <stddef.h>
#include <stdbool.h>

#include "pcie.h"
#include "target.h"

#define	ZEN_PCIE_LC_STATE_NREGS		6
#define	ZEN_PCIE_LC_STATE_PER_REG	4

/*
 * Shadow types for the fabric structures that we need to read.
 */
typedef struct {
	uint16_t	zio_pci_busno;
	uint8_t		zio_npcie_cores;
} mdb_ltssm_ioms_t;

typedef struct {
	uint8_t		zpc_nports;
} mdb_ltssm_core_t;

typedef struct {
	uint8_t		zpp_device;
	uint8_t		zpp_func;
	uintptr_t	zpp_dbg;
} mdb_ltssm_port_t;

typedef struct {
	size_t		zpd_nregs;
} mdb_ltssm_dbg_t;

/*
 * A captured register record (zen_pcie_reg_dbg_t) read from the target.
 */
typedef struct {
	uintptr_t	lr_name;
	uint32_t	*lr_val;
	hrtime_t	*lr_ts;
} ltssm_reg_t;

/*
 * The fabric layout as described by the target's CTF, resolved once per
 * invocation. The ll_stage_enum and ll_link_{up,down} fields let us decode
 * stages by name so that we don't need a private copy of the kernel's enum.
 */
typedef struct {
	ulong_t		ll_cores_off;
	size_t		ll_core_sz;
	ulong_t		ll_ports_off;
	size_t		ll_port_sz;
	ulong_t		ll_regs_off;
	size_t		ll_reg_sz;
	ulong_t		ll_name_off;
	ulong_t		ll_val_off;
	ulong_t		ll_ts_off;
	uint_t		ll_nstages;
	mdb_ctf_id_t	ll_stage_enum;
	int		ll_link_up;
	int		ll_link_down;
} ltssm_layout_t;

static ssize_t
ltssm_type_size(const char *name)
{
	mdb_ctf_id_t id;

	if (mdb_ctf_lookup_by_name(name, &id) != 0)
		return (-1);
	return (mdb_ctf_type_size(id));
}

static bool
ltssm_resolve(const char *type, const char *member, const char *elem,
    ulong_t *offp, size_t *szp)
{
	int off;
	ssize_t sz;

	if ((off = mdb_ctf_offsetof_by_name(type, member)) < 0)
		return (false);
	if ((sz = ltssm_type_size(elem)) < 0) {
		mdb_warn("failed to determine size of %s\n", elem);
		return (false);
	}
	*offp = (ulong_t)off;
	*szp = (size_t)sz;
	return (true);
}

typedef struct {
	const char	*lmf_name;
	mdb_ctf_id_t	lmf_id;
	bool		lmf_found;
} ltssm_member_find_t;

static int
ltssm_member_find_cb(const char *name, mdb_ctf_id_t id, ulong_t off __unused,
    void *arg)
{
	ltssm_member_find_t *f = arg;

	if (strcmp(name, f->lmf_name) == 0) {
		f->lmf_id = id;
		f->lmf_found = true;
		return (1);
	}
	return (0);
}

/*
 * Find the CTF type of a named struct member. mdb_ctf_member_info() is not part
 * of the dmod API, so we iterate the members ourselves.
 */
static bool
ltssm_member_type(const char *type, const char *member, mdb_ctf_id_t *idp)
{
	mdb_ctf_id_t tid;
	ltssm_member_find_t f = { .lmf_name = member };

	if (mdb_ctf_lookup_by_name(type, &tid) != 0) {
		mdb_warn("couldn't find type %s\n", type);
		return (false);
	}
	(void) mdb_ctf_member_iter(tid, ltssm_member_find_cb, &f, 0);
	if (!f.lmf_found) {
		mdb_warn("couldn't find member %s of %s\n", member, type);
		return (false);
	}
	*idp = f.lmf_id;
	return (true);
}

/*
 * Resolve an enumerator's value by scanning the enum for its name.
 */
static bool
ltssm_enum_value(mdb_ctf_id_t id, uint_t nstages, const char *name, int *valp)
{
	for (uint_t v = 0; v < nstages; v++) {
		const char *en = mdb_ctf_enum_name(id, (int)v);

		if (en != NULL && strcmp(en, name) == 0) {
			*valp = (int)v;
			return (true);
		}
	}
	mdb_warn("couldn't find enumerator %s\n", name);
	return (false);
}

static bool
ltssm_layout_init(ltssm_layout_t *l)
{
	mdb_ctf_id_t valid;
	mdb_ctf_arinfo_t ar;
	int coff, voff, toff;

	if (!ltssm_resolve("zen_ioms_t", "zio_pcie_cores", "zen_pcie_core_t",
	    &l->ll_cores_off, &l->ll_core_sz) ||
	    !ltssm_resolve("zen_pcie_core_t", "zpc_ports", "zen_pcie_port_t",
	    &l->ll_ports_off, &l->ll_port_sz) ||
	    !ltssm_resolve("zen_pcie_dbg_t", "zpd_regs", "zen_pcie_reg_dbg_t",
	    &l->ll_regs_off, &l->ll_reg_sz)) {
		return (false);
	}

	if ((coff = mdb_ctf_offsetof_by_name("zen_pcie_reg_dbg_t",
	    "zprd_name")) < 0 ||
	    (voff = mdb_ctf_offsetof_by_name("zen_pcie_reg_dbg_t",
	    "zprd_val")) < 0 ||
	    (toff = mdb_ctf_offsetof_by_name("zen_pcie_reg_dbg_t",
	    "zprd_ts")) < 0) {
		return (false);
	}
	l->ll_name_off = (ulong_t)coff;
	l->ll_val_off = (ulong_t)voff;
	l->ll_ts_off = (ulong_t)toff;

	if (!ltssm_member_type("zen_pcie_reg_dbg_t", "zprd_val", &valid) ||
	    mdb_ctf_array_info(valid, &ar) != 0) {
		mdb_warn("couldn't determine zprd_val array length\n");
		return (false);
	}
	l->ll_nstages = ar.mta_nelems;

	if (mdb_ctf_lookup_by_name("enum zen_pcie_config_stage",
	    &l->ll_stage_enum) != 0) {
		mdb_warn("couldn't find enum zen_pcie_config_stage\n");
		return (false);
	}
	return (ltssm_enum_value(l->ll_stage_enum, l->ll_nstages,
	    "ZPCS_LINK_UP", &l->ll_link_up) &&
	    ltssm_enum_value(l->ll_stage_enum, l->ll_nstages,
	    "ZPCS_LINK_DOWN", &l->ll_link_down));
}

static const char *
ltssm_stage_name(const ltssm_layout_t *l, uint_t stage)
{
	const char *id = mdb_ctf_enum_name(l->ll_stage_enum, (int)stage);

	if (id == NULL)
		return ("?");
	return (id);
}

typedef struct {
	const char	*lsr_name;
	int		lsr_idx;
} ltssm_lc_reg_t;

static ltssm_lc_reg_t lc_state_regs[ZEN_PCIE_LC_STATE_NREGS] = {
	{ "PCIEPORT::PCIE_LC_STATE0", -1 },
	{ "PCIEPORT::PCIE_LC_STATE1", -1 },
	{ "PCIEPORT::PCIE_LC_STATE2", -1 },
	{ "PCIEPORT::PCIE_LC_STATE3", -1 },
	{ "PCIEPORT::PCIE_LC_STATE4", -1 },
	{ "PCIEPORT::PCIE_LC_STATE5", -1 }
};
CTASSERT(ARRAY_SIZE(lc_state_regs) == ZEN_PCIE_LC_STATE_NREGS);

/*
 * Resolve each LC_STATE register name to its index within a port's captured
 * register array, caching the result for subsequent ports (and subsequent
 * invocations). Returns false if not all of the registers are present, in
 * which case this port has no LTSSM capture for us to decode.
 */
static bool
ltssm_resolve_lc_regs(const ltssm_reg_t *regs, size_t nregs)
{
	static bool resolved = false;

	if (resolved)
		return (true);

	for (size_t r = 0; r < nregs; r++) {
		char name[sizeof ("PCIEPORT::PCIE_LC_STATEn")];

		if (mdb_readstr(name, sizeof (name), regs[r].lr_name) <= 0)
			continue;
		for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
			if (lc_state_regs[i].lsr_idx == -1 &&
			    strcmp(name, lc_state_regs[i].lsr_name) == 0) {
				lc_state_regs[i].lsr_idx = (int)r;
				break;
			}
		}
	}

	for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
		if (lc_state_regs[i].lsr_idx == -1)
			return (false);
	}

	resolved = true;
	return (true);
}

typedef struct {
	uintptr_t		lc_saddr;
	bool			lc_detail;
	bool			lc_pipe;
	x86_processor_family_t	lc_fam;
	ltssm_layout_t		lc_layout;
} ltssm_cb_t;

static void
ltssm_print_port(uintptr_t addr, const mdb_ltssm_port_t *port, uint16_t busno,
    const ltssm_cb_t *cb)
{
	uintptr_t dbgaddr = port->zpp_dbg;
	const ltssm_layout_t *l = &cb->lc_layout;
	size_t nstages = l->ll_nstages;
	mdb_ltssm_dbg_t hdr;
	ltssm_reg_t *regs;
	const ltssm_reg_t *lc0;
	size_t nregs;

	if (dbgaddr == 0)
		return;
	if (mdb_ctf_vread(&hdr, "zen_pcie_dbg_t", "mdb_ltssm_dbg_t", dbgaddr,
	    MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if ((nregs = hdr.zpd_nregs) == 0)
		return;

	regs = mdb_zalloc(nregs * sizeof (ltssm_reg_t), UM_NOSLEEP | UM_GC);
	if (regs == NULL) {
		mdb_warn("failed to allocate memory for port capture");
		return;
	}
	for (size_t r = 0; r < nregs; r++) {
		uintptr_t raddr = dbgaddr + l->ll_regs_off + r * l->ll_reg_sz;

		regs[r].lr_val = mdb_alloc(nstages * sizeof (uint32_t),
		    UM_NOSLEEP | UM_GC);
		regs[r].lr_ts = mdb_alloc(nstages * sizeof (hrtime_t),
		    UM_NOSLEEP | UM_GC);
		if (regs[r].lr_val == NULL || regs[r].lr_ts == NULL) {
			mdb_warn("failed to allocate memory for port capture");
			return;
		}
		if (mdb_vread(&regs[r].lr_name, sizeof (regs[r].lr_name),
		    raddr + l->ll_name_off) == -1 ||
		    mdb_vread(regs[r].lr_val, nstages * sizeof (uint32_t),
		    raddr + l->ll_val_off) == -1 ||
		    mdb_vread(regs[r].lr_ts, nstages * sizeof (hrtime_t),
		    raddr + l->ll_ts_off) == -1) {
			mdb_warn("failed to read port capture at %p", dbgaddr);
			return;
		}
	}

	if (!ltssm_resolve_lc_regs(regs, nregs))
		return;
	lc0 = &regs[lc_state_regs[0].lsr_idx];

	/*
	 * In the summary (no-address) case, print one line per port giving its
	 * most recently captured current state, and flag whether a link-up or
	 * link-down event was captured. Use the detailed form (with an explicit
	 * port address) to see the full per-stage history.
	 */
	if (!cb->lc_detail) {
		uint_t latest = nstages;
		hrtime_t latest_ts = 0;
		const char *nm;
		char bdf[16];
		uint8_t cur;

		for (uint_t st = 0; st < nstages; st++) {
			if (lc0->lr_ts[st] > latest_ts) {
				latest_ts = lc0->lr_ts[st];
				latest = st;
			}
		}
		if (latest == nstages)
			return;

		/*
		 * When our output is being piped, emit just the port address so
		 * that it can be fed back into ::ltssm (or another dcmd).
		 */
		if (cb->lc_pipe) {
			mdb_printf("%lr\n", addr);
			return;
		}

		cur = lc0->lr_val[latest] & 0x3f;
		if (!zen_ltssm_lookup(cb->lc_fam, cur, &nm, NULL, NULL))
			nm = "<unknown>";
		(void) mdb_snprintf(bdf, sizeof (bdf), "%r/%r/%r",
		    busno, port->zpp_device, port->zpp_func);

		mdb_printf("%?p %-8s %-24s", addr, bdf, nm);
		if (lc0->lr_ts[l->ll_link_up] != 0)
			mdb_printf(" up");
		if (lc0->lr_ts[l->ll_link_down] != 0)
			mdb_printf(" down");
		mdb_printf("\n");
		return;
	}

	mdb_printf("%<b>%?p %r/%r/%r%</b>\n", addr, busno,
	    port->zpp_device, port->zpp_func);
	for (uint_t st = 0; st < nstages; st++) {
		hrtime_t ts = lc0->lr_ts[st];
		uint_t idx = 0;
		char tbuf[32];

		if (ts == 0)
			continue;
		mdb_nicetime(ts, tbuf, sizeof (tbuf));
		mdb_printf("    %s (%llu ns; %s)\n", ltssm_stage_name(l, st),
		    (u_longlong_t)ts, tbuf);
		for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
			int ridx = lc_state_regs[i].lsr_idx;
			uint32_t v = regs[ridx].lr_val[st];

			for (uint_t j = 0; j < ZEN_PCIE_LC_STATE_PER_REG; j++) {
				uint8_t raw = (v >> (j * 8)) & 0x3f;
				const char *nm;

				if (!zen_ltssm_lookup(cb->lc_fam, raw, &nm,
				    NULL, NULL)) {
					nm = "<unknown>";
				}
				mdb_printf("        %2u 0x%02x %s\n",
				    idx, raw, nm);
				idx++;
			}
		}
	}
}

static int
ltssm_ioms_cb(uintptr_t addr, const void *arg __unused, void *cbdata)
{
	const ltssm_cb_t *cb = cbdata;
	mdb_ltssm_ioms_t ioms;

	if (mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_ltssm_ioms_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0) {
		return (WALK_NEXT);
	}

	for (uint_t c = 0; c < ioms.zio_npcie_cores; c++) {
		uintptr_t caddr = addr + cb->lc_layout.ll_cores_off +
		    c * cb->lc_layout.ll_core_sz;
		mdb_ltssm_core_t core;

		if (mdb_ctf_vread(&core, "zen_pcie_core_t", "mdb_ltssm_core_t",
		    caddr, MDB_CTF_VREAD_QUIET) != 0) {
			continue;
		}

		for (uint_t p = 0; p < core.zpc_nports; p++) {
			uintptr_t paddr = caddr + cb->lc_layout.ll_ports_off +
			    p * cb->lc_layout.ll_port_sz;
			mdb_ltssm_port_t port;

			if (cb->lc_saddr != 0 && cb->lc_saddr != paddr)
				continue;
			if (mdb_ctf_vread(&port, "zen_pcie_port_t",
			    "mdb_ltssm_port_t", paddr,
			    MDB_CTF_VREAD_QUIET) != 0) {
				continue;
			}
			ltssm_print_port(paddr, &port, ioms.zio_pci_busno, cb);
		}
	}

	return (WALK_NEXT);
}

void
ltssm_dcmd_help(void)
{
	mdb_printf(
	    "Decode captured PCIe LTSSM state history for PCIe ports.\n"
	    "\n"
	    "With no address, prints a line per port giving its most recently\n"
	    "captured state; the EVENTS column flags 'up'/'down' for ports\n"
	    "with link-up/link-down captures. Given a port address, prints\n"
	    "the full decoded history for each of its capture points.\n"
	    "\n"
	    "LTSSM state is captured at various boot stages and whenever a\n"
	    "link comes up or goes down; this command decodes those\n"
	    "captures. The current live state is not shown here.\n");
}

int
ltssm_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	ltssm_cb_t cb = { 0 };

	if (argc != 0)
		return (DCMD_USAGE);

	if (!ltssm_layout_init(&cb.lc_layout)) {
		mdb_warn("failed to resolve fabric type layout from CTF\n");
		return (DCMD_ERR);
	}

	cb.lc_fam = oxide_mdb_target_family();
	if (cb.lc_fam == X86_PF_UNKNOWN) {
		mdb_warn(
		    "unknown target processor family; set one with ::target\n");
		return (DCMD_ERR);
	}

	if (flags & DCMD_ADDRSPEC) {
		cb.lc_saddr = addr;
		cb.lc_detail = true;
	} else if (flags & DCMD_PIPE_OUT) {
		cb.lc_pipe = true;
	} else if (DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %-8s %-24s %s%</u>\n", "ADDR", "B/D/F",
		    "STATE", "EVENTS");
	}

	if (mdb_walk("ioms", ltssm_ioms_cb, &cb) == -1) {
		mdb_warn("failed to walk ioms");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}
