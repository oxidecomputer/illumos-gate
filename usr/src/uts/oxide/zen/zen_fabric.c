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
#include <sys/amdzen/ccd.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/nbif_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/platform.h>

/*
 * --------------------------------------
 * Physical Organization and Nomenclature
 * --------------------------------------
 *
 * In AMD's Zen microarchitectures, the CPU socket is organized as a series of
 * chiplets coupled with a series of compute complexes and then a central IO
 * die.  uts/intel/os/cpuid.c has an example of what this looks like.
 *
 * Critically, this IO die is the major device that we are concerned with here,
 * as it bridges the cores to the outside world through a combination of
 * different devices and IO paths.  The part of the IO die that we will spend
 * most of our time dealing with is the "northbridge IO unit", or NBIO.  In DF
 * (data fabric) terms, NBIOs are a class of device called an IOMS (IO
 * master-slave).  These are represented in our fabric data structures as
 * subordinate to an IO die.
 *
 * Each NBIO instance implements, among other things, a PCIe root complex (RC),
 * consisting of two major components: an IO hub core (IOHC) that implements the
 * host side of the RC, and some number of PCIe cores that implement the PCIe
 * side.  The IOHC appears in PCI configuration space as a root complex and is
 * the attachment point for npe(4d).  The PCIe cores do not themselves appear in
 * config space, though each implements PCIe root ports, and each root port has
 * an associated host bridge that appears in configuration space.
 * Externally-attached PCIe devices are enumerated under these bridges, and the
 * bridge provides the standard PCIe interface to the downstream port including
 * link status and control.  Specific quantities of these vary, depending on the
 * microarchitecture.
 *
 * Again, depending on microarchitecture, some of the NBIO instances are
 * somewhat special and merit brief additional discussion.  Some instances may
 * contain additional PCIe core(s) associated with the lanes that would
 * otherwise be used for WAFL.  An instance will have the Fusion Controller Hub
 * (FCH) attached to it; the FCH doesn't contain any real PCIe devices, but it
 * does contain some fake ones and from what we can tell the NBIO is the DF
 * endpoint where MMIO transactions targeting the FCH are directed.
 *
 * The UMCs are instances of CS (coherent slave) DF components; we do not
 * discuss them further here, but details may be found in
 * uts/intel/sys/amdzen/umc.h and uts/intel/io/amdzen/zen_umc.c.
 *
 * --------------
 * Representation
 * --------------
 *
 * We represent the NBIO entities described above and the CPU core entities
 * described in cpuid.c in a hierarchical fashion:
 *
 * zen_fabric_t (DF -- root)
 * |
 * \-- zen_soc_t
 *     |
 *     \-- zen_iodie_t
 *         |
 *         +-- zen_ioms_t
 *         |   |
 *         |   +-- zen_pcie_core_t
 *         |   |   |
 *         |   |   \-- zen_pcie_port_t
 *         |   |
 *         |   \-- zen_nbif_t
 *         |
 *         \-- zen_ccd_t
 *             |
 *             \-- zen_ccx_t
 *                 |
 *                 \-- zen_core_t
 *                     |
 *                     \-- zen_thread_t
 *
 * The PCIe bridge does not have its own representation in this schema, but is
 * represented as a B/D/F associated with a PCIe port.  That B/D/F provides the
 * standard PCIe bridge interfaces associated with a root port and host bridge.
 *
 * For our purposes, each PCIe core is associated with an instance of the
 * PCIECORE register block and an RSMU (remote system management unit) register
 * block.  These implementation-specific registers control the PCIe core logic.
 * Each root port is associated with an instance of the PCIEPORT register block
 * and the standard PCIe-defined registers of the host bridge which AMD refers
 * to as PCIERCCFG.  Note that the MP1 DXIO firmware also accesses at least ome
 * of the PCIECORE, PCIEPORT, and the SMU::RSMU::RSMU::PCIE0::MMIOEXT registers,
 * and a limited set of fields in the standard bridge registers associated with
 * hotplug are controlled by that firmware as well, though the intent is that
 * they are controlled in standards-compliant ways.  These associations allow us
 * to obtain SMN register instances from a pointer to the entity to which those
 * registers pertain.
 */

/*
 * The global fabric object describing the system topology.
 *
 * XXX: Make static once old milan code is migrated fully
 */
zen_fabric_t zen_fabric;

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
zen_fabric_iodie_node_id(zen_iodie_t *iodie)
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
 * Returns the node ID corresponding to this die.
 */
uint8_t
zen_iodie_node_id(const zen_iodie_t *const iodie)
{
	return (iodie->zi_node_id);
}

/*
 * Returns the flags that have been set on this IOMS.
 */
zen_ioms_flag_t
zen_ioms_flags(const zen_ioms_t *const ioms)
{
	return (ioms->zio_flags);
}

/*
 * Returns the IO die this IOMS is attached to.
 */
zen_iodie_t *
zen_ioms_iodie(const zen_ioms_t *const ioms)
{
	return (ioms->zio_iodie);
}

/*
 * Returns the flags that have been set on this IO die.
 */
zen_iodie_flag_t
zen_iodie_flags(const zen_iodie_t *const iodie)
{
	return (iodie->zi_flags);
}

static uint32_t
zen_ccd_cores_enabled(zen_iodie_t *iodie, uint8_t ccdpno)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	df_reg_def_t phys_core_en_v3[] = {
		DF_PHYS_CORE_EN0_V3,
		DF_PHYS_CORE_EN1_V3,
	};
	df_reg_def_t phys_core_en_v4[] = {
		DF_PHYS_CORE_EN0_V4,
		DF_PHYS_CORE_EN1_V4,
		DF_PHYS_CORE_EN2_V4,
		DF_PHYS_CORE_EN3_V4,
		DF_PHYS_CORE_EN5_V4,
	};
	df_reg_def_t *phys_core_en = NULL;
	uint_t nphys_core_en, cores_per_ccd, ccds_per_reg, phys_core_reg,
	    core_shift;
	uint32_t cores_enabled;

	switch (df_rev) {
	case DF_REV_3:
		phys_core_en = phys_core_en_v3;
		nphys_core_en = ARRAY_SIZE(phys_core_en_v3);
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		phys_core_en = phys_core_en_v4;
		nphys_core_en = ARRAY_SIZE(phys_core_en_v4);
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		return (-1);
	}

	/*
	 * Each register contains 32 bits with each bit corresponding to a core.
	 * Since we know the number of Cores per CCX and CCXs per CCD, we can
	 * use that to determine which register to read and which bits to check
	 * for the given CCD.
	 */
	cores_per_ccd = consts->zpc_cores_per_ccx * ZEN_MAX_CCXS_PER_CCD;
	VERIFY3U(cores_per_ccd, <=, 32);
	ccds_per_reg = 32 / cores_per_ccd;
	phys_core_reg = ccdpno / ccds_per_reg;
	VERIFY3U(phys_core_reg, <, nphys_core_en);
	core_shift = ccdpno % ccds_per_reg * cores_per_ccd;

	cores_enabled = zen_df_bcast_read32(iodie, phys_core_en[phys_core_reg]);
	cores_enabled = bitx32(cores_enabled, core_shift + cores_per_ccd - 1,
	    core_shift);

	return (cores_enabled);
}

static uint_t
zen_fabric_ccx_init_core(zen_ccx_t *ccx, uint8_t lidx, uint8_t pidx)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	smn_reg_t reg;
	uint32_t val;
	zen_core_t *core = &ccx->zcx_cores[lidx];
	zen_ccd_t *ccd = ccx->zcx_ccd;
	uint_t nthreads = 0;

	core->zc_ccx = ccx;
	core->zc_physical_coreno = pidx;

	reg = zen_core_reg(core, D_SCFCTP_PMREG_INITPKG0);
	val = zen_core_read(core, reg);
	VERIFY3U(val, !=, 0xffffffffU);

	core->zc_logical_coreno = SCFCTP_PMREG_INITPKG0_GET_LOG_CORE(val);

	VERIFY3U(SCFCTP_PMREG_INITPKG0_GET_PHYS_CORE(val), ==, pidx);
	VERIFY3U(SCFCTP_PMREG_INITPKG0_GET_PHYS_CCX(val), ==,
	    ccx->zcx_physical_cxno);
	VERIFY3U(SCFCTP_PMREG_INITPKG0_GET_PHYS_DIE(val), ==,
	    ccd->zcd_physical_dieno);

	core->zc_nthreads = SCFCTP_PMREG_INITPKG0_GET_SMTEN(val) + 1;
	VERIFY3U(core->zc_nthreads, <=, ZEN_MAX_THREADS_PER_CORE);

	for (uint8_t thr = 0; thr < core->zc_nthreads; thr++) {
		zen_thread_t *thread = &core->zc_threads[thr];

		thread->zt_threadno = thr;
		thread->zt_core = core;
		nthreads++;

		thread->zt_apicid = fops->zfo_thread_apicid(thread);
	}

	return (nthreads);
}

static uint32_t
zen_fabric_ccx_init_soc(zen_soc_t *soc)
{
	const x86_uarchrev_t uarch = oxide_board_data->obd_cpuinfo.obc_uarchrev;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const df_rev_t df_rev = consts->zpc_df_rev;
	zen_iodie_t *iodie = &soc->zs_iodies[0];
	uint32_t nthreads = 0;
	uint32_t val;
	bool zen5;

	/*
	 * A 1-bit corresponds to an enabled CCD with the physical number being
	 * the bit position. The logical numbers are assigned sequentially for
	 * each enabled CCD.
	 *
	 * Bits x=0-7 : First port on CCM[x] has CCD enabled.
	 * Bits x=8-15: Second port on CCM[x-8] has CCD enabled.
	 */
	uint16_t ccdmap = 0;

	/*
	 * Zen 5 moved a couple of registers from SMU::PWR to L3::SOC.
	 */
	if (uarchrev_matches(uarch, X86_UARCHREV_AMD_ZEN3_ANY) ||
	    uarchrev_matches(uarch, X86_UARCHREV_AMD_ZEN4_ANY)) {
		zen5 = false;
	} else if (uarchrev_matches(uarch, X86_UARCHREV_AMD_ZEN5_ANY)) {
		zen5 = true;
	} else {
		cmn_err(CE_PANIC, "Unsupported uarch %x", uarch);
		return (-1);
	}

	/*
	 * To determine the physical CCD numbers, we iterate over the CCMs
	 * and note what CCDs (if any) are present and enabled.
	 */
	for (uint8_t ccmno = 0; ccmno < ZEN_MAX_CCMS_PER_IODIE; ccmno++) {
		uint32_t ccden;
		bool wide;
		uint32_t ccminst = ZEN_DF_FIRST_CCM_ID + ccmno;

		/*
		 * The CCM is part of the IO die, not the CCD itself. If it is
		 * disabled, we skip this CCD index as even if it exists nothing
		 * can reach it.
		 */
		val = zen_df_read32(iodie, ccminst, DF_FBIINFO0);
		VERIFY3U(DF_FBIINFO0_GET_TYPE(val), ==, DF_TYPE_CCM);
		if (DF_FBIINFO0_V3_GET_ENABLED(val) == 0)
			continue;

		/*
		 * XXX: Is this really just DF rev dependent or should be a
		 * per-uarch hook?
		 */
		switch (df_rev) {
		case DF_REV_3:
			/*
			 * With DFv3, we assume a 1-1 mapping of CCDs to CCMs.
			 */
			ccdmap |= (1 << ccmno);
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			/*
			 * DFv4+ allows for up to 2 CCDs per CCM, depending on
			 * if wide mode is enabled.
			 */
			ccden = zen_df_read32(iodie, ccminst, DF_CCD_EN_V4);

			/*
			 * Note if first possible CCD is enabled.
			 */
			ccdmap |= ((DF_CCD_EN_V4_GET_CCD_EN(ccden) & 1) <<
			    ccmno);

			/*
			 * For a second CCD, we need to check if wide mode is
			 * disabled. The actual bit to check is unfortunately
			 * slightly different between DFv4 and DFv4D2.
			 */
			if (df_rev == DF_REV_4D2) {
				wide = DF_CCD_EN_V4D2_GET_WIDE_EN(ccden);
			} else {
				val = zen_df_read32(iodie, ccminst,
				    DF_CCMCFG4_V4);
				wide = DF_CCMCFG4_V4_GET_WIDE_EN(val);
			}
			/*
			 * If wide mode is disabled, and DF::CCDEnable says the
			 * second CCD on this CCM is enabled, note that in the
			 * upper half of the ccd map.
			 */
			if (!wide)
				ccdmap |= ((DF_CCD_EN_V4_GET_CCD_EN(ccden) >> 1)
				    & 1) << ZEN_MAX_CCMS_PER_IODIE << ccmno;
			break;
		default:
			cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		}
	}

	/*
	 * Now we can iterate over `ccdmap`, which corresponds to our physical
	 * CCD numbers, and assign logical numbers to each enabled CCD.
	 */
	for (uint8_t ccdpno = 0, lccd = 0; ccdmap != 0;
	    ccdmap &= ~(1 << ccdpno), ccdpno++) {
		uint8_t pcore, lcore, pccx;
		uint32_t cores_enabled;
		zen_ccd_t *ccd = &iodie->zi_ccds[lccd];
		zen_ccx_t *ccx = &ccd->zcd_ccxs[0];
		smn_reg_t reg;

		/*
		 * Either this CCD or the CCM itself is disabled - skip it.
		 */
		if ((ccdmap & (1 << ccdpno)) == 0)
			continue;

		VERIFY3U(ccdpno, <,
		    ZEN_MAX_CCMS_PER_IODIE * DF_MAX_CCDS_PER_CCM);

		/*
		 * The CCM may have been enabled but at least for DFv3, there's
		 * a possibility the corresponding CCD is disabled. So let's
		 * double check whether any core is enabled on this CCD.
		 */
		cores_enabled = zen_ccd_cores_enabled(iodie, ccdpno);

		if (cores_enabled == 0)
			continue;

		VERIFY3U(lccd, <, consts->zpc_ccds_per_iodie);
		ccd->zcd_iodie = iodie;
		ccd->zcd_logical_dieno = lccd++;
		ccd->zcd_physical_dieno = ccdpno;

		/*
		 * The firmware should've set this correctly -- let's validate
		 * our assumption.
		 * XXX: Avoid panicking on bad data from firmware
		 */
		reg = zen_ccd_reg(ccd, D_SMUPWR_CCD_DIE_ID);
		val = zen_ccd_read(ccd, reg);
		VERIFY3U(val, ==, ccdpno);

		if (!zen5) {
			reg = zen_ccd_reg(ccd, D_SMUPWR_THREAD_CFG);
			val = zen_ccd_read(ccd, reg);
			ccd->zcd_nccxs = 1 +
			    SMUPWR_THREAD_CFG_GET_COMPLEX_COUNT(val);
		} else {
			reg = zen_ccd_reg(ccd, D_L3SOC_THREAD_CFG);
			val = zen_ccd_read(ccd, reg);
			ccd->zcd_nccxs = 1 +
			    L3SOC_THREAD_CFG_GET_COMPLEX_COUNT(val);
		}
		VERIFY3U(ccd->zcd_nccxs, <=, ZEN_MAX_CCXS_PER_CCD);

		if (ccd->zcd_nccxs == 0) {
			cmn_err(CE_NOTE, "CCD 0x%x: no CCXs reported",
			    ccd->zcd_physical_dieno);
			continue;
		}

		/*
		 * Make sure that the CCD's local understanding of
		 * enabled cores matches what we found earlier through
		 * the DF. A mismatch here is a firmware bug.
		 * XXX: Avoid panicking on bad data from firmware
		 */
		if (!zen5) {
			reg = zen_ccd_reg(ccd, D_SMUPWR_CORE_EN);
			val = zen_ccd_read(ccd, reg);
			VERIFY3U(SMUPWR_CORE_EN_GET(val), ==, cores_enabled);
		} else {
			reg = zen_ccd_reg(ccd, D_L3SOC_CORE_EN);
			val = zen_ccd_read(ccd, reg);
			VERIFY3U(L3SOC_CORE_EN_GET(val), ==, cores_enabled);
		}

		/*
		 * XXX While we know there is only ever 1 CCX per CCD on the
		 * platforms we support (Milan, Genoa, Turin), DF::CCXEnable
		 * allows for 2 because the DFv3 implementation is shared with
		 * Rome, which has up to 2 CCXs per CCD. Although we know we
		 * only ever have 1 CCX, we don't, strictly, know that the CCX
		 * is always physical index 0. Here we assume it, but we
		 * probably want to change the ZEN_MAX_xxx_PER_yyy so that they
		 * reflect the size of the physical ID spaces rather than the
		 * maximum logical entity counts.  Doing so would accommodate a
		 * part that has a single CCX per CCD, but at index 1.
		 */
		ccx->zcx_ccd = ccd;
		ccx->zcx_logical_cxno = 0;
		ccx->zcx_physical_cxno = pccx = 0;

		/*
		 * All the cores on the CCD will (should) return the
		 * same values in PMREG_INITPKG0 and PMREG_INITPKG7.
		 * The catch is that we have to read them from a core
		 * that exists or we get all-1s.  Use the mask of
		 * cores enabled on this die that we already computed
		 * to find one to read from, then bootstrap into the
		 * core enumeration.  XXX At some point we probably
		 * should do away with all this cross-checking and
		 * choose something to trust.
		 */
		for (pcore = 0; (cores_enabled & (1 << pcore)) == 0 &&
		    pcore < consts->zpc_cores_per_ccx; pcore++)
			;
		VERIFY3U(pcore, <, consts->zpc_cores_per_ccx);

		reg = SCFCTP_PMREG_INITPKG7(ccdpno, pccx, pcore);
		val = zen_smn_read(iodie, reg);
		VERIFY3U(val, !=, 0xffffffffU);

		ccx->zcx_ncores = SCFCTP_PMREG_INITPKG7_GET_N_CORES(val) + 1;
		/* XXX: overwriting this? */
		iodie->zi_nccds = SCFCTP_PMREG_INITPKG7_GET_N_DIES(val) + 1;

		for (pcore = 0, lcore = 0; pcore < consts->zpc_cores_per_ccx;
		    pcore++) {
			if ((cores_enabled & (1 << pcore)) == 0)
				continue;
			nthreads += zen_fabric_ccx_init_core(ccx, lcore, pcore);
			++lcore;
		}

		VERIFY3U(lcore, ==, ccx->zcx_ncores);
	}

	return (nthreads);
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
zen_fabric_topo_init(void)
{
	zen_fabric_t *fabric = &zen_fabric;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	const df_rev_t df_rev = consts->zpc_df_rev;
	uint8_t nsocs = 0;
	uint32_t syscfg, syscomp;
	uint32_t fch_ios_fid;
	uint32_t nthreads = 0;

	/*
	 * Make sure the platform specific constants are valid.
	 */
	VERIFY3U(consts->zpc_ioms_per_iodie, <=, ZEN_IODIE_MAX_IOMS);
	VERIFY3U(consts->zpc_ccds_per_iodie, <=, ZEN_MAX_CCDS_PER_IODIE);
	VERIFY3U(consts->zpc_cores_per_ccx, <=, ZEN_MAX_CORES_PER_CCX);

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
		zen_iodie_t *iodie = &soc->zs_iodies[0];

		soc->zs_socno = socno;
		soc->zs_fabric = fabric;
		soc->zs_niodies = ZEN_FABRIC_MAX_DIES_PER_SOC;

		iodie->zi_devno = AMDZEN_DF_FIRST_DEVICE + socno;
		iodie->zi_node_id = zen_fabric_iodie_node_id(iodie);
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

			/*
			 * uarch-specific IOMS init hook.
			 * XXX: actually most of the functionality is still
			 * in the milan impl.
			 */
			if (fops->zfo_ioms_init != NULL)
				fops->zfo_ioms_init(ioms);
		}

		/*
		 * Initialize the CCXs for this SOC/IOD.
		 */
		nthreads += zen_fabric_ccx_init_soc(soc);

		/*
		 * Generic SoC & IO die initialization is complete but let
		 * the uarch-specific code do any additional setup needed.
		 */
		if (fops->zfo_soc_init != NULL)
			fops->zfo_soc_init(soc, iodie);
	}

	/*
	 * We're done with the basic fabric init, let the uarch-specific code
	 * do any additional setup needed.
	 */
	if (fops->zfo_topo_init != NULL)
		fops->zfo_topo_init(fabric);

	if (nthreads > NCPU) {
		cmn_err(CE_WARN, "%d CPUs found but only %d supported",
		    nthreads, NCPU);
		nthreads = NCPU;
	}
	boot_max_ncpus = max_ncpus = boot_ncpus = nthreads;
}

/*
 * Utility routines to traverse and search across the Zen fabric, both the data
 * fabric and the northbridges.
 */
int
zen_fabric_walk_iodie(zen_fabric_t *fabric, zen_iodie_cb_f func, void *arg)
{
	for (uint_t socno = 0; socno < fabric->zf_nsocs; socno++) {
		zen_soc_t *soc = &fabric->zf_socs[socno];
		for (uint_t iono = 0; iono < soc->zs_niodies; iono++) {
			int ret;
			zen_iodie_t *iodie = &soc->zs_iodies[iono];

			ret = func(iodie, arg);
			if (ret != 0) {
				return (ret);
			}
		}
	}

	return (0);
}

typedef struct zen_fabric_ioms_cb {
	zen_ioms_cb_f	zfic_func;
	void		*zfic_arg;
} zen_fabric_ioms_cb_t;

static int
zen_fabric_walk_ioms_iodie_cb(zen_iodie_t *iodie, void *arg)
{
	const zen_fabric_ioms_cb_t *cb =
	    (const zen_fabric_ioms_cb_t *)arg;
	for (uint_t iomsno = 0; iomsno < iodie->zi_nioms; iomsno++) {
		zen_ioms_t *ioms = &iodie->zi_ioms[iomsno];
		int ret = cb->zfic_func(ioms, cb->zfic_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

int
zen_fabric_walk_ioms(zen_fabric_t *fabric, zen_ioms_cb_f func, void *arg)
{
	zen_fabric_ioms_cb_t cb = {
	    .zfic_func = func,
	    .zfic_arg = arg,
	};

	return (zen_fabric_walk_iodie(fabric, zen_fabric_walk_ioms_iodie_cb,
	    &cb));
}

int
zen_walk_ioms(zen_ioms_cb_f func, void *arg)
{
	return (zen_fabric_walk_ioms(&zen_fabric, func, arg));
}

typedef struct zen_fabric_nbif_cb {
	zen_nbif_cb_f	zfnc_func;
	void		*zfnc_arg;
} zen_fabric_nbif_cb_t;

static int
zen_fabric_walk_nbif_ioms_cb(zen_ioms_t *ioms, void *arg)
{
	const zen_fabric_nbif_cb_t *cb = (const zen_fabric_nbif_cb_t *)arg;
	for (uint_t nbifno = 0; nbifno < ioms->zio_nnbifs; nbifno++) {
		zen_nbif_t *nbif = &ioms->zio_nbifs[nbifno];
		int ret = cb->zfnc_func(nbif, cb->zfnc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

int
zen_fabric_walk_nbif(zen_fabric_t *fabric, zen_nbif_cb_f func, void *arg)
{
	zen_fabric_nbif_cb_t cb = {
	    .zfnc_func = func,
	    .zfnc_arg = arg,
	};

	return (zen_fabric_walk_ioms(fabric, zen_fabric_walk_nbif_ioms_cb,
	    &cb));
}

typedef struct zen_fabric_pcie_core_cb {
	zen_pcie_core_cb_f	zfpcc_func;
	void			*zfpcc_arg;
} zen_fabric_pcie_core_cb_t;

static int
zen_fabric_walk_pcie_core_cb(zen_ioms_t *ioms, void *arg)
{
	const zen_fabric_pcie_core_cb_t *cb =
	    (const zen_fabric_pcie_core_cb_t *)arg;
	for (uint_t pcno = 0; pcno < ioms->zio_npcie_cores; pcno++) {
		zen_pcie_core_t *pc = &ioms->zio_pcie_cores[pcno];
		int ret = cb->zfpcc_func(pc, cb->zfpcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

int
zen_fabric_walk_pcie_core(zen_fabric_t *fabric, zen_pcie_core_cb_f func,
    void *arg)
{
	zen_fabric_pcie_core_cb_t cb = {
	    .zfpcc_func = func,
	    .zfpcc_arg = arg,
	};

	return (zen_fabric_walk_ioms(fabric, zen_fabric_walk_pcie_core_cb,
	    &cb));
}

typedef struct zen_fabric_pcie_port_cb {
	zen_pcie_port_cb_f	zfppc_func;
	void			*zfppc_arg;
} zen_fabric_pcie_port_cb_t;

static int
zen_fabric_walk_pcie_port_cb(zen_pcie_core_t *pc, void *arg)
{
	zen_fabric_pcie_port_cb_t *cb = (zen_fabric_pcie_port_cb_t *)arg;

	for (uint_t portno = 0; portno < pc->zpc_nports; portno++) {
		zen_pcie_port_t *port = &pc->zpc_ports[portno];
		int ret = cb->zfppc_func(port, cb->zfppc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

int
zen_fabric_walk_pcie_port(zen_fabric_t *fabric, zen_pcie_port_cb_f func,
    void *arg)
{
	zen_fabric_pcie_port_cb_t cb = {
	    .zfppc_func = func,
	    .zfppc_arg = arg,
	};

	return (zen_fabric_walk_pcie_core(fabric, zen_fabric_walk_pcie_port_cb,
	    &cb));
}

typedef struct zen_fabric_ccd_cb {
	zen_ccd_cb_f	zfcc_func;
	void		*zfcc_arg;
} zen_fabric_ccd_cb_t;

static int
zen_fabric_walk_ccd_iodie_cb(zen_iodie_t *iodie, void *arg)
{
	const zen_fabric_ccd_cb_t *cb = (const zen_fabric_ccd_cb_t *)arg;

	for (uint8_t ccdno = 0; ccdno < iodie->zi_nccds; ccdno++) {
		zen_ccd_t *ccd = &iodie->zi_ccds[ccdno];
		int ret = cb->zfcc_func(ccd, cb->zfcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_ccd(zen_fabric_t *fabric, zen_ccd_cb_f func, void *arg)
{
	zen_fabric_ccd_cb_t cb = {
	    .zfcc_func = func,
	    .zfcc_arg = arg,
	};

	return (zen_fabric_walk_iodie(fabric, zen_fabric_walk_ccd_iodie_cb,
	    &cb));
}

typedef struct zen_fabric_ccx_cb {
	zen_ccx_cb_f	zfcc_func;
	void		*zfcc_arg;
} zen_fabric_ccx_cb_t;

static int
zen_fabric_walk_ccx_ccd_cb(zen_ccd_t *ccd, void *arg)
{
	const zen_fabric_ccx_cb_t *cb = (const zen_fabric_ccx_cb_t *)arg;

	for (uint8_t ccxno = 0; ccxno < ccd->zcd_nccxs; ccxno++) {
		zen_ccx_t *ccx = &ccd->zcd_ccxs[ccxno];
		int ret = cb->zfcc_func(ccx, cb->zfcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_ccx(zen_fabric_t *fabric, zen_ccx_cb_f func, void *arg)
{
	zen_fabric_ccx_cb_t cb = {
	    .zfcc_func = func,
	    .zfcc_arg = arg,
	};

	return (zen_fabric_walk_ccd(fabric, zen_fabric_walk_ccx_ccd_cb, &cb));
}

typedef struct zen_fabric_core_cb {
	zen_core_cb_f	zfcc_func;
	void		*zfcc_arg;
} zen_fabric_core_cb_t;

static int
zen_fabric_walk_core_ccx_cb(zen_ccx_t *ccx, void *arg)
{
	const zen_fabric_core_cb_t *cb = (const zen_fabric_core_cb_t *)arg;

	for (uint8_t coreno = 0; coreno < ccx->zcx_ncores; coreno++) {
		zen_core_t *core = &ccx->zcx_cores[coreno];
		int ret = cb->zfcc_func(core, cb->zfcc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_core(zen_fabric_t *fabric, zen_core_cb_f func, void *arg)
{
	zen_fabric_core_cb_t cb = {
	    .zfcc_func = func,
	    .zfcc_arg = arg,
	};

	return (zen_fabric_walk_ccx(fabric, zen_fabric_walk_core_ccx_cb, &cb));
}

typedef struct zen_fabric_thread_cb {
	zen_thread_cb_f		zftc_func;
	void			*zftc_arg;
} zen_fabric_thread_cb_t;

static int
zen_fabric_walk_thread_core_cb(zen_core_t *core, void *arg)
{
	zen_fabric_thread_cb_t *cb = (zen_fabric_thread_cb_t *)arg;

	for (uint8_t threadno = 0; threadno < core->zc_nthreads; threadno++) {
		zen_thread_t *thread = &core->zc_threads[threadno];
		int ret = cb->zftc_func(thread, cb->zftc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
zen_fabric_walk_thread(zen_fabric_t *fabric, zen_thread_cb_f func, void *arg)
{
	zen_fabric_thread_cb_t cb = {
	    .zftc_func = func,
	    .zftc_arg = arg,
	};

	return (zen_fabric_walk_core(fabric, zen_fabric_walk_thread_core_cb,
	    &cb));
}

int
zen_walk_thread(zen_thread_cb_f func, void *arg)
{
	return (zen_fabric_walk_thread(&zen_fabric, func, arg));
}

typedef struct {
	uint32_t	zffi_dest;
	zen_ioms_t	*zffi_ioms;
} zen_fabric_find_ioms_t;

static int
zen_fabric_find_ioms_cb(zen_ioms_t *ioms, void *arg)
{
	zen_fabric_find_ioms_t *zffi = (zen_fabric_find_ioms_t *)arg;

	if (zffi->zffi_dest == ioms->zio_dest_id) {
		zffi->zffi_ioms = ioms;
		return (1);
	}

	return (0);
}

static int
zen_fabric_find_ioms_by_bus_cb(zen_ioms_t *ioms, void *arg)
{
	zen_fabric_find_ioms_t *zffi = (zen_fabric_find_ioms_t *)arg;

	if (zffi->zffi_dest == ioms->zio_pci_busno) {
		zffi->zffi_ioms = ioms;
		return (1);
	}

	return (0);
}

zen_ioms_t *
zen_fabric_find_ioms(zen_fabric_t *fabric, uint32_t destid)
{
	zen_fabric_find_ioms_t zffi = {
	    .zffi_dest = destid,
	    .zffi_ioms = NULL,
	};

	(void) zen_fabric_walk_ioms(fabric, zen_fabric_find_ioms_cb,
	    &zffi);

	return (zffi.zffi_ioms);
}

zen_ioms_t *
zen_fabric_find_ioms_by_bus(zen_fabric_t *fabric, uint32_t pci_bus)
{
	zen_fabric_find_ioms_t zffi = {
	    .zffi_dest = pci_bus,
	    .zffi_ioms = NULL,
	};

	(void) zen_fabric_walk_ioms(fabric, zen_fabric_find_ioms_by_bus_cb,
	    &zffi);

	return (zffi.zffi_ioms);
}

typedef struct zen_fabric_find_pcie_core {
	const zen_iodie_t *zffpc_iodie;
	uint16_t zffpc_start;
	uint16_t zffpc_end;
	zen_pcie_core_t *zffpc_pc;
} zen_fabric_find_pcie_core_t;

static int
zen_fabric_find_pcie_core_by_lanes_cb(zen_pcie_core_t *pc, void *arg)
{
	zen_fabric_find_pcie_core_t *zffpc = (zen_fabric_find_pcie_core_t *)arg;

	if (zffpc->zffpc_iodie == pc->zpc_ioms->zio_iodie &&
	    zffpc->zffpc_start >= pc->zpc_dxio_lane_start &&
	    zffpc->zffpc_start <= pc->zpc_dxio_lane_end &&
	    zffpc->zffpc_end >= pc->zpc_dxio_lane_start &&
	    zffpc->zffpc_end <= pc->zpc_dxio_lane_end) {
		zffpc->zffpc_pc = pc;
		return (1);
	}

	return (0);
}

zen_pcie_core_t *
zen_fabric_find_pcie_core_by_lanes(zen_iodie_t *iodie,
    uint16_t start, uint16_t end)
{
	ASSERT3U(start, <=, end);

	zen_fabric_find_pcie_core_t zffpc = {
	    .zffpc_iodie = iodie,
	    .zffpc_start = start,
	    .zffpc_end = end,
	    .zffpc_pc = NULL,
	};

	(void) zen_fabric_walk_pcie_core(iodie->zi_soc->zs_fabric,
	    zen_fabric_find_pcie_core_by_lanes_cb, &zffpc);

	return (zffpc.zffpc_pc);
}

typedef struct zen_fabric_find_thread {
	uint32_t	zfft_search;
	uint32_t	zfft_count;
	zen_thread_t	*zfft_found;
} zen_fabric_find_thread_t;

static int
zen_fabric_find_thread_by_cpuid_cb(zen_thread_t *thread, void *arg)
{
	zen_fabric_find_thread_t *zfft = (zen_fabric_find_thread_t *)arg;

	if (zfft->zfft_count == zfft->zfft_search) {
		zfft->zfft_found = thread;
		return (1);
	}
	++zfft->zfft_count;

	return (0);
}

zen_thread_t *
zen_fabric_find_thread_by_cpuid(uint32_t cpuid)
{
	zen_fabric_find_thread_t zfft = {
	    .zfft_search = cpuid,
	    .zfft_count = 0,
	    .zfft_found = NULL,
	};

	(void) zen_fabric_walk_thread(&zen_fabric,
	    zen_fabric_find_thread_by_cpuid_cb, &zfft);

	return (zfft.zfft_found);
}

/*
 * Create DMA attributes that are appropriate for the use with the fabric code.
 * These attributes are mostly used for communicating with the SMU and MPIO.
 * For DMA, we know experimentally that there are generally a register pair
 * consisting of a 32-bit length and a 64-bit address. There aren't many other
 * bits that we actually know here, however, so we generally end up making some
 * rather more conservative assumptions an attempt at safety. In particular, we
 * assume and ask for page alignment.
 *
 * XXX Remove 32-bit addr_hi constraint.
 */
void
zen_fabric_dma_attr(ddi_dma_attr_t *attr)
{
	bzero(attr, sizeof (attr));
	attr->dma_attr_version = DMA_ATTR_V0;
	attr->dma_attr_addr_lo = 0;
	attr->dma_attr_addr_hi = UINT32_MAX;
	attr->dma_attr_count_max = UINT32_MAX;
	attr->dma_attr_align = MMU_PAGESIZE;
	attr->dma_attr_minxfer = 1;
	attr->dma_attr_maxxfer = UINT32_MAX;
	attr->dma_attr_seg = UINT32_MAX;
	attr->dma_attr_sgllen = 1;
	attr->dma_attr_granular = 1;
	attr->dma_attr_flags = 0;
}

static zen_ioms_rsrc_t
zen_ioms_prd_to_rsrc(pci_prd_rsrc_t rsrc)
{
	switch (rsrc) {
	case PCI_PRD_R_IO:
		return (ZIR_PCI_LEGACY);
	case PCI_PRD_R_MMIO:
		return (ZIR_PCI_MMIO);
	case PCI_PRD_R_PREFETCH:
		return (ZIR_PCI_PREFETCH);
	case PCI_PRD_R_BUS:
		return (ZIR_PCI_BUS);
	default:
		return (ZIR_NONE);
	}
}

static struct memlist *
zen_fabric_rsrc_subsume(zen_ioms_t *ioms, zen_ioms_rsrc_t rsrc)
{
	zen_ioms_memlists_t *imp;
	struct memlist **avail, **used, *ret;

	ASSERT(ioms != NULL);

	imp = &ioms->zio_memlists;
	mutex_enter(&imp->zim_lock);
	switch (rsrc) {
	case ZIR_PCI_LEGACY:
		avail = &imp->zim_io_avail_pci;
		used = &imp->zim_io_used;
		break;
	case ZIR_PCI_MMIO:
		avail = &imp->zim_mmio_avail_pci;
		used = &imp->zim_mmio_used;
		break;
	case ZIR_PCI_PREFETCH:
		avail = &imp->zim_pmem_avail;
		used = &imp->zim_pmem_used;
		break;
	case ZIR_PCI_BUS:
		avail = &imp->zim_bus_avail;
		used = &imp->zim_bus_used;
		break;
	case ZIR_GEN_LEGACY:
		avail = &imp->zim_io_avail_gen;
		used = &imp->zim_io_used;
		break;
	case ZIR_GEN_MMIO:
		avail = &imp->zim_mmio_avail_gen;
		used = &imp->zim_mmio_used;
		break;
	default:
		mutex_exit(&imp->zim_lock);
		return (NULL);
	}

	/*
	 * If there are no resources, that may be because there never were any
	 * or they had already been handed out.
	 */
	if (*avail == NULL) {
		mutex_exit(&imp->zim_lock);
		return (NULL);
	}

	/*
	 * We have some resources available for this NB instance. In this
	 * particular case, we need to first duplicate these using kmem and then
	 * we can go ahead and move all of these to the used list.  This is done
	 * for the benefit of PCI code which expects it, but we do it
	 * universally for consistency.
	 */
	ret = memlist_kmem_dup(*avail, KM_SLEEP);

	/*
	 * XXX This ends up not really coalescing ranges, but maybe that's fine.
	 */
	while (*avail != NULL) {
		struct memlist *to_move = *avail;
		memlist_del(to_move, avail);
		memlist_insert(to_move, used);
	}

	mutex_exit(&imp->zim_lock);
	return (ret);
}

/*
 * This is a request that we take resources from a given IOMS root port and
 * basically give what remains and hasn't been allocated to PCI. This is a bit
 * of a tricky process as we want to both:
 *
 *  1. Give everything that's currently available to PCI; however, it needs
 *     memlists that are allocated with kmem due to how PCI memlists work.
 *  2. We need to move everything that we're giving to PCI into our used list
 *     just for our own tracking purposes.
 */
struct memlist *
zen_fabric_pci_subsume(uint32_t bus, pci_prd_rsrc_t rsrc)
{
	zen_ioms_t *ioms;
	zen_ioms_rsrc_t ir;

	ioms = zen_fabric_find_ioms_by_bus(&zen_fabric, bus);
	if (ioms == NULL) {
		return (NULL);
	}

	ir = zen_ioms_prd_to_rsrc(rsrc);

	return (zen_fabric_rsrc_subsume(ioms, ir));
}

/*
 * This is for the rest of the available legacy IO and MMIO space that we've set
 * aside for things that are not PCI.  The intent is that the caller will feed
 * the space to busra or the moral equivalent.  While this is presently used
 * only by the FCH and is set up only for the IOMSs that have an FCH attached,
 * in principle this could be applied to other users as well, including IOAPICs
 * and IOMMUs that are present in all NB instances.  For now this is really
 * about getting all this out of earlyboot context where we don't have modules
 * like rootnex and busra and into places where it's better managed; in this it
 * has the same purpose as its PCI counterpart above.  The memlists we supply
 * don't have to be allocated by kmem, but we do it anyway for consistency and
 * ease of use for callers.
 *
 * Curiously, AMD's documentation indicates that each of the PCI and non-PCI
 * regions associated with each NB instance must be contiguous, but there's no
 * hardware reason for that beyond the mechanics of assigning resources to PCIe
 * root ports.  So if we were to improve busra to manage these resources
 * globally instead of making PCI its own separate pool, we wouldn't need this
 * clumsy non-PCI reservation and could instead assign resources globally with
 * respect to each NB instance regardless of the requesting device type.  The
 * future's so bright, we gotta wear shades.
 */
struct memlist *
zen_fabric_gen_subsume(zen_ioms_t *ioms, zen_ioms_rsrc_t ir)
{
	return (zen_fabric_rsrc_subsume(ioms, ir));
}
