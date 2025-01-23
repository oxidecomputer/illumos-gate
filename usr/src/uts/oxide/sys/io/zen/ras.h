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

#ifndef	_SYS_IO_ZEN_RAS_H
#define	_SYS_IO_ZEN_RAS_H

#include <sys/debug.h>
#include <sys/int_types.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MCAX registers are organized into 64 banks of 16 MSRs, starting at MSR
 * 0xC000_2000.  Legacy MCA registers as well as the extended MCAX register are
 * co-resident in each bank.  The bank offsets are:
 *
 *   0. CTL	(Legacy)
 *   1. STATUS	(Legacy)
 *   2. ADDR	(Legacy)
 *   3. MISC0	(Legacy)
 *   4. CFG	(Extended)
 *   5. IPID	(Extended)
 *   6. SYND	(Extended)
 *   7. Resv'ed	(Extended)
 *   8. DESTAT	(Extended)
 *   9. DEADDR	(Extended)
 *  10. MISC1	(Extended)
 *  11. MISC2	(Extended)
 *  12. MISC3	(Extended)
 *  13. MISC4	(Extended)
 *  14. SYND1	(Extended)
 *  15. SYND2	(Extended)
 *
 * The banks are also typed.  Decoding which type of bank is done by
 * matching on the pair of hardware ID and MCA type fields from the IPID
 * register.
 */

/*
 * These constants are taken from the RAS section of the PPRs
 * for Milan, Genoa, and Turin.
 */
static const uint_t ZEN_RAS_MAX_BANKS = 64;
static const uint_t ZEN_RAS_MAX_MCAX_BANKS = 32;
static const uint32_t ZEN_RAS_BANK_MSR_BASE = 0xc0002000U;

/*
 * Each MCAX register bank consists of 16 MSRs, layed out
 * as follows.  Note that three are reserved
 */
typedef enum zen_ras_mcax_bank_reg {
	ZEN_RAS_MSR_CTL,
	ZEN_RAS_MSR_STATUS,
	ZEN_RAS_MSR_ADDR,
	ZEN_RAS_MSR_MISC0,
	ZEN_RAS_MSR_CFG,
	ZEN_RAS_MSR_IPID,
	ZEN_RAS_MSR_SYND,
	ZEN_RAS_MSR_RESERVED7,
	ZEN_RAS_MSR_DESTAT,
	ZEN_RAS_MSR_DEADDR,
	ZEN_RAS_MSR_MISC1,
	ZEN_RAS_MSR_MISC2,
	ZEN_RAS_MSR_MISC3,
	ZEN_RAS_MSR_MISC4,
	ZEN_RAS_MSR_SYND1,
	ZEN_RAS_MSR_SYND2,
} zen_ras_mcax_bank_reg_t;
#define	ZEN_RAS_MSR_BANK_NREGS	(ZEN_RAS_MSR_SYND2 + 1)
CTASSERT(ZEN_RAS_MSR_BANK_NREGS == 16);

/*
 * Common bits in RAS bank configuration registers.
 */
static const uint_t ZEN_RAS_CFG_MCAX = 0;
static const uint_t ZEN_RAS_CFG_TRANSPARENT_LOGGING_SUPTD = 1;
static const uint_t ZEN_RAS_CFG_DEFERRED_LOGGING_SUPTD = 2;
static const uint_t ZEN_RAS_CFG_MCAX_EN = 32;
static const uint_t ZEN_RAS_CFG_TRANSPARENT_LOGGING_EN = 33;
static const uint_t ZEN_RAS_CFG_LOG_DEFERRED_IN_MCA_STAT = 34;

/*
 * The MCA control mask MSRs are in a block by themselves, starting
 * at ZEN_RAS_MCA_CTL_MASK_MSR_BASE and indexed by bank number.
 * Thus, bank 0 is at ZEN_RAS_MCA_CTL_MASK_MSR_BASE + 0, bank 1
 * at ZEN_RAS_MCA_CTL_MASK_MSR_BASE + 1, and so on.
 */
static const uint32_t ZEN_RAS_MCA_CTL_MASK_MSR_BASE = 0xc0010400U;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IO_ZEN_RAS_H */
