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
 * Copyright (c) 2009-2010, Intel Corporation.
 * All rights reserved.
 */
/*
 * Copyright 2019 Joyent, Inc.
 * Copyright 2025 Oxide Computer Company
 */

#include <sys/x86_archext.h>
#include <sys/machsystm.h>
#include <sys/x_call.h>
#include <sys/stat.h>
#include <sys/cpupm_oxide.h>
#include <sys/cpu_idle.h>
#include <sys/cpu_event.h>
#include <sys/archsystm.h>
#include <vm/hat_i86.h>
#include <sys/dtrace.h>
#include <sys/platform_detect.h>
#include <sys/sdt.h>
#include <sys/callb.h>

#define	CPU_IDLE_STOP_TIMEOUT		1000

extern void cpu_idle_adaptive(void);
extern uint32_t cpupm_next_cstate(cmp_c_state_t *cs_data,
    cpu_pm_state_t *pm_state, hrtime_t start);

static int cpu_idle_init(cpu_t *);
static void cpu_idle_fini(cpu_t *);
static void cpu_idle_stop(cpu_t *);
static boolean_t cpu_deep_idle_callb(void *arg, int code);
static boolean_t cpu_idle_cpr_callb(void *arg, int code);
static void cpu_deep_idle(cpu_cstate_t *cstate);

/*
 * Interfaces for modules implementing AMD's deep c-state.
 */
cpupm_state_ops_t cpu_idle_ops = {
	"Generic AMD C-state Support",
	cpu_idle_init,
	cpu_idle_fini,
	NULL,
	cpu_idle_stop
};

static kmutex_t		cpu_idle_callb_mutex;
static callb_id_t	cpu_deep_idle_callb_id;
static callb_id_t	cpu_idle_cpr_callb_id;
static uint_t		cpu_idle_cfg_state;

static kmutex_t cpu_idle_mutex;

cpu_idle_kstat_t cpu_idle_kstat = {
	{ "address_space_id",	KSTAT_DATA_STRING },
	{ "latency",		KSTAT_DATA_UINT32 },
};

/*
 * kstat update function of the c-state info
 */
static int
cpu_idle_kstat_update(kstat_t *ksp, int flag)
{
	cpu_cstate_t *cstate = ksp->ks_private;

	if (flag == KSTAT_WRITE) {
		return (EACCES);
	}

	if (cstate->cs_mechanism == CSTATE_MECHANISM_INSTRUCTION) {
		kstat_named_setstr(&cpu_idle_kstat.addr_space_id,
		"FixedInstruction");
	} else if (cstate->cs_mechanism == CSTATE_MECHANISM_IOPORT) {
		kstat_named_setstr(&cpu_idle_kstat.addr_space_id,
		"IOPort");
	} else {
		kstat_named_setstr(&cpu_idle_kstat.addr_space_id,
		"Unsupported");
	}

	cpu_idle_kstat.cs_latency.value.ui32 = cstate->cs_latency;

	return (0);
}

/*
 * c-state wakeup function.
 * Similar to cpu_wakeup and cpu_wakeup_mwait except this function deals
 * with CPUs asleep in MWAIT, HLT, or ACPI Deep C-State.
 */
void
cstate_wakeup(cpu_t *cp, int bound)
{
	struct machcpu	*mcpu = &(cp->cpu_m);
	volatile uint32_t *mcpu_mwait = mcpu->mcpu_mwait;
	cpupart_t	*cpu_part;
	uint_t		cpu_found;
	processorid_t	cpu_sid;

	cpu_part = cp->cpu_part;
	cpu_sid = cp->cpu_seqid;
	/*
	 * Clear the halted bit for that CPU since it will be woken up
	 * in a moment.
	 */
	if (bitset_in_set(&cpu_part->cp_haltset, cpu_sid)) {
		/*
		 * Clear the halted bit for that CPU since it will be
		 * poked in a moment.
		 */
		bitset_atomic_del(&cpu_part->cp_haltset, cpu_sid);

		/*
		 * We may find the current CPU present in the halted cpuset
		 * if we're in the context of an interrupt that occurred
		 * before we had a chance to clear our bit in cpu_idle().
		 * Waking ourself is obviously unnecessary, since if
		 * we're here, we're not halted.
		 */
		if (cp != CPU) {
			/*
			 * Use correct wakeup mechanism
			 */
			if ((mcpu_mwait != NULL) &&
			    (*mcpu_mwait == MWAIT_HALTED))
				MWAIT_WAKEUP(cp);
			else
				poke_cpu(cp->cpu_id);
		}
		return;
	} else {
		/*
		 * This cpu isn't halted, but it's idle or undergoing a
		 * context switch. No need to awaken anyone else.
		 */
		if (cp->cpu_thread == cp->cpu_idle_thread ||
		    cp->cpu_disp_flags & CPU_DISP_DONTSTEAL)
			return;
	}

	/*
	 * No need to wake up other CPUs if the thread we just enqueued
	 * is bound.
	 */
	if (bound)
		return;


	/*
	 * See if there's any other halted CPUs. If there are, then
	 * select one, and awaken it.
	 * It's possible that after we find a CPU, somebody else
	 * will awaken it before we get the chance.
	 * In that case, look again.
	 */
	do {
		cpu_found = bitset_find(&cpu_part->cp_haltset);
		if (cpu_found == (uint_t)-1)
			return;

	} while (bitset_atomic_test_and_del(&cpu_part->cp_haltset,
	    cpu_found) < 0);

	/*
	 * Must use correct wakeup mechanism to avoid lost wakeup of
	 * alternate cpu.
	 */
	if (cpu_found != CPU->cpu_seqid) {
		mcpu_mwait = cpu_seq[cpu_found]->cpu_m.mcpu_mwait;
		if ((mcpu_mwait != NULL) && (*mcpu_mwait == MWAIT_HALTED))
			MWAIT_WAKEUP(cpu_seq[cpu_found]);
		else
			poke_cpu(cpu_seq[cpu_found]->cpu_id);
	}
}

/*
 * Function called by CPU idle notification framework to check whether CPU
 * has been awakened. It will be called with interrupt disabled.
 * If CPU has been awakened, call cpu_idle_exit() to notify CPU idle
 * notification framework.
 */
static void
cpu_mwait_check_wakeup(void *arg)
{
	volatile uint32_t *mcpu_mwait = (volatile uint32_t *)arg;

	ASSERT(arg != NULL);
	if (*mcpu_mwait != MWAIT_HALTED) {
		/*
		 * CPU has been awakened, notify CPU idle notification system.
		 */
		cpu_idle_exit(CPU_IDLE_CB_FLAG_IDLE);
	} else {
		/*
		 * Toggle interrupt flag to detect pending interrupts.
		 * If interrupt happened, do_interrupt() will notify CPU idle
		 * notification framework so no need to call cpu_idle_exit()
		 * here.
		 */
		sti();
		SMT_PAUSE();
		cli();
	}
}

static void
cpu_mwait_ipi_check_wakeup(void *arg)
{
	volatile uint32_t *mcpu_mwait = (volatile uint32_t *)arg;

	ASSERT(arg != NULL);
	if (*mcpu_mwait != MWAIT_WAKEUP_IPI) {
		/*
		 * CPU has been awakened, notify CPU idle notification system.
		 */
		cpu_idle_exit(CPU_IDLE_CB_FLAG_IDLE);
	} else {
		/*
		 * Toggle interrupt flag to detect pending interrupts.
		 * If interrupt happened, do_interrupt() will notify CPU idle
		 * notification framework so no need to call cpu_idle_exit()
		 * here.
		 */
		sti();
		SMT_PAUSE();
		cli();
	}
}

static void
cpu_check_wakeup(void *arg)
{
	/*
	 * Toggle interrupt flag to detect pending interrupts.
	 * If interrupt happened, do_interrupt() will notify CPU idle
	 * notification framework so no need to call cpu_idle_exit() here.
	 */
	sti();
	SMT_PAUSE();
	cli();
}

/*
 * Handler to enter CPU C-states beyond 0. Meaning, execution is paused and some
 * amount of powered down.
 */
static void
cpu_cstate_enter(cpu_cstate_t *cstate)
{
	/*
	 * mcpu_mwait will be NULL if we are not actually using mwait. This
	 * function must be careful to function correctly with or without mwait.
	 */
	volatile uint32_t	*mcpu_mwait = CPU->cpu_m.mcpu_mwait;
	uint32_t		mwait_idle_state;
	cpu_t			*cpup = CPU;
	processorid_t		cpu_sid = cpup->cpu_seqid;
	cpupart_t		*cp = cpup->cpu_part;
	cstate_mechanism_t	type = cstate->cs_mechanism;
	uint32_t		cs_type = cstate->cs_type;
	int			hset_update = 1;
	cpu_idle_check_wakeup_t check_func = &cpu_check_wakeup;

	/*
	 * Set our mcpu_mwait here, so we can tell if anyone tries to
	 * wake us between now and when we actually idle.  No other cpu will
	 * attempt to set our mcpu_mwait until we add ourself to the haltset.
	 */
	if (mcpu_mwait != NULL) {
		if (type == CSTATE_MECHANISM_IOPORT) {
			mwait_idle_state = MWAIT_WAKEUP_IPI;
			check_func = &cpu_mwait_ipi_check_wakeup;
		} else {
			mwait_idle_state = MWAIT_HALTED;
			check_func = &cpu_mwait_check_wakeup;
		}
		*mcpu_mwait = mwait_idle_state;
	} else {
		/*
		 * Initialize mwait_idle_state, but with mcpu_mwait NULL we'll
		 * never actually use it here. "MWAIT_RUNNING" just
		 * distinguishes from the "WAKEUP_IPI" and "HALTED" cases above.
		 */
		mwait_idle_state = MWAIT_RUNNING;
	}

	/*
	 * If this CPU is online, and there are multiple CPUs
	 * in the system, then we should note our halting
	 * by adding ourselves to the partition's halted CPU
	 * bitmap. This allows other CPUs to find/awaken us when
	 * work becomes available.
	 */
	if (cpup->cpu_flags & CPU_OFFLINE || ncpus == 1)
		hset_update = 0;

	/*
	 * Add ourselves to the partition's halted CPUs bitmask
	 * and set our HALTED flag, if necessary.
	 *
	 * When a thread becomes runnable, it is placed on the queue
	 * and then the halted cpuset is checked to determine who
	 * (if anyone) should be awakened. We therefore need to first
	 * add ourselves to the halted cpuset, and and then check if there
	 * is any work available.
	 *
	 * Note that memory barriers after updating the HALTED flag
	 * are not necessary since an atomic operation (updating the bitmap)
	 * immediately follows. On x86 the atomic operation acts as a
	 * memory barrier for the update of cpu_disp_flags.
	 */
	if (hset_update) {
		cpup->cpu_disp_flags |= CPU_DISP_HALTED;
		bitset_atomic_add(&cp->cp_haltset, cpu_sid);
	}

	/*
	 * Check to make sure there's really nothing to do.  Work destined for
	 * this CPU may become available after this check. If we're
	 * mwait-halting we'll be notified through the clearing of our bit in
	 * the halted CPU bitmask, and a write to our mcpu_mwait.  Otherwise,
	 * we're hlt-based halting, and we'll be immediately woken by the
	 * pending interrupt.
	 *
	 * disp_anywork() checks disp_nrunnable, so we do not have to later.
	 */
	if (disp_anywork()) {
		if (hset_update) {
			cpup->cpu_disp_flags &= ~CPU_DISP_HALTED;
			bitset_atomic_del(&cp->cp_haltset, cpu_sid);
		}
		return;
	}

	/*
	 * We're on our way to being halted.
	 *
	 * Disable interrupts here so we will awaken immediately after halting
	 * if someone tries to poke us between now and the time we actually
	 * halt.
	 */
	cli();

	/*
	 * We check for the presence of our bit after disabling interrupts.
	 * If it's cleared, we'll return. If the bit is cleared after
	 * we check then the cstate_wakeup() will pop us out of the halted
	 * state.
	 *
	 * This means that the ordering of the cstate_wakeup() and the clearing
	 * of the bit by cpu_wakeup is important.
	 * cpu_wakeup() must clear our mc_haltset bit, and then call
	 * cstate_wakeup().
	 * cpu_deep_idle() must disable interrupts, then check for the bit.
	 */
	if (hset_update && bitset_in_set(&cp->cp_haltset, cpu_sid) == 0) {
		sti();
		cpup->cpu_disp_flags &= ~CPU_DISP_HALTED;
		return;
	}

	/*
	 * The check for anything locally runnable is here for performance
	 * and isn't needed for correctness. disp_nrunnable ought to be
	 * in our cache still, so it's inexpensive to check, and if there
	 * is anything runnable we won't have to wait for the poke.
	 */
	if (cpup->cpu_disp->disp_nrunnable != 0) {
		sti();
		if (hset_update) {
			cpup->cpu_disp_flags &= ~CPU_DISP_HALTED;
			bitset_atomic_del(&cp->cp_haltset, cpu_sid);
		}
		return;
	}

	/*
	 * Tell the cpu idle framework we're going to try idling.
	 *
	 * If cpu_idle_enter returns nonzero, we've found out at the last minute
	 * that we don't actually want to idle.
	 */
	boolean_t idle_ok = cpu_idle_enter(cs_type, 0, check_func,
	    (void *)mcpu_mwait) == 0;

	if (idle_ok) {
		if (type == CSTATE_MECHANISM_INSTRUCTION) {
			if (mcpu_mwait != NULL) {
				/*
				 * We're on our way to being halted.
				 * To avoid a lost wakeup, arm the monitor
				 * before checking if another cpu wrote to
				 * mcpu_mwait to wake us up.
				 */
				i86_monitor(mcpu_mwait, 0, 0);
				if (*mcpu_mwait == mwait_idle_state) {
					i86_mwait(cstate->cs_address, 1);
				}
			} else {
				mach_cpu_idle();
			}
		} else if (type == CSTATE_MECHANISM_IOPORT) {
			/*
			 * mcpu_mwait is not directly part of idling or wakeup
			 * in the I/O port case, but if available it can hint
			 * that we shouldn't actually try to idle because we're
			 * about to be woken up anyway.
			 *
			 * A trip through idle/wakeup can be upwards of a few
			 * microseconds, so avoiding that makes this a helpful
			 * optimization, but consulting mcpu_mwait is still not
			 * necessary for correctness here.
			 */
			if (mcpu_mwait == NULL ||
			    *mcpu_mwait == mwait_idle_state) {
				/*
				 * The idle call will cause us to
				 * halt which will cause the store
				 * buffer to be repartitioned,
				 * potentially exposing us to the Intel
				 * CPU vulnerability MDS. As such, we
				 * need to explicitly call that here.
				 * The other idle methods in this
				 * function do this automatically as
				 * part of the implementation of
				 * i86_mwait().
				 */
				x86_md_clear();
				(void) inl(cstate->cs_address);
			}
		}

		/*
		 * We've either idled and woken up, or decided not to idle.
		 * Either way, tell the cpu idle framework that we're not trying
		 * to idle anymore.
		 */
		cpu_idle_exit(CPU_IDLE_CB_FLAG_IDLE);
	}

	sti();

	/*
	 * We're no longer halted
	 */
	if (hset_update) {
		cpup->cpu_disp_flags &= ~CPU_DISP_HALTED;
		bitset_atomic_del(&cp->cp_haltset, cpu_sid);
	}
}

/*
 * Idle the present CPU, explicitly using hardware-supported C-states.
 */
void
cpu_cstate_idle(void)
{
	cpu_t *cp = CPU;
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	cmp_c_state_t *cs_data;
	hrtime_t start, end;
	uint32_t cs_indx;

	ASSERT(handle->cps_cstates != NULL);

	cs_data = mach_state->ms_cstate.cmp_state.cstate;
	ASSERT(cs_data != NULL);

	start = gethrtime_unscaled();

	cs_indx = cpupm_next_cstate(cs_data, handle, start);

	cpu_cstate_enter(&handle->cps_cstates[cs_indx]);

	end = gethrtime_unscaled();

	/*
	 * Update statistics
	 */
	cpupm_wakeup_cstate_data(cs_data, end);
}

boolean_t
cpu_deep_cstates_supported(void)
{
	extern int	idle_cpu_no_deep_c;

	if (idle_cpu_no_deep_c)
		return (B_FALSE);

	if (!cpuid_deep_cstates_supported())
		return (B_FALSE);

	if (!cpuid_arat_supported())
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Configure and collect C-state information based on the current processor's
 * model/family.
 */
static bool
cpu_idle_prepare_cstates(cpu_pm_state_t *handle)
{
	/*
	 * The current processor is not the one power management is being
	 * initialized for, but the family should be the same as the current
	 * processor. This is true even in a multi-socket configuration;
	 * to date x86 multi-socket configurations still require the same family
	 * if not the same model of processor in all sockets.
	 *
	 * Note that even if power management is initialized on the processor to
	 * be power managed, this code will still be correct. It will just be
	 * correct for the more obvious reason that it's discovering itself!
	 */
	switch (chiprev_family(cpuid_getchiprev(CPU))) {
		case X86_PF_AMD_MILAN:
		case X86_PF_AMD_GENOA:
		case X86_PF_AMD_TURIN:
		case X86_PF_AMD_DENSE_TURIN:
			cpupm_amd_cstates_zen(handle);
			break;
		default:
			/*
			 * Unknown processor type, we have no C-state
			 * information.
			 */
			return (false);
	}

	return (true);
}

static void
cpu_idle_free_cstate_data(cpu_pm_state_t *handle)
{
	if (handle != NULL) {
		if (handle->cps_cstates != NULL) {
			kmem_free(handle->cps_cstates,
			    handle->cps_ncstates * sizeof (cpu_cstate_t));
			handle->cps_cstates = NULL;
			handle->cps_ncstates = 0;
		}
	}
}

/*
 * Validate that this processor supports deep cstates and if so,
 * pick data tables to drive low-power idle management on this processor.
 *
 * We require ARAT on Oxide, which is a higher minimum functionality for
 * C-states than on i86pc but is present on all processors we support. Idle
 * routines on Oxide are somewhat simpler than their i86pc counterparts as a
 * result.
 */
static int
cpu_idle_init(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	char name[KSTAT_STRLEN];

	if (!cpu_idle_prepare_cstates(handle)) {
		cmn_err(CE_NOTE,
		    "Support for CPU deep idle states is being disabled due "
		    "to unknown processor type.");
		cpu_idle_fini(cp);
		return (-1);
	}

	/*
	 * There should be at least one C-state. If not,
	 * cpu_idle_prepare_cstates should have bailed us
	 * out of idle management..
	 */
	ASSERT3U(handle->cps_ncstates, >=, 1);

	for (uint_t i = 0; i < handle->cps_ncstates; i++) {
		cpu_cstate_t *cstate = &handle->cps_cstates[i];
		(void) snprintf(name, KSTAT_STRLEN - 1, "c%d", cstate->cs_type);
		/*
		 * Allocate, initialize and install cstate kstat
		 */
		cstate->cs_ksp = kstat_create("cstate", cp->cpu_id,
		    name, "misc",
		    KSTAT_TYPE_NAMED,
		    sizeof (cpu_idle_kstat) / sizeof (kstat_named_t),
		    KSTAT_FLAG_VIRTUAL);

		if (cstate->cs_ksp == NULL) {
			cmn_err(CE_NOTE, "kstat_create(c_state) fail");
		} else {
			cstate->cs_ksp->ks_data = &cpu_idle_kstat;
			cstate->cs_ksp->ks_lock = &cpu_idle_mutex;
			cstate->cs_ksp->ks_update = cpu_idle_kstat_update;
			cstate->cs_ksp->ks_data_size += MAXNAMELEN;
			cstate->cs_ksp->ks_private = cstate;
			kstat_install(cstate->cs_ksp);
		}
	}

	cpupm_alloc_domains(cp, CPUPM_C_STATES);
	cpupm_alloc_ms_cstate(cp);

	if (cpu_deep_cstates_supported()) {
		mutex_enter(&cpu_idle_callb_mutex);
		if (cpu_deep_idle_callb_id == (callb_id_t)0)
			cpu_deep_idle_callb_id = callb_add(&cpu_deep_idle_callb,
			    (void *)NULL, CB_CL_CPU_DEEP_IDLE, "cpu_deep_idle");
		if (cpu_idle_cpr_callb_id == (callb_id_t)0)
			cpu_idle_cpr_callb_id = callb_add(&cpu_idle_cpr_callb,
			    (void *)NULL, CB_CL_CPR_PM, "cpu_idle_cpr");
		mutex_exit(&cpu_idle_callb_mutex);

		/*
		 * Unlike i86pc, no need to mess with ACPI_BITREG_BUS_MASTER_RLD
		 * here; supported processors maintain cache coherency even in
		 * low-power states.
		 */
	}

	return (0);
}

/*
 * Free resources allocated by cpu_idle_init().
 */
static void
cpu_idle_fini(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;

	/*
	 * idle cpu points back to the generic one
	 */
	idle_cpu = cp->cpu_m.mcpu_idle_cpu = non_deep_idle_cpu;
	disp_enq_thread = non_deep_idle_disp_enq_thread;

	if (handle->cps_cstates != NULL) {
		for (uint_t i = 0; i < handle->cps_ncstates; i++) {
			cpu_cstate_t *cstate = &handle->cps_cstates[i];

			if (cstate->cs_ksp != NULL)
				kstat_delete(cstate->cs_ksp);
		}
	}

	cpupm_free_ms_cstate(cp);
	cpupm_free_domains(&cpupm_cstate_domains);
	cpu_idle_free_cstate_data(handle);

	mutex_enter(&cpu_idle_callb_mutex);
	if (cpu_deep_idle_callb_id != (callb_id_t)0) {
		(void) callb_delete(cpu_deep_idle_callb_id);
		cpu_deep_idle_callb_id = (callb_id_t)0;
	}
	if (cpu_idle_cpr_callb_id != (callb_id_t)0) {
		(void) callb_delete(cpu_idle_cpr_callb_id);
		cpu_idle_cpr_callb_id = (callb_id_t)0;
	}
	mutex_exit(&cpu_idle_callb_mutex);
}

/*
 * This function is introduced here to solve a race condition
 * between the master and the slave to touch c-state data structure.
 * After the slave calls this idle function to switch to the non
 * deep idle function, the master can go on to reclaim the resource.
 */
static void
cpu_idle_stop_sync(void)
{
	/* switch to the non deep idle function */
	CPU->cpu_m.mcpu_idle_cpu = non_deep_idle_cpu;
}

static void
cpu_idle_stop(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpu_pm_state_t *handle = mach_state->ms_pm_handle;
	uint_t i = 0;

	mutex_enter(&cpu_idle_callb_mutex);
	if (idle_cpu == cpu_idle_adaptive) {
		/*
		 * invoke the slave to call synchronous idle function.
		 */
		cp->cpu_m.mcpu_idle_cpu = cpu_idle_stop_sync;
		poke_cpu(cp->cpu_id);

		/*
		 * wait until the slave switchs to non deep idle function,
		 * so that the master is safe to go on to reclaim the resource.
		 */
		while (cp->cpu_m.mcpu_idle_cpu != non_deep_idle_cpu) {
			drv_usecwait(10);
			if ((++i % CPU_IDLE_STOP_TIMEOUT) == 0)
				cmn_err(CE_NOTE, "!cpu_idle_stop: the slave"
				    " idle stop timeout");
		}
	}
	mutex_exit(&cpu_idle_callb_mutex);

	if (handle->cps_cstates != NULL) {
		for (uint_t i = 0; i < handle->cps_ncstates; i++) {
			cpu_cstate_t *cstate = &handle->cps_cstates[i];

			if (cstate->cs_ksp != NULL)
				kstat_delete(cstate->cs_ksp);
		}
	}

	cpupm_free_ms_cstate(cp);
	cpupm_remove_domains(cp, CPUPM_C_STATES, &cpupm_cstate_domains);
	cpu_idle_free_cstate_data(handle);
}

static boolean_t
cpu_deep_idle_callb(void *arg, int code)
{
	boolean_t rslt = B_TRUE;

	mutex_enter(&cpu_idle_callb_mutex);
	switch (code) {
	case PM_DEFAULT_CPU_DEEP_IDLE:
		/*
		 * Default policy is same as enable
		 */
		/*FALLTHROUGH*/
	case PM_ENABLE_CPU_DEEP_IDLE:
		if ((cpu_idle_cfg_state & CPU_IDLE_DEEP_CFG) == 0)
			break;

		disp_enq_thread = cstate_wakeup;
		idle_cpu = cpu_idle_adaptive;
		cpu_idle_cfg_state &= ~CPU_IDLE_DEEP_CFG;
		break;

	case PM_DISABLE_CPU_DEEP_IDLE:
		if (cpu_idle_cfg_state & CPU_IDLE_DEEP_CFG)
			break;

		idle_cpu = non_deep_idle_cpu;
		disp_enq_thread = non_deep_idle_disp_enq_thread;
		cpu_idle_cfg_state |= CPU_IDLE_DEEP_CFG;
		break;

	default:
		cmn_err(CE_NOTE, "!cpu deep_idle_callb: invalid code %d\n",
		    code);
		break;
	}
	mutex_exit(&cpu_idle_callb_mutex);
	return (rslt);
}

static boolean_t
cpu_idle_cpr_callb(void *arg, int code)
{
	boolean_t rslt = B_TRUE;

	mutex_enter(&cpu_idle_callb_mutex);
	switch (code) {
	case CB_CODE_CPR_RESUME:
		/*
		 * Do not enable dispatcher hooks if disabled by user.
		 */
		if (cpu_idle_cfg_state & CPU_IDLE_DEEP_CFG)
			break;

		disp_enq_thread = cstate_wakeup;
		idle_cpu = cpu_idle_adaptive;
		break;

	case CB_CODE_CPR_CHKPT:
		idle_cpu = non_deep_idle_cpu;
		disp_enq_thread = non_deep_idle_disp_enq_thread;
		break;

	default:
		cmn_err(CE_NOTE, "!cpudvr cpr_callb: invalid code %d\n", code);
		break;
	}
	mutex_exit(&cpu_idle_callb_mutex);
	return (rslt);
}
