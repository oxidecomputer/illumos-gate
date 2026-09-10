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
#ifdef _KMDB
#include "zen_kmdb.h"
#include "zen_kmdb_impl.h"
#endif

/*
 * The LTSSM state history is held in the PCIEPORT::PCIE_LC_STATE[0..5]
 * registers, each of which packs four 6-bit state values. The current state is
 * in the low bits of the first register, with progressively older states in
 * the higher bits and subsequent registers.
 */
#define	ZEN_PCIE_LC_STATE_NREGS		6
#define	ZEN_PCIE_LC_STATE_PER_REG	4
#define	ZEN_PCIE_LC_STATE_MASK		0x3f

/*
 * Shadow types for the fabric structures that we need to read. A port's bus
 * number, and the numbers that address its SMN registers, are held by its
 * ancestors which we reach through the parent pointers.
 */
typedef struct {
	uint8_t		zs_num;
} mdb_ltssm_soc_t;

typedef struct {
	uintptr_t	zi_soc;
} mdb_ltssm_iodie_t;

typedef struct {
	uintptr_t	zn_iodie;
} mdb_ltssm_nbio_t;

typedef struct {
	uint8_t		zio_iohcnum;
	uint16_t	zio_pci_busno;
	uintptr_t	zio_nbio;
} mdb_ltssm_ioms_t;

typedef struct {
	uint8_t		zpc_coreno;
	uintptr_t	zpc_ioms;
} mdb_ltssm_core_t;

typedef struct {
	uint8_t		zpp_portno;
	uint8_t		zpp_device;
	uint8_t		zpp_func;
	uintptr_t	zpp_core;
	uintptr_t	zpp_dbg;
} mdb_ltssm_port_t;

typedef struct {
	size_t		zpd_nregs;
} mdb_ltssm_dbg_t;

/*
 * What we need to know about a port, gathered from it and its ancestors.
 */
typedef struct {
	uintptr_t	lp_addr;
	uintptr_t	lp_dbg;
	uint16_t	lp_busno;
	uint8_t		lp_device;
	uint8_t		lp_func;
	uint8_t		lp_sock;
	uint8_t		lp_iohcno;
	uint8_t		lp_coreno;
	uint8_t		lp_portno;
} ltssm_port_t;

/*
 * A captured register record (zen_pcie_reg_dbg_t) read from the target.
 */
typedef struct {
	uintptr_t	lr_name;
	uint32_t	*lr_val;
	hrtime_t	*lr_ts;
} ltssm_reg_t;

/*
 * The capture layout as described by the target's CTF, resolved once per
 * invocation. The ll_stage_enum and ll_link_{up,down} fields let us decode
 * stages by name so that we don't need a private copy of the kernel's enum.
 */
typedef struct {
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

#ifdef _KMDB
typedef smn_reg_t (*ltssm_lc_state_reg_f)(uint8_t, uint8_t, uint8_t, uint_t);
#endif

typedef struct {
	bool			lc_detail;
	bool			lc_pipe;
	bool			lc_live;
	x86_processor_family_t	lc_fam;
	ltssm_layout_t		lc_layout;
#ifdef _KMDB
	ltssm_lc_state_reg_f	lc_lc_state_reg;
#endif
} ltssm_cb_t;

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
	const char	*lev_name;
	int		lev_val;
	bool		lev_found;
} ltssm_enum_lookup_t;

static int
ltssm_enum_lookup_cb(const char *name, int value, void *arg)
{
	ltssm_enum_lookup_t *l = arg;

	if (strcmp(name, l->lev_name) == 0) {
		l->lev_val = value;
		l->lev_found = true;
		return (1);
	}
	return (0);
}

/*
 * Resolve an enumerator's value by name.
 */
static bool
ltssm_enum_value(mdb_ctf_id_t id, const char *name, int *valp)
{
	ltssm_enum_lookup_t l = { .lev_name = name };

	(void) mdb_ctf_enum_iter(id, ltssm_enum_lookup_cb, &l);
	if (!l.lev_found) {
		mdb_warn("couldn't find enumerator %s\n", name);
		return (false);
	}
	*valp = l.lev_val;
	return (true);
}

static bool
ltssm_layout_init(ltssm_layout_t *l)
{
	mdb_ctf_id_t structid, valid;
	mdb_ctf_arinfo_t ar;
	ulong_t moff;
	int coff, voff, toff;

	if (!ltssm_resolve("zen_pcie_dbg_t", "zpd_regs", "zen_pcie_reg_dbg_t",
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

	if (mdb_ctf_lookup_by_name("zen_pcie_reg_dbg_t", &structid) != 0 ||
	    mdb_ctf_member_info(structid, "zprd_val", &moff, &valid) != 0 ||
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
	return (ltssm_enum_value(l->ll_stage_enum, "ZPCS_LINK_UP",
	    &l->ll_link_up) &&
	    ltssm_enum_value(l->ll_stage_enum, "ZPCS_LINK_DOWN",
	    &l->ll_link_down));
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

/*
 * Read a port along with the chain of ancestors that hold its bus number and
 * the numbers used to address its SMN registers.
 */
static bool
ltssm_port_read(uintptr_t addr, ltssm_port_t *lp)
{
	mdb_ltssm_port_t port;
	mdb_ltssm_core_t core;
	mdb_ltssm_ioms_t ioms;
	mdb_ltssm_nbio_t nbio;
	mdb_ltssm_iodie_t iodie;
	mdb_ltssm_soc_t soc;

	if (mdb_ctf_vread(&port, "zen_pcie_port_t", "mdb_ltssm_port_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&core, "zen_pcie_core_t", "mdb_ltssm_core_t",
	    port.zpp_core, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_ltssm_ioms_t",
	    core.zpc_ioms, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&nbio, "zen_nbio_t", "mdb_ltssm_nbio_t",
	    ioms.zio_nbio, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&iodie, "zen_iodie_t", "mdb_ltssm_iodie_t",
	    nbio.zn_iodie, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&soc, "zen_soc_t", "mdb_ltssm_soc_t", iodie.zi_soc,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read PCIe port at %p\n", addr);
		return (false);
	}

	lp->lp_addr = addr;
	lp->lp_dbg = port.zpp_dbg;
	lp->lp_busno = ioms.zio_pci_busno;
	lp->lp_device = port.zpp_device;
	lp->lp_func = port.zpp_func;
	lp->lp_sock = soc.zs_num;
	lp->lp_iohcno = ioms.zio_iohcnum;
	lp->lp_coreno = core.zpc_coreno;
	lp->lp_portno = port.zpp_portno;
	return (true);
}

/*
 * Read a port's captured register records. Returns NULL if the port has no
 * capture, or its capture does not include the LC_STATE registers.
 */
static ltssm_reg_t *
ltssm_capture_read(const ltssm_port_t *lp, const ltssm_layout_t *l)
{
	uintptr_t dbgaddr = lp->lp_dbg;
	size_t nstages = l->ll_nstages;
	mdb_ltssm_dbg_t hdr;
	ltssm_reg_t *regs;
	size_t nregs;

	if (dbgaddr == 0)
		return (NULL);
	if (mdb_ctf_vread(&hdr, "zen_pcie_dbg_t", "mdb_ltssm_dbg_t", dbgaddr,
	    MDB_CTF_VREAD_QUIET) != 0) {
		return (NULL);
	}
	if ((nregs = hdr.zpd_nregs) == 0)
		return (NULL);

	regs = mdb_zalloc(nregs * sizeof (ltssm_reg_t), UM_NOSLEEP | UM_GC);
	if (regs == NULL) {
		mdb_warn("failed to allocate memory for port capture");
		return (NULL);
	}
	for (size_t r = 0; r < nregs; r++) {
		uintptr_t raddr = dbgaddr + l->ll_regs_off + r * l->ll_reg_sz;

		regs[r].lr_val = mdb_alloc(nstages * sizeof (uint32_t),
		    UM_NOSLEEP | UM_GC);
		regs[r].lr_ts = mdb_alloc(nstages * sizeof (hrtime_t),
		    UM_NOSLEEP | UM_GC);
		if (regs[r].lr_val == NULL || regs[r].lr_ts == NULL) {
			mdb_warn("failed to allocate memory for port capture");
			return (NULL);
		}
		if (mdb_vread(&regs[r].lr_name, sizeof (regs[r].lr_name),
		    raddr + l->ll_name_off) == -1 ||
		    mdb_vread(regs[r].lr_val, nstages * sizeof (uint32_t),
		    raddr + l->ll_val_off) == -1 ||
		    mdb_vread(regs[r].lr_ts, nstages * sizeof (hrtime_t),
		    raddr + l->ll_ts_off) == -1) {
			mdb_warn("failed to read port capture at %p", dbgaddr);
			return (NULL);
		}
	}

	if (!ltssm_resolve_lc_regs(regs, nregs))
		return (NULL);
	return (regs);
}

/*
 * Return the most recently captured stage, or -1 if nothing has been captured.
 */
static int
ltssm_capture_latest(const ltssm_reg_t *lc0, uint_t nstages)
{
	hrtime_t latest_ts = 0;
	int latest = -1;

	for (uint_t st = 0; st < nstages; st++) {
		if (lc0->lr_ts[st] > latest_ts) {
			latest_ts = lc0->lr_ts[st];
			latest = (int)st;
		}
	}
	return (latest);
}

static const char *
ltssm_state_name(const ltssm_cb_t *cb, uint32_t raw)
{
	const char *nm;

	if (!zen_ltssm_lookup(cb->lc_fam,
	    (uint8_t)(raw & ZEN_PCIE_LC_STATE_MASK), &nm, NULL, NULL)) {
		nm = "<unknown>";
	}
	return (nm);
}

/*
 * Print the LTSSM history held in a set of LC_STATE register values, most
 * recent first.
 */
static void
ltssm_print_states(const ltssm_cb_t *cb, const uint32_t *vals)
{
	uint_t idx = 0;

	for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
		for (uint_t j = 0; j < ZEN_PCIE_LC_STATE_PER_REG; j++) {
			uint8_t raw = (vals[i] >> (j * 8)) &
			    ZEN_PCIE_LC_STATE_MASK;

			mdb_printf("        %2u 0x%02x %s\n", idx, raw,
			    ltssm_state_name(cb, raw));
			idx++;
		}
	}
}

static void
ltssm_print_port(const ltssm_port_t *lp)
{
	char bdf[16];

	(void) mdb_snprintf(bdf, sizeof (bdf), "%r/%r/%r",
	    lp->lp_busno, lp->lp_device, lp->lp_func);
	mdb_printf("%?p %-8s", lp->lp_addr, bdf);
}

static void
ltssm_print_heading(const ltssm_port_t *lp)
{
	mdb_printf("%<b>%?p %r/%r/%r%</b>\n", lp->lp_addr, lp->lp_busno,
	    lp->lp_device, lp->lp_func);
}

#ifdef _KMDB
static ltssm_lc_state_reg_f
ltssm_lc_state_reg_func(x86_processor_family_t fam)
{
	switch (fam) {
	case X86_PF_AMD_MILAN:
		return (milan_pcie_port_lc_state_reg);
	case X86_PF_AMD_GENOA:
		return (genoa_pcie_port_lc_state_reg);
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		return (turin_pcie_port_lc_state_reg);
	default:
		return (NULL);
	}
}

/*
 * Read a port's LC_STATE registers from the hardware.
 */
static bool
ltssm_live_read(const ltssm_cb_t *cb, const ltssm_port_t *lp, uint32_t *vals)
{
	for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++) {
		smn_reg_t reg = cb->lc_lc_state_reg(lp->lp_iohcno,
		    lp->lp_coreno, lp->lp_portno, i);

		if (rdsmn_reg(reg, lp->lp_sock, &vals[i]) != DCMD_OK)
			return (false);
	}
	return (true);
}

/*
 * Print the summary line for a port's live state.
 */
static void
ltssm_print_live_summary(const ltssm_cb_t *cb, const ltssm_port_t *lp)
{
	uint32_t vals[ZEN_PCIE_LC_STATE_NREGS];
	const char *nm = "<error>";

	if (ltssm_live_read(cb, lp, vals))
		nm = ltssm_state_name(cb, vals[0]);
	ltssm_print_port(lp);
	mdb_printf(" %s\n", nm);
}

/*
 * Print the detailed view of a port's live state, which is the recent
 * history held in its LC_STATE registers.
 */
static void
ltssm_print_live_detail(const ltssm_cb_t *cb, const ltssm_port_t *lp)
{
	uint32_t vals[ZEN_PCIE_LC_STATE_NREGS];

	ltssm_print_heading(lp);
	if (ltssm_live_read(cb, lp, vals)) {
		mdb_printf("    LIVE\n");
		ltssm_print_states(cb, vals);
	}
}
#endif	/* _KMDB */

/*
 * Print the summary line for a port that has at least one capture. This shows
 * its most recently captured state and whether link-up or link-down events
 * were captured.
 */
static void
ltssm_print_summary(const ltssm_cb_t *cb, const ltssm_port_t *lp,
    const ltssm_reg_t *lc0, int latest)
{
	static const char *const evnames[] = { "", "up", "down", "up down" };
	const ltssm_layout_t *l = &cb->lc_layout;
	uint_t ev = 0;

	ltssm_print_port(lp);
	mdb_printf(" %-22s", ltssm_state_name(cb, lc0->lr_val[latest]));

	/*
	 * Bit 0 of ev records a link-up capture and bit 1 a link-down one,
	 * indexing evnames.
	 */
	if (lc0->lr_ts[l->ll_link_up] != 0)
		ev |= 1;
	if (lc0->lr_ts[l->ll_link_down] != 0)
		ev |= 2;
	if (ev != 0)
		mdb_printf(" %s", evnames[ev]);
	mdb_printf("\n");
}

/*
 * Print the decoded history from each of a port's capture points.
 */
static void
ltssm_print_capture(const ltssm_cb_t *cb, const ltssm_reg_t *regs,
    const ltssm_reg_t *lc0)
{
	const ltssm_layout_t *l = &cb->lc_layout;

	for (uint_t st = 0; st < l->ll_nstages; st++) {
		uint32_t vals[ZEN_PCIE_LC_STATE_NREGS];
		hrtime_t ts = lc0->lr_ts[st];
		char tbuf[32];

		if (ts == 0)
			continue;
		mdb_nicetime(ts, tbuf, sizeof (tbuf));
		mdb_printf("    %s (%llu ns; %s)\n", ltssm_stage_name(l, st),
		    (u_longlong_t)ts, tbuf);
		for (uint_t i = 0; i < ZEN_PCIE_LC_STATE_NREGS; i++)
			vals[i] = regs[lc_state_regs[i].lsr_idx].lr_val[st];
		ltssm_print_states(cb, vals);
	}
}

/*
 * Print the detailed view of a port, which is the decoded history from each
 * of its capture points.
 */
static void
ltssm_print_detail(const ltssm_cb_t *cb, const ltssm_port_t *lp,
    const ltssm_reg_t *regs, const ltssm_reg_t *lc0)
{
	ltssm_print_heading(lp);
	if (lc0 != NULL)
		ltssm_print_capture(cb, regs, lc0);
}

static int
ltssm_port_cb(uintptr_t addr, const void *arg __unused, void *cbdata)
{
	const ltssm_cb_t *cb = cbdata;
	const ltssm_reg_t *regs, *lc0 = NULL;
	ltssm_port_t lp;
	int latest = -1;

	if (!ltssm_port_read(addr, &lp))
		return (WALK_NEXT);

#ifdef _KMDB
	/*
	 * The live state is shown in place of the captures, for every port.
	 */
	if (cb->lc_live) {
		if (cb->lc_pipe)
			mdb_printf("%lr\n", addr);
		else if (cb->lc_detail)
			ltssm_print_live_detail(cb, &lp);
		else
			ltssm_print_live_summary(cb, &lp);
		return (WALK_NEXT);
	}
#endif

	if ((regs = ltssm_capture_read(&lp, &cb->lc_layout)) != NULL) {
		lc0 = &regs[lc_state_regs[0].lsr_idx];
		latest = ltssm_capture_latest(lc0, cb->lc_layout.ll_nstages);
	}

	/*
	 * Ports for which nothing has been captured only appear in the
	 * detailed output.
	 */
	if (latest < 0 && !cb->lc_detail)
		return (WALK_NEXT);

	if (cb->lc_pipe)
		mdb_printf("%lr\n", addr);
	else if (cb->lc_detail)
		ltssm_print_detail(cb, &lp, regs, lc0);
	else
		ltssm_print_summary(cb, &lp, lc0, latest);

	return (WALK_NEXT);
}

void
ltssm_dcmd_help(void)
{
	mdb_printf(
	    "Decode PCIe LTSSM state for PCIe ports.\n"
	    "\n"
	    "With no address, prints a line per port giving its most recently\n"
	    "captured state. The EVENTS column flags 'up'/'down' for ports\n"
	    "with link-up/link-down captures. Given the address of a port,\n"
	    "prints the full decoded history for each of its capture points.\n"
	    "The address of a PCIe core, or any other fabric object above\n"
	    "the port level, may be given instead to show every port beneath\n"
	    "it.\n"
	    "\n"
	    "LTSSM state is captured at various boot stages and whenever a\n"
	    "link comes up or goes down. This command decodes those captures\n"
	    "and omits ports that have none from the summary. Within kmdb,\n"
	    "the -l option reads the current state of each port from the\n"
	    "hardware instead of decoding the captures. Every port is then\n"
	    "listed in the summary, and the detailed output shows the recent\n"
	    "state history held in the port's LC_STATE registers.\n"
	    "\n%<b>Options:%</b>\n"
	    "\t-l\tread the live LTSSM state from the hardware instead of\n"
	    "\t\tdecoding the captures (kmdb only).\n");
}

int
ltssm_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	ltssm_cb_t cb = { 0 };
	uint_t live = 0;

	if (mdb_getopts(argc, argv,
	    'l', MDB_OPT_SETBITS, 1, &live,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

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

	if (live != 0) {
#ifdef _KMDB
		cb.lc_live = true;
		cb.lc_lc_state_reg = ltssm_lc_state_reg_func(cb.lc_fam);
		if (cb.lc_lc_state_reg == NULL) {
			mdb_warn("live LTSSM state is not supported on "
			    "processor family %u (%s)\n", cb.lc_fam,
			    oxide_mdb_target_family_name());
			return (DCMD_ERR);
		}
#else
		mdb_warn("live LTSSM state can only be read from kmdb\n");
		return (DCMD_ERR);
#endif
	}

	if (flags & DCMD_ADDRSPEC) {
		cb.lc_detail = true;
	} else if (flags & DCMD_PIPE_OUT) {
		cb.lc_pipe = true;
	} else if (DCMD_HDRSPEC(flags)) {
		if (cb.lc_live) {
			mdb_printf("%<u>%?s %-8s %s%</u>\n",
			    "ADDR", "B/D/F", "STATE");
		} else {
			mdb_printf("%<u>%?s %-8s %-22s %s%</u>\n",
			    "ADDR", "B/D/F", "STATE", "EVENTS");
		}
	}

	/*
	 * The walk is global unless an address was given, in which case it
	 * covers just the port, or the ports beneath a fabric object.
	 */
	if (mdb_pwalk("pcie_port", ltssm_port_cb, &cb,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		mdb_warn("failed to walk PCIe ports");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}
