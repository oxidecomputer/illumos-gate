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
} psp_c2p_t;

typedef struct psp {
	dev_info_t		*psp_dip;
	kmutex_t		psp_lock;
	psp_c2p_t		psp_c2p;
	psp_versions_t		psp_vers;
} psp_t;

static psp_t psp_data;

typedef struct psp_child_def {
	char		*pcd_node_name;
	psp_child_t	pcd_unit_addr;
} psp_child_def_t;

static const psp_child_def_t psp_children[] = {
	{ "psp_einj", PSP_C_EINJ }
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

static int
psp_c2pmbox_cmd_locked(psp_c2p_t *c2p, cpu2psp_mbox_cmd_t cmd,
    c2p_mbox_buffer_hdr_t *buf)
{
	int ret = 0;
	pfn_t pfn;
	paddr_t buf_pa;
	uint32_t hi, lo;
	uint32_t val;

	VERIFY(MUTEX_HELD(&c2p->c2p_lock));

	/*
	 * The PSP expects a 32-byte aligned physical address to the buffer.
	 */
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)buf);
	VERIFY3U(pfn, !=, PFN_INVALID);
	buf_pa = mmu_ptob(pfn) | ((uintptr_t)buf & PAGEOFFSET);
	VERIFY3B(IS_P2ALIGNED(buf_pa, PSP_C2PMBOX_BUF_ALIGN), ==, B_TRUE);
	lo = (uint32_t)buf_pa;
	hi = (uint32_t)(buf_pa >> 32);

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
		cmn_err(CE_WARN, "?psp: failed to write CPU-to-PSP mailbox "
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

static void
psp_c2pmbox_abort_locked(psp_c2p_t *c2p)
{
	c2p_mbox_buffer_hdr_t buf __aligned(PSP_C2PMBOX_BUF_ALIGN);
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
	c2p_mbox_get_ver_buffer_t buf __aligned(PSP_C2PMBOX_BUF_ALIGN);
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
	bzero(&c2p->c2p_regs, sizeof (c2p->c2p_regs));
	mutex_destroy(&c2p->c2p_lock);
}

static void
psp_c2p_init(psp_c2p_t *c2p)
{
	x86_processor_family_t fam = chiprev_family(cpuid_getchiprev(CPU));

	mutex_init(&c2p->c2p_lock, NULL, MUTEX_DRIVER, NULL);
	c2p->c2p_regs.c2p_cmd = PSP_C2PMBOX(fam);
	c2p->c2p_regs.c2p_addr_lo = PSP_C2PMBOX_BUF_ADDR_LO(fam);
	c2p->c2p_regs.c2p_addr_hi = PSP_C2PMBOX_BUF_ADDR_HI(fam);
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

	psp_c2p_init(&psp->psp_c2p);

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
	psp->psp_dip = NULL;

	mutex_exit(&psp->psp_lock);

	return (DDI_SUCCESS);
}

static int
psp_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    void *arg, void *result)
{
	char buf[32];
	dev_info_t *cdip;
	const psp_child_def_t *pcd;

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == NULL) {
			return (DDI_FAILURE);
		}
		cmn_err(CE_CONT, "psp: %s@%s, %s%d\n",
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    ddi_driver_name(rdip), ddi_get_instance(rdip));
		break;
	case DDI_CTLOPS_INITCHILD:
		cdip = arg;
		if (cdip == NULL) {
			dev_err(dip, CE_WARN, "!no child passed for "
			    "DDI_CTLOPS_INITCHILD");
			return (DDI_FAILURE);
		}

		pcd = ddi_get_parent_data(cdip);
		if (pcd == NULL) {
			dev_err(dip, CE_WARN, "!missing child parent data");
			return (DDI_FAILURE);
		}

		if (snprintf(buf, sizeof (buf), "%u", pcd->pcd_unit_addr) >=
		    sizeof (buf)) {
			dev_err(dip, CE_WARN, "!failed to construct device "
			    "addr due to overflow");
			return (DDI_FAILURE);
		}

		ddi_set_name_addr(cdip, buf);
		break;
	case DDI_CTLOPS_UNINITCHILD:
		cdip = arg;
		if (cdip == NULL) {
			dev_err(dip, CE_WARN, "!no child passed for "
			    "DDI_CTLOPS_UNINITCHILD");
			return (DDI_FAILURE);
		}

		ddi_set_name_addr(cdip, NULL);
		break;
	default:
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}
	return (DDI_SUCCESS);
}

static dev_info_t *
psp_lookup_child(const psp_t *const psp, const psp_child_def_t *const pcd)
{
	dev_info_t *pdip = psp->psp_dip;
	dev_info_t *cdip;

	ASSERT3P(pcd, !=, NULL);

	for (cdip = ddi_get_child(pdip); cdip != NULL;
	    cdip = ddi_get_next_sibling(cdip)) {
		if (ddi_get_parent_data(cdip) == pcd) {
			return (cdip);
		}
	}

	return (NULL);
}

static const psp_child_def_t *
psp_lookup_child_def(const char *devname)
{
	char *devname_dup;
	size_t devname_sz;
	char *cdrv, *caddr;
	unsigned long child_unit_addr;

	devname_dup = i_ddi_strdup(devname, KM_SLEEP);
	devname_sz = strlen(devname_dup) + 1;
	i_ddi_parse_name(devname_dup, &cdrv, &caddr, NULL);

	/*
	 * We have an explicit list of children and thus can further validate
	 * the unit address as returned from i_ddi_parse_name().
	 */
	if (cdrv == NULL || caddr == NULL ||
	    ddi_strtoul(caddr, NULL, 10, &child_unit_addr) != 0 ||
	    child_unit_addr == PSP_C_INVAL || child_unit_addr >= PSP_C_MAX) {
		kmem_free(devname_dup, devname_sz);
		return (NULL);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(psp_children); i++) {
		const psp_child_def_t *const pcd = &psp_children[i];

		if (strcmp(pcd->pcd_node_name, cdrv) == 0 &&
		    pcd->pcd_unit_addr == (psp_child_t)child_unit_addr) {
			kmem_free(devname_dup, devname_sz);
			return (pcd);
		}
	}

	kmem_free(devname_dup, devname_sz);
	return (NULL);
}

static void
psp_config_child(psp_t *psp, const psp_child_def_t *pcd)
{
	dev_info_t *pdip = psp->psp_dip;
	dev_info_t *cdip;

	cdip = psp_lookup_child(psp, pcd);

	/*
	 * If the child device already exists, nothing more to do.
	 */
	if (cdip != NULL)
		return;

	ndi_devi_alloc_sleep(pdip, pcd->pcd_node_name, (pnode_t)DEVI_SID_NODEID,
	    &cdip);
	ddi_set_parent_data(cdip, (void *)pcd);
	(void) ndi_devi_bind_driver(cdip, 0);
}

static void
psp_unconfig_child(psp_t *psp, dev_info_t *cdip)
{
	psp_child_def_t *pcd = ddi_get_parent_data(cdip);

	ASSERT3P(pcd, !=, NULL);
	ddi_set_parent_data(cdip, NULL);

	if (ndi_devi_free(cdip) != NDI_SUCCESS) {
		ddi_set_parent_data(cdip, pcd);
	}
}

static int
psp_bus_config(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
    void *arg, dev_info_t **childp)
{
	psp_t *psp = &psp_data;
	const psp_child_def_t *pcd;

	switch (op) {
	case BUS_CONFIG_ONE:
	case BUS_CONFIG_ALL:
	case BUS_CONFIG_DRIVER:
		ndi_devi_enter(pdip);
		break;
	default:
		return (NDI_FAILURE);
	}

	if (op == BUS_CONFIG_ONE) {
		pcd = psp_lookup_child_def((const char *)arg);
		if (pcd == NULL) {
			ndi_devi_exit(pdip);
			return (NDI_EINVAL);
		}
		psp_config_child(psp, pcd);
	} else {
		for (uint_t i = 0; i < ARRAY_SIZE(psp_children); i++) {
			psp_config_child(psp, &psp_children[i]);
		}
	}

	ndi_devi_exit(pdip);

	flags |= NDI_ONLINE_ATTACH;
	return (ndi_busop_bus_config(pdip, flags, op, arg, childp, 0));
}

static int
psp_bus_unconfig(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
    void *arg)
{
	psp_t *psp = &psp_data;
	const psp_child_def_t *pcd;
	dev_info_t *cdip;
	major_t major;
	int ret;

	switch (op) {
	case BUS_UNCONFIG_ONE:
	case BUS_UNCONFIG_ALL:
	case BUS_UNCONFIG_DRIVER:
		ndi_devi_enter(pdip);
		flags |= NDI_UNCONFIG;
		ret = ndi_busop_bus_unconfig(pdip, flags, op, arg);
		if (ret != NDI_SUCCESS) {
			ndi_devi_exit(pdip);
			return (ret);
		}
		break;
	default:
		return (NDI_FAILURE);
	}

	if (op == BUS_UNCONFIG_ONE) {
		pcd = psp_lookup_child_def((const char *)arg);
		if (pcd == NULL) {
			ndi_devi_exit(pdip);
			return (NDI_EINVAL);
		}

		cdip = psp_lookup_child(psp, pcd);
		if (cdip == NULL) {
			ndi_devi_exit(pdip);
			return (NDI_EINVAL);
		}

		psp_unconfig_child(psp, cdip);
	} else {
		major = (major_t)(uintptr_t)arg;
		for (uint_t i = 0; i < ARRAY_SIZE(psp_children); i++) {
			pcd = &psp_children[i];
			cdip = psp_lookup_child(psp, pcd);
			if (cdip == NULL)
				continue;

			if (op == BUS_UNCONFIG_DRIVER &&
			    (ddi_driver_major(cdip) != major)) {
				continue;
			}

			psp_unconfig_child(psp, cdip);
		}
	}

	ndi_devi_exit(pdip);

	return (NDI_SUCCESS);
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
