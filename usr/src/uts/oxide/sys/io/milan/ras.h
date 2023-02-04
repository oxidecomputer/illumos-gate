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

 */
#ifndef _SYS_IO_MILAN_RAS_H
#define _SYS_IO_MILAN_RAS_H

#include <sys/int_types.h>

/*
 * MCAX registers are organized into 64 banks of 16 MSRs, starting at MSR
 * 0xC000_2000.  Legacy MCA registers as well as the extended MCAX register are
 * co-resident in each bank.  The bank offsets are:
 *
 *   0. CTL      (Legacy)
 *   1. STATUS   (Legacy)
 *   2. ADDR     (Legacy)
 *   3. MISC0    (Legacy)
 *   4. CONFIG   (Extended)
 *   5. IPID     (Extended)
 *   6. SYND     (Extended)
 *   7. Reserved (Extended)
 *   8. DESTAT   (Extended)
 *   9. DEADDR   (Extended)
 *  10. MISC1    (Extended)
 *  11. MISC2    (Extended)
 *  12. MISC3    (Extended)
 *  13. MISC4    (Extended)
 *  14. Unlisted (Extended)  (UNDOCUMENTED)
 *  15. Unlisted (Extended)  (UNDOCUMENTED)
 *
 * Note that registers 14 and 15 (0xE, 0xF) are not documented in the PPR.  We
 * presume they are reserved and do not touch them.
 *
 * The banks are also typed.  Decoding which type of bank is done by matching on
 * the pair of hardware ID and MCA type fields from the IPID register.
 */

/*
 * MCAX bank types.  Which type each bank corresponds to depends on decoding
 * the IPID register in that bank.
 */
enum milan_ras_bank_type {
	MILAN_RBT_LS,	/* Load-Store Unit */
	MILAN_RBT_IF,	/* Instruction Fetch Unit */
	MILAN_RBT_L2,	/* L2 Cache Unit */
	MILAN_RBT_L3,	/* L3 Cache Unit */
	MILAN_RBT_MP5,	/* Microprocessor5 Management Controller */
	MILAN_RBT_PB,	/* Parameter Block */
	MILAN_RBT_UMC,	/* Unified Memory Controller */
	MILAN_RBT_NBIO,	/* Northbridge IO Unit */
	MILAN_RBT_PCIE,	/* PCIe Root Port */
	MILAN_RBT_SMU,	/* System Management Controller Unit */
	MILAN_RBT_PSP,	/* Platform Security Processor */
	MILAN_RBT_PIE,	/* Power Management, Interrupts, Etc (seriously?!) */
	MILAN_RBT_CS,	/* Coherent Slave */
	MILAN_RBT_EX,	/* Execution Unit */
	MILAN_RBT_FP,	/* Float-point Unit */
	MILAN_RBT_DE,	/* Decode Unit */
	MILAN_RBT_UNK,	/* Unknown */
};

static const int MILAN_RAS_MAX_BANKS = 64;
static const int MILAN_RAS_MAX_MCAX_BANKS = 32;
static const uint32_t MILAN_RAS_BANK_MSR_BASE = 0xc0002000U;

/*
 * Each MCAX register bank consists of 16 MSRs, layed out
 * as follows.  Note that three are reserved;
 */
enum milan_ras_mcax_bank_reg {
	MILAN_RAS_MSR_CTL,
	MILAN_RAS_MSR_STATUS,
	MILAN_RAS_MSR_ADDR,
	MILAN_RAS_MSR_MISC0,
	MILAN_RAS_MSR_CONFIG,
	MILAN_RAS_MSR_IPID,
	MILAN_RAS_MSR_SYND,
	MILAN_RAS_MSR_RESERVED7,
	MILAN_RAS_MSR_DESTAT,
	MILAN_RAS_MSR_DEADDR,
	MILAN_RAS_MSR_MISC1,
	MILAN_RAS_MSR_MISC2,
	MILAN_RAS_MSR_MISC3,
	MILAN_RAS_MSR_MISC4,
	MILAN_RAS_MSR_RESERVEDE,
	MILAN_RAS_MSR_RESERVEDF,
	MILAN_RAS_MSR_BANK_NREGS,
};

/*
 * Bits in Milan RAS bank configuration registers.
 */
static const uint64_t MILAN_RAS_CONFIG_MCAX_ENABLE = 1ULL << 32;
static const uint64_t MILAN_RAS_CONFIG_TRANSPARENT_LOGGING_SUPTD = 1ULL << 1;
static const uint64_t MILAN_RAS_CONFIG_DEFERRED_LOGGING_SUPTD = 1ULL << 2;
static const uint64_t MILAN_RAS_CONFIG_TRANSPARENT_LOGGING_ENABLE = 1ULL << 33;
static const uint64_t MILAN_RAS_CONFIG_LOG_DEFERRED_IN_MCA_STAT = 1ULL << 34;

/*
 * The MCA control mask MSRs are in a block by themselves, starting
 * at MILAN_RAS_MCA_CTL_MASK_MSR_BASE and indexed by bank number.
 * Thus, bank 0 is at MILAN_RAS_MCA_CTL_MASK_MSR_BASE + 0, bank 1
 * at MILAN_RAS_MCA_CTL_MASK_MSR_BASE + 1, and so on.
 */
static const uint32_t MILAN_RAS_MCA_CTL_MASK_MSR_BASE = 0xc0010400U;

/*
 * CS mask bits.
 */
static const uint64_t MILAN_RAS_MASK_CS_FTI_ADDR_VIOL = 1ULL << 1;

/*
 * L3 mask bits.
 */
static const uint64_t MILAN_RAS_MASK_L3_HWA = 1ULL << 7;

/*
 * Entry point for initialization.
 */
void milan_ras_init(void);

#endif /* _SYS_IO_MILAN_RAS_H */
