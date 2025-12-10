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

#include <sys/stdbool.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum apob_general_type {
	APOB_GENERAL_TYPE_CFG_INFO = 3,
	APOB_GENERAL_TYPE_S3REPLAY_BUF_INFO = 4,
	APOB_GENERAL_TYPE_EVENT_LOG = 6,
	APOB_GENERAL_TYPE_CFG_DATA = 26		/* not on Milan */
} apob_general_type;

typedef enum apob_memory_type {
	APOB_MEMORY_TYPE_GEN_ERRORS = 1,
	APOB_MEMORY_TYPE_GEN_CONFIG_INFO = 2,
	APOB_MEMORY_TYPE_APCB_BOOT_INFO = 16,
	APOB_MEMORY_TYPE_MBIST_RES_INFO = 18,
	APOB_MEMORY_TYPE_PMU_TRAIN_FAIL = 22,	/* not on Milan */
	APOB_MEMORY_TYPE_SOC_INIT_CFG = 27,
	APOB_MEMORY_TYPE_S3_DDR0 = 30,
	APOB_MEMORY_TYPE_S3_DDR1,
	APOB_MEMORY_TYPE_S3_DDR2,
	APOB_MEMORY_TYPE_S3_DDR3,
	APOB_MEMORY_TYPE_S3_DDR4,
	APOB_MEMORY_TYPE_S3_DDR5,
	APOB_MEMORY_TYPE_S3_DDR6,
	APOB_MEMORY_TYPE_S3_DDR7,
	APOB_MEMORY_TYPE_S3_DDR8,
	APOB_MEMORY_TYPE_S3_DDR9,
	/* The following entries were introduced with Genoa */
	APOB_MEMORY_TYPE_S3_MOP0 = 70,
	APOB_MEMORY_TYPE_S3_MOP1,
	APOB_MEMORY_TYPE_S3_MOP2,
	APOB_MEMORY_TYPE_S3_MOP3,
	APOB_MEMORY_TYPE_S3_MOP4,
	APOB_MEMORY_TYPE_S3_MOP5,
	APOB_MEMORY_TYPE_S3_MOP6,
	APOB_MEMORY_TYPE_S3_MOP7,
	APOB_MEMORY_TYPE_S3_MOP8,
	APOB_MEMORY_TYPE_S3_MOP9,
	APOB_MEMORY_TYPE_S3_MOP10,
	APOB_MEMORY_TYPE_S3_MOP11,
	APOB_MEMORY_TYPE_PMU_SMB0 = 90,
	APOB_MEMORY_TYPE_PMU_SMB1,
	APOB_MEMORY_TYPE_PMU_SMB2,
	APOB_MEMORY_TYPE_PMU_SMB3,
	APOB_MEMORY_TYPE_PMU_SMB4,
	APOB_MEMORY_TYPE_PMU_SMB5,
	APOB_MEMORY_TYPE_PMU_SMB6,
	APOB_MEMORY_TYPE_PMU_SMB7,
	APOB_MEMORY_TYPE_PMU_SMB8,
	APOB_MEMORY_TYPE_PMU_SMB9,
	APOB_MEMORY_TYPE_PMU_SMB10,
	APOB_MEMORY_TYPE_PMU_SMB11,
} apob_memory_type_t;

#pragma pack(1)

/*
 * This represents a single training error entry.
 */
typedef struct apob_pmu_tfi_ent {
	union {
		/*
		 * The number of bits allocated for the UMC grew from 3 to 4
		 * between Zen3 and Zen4 to accomodate the increased number
		 * of channels.
		 */
		struct {
			/*
			 * These indicate the socket and the numeric UMC entry.
			 */
			uint32_t apte_sock:1;
			uint32_t apte_umc:3;
			/*
			 * This appears to be 0 for 1D and 1 for 2D.
			 */
			uint32_t apte_1d2d:1;
			uint32_t apte_1dnum:3;
			uint32_t apte_dtype:1;
			uint32_t apte_rsvd:7;
		} s;
		struct {
			uint32_t apte_sock:1;
			uint32_t apte_umc:4;
			uint32_t apte_1d2d:1;
			uint32_t apte_1dnum:3;
			uint32_t apte_dtype:1;
			uint32_t apte_rsvd:6;
		} l;
	};
	uint32_t apte_stage:16;
	uint32_t apte_error;
	uint32_t apte_data[4];
} apob_tfi_ent_t;

typedef struct apob_pmu_tfi {
	/*
	 * While we describe this as the number of valid entries, it represents
	 * the next location that information should have been entered into.
	 */
	uint32_t apt_nvalid;
	/*
	 * The use of 40 entries here comes from AMD. For Milan, this
	 * represents 8 channels times five errors each. The APOB version has
	 * not changed with Genoa and Turin so there are still only 40 slots
	 * despite those platforms having 12 channels.
	 */
	apob_tfi_ent_t apt_ents[40];
} apob_pmu_tfi_t;

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

/*
 * ABL_MEM_AGESA_MEMORY_TEST_ERROR - Indicates that a memory test error
 * occured. Data 0 contains information about where.
 */
#define	APOB_EVENT_MEMTEST_ERROR	0x4003
#define	APOB_EVENT_MEMTEST_ERROR_GET_SOCK(x)	bitx32(x, 7, 0)
#define	APOB_EVENT_MEMTEST_ERROR_GET_CHAN(x)	bitx32(x, 15, 8)
#define	APOB_EVENT_MEMTEST_ERROR_GET_DIMM0(x)	bitx32(x, 16, 16)
#define	APOB_EVENT_MEMTEST_ERROR_GET_DIMM1(x)	bitx32(x, 17, 17)

/*
 * ABL_MEM_PMU_TRAIN_EVER_FAILED - Indicates whether the PMU ever failed to
 * train. Data 0 contains the retried channels and retry count. Turin 1.0.0.4
 * and later.
 */
#define	APOB_EVENT_PMU_RETRY_TRAIN	0x401b
#define	APOB_EVENT_PMU_RETRY_TRAIN_GET_SOCK(x)	bitx32(x, 7, 0)
#define	APOB_EVENT_PMU_RETRY_TRAIN_GET_COUNT(x)	bitx32(x, 15, 8)
#define	APOB_EVENT_PMU_RETRY_TRAIN_GET_CHANS(x)	bitx32(x, 27, 16)
#define	APOB_EVENT_PMU_RETRY_TRAIN_CHANS	12

/*
 * ABL_MEM_RRW_ERROR - MBIST resulted in an error. There is no accompanying
 * data. Genoa and later.
 */
#define	APOB_EVENT_MEM_RRW_ERROR	0x402a

/*
 * ABL_MEM_ERROR_PMIC_ERROR - A PMIC error. Data 0 contains the memory channel
 * and DIMM for the error. Data 1 contains the PMIC error registers.
 * Genoa and later.
 *
 * PMIC register definitions from JEDEC Standard 301-1A.02, Rev 1.8.5, Mar 2023
 */
#define	APOB_EVENT_MEM_PMIC_ERROR	0x406b
#define	APOB_EVENT_MEM_PMIC_ERROR_GET_SOCK(x)		bitx32(x, 7, 0)
#define	APOB_EVENT_MEM_PMIC_ERROR_GET_CHAN(x)		bitx32(x, 15, 8)
#define	APOB_EVENT_MEM_PMIC_ERROR_GET_DIMM(x)		bitx32(x, 16, 16)
#define	APOB_EVENT_MEM_PMIC_ERROR_GET_CHAN_STATUS(x)	bitx32(x, 17, 17)

#define	APOB_EVENT_MEM_PMIC_ERROR_GET_PMIC_REG4(x)	bitx32(x, 7, 0)
#define	PMIC_REG4_GET_CRITICAL_TEMPERATURE(x)		bitx32(x, 4, 4)
#define	PMIC_REG4_GET_VIN_BULK_OVER_VOLTAGE(x)		bitx32(x, 5, 5)
#define	PMIC_REG4_GET_BUCK_OV_OR_UV(x)			bitx32(x, 6, 6)
#define	PMIC_REG4_GET_ERRORS(x)				bitx32(x, 7, 7)

#define	APOB_EVENT_MEM_PMIC_ERROR_GET_PMIC_REG5(x)	bitx32(x, 15, 8)
#define	PMIC_REG5_GET_PMIC_LAST_STATUS(x)		bitx32(x, 2, 0)
#define	PMIC_REG5_PMIC_LAST_STATUS_NORMAL		0
#define	PMIC_REG5_PMIC_LAST_STATUS_BUCK_OV_OR_UV	2
#define	PMIC_REG5_PMIC_LAST_STATUS_CRIT_TEMP		3
#define	PMIC_REG5_PMIC_LAST_STATUS_VIN_BULK_OV		4
#define	PMIC_REG5_GET_PMIC_SWD_PWR_NOT_GOOD(x)		bitx32(x, 3, 3)
#define	PMIC_REG5_GET_PMIC_SWC_PWR_NOT_GOOD(x)		bitx32(x, 4, 4)
#define	PMIC_REG5_GET_PMIC_SWB_PWR_NOT_GOOD(x)		bitx32(x, 5, 5)
#define	PMIC_REG5_GET_PMIC_SWA_PWR_NOT_GOOD(x)		bitx32(x, 6, 6)

#define	APOB_EVENT_MEM_PMIC_ERROR_GET_PMIC_REG6(x)	bitx32(x, 23, 16)
#define	PMIC_REG6_GET_PMIC_SWD_OVER_VOLTAGE(x)		bitx32(x, 0, 0)
#define	PMIC_REG6_GET_PMIC_SWC_OVER_VOLTAGE(x)		bitx32(x, 1, 1)
#define	PMIC_REG6_GET_PMIC_SWB_OVER_VOLTAGE(x)		bitx32(x, 2, 2)
#define	PMIC_REG6_GET_PMIC_SWA_OVER_VOLTAGE(x)		bitx32(x, 3, 3)
#define	PMIC_REG6_GET_PMIC_SWD_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 4, 4)
#define	PMIC_REG6_GET_PMIC_SWC_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 5, 5)
#define	PMIC_REG6_GET_PMIC_SWB_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 6, 6)
#define	PMIC_REG6_GET_PMIC_SWA_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 7, 7)

/*
 * ABL_MEM_CHANNEL_POPULATION_ORDER - Memory channels not populated in
 * AMD recommended order. Data 0 contains which socket. Genoa and later.
 */
#define	APOB_EVENT_MEM_POP_ORDER	0x406c
#define	APOB_EVENT_MEM_POP_ORDER_GET_SOCK(x)		bitx32(x, 7, 0)
#define	APOB_EVENT_MEM_POP_ORDER_GET_SYSTEM_HALTED(x)	bitx32(x, 16, 16)

/*
 * ABL_MEM_SPD_VERIFY_CRC_ERROR - Failed to verify DIMM SPD CRC. Data 0
 * contains information about where. Genoa and later.
 */
#define	APOB_EVENT_MEM_SPD_CRC_ERROR	0x406d
#define	APOB_EVENT_MEM_SPD_CRC_ERROR_GET_SOCK(x)	bitx32(x, 7, 0)
#define	APOB_EVENT_MEM_SPD_CRC_ERROR_GET_CHAN(x)	bitx32(x, 15, 8)
#define	APOB_EVENT_MEM_SPD_CRC_ERROR_GET_DIMM(x)	bitx32(x, 23, 16)

/*
 * ABL_MEM_ERROR_PMIC_REAL_TIME_ERROR - The PMIC is reporting a real-time
 * error. Data 0 contains the memory channel and DIMM for the error along with
 * PMIC register 33. Data 1 contains PMIC registers 0x8-0xb. Genoa and later.
 *
 * PMIC register definitions from JEDEC Standard 301-1A.02, Rev 1.8.5, Mar 2023
 */
#define	APOB_EVENT_PMIC_RT_ERROR	0x406e
#define	APOB_EVENT_PMIC_RT_ERROR_GET_SOCK(x)		bitx32(x, 7, 0)
#define	APOB_EVENT_PMIC_RT_ERROR_GET_CHAN(x)		bitx32(x, 15, 8)
#define	APOB_EVENT_PMIC_RT_ERROR_GET_DIMM(x)		bitx32(x, 16, 16)
#define	APOB_EVENT_PMIC_RT_ERROR_GET_CHAN_STATUS(x)	bitx32(x, 17, 17)

#define	APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REG33(x)	bitx32(x, 31, 24)
#define	PMIC_REG33_GET_VOUT_1P0V_PWR_NOT_GOOD(x)	bitx32(x, 2, 2)
#define	PMIC_REG33_GET_VBIAS_VIN_BULK_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 3, 3)
#define	PMIC_REG33_GET_VIN_MGMT_PWR_GOOD_SWITCHOVER_MODE(x)	bitx32(x, 4, 4)

#define	APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REG8(x)	bitx32(x, 7, 0)
#define	PMIC_REG8_GET_VIN_BULK_INPUT_OVER_VOLTAGE(x)	bitx32(x, 0, 0)
#define	PMIC_REG8_GET_VIN_MGMT_INPUT_OVER_VOLTAGE(x)	bitx32(x, 1, 1)
#define	PMIC_REG8_GET_SWD_PWR_NOT_GOOD(x)		bitx32(x, 2, 2)
#define	PMIC_REG8_GET_SWC_PWR_NOT_GOOD(x)		bitx32(x, 3, 3)
#define	PMIC_REG8_GET_SWB_PWR_NOT_GOOD(x)		bitx32(x, 4, 4)
#define	PMIC_REG8_GET_SWA_PWR_NOT_GOOD(x)		bitx32(x, 5, 5)
#define	PMIC_REG8_GET_CRIT_TEMP_SHUTDOWN(x)		bitx32(x, 6, 6)
#define	PMIC_REG8_GET_VIN_BULK_PWR_NOT_GOOD(x)		bitx32(x, 7, 7)

#define	APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REG9(x)	bitx32(x, 15, 8)
#define	PMIC_REG9_GET_SWD_HIGH_OUTPUT_CURRENT_WARN(x)	bitx32(x, 0, 0)
#define	PMIC_REG9_GET_SWC_HIGH_OUTPUT_CURRENT_WARN(x)	bitx32(x, 1, 1)
#define	PMIC_REG9_GET_SWB_HIGH_OUTPUT_CURRENT_WARN(x)	bitx32(x, 2, 2)
#define	PMIC_REG9_GET_SWA_HIGH_OUTPUT_CURRENT_WARN(x)	bitx32(x, 3, 3)
#define	PMIC_REG9_GET_VIN_MGMT_VIN_BULK_SWITCHOVER(x)	bitx32(x, 4, 4)
#define	PMIC_REG9_GET_VOUT_1P8V_PWR_NOT_GOOD(x)		bitx32(x, 5, 5)
#define	PMIC_REG9_GET_VBIAS_PWR_NOT_GOOD(x)		bitx32(x, 6, 6)
#define	PMIC_REG9_GET_HIGH_TEMP_WARNING(x)		bitx32(x, 7, 7)

#define	APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REGA(x)	bitx32(x, 23, 16)
#define	PMIC_REGA_GET_PENDING_IBI_OR_OUTSTANDING(x)	bitx32(x, 1, 1)
#define	PMIC_REGA_GET_PARITY_ERROR(x)			bitx32(x, 2, 2)
#define	PMIC_REGA_GET_PEC_ERROR(x)			bitx32(x, 3, 3)
#define	PMIC_REGA_GET_SWD_OVER_VOLTAGE(x)		bitx32(x, 4, 4)
#define	PMIC_REGA_GET_SWC_OVER_VOLTAGE(x)		bitx32(x, 5, 5)
#define	PMIC_REGA_GET_SWB_OVER_VOLTAGE(x)		bitx32(x, 6, 6)
#define	PMIC_REGA_GET_SWA_OVER_VOLTAGE(x)		bitx32(x, 7, 7)

#define	APOB_EVENT_PMIC_RT_ERROR_GET_PMIC_REGB(x)	bitx32(x, 31, 24)
#define	PMIC_REGB_GET_SWD_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 0, 0)
#define	PMIC_REGB_GET_SWC_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 1, 1)
#define	PMIC_REGB_GET_SWB_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 2, 2)
#define	PMIC_REGB_GET_SWA_UNDER_VOLTAGE_LOCKOUT(x)	bitx32(x, 3, 3)
#define	PMIC_REGB_GET_SWD_CURRENT_LIMITER_WARN(x)	bitx32(x, 4, 4)
#define	PMIC_REGB_GET_SWC_CURRENT_LIMITER_WARN(x)	bitx32(x, 5, 5)
#define	PMIC_REGB_GET_SWB_CURRENT_LIMITER_WARN(x)	bitx32(x, 6, 6)
#define	PMIC_REGB_GET_SWA_CURRENT_LIMITER_WARN(x)	bitx32(x, 7, 7)

/*
 * MEM_EVENT_PMU_BIST - This class is used for PMU BIST event records.
 * Genoa and later.
 */
#define	ABL_EVENT_PMU_MBIST		0x4012b00

/*
 * The apob_gen_cfg_info structure is returned for (group, type, instance) =
 * (APOB_GROUP_GENERAL, APOB_GENERAL_TYPE_CFG_INFO, 0) which contains various
 * system configuration fields.
 */
typedef struct apob_gen_cfg_param {
	uint32_t	agcp_apcb_instance_id;
	/*
	 * This field indicates if the ABL should attempt eMCR. It's also
	 * used to determine if the APOB from the current boot should be
	 * saved to flash.
	 */
	bool		agcp_apob_restore;

	uint8_t		agcp_subprogram;
	uint16_t	agcp_boardmask;
	uint32_t	agcp_pad;
} apob_gen_cfg_param_t;

typedef struct apob_gen_cfg_info {
	/*
	 * The boot mode detected by ABL.
	 */
	uint32_t	agci_bootmode;
	/*
	 * The following two booleans indicate whether the ABL thinks we are
	 * running in an emulated or simulated environment.
	 */
	bool		agci_emulenv;
	bool		agci_simulenv;
	uint16_t	agci_pad1;

	/*
	 * The following data contains information about the error reporting
	 * configuration of the ABL. We currently treat it as opaque.
	 */
	uint8_t		agci_error_report[38];

	/*
	 * Since we are under a `#pragma pack(1)` here we need to be explicit
	 * about all of the required padding. AMD sources don't pack their
	 * version of this struct and don't include the following pad field.
	 */
	uint8_t		agci_pad2[2];

	/*
	 * Parameter information.
	 */
	apob_gen_cfg_param_t agci_param;

	/*
	 * The remaining structure members are CPU-specific and not currently
	 * included.
	 */
} apob_gen_cfg_info_t;

#pragma pack()	/* pack(1) */

/*
 * Consult the firmware-provided APOB system memory map to mark any holes
 * in the physical address space as reserved.
 */
extern void zen_apob_reserve_phys(void);

/*
 * Send the APOB data to the SP in order that they may be saved and used by
 * eMCR on subsequent boots.
 */
extern void zen_apob_sp_transmit(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_APOB_H */
