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

#ifndef	_SYS_IO_ZEN_APOB_H
#define	_SYS_IO_ZEN_APOB_H

/*
 * Defines types, prototypes, etc, that facilitate microarchitecture-independent
 * code interacting with microarchitecture-dependent APOB things without a
 * direct dependency.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#pragma pack(1)

/*
 * The structure returned for (group, type, instance) = (APOB_GROUP_GENERAL,
 * APOB_GENERAL_TYPE_CFG_INFO, 0) which contains various system configuration
 * fields.
 */
typedef struct apob_gen_cfg_info {
	/*
	 * The boot mode detected by ABL.
	 */
	uint32_t	agci_bootmode;
	/*
	 * The following two booleans indicate whether thew ABL thinks we are
	 * running in an emulated or simulated environment.
	 */
	bool		agci_emulenv;
	bool		agci_simulenv;
	uint16_t	agci_pad1;

	/*
	 * The following data contains information about the error reporting
	 * configuration of the ABL. We currently treat it as opaque.
	 */
	uint8_t		agci_error_report[40];

	uint32_t	agci_apcb_instance_id;
	/*
	 * This field indicates if the ABL should attempt eMCR. It's also
	 * used to determine if the APOB from the current boot should be
	 * saved to flash.
	 */
	bool		agci_apob_restore;

	uint8_t		agci_subprogram;
	uint16_t	agci_boardmask;
} apob_gen_cfg_info_t;

/*
 * The following structures and definitions relate to event log entries that
 * can enter the APOB.
 */
typedef struct apob_event {
	uint32_t aev_class;
	uint32_t aev_info;
	uint32_t aev_data0;
	uint32_t aev_data1;
} apob_event_t;

typedef struct apob_gen_event_log {
	uint16_t agevl_count;
	uint16_t agevl_pad;
	apob_event_t agevl_events[64];
} apob_gen_event_log_t;

/*
 * This enumeration represents some of the event classes that are defined. There
 * are other event classes apparently, but they cannot show up in logs that we
 * can read via this mechanism (i.e. they halt boot).
 */
typedef enum {
	APOB_EVC_ALERT	= 5,
	APOB_EVC_WARN	= 6,
	APOB_EVC_ERROR	= 7,
	APOB_EVC_CRIT	= 8,
	APOB_EVC_FATAL	= 9
} apob_event_class_t;

/*
 * Known events documented below.
 */

/*
 * ABL_MEM_PMU_TRAIN_ERROR - Indicates that the PMU failed to train DRAM. Data 0
 * contains information about where (the first bit of defines below). Data 1
 * contains information about why the error occurred.
 */
#define	APOB_EVENT_TRAIN_ERROR		0x4001
#define	APOB_EVENT_TRAIN_ERROR_GET_SOCK(x)	bitx32(x, 7, 0)
#define	APOB_EVENT_TRAIN_ERROR_GET_CHAN(x)	bitx32(x, 15, 8)
#define	APOB_EVENT_TRAIN_ERROR_GET_DIMM0(x)	bitx32(x, 16, 16)
#define	APOB_EVENT_TRAIN_ERROR_GET_DIMM1(x)	bitx32(x, 17, 17)
#define	APOB_EVENT_TRAIN_ERROR_GET_RANK0(x)	bitx32(x, 24, 24)
#define	APOB_EVENT_TRAIN_ERROR_GET_RANK1(x)	bitx32(x, 25, 25)
#define	APOB_EVENT_TRAIN_ERROR_GET_RANK2(x)	bitx32(x, 26, 26)
#define	APOB_EVENT_TRAIN_ERROR_GET_RANK3(x)	bitx32(x, 27, 27)
#define	APOB_EVENT_TRAIN_ERROR_GET_PMULOAD(x)	bitx32(x, 0, 0)
#define	APOB_EVENT_TRAIN_ERROR_GET_PMUTRAIN(x)	bitx32(x, 1, 1)

/* ABL_MEM_AGESA_MEMORY_TEST_ERROR */
#define	APOB_EVENT_MEMTEST_ERROR	0x4003

/* ABL_MEM_RRW_ERROR */
#define	APOB_EVENT_MEM_RRW_ERROR	0x402a

/* ABL_MEM_ERROR_PMIC_REAL_TIME_ERROR */
#define	APOB_EVENT_PCMIC_RT_ERROR	0x406e

/* MEM_EVENT_PMU_BIST */
#define	ABL_EVENT_PMU_MBIST		0x4013b00

#pragma pack()	/* pack(1) */

/*
 * Consult the firmware-provided APOB system memory map to mark any holes
 * in the physical address space as reserved.
 */
extern void zen_apob_reserve_phys(void);

/*
 * Preserve the APOB data to flash if appropriate.
 */
extern void zen_apob_preserve(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_APOB_H */
