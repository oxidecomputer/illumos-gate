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
 * Copyright 2026 Oxide Computer Company
 */

/*
 * A device driver that provides access to the AMD Platform Security Processor
 * (PSP/MP0), also known as the AMD Secure Processor (ASP/MPASP).
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/stdalign.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/policy.h>
#include <sys/cpuvar.h>
#include <sys/ccompile.h>
#include <sys/ddi_subrdefs.h>
#include <sys/ndi_static.h>
#include <sys/smm_amd64.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/psp.h>
#include <amdzen_client.h>

#include "psp.h"
#include "psp_client.h"

/*
 * The number of ticks we delay while waiting for the mailbox to be ready to
 * process new commands or while waiting for command completion.
 */
const uint_t psp_retry_delay = 10;
/*
 * The number of attempts we make at checking if the PSP is ready to accept new
 * commands or completed processing the last submitted request.
 */
const uint_t psp_retry_attempts = 100;

/*
 * This is used to mediate and synchronize access to the CPU-to-PSP mailbox.
 */
typedef struct psp_c2p {
	kmutex_t		c2p_lock;
	struct {
		smn_reg_t	c2p_cmd;
		smn_reg_t	c2p_addr_lo;
		smn_reg_t	c2p_addr_hi;
	}			c2p_regs;
	c2p_mbox_buffer_t	*c2p_buf;
} psp_c2p_t;

static ddi_dma_attr_t psp_c2p_dma_attrs = {
	.dma_attr_version =	DMA_ATTR_V0,
	.dma_attr_addr_lo =	0,
	.dma_attr_addr_hi =	UINT64_MAX,
	.dma_attr_count_max =	UINT32_MAX,
	.dma_attr_align =	PSP_C2PMBOX_BUF_ALIGN,
	.dma_attr_burstsizes =	0,
	.dma_attr_minxfer =	1,
	.dma_attr_maxxfer =	UINT32_MAX,
	.dma_attr_seg =		UINT32_MAX,
	.dma_attr_sgllen =	1,
	.dma_attr_granular =	1,
	.dma_attr_flags =	0
};

typedef struct psp {
	dev_info_t		*psp_dip;
	kmutex_t		psp_lock;
	psp_c2p_t		psp_c2p;
	psp_versions_t		psp_vers;
	ndis_t			*psp_ndis;
} psp_t;

static psp_t psp_data;

/*
 * The children of this nexus. Each is addressed by its psp_child_t value.
 */
static const ndis_child_def_t psp_dpe_def = {
	.ncd_nodename = "psp_dpe",
	.ncd_unit_addr = PSP_C_DPE
};

static const ndis_child_def_t psp_einj_def = {
	.ncd_nodename = "psp_einj",
	.ncd_unit_addr = PSP_C_EINJ
};

static const ndis_child_def_t *const psp_children[] = {
	&psp_dpe_def,
	&psp_einj_def
};

/*
 * Reads the CPU-to-PSP mailbox status register and returns its readiness.
 * Note this requires (& verifies) that the caller holds the `psp_lock`.
 */
static int
psp_c2pmbox_ready_locked(psp_c2p_t *c2p, uint32_t *valp)
{
	int ret;
	uint32_t val;

	VERIFY(MUTEX_HELD(&c2p->c2p_lock));

	ret = amdzen_c_smn_read(0, c2p->c2p_regs.c2p_cmd, &val);
	if (ret != 0) {
		cmn_err(CE_WARN, "psp: failed to read CPU-to-PSP mailbox "
		    "command reg: %d", ret);
		return (ret);
	}

	*valp = val;

	if (PSP_C2PMBOX_GET_RECOVERY(val)) {
		cmn_err(CE_WARN, "!psp: CPU-to-PSP mailbox not ready: "
		    "recovery needed");
		return (EINVAL);
	}

	/*
	 * The ready flag indicates if the PSP is ready to service commands.
	 */
	if (!PSP_C2PMBOX_GET_READY(val)) {
		return (EBUSY);
	}

	return (0);
}

static void psp_c2pmbox_abort_locked(psp_c2p_t *c2p);

/*
 * Submit a command to the PSP via the CPU-to-PSP mailbox.  The provided buffer
 * must already be in physically contiguous memory with the required alignment.
 * This is the low-level submission path; most callers should use
 * psp_c2pmbox_cmd_locked() which handles copying to a safe buffer.
 */
static int
psp_c2pmbox_submit_locked(psp_c2p_t *c2p, cpu2psp_mbox_cmd_t cmd,
    c2p_mbox_buffer_hdr_t *buf)
{
	int ret = 0;
	pfn_t pfn;
	paddr_t buf_pa;
	uint32_t hi, lo;
	uint32_t val;

	VERIFY(MUTEX_HELD(&c2p->c2p_lock));

	/*
	 * For non-abort commands let's make sure the PSP is in a ready state.
	 */
	if (cmd != C2P_MBOX_CMD_ABORT) {
		for (uint_t i = 0; i < psp_retry_attempts; i++) {
			if ((ret = psp_c2pmbox_ready_locked(c2p, &val)) == 0)
				break;
			if (ret != EBUSY)
				goto out;
			delay(psp_retry_delay);
		}
		if (ret == EBUSY) {
			cmn_err(CE_WARN, "psp: timed out while waiting for "
			    "CPU-to-PSP mailbox to indicate ready; trying to "
			    "issue abort...");

			psp_c2pmbox_abort_locked(c2p);

			/* Check if ready again one more time after abort */
			if ((ret = psp_c2pmbox_ready_locked(c2p, &val)) != 0) {
				cmn_err(CE_WARN, "psp: CPU-to-PSP mailbox "
				    "still not ready after abort: %d", ret);
				ret = ETIMEDOUT;
				goto out;
			}
		}
	}

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)buf);
	VERIFY3U(pfn, !=, PFN_INVALID);
	buf_pa = mmu_ptob(pfn) | ((uintptr_t)buf & PAGEOFFSET);
	VERIFY3B(IS_P2ALIGNED(buf_pa, PSP_C2PMBOX_BUF_ALIGN), ==, B_TRUE);
	lo = (uint32_t)buf_pa;
	hi = (uint32_t)(buf_pa >> 32);

	/*
	 * PSP is ready (or we're issuing an abort), let's go ahead and write
	 * the buffer address first.
	 */
	if ((ret = amdzen_c_smn_write(0, c2p->c2p_regs.c2p_addr_hi, hi)) != 0 ||
	    (ret = amdzen_c_smn_write(0, c2p->c2p_regs.c2p_addr_lo, lo)) != 0) {
		cmn_err(CE_WARN, "psp: failed to write CPU-to-PSP mailbox "
		    "buffer physical address (%lx): %d", buf_pa, ret);
		goto out;
	}

	/*
	 * We're ready to kick things off by setting the requested Command ID.
	 */
	val = PSP_C2PMBOX_SET_CMD_ID(val, cmd);
	val = PSP_C2PMBOX_CLEAR_READY(val);
	if ((ret = amdzen_c_smn_write(0, c2p->c2p_regs.c2p_cmd, val)) != 0) {
		cmn_err(CE_WARN, "psp: failed to write CPU-to-PSP mailbox "
		    "command reg: %d (0x%x)", ret, val);
		goto out;
	}

	/*
	 * Wait for the PSP to finish processing the command by polling on the
	 * ready flag.
	 */
	for (uint_t i = 0; i < psp_retry_attempts; i++) {
		ret = amdzen_c_smn_read(0, c2p->c2p_regs.c2p_cmd, &val);
		if (ret != 0) {
			cmn_err(CE_WARN, "psp: failed to poll CPU-to-PSP "
			    "mailbox command reg: %d", ret);
			goto out;
		}
		if (PSP_C2PMBOX_GET_READY(val))
			break;
		delay(psp_retry_delay);
	}
	if (!PSP_C2PMBOX_GET_READY(val)) {
		cmn_err(CE_WARN, "psp: timed out while waiting for CPU-to-PSP "
		    "command to complete processing (%d)", cmd);

		if (cmd != C2P_MBOX_CMD_ABORT) {
			/*
			 * We won't retry the command but let's at least try to
			 * get things unstuck.
			 */
			psp_c2pmbox_abort_locked(c2p);
			return (ETIMEDOUT);
		}
	}

	/*
	 * At this point the command was submitted successfully. Copy over the
	 * command result status to the provided buffer for the caller.
	 */
	buf->c2pmb_status = PSP_C2PMBOX_GET_STATUS(val);

out:
	return (ret);
}

/*
 * Submit a command to the PSP via the CPU-to-PSP mailbox.  The PSP expects
 * the buffer to reside in physically contiguous memory; to avoid requiring
 * callers to guarantee this, we copy into a preallocated contiguous buffer
 * before submission and copy the response back afterwards on success.
 */
static int
psp_c2pmbox_cmd_locked(psp_c2p_t *c2p, cpu2psp_mbox_cmd_t cmd,
    c2p_mbox_buffer_hdr_t *buf)
{
	int ret;

	VERIFY(MUTEX_HELD(&c2p->c2p_lock));

	if (buf->c2pmb_size > sizeof (*c2p->c2p_buf)) {
		return (EINVAL);
	}

	bcopy(buf, c2p->c2p_buf, buf->c2pmb_size);

	ret = psp_c2pmbox_submit_locked(c2p, cmd, &c2p->c2p_buf->c2pmb_hdr);

	if (ret == 0) {
		bcopy(c2p->c2p_buf, buf, buf->c2pmb_size);
	}

	return (ret);
}

static int
psp_c2pmbox_cmd(psp_c2p_t *c2p, cpu2psp_mbox_cmd_t cmd,
    c2p_mbox_buffer_hdr_t *buf)
{
	int ret;

	mutex_enter(&c2p->c2p_lock);
	ret = psp_c2pmbox_cmd_locked(c2p, cmd, buf);
	mutex_exit(&c2p->c2p_lock);
	return (ret);
}

int
psp_c_c2pmbox_cmd(cpu2psp_mbox_cmd_t cmd, c2p_mbox_buffer_hdr_t *buf)
{
	return (psp_c2pmbox_cmd(&psp_data.psp_c2p, cmd, buf));
}

/*
 * Submit a command to the PSP using a caller-provided buffer that is already
 * in physically contiguous memory.  Unlike psp_c_c2pmbox_cmd() no intermediate
 * copy is performed; the buffer is passed directly to the PSP.  This is
 * required for SMM commands where the PSP expects the buffer at a specific
 * pre-registered physical address.
 */
int
psp_c_c2pmbox_submit(cpu2psp_mbox_cmd_t cmd, c2p_mbox_buffer_hdr_t *buf)
{
	psp_c2p_t *c2p = &psp_data.psp_c2p;
	int ret;

	VERIFY3B(IS_P2ALIGNED(buf, PSP_C2PMBOX_BUF_ALIGN), ==, B_TRUE);

	mutex_enter(&c2p->c2p_lock);
	ret = psp_c2pmbox_submit_locked(c2p, cmd, buf);
	mutex_exit(&c2p->c2p_lock);
	return (ret);
}

static void
psp_c2pmbox_abort_locked(psp_c2p_t *c2p)
{
	c2p_mbox_buffer_hdr_t buf;
	int ret;

	VERIFY(MUTEX_HELD(&c2p->c2p_lock));

	/* Abort doesn't have any command specific data we need to provide. */
	bzero(&buf, sizeof (c2p_mbox_buffer_hdr_t));
	buf.c2pmb_size = sizeof (c2p_mbox_buffer_hdr_t);
	ret = psp_c2pmbox_cmd_locked(c2p, C2P_MBOX_CMD_ABORT, &buf);
	if (ret != 0 || buf.c2pmb_status != 0) {
		cmn_err(CE_WARN, "psp: failed to abort CPU-to-PSP command: %d "
		    "(status = %u)", ret, buf.c2pmb_status);
	}
}

static int
psp_c2pmbox_get_versions(psp_c2p_t *c2p, psp_fw_versions_t *vers)
{
	c2p_mbox_get_ver_buffer_t buf;
	int ret;

	bzero(&buf, sizeof (c2p_mbox_get_ver_buffer_t));
	buf.c2pmgvb_hdr.c2pmb_size = sizeof (c2p_mbox_get_ver_buffer_t);
	ret = psp_c2pmbox_cmd(c2p, C2P_MBOX_CMD_GET_VER, &buf.c2pmgvb_hdr);
	if (ret != 0 || buf.c2pmgvb_hdr.c2pmb_status != 0) {
		cmn_err(CE_WARN, "psp: failed to get version info: %d"
		    " (status = %u)", ret, buf.c2pmgvb_hdr.c2pmb_status);
		if (ret == 0)
			ret = buf.c2pmgvb_hdr.c2pmb_status;
		goto out;
	}
	bcopy(&buf.c2pmgvb_vers, vers, sizeof (*vers));
out:
	return (ret);
}

static void
psp_c2p_fini(psp_c2p_t *c2p)
{
	if (c2p->c2p_buf) {
		contig_free(c2p->c2p_buf, sizeof (c2p_mbox_buffer_t));
		c2p->c2p_buf = NULL;
	}
	bzero(&c2p->c2p_regs, sizeof (c2p->c2p_regs));
	mutex_destroy(&c2p->c2p_lock);
}

static int
psp_c2p_init(psp_c2p_t *c2p)
{
	x86_processor_family_t fam = chiprev_family(cpuid_getchiprev(CPU));

	mutex_init(&c2p->c2p_lock, NULL, MUTEX_DRIVER, NULL);
	c2p->c2p_regs.c2p_cmd = PSP_C2PMBOX(fam);
	c2p->c2p_regs.c2p_addr_lo = PSP_C2PMBOX_BUF_ADDR_LO(fam);
	c2p->c2p_regs.c2p_addr_hi = PSP_C2PMBOX_BUF_ADDR_HI(fam);

	c2p->c2p_buf = contig_alloc(sizeof (c2p_mbox_buffer_t),
	    &psp_c2p_dma_attrs, PSP_C2PMBOX_BUF_ALIGN, 1);
	if (c2p->c2p_buf == NULL)
		return (ENOMEM);
	bzero(c2p->c2p_buf, sizeof (c2p_mbox_buffer_t));

	return (0);
}

static void
psp_fini(void)
{
	psp_t *psp = &psp_data;

	bzero(&psp->psp_vers, sizeof (psp_versions_t));
	psp_c2p_fini(&psp->psp_c2p);
	mutex_destroy(&psp->psp_lock);
}

static int
psp_init(void)
{
	psp_t *psp = &psp_data;
	psp_fw_versions_t vers = { 0 };
	bool swap_psp_ver;
	int ret;

	mutex_init(&psp->psp_lock, NULL, MUTEX_DRIVER, NULL);

	switch (chiprev_family(cpuid_getchiprev(CPU))) {
	case X86_PF_AMD_MILAN:
		/* FALLTHROUGH */
	case X86_PF_AMD_GENOA:
		swap_psp_ver = false;
		break;
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		swap_psp_ver = true;
		break;
	default:
		cmn_err(CE_WARN, "!psp: unsupported processor family");
		ret = ENOTSUP;
		goto err;
	}

	if ((ret = psp_c2p_init(&psp->psp_c2p)) != 0)
		goto err;

	/*
	 * Ask the PSP for the running FW versions. This also serves as a test
	 * for if we can even access the mailbox. If the "BIOS Exit" command was
	 * already sent (i.e., by any BIOS/UEFI firmware before us) then any
	 * subsequent commands must come from SMM space (whose details would've
	 * been provided to the PSP with a previous command as well).
	 */
	ret = psp_c2pmbox_get_versions(&psp->psp_c2p, &vers);
	if (ret != 0) {
		cmn_err(CE_WARN, "psp: failed to get FW versions: %d", ret);
		goto err;
	}

	/*
	 * We expose the FW versions to userspace but in a slightly different
	 * form compared to how we get them from the PSP. Namely, there are some
	 * vestigal fields we don't want to expose and to normalize the endian-
	 * ness of the PSP firmware which differs between generations. We apply
	 * that transformation here to later provide to userspace on request.
	 */
	CTASSERT(sizeof (vers.pfv_psp) == sizeof (psp->psp_vers.pv_psp));
	bcopy(vers.pfv_psp, psp->psp_vers.pv_psp, sizeof (vers.pfv_psp));
	CTASSERT(sizeof (vers.pfv_agesa) == sizeof (psp->psp_vers.pv_agesa));
	bcopy(vers.pfv_agesa, psp->psp_vers.pv_agesa, sizeof (vers.pfv_agesa));
	CTASSERT(sizeof (vers.pfv_smu) == sizeof (psp->psp_vers.pv_smu));
	bcopy(vers.pfv_smu, psp->psp_vers.pv_smu, sizeof (vers.pfv_smu));

	if (swap_psp_ver) {
		CTASSERT(sizeof (psp->psp_vers.pv_psp) == sizeof (uint32_t));
		*((uint32_t *)&psp->psp_vers.pv_psp[0]) =
		    ddi_swap32(*((uint32_t *)&vers.pfv_psp[0]));
	}


#define	PSP_FW_VER_FORMAT_ARGS(fw)	fw[0], fw[1], fw[2], fw[3]

	cmn_err(CE_CONT, "?psp: FW Versions:\n"
	    "\tPSP:   0x%02x.0x%02x.0x%02x.0x%02x\n"
	    "\tAGESA: 0x%02x.0x%02x.0x%02x.0x%02x\n"
	    "\tSMU:   0x%02x.0x%02x.0x%02x.0x%02x\n",
	    PSP_FW_VER_FORMAT_ARGS(psp->psp_vers.pv_psp),
	    PSP_FW_VER_FORMAT_ARGS(psp->psp_vers.pv_agesa),
	    PSP_FW_VER_FORMAT_ARGS(psp->psp_vers.pv_smu));

#undef PSP_FW_VER_FORMAT_ARGS

	return (0);

err:
	psp_fini();
	return (ret);
}

static int
psp_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	psp_t *psp = &psp_data;
	minor_t m;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		m = getminor((dev_t)arg);
		if (m != PSP_MINOR_NUM) {
			return (DDI_FAILURE);
		}
		*resultp = (void *)psp->psp_dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		m = getminor((dev_t)arg);
		if (m != PSP_MINOR_NUM) {
			return (DDI_FAILURE);
		}
		*resultp = (void *)(uintptr_t)ddi_get_instance(psp->psp_dip);
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
psp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	psp_t *psp = &psp_data;

	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	mutex_enter(&psp->psp_lock);

	if (psp->psp_dip != NULL) {
		dev_err(dip, CE_WARN, "!psp is already attached to a "
		    "dev_info_t: %p", psp->psp_dip);
		mutex_exit(&psp->psp_lock);
		return (DDI_FAILURE);
	}

	if (ddi_create_minor_node(dip, PSP_MINOR_NAME, S_IFCHR,
	    PSP_MINOR_NUM, DDI_PSEUDO, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!failed to create minor node %s",
		    PSP_MINOR_NAME);
		mutex_exit(&psp->psp_lock);
		return (DDI_FAILURE);
	}

	psp->psp_dip = dip;
	psp->psp_ndis = ndis_init(dip, "psp", NDIS_UA_DEC, psp_children,
	    ARRAY_SIZE(psp_children), NULL, psp, 0);

	mutex_exit(&psp->psp_lock);

	return (DDI_SUCCESS);
}

static int
psp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	psp_t *psp = &psp_data;

	if (cmd == DDI_SUSPEND) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	mutex_enter(&psp->psp_lock);

	if (psp->psp_dip != dip) {
		dev_err(dip, CE_WARN,
		    "!asked to detach psp, but dip doesn't match");
		mutex_exit(&psp->psp_lock);
		return (DDI_FAILURE);
	}

	(void) ddi_remove_minor_node(psp->psp_dip, NULL);
	ndis_fini(psp->psp_ndis);
	psp->psp_ndis = NULL;
	psp->psp_dip = NULL;

	mutex_exit(&psp->psp_lock);

	return (DDI_SUCCESS);
}

static int
psp_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    void *arg, void *result)
{
	return (ndis_bus_ctl(psp_data.psp_ndis, rdip, ctlop, arg, result));
}

static int
psp_bus_config(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
    void *arg, dev_info_t **childp)
{
	return (ndis_bus_config(psp_data.psp_ndis, flags, op, arg, childp));
}

static int
psp_bus_unconfig(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
    void *arg)
{
	return (ndis_bus_unconfig(psp_data.psp_ndis, flags, op, arg));
}

static int
psp_open(dev_t *devp, int flags, int otype, cred_t *credp)
{
	/*
	 * We gate on drv_priv() for open but specific ioctl's may enforce
	 * stronger privileges.
	 */
	if (crgetzoneid(credp) != GLOBAL_ZONEID || drv_priv(credp) != 0) {
		return (EPERM);
	}

	if ((flags & (FEXCL | FNDELAY | FNONBLOCK)) != 0) {
		return (EINVAL);
	}

	if (otype != OTYP_CHR) {
		return (EINVAL);
	}

	if (getminor(*devp) != PSP_MINOR_NUM) {
		return (ENXIO);
	}

	return (0);
}

static int
psp_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	psp_t *psp = &psp_data;

	if (getminor(dev) != PSP_MINOR_NUM) {
		return (ENXIO);
	}

	if (crgetzoneid(credp) != GLOBAL_ZONEID) {
		return (EPERM);
	}

	switch (cmd) {
	case PSP_IOC_GET_VERS:
		/*
		 * Require only read and no further secpolicy than required in
		 * psp_open() to get the versions.
		 */
		if ((mode & FREAD) != FREAD) {
			return (EBADF);
		}

		if (ddi_copyout(&psp->psp_vers, (void *)arg,
		    sizeof (psp->psp_vers), mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		break;
	default:
		return (ENOTTY);
	}

	return (0);
}

static int
psp_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}

	if (getminor(dev) != PSP_MINOR_NUM) {
		return (ENXIO);
	}

	return (0);
}

struct bus_ops psp_bus_ops = {
	.busops_rev = BUSO_REV,
	.bus_dma_map = ddi_no_dma_map,
	.bus_dma_allochdl = ddi_no_dma_allochdl,
	.bus_dma_freehdl = ddi_no_dma_freehdl,
	.bus_dma_bindhdl = ddi_no_dma_bindhdl,
	.bus_dma_unbindhdl = ddi_no_dma_unbindhdl,
	.bus_dma_flush = ddi_no_dma_flush,
	.bus_dma_win = ddi_no_dma_win,
	.bus_dma_ctl = ddi_no_dma_mctl,
	.bus_prop_op = ddi_bus_prop_op,
	.bus_ctl = psp_bus_ctl,
	.bus_config = psp_bus_config,
	.bus_unconfig = psp_bus_unconfig,
};

static struct cb_ops psp_cb_ops = {
	.cb_open = psp_open,
	.cb_close = psp_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = psp_ioctl,
	.cb_devmap = nodev,
	.cb_mmap = nodev,
	.cb_segmap = nodev,
	.cb_chpoll = nochpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_flag = D_MP,
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev
};

static struct dev_ops psp_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = psp_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = psp_attach,
	.devo_detach = psp_detach,
	.devo_reset = nodev,
	.devo_bus_ops = &psp_bus_ops,
	.devo_cb_ops = &psp_cb_ops,
	.devo_quiesce = ddi_quiesce_not_needed,
};

static struct modldrv psp_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "AMD Platform Security Processor (PSP) Nexus Driver",
	.drv_dev_ops = &psp_dev_ops
};

static struct modlinkage psp_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &psp_modldrv, NULL }
};

int
_init(void)
{
	int ret;

	if ((ret = psp_init()) != 0) {
		return (ret);
	}

	if ((ret = mod_install(&psp_modlinkage)) != 0) {
		psp_fini();
	}

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&psp_modlinkage, modinfop));
}

int
_fini(void)
{
	int ret;

	if ((ret = mod_remove(&psp_modlinkage)) == 0) {
		psp_fini();
	}

	return (ret);
}
