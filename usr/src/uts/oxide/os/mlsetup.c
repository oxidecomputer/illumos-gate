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
 * Copyright (c) 2012 Gary Mills
 *
 * Copyright (c) 1993, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 by Delphix. All rights reserved.
 * Copyright 2019 Joyent, Inc.
 * Copyright 2023 Oxide Computer Company
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/disp.h>
#include <sys/promif.h>
#include <sys/cpuvar.h>
#include <sys/stack.h>
#include <sys/reboot.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/cpupart.h>
#include <sys/pset.h>
#include <sys/copyops.h>
#include <sys/pg.h>
#include <sys/debug.h>
#include <sys/x86_archext.h>
#include <sys/privregs.h>
#include <sys/machsystm.h>
#include <sys/kdi_machimpl.h>
#include <sys/archsystm.h>
#include <sys/promif.h>
#include <sys/bootvfs.h>
#include <sys/tsc.h>
#include <sys/boot_data.h>
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/hacks.h>
#include <sys/io/genoa/ras.h>
#include <genoa/genoa_apob.h>

/*
 * Setup routine called right before main(), which is common code.  We have much
 * to do still to satisfy the assumptions it will make.
 */
void
mlsetup(struct regs *rp)
{
	extern struct classfuncs sys_classfuncs;
	extern disp_t cpu0_disp;
	extern char t0stack[];

	ASSERT_STACK_ALIGNED();

	genunix_set_tunables();

	/*
	 * initialize cpu_self
	 */
	cpu[0]->cpu_self = cpu[0];

	/*
	 * Initialize idt0, gdt0, ldt0_default, ktss0 and dftss.
	 */
	init_desctbls();

	/*
	 * initialize t0
	 */
	t0.t_stk = (caddr_t)rp - MINFRAME;
	t0.t_stkbase = t0stack;
	t0.t_pri = maxclsyspri - 3;
	t0.t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t0.t_procp = &p0;
	t0.t_plockp = &p0lock.pl_lock;
	t0.t_lwp = &lwp0;
	t0.t_forw = &t0;
	t0.t_back = &t0;
	t0.t_next = &t0;
	t0.t_prev = &t0;
	t0.t_cpu = cpu[0];
	t0.t_disp_queue = &cpu0_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_bind_pset = PS_NONE;
	t0.t_bindflag = (uchar_t)default_binding_mode;
	t0.t_cpupart = &cp_default;
	t0.t_clfuncs = &sys_classfuncs.thread;
	t0.t_copyops = NULL;
	THREAD_ONPROC(&t0, CPU);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_regs = (void *)rp;
	lwp0.lwp_procp = &p0;
	t0.t_tid = p0.p_lwpcnt = p0.p_lwprcnt = p0.p_lwpid = 1;

	p0.p_exec = NULL;
	p0.p_stat = SRUN;
	p0.p_flag = SSYS;
	p0.p_tlist = &t0;
	p0.p_stksize = 2*PAGESIZE;
	p0.p_stkpageszc = 0;
	p0.p_as = &kas;
	p0.p_lockp = &p0lock;
	p0.p_brkpageszc = 0;
	p0.p_t1_lgrpid = LGRP_NONE;
	p0.p_tr_lgrpid = LGRP_NONE;
	psecflags_default(&p0.p_secflags);

	sigorset(&p0.p_ignore, &ignoredefault);

	CPU->cpu_thread = &t0;
	bzero(&cpu0_disp, sizeof (disp_t));
	CPU->cpu_disp = &cpu0_disp;
	CPU->cpu_disp->disp_cpu = CPU;
	CPU->cpu_dispthread = &t0;
	CPU->cpu_idle_thread = &t0;
	CPU->cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
	CPU->cpu_dispatch_pri = t0.t_pri;

	CPU->cpu_id = 0;

	CPU->cpu_pri = 12;		/* initial PIL for the boot CPU */

	/*
	 * Ensure that we have set the necessary feature bits before setting up
	 * PCI config space access.
	 */
	cpuid_execpass(cpu[0], CPUID_PASS_PRELUDE, x86_featureset);

	/*
	 * PCI config space access is required for fabric setup, and depends on
	 * a few addresses the early fabric initialisation code will retrieve.
	 * After setting up config space, this will then set up all our data
	 * structures for tracking the Milan topology so we can use the at later
	 * parts of the build.  We need to probe out the CCXs before we can set
	 * mcpu_hwthread, and we need mcpu_hwthread to set up brand strings for
	 * cpuid in a later pass.
	 */
	genoa_fabric_topo_init();
	CPU->cpu_m.mcpu_hwthread =
	    genoa_fabric_find_thread_by_cpuid(CPU->cpu_id);

	/*
	 * Figure out what kind of CPU this is via pass 0.  We need this before
	 * subsequent passes so that we can perform CCX setup properly; this is
	 * also the end of the line for any unsupported CPU that has somehow
	 * gotten this far. Note that determine_platform() also needs to be run
	 * before pass 0, but that was taken care of earlier in
	 * oxide_derive_platform().
	 */
	cpuid_execpass(cpu[0], CPUID_PASS_IDENT, NULL);

	/*
	 * As early as we reasonably can, we want to perform the necessary
	 * configuration in the FCH to assure that a core shutdown will
	 * correctly induce an observable reset.
	 */
	genoa_shutdown_detect_init();

	/*
	 * Now go through and set up the BSP's thread-, core-, and CCX-specific
	 * registers.  This includes registers that control what cpuid returns
	 * so it must be done before the BASIC cpuid pass.  This will be run on
	 * APs later on.
	 */
	genoa_ccx_init();

	/*
	 * Initialize the BSP's MCA banks.
	 */
	genoa_ras_init();

	/*
	 * The x86_featureset is initialized here based on the capabilities
	 * of the boot CPU.  Note that if we choose to support CPUs that have
	 * different feature sets (at which point we would almost certainly
	 * want to set the feature bits to correspond to the feature minimum)
	 * this value may be altered.
	 */
	cpuid_execpass(cpu[0], CPUID_PASS_BASIC, x86_featureset);

	/*
	 * We can't get here with an unsupported processor, so we're going to
	 * assert that whatever processor we're on supports the set of features
	 * we expect.  Since it's unusual for newer processors to remove
	 * features, this code shouldn't change much or often, and then only
	 * when adding support for newer families.  Like the t0 initialisation
	 * code above, parts of this could also be abstracted into an
	 * ISA-specific library if we wanted to share it with i86pc, in which
	 * case it really would be featureset-dependent but we'd still want to
	 * assert the features we expect.  Being able to boot without these
	 * features enabled would result in surprises during debugging, the
	 * potential for breakage in some upstack software, and, more seriously,
	 * a system that would not have the security properties users expect.
	 */

	/*
	 * Patch the tsc_read routine with appropriate set of instructions, to
	 * read the time-stamp counter while ensuring no out-of-order execution.
	 * All supported CPUs have a TSC and offer the rdtscp instruction.
	 */
	ASSERT(is_x86_feature(x86_featureset, X86FSET_TSC));
	ASSERT(is_x86_feature(x86_featureset, X86FSET_TSCP));
	patch_tsc_read(TSC_TSCP);

	/*
	 * This is a nop on AMD CPUs, but could in principle be extended in a
	 * future change so we'll continue calling into this generic function.
	 */
	patch_memops(cpuid_getvendor(CPU));

	/*
	 * While we're thinking about the TSC, let's set up %cr4 so that
	 * userland can issue rdtsc, and initialize the TSC_AUX value
	 * (the cpuid) for the rdtscp instruction.
	 */
	setcr4(getcr4() & ~CR4_TSD);
	(void) wrmsr(MSR_AMD_TSCAUX, 0);

	/*
	 * Let's get the other %cr4 stuff while we're here. Note, we defer
	 * enabling CR4_SMAP until startup_end(); however, that's importantly
	 * before we start other CPUs. That ensures that it will be synced out
	 * to other CPUs.
	 */
	ASSERT(is_x86_feature(x86_featureset, X86FSET_DE));
	ASSERT(is_x86_feature(x86_featureset, X86FSET_SMEP));
	setcr4(getcr4() | CR4_DE | CR4_SMEP);

	/*
	 * Initialize thread/cpu microstate accounting
	 */
	init_mstate(&t0, LMS_SYSTEM);
	init_cpu_mstate(CPU, CMS_SYSTEM);

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(CPU);

	pg_cpu_bootstrap(CPU);

	/*
	 * Now that we have taken over the GDT, IDT and have initialized
	 * active CPU list it's time to inform kmdb if present.
	 */
	if (boothowto & RB_DEBUG)
		kdi_idt_sync();

	/*
	 * If requested by the SP (IPCC_STARTUP_KMDB_BOOT) drop into kmdb.
	 *
	 * This must be done after cpu_list_init() since taking a trap requires
	 * that we re-compute gsbase based on the cpu list.
	 */
	if (boothowto & RB_DEBUGENTER)
		kmdb_enter();

	genoa_apob_reserve_phys();

	cpu_vm_data_init(CPU);

	rp->r_fp = 0;	/* terminate kernel stack traces! */

	prom_init("kernel", (void *)NULL);

	/*
	 * Initialize the lgrp framework
	 */
	lgrp_init(LGRP_INIT_STAGE1);

	/*
	 * Before we get too much further along, check for a furtive reset.
	 */
	genoa_check_furtive_reset();

	ASSERT_STACK_ALIGNED();

	/*
	 * Fill out cpu_ucode_info.  Update microcode if necessary.
	 */
	ucode_init();
	ucode_check(CPU);
	cpuid_pass_ucode(CPU, x86_featureset);

	if (workaround_errata(CPU) != 0)
		panic("critical workaround(s) missing for boot cpu");
}

/*
 * We are given the filename of the kernel we're booting, which may or may not
 * be meaningful but on this platform refers to a path within the CPIO archive.
 * Our job is to construct a space-separated list of paths, without the ISA
 * (/amd64) suffix, that are to be prepended to the module search path by krtld.
 * Note that this filename comes from BTPROP_NAME_WHOAMI, which is fixed on this
 * platform to be /platform/oxide/kernel/amd64/unix.  On other platforms, this
 * path can vary: one may for example construct a boot archive for i86pc that
 * puts the kernel somewhere else, and instruct loader(8) to boot that instead.
 * Since that's not an option on this architecture and we have no means of
 * passing such properties along, we could replace all of this with something
 * that just copies /platform/oxide/kernel into path and returns.  To relax the
 * need to keep this in sync, and to allow krtld evolution that could
 * conceivably change how we're called, we'll nevertheless look at the filename
 * as we do on other platforms.
 *
 * Note that krtld allocates only MAXPATHLEN for the entire path buffer, even
 * though there are at least three paths (see MOD_DEFPATH) that end up in the
 * list.  This isn't dangerous on oxide because we know that the length of the
 * path we're going to prepend here is short enough; on platforms where this
 * path is variable and/or operator-controlled, it's a bug.  We also assume that
 * the buffer we've been passed is filled with 0s, which isn't documented
 * anywhere.  This interface needs work.
 */
void
mach_modpath(char *path, const char *filename)
{
	size_t len;
	char *p;
	const char isastr[] = "/amd64";
	const size_t isalen = strlen(isastr);

	if ((p = strrchr(filename, '/')) == NULL)
		return;

	while (p > filename && *(p - 1) == '/')
		p--;	/* remove trailing '/' characters */
	if (p == filename)
		p++;	/* so "/" -is- the modpath in this case */

	len = p - filename;

	/*
	 * Remove the ISA-dependent directory name - the module subsystem will
	 * put this back again.
	 */
	if (len > isalen &&
	    strncmp(&filename[len - isalen], isastr, isalen) == 0) {
		len -= isalen;
	}

	(void) strncpy(path, filename, len);
}
