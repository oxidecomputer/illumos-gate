/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2025 Chelsio Communications, Inc.
 */

/*
 * Code shared between user and kernel space.
 */

#include <sys/types.h>
#include "osdep.h"
#include "t4fw_interface.h"

const char * t4_devlog_level(uint8_t);
const char * t4_devlog_facility(uint8_t);
uint_t t4_prep_devlog(struct fw_devlog_e *, uint_t);

