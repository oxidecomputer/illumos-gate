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

#include <sys/types.h>
#include <sys/prom_debug.h>
#include <sys/x86_archext.h>
#include <sys/ddi_subrdefs.h>
#include <sys/sysmacros.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/bitext.h>
#include <sys/spl.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_cfgspace_impl.h>
#include <sys/pcie.h>
#include <sys/platform_detect.h>

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/df.h>
#include <sys/amdzen/ccd.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/df_utils.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/hacks.h>
#include <sys/io/zen/nbif_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>
#include <sys/io/zen/smu_impl.h>

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
 * Copies the brand string into the given output buffer.  The buf and len
 * argument and return value semantics match those of snprintf(9f).
 */
size_t
zen_fabric_thread_get_brandstr(const zen_thread_t *thread,
    char *buf, size_t len)
{
	const zen_iodie_t *iodie = thread->zt_core->zc_ccx->zcx_ccd->zcd_iodie;
	return (snprintf(buf, len, "%s", iodie->zi_brandstr));
}

/*
 * No-op routine for platforms that do not support DPM weights.
 */
void
zen_fabric_thread_get_dpm_weights_noop(const zen_thread_t *thread __unused,
    const uint64_t **wp, uint32_t *nentp)
{
	*nentp = 0;
	*wp = NULL;
}

/*
 * Retrieves and reports the firmware version numbers for the SMU and DXIO/MPIO
 * on the given IO die.
 */
int
zen_fabric_dump_iodie_fw_versions(zen_iodie_t *iodie)
{
	const zen_fabric_ops_t *zfos = oxide_zen_fabric_ops();
	const uint8_t socno = iodie->zi_soc->zs_num;

	if (zen_smu_get_fw_version(iodie)) {
		zen_smu_report_fw_version(iodie);
	} else {
		cmn_err(CE_NOTE, "Socket %u: failed to read SMU version",
		    socno);
	}

	if (zfos->zfo_get_dxio_fw_version(iodie)) {
		zfos->zfo_report_dxio_fw_version(iodie);
	} else {
		cmn_err(CE_NOTE, "Socket %u: failed to read DXIO FW version",
		    socno);
	}

	return (0);
}

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
zen_fabric_set_mmio_pci_cfg_space(uint8_t dfno, uint64_t ecam_base)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
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

	/*
	 * Clear the enable bit while we update all the registers.
	 */
	val = DF_ECAM_BASE_V4_SET_EN(0, 0);
	zen_df_mech1_indirect_bcast_write32(dfno, DF_ECAM_BASE_V4, val);

	val = DF_ECAM_EXT_V4_SET_ADDR(0, ecam_base >>
	    DF_ECAM_EXT_V4_ADDR_SHIFT);
	zen_df_mech1_indirect_bcast_write32(dfno, DF_ECAM_BASE_EXT_V4, val);

	val = DF_ECAM_V4_SET_ADDR(0,
	    ((uint32_t)ecam_limit) >> DF_ECAM_V4_ADDR_SHIFT);
	zen_df_mech1_indirect_bcast_write32(dfno, DF_ECAM_LIMIT_V4, val);

	val = DF_ECAM_EXT_V4_SET_ADDR(0, ecam_limit >>
	    DF_ECAM_EXT_V4_ADDR_SHIFT);
	zen_df_mech1_indirect_bcast_write32(dfno, DF_ECAM_LIMIT_EXT_V4, val);

	/*
	 * Finally, enable and write the low bits of the base address.
	 */
	val = DF_ECAM_BASE_V4_SET_EN(0, 1);
	val = DF_ECAM_V4_SET_ADDR(val,
	    ((uint32_t)ecam_base) >> DF_ECAM_V4_ADDR_SHIFT);
	zen_df_mech1_indirect_bcast_write32(dfno, DF_ECAM_BASE_V4, val);
}

/*
 * Completely disable I/O based access to PCI configuration space.
 * After topology initialization, we can exclusively use MMIO-based access and
 * leave CFC/CF8 as otherwise normal I/O ports.
 */
static void
zen_fabric_disable_io_pci_cfg(zen_fabric_t *fabric)
{
	for (uint8_t socno = 0; socno < fabric->zf_nsocs; socno++) {
		zen_soc_t *soc = &fabric->zf_socs[socno];
		for (uint8_t iono = 0; iono < soc->zs_niodies; iono++) {
			zen_iodie_t *iodie = &soc->zs_iodies[iono];
			const df_rev_t df_rev = iodie->zi_df_rev;
			df_reg_def_t reg;
			uint32_t val;

			switch (df_rev) {
			case DF_REV_3:
				reg = DF_CORE_ACCESS_CTRL_V2;
				break;
			case DF_REV_4:
			case DF_REV_4D2:
				reg = DF_CORE_ACCESS_CTRL_V4;
				break;
			default:
				panic("Unsupported DF revision %d", df_rev);
			}

			val = zen_df_bcast_read32(iodie, reg);
			val = DF_CORE_ACCESS_CTRL_SET_DIS_PCI_CFG(val, 1);
			val = DF_CORE_ACCESS_CTRL_SET_CF8_EXT_EN(val, 0);
			zen_df_bcast_write32(iodie, reg, val);
		}
	}
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
 * Returns the Fabric ID of the IOS with the FCH.
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
		panic("Unsupported DF revision %d", df_rev);
	}
}

/*
 * Returns the assigned Node ID for the given I/O die.
 */
static uint16_t
zen_fabric_iodie_node_id(zen_iodie_t *iodie)
{
	const df_rev_t df_rev = iodie->zi_df_rev;
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
		panic("Unsupported DF revision %d", df_rev);
	}
}

/*
 * Returns the bus number to use when accessing the per-instance registers for
 * the given IOS via PCI config space.
 */
static uint8_t
zen_fabric_ios_busno(zen_iodie_t *iodie, zen_ioms_t *ioms)
{
	const df_rev_t df_rev = iodie->zi_df_rev;
	df_reg_def_t rd;

	switch (df_rev) {
	case DF_REV_3:
		rd = DF_CFG_ADDR_CTL_V2;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		rd = DF_CFG_ADDR_CTL_V4;
		break;
	default:
		panic("Unsupported DF revision %d", df_rev);
	}

	return (DF_CFG_ADDR_CTL_GET_BUS_NUM(zen_df_read32(iodie,
	    ioms->zio_ios_inst_id, rd)));
}

/*
 * Returns the PCI bus number used for accessing SMN registers on the given
 * I/O die.
 */
static uint8_t
zen_fabric_smn_busno(zen_iodie_t *iodie)
{
	const df_rev_t df_rev = iodie->zi_df_rev;
	df_reg_def_t rd;

	switch (df_rev) {
	case DF_REV_3:
		rd = DF_CFG_ADDR_CTL_V2;
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		rd = DF_CFG_ADDR_CTL_V4;
		break;
	default:
		panic("Unsupported DF revision %d", df_rev);
	}

	return (DF_CFG_ADDR_CTL_GET_BUS_NUM(zen_df_bcast_read32(iodie, rd)));
}

/*
 * Returns the total number of CCMs and IOM/IOS instances present on the given
 * I/O die, as well as the base (lowest) Instance IDs for each.
 *
 * The number of certain components as well as their base (lowest) Instance IDs
 * may vary between microarchitectures / products and rather than hardcode these
 * values for every chip we'd like to support, we discover them dynamically.
 *
 * Note that depending on the specific DF version, the IOM and IOS instances may
 * be treated as separate (IOM/IOS) components or as a single (IOMS) component
 * when it comes to accessing the per-instance registers we need. Regardless, we
 * always expect a 1-1 relationship and in the latter case, the returned
 * Instance IDs will be the same.
 */
static void
zen_fabric_discover_iodie_components(zen_iodie_t *iodie)
{
	const df_rev_t df_rev = iodie->zi_df_rev;
	const df_fabric_decomp_t *decomp = &iodie->zi_soc->zs_fabric->zf_decomp;
	df_reg_def_t reg;
	uint32_t val;
	uint8_t ccm_comp_id, iom_comp_id, ios_comp_id;
	bool found_ccm = false, found_iom = false, found_ios = false;

	/*
	 * Note we use DF::DieComponentMapC/D rather than DF::SystemComponentCnt
	 * which holds system-wide counts and hence might be inaccurate, e.g.,
	 * on a 2P system since we specifically are only interested in just the
	 * given I/O die.
	 */

	reg = (df_rev >= DF_REV_4) ? DF_DIE_COMP_MAPC_V4 :
	    DF_DIE_COMP_MAPC_V3;
	val = zen_df_bcast_read32(iodie, reg);
	iodie->zi_nccms = DF_DIE_COMP_MAPC_GET_CCM_COUNT(val);
	ccm_comp_id = DF_DIE_COMP_MAPC_GET_CCM_COMP_ID(val);

	/*
	 * Grab the count of IOM and IOS components on this I/O die and verify
	 * the 1-1 relationship between IOM and IOS instances as we expect.
	 * We also need to verify the count doesn't exceed the maximum number of
	 * zen_ioms_t instances we've statically allocated.
	 */
	reg = (df_rev >= DF_REV_4) ? DF_DIE_COMP_MAPD_V4 :
	    DF_DIE_COMP_MAPD_V3;
	val = zen_df_bcast_read32(iodie, reg);
	VERIFY3U(DF_DIE_COMP_MAPD_GET_IOM_COUNT(val), ==,
	    DF_DIE_COMP_MAPD_GET_IOS_COUNT(val));
	iodie->zi_nioms = DF_DIE_COMP_MAPD_GET_IOM_COUNT(val);
	VERIFY3U(iodie->zi_nioms, <=, ZEN_IODIE_MAX_IOMS);
	iom_comp_id = DF_DIE_COMP_MAPD_GET_IOM_COMP_ID(val);
	ios_comp_id = DF_DIE_COMP_MAPD_GET_IOS_COMP_ID(val);

	/*
	 * Unfortunately, DF::DieComponentMapC/D give us the Component ID of the
	 * lowest numbered component but we need the Instance ID to access the
	 * per-instance registers.  To find those, we'll just loop over the
	 * instances until we find the matching component.
	 */

	val = zen_df_bcast_read32(iodie, DF_FBICNT);
	iodie->zi_nents = DF_FBICNT_GET_COUNT(val);
	for (uint8_t inst = 0; inst < iodie->zi_nents; inst++) {
		uint32_t fabric_id, sock, die, comp_id;

		val = zen_df_read32(iodie, inst, DF_FBIINFO0);
		if (DF_FBIINFO0_V3_GET_ENABLED(val) == 0)
			continue;

		/*
		 * We're only interested in CCM, IOM, and IOS instances.
		 */
		switch (DF_FBIINFO0_GET_TYPE(val)) {
		case DF_TYPE_CCM:
			break;
		case DF_TYPE_IOMS:
			break;
		case DF_TYPE_NCS:
			/*
			 * DFv4 specifically (and not DFv4D2) classifies IOS
			 * instances differently. IOM instances are handled the
			 * same across all DF versions. DFv3 doesn't expose a
			 * separate IOS instance.
			 */
			if (df_rev == DF_REV_4 &&
			    DF_FBIINFO0_GET_SUBTYPE(val) ==
			    DF_NCS_SUBTYPE_IOS_V4) {
				break;
			}
			continue;
		default:
			continue;
		}

		/*
		 * To find this instance's Component ID, we must extract it
		 * from its Fabric ID.
		 */
		val = zen_df_read32(iodie, inst, DF_FBIINFO3);
		switch (df_rev) {
		case DF_REV_3:
			fabric_id = DF_FBIINFO3_V3_GET_BLOCKID(val);
			break;
		case DF_REV_4:
			fabric_id = DF_FBIINFO3_V4_GET_BLOCKID(val);
			break;
		case DF_REV_4D2:
			fabric_id = DF_FBIINFO3_V4D2_GET_BLOCKID(val);
			break;
		default:
			panic("Unsupported DF revision %d", df_rev);
		}
		zen_fabric_id_decompose(decomp, fabric_id, &sock, &die,
		    &comp_id);
		ASSERT3U(sock, ==, iodie->zi_soc->zs_num);
		ASSERT3U(die, ==, 0);

		/*
		 * With that we can check if we've got the right instance.
		 * Note, the IOM & IOS may be actually be the same instance as
		 * was the case prior to DFv4.
		 */

		if (comp_id == ccm_comp_id) {
			VERIFY3B(found_ccm, ==, false);
			iodie->zi_base_ccm_id = inst;
			found_ccm = true;
		}

		if (comp_id == iom_comp_id) {
			VERIFY3B(found_iom, ==, false);
			iodie->zi_base_iom_id = inst;
			found_iom = true;
		}

		if (comp_id == ios_comp_id) {
			VERIFY3B(found_ios, ==, false);
			iodie->zi_base_ios_id = inst;
			found_ios = true;
		}

		if (found_ccm && found_iom && found_ios)
			break;
	}

	if (!found_ccm || !found_iom || !found_ios) {
		cmn_err(CE_PANIC,
		    "Failed to find CCM, IOMS and/or IOS instance. "
		    "CCM Component ID: %u, IOM Component ID: %u, "
		    "IOS Component ID: %u", ccm_comp_id, iom_comp_id,
		    ios_comp_id);
	}
}

/*
 * Returns the assigned Fabric ID for the given IOS.
 */
static uint16_t
zen_ios_fabric_id(zen_ioms_t *ioms)
{
	const df_rev_t df_rev = ioms->zio_iodie->zi_df_rev;
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
		panic("Unsupported DF revision %d", df_rev);
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

/*
 * Returns the set of cores enabled for a CCD on the given I/O die. Each bit
 * position corresponds to an individual core at that physical index with the
 * value indicating whether the core is enabled.
 */
static uint32_t
zen_ccd_cores_enabled(zen_iodie_t *iodie, uint8_t ccdpno)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const df_rev_t df_rev = iodie->zi_df_rev;
	df_reg_def_t phys_core_en_v3[] = {
		DF_PHYS_CORE_EN0_V3,
		DF_PHYS_CORE_EN1_V3,
	};
	df_reg_def_t phys_core_en_v4[] = {
		DF_PHYS_CORE_EN0_V4,
		DF_PHYS_CORE_EN1_V4,
		DF_PHYS_CORE_EN2_V4,
		DF_PHYS_CORE_EN3_V4,
		DF_PHYS_CORE_EN4_V4,
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
		panic("Unsupported DF revision %d", df_rev);
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

static apicid_t
zen_fabric_thread_apicid(zen_thread_t *thread)
{
	zen_core_t *core = thread->zt_core;
	zen_ccx_t *ccx = core->zc_ccx;
	zen_ccd_t *ccd = ccx->zcx_ccd;
	zen_iodie_t *iodie = ccd->zcd_iodie;
	smn_reg_t reg;
	uint32_t pkg0, pkg7;
	amdzen_apic_decomp_t apic_decomp;
	x86_uarch_t uarch;
	apicid_t apicid = 0;

	uarch = uarchrev_uarch(oxide_board_data->obd_cpuinfo.obc_uarchrev);

	reg = SCFCTP_PMREG_INITPKG0(ccd->zcd_physical_dieno,
	    ccx->zcx_physical_cxno, core->zc_physical_coreno);
	pkg0 = zen_core_read(core, reg);

	reg = SCFCTP_PMREG_INITPKG7(ccd->zcd_physical_dieno,
	    ccx->zcx_physical_cxno, core->zc_physical_coreno);
	pkg7 = zen_smn_read(iodie, reg);

	zen_initpkg_to_apic(pkg0, pkg7, uarch, &apic_decomp);
	zen_apic_id_compose(&apic_decomp, iodie->zi_soc->zs_num,
	    0, ccd->zcd_logical_dieno, ccx->zcx_logical_cxno,
	    core->zc_logical_coreno, thread->zt_threadno, &apicid);

	return (apicid);
}

static uint_t
zen_fabric_ccx_init_core(zen_ccx_t *ccx, uint8_t lidx, uint8_t pidx)
{
	smn_reg_t reg;
	uint32_t val;
	zen_core_t *core = &ccx->zcx_cores[lidx];
	zen_ccd_t *ccd = ccx->zcx_ccd;

	core->zc_ccx = ccx;
	core->zc_physical_coreno = pidx;

	reg = SCFCTP_PMREG_INITPKG0(ccd->zcd_physical_dieno,
	    ccx->zcx_physical_cxno, core->zc_physical_coreno);
	val = zen_core_read(core, reg);
	VERIFY3U(val, !=, 0xffffffffU);

	core->zc_logical_coreno = SCFCTP_PMREG_INITPKG0_GET_LOG_CORE(val);
	VERIFY3U(core->zc_logical_coreno, ==, lidx);

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

		thread->zt_apicid = zen_fabric_thread_apicid(thread);
	}

	return (core->zc_nthreads);
}

static int
zen_fabric_ccx_init_soc_iodie_cb(zen_iodie_t *iodie, void *arg)
{
	const x86_uarchrev_t uarch = oxide_board_data->obd_cpuinfo.obc_uarchrev;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	uint32_t *nthreadsp = arg;
	const df_rev_t df_rev = iodie->zi_df_rev;
	uint32_t nthreads = 0;
	uint32_t val;
	uint8_t nccds = 0;
	const uint8_t nccms = iodie->zi_nccms;
	bool zen5;
	uint8_t ccm_subtype;

	/*
	 * With each CCM possibly connected to up to 2 CCDs, each bit position
	 * corresponds to one of 2 ports (SDPs) on each CCM and whether there's
	 * a CCD connected to it:
	 *
	 *	Bit Position (X)		CCM Mapping
	 *	----------------		-----------
	 *		  N-1:0			CCM X, SDP 0
	 *		2*N-1:N			CCM X-N, SDP 1
	 *
	 * Where N is the number of CCMs. This implies our bit map must be at
	 * least N * 2 (DF_MAX_CCDS_PER_CCM) bits wide.
	 *
	 * Thus, a 1-bit at position X means the CCD with physical number X is
	 * enabled and connected to CCM (X%N) via port (X/N). The logical
	 * numbers are then assigned sequentially for each enabled CCD.
	 */
	uint16_t ccdmap = 0;
	VERIFY3U(sizeof (ccdmap) * NBBY, >=, nccms * DF_MAX_CCDS_PER_CCM);

	/*
	 * Zen 5 moved a couple of registers from SMU::PWR to L3::SOC.
	 */
	if (uarchrev_matches(uarch, X86_UARCHREV_AMD_ZEN3_ANY) ||
	    uarchrev_matches(uarch, X86_UARCHREV_AMD_ZEN4_ANY)) {
		zen5 = false;
	} else if (uarchrev_matches(uarch, X86_UARCHREV_AMD_ZEN5_ANY)) {
		zen5 = true;
	} else {
		panic("Unsupported uarch %x", uarch);
	}

	/*
	 * The CCM subtype interpretation changed after DFv4 minor version 1.
	 * Pick the correct one to check against each CCM in the loop below.
	 */
	ccm_subtype = (df_rev >= DF_REV_4 && iodie->zi_df_minor >= 1) ?
	    DF_CCM_SUBTYPE_CPU_V4P1 : DF_CCM_SUBTYPE_CPU_V2;

	/*
	 * To determine the physical CCD numbers, we iterate over the CCMs
	 * and note what CCDs (if any) are present and enabled.
	 */
	for (uint8_t ccmno = 0; ccmno < nccms; ccmno++) {
		uint32_t ccminst = iodie->zi_base_ccm_id + ccmno;

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
		 * Also verify the subtype lest we accidentally try to proceed
		 * with a non-CPU CCM (e.g., an ACM).
		 */
		VERIFY3U(DF_FBIINFO0_GET_SUBTYPE(val), ==, ccm_subtype);

		switch (df_rev) {
		case DF_REV_3:
			/*
			 * With DFv3, we assume a 1-1 mapping of CCDs to CCMs.
			 */
			ccdmap |= (1 << ccmno);
			break;
		case DF_REV_4:
		case DF_REV_4D2: {
			/*
			 * DFv4+ allows for up to 2 CCDs per CCM, depending on
			 * if wide mode is enabled.
			 */
			uint32_t ccden = zen_df_read32(iodie, ccminst,
			    DF_CCD_EN_V4);
			bool ccd0en = DF_CCD_EN_V4_GET_CCD_EN(ccden) & 1;
			bool ccd1en = (DF_CCD_EN_V4_GET_CCD_EN(ccden) >> 1) & 1;
			bool wide;

			/*
			 * Note if first possible CCD is enabled.
			 */
			ccdmap |= ccd0en << ccmno;

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

			if (!wide) {
				/*
				 * If wide mode is disabled, and DF::CCDEnable
				 * says the second CCD on this CCM is enabled,
				 * note that in the upper half of the ccd map.
				 */
				ccdmap |= ccd1en << nccms << ccmno;
			} else if (DF_CCD_EN_V4_GET_CCD_EN(ccden) != 0) {
				/*
				 * But if wide mode is enabled (and thus both
				 * SDPs are connected to a single CCD) AND
				 * either of the CCDs are enabled, we'll assume
				 * the lower CCD index is the one to use.
				 *
				 * See also amdzen`amdzen_setup_df_ccm.
				 */
				ccdmap |= (1 << ccmno);
			}
			break;
		}
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
		iodie->zi_nccds++;

		/*
		 * The firmware should've set this correctly -- let's validate
		 * our assumption.
		 * XXX: Avoid panicking on bad data from firmware
		 */
		reg = amdzen_smupwr_smn_reg(ccd->zcd_physical_dieno,
		    D_SMUPWR_CCD_DIE_ID, 0);
		val = zen_ccd_read(ccd, reg);
		VERIFY3U(val, ==, ccdpno);

		if (!zen5) {
			reg = amdzen_smupwr_smn_reg(ccd->zcd_physical_dieno,
			    D_SMUPWR_THREAD_CFG, 0);
			val = zen_ccd_read(ccd, reg);
			ccd->zcd_nccxs = 1 +
			    SMUPWR_THREAD_CFG_GET_COMPLEX_COUNT(val);
		} else {
			reg = amdzen_l3soc_smn_reg(ccd->zcd_physical_dieno,
			    D_L3SOC_THREAD_CFG, 0);
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
			reg = amdzen_smupwr_smn_reg(ccd->zcd_physical_dieno,
			    D_SMUPWR_CORE_EN, 0);
			val = zen_ccd_read(ccd, reg);
			VERIFY3U(SMUPWR_CORE_EN_GET(val), ==, cores_enabled);
		} else {
			reg = amdzen_l3soc_smn_reg(ccd->zcd_physical_dieno,
			    D_L3SOC_CORE_EN, 0);
			val = zen_ccd_read(ccd, reg);
			VERIFY3U(L3SOC_CORE_EN_GET(val), ==, cores_enabled);
		}

		ccx->zcx_ccd = ccd;
		/*
		 * We always assume the first CCX is at physical index 0 and
		 * that the physical and logical numbering is equivalent.
		 */
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

		if (nccds != 0) {
			VERIFY3U(SCFCTP_PMREG_INITPKG7_GET_N_DIES(val) + 1, ==,
			    nccds);
		}
		nccds = SCFCTP_PMREG_INITPKG7_GET_N_DIES(val) + 1;

		for (pcore = 0, lcore = 0; pcore < consts->zpc_cores_per_ccx;
		    pcore++) {
			if ((cores_enabled & (1 << pcore)) == 0)
				continue;
			nthreads += zen_fabric_ccx_init_core(ccx, lcore, pcore);
			++lcore;
		}

		VERIFY3U(lcore, ==, ccx->zcx_ncores);
	}

	VERIFY3U(iodie->zi_nccds, ==, nccds);
	*nthreadsp += nthreads;

	return (0);
}

static uint32_t
zen_fabric_ccx_init_soc(zen_soc_t *soc)
{
	uint32_t nthreads;

	nthreads = 0;
	VERIFY0(zen_fabric_walk_iodie(soc->zs_fabric,
	    zen_fabric_ccx_init_soc_iodie_cb, &nthreads));

	return (nthreads);
}

/*
 * Unfortunately, we're too early in the boot process (pre CPUID_PASS_BASIC) to
 * use cpuid_get_addrsize so we just read the appropriate CPUID leaf directly.
 */
uint8_t
zen_fabric_physaddr_size(void)
{
	struct cpuid_regs cp = { .cp_eax = 0x80000008 };
	(void) __cpuid_insn(&cp);
	return (CPUID_AMD_EAX_PABITS(cp.cp_eax));
}

/*
 * The callback zen_determine_df_vers uses to actually read a given register.
 * Because we don't know what version we are yet, we do not use any of the
 * zen_df_* routines that are versioned.
 */
static uint32_t
zen_fabric_determine_df_vers_cb(const df_reg_def_t rd, const void *arg)
{
	const zen_iodie_t *iodie = arg;
	return (pci_getl_func(AMDZEN_DF_BUSNO, iodie->zi_devno, rd.drd_func,
	    rd.drd_reg));
}

static void
zen_fabric_nbif_func_init(zen_nbif_t *nbif, uint8_t funcno)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const zen_nbif_info_t *ninfo = consts->zpc_nbif_data[nbif->zn_num];
	zen_nbif_func_t *func = &nbif->zn_funcs[funcno];
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	const zen_ioms_t *ioms = nbif->zn_ioms;
	const uint8_t numports =
	    consts->zpc_pcie_int_ports[ioms->zio_iohcnum].zinp_count;

	func->znf_nbif = nbif;
	func->znf_num = funcno;
	func->znf_flags = 0;
	if (ninfo[funcno].zni_dev >= numports) {
		func->znf_type = ZEN_NBIF_T_ABSENT;
	} else {
		func->znf_type = ninfo[funcno].zni_type;
		func->znf_dev = ninfo[funcno].zni_dev;
		func->znf_func = ninfo[funcno].zni_func;
		if (ninfo[funcno].zni_enabled)
			func->znf_flags |= ZEN_NBIF_F_ENABLED;

		/* Dummy devices in theory need no explicit configuration */
		if (func->znf_type == ZEN_NBIF_T_DUMMY) {
			func->znf_flags |= ZEN_NBIF_F_NO_CONFIG;
			goto out;
		}

		/*
		 * FLR is enabled on all device types apart from AZ. However,
		 * for SATA devices, only for the first function.
		 */
		if (func->znf_type != ZEN_NBIF_T_AZ &&
		    (func->znf_type != ZEN_NBIF_T_SATA || func->znf_func < 1)) {
			func->znf_flags |= ZEN_NBIF_F_FLR_EN;
		}

		/*
		 * TPH CPLR is enabled for bridges and some other types. Some
		 * uarches extend this list via the nBIF init hook.
		 */
		if (func->znf_type == ZEN_NBIF_T_MPDMATF ||
		    func->znf_type == ZEN_NBIF_T_NTB ||
		    func->znf_type == ZEN_NBIF_T_SVNTB ||
		    func->znf_type == ZEN_NBIF_T_PVNTB ||
		    func->znf_type == ZEN_NBIF_T_NVME) {
			func->znf_flags |= ZEN_NBIF_F_TPH_CPLR_EN;
		}

		/*
		 * All functions are configured to use advisory non-fatal
		 * errors for poisoned error log by default. Some uarches
		 * selectively override this.
		 */
		func->znf_flags |= ZEN_NBIF_F_PANF_EN;
	}

out:
	/*
	 * uarch-specific nBIF init hook.
	 */
	if (fops->zfo_nbif_init != NULL)
		fops->zfo_nbif_init(nbif);
}

static void
zen_fabric_ioms_nbif_init(zen_ioms_t *ioms, uint8_t nbifno)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	zen_nbif_t *nbif = &ioms->zio_nbifs[nbifno];
	const uint8_t numports =
	    consts->zpc_pcie_int_ports[ioms->zio_iohcnum].zinp_count;

	nbif->zn_num = nbifno;
	nbif->zn_ioms = ioms;
	if (numports == 0)
		nbif->zn_nfuncs = 0;
	else
		nbif->zn_nfuncs = consts->zpc_nbif_nfunc[nbifno];

	ASSERT3U(nbif->zn_nfuncs, <=, ZEN_NBIF_MAX_FUNCS);

	for (uint8_t funcno = 0; funcno < nbif->zn_nfuncs; funcno++)
		zen_fabric_nbif_func_init(nbif, funcno);
}

static void
zen_fabric_ioms_pcie_init(zen_ioms_t *ioms)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();

	ioms->zio_npcie_cores = fops->zfo_ioms_n_pcie_cores(ioms->zio_num);

	for (uint8_t coreno = 0; coreno < ioms->zio_npcie_cores; coreno++) {
		zen_pcie_core_t *zpc = &ioms->zio_pcie_cores[coreno];
		const zen_pcie_core_info_t *cinfop;
		uint8_t uid = 0;

		zpc->zpc_coreno = coreno;
		zpc->zpc_ioms = ioms;
		zpc->zpc_nports = fops->zfo_pcie_core_n_ports(coreno);

		mutex_init(&zpc->zpc_strap_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));

		/*
		 * Calculate the unit ID for this core's first SDP port, which
		 * will later be programmed into PCIECORE::PCIE_SDP_CTRL. In
		 * all supported microarchitectures, PCIe ports are assigned
		 * contiguously across SDP ports. To determine the base unit ID
		 * for a specific core, we start with the base unit ID for core
		 * 0 and add the number of ports in each preceding core.
		 */
		uid = consts->zpc_pcie_core0_unitid;
		for (uint8_t i = 0; i < coreno; i++)
			uid += fops->zfo_pcie_core_n_ports(i);
		zpc->zpc_sdp_unit = uid;

		cinfop = fops->zfo_pcie_core_info(ioms->zio_num, coreno);
		zpc->zpc_dxio_lane_start = cinfop->zpci_dxio_start;
		zpc->zpc_dxio_lane_end = cinfop->zpci_dxio_end;
		zpc->zpc_phys_lane_start = cinfop->zpci_phy_start;
		zpc->zpc_phys_lane_end = cinfop->zpci_phy_end;

		for (uint8_t portno = 0; portno < zpc->zpc_nports; portno++) {
			zen_pcie_port_t *port = &zpc->zpc_ports[portno];
			const zen_pcie_port_info_t *pinfop =
			    fops->zfo_pcie_port_info(coreno, portno);

			port->zpp_portno = portno;
			port->zpp_core = zpc;
			port->zpp_device = pinfop->zppi_dev;
			port->zpp_func = pinfop->zppi_func;
		}
	}
}

void
zen_fabric_topo_init_ioms(zen_iodie_t *iodie, uint8_t iomsno)
{
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	const df_rev_t df_rev = consts->zpc_df_rev;
	const uint32_t fch_ios_fid = zen_fch_ios_fabric_id(df_rev);

	zen_ioms_t *ioms = &iodie->zi_ioms[iomsno];

	ioms->zio_num = iomsno;
	ioms->zio_iodie = iodie;

	ioms->zio_iom_inst_id = iodie->zi_base_iom_id + iomsno;
	ioms->zio_ios_inst_id = iodie->zi_base_ios_id + iomsno;

	ioms->zio_dest_id = zen_ios_fabric_id(ioms);
	ioms->zio_pci_busno = zen_fabric_ios_busno(iodie, ioms);

	if (ioms->zio_dest_id == fch_ios_fid) {
		ioms->zio_flags |= ZEN_IOMS_F_HAS_FCH;
	}

	/*
	 * uarch-specific IOMS init hook.
	 */
	if (fops->zfo_ioms_init != NULL)
		fops->zfo_ioms_init(ioms);

	zen_fabric_ioms_pcie_init(ioms);

	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_NBIF) != 0) {
		ioms->zio_nnbifs = consts->zpc_nnbif;
		for (uint8_t nbifno = 0; nbifno < ioms->zio_nnbifs; nbifno++)
			zen_fabric_ioms_nbif_init(ioms, nbifno);
	}
}

static void
zen_fabric_topo_init_iodie(zen_soc_t *soc, uint8_t dieno)
{
	zen_iodie_t *iodie = &soc->zs_iodies[dieno];
	zen_fabric_t *fabric = soc->zs_fabric;
	const zen_platform_consts_t *consts = oxide_zen_platform_consts();
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	const df_rev_t df_rev = consts->zpc_df_rev;
	uint8_t socno = soc->zs_num;

	iodie->zi_num = dieno;
	iodie->zi_devno = AMDZEN_DF_FIRST_DEVICE + socno;
	iodie->zi_soc = soc;

	/*
	 * Populate the major, minor, and revision fields of the given I/O die.
	 */
	zen_determine_df_vers(zen_fabric_determine_df_vers_cb, iodie,
	    &iodie->zi_df_major, &iodie->zi_df_minor, &iodie->zi_df_rev);
	if (iodie->zi_df_rev != df_rev) {
		cmn_err(CE_PANIC,
		    "DF rev mismatch: expected %d, found %d (SoC/DF: %d/0)",
		    df_rev, iodie->zi_df_rev, socno);
	}

	iodie->zi_node_id = zen_fabric_iodie_node_id(iodie);

	if (iodie->zi_node_id == 0) {
		iodie->zi_flags = ZEN_IODIE_F_PRIMARY;
	}

	/*
	 * Because we do not know the circumstances all these locks will be used
	 * during early initialization, set these to be spin locks for the
	 * moment.
	 */
	mutex_init(&iodie->zi_df_ficaa_lock, NULL, MUTEX_SPIN,
	    (ddi_iblock_cookie_t)ipltospl(15));
	mutex_init(&iodie->zi_smn_lock, NULL, MUTEX_SPIN,
	    (ddi_iblock_cookie_t)ipltospl(15));
	mutex_init(&iodie->zi_smu_lock, NULL, MUTEX_SPIN,
	    (ddi_iblock_cookie_t)ipltospl(15));
	mutex_init(&iodie->zi_mpio_lock, NULL, MUTEX_SPIN,
	    (ddi_iblock_cookie_t)ipltospl(15));

	iodie->zi_smn_busno = zen_fabric_smn_busno(iodie);

	zen_fabric_discover_iodie_components(iodie);

	fabric->zf_total_ioms += iodie->zi_nioms;
	for (uint8_t iomsno = 0; iomsno < iodie->zi_nioms; iomsno++)
		zen_fabric_topo_init_ioms(iodie, iomsno);

	/*
	 * In order to guarantee that we can safely perform SMU and DXIO
	 * functions, retrieve, store, and print firmware revisions.  We
	 * do this here after setting the SMN bus number and other
	 * initialization.
	 */
	zen_fabric_dump_iodie_fw_versions(iodie);

	/*
	 * Read the brand string from the SMU.
	 */
	if (!zen_smu_get_brand_string(iodie,
	    iodie->zi_brandstr, sizeof (iodie->zi_brandstr))) {
		iodie->zi_brandstr[0] = '\0';
	}

	/*
	 * We compare the brand string against that from first IO die, to verify
	 * the assumption that they match.  If they do not, we warn and
	 * overwrite  what we got from our SMU with what die 0 got from its SMU.
	 */
	if (strcmp(iodie->zi_brandstr,
	    iodie->zi_soc->zs_iodies[0].zi_brandstr) != 0) {
		cmn_err(CE_WARN,
		    "Brand string on IO die differs first die; overwriting: "
		    "'%s' versus '%s'",
		    iodie->zi_brandstr,
		    iodie->zi_soc->zs_iodies[0].zi_brandstr);
		bcopy(iodie->zi_soc->zs_iodies[0].zi_brandstr,
		    iodie->zi_brandstr, sizeof (iodie->zi_brandstr));
	}

	/*
	 * Invoke miscellaneous uarch-specific SMU initialization.
	 */
	if (fops->zfo_smu_misc_init != NULL)
		fops->zfo_smu_misc_init(iodie);
}

uint32_t
zen_fabric_topo_init_soc(zen_fabric_t *fabric, uint8_t socno)
{
	zen_soc_t *soc = &fabric->zf_socs[socno];
	uint32_t nthreads;

	soc->zs_num = socno;
	soc->zs_fabric = fabric;
	soc->zs_niodies = ZEN_FABRIC_MAX_DIES_PER_SOC;

	/*
	 * We've already programmed the ECAM base for the first DF above
	 * but we need to do the same for any subsequent I/O dies.
	 */
	if (socno != 0) {
		/*
		 * We assume single-die SoCs hence socno == iono but
		 * let's be explicit about it.
		 */
		VERIFY3U(ZEN_FABRIC_MAX_DIES_PER_SOC, ==, 1);
		zen_fabric_set_mmio_pci_cfg_space(socno, fabric->zf_ecam_base);
	}

	for (uint8_t dieno = 0; dieno < soc->zs_niodies; dieno++)
		zen_fabric_topo_init_iodie(soc, dieno);

	/*
	 * Initialize the CCXs for this SOC/IOD.
	 */
	nthreads = zen_fabric_ccx_init_soc(soc);

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
	uint32_t nthreads = 0;
	uint64_t mmio64_end, phys_end;

	/*
	 * Make sure the platform specific constants are actually set.
	 */
	VERIFY3U(consts->zpc_df_rev, !=, DF_REV_UNKNOWN);
	VERIFY3U(consts->zpc_ccds_per_iodie, !=, 0);
	VERIFY3U(consts->zpc_cores_per_ccx, !=, 0);

	/*
	 * And that they're within the limits we support.
	 */
	VERIFY3U(consts->zpc_ccds_per_iodie, <=, ZEN_MAX_CCDS_PER_IODIE);
	VERIFY3U(consts->zpc_cores_per_ccx, <=, ZEN_MAX_CORES_PER_CCX);

	PRM_POINT("zen_fabric_topo_init() starting...");

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
	/*
	 * The last 12 GiB of the physical address space is inaccessible and
	 * will fault on any CPU accesses and abort I/O attempts so we must
	 * stop short of it for
	 */
	const uint64_t GIB = 1024UL * 1024UL * 1024UL;
	uint8_t physaddr_size = fops->zfo_physaddr_size();

	phys_end = 1UL << physaddr_size;
	mmio64_end = phys_end - 12UL * GIB;
	VERIFY3U(mmio64_end, >, fabric->zf_mmio64_base);
	fabric->zf_mmio64_size = mmio64_end - fabric->zf_mmio64_base;

	zen_fabric_set_mmio_pci_cfg_space(0, fabric->zf_ecam_base);
	pcie_cfgspace_init();

	/*
	 * Now that we have access to PCIe configuration space, we can start
	 * discovering the specifics of the fabric topology.
	 */

	/*
	 * Grab the masks & shifts needed for decoding global Fabric IDs.
	 */
	zen_fabric_decomp_init(df_rev, &fabric->zf_decomp);

	/*
	 * Grab the number of SoCs present in the system and verify against
	 * our assumptions.
	 */
	switch (df_rev) {
	case DF_REV_3:
		syscfg = zen_df_early_read32(DF_SYSCFG_V3);
		syscomp = zen_df_early_read32(DF_COMPCNT_V2);
		nsocs = DF_SYSCFG_V3_GET_OTHER_SOCK(syscfg) + 1;
		VERIFY3U(nsocs, ==, DF_COMPCNT_V2_GET_PIE(syscomp));
		break;
	case DF_REV_4:
	case DF_REV_4D2:
		syscfg = zen_df_early_read32(DF_SYSCFG_V4);
		syscomp = zen_df_early_read32(DF_COMPCNT_V4);
		nsocs = DF_SYSCFG_V4_GET_OTHER_SOCK(syscfg) + 1;
		VERIFY3U(nsocs, ==, DF_COMPCNT_V4_GET_PIE(syscomp));
		break;
	default:
		cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
	}

	fabric->zf_nsocs = nsocs;
	for (uint8_t socno = 0; socno < nsocs; socno++)
		nthreads += zen_fabric_topo_init_soc(fabric, socno);

	zen_fabric_disable_io_pci_cfg(fabric);

	if (nthreads > NCPU) {
		cmn_err(CE_WARN, "%d CPUs found but only %d supported",
		    nthreads, NCPU);
		nthreads = NCPU;
	}
	boot_max_ncpus = max_ncpus = boot_ncpus = nthreads;
}

static int
zen_fabric_init_pcie_dbg(zen_pcie_dbg_t **dbg,
    const zen_pcie_reg_dbg_t *regs, const size_t nregs)
{
	if (nregs == 0)
		return (0);

	*dbg = kmem_zalloc(ZEN_PCIE_DBG_SIZE(nregs), KM_SLEEP);
	(*dbg)->zpd_nregs = nregs;

	for (size_t rn = 0; rn < nregs; rn++) {
		zen_pcie_reg_dbg_t *rd = &(*dbg)->zpd_regs[rn];

		rd->zprd_name = regs[rn].zprd_name;
		rd->zprd_def = regs[rn].zprd_def;
	}

	return (0);
}

static int
zen_fabric_init_pcie_core_dbg(zen_pcie_core_t *pc, void *arg)
{
	const zen_platform_consts_t *platform_consts =
	    oxide_zen_platform_consts();

	return (zen_fabric_init_pcie_dbg(&pc->zpc_dbg,
	    platform_consts->zpc_pcie_core_dbg_regs,
	    *platform_consts->zpc_pcie_core_dbg_nregs));
}

static int
zen_fabric_init_pcie_port_dbg(zen_pcie_port_t *port, void *arg)
{
	const zen_platform_consts_t *platform_consts =
	    oxide_zen_platform_consts();

	return (zen_fabric_init_pcie_dbg(&port->zpp_dbg,
	    platform_consts->zpc_pcie_port_dbg_regs,
	    *platform_consts->zpc_pcie_port_dbg_nregs));
}

static inline void *
zen_pcie_dbg_cookie(uint32_t stage, uint8_t iodie)
{
	uintptr_t rv;

	rv = (uintptr_t)stage;
	rv |= ((uintptr_t)iodie) << 32;

	return ((void *)rv);
}

static inline uint32_t
zen_pcie_dbg_cookie_to_stage(void *arg)
{
	uintptr_t av = (uintptr_t)arg;

	return ((uint32_t)(av & UINT32_MAX));
}

static inline uint8_t
zen_pcie_dbg_cookie_to_iodie(void *arg)
{
	uintptr_t av = (uintptr_t)arg;

	return ((uint8_t)(av >> 32));
}

static int
zen_pcie_populate_core_dbg(zen_pcie_core_t *pc, void *arg)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	uint32_t stage = zen_pcie_dbg_cookie_to_stage(arg);
	uint8_t iodie_match = zen_pcie_dbg_cookie_to_iodie(arg);
	zen_pcie_dbg_t *dbg = pc->zpc_dbg;

	if (dbg == NULL)
		return (0);

	if (iodie_match != ZEN_IODIE_MATCH_ANY &&
	    iodie_match != zen_iodie_node_id(pc->zpc_ioms->zio_iodie)) {
		return (0);
	}

	for (size_t rn = 0; rn < dbg->zpd_nregs; rn++) {
		smn_reg_t reg;

		reg = fabric_ops->zfo_pcie_core_reg(pc,
		    dbg->zpd_regs[rn].zprd_def);
		dbg->zpd_regs[rn].zprd_val[stage] =
		    zen_pcie_core_read(pc, reg);
		dbg->zpd_regs[rn].zprd_ts[stage] = gethrtime();
	}

	dbg->zpd_last_stage = stage;

	return (0);
}

static int
zen_pcie_populate_port_dbg(zen_pcie_port_t *port, void *arg)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	uint32_t stage = zen_pcie_dbg_cookie_to_stage(arg);
	uint8_t iodie_match = zen_pcie_dbg_cookie_to_iodie(arg);
	zen_pcie_dbg_t *dbg = port->zpp_dbg;

	if (dbg == NULL)
		return (0);

	if (iodie_match != ZEN_IODIE_MATCH_ANY &&
	    iodie_match !=
	    zen_iodie_node_id(port->zpp_core->zpc_ioms->zio_iodie)) {
		return (0);
	}

	for (size_t rn = 0; rn < dbg->zpd_nregs; rn++) {
		smn_reg_t reg;

		reg = fabric_ops->zfo_pcie_port_reg(port,
		    dbg->zpd_regs[rn].zprd_def);
		dbg->zpd_regs[rn].zprd_val[stage] =
		    zen_pcie_port_read(port, reg);
		dbg->zpd_regs[rn].zprd_ts[stage] = gethrtime();
	}

	dbg->zpd_last_stage = stage;

	return (0);
}

void
zen_pcie_populate_dbg(zen_fabric_t *fabric, uint32_t stage, uint8_t iodie_match)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	void *cookie = zen_pcie_dbg_cookie(stage, iodie_match);

	if (fabric_ops->zfo_pcie_dbg_signal != NULL)
		(fabric_ops->zfo_pcie_dbg_signal)();

	(void) zen_fabric_walk_pcie_core(fabric, zen_pcie_populate_core_dbg,
	    cookie);
	(void) zen_fabric_walk_pcie_port(fabric, zen_pcie_populate_port_dbg,
	    cookie);
}

/*
 * Our purpose here is to set up memlist structures for use in tracking. Right
 * now we use the xmemlist feature, though having something that is backed by
 * kmem would make life easier; however, that will wait for the great memlist
 * merge that is likely not to happen anytime soon.
 */
static int
zen_fabric_init_memlists(zen_ioms_t *ioms, void *arg)
{
	zen_ioms_memlists_t *imp = &ioms->zio_memlists;
	void *page = kmem_zalloc(MMU_PAGESIZE, KM_SLEEP);

	mutex_init(&imp->zim_lock, NULL, MUTEX_DRIVER, NULL);
	xmemlist_free_block(&imp->zim_pool, page, MMU_PAGESIZE);
	return (0);
}

/*
 * We want to walk the DF and record information about how PCI buses are routed.
 * We make an assumption here, which is that each DF instance has been
 * programmed the same way by the PSP/SMU (which if not done would lead to
 * some chaos). As such, we end up using the first socket's df and its first
 * IOMS to figure this out.
 */
static void
zen_route_pci_bus(zen_fabric_t *fabric)
{
	zen_iodie_t *iodie = &fabric->zf_socs[0].zs_iodies[0];
	uint_t inst = iodie->zi_ioms[0].zio_iom_inst_id;
	const zen_platform_consts_t *platform_consts =
	    oxide_zen_platform_consts();
	const df_rev_t df_rev = platform_consts->zpc_df_rev;

	for (uint_t i = 0; i < platform_consts->zpc_max_cfgmap; i++) {
		int ret;
		zen_ioms_t *ioms;
		zen_ioms_memlists_t *imp;
		uint32_t base = 0, limit = 0, dest = 0;
		uint32_t val;
		bool re = false, we = false;

		switch (df_rev) {
		case DF_REV_3:
			val = zen_df_read32(iodie, inst, DF_CFGMAP_V2(i));
			base = DF_CFGMAP_V2_GET_BUS_BASE(val);
			re = DF_CFGMAP_V2_GET_RE(val) != 0;
			we = DF_CFGMAP_V2_GET_WE(val) != 0;
			limit = DF_CFGMAP_V2_GET_BUS_LIMIT(val);
			dest = DF_CFGMAP_V3_GET_DEST_ID(val);
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			val = zen_df_read32(iodie, inst, DF_CFGMAP_BASE_V4(i));
			base = DF_CFGMAP_BASE_V4_GET_BASE(val);
			re = DF_CFGMAP_BASE_V4_GET_RE(val) != 0;
			we = DF_CFGMAP_BASE_V4_GET_WE(val) != 0;

			val = zen_df_read32(iodie, inst, DF_CFGMAP_LIMIT_V4(i));
			limit = DF_CFGMAP_LIMIT_V4_GET_LIMIT(val);
			dest = df_rev == DF_REV_4 ?
			    DF_CFGMAP_LIMIT_V4_GET_DEST_ID(val) :
			    DF_CFGMAP_LIMIT_V4D2_GET_DEST_ID(val);
			break;
		default:
			cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		}

		/*
		 * If a configuration map entry doesn't have both read and
		 * write enabled, then we treat that as something that we
		 * should skip. There is no validity bit here, so this is the
		 * closest that we can come to.
		 */
		if (!re || !we)
			continue;

		ioms = zen_fabric_find_ioms(fabric, dest);
		if (ioms == NULL) {
			cmn_err(CE_WARN, "PCI Bus fabric rule %u [0x%x, 0x%x] "
			    "maps to unknown fabric id: 0x%x", i, base, limit,
			    dest);
			continue;
		}

		if (base != ioms->zio_pci_busno) {
			cmn_err(CE_PANIC, "unexpected bus routing rule, rule "
			    "base 0x%x does not match destination base: 0x%x",
			    base, ioms->zio_pci_busno);
		}

		/*
		 * We assign the IOMS's PCI bus as used and all the remaining
		 * as available.
		 */
		imp = &ioms->zio_memlists;
		ret = xmemlist_add_span(&imp->zim_pool, base, 1,
		    &imp->zim_bus_used, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		if (base == limit)
			continue;
		ret = xmemlist_add_span(&imp->zim_pool, base + 1, limit - base,
		    &imp->zim_bus_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}
}

#define	ZEN_SEC_IOMS_GEN_IO_SPACE	0x1000

typedef struct zen_route_io {
	uint32_t	zri_per_ioms;
	uint32_t	zri_next_base;
	uint32_t	zri_cur;
	uint32_t	zri_last_ioms;
	uint32_t	zri_bases[ZEN_MAX_IO_RULES];
	uint32_t	zri_limits[ZEN_MAX_IO_RULES];
	uint32_t	zri_dests[ZEN_MAX_IO_RULES];
} zen_route_io_t;

static int
zen_io_ports_allocate(zen_ioms_t *ioms, void *arg)
{
	const zen_platform_consts_t *platform_consts =
	    oxide_zen_platform_consts();
	zen_route_io_t *zri = arg;
	zen_ioms_memlists_t *imp = &ioms->zio_memlists;
	uint32_t pci_base;
	int ret;

	VERIFY3U(zri->zri_cur, <, platform_consts->zpc_max_iorr);

	/*
	 * The primary FCH (e.g. the IOMS that has the FCH on iodie 0) always
	 * has a base of zero so we can cover the legacy I/O ports. That range
	 * is not available for PCI allocation, however.
	 */
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0 &&
	    (ioms->zio_iodie->zi_flags & ZEN_IODIE_F_PRIMARY) != 0) {
		zri->zri_bases[zri->zri_cur] = 0;
		pci_base = ZEN_IOPORT_COMPAT_SIZE;
	} else if (zri->zri_per_ioms > 2 * ZEN_SEC_IOMS_GEN_IO_SPACE) {
		zri->zri_bases[zri->zri_cur] = zri->zri_next_base;
		pci_base = zri->zri_bases[zri->zri_cur] +
		    ZEN_SEC_IOMS_GEN_IO_SPACE;
		zri->zri_next_base += zri->zri_per_ioms;

		zri->zri_last_ioms = zri->zri_cur;
	} else {
		pci_base = zri->zri_bases[zri->zri_cur] = zri->zri_next_base;
		zri->zri_next_base += zri->zri_per_ioms;

		zri->zri_last_ioms = zri->zri_cur;
	}

	zri->zri_limits[zri->zri_cur] = zri->zri_bases[zri->zri_cur] +
	    zri->zri_per_ioms - 1;
	zri->zri_dests[zri->zri_cur] = ioms->zio_dest_id;

	/*
	 * We must always have some I/O port space available for PCI. The PCI
	 * space must always be higher than any space reserved for generic/FCH
	 * use. While this is ultimately due to the way the hardware works, the
	 * more important reason is that our memlist code below relies on it.
	 */
	ASSERT3U(zri->zri_limits[zri->zri_cur], >, pci_base);
	ASSERT3U(zri->zri_bases[zri->zri_cur], <=, pci_base);

	/*
	 * We purposefully assign all of the I/O ports here and not later on as
	 * we want to make sure that we don't end up recording the fact that
	 * someone has the rest of the ports that aren't available on x86.
	 * While there is some logic in pci_boot.c that attempts to avoid
	 * allocating the legacy/compatibility space port range to PCI
	 * endpoints, it's better to tell that code exactly what's really
	 * available and what isn't. We also need to reserve the compatibility
	 * space for later allocation to FCH devices if the FCH driver or one
	 * of its children requests it.
	 */
	if (pci_base != zri->zri_bases[zri->zri_cur]) {
		ret = xmemlist_add_span(&imp->zim_pool,
		    zri->zri_bases[zri->zri_cur], pci_base,
		    &imp->zim_io_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}
	ret = xmemlist_add_span(&imp->zim_pool, pci_base,
	    zri->zri_limits[zri->zri_cur] - zri->zri_bases[zri->zri_cur] + 1,
	    &imp->zim_io_avail_pci, 0);
	VERIFY3S(ret, ==, MEML_SPANOP_OK);

	zri->zri_cur++;
	return (0);
}

/*
 * The I/O ports effectively use the RE and WE bits as enable bits. Therefore
 * we need to make sure to set the limit register before setting the base
 * register for a given entry.
 */
static int
zen_io_ports_assign(zen_iodie_t *iodie, void *arg)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	zen_route_io_t *zri = arg;

	for (uint32_t i = 0; i < zri->zri_cur; i++) {
		uint32_t base = 0, limit = 0;

		switch (df_rev) {
		case DF_REV_3:
			base = DF_IO_BASE_V2_SET_RE(base, 1);
			base = DF_IO_BASE_V2_SET_WE(base, 1);
			base = DF_IO_BASE_V2_SET_BASE(base,
			    zri->zri_bases[i] >> DF_IO_BASE_SHIFT);

			limit = DF_IO_LIMIT_V3_SET_DEST_ID(limit,
			    zri->zri_dests[i]);
			limit = DF_IO_LIMIT_V2_SET_LIMIT(limit,
			    zri->zri_limits[i] >> DF_IO_LIMIT_SHIFT);

			zen_df_bcast_write32(iodie, DF_IO_LIMIT_V2(i), limit);
			zen_df_bcast_write32(iodie, DF_IO_BASE_V2(i), base);
			break;
		case DF_REV_4:
		case DF_REV_4D2:
			base = DF_IO_BASE_V4_SET_RE(base, 1);
			base = DF_IO_BASE_V4_SET_WE(base, 1);
			base = DF_IO_BASE_V4_SET_BASE(base,
			    zri->zri_bases[i] >> DF_IO_BASE_SHIFT);

			limit = df_rev == DF_REV_4 ?
			    DF_IO_LIMIT_V4_SET_DEST_ID(limit,
			    zri->zri_dests[i]) :
			    DF_IO_LIMIT_V4D2_SET_DEST_ID(limit,
			    zri->zri_dests[i]);
			limit = DF_IO_LIMIT_V4_SET_LIMIT(limit,
			    zri->zri_limits[i] >> DF_IO_LIMIT_SHIFT);

			zen_df_bcast_write32(iodie, DF_IO_LIMIT_V4(i), limit);
			zen_df_bcast_write32(iodie, DF_IO_BASE_V4(i), base);
			break;
		default:
			cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		}
	}

	return (0);
}

/*
 * We need to set up the I/O port mappings to all IOMS instances. Like with
 * other things, for the moment we do the simple thing and make them shared
 * equally across all units. However, there are a few gotchas:
 *
 *  o The first 4 KiB of I/O ports are considered 'legacy'/'compatibility' I/O.
 *    This means that they need to go to the IOMS with the FCH.
 *  o The I/O space base and limit registers all have a 12-bit granularity.
 *  o The DF actually supports 24-bits of I/O space
 *  o x86 cores only support 16-bits of I/O space
 *  o There are only 8 routing rules here for Milan/Genoa and 16 for Turin, so
 *    1/IOMS in a 2P system
 *
 * So with all this in mind, we're going to do the following:
 *
 *  o Each IOMS will be assigned a single route (whether there are 4, 8 or 16)
 *  o We're basically going to assign the 16-bits of ports evenly between all
 *    found IOMS instances.
 *  o Yes, this means the FCH is going to lose some I/O ports relative to
 *    everything else, but that's fine. If we're constrained on I/O ports,
 *    we're in trouble.
 *  o Because we have a limited number of entries, the FCH on node 0 (e.g. the
 *    primary one) has the region starting at 0.
 *  o Whoever is last gets all the extra I/O ports filling up the 1 MiB.
 */
static void
zen_route_io_ports(zen_fabric_t *fabric)
{
	zen_route_io_t zri;
	uint32_t total_size = UINT16_MAX + 1;

	bzero(&zri, sizeof (zri));
	zri.zri_per_ioms = total_size / fabric->zf_total_ioms;
	VERIFY3U(zri.zri_per_ioms, >=, 1 << DF_IO_BASE_SHIFT);
	zri.zri_next_base = zri.zri_per_ioms;

	/*
	 * First walk each IOMS to assign things evenly. We'll come back and
	 * then find the last non-primary one and that'll be the one that gets
	 * a larger limit.
	 */
	(void) zen_fabric_walk_ioms(fabric, zen_io_ports_allocate, &zri);
	zri.zri_limits[zri.zri_last_ioms] = DF_MAX_IO_LIMIT;
	(void) zen_fabric_walk_iodie(fabric, zen_io_ports_assign, &zri);
}

#define	ZEN_SEC_IOMS_GEN_MMIO32_SPACE 0x10000
#define	ZEN_SEC_IOMS_GEN_MMIO64_SPACE 0x10000

typedef struct zen_route_mmio {
	uint32_t	zrm_cur;
	uint32_t	zrm_mmio32_base;
	uint32_t	zrm_mmio32_chunks;
	uint32_t	zrm_fch_base;
	uint32_t	zrm_fch_chunks;
	uint64_t	zrm_mmio64_base;
	uint64_t	zrm_mmio64_chunks;
	uint64_t	zrm_bases[ZEN_MAX_MMIO_RULES];
	uint64_t	zrm_limits[ZEN_MAX_MMIO_RULES];
	uint32_t	zrm_dests[ZEN_MAX_MMIO_RULES];
} zen_route_mmio_t;

/*
 * We allocate two rules per device. The first is a 32-bit rule. The second is
 * then its corresponding 64-bit. 32-bit memory is always treated as
 * non-prefetchable due to the dearth of it. 64-bit memory is only treated as
 * prefetchable because we can't practically do anything else with it due to
 * the limitations of PCI-PCI bridges (64-bit memory has to be prefetch).
 */
static int
zen_mmio_allocate(zen_ioms_t *ioms, void *arg)
{
	const zen_platform_consts_t *platform_consts =
	    oxide_zen_platform_consts();
	zen_route_mmio_t *zrm = arg;
	const uint32_t mmio_gran = 1 << DF_MMIO_SHIFT;
	zen_ioms_memlists_t *imp = &ioms->zio_memlists;
	uint32_t gen_base32 = 0;
	uint64_t gen_base64 = 0;
	int ret;

	VERIFY3U(zrm->zrm_cur, <, platform_consts->zpc_max_mmiorr);

	/*
	 * The primary FCH is treated as a special case so that its 32-bit MMIO
	 * region is as close to the subtractive compat region as possible.
	 * That region must not be made available for PCI allocation, but we do
	 * need to keep track of where it is so the FCH driver or its children
	 * can allocate from it.
	 */
	if ((ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0 &&
	    (ioms->zio_iodie->zi_flags & ZEN_IODIE_F_PRIMARY) != 0) {
		zrm->zrm_bases[zrm->zrm_cur] = zrm->zrm_fch_base;
		zrm->zrm_limits[zrm->zrm_cur] = zrm->zrm_fch_base;
		zrm->zrm_limits[zrm->zrm_cur] += zrm->zrm_fch_chunks *
		    mmio_gran - 1;
		ret = xmemlist_add_span(&imp->zim_pool,
		    zrm->zrm_limits[zrm->zrm_cur] + 1, ZEN_COMPAT_MMIO_SIZE,
		    &imp->zim_mmio_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	} else {
		zrm->zrm_bases[zrm->zrm_cur] = zrm->zrm_mmio32_base;
		zrm->zrm_limits[zrm->zrm_cur] = zrm->zrm_mmio32_base;
		zrm->zrm_limits[zrm->zrm_cur] += zrm->zrm_mmio32_chunks *
		    mmio_gran - 1;
		zrm->zrm_mmio32_base += zrm->zrm_mmio32_chunks *
		    mmio_gran;

		if (zrm->zrm_mmio32_chunks * mmio_gran >
		    2 * ZEN_SEC_IOMS_GEN_MMIO32_SPACE) {
			gen_base32 = zrm->zrm_limits[zrm->zrm_cur] -
			    (ZEN_SEC_IOMS_GEN_MMIO32_SPACE - 1);
		}
	}

	/*
	 * For secondary FCHs (and potentially any other non-PCI destination)
	 * we reserve a small amount of space for general use and give the rest
	 * to PCI. If there's not enough, we give it all to PCI.
	 */
	zrm->zrm_dests[zrm->zrm_cur] = ioms->zio_dest_id;
	if (gen_base32 != 0) {
		ret = xmemlist_add_span(&imp->zim_pool,
		    zrm->zrm_bases[zrm->zrm_cur],
		    zrm->zrm_limits[zrm->zrm_cur] -
		    zrm->zrm_bases[zrm->zrm_cur] -
		    ZEN_SEC_IOMS_GEN_MMIO32_SPACE + 1,
		    &imp->zim_mmio_avail_pci, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		ret = xmemlist_add_span(&imp->zim_pool, gen_base32,
		    ZEN_SEC_IOMS_GEN_MMIO32_SPACE,
		    &imp->zim_mmio_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	} else {
		ret = xmemlist_add_span(&imp->zim_pool,
		    zrm->zrm_bases[zrm->zrm_cur],
		    zrm->zrm_limits[zrm->zrm_cur] -
		    zrm->zrm_bases[zrm->zrm_cur] + 1,
		    &imp->zim_mmio_avail_pci, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}

	zrm->zrm_cur++;

	/*
	 * Now onto the 64-bit register, which is thankfully uniform for all
	 * IOMS entries.
	 */
	zrm->zrm_bases[zrm->zrm_cur] = zrm->zrm_mmio64_base;
	zrm->zrm_limits[zrm->zrm_cur] = zrm->zrm_mmio64_base +
	    zrm->zrm_mmio64_chunks * mmio_gran - 1;
	zrm->zrm_mmio64_base += zrm->zrm_mmio64_chunks * mmio_gran;
	zrm->zrm_dests[zrm->zrm_cur] = ioms->zio_dest_id;

	if (zrm->zrm_mmio64_chunks * mmio_gran >
	    2 * ZEN_SEC_IOMS_GEN_MMIO64_SPACE) {
		gen_base64 = zrm->zrm_limits[zrm->zrm_cur] -
		    (ZEN_SEC_IOMS_GEN_MMIO64_SPACE - 1);

		ret = xmemlist_add_span(&imp->zim_pool,
		    zrm->zrm_bases[zrm->zrm_cur],
		    zrm->zrm_limits[zrm->zrm_cur] -
		    zrm->zrm_bases[zrm->zrm_cur] -
		    ZEN_SEC_IOMS_GEN_MMIO64_SPACE + 1,
		    &imp->zim_pmem_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		ret = xmemlist_add_span(&imp->zim_pool, gen_base64,
		    ZEN_SEC_IOMS_GEN_MMIO64_SPACE,
		    &imp->zim_mmio_avail_gen, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	} else {
		ret = xmemlist_add_span(&imp->zim_pool,
		    zrm->zrm_bases[zrm->zrm_cur],
		    zrm->zrm_limits[zrm->zrm_cur] -
		    zrm->zrm_bases[zrm->zrm_cur] + 1,
		    &imp->zim_pmem_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}

	zrm->zrm_cur++;

	return (0);
}

/*
 * We need to set the three registers that make up an MMIO rule. Importantly we
 * set the control register last as that's what contains the effective enable
 * bits.
 */
static int
zen_mmio_assign(zen_iodie_t *iodie, void *arg)
{
	const df_rev_t df_rev = oxide_zen_platform_consts()->zpc_df_rev;
	zen_route_mmio_t *zrm = arg;

	for (uint32_t i = 0; i < zrm->zrm_cur; i++) {
		uint32_t base, limit;
		uint32_t ctrl = 0;

		base = zrm->zrm_bases[i] >> DF_MMIO_SHIFT;
		limit = zrm->zrm_limits[i] >> DF_MMIO_SHIFT;

		ctrl = DF_MMIO_CTL_SET_RE(ctrl, 1);
		ctrl = DF_MMIO_CTL_SET_WE(ctrl, 1);

		switch (df_rev) {
		case DF_REV_3:
			ctrl = DF_MMIO_CTL_V3_SET_DEST_ID(ctrl,
			    zrm->zrm_dests[i]);

			zen_df_bcast_write32(iodie, DF_MMIO_BASE_V2(i), base);
			zen_df_bcast_write32(iodie, DF_MMIO_LIMIT_V2(i), limit);
			zen_df_bcast_write32(iodie, DF_MMIO_CTL_V2(i), ctrl);
			break;
		case DF_REV_4:
		case DF_REV_4D2: {
			uint32_t ext = 0;

			ctrl = df_rev == DF_REV_4 ?
			    DF_MMIO_CTL_V4_SET_DEST_ID(ctrl,
			    zrm->zrm_dests[i]) :
			    DF_MMIO_CTL_V4D2_SET_DEST_ID(ctrl,
			    zrm->zrm_dests[i]);

			ext = DF_MMIO_EXT_V4_SET_BASE(ext,
			    zrm->zrm_bases[i] >> DF_MMIO_EXT_SHIFT);
			ext = DF_MMIO_EXT_V4_SET_LIMIT(ext,
			    zrm->zrm_limits[i] >> DF_MMIO_EXT_SHIFT);

			zen_df_bcast_write32(iodie, DF_MMIO_BASE_V4(i), base);
			zen_df_bcast_write32(iodie, DF_MMIO_LIMIT_V4(i), limit);
			zen_df_bcast_write32(iodie, DF_MMIO_EXT_V4(i), ext);
			zen_df_bcast_write32(iodie, DF_MMIO_CTL_V4(i), ctrl);
			break;
		}
		default:
			cmn_err(CE_PANIC, "Unsupported DF revision %d", df_rev);
		}
	}

	return (0);
}

/*
 * Routing MMIO is both important and a little complicated mostly due to the
 * how x86 actually has historically split MMIO between the below 4 GiB region
 * and the above 4 GiB region. In addition, there are only 16 routing rules
 * that we can write on some platforms, which means we get a maximum of 2
 * routing rules per IOMS (mostly because we're being lazy).
 *
 * The below 4 GiB space is split due to the compat region
 * (ZEN_PHYSADDR_COMPAT_MMIO). The way we divide up the lower region is
 * simple:
 *
 *   o The region between TOM and 4 GiB is split evenly among all IOMSs.
 *     In a 1P system with the MMIO base set at 0x8000_0000 (as it always is in
 *     the oxide architecture) this results in 512 MiB per IOMS for Milan and
 *     Genoa, and 256MiB per IOMS for Turin; with 2P it's simply half that.
 *
 *   o The part of this region at the top is assigned to the IOMS with the FCH
 *     A small part of this is removed from this routed region to account for
 *     the adjacent FCH compatibility space immediately below 4 GiB; the
 *     remainder is routed to the primary root bridge.
 *
 * 64-bit space is also simple. We find which is higher: TOM2 or the top of the
 * second hole (ZEN_PHYSADDR_IOMMU_HOLE_END). The 256 MiB ECAM region lives
 * there; above it, we just divide all the remaining space between that and
 * the end of accessible physical address space. This is the zen_fabric_t's
 * zf_mmio64_base and zf_mmio64_size members.
 *
 * Our general assumption with this strategy is that 64-bit MMIO is plentiful
 * and that's what we'd rather assign and use. This ties into the last bit
 * which is important: the hardware requires us to allocate in 16-bit chunks.
 * So we actually really treat all of our allocations as units of 64 KiB.
 */
static void
zen_route_mmio(zen_fabric_t *fabric)
{
	uint32_t mmio32_size;
	uint_t nioms32;
	zen_route_mmio_t zrm;
	const uint32_t mmio_gran = DF_MMIO_LIMIT_EXCL;

	VERIFY(IS_P2ALIGNED(fabric->zf_tom, mmio_gran));
	VERIFY3U(ZEN_PHYSADDR_COMPAT_MMIO, >, fabric->zf_tom);
	mmio32_size = ZEN_PHYSADDR_MMIO32_END - fabric->zf_tom;
	nioms32 = fabric->zf_total_ioms;
	VERIFY3U(mmio32_size, >,
	    nioms32 * mmio_gran + ZEN_COMPAT_MMIO_SIZE);

	VERIFY(IS_P2ALIGNED(fabric->zf_mmio64_base, mmio_gran));
	VERIFY3U(fabric->zf_mmio64_size, >, fabric->zf_total_ioms * mmio_gran);

	CTASSERT(IS_P2ALIGNED(ZEN_PHYSADDR_COMPAT_MMIO, DF_MMIO_LIMIT_EXCL));

	bzero(&zrm, sizeof (zrm));
	zrm.zrm_mmio32_base = fabric->zf_tom;
	zrm.zrm_mmio32_chunks = mmio32_size / mmio_gran / nioms32;
	zrm.zrm_fch_base = ZEN_PHYSADDR_MMIO32_END - mmio32_size / nioms32;
	zrm.zrm_fch_chunks = zrm.zrm_mmio32_chunks -
	    ZEN_COMPAT_MMIO_SIZE / mmio_gran;
	zrm.zrm_mmio64_base = fabric->zf_mmio64_base;
	zrm.zrm_mmio64_chunks = fabric->zf_mmio64_size / mmio_gran /
	    fabric->zf_total_ioms;

	(void) zen_fabric_walk_ioms(fabric, zen_mmio_allocate, &zrm);
	(void) zen_fabric_walk_iodie(fabric, zen_mmio_assign, &zrm);
}

/*
 * The IOHC needs our help to know where the top of memory is. This is
 * complicated for a few reasons. Right now we're relying on where TOM and TOM2
 * have been programmed by the PSP to determine that. The biggest gotcha here
 * is the secondary MMIO hole that leads to us needing to actually have a 3rd
 * register in the IOHC for indicating DRAM/MMIO splits.
 */
static int
zen_fabric_init_tom(zen_ioms_t *ioms, void *arg __unused)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	uint64_t tom, tom2, tom3;
	zen_fabric_t *fabric = ioms->zio_iodie->zi_soc->zs_fabric;

	tom = fabric->zf_tom;

	if (fabric->zf_tom2 == 0) {
		tom2 = 0;
		tom3 = 0;
	} else if (fabric->zf_tom2 > ZEN_PHYSADDR_IOMMU_HOLE_END) {
		tom2 = ZEN_PHYSADDR_IOMMU_HOLE;
		tom3 = fabric->zf_tom2 - 1;
	} else {
		tom2 = fabric->zf_tom2;
		tom3 = 0;
	}

	VERIFY3P(fabric_ops->zfo_init_tom, !=, NULL);
	(fabric_ops->zfo_init_tom)(ioms, tom, tom2, tom3);

	return (0);
}

static int
zen_fabric_disable_vga(zen_ioms_t *ioms, void *arg __unused)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();

	VERIFY3P(fabric_ops->zfo_disable_vga, !=, NULL);
	(fabric_ops->zfo_disable_vga)(ioms);

	return (0);
}

/*
 * For some reason the PCIe reference clock does not default to 100 MHz. We
 * need to do this ourselves. If we don't do this, PCIe will not be very happy.
 */
static int
zen_fabric_pcie_refclk(zen_ioms_t *ioms, void *arg __unused)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();

	VERIFY3P(fabric_ops->zfo_pcie_refclk, !=, NULL);
	(fabric_ops->zfo_pcie_refclk)(ioms);

	return (0);
}

/*
 * While the value for the delay comes from the PPR, the value for the limit
 * comes from other AMD sources. At present, these values are consistent across
 * all microarchitectures supported by this arch. If that changes in future,
 * the values should be moved to platform-specific constants or overridden in
 * the uarch-specific vector.
 */
#define	ZEN_PCI_TO_LIMIT	0x262
#define	ZEN_PCI_TO_DELAY	0x6
static int
zen_fabric_pci_crs_to(zen_ioms_t *ioms, void *arg __unused)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();

	VERIFY3P(fabric_ops->zfo_pci_crs_to, !=, NULL);
	(fabric_ops->zfo_pci_crs_to)(ioms, ZEN_PCI_TO_LIMIT, ZEN_PCI_TO_DELAY);

	return (0);
}

/*
 * Determines whether a strap setting applies for the given PCIe core and port
 * number.
 */
bool
zen_fabric_pcie_strap_matches(const zen_pcie_core_t *pc, uint8_t portno,
    const zen_pcie_strap_setting_t *strap)
{
	const zen_ioms_t *ioms = pc->zpc_ioms;
	const zen_iodie_t *iodie = ioms->zio_iodie;
	const oxide_board_t board = oxide_board_data->obd_board;

	if (strap->strap_boardmatch != 0 &&
	    strap->strap_boardmatch != board) {
		return (false);
	}

	if (strap->strap_nodematch != PCIE_NODEMATCH_ANY &&
	    strap->strap_nodematch != (uint32_t)iodie->zi_node_id) {
		return (false);
	}

	if (strap->strap_iomsmatch != PCIE_IOMSMATCH_ANY &&
	    strap->strap_iomsmatch != ioms->zio_num) {
		return (false);
	}

	if (strap->strap_corematch != PCIE_COREMATCH_ANY &&
	    strap->strap_corematch != pc->zpc_coreno) {
		return (false);
	}

	if (portno != PCIE_PORTMATCH_ANY &&
	    strap->strap_portmatch != PCIE_PORTMATCH_ANY &&
	    strap->strap_portmatch != portno) {
		return (false);
	}

	return (true);
}

/*
 * Each IOHC has registers that can further constraion what type of PCI bus
 * numbers the IOHC itself is expecting to reply to. As such, we program each
 * IOHC with its primary bus number and enable this.
 */
static int
zen_fabric_iohc_bus_num(zen_ioms_t *ioms, void *arg __unused)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();

	VERIFY3P(fabric_ops->zfo_iohc_bus_num, !=, NULL);
	(fabric_ops->zfo_iohc_bus_num)(ioms, ioms->zio_pci_busno);

	return (0);
}

/*
 * Different parts of the IOMS need to be programmed such that they can figure
 * out if they have a corresponding FCH present on them. If we're on an IOMS
 * which has an FCH then we need to update various other bis of the IOAGR and
 * related; however, if not then we just need to zero out some of this.
 */
static int
zen_fabric_iohc_fch_link(zen_ioms_t *ioms, void *arg __unused)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();

	VERIFY3P(fabric_ops->zfo_iohc_fch_link, !=, NULL);
	(fabric_ops->zfo_iohc_fch_link)(ioms,
	    (ioms->zio_flags & ZEN_IOMS_F_HAS_FCH) != 0);

	return (0);
}

/*
 * Some microarchitectures don't need all callbacks. We provide null
 * implementations for the ones that are optional and require that there are
 * no uninitialised/null members of the fabric ops vector.
 */
void
zen_null_fabric_iohc_pci_ids(zen_ioms_t *ioms __unused)
{
}

void
zen_null_fabric_sdp_control(zen_ioms_t *nbif __unused)
{
}

void
zen_null_fabric_nbif_bridges(zen_ioms_t *ioms __unused)
{
}

static int
zen_fabric_ioms_iohc_disable_unused_pcie_bridges(zen_ioms_t *ioms,
    void *arg __unused)
{
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();

	if (fops->zfo_iohc_disable_unused_pcie_bridges != NULL)
		fops->zfo_iohc_disable_unused_pcie_bridges(ioms);

	return (0);
}

static int
zen_fabric_send_pptable(zen_iodie_t *iodie, void *pptable)
{
	if (zen_smu_rpc_send_pptable(iodie, (zen_pptable_t *)pptable)) {
		/*
		 * A warning will already have been emitted in the case of a
		 * failure.
		 */
		cmn_err(CE_CONT, "?IO die %u: Sent PP Table to SMU\n",
		    iodie->zi_num);
	}

	return (0);
}

static void
zen_fabric_init_pptable(zen_fabric_t *fabric)
{
	zen_pptable_t *pptable = &fabric->zf_pptable;
	const zen_fabric_ops_t *fops = oxide_zen_fabric_ops();
	ddi_dma_attr_t attr;
	pfn_t pfn;
	size_t len;

	if (fops->zfo_smu_pptable_init == NULL)
		return;

	zen_fabric_dma_attr(&attr);
	pptable->zpp_alloc_len = len = MMU_PAGESIZE;
	pptable->zpp_table = contig_alloc(len, &attr, MMU_PAGESIZE, 1);
	bzero(pptable->zpp_table, len);

	if (!fops->zfo_smu_pptable_init(fabric, pptable->zpp_table, &len)) {
		contig_free(pptable->zpp_table, pptable->zpp_alloc_len);
		pptable->zpp_table = NULL;
		pptable->zpp_alloc_len = 0;
		return;
	}

	pptable->zpp_size = len;

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)pptable->zpp_table);
	pptable->zpp_pa = mmu_ptob((uint64_t)pfn);

	(void) zen_fabric_walk_iodie(fabric, zen_fabric_send_pptable, pptable);
}

static int
zen_fabric_enable_hsmp_int(zen_iodie_t *iodie, void *arg __unused)
{
	if (zen_smu_rpc_enable_hsmp_int(iodie)) {
		cmn_err(CE_CONT, "?IO die %u: Enabled HSMP interrupts\n",
		    iodie->zi_num);
	}

	return (0);
}

static void
zen_fabric_init_smu(zen_fabric_t *fabric)
{
	(void) zen_fabric_walk_iodie(fabric, zen_fabric_enable_hsmp_int, NULL);
}

static int
zen_fabric_init_oxio(zen_iodie_t *iodie, void *arg __unused)
{
	zen_soc_t *soc = iodie->zi_soc;
	size_t idx = iodie->zi_num + soc->zs_num * ZEN_FABRIC_MAX_DIES_PER_SOC;

	VERIFY3U(idx, <, ZEN_FABRIC_MAX_IO_DIES);
	VERIFY3P(oxide_board_data->obd_engines[idx], !=, NULL);
	VERIFY3P(oxide_board_data->obd_nengines[idx], !=, NULL);
	VERIFY3U(*oxide_board_data->obd_nengines[idx], >, 0);
	iodie->zi_engines = oxide_board_data->obd_engines[idx];
	iodie->zi_nengines = *oxide_board_data->obd_nengines[idx];

	return (0);
}

static int
zen_fabric_hotplug_data_port_init(zen_pcie_port_t *port, void *arg)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();

	if ((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) == 0) {
		return (0);
	}

	VERIFY3P(ops->zfo_pcie_hotplug_port_data_init, !=, NULL);
	(ops->zfo_pcie_hotplug_port_data_init)(port, arg);

	return (0);
}

/*
 * Allocate and initialize the hotplug table.
 */
static void
zen_fabric_hotplug_data_init(zen_fabric_t *fabric)
{
	ddi_dma_attr_t attr;
	pfn_t pfn;
	void *hp;

	CTASSERT(sizeof (zen_hotplug_table_t) < MMU_PAGESIZE);

	zen_fabric_dma_attr(&attr);
	hp = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(hp, MMU_PAGESIZE);
	fabric->zf_hotplug_table = hp;
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)hp);
	fabric->zf_hp_pa = mmu_ptob((uint64_t)pfn);
	fabric->zf_hp_alloc_len = MMU_PAGESIZE;

	(void) zen_fabric_walk_pcie_port(fabric,
	    zen_fabric_hotplug_data_port_init, hp);
}

/*
 * Based on the OXIO features and the hotplug type that are present, map these
 * to the corresponding PCIe Slot Capabilities.
 */
static uint32_t
zen_fabric_hotplug_bridge_features(zen_pcie_port_t *port)
{
	uint32_t feats;
	oxio_pcie_slot_cap_t cap;

	feats = PCIE_SLOTCAP_HP_SURPRISE | PCIE_SLOTCAP_HP_CAPABLE;

	/*
	 * Determine the set of features to advertise in the PCIe Slot
	 * Capabilities register.
	 *
	 * By default, Enterprise SSD based devices don't advertise any
	 * additional features and have no bits set in the OXIO traditional
	 * hotplug capabilities structure. The only additional setting that is
	 * required is that there is no command completion support.
	 *
	 * Otherwise we need to map features that are set into the PCIe Slot
	 * Capabilities register. These generally map somewhat directly. The
	 * main exceptions are out-of-band presence and power fault detection.
	 * The Slot presence indicator is always a combination of in-band and
	 * out-of-band presence features. Milan does not support changing the
	 * slot to only rely on out-of-band presence, so it is not checked here.
	 *
	 * Power fault detection is not advertised here. It is only used for
	 * controlling the SMU's behavior of forwarding them. We always enable
	 * power fault detection in the PCIe Port SMN registers in
	 * hotplug port initialization.
	 */
	ASSERT3U(port->zpp_oxio->oe_type, ==, OXIO_ENGINE_T_PCIE);
	ASSERT3U(port->zpp_oxio->oe_hp_type, !=, OXIO_HOTPLUG_T_NONE);

	cap = port->zpp_oxio->oe_hp_trad.ohp_cap;
	if (port->zpp_oxio->oe_hp_type == OXIO_HOTPLUG_T_ENTSSD) {
		ASSERT0(cap);
		feats |= PCIE_SLOTCAP_NO_CMD_COMP_SUPP;
	}

	if ((cap & OXIO_PCIE_CAP_PWREN) != 0) {
		feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
	}

	if ((cap & OXIO_PCIE_CAP_ATTNLED) != 0) {
		feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
	}

	if ((cap & OXIO_PCIE_CAP_PWRLED) != 0) {
		feats |= PCIE_SLOTCAP_PWR_INDICATOR;
	}

	if ((cap & OXIO_PCIE_CAP_EMIL) != 0 ||
	    (cap & OXIO_PCIE_CAP_EMILS) != 0) {
		feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
	}

	if ((cap & OXIO_PCIE_CAP_ATTNSW) != 0) {
		feats |= PCIE_SLOTCAP_ATTN_BUTTON;
	}

	return (feats);
}

/*
 * At this point we have finished telling the SMU/MPIO and its hotplug system to
 * get started.  Now we must try and synchronize PCIe slot and SMU/MPIO state,
 * because they are not the same.  In particular, we have reason to believe that
 * without a write to the slot control register, the SMU/MPIO will not write to
 * the GPIO expander and therefore all the outputs will remain at their hardware
 * device's default.
 *
 * The most important part of this is to ensure that we put the slot's power
 * into a defined state.
 */
static int
zen_hotplug_bridge_post_start(zen_pcie_port_t *port, void *arg)
{
	uint16_t ctl, sts;
	uint32_t cap;
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;

	/*
	 * If there is no hotplug support we don't do anything here today. We
	 * assume that if we're in the simple presence mode then we still need
	 * to come through here because in theory the presence changed
	 * indicators should work.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) == 0) {
		return (0);
	}

	sts = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_STS);
	cap = pci_getl_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP);

	/*
	 * At this point, surprisingly enough, it is expected that all the
	 * notification and fault detection bits be turned on at the SMU as part
	 * of turning on and off the slot. This is a little surprising. Power
	 * was one thing, but at this point it expects to have hotplug
	 * interrupts enabled and all the rest of the features that the hardware
	 * supports (e.g. no MRL sensor changed). Note, we have explicitly left
	 * out turning on the power indicator for present devices.
	 *
	 * Some of the flags need to be conditionally set based on whether or
	 * not they are actually present. We can't turn on the attention button
	 * if there is none. However, others there is no means for software to
	 * discover if they are present or not. So even though we know more and
	 * that say the power fault detection will never work if you've used
	 * Enterprise SSD (or even ExpressModule based on our masks), we set
	 * them anyways, because software will anyways and it helps get the SMU
	 * into a "reasonable" state.
	 */
	ctl = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CTL);
	if ((cap & PCIE_SLOTCAP_ATTN_BUTTON) != 0) {
		ctl |= PCIE_SLOTCTL_ATTN_BTN_EN;
	}

	ctl |= PCIE_SLOTCTL_PWR_FAULT_EN;
	ctl |= PCIE_SLOTCTL_PRESENCE_CHANGE_EN;
	ctl |= PCIE_SLOTCTL_HP_INTR_EN;

	/*
	 * Finally we need to initialize the power state based on slot presence
	 * at this time. Reminder: slot power is enabled when the bit is zero.
	 * It is possible that this may still be creating a race downstream of
	 * this, but in that case, that'll be on the pcieb hotplug logic rather
	 * than us to set up that world here. Only do this if there actually is
	 * a power controller.
	 */
	if ((cap & PCIE_SLOTCAP_POWER_CONTROLLER) != 0) {
		if ((sts & PCIE_SLOTSTS_PRESENCE_DETECTED) != 0) {
			ctl &= ~PCIE_SLOTCTL_PWR_CONTROL;
		} else {
			ctl |= PCIE_SLOTCTL_PWR_CONTROL;
		}
	}
	pci_putw_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CTL, ctl);

	return (0);
}

/*
 * Prepares a hotplug-capable PCIe core by invoking uarch-specific code that
 * sets presence detection to a logical "OR" of in-band and out-of-band presence
 * detect signals.
 */
static bool
zen_fabric_pcie_hotplug_core_init(zen_pcie_core_t *core)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();

	/*
	 * Nothing to do if there's no hotplug.
	 */
	if ((core->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG) == 0) {
		return (true);
	}

	VERIFY3P(ops->zfo_pcie_hotplug_core_init, !=, NULL);
	(ops->zfo_pcie_hotplug_core_init)(core);

	return (true);
}

static void
zen_fabric_init_pcie_hotplug_slot_caps(zen_pcie_port_t *port)
{
	zen_ioms_t *ioms = port->zpp_core->zpc_ioms;
	uint32_t slot_mask;
	uint32_t val;

	/*
	 * Go through and set up the slot capabilities register.  In our case
	 * we've already filtered out the non-hotplug capable bridges, and the
	 * physical slot number has already been programmed by non-hotplug
	 * bridge initialization.  To determine the set of hotplug features that
	 * should be set here we derive that from the actual hoptlug entities.
	 * Because one is required to give the SMU a list of functions to mask,
	 * the unmasked bits tells us what to enable as features here.
	 */
	slot_mask = PCIE_SLOTCAP_ATTN_BUTTON | PCIE_SLOTCAP_POWER_CONTROLLER |
	    PCIE_SLOTCAP_MRL_SENSOR | PCIE_SLOTCAP_ATTN_INDICATOR |
	    PCIE_SLOTCAP_PWR_INDICATOR | PCIE_SLOTCAP_HP_SURPRISE |
	    PCIE_SLOTCAP_HP_CAPABLE | PCIE_SLOTCAP_EMI_LOCK_PRESENT |
	    PCIE_SLOTCAP_NO_CMD_COMP_SUPP;

	val = pci_getl_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP);
	val &= ~slot_mask;
	val |= zen_fabric_hotplug_bridge_features(port);
	pci_putl_func(ioms->zio_pci_busno, port->zpp_device,
	    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP, val);
}

/*
 * Prepares a hotplug-capable bridge by,
 *
 * - Invoking uarch-specific code that:
 *   + sets the slot's actual number in PCIe and in a secondary SMN location.
 *   + sets state machine control bits in the PCIe IP to ensure we don't enter
 *     loopback mode or other degenerate cases
 *   + Enabling support for power faults
 * - Setting port capabilities
 * - Invoking uarch-specific code that unblocks the port from entering link
 *   training.
 */
static bool
zen_fabric_pcie_hotplug_port_init(zen_pcie_port_t *port)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();

	/*
	 * Skip over all non-hotplug slots. If we supported simple presence
	 * mode, then we would also skip this here. Though one has to ask
	 * oneself, why have hotplug if you're going to use the simple presence
	 * mode?
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_HOTPLUG) == 0) {
		return (true);
	}

	/*
	 * Perform initial uarch-specific hotplug port initialization.
	 */
	VERIFY3P(ops->zfo_pcie_hotplug_port_init, !=, NULL);
	(ops->zfo_pcie_hotplug_port_init)(port);

	/*
	 * Set up the PCIe slot capabilities register for the port.
	 */
	zen_fabric_init_pcie_hotplug_slot_caps(port);

	/*
	 * Finally, now that we've set everything else on the slot, we unblock
	 * training on the port.  Note, while this happens before we tell the
	 * SMU/MPIO about our hotplug configuration, PERST is still asserted to
	 * them on boards where that is under GPIO network control, so devices
	 * are unlikely to start suddenly training.
	 */
	VERIFY3P(ops->zfo_pcie_hotplug_port_unblock_training, !=, NULL);
	(ops->zfo_pcie_hotplug_port_unblock_training)(port);

	return (true);
}

/*
 * Initialize and start the hotplug subsystem by performing following steps:
 *
 * - Send a series of uarch-specific commands to configure i2c switches. The
 *   commands correspond to the various bit patterns that we program in the
 *   function payload.
 *
 * - Send our hotplug table, which was initialized from OXIO data.
 *
 * - Configure the cores and bridges to be ready for hotplug events.
 *
 * - Start the hotplug process by initiating a command to firmware.
 *
 * Unlike DXIO initialization, hotplug initialization only happens on the first
 * socket.  This makes some sense because the hotplug table has information
 * about which dies and sockets are used for what, and further the first socket
 * is the only socket connected to the hotplug i2c bus.
 */
static bool
zen_fabric_pcie_hotplug_init(zen_fabric_t *fabric)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_iodie_t *iodie;

	/*
	 * If there are no traditional hotplug devices present on this port,
	 * there is nothing to do.  Return true so that we continue on to the
	 * next port.
	 */
	if ((fabric->zf_flags & ZEN_FABRIC_F_TRAD_HOTPLUG) == 0) {
		return (true);
	}

	zen_fabric_hotplug_data_init(fabric);
	iodie = &fabric->zf_socs[0].zs_iodies[0];

	VERIFY3P(ops->zfo_pcie_hotplug_fw_init, !=, NULL);
	if (!ops->zfo_pcie_hotplug_fw_init(iodie)) {
		return (false);
	}

	/*
	 * Perform platform-specific core and port initialization.
	 */
	(void) zen_fabric_walk_pcie_core(fabric, zen_fabric_pcie_core_op,
	    zen_fabric_pcie_hotplug_core_init);
	(void) zen_fabric_walk_pcie_port(fabric, zen_fabric_pcie_port_op,
	    zen_fabric_pcie_hotplug_port_init);

	VERIFY3P(ops->zfo_pcie_hotplug_set_flags, !=, NULL);
	if (!ops->zfo_pcie_hotplug_set_flags(iodie)) {
		return (false);
	}

	VERIFY3P(ops->zfo_pcie_hotplug_start, !=, NULL);
	if (!ops->zfo_pcie_hotplug_start(iodie)) {
		return (false);
	}

	/*
	 * Now that this is done, we need to go back through and do some final
	 * pieces of slot initialization which are probably necessary to get
	 * MPIO/the SMU into the same place as we are with everything else.
	 */
	(void) zen_fabric_walk_pcie_port(fabric,
	    zen_hotplug_bridge_post_start, NULL);

	return (true);
}

static void
zen_fabric_init_pcie_port(zen_pcie_port_t *port)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_pcie_core_t *pc = port->zpp_core;
	zen_ioms_t *ioms = pc->zpc_ioms;
	uint32_t reg32;
	uint16_t reg16;
	bool hide;

	/*
	 * We need to determine whether or not this bridge should be considered
	 * visible. This is messy. Ideally, we'd just have every bridge be
	 * visible; however, life isn't that simple because convincing the PCIe
	 * engine that it should actually allow for completion timeouts to
	 * function as expected isn't easy. In addition, having bridges that
	 * have no devices present and never can due to the platform definition
	 * can end up wasting precious 32-bit non-prefetchable memory.  The
	 * current masking rules are based on what we have learned works from
	 * trial and error.
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
		bool hotplug, trained;

		hotplug = (pc->zpc_flags & ZEN_PCIE_CORE_F_HAS_HOTPLUG) != 0;
		VERIFY3P(ops->zfo_pcie_port_is_trained, !=, NULL);
		trained = (ops->zfo_pcie_port_is_trained)(port);
		hide = !hotplug && !trained;
	}

	if (hide) {
		port->zpp_flags |= ZEN_PCIE_PORT_F_BRIDGE_HIDDEN;
		VERIFY3P(ops->zfo_pcie_port_hide_bridge, !=, NULL);
		(ops->zfo_pcie_port_hide_bridge)(port);
	} else {
		VERIFY3P(ops->zfo_pcie_port_unhide_bridge, !=, NULL);
		(ops->zfo_pcie_port_unhide_bridge)(port);
	}

	/* Perform uarch-specific bridge initialization. */
	VERIFY3P(ops->zfo_init_bridge, !=, NULL);
	(ops->zfo_init_bridge)(port);

	/*
	 * Software expects to see the PCIe slot implemented bit when a slot
	 * actually exists. For us, this is basically anything that actually is
	 * considered MAPPED.  Set that now on the port.
	 *
	 * We also set the physical slot number into the slot capabilities
	 * register.  Again, this only applies to MAPPED ports.
	 */
	if ((port->zpp_flags & ZEN_PCIE_PORT_F_MAPPED) != 0) {
		reg16 = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, ZEN_BRIDGE_R_PCI_PCIE_CAP);
		reg16 |= PCIE_PCIECAP_SLOT_IMPL;
		pci_putw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, ZEN_BRIDGE_R_PCI_PCIE_CAP, reg16);

		reg32 = pci_getl_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP);
		reg32 &= ~(PCIE_SLOTCAP_PHY_SLOT_NUM_MASK <<
		    PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT);
		reg32 |= port->zpp_slotno << PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT;
		pci_putl_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, ZEN_BRIDGE_R_PCI_SLOT_CAP, reg32);
	}

	/*
	 * Take this opportunity to apply any requested OXIO tuning to the
	 * bridge.
	 *
	 * While in an ideal world we would apply this after mapping, either
	 * after the mapping RPC completes in MPIO initialization or after the
	 * MAPPED stage in the DXIO state machine via the SMU, experimentally it
	 * seems to get clobbered by something else (at least on Milan).  As the
	 * majority of the things we're worried about are gated behind hotplug
	 * and this isn't something we generally want to use, we will survive
	 * setting this a bit later than we'd like.
	 */
	if (port->zpp_oxio != NULL &&
	    port->zpp_oxio->oe_tuning.ot_log_limit != OXIO_SPEED_GEN_MAX) {
		reg16 = pci_getw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, ZEN_BRIDGE_R_PCI_LINK_CTL2);
		reg16 &= ~PCIE_LINKCTL2_TARGET_SPEED_MASK;
		reg16 |= oxio_loglim_to_pcie(port->zpp_oxio);
		pci_putw_func(ioms->zio_pci_busno, port->zpp_device,
		    port->zpp_func, ZEN_BRIDGE_R_PCI_LINK_CTL2, reg16);
	}
}

void
zen_fabric_init(void)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	zen_fabric_t *fabric = &zen_fabric;

	/*
	 * XXX We're missing initialization of some different pieces of the
	 * data fabric here. While some of it like scrubbing should be done as
	 * part of the memory controller driver and broader policy rather than
	 * all here right now.
	 */

	/*
	 * These register debugging facilities are costly in both space and
	 * time, so the source data used to populate them are only non-empty on
	 * DEBUG kernels.
	 */
	(void) zen_fabric_walk_pcie_core(fabric,
	    zen_fabric_init_pcie_core_dbg, NULL);
	(void) zen_fabric_walk_pcie_port(fabric,
	    zen_fabric_init_pcie_port_dbg, NULL);

	zen_fabric_walk_ioms(fabric, zen_fabric_init_memlists, NULL);

	/*
	 * When we come out of reset, the PSP and/or SMU have set up our DRAM
	 * routing rules and the PCI bus routing rules. We need to go through
	 * and save this information as well as set up I/O ports and MMIO. This
	 * process will also save our own allocations of these resources,
	 * allowing us to use them for our own purposes or for PCI.
	 */
	zen_route_pci_bus(fabric);
	zen_route_io_ports(fabric);
	zen_route_mmio(fabric);

	/*
	 * While DRAM training seems to have programmed the initial memory
	 * settings for our boot CPU and the DF, it is not done on the various
	 * IOMS instances. It is up to us to program that across them all.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_init_tom, NULL);

	/*
	 * With MMIO routed and the IOHC's understanding of TOM set up, we also
	 * want to disable the VGA MMIO hole so that the entire low memory
	 * region goes to DRAM for downstream requests just as it does from the
	 * cores.  We don't use VGA and we don't use ASeg, so there's no reason
	 * to hide this RAM from anyone.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_disable_vga, NULL);

	/*
	 * Send the Power and Performance table to the SMU.
	 */
	zen_fabric_init_pptable(fabric);

	/*
	 * Miscellaneous SMU configuration.
	 */
	zen_fabric_init_smu(fabric);

	/*
	 * Walk IOMS and disable unused PCIe bridges on each IOHC.
	 */
	zen_fabric_walk_ioms(fabric,
	    zen_fabric_ioms_iohc_disable_unused_pcie_bridges, NULL);

	/*
	 * Let's set up PCIe. To lead off, let's make sure the system uses the
	 * right subsystem IDs for IOHC devices and the correct clock, and
	 * let's start the process of dealing with the how configuration space
	 * retries should work, though this isn't sufficient for them to work.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_iohc_pci_ids);
	zen_fabric_walk_ioms(fabric, zen_fabric_pcie_refclk, NULL);
	zen_fabric_walk_ioms(fabric, zen_fabric_pci_crs_to, NULL);

	/*
	 * Here we initialize several of the IOHC features and related
	 * vendor-specific messages are all set up correctly.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_iohc_features);

	zen_fabric_walk_ioms(fabric, zen_fabric_iohc_fch_link, NULL);
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_iohc_arbitration);

	zen_fabric_walk_nbif(fabric, zen_fabric_nbif_op,
	    fabric_ops->zfo_nbif_arbitration);

	/*
	 * This sets up a bunch of hysteresis and port controls around the SDP,
	 * DMA actions, and ClkReq. In general, these values are what we're
	 * told to set them to in the PPR.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_sdp_control);

	zen_fabric_walk_nbif(fabric, zen_fabric_nbif_op,
	    fabric_ops->zfo_nbif_syshub_dma);

	/*
	 * IOHC and friends clock gating.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_iohc_clock_gating);

	/*
	 * nBIF clock gating.
	 */
	zen_fabric_walk_nbif(fabric, zen_fabric_nbif_op,
	    fabric_ops->zfo_nbif_clock_gating);

	/*
	 * IOAPIC clock gating.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_ioapic_clock_gating);

	/*
	 * With that done, proceed to initialize the IOAPIC in each IOMS. While
	 * the FCH contains what the OS generally thinks of as the IOAPIC, we
	 * need to go through and deal with interrupt routing and how that
	 * interface with each of the northbridges here.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_ioapic);

	/*
	 * For some reason programming IOHC::NB_BUS_NUM_CNTL is lopped in with
	 * the IOAPIC initialization.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_iohc_bus_num, NULL);

	/*
	 * Go through and configure all of the straps for NBIF devices before
	 * they end up starting up. This includes doing things like:
	 *
	 *  o Enabling and Disabling devices visibility through straps and their
	 *    interrupt lines.
	 *  o Device multi-function enable, related PCI config space straps.
	 *  o Subsystem IDs.
	 *  o GMI round robin.
	 *  o BIFC.
	 */
	zen_fabric_walk_nbif(fabric, zen_fabric_nbif_op,
	    fabric_ops->zfo_nbif_dev_straps);

	/*
	 * To wrap up the nBIF devices, go through and update the bridges here.
	 * We do two passes, one to get the NBIF instances and another to deal
	 * with the special instance that we believe is for the southbridge.
	 */
	zen_fabric_walk_ioms(fabric, zen_fabric_ioms_op,
	    fabric_ops->zfo_nbif_bridges);

	/*
	 * At this time, walk the I/O dies and assign each one the set of
	 * corresponding engine data that they will need to utilize and
	 * transform into AMD firmware appropriate versions. Do this before we
	 * go onto begin training. Translation will be done as part of the
	 * zfo_pcie() op below.
	 */
	zen_fabric_walk_iodie(fabric, zen_fabric_init_oxio, NULL);

	/*
	 * Move on to PCIe training.
	 */
	zen_pcie_populate_dbg(fabric, ZPCS_PRE_INIT, ZEN_IODIE_MATCH_ANY);

	VERIFY3P(fabric_ops->zfo_pcie, !=, NULL);
	(fabric_ops->zfo_pcie)(fabric);

	/*
	 * Now that we have successfully trained devices, it's time to go
	 * through and set up the bridges so that way we can actual handle them
	 * aborting transactions and related.
	 */
	zen_fabric_walk_pcie_core(fabric, zen_fabric_pcie_core_op,
	    fabric_ops->zfo_init_pcie_core);
	zen_fabric_walk_pcie_port(fabric, zen_fabric_pcie_port_op,
	    zen_fabric_init_pcie_port);

	/*
	 * XXX This is a terrible hack. We should really fix pci_boot.c.
	 */
	zen_fabric_hack_bridges(fabric);

	/*
	 * At this point, go talk to the SMU to actually initialize our hotplug
	 * support.
	 */
	zen_pcie_populate_dbg(fabric, ZPCS_PRE_HOTPLUG, ZEN_IODIE_MATCH_ANY);

	if (!zen_fabric_pcie_hotplug_init(fabric)) {
		cmn_err(CE_WARN,
		    "hotplug initialization failed; "
		    "PCIe hotplug may not function properly");
	}

	zen_pcie_populate_dbg(fabric, ZPCS_POST_HOTPLUG, ZEN_IODIE_MATCH_ANY);
}

static int
zen_fabric_nmi_cb(zen_ioms_t *ioms, void *arg)
{
	void (*uarch_nmi_func)(zen_ioms_t *) = arg;
	uarch_nmi_func(ioms);
	return (0);
}

/*
 * Enable NMIs and make sure we only ever receive them on the BSP.
 */
void
zen_fabric_enable_nmi(void)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	VERIFY3P(fabric_ops->zfo_iohc_enable_nmi, !=, NULL);
	(void) zen_walk_ioms(zen_fabric_nmi_cb,
	    fabric_ops->zfo_iohc_enable_nmi);
}

/*
 * Called for NMIs that originated from the IOHC in response to an external
 * assertion of NMI_SYNCFLOOD_L.  We must clear the indicator flag and signal
 * EOI to the fabric in order to receive subsequent such NMIs.
 */
void
zen_fabric_nmi_eoi(void)
{
	const zen_fabric_ops_t *fabric_ops = oxide_zen_fabric_ops();
	VERIFY3P(fabric_ops->zfo_iohc_nmi_eoi, !=, NULL);
	(void) zen_walk_ioms(zen_fabric_nmi_cb, fabric_ops->zfo_iohc_nmi_eoi);
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
			zen_iodie_t *iodie = &soc->zs_iodies[iono];
			int ret = func(iodie, arg);
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
	const zen_fabric_ioms_cb_t *cb = arg;
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
	const zen_fabric_nbif_cb_t *cb = arg;
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
	const zen_fabric_pcie_core_cb_t *cb = arg;
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
	zen_fabric_pcie_port_cb_t *cb = arg;

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
	const zen_fabric_ccd_cb_t *cb = arg;

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
	const zen_fabric_ccx_cb_t *cb = arg;

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
	const zen_fabric_core_cb_t *cb = arg;

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
	zen_fabric_thread_cb_t *cb = arg;

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
	zen_fabric_find_ioms_t *zffi = arg;

	if (zffi->zffi_dest == ioms->zio_dest_id) {
		zffi->zffi_ioms = ioms;
		return (1);
	}

	return (0);
}

static int
zen_fabric_find_ioms_by_bus_cb(zen_ioms_t *ioms, void *arg)
{
	zen_fabric_find_ioms_t *zffi = arg;

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
	zen_fabric_find_pcie_core_t *zffpc = arg;

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
	zen_fabric_find_thread_t *zfft = arg;

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
 * For platform-specific operations that take a single argument pointing to one
 * of the types in the fabric taxonomy and have no meaningful return value,
 * these can functions can be passed as the callback to the walker functions
 * defined above, with the actual operation passed as the argument.
 */
int
zen_fabric_pcie_core_op(zen_pcie_core_t *port, void *arg)
{
	void (*callback)(zen_pcie_core_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(port);

	return (0);
}

int
zen_fabric_iodie_op(zen_iodie_t *iodie, void *arg)
{
	void (*callback)(zen_iodie_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(iodie);

	return (0);
}

int
zen_fabric_pcie_port_op(zen_pcie_port_t *port, void *arg)
{
	void (*callback)(zen_pcie_port_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(port);

	return (0);
}

int
zen_fabric_ioms_op(zen_ioms_t *ioms, void *arg)
{
	void (*callback)(zen_ioms_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(ioms);

	return (0);
}

int
zen_fabric_nbif_op(zen_nbif_t *nbif, void *arg)
{
	void (*callback)(zen_nbif_t *) = arg;

	VERIFY3P(callback, !=, NULL);
	(callback)(nbif);

	return (0);
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

/*
 * PCIe core and port register accessors.
 *
 * Unlike the other zen_*_{read,write}() routines (e.g., zen_ccd_read(),
 * zen_ioms_write()) which all boil down to the usual indirect SMN-based access,
 * we may have to use a separate platform-specific mechanism for the PCIe core
 * and port registers (see zen_mpio_pcie_{core,port}_{read,write}()).
 *
 * Note the explicit SMN_REG_UNIT() checks to exclude SMN_UNIT_IOHCDEV_PCIE,
 * SMN_UNIT_IOMMUL1, etc, which are returned from the *_pcie_{core,port}_reg()
 * convenience functions but should otherwise always be accessed via SMN.
 */

uint32_t
zen_pcie_core_read(zen_pcie_core_t *pc, const smn_reg_t reg)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;

	if (SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_CORE ||
	    ops->zfo_pcie_core_read == NULL) {
		return (zen_smn_read(iodie, reg));
	}
	return (ops->zfo_pcie_core_read(pc, reg));
}

void
zen_pcie_core_write(zen_pcie_core_t *pc, const smn_reg_t reg,
    const uint32_t val)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_iodie_t *iodie = pc->zpc_ioms->zio_iodie;

	if (SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_CORE ||
	    ops->zfo_pcie_core_write == NULL) {
		return (zen_smn_write(iodie, reg, val));
	}
	return (ops->zfo_pcie_core_write(pc, reg, val));
}

uint32_t
zen_pcie_port_read(zen_pcie_port_t *port, const smn_reg_t reg)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;

	if (SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_PORT ||
	    ops->zfo_pcie_port_read == NULL) {
		return (zen_smn_read(iodie, reg));
	}
	return (ops->zfo_pcie_port_read(port, reg));
}

void
zen_pcie_port_write(zen_pcie_port_t *port, const smn_reg_t reg,
    const uint32_t val)
{
	const zen_fabric_ops_t *ops = oxide_zen_fabric_ops();
	zen_iodie_t *iodie = port->zpp_core->zpc_ioms->zio_iodie;

	if (SMN_REG_UNIT(reg) != SMN_UNIT_PCIE_PORT ||
	    ops->zfo_pcie_port_write == NULL) {
		return (zen_smn_write(iodie, reg, val));
	}
	return (ops->zfo_pcie_port_write(port, reg, val));
}
