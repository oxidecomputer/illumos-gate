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
 * Provides microarchitecture-independent access to the SMN (system management
 * network) and accessors that allow common parts of the Oxide architecture
 * kernel to access specific parts such as the IOMS, CCD, IO die, etc, via SMN.
 */

#include <sys/types.h>
#include <sys/boot_debug.h>
#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/pci_cfgspace.h>
#include <sys/pci_impl.h>
#include <sys/promif.h>
#include <sys/apic_common.h>

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>


/*
 * Variable to let us dump all SMN traffic while still developing.
 */
int zen_smn_log = 0;

/*
 * Output a formatted log message if `zen_smn_log != 0` with the appropriate
 * routine.  `cmn_err(9F)` is usually fine to call from an interrupt context
 * as it will avoid kmem allocations, adaptive mutexes and the like.  However,
 * an NMI does not go through the usual interrupt handlers and thus isn't
 * detectable as such.  Furthermore, it still wouldn't be safe in an NMI context
 * even if treated as a high-level interrupt as it would still attempt to take
 * spinlocks potentially held by the code preempted by the NMI.  We fall back
 * to prom_printf in that case.
 */
#define	ZEN_SMN_LOG(fmt, ...) do { \
	if (zen_smn_log == 0) \
		break; \
	if (apix_nmi_in_progress()) \
		prom_printf(fmt "\n", __VA_ARGS__); \
	else \
		cmn_err(CE_NOTE, fmt, __VA_ARGS__); \
} while (0)


uint32_t
zen_core_read(zen_core_t *core, const smn_reg_t reg)
{
	return (zen_smn_read(core->zc_ccx->zcx_ccd->zcd_iodie, reg));
}

void
zen_core_write(zen_core_t *core, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(core->zc_ccx->zcx_ccd->zcd_iodie, reg, val);
}

uint32_t
zen_ccd_read(zen_ccd_t *ccd, const smn_reg_t reg)
{
	return (zen_smn_read(ccd->zcd_iodie, reg));
}

void
zen_ccd_write(zen_ccd_t *ccd, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(ccd->zcd_iodie, reg, val);
}

uint32_t
zen_ioms_read(zen_ioms_t *ioms, const smn_reg_t reg)
{
	return (zen_smn_read(ioms->zio_nbio->zn_iodie, reg));
}

void
zen_ioms_write(zen_ioms_t *ioms, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(ioms->zio_nbio->zn_iodie, reg, val);
}

uint32_t
zen_nbio_read(zen_nbio_t *nbio, const smn_reg_t reg)
{
	return (zen_smn_read(nbio->zn_iodie, reg));
}

void
zen_nbio_write(zen_nbio_t *nbio, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(nbio->zn_iodie, reg, val);
}

uint32_t
zen_nbif_read(zen_nbif_t *nbif, const smn_reg_t reg)
{
	return (zen_smn_read(nbif->zn_ioms->zio_nbio->zn_iodie, reg));
}

void
zen_nbif_write(zen_nbif_t *nbif, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(nbif->zn_ioms->zio_nbio->zn_iodie, reg, val);
}

uint32_t
zen_nbif_func_read(zen_nbif_func_t *func, const smn_reg_t reg)
{
	zen_iodie_t *iodie = func->znf_nbif->zn_ioms->zio_nbio->zn_iodie;

	return (zen_smn_read(iodie, reg));
}

void
zen_nbif_func_write(zen_nbif_func_t *func, const smn_reg_t reg,
    const uint32_t val)
{
	zen_iodie_t *iodie = func->znf_nbif->zn_ioms->zio_nbio->zn_iodie;

	zen_smn_write(iodie, reg, val);
}

uint32_t
zen_iodie_read(zen_iodie_t *iodie, const smn_reg_t reg)
{
	return (zen_smn_read(iodie, reg));
}

void
zen_iodie_write(zen_iodie_t *iodie, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(iodie, reg, val);
}

uint32_t
zen_smn_mech1_read(const smn_reg_t reg)
{
	const uint32_t addr = SMN_REG_ADDR(reg);
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);
	uint32_t val;

	VERIFY(!zen_fabric_io_pci_cfg_disabled);

	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));

	outl(PCI_CONFADD, PCI_CADDR1(0, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, ZEN_NB_SMN_INDEX));
	outl(PCI_CONFDATA, base_addr);

	outl(PCI_CONFADD, PCI_CADDR1(0, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, ZEN_NB_SMN_DATA + addr_off));
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		val = (uint32_t)inb(PCI_CONFDATA);
		break;
	case 2:
		val = (uint32_t)inw(PCI_CONFDATA);
		break;
	case 4:
		val = inl(PCI_CONFDATA);
		break;
	default:
		bop_panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}
	if (zen_smn_log != 0) {
		eb_printf("SMN Early R reg 0x%x: 0x%x", addr, val);
	}

	return (val);
}

static uint32_t
zen_smn_read_pair(zen_iodie_t *iodie, const smn_reg_t reg, uint32_t smn_idx,
    uint32_t smn_data)
{
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t data_off = smn_data + SMN_REG_ADDR_OFF(reg);
	uint32_t val;

	pci_putl_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, smn_idx, base_addr);
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		val = (uint32_t)pci_getb_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO, data_off);
		break;
	case 2:
		val = (uint32_t)pci_getw_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO, data_off);
		break;
	case 4:
		val = pci_getl_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO, data_off);
		break;
	default:
		panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}

	return (val);
}

uint32_t
zen_smn_read(zen_iodie_t *iodie, const smn_reg_t reg)
{
	uint32_t val;

	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));
	ASSERT3P(iodie, !=, NULL);

	if (apix_nmi_in_progress()) {
		val = zen_smn_read_pair(iodie, reg, ZEN_NB_SMN_INDEX_NMI,
		    ZEN_NB_SMN_DATA_NMI);
	} else {
		mutex_enter(&iodie->zi_smn_lock);
		val = zen_smn_read_pair(iodie, reg, ZEN_NB_SMN_INDEX,
		    ZEN_NB_SMN_DATA);
		mutex_exit(&iodie->zi_smn_lock);
	}

	ZEN_SMN_LOG("SMN R reg 0x%x: 0x%x", SMN_REG_ADDR(reg), val);

	return (val);
}

static void
zen_smn_write_pair(zen_iodie_t *iodie, const smn_reg_t reg, uint32_t smn_idx,
    uint32_t smn_data, uint32_t val)
{
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t data_off = smn_data + SMN_REG_ADDR_OFF(reg);

	pci_putl_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, smn_idx, base_addr);
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		pci_putb_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, data_off, (uint8_t)val);
		break;
	case 2:
		pci_putw_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, data_off, (uint16_t)val);
		break;
	case 4:
		pci_putl_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
		    AMDZEN_NB_SMN_FUNCNO, data_off, val);
		break;
	default:
		panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}
}

void
zen_smn_write(zen_iodie_t *iodie, const smn_reg_t reg, const uint32_t val)
{
	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));
	ASSERT(SMN_REG_VALUE_FITS(reg, val));
	ASSERT3P(iodie, !=, NULL);

	ZEN_SMN_LOG("SMN W reg 0x%x: 0x%x", SMN_REG_ADDR(reg), val);

	if (apix_nmi_in_progress()) {
		zen_smn_write_pair(iodie, reg, ZEN_NB_SMN_INDEX_NMI,
		    ZEN_NB_SMN_DATA_NMI, val);
		return;
	}

	mutex_enter(&iodie->zi_smn_lock);
	zen_smn_write_pair(iodie, reg, ZEN_NB_SMN_INDEX, ZEN_NB_SMN_DATA, val);
	mutex_exit(&iodie->zi_smn_lock);
}
