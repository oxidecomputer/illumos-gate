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

#ifndef	_SYS_IO_GENOA_RAS_H
#define	_SYS_IO_GENOA_RAS_H

#include <sys/debug.h>
#include <sys/int_types.h>
#include <sys/types.h>

/*
 * MCAX registers are organized into 64 banks of 16 MSRs, starting at MSR
 * 0xC000_2000.  Legacy MCA registers as well as the extended MCAX register are
 * co-resident in each bank.  The bank offsets are:
 *
 *   0. CTL      (Legacy)
 *   1. STATUS   (Legacy)
 *   2. ADDR     (Legacy)
 *   3. MISC0    (Legacy)
 *   4. CFG   (Extended)
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
 * the IPID register in that bank.  Note, these values are strictly for our own
 * consumption, and do not reflect hardware values.
 */
enum genoa_ras_bank_type {
	GENOA_RBT_LS,	/* Load-Store Unit */
	GENOA_RBT_IF,	/* Instruction Fetch Unit */
	GENOA_RBT_L2,	/* L2 Cache Unit */
	GENOA_RBT_L3,	/* L3 Cache Unit */
	GENOA_RBT_MP5,	/* Microprocessor5 Management Controller */
	GENOA_RBT_PB,	/* Parameter Block */
	GENOA_RBT_UMC,	/* Unified Memory Controller */
	GENOA_RBT_NBIO,	/* Northbridge IO Unit */
	GENOA_RBT_PCIE,	/* PCIe Root Port */
	GENOA_RBT_SMU,	/* System Management Controller Unit */
	GENOA_RBT_PSP,	/* Platform Security Processor */
	GENOA_RBT_PIE,	/* Power Management, Interrupts, Etc (seriously?!) */
	GENOA_RBT_CS,	/* Coherent Slave */
	GENOA_RBT_EX,	/* Execution Unit */
	GENOA_RBT_FP,	/* Float-point Unit */
	GENOA_RBT_DE,	/* Decode Unit */
	GENOA_RBT_UNK,	/* Unknown */
};

/*
 * These constants are taken from the RAS section of the Genoa PPR.
 */
static const uint_t GENOA_RAS_MAX_BANKS = 64;
static const uint_t GENOA_RAS_MAX_MCAX_BANKS = 32;
static const uint32_t GENOA_RAS_BANK_MSR_BASE = 0xc0002000U;

/*
 * Each MCAX register bank consists of 16 MSRs, layed out
 * as follows.  Note that three are reserved
 */
enum genoa_ras_mcax_bank_reg {
	GENOA_RAS_MSR_CTL,
	GENOA_RAS_MSR_STATUS,
	GENOA_RAS_MSR_ADDR,
	GENOA_RAS_MSR_MISC0,
	GENOA_RAS_MSR_CFG,
	GENOA_RAS_MSR_IPID,
	GENOA_RAS_MSR_SYND,
	GENOA_RAS_MSR_RESERVED7,
	GENOA_RAS_MSR_DESTAT,
	GENOA_RAS_MSR_DEADDR,
	GENOA_RAS_MSR_MISC1,
	GENOA_RAS_MSR_MISC2,
	GENOA_RAS_MSR_MISC3,
	GENOA_RAS_MSR_MISC4,
	GENOA_RAS_MSR_RESERVEDE,
	GENOA_RAS_MSR_RESERVEDF,
	GENOA_RAS_MSR_BANK_NREGS,
};
CTASSERT(GENOA_RAS_MSR_BANK_NREGS == 16);

/*
 * Bits in Genoa RAS bank configuration registers.
 */
static const uint_t GENOA_RAS_CFG_MCAX = 0;
static const uint_t GENOA_RAS_CFG_TRANSPARENT_LOGGING_SUPTD = 1;
static const uint_t GENOA_RAS_CFG_DEFERRED_LOGGING_SUPTD = 2;
static const uint_t GENOA_RAS_CFG_MCAX_EN = 32;
static const uint_t GENOA_RAS_CFG_TRANSPARENT_LOGGING_EN = 33;
static const uint_t GENOA_RAS_CFG_LOG_DEFERRED_IN_MCA_STAT = 34;

/*
 * The MCA control mask MSRs are in a block by themselves, starting
 * at GENOA_RAS_MCA_CTL_MASK_MSR_BASE and indexed by bank number.
 * Thus, bank 0 is at GENOA_RAS_MCA_CTL_MASK_MSR_BASE + 0, bank 1
 * at GENOA_RAS_MCA_CTL_MASK_MSR_BASE + 1, and so on.
 */
static const uint32_t GENOA_RAS_MCA_CTL_MASK_MSR_BASE = 0xc0010400U;

/*
 * LS (load-store) mask bits.  Only bits we refer to in the corresponding
 * private setup code are defined here; these should eventually end up in
 * generic headers as they are not specific to the oxide arch and correspond to
 * status bits that machine-independent CPU modules will need.
 *
 * These are bit position numbers only.
 */
static const uint_t GENOA_RAS_MASK_LS_SYS_RD_DATA_LD = 19;
static const uint_t GENOA_RAS_MASK_LS_SYS_RD_DATA_SCB = 20;
static const uint_t GENOA_RAS_MASK_LS_SYS_RD_DATA_WCB = 21;

/*
 * IF (instruction fetch) mask bits.
 */
static const uint_t GENOA_RAS_MASK_IF_L2_BTB_MULTI = 11;
static const uint_t GENOA_RAS_MASK_IF_L2_TLB_MULTI = 16;

/*
 * L2 mask bits.
 */
static const uint_t GENOA_RAS_MASK_L2_HWA = 3;

/*
 * FP (floating-point) mask bits.
 */
static const uint_t GENOA_RAS_MASK_FP_HWA = 6;

/*
 * CS (coherent slave - DF) mask bits.
 */
static const uint_t GENOA_RAS_MASK_CS_FTI_ADDR_VIOL = 1;

/*
 * L3 mask bits.
 */
static const uint_t GENOA_RAS_MASK_L3_HWA = 7;

/*
 * NBIO (northbridge I/O) mask bits.
 */
static const uint_t GENOA_RAS_MASK_NBIO_PCIE_SB = 1;
static const uint_t GENOA_RAS_MASK_NBIO_PCIE_ERR_EVT = 2;

/*
 * Entry point for initialization.
 */
void genoa_ras_init(void);

#endif	/* _SYS_IO_GENOA_RAS_H */
