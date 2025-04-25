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

/*
 * Provides microarchitecture-independent access to the SMN (system management
 * network) and accessors that allow common parts of the Oxide architecture
 * kernel to access specific parts such as the IOMS, CCD, IO die, etc, via SMN.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/mutex.h>
#include <sys/pci_cfgspace.h>
#include <sys/clock.h>

#include <io/amdzen/amdzen.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform_impl.h>
#include <sys/io/zen/smn.h>


/*
 * Variable to let us dump all SMN traffic while still developing.
 */
int zen_smn_log = 0;


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
	return (zen_smn_read(ioms->zio_iodie, reg));
}

void
zen_ioms_write(zen_ioms_t *ioms, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(ioms->zio_iodie, reg, val);
}

uint32_t
zen_nbif_read(zen_nbif_t *nbif, const smn_reg_t reg)
{
	return (zen_smn_read(nbif->zn_ioms->zio_iodie, reg));
}

void
zen_nbif_write(zen_nbif_t *nbif, const smn_reg_t reg, const uint32_t val)
{
	zen_smn_write(nbif->zn_ioms->zio_iodie, reg, val);
}

uint32_t
zen_nbif_func_read(zen_nbif_func_t *func, const smn_reg_t reg)
{
	zen_iodie_t *iodie = func->znf_nbif->zn_ioms->zio_iodie;

	return (zen_smn_read(iodie, reg));
}

void
zen_nbif_func_write(zen_nbif_func_t *func, const smn_reg_t reg,
    const uint32_t val)
{
	zen_iodie_t *iodie = func->znf_nbif->zn_ioms->zio_iodie;

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

void
zen_hsmp_test(zen_iodie_t *iodie)
{
	const smn_reg_t id = SMN_MAKE_REG(0x3b10934, SMN_UNIT_IOHC);
	const smn_reg_t resp = SMN_MAKE_REG(0x3b10980, SMN_UNIT_IOHC);
	const smn_reg_t arg0 = SMN_MAKE_REG(0x3b109e0, SMN_UNIT_IOHC);
#if 0
	const smn_reg_t arg1 = SMN_MAKE_REG(0x3b109e4, SMN_UNIT_IOHC);
	const smn_reg_t arg2 = SMN_MAKE_REG(0x3b109e8, SMN_UNIT_IOHC);
	const smn_reg_t arg3 = SMN_MAKE_REG(0x3b109ec, SMN_UNIT_IOHC);
	const smn_reg_t arg4 = SMN_MAKE_REG(0x3b109f0, SMN_UNIT_IOHC);
	const smn_reg_t arg5 = SMN_MAKE_REG(0x3b109f4, SMN_UNIT_IOHC);
	const smn_reg_t arg6 = SMN_MAKE_REG(0x3b109f8, SMN_UNIT_IOHC);
	const smn_reg_t arg7 = SMN_MAKE_REG(0x3b109fc, SMN_UNIT_IOHC);
#endif
	uint32_t r;

#if 0
	// Test Message
	zen_smn_write(iodie, resp, 0);
	zen_smn_write(iodie, arg0, 0x1234);
	zen_smn_write(iodie, id, 1);
	for (uint_t i = 0; i < 100; i++) {
		for (uint_t j = 0; j < 10; j++) {
			r = zen_smn_read(iodie, resp);
			if (r != 0)
				goto testout;
		}
		eb_pausems(1);
	}
testout:
	cmn_err(CE_NOTE, "HSMP Test result: response 0x%x value 0x%x", r,
	    zen_smn_read(iodie, arg0));
#endif

	// Current freq limit
	zen_smn_write(iodie, resp, 0);
	zen_smn_write(iodie, arg0, 0);
	zen_smn_write(iodie, id, 0x19);
	for (uint_t i = 0; i < 1000; i++) {
		for (uint_t j = 0; j < 10; j++) {
			r = zen_smn_read(iodie, resp);
			if (r != 0)
				goto freqout;
		}
		eb_pausems(1);
	}

freqout:
	cmn_err(CE_NOTE, "HSMP Freq result: response 0x%x value 0x%x", r,
	    zen_smn_read(iodie, arg0));
}
