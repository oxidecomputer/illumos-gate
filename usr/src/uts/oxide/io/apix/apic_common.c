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
 * Copyright 2016 Nexenta Systems, Inc.
 * Copyright (c) 2016, 2017 by Delphix. All rights reserved.
 * Copyright 2019 Joshua M. Clulow <josh@sysmgr.org>
 * Copyright 2020 RackTop Systems, Inc.
 * Copyright 2021 Joyent, Inc.
 * Copyright 2022 Oxide Computer Company
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#define	PSMI_1_7

#include <sys/processor.h>
#include <sys/time.h>
#include <sys/psm.h>
#include <sys/smp_impldefs.h>
#include <sys/cram.h>
#include <sys/psm_common.h>
#include <sys/apic.h>
#include <sys/apix.h>
#include <sys/apic_timer.h>
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
#include <sys/cpuvar.h>
#include <sys/rm_platter.h>
#include <sys/privregs.h>
#include <sys/cyclic.h>
#include <sys/note.h>
#include <sys/pci_intr_lib.h>
#include <sys/sunndi.h>
#include <sys/prom_debug.h>
#include <sys/hpet.h>
#include <sys/clock.h>
#include <sys/sysmacros.h>
#include <sys/spl.h>
#include <sys/dditypes.h>
#include <sys/x_call.h>
#include <sys/reboot.h>
#include <sys/apic_common.h>
#include <sys/apic_timer.h>
#include <sys/tsc.h>
#include <sys/smm.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/fch.h>
#include <sys/io/fch/pmio.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/io/genoa/iohc.h>
#include <sys/io/genoa/ccx.h>
#include <sys/io/genoa/fabric.h>
#include <genoa/genoa_physaddrs.h>

static void	apic_record_ioapic_rdt(void *intrmap_private,
		    ioapic_rdt_t *irdt);
static void	apic_record_msi(void *intrmap_private, msi_regs_t *mregs);

int	apic_clkinit(int);
hrtime_t apic_gethrtime(void);
void	apic_send_ipi(int, int);
void	apic_set_idlecpu(processorid_t);
void	apic_unset_idlecpu(processorid_t);
void	apic_preshutdown(int, int);
processorid_t	apic_get_next_processorid(processorid_t);

hrtime_t apic_gettime();

enum apic_ioapic_method_type apix_mul_ioapic_method = APIC_MUL_IOAPIC_NONE;

/* Now the ones for Dynamic Interrupt distribution */
int	apic_enable_dynamic_migration = 0;

/* maximum loop count when sending Start IPIs. */
int apic_sipi_max_loop_count = 0x1000;

/*
 * These variables are frequently accessed in apic_intr_enter(),
 * apic_intr_exit and apic_setspl, so group them together
 */
volatile uint32_t *apicadr =  NULL;	/* virtual addr of local APIC	*/
int apic_clkvect;

/* vector at which error interrupts come in */
int apic_errvect;
int apic_enable_error_intr = 1;
int apic_error_display_delay = 100;

/* vector at which performance counter overflow interrupts come in */
int apic_cpcovf_vect;
int apic_enable_cpcovf_intr = 1;

/* vector at which CMCI interrupts come in */
int apic_cmci_vect;
extern void cmi_cmci_trap(void);

lock_t apic_mode_switch_lock;

int apic_pir_vect;

/*
 * Patchable global variables.
 */
int	apic_forceload = 0;

int	apic_coarse_hrtime = 1;		/* 0 - use accurate slow gethrtime() */

int	apic_flat_model = 0;		/* 0 - clustered. 1 - flat */
int	apic_panic_on_apic_error = 0;

int	apic_verbose = 0;	/* 0x1ff */

/* If set, force APIC calibration to use the PIT instead of the TSC */
int	apic_calibrate_use_pit = 0;

/*
 * It was found empirically that 5 measurements seem sufficient to give a good
 * accuracy. Most spurious measurements are higher than the target value thus
 * we eliminate up to 2/5 spurious measurements.
 */
#define	APIC_CALIBRATE_MEASUREMENTS		5

#define	APIC_CALIBRATE_PERCENT_OFF_WARNING	10

extern int pit_is_broken; /* from tscc_pit.c */

uint64_t apic_info_tsc[APIC_CALIBRATE_MEASUREMENTS];
uint64_t apic_info_pit[APIC_CALIBRATE_MEASUREMENTS];

#ifdef DEBUG
int	apic_debug = 0;
int	apic_restrict_vector = 0;

int	apic_debug_msgbuf[APIC_DEBUG_MSGBUFSIZE];
int	apic_debug_msgbufindex = 0;

#endif /* DEBUG */

uint_t apic_nticks = 0;
uint_t apic_skipped_redistribute = 0;

uint_t last_count_read = 0;
lock_t	apic_gethrtime_lock;
volatile int	apic_hrtime_stamp = 0;
volatile hrtime_t apic_nsec_since_boot = 0;

static	hrtime_t	apic_last_hrtime = 0;
int		apic_hrtime_error = 0;
int		apic_remote_hrterr = 0;
int		apic_num_nmis = 0;
int		apic_apic_error = 0;
int		apic_num_apic_errors = 0;
int		apic_num_cksum_errors = 0;

int	apic_error = 0;

/* use to make sure only one cpu handles the nmi */
lock_t	apic_nmi_lock;
/* use to make sure only one cpu handles the error interrupt */
lock_t	apic_error_lock;

/* Patchable global variables. */
int		apic_kmdb_on_nmi = 0;		/* 0 - no, 1 - yes enter kmdb */
uint32_t	apic_divide_reg_init = 0;	/* 0 - divide by 2 */

/* default apic ops without interrupt remapping */
static apic_intrmap_ops_t apic_nointrmap_ops = {
	(int (*)(int))return_instr,
	(void (*)(int))return_instr,
	(void (*)(void **, dev_info_t *, uint16_t, int, uchar_t))return_instr,
	(void (*)(void *, void *, uint16_t, int))return_instr,
	(void (*)(void **))return_instr,
	apic_record_ioapic_rdt,
	apic_record_msi,
};

apic_intrmap_ops_t *apic_vt_ops = &apic_nointrmap_ops;
apic_cpus_info_t	*apic_cpus = NULL;
cpuset_t	apic_cpumask;
uint_t		apic_picinit_called;

/*
 *	Local Function Prototypes
 */
static int apic_find_free_irq(int start, int end);

void apic_record_rdt_entry(apic_irq_t *irqptr, int irq);

/*
 * psm name pointer
 */
char *psm_name;

static int apic_probe_raw(const char *);

/* Max wait time (in repetitions) for flags to clear in an RDT entry. */
int apic_max_reps_clear_pending = 1000;

int	apic_intr_policy = INTR_ROUND_ROBIN;

int	apic_next_bind_cpu = 1; /* For round robin assignment */
				/* start with cpu 1 */

/*
 * If enabled, the distribution works as follows:
 * On every interrupt entry, the current ipl for the CPU is set in cpu_info
 * and the irq corresponding to the ipl is also set in the aci_current array.
 * interrupt exit and setspl (due to soft interrupts) will cause the current
 * ipl to be be changed. This is cache friendly as these frequently used
 * paths write into a per cpu structure.
 *
 * Sampling is done by checking the structures for all CPUs and incrementing
 * the busy field of the irq (if any) executing on each CPU and the busy field
 * of the corresponding CPU.
 * In periodic mode this is done on every clock interrupt.
 * In one-shot mode, this is done thru a cyclic with an interval of
 * apic_redistribute_sample_interval (default 10 milli sec).
 *
 * Every apic_sample_factor_redistribution times we sample, we do computations
 * to decide which interrupt needs to be migrated (see comments
 * before apic_intr_redistribute().
 */

/*
 * Following 3 variables start as % and can be patched or set using an
 * API to be defined in future. They will be scaled to
 * sample_factor_redistribution which is in turn set to hertz+1 (in periodic
 * mode), or 101 in one-shot mode to stagger it away from one sec processing
 */

int	apic_int_busy_mark = 60;
int	apic_int_free_mark = 20;
int	apic_diff_for_redistribution = 10;

/* sampling interval for interrupt redistribution for dynamic migration */
int	apic_redistribute_sample_interval = NANOSEC / 100; /* 10 millisec */

/*
 * number of times we sample before deciding to redistribute interrupts
 * for dynamic migration
 */
int	apic_sample_factor_redistribution = 101;

int	apic_redist_cpu_skip = 0;

/*
 * Maximum number of APIC CPUs in the system, -1 indicates that dynamic
 * allocation of CPU ids is disabled.
 */
int	apic_max_nproc = -1;
int	apic_nproc = 0;
size_t	apic_cpus_size = 0;

uchar_t apic_io_id[MAX_IO_APIC];
volatile uint32_t *apicioadr[MAX_IO_APIC];
uchar_t	apic_io_ver[MAX_IO_APIC];
uchar_t	apic_io_vectbase[MAX_IO_APIC];
uchar_t	apic_io_vectend[MAX_IO_APIC];
uint32_t apic_physaddr[MAX_IO_APIC];

/*
 * First available slot to be used as IRQ index into the apic_irq_table
 * for FIXED sharing an IOAPIC pin that need their own synthetic IRQ number.
 */
int apic_first_avail_irq  = APIC_FIRST_FREE_IRQ;

/*
 * apic_ioapic_lock protects the ioapics (reg select), the status, temp_bound
 * and bound elements of cpus_info and the temp_cpu element of irq_struct
 */
lock_t	apic_ioapic_lock;

int	apic_io_max = 0;	/* no. of i/o apics enabled */

uchar_t	apic_resv_vector[MAXIPL+1];

char	apic_level_intr[APIC_MAX_VECTOR+1];

/*
 * airq_mutex protects additions to the apic_irq_table - the first
 * pointer and any airq_nexts off of that one.  It also guarantees
 * that share_id is unique as new ids are generated only when new
 * irq_t structs are linked in. Once linked in the structs are never
 * deleted.  Note that there is a slight gap between allocating in
 * apic_introp_xlate and programming in addspl.
 */
kmutex_t	airq_mutex;
apic_irq_t	*apic_irq_table[APIC_MAX_VECTOR+1];

/*
 * Auto-configuration routines
 */

int
apic_probe_common(char *modname)
{
	int	i, retval = PSM_FAILURE;

	PRM_POINT("apic_probe_common()");

	if (apic_forceload < 0)
		return (retval);

	/*
	 * Remember who we are
	 */
	psm_name = modname;

	PRM_POINT("apic_probe_raw()");
	retval = apic_probe_raw(modname);
	PRM_DEBUG(retval);

	if (retval == PSM_SUCCESS) {
		extern int apic_ioapic_method_probe();

		PRM_POINT("apic_ioapic_method_probe()");
		if ((retval = apic_ioapic_method_probe()) == PSM_SUCCESS) {
			PRM_POINT("SUCCESS");
			return (PSM_SUCCESS);
		}
	}

	for (i = 0; i < apic_io_max; i++)
		mapout_ioapic((caddr_t)apicioadr[i], APIC_IO_MEMLEN);
	if (apic_cpus) {
		kmem_free(apic_cpus, apic_cpus_size);
		apic_cpus = NULL;
	}
	if (apicadr) {
		mapout_apic((caddr_t)apicadr, APIC_LOCAL_MEMLEN);
		apicadr = NULL;
	}

	PRM_DEBUG(retval);
	return (retval);
}

static int
apic_count_thread(genoa_thread_t *mtp, void *arg)
{
	int *nthreadp = arg;

	++*nthreadp;

	return (0);
}

static int
apic_enumerate_one(genoa_thread_t *mtp, void *arg)
{
	uint32_t *idxp = arg;
	apic_cpus_info_t *acip = &apic_cpus[*idxp];

	acip->aci_local_id = genoa_thread_apicid(mtp);
	acip->aci_processor_id = acip->aci_local_id;
	acip->aci_local_ver = 0;
	acip->aci_status = 0;
	CPUSET_ADD(apic_cpumask, *idxp);
	acip->aci_local_ver =
	    (uchar_t)(apic_reg_ops->apic_read(APIC_VERS_REG) & 0xff);

	VERIFY3S(*idxp, <, apic_nproc);
	++*idxp;

	return (0);
}

static int
apic_probe_raw(const char *modname)
{
	int i;
	uint32_t irqno;
	uint32_t apic_index = 0;
	mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
	mmio_reg_t reg;
	uint64_t val;

	(void) genoa_walk_thread(apic_count_thread, &apic_nproc);
	apic_cpus_size = max(apic_nproc, max_ncpus) * sizeof (*apic_cpus);
	if ((apic_cpus = kmem_zalloc(apic_cpus_size, KM_NOSLEEP)) == NULL) {
		apic_max_nproc = -1;
		apic_nproc = 0;
		return (PSM_FAILURE);
	}

	apic_enable_x2apic();

	CPUSET_ZERO(apic_cpumask);
	(void) genoa_walk_thread(apic_enumerate_one, &apic_index);

	reg = FCH_PMIO_DECODEEN_MMIO(fch_pmio);
	val = mmio_reg_read(reg);
	val = FCH_PMIO_DECODEEN_SET_IOAPICCFG(val,
	    FCH_PMIO_DECODEEN_IOAPICCFG_LOW_LAT);
	val = FCH_PMIO_DECODEEN_SET_IOAPICEN(val, 1);
	mmio_reg_write(reg, val);
	mmio_reg_block_unmap(&fch_pmio);

	apic_io_id[0] = 0xf0;
	apic_physaddr[0] = GENOA_PHYSADDR_FCH_IOAPIC;
	apicioadr[0] = (void *)mapin_ioapic(apic_physaddr[0], APIC_IO_MEMLEN,
	    PROT_READ | PROT_WRITE);

	apic_io_id[1] = 0xf1;
	apic_physaddr[1] = GENOA_PHYSADDR_IOHC_IOAPIC;
	apicioadr[1] = (void *)mapin_ioapic(apic_physaddr[1], APIC_IO_MEMLEN,
	    PROT_READ | PROT_WRITE);

	apic_io_max = 2;

	for (i = 0, irqno = 0; i < apic_io_max; i++) {
		uint32_t ver = ioapic_read(i, APIC_VERS_CMD);
		uint32_t nent = ((ver >> 16) & 0xff);

		apic_io_ver[i] = (uint8_t)(ver & 0xff);

		ASSERT3U(irqno, <, 256);
		apic_io_vectbase[i] = (uint8_t)irqno;

		ASSERT3U(nent, <=, 256 - irqno);
		irqno += nent;
		apic_io_vectend[i] = apic_io_vectbase[i] + (uint8_t)(nent - 1);

		ioapic_write(i, APIC_ID_CMD, apic_io_id[i] << 24);

		if (apic_first_avail_irq <= apic_io_vectend[i])
			apic_first_avail_irq = apic_io_vectend[i] + 1;
	}

	return (PSM_SUCCESS);
}

boolean_t
apic_cpu_in_range(int cpu)
{
	cpu &= ~IRQ_USER_BOUND;
	/* Check whether cpu id is in valid range. */
	if (cpu < 0 || cpu >= apic_nproc) {
		return (B_FALSE);
	} else if (apic_max_nproc != -1 && cpu >= apic_max_nproc) {
		/*
		 * Check whether cpuid is in valid range if CPU DR is enabled.
		 */
		return (B_FALSE);
	} else if (!CPU_IN_SET(apic_cpumask, cpu)) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

processorid_t
apic_get_next_bind_cpu(void)
{
	int i, count;
	processorid_t cpuid = 0;

	for (count = 0; count < apic_nproc; count++) {
		if (apic_next_bind_cpu >= apic_nproc) {
			apic_next_bind_cpu = 0;
		}
		i = apic_next_bind_cpu++;
		if (apic_cpu_in_range(i)) {
			cpuid = i;
			break;
		}
	}

	return (cpuid);
}

uint16_t
apic_get_apic_version(void)
{
	int i;
	uchar_t min_io_apic_ver = 0;
	static uint16_t version;		/* Cache as value is constant */
	static boolean_t found = B_FALSE;	/* Accomodate zero version */

	if (found == B_FALSE) {
		found = B_TRUE;

		/*
		 * Don't assume all IO APICs in the system are the same.
		 *
		 * Set to the minimum version.
		 */
		for (i = 0; i < apic_io_max; i++) {
			if ((apic_io_ver[i] != 0) &&
			    ((min_io_apic_ver == 0) ||
			    (min_io_apic_ver >= apic_io_ver[i])))
				min_io_apic_ver = apic_io_ver[i];
		}

		/* Assume all local APICs are of the same version. */
		version = (min_io_apic_ver << 8) | apic_cpus[0].aci_local_ver;
	}
	return (version);
}

uchar_t
irq_to_ioapic_index(int irq)
{
	int i;

	for (i = 0; i < apic_io_max; i++) {
		if (irq >= apic_io_vectbase[i] && irq <= apic_io_vectend[i])
			return ((uchar_t)i);
	}
	return (0xFF);	/* shouldn't happen */
}

int
apic_allocate_irq(int irq)
{
	int	freeirq, i;

	if ((freeirq = apic_find_free_irq(irq, (APIC_RESV_IRQ - 1))) == -1) {
		if ((freeirq = apic_find_free_irq(APIC_FIRST_FREE_IRQ,
		    (irq - 1))) == -1) {
			/*
			 * if BIOS really defines every single irq in the mps
			 * table, then don't worry about conflicting with
			 * them, just use any free slot in apic_irq_table
			 */
			for (i = APIC_FIRST_FREE_IRQ; i < APIC_RESV_IRQ; i++) {
				if (IS_IRQ_FREE(apic_irq_table[i])) {
					freeirq = i;
					break;
				}
			}

			if (freeirq == -1) {
				/* This shouldn't happen, but just in case */
				cmn_err(CE_WARN, "%s: NO available IRQ",
				    psm_name);
				return (-1);
			}
		}
	}

	if (apic_irq_table[freeirq] == NULL) {
		apic_irq_table[freeirq] =
		    kmem_zalloc(sizeof (apic_irq_t), KM_NOSLEEP);
		if (apic_irq_table[freeirq] == NULL) {
			cmn_err(CE_WARN, "%s: NO memory to allocate IRQ",
			    psm_name);
			return (-1);
		}
		apic_irq_table[freeirq]->airq_temp_cpu = IRQ_UNINIT;
		apic_irq_table[freeirq]->airq_kind = AIRQK_FREE;
	}
	return (freeirq);
}

static int
apic_find_free_irq(int start, int end)
{
	int	i;

	for (i = start; i <= end; i++) {
		/* Then see if it is free */
		if (IS_IRQ_FREE(apic_irq_table[i]))
			return (i);
	}
	return (-1);
}

/*
 * compute the polarity, trigger mode and vector for programming into
 * the I/O apic and record in airq_rdt_entry.
 */
void
apic_record_rdt_entry(apic_irq_t *irqptr, int irq)
{
	int	ioapicindex, vector;
	uint_t	level, po;

	DDI_INTR_IMPLDBG((CE_CONT, "apic_record_rdt_entry: kind = %d "
	    "irq = 0x%x dip = 0x%p vector = 0x%x\n", irqptr->airq_kind, irq,
	    (void *)irqptr->airq_dip, irqptr->airq_vector));

	vector = irqptr->airq_vector;
	ioapicindex = irqptr->airq_ioapicindex;
	/* Assume edge triggered by default */
	level = 0;
	/* Assume active high by default */
	po = 0;

	switch (irqptr->airq_kind) {
	case AIRQK_RESERVED:
	case AIRQK_FREE:
		/* XXX should we assert !FREE? */
		apic_error |= APIC_ERR_INVALID_INDEX;
		/*FALLTHROUGH*/
	case AIRQK_MSI:
	case AIRQK_MSIX:
		return;
	case AIRQK_FIXED:
		/*
		 * XXX This code is wrong and needs to be removed.  To
		 * understand why, the discussion of interrupts in io/fch/fch.c
		 * may be helpful.
		 *
		 * On PCs, the polarity of each fixed interrupt and whether
		 * assertion of it is level- or edge-triggered comes from ACPI
		 * (newer machines) or MPS tables (ancient).  There was, and
		 * realistically is, no reliable way to know this other than
		 * having designed the board or SoC and read the associated
		 * datasheets for most non-PCI/-X/e devices.
		 *
		 * Today, there is basically no reason for any PCI/-X/e device
		 * to need or use fixed interrupts; MSI and/or MSI-X is
		 * mandatory for all PCIe devices and MSI has been part of the
		 * PCI Local Bus spec since version 2.2.  So the only fixed
		 * sources we have are those from devices inside the SoC itself,
		 * which means that their attributes are no longer
		 * board-specific but rather generic across every board
		 * (regardless of machine architecture!) with the same SoC on
		 * it.  These sources are mostly from FCH peripherals, though
		 * some can originate from parts of the NBIO logic.  The one
		 * exception is INTx-emulation, which the ixbar translates into
		 * virtual wire interrupts (again, see io/fch/fch.c).  The oxide
		 * architecture does not support INTx emulation and all such
		 * sources are mapped to the IOAPIC's catch-all (spurious)
		 * virtual input pin.
		 *
		 * With that in mind, how can we get here?  There are only two
		 * paths: apix_alloc_intx() and apix_intx_rebind().  The latter
		 * attempts to preserve the polarity and trigger mode that was
		 * previously established, and is of no further interest.  The
		 * other always assumes that we've previously been asked to
		 * allocate the interrupt via PSM_INTR_OP_ALLOC_VECTORS and thus
		 * apix_intx_alloc_vector() which, contrary to their names, have
		 * absolutely nothing to do with vectors but actually allocate
		 * what should be private to the PSM: an IRQ.
		 *
		 * The PSM was designed for PCs, where polarity and trigger mode
		 * metadata for each IRQ come from firmware, so there is no
		 * (good) way to pass that information from the fch nexus driver
		 * (which knows it for all devices that can ever have FIXED
		 * interrupts) into apix; instead, it's assumed to come from
		 * "elsewhere".  Fixing this requires either making the PSM
		 * itself more general and preserving our ability to share it
		 * with other x86 implementations or modifying it for oxide only
		 * so that this metadata can be plumbed through from nexus
		 * drivers into apix.  Unfortunately, there is also code in the
		 * "common" DDI that assumes the information stored in the
		 * devinfo tree for each interrupt doesn't need to include this,
		 * and there is "common" code in pci_intr_lib.c that relies on
		 * it; together, this makes it very difficult to make this
		 * change without either making more "common" code
		 * machine-specific or breaking existing interfaces.  For now,
		 * this assumes that all FIXED interrupts are edge-triggered and
		 * active high.
		 */

		level = po = 0;
		break;
	default:
		cmn_err(CE_PANIC, "invalid airq_kind 0x%x", irqptr->airq_kind);
	}
	if (level)
		apic_level_intr[irq] = 1;

	/* Never on this architecture. */
	VERIFY(apic_io_ver[ioapicindex] != IOAPIC_VER_82489DX);

	if (apic_verbose & APIC_VERBOSE_IOAPIC_FLAG)
		prom_printf("setio: ioapic=0x%x intin=0x%x level=0x%x po=0x%x "
		    "vector=0x%x cpu=0x%x\n\n", ioapicindex,
		    irqptr->airq_intin_no, level, po, vector, irqptr->airq_cpu);

	irqptr->airq_rdt_entry = level | po | vector;
}

void
ioapic_disable_redirection(void)
{
	int ioapic_ix;
	int intin_max;
	int intin_ix;

	/* Disable the I/O APIC redirection entries */
	for (ioapic_ix = 0; ioapic_ix < apic_io_max; ioapic_ix++) {

		/* Bits 23-16 define the maximum redirection entries */
		intin_max = (ioapic_read(ioapic_ix, APIC_VERS_CMD) >> 16)
		    & 0xff;

		for (intin_ix = 0; intin_ix <= intin_max; intin_ix++) {
			/*
			 * The assumption here is that this is safe, even for
			 * systems with IOAPICs that suffer from the hardware
			 * erratum because all devices have been quiesced before
			 * this function is called. If that assumption turns
			 * out to be false, this mask operation can induce the
			 * same erratum result we're trying to avoid.
			 */
			ioapic_write(ioapic_ix, APIC_RDT_CMD + 2 * intin_ix,
			    AV_MASK);
		}
	}
}

struct apic_state {
	int32_t as_task_reg;
	int32_t as_dest_reg;
	int32_t as_format_reg;
	int32_t as_local_timer;
	int32_t as_pcint_vect;
	int32_t as_int_vect0;
	int32_t as_int_vect1;
	int32_t as_err_vect;
	int32_t as_init_count;
	int32_t as_divide_reg;
	int32_t as_spur_int_reg;
	uint32_t as_ioapic_ids[MAX_IO_APIC];
};

static void
apic_save_state(struct apic_state *sp)
{
	int	i, cpuid;
	ulong_t	iflag;

	PMD(PMD_SX, ("apic_save_state %p\n", (void *)sp))
	/*
	 * First the local APIC.
	 */
	sp->as_task_reg = apic_reg_ops->apic_get_pri();
	sp->as_dest_reg =  apic_reg_ops->apic_read(APIC_DEST_REG);
	if (apic_mode == LOCAL_APIC)
		sp->as_format_reg = apic_reg_ops->apic_read(APIC_FORMAT_REG);
	sp->as_local_timer = apic_reg_ops->apic_read(APIC_LOCAL_TIMER);
	sp->as_pcint_vect = apic_reg_ops->apic_read(APIC_PCINT_VECT);
	sp->as_int_vect0 = apic_reg_ops->apic_read(APIC_INT_VECT0);
	sp->as_int_vect1 = apic_reg_ops->apic_read(APIC_INT_VECT1);
	sp->as_err_vect = apic_reg_ops->apic_read(APIC_ERR_VECT);
	sp->as_init_count = apic_reg_ops->apic_read(APIC_INIT_COUNT);
	sp->as_divide_reg = apic_reg_ops->apic_read(APIC_DIVIDE_REG);
	sp->as_spur_int_reg = apic_reg_ops->apic_read(APIC_SPUR_INT_REG);

	/*
	 * If on the boot processor then save the IOAPICs' IDs
	 */
	if ((cpuid = psm_get_cpu_id()) == 0) {

		iflag = intr_clear();
		lock_set(&apic_ioapic_lock);

		for (i = 0; i < apic_io_max; i++)
			sp->as_ioapic_ids[i] = ioapic_read(i, APIC_ID_CMD);

		lock_clear(&apic_ioapic_lock);
		intr_restore(iflag);
	}

	/* apic_state() is currently invoked only in Suspend/Resume */
	apic_cpus[cpuid].aci_status |= APIC_CPU_SUSPEND;
}

static void
apic_restore_state(struct apic_state *sp)
{
	int	i;
	ulong_t	iflag;

	/*
	 * First the local APIC.
	 */
	apic_reg_ops->apic_write_task_reg(sp->as_task_reg);
	if (apic_mode == LOCAL_APIC) {
		apic_reg_ops->apic_write(APIC_DEST_REG, sp->as_dest_reg);
		apic_reg_ops->apic_write(APIC_FORMAT_REG, sp->as_format_reg);
	}
	apic_reg_ops->apic_write(APIC_LOCAL_TIMER, sp->as_local_timer);
	apic_reg_ops->apic_write(APIC_PCINT_VECT, sp->as_pcint_vect);
	apic_reg_ops->apic_write(APIC_INT_VECT0, sp->as_int_vect0);
	apic_reg_ops->apic_write(APIC_INT_VECT1, sp->as_int_vect1);
	apic_reg_ops->apic_write(APIC_ERR_VECT, sp->as_err_vect);
	apic_reg_ops->apic_write(APIC_INIT_COUNT, sp->as_init_count);
	apic_reg_ops->apic_write(APIC_DIVIDE_REG, sp->as_divide_reg);
	apic_reg_ops->apic_write(APIC_SPUR_INT_REG, sp->as_spur_int_reg);

	/*
	 * the following only needs to be done once, so we do it on the
	 * boot processor, since we know that we only have one of those
	 */
	if (psm_get_cpu_id() == 0) {

		iflag = intr_clear();
		lock_set(&apic_ioapic_lock);

		/* Restore IOAPICs' APIC IDs */
		for (i = 0; i < apic_io_max; i++) {
			ioapic_write(i, APIC_ID_CMD, sp->as_ioapic_ids[i]);
		}

		lock_clear(&apic_ioapic_lock);
		intr_restore(iflag);
	}
}

/*
 * Returns 0 on success
 */
int
apic_state(psm_state_request_t *rp)
{
	PMD(PMD_SX, ("apic_state "))
	switch (rp->psr_cmd) {
	case PSM_STATE_ALLOC:
		rp->req.psm_state_req.psr_state =
		    kmem_zalloc(sizeof (struct apic_state), KM_NOSLEEP);
		if (rp->req.psm_state_req.psr_state == NULL)
			return (ENOMEM);
		rp->req.psm_state_req.psr_state_size =
		    sizeof (struct apic_state);
		PMD(PMD_SX, (":STATE_ALLOC: state %p, size %lx\n",
		    rp->req.psm_state_req.psr_state,
		    rp->req.psm_state_req.psr_state_size))
		return (0);

	case PSM_STATE_FREE:
		kmem_free(rp->req.psm_state_req.psr_state,
		    rp->req.psm_state_req.psr_state_size);
		PMD(PMD_SX, (" STATE_FREE: state %p, size %lx\n",
		    rp->req.psm_state_req.psr_state,
		    rp->req.psm_state_req.psr_state_size))
		return (0);

	case PSM_STATE_SAVE:
		PMD(PMD_SX, (" STATE_SAVE: state %p, size %lx\n",
		    rp->req.psm_state_req.psr_state,
		    rp->req.psm_state_req.psr_state_size))
		apic_save_state(rp->req.psm_state_req.psr_state);
		return (0);

	case PSM_STATE_RESTORE:
		apic_restore_state(rp->req.psm_state_req.psr_state);
		PMD(PMD_SX, (" STATE_RESTORE: state %p, size %lx\n",
		    rp->req.psm_state_req.psr_state,
		    rp->req.psm_state_req.psr_state_size))
		return (0);

	default:
		return (EINVAL);
	}
}

/*
 * Probe the ioapic method for apix module. Called in apic_probe_common()
 */
int
apic_ioapic_method_probe()
{
	/*
	 * Set IOAPIC EOI handling method. The priority from low to high is:
	 *	1. IOxAPIC: with EOI register
	 *	2. IOMMU interrupt mapping
	 *	3. Mask-Before-EOI method for systems without boot
	 *	interrupt routing, such as systems with only one IOAPIC
	 *	4. Directed EOI
	 */
	if (apic_io_ver[0] >= 0x20)
		apix_mul_ioapic_method = APIC_MUL_IOAPIC_IOXAPIC;
	if (apic_io_max == 1)
		apix_mul_ioapic_method = APIC_MUL_IOAPIC_MASK;
	if (apic_directed_EOI_supported())
		apix_mul_ioapic_method = APIC_MUL_IOAPIC_DEOI;

	/*
	 * All supported machines will pass one of the previous checks, so we're
	 * going to fail here and then our caller will eventually panic.
	 */
	if (apix_mul_ioapic_method == APIC_MUL_IOAPIC_NONE)
		return (PSM_FAILURE);

	return (PSM_SUCCESS);
}

/*
 * handler for APIC Error interrupt. Just print a warning and continue
 */
int
apic_error_intr()
{
	uint_t	error0, error1, error;
	uint_t	i;

	/*
	 * We need to write before read as per 7.4.17 of system prog manual.
	 * We do both and or the results to be safe
	 */
	error0 = apic_reg_ops->apic_read(APIC_ERROR_STATUS);
	apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
	error1 = apic_reg_ops->apic_read(APIC_ERROR_STATUS);
	error = error0 | error1;

	/*
	 * Clear the APIC error status (do this on all cpus that enter here)
	 * (two writes are required due to the semantics of accessing the
	 * error status register.)
	 */
	apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);
	apic_reg_ops->apic_write(APIC_ERROR_STATUS, 0);

	/*
	 * Prevent more than 1 CPU from handling error interrupt causing
	 * double printing (interleave of characters from multiple
	 * CPU's when using prom_printf)
	 */
	if (lock_try(&apic_error_lock) == 0)
		return (error ? DDI_INTR_CLAIMED : DDI_INTR_UNCLAIMED);
	if (error) {
#if	DEBUG
		if (apic_debug)
			debug_enter("APIC Error interrupt received");
#endif /* DEBUG */
		if (apic_panic_on_apic_error)
			cmn_err(CE_PANIC,
			    "APIC Error interrupt on CPU %d. Status = %x",
			    psm_get_cpu_id(), error);
		else {
			if ((error & ~APIC_CS_ERRORS) == 0) {
				/* cksum error only */
				apic_error |= APIC_ERR_APIC_ERROR;
				apic_apic_error |= error;
				apic_num_apic_errors++;
				apic_num_cksum_errors++;
			} else {
				/*
				 * prom_printf is the best shot we have of
				 * something which is problem free from
				 * high level/NMI type of interrupts
				 */
				prom_printf("APIC Error interrupt on CPU %d. "
				    "Status 0 = %x, Status 1 = %x\n",
				    psm_get_cpu_id(), error0, error1);
				apic_error |= APIC_ERR_APIC_ERROR;
				apic_apic_error |= error;
				apic_num_apic_errors++;
				for (i = 0; i < apic_error_display_delay; i++) {
					tenmicrosec();
				}
				/*
				 * provide more delay next time limited to
				 * roughly 1 clock tick time
				 */
				if (apic_error_display_delay < 500)
					apic_error_display_delay *= 2;
			}
		}
		lock_clear(&apic_error_lock);
		return (DDI_INTR_CLAIMED);
	} else {
		lock_clear(&apic_error_lock);
		return (DDI_INTR_UNCLAIMED);
	}
}

/*
 * Turn off the mask bit in the performance counter Local Vector Table entry.
 */
void
apic_cpcovf_mask_clear(void)
{
	apic_reg_ops->apic_write(APIC_PCINT_VECT,
	    (apic_reg_ops->apic_read(APIC_PCINT_VECT) & ~APIC_LVT_MASK));
}

static int
apic_cmci_enable(xc_arg_t arg1 __unused, xc_arg_t arg2 __unused,
    xc_arg_t arg3 __unused)
{
	apic_reg_ops->apic_write(APIC_CMCI_VECT, apic_cmci_vect);
	return (0);
}

static int
apic_cmci_disable(xc_arg_t arg1 __unused, xc_arg_t arg2 __unused,
    xc_arg_t arg3 __unused)
{
	apic_reg_ops->apic_write(APIC_CMCI_VECT, apic_cmci_vect | AV_MASK);
	return (0);
}

void
apic_cmci_setup(processorid_t cpuid, boolean_t enable)
{
	cpuset_t	cpu_set;

	CPUSET_ONLY(cpu_set, cpuid);

	if (enable) {
		xc_call(0, 0, 0, CPUSET2BV(cpu_set),
		    (xc_func_t)apic_cmci_enable);
	} else {
		xc_call(0, 0, 0, CPUSET2BV(cpu_set),
		    (xc_func_t)apic_cmci_disable);
	}
}

/*ARGSUSED1*/
int
apic_cpu_start(processorid_t cpun, caddr_t arg __unused)
{
	genoa_thread_t *mtp;

	ASSERT(MUTEX_HELD(&cpu_lock));

	if (!apic_cpu_in_range(cpun)) {
		return (EINVAL);
	}

	/*
	 * The BSP cannot be started in this manner, and since it can also never
	 * be stopped, we should never get here.
	 */
	if (cpun == 0)
		return (0);

	/*
	 * Switch to apic_common_send_ipi for safety during starting other CPUs.
	 */
	if (apic_mode == LOCAL_X2APIC) {
		apic_switch_ipi_callback(B_TRUE);
	}

	/*
	 * XXX This is the corresponding XXX to the one in mp_startup.c: this
	 * has nothing at all to do with the APIC, and it isn't shareable as
	 * much of the other apix code is.  Yet this is a function whose job is
	 * to start an AP, and this is how this machine starts APs.  Clearly
	 * PSM as conceived for i86pc is not factored correctly for this
	 * machine.
	 */
	mtp = genoa_fabric_find_thread_by_cpuid(cpun);
	VERIFY(mtp != NULL);

	if (!genoa_ccx_start_thread(mtp)) {
		cmn_err(CE_WARN, "attempt to start already-running CPU 0x%x",
		    cpun);
	}

	return (0);
}

int
apic_cpu_ops(psm_cpu_request_t *reqp)
{
	if (reqp == NULL) {
		return (EINVAL);
	}

	switch (reqp->pcr_cmd) {
	case PSM_CPU_ADD:
		return (apic_cpu_add(reqp));

	case PSM_CPU_REMOVE:
		return (apic_cpu_remove(reqp));

	case PSM_CPU_STOP:
	default:
		return (ENOTSUP);
	}
}

#ifdef	DEBUG
int	apic_break_on_cpu = 9;
int	apic_stretch_interrupts = 0;
int	apic_stretch_ISR = 1 << 3;	/* IPL of 3 matches nothing now */
#endif /* DEBUG */

/*
 * generates an interprocessor interrupt to another CPU. Any changes made to
 * this routine must be accompanied by similar changes to
 * apic_common_send_ipi().
 */
void
apic_send_ipi(int cpun, int ipl)
{
	int vector;
	ulong_t flag;

	vector = apic_resv_vector[ipl];

	ASSERT((vector >= APIC_BASE_VECT) && (vector <= APIC_SPUR_INTR));

	flag = intr_clear();

	APIC_AV_PENDING_SET();

	apic_reg_ops->apic_write_int_cmd(apic_cpus[cpun].aci_local_id,
	    vector);

	intr_restore(flag);
}

void
apic_send_pir_ipi(processorid_t cpun)
{
	const int vector = apic_pir_vect;
	ulong_t flag;

	ASSERT((vector >= APIC_BASE_VECT) && (vector <= APIC_SPUR_INTR));

	flag = intr_clear();

	/* Self-IPI for inducing PIR makes no sense. */
	if ((cpun != psm_get_cpu_id())) {
		APIC_AV_PENDING_SET();
		apic_reg_ops->apic_write_int_cmd(apic_cpus[cpun].aci_local_id,
		    vector);
	}

	intr_restore(flag);
}

int
apic_get_pir_ipivect(void)
{
	return (apic_pir_vect);
}

void
apic_set_idlecpu(processorid_t cpun __unused)
{
}

void
apic_unset_idlecpu(processorid_t cpun __unused)
{
}


void
apic_ret(void)
{
}

/*
 * If apic_coarse_time == 1, then apic_gettime() is used instead of
 * apic_gethrtime().  This is used for performance instead of accuracy.
 */

hrtime_t
apic_gettime()
{
	int old_hrtime_stamp;
	hrtime_t temp;

	/*
	 * In one-shot mode, we do not keep time, so if anyone
	 * calls psm_gettime() directly, we vector over to
	 * gethrtime().
	 * one-shot mode MUST NOT be enabled if this psm is the source of
	 * hrtime.
	 */

	if (apic_oneshot)
		return (gethrtime());


gettime_again:
	while ((old_hrtime_stamp = apic_hrtime_stamp) & 1)
		apic_ret();

	temp = apic_nsec_since_boot;

	if (apic_hrtime_stamp != old_hrtime_stamp) {	/* got an interrupt */
		goto gettime_again;
	}
	return (temp);
}

/*
 * Here we return the number of nanoseconds since booting.  Note every
 * clock interrupt increments apic_nsec_since_boot by the appropriate
 * amount.
 */
hrtime_t
apic_gethrtime(void)
{
	int curr_timeval, countval, elapsed_ticks;
	int old_hrtime_stamp, status;
	hrtime_t temp;
	uint32_t cpun;
	ulong_t oflags;

	/*
	 * In one-shot mode, we do not keep time, so if anyone
	 * calls psm_gethrtime() directly, we vector over to
	 * gethrtime().
	 * one-shot mode MUST NOT be enabled if this psm is the source of
	 * hrtime.
	 */

	if (apic_oneshot)
		return (gethrtime());

	oflags = intr_clear();	/* prevent migration */

	cpun = apic_reg_ops->apic_read(APIC_LID_REG);
	if (apic_mode == LOCAL_APIC)
		cpun >>= APIC_ID_BIT_OFFSET;

	lock_set(&apic_gethrtime_lock);

gethrtime_again:
	while ((old_hrtime_stamp = apic_hrtime_stamp) & 1)
		apic_ret();

	/*
	 * Check to see which CPU we are on.  Note the time is kept on
	 * the local APIC of CPU 0.  If on CPU 0, simply read the current
	 * counter.  If on another CPU, issue a remote read command to CPU 0.
	 */
	if (cpun == apic_cpus[0].aci_local_id) {
		countval = apic_reg_ops->apic_read(APIC_CURR_COUNT);
	} else {
#ifdef	DEBUG
		APIC_AV_PENDING_SET();
#else
		if (apic_mode == LOCAL_APIC)
			APIC_AV_PENDING_SET();
#endif /* DEBUG */

		apic_reg_ops->apic_write_int_cmd(
		    apic_cpus[0].aci_local_id, APIC_CURR_ADD | AV_REMOTE);

		while ((status = apic_reg_ops->apic_read(APIC_INT_CMD1))
		    & AV_READ_PENDING) {
			apic_ret();
		}

		if (status & AV_REMOTE_STATUS)	/* 1 = valid */
			countval = apic_reg_ops->apic_read(APIC_REMOTE_READ);
		else {	/* 0 = invalid */
			apic_remote_hrterr++;
			/*
			 * return last hrtime right now, will need more
			 * testing if change to retry
			 */
			temp = apic_last_hrtime;

			lock_clear(&apic_gethrtime_lock);

			intr_restore(oflags);

			return (temp);
		}
	}
	if (countval > last_count_read)
		countval = 0;
	else
		last_count_read = countval;

	elapsed_ticks = apic_hertz_count - countval;

	curr_timeval = APIC_TICKS_TO_NSECS(elapsed_ticks);
	temp = apic_nsec_since_boot + curr_timeval;

	if (apic_hrtime_stamp != old_hrtime_stamp) {	/* got an interrupt */
		/* we might have clobbered last_count_read. Restore it */
		last_count_read = apic_hertz_count;
		goto gethrtime_again;
	}

	if (temp < apic_last_hrtime) {
		/* return last hrtime if error occurs */
		apic_hrtime_error++;
		temp = apic_last_hrtime;
	}
	else
		apic_last_hrtime = temp;

	lock_clear(&apic_gethrtime_lock);
	intr_restore(oflags);

	return (temp);
}

static int
apic_iohc_nmi_eoi(genoa_ioms_t *ioms, void *arg __unused)
{
	smn_reg_t reg;
	uint32_t v;

	reg = genoa_ioms_reg(ioms, D_IOHC_FCTL2, 0);
	v = genoa_ioms_read(ioms, reg);
	v = IOHC_FCTL2_GET_NMI(v);
	if (v != 0) {
		/*
		 * We have no ability to handle the other bits here, as
		 * those conditions may not have resulted in an NMI.  Clear only
		 * the bit whose condition we have handled.
		 */
		genoa_ioms_write(ioms, reg, v);
		reg = genoa_ioms_reg(ioms, D_IOHC_INTR_EOI, 0);
		v = IOHC_INTR_EOI_SET_NMI(0);
		genoa_ioms_write(ioms, reg, v);
	}

	return (0);
}

/* apic NMI handler */
uint_t
apic_nmi_intr(caddr_t arg __unused, caddr_t arg1 __unused)
{
	nmi_action_t action = nmi_action;
	boolean_t is_smi;

	apic_error |= APIC_ERR_NMI;

	if (!lock_try(&apic_nmi_lock))
		return (DDI_INTR_CLAIMED);
	apic_num_nmis++;

	/*
	 * The SMI handler (see ml/smintr.s) issues a self-IPI with DM=NMI after
	 * saving the SMM state.  We then end up here as we're going to panic;
	 * see the block comment at the top of that file for details.  Here we
	 * check whether an SMI has been handled by this or another CPU; it is
	 * possible that many CPUs took SMIs and we are the first to arrive.  If
	 * any CPU has taken an SMI, we must panic regardless of whether we
	 * would ordinarily ignore an NMI.
	 */
	is_smi = smm_check_nmi();

	if (action == NMI_ACTION_UNSET)
		action = NMI_ACTION_KMDB;

	if (action == NMI_ACTION_KMDB && !psm_debugger())
		action = NMI_ACTION_PANIC;

	/*
	 * We never ignore SMIs.
	 */
	if (action == NMI_ACTION_IGNORE && is_smi)
		action = NMI_ACTION_PANIC;

	switch (action) {
	case NMI_ACTION_IGNORE:
		/*
		 * prom_printf is the best shot we have of something which is
		 * problem free from high level/NMI type of interrupts
		 */
		prom_printf("NMI received\n");
		break;

	case NMI_ACTION_PANIC:
		/* Keep panic from entering kmdb. */
		nopanicdebug = 1;
		panic("%s received\n", is_smi ? "SMI" : "NMI");
		break;

	case NMI_ACTION_KMDB:
	default:
		if (is_smi)
			debug_enter("SMI received: entering kmdb\n");
		else
			debug_enter("NMI received: entering kmdb\n");
		break;
	}

	/*
	 * We must check whether this NMI may have originated from the IOHC in
	 * response to an external assertion of NMI_SYNCFLOOD_L.  If so, we must
	 * clear the indicator flag and signal EOI to the IOHC in order to
	 * receive subsequent such NMIs.
	 */
	(void) genoa_walk_ioms(apic_iohc_nmi_eoi, NULL);

	lock_clear(&apic_nmi_lock);
	return (DDI_INTR_CLAIMED);
}

processorid_t
apic_get_next_processorid(processorid_t cpu_id)
{

	int i;

	if (cpu_id == -1)
		return ((processorid_t)0);

	for (i = cpu_id + 1; i < NCPU; i++) {
		if (apic_cpu_in_range(i))
			return (i);
	}

	return ((processorid_t)-1);
}

int
apic_cpu_add(psm_cpu_request_t *reqp)
{
	ASSERT(reqp != NULL);
	reqp->req.cpu_add.cpuid = (processorid_t)-1;

	return (ENOTSUP);
}

int
apic_cpu_remove(psm_cpu_request_t *reqp)
{
	return (ENOTSUP);
}

/*
 * Return the number of ticks the APIC decrements in SF nanoseconds.
 * The fixed-frequency PIT (aka 8254) is used for the measurement.
 */
static uint64_t
apic_calibrate_pit(void)
{
	uint8_t		pit_tick_lo;
	uint16_t	pit_tick, target_pit_tick, pit_ticks_adj;
	uint32_t	pit_ticks;
	uint32_t	start_apic_tick, end_apic_tick, apic_ticks;
	ulong_t		iflag;

	if (pit_is_broken)
		return (0);

	apic_reg_ops->apic_write(APIC_DIVIDE_REG, apic_divide_reg_init);
	apic_reg_ops->apic_write(APIC_INIT_COUNT, APIC_MAXVAL);

	iflag = intr_clear();

	/*
	 * Put the PIT in mode 0, "Interrupt On Terminal Count":
	 */
	outb(PITCTL_PORT, PIT_C0 | PIT_LOADMODE | PIT_ENDSIGMODE);

	/*
	 * The PIT counts down and then the counter value wraps around.  Load
	 * the maximum counter value:
	 */
	outb(PITCTR0_PORT, 0xFF);
	outb(PITCTR0_PORT, 0xFF);

	do {
		pit_tick_lo = inb(PITCTR0_PORT);
		pit_tick = (inb(PITCTR0_PORT) << 8) | pit_tick_lo;
	} while (pit_tick < APIC_TIME_MIN ||
	    pit_tick_lo <= APIC_LB_MIN || pit_tick_lo >= APIC_LB_MAX);

	/*
	 * Wait for the PIT to decrement by 5 ticks to ensure
	 * we didn't start in the middle of a tick.
	 * Compare with 0x10 for the wrap around case.
	 */
	target_pit_tick = pit_tick - 5;
	do {
		pit_tick_lo = inb(PITCTR0_PORT);
		pit_tick = (inb(PITCTR0_PORT) << 8) | pit_tick_lo;
	} while (pit_tick > target_pit_tick || pit_tick_lo < 0x10);

	start_apic_tick = apic_reg_ops->apic_read(APIC_CURR_COUNT);

	/*
	 * Wait for the PIT to decrement by APIC_TIME_COUNT ticks
	 */
	target_pit_tick = pit_tick - APIC_TIME_COUNT;
	do {
		pit_tick_lo = inb(PITCTR0_PORT);
		pit_tick = (inb(PITCTR0_PORT) << 8) | pit_tick_lo;
	} while (pit_tick > target_pit_tick || pit_tick_lo < 0x10);

	end_apic_tick = apic_reg_ops->apic_read(APIC_CURR_COUNT);

	intr_restore(iflag);

	apic_ticks = start_apic_tick - end_apic_tick;

	/* The PIT might have decremented by more ticks than planned */
	pit_ticks_adj = target_pit_tick - pit_tick;
	/* total number of PIT ticks corresponding to apic_ticks */
	pit_ticks = APIC_TIME_COUNT + pit_ticks_adj;

	/*
	 * Determine the number of nanoseconds per APIC clock tick
	 * and then determine how many APIC ticks to interrupt at the
	 * desired frequency
	 * apic_ticks / (pitticks / PIT_HZ) = apic_ticks_per_s
	 * (apic_ticks * PIT_HZ) / pitticks = apic_ticks_per_s
	 * apic_ticks_per_ns = (apic_ticks * PIT_HZ) / (pitticks * 10^9)
	 * apic_ticks_per_SFns =
	 * (SF * apic_ticks * PIT_HZ) / (pitticks * 10^9)
	 */
	return ((SF * apic_ticks * PIT_HZ) / ((uint64_t)pit_ticks * NANOSEC));
}

/*
 * Return the number of ticks the APIC decrements in SF nanoseconds.
 * The TSC is used for the measurement.
 */
static uint64_t
apic_calibrate_tsc(void)
{
	uint64_t	tsc_now, tsc_end, tsc_amt, tsc_hz;
	uint64_t	apic_ticks;
	uint32_t	start_apic_tick, end_apic_tick;
	ulong_t		iflag;

	tsc_hz = tsc_get_freq();

	/*
	 * APIC_TIME_COUNT is in i8254 PIT ticks, which have a period
	 * slightly under 1us. We can just treat the value as the number of
	 * microseconds for our sampling period -- that is we wait
	 * APIC_TIME_COUNT microseconds (corresponding to 'tsc_amt' of TSC
	 * ticks).
	 */
	tsc_amt = tsc_hz * APIC_TIME_COUNT / MICROSEC;

	apic_reg_ops->apic_write(APIC_DIVIDE_REG, apic_divide_reg_init);
	apic_reg_ops->apic_write(APIC_INIT_COUNT, APIC_MAXVAL);

	iflag = intr_clear();

	tsc_now = tsc_read();
	tsc_end = tsc_now + tsc_amt;
	start_apic_tick = apic_reg_ops->apic_read(APIC_CURR_COUNT);

	while (tsc_now < tsc_end)
		tsc_now = tsc_read();

	end_apic_tick = apic_reg_ops->apic_read(APIC_CURR_COUNT);

	intr_restore(iflag);

	apic_ticks = start_apic_tick - end_apic_tick;

	/*
	 * We likely did not wait exactly APIC_TIME_COUNT microseconds, but
	 * slightly longer. Add the additional amount to tsc_amt.
	 */
	tsc_amt += tsc_now - tsc_end;

	/*
	 * This calculation is analogous to the one used with the PIT.
	 * However, due to the typically _much_ higher precision of the
	 * TSC compared to the PIT, we have to be careful we do not overflow.
	 *
	 * Since contemporary APIC timers have frequencies on the order of
	 * tens of MHz (i.e. 66MHz), we calculate that first. Then we
	 * scale the result by SF (because the caller wants it scaled by
	 * that amount), then convert the result to scaled (SF) ticks per ns.
	 *
	 */
	uint64_t apic_freq = apic_ticks * tsc_hz / tsc_amt;

	return (apic_freq * SF / NANOSEC);
}

/*
 * Return the number of ticks the APIC decrements in SF nanoseconds.
 * Several measurements are taken to filter out outliers.
 */
uint64_t
apic_calibrate()
{
	uint64_t	measurements[APIC_CALIBRATE_MEASUREMENTS];
	int		median_idx;
	uint64_t	median;

	/*
	 * When running under a virtual machine, the emulated PIT and APIC
	 * counters do not always return the right values and can roll over.
	 * Those spurious measurements are relatively rare but could
	 * significantly affect the calibration.
	 * Therefore we take several measurements and then keep the median.
	 * The median is preferred to the average here as we only want to
	 * discard outliers.
	 *
	 * Traditionally, only the PIT was used to calibrate the APIC as the
	 * the TSC was not calibrated at this point in the boot process (or
	 * on even (much, much) older systems, possibly not present). On
	 * newer systems, the PIT is not always present. We now default to
	 * using the TSC (since it's now calibrated early enough in the boot
	 * process to be usable), but for debugging purposes as we transition,
	 * we still try to use the PIT and record those values. On systems
	 * without a functioning PIT, the PIT measurements will always be 0.
	 */
	for (int i = 0; i < APIC_CALIBRATE_MEASUREMENTS; i++) {
		apic_info_tsc[i] = apic_calibrate_tsc();
		apic_info_pit[i] = apic_calibrate_pit();

		if (apic_calibrate_use_pit) {
			if (pit_is_broken) {
				panic("Failed to calibrate APIC due to broken "
				    "PIT");
			}
			measurements[i] = apic_info_pit[i];
		} else {
			measurements[i] = apic_info_tsc[i];
		}
	}

	/*
	 * sort results and retrieve median.
	 */
	for (int i = 0; i < APIC_CALIBRATE_MEASUREMENTS; i++) {
		for (int j = i + 1; j < APIC_CALIBRATE_MEASUREMENTS; j++) {
			if (measurements[j] < measurements[i]) {
				uint64_t tmp = measurements[i];
				measurements[i] = measurements[j];
				measurements[j] = tmp;
			}
		}
	}
	median_idx = APIC_CALIBRATE_MEASUREMENTS / 2;
	median = measurements[median_idx];

#if (APIC_CALIBRATE_MEASUREMENTS >= 3)
	/*
	 * Check that measurements are consistent. Post a warning
	 * if the three middle values are not close to each other.
	 */
	uint64_t delta_warn = median *
	    APIC_CALIBRATE_PERCENT_OFF_WARNING / 100;
	if ((median - measurements[median_idx - 1]) > delta_warn ||
	    (measurements[median_idx + 1] - median) > delta_warn) {
		cmn_err(CE_WARN, "apic_calibrate measurements lack "
		    "precision: %llu, %llu, %llu.",
		    (u_longlong_t)measurements[median_idx - 1],
		    (u_longlong_t)median,
		    (u_longlong_t)measurements[median_idx + 1]);
	}
#endif

	return (median);
}

/*
 * Initialise the APIC timer on the local APIC of CPU 0 to the desired
 * frequency.  Note at this stage in the boot sequence, the boot processor
 * is the only active processor.
 * hertz value of 0 indicates a one-shot mode request.  In this case
 * the function returns the resolution (in nanoseconds) for the hardware
 * timer interrupt.  If one-shot mode capability is not available,
 * the return value will be 0. apic_enable_oneshot is a global switch
 * for disabling the functionality.
 * A non-zero positive value for hertz indicates a periodic mode request.
 * In this case the hardware will be programmed to generate clock interrupts
 * at hertz frequency and returns the resolution of interrupts in
 * nanosecond.
 */

int
apic_clkinit(int hertz)
{
	int		ret;

	apic_int_busy_mark = (apic_int_busy_mark *
	    apic_sample_factor_redistribution) / 100;
	apic_int_free_mark = (apic_int_free_mark *
	    apic_sample_factor_redistribution) / 100;
	apic_diff_for_redistribution = (apic_diff_for_redistribution *
	    apic_sample_factor_redistribution) / 100;

	ret = apic_timer_init(hertz);
	return (ret);

}

/*
 * apic_preshutdown:
 * Called early in shutdown whilst we can still access filesystems to do
 * things like loading modules which will be required to complete shutdown
 * after filesystems are all unmounted.
 */
void
apic_preshutdown(int cmd __unused, int fcn __unused)
{
}

cyclic_id_t apic_cyclic_id;

/*
 * The following functions are in the platform specific file so that they
 * can be different functions depending on whether we are running on
 * bare metal or a hypervisor.
 */

/*
 * map an apic for memory-mapped access
 */
uint32_t *
mapin_apic(uint32_t addr, size_t len, int flags)
{
	return ((void *)psm_map_phys(addr, len, flags));
}

uint32_t *
mapin_ioapic(uint32_t addr, size_t len, int flags)
{
	return (mapin_apic(addr, len, flags));
}

/*
 * unmap an apic
 */
void
mapout_apic(caddr_t addr, size_t len)
{
	psm_unmap_phys(addr, len);
}

void
mapout_ioapic(caddr_t addr, size_t len)
{
	mapout_apic(addr, len);
}

uint32_t
ioapic_read(int ioapic_ix, uint32_t reg)
{
	volatile uint32_t *ioapic;

	ioapic = apicioadr[ioapic_ix];
	ioapic[APIC_IO_REG] = reg;
	return (ioapic[APIC_IO_DATA]);
}

void
ioapic_write(int ioapic_ix, uint32_t reg, uint32_t value)
{
	volatile uint32_t *ioapic;

	ioapic = apicioadr[ioapic_ix];
	ioapic[APIC_IO_REG] = reg;
	ioapic[APIC_IO_DATA] = value;
}

void
ioapic_write_eoi(int ioapic_ix, uint32_t value)
{
	volatile uint32_t *ioapic;

	ioapic = apicioadr[ioapic_ix];
	ioapic[APIC_IO_EOI] = value;
}

/*
 * Round-robin algorithm to find the next CPU with interrupts enabled.
 * It can't share the same static variable apic_next_bind_cpu with
 * apic_get_next_bind_cpu(), since that will cause all interrupts to be
 * bound to CPU1 at boot time.  During boot, only CPU0 is online with
 * interrupts enabled when apic_get_next_bind_cpu() and apic_find_cpu()
 * are called.  However, the apix driver assumes that there will be
 * boot_ncpus CPUs configured eventually so it tries to distribute all
 * interrupts among CPU0 - CPU[boot_ncpus - 1].  Thus to prevent all
 * interrupts being targetted at CPU1, we need to use a dedicated static
 * variable for find_next_cpu() instead of sharing apic_next_bind_cpu.
 */

processorid_t
apic_find_cpu(int flag)
{
	int i;
	static processorid_t acid = 0;

	/* Find the first CPU with the passed-in flag set */
	for (i = 0; i < apic_nproc; i++) {
		if (++acid >= apic_nproc) {
			acid = 0;
		}
		if (apic_cpu_in_range(acid) &&
		    (apic_cpus[acid].aci_status & flag)) {
			break;
		}
	}

	ASSERT((apic_cpus[acid].aci_status & flag) != 0);
	return (acid);
}

void
apic_intrmap_init(int apic_mode)
{
	int suppress_brdcst_eoi = 0;

	/*
	 * Intel Software Developer's Manual 3A, 10.12.7:
	 *
	 * Routing of device interrupts to local APIC units operating in x2APIC
	 * mode requires use of the interrupt-remapping architecture specified
	 * in the Intel Virtualization Technology for Directed I/O, Revision
	 * 1.3.
	 *
	 * In other words, to use the APIC in x2APIC mode, we need interrupt
	 * remapping, but this requirement is meaningful only when we have APIC
	 * IDs greater than 254.  If we do, then we must start up the IOMMU so
	 * we can do interrupt remapping before we enable x2APIC mode.
	 *
	 * XXX For now, the only way to end up with 256 CPUs is to have a 2S
	 * machine with dual 64c processors and SMT enabled.  That is nominally
	 * supported on Ethanol-X, but never on Gimlet.  This will need to be
	 * reworked to support such configurations.
	 */
	if (psm_vt_ops != NULL) {
		if (((apic_intrmap_ops_t *)psm_vt_ops)->
		    apic_intrmap_init(apic_mode) == DDI_SUCCESS) {

			apic_vt_ops = psm_vt_ops;

			/*
			 * We leverage the interrupt remapping engine to
			 * suppress broadcast EOI; thus we must send the
			 * directed EOI with the directed-EOI handler.
			 */
			if (apic_directed_EOI_supported() == 0) {
				suppress_brdcst_eoi = 1;
			}

			apic_vt_ops->apic_intrmap_enable(suppress_brdcst_eoi);

			if (apic_detect_x2apic()) {
				apic_enable_x2apic();
			}

			if (apic_directed_EOI_supported() == 0) {
				apic_set_directed_EOI_handler();
			}
		}
	}
}

static void
apic_record_ioapic_rdt(void *intrmap_private __unused, ioapic_rdt_t *irdt)
{
	irdt->ir_hi <<= APIC_ID_BIT_OFFSET;
}

static void
apic_record_msi(void *intrmap_private __unused, msi_regs_t *mregs)
{
	mregs->mr_addr = MSI_ADDR_HDR |
	    (MSI_ADDR_RH_FIXED << MSI_ADDR_RH_SHIFT) |
	    (MSI_ADDR_DM_PHYSICAL << MSI_ADDR_DM_SHIFT) |
	    (mregs->mr_addr << MSI_ADDR_DEST_SHIFT);
	mregs->mr_data = (MSI_DATA_TM_EDGE << MSI_DATA_TM_SHIFT) |
	    mregs->mr_data;
}

/*
 * Functions from apic_introp.c
 *
 * Those functions are used by apic_intr_ops().
 */

/*
 * MSI support flag:
 * reflects whether MSI is supported at APIC level
 * it can also be patched through /etc/system
 *
 *  0 = default value - don't know and need to call apic_check_msi_support()
 *      to find out then set it accordingly
 *  1 = supported
 * -1 = not supported
 */
int	apic_support_msi = 0;

/* Multiple vector support for MSI-X */
int	apic_msix_enable = 1;

/* Multiple vector support for MSI */
int	apic_multi_msi_enable = 1;

/*
 * Check whether the system supports MSI.
 *
 * MSI is required for PCI-E and for PCI versions later than 2.2, so if we find
 * a PCI-E bus or we find a PCI bus whose version we know is >= 2.2, then we
 * return PSM_SUCCESS to indicate this system supports MSI.
 *
 * (Currently the only way we check whether a given PCI bus supports >= 2.2 is
 * by detecting if we are running inside the KVM hypervisor, which guarantees
 * this version number.)
 */
int
apic_check_msi_support(void)
{
	dev_info_t *cdip;
	char dev_type[16];
	int dev_len;
	int hwenv = get_hwenv();

	DDI_INTR_IMPLDBG((CE_CONT, "apic_check_msi_support:\n"));

	/*
	 * check whether the first level children of root_node have
	 * PCI-E or PCI capability.
	 */
	for (cdip = ddi_get_child(ddi_root_node()); cdip != NULL;
	    cdip = ddi_get_next_sibling(cdip)) {

		DDI_INTR_IMPLDBG((CE_CONT, "apic_check_msi_support: cdip: 0x%p,"
		    " driver: %s, binding: %s, nodename: %s\n", (void *)cdip,
		    ddi_driver_name(cdip), ddi_binding_name(cdip),
		    ddi_node_name(cdip)));
		dev_len = sizeof (dev_type);
		if (ddi_getlongprop_buf(DDI_DEV_T_ANY, cdip, DDI_PROP_DONTPASS,
		    "device_type", (caddr_t)dev_type, &dev_len)
		    != DDI_PROP_SUCCESS)
			continue;
		if (strcmp(dev_type, "pciex") == 0)
			return (PSM_SUCCESS);
		if (strcmp(dev_type, "pci") == 0 &&
		    (hwenv == HW_KVM || hwenv == HW_BHYVE))
			return (PSM_SUCCESS);
	}

	/* MSI is not supported on this system */
	DDI_INTR_IMPLDBG((CE_CONT, "apic_check_msi_support: no 'pciex' "
	    "device_type found\n"));
	return (PSM_FAILURE);
}

/*
 * apic_pci_msi_unconfigure:
 *
 * This and next two interfaces are copied from pci_intr_lib.c
 * Do ensure that these two files stay in sync.
 * These needed to be copied over here to avoid a deadlock situation on
 * certain mp systems that use MSI interrupts.
 *
 * IMPORTANT regards next three interfaces:
 * i) are called only for MSI/X interrupts.
 * ii) called with interrupts disabled, and must not block
 */
void
apic_pci_msi_unconfigure(dev_info_t *rdip, int type, int inum)
{
	ushort_t		msi_ctrl;
	int			cap_ptr = i_ddi_get_msi_msix_cap_ptr(rdip);
	ddi_acc_handle_t	handle = i_ddi_get_pci_config_handle(rdip);

	ASSERT((handle != NULL) && (cap_ptr != 0));

	if (type == DDI_INTR_TYPE_MSI) {
		msi_ctrl = pci_config_get16(handle, cap_ptr + PCI_MSI_CTRL);
		msi_ctrl &= (~PCI_MSI_MME_MASK);
		pci_config_put16(handle, cap_ptr + PCI_MSI_CTRL, msi_ctrl);
		pci_config_put32(handle, cap_ptr + PCI_MSI_ADDR_OFFSET, 0);

		if (msi_ctrl &  PCI_MSI_64BIT_MASK) {
			pci_config_put16(handle,
			    cap_ptr + PCI_MSI_64BIT_DATA, 0);
			pci_config_put32(handle,
			    cap_ptr + PCI_MSI_ADDR_OFFSET + 4, 0);
		} else {
			pci_config_put16(handle,
			    cap_ptr + PCI_MSI_32BIT_DATA, 0);
		}

	} else if (type == DDI_INTR_TYPE_MSIX) {
		uintptr_t	off;
		uint32_t	mask;
		ddi_intr_msix_t	*msix_p = i_ddi_get_msix(rdip);

		ASSERT(msix_p != NULL);

		/* Offset into "inum"th entry in the MSI-X table & mask it */
		off = (uintptr_t)msix_p->msix_tbl_addr + (inum *
		    PCI_MSIX_VECTOR_SIZE) + PCI_MSIX_VECTOR_CTRL_OFFSET;

		mask = ddi_get32(msix_p->msix_tbl_hdl, (uint32_t *)off);

		ddi_put32(msix_p->msix_tbl_hdl, (uint32_t *)off, (mask | 1));

		/* Offset into the "inum"th entry in the MSI-X table */
		off = (uintptr_t)msix_p->msix_tbl_addr +
		    (inum * PCI_MSIX_VECTOR_SIZE);

		/* Reset the "data" and "addr" bits */
		ddi_put32(msix_p->msix_tbl_hdl,
		    (uint32_t *)(off + PCI_MSIX_DATA_OFFSET), 0);
		ddi_put64(msix_p->msix_tbl_hdl, (uint64_t *)off, 0);
	}
}

/*
 * apic_pci_msi_disable_mode:
 */
void
apic_pci_msi_disable_mode(dev_info_t *rdip, int type)
{
	ushort_t		msi_ctrl;
	int			cap_ptr = i_ddi_get_msi_msix_cap_ptr(rdip);
	ddi_acc_handle_t	handle = i_ddi_get_pci_config_handle(rdip);

	ASSERT((handle != NULL) && (cap_ptr != 0));

	if (type == DDI_INTR_TYPE_MSI) {
		msi_ctrl = pci_config_get16(handle, cap_ptr + PCI_MSI_CTRL);
		if (!(msi_ctrl & PCI_MSI_ENABLE_BIT))
			return;

		msi_ctrl &= ~PCI_MSI_ENABLE_BIT;	/* MSI disable */
		pci_config_put16(handle, cap_ptr + PCI_MSI_CTRL, msi_ctrl);

	} else if (type == DDI_INTR_TYPE_MSIX) {
		msi_ctrl = pci_config_get16(handle, cap_ptr + PCI_MSIX_CTRL);
		if (msi_ctrl & PCI_MSIX_ENABLE_BIT) {
			msi_ctrl &= ~PCI_MSIX_ENABLE_BIT;
			pci_config_put16(handle, cap_ptr + PCI_MSIX_CTRL,
			    msi_ctrl);
		}
	}
}

uint32_t
apic_get_localapicid(uint32_t cpuid)
{
	ASSERT(cpuid < apic_nproc && apic_cpus != NULL);

	return (apic_cpus[cpuid].aci_local_id);
}

uchar_t
apic_get_ioapicid(uchar_t ioapicindex)
{
	ASSERT(ioapicindex < MAX_IO_APIC);

	return (apic_io_id[ioapicindex]);
}
