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

#include <sys/int_types.h>
#include <sys/io/milan/ras.h>
#include <sys/mca_amd.h>
#include <sys/cmn_err.h>
#include <sys/types.h>
#include <sys/x86_archext.h>

/*
 * Initializes the per-CPU RAS registers.  Most of these are specified as MSRs.
 * According to the PPR, we must perform the following steps, in the order
 * listed:
 *
 *  1. Initialize the MCA_CTL_MASK registers.
 *  2. Initialize the MCA_CONFIG registers
 *  3. Initialize the MCA_CTL registers
 *  4. Program the MCG_CTL register
 *  5. Set CR4.MCE
 *
 * The MCA_CTL_MASK registers are usually fine in their power-on/RESET settings;
 * these disable, not enable, reporting.  That is, setting a bit in these MSRs
 * inhibits reporting of events.
 *
 * Since Oxide machines do not have run AMD-style firmware prior to booting the
 * operating system, we follow the firmware rules for configurating MCA_CONFIG,
 * rather than the "operating system" rules in the PPR.
 *
 * Note that the total count of MCA banks is available in the low bits of the
 * MCG_CAP MSR.  For each bank, the legacy MCA control register is the first MSR
 * in the bank; each bank consists of 16 MSRs starting at 0xC000_2000.
 */

static uint64_t
milan_read_bank_msr(uint_t bank, enum milan_ras_mcax_bank_reg reg)
{
	uint_t bank_offset = MILAN_RAS_MSR_BANK_NREGS * bank;
	uint_t msr = MILAN_RAS_BANK_MSR_BASE + bank_offset + reg;
	return (rdmsr(msr));
}

static void
milan_write_bank_msr(uint_t bank, enum milan_ras_mcax_bank_reg reg,
    uint64_t value)
{
	uint_t bank_offset = MILAN_RAS_MSR_BANK_NREGS * bank;
	uint_t msr = MILAN_RAS_BANK_MSR_BASE + bank_offset + reg;
	wrmsr(msr, value);
}

static inline uint_t
milan_ipid_hardware_id(uint64_t ipid)
{
	return ((ipid >> 32) & 0x0FFF);
}

static inline uint_t
milan_ipid_mca_type(uint64_t ipid)
{
	return ((ipid >> 48) & 0xFFFF);
}

/*
 * Decodes the bank type from the hardware ID and MCA "type"
 * fields in the IP ID register.
 */
static enum milan_ras_bank_type
milan_identify_bank(int bank)
{
	uint64_t ipid = milan_read_bank_msr(bank, MILAN_RAS_MSR_IPID);
	uint_t hardware_id = milan_ipid_hardware_id(ipid);
	uint_t mca_type = milan_ipid_mca_type(ipid);

	switch (hardware_id) {
	case 0x01:
		switch (mca_type) {
		case 0x01: return (MILAN_RBT_SMU);
		case 0x02: return (MILAN_RBT_MP5);
		default: return (MILAN_RBT_UNK);
		}
	case 0x05:
		switch (mca_type) {
		case 0x00: return (MILAN_RBT_PB);
		default: return (MILAN_RBT_UNK);
		}
	case 0x18:
		switch (mca_type) {
		case 0x00: return (MILAN_RBT_NBIO);
		default: return (MILAN_RBT_UNK);
		}
	case 0x2e:
		switch (mca_type) {
		case 0x01: return (MILAN_RBT_PIE);
		case 0x02: return (MILAN_RBT_CS);
		default: return (MILAN_RBT_UNK);
		}
	case 0x46:
		switch (mca_type) {
		case 0x00: return (MILAN_RBT_PCIE);
		default: return (MILAN_RBT_UNK);
		}
	case 0x96:
		switch (mca_type) {
		case 0x00: return (MILAN_RBT_UMC);
		default: return (MILAN_RBT_UNK);
		}
	case 0xb0:
		switch (mca_type) {
		case 0x10: return (MILAN_RBT_LS);
		case 0x01: return (MILAN_RBT_IF);
		case 0x02: return (MILAN_RBT_L2);
		case 0x03: return (MILAN_RBT_DE);
		case 0x05: return (MILAN_RBT_EX);
		case 0x06: return (MILAN_RBT_FP);
		default: return (MILAN_RBT_UNK);
		}
	case 0xff:
		switch (mca_type) {
		case 0x01: return (MILAN_RBT_PSP);
		default: return (MILAN_RBT_UNK);
		}
	}
	return (MILAN_RBT_UNK);
}

static void
milan_bank_mask_set(int bank, uint64_t bit)
{
	const uint64_t mask = rdmsr(MILAN_RAS_MCA_CTL_MASK_MSR_BASE + bank);
	wrmsr(MILAN_RAS_MCA_CTL_MASK_MSR_BASE + bank, mask | bit);
}

void
milan_ras_init(void)
{
	/*
	 * The total count of banks is available in the low bits of the MCG_CAP
	 * MSR.  We perform
	 */
	int nbanks = rdmsr(IA32_MSR_MCG_CAP) & MCG_CAP_COUNT_MASK;
	if (nbanks > MILAN_RAS_MAX_BANKS)
		panic("more RAS banks than we can handle (%d banks)", nbanks);
	for (int bank = 0; bank < nbanks; bank++) {
		/*
		 * Set up the bank configuration register.
		 */
		uint64_t cfg = milan_read_bank_msr(bank, MILAN_RAS_MSR_CONFIG);
		if ((cfg & MILAN_RAS_CONFIG_DEFERRED_LOGGING_SUPTD) != 0)
			cfg |= MILAN_RAS_CONFIG_LOG_DEFERRED_IN_MCA_STAT;
		if ((cfg & MILAN_RAS_CONFIG_TRANSPARENT_LOGGING_SUPTD) != 0)
			cfg &= ~MILAN_RAS_CONFIG_TRANSPARENT_LOGGING_ENABLE;

		/*
		 * Noted: the '>' is intention.  We set the MCAX enable bit
		 * in the config register iff banks <= the max constant.
		 * See 55898 sec 3.1.5.3.
		 */
		if (bank > MILAN_RAS_MAX_MCAX_BANKS) {
			cfg &= ~MILAN_RAS_CONFIG_MCAX_ENABLE;
			milan_write_bank_msr(bank, MILAN_RAS_MSR_CONFIG, cfg);
			continue;
		}
		cfg |= MILAN_RAS_CONFIG_MCAX_ENABLE;
		milan_write_bank_msr(bank, MILAN_RAS_MSR_CONFIG, cfg);

		/*
		 * Access to the IP ID register is dependent on McaX being set
		 * in the bank config register, hence we skip it for banks
		 * beyond the McaX maximum.
		 */
		switch (milan_identify_bank(bank)) {
		case MILAN_RBT_CS:
			milan_bank_mask_set(bank,
			    MILAN_RAS_MASK_CS_FTI_ADDR_VIOL);
			break;
		case MILAN_RBT_L3:
			milan_bank_mask_set(bank, MILAN_RAS_MASK_L3_HWA);
			break;
		default:
			break;
		}
	}
}
