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

#ifndef	_SYS_AMDZEN_BDAT_H
#define	_SYS_AMDZEN_BDAT_H

/*
 * Defines types, prototypes, etc for the BIOS Data ACPI Table (BDAT).
 * Note these are the definitions for the raw BDAT data provided by the pre-x86
 * firmware which is not necessarily the same as the BDAT structures provided
 * via ACPI by the BIOS/UEFI firmware.
 */

#include <sys/stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Each socket (up to 2) has a fixed 1 MiB allocated for the BDAT.
 */
#define	BDAT_SIZE	(1024 * 1024)
#define	BDAT_SOC_COUNT	2
#define	BDAT_AREA_SIZE	(BDAT_SOC_COUNT * BDAT_SIZE)

#define	BDAT_SIGNATURE	0x54414442	/* 'BDAT' */

#define	BDAT_DIMM_SPD_SCHEMA	7
#define	BDAT_DIMM_SPD_TYPE	0

#define	BDAT_MEM_TRAINING_DATA_SCHEMA			8
#define	BDAT_MEM_TRAINING_DATA_CAPABILITIES_TYPE	0
#define	BDAT_MEM_TRAINING_DATA_MODE_REGS_TYPE		2 /* Deprecated */
#define	BDAT_MEM_TRAINING_DATA_RCD_REGS_TYPE		3 /* Deprecated */
#define	BDAT_MEM_TRAINING_DATA_RANK_MARGIN_TYPE		6
#define	BDAT_MEM_TRAINING_DATA_DQ_MARGIN_TYPE		7
#define	BDAT_MEM_TRAINING_DATA_PHY_TYPE			8


#pragma pack(1)

typedef struct {
	uint32_t zbh_signature;
	uint32_t zbh_next;
} zen_bdat_header_t;

typedef struct {
	uint8_t		zbe_schema;
	uint8_t		zbe_type;
	uint16_t	zbe_size;
	uint8_t		zbe_data[];
} zen_bdat_entry_header_t;

typedef struct {
	uint8_t		zbes_socket;
	uint8_t		zbes_channel;
	uint8_t		zbes_dimm;
	uint8_t		zbes_pad1;
	uint16_t	zbes_size;
	uint16_t	zbes_pad2;
	uint8_t		zbes_data[];
} zen_bdat_entry_spd_t;

typedef struct {
	uint8_t		zbml_socket;
	uint8_t		zbml_channel;
	uint8_t		zbml_sub_channel;
	uint8_t		zbml_dimm;
	uint8_t		zbml_rank;
} zen_bdat_mem_location_t;

typedef struct {
	uint8_t		zbm_rd_dqdly[2];
	uint8_t		zbm_wr_dqdly[2];
	uint8_t		zbm_rd_vref[2];
	uint8_t		zbm_wr_vref[2];
} zen_bdat_margin_t;

typedef struct {
	zen_bdat_mem_location_t		zberm_loc;
	uint8_t				zberm_pad[3];
	zen_bdat_margin_t		zberm_margin;
} zen_bdat_entry_rank_margin_t;

typedef struct {
	zen_bdat_mem_location_t		zbedm_loc;
	uint8_t				zbedm_pad[3];
	zen_bdat_margin_t		zbedm_margin[];
} zen_bdat_entry_dq_margin_t;

#pragma pack()	/* pack(1) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_AMDZEN_BDAT_H */
