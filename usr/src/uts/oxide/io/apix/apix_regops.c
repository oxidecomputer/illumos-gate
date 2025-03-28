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
 * Copyright 2014 Josef 'Jeff' Sipek <jeffpc@josefsipek.net>
 * Copyright (c) 2014 by Delphix. All rights reserved.
 * Copyright 2018 Joyent, Inc.
 * Copyright 2022 Oxide Computer Co.
 */

#include <sys/cpuvar.h>
#include <sys/psm.h>
#include <sys/archsystm.h>
#include <sys/apic.h>
#include <sys/apic_common.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/mach_intr.h>
#include <sys/sysmacros.h>
#include <sys/trap.h>
#include <sys/x86_archext.h>
#include <sys/privregs.h>
#include <sys/psm_common.h>

/* Function prototypes of X2APIC */
static uint64_t local_x2apic_read(uint32_t msr);
static void local_x2apic_write(uint32_t msr, uint64_t value);
static int get_local_x2apic_pri(void);
static void local_x2apic_write_task_reg(uint64_t value);
static void local_x2apic_write_int_cmd(uint32_t cpu_id, uint32_t cmd1);

/*
 * According to the X2APIC specification:
 *
 *   xAPIC global enable    X2APIC enable         Description
 *   (IA32_APIC_BASE[11])   (IA32_APIC_BASE[10])
 * -----------------------------------------------------------
 *	0			0	APIC is disabled
 *	0			1	Invalid
 *	1			0	APIC is enabled in xAPIC mode
 *	1			1	APIC is enabled in X2APIC mode
 * -----------------------------------------------------------
 */

/* X2APIC : Uses RDMSR/WRMSR instructions to access APIC registers */
static apic_reg_ops_t x2apic_regs_ops = {
	local_x2apic_read,
	local_x2apic_write,
	get_local_x2apic_pri,
	local_x2apic_write_task_reg,
	local_x2apic_write_int_cmd,
	apic_send_EOI,
};

/*
 * X2APIC Implementation.
 */
static uint64_t
local_x2apic_read(uint32_t msr)
{
	uint64_t i;

	i = (uint64_t)(rdmsr(REG_X2APIC_BASE_MSR + (msr >> 2)) & 0xffffffff);
	return (i);
}

static void
local_x2apic_write(uint32_t msr, uint64_t value)
{
	uint64_t tmp;

	if (msr != APIC_EOI_REG) {
		tmp = rdmsr(REG_X2APIC_BASE_MSR + (msr >> 2));
		tmp = (tmp & 0xffffffff00000000) | value;
	} else {
		tmp = 0;
	}

	wrmsr((REG_X2APIC_BASE_MSR + (msr >> 2)), tmp);
}

static int
get_local_x2apic_pri(void)
{
	return (rdmsr(REG_X2APIC_BASE_MSR + (APIC_TASK_REG >> 2)));
}

static void
local_x2apic_write_task_reg(uint64_t value)
{
	X2APIC_WRITE(APIC_TASK_REG, value);
}

static void
local_x2apic_write_int_cmd(uint32_t cpu_id, uint32_t cmd1)
{
	wrmsr((REG_X2APIC_BASE_MSR + (APIC_INT_CMD1 >> 2)),
	    (((uint64_t)cpu_id << 32) | cmd1));
}

int
apic_detect_x2apic(void)
{
	if (!is_x86_feature(x86_featureset, X86FSET_X2APIC)) {
		panic("x2APIC support is mandatory for this kernel but was "
		    "not found via CPUID\n");
	}

	return (1);
}

void
apic_enable_x2apic(void)
{
	uint64_t apic_base_msr = rdmsr(REG_APIC_BASE_MSR);
	apic_mode_t hwmode = apic_local_mode();

	/*
	 * The Intel x2APIC spec states that the processor comes out of reset
	 * with EN (bit 11) set and EXTD (bit 10) clear; that is, in xAPIC
	 * mode or our LOCAL_APIC.  However, AMD's implementation, at least on
	 * some models, appears to come out of reset with EN = EXTD = 0, or
	 * our APIC_IS_DISABLED.  Despite this divergence from the Intel spec,
	 * AMD's implementation does follow the state transition diagram from
	 * x2APIC fig. 2-9 in that a transition from APIC_IS_DISABLED to
	 * LOCAL_X2APIC is forbidden.  AMD however do not document this in
	 * their PPRs.  We must take the set of legal transitions into
	 * consideration here; if the LAPIC is not already enabled, we must
	 * enable it first or we will take a #GP.
	 */
	switch (hwmode) {
	case APIC_MODE_NOTSET:
	default:
		/*
		 * This should never happen; it's documented as an illegal
		 * state.  The x2APIC spec says we should always be able to
		 * disable both xAPIC and x2APIC modes, so try to return to
		 * that legal state before proceeding.
		 */
		apic_base_msr &= ~LAPIC_MODE_MASK;
		wrmsr(REG_APIC_BASE_MSR, apic_base_msr);
		/*FALLTHROUGH*/
	case APIC_IS_DISABLED:
		apic_base_msr |= LAPIC_ENABLE_MASK;
		wrmsr(REG_APIC_BASE_MSR, apic_base_msr);
		/*FALLTHROUGH*/
	case LOCAL_APIC:
		apic_base_msr |= X2APIC_ENABLE_MASK;
		wrmsr(REG_APIC_BASE_MSR, apic_base_msr);
		/*FALLTHROUGH*/
	case LOCAL_X2APIC:
		if (apic_mode != LOCAL_X2APIC)
			x2apic_update_psm();
	}
}

/*
 * Change apic_reg_ops depending upon the apic_mode.
 */
void
apic_change_ops(void)
{
	if (apic_mode == LOCAL_APIC)
		apic_reg_ops = &local_apic_regs_ops;
	else if (apic_mode == LOCAL_X2APIC)
		apic_reg_ops = &x2apic_regs_ops;
}

/*
 * Generates an interprocessor interrupt to another CPU when X2APIC mode is
 * enabled.
 */
void
x2apic_send_ipi(int cpun, int ipl)
{
	int vector;
	ulong_t flag;

	ASSERT(apic_mode == LOCAL_X2APIC);

	/*
	 * With X2APIC, Intel relaxed the semantics of the
	 * WRMSR instruction such that references to the X2APIC
	 * MSR registers are no longer serializing instructions.
	 * The code that initiates IPIs assumes that some sort
	 * of memory serialization occurs. The old APIC code
	 * did a write to uncachable memory mapped registers.
	 * Any reference to uncached memory is a serializing
	 * operation. To mimic those semantics here, we do an
	 * atomic operation, which translates to a LOCK OR instruction,
	 * which is serializing.
	 */
	atomic_or_ulong(&flag, 1);

	vector = apic_resv_vector[ipl];

	flag = intr_clear();

	/*
	 * According to X2APIC specification in section '2.3.5.1' of
	 * Interrupt Command Register Semantics, the semantics of
	 * programming Interrupt Command Register to dispatch an interrupt
	 * is simplified. A single MSR write to the 64-bit ICR is required
	 * for dispatching an interrupt. Specifically with the 64-bit MSR
	 * interface to ICR, system software is not required to check the
	 * status of the delivery status bit prior to writing to the ICR
	 * to send an IPI. With the removal of the Delivery Status bit,
	 * system software no longer has a reason to read the ICR. It remains
	 * readable only to aid in debugging.
	 */
#ifdef	DEBUG
	APIC_AV_PENDING_SET();
#endif	/* DEBUG */

	if ((cpun == psm_get_cpu_id())) {
		X2APIC_WRITE(X2APIC_SELF_IPI, vector);
	} else {
		apic_reg_ops->apic_write_int_cmd(
		    apic_cpus[cpun].aci_local_id, vector);
	}

	intr_restore(flag);
}

void
x2apic_send_pir_ipi(processorid_t cpun)
{
	const int vector = apic_pir_vect;
	ulong_t flag;

	ASSERT3S(apic_mode, ==, LOCAL_X2APIC);
	ASSERT3S(vector, >=, APIC_BASE_VECT);
	ASSERT3S(vector, <=, APIC_SPUR_INTR);

	/* Serialize as described in x2apic_send_ipi() above. */
	atomic_or_ulong(&flag, 1);

	flag = intr_clear();

	/* Self-IPI for inducing PIR makes no sense. */
	if ((cpun != psm_get_cpu_id())) {
#ifdef	DEBUG
		/* Only for debugging. (again, see: x2apic_send_ipi) */
		APIC_AV_PENDING_SET();
#endif	/* DEBUG */

		apic_reg_ops->apic_write_int_cmd(apic_cpus[cpun].aci_local_id,
		    vector);
	}

	intr_restore(flag);
}

/*
 * Generates IPI to another CPU depending on the local APIC mode.
 * apic_send_ipi() and x2apic_send_ipi() depends on the configured
 * mode of the local APIC, but that may not match the actual mode
 * early in CPU startup.
 *
 * Any changes made to this routine must be accompanied by similar
 * changes to apic_send_ipi().
 */
void
apic_common_send_ipi(int cpun, int ipl)
{
	int vector;
	ulong_t flag;
	int mode = apic_local_mode();

	if (mode == LOCAL_X2APIC) {
		x2apic_send_ipi(cpun, ipl);
		return;
	}

	/*
	 * These assertions are not the best.  There are contexts in which
	 * panicking here will fail and look like a hard hang; an NMI may or
	 * may not yield a dump.  Do not upgrade these to VERIFYs, at least.
	 */
	ASSERT3S(mode, ==, LOCAL_APIC);

	vector = apic_resv_vector[ipl];
	ASSERT3S(vector, >=, APIC_BASE_VECT);
	ASSERT3S(vector, <=, APIC_SPUR_INTR);
	flag = intr_clear();
	while (local_apic_regs_ops.apic_read(APIC_INT_CMD1) & AV_PENDING)
		apic_ret();
	local_apic_regs_ops.apic_write_int_cmd(apic_cpus[cpun].aci_local_id,
	    vector);
	intr_restore(flag);
}

void
apic_common_send_pir_ipi(processorid_t cpun)
{
	const int mode = apic_local_mode();

	if (mode == LOCAL_X2APIC) {
		x2apic_send_pir_ipi(cpun);
		return;
	}

	apic_send_pir_ipi(cpun);
}
