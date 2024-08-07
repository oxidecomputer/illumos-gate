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
 * Copyright 2024 Oxide Computer Company
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/pci_cfgspace.h>

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>
#include <sys/io/zen/smn.h>


/*
 * Variable to let us dump all SMN traffic while still developing.
 */
int zen_smn_log = 0;


smn_reg_t
zen_core_reg(const zen_core_t *const core, const smn_reg_def_t def)
{
	const smn_unit_t unit = def.srd_unit;
	VERIFY3U(unit, <, SMN_UNIT_MAX);
	zen_smn_core_reg_f reg_fn = oxide_zen_smn_ops()->zso_core_reg_fn[unit];

	zen_ccx_t *ccx = core->zc_ccx;
	VERIFY3P(ccx, !=, NULL);
	zen_ccd_t *ccd = ccx->zcx_ccd;
	VERIFY3P(ccd, !=, NULL);

	if (reg_fn == NULL) {
		cmn_err(CE_PANIC, "invalid SMN register type %d for Core",
		    unit);
	}

	return (reg_fn(ccd->zcd_physical_dieno, ccx->zcx_physical_cxno, def,
	    core->zc_physical_coreno));
}

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

smn_reg_t
zen_ccd_reg(const zen_ccd_t *const ccd, const smn_reg_def_t def)
{
	const smn_unit_t unit = def.srd_unit;
	VERIFY3U(unit, <, SMN_UNIT_MAX);
	zen_smn_ccd_reg_f reg_fn = oxide_zen_smn_ops()->zso_ccd_reg_fn[unit];
	if (reg_fn == NULL)
		cmn_err(CE_PANIC, "invalid SMN register type %d for CCD", unit);

	return (reg_fn(ccd->zcd_physical_dieno, def, 0));
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

smn_reg_t
zen_ioms_reg(const zen_ioms_t *const ioms, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const smn_unit_t unit = def.srd_unit;
	VERIFY3U(unit, <, SMN_UNIT_MAX);
	zen_smn_ioms_reg_f reg_fn = oxide_zen_smn_ops()->zso_ioms_reg_fn[unit];
	if (reg_fn == NULL) {
		cmn_err(CE_PANIC, "invalid SMN register type %d for IOMS",
		    unit);
	}

	return (reg_fn(ioms->zio_num, def, reginst));
}

uint32_t
zen_ioms_read(zen_ioms_t *ioms, const smn_reg_t reg)
{
	return (zen_smn_read(ioms->zio_iodie, reg));
}

void
zen_ioms_write(zen_ioms_t *ioms, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(ioms->zio_iodie, reg, val);
}

smn_reg_t
zen_iodie_reg(const zen_iodie_t *const iodie, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const smn_unit_t unit = def.srd_unit;
	VERIFY3U(unit, <, SMN_UNIT_MAX);
	zen_smn_iodie_reg_f reg_f = oxide_zen_smn_ops()->zso_iodie_reg_fn[unit];
	if (reg_f == NULL) {
		cmn_err(CE_PANIC, "invalid SMN register type %d for IO die",
		    unit);
	}

	return (reg_f(def, reginst));
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
zen_smn_read(zen_iodie_t *iodie, const smn_reg_t reg)
{
	const uint32_t addr = SMN_REG_ADDR(reg);
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);
	uint32_t val;

	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));
	ASSERT(iodie != NULL);

	mutex_enter(&iodie->zi_smn_lock);
	pci_putl_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, base_addr);
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		val = (uint32_t)pci_getb_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA + addr_off);
		break;
	case 2:
		val = (uint32_t)pci_getw_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA + addr_off);
		break;
	case 4:
		val = pci_getl_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA);
		break;
	default:
		panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}
	if (zen_smn_log != 0) {
		cmn_err(CE_NOTE, "SMN R reg 0x%x: 0x%x", addr, val);
	}
	mutex_exit(&iodie->zi_smn_lock);

	return (val);
}

void
zen_smn_write(zen_iodie_t *iodie, const smn_reg_t reg, const uint32_t val)
{
	const uint32_t addr = SMN_REG_ADDR(reg);
	const uint32_t base_addr = SMN_REG_ADDR_BASE(reg);
	const uint32_t addr_off = SMN_REG_ADDR_OFF(reg);

	ASSERT(SMN_REG_IS_NATURALLY_ALIGNED(reg));
	ASSERT(SMN_REG_SIZE_IS_VALID(reg));
	ASSERT(SMN_REG_VALUE_FITS(reg, val));
	ASSERT(iodie != NULL);

	mutex_enter(&iodie->zi_smn_lock);
	if (zen_smn_log != 0) {
		cmn_err(CE_NOTE, "SMN W reg 0x%x: 0x%x", addr, val);
	}
	pci_putl_func(iodie->zi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, base_addr);
	switch (SMN_REG_SIZE(reg)) {
	case 1:
		pci_putb_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA + addr_off, (uint8_t)val);
		break;
	case 2:
		pci_putw_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA + addr_off, (uint16_t)val);
		break;
	case 4:
		pci_putl_func(iodie->zi_smn_busno,
		    AMDZEN_NB_SMN_DEVNO, AMDZEN_NB_SMN_FUNCNO,
		    AMDZEN_NB_SMN_DATA, val);
		break;
	default:
		panic("unreachable invalid SMN register size %u",
		    SMN_REG_SIZE(reg));
	}

	mutex_exit(&iodie->zi_smn_lock);
}
