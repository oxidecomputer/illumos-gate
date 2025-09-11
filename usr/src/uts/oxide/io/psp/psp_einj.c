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
 * A device driver that provides access to the error injection capabilities
 * provided by the PSP.
 */

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/stdbit.h>
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
#include <sys/machsystm.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/psp.h>
#include <amdzen_client.h>

#include "psp_client.h"
#include "psp_fake_smm.h"
#include "psp_einj.h"

/*
 * In addition to the RAS mailbox register, there's a region of memory used to
 * provide the necessary arguments to inject errors and check its status.
 * Unfortunately the size of this region isn't documented but we do have at
 * least one strong hint based on the size of the `ReservedRasEinj (0xF)`
 * entry in the APOB provided system memory map holes. On Turin, the base
 * address of that entry matches the address we read via the RAS mailbox command
 * PSP_RAS_MBOX_CMD_BUF_ADDR[0-3].
 */
#define	PSP_RAS_COMMAND_BUF_SZ	0x100000
CTASSERT(sizeof (psp_ras_command_buffer_t) <= PSP_RAS_COMMAND_BUF_SZ);

typedef struct psp_einj {
	dev_info_t		*pe_dip;
	kmutex_t		pe_lock;
	x86_processor_family_t	pe_fam;
	smn_reg_t		pe_ras_reg;
	/*
	 * Region of memory provided by the PSP for passing error injection
	 * parameters and checking their status.
	 */
	volatile psp_ras_command_buffer_t	*pe_ras_cmd_buf;
} psp_einj_t;

static psp_einj_t psp_einj_data;

static void
psp_einj_fini(void)
{
	psp_einj_t *pe = &psp_einj_data;

	if (pe->pe_ras_cmd_buf) {
		hat_unload(kas.a_hat, (caddr_t)pe->pe_ras_cmd_buf,
		    PSP_RAS_COMMAND_BUF_SZ, HAT_UNLOAD_UNLOCK);
		device_arena_free((void *)pe->pe_ras_cmd_buf,
		    PSP_RAS_COMMAND_BUF_SZ);
		pe->pe_ras_cmd_buf = NULL;
	}
	bzero(&pe->pe_ras_reg, sizeof (pe->pe_ras_reg));
	pe->pe_fam = X86_PF_UNKNOWN;
	mutex_destroy(&pe->pe_lock);
}

static int
psp_einj_init(void)
{
	psp_einj_t *pe = &psp_einj_data;
	int ret;

	mutex_init(&pe->pe_lock, NULL, MUTEX_DRIVER, NULL);

	pe->pe_fam = chiprev_family(cpuid_getchiprev(CPU));
	switch (pe->pe_fam) {
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		break;
	default:
		cmn_err(CE_WARN, "!psp_einj: unsupported processor family");
		ret = ENOTSUP;
		goto err;
	}

	pe->pe_ras_reg = PSP_RAS_MBOX(pe->pe_fam);

	return (0);

err:
	psp_einj_fini();
	return (ret);
}

static int
psp_einj_ras_cmd(psp_einj_t *pe, psp_ras_mbox_cmd_t cmd,
    uint16_t *status_or_data, uint8_t *alt_status)
{
	int ret;
	uint32_t val;

	VERIFY(MUTEX_HELD(&pe->pe_lock));

	val = PSP_RAS_MBOX_CLEAR_DONE(0);
	val = PSP_RAS_MBOX_SET_CMD_ID(val, cmd);
	if (cmd == PSP_RAS_MBOX_CMD_DIRECT) {
		VERIFY3P(status_or_data, !=, NULL);
		val = PSP_RAS_MBOX_SET_DATA(val, *status_or_data);
	}
	ret = amdzen_c_smn_write(0, pe->pe_ras_reg, val);
	if (ret != 0) {
		dev_err(pe->pe_dip, CE_WARN, "failed to write PSP RAS "
		    "mailbox reg: %d", ret);
		return (ret);
	}

	for (uint_t i = 0; i < psp_retry_attempts; i++) {
		ret = amdzen_c_smn_read(0, pe->pe_ras_reg, &val);
		if (ret != 0) {
			dev_err(pe->pe_dip, CE_WARN, "failed to poll PSP RAS "
			    "mailbox reg: %d", ret);
			return (ret);
		}
		if (PSP_RAS_MBOX_GET_DONE(val))
			break;
		delay(psp_retry_delay);
	}
	if (!PSP_RAS_MBOX_GET_DONE(val)) {
		dev_err(pe->pe_dip, CE_WARN, "timed out while waiting for "
		    "PSP to complete processing RAS command (%d)", cmd);
		return (ETIMEDOUT);
	}

	if (status_or_data != NULL)
		*status_or_data = PSP_RAS_MBOX_GET_STATUS(val);
	if (alt_status != NULL)
		*alt_status = PSP_RAS_MBOX_GET_ALT_STATUS(val);

	return (0);
}

static int
psp_einj_ras_cmd_direct(psp_einj_t *pe, uint16_t data, uint8_t *status)
{
	return (psp_einj_ras_cmd(pe, PSP_RAS_MBOX_CMD_DIRECT, &data, status));
}

static bool
psp_einj_ras_cmd_buf(psp_einj_t *pe, paddr_t *cmd_buf_pa)
{
	paddr_t buf_pa = 0;
	int ret;

	VERIFY(MUTEX_HELD(&pe->pe_lock));

	for (psp_ras_mbox_cmd_t cmd = PSP_RAS_MBOX_CMD_BUF_ADDR0;
	    cmd <= PSP_RAS_MBOX_CMD_BUF_ADDR3; cmd++) {
		uint16_t bits;
		uint8_t status;
		ret = psp_einj_ras_cmd(pe, cmd, &bits, &status);
		if (ret != 0 || status != 0) {
			dev_err(pe->pe_dip, CE_WARN, "failed to get PSP RAS "
			    "command buffer address: %d (status = %u)", ret,
			    status);
			return (false);
		}
		buf_pa |= (paddr_t)bits << (16 * cmd);
	}
	VERIFY3B(IS_P2ALIGNED(buf_pa, MMU_PAGESIZE), ==, B_TRUE);
	VERIFY3B(IS_P2ALIGNED(PSP_RAS_COMMAND_BUF_SZ, MMU_PAGESIZE), ==,
	    B_TRUE);

	*cmd_buf_pa = buf_pa;

	return (true);
}

static bool
psp_einj_enable_ras_mbox(psp_einj_t *pe)
{
	c2p_mbox_ras_einj_buffer_t einj_buf;
	int ret;
	uint32_t val;

	VERIFY(MUTEX_HELD(&pe->pe_lock));

	/*
	 * Don't need to do anything if the RAS mailbox register isn't all-1s.
	 */
	if (amdzen_c_smn_read(0, pe->pe_ras_reg, &val) == 0 &&
	    val != (uint32_t)-1) {
		return (true);
	}

	bzero(&einj_buf, sizeof (c2p_mbox_ras_einj_buffer_t));
	einj_buf.c2pmreb_hdr.c2pmb_size = sizeof (c2p_mbox_ras_einj_buffer_t);
	einj_buf.c2pmreb_action = PSP_ACPI_RAS_EINJ_ENABLE;
	ret = psp_c_c2pmbox_smm_cmd(C2P_MBOX_CMD_ACPI_RAS_EINJ,
	    &einj_buf.c2pmreb_hdr);
	if (ret != 0 || einj_buf.c2pmreb_hdr.c2pmb_status != 0) {
		dev_err(pe->pe_dip, CE_WARN, "failed to enable RAS EINJ: %d "
		    "(status = %u)", ret, einj_buf.c2pmreb_hdr.c2pmb_status);
		return (false);
	}

	return (true);
}

static bool
psp_einj_enable(psp_einj_t *pe)
{
	paddr_t cmd_buf_pa;

	VERIFY(MUTEX_HELD(&pe->pe_lock));

	/*
	 * We need to first enable the RAS mailbox.
	 */
	if (!psp_einj_enable_ras_mbox(pe))
		return (false);

	/*
	 * We can now send RAS mailbox commands; the first of which will be
	 * querying for the address of the RAS command buffer.
	 */
	if (!psp_einj_ras_cmd_buf(pe, &cmd_buf_pa))
		return (false);

	pe->pe_ras_cmd_buf = (volatile psp_ras_command_buffer_t *)
	    device_arena_alloc(PSP_RAS_COMMAND_BUF_SZ, VM_SLEEP);
	hat_devload(kas.a_hat, (caddr_t)pe->pe_ras_cmd_buf,
	    PSP_RAS_COMMAND_BUF_SZ, mmu_btop(cmd_buf_pa),
	    PROT_READ | PROT_WRITE, HAT_STRICTORDER | HAT_LOAD_LOCK);

	/*
	 * We should be able to access the buffer now and can start off by
	 * validating the EINJ FW revision.
	 */
	switch (pe->pe_fam) {
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		if (pe->pe_ras_cmd_buf->prcb_einj_fw_rev != PSP_EINJ_FW_REV1) {
			dev_err(pe->pe_dip, CE_WARN, "invalid EINJ FW rev: %u",
			    pe->pe_ras_cmd_buf->prcb_einj_fw_rev);
			return (false);
		}
		break;
	default:
		panic("unsupported processor family");
	}

	/*
	 * The reset state of these is unclear so we explicitly clear them.
	 */
	pe->pe_ras_cmd_buf->prcb_command_busy_status = 0;
	pe->pe_ras_cmd_buf->prcb_trigger_error_start = 0;
	pe->pe_ras_cmd_buf->prcb_trigger_error_stop = 0;
	bzero((void *)&pe->pe_ras_cmd_buf->prcb_set_error_type,
	    sizeof (pe->pe_ras_cmd_buf->prcb_set_error_type));
	bzero((void *)&pe->pe_ras_cmd_buf->prcb_set_error_type_with_addr,
	    sizeof (pe->pe_ras_cmd_buf->prcb_set_error_type_with_addr));

	dev_err(pe->pe_dip, CE_CONT, "?RAS EINJ enabled\n");

	return (true);
}

static int
psp_einj_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	psp_einj_t *pe = &psp_einj_data;

	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	mutex_enter(&pe->pe_lock);

	if (pe->pe_dip != NULL) {
		dev_err(dip, CE_WARN, "!psp_einj is already attached to a "
		    "dev_info_t: %p", pe->pe_dip);
		mutex_exit(&pe->pe_lock);
		return (DDI_FAILURE);
	}

	pe->pe_dip = dip;

	if (!psp_einj_enable(pe)) {
		goto err;
	}

	if (ddi_create_minor_node(dip, PSP_EINJ_MINOR_NAME, S_IFCHR,
	    PSP_EINJ_MINOR_NUM, DDI_PSEUDO, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!failed to create minor node %s",
		    PSP_EINJ_MINOR_NAME);
		goto err;
	}

	ddi_report_dev(dip);
	mutex_exit(&pe->pe_lock);
	return (DDI_SUCCESS);

err:
	pe->pe_dip = NULL;
	mutex_exit(&pe->pe_lock);
	return (DDI_FAILURE);
}

static int
psp_einj_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	psp_einj_t *pe = &psp_einj_data;

	if (cmd == DDI_SUSPEND) {
		return (DDI_SUCCESS);
	} else if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}

	mutex_enter(&pe->pe_lock);

	if (pe->pe_dip != dip) {
		dev_err(dip, CE_WARN, "!attempt to detach with wrong dip");
		mutex_exit(&pe->pe_lock);
		return (DDI_FAILURE);
	}

	(void) ddi_remove_minor_node(pe->pe_dip, NULL);
	pe->pe_dip = NULL;

	mutex_exit(&pe->pe_lock);

	return (DDI_SUCCESS);
}

static int
psp_einj_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	psp_einj_t *pe = &psp_einj_data;
	minor_t m;

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		m = getminor((dev_t)arg);
		if (m != PSP_EINJ_MINOR_NUM) {
			return (DDI_FAILURE);
		}
		*resultp = (void *)pe->pe_dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		m = getminor((dev_t)arg);
		if (m != PSP_EINJ_MINOR_NUM) {
			return (DDI_FAILURE);
		}
		*resultp = (void *)(uintptr_t)ddi_get_instance(pe->pe_dip);
		break;
	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
psp_einj_open(dev_t *devp, int flags, int otype, cred_t *credp)
{
	if (crgetzoneid(credp) != GLOBAL_ZONEID ||
	    secpolicy_error_inject(credp) != 0) {
		return (EPERM);
	}

	if ((flags & (FEXCL | FNDELAY | FNONBLOCK)) != 0) {
		return (EINVAL);
	}

	if (otype != OTYP_CHR) {
		return (EINVAL);
	}

	if (getminor(*devp) != PSP_EINJ_MINOR_NUM) {
		return (ENXIO);
	}

	return (0);
}

static int
psp_einj_req(psp_einj_t *pe, psp_einj_req_t *einj)
{
	volatile psp_ras_command_buffer_t *ras_cmd = pe->pe_ras_cmd_buf;
	psp_ras_error_types_t error_type = { 0 };
	volatile psp_ras_error_types_ext_t *err_ext =
	    (volatile psp_ras_error_types_ext_t *)
	    &ras_cmd->prcb_set_error_type_with_addr;
	int ret;
	uint8_t status;

	mutex_enter(&pe->pe_lock);

	if (ras_cmd->prcb_busy != 0) {
		ret = EBUSY;
		goto out;
	}

	switch (einj->per_type) {
	case PSP_EINJ_TYPE_MEM_CORRECTABLE:
		error_type.pret_memory_correctable = 1;
		break;
	case PSP_EINJ_TYPE_MEM_UNCORRECTABLE:
		error_type.pret_memory_uncorrectable = 1;
		break;
	case PSP_EINJ_TYPE_MEM_FATAL:
		error_type.pret_memory_fatal = 1;
		break;
	case PSP_EINJ_TYPE_PCIE_CORRECTABLE:
		error_type.pret_pcie_correctable = 1;
		break;
	case PSP_EINJ_TYPE_PCIE_UNCORRECTABLE:
		error_type.pret_pcie_uncorrectable = 1;
		break;
	case PSP_EINJ_TYPE_PCIE_FATAL:
		error_type.pret_pcie_fatal = 1;
		break;
	default:
		ret = EINVAL;
		goto out;
	}

	/*
	 * The Error Injection support provided by the PSP is usually used
	 * indirectly via the ACPI-based Error Injection (EINJ) table and that
	 * is clearly reflected in the "API". Thus even though we're not in an
	 * ACPI context here, ACPI (v6.3) definitions are referenced here.
	 */

	/*
	 * GET_ERROR_TYPE
	 *
	 * Make sure the requested error type is actually supported.
	 */
	if (!stdc_has_single_bit_ui(error_type.pret_val &
	    ras_cmd->prcb_error_types.pret_val)) {
		ret = ENOTSUP;
		goto out;
	}

	/*
	 * BEGIN_INJECTION_OPERATION
	 *
	 * Let the PSP know we're starting an injection operation. It will clear
	 * this bit once it's done after we've kicked off the operation below.
	 */
	ras_cmd->prcb_busy = 1;

	/*
	 * SET_ERROR_TYPE_WITH_ADDRESS
	 *
	 * Set the desired error type to inject along with the target-specific
	 * parameters.
	 */
	err_ext->prete_error_type = error_type;
	err_ext->prete_flags = 0;
	switch (einj->per_type) {
	case PSP_EINJ_TYPE_MEM_CORRECTABLE:
	case PSP_EINJ_TYPE_MEM_UNCORRECTABLE:
	case PSP_EINJ_TYPE_MEM_FATAL:
		err_ext->prete_mem_addr_valid = 1;
		err_ext->prete_mem_addr = einj->per_mem_addr;
		break;
	case PSP_EINJ_TYPE_PCIE_CORRECTABLE:
	case PSP_EINJ_TYPE_PCIE_UNCORRECTABLE:
	case PSP_EINJ_TYPE_PCIE_FATAL:
		err_ext->prete_pcie_sbdf_valid = 1;
		err_ext->prete_pcie_sbdf = einj->per_pcie_sbdf;
		break;
	default:
		panic("unexpected error injection type: %u", einj->per_type);
	}

	/*
	 * EXECUTE_OPERATION
	 *
	 * Inject the desired error into the system.
	 */
	ret = psp_einj_ras_cmd_direct(pe, PSP_RAS_EINJ_EXECUTE_OPERATION,
	    &status);
	if (ret != 0 || status != 0) {
		dev_err(pe->pe_dip, CE_NOTE, "?failed to execute EINJ: %d "
		    "(status = %u)\n", ret, status);
		if (ret == 0) {
			switch (status) {
			case EINJ_STATUS_INVALID:
				ret = EINVAL;
				break;
			case EINJ_STATUS_FAIL:
			default:
				ret = EIO;
				break;
			}
		}
		goto out;
	}

	/*
	 * CHECK_BUSY_STATUS
	 *
	 * Now we poll on the "busy" bit we had set above until the PSP clears
	 * it thus indicating the injection operations is complete.
	 */
	for (uint_t i = 0; i < psp_retry_attempts; i++) {
		if (ras_cmd->prcb_busy == 0)
			break;
		delay(psp_retry_delay);
	}
	if (ras_cmd->prcb_busy != 0) {
		dev_err(pe->pe_dip, CE_WARN, "timed out while waiting for "
		    "PSP to complete RAS EINJ operation");
		ret = ETIMEDOUT;
		goto out;
	}

	/*
	 * GET_COMMAND_STATUS
	 *
	 * Check that our error was successfully injected, otherwise make note
	 * of the error returned.
	 *
	 * Note: Seems like in some error cases at least, this status is
	 * actually returned as part of the EXECUTE_OPERATION command above.
	 */
	if (ras_cmd->prcb_command_status != EINJ_STATUS_SUCCESS) {
		dev_err(pe->pe_dip, CE_WARN, "failed to inject error: %u",
		    ras_cmd->prcb_command_status);
		switch (ras_cmd->prcb_command_status) {
		case EINJ_STATUS_INVALID:
			ret = EINVAL;
			break;
		case EINJ_STATUS_FAIL:
		default:
			ret = EIO;
			break;
		}
		goto out;
	}

	/*
	 * TRIGGER_ERROR
	 *
	 * Error injection is a 2-step process: 1) plumbing the desired error
	 * type and details and 2) actually triggering said error by setting
	 * the `TRIGGER_START` flag polled by the PSP.
	 *
	 * This may be skipped if "no trigger" is requested with the assumption
	 * the caller will trigger the error manually, e.g. via a memory access.
	 */
	if (einj->per_no_trigger)
		goto out;
	ras_cmd->prcb_trigger_error_start = 1;

	/*
	 * Wait for the PSP to acknowledge and clear the trigger flag.
	 */
	for (uint_t i = 0; i < psp_retry_attempts; i++) {
		if (ras_cmd->prcb_trigger_error_start == 0)
			break;
		delay(psp_retry_delay);
	}
	if (ras_cmd->prcb_trigger_error_start != 0) {
		dev_err(pe->pe_dip, CE_WARN, "timed out while waiting for "
		    "PSP to trigger RAS EINJ operation");
		ret = ETIMEDOUT;
	}

	/*
	 * END_OPERATION
	 *
	 * Set the `TRIGGER_END` flag to let the PSP know we're done.
	 */
	ras_cmd->prcb_trigger_error_stop = 1;

out:
	mutex_exit(&pe->pe_lock);
	return (ret);
}

static int
psp_einj_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	psp_einj_t *pe = &psp_einj_data;
	psp_einj_req_t einj = { 0 };
	int ret;

	if (getminor(dev) != PSP_EINJ_MINOR_NUM) {
		return (ENXIO);
	}

	if (crgetzoneid(credp) != GLOBAL_ZONEID) {
		return (EPERM);
	}

	if (cmd != PSP_EINJ_IOC_INJECT) {
		return (ENOTTY);
	}

	/*
	 * Require read/write for error injection.
	 */
	if ((mode & (FREAD | FWRITE)) != (FREAD | FWRITE)) {
		return (EBADF);
	}

	if (ddi_copyin((void *)arg, &einj, sizeof (einj), mode & FKIOCTL) != 0)
		return (EFAULT);

	ret = psp_einj_req(pe, &einj);

	return (ret);
}

static int
psp_einj_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}

	if (getminor(dev) != PSP_EINJ_MINOR_NUM) {
		return (ENXIO);
	}

	return (0);
}

static struct cb_ops psp_einj_cb_ops = {
	.cb_open = psp_einj_open,
	.cb_close = psp_einj_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = psp_einj_ioctl,
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

static struct dev_ops psp_einj_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = psp_einj_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = psp_einj_attach,
	.devo_detach = psp_einj_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_needed,
	.devo_cb_ops = &psp_einj_cb_ops
};

static struct modldrv psp_einj_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "AMD PSP Error Injection Driver",
	.drv_dev_ops = &psp_einj_dev_ops
};

static struct modlinkage psp_einj_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &psp_einj_modldrv, NULL }
};

int
_init(void)
{
	int ret;

	if ((ret = psp_einj_init()) != 0) {
		return (ret);
	}

	if ((ret = mod_install(&psp_einj_modlinkage)) != 0) {
		psp_einj_fini();
	}

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&psp_einj_modlinkage, modinfop));
}

int
_fini(void)
{
	int ret;

	if ((ret = mod_remove(&psp_einj_modlinkage)) == 0) {
		psp_einj_fini();
	}

	return (ret);
}
