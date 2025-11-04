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

#ifndef _OXHC_UTIL_H
#define	_OXHC_UTIL_H

/*
 * This header file contains definitions for miscellaneous utility functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum oxhc_barcode_type {
	OXBC_0XV2,
	OXBC_MPN1
} oxhc_barcode_type_t;

#define	OXHC_BARCODE_0XV2_PFX	"0XV2"
#define	OXHC_BARCODE_MPN1_PFX	"MPN1"

/*
 * The MPN1 serial scheme is used for components which use manufacturer-issued
 * serial numbers. The format is described in RFD 308. The maximum barcode
 * length including the prefix and delimiters is 128 characters, the
 * manufacturer portion is always three characters and there are four
 * delimiters.
 */
#define	OXHC_MPN1_BARCODE_MAXLEN	128
#define	OXHC_MPN1_BARCODE_MFGLEN	3
#define	OXHC_MPN1_BARCODE_NDELIMS	4
/*
 * The maximum size of an MPN1 barcode component which does not have a fixed
 * size. The fixed portions are "MPN1:mmm:::" where "mmm" is the three
 * character manufacturer ID.
 */
#define	OXHC_MPN1_BARCODE_DYNCOMPLEN		\
	(OXHC_MPN1_BARCODE_MAXLEN - \
	(sizeof (OXHC_BARCODE_MPN1_PFX) - 1) - OXHC_MPN1_BARCODE_MFGLEN - \
	OXHC_MPN1_BARCODE_NDELIMS)

/*
 * We size the fields in this barcode struct to accommodate an MPN1 barcode
 * since the OXV formats are smaller and will fit.
 * These fields are right-padded with NUL characters, but do not contain a
 * terminator if all characters are used.
 */
typedef struct oxhc_barcode {
	oxhc_barcode_type_t	ob_type;
	uint8_t			ob_mfg[OXHC_MPN1_BARCODE_MFGLEN];
	uint8_t			ob_rev[OXHC_MPN1_BARCODE_DYNCOMPLEN];
	uint8_t			ob_pn[OXHC_MPN1_BARCODE_DYNCOMPLEN];
	uint8_t			ob_sn[OXHC_MPN1_BARCODE_DYNCOMPLEN];
} oxhc_barcode_t;

extern const char *topo_oxhc_vendor_name(const char *);
extern bool topo_oxhc_barcode_parse(topo_mod_t *, const oxhc_t *,
    const uint8_t *, size_t, oxhc_barcode_t *);

#ifdef __cplusplus
}
#endif

#endif /* _OXHC_UTIL_H */
