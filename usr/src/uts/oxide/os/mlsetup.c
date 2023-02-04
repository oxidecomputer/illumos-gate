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
 * Copyright 2022 Oxide Computer Company
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/disp.h>
#include <sys/promif.h>
#include <sys/clock.h>
#include <sys/cpuvar.h>
#include <sys/stack.h>
#include <vm/as.h>
#include <vm/hat.h>
#include <sys/reboot.h>
#include <sys/avintr.h>
#include <sys/vtrace.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/cpupart.h>
#include <sys/pset.h>
#include <sys/copyops.h>
#include <sys/pg.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/sunddi.h>
#include <sys/x86_archext.h>
#include <sys/privregs.h>
#include <sys/machsystm.h>
#include <sys/ontrap.h>
#include <sys/bootconf.h>
#include <sys/kdi_machimpl.h>
#include <sys/archsystm.h>
#include <sys/promif.h>
#include <sys/apic.h>
#include <sys/apic_common.h>
#include <sys/bootvfs.h>
#include <sys/tsc.h>
#include <sys/smt.h>
#include <sys/boot_data.h>
#include <sys/io/milan/ccx.h>
#include <sys/io/milan/fabric.h>
#include <sys/io/milan/ras.h>
#include <milan/milan_apob.h>

/*
 * some globals for patching the result of cpuid
 * to solve problems w/ creative cpu vendors
 */

extern uint32_t cpuid_feature_ecx_include;
extern uint32_t cpuid_feature_ecx_exclude;
extern uint32_t cpuid_feature_edx_include;
extern uint32_t cpuid_feature_edx_exclude;

nmi_action_t nmi_action = NMI_ACTION_UNSET;

/*
 * Set console mode
 */
static void
set_console_mode(uint8_t val)
{
	struct bop_regs rp = {0};

	rp.eax.byte.ah = 0x0;
	rp.eax.byte.al = val;
	rp.ebx.word.bx = 0x0;

	BOP_DOINT(bootops, 0x10, &rp);
}


/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */
void
mlsetup(struct regs *rp)
{
	u_longlong_t prop_value;
	char prop_str[BP_MAX_STRLEN];
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
	 * check if we've got special bits to clear or set
	 * when checking cpu features
	 */

	if (bootprop_getval("cpuid_feature_ecx_include", &prop_value) != 0)
		cpuid_feature_ecx_include = 0;
	else
		cpuid_feature_ecx_include = (uint32_t)prop_value;

	if (bootprop_getval("cpuid_feature_ecx_exclude", &prop_value) != 0)
		cpuid_feature_ecx_exclude = 0;
	else
		cpuid_feature_ecx_exclude = (uint32_t)prop_value;

	if (bootprop_getval("cpuid_feature_edx_include", &prop_value) != 0)
		cpuid_feature_edx_include = 0;
	else
		cpuid_feature_edx_include = (uint32_t)prop_value;

	if (bootprop_getval("cpuid_feature_edx_exclude", &prop_value) != 0)
		cpuid_feature_edx_exclude = 0;
	else
		cpuid_feature_edx_exclude = (uint32_t)prop_value;

	if (bootprop_getstr("nmi", prop_str, sizeof (prop_str)) == 0) {
		if (strcmp(prop_str, "ignore") == 0) {
			nmi_action = NMI_ACTION_IGNORE;
		} else if (strcmp(prop_str, "panic") == 0) {
			nmi_action = NMI_ACTION_PANIC;
		} else if (strcmp(prop_str, "kmdb") == 0) {
			nmi_action = NMI_ACTION_KMDB;
		} else {
			prom_printf("unix: ignoring unknown nmi=%s\n",
			    prop_str);
		}
	}

	/*
	 * Check to see if KPTI has been explicitly enabled or disabled.
	 * We have to check this before init_desctbls().
	 */
	if (bootprop_getval("kpti", &prop_value) == 0) {
		kpti_enable = (uint64_t)(prop_value == 1);
		prom_printf("unix: forcing kpti to %s due to boot argument\n",
		    (kpti_enable == 1) ? "ON" : "OFF");
	} else {
		kpti_enable = 1;
	}

	if (bootprop_getval("pcid", &prop_value) == 0 && prop_value == 0) {
		prom_printf("unix: forcing pcid to OFF due to boot argument\n");
		x86_use_pcid = 0;
	} else if (kpti_enable != 1) {
		x86_use_pcid = 0;
	}

	/*
	 * While we don't need to check this until later, we might as well do it
	 * here.
	 */
	if (bootprop_getstr("smt_enabled", prop_str, sizeof (prop_str)) == 0) {
		if (strcasecmp(prop_str, "false") == 0 ||
		    strcmp(prop_str, "0") == 0)
			smt_boot_disable = 1;
	}

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
	milan_fabric_topo_init();
	CPU->cpu_m.mcpu_hwthread =
	    milan_fabric_find_thread_by_cpuid(CPU->cpu_id);

	/*
	 * Figure out what kind of CPU this is via pass 0.  We need this before
	 * subsequent passes so that we can perform CCX setup properly; this is
	 * also the end of the line for any unsupported CPU that has somehow
	 * gotten this far.  determine_platform() does very little on the oxide
	 * arch but needs to be run before pass 0 also.
	 */
	determine_platform();
	cpuid_execpass(cpu[0], CPUID_PASS_IDENT, NULL);

	/*
	 * Now go through and set up the BSP's thread-, core-, and CCX-specific
	 * registers.  This includes registers that control what cpuid returns
	 * so it must be done before the BASIC cpuid pass.  This will be run on
	 * APs later on.
	 */
	milan_ccx_init();

	/*
	 * Initialize the BSP's MCA banks.
	 */
	milan_ras_init();

	/*
	 * The x86_featureset is initialized here based on the capabilities
	 * of the boot CPU.  Note that if we choose to support CPUs that have
	 * different feature sets (at which point we would almost certainly
	 * want to set the feature bits to correspond to the feature minimum)
	 * this value may be altered.
	 */
	cpuid_execpass(cpu[0], CPUID_PASS_BASIC, x86_featureset);

	/*
	 * Patch the tsc_read routine with appropriate set of instructions,
	 * depending on the processor family and architecure, to read the
	 * time-stamp counter while ensuring no out-of-order execution.
	 */
	if (is_x86_feature(x86_featureset, X86FSET_TSCP)) {
		patch_tsc_read(TSC_TSCP);
	} else if (is_x86_feature(x86_featureset, X86FSET_LFENCE_SER)) {
		ASSERT(is_x86_feature(x86_featureset, X86FSET_SSE2));
		patch_tsc_read(TSC_RDTSC_LFENCE);
	}

	patch_memops(cpuid_getvendor(CPU));

	/*
	 * While we're thinking about the TSC, let's set up %cr4 so that
	 * userland can issue rdtsc, and initialize the TSC_AUX value
	 * (the cpuid) for the rdtscp instruction on appropriately
	 * capable hardware.
	 */
	if (is_x86_feature(x86_featureset, X86FSET_TSC))
		setcr4(getcr4() & ~CR4_TSD);

	if (is_x86_feature(x86_featureset, X86FSET_TSCP))
		(void) wrmsr(MSR_AMD_TSCAUX, 0);

	/*
	 * Let's get the other %cr4 stuff while we're here. Note, we defer
	 * enabling CR4_SMAP until startup_end(); however, that's importantly
	 * before we start other CPUs. That ensures that it will be synced out
	 * to other CPUs.
	 */
	if (is_x86_feature(x86_featureset, X86FSET_DE))
		setcr4(getcr4() | CR4_DE);

	if (is_x86_feature(x86_featureset, X86FSET_SMEP))
		setcr4(getcr4() | CR4_SMEP);

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
	 * If requested (boot -d) drop into kmdb.
	 *
	 * This must be done after cpu_list_init() on the 64-bit kernel
	 * since taking a trap requires that we re-compute gsbase based
	 * on the cpu list.
	 */
	if (boothowto & RB_DEBUGENTER)
		kmdb_enter();

	milan_apob_reserve_phys();

	cpu_vm_data_init(CPU);

	rp->r_fp = 0;	/* terminate kernel stack traces! */

	prom_init("kernel", (void *)NULL);

	/*
	 * Initialize the lgrp framework
	 */
	lgrp_init(LGRP_INIT_STAGE1);

	if (boothowto & RB_HALT) {
		prom_printf("unix: kernel halted by -h flag\n");
		prom_enter_mon();
	}

	ASSERT_STACK_ALIGNED();

	/*
	 * Fill out cpu_ucode_info.  Update microcode if necessary.
	 */
	ucode_check(CPU);
	cpuid_pass_ucode(CPU, x86_featureset);

	if (workaround_errata(CPU) != 0)
		panic("critical workaround(s) missing for boot cpu");
}


void
mach_modpath(char *path, const char *filename)
{
	/*
	 * Construct the directory path from the filename.
	 */

	int len;
	char *p;
	const char isastr[] = "/amd64";
	size_t isalen = strlen(isastr);

	len = strlen(SYSTEM_BOOT_PATH "/kernel");
	(void) strcpy(path, SYSTEM_BOOT_PATH "/kernel ");
	path += len + 1;

	if ((p = strrchr(filename, '/')) == NULL)
		return;

	while (p > filename && *(p - 1) == '/')
		p--;	/* remove trailing '/' characters */
	if (p == filename)
		p++;	/* so "/" -is- the modpath in this case */

	/*
	 * Remove optional isa-dependent directory name - the module
	 * subsystem will put this back again (!)
	 */
	len = p - filename;
	if (len > isalen &&
	    strncmp(&filename[len - isalen], isastr, isalen) == 0)
		p -= isalen;

	/*
	 * "/platform/mumblefrotz" + " " + MOD_DEFPATH
	 */
	len += (p - filename) + 1 + strlen(MOD_DEFPATH) + 1;
	(void) strncpy(path, filename, p - filename);
}
