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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef	_SYS_CPUFREQ_MON_H
#define	_SYS_CPUFREQ_MON_H

/*
 * Per-CPU effective/average frequency monitor. See cpufreq_mon.c.
 */

#include <sys/machlock.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void cpufreq_mon_init(void);
extern void cpufreq_mon_sample(void);

/*
 * Take an opportunistic sample from the interrupt dispatch path, unless
 * either the arriving interrupt or the interrupted context is at high level.
 * A sample is not worth adding latency to high-level dispatch, nor taking on
 * top of preempted high-level code.
 */
static inline void
cpufreq_mon_intr_sample(int oldipl, int newipl)
{
	if (newipl <= LOCK_LEVEL && oldipl <= LOCK_LEVEL)
		cpufreq_mon_sample();
}

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_CPUFREQ_MON_H */
