/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2025 Oxide Computer Company
 */

/*
 * Certain CPU-to-PSP commands may only be called from System Management Mode
 * (SMM). This module replaces the normal C2P command routine with a version
 * that allows calling such commands.
 */

#include <sys/types.h>
#include <sys/stdalign.h>
#include <sys/stdbool.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/policy.h>
#include <sys/cpuvar.h>
#include <sys/ccompile.h>
#include <sys/smm_amd64.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/psp.h>
#include <sys/ddi_subrdefs.h>

#include "psp_client.h"
#include "psp_fake_smm.h"

/*
 * The size and alignment of the buffer provided to PSP in service of
 * CPU<->PSP communication.
 */
#define	PSP_DATA_SIZE	(8 * 1024)
#define	PSP_DATA_ALIGN	(4 * 1024)

#define	_PSP_BUF_SIZE	(PSP_DATA_SIZE + \
	sizeof (c2p_mbox_buffer_t) + (PSP_C2PMBOX_BUF_ALIGN - 1) + \
	sizeof (uint32_t) + (alignof (uint32_t) - 1))
#define	PSP_BUF_SIZE	P2ROUNDUP(_PSP_BUF_SIZE, AMD64_TSEG_ALIGN)

static ddi_dma_attr_t psp_dma_attrs = {
	.dma_attr_version =	DMA_ATTR_V0,
	.dma_attr_addr_lo =	0,
	.dma_attr_addr_hi =	UINT64_MAX,
	.dma_attr_count_max =	UINT32_MAX,
	.dma_attr_align =	AMD64_TSEG_ALIGN,
	.dma_attr_burstsizes =	0,
	.dma_attr_minxfer =	1,
	.dma_attr_maxxfer =	UINT32_MAX,
	.dma_attr_seg =		UINT32_MAX,
	.dma_attr_sgllen =	1,
	.dma_attr_granular =	1,
	.dma_attr_flags =	0
};

typedef struct psp_fake_smm {
	bool			pfs_enabled;
	kmutex_t		pfs_lock;

	/*
	 * The base of the "SMM" memory region shared with the PSP.
	 */
	void			*pfs_buf;
	/*
	 * The PSP-to-CPU mailbox within `psp_buf`.
	 */
	volatile uint8_t	*pfs_data_buf;
	/*
	 * The CPU-to-PSP mailbox command buffer within `psp_buf`.
	 */
	c2p_mbox_buffer_t	*pfs_cmd_buf;
	/*
	 * Certain CPU-to-PSP commands require being in SMM. The PSP verifies
	 * that by checking this flag (which exists within the "SMM" region;
	 * `psp_buf`) and who's address we provide to the PSP.
	 */
	uint32_t		*pfs_in_smm;

	/*
	 * Dummy "register" for the PSP to check and trigger SMI's.
	 */
	uint32_t		pfs_fake_smi;
} psp_fake_smm_t;

static psp_fake_smm_t psp_fake_smm_data;

int
psp_c_c2pmbox_smm_cmd(cpu2psp_mbox_cmd_t cmd, c2p_mbox_buffer_hdr_t *buf)
{
	psp_fake_smm_t *pfs = &psp_fake_smm_data;
	int ret;

	ASSERT(pfs->pfs_enabled == true);

	/*
	 * Verify the buffer size against our max possible.
	 */
	VERIFY3U(buf->c2pmb_size, <=, sizeof (c2p_mbox_buffer_t));

	mutex_enter(&pfs->pfs_lock);

	bcopy(buf, pfs->pfs_cmd_buf, buf->c2pmb_size);

	*(pfs->pfs_in_smm) = 1;
	ret = psp_c_c2pmbox_cmd(cmd, &pfs->pfs_cmd_buf->c2pmb_hdr);
	*(pfs->pfs_in_smm) = 0;

	mutex_exit(&pfs->pfs_lock);

	return (ret);
}

static void
psp_fake_smm_fini(void)
{
	psp_fake_smm_t *pfs = &psp_fake_smm_data;
	pfs->pfs_enabled = false;
	pfs->pfs_fake_smi = 0;
	pfs->pfs_in_smm = NULL;
	pfs->pfs_cmd_buf = NULL;
	pfs->pfs_data_buf = NULL;
	if (pfs->pfs_buf) {
		contig_free(pfs->pfs_buf, PSP_BUF_SIZE);
		pfs->pfs_buf = NULL;
	}
	mutex_destroy(&pfs->pfs_lock);
}

bool
psp_fake_smm_enable(void)
{
	psp_fake_smm_t *pfs = &psp_fake_smm_data;
	c2p_mbox_smm_info_buffer_t *buf = &pfs->pfs_cmd_buf->c2pmb_smm_info;
	int ret;
	paddr_t cmd_buf_pa, smm_flag_pa, data_buf_pa, fake_smi_pa;
	pfn_t pfn;

	if (pfs->pfs_enabled) {
		return (true);
	}

	bzero(buf, sizeof (*buf));

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)pfs->pfs_cmd_buf);
	VERIFY3U(pfn, !=, PFN_INVALID);
	cmd_buf_pa = mmu_ptob(pfn) | ((uintptr_t)pfs->pfs_cmd_buf & PAGEOFFSET);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)pfs->pfs_in_smm);
	VERIFY3U(pfn, !=, PFN_INVALID);
	smm_flag_pa = mmu_ptob(pfn) | ((uintptr_t)pfs->pfs_in_smm & PAGEOFFSET);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)pfs->pfs_data_buf);
	VERIFY3U(pfn, !=, PFN_INVALID);
	data_buf_pa = mmu_ptob(pfn) |
	    ((uintptr_t)pfs->pfs_data_buf & PAGEOFFSET);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)&pfs->pfs_fake_smi);
	VERIFY3U(pfn, !=, PFN_INVALID);
	fake_smi_pa = mmu_ptob(pfn) |
	    ((uintptr_t)&pfs->pfs_fake_smi & PAGEOFFSET);

	buf->c2pmsib_hdr.c2pmb_size = sizeof (c2p_mbox_smm_info_buffer_t);

	VERIFY3B(IS_P2ALIGNED(data_buf_pa, AMD64_TSEG_ALIGN), ==, B_TRUE);
	VERIFY3B(IS_P2ALIGNED(PSP_BUF_SIZE, AMD64_TSEG_ALIGN), ==, B_TRUE);
	buf->c2pmsib_info.c2pmsi_smm_base = data_buf_pa;
	buf->c2pmsib_info.c2pmsi_smm_mask = AMD64_SMM_MASK_SET_TSEG_MASK(0,
	    PSP_BUF_SIZE - 1);

	buf->c2pmsib_info.c2pmsi_psp_data_base = data_buf_pa;
	buf->c2pmsib_info.c2pmsi_psp_data_len = PSP_DATA_SIZE;

	buf->c2pmsib_info.c2pmsi_mbox_buf_addr = cmd_buf_pa;
	buf->c2pmsib_info.c2pmsi_smm_flag_addr = smm_flag_pa;

	/*
	 * We don't actually want the PSP to trigger any SMI's given we don't
	 * support any PSP-to-CPU commands (not that it should try since we
	 * never set the P2C ready flag). But even if we did, an SMI would not
	 * be the appropriate mechanism to have it signal us in this context.
	 * But alas we can't leave the SMI trigger/register info blank here so
	 * we just provide a dummy "register" that points at some pre-allocated
	 * chunk of memory instead.
	 */

	buf->c2pmsib_info.c2pmsi_trig_info.psti_addr = fake_smi_pa;
	buf->c2pmsib_info.c2pmsi_trig_info.psti_addr_type =
	    PSP_SMM_ADDR_TYPE_MEM;
	buf->c2pmsib_info.c2pmsi_trig_info.psti_width =
	    PSP_SMM_ADDR_WIDTH_DWORD;
	buf->c2pmsib_info.c2pmsi_trig_info.psti_and_mask = (uint32_t)~1;
	buf->c2pmsib_info.c2pmsi_trig_info.psti_or_mask = 1;

	buf->c2pmsib_info.c2pmsi_reg_info.psri_smi_enb.psr_addr = fake_smi_pa;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_smi_enb.psr_addr_type =
	    PSP_SMM_ADDR_TYPE_MEM;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_smi_enb.psr_width =
	    PSP_SMM_ADDR_WIDTH_DWORD;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_smi_enb.psr_and_mask = 1 << 1;

	buf->c2pmsib_info.c2pmsi_reg_info.psri_eos.psr_addr = fake_smi_pa;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_eos.psr_addr_type =
	    PSP_SMM_ADDR_TYPE_MEM;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_eos.psr_width =
	    PSP_SMM_ADDR_WIDTH_DWORD;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_eos.psr_and_mask = 1 << 2;

	buf->c2pmsib_info.c2pmsi_reg_info.psri_fakesmien.psr_addr = fake_smi_pa;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_fakesmien.psr_addr_type =
	    PSP_SMM_ADDR_TYPE_MEM;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_fakesmien.psr_width =
	    PSP_SMM_ADDR_WIDTH_DWORD;
	buf->c2pmsib_info.c2pmsi_reg_info.psri_fakesmien.psr_and_mask = 1 << 3;

	ret = psp_c_c2pmbox_cmd(C2P_MBOX_CMD_SMM_INFO, &buf->c2pmsib_hdr);
	if (ret != 0 || buf->c2pmsib_hdr.c2pmb_status != 0) {
		cmn_err(CE_WARN, "psp_fake_smm: failed to set smm info: %d"
		    " (status = %u)", ret, buf->c2pmsib_hdr.c2pmb_status);
		return (false);
	}

	pfs->pfs_enabled = true;

	return (true);
}

static int
psp_fake_smm_init(void)
{
	psp_fake_smm_t *pfs = &psp_fake_smm_data;
	char *buf;

	switch (chiprev_family(cpuid_getchiprev(CPU))) {
	case X86_PF_AMD_MILAN:
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		break;
	default:
		cmn_err(CE_WARN, "!psp_fake_smm: unsupported processor family");
		return (ENOTSUP);
	}

	mutex_init(&pfs->pfs_lock, NULL, MUTEX_DRIVER, NULL);

	/*
	 * Any buffers shared with the PSP are expected to be in physically
	 * contiguous memory, so we go ahead and allocate them once here.
	 */
	pfs->pfs_buf = buf = contig_alloc(PSP_BUF_SIZE, &psp_dma_attrs,
	    PSP_BUF_SIZE, 1);
	bzero(buf, PSP_BUF_SIZE);

	/*
	 * The PSP-to-CPU (P2C) mailbox and buffer come first.
	 */
	VERIFY3B(IS_P2ALIGNED(PSP_BUF_SIZE, PSP_DATA_ALIGN), ==, B_TRUE);
	pfs->pfs_data_buf = (volatile uint8_t *)buf;
	buf += PSP_DATA_SIZE;
	/*
	 * The commmand buffer requires a modest 32-byte alignment.
	 */
	pfs->pfs_cmd_buf = (c2p_mbox_buffer_t *)P2ROUNDUP((uintptr_t)buf,
	    PSP_C2PMBOX_BUF_ALIGN);
	buf = (char *)pfs->pfs_cmd_buf + sizeof (c2p_mbox_buffer_t);
	/*
	 * Finally, the SMM flag at its natural alignment.
	 */
	pfs->pfs_in_smm = (uint32_t *)P2ROUNDUP((uintptr_t)buf,
	    alignof (uint32_t));

	return (0);
}

static struct modlmisc psp_fake_smm_modlmisc = {
	.misc_modops = &mod_miscops,
	.misc_linkinfo = "AMD PSP Fake SMM Command Provider",
};

static struct modlinkage psp_fake_smm_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &psp_fake_smm_modlmisc, NULL }
};

int
_init(void)
{
	int ret;

	if ((ret = psp_fake_smm_init()) != 0) {
		return (ret);
	}

	if ((ret = mod_install(&psp_fake_smm_modlinkage)) != 0) {
		psp_fake_smm_fini();
	}

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&psp_fake_smm_modlinkage, modinfop));
}

int
_fini(void)
{
	psp_fake_smm_t *pfs = &psp_fake_smm_data;
	int ret;

	/*
	 * We don't unload once we've successfully initialized because the call
	 * to the PSP indicating SMM info is one-way. Any subsequent calls to
	 * the mailbox must be made via psp_c_c2pmbox_smm_cmd().
	 */
	if (pfs->pfs_enabled) {
		return (EBUSY);
	}

	if ((ret = mod_remove(&psp_fake_smm_modlinkage)) == 0) {
		psp_fake_smm_fini();
	}

	return (ret);
}
