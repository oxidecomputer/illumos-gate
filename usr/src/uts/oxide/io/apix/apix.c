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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 * Copyright 2018 Joyent, Inc.
 * Copyright 2022 Oxide Computer Co.
 */

/*
 * To understand how the apix module interacts with the interrupt subsystem read
 * the theory statement in uts/i86pc/os/intr.c.
 */

/*
 * PSMI 1.1 extensions are supported only in 2.6 and later versions.
 * PSMI 1.2 extensions are supported only in 2.7 and later versions.
 * PSMI 1.3 and 1.4 extensions are supported in Solaris 10.
 * PSMI 1.5 extensions are supported in Solaris Nevada.
 * PSMI 1.6 extensions are supported in Solaris Nevada.
 * PSMI 1.7 extensions are supported in Solaris Nevada.
 */
#define	PSMI_1_7

#include <sys/processor.h>
#include <sys/time.h>
#include <sys/psm.h>
#include <sys/smp_impldefs.h>
#include <sys/cram.h>
#include <sys/psm_common.h>
#include <sys/pit.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/pci.h>
#include <sys/promif.h>
#include <sys/x86_archext.h>
#include <sys/cpc_impl.h>
#include <sys/uadmin.h>
#include <sys/panic.h>
#include <sys/debug.h>
#include <sys/archsystm.h>
#include <sys/trap.h>
#include <sys/machsystm.h>
#include <sys/sysmacros.h>
#include <sys/cpuvar.h>
#include <sys/rm_platter.h>
#include <sys/privregs.h>
#include <sys/note.h>
#include <sys/pci_intr_lib.h>
#include <sys/spl.h>
#include <sys/clock.h>
#include <sys/cyclic.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/x_call.h>
#include <sys/reboot.h>
#include <sys/mach_intr.h>
#include <sys/apix.h>
#include <sys/apix_irm_impl.h>
#include <sys/smm.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/iohc.h>

static int apix_probe();
static void apix_init();
static void apix_picinit(void);
static int apix_intr_enter(int, int *);
static void apix_intr_exit(int, int);
static void apix_setspl(int);
static int apix_disable_intr(processorid_t);
static void apix_enable_intr(processorid_t);
static int apix_get_clkvect(int);
static int apix_get_ipivect(int, int);
static void apix_post_cyclic_setup(void *);
static int apix_post_cpu_start();
static int apix_intr_ops(dev_info_t *, ddi_intr_handle_impl_t *,
    psm_intr_op_t, int *);

/*
 * Helper functions for apix_intr_ops()
 */
static void apix_redistribute_compute(void);
static int apix_get_pending(apix_vector_t *);
static apix_vector_t *apix_get_req_vector(ddi_intr_handle_impl_t *, ushort_t);
static int apix_get_intr_info(ddi_intr_handle_impl_t *, apic_get_intr_t *);
static char *apix_get_apic_type(void);
static int apix_intx_get_pending(int);
static void apix_intx_set_mask(int irqno);
static void apix_intx_clear_mask(int irqno);
static int apix_intx_get_shared(int irqno);
static void apix_intx_set_shared(int irqno, int delta);
static int apix_intx_alloc_vector(dev_info_t *, int, struct intrspec *);

extern int apic_clkinit(int);

/* IRM initialization for APIX PSM module */
extern void apix_irm_init(void);

extern int irm_enable;

/*
 *	Local static data
 */
static struct	psm_ops apix_ops = {
	.psm_probe = apix_probe,
	.psm_softinit = apix_init,
	.psm_picinit = apix_picinit,
	.psm_intr_enter = apix_intr_enter,
	.psm_intr_exit = apix_intr_exit,
	.psm_setspl = apix_setspl,
	.psm_addspl = apix_addspl,
	.psm_delspl = apix_delspl,
	.psm_disable_intr = apix_disable_intr,
	.psm_enable_intr = apix_enable_intr,
	.psm_set_idlecpu = apic_set_idlecpu,
	.psm_unset_idlecpu = apic_unset_idlecpu,
	.psm_clkinit = apic_clkinit,
	.psm_get_clockirq = apix_get_clkvect,
	.psm_gethrtime = apic_gethrtime,
	.psm_get_next_processorid = apic_get_next_processorid,
	.psm_cpu_start = apic_cpu_start,
	.psm_post_cpu_start = apix_post_cpu_start,
	.psm_shutdown = NULL,
	.psm_get_ipivect = apix_get_ipivect,
	.psm_send_ipi = apic_send_ipi,
	.psm_timer_reprogram = apic_timer_reprogram,
	.psm_timer_enable = apic_timer_enable,
	.psm_timer_disable = apic_timer_disable,
	.psm_post_cyclic_setup = apix_post_cyclic_setup,
	.psm_preshutdown = apic_preshutdown,
	.psm_intr_ops = apix_intr_ops,
	.psm_state = apic_state,
	.psm_cpu_ops = apic_cpu_ops,
	.psm_get_pir_ipivect = apic_get_pir_ipivect,
	.psm_send_pir_ipi = apic_send_pir_ipi,
	.psm_cmci_setup = apic_cmci_setup
};

struct psm_ops *psmops = &apix_ops;

static struct	psm_info apix_psm_info = {
	PSM_INFO_VER01_7,			/* version */
	PSM_OWN_EXCLUSIVE,			/* ownership */
	&apix_ops,				/* operation */
	APIX_NAME,				/* machine name */
	"apix MPv1.4 compatible",
};

static void *apix_hdlp;

/*
 * apix_lock is used for cpu selection and vector re-binding
 */
lock_t apix_lock;
apix_impl_t *apixs[NCPU];
/*
 * Mapping between device interrupt and the allocated vector. Indexed
 * by major number.
 */
apix_dev_vector_t **apix_dev_vector;
/*
 * Mapping between device major number and cpu id. It gets used
 * when interrupt binding policy round robin with affinity is
 * applied. With that policy, devices with the same major number
 * will be bound to the same CPU.
 */
processorid_t *apix_major_to_cpu;	/* major to cpu mapping */
kmutex_t apix_mutex;	/* for apix_dev_vector & apix_major_to_cpu */

int apix_nipis = 16;	/* Maximum number of IPIs */
/*
 * Maximum number of vectors in a CPU that can be used for interrupt
 * allocation (including IPIs and the reserved vectors).
 */
int apix_cpu_nvectors = APIX_NVECTOR;

/* number of CPUs in power-on transition state */
static int apic_poweron_cnt = 0;

/* gcpu.h */

extern void apic_do_interrupt(struct regs *rp, trap_trace_rec_t *ttp);
extern void apic_change_eoi();

/*
 *	This is the loadable module wrapper
 */

int
_init(void)
{
	if (apic_coarse_hrtime)
		apix_ops.psm_gethrtime = &apic_gettime;
	return (psm_mod_init(&apix_hdlp, &apix_psm_info));
}

int
_fini(void)
{
	return (psm_mod_fini(&apix_hdlp, &apix_psm_info));
}

int
_info(struct modinfo *modinfop)
{
	return (psm_mod_info(&apix_hdlp, &apix_psm_info, modinfop));
}

static int
apix_probe()
{
	/*
	 * apic_probe_common() is responsible for enabling x2APIC mode and
	 * updating the ops vectors to match.  It's not necessary for us to do
	 * that here, nor do we care what the current state is: all supported
	 * processors have x2APIC support.  This differs substantially from
	 * i86pc, where non-x2APIC processors are supported and let firmware
	 * decide whether x2APIC mode should be enabled if available.
	 */
	return (apic_probe_common(apix_psm_info.p_mach_idstring));
}

/*
 * Initialize the data structures used by addspl() and delspl() routines.
 */
static void
apix_softinit()
{
	int i;
	apix_impl_t *hdlp;
	int nproc;

	nproc = max(apic_nproc, apic_max_nproc);

	hdlp = kmem_zalloc(nproc * sizeof (apix_impl_t), KM_SLEEP);
	for (i = 0; i < nproc; i++) {
		apixs[i] = &hdlp[i];
		apixs[i]->x_cpuid = i;
		LOCK_INIT_CLEAR(&apixs[i]->x_lock);
	}

	/* cpu 0 is always up (for now) */
	apic_cpus[0].aci_status = APIC_CPU_ONLINE | APIC_CPU_INTR_ENABLE;

	bzero(apic_level_intr, sizeof (apic_level_intr));
	bzero(apic_irq_table, sizeof (apic_irq_table));
	mutex_init(&airq_mutex, NULL, MUTEX_DEFAULT, NULL);

	apix_dev_vector = kmem_zalloc(sizeof (apix_dev_vector_t *) * devcnt,
	    KM_SLEEP);

	if (apic_intr_policy == INTR_ROUND_ROBIN_WITH_AFFINITY) {
		apix_major_to_cpu = kmem_zalloc(sizeof (int) * devcnt,
		    KM_SLEEP);
		for (i = 0; i < devcnt; i++)
			apix_major_to_cpu[i] = IRQ_UNINIT;
	}

	mutex_init(&apix_mutex, NULL, MUTEX_DEFAULT, NULL);
}

static int
apix_get_pending_spl(void)
{
	int cpuid = CPU->cpu_id;

	return (bsrw_insn(apixs[cpuid]->x_intr_pending));
}

static uintptr_t
apix_get_intr_handler(int cpu, short vec)
{
	apix_vector_t *apix_vector;

	ASSERT(cpu < apic_nproc && vec < APIX_NVECTOR);
	if (cpu >= apic_nproc || vec >= APIX_NVECTOR)
		return (0);

	apix_vector = apixs[cpu]->x_vectbl[vec];

	return ((uintptr_t)(apix_vector->v_autovect));
}

static void
apix_init()
{
	extern void (*do_interrupt_common)(struct regs *, trap_trace_rec_t *);

	APIC_VERBOSE(INIT, (CE_CONT, "apix: psm_softinit\n"));

	do_interrupt_common = apix_do_interrupt;
	addintr = apix_add_avintr;
	remintr = apix_rem_avintr;
	get_pending_spl = apix_get_pending_spl;
	get_intr_handler = apix_get_intr_handler;
	psm_get_localapicid = apic_get_localapicid;
	psm_get_ioapicid = apic_get_ioapicid;

	apix_softinit();

	apic_pir_vect = apix_get_ipivect(XC_CPUPOKE_PIL, -1);

	/*
	 * Initialize IRM pool parameters
	 */
	if (irm_enable) {
		int	i;
		int	lowest_irq;
		int	highest_irq;

		/* number of CPUs present */
		apix_irminfo.apix_ncpus = apic_nproc;
		/* total number of entries in all of the IOAPICs present */
		lowest_irq = apic_io_vectbase[0];
		highest_irq = apic_io_vectend[0];
		for (i = 1; i < apic_io_max; i++) {
			if (apic_io_vectbase[i] < lowest_irq)
				lowest_irq = apic_io_vectbase[i];
			if (apic_io_vectend[i] > highest_irq)
				highest_irq = apic_io_vectend[i];
		}
		apix_irminfo.apix_ioapic_max_vectors =
		    highest_irq - lowest_irq + 1;
		/*
		 * Number of available per-CPU vectors excluding
		 * reserved vectors for Dtrace, int80, system-call,
		 * fast-trap, etc.
		 */
		apix_irminfo.apix_per_cpu_vectors = APIX_NAVINTR -
		    APIX_SW_RESERVED_VECTORS;

		apix_irminfo.apix_vectors_allocated = 0;
	}
}

static void
apix_init_intr()
{
	processorid_t	cpun = psm_get_cpu_id();
	uint_t nlvt;
	uint32_t svr = AV_UNIT_ENABLE | APIC_SPUR_INTR;
	extern void cmi_cmci_trap(void);

	apic_reg_ops->apic_write_task_reg(APIC_MASK_ALL);

	if (apic_mode == LOCAL_APIC) {
		/*
		 * We are running APIC in MMIO mode.
		 */
		if (apic_flat_model) {
			apic_reg_ops->apic_write(APIC_FORMAT_REG,
			    APIC_FLAT_MODEL);
		} else {
			apic_reg_ops->apic_write(APIC_FORMAT_REG,
			    APIC_CLUSTER_MODEL);
		}

		apic_reg_ops->apic_write(APIC_DEST_REG,
		    AV_HIGH_ORDER >> cpun);
	}

	if (apic_directed_EOI_supported()) {
		/*
		 * Setting the 12th bit in the Spurious Interrupt Vector
		 * Register suppresses broadcast EOIs generated by the local
		 * APIC. The suppression of broadcast EOIs happens only when
		 * interrupts are level-triggered.
		 */
		svr |= APIC_SVR_SUPPRESS_BROADCAST_EOI;
	}

	/* need to enable APIC before unmasking NMI */
	apic_reg_ops->apic_write(APIC_SPUR_INT_REG, svr);

	/*
	 * Presence of an invalid vector with delivery mode AV_FIXED can
	 * cause an error interrupt, even if the entry is masked...so
	 * write a valid vector to LVT entries along with the mask bit
	 */

	/* All APICs have timer and LINT0/1 */
	apic_reg_ops->apic_write(APIC_LOCAL_TIMER, AV_MASK|APIC_RESV_IRQ);
	apic_reg_ops->apic_write(APIC_INT_VECT0, AV_MASK|APIC_RESV_IRQ);
	apic_reg_ops->apic_write(APIC_INT_VECT1, AV_NMI);	/* enable NMI */

	/*
	 * On integrated APICs, the number of LVT entries is
	 * 'Max LVT entry' + 1; on 82489DX's (non-integrated
	 * APICs), nlvt is "3" (LINT0, LINT1, and timer)
	 */

	if (apic_cpus[cpun].aci_local_ver < APIC_INTEGRATED_VERS) {
		nlvt = 3;
	} else {
		nlvt = ((apic_reg_ops->apic_read(APIC_VERS_REG) >> 16) &
		    0xFF) + 1;
	}

	if (nlvt >= 5) {
		/* Enable performance counter overflow interrupt */

		if (!is_x86_feature(x86_featureset, X86FSET_MSR))
			apic_enable_cpcovf_intr = 0;
		if (apic_enable_cpcovf_intr) {
			if (apic_cpcovf_vect == 0) {
				int ipl = APIC_PCINT_IPL;

				apic_cpcovf_vect = apix_get_ipivect(ipl, -1);
				ASSERT(apic_cpcovf_vect);

				(void) add_avintr(NULL, ipl,
				    (avfunc)kcpc_hw_overflow_intr,
				    "apic pcint", apic_cpcovf_vect,
				    NULL, NULL, NULL, NULL);
				kcpc_hw_overflow_intr_installed = 1;
				kcpc_hw_enable_cpc_intr =
				    apic_cpcovf_mask_clear;
			}
			apic_reg_ops->apic_write(APIC_PCINT_VECT,
			    apic_cpcovf_vect);
		}
	}

	if (nlvt >= 6) {
		/*
		 * Mask the thermal interrupt vector since we don't currently
		 * use it.
		 */
		apic_reg_ops->apic_write(APIC_THERM_VECT,
		    AV_MASK | APIC_RESV_IRQ);
	}

	/* Enable error interrupt */

	if (nlvt >= 4 && apic_enable_error_intr) {
		if (apic_errvect == 0) {
			int ipl = 0xf;	/* get highest priority intr */
			apic_errvect = apix_get_ipivect(ipl, -1);
			ASSERT(apic_errvect);
			/*
			 * Not PSMI compliant, but we are going to merge
			 * with ON anyway
			 */
			(void) add_avintr(NULL, ipl,
			    (avfunc)apic_error_intr, "apic error intr",
			    apic_errvect, NULL, NULL, NULL, NULL);
		}
		apic_reg_ops->apic_write(APIC_ERR_VECT, apic_errvect);
		apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
		apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
	}

	/*
	 * Ensure a CMCI interrupt is allocated, regardless of whether it is
	 * enabled or not.
	 */
	if (apic_cmci_vect == 0) {
		const int ipl = 0x2;
		apic_cmci_vect = apix_get_ipivect(ipl, -1);
		ASSERT(apic_cmci_vect);

		(void) add_avintr(NULL, ipl,
		    (avfunc)cmi_cmci_trap, "apic cmci intr",
		    apic_cmci_vect, NULL, NULL, NULL, NULL);
	}

	apic_reg_ops->apic_write_task_reg(0);
}

static int
ioms_enable_nmi_cb(genoa_ioms_t *ioms, void *arg __unused)
{
	smn_reg_t reg;
	uint32_t v;

	/*
	 * On reset, the NMI destination in IOHC::IOHC_INTR_CNTL is set to
	 * 0xff.  We (emphatically) do not want any AP to get an NMI when we
	 * first power it on, so we deliberately set all NMI destinations to
	 * be the BSP.  Note that we do will not change this, even after APs
	 * are up (that is, NMIs will always go to the BSP):  changing it has
	 * non-zero runtime risk (see the comment above our actual enabling
	 * of NMI, below) and does not provide any value for our use case of
	 * NMI.
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_INTR_CNTL, 0);
	v = genoa_ioms_read(ioms, reg);
	v = IOHC_INTR_CNTL_SET_NMI_DEST_CTRL(v, 0);
	genoa_ioms_write(ioms, reg, v);

	if ((genoa_ioms_flags(ioms) & GENOA_IOMS_F_HAS_FCH) != 0) {
		reg = genoa_ioms_reg(ioms, D_IOHC_PIN_CTL, 0);
		v = IOHC_PIN_CTL_SET_MODE_NMI(0);
		genoa_ioms_write(ioms, reg, v);
	}

	/*
	 * Once we enable this, we can immediately take an NMI if it's
	 * currently asserted.  We want to do this last and clear out of here
	 * as quickly as possible:  this is all a bit dodgy, but the NMI
	 * handler itself needs to issue an SMN write to indicate EOI -- and
	 * if it finds that SMN-related locks are held, we will panic.  To
	 * reduce the likelihood of that, we are going to enable NMI and
	 * skedaddle...
	 */
	reg = genoa_ioms_reg(ioms, D_IOHC_MISC_RAS_CTL, 0);
	v = genoa_ioms_read(ioms, reg);
	v = IOHC_MISC_RAS_CTL_SET_NMI_SYNCFLOOD_EN(v, 1);
	genoa_ioms_write(ioms, reg, v);

	return (0);
}

static void
apix_picinit(void)
{
	APIC_VERBOSE(INIT, (CE_CONT, "apix: psm_picinit\n"));

	/*
	 * initialize interrupt remapping before apic
	 * hardware initialization
	 */
	apic_intrmap_init(apic_mode);
	if (apic_vt_ops == psm_vt_ops)
		apix_mul_ioapic_method = APIC_MUL_IOAPIC_IIR;

	/* set a flag so we know we have run apic_picinit() */
	apic_picinit_called = 1;
	LOCK_INIT_CLEAR(&apic_gethrtime_lock);
	LOCK_INIT_CLEAR(&apic_ioapic_lock);
	LOCK_INIT_CLEAR(&apic_error_lock);
	LOCK_INIT_CLEAR(&apic_mode_switch_lock);

	picsetup();	 /* initialise the 8259 */

	/* add nmi handler - least priority nmi handler */
	LOCK_INIT_CLEAR(&apic_nmi_lock);

	if (!psm_add_nmintr(0, apic_nmi_intr,
	    "apix NMI handler", (caddr_t)NULL))
		cmn_err(CE_WARN, "apix: Unable to add nmi handler");

	/*
	 * Enable the NMI functionality in the IOHC to allow external devices
	 * (i.e., the SP) to signal an NMI via the dedicated NMI_SYNCFLOOD_L
	 * pin.
	 */
	(void) genoa_walk_ioms(ioms_enable_nmi_cb, NULL);

	apix_init_intr();

	ioapix_init_intr(IOAPIC_MASK);

	/* setup global IRM pool if applicable */
	if (irm_enable)
		apix_irm_init();
}

static __inline__ void
apix_send_eoi(void)
{
	VERIFY3S(apic_mode, ==, LOCAL_X2APIC);
	X2APIC_WRITE(APIC_EOI_REG, 0);
}

/*
 * platform_intr_enter
 *
 *	Called at the beginning of the interrupt service routine, but unlike
 *	pcplusmp, does not mask interrupts. An EOI is given to the interrupt
 *	controller to enable other HW interrupts but interrupts are still
 *	masked by the IF flag.
 *
 *	Return -1 for spurious interrupts
 *
 */
static int
apix_intr_enter(int ipl, int *vectorp)
{
	struct cpu *cpu = CPU;
	uint32_t cpuid = CPU->cpu_id;
	apic_cpus_info_t *cpu_infop;
	uchar_t vector;
	apix_vector_t *vecp;
	int nipl = -1;

	/*
	 * The real vector delivered is (*vectorp + 0x20), but our caller
	 * subtracts 0x20 from the vector before passing it to us.
	 * (That's why APIC_BASE_VECT is 0x20.)
	 */
	vector = *vectorp = (uchar_t)*vectorp + APIC_BASE_VECT;

	cpu_infop = &apic_cpus[cpuid];
	if (vector == APIC_SPUR_INTR) {
		cpu_infop->aci_spur_cnt++;
		return (APIC_INT_SPURIOUS);
	}

	vecp = xv_vector(cpuid, vector);
	if (vecp == NULL) {
		if (APIX_IS_FAKE_INTR(vector))
			nipl = apix_rebindinfo.i_pri;
		apix_send_eoi();
		return (nipl);
	}
	nipl = vecp->v_pri;

	/* if interrupted by the clock, increment apic_nsec_since_boot */
	if (vector == (apic_clkvect + APIC_BASE_VECT)) {
		if (!apic_oneshot) {
			/* NOTE: this is not MT aware */
			apic_hrtime_stamp++;
			apic_nsec_since_boot += apic_nsec_per_intr;
			apic_hrtime_stamp++;
			last_count_read = apic_hertz_count;
			apix_redistribute_compute();
		}

		apix_send_eoi();

		return (nipl);
	}

	ASSERT(vecp->v_state != APIX_STATE_OBSOLETED);

	/* pre-EOI handling for level-triggered interrupts */
	if (!APIX_IS_DIRECTED_EOI(apix_mul_ioapic_method) &&
	    (vecp->v_type & APIX_TYPE_FIXED) && apic_level_intr[vecp->v_inum])
		apix_level_intr_pre_eoi(vecp->v_inum);

	/* send back EOI */
	apix_send_eoi();

	cpu_infop->aci_current[nipl] = vector;
	if ((nipl > ipl) && (nipl > cpu->cpu_base_spl)) {
		cpu_infop->aci_curipl = (uchar_t)nipl;
		cpu_infop->aci_ISR_in_progress |= 1 << nipl;
	}

#ifdef	DEBUG
	if (vector >= APIX_IPI_MIN)
		return (nipl);	/* skip IPI */

	APIC_DEBUG_BUF_PUT(vector);
	APIC_DEBUG_BUF_PUT(vecp->v_inum);
	APIC_DEBUG_BUF_PUT(nipl);
	APIC_DEBUG_BUF_PUT(psm_get_cpu_id());
	if ((apic_stretch_interrupts) && (apic_stretch_ISR & (1 << nipl)))
		drv_usecwait(apic_stretch_interrupts);
#endif /* DEBUG */

	return (nipl);
}

/*
 * Any changes made to this function must also change X2APIC
 * version of intr_exit.
 */
static void
apix_intr_exit(int prev_ipl, int arg2)
{
	int cpuid = psm_get_cpu_id();
	apic_cpus_info_t *cpu_infop = &apic_cpus[cpuid];
	apix_impl_t *apixp = apixs[cpuid];

	UNREFERENCED_1PARAMETER(arg2);

	cpu_infop->aci_curipl = (uchar_t)prev_ipl;
	/* ISR above current pri could not be in progress */
	cpu_infop->aci_ISR_in_progress &= (2 << prev_ipl) - 1;

	if (apixp->x_obsoletes != NULL) {
		if (APIX_CPU_LOCK_HELD(cpuid))
			return;

		APIX_ENTER_CPU_LOCK(cpuid);
		(void) apix_obsolete_vector(apixp->x_obsoletes);
		APIX_LEAVE_CPU_LOCK(cpuid);
	}
}

/*
 * The pcplusmp setspl code uses the TPR to mask all interrupts at or below the
 * given ipl, but apix never uses the TPR and we never mask a subset of the
 * interrupts. They are either all blocked by the IF flag or all can come in.
 *
 * For setspl, we mask all interrupts for XC_HI_PIL (15), otherwise, interrupts
 * can come in if currently enabled by the IF flag. This table shows the state
 * of the IF flag when we leave this function.
 *
 *    curr IF |	ipl == 15	ipl != 15
 *    --------+---------------------------
 *       0    |    0		    0
 *       1    |    0		    1
 */
static void
apix_setspl(int ipl)
{
	/*
	 * Interrupts at ipl above this cannot be in progress, so the following
	 * mask is ok.
	 */
	apic_cpus[psm_get_cpu_id()].aci_ISR_in_progress &= (2 << ipl) - 1;

	if (ipl == XC_HI_PIL)
		cli();
}

int
apix_addspl(int virtvec, int ipl, int min_ipl, int max_ipl)
{
	uint32_t cpuid = APIX_VIRTVEC_CPU(virtvec);
	uchar_t vector = (uchar_t)APIX_VIRTVEC_VECTOR(virtvec);
	apix_vector_t *vecp = xv_vector(cpuid, vector);

	UNREFERENCED_3PARAMETER(ipl, min_ipl, max_ipl);
	ASSERT(vecp != NULL && LOCK_HELD(&apix_lock));

	if (vecp->v_type == APIX_TYPE_FIXED)
		apix_intx_set_shared(vecp->v_inum, 1);

	/* There are more interrupts, so it's already been enabled */
	if (vecp->v_share > 1)
		return (PSM_SUCCESS);

	/* return if it is not hardware interrupt */
	if (vecp->v_type == APIX_TYPE_IPI)
		return (PSM_SUCCESS);

	/*
	 * if apix_picinit() has not been called yet, just return.
	 * At the end of apic_picinit(), we will call setup_io_intr().
	 */
	if (!apic_picinit_called)
		return (PSM_SUCCESS);

	(void) apix_setup_io_intr(vecp);

	return (PSM_SUCCESS);
}

int
apix_delspl(int virtvec, int ipl, int min_ipl, int max_ipl)
{
	uint32_t cpuid = APIX_VIRTVEC_CPU(virtvec);
	uchar_t vector = (uchar_t)APIX_VIRTVEC_VECTOR(virtvec);
	apix_vector_t *vecp = xv_vector(cpuid, vector);

	UNREFERENCED_3PARAMETER(ipl, min_ipl, max_ipl);
	ASSERT(vecp != NULL && LOCK_HELD(&apix_lock));

	if (vecp->v_type == APIX_TYPE_FIXED)
		apix_intx_set_shared(vecp->v_inum, -1);

	/* There are more interrupts */
	if (vecp->v_share > 1)
		return (PSM_SUCCESS);

	/* return if it is not hardware interrupt */
	if (vecp->v_type == APIX_TYPE_IPI)
		return (PSM_SUCCESS);

	if (!apic_picinit_called) {
		cmn_err(CE_WARN, "apix: delete 0x%x before apic init",
		    virtvec);
		return (PSM_SUCCESS);
	}

	apix_disable_vector(vecp);

	return (PSM_SUCCESS);
}

/*
 * Try and disable all interrupts. We just assign interrupts to other
 * processors based on policy. If any were bound by user request, we
 * let them continue and return failure. We do not bother to check
 * for cache affinity while rebinding.
 */
static int
apix_disable_intr(processorid_t cpun)
{
	apix_impl_t *apixp = apixs[cpun];
	apix_vector_t *vecp, *newp;
	int bindcpu, i, hardbound = 0, errbound = 0, ret, loop, type;

	lock_set(&apix_lock);

	apic_cpus[cpun].aci_status &= ~APIC_CPU_INTR_ENABLE;
	apic_cpus[cpun].aci_curipl = 0;

	/* if this is for SUSPEND operation, skip rebinding */
	if (apic_cpus[cpun].aci_status & APIC_CPU_SUSPEND) {
		for (i = APIX_AVINTR_MIN; i <= APIX_AVINTR_MAX; i++) {
			vecp = apixp->x_vectbl[i];
			if (!IS_VEC_ENABLED(vecp))
				continue;

			apix_disable_vector(vecp);
		}
		lock_clear(&apix_lock);
		return (PSM_SUCCESS);
	}

	for (i = APIX_AVINTR_MIN; i <= APIX_AVINTR_MAX; i++) {
		vecp = apixp->x_vectbl[i];
		if (!IS_VEC_ENABLED(vecp))
			continue;

		if (vecp->v_flags & APIX_VEC_F_USER_BOUND) {
			hardbound++;
			continue;
		}
		type = vecp->v_type;

		/*
		 * If there are bound interrupts on this cpu, then
		 * rebind them to other processors.
		 */
		loop = 0;
		do {
			bindcpu = apic_find_cpu(APIC_CPU_INTR_ENABLE);

			if (type != APIX_TYPE_MSI)
				newp = apix_set_cpu(vecp, bindcpu, &ret);
			else
				newp = apix_grp_set_cpu(vecp, bindcpu, &ret);
		} while ((newp == NULL) && (loop++ < apic_nproc));

		if (loop >= apic_nproc) {
			errbound++;
			cmn_err(CE_WARN, "apix: failed to rebind vector %x/%x",
			    vecp->v_cpuid, vecp->v_vector);
		}
	}

	lock_clear(&apix_lock);

	if (hardbound || errbound) {
		cmn_err(CE_WARN, "Could not disable interrupts on %d"
		    "due to user bound interrupts or failed operation",
		    cpun);
		return (PSM_FAILURE);
	}

	return (PSM_SUCCESS);
}

/*
 * Bind interrupts to specified CPU
 */
static void
apix_enable_intr(processorid_t cpun)
{
	apix_vector_t *vecp;
	int i, ret;
	processorid_t n;

	lock_set(&apix_lock);

	apic_cpus[cpun].aci_status |= APIC_CPU_INTR_ENABLE;

	/* interrupt enabling for system resume */
	if (apic_cpus[cpun].aci_status & APIC_CPU_SUSPEND) {
		for (i = APIX_AVINTR_MIN; i <= APIX_AVINTR_MAX; i++) {
			vecp = xv_vector(cpun, i);
			if (!IS_VEC_ENABLED(vecp))
				continue;

			apix_enable_vector(vecp);
		}
		apic_cpus[cpun].aci_status &= ~APIC_CPU_SUSPEND;
	}

	for (n = 0; n < apic_nproc; n++) {
		if (!apic_cpu_in_range(n) || n == cpun ||
		    (apic_cpus[n].aci_status & APIC_CPU_INTR_ENABLE) == 0)
			continue;

		for (i = APIX_AVINTR_MIN; i <= APIX_AVINTR_MAX; i++) {
			vecp = xv_vector(n, i);
			if (!IS_VEC_ENABLED(vecp) ||
			    vecp->v_bound_cpuid != cpun)
				continue;

			if (vecp->v_type != APIX_TYPE_MSI)
				(void) apix_set_cpu(vecp, cpun, &ret);
			else
				(void) apix_grp_set_cpu(vecp, cpun, &ret);
		}
	}

	lock_clear(&apix_lock);
}

/*
 * Allocate vector for IPI
 * type == -1 indicates it is an internal request. Do not change
 * resv_vector for these requests.
 */
static int
apix_get_ipivect(int ipl, int type)
{
	uchar_t vector;

	if ((vector = apix_alloc_ipi(ipl)) > 0) {
		if (type != -1)
			apic_resv_vector[ipl] = vector;
		return (vector);
	}
	apic_error |= APIC_ERR_GET_IPIVECT_FAIL;
	return (-1);	/* shouldn't happen */
}

static int
apix_get_clkvect(int ipl)
{
	int vector;

	if ((vector = apix_get_ipivect(ipl, -1)) == -1)
		return (-1);

	apic_clkvect = vector - APIC_BASE_VECT;
	APIC_VERBOSE(IPI, (CE_CONT, "apix: clock vector = %x\n",
	    apic_clkvect));
	return (vector);
}

static int
apix_post_cpu_start()
{
	int cpun;
	static int cpus_started = 1;

	/* We know this CPU + BSP  started successfully. */
	cpus_started++;

	/*
	 * On BSP we would have setup ourselves to use the X2APIC mode if it
	 * was enabled by hardware and/or firmware; on the AP we do that here,
	 * including enabling it in hardware if necessary.
	 *
	 * We enable X2APIC mode only if BSP is already in X2APIC mode; we do
	 * this even if the AP's LAPIC is disabled because we don't support
	 * that mode at all.  There should not exist any machine on which the
	 * BSP can run in X2APIC mode and the AP cannot.
	 */
	if (apic_mode == LOCAL_X2APIC && apic_detect_x2apic())
		apic_enable_x2apic();

	/*
	 * Switch back to x2apic IPI sending method for performance when target
	 * CPU has entered x2apic mode.
	 */
	if (apic_mode == LOCAL_X2APIC) {
		apic_switch_ipi_callback(B_FALSE);
	}

	splx(ipltospl(LOCK_LEVEL));
	apix_init_intr();
	smm_install_handler();

#ifdef	DEBUG
	APIC_AV_PENDING_SET();
#else
	if (apic_mode == LOCAL_APIC)
		APIC_AV_PENDING_SET();
#endif	/* DEBUG */

	/*
	 * We may be booting, or resuming from suspend; aci_status will
	 * be APIC_CPU_INTR_ENABLE if coming from suspend, so we add the
	 * APIC_CPU_ONLINE flag here rather than setting aci_status completely.
	 */
	cpun = psm_get_cpu_id();
	apic_cpus[cpun].aci_status |= APIC_CPU_ONLINE;

	apic_reg_ops->apic_write(APIC_DIVIDE_REG, apic_divide_reg_init);

	return (PSM_SUCCESS);
}

/*
 * If this module needs a periodic handler for the interrupt distribution, it
 * can be added here. The argument to the periodic handler is not currently
 * used, but is reserved for future.
 */
static void
apix_post_cyclic_setup(void *arg)
{
	UNREFERENCED_1PARAMETER(arg);

	cyc_handler_t cyh;
	cyc_time_t cyt;

	/* cpu_lock is held */
	/* set up a periodic handler for intr redistribution */

	/*
	 * In peridoc mode intr redistribution processing is done in
	 * apic_intr_enter during clk intr processing
	 */
	if (!apic_oneshot)
		return;

	/*
	 * Register a periodical handler for the redistribution processing.
	 * Though we would generally prefer to use the DDI interface for
	 * periodic handler invocation, ddi_periodic_add(9F), we are
	 * unfortunately already holding cpu_lock, which ddi_periodic_add will
	 * attempt to take for us.  Thus, we add our own cyclic directly:
	 */
	cyh.cyh_func = (void (*)(void *))apix_redistribute_compute;
	cyh.cyh_arg = NULL;
	cyh.cyh_level = CY_LOW_LEVEL;

	cyt.cyt_when = 0;
	cyt.cyt_interval = apic_redistribute_sample_interval;

	apic_cyclic_id = cyclic_add(&cyh, &cyt);
}

/*
 * Called the first time we enable x2apic mode on this cpu.
 * Update some of the function pointers to use x2apic routines.
 */
void
x2apic_update_psm(void)
{
	struct psm_ops *pops = &apix_ops;

	ASSERT(pops != NULL);

	/*
	 * The pcplusmp module's version of x2apic_update_psm makes additional
	 * changes that we do not have to make here. It needs to make those
	 * changes because pcplusmp relies on the TPR register and the means of
	 * addressing that changes when using the local apic versus the x2apic.
	 * It's also worth noting that the apix driver specific function end up
	 * being apix_foo as opposed to apic_foo and x2apic_foo.
	 */
	pops->psm_send_ipi = x2apic_send_ipi;
	send_dirintf = pops->psm_send_ipi;

	pops->psm_send_pir_ipi = x2apic_send_pir_ipi;
	psm_send_pir_ipi = pops->psm_send_pir_ipi;

	apic_mode = LOCAL_X2APIC;
	apic_change_ops();
}

/*
 * This function provides external interface to the nexus for all
 * functionalities related to the new DDI interrupt framework.
 *
 * Input:
 * dip     - pointer to the dev_info structure of the requested device
 * hdlp    - pointer to the internal interrupt handle structure for the
 *	     requested interrupt
 * intr_op - opcode for this call
 * result  - pointer to the integer that will hold the result to be
 *	     passed back if return value is PSM_SUCCESS
 *
 * Output:
 * return value is either PSM_SUCCESS or PSM_FAILURE
 */
static int
apix_intr_ops(dev_info_t *dip, ddi_intr_handle_impl_t *hdlp,
    psm_intr_op_t intr_op, int *result)
{
	int		cap;
	apix_vector_t	*vecp, *newvecp;
	struct intrspec *ispec, intr_spec;
	processorid_t target;

	ispec = &intr_spec;
	ispec->intrspec_pri = hdlp->ih_pri;
	ispec->intrspec_vec = hdlp->ih_inum;
	ispec->intrspec_func = hdlp->ih_cb_func;

	switch (intr_op) {
	case PSM_INTR_OP_ALLOC_VECTORS:
		switch (hdlp->ih_type) {
		case DDI_INTR_TYPE_MSI:
			/* allocate MSI vectors */
			*result = apix_alloc_msi(dip, hdlp->ih_inum,
			    hdlp->ih_scratch1,
			    (int)(uintptr_t)hdlp->ih_scratch2);
			break;
		case DDI_INTR_TYPE_MSIX:
			/* allocate MSI-X vectors */
			*result = apix_alloc_msix(dip, hdlp->ih_inum,
			    hdlp->ih_scratch1,
			    (int)(uintptr_t)hdlp->ih_scratch2);
			break;
		case DDI_INTR_TYPE_FIXED:
			/* allocate or share vector for fixed */
			if ((ihdl_plat_t *)hdlp->ih_private == NULL) {
				return (PSM_FAILURE);
			}
			ispec = ((ihdl_plat_t *)hdlp->ih_private)->ip_ispecp;
			*result = apix_intx_alloc_vector(dip, hdlp->ih_inum,
			    ispec);
			break;
		default:
			return (PSM_FAILURE);
		}
		break;
	case PSM_INTR_OP_FREE_VECTORS:
		apix_free_vectors(dip, hdlp->ih_inum, hdlp->ih_scratch1,
		    hdlp->ih_type);
		break;
	case PSM_INTR_OP_XLATE_VECTOR:
		/*
		 * Vectors are allocated by ALLOC and freed by FREE.
		 * XLATE finds and returns APIX_VIRTVEC_VECTOR(cpu, vector).
		 *
		 * It's necessary for us to understand how to interpret the
		 * contents of the handle.  When ih_type is MSI or MSIX, the
		 * interrrupt must have been allocated previously and has
		 * meaning only in the context of the devinfo node we've been
		 * given; in these cases, we use ih_inum to identify the
		 * specific interrupt by its index in the dev map.  All PCI
		 * devices are required to use MSI or MSIX exclusively.
		 *
		 * Non-PCI interrupts may get us here with an ih_type of FIXED,
		 * in which case we require that ih_private point to an
		 * ihdl_plat_t.  This data structure in turn points at a struct
		 * intrspec whose intrspec_vec member contains not the vector
		 * nor an IRQ number (which are private to us) but rather the
		 * interrupt source identifier.  On i86pc, there is code here
		 * that allows resolving IRQ numbers to vectors even if the
		 * interrupt isn't present in the dev map. XXX
		 */
		*result = APIX_INVALID_VECT;
		vecp = apix_get_dev_map(dip, hdlp->ih_inum, hdlp->ih_type);
		if (vecp != NULL) {
			*result = APIX_VIRTVECTOR(vecp->v_cpuid,
			    vecp->v_vector);
			break;
		}
		return (PSM_FAILURE);
	case PSM_INTR_OP_GET_PENDING:
		vecp = apix_get_dev_map(dip, hdlp->ih_inum, hdlp->ih_type);
		if (vecp == NULL)
			return (PSM_FAILURE);

		*result = apix_get_pending(vecp);
		break;
	case PSM_INTR_OP_CLEAR_MASK:
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (PSM_FAILURE);

		vecp = apix_get_dev_map(dip, hdlp->ih_inum, hdlp->ih_type);
		if (vecp == NULL)
			return (PSM_FAILURE);

		apix_intx_clear_mask(vecp->v_inum);
		break;
	case PSM_INTR_OP_SET_MASK:
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (PSM_FAILURE);

		vecp = apix_get_dev_map(dip, hdlp->ih_inum, hdlp->ih_type);
		if (vecp == NULL)
			return (PSM_FAILURE);

		apix_intx_set_mask(vecp->v_inum);
		break;
	case PSM_INTR_OP_GET_SHARED:
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (PSM_FAILURE);

		vecp = apix_get_dev_map(dip, hdlp->ih_inum, hdlp->ih_type);
		if (vecp == NULL)
			return (PSM_FAILURE);

		*result = apix_intx_get_shared(vecp->v_inum);
		break;
	case PSM_INTR_OP_SET_PRI:
		/*
		 * Called prior to adding the interrupt handler or when
		 * an interrupt handler is unassigned.
		 */
		if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
			return (PSM_SUCCESS);

		if (apix_get_dev_map(dip, hdlp->ih_inum, hdlp->ih_type) == NULL)
			return (PSM_FAILURE);

		break;
	case PSM_INTR_OP_SET_CPU:
	case PSM_INTR_OP_GRP_SET_CPU:
		/*
		 * The interrupt handle given here has been allocated
		 * specifically for this command, and ih_private carries
		 * a CPU value.
		 */
		*result = EINVAL;
		target = (int)(intptr_t)hdlp->ih_private;
		if (!apic_cpu_in_range(target)) {
			DDI_INTR_IMPLDBG((CE_WARN,
			    "[grp_]set_cpu: cpu out of range: %d\n", target));
			return (PSM_FAILURE);
		}

		lock_set(&apix_lock);

		vecp = apix_get_req_vector(hdlp, hdlp->ih_flags);
		if (!IS_VEC_ENABLED(vecp)) {
			DDI_INTR_IMPLDBG((CE_WARN,
			    "[grp]_set_cpu: invalid vector 0x%x\n",
			    hdlp->ih_vector));
			lock_clear(&apix_lock);
			return (PSM_FAILURE);
		}

		*result = 0;

		if (intr_op == PSM_INTR_OP_SET_CPU)
			newvecp = apix_set_cpu(vecp, target, result);
		else
			newvecp = apix_grp_set_cpu(vecp, target, result);

		lock_clear(&apix_lock);

		if (newvecp == NULL) {
			*result = EIO;
			return (PSM_FAILURE);
		}
		newvecp->v_bound_cpuid = target;
		hdlp->ih_vector = APIX_VIRTVECTOR(newvecp->v_cpuid,
		    newvecp->v_vector);
		break;

	case PSM_INTR_OP_GET_INTR:
		/*
		 * The interrupt handle given here has been allocated
		 * specifically for this command, and ih_private carries
		 * a pointer to a apic_get_intr_t.
		 */
		if (apix_get_intr_info(hdlp, hdlp->ih_private) != PSM_SUCCESS)
			return (PSM_FAILURE);
		break;

	case PSM_INTR_OP_CHECK_MSI:
		/*
		 * Check MSI/X is supported or not at APIC level and
		 * masked off the MSI/X bits in hdlp->ih_type if not
		 * supported before return.  If MSI/X is supported,
		 * leave the ih_type unchanged and return.
		 *
		 * hdlp->ih_type passed in from the nexus has all the
		 * interrupt types supported by the device.
		 */
		if (apic_support_msi == 0) {	/* uninitialized */
			/*
			 * if apic_support_msi is not set, call
			 * apic_check_msi_support() to check whether msi
			 * is supported first
			 */
			if (apic_check_msi_support() == PSM_SUCCESS)
				apic_support_msi = 1;	/* supported */
			else
				apic_support_msi = -1;	/* not-supported */
		}
		if (apic_support_msi == 1) {
			if (apic_msix_enable)
				*result = hdlp->ih_type;
			else
				*result = hdlp->ih_type & ~DDI_INTR_TYPE_MSIX;
		} else
			*result = hdlp->ih_type & ~(DDI_INTR_TYPE_MSI |
			    DDI_INTR_TYPE_MSIX);
		break;
	case PSM_INTR_OP_GET_CAP:
		cap = DDI_INTR_FLAG_PENDING;
		if (hdlp->ih_type == DDI_INTR_TYPE_FIXED)
			cap |= DDI_INTR_FLAG_MASKABLE;
		*result = cap;
		break;
	case PSM_INTR_OP_APIC_TYPE:
		((apic_get_type_t *)(hdlp->ih_private))->avgi_type =
		    apix_get_apic_type();
		((apic_get_type_t *)(hdlp->ih_private))->avgi_num_intr =
		    APIX_IPI_MIN;
		((apic_get_type_t *)(hdlp->ih_private))->avgi_num_cpu =
		    apic_nproc;
		hdlp->ih_ver = apic_get_apic_version();
		break;
	case PSM_INTR_OP_SET_CAP:
	default:
		return (PSM_FAILURE);
	}

	return (PSM_SUCCESS);
}

static void
apix_cleanup_busy(void)
{
	int i, j;
	apix_vector_t *vecp;

	for (i = 0; i < apic_nproc; i++) {
		if (!apic_cpu_in_range(i))
			continue;
		apic_cpus[i].aci_busy = 0;
		for (j = APIX_AVINTR_MIN; j < APIX_AVINTR_MAX; j++) {
			if ((vecp = xv_vector(i, j)) != NULL)
				vecp->v_busy = 0;
		}
	}
}

static void
apix_redistribute_compute(void)
{
	int	i, j, max_busy;

	if (!apic_enable_dynamic_migration)
		return;

	if (++apic_nticks == apic_sample_factor_redistribution) {
		/*
		 * Time to call apic_intr_redistribute().
		 * reset apic_nticks. This will cause max_busy
		 * to be calculated below and if it is more than
		 * apic_int_busy, we will do the whole thing
		 */
		apic_nticks = 0;
	}
	max_busy = 0;
	for (i = 0; i < apic_nproc; i++) {
		if (!apic_cpu_in_range(i))
			continue;
		/*
		 * Check if curipl is non zero & if ISR is in
		 * progress
		 */
		if (((j = apic_cpus[i].aci_curipl) != 0) &&
		    (apic_cpus[i].aci_ISR_in_progress & (1 << j))) {

			int	vect;
			apic_cpus[i].aci_busy++;
			vect = apic_cpus[i].aci_current[j];
			apixs[i]->x_vectbl[vect]->v_busy++;
		}

		if (!apic_nticks &&
		    (apic_cpus[i].aci_busy > max_busy))
			max_busy = apic_cpus[i].aci_busy;
	}
	if (!apic_nticks) {
		if (max_busy > apic_int_busy_mark) {
		/*
		 * We could make the following check be
		 * skipped > 1 in which case, we get a
		 * redistribution at half the busy mark (due to
		 * double interval). Need to be able to collect
		 * more empirical data to decide if that is a
		 * good strategy. Punt for now.
		 */
			apix_cleanup_busy();
			apic_skipped_redistribute = 0;
		} else
			apic_skipped_redistribute++;
	}
}

/*
 * intr_ops() service routines
 */

static int
apix_get_pending(apix_vector_t *vecp)
{
	int bit, index, irr, pending;

	/* need to get on the bound cpu */
	mutex_enter(&cpu_lock);
	affinity_set(vecp->v_cpuid);

	index = vecp->v_vector / 32;
	bit = vecp->v_vector % 32;
	irr = apic_reg_ops->apic_read(APIC_IRR_REG + index);

	affinity_clear();
	mutex_exit(&cpu_lock);

	pending = (irr & (1 << bit)) ? 1 : 0;
	if (!pending && vecp->v_type == APIX_TYPE_FIXED)
		pending = apix_intx_get_pending(vecp->v_inum);

	return (pending);
}

static apix_vector_t *
apix_get_req_vector(ddi_intr_handle_impl_t *hdlp, uint16_t flags)
{
	apix_vector_t *vecp;
	processorid_t cpuid;
	int32_t virt_vec = 0;

	switch (flags & PSMGI_INTRBY_FLAGS) {
	case PSMGI_INTRBY_IRQ:
		return (apix_intx_get_vector(hdlp->ih_vector));
	case PSMGI_INTRBY_VEC:
		virt_vec = (virt_vec == 0) ? hdlp->ih_vector : virt_vec;

		cpuid = APIX_VIRTVEC_CPU(virt_vec);
		if (!apic_cpu_in_range(cpuid))
			return (NULL);

		vecp = xv_vector(cpuid, APIX_VIRTVEC_VECTOR(virt_vec));
		break;
	case PSMGI_INTRBY_DEFAULT:
		vecp = apix_get_dev_map(hdlp->ih_dip, hdlp->ih_inum,
		    hdlp->ih_type);
		break;
	default:
		return (NULL);
	}

	return (vecp);
}

static int
apix_get_intr_info(ddi_intr_handle_impl_t *hdlp,
    apic_get_intr_t *intr_params_p)
{
	apix_vector_t *vecp;
	struct autovec *av_dev;
	int i;

	vecp = apix_get_req_vector(hdlp, intr_params_p->avgi_req_flags);
	if (IS_VEC_FREE(vecp)) {
		intr_params_p->avgi_num_devs = 0;
		intr_params_p->avgi_cpu_id = 0;
		intr_params_p->avgi_req_flags = 0;
		return (PSM_SUCCESS);
	}

	if (intr_params_p->avgi_req_flags & PSMGI_REQ_CPUID) {
		intr_params_p->avgi_cpu_id = vecp->v_cpuid;

		/* Return user bound info for intrd. */
		if (intr_params_p->avgi_cpu_id & IRQ_USER_BOUND) {
			intr_params_p->avgi_cpu_id &= ~IRQ_USER_BOUND;
			intr_params_p->avgi_cpu_id |= PSMGI_CPU_USER_BOUND;
		}
	}

	if (intr_params_p->avgi_req_flags & PSMGI_REQ_VECTOR)
		intr_params_p->avgi_vector = vecp->v_vector;

	if (intr_params_p->avgi_req_flags &
	    (PSMGI_REQ_NUM_DEVS | PSMGI_REQ_GET_DEVS))
		/* Get number of devices from apic_irq table shared field. */
		intr_params_p->avgi_num_devs = vecp->v_share;

	if (intr_params_p->avgi_req_flags &  PSMGI_REQ_GET_DEVS) {

		intr_params_p->avgi_req_flags  |= PSMGI_REQ_NUM_DEVS;

		/* Some devices have NULL dip.  Don't count these. */
		if (intr_params_p->avgi_num_devs > 0) {
			for (i = 0, av_dev = vecp->v_autovect; av_dev;
			    av_dev = av_dev->av_link) {
				if (av_dev->av_vector && av_dev->av_dip)
					i++;
			}
			intr_params_p->avgi_num_devs =
			    (uint8_t)MIN(intr_params_p->avgi_num_devs, i);
		}

		/* There are no viable dips to return. */
		if (intr_params_p->avgi_num_devs == 0) {
			intr_params_p->avgi_dip_list = NULL;

		} else {	/* Return list of dips */

			/* Allocate space in array for that number of devs. */
			intr_params_p->avgi_dip_list = kmem_zalloc(
			    intr_params_p->avgi_num_devs *
			    sizeof (dev_info_t *),
			    KM_NOSLEEP);
			if (intr_params_p->avgi_dip_list == NULL) {
				DDI_INTR_IMPLDBG((CE_WARN,
				    "apix_get_vector_intr_info: no memory"));
				return (PSM_FAILURE);
			}

			/*
			 * Loop through the device list of the autovec table
			 * filling in the dip array.
			 *
			 * Note that the autovect table may have some special
			 * entries which contain NULL dips.  These will be
			 * ignored.
			 */
			for (i = 0, av_dev = vecp->v_autovect; av_dev;
			    av_dev = av_dev->av_link) {
				if (av_dev->av_vector && av_dev->av_dip)
					intr_params_p->avgi_dip_list[i++] =
					    av_dev->av_dip;
			}
		}
	}

	return (PSM_SUCCESS);
}

static char *
apix_get_apic_type(void)
{
	return (apix_psm_info.p_mach_idstring);
}

apix_vector_t *
apix_set_cpu(apix_vector_t *vecp, int new_cpu, int *result)
{
	apix_vector_t *newp = NULL;
	dev_info_t *dip;
	int inum, cap_ptr;
	ddi_acc_handle_t handle;
	ddi_intr_msix_t *msix_p = NULL;
	ushort_t msix_ctrl;
	uintptr_t off = 0;
	uint32_t mask = 0;

	ASSERT(LOCK_HELD(&apix_lock));
	*result = ENXIO;

	/* Fail if this is an MSI intr and is part of a group. */
	if (vecp->v_type == APIX_TYPE_MSI) {
		if (i_ddi_intr_get_current_nintrs(APIX_GET_DIP(vecp)) > 1)
			return (NULL);
		else
			return (apix_grp_set_cpu(vecp, new_cpu, result));
	}

	/*
	 * Mask MSI-X. It's unmasked when MSI-X gets enabled.
	 */
	if (vecp->v_type == APIX_TYPE_MSIX && IS_VEC_ENABLED(vecp)) {
		if ((dip = APIX_GET_DIP(vecp)) == NULL)
			return (NULL);
		inum = vecp->v_devp->dv_inum;

		handle = i_ddi_get_pci_config_handle(dip);
		cap_ptr = i_ddi_get_msi_msix_cap_ptr(dip);
		msix_ctrl = pci_config_get16(handle, cap_ptr + PCI_MSIX_CTRL);
		if ((msix_ctrl & PCI_MSIX_FUNCTION_MASK) == 0) {
			/*
			 * Function is not masked, then mask "inum"th
			 * entry in the MSI-X table
			 */
			msix_p = i_ddi_get_msix(dip);
			off = (uintptr_t)msix_p->msix_tbl_addr + (inum *
			    PCI_MSIX_VECTOR_SIZE) + PCI_MSIX_VECTOR_CTRL_OFFSET;
			mask = ddi_get32(msix_p->msix_tbl_hdl, (uint32_t *)off);
			ddi_put32(msix_p->msix_tbl_hdl, (uint32_t *)off,
			    mask | 1);
		}
	}

	*result = 0;
	if ((newp = apix_rebind(vecp, new_cpu, 1)) == NULL)
		*result = EIO;

	/* Restore mask bit */
	if (msix_p != NULL)
		ddi_put32(msix_p->msix_tbl_hdl, (uint32_t *)off, mask);

	return (newp);
}

/*
 * Set cpu for MSIs
 */
apix_vector_t *
apix_grp_set_cpu(apix_vector_t *vecp, int new_cpu, int *result)
{
	apix_vector_t *newp, *vp;
	uint32_t orig_cpu = vecp->v_cpuid;
	int orig_vect = vecp->v_vector;
	int i, num_vectors, cap_ptr, msi_mask_off = 0;
	uint32_t msi_pvm = 0;
	ushort_t msi_ctrl;
	ddi_acc_handle_t handle;
	dev_info_t *dip;

	APIC_VERBOSE(INTR, (CE_CONT, "apix_grp_set_cpu: oldcpu: %x, vector: %x,"
	    " newcpu:%x\n", vecp->v_cpuid, vecp->v_vector, new_cpu));

	ASSERT(LOCK_HELD(&apix_lock));

	*result = ENXIO;

	if (vecp->v_type != APIX_TYPE_MSI) {
		DDI_INTR_IMPLDBG((CE_WARN, "set_grp: intr not MSI\n"));
		return (NULL);
	}

	if ((dip = APIX_GET_DIP(vecp)) == NULL)
		return (NULL);

	num_vectors = i_ddi_intr_get_current_nintrs(dip);
	if ((num_vectors < 1) || ((num_vectors - 1) & orig_vect)) {
		APIC_VERBOSE(INTR, (CE_WARN,
		    "set_grp: base vec not part of a grp or not aligned: "
		    "vec:0x%x, num_vec:0x%x\n", orig_vect, num_vectors));
		return (NULL);
	}

	if (vecp->v_inum != apix_get_min_dev_inum(dip, vecp->v_type))
		return (NULL);

	*result = EIO;
	for (i = 1; i < num_vectors; i++) {
		if ((vp = xv_vector(orig_cpu, orig_vect + i)) == NULL)
			return (NULL);
#ifdef DEBUG
		/*
		 * Sanity check: CPU and dip is the same for all entries.
		 * May be called when first msi to be enabled, at this time
		 * add_avintr() is not called for other msi
		 */
		if ((vp->v_share != 0) &&
		    ((APIX_GET_DIP(vp) != dip) ||
		    (vp->v_cpuid != vecp->v_cpuid))) {
			APIC_VERBOSE(INTR, (CE_WARN,
			    "set_grp: cpu or dip for vec 0x%x difft than for "
			    "vec 0x%x\n", orig_vect, orig_vect + i));
			APIC_VERBOSE(INTR, (CE_WARN,
			    "  cpu: %d vs %d, dip: 0x%p vs 0x%p\n", orig_cpu,
			    vp->v_cpuid, (void *)dip,
			    (void *)APIX_GET_DIP(vp)));
			return (NULL);
		}
#endif /* DEBUG */
	}

	cap_ptr = i_ddi_get_msi_msix_cap_ptr(dip);
	handle = i_ddi_get_pci_config_handle(dip);
	msi_ctrl = pci_config_get16(handle, cap_ptr + PCI_MSI_CTRL);

	/* MSI Per vector masking is supported. */
	if (msi_ctrl & PCI_MSI_PVM_MASK) {
		if (msi_ctrl &  PCI_MSI_64BIT_MASK)
			msi_mask_off = cap_ptr + PCI_MSI_64BIT_MASKBITS;
		else
			msi_mask_off = cap_ptr + PCI_MSI_32BIT_MASK;
		msi_pvm = pci_config_get32(handle, msi_mask_off);
		pci_config_put32(handle, msi_mask_off, (uint32_t)-1);
		APIC_VERBOSE(INTR, (CE_CONT,
		    "set_grp: pvm supported.  Mask set to 0x%x\n",
		    pci_config_get32(handle, msi_mask_off)));
	}

	if ((newp = apix_rebind(vecp, new_cpu, num_vectors)) != NULL)
		*result = 0;

	/* Reenable vectors if per vector masking is supported. */
	if (msi_ctrl & PCI_MSI_PVM_MASK) {
		pci_config_put32(handle, msi_mask_off, msi_pvm);
		APIC_VERBOSE(INTR, (CE_CONT,
		    "set_grp: pvm supported.  Mask restored to 0x%x\n",
		    pci_config_get32(handle, msi_mask_off)));
	}

	return (newp);
}

void
apix_intx_set_vector(int irqno, uint32_t cpuid, uchar_t vector)
{
	apic_irq_t *irqp;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];
	irqp->airq_cpu = cpuid;
	irqp->airq_vector = vector;
	apic_record_rdt_entry(irqp, irqno);
	mutex_exit(&airq_mutex);
}

apix_vector_t *
apix_intx_get_vector(int irqno)
{
	apic_irq_t *irqp;
	uint32_t cpuid;
	uchar_t vector;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno & 0xff];
	if (IS_IRQ_FREE(irqp) || (irqp->airq_cpu == IRQ_UNINIT)) {
		mutex_exit(&airq_mutex);
		return (NULL);
	}
	cpuid = irqp->airq_cpu;
	vector = irqp->airq_vector;
	mutex_exit(&airq_mutex);

	return (xv_vector(cpuid, vector));
}

/*
 * Must called with interrupts disabled and apic_ioapic_lock held
 */
void
apix_intx_enable(int irqno)
{
	uchar_t ioapicindex, intin;
	apic_irq_t *irqp = apic_irq_table[irqno];
	ioapic_rdt_t irdt;
	apic_cpus_info_t *cpu_infop;
	apix_vector_t *vecp = xv_vector(irqp->airq_cpu, irqp->airq_vector);

	ASSERT(LOCK_HELD(&apic_ioapic_lock) && !IS_IRQ_FREE(irqp));

	ioapicindex = irqp->airq_ioapicindex;
	intin = irqp->airq_intin_no;
	cpu_infop =  &apic_cpus[irqp->airq_cpu];

	irdt.ir_lo = AV_PDEST | AV_FIXED | irqp->airq_rdt_entry;
	irdt.ir_hi = cpu_infop->aci_local_id;

	apic_vt_ops->apic_intrmap_alloc_entry(&vecp->v_intrmap_private, NULL,
	    vecp->v_type, 1, ioapicindex);
	apic_vt_ops->apic_intrmap_map_entry(vecp->v_intrmap_private,
	    (void *)&irdt, vecp->v_type, 1);
	apic_vt_ops->apic_intrmap_record_rdt(vecp->v_intrmap_private, &irdt);

	/* write RDT entry high dword - destination */
	WRITE_IOAPIC_RDT_ENTRY_HIGH_DWORD(ioapicindex, intin,
	    irdt.ir_hi);

	/* Write the vector, trigger, and polarity portion of the RDT */
	WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapicindex, intin, irdt.ir_lo);

	vecp->v_state = APIX_STATE_ENABLED;

	APIC_VERBOSE_IOAPIC((CE_CONT, "apix_intx_enable: ioapic 0x%x"
	    " intin 0x%x rdt_low 0x%x rdt_high 0x%x\n",
	    ioapicindex, intin, irdt.ir_lo, irdt.ir_hi));
}

/*
 * Must called with interrupts disabled and apic_ioapic_lock held
 */
void
apix_intx_disable(int irqno)
{
	apic_irq_t *irqp = apic_irq_table[irqno];
	int ioapicindex, intin;

	ASSERT(LOCK_HELD(&apic_ioapic_lock) && !IS_IRQ_FREE(irqp));
	/*
	 * The assumption here is that this is safe, even for
	 * systems with IOAPICs that suffer from the hardware
	 * erratum because all devices have been quiesced before
	 * they unregister their interrupt handlers.  If that
	 * assumption turns out to be false, this mask operation
	 * can induce the same erratum result we're trying to
	 * avoid.
	 */
	ioapicindex = irqp->airq_ioapicindex;
	intin = irqp->airq_intin_no;
	ioapic_write(ioapicindex, APIC_RDT_CMD + 2 * intin, AV_MASK);

	APIC_VERBOSE_IOAPIC((CE_CONT, "apix_intx_disable: ioapic 0x%x"
	    " intin 0x%x\n", ioapicindex, intin));
}

void
apix_intx_free(int irqno)
{
	apic_irq_t *irqp;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];

	if (IS_IRQ_FREE(irqp)) {
		mutex_exit(&airq_mutex);
		return;
	}

	irqp->airq_kind = AIRQK_FREE;
	irqp->airq_cpu = IRQ_UNINIT;
	irqp->airq_vector = APIX_INVALID_VECT;
	mutex_exit(&airq_mutex);
}

#ifdef DEBUG
int apix_intr_deliver_timeouts = 0;
int apix_intr_rirr_timeouts = 0;
int apix_intr_rirr_reset_failure = 0;
#endif
int apix_max_reps_irr_pending = 10;

#define	GET_RDT_BITS(ioapic, intin, bits)	\
	(READ_IOAPIC_RDT_ENTRY_LOW_DWORD((ioapic), (intin)) & (bits))
#define	APIX_CHECK_IRR_DELAY	drv_usectohz(5000)

int
apix_intx_rebind(int irqno, processorid_t cpuid, uchar_t vector)
{
	apic_irq_t *irqp = apic_irq_table[irqno];
	ulong_t iflag;
	int waited, ioapic_ix, intin_no, level, repeats, rdt_entry, masked;

	ASSERT(irqp != NULL);

	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	ioapic_ix = irqp->airq_ioapicindex;
	intin_no = irqp->airq_intin_no;
	level = apic_level_intr[irqno];

	/*
	 * Wait for the delivery status bit to be cleared. This should
	 * be a very small amount of time.
	 */
	repeats = 0;
	do {
		repeats++;

		for (waited = 0; waited < apic_max_reps_clear_pending;
		    waited++) {
			if (GET_RDT_BITS(ioapic_ix, intin_no, AV_PENDING) == 0)
				break;
		}
		if (!level)
			break;

		/*
		 * Mask the RDT entry for level-triggered interrupts.
		 */
		irqp->airq_rdt_entry |= AV_MASK;
		rdt_entry = READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
		    intin_no);
		if ((masked = (rdt_entry & AV_MASK)) == 0) {
			/* Mask it */
			WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix, intin_no,
			    AV_MASK | rdt_entry);
		}

		/*
		 * If there was a race and an interrupt was injected
		 * just before we masked, check for that case here.
		 * Then, unmask the RDT entry and try again.  If we're
		 * on our last try, don't unmask (because we want the
		 * RDT entry to remain masked for the rest of the
		 * function).
		 */
		rdt_entry = READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
		    intin_no);
		if ((masked == 0) && ((rdt_entry & AV_PENDING) != 0) &&
		    (repeats < apic_max_reps_clear_pending)) {
			/* Unmask it */
			WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
			    intin_no, rdt_entry & ~AV_MASK);
			irqp->airq_rdt_entry &= ~AV_MASK;
		}
	} while ((rdt_entry & AV_PENDING) &&
	    (repeats < apic_max_reps_clear_pending));

#ifdef DEBUG
	if (GET_RDT_BITS(ioapic_ix, intin_no, AV_PENDING) != 0)
		apix_intr_deliver_timeouts++;
#endif

	if (!level || !APIX_IS_MASK_RDT(apix_mul_ioapic_method))
		goto done;

	/*
	 * wait for remote IRR to be cleared for level-triggered
	 * interrupts
	 */
	repeats = 0;
	do {
		repeats++;

		for (waited = 0; waited < apic_max_reps_clear_pending;
		    waited++) {
			if (GET_RDT_BITS(ioapic_ix, intin_no, AV_REMOTE_IRR)
			    == 0)
				break;
		}

		if (GET_RDT_BITS(ioapic_ix, intin_no, AV_REMOTE_IRR) != 0) {
			lock_clear(&apic_ioapic_lock);
			intr_restore(iflag);

			delay(APIX_CHECK_IRR_DELAY);

			iflag = intr_clear();
			lock_set(&apic_ioapic_lock);
		}
	} while (repeats < apix_max_reps_irr_pending);

	if (repeats >= apix_max_reps_irr_pending) {
#ifdef DEBUG
		apix_intr_rirr_timeouts++;
#endif

		/*
		 * If we waited and the Remote IRR bit is still not cleared,
		 * AND if we've invoked the timeout APIC_REPROGRAM_MAX_TIMEOUTS
		 * times for this interrupt, try the last-ditch workaround:
		 */
		if (GET_RDT_BITS(ioapic_ix, intin_no, AV_REMOTE_IRR) != 0) {
			/*
			 * Trying to clear the bit through normal
			 * channels has failed.  So as a last-ditch
			 * effort, try to set the trigger mode to
			 * edge, then to level.  This has been
			 * observed to work on many systems.
			 */
			WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
			    intin_no,
			    READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
			    intin_no) & ~AV_LEVEL);
			WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
			    intin_no,
			    READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix,
			    intin_no) | AV_LEVEL);
		}

		if (GET_RDT_BITS(ioapic_ix, intin_no, AV_REMOTE_IRR) != 0) {
#ifdef DEBUG
			apix_intr_rirr_reset_failure++;
#endif
			lock_clear(&apic_ioapic_lock);
			intr_restore(iflag);
			prom_printf("apix: Remote IRR still "
			    "not clear for IOAPIC %d intin %d.\n"
			    "\tInterrupts to this pin may cease "
			    "functioning.\n", ioapic_ix, intin_no);
			return (1);	/* return failure */
		}
	}

done:
	/* change apic_irq_table */
	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);
	apix_intx_set_vector(irqno, cpuid, vector);
	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	/* reprogramme IO-APIC RDT entry */
	apix_intx_enable(irqno);

	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);

	return (0);
}

static int
apix_intx_get_pending(int irqno)
{
	apic_irq_t *irqp;
	int intin, ioapicindex, pending;
	ulong_t iflag;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];
	if (IS_IRQ_FREE(irqp)) {
		mutex_exit(&airq_mutex);
		return (0);
	}

	/* check IO-APIC delivery status */
	intin = irqp->airq_intin_no;
	ioapicindex = irqp->airq_ioapicindex;
	mutex_exit(&airq_mutex);

	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	pending = (READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapicindex, intin) &
	    AV_PENDING) ? 1 : 0;

	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);

	return (pending);
}

/*
 * This function will mask the interrupt on the I/O APIC
 */
static void
apix_intx_set_mask(int irqno)
{
	int intin, ioapixindex, rdt_entry;
	ulong_t iflag;
	apic_irq_t *irqp;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];

	ASSERT(irqp->airq_kind != AIRQK_FREE);

	intin = irqp->airq_intin_no;
	ioapixindex = irqp->airq_ioapicindex;
	mutex_exit(&airq_mutex);

	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	rdt_entry = READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapixindex, intin);

	/* clear mask */
	WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapixindex, intin,
	    (AV_MASK | rdt_entry));

	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);
}

/*
 * This function will clear the mask for the interrupt on the I/O APIC
 */
static void
apix_intx_clear_mask(int irqno)
{
	int intin, ioapixindex, rdt_entry;
	ulong_t iflag;
	apic_irq_t *irqp;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];

	ASSERT(irqp->airq_kind != AIRQK_FREE);

	intin = irqp->airq_intin_no;
	ioapixindex = irqp->airq_ioapicindex;
	mutex_exit(&airq_mutex);

	iflag = intr_clear();
	lock_set(&apic_ioapic_lock);

	rdt_entry = READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapixindex, intin);

	/* clear mask */
	WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapixindex, intin,
	    ((~AV_MASK) & rdt_entry));

	lock_clear(&apic_ioapic_lock);
	intr_restore(iflag);
}

/*
 * For level-triggered interrupt, mask the IRQ line. Mask means
 * new interrupts will not be delivered. The interrupt already
 * accepted by a local APIC is not affected
 */
void
apix_level_intr_pre_eoi(int irq)
{
	apic_irq_t *irqp = apic_irq_table[irq];
	int apic_ix, intin_ix;

	if (irqp == NULL)
		return;

	ASSERT(apic_level_intr[irq] == TRIGGER_MODE_LEVEL);

	lock_set(&apic_ioapic_lock);

	intin_ix = irqp->airq_intin_no;
	apic_ix = irqp->airq_ioapicindex;

	if (irqp->airq_cpu != CPU->cpu_id) {
		if (!APIX_IS_MASK_RDT(apix_mul_ioapic_method))
			ioapic_write_eoi(apic_ix, irqp->airq_vector);
		lock_clear(&apic_ioapic_lock);
		return;
	}

	if (apix_mul_ioapic_method == APIC_MUL_IOAPIC_IOXAPIC) {
		/*
		 * This is a IOxAPIC and there is EOI register:
		 *	Change the vector to reserved unused vector, so that
		 *	the EOI	from Local APIC won't clear the Remote IRR for
		 *	this level trigger interrupt. Instead, we'll manually
		 *	clear it in apix_post_hardint() after ISR handling.
		 */
		WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(apic_ix, intin_ix,
		    (irqp->airq_rdt_entry & (~0xff)) | APIX_RESV_VECTOR);
	} else {
		WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(apic_ix, intin_ix,
		    AV_MASK | irqp->airq_rdt_entry);
	}

	lock_clear(&apic_ioapic_lock);
}

/*
 * For level-triggered interrupt, unmask the IRQ line
 * or restore the original vector number.
 */
void
apix_level_intr_post_dispatch(int irq)
{
	apic_irq_t *irqp = apic_irq_table[irq];
	int apic_ix, intin_ix;

	if (irqp == NULL)
		return;

	lock_set(&apic_ioapic_lock);

	intin_ix = irqp->airq_intin_no;
	apic_ix = irqp->airq_ioapicindex;

	if (APIX_IS_DIRECTED_EOI(apix_mul_ioapic_method)) {
		/*
		 * Already sent EOI back to Local APIC.
		 * Send EOI to IO-APIC
		 */
		ioapic_write_eoi(apic_ix, irqp->airq_vector);
	} else {
		/* clear the mask or restore the vector */
		WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(apic_ix, intin_ix,
		    irqp->airq_rdt_entry);

		/* send EOI to IOxAPIC */
		if (apix_mul_ioapic_method == APIC_MUL_IOAPIC_IOXAPIC)
			ioapic_write_eoi(apic_ix, irqp->airq_vector);
	}

	lock_clear(&apic_ioapic_lock);
}

static int
apix_intx_get_shared(int irqno)
{
	apic_irq_t *irqp;
	int share;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];
	if (IS_IRQ_FREE(irqp) || (irqp->airq_cpu == IRQ_UNINIT)) {
		mutex_exit(&airq_mutex);
		return (0);
	}
	share = irqp->airq_share;
	mutex_exit(&airq_mutex);

	return (share);
}

static void
apix_intx_set_shared(int irqno, int delta)
{
	apic_irq_t *irqp;

	mutex_enter(&airq_mutex);
	irqp = apic_irq_table[irqno];
	if (IS_IRQ_FREE(irqp)) {
		mutex_exit(&airq_mutex);
		return;
	}
	irqp->airq_share += delta;
	mutex_exit(&airq_mutex);
}

/*
 * Setup IRQ table. Return IRQ no or -1 on failure
 */
static int
apix_intx_setup(dev_info_t *dip, int inum, int irqno,
    struct intrspec *ispec, iflag_t *iflagp)
{
	int origirq = ispec->intrspec_vec;
	int newirq;
	apic_irq_kind_t kind = AIRQK_NONE;
	uchar_t ipin, ioapicindex;
	apic_irq_t *irqp;

	UNREFERENCED_1PARAMETER(inum);

	if (iflagp == NULL)
		return (-1);

	kind = AIRQK_FIXED;
	ioapicindex = irq_to_ioapic_index(irqno);
	ASSERT(ioapicindex != 0xFF);
	ipin = irqno - apic_io_vectbase[ioapicindex];

	if (apic_irq_table[irqno] &&
	    apic_irq_table[irqno]->airq_kind == AIRQK_FIXED) {
		ASSERT(apic_irq_table[irqno]->airq_intin_no == ipin &&
		    apic_irq_table[irqno]->airq_ioapicindex ==
		    ioapicindex);
		return (irqno);
	}

	/* allocate a new IRQ no */
	if ((irqp = apic_irq_table[irqno]) == NULL) {
		irqp = kmem_zalloc(sizeof (apic_irq_t), KM_SLEEP);
		apic_irq_table[irqno] = irqp;
	} else {
		if (irqp->airq_kind != AIRQK_FREE) {
			newirq = apic_allocate_irq(apic_first_avail_irq);
			if (newirq == -1) {
				return (-1);
			}
			irqno = newirq;
			irqp = apic_irq_table[irqno];
			ASSERT(irqp != NULL);
		}
	}

	irqp->airq_kind = kind;
	irqp->airq_ioapicindex = ioapicindex;
	irqp->airq_intin_no = ipin;
	irqp->airq_dip = dip;
	irqp->airq_origirq = (uchar_t)origirq;
	if (iflagp != NULL)
		irqp->airq_iflag = *iflagp;
	irqp->airq_cpu = IRQ_UNINIT;
	irqp->airq_vector = 0;

	return (irqno);
}

/*
 * Translate and return IRQ no
 */
static int
apix_intx_xlate_irq(dev_info_t *dip, int inum, struct intrspec *ispec)
{
	int newirq, irqno = ispec->intrspec_vec;
	iflag_t intr_flag;
	int dev_len;
	char dev_type[16];

	if (dip == NULL)
		goto nonpci;

	/*
	 * use ddi_getlongprop_buf() instead of ddi_prop_lookup_string()
	 * to avoid extra buffer allocation.
	 */
	dev_len = sizeof (dev_type);
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, ddi_get_parent(dip),
	    DDI_PROP_DONTPASS, "device_type", (caddr_t)dev_type,
	    &dev_len) == DDI_PROP_SUCCESS) {
		if ((strcmp(dev_type, "pci") == 0) ||
		    (strcmp(dev_type, "pciex") == 0)) {
			cmn_err(CE_WARN, "unsupported INTx request from broken "
			    "PCI/-X/e driver %s", ddi_driver_name(dip));
			return (-1);
		}
	}

nonpci:
	mutex_enter(&airq_mutex);

	/* XXX huashan, do we need the defconf path at all? */
	intr_flag.intr_po = INTR_PO_ACTIVE_HIGH;
	intr_flag.intr_el = INTR_EL_EDGE;
	newirq = apix_intx_setup(dip, inum, irqno, ispec, &intr_flag);
	if (newirq != -1)
		goto done;

	newirq = apix_intx_setup(dip, inum, irqno, ispec, NULL);
	if (newirq == -1) {
		mutex_exit(&airq_mutex);
		return (-1);
	}
done:
	ASSERT(apic_irq_table[newirq]);
	mutex_exit(&airq_mutex);
	return (newirq);
}

static int
apix_intx_alloc_vector(dev_info_t *dip, int inum, struct intrspec *ispec)
{
	int irqno;
	apix_vector_t *vecp;

	if ((irqno = apix_intx_xlate_irq(dip, inum, ispec)) == -1)
		return (0);

	if ((vecp = apix_alloc_intx(dip, inum, irqno)) == NULL)
		return (0);

	DDI_INTR_IMPLDBG((CE_CONT, "apix_intx_alloc_vector: dip=0x%p name=%s "
	    "irqno=0x%x cpuid=%d vector=0x%x\n",
	    (void *)dip, ddi_driver_name(dip), irqno,
	    vecp->v_cpuid, vecp->v_vector));

	return (1);
}

/*
 * Switch between safe and x2APIC IPI sending method.
 * The CPU may power on in xapic mode or x2apic mode. If the CPU needs to send
 * an IPI to other CPUs before entering x2APIC mode, it still needs to use the
 * xAPIC method. Before sending a StartIPI to the target CPU, psm_send_ipi will
 * be changed to apic_common_send_ipi, which detects current local APIC mode and
 * use the right method to send an IPI. If some CPUs fail to start up,
 * apic_poweron_cnt won't return to zero, so apic_common_send_ipi will always be
 * used. psm_send_ipi can't be simply changed back to x2apic_send_ipi if some
 * CPUs failed to start up because those failed CPUs may recover itself later at
 * unpredictable time.
 */
void
apic_switch_ipi_callback(boolean_t enter)
{
	ulong_t iflag;
	struct psm_ops *pops = psmops;

	iflag = intr_clear();
	lock_set(&apic_mode_switch_lock);
	if (enter) {
		ASSERT(apic_poweron_cnt >= 0);
		if (apic_poweron_cnt == 0) {
			pops->psm_send_ipi = apic_common_send_ipi;
			send_dirintf = pops->psm_send_ipi;
			pops->psm_send_pir_ipi = apic_common_send_pir_ipi;
			psm_send_pir_ipi = pops->psm_send_pir_ipi;
		}
		apic_poweron_cnt++;
	} else {
		ASSERT(apic_poweron_cnt > 0);
		apic_poweron_cnt--;
		if (apic_poweron_cnt == 0) {
			pops->psm_send_ipi = x2apic_send_ipi;
			send_dirintf = pops->psm_send_ipi;
			pops->psm_send_pir_ipi = x2apic_send_pir_ipi;
			psm_send_pir_ipi = pops->psm_send_pir_ipi;
		}
	}
	lock_clear(&apic_mode_switch_lock);
	intr_restore(iflag);
}

/*
 * Generic code expects apix to have this stub function; this module can't be
 * unloaded unless we failed to probe, in which case we're going to panic
 * anyway without ever sniffing userland.
 */
int
apix_loaded(void)
{
	return (1);
}
