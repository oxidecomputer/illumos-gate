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
 * Copyright 2024 Oxide Computer Company
 */

/*
 * Type, structure, and function definitions for for interacting with the
 * System Management Unit, or SMU.
 */

#ifndef	_ZEN_SMU_H
#define	_ZEN_SMU_H

#include <sys/types.h>
#include <sys/stdbool.h>

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;

/*
 * Retrieves and reports the SMU firmware version.
 */
extern bool zen_smu_get_fw_version(zen_iodie_t *iodie);
extern void zen_smu_report_fw_version(const zen_iodie_t *iodie);

/*
 * Returns true if the firmware version running on the SMU for the given IO die
 * is greater than or equal to the given major, minor, and patch versions.
 */
extern bool zen_smu_version_at_least(const zen_iodie_t *, uint8_t, uint8_t,
    uint8_t);

/*
 * Reads the CPU "name" string from the SMU.
 */
extern bool zen_smu_get_brand_string(zen_iodie_t *, char *, size_t);

/*
 * Initializes "early" SMU features.
 */
extern bool zen_smu_early_features_init(zen_iodie_t *);

/*
 * Sets mid-point SMU features.
 */
extern bool zen_smu_features_init(zen_iodie_t *);

#endif	/* _ZEN_SMU_H */
