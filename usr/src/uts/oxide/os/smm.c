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
 * Copyright 2024 Oxide Computer Co.
 */

#include <sys/limits.h>
#include <sys/x86_archext.h>
#include <sys/sysmacros.h>
#include <sys/mach_mmu.h>
#include <sys/smm.h>
#include <sys/smm_amd64.h>
#include <sys/ddidmareq.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_subrdefs.h>
#include <sys/controlregs.h>
#include <sys/archsystm.h>
#include <sys/prom_debug.h>
#include <sys/bitmap.h>
#include <vm/hat_i86.h>
#include <vm/as.h>

extern void smintr(void);
extern void smintr_end(void);

static uint32_t tseg_pa;
static uint32_t tseg_len;
static ksmm_t *ksmmp;

static inline uint32_t
smbase(processorid_t p)
{
	VERIFY3U(tseg_pa, >=, AMD64_SMBASE_HANDLER_OFF);
	VERIFY3U(UINT32_MAX - tseg_pa + 1, >=, tseg_len);
	VERIFY3U(p * SMBASE_CPU_STRIDE, <,
	    tseg_len - AMD64_SMBASE_SS_OFF - sizeof (amd64_smm_state_t) +
	    AMD64_SMBASE_HANDLER_OFF);

	return (tseg_pa - AMD64_SMBASE_HANDLER_OFF + p * SMBASE_CPU_STRIDE);
}

static inline smm_handler_t *
smh(void *tseg, processorid_t p)
{
	return ((smm_handler_t *)&(((uint8_t *)tseg)[p * SMBASE_CPU_STRIDE]));
}

int
smm_init(void)
{
	smm_handler_t *smhp, *smh_protop;
	void *tseg;
	size_t code_len = (size_t)smintr_end - (size_t)smintr;

	/*
	 * This code as well as that in smintr.s depends on these invariants.
	 * If these fail, it is likely that the definitions in smm.h have
	 * changed incorrectly.
	 */
#define	CTASSERT_COMPACT(_ma, _mb)	\
	CTASSERT(offsetof(smm_handler_t, _mb) == \
	    offsetof(smm_handler_t, _ma) + \
	    sizeof (((smm_handler_t *)(NULL))->_ma))
	CTASSERT(sizeof (smm_handler_t) ==
	    (SMBASE_CPU_STRIDE - sizeof (amd64_smm_state_t)));
	CTASSERT(offsetof(smm_handler_t, smh_code) == 0);
	CTASSERT_COMPACT(smh_idt_lim, smh_idt_base);
	CTASSERT_COMPACT(smh_gdt_lim, smh_gdt_base);
#undef	CTASSERT_COMPACT

	VERIFY3U(code_len, <=, SMH_CODE_SIZE);

	/*
	 * This architecture does not support CPU hotplug, so we will never have
	 * more than max_ncpus to worry about.  Size TSeg on this basis.  We
	 * need enough space for all the handlers plus enough space to account
	 * for the waste between the last handler and its corresponding
	 * state-save area.  TSeg must be a multiple of 128 KiB and due to the
	 * way the mask register works must also be naturally aligned.
	 */
	tseg_len = max_ncpus * SMBASE_CPU_STRIDE + AMD64_SMBASE_SS_OFF +
	    sizeof (amd64_smm_state_t) - AMD64_SMBASE_HANDLER_OFF;
	tseg_len = P2ROUNDUP(tseg_len, AMD64_TSEG_ALIGN);
	if (tseg_len > AMD64_TSEG_ALIGN) {
		int bit = highbit(tseg_len - 1);

		/*
		 * The highest bit set must be at least bit 16, since we've
		 * already rounded up to 128 KiB, we know we're *above* 128 KiB,
		 * and therefore the smallest value we could have here is
		 * 0x3_ffff.  If bit 17 is the high bit set, highbit() returns
		 * 18.
		 */
		VERIFY3S(bit, >, 17);

		tseg_len = 1U << bit;
	}

	/*
	 * We can't put a negative value into SMBASE and we want to set SMBASE
	 * for CPU0 so that the handler is at the beginning of TSeg.  Therefore
	 * we can't accept an allocation in the bottom 32 KiB of RAM, which
	 * given alignment requirements means the bottom 128 KiB.  Note that if
	 * TSeg is larger than 128 KiB, it will end up being higher due to its
	 * alignment.
	 */
	ddi_dma_attr_t tseg_attr = {
		.dma_attr_version = DMA_ATTR_VERSION,
		.dma_attr_addr_lo = AMD64_TSEG_ALIGN,
		.dma_attr_addr_hi = UINT32_MAX,
		.dma_attr_count_max = UINT32_MAX,
		.dma_attr_align = tseg_len,
		.dma_attr_minxfer = 1,
		.dma_attr_maxxfer = tseg_len,
		.dma_attr_seg = UINT32_MAX,
		.dma_attr_sgllen = 1,
		.dma_attr_granular = 1,
		.dma_attr_flags = 0
	};

	ddi_dma_attr_t ksmm_attr = {
		.dma_attr_version = DMA_ATTR_VERSION,
		.dma_attr_addr_lo = 0,
		.dma_attr_addr_hi = UINT32_MAX,
		.dma_attr_count_max = UINT32_MAX,
		.dma_attr_align = 8,
		.dma_attr_minxfer = 1,
		.dma_attr_maxxfer = sizeof (ksmm_t),
		.dma_attr_seg = UINT32_MAX,
		.dma_attr_sgllen = 1,
		.dma_attr_granular = 1,
		.dma_attr_flags = 0
	};

	/* XXX Even though we set cansleep, can this fail in other ways? */
	tseg = contig_alloc(tseg_len, &tseg_attr, tseg_len, 1);
	if (tseg == NULL)
		return (-1);
	tseg_pa = mmu_ptob(hat_getpfnum(kas.a_hat, (caddr_t)tseg));

	ksmmp = contig_alloc(max_ncpus * sizeof (ksmm_t), &ksmm_attr, 8, 1);
	if (ksmmp == NULL) {
		tseg_pa = 0;
		contig_free(tseg, tseg_len);
		return (-1);
	}

	/*
	 * We zero out TSeg to make it easier to interpret the eventual contents
	 * of the state save area: the processor does not write to the entire
	 * block, only to valid fields.  We also zero out our own data
	 * structures, except for smh_code where we fill the unused space with
	 * hlt (0xf4) instructions that should never be executed.
	 */
	bzero(tseg, tseg_len);
	bzero(ksmmp, max_ncpus * sizeof (ksmm_t));

	smh_protop = kmem_zalloc(sizeof (smm_handler_t), KM_SLEEP);

	bcopy((caddr_t)smintr, smh_protop->smh_code, code_len);
	(void) memset(smh_protop->smh_code + code_len, 0xf4,
	    SMH_CODE_SIZE - code_len);

	smh_protop->smh_ksmmpa =
	    mmu_ptob(hat_getpfnum(kas.a_hat, (caddr_t)ksmmp));

	PRM_DEBUG(tseg_pa);
	PRM_DEBUG(smh_protop->smh_ksmmpa);

	/*
	 * These are the same descriptors used in the RMP, except that we don't
	 * have a 64-bit one because the SMH doesn't run 64-bit code.
	 */
	smh_protop->smh_gdt[0] = 0;
	smh_protop->smh_gdt[TEMPGDT_KCODE64] = 0;
	smh_protop->smh_gdt[TEMPGDT_KCODE32] = 0xcf9a000000ffffULL;
	smh_protop->smh_gdt[TEMPGDT_KDATA32] = 0xcf93000000ffffULL;
	smh_protop->smh_idt_lim = 0;
	smh_protop->smh_idt_base = 0;

	for (processorid_t p = 0; p < max_ncpus; p++) {
		smhp = smh(tseg, p);

		bcopy(smh_protop, smhp, sizeof (smm_handler_t));

		smhp->smh_gdt_base = smbase(p) + AMD64_SMBASE_HANDLER_OFF +
		    (uint32_t)offsetof(smm_handler_t, smh_gdt);
		smhp->smh_gdt_lim = (uint16_t)(sizeof (smhp->smh_gdt) - 1);

		/*
		 * Adjust the ksmm_t address to reflect this CPU's entry.
		 */
		smhp->smh_ksmmpa += p * sizeof (ksmm_t);
	}

	kmem_free(smh_protop, sizeof (smm_handler_t));

	return (0);
}

void
smm_install_handler(void)
{
	uint64_t tseg_mask;
	uint64_t smm_mask = 0;
	uint64_t hwcr;

	if (tseg_pa == 0) {
		cmn_err(CE_NOTE, "TSeg is not available; no SMI handler "
		    "installed for CPU %d", CPU->cpu_id);
		return;
	}

	ASSERT(IS_P2ALIGNED(tseg_pa, AMD64_TSEG_ALIGN));
	ASSERT3U(tseg_len, !=, 0);
	ASSERT(IS_P2ALIGNED(tseg_len, AMD64_TSEG_ALIGN));

	hwcr = rdmsr(MSR_AMD_HWCR);
	if (AMD64_HWCR_GET_SMM_LOCK(hwcr) != 0) {
		cmn_err(CE_WARN, "SMM_LOCK is already set on CPU %d; no SMI "
		    "handler installed", CPU->cpu_id);
		return;
	}

	tseg_mask = ((UINT64_MAX - tseg_len + 1) >> 17) & 0x7FFFFFFFUL;
	smm_mask = AMD64_SMM_MASK_SET_TSEG_MASK(smm_mask, tseg_mask);
	smm_mask = AMD64_SMM_MASK_SET_T_MTYPE_DRAM(smm_mask,
	    AMD64_SMM_MASK_MTYPE_DRAM_WB);
	smm_mask = AMD64_SMM_MASK_SET_T_VALID(smm_mask, 1);

	wrmsr(MSR_AMD_SMM_ADDR, (uint64_t)tseg_pa);
	wrmsr(MSR_AMD_SMM_MASK, smm_mask);
	wrmsr(MSR_AMD_SMBASE, smbase(CPU->cpu_id));

	hwcr = AMD64_HWCR_SET_SMM_BASE_LOCK(hwcr, 1);
	/*
	 * The PPR says we're supposed to disable these special cycles, but it
	 * doesn't say what the special cycles do or what decodes them.  There
	 * is no obvious difference between having them on or off.
	 */
	hwcr = AMD64_HWCR_SET_RSM_SPCYC_DIS(hwcr, 1);
	hwcr = AMD64_HWCR_SET_SMI_SPCYC_DIS(hwcr, 1);
	hwcr = AMD64_HWCR_SET_SMM_LOCK(hwcr);

	wrmsr(MSR_AMD_HWCR, hwcr);
}

/*
 * Poll all CPUs for valid SMM state.  Any CPU that has experienced at least one
 * SMI will get a chance to finish updating its state.  If any CPU took an SMI,
 * we take a second lap to make sure no one else entered SMM while we were
 * looking around.  Finally, we return B_TRUE iff at least one CPU took an SMI.
 * We can't wait forever; it's possible that some CPU (or even many) didn't take
 * an SMI for one reason or another: it may have been a local SMI, there may
 * have been a hardware error, etc.  AMD's guidance is to always try to force
 * all CPUs into SMM and wait for them forever *in SMM*.  That results in an
 * undebuggable hard hang if anything goes wrong.  This way we may lose data
 * about what caused the SMI, but we're also guaranteed to finish -- and get on
 * with the panic -- in less than a second.
 */
boolean_t
smm_check_nmi(void)
{
	uint32_t wait_usec;
	uint32_t seen_smi = 0;

	if (tseg_pa == 0 || ksmmp == NULL)
		return (-1);

	for (processorid_t p = 0; p < max_ncpus; p++) {
		if (ksmmp[p].ksmm_nsmi == 0)
			continue;

		++seen_smi;

		for (wait_usec = 5000;
		    wait_usec > 0 && ksmmp->ksmm_valid != 0; wait_usec--) {
			drv_usecwait(1);
			membar_consumer();
		}
	}

	if (seen_smi == 0)
		return (B_FALSE);

	if (seen_smi == max_ncpus)
		return (B_TRUE);

	for (processorid_t p = 0; p < max_ncpus; p++) {
		if (ksmmp[p].ksmm_nsmi != 0)
			continue;

		for (wait_usec = 5000;
		    wait_usec > 0 && ksmmp->ksmm_nsmi == 0; wait_usec--) {
			drv_usecwait(1);
			membar_consumer();
		}
	}

	return (B_TRUE);
}
