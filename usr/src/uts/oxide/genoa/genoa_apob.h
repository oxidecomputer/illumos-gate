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

#ifndef _GENOA_GENOA_APOB_H
#define	_GENOA_GENOA_APOB_H

#include <sys/memlist.h>
#include <sys/bitext.h>

/*
 * Definitions that relate to parsing and understanding the Genoa APOB
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum genoa_apob_group {
	GENOA_APOB_GROUP_MEMORY	= 1,
	GENOA_APOB_GROUP_DF,
	GENOA_APOB_GROUP_CCX,
	GENOA_APOB_GROUP_NBIO,
	GENOA_APOB_GROUP_FCH,
	GENOA_APOB_GROUP_PSP,
	GENOA_APOB_GROUP_GENERAL,
	GENOA_APOB_GROUP_SMBIOS,
	GENOA_APOB_GROUP_FABRIC
} genoa_apob_group_t;

#define	GENOA_APOB_FABRIC_PHY_OVERRIDE		21
#define	GENOA_APOB_MEMORY_PMU_TRAIN_FAIL	22
#define	GENOA_APOB_GEN_EVENT_LOG		6

#define	GENOA_APOB_CCX_NONE			0xffU

/*
 * This section corresponds to an undocumented AMD interface.  Do not modify
 * these definitions or remove this packing pragma.
 *
 * A note on constants, especially in array sizes: These often correspond
 * to constants that have real meaning and that we have defined elsewhere, such
 * as the maximum number of CCXs per CCD. However, we do not and MUST NOT use
 * those constants here, because the sizes in the APOB may not be the same as
 * the underlying physical meaning.  In this example, the APOB seems to have
 * been defined so that it could support both multiple microarchitectures,
 * allowing up to 2 CCXs for each of 8 CCDs (per socket).  There is no real
 * part that has been made that way, as far as we know, which means the APOB
 * structures must be considered their own completely independent thing.
 *
 * Never confuse the APOB with reality.
 */
#pragma pack(1)

typedef struct genoa_apob_sysmap_ram_hole {
	uint64_t gasmrh_base;
	uint64_t gasmrh_size;
	uint32_t gasmrh_reason;
	uint32_t _pad;
} genoa_apob_sysmap_ram_hole_t;

/* XXX(cross): Cross-reference these with the Genoa docs. */

/*
 * What we get back (if anything) from GROUP_FABRIC type 9 instance 0
 */
typedef struct genoa_apob_sysmap {
	uint64_t gasm_high_phys;
	uint32_t gasm_hole_count;
	uint32_t _pad;
	genoa_apob_sysmap_ram_hole_t gasm_holes[18];
} genoa_apob_sysmap_t;

#define	GENOA_APOB_CCX_MAX_THREADS	2

typedef struct genoa_apob_core {
	uint8_t gac_id;
	uint8_t gac_thread_exists[GENOA_APOB_CCX_MAX_THREADS];
} genoa_apob_core_t;

#define	GENOA_APOB_CCX_MAX_CORES	8

typedef struct genoa_apob_ccx {
	uint8_t gacx_id;
	genoa_apob_core_t gacx_cores[GENOA_APOB_CCX_MAX_CORES];
} genoa_apob_ccx_t;

#define	GENOA_APOB_CCX_MAX_CCXS		2

typedef struct genoa_apob_ccd {
	uint8_t gacd_id;
	genoa_apob_ccx_t gacd_ccxs[GENOA_APOB_CCX_MAX_CCXS];
} genoa_apob_ccd_t;

#define	GENOA_APOB_CCX_MAX_CCDS		8

/*
 * What we get back (if anything) from GROUP_CCX type 3 instance 0
 */
typedef struct genoa_apob_coremap {
	genoa_apob_ccd_t gacm_ccds[GENOA_APOB_CCX_MAX_CCDS];
} genoa_apob_coremap_t;

#define	GENOA_APOB_PHY_OVERRIDE_MAX_LEN	256

/*
 * What we get back (if anything) from GROUP_FABRIC type
 * GENOA_APOB_FABRIC_PHY_OVERRIDE instance 0
 */
typedef struct genoa_apob_phyovr {
	uint32_t gap_datalen;
	uint8_t gap_data[GENOA_APOB_PHY_OVERRIDE_MAX_LEN];
} genoa_apob_phyovr_t;

/*
 * This represents a single training error entry.
 */
typedef struct genoa_apob_pmu_tfi_ent {
	/*
	 * These indicate the socket and the numeric UMC entry.
	 */
	uint32_t gapte_sock:1;
	uint32_t gapte_umc:3;
	/*
	 * This appears to be 0 for 1D and 1 for 2D.
	 */
	uint32_t gapte_1d2d:1;
	uint32_t gapte_1dnum:3;
	uint32_t gapte_rsvd:7;
	uint32_t gapte_stage:16;
	uint32_t gapte_error;
	uint32_t gapte_data[4];
} genoa_apob_tfi_ent_t;

typedef struct genoa_apob_pmu_tfi {
	/*
	 * While we describe this as the number of valid entries, it represents
	 * the next location that information should have been entered into.
	 */
	uint32_t gapt_nvalid;
	/*
	 * The use of 40 entries here comes from AMD. This represents 8 channels
	 * times five errors each.
	 */
	genoa_apob_tfi_ent_t gapt_ents[40];
} genoa_apob_pmu_tfi_t;

/*
 * The following structures and definitions relate to event log entries that can
 * enter the APOB.
 */
typedef struct genoa_apob_event {
	uint32_t gev_class;
	uint32_t gev_info;
	uint32_t gev_data0;
	uint32_t gev_data1;
} genoa_apob_event_t;

typedef struct genoa_apob_event_log {
	uint16_t gevl_count;
	uint16_t gevl_pad;
	genoa_apob_event_t gevl_events[64];
} genoa_apob_event_log_t;

/*
 * This enumeration represents some of the event classes that are defined. There
 * are other event classes apparently, but they cannot show up in logs that we
 * can read via this mechanism (i.e. they halt boot).
 */
typedef enum {
	GENOA_APOB_EVC_ALERT	= 5,
	GENOA_APOB_EVC_WARN	= 6,
	GENOA_APOB_EVC_ERROR	= 7,
	GENOA_APOB_EVC_CRIT	= 8,
	GENOA_APOB_EVC_FATAL	= 9
} genoa_apob_event_class_t;

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

extern void genoa_apob_init(uint64_t);
extern const void *genoa_apob_find(genoa_apob_group_t, uint32_t, uint32_t,
    size_t *, int *);

extern void genoa_apob_reserve_phys(void);

#ifdef __cplusplus
}
#endif

#endif /* _GENOA_GENOA_APOB_H */
