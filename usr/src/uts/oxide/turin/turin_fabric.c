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
 * Turin's I/O fabric. This consists of both the data fabric and the
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

#include <sys/io/turin/fabric_impl.h>
#include <sys/io/turin/pcie.h>
#include <sys/io/turin/pcie_impl.h>
#include <sys/io/turin/iohc.h>
#include <sys/io/turin/iommu.h>
#include <sys/io/turin/nbif_impl.h>
#include <sys/io/turin/ioapic.h>

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently follow the recommendation of the PPR. Although IOAPIC instances on
 * the larger IOHC instances have 22 bridges and the others have 9, the
 * configuration of the first 9 is common across both so we can get away with a
 * single table.
 */
static const zen_ioapic_info_t turin_ioapic_routes[IOAPIC_NROUTES_L] = {
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
	[5] = { .zii_group = 0x5, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[6] = { .zii_group = 0x6, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[7] = { .zii_group = 0x6, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[8] = { .zii_group = 0x5, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[9] = { .zii_group = 0x4, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[10] = { .zii_group = 0x3, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[11] = { .zii_group = 0x2, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[12] = { .zii_group = 0x1, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[13] = { .zii_group = 0x0, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_CDAB },
	[14] = { .zii_group = 0x0, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[15] = { .zii_group = 0x1, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[16] = { .zii_group = 0x2, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[17] = { .zii_group = 0x3, .zii_map = 0x1,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[18] = { .zii_group = 0x4, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[19] = { .zii_group = 0x5, .zii_map = 0x2,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_BCDA },
	[20] = { .zii_group = 0x0, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
	[21] = { .zii_group = 0x1, .zii_map = 0x0,
	    .zii_swiz = IOAPIC_ROUTE_INTX_SWIZZLE_ABCD },
};

CTASSERT(ARRAY_SIZE(turin_ioapic_routes) == IOAPIC_NROUTES_L);

const uint8_t turin_nbif_nfunc[] = {
	[0] = TURIN_NBIF0_NFUNCS,
	[1] = TURIN_NBIF1_NFUNCS,
	[2] = TURIN_NBIF2_NFUNCS
};

const zen_nbif_info_t turin_nbif_data[ZEN_IOMS_MAX_NBIF][ZEN_NBIF_MAX_FUNCS] = {
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
	[1] = {},
	[2] = {
		{ .zni_type = ZEN_NBIF_T_DUMMY, .zni_dev = 0, .zni_func = 0 },
		{ .zni_type = ZEN_NBIF_T_NTB, .zni_dev = 0, .zni_func = 1 }
	}
};

/*
 * This is called from the common code, via an entry in the Turin version of
 * Zen fabric ops vector. The common code is responsible for the bulk of
 * initialization; we merely fill in those bits that are microarchitecture
 * specific.
 */
void
turin_fabric_ioms_init(zen_ioms_t *ioms)
{
	const uint8_t iomsno = ioms->zio_num;

	ioms->zio_npcie_cores = turin_ioms_n_pcie_cores(iomsno);
	ioms->zio_nbionum = TURIN_NBIO_NUM(iomsno);

	/*
	 * nBIFs are actually associated with the NBIO instance but we have no
	 * representation in the fabric for NBIOs yet. Mark the first IOMS in
	 * each NBIO as holding the nBIFs.
	 */
	if (TURIN_IOMS_IOHUB_NUM(iomsno) == 0)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_NBIF;
}

/*
 * How many PCIe cores does this IOMS instance have?
 * If it's an IOHUB that has a bonus core then it will have the maximum
 * number, otherwise one fewer.
 */
uint8_t
turin_ioms_n_pcie_cores(const uint8_t iomsno)
{
	if (iomsno == TURIN_NBIO_BONUS_IOMS)
		return (TURIN_IOMS_MAX_PCIE_CORES);
	return (TURIN_IOMS_MAX_PCIE_CORES - 1);
}

/*
 * How many PCIe ports does this core instance have?
 * The bonus cores have a lower number of ports than the others.
 * Not all ports are necessarily enabled, and ports that are disabled may have
 * their associated bridges hidden; this is used to compute the locations of
 * register blocks that pertain to the port that may exist.
 */
uint8_t
turin_pcie_core_n_ports(const uint8_t pcno)
{
	if (pcno == TURIN_IOMS_BONUS_PCIE_CORENO)
		return (TURIN_PCIE_CORE_BONUS_PORTS);
	return (TURIN_PCIE_CORE_MAX_PORTS);
}

typedef enum turin_iommul1_subunit {
	TIL1SU_IOAGR
} turin_iommul1_subunit_t;

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
turin_pcie_port_reg(const zen_pcie_port_t *const port,
    const smn_reg_def_t def)
{
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_PORT:
		reg = turin_pcie_port_smn_reg(ioms->zio_num, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for PCIe port",
		    def.srd_unit);
	}

	return (reg);
}

smn_reg_t
turin_pcie_core_reg(const zen_pcie_core_t *const pc, const smn_reg_def_t def)
{
	zen_ioms_t *ioms = pc->zpc_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_CORE:
		reg = turin_pcie_core_smn_reg(ioms->zio_num, def,
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
turin_ioms_reg(const zen_ioms_t *const ioms, const smn_reg_def_t def,
    const uint16_t reginst)
{
	smn_reg_t reg;

	/*
	 * The way that these map to different register blocks is not
	 * straightforward. Because of the interleaving, the instances of units
	 * in the larger IOHC occurs before the smaller IOHC. The following
	 * table maps the IOMS number to which NBIO, IOHUB, and IOHC kind is
	 * present.
	 *
	 * 	IOMS	NBIO	IOHUB	IOHC	UNIT
	 * 	----	----	-----	----	----
	 * 	0	0	0	L	0
	 * 	1	0	1	S	4
	 * 	2	0	2	L	1
	 * 	3	0	3	S	5
	 * 	4	1	0	L	2
	 * 	5	1	1	S	6
	 * 	6	1	2	L	3
	 * 	7	1	3	S	7
	 *
	 * NB: There is sometimes an aperture gap between IOHC0 and IOHC1 and
	 * there can even be (more rarely) a change in the register offset such
	 * as in IOHC::MISC_RAS_CONTROL. If registers in the latter category
	 * are ever used via this function then it will need extending.
	 */
	const uint8_t iomsno = ioms->zio_num;
	const turin_iohc_sz iohcsz = TURIN_IOHC_SZ(iomsno);
	const uint8_t unit = iomsno / 2 + (iohcsz == IOHC_SZ_L ? 0 : 4);

	switch (def.srd_unit) {
	case SMN_UNIT_IOAPIC:
		reg = turin_ioapic_smn_reg(unit, def, reginst);
		break;
	case SMN_UNIT_IOHC:
		reg = turin_iohc_smn_reg(unit, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = turin_ioagr_smn_reg(unit, def, reginst);
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
		const turin_iommul1_subunit_t su =
		    (const turin_iommul1_subunit_t)reginst;
		switch (su) {
		case TIL1SU_IOAGR: {
			/*
			 * The IOAGR registers on IOMMUL1 are only instanced
			 * on the larger IOHCs.
			 */
			ASSERT3U(iohcsz, ==, IOHC_SZ_L);
			reg = turin_iommul1_ioagr_smn_reg(unit, def, 0);
			break;
		}
		default:
			cmn_err(CE_PANIC, "invalid IOMMUL1 subunit %d", su);
			break;
		}
		break;
	}
	case SMN_UNIT_IOMMUL2: {
		/*
		 * The L2IOMMU is only present in the larger IOHC instances.
		 */
		ASSERT3U(iohcsz, ==, IOHC_SZ_L);
		reg = turin_iommul2_smn_reg(unit, def, reginst);
		break;
	}
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
		    def.srd_unit);
	}
	return (reg);
}

static smn_reg_t
turin_nbif_reg(const zen_nbif_t *const nbif, const smn_reg_def_t def,
    const uint16_t reginst)
{
	zen_ioms_t *ioms = nbif->zn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF:
		reg = turin_nbif_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, reginst);
		break;
	case SMN_UNIT_NBIF_ALT:
		reg = turin_nbif_alt_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF",
		    def.srd_unit);
	}

	return (reg);
}

static smn_reg_t
turin_nbif_func_reg(const zen_nbif_func_t *const func, const smn_reg_def_t def)
{
	zen_nbif_t *nbif = func->znf_nbif;
	zen_ioms_t *ioms = nbif->zn_ioms;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF_FUNC:
		reg = turin_nbif_func_smn_reg(ioms->zio_nbionum, def,
		    nbif->zn_num, func->znf_dev, func->znf_func);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF func",
		    def.srd_unit);
	}

	return (reg);
}

void
turin_fabric_init_tom(zen_ioms_t *ioms, uint64_t tom, uint64_t tom2,
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
	reg = turin_ioms_reg(ioms, D_IOHC_DRAM_TOM2_HI, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_HI_SET_TOM2(val, bitx64(tom2, 40, 32));
	zen_ioms_write(ioms, reg, val);

	reg = turin_ioms_reg(ioms, D_IOHC_DRAM_TOM2_LOW, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DRAM_TOM2_LOW_SET_EN(val, 1);
	val = IOHC_DRAM_TOM2_LOW_SET_TOM2(val, bitx64(tom2, 31, 23));
	zen_ioms_write(ioms, reg, val);

	if (tom3 == 0)
		return;

	reg = turin_ioms_reg(ioms, D_IOHC_DRAM_TOM3, 0);
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
turin_fabric_disable_vga(zen_ioms_t *ioms)
{
	uint32_t val;

	val = pci_getl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_PCI_ARB);
	val = IOHC_NB_PCI_ARB_SET_VGA_HOLE(val, IOHC_NB_PCI_ARB_VGA_HOLE_RAM);
	pci_putl_func(ioms->zio_pci_busno, 0, 0, IOHC_NB_PCI_ARB, val);
}

void
turin_fabric_pcie_refclk(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOHC_REFCLK_MODE, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_REFCLK_MODE_SET_27MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_25MHZ(val, 0);
	val = IOHC_REFCLK_MODE_SET_100MHZ(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
turin_fabric_set_pci_to(zen_ioms_t *ioms, uint16_t limit, uint16_t delay)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOHC_PCIE_CRS_COUNT, 0);
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
turin_fabric_iohc_features(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOHC_FCTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_FCTL_SET_ARI(val, 1);
	/* XXX Wants to be IOHC_FCTL_P2P_DISABLE? */
	val = IOHC_FCTL_SET_P2P(val, IOHC_FCTL_P2P_DROP_NMATCH);
	zen_ioms_write(ioms, reg, val);
}

void
turin_fabric_iohc_bus_num(zen_ioms_t *ioms, uint8_t busno)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOHC_BUS_NUM_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_BUS_NUM_CTL_SET_SEGMENT(val, 0);
	val = IOHC_BUS_NUM_CTL_SET_EN(val, 1);
	val = IOHC_BUS_NUM_CTL_SET_BUS(val, busno);
	zen_ioms_write(ioms, reg, val);
}

void
turin_fabric_iohc_fch_link(zen_ioms_t *ioms, bool has_fch)
{
	const turin_iohc_sz_t iohcsz = TURIN_IOHC_SZ(ioms->zio_num);
	smn_reg_t reg;
	uint32_t val = 0;

	reg = turin_ioms_reg(ioms, D_IOHC_SB_LOCATION, 0);

	/*
	 * On the smaller IOHC instances, zero out IOHC::SB_LOCATION and we're
	 * done.
	 */
	if (iohcsz != IOHC_SZ_L) {
		zen_ioms_write(ioms, reg, 0);
		return;
	}

	if (has_fch) {
		/*
		 * Unlike with earlier platforms where the value in
		 * IOHC::SB_LOCATION was copied across, on Turin we must
		 * explicitly set both the IOMMUL1 and IOMMUL2 registers to the
		 * same provided value.
		 */
		val = IOMMUL_SB_LOCATION_SET_CORE(val,
		    IOMMUL_SB_LOCATION_CORE_GPP2);
		val = IOMMUL_SB_LOCATION_SET_PORT(val,
		    IOMMUL_SB_LOCATION_PORT_A);
	} else {
		zen_ioms_write(ioms, reg, 0);
	}

	reg = turin_ioms_reg(ioms, D_IOMMUL1_SB_LOCATION, TIL1SU_IOAGR);
	zen_ioms_write(ioms, reg, val);

	reg = turin_ioms_reg(ioms, D_IOMMUL2_SB_LOCATION, 0);
	zen_ioms_write(ioms, reg, val);
}

void
turin_fabric_iohc_arbitration(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Start with IOHC burst related entries. These are always the same
	 * across every entity. The value used for the actual time entries just
	 * varies.
	 */
	for (uint_t i = 0; i < IOHC_SION_ENTS(ioms->zio_num); i++) {
		reg = turin_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = turin_ioms_reg(ioms, D_IOHC_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);

		reg = turin_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);
		reg = turin_ioms_reg(ioms, D_IOHC_SION_S1_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_CLIREQ_BURST_VAL);

		/*
		 * The read response burst values are only programmed on the
		 * first four IOAGR instances for some reason.
		 */
		if (i > 3)
			continue;

		reg = turin_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = turin_ioms_reg(ioms, D_IOHC_SION_S0_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);

		reg = turin_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
		reg = turin_ioms_reg(ioms, D_IOHC_SION_S1_RDRSP_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOHC_SION_RDRSP_BURST_VAL);
	}

	/*
	 * Next on our list is the IOAGR. While there are 6 entries, only 4 are
	 * ever set it seems.
	 */
	for (uint_t i = 0; i < IOHC_SION_ENTS(ioms->zio_num); i++) {
		reg = turin_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = turin_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
	}

	/*
	 * Finally, the SDPMUX variant. There are two SDPMUX instances,
	 * one on IOHUB0 in each NBIO.
	 */
	if (TURIN_IOMS_IOHUB_NUM(ioms->zio_num) == 0) {
		const uint_t sdpmux = TURIN_NBIO_NUM(ioms->zio_num);

		for (uint_t i = 0; i < SDPMUX_SION_MAX_ENTS; i++) {
			reg = SDPMUX_SION_S0_CLIREQ_BURST_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
			reg = SDPMUX_SION_S0_CLIREQ_BURST_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

			reg = SDPMUX_SION_S1_CLIREQ_BURST_LOW(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
			reg = SDPMUX_SION_S1_CLIREQ_BURST_HI(sdpmux, i);
			zen_ioms_write(ioms, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		}
	}

	/*
	 * XXX We probably don't need this since we don't have USB. But until
	 * we have things working and can experiment, hard to say. If someone
	 * were to use the bus, probably something we need to consider.
	 */
	reg = turin_ioms_reg(ioms, D_IOHC_USB_QOS_CTL, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_USB_QOS_CTL_SET_UNID1_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID1_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID1_ID(val, 0x30);
	val = IOHC_USB_QOS_CTL_SET_UNID0_EN(val, 0x1);
	val = IOHC_USB_QOS_CTL_SET_UNID0_PRI(val, 0x0);
	val = IOHC_USB_QOS_CTL_SET_UNID0_ID(val, 0x2f);
	zen_ioms_write(ioms, reg, val);

	reg = turin_ioms_reg(ioms, D_IOHC_QOS_CTL, 0);
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
turin_fabric_nbif_arbitration(zen_nbif_t *nbif)
{
	smn_reg_t reg;

	/*
	 * These registers are programmed for NBIF0 on all IOMS and for NBIF2
	 * on the IOMS which are instanced on the larger IOHCs. There are no
	 * devices on NBIF1.
	 */
	const turin_iohc_sz_t iohcsz = TURIN_IOHC_SZ(nbif->zn_ioms->zio_num);

	if (nbif->zn_num == 0 || (iohcsz == IOHC_SZ_L && nbif->zn_num == 2)) {
		reg = turin_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT2, 0);
		zen_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);
		reg = turin_nbif_reg(nbif, D_NBIF_GMI_WRR_WEIGHT3, 0);
		zen_nbif_write(nbif, reg, NBIF_GMI_WRR_WEIGHTn_VAL);
	}
}

/*
 * This bit of initialization is both strange and not very well documented.
 */
void
turin_fabric_nbif_syshub_dma(zen_nbif_t *nbif)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * This register, like all SYSHUBMM registers, has no instance on
	 * NBIF2, and NBIF1 has no devices.
	 */
	if (nbif->zn_num > 0)
		return;

	reg = turin_nbif_reg(nbif, D_NBIF_ALT_BGEN_BYP_SOC, 0);
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
turin_fabric_ioapic(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;
	const uint_t nroutes = TURIN_IOHC_SZ(ioms->zio_num) == IOHC_SZ_L ?
	    IOAPIC_NROUTES_L : IOAPIC_NROUTES_S;

	for (uint_t i = 0; i < nroutes; i++) {
		reg = turin_ioms_reg(ioms, D_IOAPIC_ROUTE, i);
		val = zen_ioms_read(ioms, reg);

		val = IOAPIC_ROUTE_SET_BRIDGE_MAP(val,
		    turin_ioapic_routes[i].zii_map);
		val = IOAPIC_ROUTE_SET_INTX_SWIZZLE(val,
		    turin_ioapic_routes[i].zii_swiz);
		val = IOAPIC_ROUTE_SET_INTX_GROUP(val,
		    turin_ioapic_routes[i].zii_group);

		zen_ioms_write(ioms, reg, val);
	}

	/*
	 * The address registers are in the IOHC while the feature registers
	 * are in the IOAPIC SMN space. To ensure that the other IOAPICs can't
	 * be enabled with reset addresses, we instead lock them.
	 * XXX Should we lock primary?
	 */
	reg = turin_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_HI, 0);
	val = zen_ioms_read(ioms, reg);
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0) {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val,
		    bitx64(ZEN_PHYSADDR_IOHC_IOAPIC, 47, 32));
	} else {
		val = IOHC_IOAPIC_ADDR_HI_SET_ADDR(val, 0);
	}
	zen_ioms_write(ioms, reg, val);

	reg = turin_ioms_reg(ioms, D_IOHC_IOAPIC_ADDR_LO, 0);
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
	reg = turin_ioms_reg(ioms, D_IOAPIC_FEATURES, 0);
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
 * Do everything else required to finish configuring the nBIF and get the PCIe
 * engine up and running.
 */
void
turin_fabric_pcie(zen_fabric_t *fabric)
{
	zen_pcie_populate_dbg(fabric, TPCS_PRE_INIT, ZEN_IODIE_MATCH_ANY);
}
