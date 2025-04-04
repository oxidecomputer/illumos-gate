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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright 2012 Garrett D'Amore <garrett@damore.org>.  All rights reserved.
 * Copyright 2019 Joyent, Inc.
 */

/*
 *	npe (Nexus PCIe driver): Host to PCI-Express local bus driver
 *
 *	npe serves as the driver for PCIe Root Complexes and as the nexus driver
 *	for PCIe devices. See also: npe(7D). For more information about hotplug,
 *	see the big theory statement at uts/common/os/ddi_hp_impl.c.
 *
 *
 *	NDI EVENT HANDLING SUPPORT
 *
 *	npe supports NDI event handling. The only available event is surprise
 *	removal of a device. Child drivers can register surprise removal event
 *	callbacks by requesting an event cookie using ddi_get_eventcookie for
 *	the DDI_DEVI_REMOVE_EVENT and add their callback using
 *	ddi_add_event_handler. For an example, see the nvme driver in
 *	uts/common/io/nvme/nvme.c.
 *
 *	The NDI events in npe are retrieved using NDI_EVENT_NOPASS, which
 *	prevent them from being propagated up the tree once they reach the npe's
 *	bus_get_eventcookie operations. This is important because npe maintains
 *	the state of PCIe devices and their receptacles, via the PCIe hotplug
 *	controller driver (pciehpc).
 *
 *	Hot removal events are ultimately posted by the PCIe hotplug controller
 *	interrupt handler for hotplug events. Events are posted using the
 *	ndi_post_event interface.
 */

#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/file.h>
#include <sys/pci_impl.h>
#include <sys/pcie_impl.h>
#include <sys/sysmacros.h>
#include <sys/ddi_intr.h>
#include <sys/sunndi.h>
#include <sys/sunddi.h>
#include <sys/ddifm.h>
#include <sys/ndifm.h>
#include <sys/fm/util.h>
#include <sys/hotplug/pci/pcie_hp.h>
#include <io/pci/pci_tools_ext.h>
#include <io/pci/pci_common.h>
#include <io/pciex/pcie_nvidia.h>

/*
 * Helper Macros
 */
#define	NPE_IS_HANDLE_FOR_STDCFG_ACC(hp) \
	((hp) != NULL &&						\
	((ddi_acc_hdl_t *)(hp))->ah_platform_private != NULL &&		\
	(((ddi_acc_impl_t *)((ddi_acc_hdl_t *)(hp))->			\
	ah_platform_private)->						\
	    ahi_acc_attr &(DDI_ACCATTR_CPU_VADDR|DDI_ACCATTR_CONFIG_SPACE)) \
		== DDI_ACCATTR_CONFIG_SPACE)

/*
 * Bus Operation functions
 */
static int	npe_bus_map(dev_info_t *, dev_info_t *, ddi_map_req_t *,
		    off_t, off_t, caddr_t *);
static int	npe_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
		    void *, void *);
static int	npe_intr_ops(dev_info_t *, dev_info_t *, ddi_intr_op_t,
		    ddi_intr_handle_impl_t *, void *);
static int	npe_fm_init(dev_info_t *, dev_info_t *, int,
		    ddi_iblock_cookie_t *);
static int	npe_bus_get_eventcookie(dev_info_t *, dev_info_t *, char *,
		    ddi_eventcookie_t *);
static int	npe_bus_add_eventcall(dev_info_t *, dev_info_t *,
		    ddi_eventcookie_t, void (*)(dev_info_t *,
		    ddi_eventcookie_t, void *, void *),
		    void *, ddi_callback_id_t *);
static int	npe_bus_remove_eventcall(dev_info_t *, ddi_callback_id_t);
static int	npe_bus_post_event(dev_info_t *, dev_info_t *,
		    ddi_eventcookie_t, void *);

static int	npe_fm_callback(dev_info_t *, ddi_fm_error_t *, const void *);

/*
 * These chicken switches can be set to disable various error events.  We don't
 * want to mask any of them on this architecture but these can be used to do so.
 */
uint32_t	npe_aer_uce_mask = 0;
uint32_t	npe_aer_ce_mask = 0;
uint32_t	npe_aer_suce_mask = 0;

struct bus_ops npe_bus_ops = {
	BUSO_REV,
	npe_bus_map,
	NULL,
	NULL,
	NULL,
	i_ddi_map_fault,
	NULL,
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	ddi_dma_mctl,
	npe_ctlops,
	ddi_bus_prop_op,
	npe_bus_get_eventcookie,
	npe_bus_add_eventcall,
	npe_bus_remove_eventcall,
	npe_bus_post_event,
	0,			/* (*bus_intr_ctl)(); */
	0,			/* (*bus_config)(); */
	0,			/* (*bus_unconfig)(); */
	npe_fm_init,		/* (*bus_fm_init)(); */
	NULL,			/* (*bus_fm_fini)(); */
	NULL,			/* (*bus_fm_access_enter)(); */
	NULL,			/* (*bus_fm_access_exit)(); */
	NULL,			/* (*bus_power)(); */
	npe_intr_ops,		/* (*bus_intr_op)(); */
	pcie_hp_common_ops	/* (*bus_hp_op)(); */
};

static int	npe_open(dev_t *, int, int, cred_t *);
static int	npe_close(dev_t, int, int, cred_t *);
static int	npe_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);

struct cb_ops npe_cb_ops = {
	npe_open,			/* open */
	npe_close,			/* close */
	nodev,				/* strategy */
	nodev,				/* print */
	nodev,				/* dump */
	nodev,				/* read */
	nodev,				/* write */
	npe_ioctl,			/* ioctl */
	nodev,				/* devmap */
	nodev,				/* mmap */
	nodev,				/* segmap */
	nochpoll,			/* poll */
	pcie_prop_op,			/* cb_prop_op */
	NULL,				/* streamtab */
	D_NEW | D_MP | D_HOTPLUG,	/* Driver compatibility flag */
	CB_REV,				/* rev */
	nodev,				/* int (*cb_aread)() */
	nodev				/* int (*cb_awrite)() */
};


/*
 * Device Node Operation functions
 */
static int	npe_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int	npe_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int	npe_info(dev_info_t *, ddi_info_cmd_t, void *, void **);

struct dev_ops npe_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt  */
	npe_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	npe_attach,		/* attach */
	npe_detach,		/* detach */
	nulldev,		/* reset */
	&npe_cb_ops,		/* driver operations */
	&npe_bus_ops,		/* bus operations */
	NULL,			/* power */
	ddi_quiesce_not_needed,		/* quiesce */
};

/*
 * Internal routines in support of particular npe_ctlops.
 */
static int npe_removechild(dev_info_t *child);
static int npe_initchild(dev_info_t *child);

/*
 * External support routine
 */
extern void	npe_ck804_fix_aer_ptr(ddi_acc_handle_t cfg_hdl);
extern int	npe_disable_empty_bridges_workaround(dev_info_t *child);
extern void	npe_nvidia_error_workaround(ddi_acc_handle_t cfg_hdl);
extern void	npe_intel_error_workaround(ddi_acc_handle_t cfg_hdl);
extern void	npe_enable_htmsi_children(dev_info_t *dip);
extern int	npe_save_htconfig_children(dev_info_t *dip);
extern int	npe_restore_htconfig_children(dev_info_t *dip);

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops,				/* Type of module */
	"Host to PCIe nexus driver",		/* Name of module */
	&npe_ops,				/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

/* Save minimal state. */
void *npe_statep;

int
_init(void)
{
	int e;

	/*
	 * Initialize per-pci bus soft state pointer.
	 */
	e = ddi_soft_state_init(&npe_statep, sizeof (pci_state_t), 1);
	if (e != 0)
		return (e);

	if ((e = mod_install(&modlinkage)) != 0)
		ddi_soft_state_fini(&npe_statep);

	return (e);
}


int
_fini(void)
{
	int rc;

	rc = mod_remove(&modlinkage);
	if (rc != 0)
		return (rc);

	ddi_soft_state_fini(&npe_statep);
	return (rc);
}


int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
npe_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	minor_t		minor = getminor((dev_t)arg);
	int		instance = PCI_MINOR_NUM_TO_INSTANCE(minor);
	pci_state_t	*pcip = ddi_get_soft_state(npe_statep, instance);
	int		ret = DDI_SUCCESS;

	switch (cmd) {
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(intptr_t)instance;
		break;
	case DDI_INFO_DEVT2DEVINFO:
		if (pcip == NULL) {
			ret = DDI_FAILURE;
			break;
		}

		*result = (void *)pcip->pci_dip;
		break;
	default:
		ret = DDI_FAILURE;
		break;
	}

	return (ret);
}

/*
 * See big theory statement at the top of this file for more information about
 * surprise removal events.
 */
#define	NPE_EVENT_TAG_HOT_REMOVAL	0
static ndi_event_definition_t npe_ndi_event_defs[1] = {
	{NPE_EVENT_TAG_HOT_REMOVAL, DDI_DEVI_REMOVE_EVENT, EPL_KERNEL,
	NDI_EVENT_POST_TO_ALL}
};

static ndi_event_set_t npe_ndi_events = {
	NDI_EVENTS_REV1, ARRAY_SIZE(npe_ndi_event_defs), npe_ndi_event_defs
};

/*ARGSUSED*/
static int
npe_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int		instance = ddi_get_instance(devi);
	pci_state_t	*pcip = NULL;
	int		ret;

	if (cmd == DDI_RESUME) {
		/*
		 * the system might still be able to resume even if this fails
		 */
		(void) npe_restore_htconfig_children(devi);
		return (DDI_SUCCESS);
	}

	/*
	 * We must do this here in order to ensure that all top level devices
	 * get their HyperTransport MSI mapping regs programmed first.
	 * "Memory controller" and "hostbridge" class devices are leaf devices
	 * that may affect MSI translation functionality for devices
	 * connected to the same link/bus.
	 *
	 * This will also program HT MSI mapping registers on root buses
	 * devices (basically sitting on an HT bus) that are not dependent
	 * on the aforementioned HT devices for MSI translation.
	 */
	npe_enable_htmsi_children(devi);

	if (ddi_prop_update_string(DDI_DEV_T_NONE, devi, "device_type",
	    "pciex") != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "npe:  'device_type' prop create failed");
	}

	if (ddi_soft_state_zalloc(npe_statep, instance) == DDI_SUCCESS)
		pcip = ddi_get_soft_state(npe_statep, instance);

	if (pcip == NULL)
		return (DDI_FAILURE);

	pcip->pci_dip = devi;
	pcip->pci_soft_state = PCI_SOFT_STATE_CLOSED;

	if (pcie_init(devi, NULL) != DDI_SUCCESS)
		goto fail1;

	ret = ndi_event_alloc_hdl(pcip->pci_dip, NULL, &pcip->pci_ndi_event_hdl,
	    NDI_SLEEP);
	if (ret == NDI_SUCCESS) {
		ret = ndi_event_bind_set(pcip->pci_ndi_event_hdl,
		    &npe_ndi_events, NDI_SLEEP);
		if (ret != NDI_SUCCESS) {
			dev_err(pcip->pci_dip, CE_WARN, "npe:	failed to bind "
			    "NDI event set (error=%d)", ret);
			goto fail1;
		}
	} else {
		dev_err(pcip->pci_dip, CE_WARN, "npe:	failed to allocate "
		    "event handle (error=%d)", ret);
		goto fail1;
	}

	/* Second arg: initialize for pci_express root nexus */
	if (pcitool_init(devi, B_TRUE) != DDI_SUCCESS)
		goto fail2;

	pcip->pci_fmcap = DDI_FM_EREPORT_CAPABLE | DDI_FM_ERRCB_CAPABLE |
	    DDI_FM_ACCCHK_CAPABLE | DDI_FM_DMACHK_CAPABLE;
	ddi_fm_init(devi, &pcip->pci_fmcap, &pcip->pci_fm_ibc);

	if (pcip->pci_fmcap & DDI_FM_ERRCB_CAPABLE) {
		ddi_fm_handler_register(devi, npe_fm_callback, NULL);
	}

	PCIE_DIP2PFD(devi) = kmem_zalloc(sizeof (pf_data_t), KM_SLEEP);
	pcie_rc_init_pfd(devi, PCIE_DIP2PFD(devi));

	ddi_report_dev(devi);
	pcie_fab_init_bus(devi, PCIE_BUS_FINAL);

	return (DDI_SUCCESS);

fail2:
	(void) pcie_uninit(devi);
fail1:
	pcie_rc_fini_bus(devi);
	ddi_soft_state_free(npe_statep, instance);

	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
npe_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance = ddi_get_instance(devi);
	pci_state_t *pcip;
	int ret;

	pcip = ddi_get_soft_state(npe_statep, ddi_get_instance(devi));

	switch (cmd) {
	case DDI_DETACH:

		/*
		 * Clean up event handling first, to ensure there are no
		 * oustanding callbacks registered.
		 */
		ret = ndi_event_unbind_set(pcip->pci_ndi_event_hdl,
		    &npe_ndi_events, NDI_SLEEP);
		if (ret == NDI_SUCCESS) {
			/* ndi_event_free_hdl always succeeds. */
			(void) ndi_event_free_hdl(pcip->pci_ndi_event_hdl);
		} else {
			/*
			 * The event set will only fail to unbind if there are
			 * outstanding callbacks registered for it, which
			 * probably means a child driver still has one
			 * registered and thus was not cleaned up properly
			 * before npe's detach routine was called. Consequently,
			 * we should fail the detach here.
			 */
			dev_err(pcip->pci_dip, CE_WARN, "npe:	failed to "
			    "unbind NDI event set (error=%d)", ret);
			return (DDI_FAILURE);
		}

		pcie_fab_fini_bus(devi, PCIE_BUS_INITIAL);

		/* Uninitialize pcitool support. */
		pcitool_uninit(devi);

		if (pcie_uninit(devi) != DDI_SUCCESS)
			return (DDI_FAILURE);

		if (pcip->pci_fmcap & DDI_FM_ERRCB_CAPABLE)
			ddi_fm_handler_unregister(devi);

		pcie_rc_fini_pfd(PCIE_DIP2PFD(devi));
		kmem_free(PCIE_DIP2PFD(devi), sizeof (pf_data_t));

		ddi_fm_fini(devi);
		ddi_soft_state_free(npe_statep, instance);

		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		/*
		 * the system might still be able to suspend/resume even if
		 * this fails
		 */
		(void) npe_save_htconfig_children(devi);
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
}

/*
 * Configure the access handle for standard configuration space
 * access (see pci_fm_acc_setup for code that initializes the
 * access-function pointers).
 */
static int
npe_setup_std_pcicfg_acc(dev_info_t *rdip, ddi_map_req_t *mp,
    ddi_acc_hdl_t *hp, off_t offset, off_t len)
{
	int ret;

	if ((ret = pci_fm_acc_setup(hp, offset, len)) ==
	    DDI_SUCCESS) {
		if (DDI_FM_ACC_ERR_CAP(ddi_fm_capable(rdip)) &&
		    mp->map_handlep->ah_acc.devacc_attr_access
		    != DDI_DEFAULT_ACC) {
			ndi_fmc_insert(rdip, ACC_HANDLE,
			    (void *)mp->map_handlep, NULL);
		}
	}
	return (ret);
}

static int
npe_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp)
{
	int		rnumber;
	int		space;
	ddi_acc_hdl_t	*hp;
	ddi_map_req_t	mr;
	pci_regspec_t	pci_reg;
	pci_regspec_t	*pci_rp;
	struct regspec64 reg;
	pci_acc_cfblk_t	*cfp;
	int		retval;
	uint_t		nelem;
	uint64_t	pci_rlength;

	mr = *mp; /* Get private copy of request */
	mp = &mr;

	/*
	 * check for register number
	 */
	switch (mp->map_type) {
	case DDI_MT_REGSPEC:
		pci_reg = *(pci_regspec_t *)(mp->map_obj.rp);
		pci_rp = &pci_reg;
		if (pci_common_get_reg_prop(rdip, pci_rp) != DDI_SUCCESS)
			return (DDI_FAILURE);
		break;
	case DDI_MT_RNUMBER:
		rnumber = mp->map_obj.rnumber;
		/*
		 * get ALL "reg" properties for dip, select the one of
		 * of interest. In x86, "assigned-addresses" property
		 * is identical to the "reg" property, so there is no
		 * need to cross check the two to determine the physical
		 * address of the registers.
		 * This routine still performs some validity checks to
		 * make sure that everything is okay.
		 */
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "reg", (int **)&pci_rp, &nelem) !=
		    DDI_PROP_SUCCESS)
			return (DDI_FAILURE);

		/*
		 * validate the register number.
		 */
		nelem /= (sizeof (pci_regspec_t) / sizeof (int));
		if (rnumber >= nelem) {
			ddi_prop_free(pci_rp);
			return (DDI_FAILURE);
		}

		/*
		 * copy the required entry.
		 */
		pci_reg = pci_rp[rnumber];

		/*
		 * free the memory allocated by ddi_prop_lookup_int_array
		 */
		ddi_prop_free(pci_rp);

		pci_rp = &pci_reg;
		if (pci_common_get_reg_prop(rdip, pci_rp) != DDI_SUCCESS)
			return (DDI_FAILURE);
		mp->map_type = DDI_MT_REGSPEC;
		break;
	default:
		return (DDI_ME_INVAL);
	}

	space = pci_rp->pci_phys_hi & PCI_REG_ADDR_M;

	/*
	 * check for unmap and unlock of address space
	 */
	if ((mp->map_op == DDI_MO_UNMAP) || (mp->map_op == DDI_MO_UNLOCK)) {
		switch (space) {
		case PCI_ADDR_IO:
			reg.regspec_bustype = 1;
			break;

		case PCI_ADDR_CONFIG:
			/*
			 * We support only standard mappings for config space,
			 * so no CPU_VADDR mappings should ever get here.
			 */
			ASSERT(NPE_IS_HANDLE_FOR_STDCFG_ACC(mp->map_handlep));

			if (DDI_FM_ACC_ERR_CAP(ddi_fm_capable(rdip)) &&
			    mp->map_handlep->ah_acc.devacc_attr_access
			    != DDI_DEFAULT_ACC) {
				ndi_fmc_remove(rdip, ACC_HANDLE,
				    (void *)mp->map_handlep);
			}

			return (DDI_SUCCESS);

		case PCI_ADDR_MEM64:
		case PCI_ADDR_MEM32:
			reg.regspec_bustype = 0;
			break;

		default:
			return (DDI_FAILURE);
		}

		reg.regspec_addr = (uint64_t)pci_rp->pci_phys_mid << 32 |
		    (uint64_t)pci_rp->pci_phys_low;
		reg.regspec_size = (uint64_t)pci_rp->pci_size_hi << 32 |
		    (uint64_t)pci_rp->pci_size_low;

		/*
		 * Adjust offset and length
		 * A non-zero length means override the one in the regspec.
		 */
		if (reg.regspec_addr + offset < MAX(reg.regspec_addr, offset))
			return (DDI_FAILURE);
		reg.regspec_addr += offset;
		if (len != 0)
			reg.regspec_size = len;

		mp->map_obj.rp = (struct regspec *)&reg;
		mp->map_flags |= DDI_MF_EXT_REGSPEC;
		retval = ddi_map(dip, mp, (off_t)0, (off_t)0, vaddrp);
		if (DDI_FM_ACC_ERR_CAP(ddi_fm_capable(rdip)) &&
		    mp->map_handlep->ah_acc.devacc_attr_access !=
		    DDI_DEFAULT_ACC) {
			ndi_fmc_remove(rdip, ACC_HANDLE,
			    (void *)mp->map_handlep);
		}
		return (retval);

	}

	/* check for user mapping request - not legal for Config */
	if (space == PCI_ADDR_CONFIG &&
	    (mp->map_op == DDI_MO_MAP_HANDLE ||
	    (mp->map_flags & DDI_MF_DEVICE_MAPPING) != 0)) {
		cmn_err(CE_NOTE, "npe: Config mapping request from user\n");
		return (DDI_FAILURE);
	}


	/*
	 * Note that pci_fm_acc_setup() is called to serve two purposes
	 * i) enable legacy PCI I/O style config space access
	 * ii) register with FMA
	 */
	if (space == PCI_ADDR_CONFIG) {

		/* Can't map config space without a handle */
		hp = (ddi_acc_hdl_t *)mp->map_handlep;
		if (hp == NULL)
			return (DDI_FAILURE);

		/* record the device address for future reference */
		cfp = (pci_acc_cfblk_t *)&hp->ah_bus_private;
		cfp->c_busnum = PCI_REG_BUS_G(pci_rp->pci_phys_hi);
		cfp->c_devnum = PCI_REG_DEV_G(pci_rp->pci_phys_hi);
		cfp->c_funcnum = PCI_REG_FUNC_G(pci_rp->pci_phys_hi);

		/*
		 * Range checks are done by code we're about to call.
		 */
		*vaddrp = (caddr_t)offset;

		return (npe_setup_std_pcicfg_acc(rdip, mp, hp, offset, len));
	}

	/*
	 * range check
	 */
	pci_rlength = (uint64_t)pci_rp->pci_size_low |
	    (uint64_t)pci_rp->pci_size_hi << 32;
	if ((offset >= pci_rlength) || (len > pci_rlength) ||
	    (offset + len > pci_rlength) || (offset + len < MAX(offset, len))) {
		return (DDI_FAILURE);
	}

	/*
	 * convert the pci regsec into the generic regspec used by the
	 * parent root nexus driver.
	 */
	switch (space) {
	case PCI_ADDR_IO:
		reg.regspec_bustype = 1;
		break;
	case PCI_ADDR_MEM64:
	case PCI_ADDR_MEM32:
		reg.regspec_bustype = 0;
		break;
	default:
		return (DDI_FAILURE);
	}

	reg.regspec_addr = (uint64_t)pci_rp->pci_phys_mid << 32 |
	    (uint64_t)pci_rp->pci_phys_low;
	reg.regspec_size = pci_rlength;

	/*
	 * Adjust offset and length
	 * A non-zero length means override the one in the regspec.
	 */
	if (reg.regspec_addr + offset < MAX(reg.regspec_addr, offset))
		return (DDI_FAILURE);
	reg.regspec_addr += offset;
	if (len != 0)
		reg.regspec_size = len;


	mp->map_obj.rp = (struct regspec *)&reg;
	mp->map_flags |= DDI_MF_EXT_REGSPEC;
	retval = ddi_map(dip, mp, (off_t)0, (off_t)0, vaddrp);
	if (retval == DDI_SUCCESS) {
		ASSERT3S(space, !=, PCI_ADDR_CONFIG);

		if (DDI_FM_ACC_ERR_CAP(ddi_fm_capable(rdip)) &&
		    mp->map_handlep->ah_acc.devacc_attr_access !=
		    DDI_DEFAULT_ACC) {
			ndi_fmc_insert(rdip, ACC_HANDLE,
			    (void *)mp->map_handlep, NULL);
		}
	}
	return (retval);
}



/*ARGSUSED*/
static int
npe_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	int		totreg;
	uint_t		reglen;
	pci_regspec_t	*drv_regp;
	struct attachspec *asp;
	struct detachspec *dsp;
	pci_state_t	*pci_p = ddi_get_soft_state(npe_statep,
	    ddi_get_instance(dip));

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);
		cmn_err(CE_CONT, "?PCI Express-device: %s@%s, %s%d\n",
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    ddi_driver_name(rdip), ddi_get_instance(rdip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		return (npe_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		return (npe_removechild((dev_info_t *)arg));

	case DDI_CTLOPS_SIDDEV:
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);

		*(int *)result = 0;
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "reg", (int **)&drv_regp,
		    &reglen) != DDI_PROP_SUCCESS) {
			return (DDI_FAILURE);
		}

		totreg = (reglen * sizeof (int)) / sizeof (pci_regspec_t);
		if (ctlop == DDI_CTLOPS_NREGS)
			*(int *)result = totreg;
		else if (ctlop == DDI_CTLOPS_REGSIZE) {
			uint64_t val;
			int rn;

			rn = *(int *)arg;
			if (rn >= totreg) {
				ddi_prop_free(drv_regp);
				return (DDI_FAILURE);
			}
			val = drv_regp[rn].pci_size_low |
			    (uint64_t)drv_regp[rn].pci_size_hi << 32;
			if (val > OFF_MAX) {
				int ce = CE_NOTE;
#ifdef DEBUG
				ce = CE_WARN;
#endif
				dev_err(rdip, ce, "failed to get register "
				    "size, value larger than OFF_MAX: 0x%"
				    PRIx64 "\n", val);
				return (DDI_FAILURE);
			}
			*(off_t *)result = (off_t)val;
		}
		ddi_prop_free(drv_regp);

		return (DDI_SUCCESS);

	case DDI_CTLOPS_POWER:
	{
		power_req_t	*reqp = (power_req_t *)arg;
		/*
		 * We currently understand reporting of PCI_PM_IDLESPEED
		 * capability. Everything else is passed up.
		 */
		if ((reqp->request_type == PMR_REPORT_PMCAP) &&
		    (reqp->req.report_pmcap_req.cap ==  PCI_PM_IDLESPEED))
			return (DDI_SUCCESS);

		break;
	}

	case DDI_CTLOPS_PEEK:
	case DDI_CTLOPS_POKE:
		return (pci_common_peekpoke(dip, rdip, ctlop, arg, result));

	/* X86 systems support PME wakeup from suspended state */
	case DDI_CTLOPS_ATTACH:
		if (!pcie_is_child(dip, rdip))
			return (DDI_SUCCESS);

		asp = (struct attachspec *)arg;
		if ((asp->when == DDI_POST) && (asp->result == DDI_SUCCESS)) {
			pf_init(rdip, (void *)pci_p->pci_fm_ibc, asp->cmd);
			(void) pcie_postattach_child(rdip);
		}

		/* only do this for immediate children */
		if (asp->cmd == DDI_RESUME && asp->when == DDI_PRE &&
		    ddi_get_parent(rdip) == dip)
			if (pci_pre_resume(rdip) != DDI_SUCCESS) {
				/* Not good, better stop now. */
				cmn_err(CE_PANIC,
				    "Couldn't pre-resume device %p",
				    (void *) dip);
				/* NOTREACHED */
			}

		return (DDI_SUCCESS);

	case DDI_CTLOPS_DETACH:
		if (!pcie_is_child(dip, rdip))
			return (DDI_SUCCESS);

		dsp = (struct detachspec *)arg;

		if (dsp->when == DDI_PRE)
			pf_fini(rdip, dsp->cmd);

		/* only do this for immediate children */
		if (dsp->cmd == DDI_SUSPEND && dsp->when == DDI_POST &&
		    ddi_get_parent(rdip) == dip)
			if (pci_post_suspend(rdip) != DDI_SUCCESS)
				return (DDI_FAILURE);

		return (DDI_SUCCESS);

	default:
		break;
	}

	return (ddi_ctlops(dip, rdip, ctlop, arg, result));

}


/*
 * npe_intr_ops
 */
static int
npe_intr_ops(dev_info_t *pdip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result)
{
	return (pci_common_intr_ops(pdip, rdip, intr_op, hdlp, result));
}


static int
npe_initchild(dev_info_t *child)
{
	char		name[80];
	pcie_bus_t	*bus_p;
	uint32_t	regs;
	ddi_acc_handle_t	cfg_hdl;

	/*
	 * Do not bind drivers to empty bridges.
	 * Fail above, if the bridge is found to be hotplug capable
	 */
	if (npe_disable_empty_bridges_workaround(child) == 1)
		return (DDI_FAILURE);

	if (pci_common_name_child(child, name, 80) != DDI_SUCCESS)
		return (DDI_FAILURE);

	ddi_set_name_addr(child, name);

	/*
	 * Pseudo nodes indicate a prototype node with per-instance
	 * properties to be merged into the real h/w device node.
	 * The interpretation of the unit-address is DD[,F]
	 * where DD is the device id and F is the function.
	 */
	if (ndi_dev_is_persistent_node(child) == 0) {
		extern int pci_allow_pseudo_children;

		ddi_set_parent_data(child, NULL);

		/*
		 * Try to merge the properties from this prototype
		 * node into real h/w nodes.
		 */
		if (ndi_merge_node(child, pci_common_name_child) ==
		    DDI_SUCCESS) {
			/*
			 * Merged ok - return failure to remove the node.
			 */
			ddi_set_name_addr(child, NULL);
			return (DDI_FAILURE);
		}

		/* workaround for DDIVS to run under PCI Express */
		if (pci_allow_pseudo_children) {
			/*
			 * If the "interrupts" property doesn't exist,
			 * this must be the ddivs no-intr case, and it returns
			 * DDI_SUCCESS instead of DDI_FAILURE.
			 */
			if (ddi_prop_get_int(DDI_DEV_T_ANY, child,
			    DDI_PROP_DONTPASS, "interrupts", -1) == -1)
				return (DDI_SUCCESS);
			/*
			 * Create the ddi_parent_private_data for a pseudo
			 * child.
			 */
			pci_common_set_parent_private_data(child);
			return (DDI_SUCCESS);
		}

		/*
		 * The child was not merged into a h/w node,
		 * but there's not much we can do with it other
		 * than return failure to cause the node to be removed.
		 */
		cmn_err(CE_WARN, "!%s@%s: %s.conf properties not merged",
		    ddi_get_name(child), ddi_get_name_addr(child),
		    ddi_get_name(child));
		ddi_set_name_addr(child, NULL);
		return (DDI_NOT_WELL_FORMED);
	}

	if (ddi_prop_get_int(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "interrupts", -1) != -1)
		pci_common_set_parent_private_data(child);
	else
		ddi_set_parent_data(child, NULL);

	/* Optionally disable certain errors for debugging */
	regs = pcie_get_aer_uce_mask() | npe_aer_uce_mask;
	pcie_set_aer_uce_mask(regs);
	/*
	 * If URs are masked, mask SERRs as well, otherwise the system will
	 * still be notified of URs
	 */
	if (regs & PCIE_AER_UCE_UR)
		pcie_set_serr_mask(1);

	regs = pcie_get_aer_ce_mask() | npe_aer_ce_mask;
	pcie_set_aer_ce_mask(regs);
	regs = pcie_get_aer_suce_mask() | npe_aer_suce_mask;
	pcie_set_aer_suce_mask(regs);

	if (pci_config_setup(child, &cfg_hdl) == DDI_SUCCESS) {
		npe_ck804_fix_aer_ptr(cfg_hdl);
		npe_nvidia_error_workaround(cfg_hdl);
		npe_intel_error_workaround(cfg_hdl);
		pci_config_teardown(&cfg_hdl);
	}

	bus_p = PCIE_DIP2BUS(child);
	if (bus_p) {
		uint16_t device_id = (uint16_t)(bus_p->bus_dev_ven_id >> 16);
		uint16_t vendor_id = (uint16_t)(bus_p->bus_dev_ven_id & 0xFFFF);
		uint16_t rev_id = bus_p->bus_rev_id;

		/* Disable AER for certain NVIDIA Chipsets */
		if ((vendor_id == NVIDIA_VENDOR_ID) &&
		    (device_id == NVIDIA_CK804_DEVICE_ID) &&
		    (rev_id < NVIDIA_CK804_AER_VALID_REVID))
			bus_p->bus_aer_off = 0;

		pcie_init_dom(child);
		(void) pcie_initchild(child);
	}

	return (DDI_SUCCESS);
}


static int
npe_removechild(dev_info_t *dip)
{
	pcie_uninitchild(dip);

	ddi_set_name_addr(dip, NULL);

	/*
	 * Strip the node to properly convert it back to prototype form
	 */
	ddi_remove_minor_node(dip, NULL);

	ddi_prop_remove_all(dip);

	return (DDI_SUCCESS);
}

static int
npe_open(dev_t *devp, int flags, int otyp, cred_t *credp)
{
	minor_t		minor = getminor(*devp);
	int		instance = PCI_MINOR_NUM_TO_INSTANCE(minor);
	pci_state_t	*pci_p = ddi_get_soft_state(npe_statep, instance);
	int	rv;

	/*
	 * Make sure the open is for the right file type.
	 */
	if (otyp != OTYP_CHR)
		return (EINVAL);

	if (pci_p == NULL)
		return (ENXIO);

	mutex_enter(&pci_p->pci_mutex);
	switch (PCI_MINOR_NUM_TO_PCI_DEVNUM(minor)) {
	case PCI_TOOL_REG_MINOR_NUM:
	case PCI_TOOL_INTR_MINOR_NUM:
		break;
	default:
		/* Handle devctl ioctls */
		rv = pcie_open(pci_p->pci_dip, devp, flags, otyp, credp);
		mutex_exit(&pci_p->pci_mutex);
		return (rv);
	}

	/* Handle pcitool ioctls */
	if (flags & FEXCL) {
		if (pci_p->pci_soft_state != PCI_SOFT_STATE_CLOSED) {
			mutex_exit(&pci_p->pci_mutex);
			cmn_err(CE_NOTE, "npe_open: busy");
			return (EBUSY);
		}
		pci_p->pci_soft_state = PCI_SOFT_STATE_OPEN_EXCL;
	} else {
		if (pci_p->pci_soft_state == PCI_SOFT_STATE_OPEN_EXCL) {
			mutex_exit(&pci_p->pci_mutex);
			cmn_err(CE_NOTE, "npe_open: busy");
			return (EBUSY);
		}
		pci_p->pci_soft_state = PCI_SOFT_STATE_OPEN;
	}
	mutex_exit(&pci_p->pci_mutex);

	return (0);
}

static int
npe_close(dev_t dev, int flags, int otyp, cred_t *credp)
{
	minor_t		minor = getminor(dev);
	int		instance = PCI_MINOR_NUM_TO_INSTANCE(minor);
	pci_state_t	*pci_p = ddi_get_soft_state(npe_statep, instance);
	int	rv;

	if (pci_p == NULL)
		return (ENXIO);

	mutex_enter(&pci_p->pci_mutex);

	switch (PCI_MINOR_NUM_TO_PCI_DEVNUM(minor)) {
	case PCI_TOOL_REG_MINOR_NUM:
	case PCI_TOOL_INTR_MINOR_NUM:
		break;
	default:
		/* Handle devctl ioctls */
		rv = pcie_close(pci_p->pci_dip, dev, flags, otyp, credp);
		mutex_exit(&pci_p->pci_mutex);
		return (rv);
	}

	/* Handle pcitool ioctls */
	pci_p->pci_soft_state = PCI_SOFT_STATE_CLOSED;
	mutex_exit(&pci_p->pci_mutex);
	return (0);
}

static int
npe_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp, int *rvalp)
{
	minor_t		minor = getminor(dev);
	int		instance = PCI_MINOR_NUM_TO_INSTANCE(minor);
	pci_state_t	*pci_p = ddi_get_soft_state(npe_statep, instance);
	int		ret = ENOTTY;

	if (pci_p == NULL)
		return (ENXIO);

	switch (PCI_MINOR_NUM_TO_PCI_DEVNUM(minor)) {
	case PCI_TOOL_REG_MINOR_NUM:
	case PCI_TOOL_INTR_MINOR_NUM:
		/* To handle pcitool related ioctls */
		ret =  pci_common_ioctl(pci_p->pci_dip, dev, cmd, arg, mode,
		    credp, rvalp);
		break;
	default:
		/* To handle devctl and hotplug related ioctls */
		ret = pcie_ioctl(pci_p->pci_dip, dev, cmd, arg, mode, credp,
		    rvalp);
		break;
	}

	return (ret);
}

/*ARGSUSED*/
static int
npe_fm_init(dev_info_t *dip, dev_info_t *tdip, int cap,
    ddi_iblock_cookie_t *ibc)
{
	pci_state_t  *pcip = ddi_get_soft_state(npe_statep,
	    ddi_get_instance(dip));

	ASSERT(ibc != NULL);
	*ibc = pcip->pci_fm_ibc;

	return (pcip->pci_fmcap);
}

static int
npe_bus_get_eventcookie(dev_info_t *dip, dev_info_t *rdip, char *eventname,
    ddi_eventcookie_t *cookiep)
{
	pci_state_t *pcip = ddi_get_soft_state(npe_statep,
	    ddi_get_instance(dip));

	return (ndi_event_retrieve_cookie(pcip->pci_ndi_event_hdl, rdip,
	    eventname, cookiep, NDI_EVENT_NOPASS));
}

static int
npe_bus_add_eventcall(dev_info_t *dip, dev_info_t *rdip,
    ddi_eventcookie_t cookie, void (*callback)(dev_info_t *dip,
    ddi_eventcookie_t cookie, void *arg, void *bus_impldata),
    void *arg, ddi_callback_id_t *cb_id)
{
	pci_state_t *pcip = ddi_get_soft_state(npe_statep,
	    ddi_get_instance(dip));

	return (ndi_event_add_callback(pcip->pci_ndi_event_hdl, rdip, cookie,
	    callback, arg, NDI_SLEEP, cb_id));
}

static int
npe_bus_remove_eventcall(dev_info_t *dip, ddi_callback_id_t cb_id)
{
	pci_state_t *pcip = ddi_get_soft_state(npe_statep,
	    ddi_get_instance(dip));
	return (ndi_event_remove_callback(pcip->pci_ndi_event_hdl, cb_id));
}

static int
npe_bus_post_event(dev_info_t *dip, dev_info_t *rdip,
    ddi_eventcookie_t cookie, void *impl_data)
{
	pci_state_t *pcip = ddi_get_soft_state(npe_statep,
	    ddi_get_instance(dip));
	return (ndi_event_do_callback(pcip->pci_ndi_event_hdl, rdip, cookie,
	    impl_data));

}

/*ARGSUSED*/
static int
npe_fm_callback(dev_info_t *dip, ddi_fm_error_t *derr, const void *no_used)
{
	/*
	 * On current x86 systems, npe's callback does not get called for failed
	 * loads.  If in the future this feature is used, the fault PA should be
	 * logged in the derr->fme_bus_specific field.  The appropriate PCIe
	 * error handling code should be called and needs to be coordinated with
	 * safe access handling.
	 */

	return (DDI_FM_OK);
}
