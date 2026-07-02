/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Oxide Computer Company
 */

/*
 * A device driver that provides user access to the PM log (metrics) table
 * from the SMU power management firmware.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/bitext.h>
#include <sys/cmn_err.h>
#include <sys/cpuvar.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/policy.h>
#include <sys/stdbool.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vmem.h>
#include <sys/vmsystm.h>
#include <sys/x86_archext.h>
#include <sys/zone.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>
#include <amdzen_client.h>
#include <sys/amdzen/smupm.h>

#include "upmt.h"

uint_t upmt_reply_retry_count = 100;
uint_t upmt_reply_retry_delay = 10;	/* ticks */

/*
 * The PM log mailbox registers, message numbers and table size for one
 * processor family.
 */
typedef struct upmt_fam {
	x86_processor_family_t	uf_family;
	smn_reg_def_t		uf_msg_reg;
	smn_reg_def_t		uf_resp_reg;
	uint32_t		uf_op_read_sample;
	uint32_t		uf_op_get_dram_addr;
	uint32_t		uf_op_get_table_version;
	size_t			uf_table_size;
} upmt_fam_t;

static const upmt_fam_t upmt_fams[] = {
	{
		.uf_family = X86_PF_AMD_MILAN,
		.uf_msg_reg = D_SMN_SMUPM_MSG_MILAN,
		.uf_resp_reg = D_SMN_SMUPM_RESP_MILAN,
		.uf_op_read_sample = SMUPM_MILAN_OP_READ_SAMPLE,
		.uf_op_get_dram_addr = SMUPM_MILAN_OP_GET_DRAM_ADDR,
		.uf_op_get_table_version = SMUPM_MILAN_OP_GET_TABLE_VERSION,
		.uf_table_size = SMUPM_TABLE_SIZE,
	}, {
		.uf_family = X86_PF_AMD_TURIN,
		.uf_msg_reg = D_SMN_SMUPM_MSG_TURIN,
		.uf_resp_reg = D_SMN_SMUPM_RESP_TURIN,
		.uf_op_read_sample = SMUPM_TURIN_OP_READ_SAMPLE,
		.uf_op_get_dram_addr = SMUPM_TURIN_OP_GET_DRAM_ADDR,
		.uf_op_get_table_version = SMUPM_TURIN_OP_GET_TABLE_VERSION,
		.uf_table_size = SMUPM_TABLE_SIZE,
	}, {
		.uf_family = X86_PF_AMD_DENSE_TURIN,
		.uf_msg_reg = D_SMN_SMUPM_MSG_TURIN,
		.uf_resp_reg = D_SMN_SMUPM_RESP_TURIN,
		.uf_op_read_sample = SMUPM_TURIN_OP_READ_SAMPLE,
		.uf_op_get_dram_addr = SMUPM_TURIN_OP_GET_DRAM_ADDR,
		.uf_op_get_table_version = SMUPM_TURIN_OP_GET_TABLE_VERSION,
		.uf_table_size = SMUPM_TABLE_SIZE,
	}
};

/*
 * The adopted PM table buffer of one DF.
 */
typedef struct upmt_table {
	bool		ut_valid;
	uint64_t	ut_pa;
	caddr_t		ut_kva;
	size_t		ut_len;
	uint32_t	ut_version;
} upmt_table_t;

typedef struct {
	dev_info_t		*upmt_dip;
	const upmt_fam_t	*upmt_fam_data;
	uint_t			upmt_ndfs;
	upmt_table_t		*upmt_tables;
	kmutex_t		upmt_lock;
} upmt_t;

static upmt_t upmt_data;

static bool upmt_adopt(upmt_t *, uint_t);

static int
upmt_open(dev_t *devp, int flags, int otype, cred_t *credp)
{
	upmt_t *upmt = &upmt_data;
	minor_t m;

	if (crgetzoneid(credp) != GLOBAL_ZONEID ||
	    secpolicy_hwmanip(credp) != 0) {
		return (EPERM);
	}

	if ((flags & (FEXCL | FNDELAY | FNONBLOCK)) != 0)
		return (EINVAL);

	if (otype != OTYP_CHR)
		return (EINVAL);

	m = getminor(*devp);
	if (m >= upmt->upmt_ndfs)
		return (ENXIO);

	/*
	 * A buffer may have been registered since we last looked, so a DF
	 * without a table gets another chance at adoption on each open.
	 * Registration is usually done by system firmware before boot, or by
	 * the operating system during boot, but there is no fixed ordering
	 * with respect to this driver attaching, and debugging tools that
	 * talk to the SMU directly can register a buffer at any time.
	 */
	mutex_enter(&upmt->upmt_lock);
	if (!upmt->upmt_tables[m].ut_valid)
		(void) upmt_adopt(upmt, m);
	if (!upmt->upmt_tables[m].ut_valid) {
		mutex_exit(&upmt->upmt_lock);
		return (ENXIO);
	}
	mutex_exit(&upmt->upmt_lock);

	return (0);
}

/*
 * Issue one message on the tool mailbox of the given DF.
 */
static int
upmt_tool_rpc(upmt_t *upmt, uint_t dfno, uint32_t op, uint32_t arg0,
    uint32_t arg1, uint32_t *out0, uint32_t *out1)
{
	const upmt_fam_t *fam = upmt->upmt_fam_data;
	const smn_reg_t msg = amdzen_smn_smupm_reg(0, fam->uf_msg_reg, 0);
	const smn_reg_t resp = amdzen_smn_smupm_reg(0, fam->uf_resp_reg, 0);
	uint32_t val;
	int ret;

	ASSERT(MUTEX_HELD(&upmt->upmt_lock));

	ret = amdzen_c_smn_write(dfno, resp, SMUPM_RESPONSE_INCOMPLETE);
	if (ret != 0)
		return (ret);
	if ((ret = amdzen_c_smn_write(dfno, SMN_SMUPM_ARG(0), arg0)) != 0)
		return (ret);
	if ((ret = amdzen_c_smn_write(dfno, SMN_SMUPM_ARG(1), arg1)) != 0)
		return (ret);
	for (uint_t i = 2; i < SMUPM_NARGS; i++) {
		if ((ret = amdzen_c_smn_write(dfno, SMN_SMUPM_ARG(i), 0)) != 0)
			return (ret);
	}
	if ((ret = amdzen_c_smn_write(dfno, msg, op)) != 0)
		return (ret);

	val = SMUPM_RESPONSE_INCOMPLETE;
	for (uint_t i = 0; i < upmt_reply_retry_count; i++) {
		if ((ret = amdzen_c_smn_read(dfno, resp, &val)) != 0)
			return (ret);
		if (val != SMUPM_RESPONSE_INCOMPLETE)
			break;
		delay(upmt_reply_retry_delay);
	}
	if (val == SMUPM_RESPONSE_INCOMPLETE)
		return (ETIMEDOUT);
	if (val != SMUPM_RESPONSE_OK)
		return (EIO);

	if (out0 != NULL &&
	    (ret = amdzen_c_smn_read(dfno, SMN_SMUPM_ARG(0), out0)) != 0) {
		return (ret);
	}
	if (out1 != NULL &&
	    (ret = amdzen_c_smn_read(dfno, SMN_SMUPM_ARG(1), out1)) != 0) {
		return (ret);
	}

	return (0);
}

/*
 * Ask the SMU for the DRAM buffer address registered on this DF and adopt
 * the buffer found there.
 */
static bool
upmt_adopt(upmt_t *upmt, uint_t dfno)
{
	const upmt_fam_t *fam = upmt->upmt_fam_data;
	upmt_table_t *ut = &upmt->upmt_tables[dfno];
	uint32_t lo = 0, hi = 0;
	uint64_t pa;

	ASSERT(MUTEX_HELD(&upmt->upmt_lock));

	if (upmt_tool_rpc(upmt, dfno, fam->uf_op_get_dram_addr, 0, 0,
	    &lo, &hi) != 0) {
		return (false);
	}

	pa = (uint64_t)hi << 32 | lo;
	if (pa == 0 || !IS_P2ALIGNED(pa, MMU_PAGESIZE))
		return (false);

	for (pgcnt_t i = 0; i < mmu_btop(fam->uf_table_size); i++) {
		if (pf_is_memory(mmu_btop(pa) + i) == 0)
			return (false);
	}

	ut->ut_pa = pa;
	ut->ut_len = fam->uf_table_size;
	ut->ut_kva = vmem_alloc(heap_arena, ut->ut_len, VM_SLEEP);
	hat_devload(kas.a_hat, ut->ut_kva, ut->ut_len, mmu_btop(pa),
	    PROT_READ, HAT_LOAD_LOCK);
	ut->ut_version = 0;
	if (upmt_tool_rpc(upmt, dfno, fam->uf_op_get_table_version, 0, 0,
	    &ut->ut_version, NULL) != 0) {
		dev_err(upmt->upmt_dip, CE_WARN,
		    "!DF %u: failed to read the PM table version", dfno);
	}
	ut->ut_valid = true;

	dev_err(upmt->upmt_dip, CE_CONT, "?DF %u: adopted SMU PM table at "
	    "pa 0x%lx (%lu bytes, version 0x%x)\n", dfno, pa, ut->ut_len,
	    ut->ut_version);

	return (true);
}

static int
upmt_refresh(upmt_t *upmt, uint_t dfno)
{
	const upmt_fam_t *fam = upmt->upmt_fam_data;
	const upmt_table_t *ut = &upmt->upmt_tables[dfno];

	ASSERT(MUTEX_HELD(&upmt->upmt_lock));
	ASSERT(ut->ut_valid);

	return (upmt_tool_rpc(upmt, dfno, fam->uf_op_read_sample,
	    bitx64(ut->ut_pa, 31, 0), bitx64(ut->ut_pa, 63, 32), NULL, NULL));
}

static int
upmt_ioctl_info(upmt_t *upmt, uint_t dfno, intptr_t arg, int mode)
{
	const upmt_table_t *ut = &upmt->upmt_tables[dfno];
	upmt_info_t ui;

	bzero(&ui, sizeof (ui));
	mutex_enter(&upmt->upmt_lock);
	ASSERT(ut->ut_valid);
	ui.ui_version = ut->ut_version;
	ui.ui_len = ut->ut_len;
	mutex_exit(&upmt->upmt_lock);

	if (ddi_copyout(&ui, (void *)arg, sizeof (ui), mode & FKIOCTL) != 0)
		return (EFAULT);

	return (0);
}

static int
upmt_ioctl_read(upmt_t *upmt, uint_t dfno, intptr_t arg, int mode)
{
	const upmt_table_t *ut = &upmt->upmt_tables[dfno];
	upmt_read_t ur;
	size_t len;
	int ret = 0;

	if (ddi_copyin((void *)arg, &ur, sizeof (ur), mode & FKIOCTL) != 0)
		return (EFAULT);

	mutex_enter(&upmt->upmt_lock);
	ASSERT(ut->ut_valid);
	len = ut->ut_len;

	if (ur.ur_len < len) {
		mutex_exit(&upmt->upmt_lock);
		ur.ur_len = len;
		if (ddi_copyout(&ur, (void *)arg, sizeof (ur),
		    mode & FKIOCTL) != 0) {
			return (EFAULT);
		}
		return (EOVERFLOW);
	}

	/*
	 * The SMU writes to the table only in response to a refresh message,
	 * and refreshes are sent while holding the lock, so copying straight
	 * from the table here yields a coherent snapshot. The lock may be
	 * held across page faults on the user buffer as a consequence, which
	 * is acceptable for a diagnostic interface such as this.
	 */
	ur.ur_version = ut->ut_version;
	if (ddi_copyout(ut->ut_kva, (void *)(uintptr_t)ur.ur_buf, len,
	    mode & FKIOCTL) != 0) {
		ret = EFAULT;
	}
	mutex_exit(&upmt->upmt_lock);

	if (ret == 0) {
		ur.ur_len = len;
		if (ddi_copyout(&ur, (void *)arg, sizeof (ur),
		    mode & FKIOCTL) != 0) {
			ret = EFAULT;
		}
	}

	return (ret);
}

static int
upmt_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	upmt_t *upmt = &upmt_data;
	uint_t dfno;
	int ret;

	/*
	 * A table is always adopted before an open of the corresponding
	 * minor succeeds, and remains valid for as long as the device is
	 * open, so its validity is asserted in the individual operations
	 * rather than checked here.
	 */
	dfno = getminor(dev);
	if (dfno >= upmt->upmt_ndfs)
		return (ENXIO);

	if (crgetzoneid(credp) != GLOBAL_ZONEID ||
	    secpolicy_hwmanip(credp) != 0) {
		return (EPERM);
	}

	switch (cmd) {
	case UPMT_INFO:
		if ((mode & FREAD) == 0)
			return (EBADF);
		return (upmt_ioctl_info(upmt, dfno, arg, mode));
	case UPMT_REFRESH:
		if ((mode & FWRITE) == 0)
			return (EBADF);
		mutex_enter(&upmt->upmt_lock);
		ret = upmt_refresh(upmt, dfno);
		mutex_exit(&upmt->upmt_lock);
		return (ret);
	case UPMT_READ:
		if ((mode & FREAD) == 0)
			return (EBADF);
		return (upmt_ioctl_read(upmt, dfno, arg, mode));
	default:
		return (ENOTTY);
	}
}

static int
upmt_close(dev_t dev, int flag __unused, int otyp, cred_t *credp __unused)
{
	upmt_t *upmt = &upmt_data;

	if (otyp != OTYP_CHR)
		return (EINVAL);
	if (getminor(dev) >= upmt->upmt_ndfs)
		return (ENXIO);

	return (0);
}

static void
upmt_cleanup(upmt_t *upmt)
{
	ddi_remove_minor_node(upmt->upmt_dip, NULL);
	if (upmt->upmt_tables != NULL) {
		for (uint_t i = 0; i < upmt->upmt_ndfs; i++) {
			upmt_table_t *ut = &upmt->upmt_tables[i];

			if (!ut->ut_valid)
				continue;
			hat_unload(kas.a_hat, ut->ut_kva, ut->ut_len,
			    HAT_UNLOAD_UNLOCK);
			vmem_free(heap_arena, ut->ut_kva, ut->ut_len);
		}
		kmem_free(upmt->upmt_tables,
		    upmt->upmt_ndfs * sizeof (upmt_table_t));
		upmt->upmt_tables = NULL;
	}
	upmt->upmt_ndfs = 0;
	upmt->upmt_dip = NULL;
	mutex_destroy(&upmt->upmt_lock);
}

static int
upmt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	upmt_t *upmt = &upmt_data;
	x86_processor_family_t fam;

	if (cmd == DDI_RESUME)
		return (DDI_SUCCESS);
	else if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (upmt->upmt_dip != NULL) {
		dev_err(dip, CE_WARN,
		    "!upmt is already attached to a dev_info_t: %p",
		    upmt->upmt_dip);
		return (DDI_FAILURE);
	}

	fam = chiprev_family(cpuid_getchiprev(CPU));
	upmt->upmt_fam_data = NULL;
	for (uint_t i = 0; i < ARRAY_SIZE(upmt_fams); i++) {
		if (upmt_fams[i].uf_family == fam) {
			upmt->upmt_fam_data = &upmt_fams[i];
			break;
		}
	}
	if (upmt->upmt_fam_data == NULL)
		return (DDI_FAILURE);

	upmt->upmt_dip = dip;
	mutex_init(&upmt->upmt_lock, NULL, MUTEX_DRIVER, NULL);
	upmt->upmt_ndfs = amdzen_c_df_count();
	upmt->upmt_tables = kmem_zalloc(
	    upmt->upmt_ndfs * sizeof (upmt_table_t), KM_SLEEP);

	/*
	 * Adoption of each DF's table is deferred to open time as a buffer
	 * may be registered with the SMU at any point after we attach.
	 */
	for (uint_t i = 0; i < upmt->upmt_ndfs; i++) {
		char buf[32];

		(void) snprintf(buf, sizeof (buf), "upmt.%u", i);
		if (ddi_create_minor_node(dip, buf, S_IFCHR, i, DDI_PSEUDO,
		    0) != DDI_SUCCESS) {
			dev_err(dip, CE_WARN,
			    "!failed to create minor %s", buf);
			upmt_cleanup(upmt);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

static int
upmt_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	upmt_t *upmt = &upmt_data;
	minor_t m;

	if (upmt->upmt_dip == NULL)
		return (DDI_FAILURE);

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		m = getminor((dev_t)arg);
		if (m >= upmt->upmt_ndfs)
			return (DDI_FAILURE);
		*resultp = (void *)upmt->upmt_dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		m = getminor((dev_t)arg);
		if (m >= upmt->upmt_ndfs)
			return (DDI_FAILURE);
		*resultp = (void *)(uintptr_t)ddi_get_instance(upmt->upmt_dip);
		break;
	default:
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
upmt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	upmt_t *upmt = &upmt_data;

	if (cmd == DDI_SUSPEND)
		return (DDI_SUCCESS);
	else if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	if (upmt->upmt_dip != dip) {
		dev_err(dip, CE_WARN,
		    "!asked to detach upmt, but dip doesn't match");
		return (DDI_FAILURE);
	}

	upmt_cleanup(upmt);

	return (DDI_SUCCESS);
}

static struct cb_ops upmt_cb_ops = {
	.cb_open = upmt_open,
	.cb_close = upmt_close,
	.cb_strategy = nodev,
	.cb_print = nodev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = upmt_ioctl,
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

static struct dev_ops upmt_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = upmt_getinfo,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = upmt_attach,
	.devo_detach = upmt_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_needed,
	.devo_cb_ops = &upmt_cb_ops
};

static struct modldrv upmt_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "AMD User PMFW Log Access",
	.drv_dev_ops = &upmt_dev_ops
};

static struct modlinkage upmt_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &upmt_modldrv, NULL }
};

int
_init(void)
{
	return (mod_install(&upmt_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&upmt_modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&upmt_modlinkage));
}
