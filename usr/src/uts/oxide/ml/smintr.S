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
 * Copyright 2022 Oxide Computer Co.
 */

#include <sys/apic.h>
#include <sys/asm_linkage.h>
#include <sys/asm_misc.h>
#include <sys/x86_archext.h>
#include <sys/smm_amd64.h>
#include <sys/smm.h>
#include <sys/segments.h>
#include <sys/controlregs.h>
#include <sys/io/fch/smi.h>
#include "assym.h"

/*
 * This is not a place of honour.  No highly esteemed deed is commemorated
 * here.  Nothing valued is here.  Quite the opposite, in fact: this is here
 * only because a feature (SMM) that we neither want nor use is extremely
 * hazardous and there is no way to disable it.
 *
 * This is the SMI handler.  There is one copy of this for each CPU, placed at
 * SMBASE + 0x8000.  They are all absolutely identical and there is no good
 * reason for this replication, but the architecture defines a region
 * containing a handler and state save area for each CPU and there is no way
 * to dissociate them.  Therefore if we were to assign the same SMBASE to
 * every CPU and more than one entered SMM at the same time, the state save
 * area would be trashed.  There is no known way to synchronise this because
 * the state save area is written by hardware.  That's not necessarily a
 * problem (we can still panic just fine), except that we really need to
 * understand how we got here so we can make sure it never happens again. So
 * we waste the customer's valuable resources (DRAM) and replicate this
 * handler 128 times (or more!) all to detect the abuse of a feature that we
 * neither need nor use.
 *
 * The first thing to understand, and the one thing to keep in mind always, is
 * that we have but a single mission here: to induce a panic.  Under no
 * circumstances should an SMI ever occur.  No exceptions whatsoever.  We
 * either leave disabled or explicitly disable every known source of SMIs.
 * Arriving here means that one of three things has gone horribly wrong:
 *
 * 1. We've encountered a hardware bug allowing an SMI to be generated when
 * none was configured.
 *
 * 2. There is an undocumented, enabled by default, SMI source that we don't
 * know how to disable, and its trigger condition somehow occurred.
 *
 * 3. A serious kernel bug, either on its own or with help from a malicious
 * actor, has caused one or more sources of SMIs to be enabled and triggered.
 *
 * Regardless of how we got here, if no one else has yet done so we want to
 * copy the state save area to the kernel's buffer along with a few MSRs and
 * bits from the FCH's SMI status registers, generate an NMI, and then resume.
 * If someone else has already initiated these actions we will simply resume.
 * The NMI handler will look at the flags in the kernel's SMM state save
 * buffer and note that the NMI was induced by the SMI handler to generate an
 * appropriate panic message or enter the debugger.
 *
 * There are two interrelated choices we have to make here.  First, how much
 * useful processor state do we want to build up?  We start out in something
 * very much like real mode which is very nearly useless.  Do we want to enter
 * big flat mode?  Do we want to continue to 64-bit mode, presumably using the
 * kernel's %cr3?  Doing so allows us to run C code and take advantage of
 * existing kernel functions.  That also opens the possibility of incorrectly
 * relying upon some kernel state that may have been damaged and thereby
 * failing to perform our sole duty: panicking.  The second choice is whether
 * and how to do I/O.  Ideally we will not need to do any I/O at all: we can
 * generate the NMI we need by relying on the fact that this machine
 * architecture mandates X2APIC mode, so we need only wrmsr and then rsm.  The
 * tough part is making sure another SMI is not immediately asserted, because
 * if that occurs it will be taken before the NMI and we will never make
 * progress toward our singular objective.  If we do need to do I/O, we have a
 * few choices but they're all bad; the most likely registers we'd need to
 * access would be in the FCH, but we might also need to access the IOHC.  In
 * either case, our sole reason for doing so would be to cause deassertion of
 * whatever caused the SMI, a task complicated by the fact that there are so
 * many possible sources of them and to the extent that we know what those
 * sources are we have *already disabled them*.  In any event, our I/O options
 * are:
 *
 * * Pretty much everything is accessible via SMN.  If we want to enter 64-bit
 * mode, we can use the kernel's genoa_smn_{read,write}32() functions.  These
 * rely on PCI config space access having been set up, which isn't a problem
 * since this handler isn't installed until after that's done.  This approach
 * also, obviously, relies on the kernel's pagetables and executing kernel
 * code.
 *
 * * An alternative means of accessing config space to set up SMN transactions
 * is to use the legacy I/O space indirect register pair at 0xcf8/0xcfc.  The
 * catch is that although we don't currently do so, AMD recommends disabling
 * this access method once ECAM is set up.  Still, this is probably our best
 * bet, because...
 *
 * * If we don't want to rely on the kernel's pagetables and PCI config space
 * access, we can stop at big flat mode.  But ECS is going to be hoisted above
 * the 4 GiB boundary to save PCIe MMIO space, which would preclude accessing
 * it from big flat mode.  Worse, AMD's implementation of ECAM requires the
 * use of the UC access mode, which means that without paging enabled the
 * region to be accessed would have to be covered by a UC MTRR which we don't
 * currently (and would not otherwise) set up.
 *
 * * We don't have to use SMN to access the FCH::SMI block; it's also
 * available via MMIO, and the physical addresses of everything we need are
 * below the 4 GiB boundary.  To do this, we need to make sure our accesses
 * are UC; the easiest way to do this is to set CR0.CD.  This doesn't help us
 * if we need to access the IOHC (which is SMN-only unless we set up the
 * FastRegs aperture), but our current implementation doesn't.
 *
 * We'll start by assuming that we don't need to do any I/O.  The FCH's SMI
 * mechanism seems to be gated on two control fields:
 * FCH::SMI::SMITRIG0[smienb] (the closest thing to a master bit that we have)
 * and FCH::SMI::SMITRIG0[eos] which is cleared by HW when an FCH-driven SMI
 * event occurs and intended to be set by SW to re-enable FCH SMIs.  Leaving
 * aside the obvious question of how smienb came to be set in the first place
 * -- we certainly never set it and in fact clear it explicitly -- it seems
 * reasonable to expect that if we got here through that path the eos bit will
 * be clear and will not be set again unless we set it.  This would have the
 * effect of blocking further SMIs, which is exactly what we want.  We'll save
 * the contents of a few of these registers but leave them unmodified.
 *
 * That leaves non-FCH generated SMIs.  These are also referred to as local
 * sources and include MCA (supposed to happen only if PFEH is enabled, which
 * we never do), I/O traps (ditto), and APIC sources set up for the SMI
 * delivery mode (you guessed it, we never do).  We've got a couple of choices
 * here too, and these are more interesting:
 *
 * * We can configure Core::X86::Msr::SmiTrigIoCycle to forward the event to
 * the FCH.  This is how PCs set things up, which promotes any local SMI to a
 * broadcast SMI.  The PPR tells us that if we don't do this, the DF generates
 * a local SMI.  What's not clear is whether that still happens if we do it;
 * i.e., does setting this up force all SMI generation through the FCH?  If
 * so, that's desirable for two reasons: first, they'd then be gated on the
 * master enable bits there and presumably would never be generated; second,
 * it means we would only ever need to worry about one path for deasserting
 * subsequent SMIs.  Probably.
 *
 * * We can leave SmiTrigIoCycle disabled and deal with any local sources by
 * reading and writing MSRs, which of course does not require any I/O.
 *
 * For now we'll try the second approach, assuming once again that the
 * documentation is reasonably complete and correct and we shouldn't need to
 * do anything to prevent an SMI from recurring.  This is a bit dicier because
 * there's no clear documentation of what's needed to deassert MCA-originated
 * SMI sources.  Obviously, the fact that we never enable any of the
 * functionality that would enable those sources in the first place makes it
 * much more difficult to be confident that we've acknowledged and deasserted
 * them.  Perhaps the best we can do is to check the local status bits in the
 * state save area and if we see that the source is an MCA deferred error or
 * redirected interrupt we can disable those bits in MSRs before resuming.
 *
 * Empirically, we find that even the most straightforward "local" source, a
 * directed IPI with delivery mode set to SMI, ends up being broadcast to all
 * processors.  Whether this also happens for PFEH MCA and redirected
 * interrupts and locally-defined legacy I/O space trapping is anyone's guess.
 *
 * The last collection of sources is the IOHC RAS controller.  There is a
 * mechanism here for software to generate an SMI, and for PCIe root
 * complexes, NBIFs, and potentially other downstream entities to do so as
 * well (by now you should not need to be told that these are all disabled).
 * The interrupts generated by this mechanism seem to be sent directly to the
 * APICs, by default to the broadcast recipient 0xFF.  The NMI EOI bit used
 * here will prevent subsequent NMIs being generated by this source until set
 * by software, and we will assume that the SMI analogue works the same way
 * and all we need to do to prevent subsequent SMIs from this source is to
 * avoid writing the EOI bit.
 *
 * With that in mind, we can eschew setting up 64-bit mode, and in principle
 * could even remain in 16-bit mode if we wished.  Big flat mode is an easier
 * and more pleasant programming environment, however, and it's simple to set
 * up.  That'll be our compromise position unless and until we learn that we
 * need something more.
 *
 * Our initial state and the state-save area are documented in AMD64 4.03 vol.
 * 2 chapter 10 and the Milan B1 PPR 2.1.19.1.  Our implementation of the
 * state-save area and SMM-related core registers can be found in
 * uts/intel/sys/smm_amd64.h.  A discussion of that state will not be
 * replicated here as those sources are adequate.
 *
 * Rules of the road:
 *
 * One of our constraints is similar to one we have during boot (see
 * mpcore.s): because we do not guarantee that SMBASE lies in the first MiB of
 * physical memory, we are not allowed to use %cs either implicitly or
 * explicitly until we have set up the GDT.  In other words, %csbase is once
 * again magic but we are not allowed to use the selector value in %cs as the
 * target of a far jump, call, or return.  The architecture does guarantee
 * that %csbase has been set to SMBASE and the limit to 0xFFFF_FFFF.  This
 * means that, much like during boot, we may access memory using %cs even
 * though we cannot call through it.  It also means that, like during boot, we
 * cannot copy %cs into other segment registers and then use them to access
 * data; the magic in %csbase won't follow the selector.
 *
 * It should go without saying that if we were on the fence about setting up
 * big flat mode before, this should get us off it.
 *
 * Also unlike boot, the general-purpose registers are in an undefined state.
 * It's important not to assume anything about what they contain.
 *
 * The other thing to keep in mind is that we have only 512 bytes for the
 * handler.  The SMBASE value is incremented by 1024 bytes for each CPU, so
 * our overall memory layout looks like this:
 *
 * +---------------------+ <-- BASE + (N - 1) * 0x400 + 0x8000
 * | CPU N-1 state-save  |
 * +---------------------+ <-- BASE + (N - 1) * 0x400 + 0x7E00
 * |       unused        |
 * +---------------------+ <-- BASE + (N - 2) * 0x400 + 0x8000
 * | CPU N-2 state-save  |
 * +---------------------+ <-- BASE + (N - 2) * 0x400 + 0x7E00
 * |         ...         |
 * +---------------------+ <-- BASE + (N - 31) * 0x400 + 0x8000
 * | CPU N-31 state-save |
 * +---------------------+ <-- BASE + (N - 31) * 0x400 + 0x7E00
 * |       unused        |
 * +---------------------+ <-- BASE + (N - 32) * 0x400 + 0x8000
 * | CPU N-32 state-save |
 * +---------------------+ <-- BASE + (N - 32) * 0x400 + 0x7E00
 * |   CPU N-1 handler   |
 * +---------------------+ <-- BASE + (N - 1) * 0x400
 * |         ...         | ...
 * +---------------------+ <-- BASE + 0x8400
 * |   CPU 1 state-save  |
 * +---------------------+ <-- BASE + 0x8200
 * |    CPU 32 handler   |
 * +---------------------+ <-- BASE + 0x8000
 * |   CPU 0 state-save  |
 * +---------------------+ <-- BASE + 0x7E00
 * |    CPU 31 handler   |
 * +---------------------+ <-- BASE + 0x7C00
 * |         ...         | ...
 * +---------------------+ <-- BASE + 0x600
 * |    CPU 1 handler    |
 * +---------------------+ <-- BASE + 0x400
 * |       unused        |
 * +---------------------+ <-- BASE + 0x200
 * |    CPU 0 handler    |
 * +---------------------+ <-- BASE, beginning of TSeg, CPU32 SMBASE
 * | available to kernel |
 * +---------------------+ <-- BASE - 0x8000, CPU0 SMBASE
 *
 * We can thus compute each address as follows:
 *
 * CPU n SMBASE = BASE + (n * 0x400) - 0x8000
 * CPU n handler = BASE + (n * 0x400)
 * CPU n state-save = BASE + (n * 0x400) + 0x7E00
 * end of TSeg for N CPUs = BASE + (N - 1) * 0x400 + 0x7FFF
 *
 * The size of TSeg is rounded up to the next 128 KiB because the base and
 * limit registers are defined that way, so 128 processors require a 256 KiB
 * TSeg, 256 require a 384 KiB TSeg, etc.  This also means that if one wishes
 * to use ASeg (a 128 KiB space), the largest number of processors that can be
 * accommodated would be 96.  When SMT is enabled, each thread has its own
 * SMBASE and counts as a processor for this purpose.  Because we generally
 * support systems that already exist with more than 96 processors, we always
 * allocate and use TSeg and leave ASeg disabled.  All of this is set up in
 * os/smm.c.
 *
 * The space immediately below TSeg is ordinary memory that can be used by the
 * kernel for any purpose; although it lies above the first 31 CPUs' SMBASE it
 * is not part of TSeg nor is it part of any handler or state-save area.
 *
 * It is reasonable to ask whether we need to put the synchronisation
 * variables used by the handler inside TSeg, so that the handler need never
 * read from memory that can be modified outside SMM.  In a legacy
 * architecture, the trust model is that firmware (which includes the SMI
 * handlers and all the code they invoke) is trusted and software (including
 * the privileged kernel) is not.  That is not our model -- we don't have
 * firmware at all -- so let's consider our threat model here.  Since the sole
 * purpose of our SMI handler is to cause a panic, it is reasonable to
 * conclude that the undesirable outcome a malicious actor could cause is that
 * we do not panic.  That could be achieved by altering the memory used to
 * store the synchronisation variables.  However, anyone who can do that can
 * also replace the contents of the NMI handler, remove that handler
 * altogether, reprogram the APIC, or any of dozens of other things that would
 * prevent us from panicking.  That such an attacker can trigger SMIs is
 * merely an annoyance and perhaps a rather baroque avenue to a denial of
 * service.  The thing we must prevent is modification of any of the handlers,
 * whether by kernel or user code or by handlers acting as confused deputies.
 * The machine is already thoroughly compromised if someone can alter the
 * synchronisation objects and/or trigger SMIs; all we're trying to accomplish
 * here is prevent the malicious actor from getting a place to hide malicious
 * code.  Since the addresses of the synchronisation objects and kernel
 * state-save buffer are embedded inside TSeg at the time we set up the
 * handlers (prior to running any user software), there isn't any way for an
 * attacker to modify that data that will open the door to modifying the
 * handlers or cause them to execute code outside TSeg.  The bottom line is
 * that we're accomplishing only two things here:
 *
 * 1. Preventing an attacker who has already compromised the machine totally
 * from hiding code in ASeg or TSeg and running it invisibly.
 *
 * 2. Panicking if an SMI occurs because of a hardware or software defect;
 * i.e., in the absence of a successful attack by someone who wishes to
 * prevent the panic.
 *
 * With that in mind, we don't need to worry very much about where the
 * synchronisation objects reside; they're just integers accessed atomically
 * and never used to compute the target address of any load, store, or control
 * transfer instruction.  Keep it that way!
 */

#define	STORE_MSR64(_msr, _base, _member)	\
	movl	$(_msr), %ecx;			\
	rdmsr;					\
	movl	%eax, (_member)(_base);		\
	movl	%edx, (_member + 4)(_base)

#define	STORE_MMIO32(_mmiobase, _mmiooff, _base, _member)	\
	movl	(_mmiooff)(_mmiobase), %eax;			\
	movl	%eax, (_member)(_base)

	.code16
	.globl smintr
	ENTRY_NP(smintr)

	/*
	 * Clear all registers and relevant flags.  IF and TF are
	 * architecturally guaranteed to be clear already, and we do not rely
	 * on the arithmetic flags without first setting them.  Register usage
	 * note: we set up %bx/%ebx at each step so that it points to the
	 * smm_handler_t in whatever addressing mode we are using.  This makes
	 * use of the assym offsets convenient.
	 */
	cld
	xorl	%eax, %eax
	movl	$AMD64_SMBASE_HANDLER_OFF, %ebx
	movl	$MSR_AMD_SMBASE, %ecx
	movl	%eax, %edx
	movl	%eax, %esi
	movl	%eax, %edi
	movl	%eax, %esp
	movl	%eax, %ebp

	lidtl	%cs:SMH_IDT_LIM(%bx)
	lgdtl	%cs:SMH_GDT_LIM(%bx)

	leaw	(SMH_SCRATCH - 2)(%bx), %bp	/* Avoid %sp's implicit %ss */

	/*
	 * Get the handler's absolute address from the SmmBase MSR.  The upper
	 * 32 bits are always clear; the PA is in %eax.  We set the MSR number
	 * in %ecx above.
	 */
	rdmsr
	/* %edx = smm_pe32 PA */
	leal	(smm_pe32 - smintr + AMD64_SMBASE_HANDLER_OFF)(%eax), %edx

	movw	$TEMP_CS32_SEL, %cs:(%bp)
	subw	$4, %bp				/* Not pushl; must use %cs */
	movl	%edx, %cs:(%bp)

	/*
	 * Now we're going to enter protected mode and jump into a 32-bit
	 * code segment.  Set up %ebx for use there; it's just SMBASE plus the
	 * handler offset.
	 */
	addl	%eax, %ebx

	/*
	 * Per AMD64 4.03 vol. 2 table 10-3, we are guaranteed that %cr4 and
	 * EFER are 0.  We are not guaranteed a value of %cr0, only that PE,
	 * EM, TS, and PG are clear.  That's sufficient for safe operation,
	 * but we don't have any reason to want to preserve what was already
	 * there for our own use (it will be restored via rsm at the end
	 * regardless).  Replace whatever is there with exactly what we want
	 * as we enter protected mode.  Note that we are also architecturally
	 * guaranteed that we can make cacheable accesses to DRAM without
	 * having to invalidate upon entry or exit (ibid.  10.3.4); therefore
	 * we don't -- presently -- need CD.
	 */
	movl	$(CR0_PE | CR0_ET), %eax
	movl	%eax, %cr0

	ljmpl	*%cs:(%bp)			/* Not lretl; must use %cs */

smm_pe32:
	.code32

	/*
	 * We are now in protected mode in a 32-bit code segment.  First set up
	 * our data segments; we don't have a stack so we don't set up %ss.
	 * Debugging is pretty much impossible from here without setting up a
	 * whole second kernel (a la UEFI) so the idea of getting a backtrace
	 * is wishful thinking.  We set %es here so we can use movsl later.
	 */
	movw	$TEMP_DS32_SEL, %ax
	movw	%ax, %ds
	movw	%ax, %es

	/*
	 * We are now ready to go to work.  We have access to the address of
	 * the kernel's SMM status buffer for this CPU in our smh_ksmm member,
	 * which we'll stash in %ebp.  For the duration, we access members of
	 * ksmm_t via offsets from %ebp.
	 */
	movl	SMH_KSMMPA(%ebx), %ebp

	movl	$1, %eax
	xaddl	%eax, KSMM_NSMI(%ebp);
	cmpl	$0, %eax
	jne	1f

	/*
	 * Copy the state save area into the kernel's buffer.
	 */
	leal	KSMM_STATE_SAVE(%ebp), %edi
	leal	(AMD64_SMBASE_SS_OFF - AMD64_SMBASE_HANDLER_OFF)(%ebx), %esi
	movl	$(AMD64_SMM_STATE_SIZE >> 2), %ecx
	rep movsl

	/*
	 * CPU-local SMI source and related useful information.
	 */
	STORE_MSR64(MSR_AMD_SMI_IO_TRAP_0, %ebp, KSMM_MSR_SMI_IO_TRAP_0)
	STORE_MSR64(MSR_AMD_SMI_IO_TRAP_1, %ebp, KSMM_MSR_SMI_IO_TRAP_1)
	STORE_MSR64(MSR_AMD_SMI_IO_TRAP_2, %ebp, KSMM_MSR_SMI_IO_TRAP_2)
	STORE_MSR64(MSR_AMD_SMI_IO_TRAP_3, %ebp, KSMM_MSR_SMI_IO_TRAP_3)
	STORE_MSR64(MSR_AMD_SMI_IO_TRAP_CTL, %ebp, KSMM_MSR_SMI_IO_TRAP_CTL)
	STORE_MSR64(MSR_AMD_PFEH_CFG, %ebp, KSMM_MSR_PFEH_CFG)
	STORE_MSR64(MSR_AMD_PFEH_CLOAK_CFG, %ebp, KSMM_MSR_PFEH_CLOAK_CFG)
	STORE_MSR64(MSR_AMD_PFEH_DEF_INT, %ebp, KSMM_MSR_PFEH_DEF_INT)

	/*
	 * FCH SMI state.  We have to use either MMIO or SMN to access, and
	 * MMIO is much simpler.  We use the big hammer of CD here because we
	 * don't have any other reasonable way of guaranteeing UC access to
	 * this MMIO region.  The alternative would be to set up an MTRR.
	 */
	movl	$(FCH_SMI_PHYS_BASE), %edi
	movl	$(CR0_PE | CR0_ET | CR0_CD), %eax
	movl	%eax, %cr0

	STORE_MMIO32(%edi, FCH_SMI_REGOFF_EVENTSTATUS, %ebp, KSMM_SMI_EVENT_STATUS)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_CAPT_DATA, %ebp, KSMM_SMI_CAPT_DATA)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_CAPT_VALID, %ebp, KSMM_SMI_CAPT_VALID)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_STATUS0, %ebp, KSMM_SMI_STATUS_0)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_STATUS1, %ebp, KSMM_SMI_STATUS_1)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_STATUS2, %ebp, KSMM_SMI_STATUS_2)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_STATUS3, %ebp, KSMM_SMI_STATUS_3)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_STATUS4, %ebp, KSMM_SMI_STATUS_4)
	STORE_MMIO32(%edi, FCH_SMI_REGOFF_SMITRIG0, %ebp, KSMM_SMI_TRIG_0)

	movl	$(CR0_PE | CR0_ET), %eax
	movl	%eax, %cr0

	/*
	 * We want all previous stores to be visible before the valid flag
	 * becomes visible on another CPU.  We don't need another sfence (or
	 * mfence) afterward.  This allows the NMI to be reordered in front of
	 * the ksmm_valid store's global visibility (AMD64 4.03 vol. 2
	 * 16.11.2), but since we are sending the NMI to the same CPU we're
	 * currently running on, it's guaranteed to have visibility when we
	 * run the NMI handler regardless.  The only way this could then be a
	 * problem is if another CPU were already in the NMI handler for a
	 * reason other than an SMI; in that case it may fail to note that an
	 * SMI occurred in the resulting panic message, though the ksmm state
	 * would still be saved.  There is nothing we can do about that here;
	 * an mfence or other serialising instruction wouldn't help.
	 */
	sfence
	movl	$1, KSMM_VALID(%ebp)

	/*
	 * Trigger an NMI, which will be held pending until we exit SMM.  This
	 * handler is not installed until we have enabled x2APIC mode, so if
	 * we're not in x2APIC mode something has already gone too far off the
	 * rails to recover.
	 */
	movl	$(REG_X2APIC_BASE_MSR + (APIC_LID_REG >> 2)), %ecx
	rdmsr
	movl	$(REG_X2APIC_BASE_MSR + (APIC_INT_CMD1 >> 2)), %ecx
	movl	%eax, %edx
	movl	$AV_NMI, %eax
	wrmsr

1:
	rsm

	SET_SIZE(smintr)
	.globl smintr_end
smintr_end:
