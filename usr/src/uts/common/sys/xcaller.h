/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * Copyright 2022 Oxide Computer Company
 */


struct xcaller_basic_test {
	uint32_t	xbt_count;
	int32_t		xbt_target;
	uint64_t	xbt_duration;
	uint64_t	*xbt_timings;
};

#define	XCALLER_IOC_BASE	(('X' << 16) | ('C' << 8))

#define	XCALLER_BASIC_TEST	(XCALLER_IOC_BASE | 0x01)
