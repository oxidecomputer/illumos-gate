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
 *	::fabric, ::ioms, ::pcie_core, ::pcie_port, ::nbif, ::nbif_func, ::ccd,
 *	::ccx, ::zen_core, ::zen_thread
 * and walkers for:
 *	soc, iodie, nbio, ioms, pcie_core, pcie_port, nbif, nbif_func, ccd, ccx,
 *	zen_core, zen_thread
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

#include "fabric.h"

/*
 * An oxio engine's oe_name is a free-form human-readable descriptor with no
 * fixed maximum length. This buffer is generously sized for display and
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
	uint16_t	zpc_dxio_lane_start;
	uint16_t	zpc_dxio_lane_end;
	uintptr_t	zpc_ioms;
} mdb_zen_pcie_core_t;

typedef struct {
	uint8_t		zpp_portno;
	uint8_t		zpp_device;
	uint8_t		zpp_func;
	uint16_t	zpp_slotno;
	uintptr_t	zpp_oxio;
	uintptr_t	zpp_core;
} mdb_zen_pcie_port_t;

typedef struct {
	uint8_t		zn_num;
	uint8_t		zn_nfuncs;
	uintptr_t	zn_ioms;
} mdb_zen_nbif_t;

typedef struct {
	uint8_t		znf_num;
	uint8_t		znf_dev;
	uint8_t		znf_func;
	uintptr_t	znf_nbif;
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
	uintptr_t	zcx_ccd;
} mdb_zen_ccx_t;

typedef struct {
	uint8_t		zc_logical_coreno;
	uint8_t		zc_physical_coreno;
	uint8_t		zc_nthreads;
	uintptr_t	zc_ccx;
} mdb_zen_core_t;

typedef struct {
	uint8_t		zt_threadno;
	uint32_t	zt_apicid;
	uintptr_t	zt_core;
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
	{ "HOTPLUG",	"ZEN_PCIE_PORT_F_HOTPLUG" },
	{ "TRAINED",	"ZEN_PCIE_PORT_F_TRAINED" }
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

/*
 * Describe the oxio engine behind a PCIe port as its tile and lane range, and
 * its name.
 */
static bool
fabric_oxio_describe(uintptr_t addr, char *lanes, size_t lanelen, char *name,
    size_t namelen)
{
	mdb_fabric_oxio_t oxio;
	uint32_t tile;

	if (addr == 0 || mdb_ctf_vread(&oxio, "oxio_engine_t",
	    "mdb_fabric_oxio_t", addr, MDB_CTF_VREAD_QUIET) != 0) {
		return (false);
	}
	tile = fabric_enum_read(addr, fabric_layout.fl_oxio_tile_off);
	(void) mdb_snprintf(lanes, lanelen, "%s/%rx%r",
	    fabric_enum_short(fabric_layout.fl_tile, tile, "OXIO_TILE_",
	    sizeof ("OXIO_TILE_") - 1), oxio.oe_lane, oxio.oe_nlanes);
	if (mdb_readstr(name, namelen, oxio.oe_name) <= 0)
		(void) strcpy(name, "??");
	return (true);
}

static void
fabric_print_port(uintptr_t addr, fabric_data_t *cbd)
{
	mdb_zen_pcie_port_t port;
	char lanes[16], name[FABRIC_OXIO_NAME_MAX];
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
		if (fabric_oxio_describe(port.zpp_oxio, lanes, sizeof (lanes),
		    name, sizeof (name))) {
			mdb_printf(" [%s] %s", name, lanes);
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
	uint_t ccd = 0, nbif = 0, verbose = 0;

	cbd.fd_printing = true;
	if (flags & DCMD_ADDRSPEC) {
		cbd.fd_saddr = addr;
		cbd.fd_printing = false;
	}

	if (mdb_getopts(argc, argv,
	    'c', MDB_OPT_SETBITS, 1, &ccd,
	    'n', MDB_OPT_SETBITS, 1, &nbif,
	    'v', MDB_OPT_SETBITS, 1, &verbose,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}
	cbd.fd_ccd = (ccd != 0);
	cbd.fd_nbif = (nbif != 0);
	cbd.fd_verbose = (verbose != 0);

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

/*
 * Summary dcmds. Each prints the nodes at one level of the fabric tree as a
 * table, subject to the filters given as options. Given the address of a node
 * at or above that level, only the nodes beneath it are shown. When the output
 * is piped, just the addresses are emitted so that they can be fed to another
 * dcmd.
 */

/*
 * A numeric filter option is unset while it holds UINT64_MAX.
 */
static bool
fabric_match(uint64_t want, uint64_t have)
{
	return (want == UINT64_MAX || want == have);
}

/*
 * Flag filters. The -f option names flags that must all be set and -x names
 * flags that must all be clear, each as a comma-separated list of the labels
 * shown in the FLAGS column.
 */
typedef struct {
	uint32_t	ff_req;
	uint32_t	ff_excl;
} fabric_flag_filter_t;

static bool
fabric_flag_parse(const char *list, const mdb_bitmask_t *tbl, uint32_t *maskp)
{
	const char *p = list;

	for (;;) {
		const char *end = strchr(p, ',');
		size_t len = (end == NULL) ? strlen(p) : (size_t)(end - p);
		const mdb_bitmask_t *bm;
		char name[32];

		if (len == 0 || len >= sizeof (name)) {
			mdb_warn("invalid flag list '%s'\n", list);
			return (false);
		}
		(void) strncpy(name, p, len);
		name[len] = '\0';

		for (bm = tbl; bm->bm_name != NULL; bm++) {
			if (strcasecmp(bm->bm_name, name) == 0) {
				*maskp |= (uint32_t)bm->bm_bits;
				break;
			}
		}
		if (bm->bm_name == NULL) {
			mdb_warn("unknown flag '%s'\n", name);
			return (false);
		}
		if (end == NULL)
			return (true);
		p = end + 1;
	}
}

static bool
fabric_flag_filter_init(fabric_flag_filter_t *ff, const char *req,
    const char *excl, const mdb_bitmask_t *tbl)
{
	ff->ff_req = 0;
	ff->ff_excl = 0;
	if (req != NULL && !fabric_flag_parse(req, tbl, &ff->ff_req))
		return (false);
	if (excl != NULL && !fabric_flag_parse(excl, tbl, &ff->ff_excl))
		return (false);
	return (true);
}

static bool
fabric_flag_match(const fabric_flag_filter_t *ff, uint32_t flags)
{
	return ((flags & ff->ff_req) == ff->ff_req &&
	    (flags & ff->ff_excl) == 0);
}

/*
 * Format a flag set for a table column, with "-" standing in for none.
 */
static const char *
fabric_flag_str(char *buf, size_t len, uint32_t flags,
    const mdb_bitmask_t *tbl)
{
	if (flags == 0)
		return ("-");
	(void) mdb_snprintf(buf, len, "%b", flags, tbl);
	return (buf);
}

static bool
fabric_table_init(void)
{
	if (!fabric_layout_init()) {
		mdb_warn("failed to resolve fabric type layout from CTF\n");
		return (false);
	}
	return (true);
}

/*
 * The common part of each summary dcmd's help text. The flag labels come from
 * the static definitions rather than the tables built from CTF, as the latter
 * are only populated once a dcmd has run.
 */
static void
fabric_table_help(const char *what, const char *opts,
    const fabric_flag_def_t *defs, uint_t ndefs)
{
	mdb_printf(
	    "Prints a summary of the %s in the zen fabric.\n"
	    "\n"
	    "Given the address of a fabric object above this level, only the\n"
	    "%s beneath it are shown. When the output is piped, only the\n"
	    "addresses are emitted so that they can be fed to another dcmd.\n"
	    "\n%<b>Options:%</b>\n%s", what, what, opts);
	if (defs == NULL)
		return;
	mdb_printf(
	    "\t-f flags\tonly show entries with all of the named flags set.\n"
	    "\t-x flags\tonly show entries with none of the named flags set.\n"
	    "\t\tFlags are given as a comma-separated list from:\n"
	    "\t\t\t%s", defs[0].ffd_label);
	for (uint_t i = 1; i < ndefs; i++)
		mdb_printf(" %s", defs[i].ffd_label);
	mdb_printf("\n");
}

typedef struct {
	uint_t			fid_flags;
	uint64_t		fid_num;
	uint64_t		fid_iohcnum;
	uint64_t		fid_iohubnum;
	uint64_t		fid_nbionum;
	uint64_t		fid_pcibus;
	fabric_flag_filter_t	fid_ff;
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

	if (!fabric_match(data->fid_num, ioms.zio_num) ||
	    !fabric_match(data->fid_iohcnum, ioms.zio_iohcnum) ||
	    !fabric_match(data->fid_iohubnum, ioms.zio_iohubnum) ||
	    !fabric_match(data->fid_nbionum, nbio.zn_num) ||
	    !fabric_match(data->fid_pcibus, ioms.zio_pci_busno) ||
	    !fabric_flag_match(&data->fid_ff, flags)) {
		return (WALK_NEXT);
	}

	if (data->fid_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %5r %4r %6r %b%s%s\n",
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
	fabric_table_help("IOMS",
	    "\t-h num\tonly show the IOMS with the specified IOHUB number.\n"
	    "\t-n num\tonly show the IOMS with the specified number.\n"
	    "\t-N num\tonly show IOMS within the specified NBIO.\n"
	    "\t-i num\tonly show the IOMS with the specified IOHC number.\n"
	    "\t-b bus\tonly show the IOMS with the specified PCI bus number.\n",
	    fabric_ioms_flag_defs, ARRAY_SIZE(fabric_ioms_flag_defs));
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
	const char *req = NULL, *excl = NULL;

	if (mdb_getopts(argc, argv,
	    'h', MDB_OPT_UINT64, &data.fid_iohubnum,
	    'n', MDB_OPT_UINT64, &data.fid_num,
	    'N', MDB_OPT_UINT64, &data.fid_nbionum,
	    'i', MDB_OPT_UINT64, &data.fid_iohcnum,
	    'b', MDB_OPT_UINT64, &data.fid_pcibus,
	    'f', MDB_OPT_STR, &req,
	    'x', MDB_OPT_STR, &excl,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_table_init() ||
	    !fabric_flag_filter_init(&data.fid_ff, req, excl,
	    fabric_ioms_flags)) {
		return (DCMD_ERR);
	}

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %4s %4s %5s %4s %6s %s%</u>\n",
		    "ADDR", "NUM", "IOHC", "NBIO", "IOHUB", "BUS", "NCORES",
		    "FLAGS");
	}

	if (mdb_pwalk("ioms", i_ioms, &data,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

typedef struct {
	uint_t			fpc_flags;
	uint64_t		fpc_num;
	uint64_t		fpc_iohcnum;
	uint64_t		fpc_pcibus;
	fabric_flag_filter_t	fpc_ff;
} fabric_pcie_core_data_t;

static int
i_pcie_core(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_pcie_core_data_t *data = cb_data;
	mdb_zen_pcie_core_t core;
	mdb_zen_ioms_t ioms;
	char lanes[16], fstr[64];
	uint32_t flags;

	if (mdb_ctf_vread(&core, "zen_pcie_core_t", "mdb_zen_pcie_core_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_zen_ioms_t", core.zpc_ioms,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read PCIe core at %p\n", addr);
		return (WALK_NEXT);
	}
	flags = fabric_enum_read(addr, fabric_layout.fl_core_flags_off);

	if (!fabric_match(data->fpc_num, core.zpc_coreno) ||
	    !fabric_match(data->fpc_iohcnum, ioms.zio_iohcnum) ||
	    !fabric_match(data->fpc_pcibus, ioms.zio_pci_busno) ||
	    !fabric_flag_match(&data->fpc_ff, flags)) {
		return (WALK_NEXT);
	}

	if (data->fpc_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	(void) mdb_snprintf(lanes, sizeof (lanes), "%r-%r",
	    core.zpc_dxio_lane_start, core.zpc_dxio_lane_end);
	mdb_printf("%?p %4r %4r %4r %4r %6r %-9s %s\n", addr, core.zpc_coreno,
	    ioms.zio_num, ioms.zio_iohcnum, ioms.zio_pci_busno, core.zpc_nports,
	    lanes, fabric_flag_str(fstr, sizeof (fstr), flags,
	    fabric_core_flags));

	return (WALK_NEXT);
}

void
fabric_pcie_core_dcmd_help(void)
{
	fabric_table_help("PCIe cores",
	    "\t-n num\tonly show cores with the specified number within their\n"
	    "\t\tIOMS.\n"
	    "\t-i num\tonly show cores on the specified IOHC.\n"
	    "\t-b bus\tonly show cores on the specified PCI bus.\n",
	    fabric_core_flag_defs, ARRAY_SIZE(fabric_core_flag_defs));
}

int
fabric_pcie_core_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	fabric_pcie_core_data_t data = {
		.fpc_flags = flags,
		.fpc_num = UINT64_MAX,
		.fpc_iohcnum = UINT64_MAX,
		.fpc_pcibus = UINT64_MAX
	};
	const char *req = NULL, *excl = NULL;

	if (mdb_getopts(argc, argv,
	    'n', MDB_OPT_UINT64, &data.fpc_num,
	    'i', MDB_OPT_UINT64, &data.fpc_iohcnum,
	    'b', MDB_OPT_UINT64, &data.fpc_pcibus,
	    'f', MDB_OPT_STR, &req,
	    'x', MDB_OPT_STR, &excl,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_table_init() ||
	    !fabric_flag_filter_init(&data.fpc_ff, req, excl,
	    fabric_core_flags)) {
		return (DCMD_ERR);
	}

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %4s %4s %4s %6s %-9s %s%</u>\n",
		    "ADDR", "NUM", "IOMS", "IOHC", "BUS", "NPORTS", "LANES",
		    "FLAGS");
	}

	if (mdb_pwalk("pcie_core", i_pcie_core, &data,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

typedef struct {
	uint_t			fpp_flags;
	uint64_t		fpp_num;
	uint64_t		fpp_core;
	uint64_t		fpp_pcibus;
	uint64_t		fpp_slot;
	fabric_flag_filter_t	fpp_ff;
} fabric_pcie_port_data_t;

static int
i_pcie_port(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_pcie_port_data_t *data = cb_data;
	mdb_zen_pcie_port_t port;
	mdb_zen_pcie_core_t core;
	mdb_zen_ioms_t ioms;
	char bdf[16], lanes[16], name[FABRIC_OXIO_NAME_MAX], fstr[64];
	uint32_t flags;

	if (mdb_ctf_vread(&port, "zen_pcie_port_t", "mdb_zen_pcie_port_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&core, "zen_pcie_core_t", "mdb_zen_pcie_core_t",
	    port.zpp_core, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_zen_ioms_t", core.zpc_ioms,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read PCIe port at %p\n", addr);
		return (WALK_NEXT);
	}
	flags = fabric_enum_read(addr, fabric_layout.fl_port_flags_off);

	if (!fabric_match(data->fpp_num, port.zpp_portno) ||
	    !fabric_match(data->fpp_core, core.zpc_coreno) ||
	    !fabric_match(data->fpp_pcibus, ioms.zio_pci_busno) ||
	    !fabric_match(data->fpp_slot, port.zpp_slotno) ||
	    !fabric_flag_match(&data->fpp_ff, flags)) {
		return (WALK_NEXT);
	}

	if (data->fpp_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	(void) mdb_snprintf(bdf, sizeof (bdf), "%r/%r/%r", ioms.zio_pci_busno,
	    port.zpp_device, port.zpp_func);
	if (!fabric_oxio_describe(port.zpp_oxio, lanes, sizeof (lanes), name,
	    sizeof (name))) {
		(void) strcpy(lanes, "-");
		(void) strcpy(name, "-");
	}
	mdb_printf("%?p %-8s %4r %4r %4r %-8s %-18s %s\n", addr, bdf,
	    core.zpc_coreno, port.zpp_portno, port.zpp_slotno, lanes, name,
	    fabric_flag_str(fstr, sizeof (fstr), flags, fabric_port_flags));

	return (WALK_NEXT);
}

void
fabric_pcie_port_dcmd_help(void)
{
	fabric_table_help("PCIe ports",
	    "\t-n num\tonly show ports with the specified number within their\n"
	    "\t\tcore.\n"
	    "\t-c num\tonly show ports on the core with the specified number\n"
	    "\t\twithin its IOMS.\n"
	    "\t-b bus\tonly show ports on the specified PCI bus.\n"
	    "\t-s slot\tonly show the port with the specified slot number.\n",
	    fabric_port_flag_defs, ARRAY_SIZE(fabric_port_flag_defs));
}

int
fabric_pcie_port_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	fabric_pcie_port_data_t data = {
		.fpp_flags = flags,
		.fpp_num = UINT64_MAX,
		.fpp_core = UINT64_MAX,
		.fpp_pcibus = UINT64_MAX,
		.fpp_slot = UINT64_MAX
	};
	const char *req = NULL, *excl = NULL;

	if (mdb_getopts(argc, argv,
	    'n', MDB_OPT_UINT64, &data.fpp_num,
	    'c', MDB_OPT_UINT64, &data.fpp_core,
	    'b', MDB_OPT_UINT64, &data.fpp_pcibus,
	    's', MDB_OPT_UINT64, &data.fpp_slot,
	    'f', MDB_OPT_STR, &req,
	    'x', MDB_OPT_STR, &excl,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_table_init() ||
	    !fabric_flag_filter_init(&data.fpp_ff, req, excl,
	    fabric_port_flags)) {
		return (DCMD_ERR);
	}

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %-8s %4s %4s %4s %-8s %-18s %s%</u>\n",
		    "ADDR", "B/D/F", "CORE", "PORT", "SLOT", "LANES", "NAME",
		    "FLAGS");
	}

	if (mdb_pwalk("pcie_port", i_pcie_port, &data,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

typedef struct {
	uint_t		fnb_flags;
	uint64_t	fnb_num;
	uint64_t	fnb_pcibus;
} fabric_nbif_data_t;

static int
i_nbif(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_nbif_data_t *data = cb_data;
	mdb_zen_nbif_t nbif;
	mdb_zen_ioms_t ioms;

	if (mdb_ctf_vread(&nbif, "zen_nbif_t", "mdb_zen_nbif_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_zen_ioms_t", nbif.zn_ioms,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read nBIF at %p\n", addr);
		return (WALK_NEXT);
	}

	if (!fabric_match(data->fnb_num, nbif.zn_num) ||
	    !fabric_match(data->fnb_pcibus, ioms.zio_pci_busno)) {
		return (WALK_NEXT);
	}

	if (data->fnb_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %6r\n", addr, nbif.zn_num, ioms.zio_num,
	    ioms.zio_pci_busno, nbif.zn_nfuncs);

	return (WALK_NEXT);
}

void
fabric_nbif_dcmd_help(void)
{
	fabric_table_help("nBIFs",
	    "\t-n num\tonly show nBIFs with the specified number within their\n"
	    "\t\tIOMS.\n"
	    "\t-b bus\tonly show nBIFs on the specified PCI bus.\n",
	    NULL, 0);
}

int
fabric_nbif_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	fabric_nbif_data_t data = {
		.fnb_flags = flags,
		.fnb_num = UINT64_MAX,
		.fnb_pcibus = UINT64_MAX
	};

	if (mdb_getopts(argc, argv,
	    'n', MDB_OPT_UINT64, &data.fnb_num,
	    'b', MDB_OPT_UINT64, &data.fnb_pcibus,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_table_init())
		return (DCMD_ERR);

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %4s %4s %6s%</u>\n",
		    "ADDR", "NUM", "IOMS", "BUS", "NFUNCS");
	}

	if (mdb_pwalk("nbif", i_nbif, &data,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

typedef struct {
	uint_t			fnf_flags;
	uint64_t		fnf_num;
	uint64_t		fnf_pcibus;
	const char		*fnf_type;
	fabric_flag_filter_t	fnf_ff;
} fabric_nbif_func_data_t;

static int
i_nbif_func(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_nbif_func_data_t *data = cb_data;
	mdb_zen_nbif_func_t func;
	mdb_zen_nbif_t nbif;
	mdb_zen_ioms_t ioms;
	char devfn[8], fstr[64];
	const char *tn;
	uint32_t type, flags;

	if (mdb_ctf_vread(&func, "zen_nbif_func_t", "mdb_zen_nbif_func_t",
	    addr, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&nbif, "zen_nbif_t", "mdb_zen_nbif_t", func.znf_nbif,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ioms, "zen_ioms_t", "mdb_zen_ioms_t", nbif.zn_ioms,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read nBIF function at %p\n", addr);
		return (WALK_NEXT);
	}
	type = fabric_enum_read(addr, fabric_layout.fl_func_type_off);
	flags = fabric_enum_read(addr, fabric_layout.fl_func_flags_off);
	tn = fabric_enum_short(fabric_layout.fl_nbif_type, type,
	    "ZEN_NBIF_T_", sizeof ("ZEN_NBIF_T_") - 1);

	if (!fabric_match(data->fnf_num, func.znf_num) ||
	    !fabric_match(data->fnf_pcibus, ioms.zio_pci_busno) ||
	    (data->fnf_type != NULL && strcasecmp(data->fnf_type, tn) != 0) ||
	    !fabric_flag_match(&data->fnf_ff, flags)) {
		return (WALK_NEXT);
	}

	if (data->fnf_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	(void) mdb_snprintf(devfn, sizeof (devfn), "%r/%r", func.znf_dev,
	    func.znf_func);
	mdb_printf("%?p %4r %4r %4r %-6s %-8s %s\n", addr, nbif.zn_num,
	    ioms.zio_pci_busno, func.znf_num, devfn, tn,
	    fabric_flag_str(fstr, sizeof (fstr), flags, fabric_nbif_flags));

	return (WALK_NEXT);
}

void
fabric_nbif_func_dcmd_help(void)
{
	fabric_table_help("nBIF functions",
	    "\t-n num\tonly show functions with the specified number within\n"
	    "\t\ttheir nBIF.\n"
	    "\t-b bus\tonly show functions on the specified PCI bus.\n"
	    "\t-t type\tonly show functions of the specified type, as\n"
	    "\t\tshown in the TYPE column.\n",
	    fabric_nbif_flag_defs, ARRAY_SIZE(fabric_nbif_flag_defs));
}

int
fabric_nbif_func_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	fabric_nbif_func_data_t data = {
		.fnf_flags = flags,
		.fnf_num = UINT64_MAX,
		.fnf_pcibus = UINT64_MAX
	};
	const char *req = NULL, *excl = NULL;

	if (mdb_getopts(argc, argv,
	    'n', MDB_OPT_UINT64, &data.fnf_num,
	    'b', MDB_OPT_UINT64, &data.fnf_pcibus,
	    't', MDB_OPT_STR, &data.fnf_type,
	    'f', MDB_OPT_STR, &req,
	    'x', MDB_OPT_STR, &excl,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_table_init() ||
	    !fabric_flag_filter_init(&data.fnf_ff, req, excl,
	    fabric_nbif_flags)) {
		return (DCMD_ERR);
	}

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %4s %4s %-6s %-8s %s%</u>\n",
		    "ADDR", "NBIF", "BUS", "NUM", "DEV/FN", "TYPE", "FLAGS");
	}

	if (mdb_pwalk("nbif_func", i_nbif_func, &data,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*
 * The CPU side of the tree. At each level the kernel records two numbers for
 * a node. The logical number counts the enabled nodes within the parent from
 * 0 in hardware order, so it is always dense. The physical number is the
 * hardware's own index for the node and is sparse when some are disabled. A
 * CCX with cores 2 and 3 fused off has cores with physical numbers 0, 1, 4
 * and 5 but logical numbers 0 to 3. The NUM column shows the logical number
 * and PHYS, where present, the physical one.
 */
typedef struct {
	uint_t		fcp_flags;
	uint64_t	fcp_num;
	uint64_t	fcp_phys;
	uint64_t	fcp_apicid;
} fabric_cpu_data_t;

static int
i_ccd(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_cpu_data_t *data = cb_data;
	mdb_zen_ccd_t ccd;

	if (mdb_ctf_vread(&ccd, "zen_ccd_t", "mdb_zen_ccd_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read CCD at %p\n", addr);
		return (WALK_NEXT);
	}

	if (!fabric_match(data->fcp_num, ccd.zcd_logical_dieno) ||
	    !fabric_match(data->fcp_phys, ccd.zcd_physical_dieno)) {
		return (WALK_NEXT);
	}

	if (data->fcp_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %5r\n", addr, ccd.zcd_logical_dieno,
	    ccd.zcd_physical_dieno, ccd.zcd_nccxs);

	return (WALK_NEXT);
}

static int
i_ccx(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_cpu_data_t *data = cb_data;
	mdb_zen_ccx_t ccx;
	mdb_zen_ccd_t ccd;

	if (mdb_ctf_vread(&ccx, "zen_ccx_t", "mdb_zen_ccx_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ccd, "zen_ccd_t", "mdb_zen_ccd_t", ccx.zcx_ccd,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read CCX at %p\n", addr);
		return (WALK_NEXT);
	}

	if (!fabric_match(data->fcp_num, ccx.zcx_logical_cxno) ||
	    !fabric_match(data->fcp_phys, ccx.zcx_physical_cxno)) {
		return (WALK_NEXT);
	}

	if (data->fcp_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %6r\n", addr, ccd.zcd_logical_dieno,
	    ccx.zcx_logical_cxno, ccx.zcx_physical_cxno, ccx.zcx_ncores);

	return (WALK_NEXT);
}

/*
 * Describe the APIC IDs of a core's threads as a range, or a single value when
 * there is only one thread.
 */
static const char *
fabric_core_apicids(uintptr_t addr, const mdb_zen_core_t *core, char *buf,
    size_t len)
{
	mdb_zen_thread_t first, last;

	if (core->zc_nthreads == 0 ||
	    mdb_ctf_vread(&first, "zen_thread_t", "mdb_zen_thread_t",
	    fabric_elem(addr, &fabric_layout.fl_threads, 0),
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&last, "zen_thread_t", "mdb_zen_thread_t",
	    fabric_elem(addr, &fabric_layout.fl_threads, core->zc_nthreads - 1),
	    MDB_CTF_VREAD_QUIET) != 0) {
		return ("-");
	}
	if (core->zc_nthreads == 1) {
		(void) mdb_snprintf(buf, len, "%r", first.zt_apicid);
	} else {
		(void) mdb_snprintf(buf, len, "%r-%r", first.zt_apicid,
		    last.zt_apicid);
	}
	return (buf);
}

static int
i_zen_core(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_cpu_data_t *data = cb_data;
	mdb_zen_core_t core;
	mdb_zen_ccx_t ccx;
	mdb_zen_ccd_t ccd;
	char apicids[24];

	if (mdb_ctf_vread(&core, "zen_core_t", "mdb_zen_core_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ccx, "zen_ccx_t", "mdb_zen_ccx_t", core.zc_ccx,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ccd, "zen_ccd_t", "mdb_zen_ccd_t", ccx.zcx_ccd,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read core at %p\n", addr);
		return (WALK_NEXT);
	}

	if (!fabric_match(data->fcp_num, core.zc_logical_coreno) ||
	    !fabric_match(data->fcp_phys, core.zc_physical_coreno)) {
		return (WALK_NEXT);
	}

	if (data->fcp_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %4r %8r %s\n", addr, ccd.zcd_logical_dieno,
	    ccx.zcx_logical_cxno, core.zc_logical_coreno,
	    core.zc_physical_coreno, core.zc_nthreads,
	    fabric_core_apicids(addr, &core, apicids, sizeof (apicids)));

	return (WALK_NEXT);
}

static int
i_zen_thread(uintptr_t addr, const void *arg __unused, void *cb_data)
{
	fabric_cpu_data_t *data = cb_data;
	mdb_zen_thread_t thread;
	mdb_zen_core_t core;
	mdb_zen_ccx_t ccx;
	mdb_zen_ccd_t ccd;

	if (mdb_ctf_vread(&thread, "zen_thread_t", "mdb_zen_thread_t", addr,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&core, "zen_core_t", "mdb_zen_core_t",
	    thread.zt_core, MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ccx, "zen_ccx_t", "mdb_zen_ccx_t", core.zc_ccx,
	    MDB_CTF_VREAD_QUIET) != 0 ||
	    mdb_ctf_vread(&ccd, "zen_ccd_t", "mdb_zen_ccd_t", ccx.zcx_ccd,
	    MDB_CTF_VREAD_QUIET) != 0) {
		mdb_warn("failed to read thread at %p\n", addr);
		return (WALK_NEXT);
	}

	if (!fabric_match(data->fcp_num, thread.zt_threadno) ||
	    !fabric_match(data->fcp_apicid, thread.zt_apicid)) {
		return (WALK_NEXT);
	}

	if (data->fcp_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %4r %6r\n", addr, ccd.zcd_logical_dieno,
	    ccx.zcx_logical_cxno, core.zc_logical_coreno, thread.zt_threadno,
	    thread.zt_apicid);

	return (WALK_NEXT);
}

/*
 * The four CPU-side dcmds share their option handling, differing only in the
 * walker, the callback and the table header.
 */
static int
fabric_cpu_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv,
    const char *walker, mdb_walk_cb_t cb, const char *hdr)
{
	fabric_cpu_data_t data = {
		.fcp_flags = flags,
		.fcp_num = UINT64_MAX,
		.fcp_phys = UINT64_MAX,
		.fcp_apicid = UINT64_MAX
	};

	if (mdb_getopts(argc, argv,
	    'n', MDB_OPT_UINT64, &data.fcp_num,
	    'p', MDB_OPT_UINT64, &data.fcp_phys,
	    'a', MDB_OPT_UINT64, &data.fcp_apicid,
	    NULL) != argc) {
		return (DCMD_USAGE);
	}

	if (!fabric_table_init())
		return (DCMD_ERR);

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags))
		mdb_printf("%<u>%?s%s%</u>\n", "ADDR", hdr);

	if (mdb_pwalk(walker, cb, &data,
	    (flags & DCMD_ADDRSPEC) ? addr : 0) == -1) {
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

void
fabric_ccd_dcmd_help(void)
{
	fabric_table_help("CCDs",
	    "\t-n num\tonly show the CCD with the specified logical number.\n"
	    "\t-p num\tonly show the CCD with the specified physical number.\n",
	    NULL, 0);
}

int
fabric_ccd_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (fabric_cpu_dcmd(addr, flags, argc, argv, "ccd", i_ccd,
	    "  NUM PHYS NCCXS"));
}

void
fabric_ccx_dcmd_help(void)
{
	fabric_table_help("CCXs",
	    "\t-n num\tonly show CCXs with the specified logical number\n"
	    "\t\twithin their CCD.\n"
	    "\t-p num\tonly show CCXs with the specified physical number.\n",
	    NULL, 0);
}

int
fabric_ccx_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (fabric_cpu_dcmd(addr, flags, argc, argv, "ccx", i_ccx,
	    "  CCD  NUM PHYS NCORES"));
}

void
fabric_core_dcmd_help(void)
{
	fabric_table_help("CPU cores",
	    "\t-n num\tonly show cores with the specified logical number\n"
	    "\t\twithin their CCX.\n"
	    "\t-p num\tonly show cores with the specified physical number.\n",
	    NULL, 0);
}

int
fabric_core_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (fabric_cpu_dcmd(addr, flags, argc, argv, "zen_core",
	    i_zen_core, "  CCD  CCX  NUM PHYS NTHREADS APICIDS"));
}

void
fabric_thread_dcmd_help(void)
{
	fabric_table_help("CPU threads",
	    "\t-n num\tonly show threads with the specified number within\n"
	    "\t\ttheir core.\n"
	    "\t-a id\tonly show the thread with the specified APIC ID.\n",
	    NULL, 0);
}

int
fabric_thread_dcmd(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv)
{
	return (fabric_cpu_dcmd(addr, flags, argc, argv, "zen_thread",
	    i_zen_thread, "  CCD  CCX CORE  NUM APICID"));
}

/*
 * The walkers yield the target address of each node at a given level of the
 * fabric tree. Consumers read the node's contents from that address via CTF.
 * The tree is descended once when a walk is initialised to collect the
 * addresses, which are then stepped through.
 *
 * A walk may be global, or may start from the address of a node at or above
 * the level being walked, in which case only that node's descendants (or the
 * node itself) are yielded. For example, `<pcie_core>::walk pcie_port` yields
 * only the ports of that core.
 */

/*
 * Each level's name, parent level and the location of its nodes within the
 * parent. The number of nodes is read from a counter in the parent, whose
 * offset and size are resolved from CTF along with the rest of the layout.
 */
typedef struct {
	const char		*fld_name;
	fabric_level_t		fld_parent;
	const fabric_arr_t	*fld_arr;
	const char		*fld_ptype;	/* parent's kernel type */
	const char		*fld_count;	/* count member in the parent */
	ulong_t			fld_count_off;
	size_t			fld_count_sz;
} fabric_level_def_t;

static fabric_level_def_t fabric_levels[FABRIC_L_NLEVELS] = {
	[FABRIC_L_SOC] = {
		.fld_name = "soc",
		.fld_parent = FABRIC_L_ROOT,
		.fld_arr = &fabric_layout.fl_socs,
		.fld_ptype = "zen_fabric_t",
		.fld_count = "zf_nsocs"
	},
	[FABRIC_L_IODIE] = {
		.fld_name = "iodie",
		.fld_parent = FABRIC_L_SOC,
		.fld_arr = &fabric_layout.fl_iodies,
		.fld_ptype = "zen_soc_t",
		.fld_count = "zs_niodies"
	},
	[FABRIC_L_NBIO] = {
		.fld_name = "nbio",
		.fld_parent = FABRIC_L_IODIE,
		.fld_arr = &fabric_layout.fl_nbio,
		.fld_ptype = "zen_iodie_t",
		.fld_count = "zi_nnbio"
	},
	[FABRIC_L_IOMS] = {
		.fld_name = "ioms",
		.fld_parent = FABRIC_L_NBIO,
		.fld_arr = &fabric_layout.fl_ioms,
		.fld_ptype = "zen_nbio_t",
		.fld_count = "zn_nioms"
	},
	[FABRIC_L_PCIE_CORE] = {
		.fld_name = "pcie_core",
		.fld_parent = FABRIC_L_IOMS,
		.fld_arr = &fabric_layout.fl_cores,
		.fld_ptype = "zen_ioms_t",
		.fld_count = "zio_npcie_cores"
	},
	[FABRIC_L_PCIE_PORT] = {
		.fld_name = "pcie_port",
		.fld_parent = FABRIC_L_PCIE_CORE,
		.fld_arr = &fabric_layout.fl_ports,
		.fld_ptype = "zen_pcie_core_t",
		.fld_count = "zpc_nports"
	},
	[FABRIC_L_NBIF] = {
		.fld_name = "nbif",
		.fld_parent = FABRIC_L_IOMS,
		.fld_arr = &fabric_layout.fl_nbifs,
		.fld_ptype = "zen_ioms_t",
		.fld_count = "zio_nnbifs"
	},
	[FABRIC_L_NBIF_FUNC] = {
		.fld_name = "nbif_func",
		.fld_parent = FABRIC_L_NBIF,
		.fld_arr = &fabric_layout.fl_funcs,
		.fld_ptype = "zen_nbif_t",
		.fld_count = "zn_nfuncs"
	},
	[FABRIC_L_CCD] = {
		.fld_name = "ccd",
		.fld_parent = FABRIC_L_IODIE,
		.fld_arr = &fabric_layout.fl_ccds,
		.fld_ptype = "zen_iodie_t",
		.fld_count = "zi_nccds"
	},
	[FABRIC_L_CCX] = {
		.fld_name = "ccx",
		.fld_parent = FABRIC_L_CCD,
		.fld_arr = &fabric_layout.fl_ccxs,
		.fld_ptype = "zen_ccd_t",
		.fld_count = "zcd_nccxs"
	},
	[FABRIC_L_CORE] = {
		.fld_name = "zen_core",
		.fld_parent = FABRIC_L_CCX,
		.fld_arr = &fabric_layout.fl_ccx_cores,
		.fld_ptype = "zen_ccx_t",
		.fld_count = "zcx_ncores"
	},
	[FABRIC_L_THREAD] = {
		.fld_name = "zen_thread",
		.fld_parent = FABRIC_L_CORE,
		.fld_arr = &fabric_layout.fl_threads,
		.fld_ptype = "zen_core_t",
		.fld_count = "zc_nthreads"
	}
};

typedef struct {
	fabric_level_t	fc_level;	/* the level being walked */
	uintptr_t	fc_start;	/* restrict to this node's subtree */
	bool		fc_found;	/* fc_start was encountered */
	uintptr_t	*fc_addrs;
	uint_t		fc_n;
	uint_t		fc_cap;
	uint_t		fc_idx;
} fabric_collect_t;

/*
 * Resolve the offset and size of each level's count member.
 */
static bool
fabric_levels_init(void)
{
	for (uint_t i = FABRIC_L_ROOT + 1; i < FABRIC_L_NLEVELS; i++) {
		fabric_level_def_t *ld = &fabric_levels[i];
		mdb_ctf_id_t tid, mid;
		ulong_t off;
		ssize_t sz;

		if (mdb_ctf_lookup_by_name(ld->fld_ptype, &tid) != 0 ||
		    mdb_ctf_member_info(tid, ld->fld_count, &off, &mid) != 0) {
			mdb_warn("failed to find %s::%s", ld->fld_ptype,
			    ld->fld_count);
			return (false);
		}
		if ((sz = mdb_ctf_type_size(mid)) < 0) {
			mdb_warn("failed to determine the size of %s::%s",
			    ld->fld_ptype, ld->fld_count);
			return (false);
		}
		if (sz == 0 || sz > sizeof (uint64_t) || off % NBBY != 0) {
			mdb_warn("%s::%s has an unsupported size (%ld bytes) "
			    "or alignment (bit offset %lu)\n", ld->fld_ptype,
			    ld->fld_count, (long)sz, off);
			return (false);
		}
		ld->fld_count_off = off / NBBY;
		ld->fld_count_sz = (size_t)sz;
	}
	return (true);
}

/*
 * Read the number of nodes of the given level held by the parent at paddr.
 */
static bool
fabric_count_read(const fabric_level_def_t *ld, uintptr_t paddr, uint_t *np)
{
	uint64_t v = 0;

	/*
	 * The target is little-endian, so a count narrower than 64 bits lands
	 * in the low-order bytes.
	 */
	if (mdb_vread(&v, ld->fld_count_sz, paddr + ld->fld_count_off) !=
	    (ssize_t)ld->fld_count_sz) {
		return (false);
	}
	*np = (uint_t)v;
	return (true);
}

static void
fabric_collect_push(fabric_collect_t *c, uintptr_t addr)
{
	if (c->fc_n == c->fc_cap) {
		uint_t ncap = (c->fc_cap == 0) ? 64 : c->fc_cap * 2;
		uintptr_t *na;

		/* The old array is left for the garbage collector */
		na = mdb_alloc(ncap * sizeof (uintptr_t), UM_SLEEP | UM_GC);
		if (c->fc_addrs != NULL) {
			(void) memcpy(na, c->fc_addrs,
			    c->fc_n * sizeof (uintptr_t));
		}
		c->fc_addrs = na;
		c->fc_cap = ncap;
	}
	c->fc_addrs[c->fc_n++] = addr;
}

/*
 * Descend from the node at paddr, whose children are at level path[idx],
 * towards the level being collected. Nodes are collected once we are within
 * the subtree of the start node, if one was given.
 */
static void
fabric_collect_descend(fabric_collect_t *c, const fabric_level_t *path,
    uint_t depth, uint_t idx, uintptr_t paddr, bool within)
{
	const fabric_level_def_t *ld = &fabric_levels[path[idx]];
	uint_t n;

	if (!fabric_count_read(ld, paddr, &n))
		return;

	for (uint_t i = 0; i < n; i++) {
		uintptr_t addr = fabric_elem(paddr, ld->fld_arr, i);
		bool w = within;

		if (addr == c->fc_start) {
			c->fc_found = true;
			w = true;
		}
		if (idx + 1 == depth) {
			if (w)
				fabric_collect_push(c, addr);
		} else {
			fabric_collect_descend(c, path, depth, idx + 1, addr,
			    w);
		}
	}
}

static bool
fabric_collect(fabric_collect_t *c)
{
	fabric_level_t path[FABRIC_L_NLEVELS];
	uint_t depth = 0, i;
	GElf_Sym sym;

	/*
	 * Build the chain of levels from the root down to the one being
	 * collected.
	 */
	for (fabric_level_t l = c->fc_level; l != FABRIC_L_ROOT;
	    l = fabric_levels[l].fld_parent) {
		depth++;
	}
	i = depth;
	for (fabric_level_t l = c->fc_level; l != FABRIC_L_ROOT;
	    l = fabric_levels[l].fld_parent) {
		path[--i] = l;
	}

	if (mdb_lookup_by_name("zen_fabric", &sym) == -1) {
		mdb_warn("failed to find 'zen_fabric'");
		return (false);
	}

	fabric_collect_descend(c, path, depth, 0, sym.st_value,
	    c->fc_start == 0);
	return (true);
}

/*
 * The level to walk is passed as the walker's init argument.
 */
int
fabric_walk_init(mdb_walk_state_t *wsp)
{
	fabric_level_t level = (fabric_level_t)(uintptr_t)wsp->walk_arg;
	fabric_collect_t *c;

	if (!fabric_layout_init() || !fabric_levels_init()) {
		mdb_warn("failed to resolve fabric type layout from CTF\n");
		return (WALK_ERR);
	}

	c = mdb_zalloc(sizeof (*c), UM_SLEEP | UM_GC);
	c->fc_level = level;
	c->fc_start = wsp->walk_addr;

	if (!fabric_collect(c))
		return (WALK_ERR);
	if (c->fc_start != 0 && !c->fc_found) {
		mdb_warn("%p is not a %s or an ancestor of one\n", c->fc_start,
		    fabric_levels[level].fld_name);
		return (WALK_ERR);
	}

	wsp->walk_data = c;
	return (WALK_NEXT);
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
