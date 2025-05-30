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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2012 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2014, 2016 by Delphix. All rights reserved.
 * Copyright 2020 Joyent, Inc.
 * Copyright 2025 Oxide Computer Company
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/disp.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/x86_archext.h>
#include <sys/archsystm.h>
#include <sys/cpuvar.h>
#include <sys/psm_defs.h>
#include <sys/clock.h>
#include <sys/atomic.h>
#include <sys/lockstat.h>
#include <sys/smp_impldefs.h>
#include <sys/dtrace.h>
#include <sys/time.h>
#include <sys/panic.h>
#include <sys/cpu.h>
#include <sys/sdt.h>
#include <sys/comm_page.h>
#include <sys/bootconf.h>
#include <sys/kobj.h>
#include <sys/kobj_lex.h>
#include <sys/tsc.h>
#include <sys/prom_debug.h>
#include <util/qsort.h>

/*
 * Using the Pentium's TSC register for gethrtime()
 * ------------------------------------------------
 *
 * The Pentium family, like many chip architectures, has a high-resolution
 * timestamp counter ("TSC") which increments once per CPU cycle.  The contents
 * of the timestamp counter are read with the RDTSC instruction.
 *
 * As with its UltraSPARC equivalent (the %tick register), TSC's cycle count
 * must be translated into nanoseconds in order to implement gethrtime().
 * We avoid inducing floating point operations in this conversion by
 * implementing the same nsec_scale algorithm as that found in the sun4u
 * platform code.  The sun4u NATIVE_TIME_TO_NSEC_SCALE block comment contains
 * a detailed description of the algorithm; the comment is not reproduced
 * here.  This implementation differs only in its value for NSEC_SHIFT:
 * we implement an NSEC_SHIFT of 5 (instead of sun4u's 4) to allow for
 * 60 MHz Pentiums.
 *
 * While TSC and %tick are both cycle counting registers, TSC's functionality
 * falls short in several critical ways:
 *
 *  (a)	TSCs on different CPUs are not guaranteed to be in sync.  While in
 *	practice they often _are_ in sync, this isn't guaranteed by the
 *	architecture.
 *
 *  (b)	The TSC cannot be reliably set to an arbitrary value.  The architecture
 *	only supports writing the low 32-bits of TSC, making it impractical
 *	to rewrite.
 *
 *  (c)	The architecture doesn't have the capacity to interrupt based on
 *	arbitrary values of TSC; there is no TICK_CMPR equivalent.
 *
 * Together, (a) and (b) imply that software must track the skew between
 * TSCs and account for it (it is assumed that while there may exist skew,
 * there does not exist drift).  To determine the skew between CPUs, we
 * have newly onlined CPUs call tsc_sync_slave(), while the CPU performing
 * the online operation calls tsc_sync_master().
 *
 * In the absence of time-of-day clock adjustments, gethrtime() must stay in
 * sync with gettimeofday().  This is problematic; given (c), the software
 * cannot drive its time-of-day source from TSC, and yet they must somehow be
 * kept in sync.  We implement this by having a routine, tsc_tick(), which
 * is called once per second from the interrupt which drives time-of-day.
 *
 * Note that the hrtime base for gethrtime, tsc_hrtime_base, is modified
 * atomically with nsec_scale under CLOCK_LOCK.  This assures that time
 * monotonically increases.
 */

#define	NSEC_SHIFT 5

static uint_t nsec_unscale;

/*
 * These two variables used to be grouped together inside of a structure that
 * lived on a single cache line. A regression (bug ID 4623398) caused the
 * compiler to emit code that "optimized" away the while-loops below. The
 * result was that no synchronization between the onlining and onlined CPUs
 * took place.
 */
static volatile int tsc_ready;
static volatile int tsc_sync_go;

/*
 * Used as indices into the tsc_sync_snaps[] array.
 */
#define	TSC_MASTER		0
#define	TSC_SLAVE		1

/*
 * Used in the tsc_master_sync()/tsc_slave_sync() rendezvous.
 */
#define	TSC_SYNC_STOP		1
#define	TSC_SYNC_GO		2
#define	TSC_SYNC_DONE		3
#define	SYNC_ITERATIONS		10

#define	TSC_CONVERT_AND_ADD(tsc, hrt, scale) {		\
	unsigned int *_l = (unsigned int *)&(tsc);	\
	(hrt) += mul32(_l[1], scale) << NSEC_SHIFT;	\
	(hrt) += mul32(_l[0], scale) >> (32 - NSEC_SHIFT); \
}

#define	TSC_CONVERT(tsc, hrt, scale) {			\
	unsigned int *_l = (unsigned int *)&(tsc);	\
	(hrt) = mul32(_l[1], scale) << NSEC_SHIFT;	\
	(hrt) += mul32(_l[0], scale) >> (32 - NSEC_SHIFT); \
}

int tsc_master_slave_sync_needed = 1;

typedef struct tsc_sync {
	volatile hrtime_t master_tsc, slave_tsc;
} tsc_sync_t;
static tsc_sync_t *tscp;

static hrtime_t	tsc_last_jumped = 0;
static int	tsc_jumped = 0;
static uint32_t	tsc_wayback = 0;
/*
 * The cap of 1 second was chosen since it is the frequency at which the
 * tsc_tick() function runs which means that when gethrtime() is called it
 * should never be more than 1 second since tsc_last was updated.
 */
static hrtime_t tsc_resume_cap_ns = NANOSEC;	 /* 1s */

static hrtime_t	shadow_tsc_hrtime_base;
static hrtime_t	shadow_tsc_last;
static uint_t	shadow_nsec_scale;
static uint32_t	shadow_hres_lock;
int get_tsc_ready();

/*
 * Allow an operator specify an explicit TSC calibration source
 * via /etc/system e.g. `set tsc_calibration="pit"`
 */
char *tsc_calibration;

/*
 * The source that was used to calibrate the TSC. This is currently just
 * for diagnostic purposes.
 */
static tsc_calibrate_t *tsc_calibration_source;

/* The TSC frequency after calibration */
static uint64_t tsc_freq;

static inline hrtime_t
tsc_protect(hrtime_t a)
{
	if (a > tsc_resume_cap) {
		atomic_inc_32(&tsc_wayback);
		DTRACE_PROBE3(tsc__wayback, htrime_t, a, hrtime_t, tsc_last,
		    uint32_t, tsc_wayback);
		return (tsc_resume_cap);
	}
	return (a);
}

hrtime_t
tsc_gethrtime(void)
{
	uint32_t old_hres_lock;
	hrtime_t tsc, hrt;

	do {
		old_hres_lock = hres_lock;

		if ((tsc = tsc_read()) >= tsc_last) {
			/*
			 * It would seem to be obvious that this is true
			 * (that is, the past is less than the present),
			 * but it isn't true in the presence of suspend/resume
			 * cycles.  If we manage to call gethrtime()
			 * after a resume, but before the first call to
			 * tsc_tick(), we will see the jump.  In this case,
			 * we will simply use the value in TSC as the delta.
			 */
			tsc -= tsc_last;
		} else if (tsc >= tsc_last - 2*tsc_max_delta) {
			/*
			 * There is a chance that tsc_tick() has just run on
			 * another CPU, and we have drifted just enough so that
			 * we appear behind tsc_last.  In this case, force the
			 * delta to be zero.
			 */
			tsc = 0;
		} else {
			/*
			 * If we reach this else clause we assume that we have
			 * gone through a suspend/resume cycle and use the
			 * current tsc value as the delta.
			 *
			 * In rare cases we can reach this else clause due to
			 * a lack of monotonicity in the TSC value.  In such
			 * cases using the current TSC value as the delta would
			 * cause us to return a value ~2x of what it should
			 * be.  To protect against these cases we cap the
			 * suspend/resume delta at tsc_resume_cap.
			 */
			tsc = tsc_protect(tsc);
		}

		hrt = tsc_hrtime_base;

		TSC_CONVERT_AND_ADD(tsc, hrt, nsec_scale);
	} while ((old_hres_lock & ~1) != hres_lock);

	return (hrt);
}

hrtime_t
tsc_gethrtime_delta(void)
{
	uint32_t old_hres_lock;
	hrtime_t tsc, hrt;
	ulong_t flags;

	do {
		old_hres_lock = hres_lock;

		/*
		 * We need to disable interrupts here to assure that we
		 * don't migrate between the call to tsc_read() and
		 * adding the CPU's TSC tick delta. Note that disabling
		 * and reenabling preemption is forbidden here because
		 * we may be in the middle of a fast trap. In the amd64
		 * kernel we cannot tolerate preemption during a fast
		 * trap. See _update_sregs().
		 */

		flags = clear_int_flag();
		tsc = tsc_read() + tsc_sync_tick_delta[CPU->cpu_id];
		restore_int_flag(flags);

		/* See comments in tsc_gethrtime() above */

		if (tsc >= tsc_last) {
			tsc -= tsc_last;
		} else if (tsc >= tsc_last - 2 * tsc_max_delta) {
			tsc = 0;
		} else {
			tsc = tsc_protect(tsc);
		}

		hrt = tsc_hrtime_base;

		TSC_CONVERT_AND_ADD(tsc, hrt, nsec_scale);
	} while ((old_hres_lock & ~1) != hres_lock);

	return (hrt);
}

hrtime_t
tsc_gethrtime_tick_delta(void)
{
	hrtime_t hrt;
	ulong_t flags;

	flags = clear_int_flag();
	hrt = tsc_sync_tick_delta[CPU->cpu_id];
	restore_int_flag(flags);

	return (hrt);
}

/* Calculate the hrtime while exposing the parameters of that calculation. */
hrtime_t
tsc_gethrtime_params(uint64_t *tscp, uint32_t *scalep, uint8_t *shiftp)
{
	uint32_t old_hres_lock, scale;
	hrtime_t tsc, last, base;

	do {
		old_hres_lock = hres_lock;

		if (gethrtimef == tsc_gethrtime_delta) {
			ulong_t flags;

			flags = clear_int_flag();
			tsc = tsc_read() + tsc_sync_tick_delta[CPU->cpu_id];
			restore_int_flag(flags);
		} else {
			tsc = tsc_read();
		}

		last = tsc_last;
		base = tsc_hrtime_base;
		scale = nsec_scale;

	} while ((old_hres_lock & ~1) != hres_lock);

	/* See comments in tsc_gethrtime() above */
	if (tsc >= last) {
		tsc -= last;
	} else if (tsc >= last - 2 * tsc_max_delta) {
		tsc = 0;
	} else {
		tsc = tsc_protect(tsc);
	}

	TSC_CONVERT_AND_ADD(tsc, base, nsec_scale);

	if (tscp != NULL) {
		/*
		 * Do not simply communicate the delta applied to the hrtime
		 * base, but rather the effective TSC measurement.
		 */
		*tscp = tsc + last;
	}
	if (scalep != NULL) {
		*scalep = scale;
	}
	if (shiftp != NULL) {
		*shiftp = NSEC_SHIFT;
	}

	return (base);
}

/*
 * This is similar to tsc_gethrtime_delta, but it cannot actually spin on
 * hres_lock.  As a result, it caches all of the variables it needs; if the
 * variables don't change, it's done.
 */
hrtime_t
dtrace_gethrtime(void)
{
	uint32_t old_hres_lock;
	hrtime_t tsc, hrt;
	ulong_t flags;

	do {
		old_hres_lock = hres_lock;

		/*
		 * Interrupts are disabled to ensure that the thread isn't
		 * migrated between the tsc_read() and adding the CPU's
		 * TSC tick delta.
		 */
		flags = clear_int_flag();

		tsc = tsc_read();

		if (gethrtimef == tsc_gethrtime_delta)
			tsc += tsc_sync_tick_delta[CPU->cpu_id];

		restore_int_flag(flags);

		/*
		 * See the comments in tsc_gethrtime(), above.
		 */
		if (tsc >= tsc_last)
			tsc -= tsc_last;
		else if (tsc >= tsc_last - 2*tsc_max_delta)
			tsc = 0;
		else
			tsc = tsc_protect(tsc);

		hrt = tsc_hrtime_base;

		TSC_CONVERT_AND_ADD(tsc, hrt, nsec_scale);

		if ((old_hres_lock & ~1) == hres_lock)
			break;

		/*
		 * If we're here, the clock lock is locked -- or it has been
		 * unlocked and locked since we looked.  This may be due to
		 * tsc_tick() running on another CPU -- or it may be because
		 * some code path has ended up in dtrace_probe() with
		 * CLOCK_LOCK held.  We'll try to determine that we're in
		 * the former case by taking another lap if the lock has
		 * changed since when we first looked at it.
		 */
		if (old_hres_lock != hres_lock)
			continue;

		/*
		 * So the lock was and is locked.  We'll use the old data
		 * instead.
		 */
		old_hres_lock = shadow_hres_lock;

		/*
		 * Again, disable interrupts to ensure that the thread
		 * isn't migrated between the tsc_read() and adding
		 * the CPU's TSC tick delta.
		 */
		flags = clear_int_flag();

		tsc = tsc_read();

		if (gethrtimef == tsc_gethrtime_delta)
			tsc += tsc_sync_tick_delta[CPU->cpu_id];

		restore_int_flag(flags);

		/*
		 * See the comments in tsc_gethrtime(), above.
		 */
		if (tsc >= shadow_tsc_last)
			tsc -= shadow_tsc_last;
		else if (tsc >= shadow_tsc_last - 2 * tsc_max_delta)
			tsc = 0;
		else
			tsc = tsc_protect(tsc);

		hrt = shadow_tsc_hrtime_base;

		TSC_CONVERT_AND_ADD(tsc, hrt, shadow_nsec_scale);
	} while ((old_hres_lock & ~1) != shadow_hres_lock);

	return (hrt);
}

hrtime_t
tsc_gethrtimeunscaled(void)
{
	uint32_t old_hres_lock;
	hrtime_t tsc;

	do {
		old_hres_lock = hres_lock;

		/* See tsc_tick(). */
		tsc = tsc_read() + tsc_last_jumped;
	} while ((old_hres_lock & ~1) != hres_lock);

	return (tsc);
}

/*
 * Convert a nanosecond based timestamp to tsc
 */
uint64_t
tsc_unscalehrtime(hrtime_t nsec)
{
	hrtime_t tsc;

	if (tsc_gethrtime_enable) {
		TSC_CONVERT(nsec, tsc, nsec_unscale);
		return (tsc);
	}
	return ((uint64_t)nsec);
}

/* Convert a tsc timestamp to nanoseconds */
void
tsc_scalehrtime(hrtime_t *tsc)
{
	hrtime_t hrt;
	hrtime_t mytsc;

	if (tsc == NULL)
		return;
	mytsc = *tsc;

	TSC_CONVERT(mytsc, hrt, nsec_scale);
	*tsc  = hrt;
}

hrtime_t
tsc_gethrtimeunscaled_delta(void)
{
	hrtime_t hrt;
	ulong_t flags;

	/*
	 * Similarly to tsc_gethrtime_delta, we need to disable preemption
	 * to prevent migration between the call to tsc_gethrtimeunscaled
	 * and adding the CPU's hrtime delta. Note that disabling and
	 * reenabling preemption is forbidden here because we may be in the
	 * middle of a fast trap. In the amd64 kernel we cannot tolerate
	 * preemption during a fast trap. See _update_sregs().
	 */

	flags = clear_int_flag();
	hrt = tsc_gethrtimeunscaled() + tsc_sync_tick_delta[CPU->cpu_id];
	restore_int_flag(flags);

	return (hrt);
}

/*
 * TSC Sync Master
 *
 * Typically called on the boot CPU, this attempts to quantify TSC skew between
 * different CPUs.  If an appreciable difference is found, gethrtimef will be
 * changed to point to tsc_gethrtime_delta().
 *
 * Calculating skews is precise only when the master and slave TSCs are read
 * simultaneously; however, there is no algorithm that can read both CPUs in
 * perfect simultaneity.  The proposed algorithm is an approximate method based
 * on the behaviour of cache management.  The slave CPU continuously polls the
 * TSC while reading a global variable updated by the master CPU.  The latest
 * TSC reading is saved when the master's update (forced via mfence) reaches
 * visibility on the slave.  The master will also take a TSC reading
 * immediately following the mfence.
 *
 * While the delay between cache line invalidation on the slave and mfence
 * completion on the master is not repeatable, the error is heuristically
 * assumed to be 1/4th of the write time recorded by the master.  Multiple
 * samples are taken to control for the variance caused by external factors
 * such as bus contention.  Each sample set is independent per-CPU to control
 * for differing memory latency on NUMA systems.
 *
 * TSC sync is disabled in the context of virtualization because the CPUs
 * assigned to the guest are virtual CPUs which means the real CPUs on which
 * guest runs keep changing during life time of guest OS. So we would end up
 * calculating TSC skews for a set of CPUs during boot whereas the guest
 * might migrate to a different set of physical CPUs at a later point of
 * time.
 */
void
tsc_sync_master(processorid_t slave)
{
	ulong_t flags, source, min_write_time = ~0UL;
	hrtime_t write_time, mtsc_after, last_delta = 0;
	tsc_sync_t *tsc = tscp;
	int cnt;
	int hwtype;

	hwtype = get_hwenv();
	if (!tsc_master_slave_sync_needed || (hwtype & HW_VIRTUAL) != 0)
		return;

	flags = clear_int_flag();
	source = CPU->cpu_id;

	for (cnt = 0; cnt < SYNC_ITERATIONS; cnt++) {
		while (tsc_sync_go != TSC_SYNC_GO)
			SMT_PAUSE();

		tsc->master_tsc = tsc_read();
		membar_enter();
		mtsc_after = tsc_read();
		while (tsc_sync_go != TSC_SYNC_DONE)
			SMT_PAUSE();
		write_time =  mtsc_after - tsc->master_tsc;
		if (write_time <= min_write_time) {
			hrtime_t tdelta;

			tdelta = tsc->slave_tsc - mtsc_after;
			if (tdelta < 0)
				tdelta = -tdelta;
			/*
			 * If the margin exists, subtract 1/4th of the measured
			 * write time from the master's TSC value.  This is an
			 * estimate of how late the mfence completion came
			 * after the slave noticed the cache line change.
			 */
			if (tdelta > (write_time/4)) {
				tdelta = tsc->slave_tsc -
				    (mtsc_after - (write_time/4));
			} else {
				tdelta = tsc->slave_tsc - mtsc_after;
			}
			last_delta = tsc_sync_tick_delta[source] - tdelta;
			tsc_sync_tick_delta[slave] = last_delta;
			min_write_time = write_time;
		}

		tsc->master_tsc = tsc->slave_tsc = write_time = 0;
		membar_enter();
		tsc_sync_go = TSC_SYNC_STOP;
	}

	/*
	 * Only enable the delta variants of the TSC functions if the measured
	 * skew is greater than the fastest write time.
	 */
	last_delta = (last_delta < 0) ? -last_delta : last_delta;
	if (last_delta > min_write_time) {
		gethrtimef = tsc_gethrtime_delta;
		gethrtimeunscaledf = tsc_gethrtimeunscaled_delta;
		tsc_ncpu = NCPU;
	}
	restore_int_flag(flags);
}

/*
 * TSC Sync Slave
 *
 * Called by a CPU which has just been onlined.  It is expected that the CPU
 * performing the online operation will call tsc_sync_master().
 *
 * Like tsc_sync_master, this logic is skipped on virtualized platforms.
 */
void
tsc_sync_slave(void)
{
	ulong_t flags;
	hrtime_t s1;
	tsc_sync_t *tsc = tscp;
	int cnt;
	int hwtype;

	hwtype = get_hwenv();
	if (!tsc_master_slave_sync_needed || (hwtype & HW_VIRTUAL) != 0)
		return;

	flags = clear_int_flag();

	for (cnt = 0; cnt < SYNC_ITERATIONS; cnt++) {
		/* Re-fill the cache line */
		s1 = tsc->master_tsc;
		membar_enter();
		tsc_sync_go = TSC_SYNC_GO;
		do {
			/*
			 * Do not put an SMT_PAUSE here.  If the master and
			 * slave are the same hyper-threaded CPU, we want the
			 * master to yield as quickly as possible to the slave.
			 */
			s1 = tsc_read();
		} while (tsc->master_tsc == 0);
		tsc->slave_tsc = s1;
		membar_enter();
		tsc_sync_go = TSC_SYNC_DONE;

		while (tsc_sync_go != TSC_SYNC_STOP)
			SMT_PAUSE();
	}

	restore_int_flag(flags);
}

/*
 * Called once per second on a CPU from the cyclic subsystem's
 * CY_HIGH_LEVEL interrupt.  (No longer just cpu0-only)
 */
void
tsc_tick(void)
{
	hrtime_t now, delta;
	ushort_t spl;

	/*
	 * Before we set the new variables, we set the shadow values.  This
	 * allows for lock free operation in dtrace_gethrtime().
	 */
	lock_set_spl((lock_t *)&shadow_hres_lock + HRES_LOCK_OFFSET,
	    ipltospl(CBE_HIGH_PIL), &spl);

	shadow_tsc_hrtime_base = tsc_hrtime_base;
	shadow_tsc_last = tsc_last;
	shadow_nsec_scale = nsec_scale;

	shadow_hres_lock++;
	splx(spl);

	CLOCK_LOCK(&spl);

	now = tsc_read();

	if (gethrtimef == tsc_gethrtime_delta)
		now += tsc_sync_tick_delta[CPU->cpu_id];

	if (now < tsc_last) {
		/*
		 * The TSC has just jumped into the past.  We assume that
		 * this is due to a suspend/resume cycle, and we're going
		 * to use the _current_ value of TSC as the delta.  This
		 * will keep tsc_hrtime_base correct.  We're also going to
		 * assume that rate of tsc does not change after a suspend
		 * resume (i.e nsec_scale remains the same).
		 */
		delta = now;
		delta = tsc_protect(delta);
		tsc_last_jumped += tsc_last;
		tsc_jumped = 1;
	} else {
		/*
		 * Determine the number of TSC ticks since the last clock
		 * tick, and add that to the hrtime base.
		 */
		delta = now - tsc_last;
	}

	TSC_CONVERT_AND_ADD(delta, tsc_hrtime_base, nsec_scale);
	tsc_last = now;

	CLOCK_UNLOCK(spl);
}

void
tsc_hrtimeinit(uint64_t cpu_freq_hz)
{
	extern int gethrtime_hires;
	longlong_t tsc;
	ulong_t flags;

	/*
	 * cpu_freq_hz is the measured cpu frequency in hertz
	 */

	/*
	 * We can't accommodate CPUs slower than 31.25 MHz.
	 */
	ASSERT(cpu_freq_hz > NANOSEC / (1 << NSEC_SHIFT));
	nsec_scale =
	    (uint_t)(((uint64_t)NANOSEC << (32 - NSEC_SHIFT)) / cpu_freq_hz);
	nsec_unscale =
	    (uint_t)(((uint64_t)cpu_freq_hz << (32 - NSEC_SHIFT)) / NANOSEC);

	flags = clear_int_flag();
	tsc = tsc_read();
	(void) tsc_gethrtime();
	tsc_max_delta = tsc_read() - tsc;
	restore_int_flag(flags);
	gethrtimef = tsc_gethrtime;
	gethrtimeunscaledf = tsc_gethrtimeunscaled;
	scalehrtimef = tsc_scalehrtime;
	unscalehrtimef = tsc_unscalehrtime;
	hrtime_tick = tsc_tick;
	gethrtime_hires = 1;
	/*
	 * Being part of the comm page, tsc_ncpu communicates the published
	 * length of the tsc_sync_tick_delta array.  This is kept zeroed to
	 * ignore the absent delta data while the TSCs are synced.
	 */
	tsc_ncpu = 0;
	/*
	 * Allocate memory for the structure used in the tsc sync logic.
	 * This structure should be aligned on a multiple of cache line size.
	 */
	tscp = kmem_zalloc(PAGESIZE, KM_SLEEP);

	/*
	 * Convert the TSC resume cap ns value into its unscaled TSC value.
	 * See tsc_gethrtime().
	 */
	if (tsc_resume_cap == 0)
		TSC_CONVERT(tsc_resume_cap_ns, tsc_resume_cap, nsec_unscale);
}

int
get_tsc_ready()
{
	return (tsc_ready);
}

/*
 * Adjust all the deltas by adding the passed value to the array and activate
 * the "delta" versions of the gethrtime functions.  It is possible that the
 * adjustment could be negative.  Such may occur if the SunOS instance was
 * moved by a virtual manager to a machine with a higher value of TSC.
 */
void
tsc_adjust_delta(hrtime_t tdelta)
{
	int		i;

	for (i = 0; i < NCPU; i++) {
		tsc_sync_tick_delta[i] += tdelta;
	}

	gethrtimef = tsc_gethrtime_delta;
	gethrtimeunscaledf = tsc_gethrtimeunscaled_delta;
	tsc_ncpu = NCPU;
}

/*
 * Suspend/resume is not supported on this architecture so we do not implement
 * TSC functions for it.  However, this variable is referenced ifndef sparc
 * (sigh) on common/cpr/cpr_main.c so it has to exist.  It doesn't control
 * anything and should go away.
 */
int		tsc_resume_in_cyclic = 0;

static int
tsc_calibrate_cmp(const void *a, const void *b)
{
	const tsc_calibrate_t * const *a1 = a;
	const tsc_calibrate_t * const *b1 = b;
	const tsc_calibrate_t *l = *a1;
	const tsc_calibrate_t *r = *b1;

	/* Sort from highest preference to lowest preference */
	if (l->tscc_preference > r->tscc_preference)
		return (-1);
	if (l->tscc_preference < r->tscc_preference)
		return (1);

	/* For equal preference sources, sort alphabetically */
	int c = strcmp(l->tscc_source, r->tscc_source);

	if (c < 0)
		return (-1);
	if (c > 0)
		return (1);
	return (0);
}

SET_DECLARE(tsc_calibration_set, tsc_calibrate_t);

static tsc_calibrate_t *
tsc_calibrate_get_force(const char *source)
{
	tsc_calibrate_t **tsccpp;

	VERIFY3P(source, !=, NULL);

	SET_FOREACH(tsccpp, tsc_calibration_set) {
		tsc_calibrate_t *tsccp = *tsccpp;

		if (strcasecmp(source, tsccp->tscc_source) == 0)
			return (tsccp);
	}

	/*
	 * If an operator explicitly gave a TSC value and we didn't find it,
	 * we should let them know.
	 */
	cmn_err(CE_NOTE,
	    "Explicit TSC calibration source '%s' not found; using default",
	    source);

	return (NULL);
}

/*
 * As described in tscc_pit.c, as an intertim measure as we transition to
 * alternate calibration sources besides the PIT, we still want to gather
 * what the values would have been had we used the PIT. Therefore, if we're
 * using a source other than the PIT, we explicitly run the PIT calibration
 * which will store the TSC frequency as measured by the PIT for the
 * benefit of the APIC code (as well as any potential diagnostics).
 */
static void
tsc_pit_also(void)
{
	tsc_calibrate_t *pit = tsc_calibrate_get_force("PIT");
	uint64_t dummy;

	/* We should always have the PIT as a possible calibration source */
	VERIFY3P(pit, !=, NULL);

	/* If we used the PIT to calibrate, we don't need to run again */
	if (tsc_calibration_source == pit)
		return;

	/*
	 * Since we're not using the PIT as the actual TSC calibration source,
	 * we don't care about the results or saving the result -- tscc_pit.c
	 * saves the frequency in a global for the benefit of the APIC code.
	 */
	(void) pit->tscc_calibrate(&dummy);
}

uint64_t
tsc_calibrate(void)
{
	tsc_calibrate_t **tsccpp, *force;
	size_t tsc_set_size;
	int tsc_name_len;

	/*
	 * Every x86 system since the Pentium has TSC support. Since we
	 * only support 64-bit x86 systems, there should always be a TSC
	 * present, and something's horribly wrong if it's missing.
	 */
	if (!is_x86_feature(x86_featureset, X86FSET_TSC))
		panic("System does not have TSC support");

	/*
	 * If we already successfully calibrated the TSC, no need to do
	 * it again.
	 */
	if (tsc_freq > 0)
		return (tsc_freq);

	PRM_POINT("Calibrating the TSC...");

	/*
	 * Allow an operator to explicitly specify a calibration source via
	 * `set tsc_calibration=foo` in the bootloader or
	 * `set tsc_calibration="foo"` in /etc/system (preferring a bootloader
	 * supplied value over /etc/system).
	 *
	 * If no source is given, or the specified source is not found, we
	 * fallback to trying all of the known sources in order by preference
	 * (high preference value to low preference value) until one succeeds.
	 */
	tsc_name_len = BOP_GETPROPLEN(bootops, "tsc_calibration");
	if (tsc_name_len > 0) {
		/* Overwrite any /etc/system supplied value */
		if (tsc_calibration != NULL) {
			size_t len = strlen(tsc_calibration) + 1;

			kobj_free_string(tsc_calibration, len);
		}

		tsc_calibration = kmem_zalloc(tsc_name_len + 1, KM_SLEEP);
		BOP_GETPROP(bootops, "tsc_calibration", tsc_calibration);
	}

	if (tsc_calibration != NULL &&
	    (force = tsc_calibrate_get_force(tsc_calibration)) != NULL) {
		if (tsc_name_len > 0) {
			PRM_POINT("Forcing bootloader specified TSC calibration"
			    " source");
		} else {
			PRM_POINT("Forcing /etc/system specified TSC "
			    "calibration source");
		}
		PRM_DEBUGS(force->tscc_source);

		if (!force->tscc_calibrate(&tsc_freq))
			panic("Failed to calibrate the TSC");

		tsc_calibration_source = force;

		/*
		 * We've saved the tsc_calibration_t that matched the value
		 * of tsc_calibration at this point, so we can release the
		 * memory for the value now.
		 */
		if (tsc_name_len > 0) {
			kmem_free(tsc_calibration, tsc_name_len + 1);
		} else if (tsc_calibration != NULL) {
			size_t len = strlen(tsc_calibration) + 1;

			kobj_free_string(tsc_calibration, len);
		}
		tsc_calibration = NULL;

		tsc_pit_also();
		return (tsc_freq);
	}

	/*
	 * While we could sort the set contents in place, we'll make a copy
	 * of the set and avoid modifying the original set.
	 */
	tsc_set_size = SET_COUNT(tsc_calibration_set) *
	    sizeof (tsc_calibrate_t **);
	tsccpp = kmem_zalloc(tsc_set_size, KM_SLEEP);
	bcopy(SET_BEGIN(tsc_calibration_set), tsccpp, tsc_set_size);

	/*
	 * Sort by preference, highest to lowest
	 */
	qsort(tsccpp, SET_COUNT(tsc_calibration_set),
	    sizeof (tsc_calibrate_t **), tsc_calibrate_cmp);

	for (uint_t i = 0; i < SET_COUNT(tsc_calibration_set); i++) {
		PRM_DEBUGS(tsccpp[i]->tscc_source);
		if (tsccpp[i]->tscc_calibrate(&tsc_freq)) {
			VERIFY3U(tsc_freq, >, 0);

			cmn_err(CE_CONT,
			    "?TSC calibrated using %s; freq is %lu MHz\n",
			    tsccpp[i]->tscc_source, tsc_freq / 1000000);

			/*
			 * Note that tsccpp is just a (sorted) array of
			 * pointers to the tsc_calibration_t's (from the
			 * linker set). The actual tsc_calibration_t's aren't
			 * kmem_alloc()ed (being part of the linker set), so
			 * it's safe to keep a pointer to the one that was
			 * used for calibration (intended for diagnostic
			 * purposes).
			 */
			tsc_calibration_source = tsccpp[i];

			kmem_free(tsccpp, tsc_set_size);
			tsc_pit_also();
			return (tsc_freq);
		}
	}

	/*
	 * In case it's useful, we don't free tsccpp -- we're about to panic
	 * anyway.
	 */
	panic("Failed to calibrate TSC");
}

uint64_t
tsc_get_freq(void)
{
	VERIFY(tsc_freq > 0);
	return (tsc_freq);
}

void
eb_pausems(uint64_t delay_ms)
{
	const hrtime_t delay_ns = MSEC2NSEC(delay_ms);
	extern int gethrtime_hires;

	if (gethrtime_hires != 0) {
		/* The TSC is calibrated, we can use drv_usecwait() */
		drv_usecwait(NSEC2USEC(delay_ns));
	} else {
		/*
		 * The TSC has not yet been calibrated so assume its frequency
		 * is 2GHz (2 ticks per nanosecond). This is approximately
		 * correct for Gimlet and should be the right order of magnitude
		 * for future platforms. This delay does not have be accurate
		 * and is only used very early in boot.
		 */
		const hrtime_t start = tsc_read();
		while (tsc_read() < start + (delay_ns << 1))
			SMT_PAUSE();
	}
}
