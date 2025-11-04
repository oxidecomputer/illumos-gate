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

#ifndef _MILAN_MILAN_APOB_H
#define	_MILAN_MILAN_APOB_H

#include <sys/memlist.h>
#include <sys/bitext.h>

/*
 * Definitions that relate to parsing and understanding the Milan APOB.  These
 * definitions are specific to Milan and may not be applicable to other
 * processor families.  Common definitions go in sys/apob.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	MILAN_APOB_FABRIC_PHY_OVERRIDE		21

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

#pragma pack()

extern void milan_apob_reserve_phys(void);

#ifdef __cplusplus
}
#endif

#endif /* _MILAN_MILAN_APOB_H */
