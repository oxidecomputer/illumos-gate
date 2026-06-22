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

/*
 * Per-CPU effective/average frequency monitor.
 *
 * The APERF and MPERF MSRs form the hardware co-ordination feedback mechanism.
 * While a core is active (in C0), MPERF increments at the invariant TSC / P0
 * frequency and APERF increments at the actual delivered frequency. Both freeze
 * while the core is halted in a C-state. Over an interval their ratio is the
 * delivered frequency relative to the base (TSC) clock, boost included. APERF
 * over elapsed wall-clock time (which keeps advancing through idle) is a
 * measure of delivered work reporting the cycles actually executed per second
 * of real time, counting halted time as 0 Hz.
 *
 * Reading these MSRs requires executing on the logical CPU that owns them.
 * Rather than forcing a periodic interrupt onto every CPU to do so we sample
 * opportunistically from events the CPU reaches on its own.
 * cpufreq_mon_sample() is called from the context-switch path, as a thread is
 * resumed on the CPU, and from the hardware interrupt path. Sampling at the
 * switch covers the transition into idle, which is itself a switch to the idle
 * thread, as well as ordinary thread switches. The interrupt hook additionally
 * covers a CPU running a single thread that never yields. The counters are
 * cumulative, so we only need the two readings that bracket a sampling window.
 * Each call does a single timestamp comparison, and only the first one past
 * the interval reads the MSRs and republishes.
 *
 * The sample can run in interrupt context, so it must be interrupt-safe. The
 * per-CPU state has a single writer (this core), and its brief critical
 * section runs with interrupts masked so a nested interrupt cannot re-enter
 * it. The published values are plain aligned 64-bit stores read without a
 * lock, so a reader may see fields from two adjacent windows; that should
 * not present a problem.
 *
 * The remaining blind spot is a CPU that never context-switches AND takes no
 * interrupts, a single thread pinned to an interrupt-isolated CPU. That is
 * presumably a CPU we must not perturb, so we leave it be. The published
 * "snapshot_hrtime" records when each CPU was last refreshed so the staleness
 * is visible.
 *
 * A CPU with an APIC ID >= 256 is not a special case here, even without an
 * IOMMU interrupt-remapping driver. The context-switch path covers it, and
 * IPIs, cross-calls and the local timer reach it directly in x2apic mode
 * without remapping. It just takes no device interrupts, a subset of its
 * sampling opportunities.
 *
 * The base is cpu_freq_hz, the measured (invariant) TSC frequency, the rate at
 * which MPERF counts. We do not use cpu_curr_clock since on platforms that
 * manage P-states the latter tracks the requested P-state rather than that
 * fixed rate.
 *
 * APERF and MPERF are 64-bit free-running counters. At any real delivered
 * frequency they take millennia to wrap, and a sampling window spans well under
 * a second, so true rollover is not a concern. The discontinuity we do guard
 * against is a counter being reset to zero from under us (turbo.c historically
 * did this; firmware or a future consumer might). We treat them as a read-only
 * shared resource, and as belt and braces an interval whose counters appear to
 * have moved backwards is discarded and the baseline re-taken, so we degrade to
 * a skipped sample rather than report a bogus frequency.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/cpuvar.h>
#include <sys/kstat.h>
#include <sys/time.h>
#include <sys/stdbool.h>
#include <sys/x86_archext.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/cpufreq_mon.h>

/*
 * Minimum interval between published samples, in nanoseconds. Tunable via
 * /etc/system. A value of zero disables the monitor.
 */
hrtime_t cpufreq_mon_interval = NANOSEC;		/* 1s */

/*
 * The same interval expressed in unscaled (gethrtime_unscaled) units, so the
 * hot-path comparison needs no scaling. Computed once when the monitor starts.
 */
static hrtime_t cpufreq_mon_interval_unscaled;

/*
 * Per-CPU published kstat. The sampling path writes these with plain aligned
 * stores.
 */
typedef struct cpufreq_kstat {
	kstat_named_t	ck_effective_hz;
	kstat_named_t	ck_average_hz;
	kstat_named_t	ck_base_hz;
	kstat_named_t	ck_aperf_delta;
	kstat_named_t	ck_mperf_delta;
	kstat_named_t	ck_interval_ns;
	kstat_named_t	ck_snapshot_hrtime;
} cpufreq_kstat_t;

/*
 * Per-CPU sampling state. There is a single writer per logical CPU (its own
 * context-switch and interrupt paths, mutually excluded by masking interrupts),
 * so no lock is needed.
 */
typedef struct cpufreq_mon_cpu {
	kstat_t		*cfm_ksp;	/* this CPU's kstat */
	uint64_t	cfm_base_hz;	/* invariant base (TSC) frequency */
	uint64_t	cfm_aperf;	/* APERF at window start */
	uint64_t	cfm_mperf;	/* MPERF at window start */
	hrtime_t	cfm_time;	/* window start (unscaled) */
	bool		cfm_primed;	/* baseline established? */
} cpufreq_mon_cpu_t;

/*
 * Per-CPU state, indexed by cpu_seqid. NULL until the monitor is enabled,
 * which is also how the sample path knows there is nothing to do.
 */
static cpufreq_mon_cpu_t **cpufreq_mon_state;

static int
cpufreq_mon_kstat_update(kstat_t *ksp __unused, int rw)
{
	if (rw == KSTAT_WRITE)
		return (EACCES);
	return (0);
}

/*
 * Sample this CPU's APERF/MPERF counters and, once the interval has elapsed,
 * publish the derived frequencies. Called on-core from the context-switch path
 * as a thread is resumed and from the interrupt dispatchers (do_interrupt() and
 * apix_do_interrupt()) as they service an interrupt.
 */
void
cpufreq_mon_sample(void)
{
	cpufreq_mon_cpu_t **state = cpufreq_mon_state;
	cpufreq_mon_cpu_t *cfm;
	cpufreq_kstat_t *cfk;
	uint64_t aperf, mperf, da, dm, avg, eff = 0;
	hrtime_t now, dt, dt_ns, snap;
	ulong_t iflag;
	bool have_eff = false;

	if (state == NULL)
		return;
	cfm = state[CPU->cpu_seqid];
	if (cfm == NULL)
		return;

	/*
	 * Fast path: not enough time has elapsed to take a fresh sample, so
	 * all we do is read the cycle counter and compare. gethrtime_unscaled()
	 * is the raw TSC, avoiding the scaling gethrtime() does on every call.
	 */
	now = gethrtime_unscaled();
	if (cfm->cfm_primed &&
	    (now - cfm->cfm_time) < cpufreq_mon_interval_unscaled) {
		return;
	}

	/*
	 * Time to sample. We may be in interrupt context, so rather than take
	 * a lock we mask interrupts for the brief critical section. That keeps
	 * the rdmsr pair consistent and stops a nested interrupt from
	 * re-entering mid-update. Re-check the interval under the mask in case
	 * a nested sample beat us to it.
	 */
	iflag = intr_clear();
	now = gethrtime_unscaled();
	if (cfm->cfm_primed &&
	    (now - cfm->cfm_time) < cpufreq_mon_interval_unscaled) {
		intr_restore(iflag);
		return;
	}

	aperf = rdmsr(MSR_APERF);
	mperf = rdmsr(MSR_MPERF);

	if (!cfm->cfm_primed)
		goto baseline;

	/*
	 * A counter that moved backwards was zeroed out from under us.
	 * Drop this interval and re-establish a baseline.
	 */
	if (aperf < cfm->cfm_aperf || mperf < cfm->cfm_mperf)
		goto baseline;

	da = aperf - cfm->cfm_aperf;
	dm = mperf - cfm->cfm_mperf;
	dt = now - cfm->cfm_time;
	if (dt <= 0)
		goto baseline;

	/*
	 * average_Hz: APERF cycles over elapsed wall-clock time - the cycles
	 * actually executed per second of real time, so it measures delivered
	 * work rather than a clock speed and counts idle as 0 Hz. For a large
	 * cycle count (a long window) scale it and the interval down together
	 * so the NANOSEC multiply stays within 64 bits while preserving the
	 * ratio, at a small loss of precision.
	 */
	dt_ns = dt;
	scalehrtime(&dt_ns);
	if (dt_ns <= 0)
		goto baseline;
	{
		uint64_t sda = da;
		uint64_t sdt = (uint64_t)dt_ns;

		while (sda >= (1ULL << 33)) {
			sda >>= 1;
			sdt >>= 1;
		}
		avg = (sdt != 0) ? sda * NANOSEC / sdt : 0;
	}

	/*
	 * effective_Hz: base * APERF / MPERF, the clock delivered while the
	 * core was executing (C0). Scale both deltas down together so the base
	 * multiply cannot overflow 64 bits. A wholly idle interval (dm == 0)
	 * leaves the effective frequency undefined, so we keep the previously
	 * published value.
	 */
	if (dm != 0) {
		uint64_t sa = da;
		uint64_t sm = dm;

		while (sa >= (1ULL << 30) || sm >= (1ULL << 30)) {
			sa >>= 1;
			sm >>= 1;
		}
		if (sm != 0) {
			eff = cfm->cfm_base_hz * sa / sm;
			have_eff = true;
		}
	}

	/*
	 * Publish. snapshot_hrtime is the scaled (gethrtime) time of this
	 * sample, so a consumer can compare it against the kstat snapshot time.
	 */
	snap = now;
	scalehrtime(&snap);
	cfk = (cfm->cfm_ksp != NULL) ? cfm->cfm_ksp->ks_data : NULL;
	if (cfk != NULL) {
		cfk->ck_average_hz.value.ui64 = avg;
		cfk->ck_aperf_delta.value.ui64 = da;
		cfk->ck_mperf_delta.value.ui64 = dm;
		cfk->ck_interval_ns.value.ui64 = (uint64_t)dt_ns;
		cfk->ck_snapshot_hrtime.value.ui64 = (uint64_t)snap;
		if (have_eff)
			cfk->ck_effective_hz.value.ui64 = eff;
	}

baseline:
	cfm->cfm_aperf = aperf;
	cfm->cfm_mperf = mperf;
	cfm->cfm_time = now;
	cfm->cfm_primed = true;
	intr_restore(iflag);
}

static void
cpufreq_mon_cpu_create(cpu_t *cp)
{
	cpufreq_mon_cpu_t *cfm;
	kstat_t *ksp;

	ASSERT(MUTEX_HELD(&cpu_lock));

	if (cpufreq_mon_state[cp->cpu_seqid] != NULL)
		return;

	cfm = kmem_zalloc(sizeof (*cfm), KM_SLEEP);
	cfm->cfm_base_hz = cpu_freq_hz;

	ksp = kstat_create("cpufreq", cp->cpu_id, "cpufreq", "misc",
	    KSTAT_TYPE_NAMED,
	    sizeof (cpufreq_kstat_t) / sizeof (kstat_named_t), 0);
	if (ksp != NULL) {
		cpufreq_kstat_t *cfk = ksp->ks_data;

		kstat_named_init(&cfk->ck_effective_hz,
		    "effective_Hz", KSTAT_DATA_UINT64);
		kstat_named_init(&cfk->ck_average_hz,
		    "average_Hz", KSTAT_DATA_UINT64);
		kstat_named_init(&cfk->ck_base_hz,
		    "base_Hz", KSTAT_DATA_UINT64);
		kstat_named_init(&cfk->ck_aperf_delta,
		    "aperf_delta", KSTAT_DATA_UINT64);
		kstat_named_init(&cfk->ck_mperf_delta,
		    "mperf_delta", KSTAT_DATA_UINT64);
		kstat_named_init(&cfk->ck_interval_ns,
		    "interval_ns", KSTAT_DATA_UINT64);
		kstat_named_init(&cfk->ck_snapshot_hrtime,
		    "snapshot_hrtime", KSTAT_DATA_UINT64);
		cfk->ck_base_hz.value.ui64 = cfm->cfm_base_hz;

		ksp->ks_private = cfm;
		ksp->ks_update = cpufreq_mon_kstat_update;
		kstat_install(ksp);
		cfm->cfm_ksp = ksp;
	}

	cpufreq_mon_state[cp->cpu_seqid] = cfm;
}

static void
cpufreq_mon_cpu_destroy(cpu_t *cp)
{
	cpufreq_mon_cpu_t *cfm;

	ASSERT(MUTEX_HELD(&cpu_lock));

	cfm = cpufreq_mon_state[cp->cpu_seqid];
	if (cfm == NULL)
		return;
	cpufreq_mon_state[cp->cpu_seqid] = NULL;

	if (cfm->cfm_ksp != NULL)
		kstat_delete(cfm->cfm_ksp);
	kmem_free(cfm, sizeof (*cfm));
}

static int
cpufreq_mon_cpu_setup(cpu_setup_t what, int id, void *arg __unused)
{
	cpu_t *cp = cpu_get((processorid_t)id);

	if (cp == NULL)
		return (0);

	switch (what) {
	case CPU_CONFIG:
		cpufreq_mon_cpu_create(cp);
		break;
	case CPU_UNCONFIG:
		cpufreq_mon_cpu_destroy(cp);
		break;
	default:
		break;
	}

	return (0);
}

/*
 * Enable the per-CPU frequency monitor. Called once, after all CPUs are online
 * and with cpu_lock not held. We set up the CPUs that are already present and
 * register a callback to track any that come or go later. After this the sample
 * path begins publishing.
 */
void
cpufreq_mon_init(void)
{
	cpufreq_mon_cpu_t **state;
	cpu_t *cp;

	ASSERT(!MUTEX_HELD(&cpu_lock));

	if (cpufreq_mon_interval <= 0)
		return;
	if (!is_x86_feature(x86_featureset, X86FSET_EFF_FREQ_IF))
		return;

	cpufreq_mon_interval_unscaled = unscalehrtime(cpufreq_mon_interval);

	state = kmem_zalloc(max_ncpus * sizeof (cpufreq_mon_cpu_t *), KM_SLEEP);

	mutex_enter(&cpu_lock);
	cpufreq_mon_state = state;
	register_cpu_setup_func(cpufreq_mon_cpu_setup, NULL);

	cp = cpu_list;
	do {
		cpufreq_mon_cpu_create(cp);
		cp = cp->cpu_next;
	} while (cp != cpu_list);
	mutex_exit(&cpu_lock);
}
