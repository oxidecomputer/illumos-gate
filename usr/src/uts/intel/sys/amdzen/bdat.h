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

typedef enum {
	PDT_CS_DLY,
	PDT_CLK_DLY,
	PDT_CA_DLY,
	PDT_RX_PB_DLY,
	PDT_VREF_DAC0,
	PDT_VREF_DAC1,
	PDT_VREF_DAC2,
	PDT_VREF_DAC3,
	PDT_DFE_TAP2,
	PDT_DFE_TAP3,
	PDT_DFE_TAP4,
	PDT_TX_DQ_DLY,
	PDT_TX_DQS_DLY,
	PDT_RX_EN_DLY,
	PDT_RX_CLK_DLY,
	PDT_DFIMRL,
	PDT_MAX,
} zen_bdat_phy_data_type_t;

typedef enum {
	PDS_PER_BIT,
	PDS_PER_STROBE,
	PDS_PER_RANK,
	PDS_PER_SUB_CHANNEL,
	PDS_PER_CHANNEL,
	PDS_PER_NIBBLE,
	PDS_PER_BYTE,
	PDS_PER_DIMM,
	PDS_MAX,
} zen_bdat_phy_data_scope_t;

typedef enum {
	PDP_0,
	PDP_1,
	PDP_2,
	PDP_3,
	PDP_MAX,
	PDP_NA = 0xFF,
} zen_bdat_phy_data_pstate_t;

typedef struct {
	zen_bdat_mem_location_t		zbepd_loc;
	uint8_t				zbepd_type;
	uint8_t				zbepd_scope;
	uint8_t				zbepd_pstate;
	uint8_t				zbepd_nelems;
	uint8_t				zbepd_elems_size;
	uint8_t				zbepd_pad[2];
	uint8_t				zbepd_data[];
} zen_bdat_entry_phy_data_t;

#pragma pack()	/* pack(1) */

#define	BDAT_NCHANS		12
#define	BDAT_NSUBCHANS		2
#define	BDAT_NDIMMS		2
#define	BDAT_NCS		2
#define	BDAT_NRANKS		2
#define	BDAT_NBITS		80
#define	BDAT_NNIBS		(BDAT_NBITS / (NBBY / 2))
#define	BDAT_NBYTES		(BDAT_NBITS / NBBY)
#define	BDAT_NVREFDACS		4
#define	BDAT_NVREFDACCTLS	(NBBY + 1)
#define	BDAT_NVREFDACBITS	(BDAT_NBYTES * BDAT_NVREFDACCTLS)
#define	BDAT_NDFETAPS		3

/*
 * Unlike the other definitions above, this structure doesn't appear verbatim
 * in the BDAT but represents a consolidated view of all the
 * `zen_bdat_entry_phy_data_t` for a given socket/channel/p-state.
 */
typedef struct {
	/*
	 * These identify which socket, channel and P-state this set of
	 * consolidated entries correspond to.
	 */
	uint8_t		zbpd_sock;
	uint8_t		zbpd_chan;
	uint8_t		zbpd_pstate;

	/*
	 * Chip-Select (CS) Delay - [Sub-Channel][DIMM][CS]
	 */
	uint8_t		zbpd_csdly[BDAT_NSUBCHANS][BDAT_NDIMMS][BDAT_NCS];
	/*
	 * SDRAM Clock (CLK) Delay - [DIMM]
	 */
	uint8_t		zbpd_clkdly[BDAT_NDIMMS];
	/*
	 * Command/Address (CA) Delay - [Sub-Channel][CA Bit 0..7]
	 */
	uint8_t		zbpd_cadly[BDAT_NSUBCHANS][NBBY];
	/*
	 * Per-bit Rx Delay - [DIMM][Rank][DByte 0..9][Bit 0..7]
	 *
	 * Note, unlike the others this field is not per P-state. Any such
	 * entries we find in the BDAT that otherwise match on this socket &
	 * channel will only be populated in the P-state 0 zen_bdat_phy_data_t.
	 * We include it here for simplicity and to avoid having an extra
	 * P-state dimension for everything else.
	 */
	uint8_t		zbpd_rxpbdly[BDAT_NDIMMS][BDAT_NRANKS][BDAT_NBITS];
	/*
	 * Per-bit Vref DAC values - [VrefDac 0..3][DByte 0..9][Bit 0..7, DBI]
	 */
	uint8_t		zbpd_vrefdac[BDAT_NVREFDACS][BDAT_NVREFDACBITS];
	/*
	 * Per-bit DFE Tap values - [DFETap 2,3,4][DByte 0..9][Bit 0..7]
	 */
	uint8_t		zbpd_dfetap[BDAT_NDFETAPS][BDAT_NBITS];
	/*
	 * Per-bit Write DQ Delay - [DIMM][Rank][DByte 0..9][Bit 0..7]
	 */
	uint16_t	zbpd_txdqdly[BDAT_NDIMMS][BDAT_NRANKS][BDAT_NBITS];
	/*
	 * Per-nibble Write DQS Delay - [DIMM][Rank][DByte 0..9][Nibble 0,1]
	 */
	uint16_t	zbpd_txdqsdly[BDAT_NDIMMS][BDAT_NRANKS][BDAT_NNIBS];
	/*
	 * Per-nibble Rx Enable Delay - [DIMM][Rank][DByte 0..9][Nibble 0,1]
	 */
	uint16_t	zbpd_rxendly[BDAT_NDIMMS][BDAT_NRANKS][BDAT_NNIBS];
	/*
	 * Per-nibble Rx DQS to Clk Delay - [DIMM][Rank][DByte 0..9][Nibble 0,1]
	 */
	uint8_t		zbpd_rxclkdly[BDAT_NDIMMS][BDAT_NRANKS][BDAT_NNIBS];
	/*
	 * Per-byte DDR PHY Interface (DFI) Max Read Latency - [DByte 0..9]
	 */
	uint8_t		zbpd_dfimrl[BDAT_NBYTES];
} zen_bdat_phy_data_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_AMDZEN_BDAT_H */
