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
 * Copyright 2023 Oxide Computer Company
 */

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/modctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/policy.h>
#include <sys/file.h>
#include <sys/x86_archext.h> /* rdmsr, wrmsr */

#include <sys/io/milan/fabric.h>
#include <sys/io/milan/psp_impl.h>
#include <sys/io/milan/iohc.h>

/* ========================== Parameter to PSP ============================ */

typedef struct psp_param {
	milan_psp_param_header_t mpp_header;
	char mpp_payload[];
} psp_param_t;

typedef struct psp_param_config {
	psp_param_t *mppc_buffer;
	uint64_t mppc_pa;
	size_t mppc_alloc_len;
	ddi_acc_handle_t mppc_acc_handle;
	ddi_dma_handle_t mppc_dma_handle;
	caddr_t mppc_caddr;
} psp_param_config_t;

/*
 * Create DMA attributes that are appropriate for the PSP. In particular, we
 * know experimentally that there is usually a 32-bit length register for DMA
 * and generally a 64-bit address register. There aren't many other bits that we
 * actually know here, as such, we generally end up making some assumptions out
 * of paranoia in an attempt at safety. In particular, we assume and ask for
 * page alignment here.
 *
 * XXX Remove 32-bit addr_hi constraint.
 */
static void
psp_param_buffer_dma_attr(ddi_dma_attr_t *attr)
{
	bzero(attr, sizeof (attr));
	attr->dma_attr_version = DMA_ATTR_V0;
	attr->dma_attr_addr_lo = 0;
	attr->dma_attr_addr_hi = UINT32_MAX;
	attr->dma_attr_count_max = UINT32_MAX;
	attr->dma_attr_align = MMU_PAGESIZE;
	attr->dma_attr_minxfer = 1;
	attr->dma_attr_maxxfer = UINT32_MAX;
	attr->dma_attr_seg = UINT32_MAX;
	attr->dma_attr_sgllen = 1;
	attr->dma_attr_granular = 1;
	attr->dma_attr_flags = 0;
}

static int
psp_param_buffer_init(psp_param_config_t *conf, dev_info_t *devi)
{
	ddi_device_acc_attr_t dev_attr = {
		.devacc_attr_version = DDI_DEVICE_ATTR_V0,
		.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
		.devacc_attr_dataorder = DDI_STRICTORDER_ACC,
	};

	ddi_dma_attr_t attr;
	psp_param_buffer_dma_attr(&attr);
	if (ddi_dma_alloc_handle(devi, &attr, DDI_DMA_SLEEP, 0,
	    &conf->mppc_dma_handle) != DDI_SUCCESS) {
		cmn_err(CE_PANIC, "Could not alloc DMA handle");
		return (EFAULT);
	}
	if (ddi_dma_mem_alloc(conf->mppc_dma_handle, MMU_PAGESIZE, &dev_attr,
	    DDI_DMA_CONSISTENT | IOMEM_DATA_UNCACHED, DDI_DMA_SLEEP, 0,
	    &conf->mppc_caddr, &conf->mppc_alloc_len, &conf->mppc_acc_handle)
	    != DDI_SUCCESS) {
		cmn_err(CE_PANIC, "Could not alloc ACC handle");
		ddi_dma_free_handle(&conf->mppc_dma_handle);
		return (EFAULT);
	}
	bzero((void *) conf->mppc_caddr, conf->mppc_alloc_len);
	conf->mppc_buffer = (psp_param_t *)conf->mppc_caddr;

	/*
	 * Need the physical address of it--and it must not move or swap out.
	 */
	pfn_t pfn;
	pfn = hat_getpfnum(kas.a_hat, conf->mppc_caddr);
	conf->mppc_pa = mmu_ptob(pfn);

	return (0);
}

/*
 * Precondition:
 *
 *   Previous RPC finished already.
 *   Use psp_await_rpc if you want to make sure.
 */
static void
psp_param_buffer_fini(psp_param_config_t *conf, dev_info_t *devi)
{
	ddi_dma_mem_free(&conf->mppc_acc_handle);
	ddi_dma_free_handle(&conf->mppc_dma_handle);
}

/* ========================= PSP mailbox ======================== */

/*
 * Note: This is a RPC structure in DRAM that can be read or written by
 * the PSP at any time.  Rome and Genoa have an alternative to this in
 * SMN.
 */
/* FIXME packing? */
typedef struct psp_mbox_rpc {
	uint32_t mpmr_command;
	uint64_t mpmr_buffer; /* psp_param_buffer_t*, but physical */
} psp_mbox_rpc_t;

/* set by target */
#define	COMMAND_GET_STATUS(command) ((command) & 0xFFFF)

/* set by host */
#define	COMMAND_GET_OPCODE(command) (((command) >> 16) & 0xFF)

/* set by host */
#define	COMMAND_SET_OPCODE(command, opcode) \
	(((command) & 0xFF00FFFF) | ((((opcode) & 0xFF) << 16)))

/* set by target */ /* TODO: use */
#define	COMMAND_GET_RESET_REQUIRED(command) (((command) >> 29) & 0x1)

/* set by target */
#define	COMMAND_GET_RECOVERY(command) (((command) >> 30) & 0x1)

/* set by target */
#define	COMMAND_GET_READY(command) (((command) >> 31) & 0x1)

typedef struct psp_config {
	psp_mbox_rpc_t *mpc_rpc;
	uint64_t mpc_pa;
	size_t mpc_alloc_len;
	ddi_acc_handle_t mpc_acc_handle;
	ddi_dma_handle_t mpc_dma_handle;
	caddr_t mpc_caddr;

	struct psp_param_config mpc_mppc;
} psp_config_t;

/*
 * Create DMA attributes that are appropriate for the PSP. In particular, we
 * know experimentally that there is usually a 32-bit length register for DMA
 * and generally a 64-bit address register. There aren't many other bits that we
 * actually know here, as such, we generally end up making some assumptions out
 * of paranoia in an attempt at safety.
 *
 * XXX Remove 32-bit addr_hi constraint.
 */
static void
psp_dma_attr(ddi_dma_attr_t *attr)
{
	bzero(attr, sizeof (attr));
	attr->dma_attr_version = DMA_ATTR_V0;
	attr->dma_attr_addr_lo = 0;
	attr->dma_attr_addr_hi = UINT32_MAX;
	attr->dma_attr_count_max = UINT32_MAX;
	attr->dma_attr_align = 1 << 20;
	attr->dma_attr_minxfer = 1;
	attr->dma_attr_maxxfer = UINT32_MAX;
	attr->dma_attr_seg = UINT32_MAX;
	attr->dma_attr_sgllen = 1;
	attr->dma_attr_granular = 1;
	attr->dma_attr_flags = 0;
}

/*
 * Note: This is mostly its own memory allocation because, on Rome and Genoa,
 * psp_config_t could be replaced by SMN accesses if we wanted.  But even
 * there, the parameter buffer (see psp_param_config_t) would still be the
 * same.
 *
 * See also: psp_param_config_t for the actual payload.
 */
static int
psp_config_init(psp_config_t *conf, dev_info_t *devi)
{
	ddi_dma_attr_t attr;
	psp_dma_attr(&attr);
	CTASSERT(sizeof (*conf->mpc_rpc) <= MMU_PAGESIZE);
	ddi_device_acc_attr_t dev_attr = {
		.devacc_attr_version = DDI_DEVICE_ATTR_V0,
		.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
		.devacc_attr_dataorder = DDI_STRICTORDER_ACC,
	};

	if (ddi_dma_alloc_handle(devi, &attr, DDI_DMA_SLEEP, 0,
	    &conf->mpc_dma_handle) != DDI_SUCCESS) {
		cmn_err(CE_PANIC, "Could not alloc DMA handle");
		return (EFAULT);
	}

	if (ddi_dma_mem_alloc(conf->mpc_dma_handle, MMU_PAGESIZE, &dev_attr,
	    DDI_DMA_CONSISTENT | IOMEM_DATA_UNCACHED, DDI_DMA_SLEEP, 0,
	    &conf->mpc_caddr, &conf->mpc_alloc_len, &conf->mpc_acc_handle)
	    != DDI_SUCCESS) {
		cmn_err(CE_PANIC, "Could not alloc ACC handle");
		ddi_dma_free_handle(&conf->mpc_dma_handle);
		return (EFAULT);
	}
	bzero((void*)conf->mpc_caddr, MMU_PAGESIZE);
	conf->mpc_rpc = (psp_mbox_rpc_t *)conf->mpc_caddr;

	pfn_t pfn;
	pfn = hat_getpfnum(kas.a_hat, conf->mpc_caddr);
	conf->mpc_pa = mmu_ptob(pfn);

	/*
	 * Also allocate the parameter buffer.
	 * Technically, we could swap out the parameter buffer at any time.
	 */

	if (psp_param_buffer_init(&conf->mpc_mppc, devi)) { /* failed */
		ddi_dma_mem_free(&conf->mpc_acc_handle);
		ddi_dma_free_handle(&conf->mpc_dma_handle);
		return (EFAULT);
	}
	conf->mpc_rpc->mpmr_buffer = conf->mpc_mppc.mppc_pa;
	return (0);
}

/*
 * Precondition:
 *
 *   Previous RPC finished already.
 *   Use psp_await_rpc if you want to make sure.
 */
static void
psp_config_fini(psp_config_t *conf, dev_info_t *devi)
{
	psp_param_buffer_fini(&conf->mpc_mppc, devi);

	ddi_dma_mem_free(&conf->mpc_acc_handle);
	ddi_dma_free_handle(&conf->mpc_dma_handle);
	conf->mpc_rpc = NULL;
}

/* ============================ PSP mailbox runtime ========================= */

/*
 * Wait for the previous RPC to finish.
 * Return status code from remote, or error from us (negative).
 */
static int
psp_await_rpc(psp_config_t *conf)
{
	int counter = 100;
	__sync_synchronize();
	while (!COMMAND_GET_READY(conf->mpc_rpc->mpmr_command) ||
	    COMMAND_GET_OPCODE(conf->mpc_rpc->mpmr_command)) {
		if (counter-- == 0) {
			return (-EIO);
		}
		drv_usecwait(1000);
		/* TODO sched_yield(); maybe */
		__sync_synchronize();
	}
	/*
	 * status 0: OK; status > 0: error reported by PSP;
	 * otherwise: EIO or similar.
	 */
	return (COMMAND_GET_STATUS(conf->mpc_rpc->mpmr_command));
}

/*
 * Precondition:
 *
 *   Previous RPC finished already.
 *   Use psp_await_rpc if you want to make sure.
 *
 * Note: This does not block until the PSP is done with the RPC.
 */
static void
psp_start_rpc(psp_config_t *conf, uint32_t opcode)
{
	__sync_synchronize();
	conf->mpc_rpc->mpmr_command = COMMAND_SET_OPCODE(0, opcode);
}

/* ========================= PSP mailbox attaching ======================== */

struct attached_state {
	dev_info_t *devi;
	int instance;
	kmutex_t lock;
	psp_config_t psp_config;
};

static int
milan_fabric_init_psp(milan_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	uint64_t rpc_data_phy;
	struct attached_state *qsp = arg;
	milan_iodie_t *iodie;

	if (qsp->psp_config.mpc_rpc) {
		/*
		 * XXX there's probably just one! If not, we should figure out
		 * which one we are attaching to (see qsp->instance or
		 * something).
		 */
		return (0);
	}

	if (milan_ioms_flags(ioms) & MILAN_IOMS_F_HAS_FCH) {
		cmn_err(CE_WARN, "Selected ioms has FCH");
	}

	if (milan_ioms_flags(ioms) & MILAN_IOMS_F_HAS_WAFL) {
		cmn_err(CE_WARN, "Selected ioms has WAFL");
	}

	iodie = milan_ioms_iodie(ioms);
	if (milan_iodie_flags(iodie) & MILAN_IODIE_F_PRIMARY) {
	} else {
		cmn_err(CE_WARN, "Selected iodie of that ioms is not primary!");
	}

	cmn_err(CE_WARN, "Selected node id: 0x%X", milan_iodie_node_id(iodie));

	if (psp_config_init(&qsp->psp_config, qsp->devi)) { /* failed */
		return (EFAULT);
	}
	rpc_data_phy = qsp->psp_config.mpc_pa;

	if ((rpc_data_phy & ((1U << 20) - 1U)) != 0) {
		cmn_err(CE_PANIC, "Internal error: memory block not aligned");
		return (EFAULT);
	}

	reg = milan_ioms_reg(ioms, D_IOHC_PSP_ADDR_LO, 0);
	val = milan_ioms_read(ioms, reg);
	cmn_err(CE_NOTE, "init_psp: D_IOHC_PSP_ADDR_LO before: 0x%X", val);
	if (IOHC_PSP_ADDR_LO_GET_EN(val)) {
		cmn_err(CE_PANIC, "PSP mailbox is already enabled. Refusing");
		return (EBUSY);
	}

	if (IOHC_PSP_ADDR_LO_GET_LOCK(val)) {
		cmn_err(CE_PANIC, "PSP mailbox is locked. Refusing");
		return (EBUSY);
	}

	reg = milan_ioms_reg(ioms, D_IOHC_PSP_ADDR_HI, 0);
	val = milan_ioms_read(ioms, reg);
	val = IOHC_PSP_ADDR_HI_SET_ADDR(val, bitx64(rpc_data_phy, 47, 32));
	milan_ioms_write(ioms, reg, val);
	cmn_err(CE_NOTE, "init_psp: D_IOHC_PSP_ADDR_LO: 0x%X", val);

	cmn_err(CE_NOTE, "init_psp: D_IOHC_PSP_ADDR_HI before: 0x%X", val);
	val = IOHC_PSP_ADDR_LO_SET_ADDR(val, bitx64(rpc_data_phy, 31, 20));
	val = IOHC_PSP_ADDR_LO_SET_LOCK(val, 0);
	val = IOHC_PSP_ADDR_LO_SET_EN(val, 1);
	milan_ioms_write(ioms, reg, val);
	cmn_err(CE_NOTE, "init_psp: D_IOHC_PSP_ADDR_HI: 0x%X", val);

	/* Note: Cannot set PSP BAR twice or we will get a CPU exception. */
	if (rdmsr(0xC00110A2) == 0) { /* the hardware default */
		wrmsr(0xC00110A2, rpc_data_phy);
	} else {
		cmn_err(CE_PANIC, "init_psp: PSP BAR in MSR already set?!");
		return (EBUSY);
	}

	return (0);
}

static int
milan_fabric_fini_psp(milan_ioms_t *ioms, void *arg)
{
	smn_reg_t reg;
	uint32_t val;
	struct attached_state *qsp;
	int status;
	psp_config_t *conf;

	qsp = arg;

	if (qsp->psp_config.mpc_rpc == NULL) {
		return (0);
	}

	conf = &qsp->psp_config;

	status = psp_await_rpc(conf);
	if (status) {
		cmn_err(CE_WARN,
		    "psp_detach: Dropped response of prev command (status %d)",
		    status);
	}

	psp_config_fini(conf, qsp->devi);

	reg = milan_ioms_reg(ioms, D_IOHC_PSP_ADDR_LO, 0);
	val = milan_ioms_read(ioms, reg);
	if (IOHC_PSP_ADDR_LO_GET_LOCK(val)) {
		cmn_err(CE_PANIC,
		    "psp_detach: Couldn't detach because hw reg is locked.");
		return (EBUSY);
	}
	val = IOHC_PSP_ADDR_LO_SET_EN(val, 0);
	milan_ioms_write(ioms, reg, val);

	return (0);
}

/* ========================= kernel module attaching ======================== */

static void *attached_state_head;

static int
psp_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct attached_state *qsp;
	int instance = ddi_get_instance(devi); /* TODO: minor */
	switch (cmd) {
	case DDI_ATTACH:
		/*
		 * TODO: Maybe use ddi_set_driver_private(devi, softc)
		 */
		if (ddi_soft_state_zalloc(attached_state_head, instance) !=
		    DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "psp_attach: Unable to allocate state");
			return (DDI_FAILURE);
		}
		if ((qsp = ddi_get_soft_state(attached_state_head, instance)) ==
		    NULL) {
			cmn_err(CE_WARN,
			    "psp_attach: Unable to obtain state");
			ddi_soft_state_free(devi, instance);
			return (DDI_FAILURE);
		}

		qsp->devi = devi;
		qsp->instance = instance;
		mutex_init(&qsp->lock, NULL, MUTEX_DRIVER, NULL);

		if (ddi_create_minor_node(devi, ddi_get_name(devi), S_IFCHR,
		    instance, DDI_PSEUDO, 0) != DDI_SUCCESS) {
			cmn_err(CE_NOTE,
			    "psp_attach: could not add char node");
			return (DDI_FAILURE);
		}

		mutex_enter(&qsp->lock);
		if (milan_walk_ioms(milan_fabric_init_psp, qsp)) {
			mutex_exit(&qsp->lock);
			cmn_err(CE_PANIC,
			    "psp_attach: milan_fabric_init_psp failed");
			return (DDI_FAILURE);
		}
		mutex_exit(&qsp->lock);
		if (qsp->psp_config.mpc_rpc == NULL) {
			cmn_err(CE_PANIC,
			    "psp_attach: Unable to obtain PSP mailbox");
			return (DDI_FAILURE);
		}

		return (DDI_SUCCESS);
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

static int
psp_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	struct attached_state *qsp;
	int instance = ddi_get_instance(devi);
	if ((qsp = ddi_get_soft_state(attached_state_head, instance)) == NULL) {
		cmn_err(CE_WARN,
		    "psp_detach: Unable to obtain state");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_DETACH:
		/*
		 * Just to make sure we got the right device in
		 * milan_fabric_fini_psp later
		 */
		qsp->devi = devi;

		mutex_enter(&qsp->lock);
		if (milan_walk_ioms(milan_fabric_fini_psp, qsp)) { /* failed */
			cmn_err(CE_PANIC,
			    "psp_attach: milan_fabric_fini_psp failed");
			mutex_exit(&qsp->lock);
			return (DDI_FAILURE);
		}
		mutex_exit(&qsp->lock);

		ddi_remove_minor_node(devi, NULL);
		mutex_destroy(&qsp->lock);

		(void) ddi_soft_state_free(attached_state_head, instance);
		return (DDI_SUCCESS);

	case DDI_SUSPEND: {
		int status;
		status = psp_await_rpc(&qsp->psp_config);
		if (status) {
			cmn_err(CE_WARN,
			    "psp_detach: Dropped response of prev command "
			    "(status %d)", status);
		}
		return (DDI_SUCCESS);
	}

	default:
		cmn_err(CE_WARN, "psp_detach: unknown cmd 0x%x", cmd);
		return (DDI_FAILURE);
	}
}

/* ==================== kernel module main functionality ==================== */

/*ARGSUSED*/
static int
psp_info(dev_info_t *devi, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	struct attached_state *qsp;
	dev_t dev = (dev_t)arg;
	minor_t minor = getminor(dev);
	if ((qsp = ddi_get_soft_state(attached_state_head, minor)) ==
	    NULL) {
		cmn_err(CE_WARN,
		    "psp_detach: Unable to obtain state");
		return (ENXIO);
	}

	/* FIXME: make it so that minor = instance(devi) */
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		/* Note: Assumes psp_attach succeeded before */
		*result = qsp->devi;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)minor;
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
psp_open(dev_t *dev, int flag, int otyp, cred_t *cred)
{
	struct attached_state *qsp;
	if ((qsp = ddi_get_soft_state(attached_state_head, getminor(*dev))) ==
	    NULL) {
		cmn_err(CE_WARN,
		    "psp_open: Unable to obtain state");
		return (ENXIO);
	}

	cmn_err(CE_NOTE, "Inside psp_open");
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}
	if ((flag & FEXCL) == 0) {
		return (EINVAL);
	}
	if ((flag & (FNDELAY | FNONBLOCK)) != 0) {
		return (EINVAL);
	}
	/* Require PRIV_SYS_RES_CONFIG, same as psradm */
	if (secpolicy_ponline(cred)) {
		return (EPERM);
	}
	mutex_enter(&qsp->lock);
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
psp_close(dev_t dev, int flag, int otyp, cred_t *cred)
{
	struct attached_state *qsp;
	if ((qsp = ddi_get_soft_state(attached_state_head, getminor(dev))) ==
	    NULL) {
		cmn_err(CE_WARN,
		    "psp_close: Unable to obtain state");
		return (ENXIO);
	}

	cmn_err(CE_NOTE, "Inside psp_close");
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}
	mutex_exit(&qsp->lock);
	return (DDI_SUCCESS);
}

struct psp_rpc_spec {
	uint32_t opcode;
};

static const struct psp_rpc_spec psp_rpc_specs[256] = {
	[PSP_GET_SUPPORTED_FEATURES & 0xFF] = {
		.opcode = 0x05,
	},
	[PSP_GET_VERSION & 0xFF] = {
		.opcode = 0x19,
	},
	[PSP_GET_SPI_BLOCK_SIZE & 0xFF] = {
		.opcode = 0x83,
	},
	[PSP_GET_KVM_INFO & 0xFF] = {
		.opcode = 0x1e,
	},
	[PSP_GET_HSTI_STATE & 0xFF] = {
		.opcode = 0x14,
	},
	[PSP_DISABLE_PSB & 0xFF] = {
		.opcode = 0x4a,
	},
	[PSP_POST_DONE & 0xFF] = {
		.opcode = 0x06,
	},
	[PSP_ABORT_COMMAND & 0xFF] = {
		.opcode = 0xfe,
	},
	[PSP_INJECT_RAS_ERROR & 0xFF] = {
		.opcode = 0x22,
	},
	[PSP_SET_ACPI_EINJ & 0xFF] = {
		.opcode = 0x41,
	},
	[PSP_FUSE_PSB & 0xFF] = {
		.opcode = 0x21,
	},
};

/* TODO: Do I have to set *rval = -1 on every return (ENXIO) etc? */
static int
psp_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred, int *rval)
{
	struct attached_state *qsp;
	const psp_param_t *response;
	uint32_t csr = 0;
	int status;

	if (((cmd >> 8) << 8) != PSPIOC) {
		return (ENXIO);
	}
	if ((qsp = ddi_get_soft_state(attached_state_head, getminor(dev))) ==
	    NULL) {
		return (ENXIO);
	}

	/* Require PRIV_SYS_RES_CONFIG, same as psradm */
	if (secpolicy_ponline(cred)) {
		return (EPERM);
	}

	*rval = 0;

	psp_config_t *conf = &qsp->psp_config;

	if (COMMAND_GET_RECOVERY(conf->mpc_rpc->mpmr_command)) {
		cmn_err(CE_WARN,
		    "psp_ioctl: In recovery mode--ignoring ioctl.");
		return (EIO);
	}

	/* Make sure that the previous command is done already */

	status = psp_await_rpc(conf);
	if (status) {
		cmn_err(CE_WARN,
		    "psp_ioctl: Dropped response of previous command "
		    "(status = %d)", status);
	}

	/* Prepare param */

	switch (cmd) {
		case PSP_ABORT_COMMAND:
			/*
			 * If the operation is still running, maybe it still
			 * needs the param.
			 */
			csr = psp_rpc_specs[cmd & 0xFF].opcode;
			break;
		default: {
			psp_param_config_t *param_config;
			param_config = &qsp->psp_config.mpc_mppc;
			const struct psp_rpc_spec *psp_rpc_spec =
			    &psp_rpc_specs[cmd & 0xFF];
			if (psp_rpc_spec->opcode == 0) {
				return (ENOTTY);
			} else {
				csr = psp_rpc_spec->opcode;
			}

			milan_psp_param_header_t header;
			if (ddi_copyin((void *)arg, &header, sizeof (header),
			    mode) != 0) {
				return (EFAULT);
			}
			uint32_t expected_size = header.mpph_total_size;
			if (expected_size > param_config->mppc_alloc_len) {
				return (EFBIG);
			}
			if (ddi_copyin((void *)arg, param_config->mppc_buffer,
			    expected_size, mode) != 0) {
				return (EFAULT);
			}
		}
	}

	/* Perform RPC */

	status = 0;
	uint32_t expected_size;
	switch (cmd) {
	case PSP_ABORT_COMMAND:
	case PSP_DISABLE_PSB:
	case PSP_POST_DONE:
	case PSP_INJECT_RAS_ERROR:
	case PSP_SET_ACPI_EINJ:
	case PSP_FUSE_PSB:
		if (!(mode & FWRITE)) {
			return (EACCES);
		}
		/* fallthrough */
	case PSP_GET_VERSION:
	case PSP_GET_HSTI_STATE:
	case PSP_GET_SUPPORTED_FEATURES: {
		if (!(mode & FREAD)) {
			return (EACCES);
		}
		expected_size =
		    conf->mpc_mppc.mppc_buffer->mpp_header.mpph_total_size;
		psp_start_rpc(&qsp->psp_config, csr);
		status = psp_await_rpc(&qsp->psp_config);
		if (status) {
			*rval = status;
			/*
			 * Read response and copy to user anyway.
			 */
		}
		break;
	}
	default:
		return (ENOTTY);
	}

	/* Read response */

	response = qsp->psp_config.mpc_mppc.mppc_buffer;
	if (response->mpp_header.mpph_total_size <
	    sizeof (response->mpp_header) ||
	    response->mpp_header.mpph_total_size != expected_size) {
		return (EFAULT);
	}
	if (ddi_copyout(response, (void *)arg, expected_size, mode) != 0) {
		return (EFAULT);
	}

	return (0);
}

static int
psp_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
    size_t *maplen, uint_t model)
{
	return (ENXIO);
}

static struct cb_ops cb_psp_ops = {
	.cb_open = psp_open,
	.cb_close = psp_close,
	.cb_strategy = nulldev,
	.cb_print = nulldev,
	.cb_dump = nodev,
	.cb_read = nodev,
	.cb_write = nodev,
	.cb_ioctl = psp_ioctl,
	.cb_devmap = psp_devmap,
	.cb_mmap = nodev,
	.cb_segmap = ddi_devmap_segmap,
	.cb_chpoll = nochpoll,
	.cb_prop_op = ddi_prop_op,
	.cb_str = NULL,
	.cb_flag = D_MP, /* D_NEW | D_MTSAFE, */
	.cb_rev = CB_REV,
	.cb_aread = nodev,
	.cb_awrite = nodev,
};

static struct dev_ops psp_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_getinfo = psp_info,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = psp_attach,
	.devo_detach = psp_detach,
	.devo_reset = nodev,
	.devo_cb_ops = &cb_psp_ops,
	.devo_bus_ops = NULL,
	.devo_power = NULL,
	.devo_quiesce = ddi_quiesce_not_needed,
};

static struct modldrv modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "PSP driver",
	.drv_dev_ops = &psp_ops,
};

static struct modlinkage modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = {
		&modldrv,
		NULL,
	},
};

int
_init(void)
{
	int e;
	cmn_err(CE_NOTE, "Inside psp _init");

	if ((e = ddi_soft_state_init(&attached_state_head,
	    sizeof (struct attached_state), 1)) != 0)
		return (e);

	e = mod_install(&modlinkage);
	return (e);
}

int
_fini(void)
{
	int e;
	cmn_err(CE_NOTE, "Inside psp _fini");
	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}
	ddi_soft_state_fini(&attached_state_head);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
