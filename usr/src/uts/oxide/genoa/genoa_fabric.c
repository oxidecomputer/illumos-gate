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
#include <sys/spl.h>

#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/mpio.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>

#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/io/genoa/pcie_rsmu.h>
#include <sys/io/genoa/iohc.h>
#include <sys/io/genoa/iommu.h>
#include <sys/io/genoa/nbif_impl.h>
#include <sys/io/genoa/ioapic.h>

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

void
genoa_fabric_thread_get_dpm_weights(const zen_thread_t *thread __unused,
    const uint64_t **wp, uint32_t *nentp)
{
	/*
	 * Genoa no longer reads the DPM weights from the SMU so we just return
	 * a non-zero count with a NULL pointer to indicate the corresponding
	 * indices should be zeroed out.
	 */
	*nentp = GENOA_MAX_DPM_WEIGHTS;
	*wp = NULL;
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
	zen_ioms_t *ioms = nbif->zn_ioms;
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
	zen_nbif_t *nbif = func->znf_nbif;
	zen_ioms_t *ioms = nbif->zn_ioms;
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
genoa_fabric_unhide_bridges(zen_pcie_port_t *port)
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

/*
 * Go through and configure and set up devices and functions. In particular we
 * need to go through and set up the following:
 *
 *  o Strap bits that determine whether or not the function is enabled
 *  o Enabling the interrupts of corresponding functions
 *  o Setting up specific PCI device straps around multi-function, FLR, poison
 *    control, TPH settings, etc.
 *
 * XXX For getting to PCIe faster and since we're not going to use these, and
 * they're all disabled, for the moment we just ignore the straps that aren't
 * related to interrupts, enables, and cfg comps.
 */
void
genoa_fabric_nbif_dev_straps(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t intr;

	reg = genoa_nbif_reg(nbif, D_NBIF_INTR_LINE_EN, 0);
	intr = zen_nbif_read(nbif, reg);
	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++) {
		smn_reg_t strapreg;
		uint32_t strap;
		zen_nbif_func_t *func = &nbif->zn_funcs[funcno];

		/*
		 * This indicates that we have a dummy function or similar. In
		 * which case there's not much to do here, the system defaults
		 * are generally what we want. XXX Kind of sort of. Not true
		 * over time.
		 */
		if ((func->znf_flags & ZEN_NBIF_F_NO_CONFIG) != 0) {
			continue;
		}

		strapreg = genoa_nbif_func_reg(func, D_NBIF_FUNC_STRAP0);
		strap = zen_nbif_func_read(func, strapreg);

		if ((func->znf_flags & ZEN_NBIF_F_ENABLED) != 0) {
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
	}

	zen_nbif_write(nbif, reg, intr);

	/*
	 * Each nBIF has up to three devices on them, though not all of them
	 * seem to be used. However, it's suggested that we enable completion
	 * timeouts on all three device straps.
	 */
	for (uint8_t devno = 0; devno < GENOA_NBIF_MAX_DEVS; devno++) {
		smn_reg_t reg;
		uint32_t val;

		reg = genoa_nbif_reg(nbif, D_NBIF_PORT_STRAP3, devno);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP3_SET_COMP_TO(val, 1);
		zen_nbif_write(nbif, reg, val);
	}
}

/*
 * Do everything else required to finish configuring the nBIF and get the PCIe
 * engine up and running.
 */
void
genoa_fabric_pcie(zen_fabric_t *fabric)
{
	zen_pcie_populate_dbg(fabric, GPCS_PRE_INIT, ZEN_IODIE_MATCH_ANY);
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
 *
 * These can be matched to a board identifier, I/O die DF node ID, NBIO/IOMS
 * number, PCIe core number (pcie_core_t.zpc_coreno), and PCIe port number
 * (pcie_port_t.gpp_portno).  The board sentinel value MBT_ANY is 0 and may be
 * omitted, but the others require nonzero sentinels as 0 is a valid index.  The
 * sentinel values of 0xFF here cannot match any real NBIO, RC, or port: there
 * are at most 4 NBIOs per die, 3 RC per NBIO, and 8 ports (bridges) per RC.
 * The RC and port filters are meaningful only if the corresponding strap exists
 * at the corresponding level.  The node ID, which incorporates both socket and
 * die number (die number is always 0 for Genoa), is 8 bits so in principle it
 * could be 0xFF and we use 32 bits there instead.  While it's still 8 bits in
 * Genoa, AMD have reserved another 8 bits that are likely to be used in future
 * families so we opt to go all the way to 32 here.  This can be reevaluated
 * when this is refactored to support multiple families.
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
	GENOA_STRAP_PCIE_SWUS_AER_EN,
};

/*
 * PCIe Straps that have other values.
 */
static const zen_pcie_strap_setting_t genoa_pcie_strap_settings[] = {
	{
		.strap_reg = GENOA_STRAP_PCIE_P_MAX_PAYLOAD_SUP,
		.strap_data = 0x2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_PLL_FREQ_MODE,
		.strap_data = 2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_DS_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_US_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_16GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_16GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_32GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_32GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_SUBVID,
		.strap_data = PCI_VENDOR_ID_OXIDE,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_SUBDID,
		.strap_data = PCI_SDID_OXIDE_GIMLET_BASE,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
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
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_E2E_TLP_PREFIX_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_10B_TAG_CMPL_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_10B_TAG_REQ_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TCOMMONMODE_TIME,
		.strap_data = 0xa,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TPON_SCALE,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_TPON_VALUE,
		.strap_data = 0xf,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_DLF_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_DLF_EXCHANGE_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_WRP_MISC,
		.strap_data = 0x1 << 9,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_FOM_TIME,
		.strap_data = GENOA_STRAP_PCIE_P_FOM_300US,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SPC_MODE_8GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SPC_MODE_16GT,
		.strap_data = 0x2,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_32GT_PRECODE_REQ,
		.strap_data = 0x0,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_SRIS_EN,
		.strap_data = 1,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP,
		.strap_data = 0,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	{
		.strap_reg = GENOA_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP,
		.strap_data = 0,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = 0,
		.strap_nbiomatch = 0,
		.strap_corematch = 1,
		.strap_portmatch = 1
	},
	// {
	// 	.strap_reg = GENOA_STRAP_PCIE_P_L0s_EXIT_LAT,
	// 	.strap_data = PCIE_LINKCAP_L0S_EXIT_LAT_MAX >> 12,
	// 	.strap_nodematch = PCIE_NODEMATCH_ANY,
	// 	.strap_nbiomatch = PCIE_NBIOMATCH_ANY,
	// 	.strap_corematch = PCIE_COREMATCH_ANY,
	// 	.strap_portmatch = PCIE_PORTMATCH_ANY
	// }
};

static void
genoa_fabric_write_pcie_strap(zen_pcie_core_t *pc,
    const uint32_t reg, const uint32_t data)
{
	const zen_ioms_t *ioms = pc->zpc_ioms;
	uint32_t addr, inst;

	inst = ioms->zio_num + 4 * pc->zpc_coreno;

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
	const zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;

	if (iodie != NULL && pc->zpc_ioms->zio_iodie != iodie)
		return;

	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_enable); i++) {
		genoa_fabric_write_pcie_strap(pc,
		    genoa_pcie_strap_enable[i], 0x1);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_disable); i++) {
		genoa_fabric_write_pcie_strap(pc,
		    genoa_pcie_strap_disable[i], 0x0);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_strap_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &genoa_pcie_strap_settings[i];

		if (zen_fabric_pcie_strap_matches(pc, PCIE_PORTMATCH_ANY,
		    strap)) {
			genoa_fabric_write_pcie_strap(pc,
			    strap->strap_reg, strap->strap_data);
		}
	}

	/* Handle Special case for DLF which needs to be set on non WAFL */
	/* Does not appear to be used on Genoa. */
	if (pc->zpc_coreno != GENOA_IOMS_BONUS_PCIE_CORENO) {
		genoa_fabric_write_pcie_strap(pc, GENOA_STRAP_PCIE_DLF_EN, 1);
	}

	/* Handle per bridge initialization */
	for (uint_t i = 0; i < ARRAY_SIZE(genoa_pcie_port_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &genoa_pcie_port_settings[i];
		for (uint_t j = 0; j < pc->zpc_nports; j++) {
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
genoa_fabric_init_smn_port_state(zen_pcie_port_t *port)
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

	/*
	 * Set search equalization modes.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_8GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_8GT_SET_SEARCH_MODE(val, 3);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_16GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_16GT_SET_SEARCH_MODE(val, 3);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_32GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_32GT_SET_SEARCH_MODE(val, 3);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Set preset masks.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_PRST_MASK_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_8GT(val, 0x370);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_16GT(val, 0x370);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_32GT(val, 0x78);
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
genoa_fabric_init_bridges(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;
	bool hide;
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;

	/*
	 * We need to determine whether or not this bridge should be considered
	 * visible. This is messy. Ideally, we'd just have every bridge be
	 * visible; however, life isn't that simple because convincing the PCIe
	 * engine that it should actually allow for completion timeouts to
	 * function as expected. In addition, having bridges that have no
	 * devices present and never can due to the platform definition can end
	 * up being rather wasteful of precious 32-bit non-prefetchable memory.
	 * The current masking rules are based on what we have learned from
	 * trial and error works.
	 *
	 * Strictly speaking, a bridge will work from a completion timeout
	 * perspective if the SMU thinks it belongs to a PCIe port that has any
	 * hotpluggable elements or otherwise has a device present.
	 * Unfortunately the case you really want to work, a non-hotpluggable,
	 * but defined device that does not have a device present should be
	 * visible does not work.
	 *
	 * Ultimately, what we have implemented here is to basically say if a
	 * bridge is not mapped to an endpoint, then it is not shown. If it is,
	 * and it belongs to a hot-pluggable port then we always show it.
	 * Otherwise we only show it if there's a device present.
	 */
	hide = true;
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0) {
		zen_mpio_ict_link_status_t *lp =
		    &port->zpp_ask_port->zma_status;
		bool hotplug, trained;

		hotplug = (pc->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG) != 0;
		trained = lp->zmils_state == ZEN_MPIO_LINK_STATE_TRAINED;
		hide = !hotplug && !trained;
	}

	if (hide) {
		port->zpp_flags |= ZEN_PCIE_PORT_F_BRIDGE_HIDDEN;
	}

	reg = genoa_pcie_port_reg(port, D_IOHCDEV_PCIE_BRIDGE_CTL);
	val = zen_pcie_port_read(port, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	if (hide) {
		val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 1);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 1);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 1);
	} else {
		val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 0);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 0);
		val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 0);
	}
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_TX_PORT_CTL1);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_PORT_CTL1_SET_TLP_FLUSH_DOWN_DIS(val, 0);
	val = PCIE_PORT_TX_PORT_CTL1_SET_CPL_PASS(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Set search equalization modes.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_8GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_8GT_SET_SEARCH_MODE(val, 3);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_16GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_16GT_SET_SEARCH_MODE(val, 3);
	zen_pcie_port_write(port, reg, val);

	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_32GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_32GT_SET_SEARCH_MODE(val, 3);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Set preset masks.
	 */
	reg = genoa_pcie_port_reg(port, D_PCIE_PORT_LC_PRST_MASK_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_8GT(val, 0x370);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_16GT(val, 0x370);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_PRESET_MASK_32GT(val, 0x78);
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

	/*
	 * Software expects to see the PCIe slot implemented bit when a slot
	 * actually exists. For us, this is basically anything that actually is
	 * considered MAPPED. Set that now on the port.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0) {
		uint16_t reg;

		reg = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, GENOA_BRIDGE_R_PCI_PCIE_CAP);
		reg |= PCIE_PCIECAP_SLOT_IMPL;
		pci_putw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, GENOA_BRIDGE_R_PCI_PCIE_CAP, reg);
	}
}

/*
 * This is a companion to genoa_fabric_init_bridges, that operates on the PCIe
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
	 * hidden in the core. Genoa processors generally only support timing
	 * margining as that's what's required by PCIe Gen 4. Voltage margining
	 * was made mandatory in Gen 5.
	 *
	 * The first register (D_PCIE_CORE_RX_MARGIN_CTL_CAP) sets up the
	 * supported margining. The second register (D_PCIE_CORE_RX_MARGIN1)
	 * sets the supported offsets and steps. These values are given us by
	 * AMD in a roundabout fashion. These values translate into allowing the
	 * maximum timing offset to be 50% of a UI (unit interval) and taking up
	 * to 23 steps in either direction. Because we've set the maximum offset
	 * to be 50%, each step takes 50%/23 or ~2.17%. The third register
	 * (D_PCIE_CORE_RX_MARGIN2) is used to set how many lanes can be
	 * margined at the same time. Similarly we've been led to believe the
	 * entire core supports margining at once, so that's 16 lanes and the
	 * register is encoded as a zeros based value (so that's why we write
	 * 0xf).
	 */
	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN_CTL_CAP);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN_CTL_CAP_SET_IND_TIME(val, 1);
	zen_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN1);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN1_SET_MAX_TIME_OFF(val, 0x19);
	val = PCIE_CORE_RX_MARGIN1_SET_NUM_TIME_STEPS(val, 0x10);
	zen_pcie_core_write(pc, reg, val);

	reg = genoa_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN2);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN2_SET_NLANES(val, 0xf);
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

typedef struct genoa_ioms_pcie_port_info {
	uint8_t gippi_count;
	zen_pcie_port_info_t gippi_info[4];
} genoa_ioms_pcie_port_info_t;

/*
 * These are internal bridges that correspond to NBIFs; they are modeled as
 * ports but there is no physical port brought out of the package.
 */
static const genoa_ioms_pcie_port_info_t genoa_int_ports[GENOA_IOMS_PER_IODIE] =
{
	[0] = {
		.gippi_count = 2,
		.gippi_info = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
			{ .zppi_dev = 0x7, .zppi_func = 0x2, },
		},
	},
	[1] = {
		.gippi_count = 1,
		.gippi_info = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
		},
	},
	[2] = {
		.gippi_count = 2,
		.gippi_info = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
			{ .zppi_dev = 0x7, .zppi_func = 0x2, },
		},
	},
	[3] = {
		.gippi_count = 1,
		.gippi_info = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
		},
	},
};

typedef struct {
	zen_ioms_t *pbc_ioms;
	uint8_t pbc_busoff;
} pci_bus_counter_t;

static int
genoa_fabric_hack_bridges_cb(zen_pcie_port_t *port, void *arg)
{
	uint8_t bus, secbus;
	pci_bus_counter_t *pbc = arg;
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;

	bus = ioms->zio_pci_busno;
	if (pbc->pbc_ioms != ioms) {
		pbc->pbc_ioms = ioms;
		const genoa_ioms_pcie_port_info_t *int_ports =
		    &genoa_int_ports[ioms->zio_num];
		pbc->pbc_busoff = 1 + int_ports->gippi_count;
		for (uint_t i = 0; i < int_ports->gippi_count; i++) {
			const zen_pcie_port_info_t *info =
			    &int_ports->gippi_info[i];
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_PRIBUS, bus);
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_SECBUS, bus + 1 + i);
			pci_putb_func(bus, info->zppi_dev, info->zppi_func,
			    PCI_BCNF_SUBBUS, bus + 1 + i);

		}
	}

	if ((port->zpp_flags & ZEN_PCIE_PORT_F_BRIDGE_HIDDEN) != 0) {
		return (0);
	}

	secbus = bus + pbc->pbc_busoff;

	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_PRIBUS, bus);
	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_SECBUS, secbus);
	pci_putb_func(bus, port->zpp_device, port->zpp_func,
	    PCI_BCNF_SUBBUS, secbus);
	pbc->pbc_busoff++;

	return (0);
}

/*
 * XXX This whole function exists to workaround deficiencies in software and
 * basically try to ape parts of the PCI firmware spec. The OS should natively
 * handle this. In particular, we currently do the following:
 *
 *   * Program a single downstream bus onto each root port. We can only get away
 *     with this because we know there are no other bridges right now. This
 *     cannot be a long term solution, though I know we will be temped to make
 *     it one. I'm sorry future us.
 */
static bool genoa_hotplug_init(zen_fabric_t *);
void
genoa_fabric_hack_bridges(zen_fabric_t *fabric)
{
	pci_bus_counter_t c;
	bzero(&c, sizeof (c));

	zen_fabric_walk_pcie_port(fabric, genoa_fabric_hack_bridges_cb, &c);
}
