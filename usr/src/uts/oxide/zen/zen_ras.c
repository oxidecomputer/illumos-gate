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

/*
 * Provide generic code for RAS enablement.  This provides a level of
 * indirection that allows us to manipulate RAS from common code without a
 * direct dependency on any specific microarchitecture.
 */

#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/ras_impl.h>
#include <sys/io/zen/uarch.h>

#include <sys/bitext.h>
#include <sys/stdint.h>
#include <sys/debug.h>
#include <sys/mca_x86.h>

/*
 * Reads a bank register.  Note that we do not test the bank number for
 * validity, as this is static and only called in contexts where we know the
 * bank is valid.
 */
static uint64_t
read_bank_msr(uint_t bank, zen_ras_mcax_bank_reg_t reg)
{
	uint_t bank_offset = ZEN_RAS_MSR_BANK_NREGS * bank;
	uint_t msr = ZEN_RAS_BANK_MSR_BASE + bank_offset + reg;
	return (rdmsr(msr));
}

/*
 * Writes a value to a bank register.  Note that we do not test the bank number
 * for validity, as this is static and only called in contexts where we know the
 * bank is valid.
 */
static void
write_bank_msr(uint_t bank, zen_ras_mcax_bank_reg_t reg, uint64_t value)
{
	uint_t bank_offset = ZEN_RAS_MSR_BANK_NREGS * bank;
	uint_t msr = ZEN_RAS_BANK_MSR_BASE + bank_offset + reg;
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
clrb(uint64_t val, uint_t bit)
{
	return (bitset64(val, bit, bit, 0));
}

static zen_ras_bank_type_t
zen_ras_identify_bank(const zen_ras_bank_type_map_t *type_map,
    const size_t type_map_len, uint_t bank)
{
	/*
	 * Access to the IP ID register is dependent on McaX being set in the
	 * bank config register, hence we skip it for banks beyond the McaX
	 * maximum.
	 */
	const uint64_t ipid = read_bank_msr(bank, ZEN_RAS_MSR_IPID);
	uint64_t hardware_id = ipid_hardware_id(ipid);
	uint64_t mca_type = ipid_mca_type(ipid);

	for (uint_t i = 0; i < type_map_len; i++) {
		if (type_map[i].zrbtm_hardware_id == hardware_id &&
		    type_map[i].zrbtm_mca_type == mca_type)
			return (type_map[i].zrbtm_bank_type);
	}

	return (ZEN_RBT_UNK);
}

/*
 * Identifies and initializes the RAS registers on the given bank.
 */
static void
zen_ras_init_bank(const zen_ras_init_data_t *ras, uint_t bank)
{
	/*
	 * Set up the bank configuration register.  We must:
	 *
	 * 1. Set MCA_CFG[McaxEn] if bank <= ZEN_RAS_MAX_MCAX_BANKS; else clear
	 * 2. Set MCA_CFG[LogDefferedInMcaStat]
	 * 3. Clear MCA_CFG[TransparentErrorLoggingEnable]
	 *
	 * The ZEN_RAS_MAX_MCAX_BANKS constant is currently sized appropriately
	 * for Milan, Genoa, and Turin.
	 */
	uint64_t cfg = read_bank_msr(bank, ZEN_RAS_MSR_CFG);

	/*
	 * Not all banks are guaranteed to exist; if a bank is somewhere in the
	 * middle of the array and doesn't really exist on this processor, all
	 * the bits indicating what it supports will be clear.  This is
	 * architecturally allowed, and we need to check for it and avoid
	 * enabling non-MCAX-capable banks.
	 */
	if (!is_setb(cfg, ZEN_RAS_CFG_MCAX))
		return;

	if (is_setb(cfg, ZEN_RAS_CFG_DEFERRED_LOGGING_SUPTD))
		cfg = setb(cfg, ZEN_RAS_CFG_LOG_DEFERRED_IN_MCA_STAT);
	if (is_setb(cfg, ZEN_RAS_CFG_TRANSPARENT_LOGGING_SUPTD))
		cfg = clrb(cfg, ZEN_RAS_CFG_TRANSPARENT_LOGGING_EN);

	/*
	 * Note: the '>' is intentional.  We set the MCAX enable bit
	 * in the config register iff banks <= the max constant.
	 * See 55898 sec 3.1.5.3.
	 */
	if (bank > ZEN_RAS_MAX_MCAX_BANKS) {
		cfg = clrb(cfg, ZEN_RAS_CFG_MCAX_EN);
		write_bank_msr(bank, ZEN_RAS_MSR_CFG, cfg);
		return;
	}
	cfg = setb(cfg, ZEN_RAS_CFG_MCAX_EN);
	write_bank_msr(bank, ZEN_RAS_MSR_CFG, cfg);

	/*
	 * The PPRs for the various uarchs do mention other bits that are BIOS
	 * initialized, but most we don't concern ourselves with.  For instance,
	 * the "LOCKED" bits in MISC registers that would indicate to the OS
	 * that if threshold interrupt types were set to SMI, are explicitly
	 * avoided in the Oxide architecture (where we not only don't use SMM,
	 * we go to great lengths to try and disable it).
	 *
	 * Things like where MCA_CONFIG_UMC[McaFruTextInMca] on e.g. Genoa are
	 * more questionable, and may be something we want to support at some
	 * point, but we do not currently.  Similarly with IntEn and
	 * McaFruTextInMca in various MCA_CONFIG_* registers on Turin (note
	 * that the BIOS values are the same as the reset values on those
	 * there).
	 */
	zen_ras_bank_type_t bank_type = zen_ras_identify_bank(
	    ras->zrid_bank_type_map, ras->zrid_bank_type_nmap, bank);
	for (size_t i = 0; i < ras->zrid_bank_mask_nmap; i++) {
		const zen_ras_bank_mask_bits_t *mask_bits =
		    &ras->zrid_bank_mask_map[i];

		if (mask_bits->zrbmb_bank_type != bank_type)
			continue;

		/*
		 * If the bank type exists in the map, we read the bank's mask
		 * control MSR, set whatever bits are defined in the map, and
		 * write it back.
		 *
		 * Note that so far, on the microarchitectures that we support,
		 * BIOS init only sets bits.
		 */
		const uint64_t msr = ZEN_RAS_MCA_CTL_MASK_MSR_BASE + bank;
		uint64_t mask = rdmsr(msr);
		for (uint_t j = 0; j < mask_bits->zrbmb_nbits; j++)
			mask = setb(mask, mask_bits->zrbmb_bits[j]);
		wrmsr(msr, mask);
	}
}
/*
 * Initialize the RAS registers on each MCA(X) bank.  Note that this is called
 * once on each CPU in the system.
 */
void
zen_ras_init(void)
{
	const zen_platform_consts_t *pc = oxide_zen_platform_consts();
	const zen_ras_init_data_t *ras = pc->zpc_ras_init_data;

	if (ras == NULL) {
		cmn_err(CE_WARN, "Skipping RAS initialization: no init data.");
		return;
	}

	/*
	 * The total count of banks is available in the low bits of the MCG_CAP
	 * MSR.  It is capped at a maximum, as per the PPR.
	 */
	const uint_t nbanks = MCG_CAP_COUNT(rdmsr(IA32_MSR_MCG_CAP));
	if (nbanks > ZEN_RAS_MAX_BANKS)
		panic("more RAS banks than we can handle (%u banks)", nbanks);
	for (uint_t bank = 0; bank < nbanks; bank++) {
		zen_ras_init_bank(ras, bank);
	}
}
