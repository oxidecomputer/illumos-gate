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
 * This part of the file contains the mdb support for dcmds:
 *	::fabric, ::ioms
 * and walkers for:
 *	soc, iodie, nbio, ioms
 *
 * The fabric tree is read from the target via CTF rather than by including
 * the kernel's fabric headers. This decouples the debugger from the
 * environment it was built in at the expense of more code and complexity,
 * particularly around enum and bitflag handling. Each kernel structure has a
 * "shadow" mdb_*_t type below containing only the members we use, populated by
 * mdb_ctf_vread(). The embedded sub-object arrays are accessed using offsets
 * and element sizes resolved from CTF and cached in fabric_layout.
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_target.h>
#include <mdb/mdb_ctf.h>

#include <sys/sysmacros.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * An oxio engine's oe_name is a free-form human-readable descriptor with no
 * fixed maximum length; this buffer is generously sized for display and
 * mdb_readstr() truncates anything longer.
 */
#define	FABRIC_OXIO_NAME_MAX	128

/*
 * Shadow structures for the fabric tree. The member names must match those of
 * the corresponding kernel types. Enums are not included here as the CTF
 * mapping does not handle that without us declaring shadow enums too, which
 * is just something else to keep in sync. Instead we calculate the offset for
 * the enums we want and read them directly.
 */
typedef struct {
	uint8_t		zf_nsocs;
} mdb_zen_fabric_t;

typedef struct {
	uint8_t		zs_num;
	uint8_t		zs_niodies;
} mdb_zen_soc_t;

typedef struct {
	uint8_t		zi_num;
	uint8_t		zi_nnbio;
	uint8_t		zi_nccds;
} mdb_zen_iodie_t;

typedef struct {
	uint8_t		zn_num;
	uint8_t		zn_nioms;
} mdb_zen_nbio_t;

typedef struct {
	uint8_t		zio_num;
	uint8_t		zio_iohcnum;
	uint8_t		zio_iohubnum;
	uint16_t	zio_pci_busno;
	uint8_t		zio_npcie_cores;
	uint8_t		zio_nnbifs;
	uintptr_t	zio_nbio;
} mdb_zen_ioms_t;

typedef struct {
	uint8_t		zpc_coreno;
	uint8_t		zpc_nports;
} mdb_zen_pcie_core_t;

typedef struct {
	uint8_t		zpp_portno;
	uint8_t		zpp_device;
	uint8_t		zpp_func;
	uint16_t	zpp_slotno;
	uintptr_t	zpp_oxio;
} mdb_zen_pcie_port_t;

typedef struct {
	uint8_t		zn_num;
	uint8_t		zn_nfuncs;
} mdb_zen_nbif_t;

typedef struct {
	uint8_t		znf_num;
	uint8_t		znf_dev;
	uint8_t		znf_func;
} mdb_zen_nbif_func_t;

typedef struct {
	uint8_t		zcd_logical_dieno;
	uint8_t		zcd_physical_dieno;
	uint8_t		zcd_nccxs;
} mdb_zen_ccd_t;

typedef struct {
	uint8_t		zcx_logical_cxno;
	uint8_t		zcx_physical_cxno;
	uint8_t		zcx_ncores;
} mdb_zen_ccx_t;

typedef struct {
	uint8_t		zc_logical_coreno;
	uint8_t		zc_physical_coreno;
	uint8_t		zc_nthreads;
} mdb_zen_core_t;

typedef struct {
	uint8_t		zt_threadno;
	uint32_t	zt_apicid;
} mdb_zen_thread_t;

typedef struct {
	uintptr_t	oe_name;
	uint8_t		oe_lane;
	uint8_t		oe_nlanes;
} mdb_fabric_oxio_t;

/*
 * The byte offset of an embedded array within its parent, and the size of one
 * of its elements. Element i then lives at parent_addr + fa_off + i * fa_sz.
 */
typedef struct {
	ulong_t		fa_off;
	size_t		fa_sz;
} fabric_arr_t;

/*
 * The fabric layout as described by the target's CTF, resolved once per
 * invocation.
 */
typedef struct {
	fabric_arr_t	fl_socs;
	fabric_arr_t	fl_iodies;
	fabric_arr_t	fl_nbio;
	fabric_arr_t	fl_ccds;
	fabric_arr_t	fl_ioms;
	fabric_arr_t	fl_cores;
	fabric_arr_t	fl_nbifs;
	fabric_arr_t	fl_ports;
	fabric_arr_t	fl_funcs;
	fabric_arr_t	fl_ccxs;
	fabric_arr_t	fl_ccx_cores;
	fabric_arr_t	fl_threads;
	mdb_ctf_id_t	fl_iohc_type;	/* enum zen_iohc_type_t */
	mdb_ctf_id_t	fl_nbif_type;	/* enum zen_nbif_func_type_t */
	mdb_ctf_id_t	fl_tile;	/* enum oxio_tile_t */
	uint32_t	fl_port_hidden;	/* ZEN_PCIE_PORT_F_BRIDGE_HIDDEN */
	uint32_t	fl_core_used;	/* ZEN_PCIE_CORE_F_USED */
	/*
	 * Offsets of enum-typed scalar members. mdb_ctf_vread() will not map a
	 * target enum onto an integer shadow member, and using the kernel enum
	 * types in the shadow would defeat the point of dropping the fabric
	 * headers, so these are read directly with mdb_vread() instead.
	 */
	ulong_t		fl_ioms_iohctype_off;
	ulong_t		fl_ioms_flags_off;
	ulong_t		fl_core_flags_off;
	ulong_t		fl_port_flags_off;
	ulong_t		fl_func_type_off;
	ulong_t		fl_func_flags_off;
	ulong_t		fl_oxio_tile_off;
} fabric_layout_t;

static fabric_layout_t fabric_layout;

/*
 * The captured flag enumerators are displayed with custom abbreviations that
 * cannot be derived from the enumerator name, so we keep a static table mapping
 * each enumerator to its label and resolve the values from CTF at runtime.
 */
typedef struct {
	const char	*ffd_label;
	const char	*ffd_enum;
} fabric_flag_def_t;

static const fabric_flag_def_t fabric_port_flag_defs[] = {
	{ "MAPPED",	"ZEN_PCIE_PORT_F_MAPPED" },
	{ "HIDDEN",	"ZEN_PCIE_PORT_F_BRIDGE_HIDDEN" },
	{ "HOTPLUG",	"ZEN_PCIE_PORT_F_HOTPLUG" }
};

static const fabric_flag_def_t fabric_core_flag_defs[] = {
	{ "USED",	"ZEN_PCIE_CORE_F_USED" },
	{ "HOTPLUG",	"ZEN_PCIE_CORE_F_HAS_HOTPLUG" }
};

static const fabric_flag_def_t fabric_nbif_flag_defs[] = {
	{ "EN",		"ZEN_NBIF_F_ENABLED" },
	{ "NOCFG",	"ZEN_NBIF_F_NO_CONFIG" },
	{ "FLR",	"ZEN_NBIF_F_FLR_EN" },
	{ "ACS",	"ZEN_NBIF_F_ACS_EN" },
	{ "AER",	"ZEN_NBIF_F_AER_EN" },
	{ "PMS",	"ZEN_NBIF_F_PMSTATUS_EN" },
	{ "CPLR",	"ZEN_NBIF_F_TPH_CPLR_EN" },
	{ "PANF",	"ZEN_NBIF_F_PANF_EN" }
};

static const fabric_flag_def_t fabric_ioms_flag_defs[] = {
	{ "FCH",	"ZEN_IOMS_F_HAS_FCH" },
	{ "BONUS",	"ZEN_IOMS_F_HAS_BONUS" },
	{ "NBIF",	"ZEN_IOMS_F_HAS_NBIF" }
};

static mdb_bitmask_t fabric_port_flags[ARRAY_SIZE(fabric_port_flag_defs) + 1];
static mdb_bitmask_t fabric_core_flags[ARRAY_SIZE(fabric_core_flag_defs) + 1];
static mdb_bitmask_t fabric_nbif_flags[ARRAY_SIZE(fabric_nbif_flag_defs) + 1];
static mdb_bitmask_t fabric_ioms_flags[ARRAY_SIZE(fabric_ioms_flag_defs) + 1];

typedef struct {
	bool		fd_verbose;
	bool		fd_ccd;
	bool		fd_nbif;
	bool		fd_printing;
	uintptr_t	fd_saddr;
	uint_t		fd_indent;
	uint16_t	fd_busno;
} fabric_data_t;

/*
 * Resolve the byte offset of an embedded array member and the size of its
 * element type.
 */
static bool
fabric_resolve(const char *type, const char *member, const char *elem,
    fabric_arr_t *out)
{
	int off;
	ssize_t sz;
	mdb_ctf_id_t id;

	if ((off = mdb_ctf_offsetof_by_name(type, member)) < 0) {
		mdb_warn("failed to find %s::%s\n", type, member);
		return (false);
	}
	if (mdb_ctf_lookup_by_name(elem, &id) != 0 ||
	    (sz = mdb_ctf_type_size(id)) < 0) {
		mdb_warn("failed to determine size of %s\n", elem);
		return (false);
	}
	out->fa_off = (ulong_t)off;
	out->fa_sz = (size_t)sz;
	return (true);
}

/*
 * Resolve the byte offset of a scalar member.
 */
static bool
fabric_resolve_off(const char *type, const char *member, ulong_t *out)
{
	int off;

	if ((off = mdb_ctf_offsetof_by_name(type, member)) < 0) {
		mdb_warn("failed to find %s::%s\n", type, member);
		return (false);
	}
	*out = (ulong_t)off;
	return (true);
}

/*
 * Read an enum-typed scalar member (a 4-byte int) directly from the target.
 */
static uint32_t
fabric_enum_read(uintptr_t addr, ulong_t off)
{
	uint32_t v = 0;

	(void) mdb_vread(&v, sizeof (v), addr + off);
	return (v);
}

typedef struct {
	const fabric_flag_def_t	*fb_defs;
	uint_t			fb_ndefs;
	mdb_bitmask_t		*fb_out;
} fabric_flag_build_t;

static int
fabric_flag_build_cb(const char *name, int value, void *arg)
{
	fabric_flag_build_t *b = arg;

	for (uint_t i = 0; i < b->fb_ndefs; i++) {
		if (strcmp(name, b->fb_defs[i].ffd_enum) == 0) {
			b->fb_out[i].bm_name = b->fb_defs[i].ffd_label;
			b->fb_out[i].bm_mask = (u_longlong_t)value;
			b->fb_out[i].bm_bits = (u_longlong_t)value;
			break;
		}
	}
	return (0);
}

/*
 * Build a NULL-terminated mdb_bitmask_t table by resolving each flag def's
 * enumerator value from the named CTF enum.
 */
static bool
fabric_build_flags(const char *enumtype, const fabric_flag_def_t *defs,
    uint_t ndefs, mdb_bitmask_t *out)
{
	mdb_ctf_id_t id;
	fabric_flag_build_t b = { defs, ndefs, out };

	if (mdb_ctf_lookup_by_name(enumtype, &id) != 0) {
		mdb_warn("failed to find enum %s\n", enumtype);
		return (false);
	}
	(void) mdb_ctf_enum_iter(id, fabric_flag_build_cb, &b);

	out[ndefs].bm_name = NULL;
	out[ndefs].bm_mask = 0;
	out[ndefs].bm_bits = 0;

	for (uint_t i = 0; i < ndefs; i++) {
		if (out[i].bm_name == NULL) {
			mdb_warn("failed to resolve flag %s in %s\n",
			    defs[i].ffd_enum, enumtype);
			return (false);
		}
	}
	return (true);
}

/*
 * Return the bit value of a previously-built flag table entry by label.
 */
static uint32_t
fabric_flag_bit(const mdb_bitmask_t *tbl, const char *label)
{
	for (; tbl->bm_name != NULL; tbl++) {
		if (strcmp(tbl->bm_name, label) == 0)
			return ((uint32_t)tbl->bm_bits);
	}
	return (0);
}

static bool
fabric_layout_init(void)
{
	static const struct {
		const char		*fft_enum;
		const fabric_flag_def_t	*fft_defs;
		uint_t			fft_ndefs;
		mdb_bitmask_t		*fft_out;
	} flag_tables[] = {
		{ "zen_pcie_port_flag_t", fabric_port_flag_defs,
		    ARRAY_SIZE(fabric_port_flag_defs), fabric_port_flags },
		{ "zen_pcie_core_flag_t", fabric_core_flag_defs,
		    ARRAY_SIZE(fabric_core_flag_defs), fabric_core_flags },
		{ "zen_nbif_func_flag_t", fabric_nbif_flag_defs,
		    ARRAY_SIZE(fabric_nbif_flag_defs), fabric_nbif_flags },
		{ "zen_ioms_flag_t", fabric_ioms_flag_defs,
		    ARRAY_SIZE(fabric_ioms_flag_defs), fabric_ioms_flags }
	};
	fabric_layout_t *l = &fabric_layout;

	if (!fabric_resolve("zen_fabric_t", "zf_socs", "zen_soc_t",
	    &l->fl_socs) ||
	    !fabric_resolve("zen_soc_t", "zs_iodies", "zen_iodie_t",
	    &l->fl_iodies) ||
	    !fabric_resolve("zen_iodie_t", "zi_nbio", "zen_nbio_t",
	    &l->fl_nbio) ||
	    !fabric_resolve("zen_iodie_t", "zi_ccds", "zen_ccd_t",
	    &l->fl_ccds) ||
	    !fabric_resolve("zen_nbio_t", "zn_ioms", "zen_ioms_t",
	    &l->fl_ioms) ||
	    !fabric_resolve("zen_ioms_t", "zio_pcie_cores", "zen_pcie_core_t",
	    &l->fl_cores) ||
	    !fabric_resolve("zen_ioms_t", "zio_nbifs", "zen_nbif_t",
	    &l->fl_nbifs) ||
	    !fabric_resolve("zen_pcie_core_t", "zpc_ports", "zen_pcie_port_t",
	    &l->fl_ports) ||
	    !fabric_resolve("zen_nbif_t", "zn_funcs", "zen_nbif_func_t",
	    &l->fl_funcs) ||
	    !fabric_resolve("zen_ccd_t", "zcd_ccxs", "zen_ccx_t",
	    &l->fl_ccxs) ||
	    !fabric_resolve("zen_ccx_t", "zcx_cores", "zen_core_t",
	    &l->fl_ccx_cores) ||
	    !fabric_resolve("zen_core_t", "zc_threads", "zen_thread_t",
	    &l->fl_threads)) {
		return (false);
	}

	if (!fabric_resolve_off("zen_ioms_t", "zio_iohctype",
	    &l->fl_ioms_iohctype_off) ||
	    !fabric_resolve_off("zen_ioms_t", "zio_flags",
	    &l->fl_ioms_flags_off) ||
	    !fabric_resolve_off("zen_pcie_core_t", "zpc_flags",
	    &l->fl_core_flags_off) ||
	    !fabric_resolve_off("zen_pcie_port_t", "zpp_flags",
	    &l->fl_port_flags_off) ||
	    !fabric_resolve_off("zen_nbif_func_t", "znf_type",
	    &l->fl_func_type_off) ||
	    !fabric_resolve_off("zen_nbif_func_t", "znf_flags",
	    &l->fl_func_flags_off) ||
	    !fabric_resolve_off("oxio_engine_t", "oe_tile",
	    &l->fl_oxio_tile_off)) {
		return (false);
	}

	if (mdb_ctf_lookup_by_name("zen_iohc_type_t", &l->fl_iohc_type) != 0 ||
	    mdb_ctf_lookup_by_name("zen_nbif_func_type_t",
	    &l->fl_nbif_type) != 0 ||
	    mdb_ctf_lookup_by_name("oxio_tile_t", &l->fl_tile) != 0) {
		mdb_warn("failed to resolve fabric enum types\n");
		return (false);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(flag_tables); i++) {
		if (!fabric_build_flags(flag_tables[i].fft_enum,
		    flag_tables[i].fft_defs, flag_tables[i].fft_ndefs,
		    flag_tables[i].fft_out)) {
			return (false);
		}
	}

	l->fl_port_hidden = fabric_flag_bit(fabric_port_flags, "HIDDEN");
	l->fl_core_used = fabric_flag_bit(fabric_core_flags, "USED");
	if (l->fl_port_hidden == 0 || l->fl_core_used == 0) {
		mdb_warn("failed to resolve required PCIe flag bits\n");
		return (false);
	}

	return (true);
}

static uintptr_t
fabric_elem(uintptr_t base, const fabric_arr_t *arr, uint_t i)
{
	return (base + arr->fa_off + (uintptr_t)i * arr->fa_sz);
}

static const char *
fabric_enum_short(mdb_ctf_id_t id, uint_t val, const char *prefix, size_t plen)
{
	const char *nm = mdb_ctf_enum_name(id, (int)val);

	if (nm == NULL)
		return ("??");
	if (strncmp(nm, prefix, plen) == 0)
		return (nm + plen);
	return (nm);
}

static bool
fabric_iohc_large(uint32_t type)
{
	const char *nm = mdb_ctf_enum_name(fabric_layout.fl_iohc_type,
	    (int)type);

	return (nm != NULL && strcmp(nm, "ZEN_IOHCT_LARGE") == 0);
}

static void
fabric_print_port(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_pcie_port_t port;
	mdb_fabric_oxio_t oxio;
	uint32_t flags;

	if (mdb_ctf_vread(&port, "zen_pcie_port_t", "mdb_zen_pcie_port_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	flags = fabric_enum_read(addr, fabric_layout.fl_port_flags_off);
	if (!cbd->fd_verbose && (flags & fabric_layout.fl_port_hidden))
		return;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p PORT %r [%r/%r/%r] slot %r",
		    cbd->fd_indent * 2, "", addr, port.zpp_portno,
		    cbd->fd_busno, port.zpp_device, port.zpp_func,
		    port.zpp_slotno);
		if (port.zpp_oxio != 0 && mdb_ctf_vread(&oxio, "oxio_engine_t",
		    "mdb_fabric_oxio_t", port.zpp_oxio,
		    MDB_CTF_VREAD_QUIET) == 0) {
			char descr[FABRIC_OXIO_NAME_MAX];
			uint32_t tile = fabric_enum_read(port.zpp_oxio,
			    fabric_layout.fl_oxio_tile_off);

			if (mdb_readstr(descr, sizeof (descr),
			    oxio.oe_name) <= 0) {
				(void) strcpy(descr, "??");
			}

			mdb_printf(" [%s] %s/%rx%r", descr,
			    fabric_enum_short(fabric_layout.fl_tile, tile,
			    "OXIO_TILE_", sizeof ("OXIO_TILE_") - 1),
			    oxio.oe_lane, oxio.oe_nlanes);
		}
		if (flags != 0 && cbd->fd_verbose)
			mdb_printf(" <%b>", flags, fabric_port_flags);
		mdb_printf("\n");
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_core(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_pcie_core_t core;
	uint32_t flags;

	if (mdb_ctf_vread(&core, "zen_pcie_core_t", "mdb_zen_pcie_core_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	flags = fabric_enum_read(addr, fabric_layout.fl_core_flags_off);
	if (!cbd->fd_verbose && !(flags & fabric_layout.fl_core_used))
		return;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CORE %r",
		    cbd->fd_indent * 2, "", addr, core.zpc_coreno);
		if (cbd->fd_verbose && flags != 0)
			mdb_printf(" <%b>", flags, fabric_core_flags);
		mdb_printf("\n");
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < core.zpc_nports; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_ports, i);
		fabric_print_port(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_nbif_func(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_nbif_func_t func;
	uint32_t type, flags;

	if (mdb_ctf_vread(&func, "zen_nbif_func_t", "mdb_zen_nbif_func_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	type = fabric_enum_read(addr, fabric_layout.fl_func_type_off);
	flags = fabric_enum_read(addr, fabric_layout.fl_func_flags_off);
	if (!cbd->fd_verbose) {
		const char *tn = mdb_ctf_enum_name(fabric_layout.fl_nbif_type,
		    (int)type);

		if (tn != NULL && strcmp(tn, "ZEN_NBIF_T_ABSENT") == 0)
			return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p FUNC %r [%r/%r] %s",
		    cbd->fd_indent * 2, "", addr, func.znf_num,
		    func.znf_dev, func.znf_func,
		    fabric_enum_short(fabric_layout.fl_nbif_type, type,
		    "ZEN_NBIF_T_", sizeof ("ZEN_NBIF_T_") - 1));
		if (cbd->fd_verbose && flags != 0)
			mdb_printf(" <%b>", flags, fabric_nbif_flags);
		mdb_printf("\n");
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_nbif(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_nbif_t nbif;

	if (mdb_ctf_vread(&nbif, "zen_nbif_t", "mdb_zen_nbif_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (!cbd->fd_verbose && nbif.zn_nfuncs == 0)
		return;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p NBIF %r\n",
		    cbd->fd_indent * 2, "", addr, nbif.zn_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < nbif.zn_nfuncs; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_funcs, i);
		fabric_print_nbif_func(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_ioms(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_ioms_t ioms;
	uint32_t iohctype, flags;

	if (mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_zen_ioms_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	iohctype = fabric_enum_read(addr, fabric_layout.fl_ioms_iohctype_off);
	flags = fabric_enum_read(addr, fabric_layout.fl_ioms_flags_off);
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p IOMS %r / IOHC %r (%s)",
		    cbd->fd_indent * 2, "", addr,
		    ioms.zio_num, ioms.zio_iohcnum,
		    fabric_iohc_large(iohctype) ? "Large" : "Small");
		if (flags != 0)
			mdb_printf(" <%b>", flags, fabric_ioms_flags);
		mdb_printf("\n");
		cbd->fd_indent++;
	}
	cbd->fd_busno = ioms.zio_pci_busno;
	for (uint_t i = 0; i < ioms.zio_npcie_cores; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_cores, i);
		fabric_print_core(child, cbd);
	}
	if (cbd->fd_nbif) {
		for (uint_t i = 0; i < ioms.zio_nnbifs; i++) {
			uintptr_t child = fabric_elem(addr,
			    &fabric_layout.fl_nbifs, i);
			fabric_print_nbif(child, cbd);
		}
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_nbio(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_nbio_t nbio;

	if (mdb_ctf_vread(&nbio, "zen_nbio_t", "mdb_zen_nbio_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p NBIO %r\n", cbd->fd_indent * 2, "",
		    addr, nbio.zn_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < nbio.zn_nioms; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_ioms, i);
		fabric_print_ioms(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_cpu_thread(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_thread_t thread;

	if (mdb_ctf_vread(&thread, "zen_thread_t", "mdb_zen_thread_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p thread %r (APIC %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    thread.zt_threadno, thread.zt_apicid);
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_cpu_core(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_core_t core;

	if (mdb_ctf_vread(&core, "zen_core_t", "mdb_zen_core_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CORE %r (phys %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    core.zc_logical_coreno, core.zc_physical_coreno);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < core.zc_nthreads; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_threads,
		    i);
		fabric_print_cpu_thread(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_ccx(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_ccx_t ccx;

	if (mdb_ctf_vread(&ccx, "zen_ccx_t", "mdb_zen_ccx_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CCX %r (phys %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    ccx.zcx_logical_cxno, ccx.zcx_physical_cxno);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < ccx.zcx_ncores; i++) {
		uintptr_t child = fabric_elem(addr,
		    &fabric_layout.fl_ccx_cores, i);
		fabric_print_cpu_core(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_ccd(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_ccd_t ccd;

	if (mdb_ctf_vread(&ccd, "zen_ccd_t", "mdb_zen_ccd_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CCD %r (phys %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    ccd.zcd_logical_dieno, ccd.zcd_physical_dieno);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < ccd.zcd_nccxs; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_ccxs, i);
		fabric_print_ccx(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_iodie(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_iodie_t iodie;

	if (mdb_ctf_vread(&iodie, "zen_iodie_t", "mdb_zen_iodie_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p IODIE %r\n", cbd->fd_indent * 2, "",
		    addr, iodie.zi_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < iodie.zi_nnbio; i++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_nbio, i);
		fabric_print_nbio(child, cbd);
	}
	if (cbd->fd_ccd) {
		for (uint_t i = 0; i < iodie.zi_nccds; i++) {
			uintptr_t child = fabric_elem(addr,
			    &fabric_layout.fl_ccds, i);
			fabric_print_ccd(child, cbd);
		}
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_soc(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_soc_t soc;

	if (mdb_ctf_vread(&soc, "zen_soc_t", "mdb_zen_soc_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p SOC %r\n", cbd->fd_indent * 2, "",
		    addr, soc.zs_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < soc.zs_niodies; i++) {
		uintptr_t child = fabric_elem(addr,
		    &fabric_layout.fl_iodies, i);
		fabric_print_iodie(child, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

void
fabric_dcmd_help(void)
{
	mdb_printf(
	    "Prints a summary of the zen fabric tree.\n"
	    "\n%<b>Options:%</b>\n"
	    "\t-c\tinclude CCDs, CCXs, Cores and Threads.\n"
	    "\t-n\tinclude nBIFs and nBIF functions.\n"
	    "\t-v\tinclude unused items and always display flags.\n");
}

int
fabric_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	GElf_Sym sym;
	mdb_zen_fabric_t fabric;
	fabric_data_t cbd = { 0 };

	cbd.fd_printing = true;
	if (flags & DCMD_ADDRSPEC) {
		cbd.fd_saddr = addr;
		cbd.fd_printing = false;
	}

	if (mdb_getopts(argc, argv,
	    'c', MDB_OPT_SETBITS, true, &cbd.fd_ccd,
	    'n', MDB_OPT_SETBITS, true, &cbd.fd_nbif,
	    'v', MDB_OPT_SETBITS, true, &cbd.fd_verbose,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_layout_init()) {
		mdb_warn("failed to resolve fabric type layout from CTF\n");
		return (DCMD_ERR);
	}

	if (mdb_lookup_by_name("zen_fabric", &sym) == -1) {
		mdb_warn("failed to find 'zen_fabric'");
		return (DCMD_ERR);
	}
	addr = sym.st_value;

	if (mdb_ctf_vread(&fabric, "zen_fabric_t", "mdb_zen_fabric_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("can't read zen_fabric structure at %p", addr);
		return (DCMD_ERR);
	}

	for (uint_t s = 0; s < fabric.zf_nsocs; s++) {
		uintptr_t child = fabric_elem(addr, &fabric_layout.fl_socs, s);
		fabric_print_soc(child, &cbd);
	}

	return (DCMD_OK);
}

typedef struct {
	uint64_t	fid_num;
	uint64_t	fid_iohcnum;
	uint64_t	fid_iohubnum;
	uint64_t	fid_nbionum;
	uint64_t	fid_pcibus;
	uint_t		fid_flags;
} fabric_ioms_data_t;

static int
i_ioms(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_ioms_data_t *data = cb_data;
	mdb_zen_ioms_t ioms;
	mdb_zen_nbio_t nbio;
	uint32_t iohctype, flags;

	if (mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_zen_ioms_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0) {
		return (WALK_NEXT);
	}
	iohctype = fabric_enum_read(addr, fabric_layout.fl_ioms_iohctype_off);
	flags = fabric_enum_read(addr, fabric_layout.fl_ioms_flags_off);

	if (mdb_ctf_vread(&nbio, "zen_nbio_t", "mdb_zen_nbio_t",
	    ioms.zio_nbio, MDB_CTF_VREAD_QUIET) != 0) {
		nbio.zn_num = UINT8_MAX;
	}

	if (data->fid_num != UINT64_MAX && data->fid_num != ioms.zio_num)
		return (WALK_NEXT);
	if (data->fid_iohcnum != UINT64_MAX &&
	    data->fid_iohcnum != ioms.zio_iohcnum) {
		return (WALK_NEXT);
	}
	if (data->fid_iohubnum != UINT64_MAX &&
	    data->fid_iohubnum != ioms.zio_iohubnum) {
		return (WALK_NEXT);
	}
	if (data->fid_nbionum != UINT64_MAX &&
	    data->fid_nbionum != nbio.zn_num) {
		return (WALK_NEXT);
	}
	if (data->fid_pcibus != UINT64_MAX &&
	    data->fid_pcibus != ioms.zio_pci_busno) {
		return (WALK_NEXT);
	}

	if (data->fid_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %5r %4r %5r %b%s%s\n",
	    addr, ioms.zio_num, ioms.zio_iohcnum, nbio.zn_num,
	    ioms.zio_iohubnum, ioms.zio_pci_busno, ioms.zio_npcie_cores,
	    flags, fabric_ioms_flags,
	    flags != 0 ? "," : "",
	    fabric_iohc_large(iohctype) ? "LARGE" : "");

	return (WALK_NEXT);
}

void
fabric_ioms_dcmd_help(void)
{
	mdb_printf(
	    "Prints a summary of the IOMS in the zen fabric.\n"
	    "\n%<b>Options:%</b>\n"
	    "\t-h num\tonly show the IOMS with the specified IOHUB number.\n"
	    "\t-n num\tonly show the IOMS with the specified number.\n"
	    "\t-N num\tonly show IOMS within the specified NBIO.\n"
	    "\t-i num\tonly show the IOMS with the specified IOHC number.\n"
	    "\t-b bus\tonly show the IOMS with the specified PCI bus number.\n"
	    "\n%<b>Notes:%</b>\n"
	    "\tThe output of this command can be piped into %<b>::fabric%</b>\n"
	    "\tto summarise objects beneath it.\n");
}

int
fabric_ioms_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	fabric_ioms_data_t data = {
		.fid_flags = flags,
		.fid_num = UINT64_MAX,
		.fid_iohcnum = UINT64_MAX,
		.fid_iohubnum = UINT64_MAX,
		.fid_nbionum = UINT64_MAX,
		.fid_pcibus = UINT64_MAX
	};

	if (flags & DCMD_ADDRSPEC)
		return (DCMD_USAGE);

	if (mdb_getopts(argc, argv,
	    'h', MDB_OPT_UINT64, &data.fid_iohubnum,
	    'n', MDB_OPT_UINT64, &data.fid_num,
	    'N', MDB_OPT_UINT64, &data.fid_nbionum,
	    'i', MDB_OPT_UINT64, &data.fid_iohcnum,
	    'b', MDB_OPT_UINT64, &data.fid_pcibus,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_layout_init()) {
		mdb_warn("failed to resolve fabric type layout from CTF\n");
		return (DCMD_ERR);
	}

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %4s %4s %5s %4s %5s %s%</u>\n",
		    "ADDR", "NUM", "IOHC", "NBIO", "IOHUB", "BUS", "CORES",
		    "FLAGS");
	}

	if (mdb_walk("ioms", i_ioms, &data) == -1)
		return (DCMD_ERR);

	return (DCMD_OK);
}

/*
 * The walkers yield the target address of each node at a given level. Consumers
 * read the node's contents from that address via CTF. We collect the addresses
 * up front by descending the tree once, then step through them.
 */
typedef enum {
	FABRIC_L_SOC,
	FABRIC_L_IODIE,
	FABRIC_L_NBIO,
	FABRIC_L_IOMS
} fabric_level_t;

typedef struct {
	uintptr_t	*fc_addrs;
	uint_t		fc_n;
	uint_t		fc_cap;
	uint_t		fc_idx;
} fabric_collect_t;

static bool
fabric_collect_push(fabric_collect_t *c, uintptr_t addr)
{
	if (c->fc_n == c->fc_cap) {
		uint_t ncap = (c->fc_cap == 0) ? 16 : c->fc_cap * 2;
		uintptr_t *na;

		na = mdb_alloc(ncap * sizeof (uintptr_t), UM_NOSLEEP | UM_GC);
		if (na == NULL) {
			mdb_warn("failed to allocate memory for fabric walker");
			return (false);
		}
		if (c->fc_addrs != NULL) {
			(void) memcpy(na, c->fc_addrs,
			    c->fc_n * sizeof (uintptr_t));
			mdb_free(c->fc_addrs, c->fc_cap * sizeof (uintptr_t));
		}
		c->fc_addrs = na;
		c->fc_cap = ncap;
	}
	c->fc_addrs[c->fc_n++] = addr;
	return (true);
}

static bool
fabric_collect(fabric_level_t level, fabric_collect_t *c)
{
	GElf_Sym sym;
	uintptr_t fabaddr;
	mdb_zen_fabric_t fab;

	if (mdb_lookup_by_name("zen_fabric", &sym) == -1) {
		mdb_warn("failed to find 'zen_fabric'");
		return (false);
	}
	fabaddr = sym.st_value;

	if (mdb_ctf_vread(&fab, "zen_fabric_t", "mdb_zen_fabric_t",
	    fabaddr, MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("can't read zen_fabric structure at %p", fabaddr);
		return (false);
	}

	for (uint_t s = 0; s < fab.zf_nsocs; s++) {
		uintptr_t saddr = fabric_elem(fabaddr, &fabric_layout.fl_socs,
		    s);
		mdb_zen_soc_t soc;

		if (level == FABRIC_L_SOC) {
			if (!fabric_collect_push(c, saddr))
				return (false);
			continue;
		}
		if (mdb_ctf_vread(&soc, "zen_soc_t", "mdb_zen_soc_t", saddr,
		    MDB_CTF_VREAD_QUIET) != 0) {
			continue;
		}

		for (uint_t d = 0; d < soc.zs_niodies; d++) {
			uintptr_t iaddr = fabric_elem(saddr,
			    &fabric_layout.fl_iodies, d);
			mdb_zen_iodie_t iodie;

			if (level == FABRIC_L_IODIE) {
				if (!fabric_collect_push(c, iaddr))
					return (false);
				continue;
			}
			if (mdb_ctf_vread(&iodie, "zen_iodie_t",
			    "mdb_zen_iodie_t", iaddr,
			    MDB_CTF_VREAD_QUIET) != 0) {
				continue;
			}

			for (uint_t n = 0; n < iodie.zi_nnbio; n++) {
				uintptr_t naddr = fabric_elem(iaddr,
				    &fabric_layout.fl_nbio, n);
				mdb_zen_nbio_t nbio;

				if (level == FABRIC_L_NBIO) {
					if (!fabric_collect_push(c, naddr))
						return (false);
					continue;
				}
				if (mdb_ctf_vread(&nbio, "zen_nbio_t",
				    "mdb_zen_nbio_t", naddr,
				    MDB_CTF_VREAD_QUIET) != 0) {
					continue;
				}

				for (uint_t m = 0; m < nbio.zn_nioms; m++) {
					uintptr_t maddr = fabric_elem(naddr,
					    &fabric_layout.fl_ioms, m);

					if (!fabric_collect_push(c, maddr))
						return (false);
				}
			}
		}
	}

	return (true);
}

static int
fabric_walk_init_common(mdb_walk_state_t *wsp, fabric_level_t level)
{
	fabric_collect_t *c;

	if (wsp->walk_addr != 0) {
		mdb_warn("zen walkers only support global walks\n");
		return (WALK_ERR);
	}

	if (!fabric_layout_init()) {
		mdb_warn("failed to resolve fabric type layout from CTF\n");
		return (WALK_ERR);
	}

	c = mdb_zalloc(sizeof (*c), UM_NOSLEEP | UM_GC);
	if (c == NULL) {
		mdb_warn("failed to allocate memory for fabric walker");
		return (WALK_ERR);
	}

	if (!fabric_collect(level, c))
		return (WALK_ERR);

	wsp->walk_data = c;
	return (WALK_NEXT);
}

int
fabric_walk_soc_init(mdb_walk_state_t *wsp)
{
	return (fabric_walk_init_common(wsp, FABRIC_L_SOC));
}

int
fabric_walk_iodie_init(mdb_walk_state_t *wsp)
{
	return (fabric_walk_init_common(wsp, FABRIC_L_IODIE));
}

int
fabric_walk_nbio_init(mdb_walk_state_t *wsp)
{
	return (fabric_walk_init_common(wsp, FABRIC_L_NBIO));
}

int
fabric_walk_ioms_init(mdb_walk_state_t *wsp)
{
	return (fabric_walk_init_common(wsp, FABRIC_L_IOMS));
}

int
fabric_walk_step(mdb_walk_state_t *wsp)
{
	fabric_collect_t *c = wsp->walk_data;
	uintptr_t addr;

	if (c->fc_idx >= c->fc_n)
		return (WALK_DONE);

	addr = c->fc_addrs[c->fc_idx++];
	return (wsp->walk_callback(addr, NULL, wsp->walk_cbdata));
}
