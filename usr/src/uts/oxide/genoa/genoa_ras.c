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
 * Copyright 2023 Oxide Computer Company
 */

/*
 * This implements RAS support on Milan.
 */

#include <sys/bitext.h>
#include <sys/cmn_err.h>
#include <sys/int_types.h>
#include <sys/io/milan/ras.h>
#include <sys/mca_amd.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/x86_archext.h>

/*
 * Initializes the per-CPU RAS registers.  Most of these are specified as MSRs.
 * According to the PPR, we must perform the following steps, in the order
 * listed:
 *
 *  1. Initialize the MCA_CTL_MASK registers.
 *  2. Initialize the MCA_CFG registers
 *  3. Initialize the MCA_CTL registers
 *  4. Program the MCG_CTL register
 *  5. Set CR4.MCE
 *
 * We perform steps 1-3 here.
 *
 * The MCA_CTL_MASK registers are usually fine in their power-on/RESET settings;
 * these disable, not enable, reporting.  That is, setting a bit in these MSRs
 * inhibits reporting of events.
 *
 * Since Oxide machines do not run AMD-style firmware prior to booting the
 * operating system, we follow the firmware rules for configurating MCA_CFG,
 * rather than the "operating system" rules in the PPR.
 *
 * Note that the total count of MCA banks is available in the low bits of the
 * MCG_CAP MSR.  For each bank, the legacy MCA control register is the first MSR
 * in the bank; each bank consists of 16 MSRs starting at 0xC000_2000.
 */

/*
 * Reads a bank register.  Note that we do not test the bank number for
 * validity, as this is static and only called in contexts where we know the
 * bank is valid.
 */
static uint64_t
read_bank_msr(uint_t bank, enum milan_ras_mcax_bank_reg reg)
{
	uint_t bank_offset = MILAN_RAS_MSR_BANK_NREGS * bank;
	uint_t msr = MILAN_RAS_BANK_MSR_BASE + bank_offset + reg;
	return (rdmsr(msr));
}

/*
 * Writes a value to a bank register.  Note that we do not test the bank number
 * for validity, as this is static and only called in contexts where we know the
 * bank is valid.
 */
static void
write_bank_msr(uint_t bank, enum milan_ras_mcax_bank_reg reg, uint64_t value)
{
	uint_t bank_offset = MILAN_RAS_MSR_BANK_NREGS * bank;
	uint_t msr = MILAN_RAS_BANK_MSR_BASE + bank_offset + reg;
	wrmsr(msr, value);
}

static uint64_t
ipid_hardware_id(uint64_t ipid)
{
	return (bitx64(ipid, 43, 32));
}

static uint64_t
ipid_mca_type(uint64_t ipid)
{
	return (bitx64(ipid, 63, 48));
}

/*
 * Decodes the bank type from the hardware ID and MCA "type"
 * fields in the IP ID register.
 */
static enum milan_ras_bank_type
identify_bank(uint_t bank)
{
	const struct {
		uint64_t hardware_id;
		uint64_t mca_type;
		enum milan_ras_bank_type bank_type;
	} type_map[] = {
		/*
		 * These constants are taken from the PPR and seem mostly
		 * arbitrary.  surely there is some underlying meaning, but it
		 * is not immediately evident from the table in the
		 * documentation.  Note that the ordering here is from the table
		 * in the PPR from ease of cross-reference.
		 */
		{ 0xb0, 0x10, MILAN_RBT_LS },
		{ 0xb0, 0x01, MILAN_RBT_IF },
		{ 0xb0, 0x02, MILAN_RBT_L2 },
		{ 0xb0, 0x07, MILAN_RBT_L3 },
		{ 0x01, 0x02, MILAN_RBT_MP5 },
		{ 0x05, 0x00, MILAN_RBT_PB },
		{ 0x96, 0x00, MILAN_RBT_UMC },
		{ 0x18, 0x00, MILAN_RBT_NBIO },
		{ 0x46, 0x00, MILAN_RBT_PCIE },
		{ 0x01, 0x01, MILAN_RBT_SMU },
		{ 0xff, 0x01, MILAN_RBT_PSP },
		{ 0x2e, 0x01, MILAN_RBT_PIE },
		{ 0x2e, 0x02, MILAN_RBT_CS },
		{ 0xb0, 0x05, MILAN_RBT_EX },
		{ 0xb0, 0x06, MILAN_RBT_FP },
		{ 0xb0, 0x03, MILAN_RBT_DE },
	};

	uint64_t ipid = read_bank_msr(bank, MILAN_RAS_MSR_IPID);
	uint64_t hardware_id = ipid_hardware_id(ipid);
	uint64_t mca_type = ipid_mca_type(ipid);

	for (uint_t i = 0; i < ARRAY_SIZE(type_map); i++) {
		if (type_map[i].hardware_id == hardware_id &&
		    type_map[i].mca_type == mca_type)
			return (type_map[i].bank_type);
	}

	return (MILAN_RBT_UNK);
}

static void
bank_mask_set(int bank, uint64_t bit)
{
	const uint64_t mask = rdmsr(MILAN_RAS_MCA_CTL_MASK_MSR_BASE + bank);
	const uint64_t value = bitset64(mask, bit, bit, 1);
	wrmsr(MILAN_RAS_MCA_CTL_MASK_MSR_BASE + bank, value);
}

static boolean_t
is_setb(uint64_t val, uint_t bit)
{
	return (bitx64(val, bit, bit) != 0);
}

static uint64_t
setb(uint64_t val, uint_t bit)
{
	return (bitset64(val, bit, bit, 1));
}

static uint64_t
delb(uint64_t val, uint_t bit)
{
	return (bitdel64(val, bit, bit));
}

/*
 * Initialize the RAS registers, as per the PPR.  Note that this is called once
 * on each CPU in the system.
 */
void
milan_ras_init(void)
{
	/*
	 * The total count of banks is available in the low bits of the MCG_CAP
	 * MSR.  It is capped at a maximum, as per the PPR.
	 */
	uint_t nbanks = MCG_CAP_COUNT(rdmsr(IA32_MSR_MCG_CAP));
	if (nbanks > MILAN_RAS_MAX_BANKS)
		panic("more RAS banks than we can handle (%d banks)", nbanks);
	for (uint_t bank = 0; bank < nbanks; bank++) {
		/*
		 * Set up the bank configuration register.
		 */
		uint64_t cfg = read_bank_msr(bank, MILAN_RAS_MSR_CFG);

		/*
		 * Not all banks are guaranteed to exist; if a bank is somewhere
		 * in the middle of the array and doesn't really exist on this
		 * processor, all the bits indicating what it supports will be
		 * clear.  This is architecturally allowed, and we need to check
		 * for it and avoid enabling non-MCAX-capable banks.
		 */
		if (!is_setb(cfg, MILAN_RAS_CFG_MCAX))
			continue;

		if (is_setb(cfg, MILAN_RAS_CFG_DEFERRED_LOGGING_SUPTD))
			cfg = setb(cfg, MILAN_RAS_CFG_LOG_DEFERRED_IN_MCA_STAT);
		if (is_setb(cfg, MILAN_RAS_CFG_TRANSPARENT_LOGGING_SUPTD))
			cfg = delb(cfg, MILAN_RAS_CFG_TRANSPARENT_LOGGING_EN);

		/*
		 * Noted: the '>' is intention.  We set the MCAX enable bit
		 * in the config register iff banks <= the max constant.
		 * See 55898 sec 3.1.5.3.
		 */
		if (bank > MILAN_RAS_MAX_MCAX_BANKS) {
			cfg = delb(cfg, MILAN_RAS_CFG_MCAX_EN);
			write_bank_msr(bank, MILAN_RAS_MSR_CFG, cfg);
			continue;
		}
		cfg = setb(cfg, MILAN_RAS_CFG_MCAX_EN);
		write_bank_msr(bank, MILAN_RAS_MSR_CFG, cfg);

		/*
		 * Access to the IP ID register is dependent on McaX being set
		 * in the bank config register, hence we skip it for banks
		 * beyond the McaX maximum.
		 */
		switch (identify_bank(bank)) {
		case MILAN_RBT_LS:
			bank_mask_set(bank, MILAN_RAS_MASK_LS_SYS_RD_DATA_LD);
			bank_mask_set(bank, MILAN_RAS_MASK_LS_SYS_RD_DATA_SCB);
			bank_mask_set(bank, MILAN_RAS_MASK_LS_SYS_RD_DATA_WCB);
			break;
		case MILAN_RBT_IF:
			/* These appear to be set by HW/FW; take no chances. */
			bank_mask_set(bank, MILAN_RAS_MASK_IF_L2_BTB_MULTI);
			bank_mask_set(bank, MILAN_RAS_MASK_IF_L2_TLB_MULTI);
			break;
		case MILAN_RBT_L2:
			bank_mask_set(bank, MILAN_RAS_MASK_L2_HWA);
			break;
		case MILAN_RBT_FP:
			bank_mask_set(bank, MILAN_RAS_MASK_FP_HWA);
			break;
		case MILAN_RBT_CS:
			bank_mask_set(bank, MILAN_RAS_MASK_CS_FTI_ADDR_VIOL);
			break;
		case MILAN_RBT_L3:
			bank_mask_set(bank, MILAN_RAS_MASK_L3_HWA);
			break;
		case MILAN_RBT_NBIO:
			bank_mask_set(bank, MILAN_RAS_MASK_NBIO_PCIE_SB);
			bank_mask_set(bank, MILAN_RAS_MASK_NBIO_PCIE_ERR_EVT);
			break;
		default:
			break;
		}
	}
}
