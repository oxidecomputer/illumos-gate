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

#ifndef _MILAN_MILAN_APOB_H
#define	_MILAN_MILAN_APOB_H

#include <sys/memlist.h>
#include <sys/bitext.h>

/*
 * Definitions that relate to parsing and understanding the Milan APOB
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum milan_apob_group {
	MILAN_APOB_GROUP_MEMORY	= 1,
	MILAN_APOB_GROUP_DF,
	MILAN_APOB_GROUP_CCX,
	MILAN_APOB_GROUP_NBIO,
	MILAN_APOB_GROUP_FCH,
	MILAN_APOB_GROUP_PSP,
	MILAN_APOB_GROUP_GENERAL,
	MILAN_APOB_GROUP_SMBIOS,
	MILAN_APOB_GROUP_FABRIC
} milan_apob_group_t;

#define	MILAN_APOB_FABRIC_PHY_OVERRIDE		21
#define	MILAN_APOB_MEMORY_PMU_TRAIN_FAIL	22
#define	MILAN_APOB_GEN_EVENT_LOG		6

#define	MILAN_APOB_CCX_NONE			0xffU

/*
 * This section constitutes an undocumented AMD interface.  Do not modify
 * these definitions nor remove this packing pragma.
 *
 * A note on constants, especially in array sizes: These often correspond
 * to constants that have real meaning and that we have defined elsewhere, such
 * as the maximum number of CCXs per CCD.  However, we do not and MUST NOT use
 * those constants here, because the sizes in the APOB may not be the same as
 * the underlying physical meaning.  In this example, the APOB seems to have
 * been defined so that it could support both Rome and Milan, allowing up to
 * 2 CCXs for each of 8 CCDs (per socket).  There is no real part that has
 * been made that way, as far as we know, which means the APOB structures must
 * be considered their own completely independent thing.
 *
 * Never confuse the APOB with reality.
 */
#pragma pack(1)

typedef struct milan_apob_sysmap_ram_hole {
	uint64_t masmrh_base;
	uint64_t masmrh_size;
	uint32_t masmrh_reason;
	uint32_t _pad;
} milan_apob_sysmap_ram_hole_t;

/*
 * What we get back (if anything) from GROUP_FABRIC type 9 instance 0
 */
typedef struct milan_apob_sysmap {
	uint64_t masm_high_phys;
	uint32_t masm_hole_count;
	uint32_t _pad;
	milan_apob_sysmap_ram_hole_t masm_holes[18];
} milan_apob_sysmap_t;

#define	MILAN_APOB_CCX_MAX_THREADS	2

typedef struct milan_apob_core {
	uint8_t mac_id;
	uint8_t mac_thread_exists[MILAN_APOB_CCX_MAX_THREADS];
} milan_apob_core_t;

#define	MILAN_APOB_CCX_MAX_CORES	8

typedef struct milan_apob_ccx {
	uint8_t macx_id;
	milan_apob_core_t macx_cores[MILAN_APOB_CCX_MAX_CORES];
} milan_apob_ccx_t;

#define	MILAN_APOB_CCX_MAX_CCXS		2

typedef struct milan_apob_ccd {
	uint8_t macd_id;
	milan_apob_ccx_t macd_ccxs[MILAN_APOB_CCX_MAX_CCXS];
} milan_apob_ccd_t;

#define	MILAN_APOB_CCX_MAX_CCDS		8

/*
 * What we get back (if anything) from GROUP_CCX type 3 instance 0
 */
typedef struct milan_apob_coremap {
	milan_apob_ccd_t macm_ccds[MILAN_APOB_CCX_MAX_CCDS];
} milan_apob_coremap_t;

#define	MILAN_APOB_PHY_OVERRIDE_MAX_LEN	256

/*
 * What we get back (if anything) from GROUP_FABRIC type
 * MILAN_APOB_FABRIC_PHY_OVERRIDE instance 0
 */
typedef struct milan_apob_phyovr {
	uint32_t map_datalen;
	uint8_t map_data[MILAN_APOB_PHY_OVERRIDE_MAX_LEN];
} milan_apob_phyovr_t;

/*
 * This represents a single training error entry.
 */
typedef struct milan_apob_pmu_tfi_ent {
	/*
	 * These indicate the socket and the numeric UMC entry.
	 */
	uint32_t mapte_sock:1;
	uint32_t mapte_umc:3;
	/*
	 * This appears to be 0 for 1D and 1 for 2D.
	 */
	uint32_t mapte_1d2d:1;
	uint32_t mapte_1dnum:3;
	uint32_t mapte_rsvd:7;
	uint32_t mapte_stage:16;
	uint32_t mapte_error;
	uint32_t mapte_data[4];
} milan_apob_tfi_ent_t;

typedef struct milan_apob_pmu_tfi {
	/*
	 * While we describe this as the number of valid entries, it represents
	 * the next location that information should have been entered into.
	 */
	uint32_t mapt_nvalid;
	/*
	 * The use of 40 entries here comes from AMD. This represents 8 channels
	 * times five errors each.
	 */
	milan_apob_tfi_ent_t mapt_ents[40];
} milan_apob_pmu_tfi_t;

/*
 * The following structures and definitions relate to event log entries that can
 * enter the APOB.
 */
typedef struct milan_apob_event {
	uint32_t mev_class;
	uint32_t mev_info;
	uint32_t mev_data0;
	uint32_t mev_data1;
} milan_apob_event_t;

typedef struct milan_apob_event_log {
	uint16_t mevl_count;
	uint16_t mevl_pad;
	milan_apob_event_t mevl_events[64];
} milan_apob_event_log_t;

/*
 * This enumeration represents some of the event classes that are defined. There
 * are other event classes apparently, but they cannot show up in logs that we
 * can read via this mechanism (i.e. they halt boot).
 */
typedef enum {
	MILAN_APOB_EVC_ALERT	= 5,
	MILAN_APOB_EVC_WARN	= 6,
	MILAN_APOB_EVC_ERROR	= 7,
	MILAN_APOB_EVC_CRIT	= 8,
	MILAN_APOB_EVC_FATAL	= 9
} milan_apob_event_class_t;

/*
 * Known events documented below.
 */

/*
 * ABL_MEM_PMU_TRAIN_ERROR - Indicates that the PMU failed to train DRAM. Data 0
 * contains information about where (the first bit of defines below). Data 1
 * contains information about why the error occurred.
 */
#define	APOB_EVENT_TRAIN_ERROR	0x4001
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

#pragma pack()

extern void milan_apob_init(uint64_t);
extern const void *milan_apob_find(milan_apob_group_t, uint32_t, uint32_t,
    size_t *, int *);

extern void milan_apob_reserve_phys(void);

#ifdef __cplusplus
}
#endif

#endif /* _MILAN_MILAN_APOB_H */
