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
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 *
 * Copyright 2019 Joyent, Inc.
 * Copyright 2020 OmniOS Community Edition (OmniOSce) Association.
 * Copyright 2022 Oxide Computer Co.
 */

#include <sys/asm_linkage.h>
#include <sys/asm_misc.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/x86_archext.h>
#include <sys/rm_platter.h>

#include <sys/segments.h>
#include "assym.h"

/*
 * APs start at the reset vector and immediately jump to this code copied onto
 * the beginning of the page on which the reset vector is located; see mp_rmp.c.
 *
 * Our assumptions:
 *
 * - We are running in real mode
 * - The RMP has been populated for us
 * - The GDT, IDT, ktss and page directory has been built for us
 * - %csbase is magic; see discussion below
 *
 * Our actions to start up each AP:
 *
 * - First, get to 64-bit mode, with a stop at big-flat mode so that we can
 *   reference memory using real descriptors TEMP_CS32_SEL and TEMP_DS32_SEL
 *   in our temporary GDT.
 * - We start using our real GDT by loading correct values in the selector
 *   registers (cs=KCS_SEL, ds=es=ss=KDS_SEL, fs=KFS_SEL, gs=KGS_SEL).
 * - We change over to using our IDT.
 * - We load the default LDT into the hardware LDT register.
 * - We load the default TSS into the hardware task register.
 * - call mp_startup(void) indirectly through the T_PC
 *
 * We don't support stopping a CPU, because the hardware provides no way to do
 * it.  CPUs can be taken offline through the ordinary code paths, but cannot
 * be shut off.  Neither our kernel nor any current processor supports
 * cpr/suspend/resume, either; everything here is reversible only by dropping
 * our power state to A2 or lower (G3 as visible to this processor).
 *
 * The Magic %csbase
 *
 * When an AMD processor family 0x17 and later starts up (BSP or AP), it begins
 * fetching instructions from memory, not from the boot flash's MMIO space.
 * This memory can be, and generally is, above the 1 MiB boundary.  Yet for
 * reasons known only to AMD, the processor is still in real mode.  How, then,
 * do memory accesses (including instruction fetches!) work?  The answer is
 * that while %cs is set to the same value it has been from time immemorial
 * (0xf000), %csbase is magically set to refer to the 32-bit location set up
 * in the APCB (or, possibly, set up by a subsequent RPC to the PSP, if that
 * actually works).  This is why we saved the BSP's reset vector and the page
 * it's on: the APs are going to start in the same place.  As long as %cs is
 * left at its default value, one may access anything within or offsettable
 * against the 64 kiB region ending 16 bytes past the reset vector at
 * %cs:0xfff0.
 *
 * Unfortunately (as if this whole thing weren't unfortunate enough as it is),
 * the magic is in %csbase, not %cs.  Copying %cs to %ds, %ss, or any other
 * segment register is useless; the hidden portion of the segment registers
 * will be set to their ordinary real-mode values and you will be able to
 * access only the bottom MiB of memory (and/or MMIO, if you've set things up
 * that way which we do not; there is nothing in the legacy range that we need
 * or want).
 *
 * The rule, then, is simple:
 *
 * ** Until we have set up a GDT with valid 32-bit selectors and put those
 *    selectors into the segment registers, all memory references MUST be
 *    made against %cs, and referencing memory outside the boot segment's
 *    offsettable range is impossible. **
 *
 * In our case, we reference only the last page in the segment, which has been
 * reserved for us.  Writing outside this page will corrupt kernel memory and
 * is therefore not recommended.  Our objective is simply to get out of this
 * state as quickly as possible, starting by setting up a GDT with normal
 * descriptors we can use to reference other memory.
 *
 * The interesting thing about all this is that this weird initial state is
 * made possible by the PSP populating the CC6 save buffers and then treating
 * startup as CC6 resume.  In principle, then, it should be possible to populate
 * the CC6 save buffer with something much spicier than a single magic segment
 * selector base address.  The contents of the CC6 save buffer are not
 * documented anywhere, but they must surely include things like %cr0, %cr3,
 * %cr4, EFER, and the xDTRs.  Someone should think about what it would take
 * to start all the APs directly in long mode, so that this godawful relic of
 * the 1970s can be laid to rest where it belonged 30 years ago.
 */

	/*
	 * NOTE:  The GNU assembler automatically does the right thing to
	 *	  generate data size operand prefixes based on the code size
	 *	  generation mode (e.g. .code16, .code32, .code64) and as such
	 *	  prefixes need not be used on instructions EXCEPT in the case
	 *	  of address prefixes for code for which the reference is not
	 *	  automatically of the default operand size.
	 */
	.code16
	ENTRY_NP(real_mode_start_cpu)

	cli

	/*
	 * Register usage note: we set up %bx/%ebx/%rbx at each step so that it
	 * points to the rm_platter_t in whatever addressing mode we are using.
	 * This makes use of the assym offsets convenient.
	 */
	movw	$RMP_BASE_SEGOFF, %bx

	lidtl	%cs:TEMPIDTOFF(%bx)
	lgdtl	%cs:TEMPGDTOFF(%bx)

	leaw	RVCODEOFF(%bx), %bp		/* Avoid %sp's implicit %ss */

	movl	%cs:PE32OFF(%bx), %eax
	subw	$2, %bp				/* Not pushw; must use %cs */
	movw	$TEMP_CS32_SEL, %cs:(%bp)
	subw	$4, %bp				/* Not pushl; must use %cs */
	movl	%eax, %cs:(%bp)

	/*
	 * Now we're going to enter protected mode and jump into a 32-bit
	 * code segment.  Set up %ebx for use there while it's convenient to
	 * load our base address from the RMP.
	 */
	movl	%cs:RMPBASEPAOFF(%bx), %ebx

	movl	%cr0, %eax
	orl	$(CR0_PE | CR0_WP | CR0_AM), %eax
	movl	%eax, %cr0

	ljmpl	*%cs:(%bp)			/* Not lretl; must use %cs */

	.globl	pe32start
pe32start:
	.code32

	/*
	 * We are now in protected mode in a 32-bit code segment.  Now we can
	 * access the contents of the RMP using their absolute physical
	 * addresses, and get to long mode.
	 */
	movw	$TEMP_DS32_SEL, %ax
	movw	%ax, %ds
	movw	%ax, %ss

	leal	RVCODEOFF(%ebx), %esp

	/*
	 * Set our default MTRR to enable WB.  We don't otherwise use MTRRs.
	 */
	movl	$MSR_MTRR_DEF_TYPE, %ecx
	movl	$(MTRR_DEF_TYPE_EN | MTRR_TYPE_WB), %eax
	xorl	%edx, %edx
	wrmsr

	/*
	 * Copy most of %cr4's contents from the BSP, then enable PAE.
	 */
	movl	%cr4, %eax
	orl	CR4OFF(%ebx), %eax
	orl	$CR4_PAE, %eax
	movl	%eax, %cr4

	/*
	 * Point cr3 to the 64-bit long mode page tables.
	 *
	 * Note that these MUST exist in 32-bit space, as we don't have
	 * a way to load %cr3 with a 64-bit base address for the page tables
	 * until the CPU is actually executing in 64-bit long mode.
	 */
	movl	CR3OFF(%ebx), %eax
	movl	%eax, %cr3

	/*
	 * Set long mode enable in EFER (EFER.LME = 1)
	 */
	movl	$MSR_AMD_EFER, %ecx
	rdmsr
	orl	$AMD_EFER_LME, %eax
	wrmsr

	movl	LM64OFF(%ebx), %eax
	pushl	$TEMP_CS64_SEL
	pushl	%eax

	/*
	 * Finally, turn on paging (CR0.PG = 1) to activate long mode.  The
	 * instruction after setting CR0_PG must be a branch, per AMD64 14.6.1.
	 * As when we left 16-bit mode, we have already set up our destination
	 * at (%esp) so that the next instruction can be lretl.
	 */
	movl	%cr0, %eax
	orl	$CR0_PG, %eax
	movl	%eax, %cr0

	lretl

	.globl	long_mode_64
long_mode_64:
	.code64
	/*
	 * We are now running in long mode with a 64-bit CS (EFER.LMA=1,
	 * CS.L=1) so we now have access to 64-bit instructions.
	 *
	 * First, set the 64-bit GDT base.
	 */
	lgdtq	GDTROFF(%rbx)		/* load 64-bit GDT */

	/*
	 * Save the CPU number in %r11; get the value here since it's saved in
	 * the real mode platter.
	 */
	movl	CPUNOFF(%rbx), %r11d

	/*
	 * Now do an lretq to load CS with the appropriate selector for the
	 * kernel's 64-bit GDT and to start executing 64-bit setup code at the
	 * virtual address where boot originally loaded this code rather than
	 * the copy in the real mode platter's rm_code array as we've been
	 * doing so far.
	 */
	pushq	$KCS_SEL
	pushq	$kernel_cs_code
	lretq
	.globl real_mode_start_cpu_end
real_mode_start_cpu_end:
	nop

kernel_cs_code:
	/*
	 * Complete the balance of the setup we need to before executing
	 * 64-bit kernel code (namely init rsp, TSS, LGDT, FS and GS).
	 */
	.globl	rm_platter_va
	movq	rm_platter_va, %rbx
	lidtq	IDTROFF(%rbx)

	movw	$KDS_SEL, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss

	movw	$KTSS_SEL, %ax		/* setup kernel TSS */
	ltr	%ax

	xorw	%ax, %ax		/* clear LDTR */
	lldt	%ax

	/*
	 * Set GS to the address of the per-cpu structure as contained in
	 * cpu[cpu_number].
	 *
	 * We've disabled wrgsbase, so we have to stuff the low 32 bits in
	 * %eax and the high 32 bits in %edx, then call wrmsr.
	 */
	leaq	cpu(%rip), %rdi
	movl	(%rdi, %r11, 8), %eax
	movl	4(%rdi, %r11, 8), %edx
	movl	$MSR_AMD_GSBASE, %ecx
	wrmsr

	/*
	 * Init FS and KernelGSBase.
	 *
	 * Based on code in mlsetup(), set them both to 8G (which shouldn't be
	 * valid until some 64-bit processes run); this will then cause an
	 * exception in any code that tries to index off them before they are
	 * properly setup.
	 */
	xorl	%eax, %eax		/* low 32 bits = 0 */
	movl	$2, %edx		/* high 32 bits = 2 */
	movl	$MSR_AMD_FSBASE, %ecx
	wrmsr

	movl	$MSR_AMD_KGSBASE, %ecx
	wrmsr

	/*
	 * Init %rsp to the exception stack set in tss_ist1 and create a legal
	 * AMD64 ABI stack frame
	 */
	movq	%gs:CPU_TSS, %rax
	movq	TSS_IST1(%rax), %rsp
	pushq	$0		/* null return address */
	pushq	$0		/* null frame pointer terminates stack trace */
	movq	%rsp, %rbp	/* stack aligned on 16-byte boundary */

	movq	%cr0, %rax
	andq    $~(CR0_TS|CR0_EM), %rax	/* clear emulate math chip bit */
	orq     $(CR0_MP|CR0_NE), %rax
	movq    %rax, %cr0		/* set machine status word */

	/*
	 * Before going any further, enable usage of page table NX bit if
	 * that's how our page tables are set up.
	 */
	btl	$X86FSET_NX, x86_featureset(%rip)
	jnc	1f
	movl	$MSR_AMD_EFER, %ecx
	rdmsr
	orl	$AMD_EFER_NXE, %eax
	wrmsr
1:

	/*
	 * Complete the rest of the setup and call mp_startup().
	 */
	movq	%gs:CPU_THREAD, %rax	/* get thread ptr */
	movq	T_PC(%rax), %rax
	INDIRECT_CALL_REG(rax)		/* call mp_startup_boot */
	/* not reached */
	int	$20			/* whoops, returned somehow! */

	SET_SIZE(real_mode_start_cpu)
