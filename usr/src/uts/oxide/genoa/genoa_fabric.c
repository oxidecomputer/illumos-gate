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
	case SMN_UNIT_IOHC:
		reg = genoa_iohc_smn_reg(ioms->zio_num, def, reginst);
		break;
	case SMN_UNIT_IOAGR:
		reg = genoa_ioagr_smn_reg(ioms->zio_num, def, reginst);
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
			reg = genoa_iommul1_ioagr_smn_reg(ioms->zio_num, def,
			    0);
			break;
		default:
			cmn_err(CE_PANIC, "invalid IOMMUL1 subunit %d", su);
			break;
		}
		break;
	}
	case SMN_UNIT_IOMMUL2:
		reg = genoa_iommul2_smn_reg(ioms->zio_num, def, reginst);
		break;
	default:
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
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

/*
 * Do everything else required to finish configuring the nBIF and get the PCIe
 * engine up and running.
 */
void
genoa_fabric_pcie(zen_fabric_t *fabric)
{
	zen_pcie_populate_dbg(fabric, GPCS_PRE_INIT, ZEN_IODIE_MATCH_ANY);
}
