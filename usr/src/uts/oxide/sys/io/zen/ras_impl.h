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

#ifndef	_SYS_IO_ZEN_RAS_IMPL_H
#define	_SYS_IO_ZEN_RAS_IMPL_H

/*
 * Types, prototypes and so forth for initializing RAS from the common parts of
 * the Oxide architecture code.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Data types, function prototypes, and so forth related to RAS implementation
 * on the Oxide platform.  Note that architecturally defined values are in the
 * corresponding "ras.h" header file.
 */

#include <sys/io/zen/ras.h>

/*
 * Abstract MCAX bank types.
 *
 * The type of each bank is determined by decoding the IPID register in that
 * bank.  Note, these values are strictly for our own consumption, and do not
 * reflect hardware values.  This enum contains the union of all bank types
 * across our supported microarchitectures, and not every uarch supports every
 * bank type.  A uarch ops vector entry inspects the hardware and projects the
 * decoded bank type into one of these values.
 */
typedef enum zen_ras_bank_type {
	ZEN_RBT_LS,	/* Load-Store Unit */
	ZEN_RBT_IF,	/* Instruction Fetch Unit */
	ZEN_RBT_L2,	/* L2 Cache Unit */
	ZEN_RBT_L3,	/* L3 Cache Unit */
	ZEN_RBT_MP5,	/* Microprocessor5 Management Controller */
	ZEN_RBT_PB,	/* Parameter Block */
	ZEN_RBT_PCS_GMI,  /* Physical Coding Sublayer GMI Controller */
	ZEN_RBT_KPX_GMI,  /* Kompressed Packet Mux GMI: High speed interface */
	ZEN_RBT_KPX_WAFL, /* KPX Wide-Area Fabric Link */
	ZEN_RBT_MPDMA,	/* DMA Engine Controller */
	ZEN_RBT_UMC,	/* Unified Memory Controller */
	ZEN_RBT_PCIE,	/* PCIe Root Port */
	ZEN_RBT_SATA,	/* SATA (Serial ATA); unused on Oxide */
	ZEN_RBT_USB,	/* Universal Serial Bus; unused on Oxide */
	ZEN_RBT_NBIO,	/* Northbridge IO Unit */
	ZEN_RBT_NBIF,	/* Northbridge interface */
	ZEN_RBT_SMU,	/* System Management Controller Unit */
	ZEN_RBT_SHUB,	/* System Hub */
	ZEN_RBT_PIE,	/* Power Management, Interrupts, Etc (seriously?!) */
	ZEN_RBT_PSP,	/* Platform Security Processor */
	ZEN_RBT_PCS_XGMI,   /* PCS Socket-to-Socket GMI (XGMI) Controller */
	ZEN_RBT_KPX_SERDES, /* KPX Serializer/Deserializer */
	ZEN_RBT_CS,	/* Coherent Slave */
	ZEN_RBT_EX,	/* Execution Unit */
	ZEN_RBT_FP,	/* Float-point Unit */
	ZEN_RBT_DE,	/* Decode Unit */
	ZEN_RBT_UNK,	/* Unknown */
} zen_ras_bank_type_t;

typedef struct zen_ras_bank_type_map {
	const uint64_t			zrbtm_hardware_id;
	const uint64_t			zrbtm_mca_type;
	const zen_ras_bank_type_t	zrbtm_bank_type;
} zen_ras_bank_type_map_t;

/*
 * These identify what bits we set in RAS mask registers for various types of
 * MCA(X) banks.
 */
typedef struct zen_ras_bank_mask_bits {
	const zen_ras_bank_type_t	zrbmb_bank_type;
	const size_t			zrbmb_nbits;
	const uint_t			*zrbmb_bits;
} zen_ras_bank_mask_bits_t;

/*
 * The per-microarchitecture constant data that we embed into the platform
 * constants for each uarch.
 */

typedef struct zen_ras_init_data {
	const zen_ras_bank_type_map_t	*zrid_bank_type_map;
	const size_t			zrid_bank_type_nmap;
	const zen_ras_bank_mask_bits_t	*zrid_bank_mask_map;
	const size_t			zrid_bank_mask_nmap;
} zen_ras_init_data_t;

/*
 * Initialize the current CPU's MCA banks.
 */
extern void zen_ras_init(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_RAS_IMPL_H */
