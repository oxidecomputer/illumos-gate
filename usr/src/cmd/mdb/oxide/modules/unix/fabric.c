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

/*
 * This part of the file contains the mdb support for dcmds:
 *	::fabric, ::ioms
 * and walkers for:
 *	soc, iodie, nbio, ioms
 *
 */

#include <mdb/mdb_param.h>
#include <mdb/mdb_modapi.h>
#include <mdb/mdb_target.h>
#include <mdb/mdb_ctf.h>

#define	_SYS_MACHPARAM_H
#define	_SYS_MACHCPUVAR_H
#define	_SYS_CPUVAR_H
typedef struct cpuset cpuset_t;
#include <zen/fabric_impl.h>

#include <stddef.h>
#include <stdbool.h>

#define	ADDR(x, y) ((uintptr_t)(void *)(x) - (uintptr_t)(void *)(y) + addr)

typedef struct {
	bool		fd_verbose;
	bool		fd_ccd;
	bool		fd_nbif;
	bool		fd_printing;
	uintptr_t	fd_saddr;
	uint_t		fd_indent;
	zen_ioms_t	*fd_ioms;
} fabric_data_t;

static const char *fabric_tile_map[] = {
	[OXIO_TILE_G0] = "G0",
	[OXIO_TILE_P0] = "P0",
	[OXIO_TILE_G1] = "G1",
	[OXIO_TILE_P1] = "P1",
	[OXIO_TILE_G2] = "G2",
	[OXIO_TILE_P2] = "P2",
	[OXIO_TILE_G3] = "G3",
	[OXIO_TILE_P3] = "P3",
	[OXIO_TILE_P4] = "P4",
	[OXIO_TILE_P5] = "P5"
};

static const mdb_bitmask_t fabric_port_flags[] = {
	{ "MAPPED", ZEN_PCIE_PORT_F_MAPPED, ZEN_PCIE_PORT_F_MAPPED },
	{ "HIDDEN", ZEN_PCIE_PORT_F_BRIDGE_HIDDEN,
	    ZEN_PCIE_PORT_F_BRIDGE_HIDDEN },
	{ "HOTPLUG", ZEN_PCIE_PORT_F_HOTPLUG, ZEN_PCIE_PORT_F_HOTPLUG },
	{ NULL, 0, 0 }
};

static void
fabric_print_port(uintptr_t addr, zen_pcie_port_t *port, fabric_data_t *cbd)
{
	oxio_engine_t oxio;

	if (!cbd->fd_verbose &&
	    (port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN)) {
		return;
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p PORT %r [%r/%r/%r] slot %r",
		    cbd->fd_indent * 2, "", addr, port->zpp_portno,
		    cbd->fd_ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, port->zpp_slotno);
		if (mdb_vread(&oxio, sizeof (oxio),
		    (uintptr_t)(void *)port->zpp_oxio) != -1) {
			char descr[MAXPATHLEN];

			if (mdb_readstr(descr, sizeof (descr),
			    (uintptr_t)(void *)oxio.oe_name) <= 0) {
				(void) strcpy(descr, "??");
			}

			mdb_printf(" [%s] %s/%rx%r", descr,
			    oxio.oe_tile < ARRAY_SIZE(fabric_tile_map) ?
			    fabric_tile_map[oxio.oe_tile] : "??",
			    oxio.oe_lane, oxio.oe_nlanes);
		}
		if (port->zpp_flags != 0 && cbd->fd_verbose)
			mdb_printf(" <%b>", port->zpp_flags, fabric_port_flags);
		mdb_printf("\n");
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static const mdb_bitmask_t fabric_core_flags[] = {
	{ "USED", ZEN_PCIE_CORE_F_USED, ZEN_PCIE_CORE_F_USED },
	{ "HOTPLUG", ZEN_PCIE_CORE_F_HAS_HOTPLUG, ZEN_PCIE_CORE_F_HAS_HOTPLUG },
	{ NULL, 0, 0 }
};

static void
fabric_print_core(uintptr_t addr, zen_pcie_core_t *core, fabric_data_t *cbd)
{
	if (!cbd->fd_verbose && !(core->zpc_flags & ZEN_PCIE_CORE_F_USED))
		return;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CORE %r",
		    cbd->fd_indent * 2, "", addr, core->zpc_coreno);
		if (cbd->fd_verbose && core->zpc_flags != 0)
			mdb_printf(" <%b>", core->zpc_flags, fabric_core_flags);
		mdb_printf("\n");
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < core->zpc_nports; i++) {
		zen_pcie_port_t *port = &core->zpc_ports[i];
		fabric_print_port(ADDR(port, core), port, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static const char *fabric_nbif_type_map[] = {
	[ZEN_NBIF_T_ABSENT] = "ABSENT",
	[ZEN_NBIF_T_DUMMY] = "DUMMY",
	[ZEN_NBIF_T_ACP] = "ACP",
	[ZEN_NBIF_T_AZ] = "AZ",
	[ZEN_NBIF_T_MPDMATF] = "MPDMATF",
	[ZEN_NBIF_T_NTB] = "NTB",
	[ZEN_NBIF_T_NVME] = "NVME",
	[ZEN_NBIF_T_PSPCCP] = "PSPCCP",
	[ZEN_NBIF_T_PTDMA] = "PTDMA",
	[ZEN_NBIF_T_PVNTB] = "PVNTB",
	[ZEN_NBIF_T_SATA] = "SATA",
	[ZEN_NBIF_T_SVNTB] = "SVNTB",
	[ZEN_NBIF_T_USB] = "USB"
};

static const mdb_bitmask_t fabric_nbif_flags[] = {
	{ "EN", ZEN_NBIF_F_ENABLED, ZEN_NBIF_F_ENABLED },
	{ "NOCFG", ZEN_NBIF_F_NO_CONFIG, ZEN_NBIF_F_NO_CONFIG },
	{ "FLR", ZEN_NBIF_F_FLR_EN, ZEN_NBIF_F_FLR_EN },
	{ "ACS", ZEN_NBIF_F_ACS_EN, ZEN_NBIF_F_ACS_EN },
	{ "AER", ZEN_NBIF_F_AER_EN, ZEN_NBIF_F_AER_EN },
	{ "PMS", ZEN_NBIF_F_PMSTATUS_EN, ZEN_NBIF_F_PMSTATUS_EN },
	{ "CPLR", ZEN_NBIF_F_TPH_CPLR_EN, ZEN_NBIF_F_TPH_CPLR_EN },
	{ "PANF", ZEN_NBIF_F_PANF_EN, ZEN_NBIF_F_PANF_EN },
	{ NULL, 0, 0 }
};

static void
fabric_print_nbif_func(uintptr_t addr, zen_nbif_func_t *func,
    fabric_data_t *cbd)
{
	if (!cbd->fd_verbose && func->znf_type == ZEN_NBIF_T_ABSENT)
		return;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p FUNC %r [%r/%r] %s",
		    cbd->fd_indent * 2, "", addr, func->znf_num,
		    func->znf_dev, func->znf_func,
		    func->znf_type < ARRAY_SIZE(fabric_nbif_type_map) ?
		    fabric_nbif_type_map[func->znf_type] : "??");
		if (cbd->fd_verbose && func->znf_flags != 0)
			mdb_printf(" <%b>", func->znf_flags, fabric_nbif_flags);
		mdb_printf("\n");
		cbd->fd_indent++;
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_nbif(uintptr_t addr, zen_nbif_t *nbif, fabric_data_t *cbd)
{
	if (!cbd->fd_verbose && nbif->zn_nfuncs == 0)
		return;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p NBIF %r\n",
		    cbd->fd_indent * 2, "", addr, nbif->zn_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < nbif->zn_nfuncs; i++) {
		zen_nbif_func_t *func = &nbif->zn_funcs[i];
		fabric_print_nbif_func(ADDR(func, nbif), func, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static const mdb_bitmask_t fabric_ioms_flags[] = {
	{ "FCH", ZEN_IOMS_F_HAS_FCH, ZEN_IOMS_F_HAS_FCH },
	{ "BONUS", ZEN_IOMS_F_HAS_BONUS, ZEN_IOMS_F_HAS_BONUS },
	{ "NBIF", ZEN_IOMS_F_HAS_NBIF, ZEN_IOMS_F_HAS_NBIF },
	{ NULL, 0, 0 }
};

static void
fabric_print_ioms(uintptr_t addr, zen_ioms_t *ioms, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf(
		    "%*s%0?p IOMS %r / IOHC %r (%s)",
		    cbd->fd_indent * 2, "", addr,
		    ioms->zio_num, ioms->zio_iohcnum,
		    ioms->zio_iohctype == ZEN_IOHCT_LARGE ? "Large" : "Small");
		if (ioms->zio_flags != 0)
			mdb_printf(" <%b>", ioms->zio_flags, fabric_ioms_flags);
		mdb_printf("\n");
		cbd->fd_indent++;
	}
	cbd->fd_ioms = ioms;
	for (uint_t i = 0; i < ioms->zio_npcie_cores; i++) {
		zen_pcie_core_t *core = &ioms->zio_pcie_cores[i];
		fabric_print_core(ADDR(core, ioms), core, cbd);
	}
	if (cbd->fd_nbif) {
		for (uint_t i = 0; i < ioms->zio_nnbifs; i++) {
			zen_nbif_t *nbif = &ioms->zio_nbifs[i];
			fabric_print_nbif(ADDR(nbif, ioms), nbif, cbd);
		}
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_nbio(uintptr_t addr, zen_nbio_t *nbio, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p NBIO %r\n", cbd->fd_indent * 2, "",
		    addr, nbio->zn_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < nbio->zn_nioms; i++) {
		zen_ioms_t *ioms = &nbio->zn_ioms[i];
		fabric_print_ioms(ADDR(ioms, nbio), ioms, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_cpu_thread(uintptr_t addr, zen_thread_t *thread,
    fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p thread %r (APIC %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    thread->zt_threadno, thread->zt_apicid);
	}
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_cpu_core(uintptr_t addr, zen_core_t *core, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CORE %r (phys %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    core->zc_logical_coreno, core->zc_physical_coreno);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < core->zc_nthreads; i++) {
		zen_thread_t *thread = &core->zc_threads[i];
		fabric_print_cpu_thread(ADDR(thread, core), thread, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_ccx(uintptr_t addr, zen_ccx_t *ccx, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CCX %r (phys %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    ccx->zcx_logical_cxno, ccx->zcx_physical_cxno);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < ccx->zcx_ncores; i++) {
		zen_core_t *core = &ccx->zcx_cores[i];
		fabric_print_cpu_core(ADDR(core, ccx), core, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_ccd(uintptr_t addr, zen_ccd_t *ccd, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p CCD %r (phys %r)\n",
		    cbd->fd_indent * 2, "", addr,
		    ccd->zcd_logical_dieno, ccd->zcd_physical_dieno);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < ccd->zcd_nccxs; i++) {
		zen_ccx_t *ccx = &ccd->zcd_ccxs[i];
		fabric_print_ccx(ADDR(ccx, ccd), ccx, cbd);
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_iodie(uintptr_t addr, zen_iodie_t *iodie, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p IODIE %r\n", cbd->fd_indent * 2, "",
		    addr, iodie->zi_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < iodie->zi_nnbio; i++) {
		zen_nbio_t *nbio = &iodie->zi_nbio[i];
		fabric_print_nbio(ADDR(nbio, iodie), nbio, cbd);
	}
	if (cbd->fd_ccd) {
		for (uint_t i = 0; i < iodie->zi_nccds; i++) {
			zen_ccd_t *ccd = &iodie->zi_ccds[i];
			fabric_print_ccd(ADDR(ccd, iodie), ccd, cbd);
		}
	}
	if (cbd->fd_printing)
		cbd->fd_indent--;
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = false;
}

static void
fabric_print_soc(uintptr_t addr, zen_soc_t *soc, fabric_data_t *cbd)
{
	if (cbd->fd_saddr == addr)
		cbd->fd_printing = true;
	if (cbd->fd_printing) {
		mdb_printf("%*s%0?p SOC %r\n", cbd->fd_indent * 2, "",
		    addr, soc->zs_num);
		cbd->fd_indent++;
	}
	for (uint_t i = 0; i < soc->zs_niodies; i++) {
		zen_iodie_t *iodie = &soc->zs_iodies[i];
		fabric_print_iodie(ADDR(iodie, soc), iodie, cbd);
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
	zen_fabric_t *fabric;
	GElf_Sym sym;
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

	if (mdb_lookup_by_name("zen_fabric", &sym) == -1) {
		mdb_warn("failed to find 'zen_fabric'");
		return (DCMD_ERR);
	}
	addr = sym.st_value;

	fabric = mdb_zalloc(sizeof (*fabric), UM_NOSLEEP | UM_GC);
	if (fabric == NULL) {
		mdb_warn("failed to allocate memory for fabric");
		return (DCMD_ERR);
	}

	if (mdb_vread(fabric, sizeof (*fabric), addr) == -1) {
		mdb_warn("can't read zen_fabric structure at %p", addr);
		return (WALK_ERR);
	}

	for (uint_t s = 0; s < fabric->zf_nsocs; s++) {
		zen_soc_t *soc = &fabric->zf_socs[s];
		fabric_print_soc(ADDR(soc, fabric), soc, &cbd);
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

typedef struct {
	uint8_t		zn_num;
} mdb_zen_nbio_t;

static int
i_ioms(uintptr_t addr, const void *arg, void *cb_data)
{
	fabric_ioms_data_t *data = cb_data;
	const zen_ioms_t *ioms = arg;
	mdb_zen_nbio_t nbio;

	if (mdb_ctf_vread(&nbio, "zen_nbio_t", "mdb_zen_nbio_t",
	    (uintptr_t)(void *)ioms->zio_nbio, 0) == -1) {
		nbio.zn_num = UINT8_MAX;
	}

	if (data->fid_num != UINT64_MAX && data->fid_num != ioms->zio_num)
		return (WALK_NEXT);
	if (data->fid_iohcnum != UINT64_MAX &&
	    data->fid_iohcnum != ioms->zio_iohcnum) {
		return (WALK_NEXT);
	}
	if (data->fid_iohubnum != UINT64_MAX &&
	    data->fid_iohubnum != ioms->zio_iohubnum) {
		return (WALK_NEXT);
	}
	if (data->fid_nbionum != UINT64_MAX &&
	    data->fid_nbionum != nbio.zn_num) {
		return (WALK_NEXT);
	}
	if (data->fid_pcibus != UINT64_MAX &&
	    data->fid_pcibus != ioms->zio_pci_busno) {
		return (WALK_NEXT);
	}

	if (data->fid_flags & DCMD_PIPE_OUT) {
		mdb_printf("%lr\n", addr);
		return (WALK_NEXT);
	}

	mdb_printf("%?p %4r %4r %4r %5r %4r %5r %b%s%s\n",
	    addr, ioms->zio_num, ioms->zio_iohcnum, nbio.zn_num,
	    ioms->zio_iohubnum, ioms->zio_pci_busno, ioms->zio_npcie_cores,
	    ioms->zio_flags, fabric_ioms_flags,
	    ioms->zio_flags != 0 ? "," : "",
	    ioms->zio_iohctype == ZEN_IOHCT_LARGE ? "LARGE" : "");

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

	if (!(flags & DCMD_PIPE_OUT) && DCMD_HDRSPEC(flags)) {
		mdb_printf("%<u>%?s %4s %4s %4s %5s %4s %5s %s%</u>\n",
		    "ADDR", "NUM", "IOHC", "NBIO", "IOHUB", "BUS", "CORES",
		    "FLAGS");
	}

	if (mdb_walk("ioms", i_ioms, &data) == -1)
		return (DCMD_ERR);

	return (DCMD_OK);
}

typedef struct {
	zen_fabric_t		fwd_fabric;
	uintptr_t		fwd_addr;
	uint8_t			fwd_soc;
	uint8_t			fwd_iodie;
	uint8_t			fwd_nbio;
	uint8_t			fwd_ioms;
} fabric_walk_data_t;

int
fabric_walk_init(mdb_walk_state_t *wsp)
{
	fabric_walk_data_t *data;
	GElf_Sym sym;

	if (wsp->walk_addr != 0) {
		mdb_warn("zen walkers only support global walks\n");
		return (WALK_ERR);
	}

	if (mdb_lookup_by_name("zen_fabric", &sym) == -1) {
		mdb_warn("failed to find 'zen_fabric'");
		return (WALK_ERR);
	}

	data = mdb_zalloc(sizeof (*data), UM_NOSLEEP | UM_GC);
	if (data == NULL) {
		mdb_warn("failed to allocate memory for fabric walker");
		return (WALK_ERR);
	}

	if (mdb_vread(&data->fwd_fabric, sizeof (data->fwd_fabric),
	    sym.st_value) == -1) {
		mdb_warn("can't read zen_fabric structure at %p", sym.st_value);
		return (WALK_ERR);
	}
	data->fwd_addr = sym.st_value;

	wsp->walk_data = data;
	return (WALK_NEXT);
}

int
fabric_walk_soc_step(mdb_walk_state_t *wsp)
{
	fabric_walk_data_t *fwd = wsp->walk_data;
	zen_fabric_t *fabric = &fwd->fwd_fabric;
	uintptr_t addr;

	while (fwd->fwd_soc < fabric->zf_nsocs) {
		zen_soc_t *soc = &fabric->zf_socs[fwd->fwd_soc++];
		addr = fwd->fwd_addr + (uintptr_t)(void *)soc -
		    (uintptr_t)(void *)fabric;
		return (wsp->walk_callback(addr, soc, wsp->walk_cbdata));
	}

	return (WALK_DONE);
}

int
fabric_walk_iodie_step(mdb_walk_state_t *wsp)
{
	fabric_walk_data_t *fwd = wsp->walk_data;
	zen_fabric_t *fabric = &fwd->fwd_fabric;
	uintptr_t addr;

	while (fwd->fwd_soc < fabric->zf_nsocs) {
		zen_soc_t *soc = &fabric->zf_socs[fwd->fwd_soc];
		while (fwd->fwd_iodie < soc->zs_niodies) {
			zen_iodie_t *iodie = &soc->zs_iodies[fwd->fwd_iodie++];
			addr = fwd->fwd_addr + (uintptr_t)(void *)iodie -
			    (uintptr_t)(void *)fabric;
			return (wsp->walk_callback(addr, iodie,
			    wsp->walk_cbdata));
		}
		fwd->fwd_iodie = 0;
		fwd->fwd_soc++;
	}

	return (WALK_DONE);
}

int
fabric_walk_nbio_step(mdb_walk_state_t *wsp)
{
	fabric_walk_data_t *fwd = wsp->walk_data;
	zen_fabric_t *fabric = &fwd->fwd_fabric;
	uintptr_t addr;

	while (fwd->fwd_soc < fabric->zf_nsocs) {
		zen_soc_t *soc = &fabric->zf_socs[fwd->fwd_soc];
		while (fwd->fwd_iodie < soc->zs_niodies) {
			zen_iodie_t *iodie = &soc->zs_iodies[fwd->fwd_iodie];
			while (fwd->fwd_nbio < iodie->zi_nnbio) {
				zen_nbio_t *nbio =
				    &iodie->zi_nbio[fwd->fwd_nbio++];
				addr = fwd->fwd_addr + (uintptr_t)(void *)nbio -
				    (uintptr_t)(void *)fabric;
				return (wsp->walk_callback(addr, nbio,
				    wsp->walk_cbdata));
			}
			fwd->fwd_nbio = 0;
			fwd->fwd_iodie++;
		}
		fwd->fwd_iodie = 0;
		fwd->fwd_soc++;
	}

	return (WALK_DONE);
}

int
fabric_walk_ioms_step(mdb_walk_state_t *wsp)
{
	fabric_walk_data_t *fwd = wsp->walk_data;
	zen_fabric_t *fabric = &fwd->fwd_fabric;
	uintptr_t addr;

	while (fwd->fwd_soc < fabric->zf_nsocs) {
		zen_soc_t *soc = &fabric->zf_socs[fwd->fwd_soc];
		while (fwd->fwd_iodie < soc->zs_niodies) {
			zen_iodie_t *iodie = &soc->zs_iodies[fwd->fwd_iodie];
			while (fwd->fwd_nbio < iodie->zi_nnbio) {
				zen_nbio_t *nbio =
				    &iodie->zi_nbio[fwd->fwd_nbio];
				while (fwd->fwd_ioms < nbio->zn_nioms) {
					zen_ioms_t *ioms =
					    &nbio->zn_ioms[fwd->fwd_ioms++];
					addr = fwd->fwd_addr +
					    (uintptr_t)(void *)ioms -
					    (uintptr_t)(void *)fabric;
					return (wsp->walk_callback(
					    addr, ioms, wsp->walk_cbdata));
				}
				fwd->fwd_ioms = 0;
				fwd->fwd_nbio++;
			}
			fwd->fwd_nbio = 0;
			fwd->fwd_iodie++;
		}
		fwd->fwd_iodie = 0;
		fwd->fwd_soc++;
	}

	return (WALK_DONE);
}
