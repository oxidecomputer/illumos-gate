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
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>

#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>

#include <sys/io/genoa/fabric_impl.h>
#include <sys/io/genoa/pcie.h>
#include <sys/io/genoa/pcie_impl.h>
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
