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

#ifndef	_CPU_OXIDE_H
#define	_CPU_OXIDE_H

#include <sys/cpuvar.h>
#include <sys/kstat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Labels for C-states supported by the system. The supported C-states and their
 * meanings may vary across processor family and vendor.
 *
 * When adding a new C-state here, consider if it should have an idle percent
 * tunable in cpupm_next_cstate.
 *
 * The numeric values are arbitrary, but chosen to reflect the enum labels.
 */
typedef enum cpu_cstate_kind {
	CPU_CSTATE_C0 = 0,
	CPU_CSTATE_C1 = 1,
	CPU_CSTATE_C6 = 6
} cpu_cstate_kind_t;

/*
 * CPU Domain Coordination Types
 */
#define	CPU_PM_SW_ALL	0xfc
#define	CPU_PM_SW_ANY	0xfd
#define	CPU_PM_HW_ALL	0xfe

typedef struct cpu_power_state {
	/*
	 * An identifier describing some power domain. Processors share a power
	 * domain if their sd_domain are equal.
	 */
	uint32_t sd_domain;
	uint32_t sd_type;
} cpu_power_state_domain_t;

/*
 * Information about one P-state. One might also expect to see power or
 * transition latency figures here, but this is an interim definition until we
 * more fully support P-states.
 */
typedef struct cpu_pstate {
	/*
	 * Minimum frequency a processor in this P-state is expected to operate
	 * at. Depending on processor and vendor, the highest-performance
	 * P-state may result in a higher actual frequency, depending on "turbo"
	 * or "boost" features and their enablement.
	 *
	 * Expected to be in MHz by `cpu_get_speeds` and
	 * `pwrnow_pstate_transition`
	 */
	uint32_t ps_freq;
	/*
	 * The identifier that can be used to switch to the P-state described by
	 * this structure. This may be contiguous integers in practice, but
	 * should be understood as an arbitrary value for later P-state changes.
	 *
	 * On AMD processors, this is what would be written to
	 * MSR_AMD_PSTATE_CTL. Here, while ps_state is probably a contiguous
	 * series of integers starting at 0, it does not need to be. It is
	 * technically possible (however unlikely) that P-state MSRs could be
	 * configured like so:
	 *
	 *             PstateEn
	 * PStateDef0: 0        .. <reserved> .. <P-state config>
	 * PStateDef1: 1        .. <reserved> .. <P-state config>
	 * PStateDef2: 0        .. <reserved> .. <P-state config>
	 * PStateDef3: 1        .. <reserved> .. <P-state config>
	 * PStateDefN: ...
	 *
	 * in such a case, the first two cpu_pstate_t would have ps_state 1 and
	 * 3, skipping 0 and 2.
	 */
	uint32_t ps_state;
} cpu_pstate_t;

typedef enum cstate_mechanism {
	/*
	 * The corresponding C-state is requested exclusively through a fixed
	 * instruction (or instruction sequence), such as `hlt` or
	 * `monitor/mwait`, that does not depend on the C-state being entered.
	 */
	CSTATE_MECHANISM_INSTRUCTION,
	/*
	 * The corresponding C-state is requested through an I/O read (`inl()`).
	 */
	CSTATE_MECHANISM_IOPORT
} cstate_mechanism_t;

/*
 * A description of a single C-state.
 * Container for C-state information
 */
typedef struct cpu_cstate {
	cstate_mechanism_t cs_mechanism;
	/*
	 * The address to interact with when requesting this C-state. May be 0
	 * if the mechanism does not involve any particular address (for
	 * example, `hlt`.)
	 */
	uint32_t cs_address;
	cpu_cstate_kind_t cs_type;
	uint32_t cs_latency;
	kstat_t	*cs_ksp;
} cpu_cstate_t;

/*
 * The power management capabilities and constraints of a processor.
 *
 * In practice every CPU will probably have equivalent P-state and C-state
 * arrays, while the domain structs will be more varied.
 */
typedef struct cpu_pm_state {
	/*
	 * The processor whose power management state this struct describes.
	 */
	processorid_t cpu_id;
	cpu_power_state_domain_t cps_cstate_domain;
	/*
	 * C-states are ordered in increasing entry/exit latency.
	 */
	cpu_cstate_t *cps_cstates;
	uint32_t cps_ncstates;

	cpu_power_state_domain_t cps_pstate_domain;
	/*
	 * P-states are ordered in increasing entry/exit latency.
	 */
	cpu_pstate_t *cps_pstates;
	uint32_t cps_npstates;
	/*
	 * The highest-performance P-state. This is almost certainly just P0,
	 * but technically it's defined by the processor. This value is not
	 * meaningful if cps_npstates==0.
	 */
	uint32_t cps_pstate_max;
} cpu_pm_state_t;

extern uint_t cpu_get_speeds(cpu_pm_state_t *, int **);
extern uint_t cpu_get_max_cstates(cpu_pm_state_t *);
extern void cpu_free_speeds(int *, uint_t);
extern cpu_pm_state_t *cpupm_oxide_init(cpu_t *);
extern void cpupm_oxide_fini(cpu_pm_state_t *);

extern void cpupm_amd_cstates_zen(cpu_pm_state_t *handle);

#ifdef __cplusplus
}
#endif

#endif	/* _CPU_OXIDE_H */
