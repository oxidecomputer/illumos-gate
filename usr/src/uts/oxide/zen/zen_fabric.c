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

#include <sys/types.h>
#include <sys/prom_debug.h>
#include <sys/x86_archext.h>
#include <sys/sysmacros.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/spl.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/platform_detect.h>

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/df.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/fabric.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>

#include <zen/df_utils.h>
#include <zen/physaddrs.h>


/*
 * The global fabric object describing the system topology.
 *
 * XXX: Make static once old milan code is migrated fully
 */
zen_fabric_t zen_fabric;


void
zen_fabric_topo_init(void)
{
	oxide_zen_fabric_ops()->zfo_topo_init();
}

uint64_t
zen_fabric_ecam_base(void)
{
	uint64_t ecam = zen_fabric.zf_ecam_base;
	ASSERT3U(ecam, !=, 0);
	return (ecam);
}

/*
 * Starting in DFv4, the DF requires that whatever address is set for PCI MMIO
 * access (via Core::X86::Msr::MmioCfgBaseAddr) matches the value set in
 * DF::MmioPciCfg{Base,Limit}Addr{,Ext}. This value can be changed via the
 * firmware with APCB tokens:
 *     APCB_TOKEN_UID_DF_PCI_MMIO{,HI}_BASE
 *     APCB_TOKEN_UID_DF_PCI_MMIO_SIZE
 * But rather than require some fixed address in either the firmware or the OS,
 * we'll update the DF registers to match the address we've chosen. This does
 * present a bit of a chicken-and-egg problem since we've not setup PCIe
 * configuration space yet, so instead we must resort to the classic PCI
 * Configuration Mechanism #1 via x86 I/O ports.
 */
static void
zen_fabric_set_mmio_pci_cfg_space(uint64_t ecam_base)
{
	df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	switch (df_rev) {
	case DF_REV_3:
		/* Nothing to do pre-DFv4 */
		return;
	case DF_REV_4:
	case DF_REV_4D2:
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
	}

	uint32_t val;
	uint64_t ecam_limit = ecam_base + PCIE_CFGSPACE_SIZE -
	    DF_ECAM_LIMIT_EXCL;

	val = DF_ECAM_BASE_V4_SET_EN(0, 1);
	val = DF_ECAM_V4_SET_ADDR(val,
	    ((uint32_t)ecam_base) >> DF_ECAM_V4_ADDR_SHIFT);
	zen_df_mech1_write32(DF_ECAM_BASE_V4, val);

	val = DF_ECAM_EXT_V4_SET_ADDR(0, ecam_base >>
	    DF_ECAM_EXT_V4_ADDR_SHIFT);
	zen_df_mech1_write32(DF_ECAM_BASE_EXT_V4, val);

	val = DF_ECAM_V4_SET_ADDR(0,
	    ((uint32_t)ecam_limit) >> DF_ECAM_V4_ADDR_SHIFT);
	zen_df_mech1_write32(DF_ECAM_LIMIT_V4, val);

	val = DF_ECAM_EXT_V4_SET_ADDR(0, ecam_limit >>
	    DF_ECAM_EXT_V4_ADDR_SHIFT);
	zen_df_mech1_write32(DF_ECAM_LIMIT_EXT_V4, val);
}

static void
zen_fabric_decomp_init(df_rev_t df_rev, df_fabric_decomp_t *decomp)
{
	df_reg_def_t fid0def, fid1def, fid2def;
	uint32_t fid0, fid1, fid2;

	switch (df_rev) {
	case DF_REV_3:
		fid0def = DF_FIDMASK0_V3;
		fid1def = DF_FIDMASK1_V3;
		/*
		 * DFv3 doesn't have a third mask register but for the sake
		 * of pulling out the common register read logic, we'll just
		 * set it to a valid register. The read result won't be used.
		 */
		fid2def = DF_FIDMASK1_V3;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		fid0def = DF_FIDMASK0_V4;
		fid1def = DF_FIDMASK1_V4;
		fid2def = DF_FIDMASK2_V4;
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
	}

	fid0 = zen_df_early_read32(fid0def);
	fid1 = zen_df_early_read32(fid1def);
	fid2 = zen_df_early_read32(fid2def);

	switch (df_rev) {
	case DF_REV_3:
		decomp->dfd_sock_mask = DF_FIDMASK1_V3_GET_SOCK_MASK(fid1);
		decomp->dfd_die_mask = DF_FIDMASK1_V3_GET_DIE_MASK(fid1);
		decomp->dfd_node_mask = DF_FIDMASK0_V3_GET_NODE_MASK(fid0);
		decomp->dfd_comp_mask = DF_FIDMASK0_V3_GET_COMP_MASK(fid0);
		decomp->dfd_sock_shift = DF_FIDMASK1_V3_GET_SOCK_SHIFT(fid1);
		decomp->dfd_die_shift = 0;
		decomp->dfd_node_shift = DF_FIDMASK1_V3_GET_NODE_SHIFT(fid1);
		decomp->dfd_comp_shift = 0;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		/*
		 * DFv3.5 and DFv4 have the same format in different registers.
		 */
		decomp->dfd_sock_mask = DF_FIDMASK2_V3P5_GET_SOCK_MASK(fid2);
		decomp->dfd_die_mask = DF_FIDMASK2_V3P5_GET_DIE_MASK(fid2);
		decomp->dfd_node_mask = DF_FIDMASK0_V3P5_GET_NODE_MASK(fid0);
		decomp->dfd_comp_mask = DF_FIDMASK0_V3P5_GET_COMP_MASK(fid0);
		decomp->dfd_sock_shift = DF_FIDMASK1_V3P5_GET_SOCK_SHIFT(fid1);
		decomp->dfd_die_shift = 0;
		decomp->dfd_node_shift = DF_FIDMASK1_V3P5_GET_NODE_SHIFT(fid1);
		decomp->dfd_comp_shift = 0;
		break;
	default:
		cmn_err(CE_PANIC,
		    "Encountered previously rejected DF revision: %d", df_rev);
	}
}

/*
 * DF::SpecialSysFunctionFabricID2[FchIOMSFabricID / FchIOSFabricID]
 */
static uint32_t
zen_fch_ios_fabric_id(df_rev_t df_rev)
{
	switch (df_rev) {
	case DF_REV_3:
		return (DF_SYS_FUN_FID2_V3_GET_FCH_IOMS_FID(
		    zen_df_early_read32(DF_SYS_FUN_FID2_V3)));
	case DF_REV_4:
		return (DF_SYS_FUN_FID2_V4_GET_FCH_IOS_FID(
		    zen_df_early_read32(DF_SYS_FUN_FID2_V4)));
	case DF_REV_4D2:
		return (DF_SYS_FUN_FID2_V4D2_GET_FCH_IOS_FID(
		    zen_df_early_read32(DF_SYS_FUN_FID2_V4)));
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		return (-1);
	}
}

/*
 * DF::SystemCfg[NodeId]
 */
static uint16_t
zen_iodie_node_id(zen_iodie_t *iodie)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	switch (df_rev) {
	case DF_REV_3:
		return (DF_SYSCFG_V3_GET_NODE_ID(zen_df_bcast_read32(iodie,
		    DF_SYSCFG_V3)));
	case DF_REV_4:
		return (DF_SYSCFG_V4_GET_NODE_ID(zen_df_bcast_read32(iodie,
		    DF_SYSCFG_V4)));
	case DF_REV_4D2:
		return (DF_SYSCFG_V4D2_GET_NODE_ID(zen_df_bcast_read32(iodie,
		    DF_SYSCFG_V4)));
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		return (-1);
	}
}

/*
 * DF::CfgAddressCntl[SecBusNum]
 */
static uint8_t
zen_fabric_busno(zen_iodie_t *iodie, zen_ioms_t *ioms)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	df_reg_def_t rd;
	uint32_t val;

	switch (df_rev) {
	case DF_REV_3:
		rd = DF_CFG_ADDR_CTL_V2;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		rd = DF_CFG_ADDR_CTL_V4;
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		return (-1);
	}

	/*
	 * If ioms isn't NULL, we'll read the given IOS register instance.
	 * Otherwise, we simply do a broadcast read.
	 */
	val = (ioms != NULL) ? zen_df_read32(iodie, ioms->zio_ios_inst_id, rd)
	    : zen_df_bcast_read32(iodie, rd);

	return (DF_CFG_ADDR_CTL_GET_BUS_NUM(val));
}

/*
 * DF::FabricBlockInstanceInformation3_IOS[BlockFabricID]
 */
static uint16_t
zen_ios_fabric_id(zen_ioms_t *ioms)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	uint32_t finfo3 = zen_df_read32(ioms->zio_iodie, ioms->zio_ios_inst_id,
	    DF_FBIINFO3);

	switch (df_rev) {
	case DF_REV_3:
		return (DF_FBIINFO3_V3_GET_BLOCKID(finfo3));
	case DF_REV_4:
		return (DF_FBIINFO3_V4_GET_BLOCKID(finfo3));
	case DF_REV_4D2:
		return (DF_FBIINFO3_V4D2_GET_BLOCKID(finfo3));
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		return (-1);
	}
}

/*
 * Right now we're running on the boot CPU. We know that a single socket has to
 * be populated. Our job is to go through and determine what the rest of the
 * topology of this system looks like in terms of the data fabric, north
 * bridges, and related. We can rely on the DF instance 0/18/0 to exist;
 * however, that's it.
 *
 * An important rule of discovery here is that we should not rely on invalid PCI
 * reads. We should be able to bootstrap from known good data and what the
 * actual SoC has discovered here rather than trying to fill that in ourselves.
 */
void
zen_fabric_topo_init_common(void)
{
	zen_fabric_t *fabric = &zen_fabric;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const df_rev_t df_rev = consts->zpc_df_rev;
	uint8_t nsocs = 0;
	uint32_t syscfg, syscomp;
	uint32_t fch_ios_fid;

	PRM_POINT("zen_fabric_topo_init_common() starting...");

	/*
	 * Before we can do anything else, we must set up PCIe ECAM.  We locate
	 * this region beyond either the end of DRAM or the IOMMU hole,
	 * whichever is higher.  The remainder of the 64-bit MMIO space is
	 * available for allocation to IOMSs (for e.g. PCIe devices).
	 */

	fabric->zf_tom = MSR_AMD_TOM_MASK(rdmsr(MSR_AMD_TOM));
	fabric->zf_tom2 = MSR_AMD_TOM2_MASK(rdmsr(MSR_AMD_TOM2));

	fabric->zf_ecam_base = P2ROUNDUP(MAX(fabric->zf_tom2,
	    ZEN_PHYSADDR_IOMMU_HOLE_END), PCIE_CFGSPACE_ALIGN);
	fabric->zf_mmio64_base = fabric->zf_ecam_base + PCIE_CFGSPACE_SIZE;

	zen_fabric_set_mmio_pci_cfg_space(fabric->zf_ecam_base);
	pcie_cfgspace_init();
	// XXX: disable IO access to ECS

	/*
	 * Now that we have access to PCIe configuration space, we can start
	 * discovering the specifics of the fabric topology.
	 */

	/*
	 * Grab the masks & shifts needed for decoding global Fabric IDs.
	 */
	zen_fabric_decomp_init(df_rev, &fabric->zf_decomp);

	/*
	 * Grab the number of SOCs present in the system and verify against
	 * our assumptions.
	 */
	switch (df_rev) {
	case DF_REV_3:
		syscfg = zen_df_early_read32(DF_SYSCFG_V3);
		syscomp = zen_df_early_read32(DF_COMPCNT_V2);
		nsocs = DF_SYSCFG_V3_GET_OTHER_SOCK(syscfg) + 1;
		VERIFY3U(nsocs, ==, DF_COMPCNT_V2_GET_PIE(syscomp));
		VERIFY3U(nsocs * consts->zpc_ioms_per_iodie, ==,
		    DF_COMPCNT_V2_GET_IOMS(syscomp));
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		syscfg = zen_df_early_read32(DF_SYSCFG_V4);
		syscomp = zen_df_early_read32(DF_COMPCNT_V4);
		nsocs = DF_SYSCFG_V4_GET_OTHER_SOCK(syscfg) + 1;
		VERIFY3U(nsocs, ==, DF_COMPCNT_V4_GET_PIE(syscomp));
		VERIFY3U(nsocs * consts->zpc_ioms_per_iodie, ==,
		    DF_COMPCNT_V4_GET_IOM(syscomp));
		VERIFY3U(nsocs * consts->zpc_ioms_per_iodie, ==,
		    DF_COMPCNT_V4_GET_IOS(syscomp));
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
	}

	fch_ios_fid = zen_fch_ios_fabric_id(df_rev);

	fabric->zf_nsocs = nsocs;
	for (uint8_t socno = 0; socno < nsocs; socno++) {
		zen_soc_t *soc = &fabric->zf_socs[socno];
		zen_iodie_t *iodie = &soc->zs_iodie;

		soc->zs_socno = socno;
		soc->zs_fabric = fabric;

		iodie->zi_devno = AMDZEN_DF_FIRST_DEVICE + socno;
		iodie->zi_node_id = zen_iodie_node_id(iodie);
		iodie->zi_soc = soc;

		if (iodie->zi_node_id == 0) {
			iodie->zi_flags = ZEN_IODIE_F_PRIMARY;
		}

		/*
		 * stlouis#574: Because we do not know the circumstances all
		 * these locks will be used during early initialization, set
		 * these to be spin locks for the moment.
		 */
		mutex_init(&iodie->zi_df_ficaa_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->zi_smn_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->zi_smu_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));

		iodie->zi_smn_busno = zen_fabric_busno(iodie, NULL);

		iodie->zi_nioms = consts->zpc_ioms_per_iodie;
		fabric->zf_total_ioms += iodie->zi_nioms;
		for (uint8_t iomsno = 0; iomsno < iodie->zi_nioms; iomsno++) {
			zen_ioms_t *ioms = &iodie->zi_ioms[iomsno];

			ioms->zio_num = iomsno;
			ioms->zio_iodie = iodie;

			ioms->zio_iom_inst_id = consts->zpc_df_first_iom_id +
			    iomsno;
			ioms->zio_ios_inst_id = consts->zpc_df_first_ios_id +
			    iomsno;

			ioms->zio_dest_id = zen_ios_fabric_id(ioms);
			ioms->zio_pci_busno = zen_fabric_busno(iodie, ioms);

			if (ioms->zio_dest_id == fch_ios_fid) {
				ioms->zio_flags |= ZEN_IOMS_F_HAS_FCH;
			}
		}
	}
}
