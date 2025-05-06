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
 * Various routines and things to access, initialize, understand, and manage
 * Genoa's I/O fabric. This consists of both the data fabric and the
 * northbridges.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/pci_ident.h>
#include <sys/platform_detect.h>

#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/mpio.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>
#include <sys/io/zen/smu_impl.h>

#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/ioapic.h>
#include <sys/io/genoa/iohc.h>
#include <sys/io/genoa/iommu.h>
#include <sys/io/genoa/mpio_impl.h>
#include <sys/io/genoa/nbif_impl.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/io/genoa/pcie_rsmu.h>
#include <sys/io/genoa/pptable.h>
#include <sys/io/genoa/smu.h>

/*
 * This table encodes knowledge about how the SoC assigns devices and functions
 * to root ports.
 */
static const zen_pcie_port_info_t
    genoa_pcie[GENOA_IOMS_MAX_PCIE_CORES][GENOA_PCIE_CORE_MAX_PORTS] = {
	[0] = {
		{  .zppi_dev = 0x1, .zppi_func = 0x1 },
		{  .zppi_dev = 0x1, .zppi_func = 0x2 },
		{  .zppi_dev = 0x1, .zppi_func = 0x3 },
		{  .zppi_dev = 0x1, .zppi_func = 0x4 },
		{  .zppi_dev = 0x1, .zppi_func = 0x5 },
		{  .zppi_dev = 0x1, .zppi_func = 0x6 },
		{  .zppi_dev = 0x1, .zppi_func = 0x7 },
		{  .zppi_dev = 0x2, .zppi_func = 0x1 },
		{  .zppi_dev = 0x2, .zppi_func = 0x2 }
	},
	[1] = {
		{  .zppi_dev = 0x3, .zppi_func = 0x1 },
		{  .zppi_dev = 0x3, .zppi_func = 0x2 },
		{  .zppi_dev = 0x3, .zppi_func = 0x3 },
		{  .zppi_dev = 0x3, .zppi_func = 0x4 },
		{  .zppi_dev = 0x3, .zppi_func = 0x5 },
		{  .zppi_dev = 0x3, .zppi_func = 0x6 },
		{  .zppi_dev = 0x3, .zppi_func = 0x7 },
		{  .zppi_dev = 0x4, .zppi_func = 0x1 },
		{  .zppi_dev = 0x4, .zppi_func = 0x2 }
	},
	[2] = {
		{  .zppi_dev = 0x5, .zppi_func = 0x1 },
		{  .zppi_dev = 0x5, .zppi_func = 0x2 },
		{  .zppi_dev = 0x5, .zppi_func = 0x3 },
		{  .zppi_dev = 0x5, .zppi_func = 0x4 }
	}
};

/*
 * These are internal bridges that correspond to NBIFs; they are modeled as
 * ports but there is no physical port brought out of the package.
 */
const zen_iohc_nbif_ports_t
    genoa_pcie_int_ports[GENOA_IOMS_PER_IODIE] = {
	[0] = {
		.zinp_count = 2,
		.zinp_ports = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
			{ .zppi_dev = 0x7, .zppi_func = 0x2, },
		},
	},
	[1] = {
		.zinp_count = 1,
		.zinp_ports = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
		},
	},
	[2] = {
		.zinp_count = 2,
		.zinp_ports = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
			{ .zppi_dev = 0x7, .zppi_func = 0x2, },
		},
	},
	[3] = {
		.zinp_count = 1,
		.zinp_ports = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
		},
	},
};

/*
 * This table encodes the mapping of the set of dxio lanes to a given PCIe core
 * on an IOMS. Note, that all lanes here are inclusive. e.g. [start, end].
 * The subsequent table encodes mappings for the bonus cores.
 */
static const zen_pcie_core_info_t genoa_lane_maps[8] = {
	/* name, DXIO start, DXIO end, PHY start, PHY end */
	{ "P0", 0x00, 0x0f, 0x00, 0x0f },	/* IOMS0, core 0 */
	{ "G0", 0x60, 0x6f, 0x60, 0x6f },	/* IOMS0, core 1 */
	{ "P1", 0x20, 0x2f, 0x20, 0x2f },	/* IOMS1, core 0 */
	{ "G1", 0x40, 0x4f, 0x40, 0x4f },	/* IOMS1, core 1 */
	{ "P2", 0x30, 0x3f, 0x30, 0x3f },	/* IOMS2, core 0 */
	{ "G2", 0x70, 0x7f, 0x70, 0x7f },	/* IOMS2, core 1 */
	{ "P3", 0x10, 0x1f, 0x10, 0x1f },	/* IOMS3, core 0 */
	{ "G3", 0x50, 0x5f, 0x50, 0x5f }	/* IOMS3, core 1 */
};

static const zen_pcie_core_info_t genoa_bonus_maps[2] = {
	{ "P5", 0x84, 0x87, 0x84, 0x87 },	/* IOMS 0, core 2 */
	{ "P4", 0x80, 0x83, 0x80, 0x83 }	/* IOMS 2, core 2 */
};

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently follow the recommendation of the PPR.
 */
static const zen_ioapic_info_t genoa_ioapic_routes[IOAPIC_NROUTES] = {
	[0] = { .zii_group = 0x0, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[1] = { .zii_group = 0x1, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[2] = { .zii_group = 0x2, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[3] = { .zii_group = 0x3, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[4] = { .zii_group = 0x4, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[5] = { .zii_group = 0x4, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[6] = { .zii_group = 0x3, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[7] = { .zii_group = 0x2, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[8] = { .zii_group = 0x1, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[9] = { .zii_group = 0x0, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[10] = { .zii_group = 0x0, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[11] = { .zii_group = 0x1, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[12] = { .zii_group = 0x2, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[13] = { .zii_group = 0x3, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[14] = { .zii_group = 0x4, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_DABC },
	[15] = { .zii_group = 0x4, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[16] = { .zii_group = 0x3, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[17] = { .zii_group = 0x2, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[18] = { .zii_group = 0x1, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[19] = { .zii_group = 0x0, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[20] = { .zii_group = 0x0, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[21] = { .zii_group = 0x1, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[22] = { .zii_group = 0x2, .zii_map = 0x3,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[23] = { .zii_group = 0x3, .zii_map = 0x3,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
};

CTASSERT(ARRAY_SIZE(genoa_ioapic_routes) == IOAPIC_NROUTES);

const uint8_t genoa_nbif_nfunc[] = {
	[0] = GENOA_NBIF0_NFUNCS,
	[1] = GENOA_NBIF1_NFUNCS,
	[2] = GENOA_NBIF2_NFUNCS
};

const zen_nbif_info_t genoa_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS] = {
	[0] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_MPDMATF, .zni_dev = 0, .zni_func = 1 },
		{ .zni_type = ZEN_NBIF_T_NTB, .zni_dev = 0, .zni_func = 2 },
		{ .zni_type = ZEN_NBIF_T_SVNTB, .zni_dev = 0, .zni_func = 3 },
		{ .zni_type = ZEN_NBIF_T_USB, .zni_dev = 0, .zni_func = 4 },
		{ .zni_type = ZEN_NBIF_T_PSPCCP, .zni_dev = 0, .zni_func = 5 },
		{ .zni_type = ZEN_NBIF_T_ACP, .zni_dev = 0, .zni_func = 6 },
		{ .zni_type = ZEN_NBIF_T_AZ, .zni_dev = 0, .zni_func = 7 },

		{ .zni_type = ZEN_NBIF_T_SATA, .zni_dev = 1, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_SATA, .zni_dev = 1, .zni_func = 1 }
	},
	[1] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_MPDMATF, .zni_dev = 0, .zni_func = 1 },
		{ .zni_type = ZEN_NBIF_T_PVNTB, .zni_dev = 0, .zni_func = 2 },
		{ .zni_type = ZEN_NBIF_T_SVNTB, .zni_dev = 0, .zni_func = 3 }
	},
	[2] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_NTB, .zni_dev = 0, .zni_func = 1 },
		{ .zni_type = ZEN_NBIF_T_NVME, .zni_dev = 0, .zni_func = 2 }
	}
};

/*
 * How many PCIe cores does this IOMS instance have?
 * If it's an IOHUB that has a bonus core then it will have the maximum
 * number, otherwise one fewer.
 */
uint8_t
genoa_ioms_n_pcie_cores(const uint8_t iomsno)
{
	if (GENOA_IOMS_IOHUB_NUM(iomsno) == GENOA_NBIO_BONUS_IOHUB)
		return (GENOA_IOMS_MAX_PCIE_CORES);
	return (GENOA_IOMS_MAX_PCIE_CORES - 1);
}

/*
 * How many PCIe ports does this core instance have?
 * The bonus cores have a lower number of ports than the others.
 * Not all ports are necessarily enabled, and ports that are disabled may have
 * their associated bridges hidden; this is used to compute the locations of
 * register blocks that pertain to the port that may exist.
 */
uint8_t
genoa_pcie_core_n_ports(const uint8_t pcno)
{
	if (pcno == GENOA_IOMS_BONUS_PCIE_CORENO)
		return (GENOA_PCIE_CORE_BONUS_PORTS);
	return (GENOA_PCIE_CORE_MAX_PORTS);
}

const zen_pcie_core_info_t *
genoa_pcie_core_info(const uint8_t iomsno, const uint8_t coreno)
{
	uint8_t index;

	if (coreno == GENOA_IOMS_BONUS_PCIE_CORENO) {
		index = GENOA_NBIO_NUM(iomsno);
		VERIFY3U(index, <, ARRAY_SIZE(genoa_bonus_maps));
		return (&genoa_bonus_maps[index]);
	}

	index = iomsno * 2 + coreno;
	VERIFY3U(index, <, ARRAY_SIZE(genoa_lane_maps));
	return (&genoa_lane_maps[index]);
}

const zen_pcie_port_info_t *
genoa_pcie_port_info(const uint8_t coreno, const uint8_t portno)
{
	return (&genoa_pcie[coreno][portno]);
}

bool
genoa_fabric_smu_pptable_init(zen_fabric_t *fabric, void *pptable, size_t *len)
{
	const zen_iodie_t *iodie = &fabric->zf_socs[0].zs_iodies[0];
	const uint8_t maj = iodie->zi_smu_fw[0];
	const uint8_t min = iodie->zi_smu_fw[1];

	/*
	 * The format of the PP table is consistent across several SMU
	 * versions. If we encounter a version we have not verified then we
	 * panic.
	 */
	if (maj != 71 || min < 111 || min > 124) {
		cmn_err(CE_PANIC,
		    "The PP table layout for SMU version %u.%u is unknown",
		    maj, min);
		/* NOTREACHED */
	}

	genoa_pptable_v71_111_t *gpp = pptable;
	CTASSERT(sizeof (*gpp) <= MMU_PAGESIZE);
	VERIFY3U(sizeof (*gpp), <=, *len);

	memset(&gpp->gpp_cppc.gppc_thr_map, 0xff,
	    sizeof (gpp->gpp_cppc.gppc_thr_map));

	/*
	 * Explicitly disable the overclocking part of the table.
	 */
	gpp->gpp_overclock.gppo_oc_dis = 1;

	/*
	 * Set platform-specific power and current limits.
	 */
	gpp->gpp_platform_limits.gppp_tdp = oxide_board_data->obd_tdp;
	gpp->gpp_platform_limits.gppp_ppt = oxide_board_data->obd_ppt;
	gpp->gpp_platform_limits.gppp_tdc = oxide_board_data->obd_tdc;
	gpp->gpp_platform_limits.gppp_edc = oxide_board_data->obd_edc;

	*len = sizeof (*gpp);

	return (true);
}

/*
 * This is called from the common code, via an entry in the Genoa version of
 * Zen fabric ops vector. The common code is responsible for the bulk of
 * initialization; we merely fill in those bits that are microarchitecture
 * specific.
 */
void
genoa_fabric_ioms_init(zen_ioms_t *ioms)
{
	const uint8_t iomsno = ioms->zio_num;

	if (GENOA_IOMS_IOHUB_NUM(iomsno) == GENOA_NBIO_BONUS_IOHUB)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_BONUS;

	/*
	 * Genoa has a 1:1 mapping between IOHCs and IOMSs, and all IOHCs are
	 * the same type.
	 */
	ioms->zio_nbionum = GENOA_NBIO_NUM(iomsno);
	ioms->zio_iohcnum = iomsno;
	ioms->zio_iohctype = ZEN_IOHCT_LARGE;

	/*
	 * nBIFs are actually associated with the NBIO instance but we have no
	 * representation in the fabric for NBIOs. Mark the first IOMS in each
	 * NBIO as holding the nBIFs.
	 */
	if (GENOA_IOMS_IOHUB_NUM(iomsno) == 0)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_NBIF;
}


typedef enum genoa_iommul1_subunit {
	GIL1SU_IOAGR
} genoa_iommul1_subunit_t;

/*
 * Convenience functions for accessing SMN registers pertaining to a bridge.
 * These are candidates for making public if/when other code needs to manipulate
 * bridges.  There are some tradeoffs here: we don't need any of these
 * functions; callers could instead look up registers themselves, retrieve the
 * iodie by chasing back-pointers, and call zen_smn_{read,write}32()
 * themselves.  Indeed, they still can, and if there are many register accesses
 * to be made in code that materially affects performance, that is likely to be
 * preferable.  However, it has a major drawback: it requires each caller to get
 * the ordered set of instance numbers correct when constructing the register,
 * and there is little or nothing that can be done to help them.  Most of the
 * register accessors will blow up if the instance numbers are obviously out of
 * range, but there is little we can do to prevent them being given out of
 * order, for example.  Constructing incompatible struct types for each instance
 * level seems impractical.  So instead we isolate those calculations here and
 * allow callers to treat each bridge's (or other object's) collections of
 * pertinent registers opaquely.  This is probably closest to what we
 * conceptually want this to look like anyway; callers should be focused on
 * controlling the device, not on the mechanics of how to do so.  Nevertheless,
 * we do not foreclose on arbitrary SMN access if that's useful.
 *
 * We provide similar collections of functions below for other entities we
 * model in the fabric.
 */

smn_reg_t
genoa_pcie_port_reg(const zen_pcie_port_t *const port,
    const smn_reg_def_t def)
{
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_IOHCDEV_PCIE:
		reg = genoa_iohcdev_pcie_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	case SMN_UNIT_PCIE_PORT:
		reg = genoa_pcie_port_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe port",
		    def.srd_unit);
	}

	return (reg);
}

smn_reg_t
genoa_pcie_core_reg(const zen_pcie_core_t *const pc, const smn_reg_def_t def)
{
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_CORE:
		reg = genoa_pcie_core_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno);
		break;
	case SMN_UNIT_IOMMUL1:
		reg = genoa_iommul1_pcie_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe RC",
		    def.srd_unit);
	}

	return (reg);
}

/*
 * We consider the IOAGR to be part of the NBIO/IOHC/IOMS, so the IOMMUL1's
 * IOAGR block falls under the IOMS; the IOAPIC and IOMMUL2 are similar as they
 * do not (currently) have independent representation in the fabric.
 */
static smn_reg_t
genoa_ioms_reg(const zen_ioms_t *const ioms, const smn_reg_def_t def,
    const uint16_t reginst)
{
	smn_reg_t reg;
	switch (def.srd_unit) {
	case SMN_UNIT_IOAPIC:
		reg = genoa_ioapic_smn_reg(ioms->zio_iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOHC:
		reg = genoa_iohc_smn_reg(ioms->zio_iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = genoa_ioagr_smn_reg(ioms->zio_iohcnum, def, reginst);
		break;
	case SMN_UNIT_SDPMUX:
		reg = genoa_sdpmux_smn_reg(ioms->zio_nbionum, def, reginst);
		break;
	case SMN_UNIT_SST:
		reg = genoa_sst_smn_reg(ioms->zio_nbionum, def, reginst);
		break;
	case SMN_UNIT_IOMMUL1: {
		/*
		 * Confusingly, this pertains to the IOMS, not the NBIF; there
		 * is only one unit per IOMS, not one per NBIF.  Because.  To
		 * accommodate this, we need to treat the reginst as an
		 * enumerated type to distinguish the sub-units.  As gross as
		 * this is, it greatly reduces triplication of register
		 * definitions.  There is no way to win here.
		 */
		const genoa_iommul1_subunit_t su =
		    (const genoa_iommul1_subunit_t)reginst;
		switch (su) {
		case GIL1SU_IOAGR:
			reg = genoa_iommul1_ioagr_smn_reg(ioms->zio_iohcnum,
			    def, 0);
			break;
		default:
			cmn_err(CE_PANIC, "invalid IOMMUL1 subunit %d", su);
			break;
		}
		break;
	}
	case SMN_UNIT_IOMMUL2:
		reg = genoa_iommul2_smn_reg(ioms->zio_iohcnum, def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
		    def.srd_unit);
	}
	return (reg);
}

static smn_reg_t
genoa_nbif_reg(const zen_nbif_t *const nbif, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const zen_ioms_t *ioms = nbif->zn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF:
		reg = genoa_nbif_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, reginst);
		break;
	case SMN_UNIT_NBIF_ALT:
		reg = genoa_nbif_alt_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF",
		    def.srd_unit);
	}

	return (reg);
}

static smn_reg_t
genoa_nbif_func_reg(const zen_nbif_func_t *const func, const smn_reg_def_t def)
{
	const zen_nbif_t *nbif = func->znf_nbif;
	const zen_ioms_t *ioms = nbif->zn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF_FUNC:
		reg = genoa_nbif_func_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, func->znf_dev, func->znf_func);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF func",
		    def.srd_unit);
	}

	return (reg);
}

void
genoa_fabric_init_tom(zen_ioms_t *ioms, uint64_t tom, uint64_t tom2,
    uint64_t tom3)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * This register is a little funky. Bit 32 of the address has to be
	 * specified in bit 0. Otherwise, bits 31:23 are the limit.
	 */
	val = pci_getl_func(ioms->zio_pci_busno, 0, 0, IOHC_TOM);
	if (bitx64(tom, 32, 32) != 0)
		val = IOHC_TOM_SET_BIT32(val, 1);

	val = IOHC_TOM_SET_TOM(val, bitx64(tom, 31, 23));
	pci_putl_func(ioms->zio_pci_busno, 0, 0, IOHC_TOM, val);

	if (tom2 == 0)
		return;

	/*
	 * Write the upper register before the lower so we don't accidentally
	 * enable it in an incomplete fashion.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_DRAM_TOM2_HI, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_HI_SET_TOM2(val, bitx64(tom2, 40, 32));
	zen_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOHC_DRAM_TOM2_LOW, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_LOW_SET_EN(val, 1);
	val = IOHC_DRAM_TOM2_LOW_SET_TOM2(val, bitx64(tom2, 31, 23));
	zen_ioms_write(ioms, reg, val);

	if (tom3 == 0)
		return;

	reg = genoa_ioms_reg(ioms, D_IOHC_DRAM_TOM3, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM3_SET_EN(val, 1);
	val = IOHC_DRAM_TOM3_SET_LIMIT(val, bitx64(tom3, 51, 22));
	zen_ioms_write(ioms, reg, val);
}

/*
 * We want to disable VGA and send all downstream accesses to its address range
 * to DRAM just as we do from the cores. This requires clearing
 * IOHC::NB_PCI_ARB[VGA_HOLE]; for reasons unknown, the default here is
 * different from the other settings that typically default to VGA-off. The
 * rest of this register has nothing to do with decoding and we leave its
 * contents alone.
 */
void
genoa_fabric_disable_vga(zen_ioms_t *ioms)
{
	uint32_t val;

	val = pci_getl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_PCI_ARB);
	val = IOHC_NB_PCI_ARB_SET_VGA_HOLE(val, IOHC_NB_PCI_ARB_VGA_HOLE_RAM);
	pci_putl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_PCI_ARB, val);
}

void
genoa_fabric_pcie_refclk(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_REFCLK_MODE, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_REFCLK_MODE_SET_27MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_25MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_100MHZ(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
genoa_fabric_set_pci_to(zen_ioms_t *ioms, uint16_t limit, uint16_t delay)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_PCIE_CRS_COUNT, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_PCIE_CRS_COUNT_SET_LIMIT(val, limit);
	val = IOHC_PCIE_CRS_COUNT_SET_DELAY(val, delay);
	zen_ioms_write(ioms, reg, val);
}

/*
 * XXX We're using lazy defaults of what the system default has historically
 * been here for some of these. We should test and forcibly disable in
 * hardware. Probably want to manipulate IOHC::PCIE_VDM_CNTL2 at some point to
 * better figure out the VDM story. XXX
 * Also, ARI enablement is being done earlier than otherwise because we want to
 * only touch this reg in one place if we can.
 */
void
genoa_fabric_iohc_features(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_FCTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_FCTL_SET_ARI(val, 1);
	/* XXX Wants to be IOHC_FCTL_P2P_DISABLE? */
	val = IOHC_FCTL_SET_P2P(val, IOHC_FCTL_P2P_DROP_NMATCH);
	zen_ioms_write(ioms, reg, val);
}

void
genoa_fabric_iohc_bus_num(zen_ioms_t *ioms, uint8_t busno)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOHC_BUS_NUM_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_BUS_NUM_CTL_SET_SEGMENT(val, 0);
	val = IOHC_BUS_NUM_CTL_SET_EN(val, 1);
	val = IOHC_BUS_NUM_CTL_SET_BUS(val, busno);
	zen_ioms_write(ioms, reg, val);
}

void
genoa_fabric_ioms_iohc_disable_unused_pcie_bridges(zen_ioms_t *ioms)
{
	uint32_t val;

	if (GENOA_IOMS_IOHUB_NUM(ioms->zio_num) == GENOA_NBIO_BONUS_IOHUB)
		return;

	const smn_reg_t smn_regs[4] = {
		IOHCDEV_PCIE_BRIDGE_CTL(ioms->zio_num, 2, 0),
		IOHCDEV_PCIE_BRIDGE_CTL(ioms->zio_num, 2, 1),
		IOHCDEV_PCIE_BRIDGE_CTL(ioms->zio_num, 2, 2),
		IOHCDEV_PCIE_BRIDGE_CTL(ioms->zio_num, 2, 3),
	};

	for (uint_t i = 0; i < ARRAY_SIZE(smn_regs); i++) {
		val = zen_ioms_read(ioms, smn_regs[i]);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 1);
		val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 1);
		zen_ioms_write(ioms, smn_regs[i], val);
	}
}

void
genoa_fabric_iohc_fch_link(zen_ioms_t *ioms, bool has_fch)
{
	smn_reg_t reg;

	reg = genoa_ioms_reg(ioms, D_IOHC_SB_LOCATION, 0);
	if (has_fch) {
		smn_reg_t iommureg;
		uint32_t val;

		val = zen_ioms_read(ioms, reg);
		iommureg = genoa_ioms_reg(ioms, D_IOMMUL1_SB_LOCATION,
		    GIL1SU_IOAGR);
		zen_ioms_write(ioms, iommureg, val);
		iommureg = genoa_ioms_reg(ioms, D_IOMMUL2_SB_LOCATION, 0);
		zen_ioms_write(ioms, iommureg, val);
	} else {
		zen_ioms_write(ioms, reg, 0);
	}
}

void
genoa_fabric_iohc_arbitration(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Start with IOHC burst related entries. These are always the same
	 * across every entity. The value used for the actual time entries just
	 * varies.
	 */
	for (uint_t i = 0; i < IOHC_SION_MAX_ENTS; i++) {
		uint32_t tsval;

		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);

		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);

		switch (i) {
		case 0:
		case 1:
		case 2:
			tsval = IOHC_SION_CLIREQ_TIME_0_2_VAL;
			break;
		case 3:
		case 4:
			tsval = IOHC_SION_CLIREQ_TIME_3_4_VAL;
			break;
		case 5:
			tsval = IOHC_SION_CLIREQ_TIME_5_VAL;
			break;
		default:
			continue;
		}

		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_TIME_LOW, i);
		zen_ioms_write(ioms, reg, tsval);
		reg = genoa_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_TIME_HI, i);
		zen_ioms_write(ioms, reg, tsval);
	}

	/*
	 * Next on our list is the IOAGR. While there are 5 entries, only 4 are
	 * ever set it seems.
	 */
	for (uint_t i = 0; i < 4; i++) {
		uint32_t tsval;

		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);

		switch (i) {
		case 0 ... 2:
			tsval = IOAGR_SION_CLIREQ_TIME_0_2_VAL;
			break;
		case 3:
			tsval = IOAGR_SION_CLIREQ_TIME_3_VAL;
			break;
		default:
			continue;
		}

		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_TIME_LOW, i);
		zen_ioms_write(ioms, reg, tsval);
		reg = genoa_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_TIME_HI, i);
		zen_ioms_write(ioms, reg, tsval);
	}

	/*
	 * Finally, the SDPMUX variant. There are only two SDPMUX instances,
	 * one on IOHUB0 in each NBIO.
	 */
	if (GENOA_IOMS_IOHUB_NUM(ioms->zio_num) == 0) {
		const uint_t sdpmux = GENOA_NBIO_NUM(ioms->zio_num);

		for (uint_t i = 0; i < SDPMUX_SION_MAX_ENTS; i++) {
			reg = SDPMUX_SION_S0_CLIREQ_BURST_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
			reg = SDPMUX_SION_S0_CLIREQ_BURST_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

			reg = SDPMUX_SION_S1_CLIREQ_BURST_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
			reg = SDPMUX_SION_S1_CLIREQ_BURST_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

			reg = SDPMUX_SION_S0_RDRSP_BURST_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
			reg = SDPMUX_SION_S0_RDRSP_BURST_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);

			reg = SDPMUX_SION_S1_RDRSP_BURST_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);
			reg = SDPMUX_SION_S1_RDRSP_BURST_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_RDRSP_BURST_VAL);

			reg = SDPMUX_SION_S0_CLIREQ_TIME_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_TIME_VAL);
			reg = SDPMUX_SION_S0_CLIREQ_TIME_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_TIME_VAL);

			reg = SDPMUX_SION_S1_CLIREQ_TIME_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, 0);
			reg = SDPMUX_SION_S1_CLIREQ_TIME_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, 0);
		}
	}

	/*
	 * XXX We probably don't need this since we don't have USB. But
	 * until we have things working and can experiment, hard to
	 * say. If someone were to use the bus, probably something we
	 * need to consider.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_USB_QOS_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_USB_QOS_CTL_SET_UNID1_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID1_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID1_ID(val, 0x30);
	val = IOHC_USB_QOS_CTL_SET_UNID0_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID0_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID0_ID(val, 0x2f);
	zen_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOHC_QOS_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_QOS_CTL_SET_VC7_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC6_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC5_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC4_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC3_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC2_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC1_PRI(val, 0);
	val = IOHC_QOS_CTL_SET_VC0_PRI(val, 0);
	zen_ioms_write(ioms, reg, val);
}

void
genoa_fabric_nbif_arbitration(zen_nbif_t *nbif)
{
	smn_reg_t reg;

	reg = genoa_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT2, 0);
	zen_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);
	reg = genoa_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT3, 0);
	zen_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);
}

/*
 * This bit of initialization is both strange and not very well documented.
 */
void
genoa_fabric_nbif_syshub_dma(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * This register is only programmed on NBIF0
	 */
	if (nbif->zn_num > 0)
		return;

	reg = genoa_nbif_reg(nbif, D_NBIF_ALT_BGEN_BYP_SOC, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW0(val, 1);
	val = NBIF_ALT_BGEN_BYP_SOC_SET_DMA_SW1(val, 1);
	zen_nbif_write(nbif, reg, val);
}

void
genoa_fabric_iohc_clock_gating(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	const smn_reg_def_t iohc_regs[] = {
		D_IOHC_GCG_LCLK_CTL0,
		D_IOHC_GCG_LCLK_CTL1,
		D_IOHC_GCG_LCLK_CTL2
	};

	for (uint_t i = 0; i < ARRAY_SIZE(iohc_regs); i++) {
		reg = genoa_ioms_reg(ioms, iohc_regs[i], 0);
		val = zen_ioms_read(ioms, reg);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK9(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK8(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK7(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK6(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK5(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK4(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK3(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK2(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK1(val, 0);
		val = IOHC_GCG_LCLK_CTL_SET_SOCLK0(val, 0);
		zen_ioms_write(ioms, reg, val);
	}

	reg = genoa_ioms_reg(ioms, D_IOAGR_GCG_LCLK_CTL0, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK9(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK8(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK7(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK6(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK5(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK4(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK3(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK2(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK1(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK0(val, 0);
	zen_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOAGR_GCG_LCLK_CTL1, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK3(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK2(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK1(val, 0);
	val = IOAGR_GCG_LCLK_CTL_SET_SOCLK0(val, 0);
	zen_ioms_write(ioms, reg, val);

	const smn_reg_def_t sdpmux_regs[] = {
		D_SDPMUX_GCG_LCLK_CTL0,
		D_SDPMUX_GCG_LCLK_CTL1
	};

	for (uint_t i = 0; i < ARRAY_SIZE(sdpmux_regs); i++) {
		reg = genoa_ioms_reg(ioms, sdpmux_regs[i], 0);
		val = zen_ioms_read(ioms, reg);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK9(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK8(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK7(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK6(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK5(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK4(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK3(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK2(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK1(val, 0);
		val = SDPMUX_GCG_LCLK_CTL_SET_SOCLK0(val, 0);
		zen_ioms_write(ioms, reg, val);
	}

	/* Only NBIO1 has a bonus SST instance */
	const uint16_t sstcnt =
	    (ioms->zio_nbionum == GENOA_NBIO_BONUS_SST) ? 2 : 1;

	for (uint16_t i = 0; i < sstcnt; i++) {
		reg = genoa_ioms_reg(ioms, D_SST_CLOCK_CTL, i);
		val = zen_ioms_read(ioms, reg);
		val = SST_CLOCK_CTL_SET_RXCLKGATE_EN(val, 1);
		val = SST_CLOCK_CTL_SET_TXCLKGATE_EN(val, 1);
		val = SST_CLOCK_CTL_SET_PCTRL_IDLE_TIME(val,
		    SST_CLOCK_CTL_PCTRL_IDLE_TIME);
		zen_ioms_write(ioms, reg, val);

		reg = genoa_ioms_reg(ioms, D_SST_SION_WRAP_CFG_GCG_LCLK_CTL, i);
		val = zen_ioms_read(ioms, reg);
		val = SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK4(val, 1);
		zen_ioms_write(ioms, reg, val);
	}
}

void
genoa_fabric_nbif_clock_gating(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_nbif_reg(nbif, D_NBIF_MGCG_CTL_LCLK, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_MGCG_CTL_LCLK_SET_EN(val, 1);
	zen_nbif_write(nbif, reg, val);

	/* LCLK deep sleep must be enabled in order for IOAGR to go idle. */
	reg = genoa_nbif_reg(nbif, D_NBIF_DS_CTL_LCLK, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_DS_CTL_LCLK_SET_EN(val, 1);
	zen_nbif_write(nbif, reg, val);

	/*
	 * There is only one of these register instances per NBIO.
	 */
	if (nbif->zn_num == 0) {
		reg = genoa_nbif_reg(nbif, D_NBIF_ALT_SION_CTL, 0);
		val = zen_nbif_read(nbif, reg);

		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK9(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK8(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK7(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK6(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK5(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK4(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK3(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK2(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK1(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL0_SOCLK0(val, 0);

		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK9(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK8(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK7(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK6(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK5(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK4(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK3(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK2(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK1(val, 0);
		val = NBIF_ALT_SION_CTL_SET_CTL1_SOCLK0(val, 0);

		zen_nbif_write(nbif, reg, val);
	}

	/*
	 * These registers are weird SYSHUB and nBIF crossovers in the
	 * alternate space, where there are only two nBIF instances.
	 */
	if (nbif->zn_num < 2) {
		reg = genoa_nbif_reg(nbif, D_NBIF_ALT_NGDC_MGCG_CTL, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_NGDC_MGCG_CTL_SET_EN(val, 1);
		zen_nbif_write(nbif, reg, val);

		reg = genoa_nbif_reg(nbif, D_NBIF_ALT_MGCG_CTL_SCLK, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_MGCG_CTL_SCLK_SET_EN(val, 1);
		zen_nbif_write(nbif, reg, val);

		reg = genoa_nbif_reg(nbif, D_NBIF_ALT_DS_CTL_SOCCLK, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_DS_CTL_SOCCLK_SET_EN(val, 1);
		zen_nbif_write(nbif, reg, val);

		/* The SHUBCLK registers only exist on nBIF0 */
		if (nbif->zn_num == 0) {
			reg = genoa_nbif_reg(
			    nbif, D_NBIF_ALT_MGCG_CTL_SHCLK, 0);
			val = zen_nbif_read(nbif, reg);
			val = NBIF_ALT_MGCG_CTL_SHCLK_SET_EN(val, 1);
			zen_nbif_write(nbif, reg, val);

			reg = genoa_nbif_reg(nbif,
			    D_NBIF_ALT_DS_CTL_SHUBCLK, 0);
			val = zen_nbif_read(nbif, reg);
			val = NBIF_ALT_DS_CTL_SHUBCLK_SET_EN(val, 1);
			zen_nbif_write(nbif, reg, val);
		}
	}

	if (nbif->zn_num == 2) {
		reg = genoa_nbif_reg(nbif, D_NBIF_PG_MISC_CTL0, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PG_MISC_CTL0_SET_LDMASK(val, 0);
		zen_nbif_write(nbif, reg, val);
	}
}

void
genoa_fabric_ioapic_clock_gating(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_ioms_reg(ioms, D_IOAPIC_GCG_LCLK_CTL0, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK2(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK1(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK0(val, 0);
	zen_ioms_write(ioms, reg, val);
}

/*
 * We need to initialize each IOAPIC as there is one per IOMS. First we
 * initialize the interrupt routing table. This is used to mux the various
 * legacy INTx interrupts and the bridge's interrupt to a given location. This
 * follows from the PPR.
 *
 * After that we need to go through and program the feature register for the
 * IOAPIC and its address. Because there is one IOAPIC per IOMS, one has to be
 * elected the primary and the rest, secondary. This is done based on which
 * IOMS has the FCH.
 */
void
genoa_fabric_ioapic(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	for (uint_t i = 0; i < ARRAY_SIZE(genoa_ioapic_routes); i++) {
		reg = genoa_ioms_reg(ioms, D_IOAPIC_ROUTE, i);
		val = zen_ioms_read(ioms, reg);

		val = IOAPIC_ROUTE_SET_BRIDGE_MAP(val,
		    genoa_ioapic_routes[i].zii_map);
		val = IOAPIC_ROUTE_SET_INTX_SWIZZLE(val,
		    genoa_ioapic_routes[i].zii_swiz);
		val = IOAPIC_ROUTE_SET_INTX_GROUP(val,
		    genoa_ioapic_routes[i].zii_group);

		zen_ioms_write(ioms, reg, val);
	}

	/*
	 * The address registers are in the IOHC while the feature registers
	 * are in the IOAPIC SMN space. To ensure that the other IOAPICs can't
	 * be enabled with reset addresses, we instead lock them.
	 * XXX Should we lock primary?
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_HI, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val,
		    bitx64(ZEN_PHYSADDR_IOHC_IOAPIC, 47, 32));
	} else {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val, 0);
	}
	zen_ioms_write(ioms, reg, val);

	reg = genoa_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_LO, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_LO_SET_ADDR(val,
		    bitx64(ZEN_PHYSADDR_IOHC_IOAPIC, 31, 8));
		val = IOHC_IOAPIC_ADDR_LO_SET_LOCK(val, 0);
		val = IOHC_IOAPIC_ADDR_LO_SET_EN(val, 1);
	} else {
		val = IOHC_IOAPIC_ADDR_LO_SET_ADDR(val, 0);
		val = IOHC_IOAPIC_ADDR_LO_SET_LOCK(val, 1);
		val = IOHC_IOAPIC_ADDR_LO_SET_EN(val, 0);
	}
	zen_ioms_write(ioms, reg, val);

	/*
	 * Every IOAPIC requires that we enable 8-bit addressing and that it be
	 * able to generate interrupts to the FCH. The most important bit here
	 * is the secondary bit which determines whether or not this IOAPIC is
	 * subordinate to another.
	 */
	reg = genoa_ioms_reg(ioms, D_IOAPIC_FEATURES, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOAPIC_FEATURES_SET_SECONDARY(val, 0);
	} else {
		val = IOAPIC_FEATURES_SET_SECONDARY(val, 1);
	}
	val = IOAPIC_FEATURES_SET_FCH(val, 1);
	val = IOAPIC_FEATURES_SET_ID_EXT(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
genoa_fabric_hide_bridge(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * All bridges need to be visible before we attempt to
	 * configure MPIO.
	 */
	reg = genoa_pcie_port_reg(port, D_IOHCDEV_PCIE_BRIDGE_CTL);
	val = zen_pcie_port_read(port, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 1);
	zen_pcie_port_write(port, reg, val);
}

void
genoa_fabric_unhide_bridge(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * All bridges need to be visible before we attempt to
	 * configure MPIO.
	 */
	reg = genoa_pcie_port_reg(port, D_IOHCDEV_PCIE_BRIDGE_CTL);
	val = zen_pcie_port_read(port, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 0);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 0);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 0);
	zen_pcie_port_write(port, reg, val);
}

void
genoa_fabric_nbif_init(zen_nbif_t *nbif)
{
	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++) {
		zen_nbif_func_t *func = &nbif->zn_funcs[funcno];

		/* PM_STATUS is enabled for USB devices, SATA, etc. */
		if (func->znf_type == ZEN_NBIF_T_USB ||
		    func->znf_type == ZEN_NBIF_T_SATA ||
		    func->znf_type == ZEN_NBIF_T_MPDMATF) {
			func->znf_flags |= ZEN_NBIF_F_PMSTATUS_EN;
		}

		/*
		 * TPH CPLR is additionally enabled for USB devices and for the
		 * first SATA function.
		 */
		if (func->znf_type == ZEN_NBIF_T_USB ||
		    (func->znf_type == ZEN_NBIF_T_SATA && func->znf_func < 1)) {
			func->znf_flags |= ZEN_NBIF_F_TPH_CPLR_EN;
		}
	}
}

/*
 * Go through and configure and set up devices and functions. In particular we
 * need to go through and set up the following:
 *
 *  o Strap bits that determine whether or not the function is enabled
 *  o Enabling the interrupts of corresponding functions
 *  o Setting up specific PCI device straps around multi-function, FLR, poison
 *    control, TPH settings, etc.
 */
void
genoa_fabric_nbif_dev_straps(zen_nbif_t *nbif)
{
	const uint8_t iohcno = nbif->zn_ioms->zio_iohcnum;
	smn_reg_t intrreg, reg;
	uint32_t intr, val;

	intrreg = genoa_nbif_reg(nbif, D_NBIF_INTR_LINE_EN, 0);
	intr = zen_nbif_read(nbif, intrreg);
	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++) {
		smn_reg_t strapreg;
		uint32_t strap;
		zen_nbif_func_t *func = &nbif->zn_funcs[funcno];

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP0);
		strap = zen_nbif_func_read(func, strapreg);

		if (func->znf_type == ZEN_NBIF_T_DUMMY) {
			/*
			 * AMD sources suggest that the device ID for the dummy
			 * device should be changed from the reset values of
			 * 0x14ac (nBIF0) and 0x14c2 (nBIF2) to 0x14dc which is
			 * the ID for SDXI. This doesn't seem to make sense
			 * (and doesn't take even if we try) so we just skip
			 * any additional configuration for the dummy device.
			 */
			continue;
		} else if ((func->znf_flags & ZEN_NBIF_F_ENABLED) != 0) {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 1);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->znf_dev, func->znf_func, 1);

			/*
			 * Strap enabled SATA devices to what AMD asks for.
			 */
			if (func->znf_type == ZEN_NBIF_T_SATA) {
				strap = NBIF_FUNC_STRAP0_SET_MAJ_REV(strap, 7);
				strap = NBIF_FUNC_STRAP0_SET_MIN_REV(strap, 1);
			}
		} else {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 0);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->znf_dev, func->znf_func, 0);
		}

		zen_nbif_func_write(func, strapreg, strap);

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP2);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP2_SET_ACS_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_ACS_EN) ? 1 : 0);
		strap = NBIF_FUNC_STRAP2_SET_AER_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_AER_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP3);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP3_SET_PM_STATUS_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_PMSTATUS_EN) ? 1 : 0);
		strap = NBIF_FUNC_STRAP3_SET_PANF_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_PANF_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP4);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP4_SET_FLR_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_FLR_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP7);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP7_SET_TPH_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_TPH_CPLR_EN) ? 1 : 0);
		strap = NBIF_FUNC_STRAP7_SET_TPH_CPLR_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_TPH_CPLR_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);
	}

	zen_nbif_write(nbif, intrreg, intr);

	/*
	 * Each nBIF has up to two devices on them, though not all of them
	 * seem to be used. It's suggested that we enable completion timeouts
	 * and TLP processing hints completer support on all of them.
	 */
	for (uint8_t devno = 0; devno < GENOA_NBIF_MAX_PORTS; devno++) {
		reg = genoa_nbif_reg(nbif, D_NBIF_PORT_STRAP3, devno);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP3_SET_COMP_TO(val, 1);
		zen_nbif_write(nbif, reg, val);

		reg = genoa_nbif_reg(nbif, D_NBIF_PORT_STRAP6, devno);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP6_SET_TPH_CPLR_EN(val,
		    NBIF_PORT_STRAP6_TPH_CPLR_SUP);
		zen_nbif_write(nbif, reg, val);
	}

	/*
	 * For the root port functions within nBIF, program the B/D/F values.
	 */
	ASSERT3U(iohcno, <, ARRAY_SIZE(genoa_pcie_int_ports));
	const zen_iohc_nbif_ports_t *ports = &genoa_pcie_int_ports[iohcno];
	for (uint8_t i = 0; i < ports->zinp_count; i++) {
		const zen_pcie_port_info_t *port = &ports->zinp_ports[i];

		reg = genoa_nbif_reg(nbif, D_NBIF_PORT_STRAP7, i);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP7_SET_BUS(val,
		    nbif->zn_ioms->zio_pci_busno);
		val = NBIF_PORT_STRAP7_SET_DEV(val, port->zppi_dev);
		val = NBIF_PORT_STRAP7_SET_FUNC(val, port->zppi_func);
		zen_nbif_write(nbif, reg, val);
	}
}


/*
 * These are the tile ID mappings that firmware uses specifically for hotplug.
 */
typedef enum genoa_pci_hotplug_tile_id {
	GENOA_HOTPLUG_TILE_P0 = 0,
	GENOA_HOTPLUG_TILE_P1,
	GENOA_HOTPLUG_TILE_P2,
	GENOA_HOTPLUG_TILE_P3,
	GENOA_HOTPLUG_TILE_G0,
	GENOA_HOTPLUG_TILE_G1,
	GENOA_HOTPLUG_TILE_G2,
	GENOA_HOTPLUG_TILE_G3,
} genoa_pci_hotplug_tile_id_t;

/*
 * Translates from our internal OXIO tile identifier to an integer understood by
 * Genoa's hotplug firmware.
 */
uint8_t
genoa_fabric_hotplug_tile_id(const oxio_engine_t *oxio)
{
	VERIFY3P(oxio->oe_type, ==, OXIO_ENGINE_T_PCIE);
	ASSERT3U(oxio->oe_tile, <=, GENOA_HOTPLUG_TILE_G3);

	switch (oxio->oe_tile) {
	case OXIO_TILE_G0:
		return (GENOA_HOTPLUG_TILE_G0);
	case OXIO_TILE_P0:
		return (GENOA_HOTPLUG_TILE_P0);
	case OXIO_TILE_G1:
		return (GENOA_HOTPLUG_TILE_G1);
	case OXIO_TILE_P1:
		return (GENOA_HOTPLUG_TILE_P1);
	case OXIO_TILE_G2:
		return (GENOA_HOTPLUG_TILE_G2);
	case OXIO_TILE_P2:
		return (GENOA_HOTPLUG_TILE_P2);
	case OXIO_TILE_G3:
		return (GENOA_HOTPLUG_TILE_G3);
	case OXIO_TILE_P3:
		return (GENOA_HOTPLUG_TILE_P3);
	case OXIO_TILE_P4:
	case OXIO_TILE_P5:
		panic("PCIe Tile ID 0x%x (%s) cannot be used with hotplug",
		    oxio->oe_tile, oxio->oe_tile == OXIO_TILE_P4 ? "P4": "P5");
	default:
		panic("cannot map invalid PCIe Tile ID 0x%x", oxio->oe_tile);
	}
}

/*
 * Prepares a hotplug-capable bridge by,
 *
 * - Setting the slot's actual number in PCIe and in a secondary SMN location.
 * - Setting state machine control bits in the PCIe IP to ensure we don't
 *   enter loopback mode or other degenerate cases
 * - Enabling support for power faults
 */
void
genoa_fabric_hotplug_port_init(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	ASSERT3U(port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG, !=, 0);

	/*
	 * Set the hotplug slot information in the PCIe IP, presumably so that
	 * it'll do something useful for the SMU/MPIO.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_HP_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_HP_CTL_SET_SLOT(val, port->zpp_slotno);
	val = PCIE_PORT_HP_CTL_SET_ACTIVE(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * This register appears to ensure that we don't remain in the detect
	 * state machine state.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL5);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL5_SET_WAIT_DETECT(val, 0);
	zen_pcie_port_write(port, reg, val);

	/*
	 * This bit is documented to cause the LC to disregard most training
	 * control bits in received TS1 and TS2 ordered sets.  Training control
	 * bits include Compliance Receive, Hot Reset, Link Disable, Loopback,
	 * and Disable Scrambling.  As all our ports are Downstream Ports, we
	 * are required to ignore most of these; the PCIe standard still
	 * requires us to act on Compliance Receive and the PPR implies that we
	 * do even if this bit is set (the other four are listed as being
	 * ignored).
	 *
	 * However... an AMD firmware bug for which we have no additional
	 * information implies that this does more than merely ignore training
	 * bits in received TSx, and also makes the Secondary Bus Reset bit in
	 * the Bridge Control register not work or work incorrectly.  That is,
	 * there may be a hardware bug that causes this bit to have unintended
	 * and undocumented side effects that also violate the standard.  In our
	 * case, we're going to set this anyway, because there is nothing
	 * anywhere in illumos that uses the Secondary Bus Reset feature and it
	 * seems much more important to be sure that our downstream ports can't
	 * be disabled or otherwise affected by a misbehaving or malicious
	 * downstream device that might set some of these bits.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_TRAINBITS_DIS(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure that power faults can actually work (in theory).
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_PCTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_PCTL_SET_PWRFLT_EN(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Indicate that the slot supports disabling of in-band presence for
	 * determining PD state/component presence.
	 */
	val = pci_getl_func(port->zpp_core->zpc_ioms->zio_pci_busno,
	    port->zpp_device, port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP2);
	val |= PCIE_SLOTCAP2_INB_PRES_DET_DIS_SUP;
	pci_putl_func(port->zpp_core->zpc_ioms->zio_pci_busno,
	    port->zpp_device, port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP2, val);
}

/*
 * Unblocks training on the given port by clearing the corresponding
 * HOLD_TRAINING bit in the associated PCIe core's PCIECORE::SWRST_CONTROL_6.
 */
void
genoa_fabric_hotplug_port_unblock_training(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;
	zen_pcie_core_t *pc = port->zpp_core;

	ASSERT3U(port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG, !=, 0);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_SWRST_CTL6);
	val = zen_pcie_core_read(pc, reg);
	val = bitset32(val, port->zpp_portno, port->zpp_portno, 0);
	zen_pcie_core_write(pc, reg, val);
}

/*
 * Prepares the PCIe core for hotplug by ensuring that presence detect mux
 * select is set to a logical "OR" of in-band and out-of-band PD signals.
 */
void
genoa_fabric_hotplug_core_init(zen_pcie_core_t *pc)
{
	smn_reg_t reg;
	uint32_t val;

	ASSERT3U(pc->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG, !=, 0);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PRES);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PRES_SET_MODE(val, PCIE_CORE_PRES_MODE_OR);
	zen_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_COMMON_AER_MASK);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_COMMON_AER_MASK_SET_SD_PD(val, 1);
	val = PCIE_CORE_COMMON_AER_MASK_SET_SD_DPC(val, 0);
	val = PCIE_CORE_COMMON_AER_MASK_SET_SD_HP_OFF(val, 0);
	val = PCIE_CORE_COMMON_AER_MASK_SET_SD_HP_SURP(val, 0);
	val = PCIE_CORE_COMMON_AER_MASK_SET_SD_PME_HS(val, 0);
	val = PCIE_CORE_COMMON_AER_MASK_SET_SD_PME_OFF(val, 0);
	zen_pcie_core_write(pc, reg, val);
}

/*
 * The Turin version of flags sent in the hotplug start RPC includes more data
 * than for either Milan or Genoa; for both of the other two, we mostly punt
 * since, in the Oxide architecture, the arguments are always zero.  Here, we
 * try to provide a type that encodes some of the semantics of the various bits.
 * The widths of these fields are mostly deduced from examination of AGESA.
 */
bool
genoa_fabric_hotplug_start(zen_iodie_t *iodie)
{
	return (zen_mpio_rpc_start_hotplug(iodie, 0));
}

/*
 * Do everything else required to finish configuring the nBIF and get the PCIe
 * engine up and running.
 */
void
genoa_fabric_pcie(zen_fabric_t *fabric)
{
	zen_mpio_pcie_init(fabric);
}

void
genoa_iohc_enable_nmi(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t v;

	/*
	 * On reset, the NMI destination in IOHC::IOHC_INTR_CNTL is set to
	 * 0xff.  We (emphatically) do not want any AP to get an NMI when we
	 * first power it on, so we deliberately set all NMI destinations to
	 * be the BSP.  Note that we do will not change this, even after APs
	 * are up (that is, NMIs will always go to the BSP):  changing it has
	 * non-zero runtime risk (see the comment above our actual enabling
	 * of NMI, below) and does not provide any value for our use case of
	 * NMI.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_INTR_CTL, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_INTR_CTL_SET_NMI_DEST_CTRL(v, 0);
	zen_ioms_write(ioms, reg, v);

	if ((zen_ioms_flags(ioms) & ZEN_IOMS_F_HAS_FCH) != 0) {
		reg = genoa_ioms_reg(ioms, D_IOHC_PIN_CTL, 0);
		v = zen_ioms_read(ioms, reg);
		v = IOHC_PIN_CTL_SET_MODE_NMI(v);
		zen_ioms_write(ioms, reg, v);
	}

	/*
	 * Once we enable this, we can immediately take an NMI if it's
	 * currently asserted.  We want to do this last and clear out of here
	 * as quickly as possible:  this is all a bit dodgy, but the NMI
	 * handler itself needs to issue an SMN write to indicate EOI -- and
	 * if it finds that SMN-related locks are held, we will panic.  To
	 * reduce the likelihood of that, we are going to enable NMI and
	 * skedaddle...
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_MISC_RAS_CTL, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_MISC_RAS_CTL_SET_NMI_SYNCFLOOD_EN(v, 1);
	zen_ioms_write(ioms, reg, v);
}

void
genoa_iohc_nmi_eoi(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t v;

	reg = genoa_ioms_reg(ioms, D_IOHC_FCTL2, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_FCTL2_GET_NMI(v);
	if (v != 0) {
		/*
		 * We have no ability to handle the other bits here, as
		 * those conditions may not have resulted in an NMI.  Clear only
		 * the bit whose condition we have handled.
		 */
		zen_ioms_write(ioms, reg, v);
		reg = genoa_ioms_reg(ioms, D_IOHC_INTR_EOI, 0);
		v = IOHC_INTR_EOI_SET_NMI(0);
		zen_ioms_write(ioms, reg, v);
	}
}

/*
 * These PCIe straps need to be set after mapping is done, but before link
 * training has started. While we do not understand in detail what all of these
 * registers do, we've split this broadly into 2 categories:
 * 1) Straps where:
 *     a) the defaults in hardware seem to be reasonable given our (sometimes
 *     limited) understanding of their function
 *     b) are not features/parameters that we currently care specifically about
 *     one way or the other
 *     c) and we are currently ok with the defaults changing out from underneath
 *     us on different hardware revisions unless proven otherwise.
 * or 2) where:
 *     a) We care specifically about a feature enough to ensure that it is set
 *     (e.g. AERs) or purposefully disabled (e.g. I2C_DBG_EN)
 *     b) We are not ok with these changing based on potentially different
 *     defaults set in different hardware revisions
 * For 1), we've chosen to leave them based on whatever the hardware has chosen
 * as the default, while all the straps detailed underneath fall into category
 * 2. Note that this list is by no means definitive, and will almost certainly
 * change as our understanding of what we require from the hardware evolves.
 */

/*
 * PCIe Straps that we unconditionally set to 1
 */
static const uint32_t genoa_pcie_strap_enable[] = {
	GENOA_STRAP_PCIE_MSI_EN,
	GENOA_STRAP_PCIE_AER_EN,
	GENOA_STRAP_PCIE_GEN2_FEAT_EN,
	GENOA_STRAP_PCIE_NPEM_EN,
	GENOA_STRAP_PCIE_CPL_TO_EN,	/* We want completion timeouts */
	GENOA_STRAP_PCIE_TPH_EN,
	GENOA_STRAP_PCIE_MULTI_FUNC_EN,
	GENOA_STRAP_PCIE_DPC_EN,
	GENOA_STRAP_PCIE_ARI_EN,
	GENOA_STRAP_PCIE_PL_16G_EN,
	GENOA_STRAP_PCIE_PL_32G_EN,
	GENOA_STRAP_PCIE_LANE_MARGIN_EN,
	GENOA_STRAP_PCIE_LTR_SUP,
	GENOA_STRAP_PCIE_LINK_BW_NOTIF_SUP,
	GENOA_STRAP_PCIE_GEN3_1_FEAT_EN,
	GENOA_STRAP_PCIE_GEN4_FEAT_EN,
	GENOA_STRAP_PCIE_GEN5_FEAT_EN,
	GENOA_STRAP_PCIE_ECRC_GEN_EN,
	GENOA_STRAP_PCIE_SWUS_ECRC_GEN_EN,
	GENOA_STRAP_PCIE_ECRC_CHECK_EN,
	GENOA_STRAP_PCIE_SWUS_ECRC_CHECK_EN,
	GENOA_STRAP_PCIE_SWUS_ARI_EN,
	GENOA_STRAP_PCIE_CPL_ABORT_ERR_EN,
	GENOA_STRAP_PCIE_INT_ERR_EN,
	GENOA_STRAP_PCIE_MARGIN_IGN_C_SKP,
	GENOA_STRAP_SURPRISE_DOWN_ERR_EN,
	GENOA_STRAP_PCIE_SWUS_AER_EN,
	GENOA_STRAP_PCIE_P_ERR_COR_EN,

	/* ACS straps */
	GENOA_STRAP_PCIE_ACS_EN,
	GENOA_STRAP_PCIE_ACS_SRC_VALID,
	GENOA_STRAP_PCIE_ACS_TRANS_BLOCK,
	GENOA_STRAP_PCIE_ACS_DIRECT_TRANS_P2P,
	GENOA_STRAP_PCIE_ACS_P2P_CPL_REDIR,
	GENOA_STRAP_PCIE_ACS_P2P_REQ_RDIR,
	GENOA_STRAP_PCIE_ACS_UPSTREAM_FWD,
};

/*
 * PCIe Straps that we unconditionally set to 0
 * These are generally debug and test settings that are usually not a good idea
 * in my experience to allow accidental enablement.
 */
static const uint32_t genoa_pcie_strap_disable[] = {
	GENOA_STRAP_PCIE_I2C_DBG_EN,
	GENOA_STRAP_PCIE_DEBUG_RXP,
	GENOA_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST,
	GENOA_STRAP_PCIE_ERR_REPORT_DIS,
	GENOA_STRAP_PCIE_TX_TEST_ALL,
	GENOA_STRAP_PCIE_MCAST_EN,
	GENOA_STRAP_PCIE_DESKEW_EMPTY,
	/*
	 * We do not currently enable CXL support, so we disable alternative
	 * protocol negotiations.
	 */
	GENOA_STRAP_PCIE_P_ALT_PROT_EN,
};

/*
 * PCIe Straps that have other values.
 */
static const zen_pcie_strap_setting_t genoa_pcie_strap_settings[] = {
	{
		.strap_reg = GENOA_STRAP_PCIE_P_MAX_PAYLOAD_SUP,
		.strap_data = 0x2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_PLL_FREQ_MODE,
		.strap_data = 2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_DS_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_US_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_16GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_16GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_32GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_32GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_DLF_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
};

/*
 * PCIe Straps that exist on a per-port level.  Most pertain to the port itself;
 * others pertain to features exposed via the associated bridge.
 */
static const zen_pcie_strap_setting_t genoa_pcie_port_settings[] = {
	{
		.strap_reg = GENOA_STRAP_PCIE_P_EXT_FMT_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_E2E_TLP_PREFIX_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_10B_TAG_CMPL_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_10B_TAG_REQ_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TCOMMONMODE_TIME,
		.strap_data = 0xa,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TPON_SCALE,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TPON_VALUE,
		.strap_data = 0xf,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_DLF_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_DLF_EXCHANGE_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_WRP_MISC,
		.strap_data = GENOA_STRAP_PCIE_WRP_MISC_SSID_EN,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_FOM_TIME,
		.strap_data = GENOA_STRAP_PCIE_P_FOM_300US,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SPC_MODE_8GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SPC_MODE_16GT,
		.strap_data = 0x2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_32GT_PRECODE_REQ,
		.strap_data = 0x2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_L0s_EXIT_LAT,
		.strap_data = PCIE_LINKCAP_L0S_EXIT_LAT_MAX >> 12,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_EQ_BYPASS_TO_HR_ADV,
		.strap_data = 0,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = GENOA_IOMS_BONUS_PCIE_CORENO,
		.strap_portmatch = PCIE_PORTMATCH_ANY,
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_PM_SUB_SUP,
		.strap_data = 0,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iomsmatch = PCIE_IOMSMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY,
	},
};

static void
genoa_fabric_write_pcie_strap(zen_pcie_core_t *pc,
    const uint32_t reg, const uint32_t data)
{
	const zen_ioms_t *ioms = pc->zpc_ioms;
	uint32_t addr, inst;

	inst = ioms->zio_iohcnum + 4 * pc->zpc_coreno;
	if (pc->zpc_coreno == GENOA_IOMS_BONUS_PCIE_CORENO)
		inst = 9;

	/*
	 * The strap namespace has gone through some changes in the processor
	 * revisions and changed started with B0 processors. For earlier
	 * processors we must adjust things. Please see
	 * <sys/io/genoa/pcie_rsmu.h> for more information.
	 */
	addr = reg;
	if (!chiprev_at_least(cpuid_getchiprev(CPU),
	    X86_CHIPREV_AMD_GENOA_B0) &&
	    addr >= GENOA_STRAP_PCIE_B0_ADJ_BASE) {
		addr -= GENOA_STRAP_PCIE_B0_ADJ_VAL;
	}

	zen_mpio_write_pcie_strap(pc, addr + (inst << 16), data);
}

/*
 * Here we set up all the straps for PCIe features that we care about and want
 * advertised as capabilities. Note that we do not enforce any order between the
 * straps. It is our understanding that the straps themselves do not kick off
 * any change, but instead another stage (presumably before link training)
 * initializes the read of all these straps in one go.
 * Currently, we set these straps on all cores and all ports regardless of
 * whether they are used, though this may be changed if it proves problematic.
 * We do however operate on a single I/O die at a time, because we are called
 * out of the DXIO state machine which also operates on a single I/O die at a
 * time, unless our argument is NULL.  This allows us to avoid changing strap
 * values on 2S machines for entities that were already configured completely
 * during socket 0's DXIO SM.
 */
void
genoa_fabric_init_pcie_straps(zen_pcie_core_t *pc)
{
	for (size_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_enable); i++) {
		genoa_fabric_write_pcie_strap(pc,
		    genoa_pcie_strap_enable[i], 0x1);
	}
	for (size_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_disable); i++) {
		genoa_fabric_write_pcie_strap(pc,
		    genoa_pcie_strap_disable[i], 0x0);
	}
	for (size_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &genoa_pcie_strap_settings[i];

		if (zen_fabric_pcie_strap_matches(pc, PCIE_PORTMATCH_ANY,
		    strap)) {
			genoa_fabric_write_pcie_strap(pc,
			    strap->strap_reg, strap->strap_data);
		}
	}

	if (!chiprev_at_least(cpuid_getchiprev(CPU),
	    X86_CHIPREV_AMD_GENOA_B0)) {
		genoa_fabric_write_pcie_strap(pc,
		    GENOA_STRAP_PCIE_P_COMPLIANCE_DIS, 1);
	}

	/* Handle per bridge initialization */
	for (size_t i = 0; i < ARRAY_SIZE(genoa_pcie_port_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &genoa_pcie_port_settings[i];
		for (uint8_t j = 0; j < pc->zpc_nports; j++) {
			if (zen_fabric_pcie_strap_matches(pc, j, strap)) {
				genoa_fabric_write_pcie_strap(pc,
				    strap->strap_reg +
				    (j * GENOA_STRAP_PCIE_NUM_PER_PORT),
				    strap->strap_data);
			}
		}
	}
}

void
genoa_fabric_init_pcie_port(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Turn off unused lanes.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_WIDTH_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_TURN_OFF_UNUSED_LANES(val, 1);
	zen_pcie_port_write(port, reg, val);
}

void
genoa_fabric_init_pcie_port_after_reconfig(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Set search equalization modes.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_8GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_8GT_SET_SEARCH_MODE(val,
	    PCIE_PORT_LC_EQ_CTL_8GT_SEARCH_MODE_PRST);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_16GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_16GT_SET_SEARCH_MODE(val,
	    PCIE_PORT_LC_EQ_CTL_16GT_SEARCH_MODE_PRST);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_32GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_32GT_SET_SEARCH_MODE(val,
	    PCIE_PORT_LC_EQ_CTL_32GT_SEARCH_MODE_PRST);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Set preset masks.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_PRST_MASK_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_MASK_8GT(val,
	    PCIE_PORT_LC_PRST_MASK_CTL_8GT_VAL);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_MASK_16GT(val,
	    PCIE_PORT_LC_PRST_MASK_CTL_16GT_VAL);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_MASK_32GT(val,
	    PCIE_PORT_LC_PRST_MASK_CTL_32GT_VAL);
	zen_pcie_port_write(port, reg, val);
}

/*
 * Here we are going through bridges and need to start setting them up with the
 * various features that we care about. Most of these are an attempt to have
 * things set up so PCIe enumeration can meaningfully actually use these. The
 * exact set of things required is ill-defined. Right now this includes:
 *
 *   * Enabling the bridges such that they can actually allow software to use
 *     them. XXX Though really we should disable DMA until such a time as we're
 *     OK with that.
 *
 *   * Changing settings that will allow the links to actually flush TLPs when
 *     the link goes down.
 */
void
genoa_fabric_init_bridge(zen_pcie_port_t *port)
{
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_TX_PORT_CTL1);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_PORT_CTL1_SET_TLP_FLUSH_DOWN_DIS(val, 0);
	val = PCIE_PORT_TX_PORT_CTL1_SET_CPL_PASS(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure the hardware knows the corresponding b/d/f for this bridge.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_TX_ID);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_ID_SET_BUS(val, ioms->zio_pci_busno);
	val = PCIE_PORT_TX_ID_SET_DEV(val, port->zpp_device);
	val = PCIE_PORT_TX_ID_SET_FUNC(val, port->zpp_func);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Next, we have to go through and set up a bunch of the lane controller
	 * configuration controls for the individual port. These include
	 * various settings around how idle transitions occur, how it replies to
	 * certain messages, and related.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL_SET_L1_IMM_ACK(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_L0S_L1_TRAIN(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_WIDTH_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_DUAL_RECONFIG(val, 1);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_RENEG_EN(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL2);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL2_SET_ELEC_IDLE(val,
	    PCIE_PORT_LC_CTL2_ELEC_IDLE_M1);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL3);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * AMD's current default is to disable certain classes of receiver
	 * errors. XXX We need to understand why.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_HW_DBG);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_HW_DBG_SET_DBG13(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure the 8 GT/s symbols per clock is set to 2.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_CTL6);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL6_SET_SPC_MODE_8GT(val,
	    PCIE_PORT_LC_CTL6_SPC_MODE_8GT_2);
	zen_pcie_port_write(port, reg, val);
}

/*
 * This is a companion to genoa_fabric_init_bridge, that operates on the PCIe
 * core level before we get to the individual bridge. This initialization
 * generally is required to ensure that each port (regardless of whether it's
 * hidden or not) is able to properly generate an all 1s response. In addition
 * we have to take care of things like atomics, idling defaults, certain
 * receiver completion buffer checks, etc.
 */
void
genoa_fabric_init_pcie_core(zen_pcie_core_t *pc)
{
	smn_reg_t reg;
	uint32_t val;

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_CI_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_CI_CTL_SET_LINK_DOWN_CTO_EN(val, 1);
	val = PCIE_CORE_CI_CTL_SET_IGN_LINK_DOWN_CTO_ERR(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Program the unit ID for this device's SDP port.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_SDP_CTL);
	val = zen_pcie_core_read(pc, reg);
	/*
	 * The unit ID is split into two parts, and written to different
	 * fields in this register.
	 */
	ASSERT0(pc->zpc_sdp_unit & 0x8000000);
	val = PCIE_CORE_SDP_CTL_SET_UNIT_ID_HI(val,
	    bitx8(pc->zpc_sdp_unit, 6, 3));
	val = PCIE_CORE_SDP_CTL_SET_UNIT_ID_LO(val,
	    bitx8(pc->zpc_sdp_unit, 2, 0));
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Program values required for receiver margining to work. These are
	 * hidden in the core. Voltage margining was made mandatory in Gen 5.
	 * There are three registers involved.
	 */

	/*
	 * The first register (D_PCIE_CORE_RX_MARGIN_CTL_CAP) sets up the
	 * margining support.  We set things up to support voltage margining,
	 * and make left/right timing and up/down voltage independent.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN_CTL_CAP);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_TIME(val, 1);
	val = PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_VOLT(val, 1);
	val = PCIE_CORE_RX_MARGIN_CTL_CAP_SET_VOLT_SUP(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * The second register (D_PCIE_CORE_RX_MARGIN1) sets the maximum
	 * supported offsets and steps, but the values actually used may be
	 * smaller, depending on the characteristics of the device on the
	 * distant end.
	 *
	 * The maximum voltage offset controls the maximum swing at the maximum
	 * stepped value, relative to the default setting, as a percentage of
	 * 1V; our value of 0xD is thus 0.13V. This is the value in the
	 * register at reset, and presumably recommended by AMD. This 130mV
	 * range is more than enough to prove a link against published
	 * acceptance criteria, but we may want to increase this in the future
	 * in order to find the extremes of the available margin.
	 *
	 * The maximum timing offset value is the maximum offset from default
	 * setting at the maximum stepped value as a percentage of a nominal UI
	 * (Unit Interval) at 16 GT/s.  0x19 is thus 25%.
	 *
	 * The maximum number of time steps is the timing steps, to the right or
	 * left, that can be taken from the default setting; it must be at least
	 * +/- 20% of the UI.  Our value of 0x10 is 16.
	 *
	 * Finally, the number of voltage steps is the number of steps either up
	 * or down from the default setting.  The PPR says that steps have a
	 * minimum of +/- 50mV as measured by the 16 GT/s reference equalizer.
	 * It appears that 0x1D is the maximum supported value which equates to
	 * 29 steps in each direction.  Setting it any higher results in
	 * margining failing completely, and the port losing margining
	 * capabilities entirely until the CPU is reset.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN1);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN1_SET_MAX_VOLT_OFF(val, 0xd);
	val = PCIE_CORE_RX_MARGIN1_SET_MAX_TIME_OFF(val, 0x19);
	val = PCIE_CORE_RX_MARGIN1_SET_NUM_TIME_STEPS(val, 0x10);
	val = PCIE_CORE_RX_MARGIN1_SET_NUM_VOLT_STEPS(val, 0x1d);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * The third register (D_PCIE_CORE_RX_MARGIN2) sets sampling parameters
	 * and the number of lanes that can be margined at the same time.
	 * We've been led to believe the entire core supports margining at
	 * once, or 16 lanes, but note that the register is encoded as a zeros
	 * based value, so we write 0xf. We program the ratios to sample all
	 * bits received during margining.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN2);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN2_SET_NLANES(val, 0xf);
	val = PCIE_CORE_RX_MARGIN2_SET_TIME_RATIO(val, 0x3f);
	val = PCIE_CORE_RX_MARGIN2_SET_VOLT_RATIO(val, 0x3f);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Ensure that RCB checking is what's seemingly expected.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PCIE_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_CTL_SET_RCB_BAD_ATTR_DIS(val, 1);
	val = PCIE_CORE_PCIE_CTL_SET_RCB_BAD_SIZE_DIS(val, 0);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Enabling atomics in the RC requires a few different registers. Both
	 * a strap has to be overridden and then corresponding control bits.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_STRAP_F0);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_ROUTE(val, 1);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_EN(val, 1);
	zen_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PCIE_TX_CTL1);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_TX_CTL1_SET_TX_ATOMIC_ORD_DIS(val, 1);
	val = PCIE_CORE_PCIE_TX_CTL1_SET_TX_ATOMIC_OPS_DIS(val, 0);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Ensure the correct electrical idle mode detection is set. In
	 * addition, it's been recommended we ignore the K30.7 EDB (EnD Bad)
	 * special symbol errors.
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_PCIE_P_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_P_CTL_SET_ELEC_IDLE(val,
	    PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M1);
	val = PCIE_CORE_PCIE_P_CTL_SET_IGN_EDB_ERR(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * The IOMMUL1 does not have an instance for the on-the side WAFL lanes.
	 * Skip the WAFL port if we're that.
	 */
	if (pc->zpc_coreno >= IOMMUL1_N_PCIE_CORES)
		return;

	reg = genoa_pcie_core_reg(pc, D_IOMMUL1_CTL1);
	val = zen_pcie_core_read(pc, reg);
	val = IOMMUL1_CTL1_SET_ORDERING(val, 1);
	zen_pcie_core_write(pc, reg, val);
}

void
genoa_set_mpio_global_config(zen_mpio_global_config_t *zconfig)
{
	/*
	 * Note: This CTASSERT is not in genoa/mpio.h because
	 * zen_mpio_global_config_t is not visible there.
	 */
	CTASSERT(sizeof (genoa_mpio_global_config_t) ==
	    sizeof (zen_mpio_global_config_t));

	genoa_mpio_global_config_t *config =
	    (genoa_mpio_global_config_t *)zconfig;
	config->gmgc_skip_vet = 1;
	config->gmgc_use_phy_sram = 1;
	config->gmgc_valid_phy_firmware = 1;
	config->gmgc_en_pcie_noncomp_wa = 1;
	config->gmgc_pwr_mgmt_clk_gating = 1;
}

void
genoa_smu_features_init(zen_iodie_t *iodie)
{
	/*
	 * Not all combinations of SMU features will result in correct system
	 * behavior, so we therefore err on the side of matching stock platform
	 * enablement for Genoa rev Bx -- even where that means enabling
	 * features with unknown functionality.
	 *
	 * Note, CPPC is optional and this is the default; we set it here
	 * because AGESA does.
	 */
	uint32_t features = GENOA_SMU_FEATURE_DATA_CALCULATION |
	    GENOA_SMU_FEATURE_PPT |
	    GENOA_SMU_FEATURE_THERMAL_DESIGN_CURRENT |
	    GENOA_SMU_FEATURE_THERMAL |
	    GENOA_SMU_FEATURE_FIT |
	    GENOA_SMU_FEATURE_ELECTRICAL_DESIGN_CURRENT |
	    GENOA_SMU_FEATURE_CSTATE_BOOST |
	    GENOA_SMU_FEATURE_PROCESSOR_THROTTLING_TEMPERATURE |
	    GENOA_SMU_FEATURE_CORE_CLOCK_DPM |
	    GENOA_SMU_FEATURE_FABRIC_CLOCK_DPM |
	    GENOA_SMU_FEATURE_LCLK_DPM |
	    GENOA_SMU_FEATURE_LCLK_DEEP_SLEEP |
	    GENOA_SMU_FEATURE_DYNAMIC_VID_OPTIMIZER |
	    GENOA_SMU_FEATURE_CORE_C6 |
	    GENOA_SMU_FEATURE_DF_CSTATES |
	    GENOA_SMU_FEATURE_CLOCK_GATING |
	    GENOA_SMU_FEATURE_CPPC |
	    GENOA_SMU_FEATURE_GMI_DLWM |
	    GENOA_SMU_FEATURE_XGMI_DLWM;

	/*
	 * Some features are disabled on Ax and AB silicon spins.  Note that we
	 * never explicitly set GENOA_SMU_FEATURE_GMI_FOLDING, so disabling it
	 * here is a no-op, but we include it in the disabled set anyway as
	 * documentation.  Note that we are too early in boot to use
	 * `cpuid_getchiprev(CPU)` here.
	 */
	if (!chiprev_at_least(oxide_board_data->obd_cpuinfo.obc_chiprev,
	    X86_CHIPREV_AMD_GENOA_B0)) {
		const uint32_t disabled_ax = GENOA_SMU_FEATURE_DF_CSTATES |
		    GENOA_SMU_FEATURE_FABRIC_CLOCK_DPM |
		    GENOA_SMU_FEATURE_XGMI_DLWM |
		    GENOA_SMU_FEATURE_GMI_DLWM |
		    GENOA_SMU_FEATURE_GMI_FOLDING;
		features &= ~disabled_ax;
	}

	const uint32_t features_ext = GENOA_SMU_EXT_FEATURE_PCC |
	    GENOA_SMU_EXT_FEATURE_MPDMA_TF_CLK_DEEP_SLEEP |
	    GENOA_SMU_EXT_FEATURE_MPDMA_PM_CLK_DEEP_SLEEP;

	VERIFY(zen_smu_set_features(iodie, features, features_ext));
}
