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
 */
/*
 * Copyright (c) 2009, Intel Corporation.
 * All rights reserved.
 * Copyright 2025 Oxide Computer Company
 */

#include <sys/cpu_pm.h>
#include <sys/x86_archext.h>
#include <sys/sdt.h>
#include <sys/spl.h>
#include <sys/machsystm.h>
#include <sys/archsystm.h>
#include <sys/cpupm.h>
#include <sys/cpu_idle.h>
#include <sys/cpupm_oxide.h>
#include <sys/dtrace.h>
#include <sys/note.h>

/*
 * This callback is used to build the PPM CPU domains once
 * a CPU device has been started. The callback is initialized
 * by the PPM driver to point to a routine that will build the
 * domains.
 */
void (*cpupm_ppm_alloc_pstate_domains)(cpu_t *);

/*
 * This callback is used to remove CPU from the PPM CPU domains
 * when the cpu driver is detached. The callback is initialized
 * by the PPM driver to point to a routine that will remove CPU
 * from the domains.
 */
void (*cpupm_ppm_free_pstate_domains)(cpu_t *);

/*
 * This callback is used to redefine the topspeed for a CPU device.
 * Since all CPUs in a domain should have identical properties, this
 * callback is initialized by the PPM driver to point to a routine
 * that will redefine the topspeed for all devices in a CPU domain.
 *
 * This callback will never actually be executed on Oxide, since we don't have
 * ACPI let alone _PPC notifications, but it's included to satisfy the PPM
 * driver's symbol reference.
 */
void (*cpupm_redefine_topspeed)(void *);

/*
 * These callbacks are used by the PPM driver to call into the CPU driver. It is
 * unlikely these are actually ever used, as on Oxide they are only reachable
 * through ppm_ioctl with PPM{GET,SET}_NORMAL "for test purposes".
 *
 * Regardless, the interface exists, so these need to exist. Whether they need
 * to do what they say is another question.
 */
void (*cpupm_set_topspeed_callb)(void *, int);
int (*cpupm_get_topspeed_callb)(void *);

static void cpupm_init_top_speed(void *);

/*
 * Until proven otherwise, all power states are manageable.
 */
static uint32_t cpupm_enabled = CPUPM_ALL_STATES;

cpupm_state_domains_t *cpupm_pstate_domains = NULL;
cpupm_state_domains_t *cpupm_cstate_domains = NULL;

/*
 * c-state tunables
 *
 * cpupm_cs_sample_interval is the length of time we wait before
 * recalculating c-state statistics.  When a CPU goes idle it checks
 * to see if it has been longer than cpupm_cs_sample_interval since it last
 * caculated which C-state to go to.
 *
 * cpupm_cs_idle_cost_tunable is the ratio of time CPU spends executing + idle
 * divided by time spent in the idle state transitions.
 * A value of 10 means the CPU will not spend more than 1/10 of its time
 * in idle latency.  The worst case performance will be 90% of non Deep C-state
 * kernel.
 *
 * cpupm_cs_idle_save_tunable is how long we must stay in a deeper C-state
 * before it is worth going there.  Expressed as a multiple of latency.
 *
 * cpupm_C6_idle_pct_tunable the minimum percentage of the last
 * cpupm_cs_sample_interval that must be idle to consider C6 or deeper idle
 * states. This is inherited from i86pc's C2 threshold, since ACPI C2 and the
 * current (default) configuration for Zen C6 are similar. When picking a value
 * for this tunable, one consideration is that the cost and save tunables do not
 * account for other effects like L1/L2/L3 cache flushes that come with deeper
 * power states.
 */
uint32_t cpupm_cs_sample_interval = 100*1000*1000;	/* 100 milliseconds */
uint32_t cpupm_cs_idle_cost_tunable = 10;	/* work time / latency cost */
uint32_t cpupm_cs_idle_save_tunable = 2;	/* idle power savings */
uint16_t cpupm_C6_idle_pct_tunable = 70;

extern boolean_t cpupm_intel_init(cpu_t *);
extern boolean_t cpupm_amd_init(cpu_t *);

typedef struct cpupm_vendor {
	boolean_t	(*cpuv_init)(cpu_t *);
} cpupm_vendor_t;

/*
 * Table of supported vendors.
 */
static cpupm_vendor_t cpupm_vendors[] = {
	cpupm_amd_init,
	NULL
};

/*
 * Initialize the machine.
 * See if a module exists for managing power for this CPU.
 */
/*ARGSUSED*/
void
cpupm_init(cpu_t *cp)
{
	cpupm_vendor_t *vendors;
	cpupm_mach_state_t *mach_state;
	cpu_pm_state_t *handle;
	struct machcpu *mcpu = &(cp->cpu_m);
	int *speeds;
	uint_t nspeeds;
	int ret;

	mach_state = cp->cpu_m.mcpu_pm_mach_state =
	    kmem_zalloc(sizeof (cpupm_mach_state_t), KM_SLEEP);
	mach_state->ms_caps = CPUPM_NO_STATES;
	mutex_init(&mach_state->ms_lock, NULL, MUTEX_DRIVER, NULL);

	handle = cpupm_oxide_init(cp);
	if (handle == NULL) {
		cpupm_fini(cp);
		cmn_err(CE_WARN, "!cpupm_init: processor %d: "
		    "unable to get ACPI handle", cp->cpu_id);
		cmn_err(CE_NOTE, "!CPU power management will not function.");
		CPUPM_DISABLE();
		return;
	}

	mach_state->ms_pm_handle = handle;

	/*
	 * Loop through the CPU management module table and see if
	 * any of the modules implement CPU power management
	 * for this CPU.
	 */
	for (vendors = cpupm_vendors; vendors->cpuv_init != NULL; vendors++) {
		if (vendors->cpuv_init(cp))
			break;
	}

	/*
	 * Nope, we can't power manage this CPU.
	 */
	if (vendors == NULL) {
		cpupm_fini(cp);
		CPUPM_DISABLE();
		return;
	}

	/*
	 * If P-state support exists for this system, then initialize it.
	 */
	if (mach_state->ms_pstate.cmp_ops != NULL) {
		ret = mach_state->ms_pstate.cmp_ops->cpus_init(cp);
		if (ret != 0) {
			mach_state->ms_pstate.cmp_ops = NULL;
			cpupm_disable(CPUPM_P_STATES);
		} else {
			nspeeds = cpupm_get_speeds(cp, &speeds);
			if (nspeeds == 0) {
				cmn_err(CE_NOTE, "!cpupm_init: processor %d:"
				    " no speeds to manage", cp->cpu_id);
			} else {
				cpupm_set_supp_freqs(cp, speeds, nspeeds);
				cpupm_free_speeds(speeds, nspeeds);
				mach_state->ms_caps |= CPUPM_P_STATES;
			}
		}
	} else {
		cpupm_disable(CPUPM_P_STATES);
	}

	/*
	 * If C-states support exists for this system, then initialize it.
	 */
	if (mach_state->ms_cstate.cmp_ops != NULL) {
		ret = mach_state->ms_cstate.cmp_ops->cpus_init(cp);
		if (ret != 0) {
			mach_state->ms_cstate.cmp_ops = NULL;
			mcpu->max_cstates = CPU_CSTATE_C1;
			cpupm_disable(CPUPM_C_STATES);
			/*
			 * We've determined we can't manage C-states, so make
			 * sure the idle/wakeup routines are set to something
			 * safe before proceeding. "non-deep" idle should always
			 * be safe, so use it.
			 */
			idle_cpu = non_deep_idle_cpu;
			disp_enq_thread = non_deep_idle_disp_enq_thread;
		} else if (cpu_deep_cstates_supported()) {
			mcpu->max_cstates = handle->cps_ncstates;
			cp->cpu_m.mcpu_idle_cpu = cpu_cstate_idle;
			disp_enq_thread = cstate_wakeup;
			mach_state->ms_caps |= CPUPM_C_STATES;
		} else {
			/*
			 * Similar to failing to initialize C-state support, we
			 * can't handle deep C-states on this system. Fall back
			 * to known-safe idle/wakeup options.
			 */
			mcpu->max_cstates = CPU_CSTATE_C1;
			idle_cpu = non_deep_idle_cpu;
			disp_enq_thread = non_deep_idle_disp_enq_thread;
		}
	} else {
		cpupm_disable(CPUPM_C_STATES);
	}


	if (mach_state->ms_caps == CPUPM_NO_STATES) {
		cpupm_fini(cp);
		CPUPM_DISABLE();
		return;
	}

	if (mach_state->ms_caps & CPUPM_P_STATES) {
		cpupm_init_top_speed(cp);
	}
}

/*
 * Free any resources allocated during cpupm initialization or cpupm start.
 */
/*ARGSUSED*/
void
cpupm_free(cpu_t *cp, boolean_t cpupm_stop)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;

	if (mach_state == NULL)
		return;

	if (mach_state->ms_pstate.cmp_ops != NULL) {
		if (cpupm_stop)
			mach_state->ms_pstate.cmp_ops->cpus_stop(cp);
		else
			mach_state->ms_pstate.cmp_ops->cpus_fini(cp);
		mach_state->ms_pstate.cmp_ops = NULL;
	}

	if (mach_state->ms_cstate.cmp_ops != NULL) {
		if (cpupm_stop)
			mach_state->ms_cstate.cmp_ops->cpus_stop(cp);
		else
			mach_state->ms_cstate.cmp_ops->cpus_fini(cp);

		mach_state->ms_cstate.cmp_ops = NULL;
	}

	if (mach_state->ms_pm_handle != NULL) {
		cpupm_oxide_fini(mach_state->ms_pm_handle);
		mach_state->ms_pm_handle = NULL;
	}

	mutex_destroy(&mach_state->ms_lock);
	kmem_free(mach_state, sizeof (cpupm_mach_state_t));
	cp->cpu_m.mcpu_pm_mach_state = NULL;
}

void
cpupm_fini(cpu_t *cp)
{
	/*
	 * call (*cpus_fini)() ops to release the cpupm resource
	 * in the P/C-state driver
	 */
	cpupm_free(cp, B_FALSE);
}

void
cpupm_start(cpu_t *cp)
{
	cpupm_init(cp);
}

void
cpupm_stop(cpu_t *cp)
{
	/*
	 * call (*cpus_stop)() ops to reclaim the cpupm resource
	 * in the P/C-state driver
	 */
	cpupm_free(cp, B_TRUE);
}

/*
 * If A CPU has started and at least one power state is manageable,
 * then the CPU is ready for power management.
 */
boolean_t
cpupm_is_ready(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	uint32_t cpupm_caps = mach_state->ms_caps;

	if (cpupm_enabled == CPUPM_NO_STATES) {
		return (B_FALSE);
	}

	if ((cpupm_caps & CPUPM_P_STATES) || (cpupm_caps & CPUPM_C_STATES)) {
		return (B_TRUE);
	}
	return (B_FALSE);
}

boolean_t
cpupm_is_enabled(uint32_t state)
{
	return ((cpupm_enabled & state) == state);
}

/*
 * By default, all states are enabled.
 */
void
cpupm_disable(uint32_t state)
{

	if (state & CPUPM_P_STATES) {
		cpupm_free_domains(&cpupm_pstate_domains);
	}
	if (state & CPUPM_C_STATES) {
		cpupm_free_domains(&cpupm_cstate_domains);
	}
	cpupm_enabled &= ~state;
}

/*
 * Allocate power domains for P- and C-states.
 *
 * `cpupm_alloc_domains` requires the corresponding state type's tables have
 * been fully described: individual P-/C-states are enumerated and information
 * describing the domains this logical processor lie in must have been set.
 */
void
cpupm_alloc_domains(cpu_t *cp, int state)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	cpupm_state_domains_t **dom_ptr;
	cpupm_state_domains_t *dptr;
	cpupm_state_domains_t **mach_dom_state_ptr;
	uint32_t domain;
	uint32_t type;

	switch (state) {
	case CPUPM_P_STATES:
		domain = handle->cps_pstate_domain.sd_domain;
		type = handle->cps_pstate_domain.sd_type;
		dom_ptr = &cpupm_pstate_domains;
		mach_dom_state_ptr = &mach_state->ms_pstate.cmp_domain;
		break;
	case CPUPM_C_STATES:
		domain = handle->cps_cstate_domain.sd_domain;
		type = handle->cps_cstate_domain.sd_type;
		dom_ptr = &cpupm_cstate_domains;
		mach_dom_state_ptr = &mach_state->ms_cstate.cmp_domain;
		break;
	default:
		return;
	}

	for (dptr = *dom_ptr; dptr != NULL; dptr = dptr->pm_next) {
		if (dptr->pm_domain == domain)
			break;
	}

	/* new domain is created and linked at the head */
	if (dptr == NULL) {
		dptr = kmem_zalloc(sizeof (cpupm_state_domains_t), KM_SLEEP);
		dptr->pm_domain = domain;
		dptr->pm_type = type;
		dptr->pm_next = *dom_ptr;
		mutex_init(&dptr->pm_lock, NULL, MUTEX_SPIN,
		    (void *)ipltospl(DISP_LEVEL));
		CPUSET_ZERO(dptr->pm_cpus);
		*dom_ptr = dptr;
	}
	CPUSET_ADD(dptr->pm_cpus, cp->cpu_id);
	*mach_dom_state_ptr = dptr;
}

/*
 * Free C, P or T state power domains
 */
void
cpupm_free_domains(cpupm_state_domains_t **dom_ptr)
{
	cpupm_state_domains_t *this_domain, *next_domain;

	this_domain = *dom_ptr;
	while (this_domain != NULL) {
		next_domain = this_domain->pm_next;
		mutex_destroy(&this_domain->pm_lock);
		kmem_free((void *)this_domain,
		    sizeof (cpupm_state_domains_t));
		this_domain = next_domain;
	}
	*dom_ptr = NULL;
}

/*
 * Remove CPU from C, P or T state power domains
 */
void
cpupm_remove_domains(cpu_t *cp, int state, cpupm_state_domains_t **dom_ptr)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpupm_state_domains_t *dptr;
	uint32_t pm_domain;

	ASSERT(mach_state);

	switch (state) {
	case CPUPM_P_STATES:
		pm_domain = mach_state->ms_pstate.cmp_domain->pm_domain;
		break;
	case CPUPM_C_STATES:
		pm_domain = mach_state->ms_cstate.cmp_domain->pm_domain;
		break;
	default:
		return;
	}

	/*
	 * Find the CPU C, P or T state power domain
	 */
	for (dptr = *dom_ptr; dptr != NULL; dptr = dptr->pm_next) {
		if (dptr->pm_domain == pm_domain)
			break;
	}

	/*
	 * return if no matched domain found
	 */
	if (dptr == NULL)
		return;

	/*
	 * We found one matched power domain, remove CPU from its cpuset.
	 * pm_lock(spin lock) here to avoid the race conditions between
	 * event change notification and cpu remove.
	 */
	mutex_enter(&dptr->pm_lock);
	if (CPU_IN_SET(dptr->pm_cpus, cp->cpu_id))
		CPUSET_DEL(dptr->pm_cpus, cp->cpu_id);
	mutex_exit(&dptr->pm_lock);
}

void
cpupm_alloc_ms_cstate(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state;
	cpupm_mach_power_state_t *ms_cstate;

	mach_state = (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	ms_cstate = &mach_state->ms_cstate;
	ASSERT(ms_cstate->cmp_state.cstate == NULL);
	ms_cstate->cmp_state.cstate = kmem_zalloc(sizeof (cmp_c_state_t),
	    KM_SLEEP);
	ms_cstate->cmp_state.cstate->cs_next_cstate = CPU_CSTATE_C1;
}

void
cpupm_free_ms_cstate(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpupm_mach_power_state_t *ms_cstate = &mach_state->ms_cstate;

	if (ms_cstate->cmp_state.cstate != NULL) {
		kmem_free(ms_cstate->cmp_state.cstate, sizeof (cmp_c_state_t));
		ms_cstate->cmp_state.cstate = NULL;
	}
}

void
cpupm_state_change(cpu_t *cp, int level, int state)
{
	cpupm_mach_state_t	*mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpupm_state_ops_t	*state_ops;
	cpupm_state_domains_t	*state_domain;
	cpuset_t		set;

	DTRACE_PROBE2(cpupm__state__change, cpu_t *, cp, int, level);

	if (mach_state == NULL) {
		return;
	}

	switch (state) {
	case CPUPM_P_STATES:
		state_ops = mach_state->ms_pstate.cmp_ops;
		state_domain = mach_state->ms_pstate.cmp_domain;
		break;
	default:
		return;
	}

	switch (state_domain->pm_type) {
	case CPU_PM_SW_ANY:
		/*
		 * A request on any CPU in the domain transitions the domain
		 */
		CPUSET_ONLY(set, cp->cpu_id);
		state_ops->cpus_change(set, level);
		break;
	case CPU_PM_SW_ALL:
		/*
		 * All CPUs in the domain must request the transition
		 */
	case CPU_PM_HW_ALL:
		/*
		 * P-state transitions are coordinated by the hardware
		 * For now, request the transition on all CPUs in the domain,
		 * but looking ahead we can probably be smarter about this.
		 */
		mutex_enter(&state_domain->pm_lock);
		state_ops->cpus_change(state_domain->pm_cpus, level);
		mutex_exit(&state_domain->pm_lock);
		break;
	default:
		cmn_err(CE_NOTE, "Unknown domain coordination type: %d",
		    state_domain->pm_type);
	}
}

/*
 * CPU PM interfaces exposed to the CPU power manager
 */
/*ARGSUSED*/
id_t
cpupm_plat_domain_id(cpu_t *cp, cpupm_dtype_t type)
{
	cpupm_mach_state_t	*mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);

	if ((mach_state == NULL) || (!cpupm_is_enabled(CPUPM_P_STATES) &&
	    !cpupm_is_enabled(CPUPM_C_STATES))) {
		return (CPUPM_NO_DOMAIN);
	}
	if (type == CPUPM_DTYPE_ACTIVE) {
		/*
		 * Return P-State domain for the specified CPU
		 */
		if (mach_state->ms_pstate.cmp_domain) {
			return (mach_state->ms_pstate.cmp_domain->pm_domain);
		}
	} else if (type == CPUPM_DTYPE_IDLE) {
		/*
		 * Return C-State domain for the specified CPU
		 */
		if (mach_state->ms_cstate.cmp_domain) {
			return (mach_state->ms_cstate.cmp_domain->pm_domain);
		}
	}
	return (CPUPM_NO_DOMAIN);
}

uint_t
cpupm_plat_state_enumerate(cpu_t *cp, cpupm_dtype_t type,
    cpupm_state_t *states)
{
	int	*speeds = NULL;
	uint_t	nspeeds, i;

	/*
	 * Idle domain support unimplemented
	 */
	if (type != CPUPM_DTYPE_ACTIVE) {
		return (0);
	}
	nspeeds = cpupm_get_speeds(cp, &speeds);

	/*
	 * If the caller passes NULL for states, just return the
	 * number of states.
	 */
	if (states != NULL) {
		for (i = 0; i < nspeeds; i++) {
			states[i].cps_speed = speeds[i];
			states[i].cps_handle = (cpupm_handle_t)i;
		}
	}
	cpupm_free_speeds(speeds, nspeeds);
	return (nspeeds);
}

/*ARGSUSED*/
int
cpupm_plat_change_state(cpu_t *cp, cpupm_state_t *state)
{
	if (!cpupm_is_ready(cp))
		return (-1);

	cpupm_state_change(cp, (int)state->cps_handle, CPUPM_P_STATES);

	return (0);
}

/*ARGSUSED*/
/*
 * Note: It is the responsibility of the users of
 * cpupm_get_speeds() to free the memory allocated
 * for speeds using cpupm_free_speeds()
 */
uint_t
cpupm_get_speeds(cpu_t *cp, int **speeds)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	return (cpu_get_speeds(mach_state->ms_pm_handle, speeds));
}

/*ARGSUSED*/
void
cpupm_free_speeds(int *speeds, uint_t nspeeds)
{
	cpu_free_speeds(speeds, nspeeds);
}

/*
 * All CPU instances have been initialized successfully.
 */
boolean_t
cpupm_power_ready(cpu_t *cp)
{
	return (cpupm_is_enabled(CPUPM_P_STATES) && cpupm_is_ready(cp));
}

/*
 * All CPU instances have been initialized successfully.
 */
boolean_t
cpupm_cstate_ready(cpu_t *cp)
{
	return (cpupm_is_enabled(CPUPM_C_STATES) && cpupm_is_ready(cp));
}

/*
 * Get the highest-performance P-state.
 *
 * This is almost certainly P0. This is called from cpudrv as well used below,
 * though, so it still exists for now. This function made more sense on i86pc
 * where system firmware could artificially limit (hide) high-performance
 * P-states in certain circumstances.
 */
int
cpupm_get_top_speed(cpu_t *cp)
{
	cpupm_mach_state_t	*mach_state;
	cpu_pm_state_t		*handle;

	mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	handle = mach_state->ms_pm_handle;

	ASSERT3U(handle->cps_pstate_max, <, handle->cps_npstates);

	return (handle->cps_pstate_max);
}

/*
 * Set the maximum power state to the highest-performance P-state.
 *
 * Practically speaking, this will find P0 is the highest-performance state,
 * then set P0 as the highest-performance state as a no-op.
 * cpupm_redefine_max_activepwr_state gets into the common bits of power
 * management, though, and it's not immediately clear if this defaults the right
 * way if we *don't* call it. Side-step the question by just plumbing our zero
 * over there for now.
 */
static void
cpupm_init_top_speed(void *ctx)
{
	cpu_t			*cp = ctx;
	int			top_speed;

	top_speed = cpupm_get_top_speed(cp);
	cpupm_redefine_max_activepwr_state(cp, top_speed);
}

/*
 * Update cpupm cstate data each time CPU exits idle.
 */
void
cpupm_wakeup_cstate_data(cmp_c_state_t *cs_data, hrtime_t end)
{
	cs_data->cs_idle_exit = end;
}

/*
 * Determine next cstate based on cpupm data.
 * Update cpupm cstate data each time CPU goes idle.
 * Do as much as possible in the idle state bookkeeping function because the
 * performance impact while idle is minimal compared to in the wakeup function
 * when there is real work to do.
 */
uint32_t
cpupm_next_cstate(cmp_c_state_t *cs_data, cpu_pm_state_t *pm_state,
    hrtime_t start)
{
	hrtime_t duration;
	hrtime_t ave_interval;
	hrtime_t ave_idle_time;
	uint32_t i, smpl_cnt;
	cpu_cstate_t *cstates = pm_state->cps_cstates;
	/*
	 * C-states are ordered by decreasing power. Assume we can sleep in the
	 * deepest manner, and the rest of the checks here will determine if the
	 * minimum acceptable power state is actually more shallow.
	 */
	uint32_t deepest_cstate = pm_state->cps_ncstates;

	duration = cs_data->cs_idle_exit - cs_data->cs_idle_enter;
	scalehrtime(&duration);
	cs_data->cs_idle += duration;
	cs_data->cs_idle_enter = start;

	smpl_cnt = ++cs_data->cs_cnt;
	cs_data->cs_smpl_len = start - cs_data->cs_smpl_start;
	scalehrtime(&cs_data->cs_smpl_len);
	if (cs_data->cs_smpl_len > cpupm_cs_sample_interval) {
		cs_data->cs_smpl_idle = cs_data->cs_idle;
		cs_data->cs_idle = 0;
		cs_data->cs_smpl_idle_pct = ((100 * cs_data->cs_smpl_idle) /
		    cs_data->cs_smpl_len);

		cs_data->cs_smpl_start = start;
		cs_data->cs_cnt = 0;

		/*
		 * Will CPU be idle long enough to save power?
		 */
		ave_idle_time = (cs_data->cs_smpl_idle / smpl_cnt) / 1000;
		for (i = 1; i < deepest_cstate; ++i) {
			if (ave_idle_time < (cstates[i].cs_latency *
			    cpupm_cs_idle_save_tunable)) {
				deepest_cstate = i;
				DTRACE_PROBE3(cpupm__next__cstate, cpu_t *,
				    CPU, uint32_t, i,
				    cpupm_cstate_reason_t,
				    CSTATE_REASON_IDLE_THRESHOLD);
			}
		}

		/*
		 * Wakeup often (even when non-idle time is very short)?
		 * Some producer/consumer type loads fall into this category.
		 */
		ave_interval = (cs_data->cs_smpl_len / smpl_cnt) / 1000;
		for (i = 1; i < deepest_cstate; ++i) {
			if (ave_interval <= (cstates[i].cs_latency *
			    cpupm_cs_idle_cost_tunable)) {
				deepest_cstate = i;
				DTRACE_PROBE3(cpupm__next__cstate, cpu_t *,
				    CPU, uint32_t, i,
				    cpupm_cstate_reason_t,
				    CSTATE_REASON_WAKEUP_THRESHOLD);
			}
		}

		/*
		 * Idle percent
		 */
		for (i = 1; i < deepest_cstate; ++i) {
			switch (cstates[i].cs_type) {
			case CPU_CSTATE_C0:
				/*
				 * We don't "enter" C0, it's just the absence
				 * of being in C1-or-deeper. So there's no
				 * tunable to stay "out" of C0.
				 */
				break;
			case CPU_CSTATE_C1:
				/*
				 * C1 is cheap enough (both in latency and
				 * cache effects) that we don't have a tunable
				 * to stay out of it purely based on idleness.
				 */
				break;
			case CPU_CSTATE_C6:
				if (cs_data->cs_smpl_idle_pct <
				    cpupm_C6_idle_pct_tunable) {
					deepest_cstate = i;
					DTRACE_PROBE3(cpupm__next__cstate,
					    cpu_t *, CPU, uint32_t, i,
					    cpupm_cstate_reason_t,
					    CSTATE_REASON_IDLE_TUNABLE);
				}
				break;
			}
		}

		cs_data->cs_next_cstate = deepest_cstate - 1;
	}

	return (cs_data->cs_next_cstate);
}
