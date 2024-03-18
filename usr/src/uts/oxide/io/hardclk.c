/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2023 Oxide Computer Co.
 */

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	All Rights Reserved	*/

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/lockstat.h>

#include <sys/clock.h>
#include <sys/debug.h>
#include <sys/smp_impldefs.h>
#include <sys/stdbool.h>
#include <sys/zone.h>

/*
 * This file contains all generic part of clock and timer handling.
 * We do not support a hardware time-of-day unit, sometimes called a real-time
 * clock (distinct from the POSIX notion of CLOCK_REALTIME), on this
 * architecture so some of this is stubbed out.
 */

void
tod_set(timestruc_t ts)
{
	time_t adj;
	static bool already_stepped = false;
	extern time_t boot_time;

	ASSERT(MUTEX_HELD(&tod_lock));

	/*
	 * There is no TOD unit, so there's nothing to do regarding that.
	 *
	 * However we take this opportunity to spot when the clock is stepped
	 * significantly forward, and use that as a cue that the system clock
	 * has been set initially after time synchronisation. When this happens
	 * we go through and update the global `boot_time` variable, and the
	 * `zone_boot_time` stored in each active zone (including the GZ) to
	 * correct the kstats and so that userland software can use this to
	 * obtain a more correct notion of the time that the system, and each
	 * zone, booted.
	 *
	 * To protect somewhat against a system clock being stepped multiple
	 * times forwards and backwards, either by hand or as a result of
	 * an upstream NTP server being authoratatively stuck in the past, we
	 * are only prepared to do this once per boot.
	 */
	if (already_stepped)
		return;

	adj = ts.tv_sec - hrestime.tv_sec;
	if (adj < 86400)
		return;

	already_stepped = true;

	if (boot_time < INT64_MAX - adj)
		boot_time += adj;

	zone_boottime_adjust(adj);
}

timestruc_t
tod_get(void)
{
	timestruc_t ts = { 0 };

	ASSERT(MUTEX_HELD(&tod_lock));

	return (ts);
}

/*
 * The following wrappers have been added so that locking
 * can be exported to platform-independent clock routines
 * (ie adjtime(), clock_setttime()), via a functional interface.
 */
int
hr_clock_lock(void)
{
	ushort_t s;

	CLOCK_LOCK(&s);
	return (s);
}

void
hr_clock_unlock(int s)
{
	CLOCK_UNLOCK(s);
}

void
sgmtl(time_t arg)
{
}

time_t
ggmtl(void)
{
	return (0);
}

void
rtcsync(void)
{
}
