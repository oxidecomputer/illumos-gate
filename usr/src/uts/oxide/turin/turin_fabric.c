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
 * Various routines and things to access, initialize, understand, and manage
 * Turin's I/O fabric. This consists of both the data fabric and the
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
#include <sys/stdbool.h>

#include <sys/amdzen/fch/gpio.h>
#include <sys/amdzen/mmioreg.h>

#include <sys/io/zen/hacks.h>
#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/mpio.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/smn.h>
#include <sys/io/zen/smu_impl.h>

#include <sys/io/turin/fabric_impl.h>
#include <sys/io/turin/pcie_impl.h>
#include <sys/io/turin/pcie_rsmu.h>
#include <sys/io/turin/iohc.h>
#include <sys/io/turin/iomux.h>
#include <sys/io/turin/iommu.h>
#include <sys/io/turin/mpio_impl.h>
#include <sys/io/turin/nbif_impl.h>
#include <sys/io/turin/smu.h>
#include <sys/io/turin/ioapic.h>
#include <sys/io/turin/pptable.h>

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
    turin_pcie[TURIN_IOHC_MAX_PCIE_CORES][TURIN_PCIE_CORE_MAX_PORTS] = {
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
 * These are internal bridges.  They are modeled as ports but there is no
 * physical port brought out of the package.  Indexed by IOHC number, on
 * large IOHC's only (note that the large IOHCs have indices 0..3).
 */
const zen_iohc_nbif_ports_t turin_pcie_int_ports[TURIN_IOHC_PER_IODIE] = {
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
		.zinp_count = 1,
		.zinp_ports = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
		},
	},
	[3] = {
		.zinp_count = 2,
		.zinp_ports = {
			{ .zppi_dev = 0x7, .zppi_func = 0x1, },
			{ .zppi_dev = 0x7, .zppi_func = 0x2, },
		},
	},
	[4] = {
		.zinp_count = 0,
	},
	[5] = {
		.zinp_count = 0,
	},
	[6] = {
		.zinp_count = 0,
	},
	[7] = {
		.zinp_count = 0,
	},
};

/*
 * This table encodes the mapping of the set of dxio lanes to a given PCIe core
 * on an IOMS. The dxio engine uses different lane numbers than the phys. Note,
 * that all lanes here are inclusive. e.g. [start, end].
 * The subsequent tables encode mappings for the bonus cores.
 */
static const zen_pcie_core_info_t turin_lane_maps[8] = {
	/* name, DXIO start, DXIO end, PHY start, PHY end */
	{ "P0", 0x00, 0x0f, 0x00, 0x0f },	/* IOHC0, IOMS0, core 0 */
	{ "G0", 0x60, 0x6f, 0x60, 0x6f },	/* IOHC1, IOMS2, core 0 */
	{ "P2", 0x30, 0x3f, 0x30, 0x3f },	/* IOHC2, IOMS4, core 0 */
	{ "G2", 0x70, 0x7f, 0x70, 0x7f },	/* IOHC3, IOMS6, core 0 */
	{ "G1", 0x40, 0x4f, 0x40, 0x4f },	/* IOHC4, IOMS3, core 0 */
	{ "P1", 0x20, 0x2f, 0x20, 0x2f },	/* IOHC5, IOMS1, core 0 */
	{ "G3", 0x50, 0x5f, 0x50, 0x5f },	/* IOHC6, IOMS7, core 0 */
	{ "P3", 0x10, 0x1f, 0x10, 0x1f },	/* IOHC7, IOMS5, core 0 */
};

static const zen_pcie_core_info_t turin_bonus_map = {
	"P4", 0x80, 0x87, 0x80, 0x87		/* IOHC1, IOMS2, core 1 */
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

uint8_t
turin_fabric_ioms_nbio_num(uint8_t iomsno)
{
	return (TURIN_NBIO_NUM(iomsno));
}

/*
 * How many PCIe cores does this IOHC instance have?
 * If it's an IOHUB that has a bonus core then it will have the maximum
 * number, otherwise one fewer.
 */
uint8_t
turin_iohc_n_pcie_cores(const uint8_t iohcno)
{
	if (iohcno == TURIN_NBIO_BONUS_IOHC)
		return (TURIN_IOHC_MAX_PCIE_CORES);
	return (TURIN_IOHC_MAX_PCIE_CORES - 1);
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
	if (pcno == TURIN_IOHC_BONUS_PCIE_CORENO)
		return (TURIN_PCIE_CORE_BONUS_PORTS);
	return (TURIN_PCIE_CORE_MAX_PORTS);
}

const zen_pcie_core_info_t *
turin_pcie_core_info(const uint8_t iohcno, const uint8_t coreno)
{
	if (coreno == TURIN_IOHC_BONUS_PCIE_CORENO)
		return (&turin_bonus_map);

	VERIFY3U(iohcno, <, ARRAY_SIZE(turin_lane_maps));
	return (&turin_lane_maps[iohcno]);
}

const zen_pcie_port_info_t *
turin_pcie_port_info(const uint8_t coreno, const uint8_t portno)
{
	return (&turin_pcie[coreno][portno]);
}

bool
turin_fabric_smu_pptable_init(zen_fabric_t *fabric, void *pptable, size_t *len)
{
	const zen_iodie_t *iodie = &fabric->zf_socs[0].zs_iodies[0];
	const uint8_t maj = iodie->zi_smu_fw[0];
	const uint8_t min = iodie->zi_smu_fw[1];
	const x86_processor_family_t family =
	    chiprev_family(cpuid_getchiprev(CPU));

	/*
	 * The format of the PP table is consistent across several SMU
	 * versions. If we encounter a version we have not verified then we
	 * panic rather than risk loading incompatible data.
	 */
	bool valid = true;

	switch (family) {
	case X86_PF_AMD_TURIN:
		if (maj != 94 || min < 91 || min > 129)
			valid = false;
		break;
	case X86_PF_AMD_DENSE_TURIN:
		if (maj != 99 || min < 91 || min > 129)
			valid = false;
		break;
	default:
		valid = false;
		break;
	}

	if (!valid) {
		cmn_err(CE_PANIC,
		    "The PP table layout for SMU version %u.%u is unknown",
		    maj, min);
		/* NOTREACHED */
	}

	turin_pptable_v94_91_t *tpp = pptable;
	CTASSERT(sizeof (*tpp) <= MMU_PAGESIZE);
	VERIFY3U(sizeof (*tpp), <=, *len);

	/*
	 * Explicitly disable the overclocking part of the table.
	 */
	tpp->tpp_overclock.tppo_oc_dis = 1;

	/*
	 * Force cores on the same VDDCR_CPU voltage rail to run at the same
	 * frequency.
	 *
	 * This is a workaround for Erratum 1634: If Cores on the Same Voltage
	 * Supply Run at Different Frequencies, the System May Behave
	 * Unpredictably.
	 *
	 * Introduced in Turin PI 1.0.0.7 (SMU minor version 125/0x7D).
	 */
	if (min >= 125)
		tpp->tpp_cclk_mode = 1;

	/*
	 * Set platform-specific power and current limits.
	 */
	tpp->tpp_platform_limits.tppp_tdp = oxide_board_data->obd_tdp;
	tpp->tpp_platform_limits.tppp_ppt = oxide_board_data->obd_ppt;
	tpp->tpp_platform_limits.tppp_tdc = oxide_board_data->obd_tdc;
	tpp->tpp_platform_limits.tppp_edc = oxide_board_data->obd_edc;

#ifdef DEBUG
	cmn_err(CE_CONT, "?Set Platform TDP = 0x%x (%uW)\n",
	    tpp->tpp_platform_limits.tppp_tdp,
	    tpp->tpp_platform_limits.tppp_tdp);
	cmn_err(CE_CONT, "?Set Platform PPT = 0x%x (%uW)\n",
	    tpp->tpp_platform_limits.tppp_ppt,
	    tpp->tpp_platform_limits.tppp_ppt);
	cmn_err(CE_CONT, "?Set Platform TDC = 0x%x (%uA)\n",
	    tpp->tpp_platform_limits.tppp_tdc,
	    tpp->tpp_platform_limits.tppp_tdc);
	cmn_err(CE_CONT, "?Set Platform EDC = 0x%x (%uA)\n",
	    tpp->tpp_platform_limits.tppp_edc,
	    tpp->tpp_platform_limits.tppp_edc);
#endif

	*len = sizeof (*tpp);

	return (true);
}

void
turin_fabric_smu_pptable_post(zen_iodie_t *iodie)
{
	zen_platform_limits_t limits;

	if (zen_smu_rpc_get_platform_limits(iodie, &limits)) {
#ifdef DEBUG
		cmn_err(CE_CONT, "?TDP 0x%x [0x%x,0x%x]\n",
		    limits.zpl_tdp, limits.zpl_tdp_min, limits.zpl_tdp_max);
		cmn_err(CE_CONT, "?PPT 0x%x [,0x%x]\n",
		    limits.zpl_ppt, limits.zpl_ppt_max);
		cmn_err(CE_CONT, "?EDC 0x%x [,0x%x]\n",
		    limits.zpl_edc, limits.zpl_edc_max);
#endif
		iodie->zi_tdp = limits.zpl_tdp;
		iodie->zi_tdp_min = limits.zpl_tdp_min;
		iodie->zi_tdp_max = limits.zpl_tdp_max;
		iodie->zi_ppt = limits.zpl_ppt;
		iodie->zi_ppt_max = limits.zpl_ppt_max;
		iodie->zi_edc = limits.zpl_edc;
		iodie->zi_edc_max = limits.zpl_edc_max;
	}
}

/*
 * This is called from the common code, via an entry in the Turin version of
 * Zen fabric ops vector. The common code is responsible for the bulk of
 * initialization; we merely fill in those bits that are microarchitecture
 * specific.
 */
void
turin_fabric_nbio_init(zen_nbio_t *nbio)
{
	nbio->zn_sst_start = 0;
	nbio->zn_sst_count = TURIN_NBIO_SST_COUNT;

	/* There is no SST instance 0 on NBIO1 */
	if (nbio->zn_num == 1) {
		nbio->zn_sst_start++;
		nbio->zn_sst_count--;
	}
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
	/*
	 * The mapping between the IOMS instance number and the corresponding
	 * IOHC index is not straightforward. See "IOHC Instance Numbering"
	 * in the theory statement at the top of this file.
	 */
	const uint8_t iohcmap[] = { 0, 5, 1, 4, 2, 7, 3, 6 };
	const uint8_t index = ioms->zio_num;

	VERIFY3U(index, <, ARRAY_SIZE(iohcmap));
	ioms->zio_iohcnum = iohcmap[index];
	ioms->zio_iohubnum = TURIN_IOHC_IOHUB_NUM(ioms->zio_iohcnum);

	if (ioms->zio_iohcnum == TURIN_NBIO_BONUS_IOHC)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_BONUS;

	/*
	 * The even numbered IOMS instances are connected to the larger IOHC
	 * type.
	 */
	ioms->zio_iohctype = ioms->zio_num % 2 == 0 ? ZEN_IOHCT_LARGE :
	    ZEN_IOHCT_SMALL;


	/* Only the large IOHC types have nBIFs */
	if (ioms->zio_iohctype == ZEN_IOHCT_LARGE)
		ioms->zio_flags |= ZEN_IOMS_F_HAS_NBIF;
}

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
	const uint8_t iohcnum = pc->zpc_ioms->zio_iohcnum;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_IOHCDEV_PCIE:
		reg = turin_iohcdev_pcie_smn_reg(iohcnum, def,
		    pc->zpc_coreno, port->zpp_portno);
		break;
	case SMN_UNIT_PCIE_PORT:
		reg = turin_pcie_port_smn_reg(iohcnum, def,
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
	const uint8_t iohcnum = pc->zpc_ioms->zio_iohcnum;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_PCIE_CORE:
		reg = turin_pcie_core_smn_reg(iohcnum, def, pc->zpc_coreno);
		break;
	case SMN_UNIT_IOMMUL1:
		VERIFY3U(pc->zpc_coreno, ==, 0);
		reg = turin_iommul1_pcie_smn_reg(iohcnum, def, 0);
		break;
	case SMN_UNIT_IOMMUL1_IOAGR:
		/*
		 * The only ports accessed through the IOMMUL1's IO aggregator
		 * are on the (unused) bonus PCIe6 cores, which correspond to
		 * unit ID 0.  We don't use these, but AGESA sets them, so we do
		 * as well.
		 */
		VERIFY3U(pc->zpc_coreno, ==, TURIN_IOHC_BONUS_PCIE6_CORENO);
		reg = turin_iommul1_ioagr_pcie_smn_reg(iohcnum, def, 0);
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
	const uint8_t iohcnum = ioms->zio_iohcnum;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_IOAPIC:
		reg = turin_ioapic_smn_reg(iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOHC:
		reg = turin_iohc_smn_reg(iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = turin_ioagr_smn_reg(iohcnum, def, reginst);
		break;
	case SMN_UNIT_IOMMUL1:
		reg = turin_iommul1_pcie_smn_reg(iohcnum, def, 0);
		break;
	case SMN_UNIT_IOMMUL1_IOAGR:
		VERIFY3U(ioms->zio_iohctype, ==, ZEN_IOHCT_LARGE);
		reg = turin_iommul1_ioagr_pcie_smn_reg(iohcnum, def, 0);
		break;
	case SMN_UNIT_IOMMUL2:
		/*
		 * The L2IOMMU is only present in the larger IOHC instances.
		 */
		VERIFY3U(ioms->zio_iohctype, ==, ZEN_IOHCT_LARGE);
		reg = turin_iommul2_smn_reg(iohcnum, def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
		    def.srd_unit);
	}
	return (reg);
}

static smn_reg_t
turin_nbio_reg(const zen_nbio_t *const nbio, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const uint8_t nbionum = nbio->zn_num;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_SDPMUX:
		reg = turin_sdpmux_smn_reg(nbionum, def, reginst);
		break;
	case SMN_UNIT_SST:
		reg = turin_sst_smn_reg(nbionum, def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIO",
		    def.srd_unit);
	}
	return (reg);
}

static smn_reg_t
turin_nbif_reg(const zen_nbif_t *const nbif, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const uint8_t nbionum = nbif->zn_ioms->zio_nbio->zn_num;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF:
		reg = turin_nbif_smn_reg(nbionum, def, nbif->zn_num, reginst);
		break;
	case SMN_UNIT_NBIF_ALT:
		reg = turin_nbif_alt_smn_reg(nbionum, def, nbif->zn_num,
		    reginst);
		break;
	case SMN_UNIT_NBIF_ALT2:
		reg = turin_nbif_alt2_smn_reg(nbionum, def, nbif->zn_num,
		    reginst);
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
	const zen_nbif_t *nbif = func->znf_nbif;
	const uint8_t nbionum = nbif->zn_ioms->zio_nbio->zn_num;
	smn_reg_t reg;

	switch (def.srd_unit) {
	case SMN_UNIT_NBIF_FUNC:
		reg = turin_nbif_func_smn_reg(nbionum, def,
		    nbif->zn_num, func->znf_dev, func->znf_func);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for NBIF func",
		    def.srd_unit);
	}

	return (reg);
}

/*
 * XXX - stlouis#661 - Using addresses larger than 44-bits results in the
 * 64-bit BARs being unusable on Turin for reasons not yet understood.
 * Temporarily clamp the physical address size until this is resolved.
 */
uint8_t
turin_fabric_physaddr_size(void)
{
	uint8_t width = zen_fabric_physaddr_size();

	width = MIN(width, 44);

	return (width);
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

	reg = turin_ioms_reg(ioms, D_IOHC_DBG0, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_DBG0_SET_ROOT_STRMID(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
turin_fabric_nbio_features(zen_nbio_t *nbio)
{
	smn_reg_t reg;
	uint32_t val;

	for (uint16_t i = nbio->zn_sst_start;
	    i < nbio->zn_sst_start + nbio->zn_sst_count; i++) {
		reg = turin_nbio_reg(nbio, D_SST_DBG0, i);
		val = zen_nbio_read(nbio, reg);
		val = SST_DBG0_SET_LCLK_CTL_NBIO_DIS(val, 1);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio,
		    D_SST_RDRSPPOOLCREDIT_ALLOC_LO, i);
		val = zen_nbio_read(nbio, reg);
		val = SST_RDRSPPOOLCREDIT_ALLOC_LO_SET(val, 1);
		zen_nbio_write(nbio, reg, val);
	}
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
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOHC_SB_LOCATION, 0);

	/*
	 * On the smaller IOHC instances, zero out IOHC::SB_LOCATION and we're
	 * done.
	 */
	if (ioms->zio_iohctype != ZEN_IOHCT_LARGE) {
		zen_ioms_write(ioms, reg, 0);
		return;
	}

	/*
	 * If we do not have an FCH, we zero the IOHC SB location, otherwise, we
	 * do not touch it.
	 */
	if (!has_fch)
		zen_ioms_write(ioms, reg, 0);

	/*
	 * Unlike with earlier platforms where the value in IOHC::SB_LOCATION
	 * was copied across, on Turin we must explicitly set both the IOMMUL1
	 * IOAGR and IOMMUL2 registers to the same provided value.  Note that we
	 * do not set D_IOMMUL1_SB_LOCATION; neither does AGESA.
	 */
	val = 0;
	if (has_fch) {
		val = IOMMUL_SB_LOCATION_SET_CORE(0,
		    IOMMUL_SB_LOCATION_CORE_GPP2);
		val = IOMMUL_SB_LOCATION_SET_PORT(val,
		    IOMMUL_SB_LOCATION_PORT_A);
	}

	reg = turin_ioms_reg(ioms, D_IOMMUL1_IOAGR_SB_LOCATION, 0);
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
	for (uint_t i = 0; i < IOHC_SION_ENTS(ioms->zio_iohcnum); i++) {
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
	for (uint_t i = 0; i < IOHC_SION_ENTS(ioms->zio_iohcnum); i++) {
		reg = turin_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_LOW, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
		reg = turin_ioms_reg(ioms, D_IOAGR_SION_S0_CLIREQ_BURST_HI, i);
		zen_ioms_write(ioms, reg, IOAGR_SION_CLIREQ_BURST_VAL);
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
turin_fabric_nbio_arbitration(zen_nbio_t *nbio)
{
	smn_reg_t reg;
	uint32_t val;

	const uint_t sdpmux = nbio->zn_num;

	for (uint_t i = 0; i < SDPMUX_SION_MAX_ENTS; i++) {
		reg = SDPMUX_SION_S0_CLIREQ_BURST_LOW(sdpmux, i);
		zen_nbio_write(nbio, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = SDPMUX_SION_S0_CLIREQ_BURST_HI(sdpmux, i);
		zen_nbio_write(nbio, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

		reg = SDPMUX_SION_S1_CLIREQ_BURST_LOW(sdpmux, i);
		zen_nbio_write(nbio, reg, SDPMUX_SION_CLIREQ_BURST_VAL);
		reg = SDPMUX_SION_S1_CLIREQ_BURST_HI(sdpmux, i);
		zen_nbio_write(nbio, reg, SDPMUX_SION_CLIREQ_BURST_VAL);

		/*
		 * We set a number of values related to IOHC SDPMUX performance.
		 * These bits enable sending and receiving early ClkReq for
		 * various clients.
		 */
		reg = turin_nbio_reg(nbio, D_SDPMUX_DMA_OEWAKE_EN, 0);
		val = SDPMUX_DMA_OEWAKE_EN_SET_EGR(0,
		    SDPMUX_DMA_OEWAKE_EN_EGR_VAL);
		val = SDPMUX_DMA_OEWAKE_EN_SET_INGR(val,
		    SDPMUX_DMA_OEWAKE_EN_INGR_VAL);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio, D_SDPMUX_HST_OEWAKE_EN, 0);
		val = SDPMUX_HST_OEWAKE_EN_SET_EGR(0,
		    SDPMUX_HST_OEWAKE_EN_EGR_VAL);
		val = SDPMUX_HST_OEWAKE_EN_SET_INGR(val,
		    SDPMUX_HST_OEWAKE_EN_INGR_VAL);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio, D_SDPMUX_NTB_OEWAKE_EN, 0);
		val = SDPMUX_NTB_OEWAKE_EN_SET_EGR(0,
		    SDPMUX_NTB_OEWAKE_EN_EGR_VAL);
		val = SDPMUX_NTB_OEWAKE_EN_SET_INGR(val,
		    SDPMUX_NTB_OEWAKE_EN_INGR_VAL);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio, D_SDPMUX_DMA_CEWAKE_EN, 0);
		val = SDPMUX_DMA_CEWAKE_EN_SET_EGR(0,
		    SDPMUX_DMA_CEWAKE_EN_EGR_VAL);
		val = SDPMUX_DMA_CEWAKE_EN_SET_INGR(val,
		    SDPMUX_DMA_CEWAKE_EN_INGR_VAL);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio, D_SDPMUX_HST_CEWAKE_EN, 0);
		val = SDPMUX_HST_CEWAKE_EN_SET_EGR(0,
		    SDPMUX_HST_CEWAKE_EN_EGR_VAL);
		val = SDPMUX_HST_CEWAKE_EN_SET_INGR(val,
		    SDPMUX_HST_CEWAKE_EN_INGR_VAL);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio, D_SDPMUX_NTB_CEWAKE_EN, 0);
		val = SDPMUX_NTB_CEWAKE_EN_SET_EGR(0,
		    SDPMUX_NTB_CEWAKE_EN_EGR_VAL);
		val = SDPMUX_NTB_CEWAKE_EN_SET_INGR(val,
		    SDPMUX_NTB_CEWAKE_EN_INGR_VAL);
		zen_nbio_write(nbio, reg, val);
	}
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

void
turin_fabric_iohc_clock_gating(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOHC_GCG_LCLK_CTL0, 0);
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

	reg = turin_ioms_reg(ioms, D_IOHC_GCG_LCLK_CTL1, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK9(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK8(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK7(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK6(val,
	    ioms->zio_iohctype == ZEN_IOHCT_LARGE ? 1 : 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK5(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK4(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK3(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK2(val, 0);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK1(val,
	    ioms->zio_iohctype == ZEN_IOHCT_LARGE ? 0 : 1);
	val = IOHC_GCG_LCLK_CTL_SET_SOCLK0(val, 0);
	zen_ioms_write(ioms, reg, val);

	reg = turin_ioms_reg(ioms, D_IOHC_GCG_LCLK_CTL2, 0);
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

	reg = turin_ioms_reg(ioms, D_IOAGR_GCG_LCLK_CTL0, 0);
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

	reg = turin_ioms_reg(ioms, D_IOAGR_GCG_LCLK_CTL1, 0);
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

	/*
	 * Mask IOHC LCLK deep sleep for IOHUB2 since PCIE6 is not utilized on
	 * Turin.
	 */
	if (ioms->zio_iohubnum == 2) {
		reg = turin_ioms_reg(ioms, D_IOHC_NBIO_LCLK_DS_MASK, 0);
		zen_ioms_write(ioms, reg, TURIN_IOHC_BONUS_PCIE6_CORENO);
	}
}

void
turin_fabric_nbio_clock_gating(zen_nbio_t *nbio)
{
	smn_reg_t reg;
	uint32_t val;

	for (uint16_t i = nbio->zn_sst_start;
	    i < nbio->zn_sst_start + nbio->zn_sst_count; i++) {
		reg = turin_nbio_reg(nbio, D_SST_CLOCK_CTL, i);
		val = zen_nbio_read(nbio, reg);
		val = SST_CLOCK_CTL_SET_RXCLKGATE_EN(val, 1);
		val = SST_CLOCK_CTL_SET_TXCLKGATE_EN(val, 1);
		zen_nbio_write(nbio, reg, val);

		reg = turin_nbio_reg(nbio, D_SST_SION_WRAP_CFG_GCG_LCLK_CTL, i);
		val = zen_nbio_read(nbio, reg);
		val = SST_SION_WRAP_CFG_GCG_LCLK_CTL_SET_SOCLK4(val, 1);
		zen_nbio_write(nbio, reg, val);
	}
}

void
turin_fabric_nbif_clock_gating(zen_nbif_t *nbif)
{
	const zen_iohc_type_t iohctype = nbif->zn_ioms->zio_iohctype;
	smn_reg_t reg;
	uint32_t val;

	reg = turin_nbif_reg(nbif, D_NBIF_MGCG_CTL_LCLK, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_MGCG_CTL_LCLK_SET_EN(val, 1);
	zen_nbif_write(nbif, reg, val);

	/*
	 * LCLK deep sleep must be enabled in order for IOAGR to go idle.
	 * This kind of makes sense since the LCLK is the internal clock that's
	 * driving all of these devices. If the LCLK can't enter a deep sleep
	 * then there's no reason the IOAGR and other devices driven from it
	 * will be able to.
	 */
	reg = turin_nbif_reg(nbif, D_NBIF_DS_CTL_LCLK, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_DS_CTL_LCLK_SET_EN(val, 1);
	zen_nbif_write(nbif, reg, val);

	/*
	 * These registers are weird SYSHUB and nBIF crossovers in the
	 * alternate space, where there are only two nBIF instances.
	 */
	if (nbif->zn_num < 2) {
		reg = turin_nbif_reg(nbif, D_NBIF_HST_SION_CTL0, 0);
		val = zen_nbif_read(nbif, reg);

		val = NBIF_HST_SION_CTL0_SOCKL9(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL8(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL7(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL6(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL5(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL4(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL3(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL2(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL1(val, 1);
		val = NBIF_HST_SION_CTL0_SOCKL0(val, 1);

		val = NBIF_HST_SION_CTL1_SOCKL9(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL8(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL7(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL6(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL5(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL4(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL3(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL2(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL1(val, 1);
		val = NBIF_HST_SION_CTL1_SOCKL0(val, 1);

		zen_nbif_write(nbif, reg, val);

		reg = turin_nbif_reg(nbif, D_NBIF_ALT_GDC_HST_SION_CTL0, 0);
		val = zen_nbif_read(nbif, reg);

		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL9(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL8(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL7(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL6(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL5(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL4(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL3(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL2(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL1(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL0_SOCKL0(val, 1);

		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL9(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL8(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL7(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL6(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL5(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL4(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL3(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL2(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL1(val, 1);
		val = NBIF_ALT_GDC_HST_SION_CTL1_SOCKL0(val, 1);

		zen_nbif_write(nbif, reg, val);

		reg = turin_nbif_reg(nbif, D_NBIF_ALT_GDC_DMA_SION_CTL0, 0);
		val = zen_nbif_read(nbif, reg);

		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL9(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL8(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL7(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL6(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL5(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL4(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL3(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL2(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL1(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL0_SOCKL0(val, 1);

		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL9(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL8(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL7(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL6(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL5(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL4(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL3(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL2(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL1(val, 1);
		val = NBIF_ALT_GDC_DMA_SION_CTL1_SOCKL0(val, 1);

		zen_nbif_write(nbif, reg, val);

		reg = turin_nbif_reg(nbif, D_NBIF_ALT_NGDC_MGCG_CTL, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_NGDC_MGCG_CTL_SET_EN(val, 1);
		zen_nbif_write(nbif, reg, val);

		reg = turin_nbif_reg(nbif, D_NBIF_ALT_MGCG_CTL_SHCLK, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_MGCG_CTL_SHCLK_SET_EN(val, 1);
		zen_nbif_write(nbif, reg, val);

		reg = turin_nbif_reg(nbif, D_NBIF_ALT_MGCG_CTL_SCLK, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_ALT_MGCG_CTL_SCLK_SET_EN(val, 1);
		zen_nbif_write(nbif, reg, val);

		/*
		 * Enable SOCCLK and SHUBCLK deep sleep on large IOHCs.
		 */
		if (iohctype == ZEN_IOHCT_LARGE) {
			reg = turin_nbif_reg(nbif, D_NBIF_ALT_DS_CTL_SOCCLK, 0);
			val = zen_nbif_read(nbif, reg);
			val = NBIF_ALT_DS_CTL_SOCCLK_SET_EN(val, 1);
			zen_nbif_write(nbif, reg, val);

			reg = turin_nbif_reg(nbif,
			    D_NBIF_ALT_DS_CTL_SHUBCLK, 0);
			val = zen_nbif_read(nbif, reg);
			val = NBIF_ALT_DS_CTL_SHUBCLK_SET_EN(val, 1);
			zen_nbif_write(nbif, reg, val);
		}
	}

	if (nbif->zn_num == 2) {
		reg = turin_nbif_reg(nbif, D_NBIF_PG_MISC_CTL0, 0);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PG_MISC_CTL0_SET_LDMASK(val, 0);
		zen_nbif_write(nbif, reg, val);
	}
}

void
turin_fabric_ioapic_clock_gating(zen_ioms_t *ioms)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_ioms_reg(ioms, D_IOAPIC_GCG_LCLK_CTL0, 0);
	val = zen_ioms_read(ioms, reg);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK9(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK8(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK7(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK6(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK5(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK4(val, 0);
	val = IOAPIC_GCG_LCLK_CTL0_SET_SOCLK3(val, 0);
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

void
turin_fabric_nbif_init(zen_nbif_t *nbif)
{
	const uint8_t iohubno = nbif->zn_ioms->zio_iohubnum;

	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++) {
		zen_nbif_func_t *func = &nbif->zn_funcs[funcno];

		/*
		 * On Turin, nBIF2 and nBIF0's PSPCCP and ACP functions are
		 * only present on the first IOHC in each NBIO - that is the
		 * one which contains IOHUB0.
		 */
		if (iohubno != 0 && (nbif->zn_num > 1 ||
		    func->znf_type == ZEN_NBIF_T_PSPCCP ||
		    func->znf_type == ZEN_NBIF_T_ACP)) {
			func->znf_type = ZEN_NBIF_T_ABSENT;
			func->znf_flags = 0;
		}

		/* AER is enabled on USB and SATA devices */
		if (func->znf_type == ZEN_NBIF_T_USB ||
		    func->znf_type == ZEN_NBIF_T_SATA) {
			func->znf_flags |= ZEN_NBIF_F_AER_EN;
		}

		/* PM_STATUS is enabled for USB devices */
		if (func->znf_type == ZEN_NBIF_T_USB)
			func->znf_flags |= ZEN_NBIF_F_PMSTATUS_EN;
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
turin_fabric_nbif_dev_straps(zen_nbif_t *nbif)
{
	const uint8_t iohcno = nbif->zn_ioms->zio_iohcnum;
	const uint8_t iohubno = nbif->zn_ioms->zio_iohubnum;
	smn_reg_t intrreg, reg;
	uint32_t intr, val;

	intrreg = turin_nbif_reg(nbif, D_NBIF_INTR_LINE_EN, 0);
	intr = zen_nbif_read(nbif, intrreg);
	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++) {
		smn_reg_t strapreg;
		uint32_t strap;
		zen_nbif_func_t *func = &nbif->zn_funcs[funcno];

		strapreg = turin_nbif_func_reg(func, D_NBIF_FUNC_STRAP0);
		strap = zen_nbif_func_read(func, strapreg);

		if (func->znf_type == ZEN_NBIF_T_DUMMY) {
			/*
			 * AMD sources suggest that the device ID for the dummy
			 * device should be changed from the reset values of
			 * 0x1556 (nBIF0) and 0x1559 (nBIF2) to 0x14dc which is
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

		strapreg = turin_nbif_func_reg(func, D_NBIF_FUNC_STRAP2);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP2_SET_ACS_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_ACS_EN) ? 1 : 0);
		strap = NBIF_FUNC_STRAP2_SET_AER_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_AER_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);

		strapreg = turin_nbif_func_reg(func, D_NBIF_FUNC_STRAP3);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP3_SET_PM_STATUS_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_PMSTATUS_EN) ? 1 : 0);
		strap = NBIF_FUNC_STRAP3_SET_PANF_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_PANF_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);

		strapreg = turin_nbif_func_reg(func, D_NBIF_FUNC_STRAP4);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP4_SET_FLR_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_FLR_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);

		strapreg = turin_nbif_func_reg(func, D_NBIF_FUNC_STRAP7);
		strap = zen_nbif_func_read(func, strapreg);
		strap = NBIF_FUNC_STRAP7_SET_TPH_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_TPH_CPLR_EN) ? 1 : 0);
		strap = NBIF_FUNC_STRAP7_SET_TPH_CPLR_EN(strap,
		    (func->znf_flags & ZEN_NBIF_F_TPH_CPLR_EN) ? 1 : 0);
		zen_nbif_func_write(func, strapreg, strap);
	}

	zen_nbif_write(nbif, intrreg, intr);

	/*
	 * Each nBIF has up to two ports on it, though not all of them seem to
	 * be used. It's suggested that we enable completion timeouts on all
	 * port straps for nBIF0, and the same for nBIF2 where it exists.
	 */
	if (nbif->zn_num == 0 || nbif->zn_num == 2) {
		for (uint8_t devno = 0; devno < TURIN_NBIF_MAX_PORTS; devno++) {
			reg = turin_nbif_reg(nbif, D_NBIF_PORT_STRAP3, devno);
			val = zen_nbif_read(nbif, reg);
			val = NBIF_PORT_STRAP3_SET_COMP_TO(val, 1);
			zen_nbif_write(nbif, reg, val);
		}
	}

	/*
	 * Configure TLP processing hints completer support strap.
	 */
	for (uint8_t devno = 0; devno < TURIN_NBIF_MAX_PORTS; devno++) {
		reg = turin_nbif_reg(nbif, D_NBIF_PORT_STRAP6, devno);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP6_SET_TPH_CPLR_EN(val,
		    NBIF_PORT_STRAP6_TPH_CPLR_SUP);
		zen_nbif_write(nbif, reg, val);
	}

	/*
	 * For the root port functions within nBIF, program the B/D/F values
	 * and port number.
	 */
	ASSERT3U(iohcno, <, ARRAY_SIZE(turin_pcie_int_ports));
	const zen_iohc_nbif_ports_t *ports = &turin_pcie_int_ports[iohcno];
	for (uint8_t i = 0; i < ports->zinp_count; i++) {
		const zen_pcie_port_info_t *port = &ports->zinp_ports[i];

		reg = turin_nbif_reg(nbif, D_NBIF_PORT_STRAP7, i);
		val = zen_nbif_read(nbif, reg);
		val = NBIF_PORT_STRAP7_SET_BUS(val,
		    nbif->zn_ioms->zio_pci_busno);
		val = NBIF_PORT_STRAP7_SET_DEV(val, port->zppi_dev);
		val = NBIF_PORT_STRAP7_SET_FUNC(val, port->zppi_func);
		val = NBIF_PORT_STRAP7_SET_PORT(val,
		    (port->zppi_dev << 4) | port->zppi_func);
		zen_nbif_write(nbif, reg, val);
	}

	reg = turin_nbif_reg(nbif, D_NBIF_BIFC_GMI_SDP_REQ_PCRED, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_BIFC_GMI_SDP_REQ_PCRED_SET_VC5(val, 1);
	if (iohubno == 2)
		val = NBIF_BIFC_GMI_SDP_REQ_PCRED_SET_VC4(val, 1);
	zen_nbif_write(nbif, reg, val);

	reg = turin_nbif_reg(nbif, D_NBIF_BIFC_GMI_SDP_DAT_PCRED, 0);
	val = zen_nbif_read(nbif, reg);
	val = NBIF_BIFC_GMI_SDP_DAT_PCRED_SET_VC5(val, 1);
	if (iohubno == 2)
		val = NBIF_BIFC_GMI_SDP_DAT_PCRED_SET_VC4(val, 1);
	zen_nbif_write(nbif, reg, val);
}

/*
 * These are the tile ID mappings that firmware uses specifically for hotplug.
 */
typedef enum turin_pci_hotplug_tile_id {
	TURIN_HOTPLUG_TILE_P0 = 0,
	TURIN_HOTPLUG_TILE_G0,
	TURIN_HOTPLUG_TILE_P2,
	TURIN_HOTPLUG_TILE_G2,
	TURIN_HOTPLUG_TILE_G1,
	TURIN_HOTPLUG_TILE_P1,
	TURIN_HOTPLUG_TILE_G3,
	TURIN_HOTPLUG_TILE_P3,
} turin_pci_tile_hotplug_id_t;

/*
 * Translates from our internal OXIO tile identifier to an integer understood by
 * Turin's hotplug firmware.
 */
uint8_t
turin_fabric_hotplug_tile_id(const oxio_engine_t *oxio)
{
	VERIFY3P(oxio->oe_type, ==, OXIO_ENGINE_T_PCIE);
	ASSERT3U(oxio->oe_tile, <=, TURIN_HOTPLUG_TILE_P3);

	switch (oxio->oe_tile) {
	case OXIO_TILE_G0:
		return (TURIN_HOTPLUG_TILE_G0);
	case OXIO_TILE_P0:
		return (TURIN_HOTPLUG_TILE_P0);
	case OXIO_TILE_G1:
		return (TURIN_HOTPLUG_TILE_G1);
	case OXIO_TILE_P1:
		return (TURIN_HOTPLUG_TILE_P1);
	case OXIO_TILE_G2:
		return (TURIN_HOTPLUG_TILE_G2);
	case OXIO_TILE_P2:
		return (TURIN_HOTPLUG_TILE_P2);
	case OXIO_TILE_G3:
		return (TURIN_HOTPLUG_TILE_G3);
	case OXIO_TILE_P3:
		return (TURIN_HOTPLUG_TILE_P3);
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
turin_fabric_hotplug_port_init(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	ASSERT3U(port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG, !=, 0);

	/*
	 * Set the hotplug slot information in the PCIe IP, presumably so that
	 * it'll do something useful for the SMU/MPIO.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_HP_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_HP_CTL_SET_SLOT(val, port->zpp_slotno);
	val = PCIE_PORT_HP_CTL_SET_ACTIVE(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * This register appears to ensure that we don't remain in the detect
	 * state machine state.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_CTL5);
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
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_TRAINBITS_DIS(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Make sure that power faults can actually work (in theory).
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_PCTL);
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
turin_fabric_hotplug_port_unblock_training(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;
	zen_pcie_core_t *pc = port->zpp_core;

	ASSERT3U(port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG, !=, 0);

	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_SWRST_CTL6);
	val = zen_pcie_core_read(pc, reg);
	val = bitset32(val, port->zpp_portno, port->zpp_portno, 0);
	zen_pcie_core_write(pc, reg, val);
}

/*
 * Prepares the PCIe core for hotplug by ensuring that presence detect mux
 * select is set to a logical "OR" of in-band and out-of-band PD signals.
 */
void
turin_fabric_hotplug_core_init(zen_pcie_core_t *pc)
{
	smn_reg_t reg;
	uint32_t val;

	ASSERT3U(pc->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG, !=, 0);

	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_PRES);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PRES_SET_MODE(val, PCIE_CORE_PRES_MODE_OR);
	zen_pcie_core_write(pc, reg, val);

	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_COMMON_AER_MASK);
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
typedef struct turin_hotplug_start_flags {		/* Bit Offset */
	uint32_t	thsf_slot_index:4;		/*  0 */
	uint32_t	thsf_mode:4;			/*  4 */
	uint32_t	thsf_settle_time:8;		/*  8 */
	uint32_t	thsf_presence_detect_settle:1;	/* 16 */
	uint32_t	thsf_settle_time_multiplier:2;	/* 17 */
	uint32_t	thsf_dlpc_count:4;		/* 19 */
	uint32_t	thsf_dis_bridgedis_ctl:1;	/* 23 */
} turin_hotplug_start_flags_t;

bool
turin_fabric_hotplug_start(zen_iodie_t *iodie)
{
	uint32_t flags;
	turin_hotplug_start_flags_t *fp;

	flags = 0;
	fp = (turin_hotplug_start_flags_t *)&flags;
	fp->thsf_dlpc_count = 3;

	return (zen_mpio_rpc_start_hotplug(iodie, flags));
}

/*
 * Do everything else required to finish configuring the nBIF and get the PCIe
 * engine up and running.
 */
void
turin_fabric_pcie(zen_fabric_t *fabric)
{
	zen_mpio_pcie_init(fabric);
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

void
turin_pcie_dbg_signal(void)
{
	static bool gpio_configured;

	/*
	 * On Cosmo, we want to signal via GPIO that we're collecting register
	 * data. We use AGPIO22 (SP5_TO_FPGA1_DEBUG_2) for this and will toggle
	 * this pin's state each time we collect registers. This allows
	 * someone using a logic analyser to look at low-speed signals to
	 * correlate those observations with these register values. The
	 * register values are not a snapshot, but we do collect the timestamp
	 * associated with each one so it's at least possible to reassemble a
	 * complete strip chart with coordinated timestamps.
	 *
	 * If this is the first time we're using the GPIO, we will reset its
	 * output, then toggle it twice at 1 microsecond intervals to provide a
	 * clear start time (since the GPIO was previously an input and would
	 * have read at an undefined level).
	 */
	if (oxide_board_data->obd_board != OXIDE_BOARD_COSMO)
		return;

	if (!gpio_configured) {
		zen_hack_gpio_config(22, TURIN_FCH_IOMUX_22_AGPIO22);
		zen_hack_gpio(ZHGOP_TOGGLE, 22);
		drv_usecwait(1);
		gpio_configured = true;
	}
	zen_hack_gpio(ZHGOP_TOGGLE, 22);
}

void
turin_set_mpio_global_config(zen_mpio_global_config_t *zconfig)
{
	/*
	 * Note: This CTASSERT is not in turin/mpio.h because
	 * zen_mpio_global_config_t is not visible there.
	 */
	CTASSERT(sizeof (turin_mpio_global_config_t) ==
	    sizeof (zen_mpio_global_config_t));

	turin_mpio_global_config_t *config =
	    (turin_mpio_global_config_t *)zconfig;
	config->tmgc_skip_vet = 1;
	config->tmgc_use_phy_sram = 1;
	config->tmgc_valid_phy_firmware = 1;
	config->tmgc_en_pcie_noncomp_wa = 1;
	config->tmgc_pwr_mgmt_clk_gating = 1;
	config->tmgc_2spc_gen4_en = 1;
	config->tmgc_2spc_gen5_en = 1;
	config->tmgc_tx_fifo_rd_ptr_offset = TURIN_TX_FIFO_READ_PTR_VAL;
}

/*
 * Not all combinations of SMU features will result in correct system
 * behavior, so we therefore err on the side of matching stock platform
 * enablement -- even where that means enabling features with unknown
 * functionality.
 */
void
turin_smu_features_init(zen_iodie_t *iodie)
{
	/*
	 * We keep close to the default bits set by AGESA 1.0.0.0.  Note that
	 * CPPC is optional, but is explicitly set by AGESA, so we do that here
	 * as well.
	 */
	const uint32_t features =
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
	const uint32_t features_ext = TURIN_SMU_EXT_FEATURE_SOC_XVMIN;

	VERIFY(zen_smu_set_features(iodie, features, features_ext));
}

/*
 * These PCIe straps need to be set after mapping is done, but before link
 * training has started.  While we do not understand in detail what all of these
 * registers do, we have broadly split them into 2 categories:
 *
 * 1. Straps where:
 *     a. the defaults in hardware seem to be reasonable given our, sometimes
 *        limited, understanding of their function
 *     b. are not features/parameters that we currently care specifically about
 *        one way or the other
 *     c. and we are currently ok with the defaults changing out from underneath
 *        us on different hardware revisions unless proven otherwise.
 * or,
 * 2. where:
 *     a. we care specifically about a feature enough to ensure that it is set
 *        (e.g. AERs) or purposefully disabled (e.g. I2C_DBG_EN)
 *     b. we are not ok with these changing based on potentially different
 *        defaults set in different hardware revisions
 *
 * For (1), we've chosen to leave them based on whatever the hardware has chosen
 * for the default, while all the straps detailed underneath fall into category
 * (2).  Note that this list is by no means definitive, and will almost
 * certainly change as our understanding of what we require from the hardware
 * evolves.
 */

/*
 * PCIe Straps that we unconditionally set to 1
 */
static const uint32_t turin_pcie_strap_enable[] = {
	TURIN_STRAP_PCIE_MSI_EN,
	TURIN_STRAP_PCIE_AER_EN,
	TURIN_STRAP_PCIE_GEN2_FEAT_EN,
	TURIN_STRAP_PCIE_NPEM_EN,
	TURIN_STRAP_PCIE_CPL_TO_EN,	/* We want completion timeouts */
	TURIN_STRAP_PCIE_TPH_EN,
	TURIN_STRAP_PCIE_MULTI_FUNC_EN,
	TURIN_STRAP_PCIE_DPC_EN,
	TURIN_STRAP_PCIE_ARI_EN,
	TURIN_STRAP_PCIE_PL_16G_EN,
	TURIN_STRAP_PCIE_PL_32G_EN,
	TURIN_STRAP_PCIE_LANE_MARGIN_EN,
	TURIN_STRAP_PCIE_LTR_SUP,
	TURIN_STRAP_PCIE_LINK_BW_NOTIF_SUP,
	TURIN_STRAP_PCIE_GEN3_1_FEAT_EN,
	TURIN_STRAP_PCIE_GEN4_FEAT_EN,
	TURIN_STRAP_PCIE_GEN5_FEAT_EN,
	TURIN_STRAP_PCIE_ECRC_GEN_EN,
	TURIN_STRAP_PCIE_SWUS_ECRC_GEN_EN,
	TURIN_STRAP_PCIE_ECRC_CHECK_EN,
	TURIN_STRAP_PCIE_SWUS_ECRC_CHECK_EN,
	TURIN_STRAP_PCIE_SWUS_ARI_EN,
	TURIN_STRAP_PCIE_CPL_ABORT_ERR_EN,
	TURIN_STRAP_PCIE_INT_ERR_EN,
	TURIN_STRAP_SURPRISE_DOWN_ERR_EN,
	TURIN_STRAP_PCIE_SWUS_AER_EN,
	TURIN_STRAP_PCIE_P_ERR_COR_EN,
	TURIN_STRAP_PCIE_DOE_EN,

	/* ACS straps */
	TURIN_STRAP_PCIE_ACS_EN,
	TURIN_STRAP_PCIE_ACS_SRC_VALID,
	TURIN_STRAP_PCIE_ACS_TRANS_BLOCK,
	TURIN_STRAP_PCIE_ACS_DIRECT_TRANS_P2P,
	TURIN_STRAP_PCIE_ACS_P2P_CPL_REDIR,
	TURIN_STRAP_PCIE_ACS_P2P_REQ_RDIR,
	TURIN_STRAP_PCIE_ACS_UPSTREAM_FWD,
};

/*
 * PCIe Straps that we unconditionally set to 0
 * These are generally debug and test settings that are usually not a good idea
 * in my experience to allow accidental enablement.
 */
static const uint32_t turin_pcie_strap_disable[] = {
	TURIN_STRAP_PCIE_I2C_DBG_EN,
	TURIN_STRAP_PCIE_DEBUG_RXP,
	TURIN_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST,
	TURIN_STRAP_PCIE_ERR_REPORT_DIS,
	TURIN_STRAP_PCIE_TX_TEST_ALL,
	TURIN_STRAP_PCIE_MCAST_EN,
	TURIN_STRAP_PCIE_DESKEW_EMPTY,
	TURIN_STRAP_PCIE_MARGIN_IGN_C_SKP,
	/*
	 * We do not currently enable CXL support, so we disable alternative
	 * protocol negotiations.
	 */
	TURIN_STRAP_PCIE_P_ALT_PROT_EN,
};

/*
 * PCIe Straps that have other values.
 */
static const zen_pcie_strap_setting_t turin_pcie_strap_settings[] = {
	{
		.strap_reg = TURIN_STRAP_PCIE_PLL_FREQ_MODE,
		.strap_data = 3,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_EQ_DS_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_EQ_US_RX_PRESET_HINT,
		.strap_data = PCIE_GEN3_RX_PRESET_9DB,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_16GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_16GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_32GT_EQ_DS_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_7,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_32GT_EQ_US_TX_PRESET,
		.strap_data = PCIE_TX_PRESET_4,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_DLF_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_SUBVID,
		.strap_data = PCI_VENDOR_ID_OXIDE,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_SUBDID,
		.strap_data = PCI_SDID_OXIDE_COSMO_BASE,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY
	},

};

/*
 * PCIe Straps that exist on a per-port level.  Most pertain to the port itself;
 * others pertain to features exposed via the associated bridge.
 */
static const zen_pcie_strap_setting_t turin_pcie_port_settings[] = {
	{
		.strap_reg = TURIN_STRAP_PCIE_P_EXT_FMT_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_E2E_TLP_PREFIX_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_10B_TAG_CMPL_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_10B_TAG_REQ_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_TCOMMONMODE_TIME,
		.strap_data = 0xa,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_TPON_SCALE,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_TPON_VALUE,
		.strap_data = 0xf,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_DLF_SUP,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_DLF_EXCHANGE_EN,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_WRP_MISC,
		.strap_data = TURIN_STRAP_PCIE_WRP_MISC_SSID_EN,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_FOM_TIME,
		.strap_data = TURIN_STRAP_PCIE_P_FOM_300US,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_SPC_MODE_8GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_SPC_MODE_16GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_SPC_MODE_32GT,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_32GT_PRECODE_REQ,
		.strap_data = 0x1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_L0s_EXIT_LAT,
		.strap_data = PCIE_LINKCAP_L0S_EXIT_LAT_MAX >> 12,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_L0_TO_L0s_DIS,
		.strap_data = 1,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY,
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_EQ_BYPASS_TO_HR_ADV,
		.strap_data = 0,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = TURIN_IOHC_BONUS_PCIE_CORENO,
		.strap_portmatch = PCIE_PORTMATCH_ANY,
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_PM_SUB_SUP,
		.strap_data = 0,
		.strap_nodematch = PCIE_NODEMATCH_ANY,
		.strap_iohcmatch = PCIE_IOHCMATCH_ANY,
		.strap_corematch = PCIE_COREMATCH_ANY,
		.strap_portmatch = PCIE_PORTMATCH_ANY,
	},
	/*
	 * Enable SRIS and associated parameters on the backplane port which
	 * is node 0, P0 (IOHC 0, Core 0, Port 1).
	 */
	{
		.strap_reg = TURIN_STRAP_PCIE_P_SRIS_EN,
		.strap_data = 0x1,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = 0,
		.strap_iohcmatch = 0,
		.strap_corematch = 0,
		.strap_portmatch = 1
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP,
		.strap_data = 0,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = 0,
		.strap_iohcmatch = 0,
		.strap_corematch = 0,
		.strap_portmatch = 1
	},
	{
		.strap_reg = TURIN_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP,
		.strap_data = 0,
		.strap_boardmatch = OXIDE_BOARD_COSMO,
		.strap_nodematch = 0,
		.strap_iohcmatch = 0,
		.strap_corematch = 0,
		.strap_portmatch = 1
	}
};

static void
turin_fabric_write_pcie_strap(zen_pcie_core_t *pc,
    const uint32_t reg, const uint32_t data)
{
	const zen_ioms_t *ioms = pc->zpc_ioms;
	uint32_t inst;

	inst = ioms->zio_iohcnum;
	if (pc->zpc_coreno == TURIN_IOHC_BONUS_PCIE_CORENO)
		inst = 8;

	zen_mpio_write_pcie_strap(pc, reg + (inst << 16), data);
}

/*
 * Returns true IFF the given IOHC number corresponds to a P link,
 * and not a G link.
 */
static bool
turin_iohc_is_p_link(const uint8_t iohcno)
{
	switch (iohcno) {
	case 0:
	case 2:
	case 5:
	case 7:
		return (true);
	default:
		return (false);
	}
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
turin_fabric_init_pcie_straps(zen_pcie_core_t *pc)
{
	for (size_t i = 0; i < ARRAY_SIZE(turin_pcie_strap_enable); i++) {
		turin_fabric_write_pcie_strap(pc,
		    turin_pcie_strap_enable[i], 0x1);
	}
	for (size_t i = 0; i < ARRAY_SIZE(turin_pcie_strap_disable); i++) {
		turin_fabric_write_pcie_strap(pc,
		    turin_pcie_strap_disable[i], 0x0);
	}
	for (size_t i = 0; i < ARRAY_SIZE(turin_pcie_strap_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &turin_pcie_strap_settings[i];

		if (zen_fabric_pcie_strap_matches(pc, PCIE_PORTMATCH_ANY,
		    strap)) {
			turin_fabric_write_pcie_strap(pc,
			    strap->strap_reg, strap->strap_data);
		}
	}

	/*
	 * As an exception to our general rule of not handling CXL, if we're
	 * for the CXL-capable bridges we set the CXL base SMN address.  AGESA
	 * always does this.
	 */
	const uint8_t iohcno = pc->zpc_ioms->zio_iohcnum;
	if (turin_iohc_is_p_link(iohcno)) {
		turin_fabric_write_pcie_strap(pc, TURIN_STRAP_PCIE_CXL_SMN_BASE,
		    TURIN_STRAP_PCIE_CXL_SMN_BASE_OFFSET + iohcno);
	}

	/* Handle per bridge initialization */
	for (size_t i = 0; i < ARRAY_SIZE(turin_pcie_port_settings); i++) {
		const zen_pcie_strap_setting_t *strap =
		    &turin_pcie_port_settings[i];
		for (uint8_t j = 0; j < pc->zpc_nports; j++) {
			if (zen_fabric_pcie_strap_matches(pc, j, strap)) {
				turin_fabric_write_pcie_strap(pc,
				    strap->strap_reg +
				    (j * TURIN_STRAP_PCIE_NUM_PER_PORT),
				    strap->strap_data);
			}
		}
	}
}

void
turin_fabric_init_pcie_port(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Turn off unused lanes.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_WIDTH_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_TURN_OFF_UNUSED_LANES(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Ensure the FAPE registers are zeroed.  This is the reset value, but
	 * AGESA is explicit about initializing them and so are we.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_FAPE_CTL_8GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_FAPE_CTL_8GT_SET_EN(val, 0);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_FAPE_CTL_16GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_FAPE_CTL_16GT_SET_EN(val, 0);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_FAPE_CTL_32GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_FAPE_CTL_32GT_SET_EN(val, 0);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Disable TLP flushes on data-link down, and allow the completion pass.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_TX_PORT_CTL1);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_TX_PORT_CTL1_SET_TLP_FLUSH_DOWN_DIS(val, 0);
	val = PCIE_PORT_TX_PORT_CTL1_SET_CPL_PASS(val, 1);
	zen_pcie_port_write(port, reg, val);
}

void
turin_fabric_init_pcie_port_after_reconfig(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Set search equalization modes.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_8GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_8GT_SET_SEARCH_MODE(val,
	    PCIE_PORT_LC_EQ_CTL_8GT_SEARCH_MODE_PRST);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_16GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_16GT_SET_SEARCH_MODE(val,
	    PCIE_PORT_LC_EQ_CTL_16GT_SEARCH_MODE_PRST);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_EQ_CTL_32GT);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_EQ_CTL_32GT_SET_SEARCH_MODE(val,
	    PCIE_PORT_LC_EQ_CTL_32GT_SEARCH_MODE_PRST);
	zen_pcie_port_write(port, reg, val);

	/*
	 * Set preset masks.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_PRST_MASK_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_MASK_8GT(val,
	    PCIE_PORT_LC_PRST_MASK_CTL_8GT_VAL);
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_MASK_16GT(val,
	    PCIE_PORT_LC_PRST_MASK_CTL_16GT_VAL);

	/* The Gen5 value can be overridden per board */
	const uint32_t mask32gt =
	    oxide_board_data->obd_pcie_gen5_eq_preset_mask != 0 ?
	    oxide_board_data->obd_pcie_gen5_eq_preset_mask :
	    PCIE_PORT_LC_PRST_MASK_CTL_32GT_VAL;
	val = PCIE_PORT_LC_PRST_MASK_CTL_SET_MASK_32GT(val, mask32gt);

	zen_pcie_port_write(port, reg, val);

	/*
	 * Fixups that are specific to Turin Cx parts.  These are undocumented.
	 */
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_C0) &&
	    port->zpp_core->zpc_coreno != TURIN_IOHC_BONUS_PCIE_CORENO) {
		reg = turin_pcie_port_reg(port, D_PCIE_PORT_HW_DBG_LC);
		val = zen_pcie_port_read(port, reg);
		switch (port->zpp_portno) {
		case 0:
			/*
			 * AGESA sets these bits seprately, in two RMW cycles,
			 * but we just do it in one.
			 */
			val = PCIE_PORT_HW_DBG_LC_SET_DBG09(val, 1);
			val = PCIE_PORT_HW_DBG_LC_SET_DBG05(val, 1);
			break;
		case 1:
			/*
			 * As above, AGESA does these separately, but we combine
			 * them.
			 */
			val = PCIE_PORT_HW_DBG_LC_SET_DBG10(val, 1);
			val = PCIE_PORT_HW_DBG_LC_SET_DBG05(val, 1);
			break;
		case 2:
			val = PCIE_PORT_HW_DBG_LC_SET_DBG10(val, 1);
			break;
		case 3:
			val = PCIE_PORT_HW_DBG_LC_SET_DBG11(val, 1);
		}
		zen_pcie_port_write(port, reg, val);
	}
}

static void
turin_hide_nbif_bridge(zen_ioms_t *ioms, uint8_t portno)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_iohcdev_nbif_smn_reg(ioms->zio_iohcnum,
	    D_IOHCDEV_NBIF_BRIDGE_CTL, 0, portno);
	val = zen_ioms_read(ioms, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 1);
	zen_ioms_write(ioms, reg, val);
}

static void
turin_hide_pci_bridge(zen_ioms_t *ioms, uint8_t coreno, uint8_t portno)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_iohcdev_pcie_smn_reg(ioms->zio_iohcnum,
	    D_IOHCDEV_PCIE_BRIDGE_CTL, coreno, portno);
	val = zen_ioms_read(ioms, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 1);
	zen_ioms_write(ioms, reg, val);
}

void
turin_fabric_hide_bridge(zen_pcie_port_t *port)
{
	const zen_pcie_core_t *pc = port->zpp_core;

	turin_hide_pci_bridge(pc->zpc_ioms, pc->zpc_coreno, port->zpp_portno);
}

void
turin_fabric_unhide_bridge(zen_pcie_port_t *port)
{
	smn_reg_t reg;
	uint32_t val;

	/*
	 * All bridges need to be visible before we attempt to
	 * configure MPIO.
	 */
	reg = turin_pcie_port_reg(port, D_IOHCDEV_PCIE_BRIDGE_CTL);
	val = zen_pcie_port_read(port, reg);
	val = IOHCDEV_BRIDGE_CTL_SET_CRS_ENABLE(val, 1);
	val = IOHCDEV_BRIDGE_CTL_SET_BRIDGE_DISABLE(val, 0);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_BUS_MASTER(val, 0);
	val = IOHCDEV_BRIDGE_CTL_SET_DISABLE_CFG(val, 0);
	zen_pcie_port_write(port, reg, val);
}

/*
 * Here we are going through bridges and need to start setting them up with the
 * various features that we care about. Most of these are an attempt to have
 * things set up so PCIe enumeration can meaningfully actually use these. The
 * exact set of things required is ill-defined. Right now this means enabl¡ing
 * the bridges such that they can actually allow software to use them.
 *
 * XXX: We really should disable DMA until the rest of the system is set up and
 * ready to use it.
 *
 * Note that AGESA makes some adjustments to PCIEPORT::PCIE_LC_CNTL4 related to
 * L1, L1.1 and L1.2 states, which we are not using and do not touch.
 */
void
turin_fabric_init_bridge(zen_pcie_port_t *port)
{
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;
	smn_reg_t reg;
	uint32_t val;

	/*
	 * Make sure the hardware knows the corresponding b/d/f for this bridge.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_TX_ID);
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
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL_SET_L1_IMM_ACK(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_TRAIN_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_TRAIN_CTL_SET_L0S_L1_TRAIN(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_WIDTH_CTL);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_DUAL_RECONFIG(val, 1);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_L1_RECONFIG_EN(val, 1);
	val = PCIE_PORT_LC_WIDTH_CTL_SET_RENEG_EN(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_CTL2);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL2_SET_ELEC_IDLE(val,
	    PCIE_PORT_LC_CTL2_ELEC_IDLE_M1);
	val = PCIE_PORT_LC_CTL2_WAIT_OTHER_LANES_MODE(val, 1);
	zen_pcie_port_write(port, reg, val);

	reg = turin_pcie_port_reg(port, D_PCIE_PORT_LC_CTL3);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_LC_CTL3_SET_DOWN_SPEED_CHANGE(val, 1);
	zen_pcie_port_write(port, reg, val);

	/*
	 * AMD's current default is to disable certain classes of receiver
	 * errors. XXX We need to understand why.
	 */
	reg = turin_pcie_port_reg(port, D_PCIE_PORT_HW_DBG_LC);
	val = zen_pcie_port_read(port, reg);
	val = PCIE_PORT_HW_DBG_LC_SET_DBG15(val, 1);
	zen_pcie_port_write(port, reg, val);
}

/*
 * On Turin, we have to hide unused bridges on the "large" IOHCs.
 *
 * There are two internal ports on each large IOHC for nBIF: device 7, functions
 * 1 and 2, correspondiing to nBIF0 Ports 0 and 1.  The second port is used for
 * SATA but is not present, and thus needs hiding on IOHC 1 and 2 (aka. IOMS 2
 * and 4, aka. NBIO0/IOHUB2 and NBIO1/IOHUB0).  Note that these happen to be the
 * IOHCs which have the bonus core and FCH respectively, which is perhaps not a
 * coincidence.
 *
 * The way we model IOHC::IOHC_Bridge_CNTL is as a set of units like this, from
 * right to left:
 *
 * Unit 0 - IOHC0PCIE0DEVINDCFG[8:0] - PCIe core with 9 ports
 * Unit 1 - IOHC0PCIE5DEVINDCFG[7:0] - Bonus PCIe core with 8 ports
 * Unit 2 - IOHC0PCIE6DEVINDCFG[2:0] - Unused PCIe core with 3 ports
 * Unit 3 - IOHC0NBIF1DEVINDCFG[1:0] - nBIF device with 2 ports
 * Unit 4 - IOHC0INTSBDEVINDCFG0
 *
 * This is why we always select unit 0 in turin_hide_nbif_bridge above: there is
 * only one nBIF unit in the bridge control register and
 * turin_iohcdev_nbif_smn_reg indexes from 0.
 */
void
turin_fabric_ioms_iohc_disable_unused_pcie_bridges(zen_ioms_t *ioms)
{
	if (ioms->zio_iohctype != ZEN_IOHCT_LARGE)
		return;

	/*
	 * Hide bridges on the unused PCIE6.
	 */
	for (uint8_t i = 0; i < TURIN_PCIE6_CORE_BONUS_PORTS; i++)
		turin_hide_pci_bridge(ioms, TURIN_IOHC_BONUS_PCIE6_CORENO, i);

	/*
	 * The description of the bridge control register says to disable the
	 * unused internal bridges on init.
	 */
	ASSERT3U(ioms->zio_iohcnum, <, ARRAY_SIZE(turin_pcie_int_ports));
	for (uint8_t i =
	    turin_pcie_int_ports[ioms->zio_iohcnum].zinp_count;
	    i < TURIN_NBIF_MAX_PORTS; i++) {
		turin_hide_nbif_bridge(ioms, i);
	}

	/*
	 * Where we don't have bonus cores, hide the bridges that would exist if
	 * we had bonus cores.
	 */
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_BONUS) == 0) {
		for (uint8_t i = 0; i < TURIN_PCIE_CORE_BONUS_PORTS; i++) {
			turin_hide_pci_bridge(ioms,
			    TURIN_IOHC_BONUS_PCIE_CORENO, i);
		}
	}
}

/*
 * This is a companion to turin_fabric_init_bridge, that operates on the PCIe
 * core level before we get to the individual bridge. This initialization
 * generally is required to ensure that each port (regardless of whether it's
 * hidden or not) is able to properly generate an all 1s response. In addition
 * we have to take care of things like atomics, idling defaults, certain
 * receiver completion buffer checks, etc.
 */
void
turin_fabric_init_pcie_core(zen_pcie_core_t *pc)
{
	smn_reg_t reg;
	uint32_t val;

	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_RCB_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RCB_CTL_SET_IGN_LINK_DOWN_ERR(val, 1);
	val = PCIE_CORE_RCB_CTL_SET_LINK_DOWN_CTO_EN(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Program the unit ID for this device's SDP port.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_SDP_CTL);
	val = zen_pcie_core_read(pc, reg);
	/*
	 * The unit ID is split into two parts, and written to different
	 * fields in this register.
	 */
	ASSERT0(pc->zpc_sdp_unit >> 7);
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
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN_CTL_CAP);
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
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN1);
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
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_RX_MARGIN2);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_MARGIN2_SET_NLANES(val, 0xf);
	val = PCIE_CORE_RX_MARGIN2_SET_TIME_RATIO(val, 0x3f);
	val = PCIE_CORE_RX_MARGIN2_SET_VOLT_RATIO(val, 0x3f);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Enabling atomics in the RC requires a few different registers. Both
	 * a strap has to be overridden and then corresponding control bits.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_STRAP_F0);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_ROUTE(val, 1);
	val = PCIE_CORE_STRAP_F0_SET_ATOMIC_EN(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Enable 7-bit TPH steering logic in PCIECORE::PCIE_MST_CTRL_2 and
	 * PCIECORE::PCIE_RX_CNTL4.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_MST_CTL2);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_MSG_CTL2_SET_CI_7BIT_ST_TAG_EN(val, 1);
	zen_pcie_core_write(pc, reg, val);

	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_RX_CTL4);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_RX_CTL4_SET_7BIT_ST_TAG_EN(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Set atomic operation ordering behavior in PCIECORE::PCIE_TX_CTRL_1.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_PCIE_TX_CTL1);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_TX_CTL1_SET_TX_ATOMIC_ORD_DIS(val, 1);
	val = PCIE_CORE_PCIE_TX_CTL1_SET_TX_ATOMIC_OPS_DIS(val, 0);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Disable extracting destination ID and message headers from the
	 * request channel, rather than encapsulated data fields.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_PCIE_TX_CTL3);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_TX_CTL3_SET_ENCMSG_DST_ID_FROM_SDP_REQ_EN(val, 0);
	val = PCIE_CORE_PCIE_TX_CTL3_SET_ENCMSG_HDR_FROM_SDP_REQ_EN(val, 0);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Ensure the correct electrical idle mode detection is set. In
	 * addition, it's been recommended we ignore the K30.7 EDB (EnD Bad)
	 * special symbol errors.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_PCIE_P_CTL);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_PCIE_P_CTL_SET_ELEC_IDLE(val,
	    PCIE_CORE_PCIE_P_CTL_ELEC_IDLE_M1);
	val = PCIE_CORE_PCIE_P_CTL_SET_IGN_EDB_ERR(val, 1);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * Adjust pool credits reserved for PCIe SLV OrigData and Req VC1.
	 */
	reg = turin_pcie_core_reg(pc, D_PCIE_CORE_SLV_CTL1);
	val = zen_pcie_core_read(pc, reg);
	val = PCIE_CORE_SLV_CTL1_SET_PHDR_CREDITS_RSVD(val,
	    PCIE_CORE_SLV_CTL1_VC1_POOL_CREDS_VAL);
	val = PCIE_CORE_SLV_CTL1_SET_PDAT_CREDITS_RSVD(val,
	    PCIE_CORE_SLV_CTL1_VC1_POOL_CREDS_VAL);
	zen_pcie_core_write(pc, reg, val);

	/*
	 * The IOMMUL1 does not have an instance for the bonus core.
	 *
	 * AMD also sets the ordering bit on the IO aggregator for the unused
	 * PCIE6 core on large IOHCs.  But these are completely unused on Turin
	 * and we prentend they do not exist; they are (deliberately) not even
	 * represented in our taxonomy of fabric objects.  Thus, this code can
	 * never visit such a core, so we don't try to set the ordering bit on
	 * the IOAGR register instance.  See the comment in turin/fabric_impl.h
	 * on TURIN_IOHC_MAX_PCIE_CORES for more details.
	 */
	if (pc->zpc_coreno == 0) {
		reg = turin_pcie_core_reg(pc, D_IOMMUL1_CTL1);
		val = zen_pcie_core_read(pc, reg);
		val = IOMMUL1_CTL1_SET_ORDERING(val, 1);
		zen_pcie_core_write(pc, reg, val);
	}

	/*
	 * Fixups that are specific to Turin Cx parts.
	 *
	 * AGESA does this in a callback after reconfig.  We do it here, as this
	 * is where we handle the rest of the core state set up.
	 */
	x86_chiprev_t chiprev = cpuid_getchiprev(CPU);
	if (chiprev_at_least(chiprev, X86_CHIPREV_AMD_TURIN_C0) &&
	    pc->zpc_coreno != TURIN_IOHC_BONUS_PCIE_CORENO) {
		reg = turin_pcie_core_reg(pc, D_PCIE_CORE_PCIE_P_CTL);
		val = zen_pcie_core_read(pc, reg);
		val = PCIE_CORE_PCIE_P_CTL_SET_ALWAYS_USE_FAST_TXCLK(val, 1);
		zen_pcie_core_write(pc, reg, val);
	}
}
