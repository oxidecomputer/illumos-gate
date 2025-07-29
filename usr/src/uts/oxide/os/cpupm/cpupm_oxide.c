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

#include <sys/amdzen/ccx.h>
#include <sys/cpu_idle.h>
#include <sys/cpupm_oxide.h>
#include <sys/io/zen/physaddrs.h>
#include <sys/pwrnow.h>
#include <sys/stdbool.h>
#include <sys/x86_archext.h>

/*
 * Return supported frequencies in MHz.
 */
uint_t
cpu_get_speeds(cpu_pm_state_t *handle, int **speeds)
{
	cpu_pstate_t *pstate = handle->cps_pstates;
	uint_t nspeeds = handle->cps_npstates;
	int *hspeeds;
	uint_t speed;

	if (nspeeds == 0) {
		*speeds = NULL;
		return (0);
	}

	hspeeds = kmem_zalloc(nspeeds * sizeof (int), KM_SLEEP);
	for (uint_t i = 0; i < nspeeds; i++) {
		speed = pstate->ps_freq;
		ASSERT3U(speed, <=, INT_MAX);

		hspeeds[i] = (int)speed;
		pstate++;
	}
	*speeds = hspeeds;
	return (nspeeds);
}

/*
 * Free resources allocated by cpu_get_speeds().
 */
void
cpu_free_speeds(int *speeds, uint_t nspeeds)
{
	kmem_free(speeds, nspeeds * sizeof (int));
}

cpu_pm_state_t *
cpupm_oxide_init(cpu_t *cp)
{
	cpu_pm_state_t *handle;

	handle = kmem_zalloc(sizeof (cpu_pm_state_t), KM_SLEEP);
	handle->cpu_id = cp->cpu_id;

	return (handle);
}

void
cpupm_oxide_fini(cpu_pm_state_t *state)
{
	if (state != NULL) {
		kmem_free(state, sizeof (cpu_pm_state_t));
	}
}

bool
cpupm_amd_init(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);

	if (x86_vendor != X86_VENDOR_AMD) {
		return (false);
	}

	/*
	 * Without hardware P-state detection there is nothing to manage, so
	 * disable pstates.
	 */
	mach_state->ms_pstate.cmp_ops = NULL;

	mach_state->ms_cstate.cmp_ops = &cpu_idle_ops;

	return (true);
}

/*
 * C-state setup that must be run on the specific logical processor for which
 * power management is being initialized.
 */
static int
amd_cstate_zen_cpu_setup(xc_arg_t arg1 __unused, xc_arg_t arg2 __unused,
    xc_arg_t arg3 __unused)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)CPU->cpu_m.mcpu_pm_mach_state;
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	const x86_uarch_t uarch = uarchrev_uarch(cpuid_getuarchrev(CPU));
	uint64_t v;

	/*
	 * PPRs state that MSR_AMD_CSTATE_CFG and MSR_AMD_CSTATE_BASE_ADDR must
	 * be set the same on all cores. You may ask, "but what about skew as
	 * each processor reaches amd_cstate_zen_msr_setup()?" - this is a great
	 * (and unclear) question.
	 *
	 * Until we need different CCRs to behave differently, configure them
	 * all the same. Other than CC6, CCR settings are left the same as their
	 * at-reset defaults because while those settings may be interesting,
	 * they are not very documented and we don't know better values to use
	 * yet. See the definition of CSTATE_CFG for more here.
	 */
	v = rdmsr(MSR_AMD_CSTATE_CFG);
	switch (uarch) {
	case X86_UARCH_AMD_ZEN5:
		v = AMD_CSTATE_CFG_U_ZEN5_SET_CCR3_CC6EN(v, 1);
		/* FALLTHROUGH */
	case X86_UARCH_AMD_ZEN4:
	case X86_UARCH_AMD_ZEN3:
		v = AMD_CSTATE_CFG_SET_CCR2_CC6EN(v, 1);
		v = AMD_CSTATE_CFG_SET_CCR1_CC6EN(v, 1);
		v = AMD_CSTATE_CFG_SET_CCR0_CC6EN(v, 1);
		break;
	default:
		panic("Unsupported uarch 0x%x", uarch);
	}
	wrmsr_and_test(MSR_AMD_CSTATE_CFG, v);

	if (uarch == X86_UARCH_AMD_ZEN5) {
		v = rdmsr(MSR_AMD_CSTATE_CFG2);
		v = AMD_CSTATE_CFG2_U_ZEN5_SET_CCR7_CC6EN(v, 1);
		v = AMD_CSTATE_CFG2_U_ZEN5_SET_CCR6_CC6EN(v, 1);
		v = AMD_CSTATE_CFG2_U_ZEN5_SET_CCR5_CC6EN(v, 1);
		v = AMD_CSTATE_CFG2_U_ZEN5_SET_CCR4_CC6EN(v, 1);
		wrmsr_and_test(MSR_AMD_CSTATE_CFG2, v);
	}

	wrmsr_and_test(MSR_AMD_CSTATE_BASE_ADDR, ZEN_IOPORT_CSTATE_BASE_ADDR);

	/*
	 * AMD C-states relate to physical cores, and are shared across logical
	 * threads on a core. Logical threads are the unit by which C-state
	 * changes are requested, though. This means that if SMT is enabled, a
	 * C-state domain is a core's SMT twins. Otherwise, a C-state domain is
	 * the physical core's single logical thread.
	 *
	 * Conveniently, this exactly matches the description of coreid.
	 */
	id_t coreid = cpuid_get_coreid(CPU);
	ASSERT3S(coreid, >=, 0);
	ASSERT3S(coreid, <, INT_MAX);
	handle->cps_cstate_domain.sd_domain = (uint32_t)coreid;
	handle->cps_cstate_domain.sd_type = CPU_PM_HW_ALL;

	return (0);
}

/*
 * Detect the currently-configured C-states, prepare tables describing how to
 * enter them as well as expected transition latency when doing so.
 *
 * As things stand, C-states are defined the same between Milan/Genoa/Turin
 * (excepting details like PC6 which don't make it to OS visibility anyway)
 * so we can reuse the same routine across supported processor families so far.
 */
void
cpupm_amd_cstates_zen(cpu_pm_state_t *handle)
{
	/*
	 * CPUPM is initialized from the BSP, even for all other APs that are
	 * brought up. To do processor-specific configuration, cross-call that
	 * processor with the needful.
	 *
	 * cpupm_init() can probably be moved to run on the AP for which power
	 * management is being initialized, at which point we can just call
	 * amd_cstate_zen_msr_setup directly.
	 */
	cpuset_t pm_cpu;
	CPUSET_ONLY(pm_cpu, handle->cpu_id);
	xc_call(0, 0, 0, CPUSET2BV(pm_cpu), amd_cstate_zen_cpu_setup);

	cpu_cstate_t *cstates;

	/*
	 * Currently configure two C-states: one for CC1, one for CC6.
	 * As an implementation detail, CC6 is reached by entering CC1, waiting
	 * for a timer to expire indicating cache inactivity, flushing L2,
	 * then powering further down.
	 *
	 * In `cpu_idle_mwait` we `i86_mwait(0, 0);`, which initially had no
	 * C-state semantic, but the first hint of 0 has become interpreted as
	 * the desired C-state minus one. So we request CC1 that way as well.
	 */
	handle->cps_ncstates = 2;
	size_t alloc_size = handle->cps_ncstates * sizeof (cpu_cstate_t);
	cstates = kmem_zalloc(alloc_size, KM_SLEEP);
	handle->cps_cstates = cstates;

	cpu_cstate_t *c1 = &cstates[0];
	c1->cs_mechanism = CSTATE_MECHANISM_INSTRUCTION;
	c1->cs_address = 0;
	c1->cs_type = CPU_CSTATE_C1;
	/*
	 * I've found no documentation on how quickly C1 is entered. There are
	 * some configurable timers that seem like they would control this.
	 *
	 * Even with those timers set to zero, there presumably is some latency
	 * in changing clock dividers and frequency selection, so entering and
	 * exiting C1 is probably not *zero* latency. CC1 is what backs ACPI C1
	 * on ACPI systems, so presumably the latencies are not "too bad", as
	 * ACPI requires C1 to be fast enough that latency is not a
	 * consideration in power saving decisions.
	 *
	 * I've measured this as at roughly 8 microseconds or less with current
	 * CC1 settings, but that is an overestimate. This presmuably requires
	 * the SMU to service an interrupt, so that's a lower bound.
	 */
	c1->cs_latency = 10;

	cpu_cstate_t *c2 = &cstates[1];
	c2->cs_mechanism = CSTATE_MECHANISM_IOPORT;
	c2->cs_address = ZEN_IOPORT_CSTATE_BASE_ADDR;
	c2->cs_type = CPU_CSTATE_C6;
	/*
	 * This number is only roughly accurate. Actually a function of
	 * `Core::X86::Msr::CSTATE_POLICY`'s TMRLEN fields, as well as CC1
	 * latency.
	 *
	 * I've measured this at 20-25 microseconds with current CC1/CC6
	 * settings, with an occasional ~100 microsecond observation. If the
	 * ~100 microsecond observations are not noise, I can't explain why they
	 * would be that high. This could use a closer look.
	 */
	c2->cs_latency = 100;
}
