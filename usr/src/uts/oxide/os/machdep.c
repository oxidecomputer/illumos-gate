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
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2020 Joyent, Inc.
 * Copyright 2022 Oxide Computer Co.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/segments.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/vm.h>

#include <sys/kernel_ipcc.h>

#include <sys/disp.h>
#include <sys/class.h>

#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/kmem.h>

#include <sys/reboot.h>
#include <sys/uadmin.h>
#include <sys/callb.h>

#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/file.h>

#include <sys/procfs.h>
#include <sys/acct.h>

#include <sys/vfs.h>
#include <sys/dnlc.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/debug.h>

#include <sys/dumphdr.h>
#include <sys/bootconf.h>
#include <sys/varargs.h>
#include <sys/promif.h>
#include <sys/modctl.h>

#include <sys/consdev.h>
#include <sys/frame.h>

#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/psw.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/clock.h>
#include <sys/tss.h>
#include <sys/cpu.h>
#include <sys/stack.h>
#include <sys/trap.h>
#include <sys/pic.h>
#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/seg_kp.h>
#include <vm/hat_i86.h>
#include <sys/swap.h>
#include <sys/thread.h>
#include <sys/sysconf.h>
#include <sys/vm_machparam.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/machlock.h>
#include <sys/x_call.h>
#include <sys/instance.h>

#include <sys/time.h>
#include <sys/smp_impldefs.h>
#include <sys/psm_types.h>
#include <sys/atomic.h>
#include <sys/panic.h>
#include <sys/cpuvar.h>
#include <sys/dtrace.h>
#include <sys/bl.h>
#include <sys/nvpair.h>
#include <sys/x86_archext.h>
#include <sys/pool_pset.h>
#include <sys/autoconf.h>
#include <sys/mem.h>
#include <sys/dumphdr.h>
#include <sys/compress.h>
#include <sys/cpu_module.h>

#include <sys/machelf.h>
#include <sys/kobj.h>
#include <sys/multiboot.h>

#ifdef	TRAPTRACE
#include <sys/traptrace.h>
#endif	/* TRAPTRACE */

#include <c2/audit.h>
#include <sys/clock_impl.h>

extern void audit_enterprom(int);
extern void audit_exitprom(int);
extern void prom_poll_enter(void);

/*
 * Occassionally the kernel knows better whether to power-off or reboot.
 */
int force_shutdown_method = AD_UNKNOWN;

/*
 * The panicbuf array is used to record messages and state:
 */
char panicbuf[PANICBUFSIZE];

/*
 * maxphys - used during physio
 * klustsize - used for klustering by swapfs and specfs
 */
int maxphys = 56 * 1024;    /* XXX See vm_subr.c - max b_count in physio */
int klustsize = 56 * 1024;

/*
 * defined here, though unused on x86,
 * to make kstat_fr.c happy.
 */
int vac;

void debug_enter(char *);

extern void pm_cfb_check_and_powerup(void);
extern void pm_cfb_rele(void);

/*
 * Instructions to enable or disable SMAP, respectively.
 */
static const uint8_t clac_instr[3] = { 0x0f, 0x01, 0xca };
static const uint8_t stac_instr[3] = { 0x0f, 0x01, 0xcb };

/*
 * Stop the other CPUs by cross-calling them and forcing them to enter
 * the provided function.
 */
static void
stop_other_cpus(cpu_t *cp, xc_func_t func)
{
	processorid_t i;
	cpuset_t xcset;

	(void) splzs();

	CPUSET_ALL_BUT(xcset, cp->cpu_id);
	xc_priority(0, 0, 0, CPUSET2BV(xcset), func);

	for (i = 0; i < NCPU; i++) {
		if (i != cp->cpu_id && cpu[i] != NULL &&
		    (cpu[i]->cpu_flags & CPU_EXISTS)) {
			cpu[i]->cpu_flags |= CPU_QUIESCED;
		}
	}
}

static void __NORETURN
cpu_hlt_loop(void)
{
	for (;;)
		mach_cpu_idle();
}

void
reset(void)
{
	kernel_ipcc_reboot();
	cpu_hlt_loop();
}

static void
poweroff(void)
{
	kernel_ipcc_poweroff();
	cpu_hlt_loop();
}

static void
mdboot_stop_other_cpus(void)
{
	stop_other_cpus(CPU, (xc_func_t)cpu_hlt_loop);
}

/*
 * Machine dependent code to reboot/halt.
 *
 * "mdep" is interpreted as a character pointer; if non-null, it is a pointer
 * to a string to be used as the argument string when rebooting.
 *
 * "invoke_cb" is a boolean. It is set to true when mdboot() can safely
 * invoke CB_CL_MDBOOT callbacks before shutting the system down, i.e. when
 * we are in a normal shutdown sequence (interrupts are not blocked, the
 * system is not panicking or being suspended).
 *
 * This function is called from kadmin() and from panicsys(). When called from
 * panicsys(), the global 'panicstr' will be non-NULL and this can be used to
 * differentiate between the two calling paths. When we are panicking, we don't
 * need to stop the other CPUs or disable pre-emption here as it will already
 * have been done.
 */
void
mdboot(int cmd, int fcn, char *mdep, boolean_t invoke_cb)
{
	static boolean_t is_first_quiesce = B_TRUE;
	static boolean_t is_first_reset = B_TRUE;

	if (fcn == AD_FASTREBOOT)
		fcn = AD_BOOT;

	if (panicstr == NULL) {
		kpreempt_disable();
		affinity_set(CPU_CURRENT);
	}

	if (force_shutdown_method != AD_UNKNOWN)
		fcn = force_shutdown_method;

	/*
	 * XXX - rconsvp is set to NULL to ensure that output messages
	 * are sent to the underlying "hardware" device using the
	 * monitor's printf routine since we are in the process of
	 * either rebooting or halting the machine.
	 */
	rconsvp = NULL;

	/*
	 * Print the reboot message now, before pausing other cpus.
	 * There is a race condition in the printing support that
	 * can deadlock multiprocessor machines. In particular, cprintf()
	 * will use a cross call to post the log message if our priority is
	 * too high.
	 */
	if (!(fcn == AD_HALT || fcn == AD_POWEROFF))
		prom_printf("rebooting...\n");

	/* make sure there are no more changes to the device tree */
	devtree_freeze();

	if (invoke_cb)
		(void) callb_execute_class(CB_CL_MDBOOT, 0);

	/*
	 * Clear any unresolved UEs from memory.
	 */
	page_retire_mdboot();

	/*
	 * stop other cpus and raise our priority.  since there is only
	 * one active cpu after this, and our priority will be too high
	 * for us to be preempted, we're essentially single threaded
	 * from here on out.
	 */
	(void) spl6();
	if (panicstr == NULL) {
		mutex_enter(&cpu_lock);
		pause_cpus(NULL, NULL);
		mutex_exit(&cpu_lock);
	}

	/*
	 * Try to quiesce devices.
	 */
	if (is_first_quiesce) {
		int reset_status = 0;

		/*
		 * Clear is_first_quiesce before calling quiesce_devices()
		 * so that if quiesce_devices() causes panics, it will not
		 * be invoked again.
		 */
		is_first_quiesce = B_FALSE;

		quiesce_active = 1;
		quiesce_devices(ddi_root_node(), &reset_status);
		quiesce_active = 0;
	}

	/*
	 * Try to reset devices. reset_leaves() should only be called
	 * when there are no other threads that could be accessing devices.
	 */
	if (is_first_reset && quiesce_active) {
		/*
		 * Clear is_first_reset before calling reset_devices()
		 * so that if reset_devices() causes panics, it will not
		 * be invoked again.
		 */
		is_first_reset = B_FALSE;
		reset_leaves();
	}

	/*
	 * quiescing can result in calls to cmn_err(), particularly in a DEBUG
	 * kernel. If we stop the other CPUs earlier than here, that printing
	 * can result in a deadlock.
	 */
	if (panicstr == NULL)
		mdboot_stop_other_cpus();
	prom_poll_enter();

	(void) spl8();
	(*psm_shutdownf)(cmd, fcn);

	if (fcn == AD_HALT || fcn == AD_POWEROFF)
		poweroff();
	else
		reset();

	cpu_hlt_loop();
	/*NOTREACHED*/
}

/* mdpreboot - may be called prior to mdboot while root fs still mounted */
/*ARGSUSED*/
void
mdpreboot(int cmd, int fcn, char *mdep)
{
	if (fcn == AD_FASTREBOOT) {
		fcn = AD_BOOT;
		cmn_err(CE_WARN,
		    "Fast reboot is not supported on this platform");
	}

	(*psm_preshutdownf)(cmd, fcn);
}

/*
 *	Machine dependent abort sequence handling
 */
void
abort_sequence_enter(char *msg)
{
	if (abort_enable == 0) {
		if (AU_ZONE_AUDITING(GET_KCTX_GZ))
			audit_enterprom(0);
		return;
	}
	if (AU_ZONE_AUDITING(GET_KCTX_GZ))
		audit_enterprom(1);
	debug_enter(msg);
	if (AU_ZONE_AUDITING(GET_KCTX_GZ))
		audit_exitprom(1);
}

/*
 * Enter debugger.  Called when the user types ctrl-alt-d or whenever
 * code wants to enter the debugger and possibly resume later.
 *
 * msg:	message to print, possibly NULL.
 */
void
debug_enter(char *msg)
{
	if (dtrace_debugger_init != NULL)
		(*dtrace_debugger_init)();

	if (msg != NULL || (boothowto & RB_DEBUG))
		prom_printf("\n");

	if (msg != NULL)
		prom_printf("%s\n", msg);

	if (boothowto & RB_DEBUG)
		kmdb_enter();

	if (dtrace_debugger_fini != NULL)
		(*dtrace_debugger_fini)();
}

/*
 * On other platforms this routine should halt the machine and return to the
 * monitor, usually requesting a keypress before proceeding to reboot.
 * For Oxide, it triggers a reboot straight away if KMDB is not present.
 *
 * XXX - This function used to call prom_exit_to_mon() but that prompts a
 * non-existent user to press a key via prom_reboot_prompt(), which is in
 * shared code. If that prompt function was moved into machdep, we could go
 * back to calling prom_exit_to_mon() again. That will also help other reboot
 * paths that call prom_panic().
 */
void
halt(char *s)
{
	mdboot_stop_other_cpus();
	if (s)
		prom_printf("(%s) \n", s);
	prom_poll_enter();
	if (boothowto & RB_DEBUG)
		kmdb_enter();
	reset();
	/*NOTREACHED*/
}

/*
 * Initiate interrupt redistribution.
 */
void
i_ddi_intr_redist_all_cpus()
{
}

/*
 * XXX These probably ought to live somewhere else
 * XXX They are called from mem.c
 */

/*
 * Convert page frame number to an OBMEM page frame number
 * (i.e. put in the type bits -- zero for this implementation)
 */
pfn_t
impl_obmem_pfnum(pfn_t pf)
{
	return (pf);
}

#ifdef	NM_DEBUG
int nmi_test = 0;	/* checked in intentry.s during clock int */
int nmtest = -1;
nmfunc1(int arg, struct regs *rp)
{
	printf("nmi called with arg = %x, regs = %x\n", arg, rp);
	nmtest += 50;
	if (arg == nmtest) {
		printf("ip = %x\n", rp->r_pc);
		return (1);
	}
	return (0);
}

#endif

#include <sys/bootsvcs.h>

/* Hacked up initialization for initial kernel check out is HERE. */
/* The basic steps are: */
/*	kernel bootfuncs definition/initialization for KADB */
/*	kadb bootfuncs pointer initialization */
/*	putchar/getchar (interrupts disabled) */

/* kadb bootfuncs pointer initialization */

int
sysp_getchar()
{
	int i;
	ulong_t s;

	if (cons_polledio == NULL) {
		/* Uh oh */
		prom_printf("getchar called with no console\n");
		for (;;)
			/* LOOP FOREVER */;
	}

	s = clear_int_flag();
	i = cons_polledio->cons_polledio_getchar(
	    cons_polledio->cons_polledio_argument);
	restore_int_flag(s);
	return (i);
}

void
sysp_putchar(int c)
{
	ulong_t s;

	/*
	 * We have no alternative but to drop the output on the floor.
	 */
	if (cons_polledio == NULL ||
	    cons_polledio->cons_polledio_putchar == NULL)
		return;

	s = clear_int_flag();
	cons_polledio->cons_polledio_putchar(
	    cons_polledio->cons_polledio_argument, c);
	restore_int_flag(s);
}

int
sysp_ischar()
{
	int i;
	ulong_t s;

	if (cons_polledio == NULL ||
	    cons_polledio->cons_polledio_ischar == NULL)
		return (0);

	s = clear_int_flag();
	i = cons_polledio->cons_polledio_ischar(
	    cons_polledio->cons_polledio_argument);
	restore_int_flag(s);
	return (i);
}

int
goany(void)
{
	prom_printf("Type any key to continue ");
	(void) prom_getchar();
	prom_printf("\n");
	return (1);
}

static struct boot_syscalls kern_sysp = {
	sysp_getchar,	/*	unchar	(*getchar)();	7  */
	sysp_putchar,	/*	int	(*putchar)();	8  */
	sysp_ischar,	/*	int	(*ischar)();	9  */
};

void
kadb_uses_kernel()
{
	/*
	 * This routine is now totally misnamed, since it does not in fact
	 * control kadb's I/O; it only controls the kernel's prom_* I/O.
	 */
	sysp = &kern_sysp;
}

/*
 *	the interface to the outside world
 */

/*
 * set_idle_cpu is called from idle() when a CPU becomes idle.
 */
/*LINTED: static unused */
static uint_t last_idle_cpu;

/*ARGSUSED*/
void
set_idle_cpu(int cpun)
{
	last_idle_cpu = cpun;
	(*psm_set_idle_cpuf)(cpun);
}

/*
 * unset_idle_cpu is called from idle() when a CPU is no longer idle.
 */
/*ARGSUSED*/
void
unset_idle_cpu(int cpun)
{
	(*psm_unset_idle_cpuf)(cpun);
}

/*
 * This routine is almost correct now, but not quite.  It still needs the
 * equivalent concept of "hres_last_tick", just like on the sparc side.
 * The idea is to take a snapshot of the hi-res timer while doing the
 * hrestime_adj updates under hres_lock in locore, so that the small
 * interval between interrupt assertion and interrupt processing is
 * accounted for correctly.  Once we have this, the code below should
 * be modified to subtract off hres_last_tick rather than hrtime_base.
 *
 * I'd have done this myself, but I don't have source to all of the
 * vendor-specific hi-res timer routines (grrr...).  The generic hook I
 * need is something like "gethrtime_unlocked()", which would be just like
 * gethrtime() but would assume that you're already holding CLOCK_LOCK().
 * This is what the GET_HRTIME() macro is for on sparc (although it also
 * serves the function of making time available without a function call
 * so you don't take a register window overflow while traps are disabled).
 */
void
pc_gethrestime(timestruc_t *tp)
{
	int lock_prev;
	timestruc_t now;
	int nslt;		/* nsec since last tick */
	int adj;		/* amount of adjustment to apply */

loop:
	lock_prev = hres_lock;
	now = hrestime;
	nslt = (int)(gethrtime() - hres_last_tick);
	if (nslt < 0) {
		/*
		 * nslt < 0 means a tick came between sampling
		 * gethrtime() and hres_last_tick; restart the loop
		 */

		goto loop;
	}
	now.tv_nsec += nslt;
	if (hrestime_adj != 0) {
		if (hrestime_adj > 0) {
			adj = (nslt >> ADJ_SHIFT);
			if (adj > hrestime_adj)
				adj = (int)hrestime_adj;
		} else {
			adj = -(nslt >> ADJ_SHIFT);
			if (adj < hrestime_adj)
				adj = (int)hrestime_adj;
		}
		now.tv_nsec += adj;
	}
	while ((unsigned long)now.tv_nsec >= NANOSEC) {

		/*
		 * We might have a large adjustment or have been in the
		 * debugger for a long time; take care of (at most) four
		 * of those missed seconds (tv_nsec is 32 bits, so
		 * anything >4s will be wrapping around).  However,
		 * anything more than 2 seconds out of sync will trigger
		 * timedelta from clock() to go correct the time anyway,
		 * so do what we can, and let the big crowbar do the
		 * rest.  A similar correction while loop exists inside
		 * hres_tick(); in all cases we'd like tv_nsec to
		 * satisfy 0 <= tv_nsec < NANOSEC to avoid confusing
		 * user processes, but if tv_sec's a little behind for a
		 * little while, that's OK; time still monotonically
		 * increases.
		 */

		now.tv_nsec -= NANOSEC;
		now.tv_sec++;
	}
	if ((hres_lock & ~1) != lock_prev)
		goto loop;

	*tp = now;
}

void
gethrestime_lasttick(timespec_t *tp)
{
	int s;

	s = hr_clock_lock();
	*tp = hrestime;
	hr_clock_unlock(s);
}

time_t
gethrestime_sec(void)
{
	timestruc_t now;

	gethrestime(&now);
	return (now.tv_sec);
}

/*
 * Initialize a kernel thread's stack
 */

caddr_t
thread_stk_init(caddr_t stk)
{
	ASSERT(((uintptr_t)stk & (STACK_ALIGN - 1)) == 0);
	return (stk - SA(MINFRAME));
}

/*
 * Initialize lwp's kernel stack.
 */

#ifdef TRAPTRACE
/*
 * There's a tricky interdependency here between use of sysenter and
 * TRAPTRACE which needs recording to avoid future confusion (this is
 * about the third time I've re-figured this out ..)
 *
 * Here's how debugging lcall works with TRAPTRACE.
 *
 * 1 We're in userland with a breakpoint on the lcall instruction.
 * 2 We execute the instruction - the instruction pushes the userland
 *   %ss, %esp, %efl, %cs, %eip on the stack and zips into the kernel
 *   via the call gate.
 * 3 The hardware raises a debug trap in kernel mode, the hardware
 *   pushes %efl, %cs, %eip and gets to dbgtrap via the idt.
 * 4 dbgtrap pushes the error code and trapno and calls cmntrap
 * 5 cmntrap finishes building a trap frame
 * 6 The TRACE_REGS macros in cmntrap copy a REGSIZE worth chunk
 *   off the stack into the traptrace buffer.
 *
 * This means that the traptrace buffer contains the wrong values in
 * %esp and %ss, but everything else in there is correct.
 *
 * Here's how debugging sysenter works with TRAPTRACE.
 *
 * a We're in userland with a breakpoint on the sysenter instruction.
 * b We execute the instruction - the instruction pushes -nothing-
 *   on the stack, but sets %cs, %eip, %ss, %esp to prearranged
 *   values to take us to sys_sysenter, at the top of the lwp's
 *   stack.
 * c goto 3
 *
 * At this point, because we got into the kernel without the requisite
 * five pushes on the stack, if we didn't make extra room, we'd
 * end up with the TRACE_REGS macro fetching the saved %ss and %esp
 * values from negative (unmapped) stack addresses -- which really bites.
 * That's why we do the '-= 8' below.
 *
 * XXX	Note that reading "up" lwp0's stack works because t0 is declared
 *	right next to t0stack in locore.s
 */
#endif

caddr_t
lwp_stk_init(klwp_t *lwp, caddr_t stk)
{
	caddr_t oldstk;
	struct pcb *pcb = &lwp->lwp_pcb;

	oldstk = stk;
	stk -= SA(sizeof (struct regs) + SA(MINFRAME));
#ifdef TRAPTRACE
	stk -= 2 * sizeof (greg_t); /* space for phony %ss:%sp (see above) */
#endif
	stk = (caddr_t)((uintptr_t)stk & ~(STACK_ALIGN - 1ul));
	bzero(stk, oldstk - stk);
	lwp->lwp_regs = (void *)(stk + SA(MINFRAME));

	/*
	 * Arrange that the virtualized %fs and %gs GDT descriptors
	 * have a well-defined initial state (present, ring 3
	 * and of type data).
	 */
	if (lwp_getdatamodel(lwp) == DATAMODEL_NATIVE)
		pcb->pcb_fsdesc = pcb->pcb_gsdesc = zero_udesc;
	else
		pcb->pcb_fsdesc = pcb->pcb_gsdesc = zero_u32desc;
	lwp_installctx(lwp);
	return (stk);
}

/*
 * Use this opportunity to free any dynamically allocated fp storage.
 */
void
lwp_stk_fini(klwp_t *lwp)
{
	fp_lwp_cleanup(lwp);
}

void
lwp_fp_init(klwp_t *lwp)
{
	fp_lwp_init(lwp);
}

/*
 * If we're not the panic CPU, we wait in panic_idle for reboot.
 */
void
panic_idle(void)
{
	splx(ipltospl(CLOCK_LEVEL));
	(void) setjmp(&curthread->t_pcb);

	dumpsys_helper();

	for (;;)
		i86_halt();
}

/*
 * Stop the other CPUs by cross-calling them and forcing them to enter
 * the panic_idle() loop above.
 */
void
panic_stopcpus(cpu_t *cp, kthread_t *t, int spl)
{
	stop_other_cpus(cp, (xc_func_t)panic_idle);
}

/*
 * Platform callback following each entry to panicsys().
 */
/*ARGSUSED*/
void
panic_enter_hw(int spl)
{
	/* Nothing to do here */
}

/*
 * Platform-specific code to execute after panicstr is set: we invoke
 * the PSM entry point to indicate that a panic has occurred.
 */
/*ARGSUSED*/
void
panic_quiesce_hw(panic_data_t *pdp)
{
	psm_notifyf(PSM_PANIC_ENTER);

	cmi_panic_callback();

#ifdef	TRAPTRACE
	/*
	 * Turn off TRAPTRACE
	 */
	TRAPTRACE_FREEZE;
#endif	/* TRAPTRACE */
}

/*
 * Platform callback prior to writing crash dump.
 */
/*ARGSUSED*/
void
panic_dump_hw(int spl)
{
	/* Nothing to do here */
}

void *
plat_traceback(void *fpreg)
{
	return (fpreg);
}

/*ARGSUSED*/
void
plat_tod_fault(enum tod_fault_type tod_bad)
{}

/*ARGSUSED*/
int
blacklist(int cmd, const char *scheme, nvlist_t *fmri, const char *class)
{
	return (ENOTSUP);
}

/*
 * The underlying console output routines are protected by raising IPL in case
 * we are still calling into the early boot services.  Once we start calling
 * the kernel console emulator, it will disable interrupts completely during
 * character rendering (see sysp_putchar, for example).  Refer to the comments
 * and code in common/os/console.c for more information on these callbacks.
 */
/*ARGSUSED*/
int
console_enter(int busy)
{
	return (splzs());
}

/*ARGSUSED*/
void
console_exit(int busy, int spl)
{
	splx(spl);
}

/*
 * Allocate a region of virtual address space, unmapped.
 * Stubbed out except on sparc, at least for now.
 */
/*ARGSUSED*/
void *
boot_virt_alloc(void *addr, size_t size)
{
	return (addr);
}

void
tenmicrosec(void)
{
	extern int gethrtime_hires;

	if (gethrtime_hires) {
		hrtime_t start, end;
		start = end =  gethrtime();
		while ((end - start) < (10 * (NANOSEC / MICROSEC))) {
			SMT_PAUSE();
			end = gethrtime();
		}
	} else {
		panic("TSC was not calibrated!");
	}
}

/*
 * get_cpu_mstate() is passed an array of timestamps, NCMSTATES
 * long, and it fills in the array with the time spent on cpu in
 * each of the mstates, where time is returned in nsec.
 *
 * No guarantee is made that the returned values in times[] will
 * monotonically increase on sequential calls, although this will
 * be true in the long run. Any such guarantee must be handled by
 * the caller, if needed. This can happen if we fail to account
 * for elapsed time due to a generation counter conflict, yet we
 * did account for it on a prior call (see below).
 *
 * The complication is that the cpu in question may be updating
 * its microstate at the same time that we are reading it.
 * Because the microstate is only updated when the CPU's state
 * changes, the values in cpu_intracct[] can be indefinitely out
 * of date. To determine true current values, it is necessary to
 * compare the current time with cpu_mstate_start, and add the
 * difference to times[cpu_mstate].
 *
 * This can be a problem if those values are changing out from
 * under us. Because the code path in new_cpu_mstate() is
 * performance critical, we have not added a lock to it. Instead,
 * we have added a generation counter. Before beginning
 * modifications, the counter is set to 0. After modifications,
 * it is set to the old value plus one.
 *
 * get_cpu_mstate() will not consider the values of cpu_mstate
 * and cpu_mstate_start to be usable unless the value of
 * cpu_mstate_gen is both non-zero and unchanged, both before and
 * after reading the mstate information. Note that we must
 * protect against out-of-order loads around accesses to the
 * generation counter. Also, this is a best effort approach in
 * that we do not retry should the counter be found to have
 * changed.
 *
 * cpu_intracct[] is used to identify time spent in each CPU
 * mstate while handling interrupts. Such time should be reported
 * against system time, and so is subtracted out from its
 * corresponding cpu_acct[] time and added to
 * cpu_acct[CMS_SYSTEM].
 */

void
get_cpu_mstate(cpu_t *cpu, hrtime_t *times)
{
	int i;
	hrtime_t now, start;
	uint16_t gen;
	uint16_t state;
	hrtime_t intracct[NCMSTATES];

	/*
	 * Load all volatile state under the protection of membar.
	 * cpu_acct[cpu_mstate] must be loaded to avoid double counting
	 * of (now - cpu_mstate_start) by a change in CPU mstate that
	 * arrives after we make our last check of cpu_mstate_gen.
	 */

	now = gethrtime_unscaled();
	gen = cpu->cpu_mstate_gen;

	membar_consumer();	/* guarantee load ordering */
	start = cpu->cpu_mstate_start;
	state = cpu->cpu_mstate;
	for (i = 0; i < NCMSTATES; i++) {
		intracct[i] = cpu->cpu_intracct[i];
		times[i] = cpu->cpu_acct[i];
	}
	membar_consumer();	/* guarantee load ordering */

	if (gen != 0 && gen == cpu->cpu_mstate_gen && now > start)
		times[state] += now - start;

	for (i = 0; i < NCMSTATES; i++) {
		if (i == CMS_SYSTEM)
			continue;
		times[i] -= intracct[i];
		if (times[i] < 0) {
			intracct[i] += times[i];
			times[i] = 0;
		}
		times[CMS_SYSTEM] += intracct[i];
		scalehrtime(&times[i]);
	}
	scalehrtime(&times[CMS_SYSTEM]);
}

/*
 * This is a version of the rdmsr instruction that allows
 * an error code to be returned in the case of failure.
 */
int
checked_rdmsr(uint_t msr, uint64_t *value)
{
	if (!is_x86_feature(x86_featureset, X86FSET_MSR))
		return (ENOTSUP);
	*value = rdmsr(msr);
	return (0);
}

/*
 * This is a version of the wrmsr instruction that allows
 * an error code to be returned in the case of failure.
 */
int
checked_wrmsr(uint_t msr, uint64_t value)
{
	if (!is_x86_feature(x86_featureset, X86FSET_MSR))
		return (ENOTSUP);
	wrmsr(msr, value);
	return (0);
}

/*
 * The mem driver's usual method of using hat_devload() to establish a
 * temporary mapping will not work for foreign pages mapped into this
 * domain or for the special hypervisor-provided pages.  For the foreign
 * pages, we often don't know which domain owns them, so we can't ask the
 * hypervisor to set up a new mapping.  For the other pages, we don't have
 * a pfn, so we can't create a new PTE.  For these special cases, we do a
 * direct uiomove() from the existing kernel virtual address.
 */
/*ARGSUSED*/
int
plat_mem_do_mmio(struct uio *uio, enum uio_rw rw)
{
	return (ENOTSUP);
}

pgcnt_t
num_phys_pages()
{
	pgcnt_t npages = 0;
	struct memlist *mp;

	for (mp = phys_install; mp != NULL; mp = mp->ml_next)
		npages += mp->ml_size >> PAGESHIFT;

	return (npages);
}

/* cpu threshold for compressed dumps */
uint_t dump_plat_mincpu_default = DUMP_PLAT_X86_64_MINCPU;

int
dump_plat_addr()
{
	return (0);
}

void
dump_plat_pfn()
{
}

/*ARGSUSED*/
int
dump_plat_data(void *dump_cbuf)
{
	return (0);
}

/*
 * Calculates a linear address, given the CS selector and PC values
 * by looking up the %cs selector process's LDT or the CPU's GDT.
 * proc->p_ldtlock must be held across this call.
 */
int
linear_pc(struct regs *rp, proc_t *p, caddr_t *linearp)
{
	user_desc_t	*descrp;
	caddr_t		baseaddr;
	uint16_t	idx = SELTOIDX(rp->r_cs);

	ASSERT(rp->r_cs <= 0xFFFF);
	ASSERT(MUTEX_HELD(&p->p_ldtlock));

	if (SELISLDT(rp->r_cs)) {
		/*
		 * Currently 64 bit processes cannot have private LDTs.
		 */
		ASSERT(p->p_model != DATAMODEL_LP64);

		if (p->p_ldt == NULL)
			return (-1);

		descrp = &p->p_ldt[idx];
		baseaddr = (caddr_t)(uintptr_t)USEGD_GETBASE(descrp);

		/*
		 * Calculate the linear address (wraparound is not only ok,
		 * it's expected behavior).  The cast to uint32_t is because
		 * LDT selectors are only allowed in 32-bit processes.
		 */
		*linearp = (caddr_t)(uintptr_t)(uint32_t)((uintptr_t)baseaddr +
		    rp->r_pc);
	} else {
#ifdef DEBUG
		descrp = &CPU->cpu_gdt[idx];
		baseaddr = (caddr_t)(uintptr_t)USEGD_GETBASE(descrp);
		/* GDT-based descriptors' base addresses should always be 0 */
		ASSERT(baseaddr == 0);
#endif
		*linearp = (caddr_t)(uintptr_t)rp->r_pc;
	}

	return (0);
}

/*
 * The implementation of dtrace_linear_pc is similar to the that of
 * linear_pc, above, but here we acquire p_ldtlock before accessing
 * p_ldt.  This implementation is used by the pid provider; we prefix
 * it with "dtrace_" to avoid inducing spurious tracing events.
 */
int
dtrace_linear_pc(struct regs *rp, proc_t *p, caddr_t *linearp)
{
	user_desc_t	*descrp;
	caddr_t		baseaddr;
	uint16_t	idx = SELTOIDX(rp->r_cs);

	ASSERT(rp->r_cs <= 0xFFFF);

	if (SELISLDT(rp->r_cs)) {
		/*
		 * Currently 64 bit processes cannot have private LDTs.
		 */
		ASSERT(p->p_model != DATAMODEL_LP64);

		mutex_enter(&p->p_ldtlock);
		if (p->p_ldt == NULL) {
			mutex_exit(&p->p_ldtlock);
			return (-1);
		}
		descrp = &p->p_ldt[idx];
		baseaddr = (caddr_t)(uintptr_t)USEGD_GETBASE(descrp);
		mutex_exit(&p->p_ldtlock);

		/*
		 * Calculate the linear address (wraparound is not only ok,
		 * it's expected behavior).  The cast to uint32_t is because
		 * LDT selectors are only allowed in 32-bit processes.
		 */
		*linearp = (caddr_t)(uintptr_t)(uint32_t)((uintptr_t)baseaddr +
		    rp->r_pc);
	} else {
#ifdef DEBUG
		descrp = &CPU->cpu_gdt[idx];
		baseaddr = (caddr_t)(uintptr_t)USEGD_GETBASE(descrp);
		/* GDT-based descriptors' base addresses should always be 0 */
		ASSERT(baseaddr == 0);
#endif
		*linearp = (caddr_t)(uintptr_t)rp->r_pc;
	}

	return (0);
}

/*
 * We need to post a soft interrupt to reprogram the lbolt cyclic when
 * switching from event to cyclic driven lbolt. The following code adds
 * and posts the softint for x86.
 */
static ddi_softint_hdl_impl_t lbolt_softint_hdl =
	{0, 0, NULL, NULL, 0, NULL, NULL, NULL};

void
lbolt_softint_add(void)
{
	(void) add_avsoftintr((void *)&lbolt_softint_hdl, LOCK_LEVEL,
	    (avfunc)lbolt_ev_to_cyclic, "lbolt_ev_to_cyclic", NULL, NULL);
}

void
lbolt_softint_post(void)
{
	(*setsoftint)(CBE_LOCK_PIL, lbolt_softint_hdl.ih_pending);
}

/*
 * If SMAP is supported, look through hi_calls and inline
 * calls to smap_enable() to clac and smap_disable() to stac.
 */
void
hotinline_smap(hotinline_desc_t *hid)
{
	if (is_x86_feature(x86_featureset, X86FSET_SMAP) == B_FALSE)
		return;

	if (strcmp(hid->hid_symname, "smap_enable") == 0) {
		bcopy(clac_instr, (void *)hid->hid_instr_offset,
		    sizeof (clac_instr));
	} else if (strcmp(hid->hid_symname, "smap_disable") == 0) {
		bcopy(stac_instr, (void *)hid->hid_instr_offset,
		    sizeof (stac_instr));
	}
}

/*
 * Loop through hi_calls and hand off the inlining to
 * the appropriate calls.
 */
void
do_hotinlines(struct module *mp)
{
	for (hotinline_desc_t *hid = mp->hi_calls; hid != NULL;
	    hid = hid->hid_next) {
		hotinline_smap(hid);
	}
}
