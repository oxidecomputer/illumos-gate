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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2025 Oxide Computer Company
 */

#include <sys/x86_archext.h>
#include <sys/machsystm.h>
#include <sys/x_call.h>
#include <sys/pwrnow.h>
#include <sys/cpupm_oxide.h>
#include <sys/cpupm.h>
#include <sys/dtrace.h>
#include <sys/sdt.h>
#include <sys/stdbool.h>

static int pwrnow_init(cpu_t *);
static void pwrnow_fini(cpu_t *);
static void pwrnow_power(cpuset_t, uint32_t);
static void pwrnow_stop(cpu_t *);

static boolean_t pwrnow_cpb_supported(void);

/*
 * Interfaces for modules implementing AMD's PowerNow!.
 */
cpupm_state_ops_t pwrnow_ops = {
	"PowerNow! Technology",
	pwrnow_init,
	pwrnow_fini,
	pwrnow_power,
	pwrnow_stop
};

/*
 * Error returns
 */
#define	PWRNOW_RET_SUCCESS		0x00
#define	PWRNOW_RET_NO_PM		0x01

/*
 * Debugging support
 */
#ifdef	DEBUG
volatile int pwrnow_debug = 0;
#define	PWRNOW_DEBUG(arglist) if (pwrnow_debug) printf arglist;
#else
#define	PWRNOW_DEBUG(arglist)
#endif

/*
 * Detect the current CPU's P-states and prepare structures describing them.
 */
static bool
pwrnow_pstate_prepare(cpu_pm_state_t *handle)
{
	/*
	 * We're not actually handling P-states yet. Error out in this stub
	 * function as there is nothing to manage and callers should not even
	 * try.
	 */
	return (false);
}

void
pwrnow_free_pstate_data(cpu_pm_state_t *handle)
{
	if (handle != NULL) {
		if (handle->cps_pstates != NULL) {
			kmem_free(handle->cps_pstates,
			    handle->cps_npstates * sizeof (cpu_pstate_t));
			handle->cps_pstates = NULL;
			handle->cps_npstates = 0;
		}
	}
}

/*
 * Transition the current processor to the requested state.
 */
static int
pwrnow_pstate_transition(xc_arg_t arg1, xc_arg_t arg2 __unused,
    xc_arg_t arg3 __unused)
{
	uint32_t req_state = (uint32_t)arg1;
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)CPU->cpu_m.mcpu_pm_mach_state;
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	cpu_pstate_t *req_pstate = &handle->cps_pstates[req_state];
	uint32_t state_nr = req_pstate->ps_state;

	DTRACE_PROBE1(pwrnow_transition_freq, uint32_t,
	    req_pstate->ps_freq);

	/*
	 * Initiate the processor p-state change.
	 */
	wrmsr(MSR_AMD_PSTATE_CTL, state_nr);

	DTRACE_PROBE1(pwrnow_ctrl_write, uint32_t, state_nr);

	if (mach_state->ms_turbo != NULL)
		cpupm_record_turbo_info(mach_state->ms_turbo,
		    mach_state->ms_pstate.cmp_state.pstate, req_state);

	mach_state->ms_pstate.cmp_state.pstate = req_state;
	cpu_set_curr_clock((uint64_t)req_pstate->ps_freq * 1000000UL);
	return (0);
}

static void
pwrnow_power(cpuset_t set, uint32_t req_state)
{
	/*
	 * If thread is already running on target CPU then just
	 * make the transition request. Otherwise, we'll need to
	 * make a cross-call.
	 */
	kpreempt_disable();
	if (CPU_IN_SET(set, CPU->cpu_id)) {
		(void) pwrnow_pstate_transition(req_state, 0, 0);
		CPUSET_DEL(set, CPU->cpu_id);
	}
	if (!CPUSET_ISNULL(set)) {
		xc_call((xc_arg_t)req_state, 0, 0,
		    CPUSET2BV(set), pwrnow_pstate_transition);
	}
	kpreempt_enable();
}

/*
 * Validate that this processor supports PowerNow! and if so,
 * get its P-state data and cache it.
 */
static int
pwrnow_init(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	static int logged = 0;

	PWRNOW_DEBUG(("pwrnow_init: processor %d\n", cp->cpu_id));

	/*
	 * Cache (and potentially configure) hardware P-states.
	 */
	if (!pwrnow_pstate_prepare(handle)) {
		if (!logged) {
			cmn_err(CE_NOTE, "!PowerNow! support is being "
			    "disabled due to not detecting P-state support.");
			logged = 1;
		}
		pwrnow_fini(cp);
		return (PWRNOW_RET_NO_PM);
	}

	cpupm_alloc_domains(cp, CPUPM_P_STATES);

	/*
	 * Check for Core Performance Boost support
	 */
	if (pwrnow_cpb_supported())
		mach_state->ms_turbo = cpupm_turbo_init(cp);

	PWRNOW_DEBUG(("Processor %d succeeded.\n", cp->cpu_id))
	return (PWRNOW_RET_SUCCESS);
}

/*
 * Free resources allocated by pwrnow_init().
 */
static void
pwrnow_fini(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;

	cpupm_free_domains(&cpupm_pstate_domains);
	pwrnow_free_pstate_data(handle);

	if (mach_state->ms_turbo != NULL)
		cpupm_turbo_fini(mach_state->ms_turbo);
	mach_state->ms_turbo = NULL;
}

boolean_t
pwrnow_supported()
{
	struct cpuid_regs cpu_regs;

	/* Required features */
	ASSERT(is_x86_feature(x86_featureset, X86FSET_CPUID));
	if (!is_x86_feature(x86_featureset, X86FSET_MSR)) {
		PWRNOW_DEBUG(("No CPUID or MSR support."));
		return (B_FALSE);
	}

	/*
	 * Get the Advanced Power Management Information.
	 */
	cpu_regs.cp_eax = 0x80000007;
	(void) __cpuid_insn(&cpu_regs);

	/*
	 * We currently only support CPU power management of
	 * processors that are P-state TSC invariant
	 */
	if (!(cpu_regs.cp_edx & CPUID_AMD_8X07_EDX_TSC_INV)) {
		PWRNOW_DEBUG(("No support for CPUs that are not P-state "
		    "TSC invariant.\n"));
		return (B_FALSE);
	}

	/*
	 * We only support the "Fire and Forget" style of PowerNow! (i.e.,
	 * single MSR write to change speed).
	 */
	if (!(cpu_regs.cp_edx & CPUID_AMD_8X07_EDX_PSTATE_HW)) {
		PWRNOW_DEBUG(("Hardware P-State control is not supported.\n"));
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
pwrnow_cpb_supported(void)
{
	struct cpuid_regs cpu_regs;

	/* Required features */
	ASSERT(is_x86_feature(x86_featureset, X86FSET_CPUID));
	if (!is_x86_feature(x86_featureset, X86FSET_MSR)) {
		PWRNOW_DEBUG(("No CPUID or MSR support."));
		return (B_FALSE);
	}

	/*
	 * Get the Advanced Power Management Information.
	 */
	cpu_regs.cp_eax = 0x80000007;
	(void) __cpuid_insn(&cpu_regs);

	if (!(cpu_regs.cp_edx & CPUID_AMD_8X07_EDX_CPB))
		return (B_FALSE);

	return (B_TRUE);
}

static void
pwrnow_stop(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;

	cpupm_remove_domains(cp, CPUPM_P_STATES, &cpupm_pstate_domains);
	pwrnow_free_pstate_data(handle);

	if (mach_state->ms_turbo != NULL)
		cpupm_turbo_fini(mach_state->ms_turbo);
	mach_state->ms_turbo = NULL;
}
