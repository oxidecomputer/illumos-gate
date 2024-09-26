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
#include <sys/io/zen/smu_impl.h>

#include <sys/io/turin/fabric_impl.h>
#include <sys/io/turin/pcie.h>
#include <sys/io/turin/pcie_impl.h>
#include <sys/io/turin/iohc.h>
#include <sys/io/turin/iommu.h>
#include <sys/io/turin/nbif_impl.h>
#include <sys/io/turin/smu_impl.h>
#include <sys/io/turin/ioapic.h>

/*
 * Various routines and things to access, initialize, understand, and manage
 * Turin's I/O fabric. This consists of both the data fabric and the
 * northbridges.
 *
 * --------------------------------------
 * Physical Organization and Nomenclature
 * --------------------------------------
 *
 * In AMD's Zen 5 designs, the CPU socket is organized as a series of
 * chiplets with a series of compute complexes and then a central I/O die.
 * Critically, this I/O die is the major device that we are concerned with here
 * as it bridges the cores to basically the outside world through a combination
 * of different devices and I/O paths. The part of the I/O die that we will
 * spend most of our time dealing with is the IOM (I/O master) and IOS (I/O
 * slave) units. These are represented together in our fabric data structures
 * as combined IOMS units subordinate to an I/O die. On Turin processors, each
 * I/O die has 8 IOMS that are grouped together into higher level NBIO
 * (northbridge I/O) units. There are two NBIOs per I/O die which results in
 * each having 4 IOMS.
 *
 *                                 data fabric
 *                                     |
 *         +---------------------------|---------------------------+
 *         |  I/O Die                  |                           |
 *         |                           |                      +-------+
 *         |                           |                   +--+  FCH  |
 *         |                           |                   |  +-------+
 *         |  +-------------------+    |    +--------------|----+  |
 *         |  |       NBIO0       |    |    |       NBIO1  |    |  |
 *         |  |                   |    |    |              |    |  |
 *         |  |  +-------------+  |    |    |  +-----------+-+  |  |
 *         |  |  |  IOMS0      |-------+    |  |  IOMS4      |  |  |
 *     P0 PPPPPPP|  IOHUB0     |  |    |    |  |  IOHUB0     |PPPPPPP P2
 *         |  |  |  IOHC0(L)   |  |    +-------|  IOHC2(L)   |  |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *         |  |                   |    |    |                   |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *         |  |  |  IOMS3      |-------+    |  |  IOMS7      |  |  |
 *     G1 PPPPPPP|  IOHUB1     |  |    |    |  |  IOHUB1     |PPPPPPP G3
 *         |  |  |  IOHC4(S)   |  |    +-------|  IOHC6(S)   |  |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *         |  |                   |    |    |                   |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *     G0 PPPPPPP|  IOMS2      |-------+    |  |  IOMS6      |  |  |
 *         |  |  |  IOHUB2     |  |    |    |  |  IOHUB2     |PPPPPPP G2
 *   P4/5 PPPPPPP|  IOHC1(L)   |  |    +-------|  IOHC3(L)   |  |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *         |  |                   |    |    |                   |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *         |  |  |  IOMS1      |-------+    |  |  IOMS5      |  |  |
 *     P1 PPPPPPP|  IOHUB3     |  |    |    |  |  IOHUB3     |PPPPPPP P3
 *         |  |  |  IOHC5(S)   |  |    +-------|  IOHC7(S)   |  |  |
 *         |  |  +-------------+  |    |    |  +-------------+  |  |
 *         |  +-------------------+    |    +-------------------+  |
 *         |                           |                           |
 *         +---------------------------|---------------------------+
 *                                     |
 *                                     |
 *
 * Each IOMS instance implements, among other things, a PCIe root complex (RC),
 * consisting of two major components: an I/O hub core (IOHC) that implements
 * the host side of the RC, and one or two I/O hubs and PCIe cores that
 * implement the PCIe side. These components are accessible via the system
 * management network (SMN, also called the scalable control fabric) and that
 * is the primary way in which they are configured. The IOHC also appears in
 * PCI configuration space as a root complex and is the attachment point for
 * npe(4D). The PCIe cores do not themselves appear in config space; however,
 * each implements up to 9 PCIe root ports, and each root port has an
 * associated host bridge that appears in configuration space.
 * Externally-attached PCIe devices are enumerated under these bridges, and the
 * bridge provides the standard PCIe interface to the downstream port including
 * link status and control.
 *
 * Turin has two different types of IOHCs which the PPR calls IOHC0 and IOHC1.
 * IOHC0 is larger than IOHC1 and is connected to an L2IOMMU, while IOHC1 is
 * not. IOHC0 has multiple L1IOMMUs, IOHC1 only has a single one. Each IOHC is
 * separately connected to the data fabric and there is a 1:1 mapping between
 * IOHCs and IOMS instances in the system, leading to there being a total of 8
 * IOHCs (4 instances of the larger IOHC0 and 4 instances of the smaller IOHC1).
 * The even-numbered IOMS[0;2;4;6] contain the larger IOHC type while the
 * odd-numbered IOMS[1;3;5;7] contain the smaller type. The size of the IOHC
 * for each IOMS is indicated in the diagram above as (L) or (S).
 *
 * Two of the IOMS instances are somewhat special and merit brief additional
 * discussion. Instance 2 has a second PCIe core, which is associated with the
 * 8 bonus PCIe Gen3 ports. These are sometimes referred to as P4 and P5, two
 * 4-lane entities, but there is just a single bonus core. Instance 4 has the
 * Fusion Controller Hub (FCH) attached to it; the FCH doesn't contain any real
 * PCIe devices, but it does contain some fake ones and from what we can tell
 * the IOMS is the DF endpoint where MMIO transactions targeting the FCH are
 * directed.
 *
 * -----------------------
 * IOHC Instance Numbering
 * -----------------------
 *
 * Although there is a 1:1 correspondence between IOMS, IOHCs and IOHUBs, they
 * are not identically numbered. This is most easily seen in the diagram
 * above where the IOHUBs are shown numbered sequentially within each NBIO,
 * but the odd numbered IOMS (1,3) are reversed in order. The IOHCs are
 * numbered sequentially across the larger instances before the smaller, and
 * within that, NBIO0 before NBIO1.
 *
 * When accessing IOHC registers, the correct IOHC instance number pertaining
 * to the IOMS of interest must be used. This is calculated once for each IOMS
 * and saved in the zen_ioms_t structure for ease of use. Additionally, since
 * the different sized IOHCs have different characteristics, the IOHC type is
 * also stored for each IOMS.
 *
 * --------------
 * Representation
 * --------------
 *
 * We represent the IOMS entities described above in a hierarchical fashion:
 *
 * zen_fabric_t (DF -- root)
 * |
 * \-- zen_soc_t (qty 1 or 2)
 *     |
 *     \-- zen_iodie_t (qty 1)
 *         |
 *         \-- zen_ioms_t (qty 8, four per NBIO)
 *             |
 *             \-- zen_pcie_core_t (qty 1, except 2 for IOMS2)
 *                 |
 *                 \-- zen_pcie_port_t (qty 9, except 8 for IOMS2 RC 1)
 */

/*
 * This table encodes knowledge about how the SoC assigns devices and functions
 * to root ports.
 */
static const zen_pcie_port_info_t
    turin_pcie[TURIN_IOMS_MAX_PCIE_CORES][TURIN_PCIE_CORE_MAX_PORTS] = {
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
	}
};

/*
 * This table encodes the mapping of the set of dxio lanes to a given PCIe core
 * on an IOMS. The dxio engine uses different lane numbers than the phys. Note,
 * that all lanes here are inclusive. e.g. [start, end].
 * The subsequent tables encode mappings for the bonus cores.
 */
static const zen_pcie_core_info_t turin_lane_maps[8] = {
	/* name, DXIO start, DXIO end, PHY start, PHY end */
	{ "P0", 0x00, 0x0f, 0x00, 0x0f },	/* IOMS0, core 0 */
	{ "P1", 0x20, 0x2f, 0x20, 0x2f },	/* IOMS1, core 0 */
	{ "G0", 0x60, 0x6f, 0x60, 0x6f },	/* IOMS2, core 0 */
	{ "G1", 0x40, 0x4f, 0x40, 0x4f },	/* IOMS3, core 0 */
	{ "P2", 0x30, 0x3f, 0x30, 0x3f },	/* IOMS4, core 0 */
	{ "P3", 0x10, 0x1f, 0x10, 0x1f },	/* IOMS5, core 0 */
	{ "G2", 0x70, 0x7f, 0x70, 0x7f },	/* IOMS6, core 0 */
	{ "G3", 0x50, 0x5f, 0x50, 0x5f }	/* IOMS7, core 0 */
};

static const zen_pcie_core_info_t turin_bonus_map = {
	"P4", 0x80, 0x87, 0x80, 0x87		/* IOMS2, core 1 */
};

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

const zen_pcie_core_info_t *
turin_pcie_core_info(const uint8_t iomsno, const uint8_t coreno)
{
	if (coreno == TURIN_IOMS_BONUS_PCIE_CORENO)
		return (&turin_bonus_map);

	VERIFY3U(iomsno, <, ARRAY_SIZE(turin_lane_maps));
	return (&turin_lane_maps[iomsno]);
}

const zen_pcie_port_info_t *
turin_pcie_port_info(const uint8_t coreno, const uint8_t portno)
{
	return (&turin_pcie[coreno][portno]);
}

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

	ioms->zio_nbionum = TURIN_NBIO_NUM(iomsno);

	if (iomsno == TURIN_NBIO_BONUS_IOMS)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_BONUS;

	/*
	 * The even numbered IOMS instances are connected to the larger IOHC
	 * type.
	 */
	ioms->zio_iohctype = iomsno % 2 == 0 ? ZEN_IOHCT_LARGE :
	    ZEN_IOHCT_SMALL;

	/*
	 * The mapping between the IOMS instance number and the corresponding
	 * IOHC index is not straightforward. See "IOHC Instance Numbering"
	 * in the theory statement at the top of this file.
	 */
	const uint8_t iohcmap[] = { 0, 5, 1, 4, 2, 7, 3, 6 };
	VERIFY3U(iomsno, <, ARRAY_SIZE(iohcmap));
	ioms->zio_iohcnum = iohcmap[iomsno];

	/*
	 * nBIFs are actually associated with the NBIO instance but we have no
	 * representation in the fabric for NBIOs yet. Mark the first IOMS in
	 * each NBIO as holding the nBIFs.
	 */
	if (TURIN_NBIO_IOMS_NUM(iomsno) == 0)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_NBIF;
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

	switch (def.srd_unit) {
	case SMN_UNIT_IOAPIC:
		reg = turin_ioapic_smn_reg(ioms->zio_iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOHC:
		reg = turin_iohc_smn_reg(ioms->zio_iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = turin_ioagr_smn_reg(ioms->zio_iohcnum, def, reginst);
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
			VERIFY3U(ioms->zio_iohctype, ==, ZEN_IOHCT_LARGE);
			reg = turin_iommul1_ioagr_smn_reg(ioms->zio_iohcnum,
			    def, 0);
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
		VERIFY3U(ioms->zio_iohctype, ==, ZEN_IOHCT_LARGE);
		reg = turin_iommul2_smn_reg(ioms->zio_iohcnum, def, reginst);
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
	smn_reg_t reg;
	uint32_t val = 0;

	reg = turin_ioms_reg(ioms, D_IOHC_SB_LOCATION, 0);

	/*
	 * On the smaller IOHC instances, zero out IOHC::SB_LOCATION and we're
	 * done.
	 */
	if (ioms->zio_iohctype != ZEN_IOHCT_LARGE) {
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
	 * one on the first IOHUB in each NBIO.
	 */
	if (TURIN_NBIO_IOMS_NUM(ioms->zio_num) == 0) {
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
	const zen_iohc_type_t iohctype = nbif->zn_ioms->zio_iohctype;

	if (nbif->zn_num == 0 ||
	    (iohctype == ZEN_IOHCT_LARGE && nbif->zn_num == 2)) {
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
	const uint_t nroutes = ioms->zio_iohctype == ZEN_IOHCT_LARGE ?
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
turin_fabric_nbif_dev_straps(zen_nbif_t *nbif)
{
	const zen_iohc_type_t iohctype = nbif->zn_ioms->zio_iohctype;
	smn_reg_t reg;
	uint32_t intr;

	reg = turin_nbif_reg(nbif, D_NBIF_INTR_LINE_EN, 0);
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

		strapreg = turin_nbif_func_reg(func, D_NBIF_FUNC_STRAP0);
		strap = zen_nbif_func_read(func, strapreg);

		if ((func->znf_flags & ZEN_NBIF_F_ENABLED) != 0) {
			strap = NBIF_FUNC_STRAP0_SET_EXIST(strap, 1);
			intr = NBIF_INTR_LINE_EN_SET_I(intr,
			    func->znf_dev, func->znf_func, 1);

			/*
			 * Although the PPR suggests using 0x71 here, other AMD
			 * sources use 0x0, and experimentally the device
			 * actually ends up with a revision of 0x93 from
			 * somewhere.
			 */
			if (func->znf_type == ZEN_NBIF_T_SATA) {
				strap = NBIF_FUNC_STRAP0_SET_MAJ_REV(strap, 0);
				strap = NBIF_FUNC_STRAP0_SET_MIN_REV(strap, 0);
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
	 * timeouts on all three device straps for NBIF0, and the same for
	 * NBIF2 on the IOMS which are connected to a larger IOHC type.
	 */
	if (nbif->zn_num == 0 ||
	    (iohctype == ZEN_IOHCT_LARGE && nbif->zn_num == 2)) {
		for (uint8_t devno = 0; devno < TURIN_NBIF_MAX_DEVS; devno++) {
			smn_reg_t reg;
			uint32_t val;

			reg = turin_nbif_reg(nbif, D_NBIF_PORT_STRAP3, devno);
			val = zen_nbif_read(nbif, reg);
			val = NBIF_PORT_STRAP3_SET_COMP_TO(val, 1);
			zen_nbif_write(nbif, reg, val);
		}
	}
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

void
turin_iohc_enable_nmi(zen_ioms_t *ioms)
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
	reg = turin_ioms_reg(ioms, D_IOHC_INTR_CTL, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_INTR_CTL_SET_NMI_DEST_CTRL(v, 0);
	zen_ioms_write(ioms, reg, v);

	if ((zen_ioms_flags(ioms) & ZEN_IOMS_F_HAS_FCH) != 0) {
		reg = turin_ioms_reg(ioms, D_IOHC_PIN_CTL, 0);
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
	reg = turin_ioms_reg(ioms,
	    ioms->zio_iohctype == ZEN_IOHCT_LARGE ?
	    D_IOHC_MISC_RAS_CTL_L : D_IOHC_MISC_RAS_CTL_S, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_MISC_RAS_CTL_SET_NMI_SYNCFLOOD_EN(v, 1);
	zen_ioms_write(ioms, reg, v);
}

void
turin_iohc_nmi_eoi(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t v;

	reg = turin_ioms_reg(ioms, D_IOHC_FCTL2, 0);
	v = zen_ioms_read(ioms, reg);
	v = IOHC_FCTL2_GET_NMI(v);
	if (v != 0) {
		/*
		 * We have no ability to handle the other bits here, as
		 * those conditions may not have resulted in an NMI.  Clear only
		 * the bit whose condition we have handled.
		 */
		zen_ioms_write(ioms, reg, v);
		reg = turin_ioms_reg(ioms, D_IOHC_INTR_EOI, 0);
		v = IOHC_INTR_EOI_SET_NMI(0);
		zen_ioms_write(ioms, reg, v);
	}
}

static bool
turin_smu_set_features(zen_iodie_t *iodie,
    uint32_t features, uint32_t features_ext, uint32_t features64)
{
	zen_smu_rpc_t rpc = { 0 };
	zen_smu_rpc_res_t res;

	/*
	 * Early features in PEI are zeroed, but issuing this RPC
	 * seem to be important to enabling subsequent MPIO RPCs
	 * to succeed.
	 */
	rpc.zsr_req = ZEN_SMU_OP_ENABLE_FEATURE;
	rpc.zsr_args[0] = features;
	rpc.zsr_args[1] = features_ext;
	rpc.zsr_args[2] = features64;
	res = zen_smu_rpc(iodie, &rpc);
	if (res != ZEN_SMU_RPC_OK) {
		cmn_err(CE_WARN,
		    "Socket %u: SMU Enable Features RPC failed: %s (SMU 0x%x)",
		    iodie->zi_soc->zs_num, zen_smu_rpc_res_str(res),
		    rpc.zsr_resp);
		return (false);
	}
	cmn_err(CE_CONT,
	    "?Socket %u SMU features (0x%08x, 0x%08x, 0x%08x) enabled\n",
	    iodie->zi_soc->zs_num, features, features_ext, features64);

	return (true);
}

/*
 * Early features are zeroed.
 */
bool
turin_smu_early_features_init(zen_iodie_t *iodie)
{
	return (turin_smu_set_features(iodie, 0, 0, 0));
}

/*
 * Not all combinations of SMU features will result in correct system
 * behavior, so we therefore err on the side of matching stock platform
 * enablement -- even where that means enabling features with unknown
 * functionality.
 */

bool
turin_smu_features_init(zen_iodie_t *iodie)
{
	const uint32_t TURIN_FEATURES =
	    TURIN_SMU_FEATURE_DATA_CALCULATION |
	    TURIN_SMU_FEATURE_PPT |
	    TURIN_SMU_FEATURE_THERMAL_DESIGN_CURRENT |
	    TURIN_SMU_FEATURE_THERMAL |
	    TURIN_SMU_FEATURE_FIT |
	    TURIN_SMU_FEATURE_ELECTRICAL_DESIGN_CURRENT |
	    TURIN_SMU_FEATURE_CSTATE_BOOST |
	    TURIN_SMU_FEATURE_PROCESSOR_THROTTLING_TEMPERATURE |
	    TURIN_SMU_FEATURE_CORE_CLOCK_DPM |
	    TURIN_SMU_FEATURE_FABRIC_CLOCK_DPM |
	    TURIN_SMU_FEATURE_LCLK_DPM |
	    TURIN_SMU_FEATURE_PSI7 |
	    TURIN_SMU_FEATURE_LCLK_DEEP_SLEEP |
	    TURIN_SMU_FEATURE_DYNAMIC_VID_OPTIMIZER |
	    TURIN_SMU_FEATURE_CORE_C6 |
	    TURIN_SMU_FEATURE_DF_CSTATES |
	    TURIN_SMU_FEATURE_CLOCK_GATING |
	    TURIN_SMU_FEATURE_CPPC |
	    TURIN_SMU_FEATURE_GMI_FOLDING |
	    TURIN_SMU_FEATURE_XGMI_DLWM |
	    TURIN_SMU_FEATURE_PCC |
	    TURIN_SMU_FEATURE_FP_DIDT |
	    TURIN_SMU_FEATURE_MPDMA_TF_CLK_DEEP_SLEEP |
	    TURIN_SMU_FEATURE_MPDMA_PM_CLK_DEEP_SLEEP;
	const uint32_t FEATURES_EXT = TURIN_SMU_EXT_FEATURE_SOC_XVMIN;

	return (turin_smu_set_features(iodie, TURIN_FEATURES, FEATURES_EXT, 0));
}
