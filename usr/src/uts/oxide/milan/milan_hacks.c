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
 * Copyright 2023 Oxide Computer Company
 */

#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/milan/hacks.h>

/*
 * Various regrettable hacks that are unfortunate but necessary -- and don't
 * seem to fit anywhere else.  This file could also be called milan_misc.c or
 * milan_subr.c, but it seems that being slightly pejorative with respect to its
 * name may make it a little less likely to grow appendages that in fact belong
 * elsewhere...
 */

/*
 * This is a total hack. Unfortunately the SMU relies on x86 software to
 * actually set the i2c clock up to something expected for it. Temporarily do
 * this the max power way.  We set all the defined fields of the control
 * register, preserving only those that are reserved.
 */
boolean_t
milan_fixup_i2c_clock(void)
{
	mmio_reg_block_t fch_i2c0 = fch_i2c_mmio_block(0);
	mmio_reg_t reg;
	uint32_t val;

	reg = FCH_I2C_IC_CON_MMIO(fch_i2c0);
	val = mmio_reg_read(reg);
	val = FCH_I2C_IC_CON_SET_HOLD_ON_RX_FULL(val, 0);
	val = FCH_I2C_IC_CON_SET_TXE_INTR_EN(val, 0);
	val = FCH_I2C_IC_CON_SET_SD_INTR_ADDRONLY(val, 0);
	val = FCH_I2C_IC_CON_SET_SLAVE_DIS(val, 1);
	val = FCH_I2C_IC_CON_SET_RESTART_EN(val, 1);
	val = FCH_I2C_IC_CON_SET_MA_ADDRWIDTH(val, FCH_I2C_IC_CON_ADDRWIDTH_7);
	val = FCH_I2C_IC_CON_SET_SL_ADDRWIDTH(val, FCH_I2C_IC_CON_ADDRWIDTH_7);
	val = FCH_I2C_IC_CON_SET_SPEED(val, FCH_I2C_IC_CON_SPEED_STD);
	val = FCH_I2C_IC_CON_SET_MASTER_EN(val, 1);
	mmio_reg_write(reg, val);

	mmio_reg_block_unmap(&fch_i2c0);

	return (B_TRUE);
}

/*
 * Another clock hack.  Like the I2C fixup, this is basically fine but
 * unfortunate.  Enables or disables PCIe spread spectrum via the Huashan FCH's
 * clock generator.  We only ever enable this but this function can also turn it
 * off.  The PPR says this should be done only if the FCH is in "internal clock
 * mode"; what that means is not clear but the way to check for it is.  If the
 * caller tries to enable SSC in external clock mode, we fail.  Disabling SSC is
 * always allowed.  At present this works only for socket 0 as the fch driver
 * hasn't set up the remote FCH aperture yet!  However, the PPR also says we're
 * supposed to enable SSC only on socket 0 anyway, presumably because the clock
 * from socket 0 ends up being passed along to socket 1.
 */
boolean_t
milan_cgpll_set_ssc(boolean_t ssc)
{
	mmio_reg_block_t fch_misc_a = fch_misc_a_mmio_block();
	mmio_reg_t reg;
	uint32_t val;

	if (ssc) {
		reg = FCH_MISC_A_STRAPSTATUS_MMIO(fch_misc_a);
		val = mmio_reg_read(reg);
		if (FCH_MISC_A_STRAPSTATUS_GET_CLKGEN(val) !=
		    FCH_MISC_A_STRAPSTATUS_CLKGEN_INT) {
			return (B_FALSE);
		}
	}

	reg = FCH_MISC_A_CGPLLCFG3_MMIO(fch_misc_a);
	val = mmio_reg_read(reg);
	val = FCH_MISC_A_CGPLLCFG3_SET_FRACN_EN_OVR(val, 1);
	mmio_reg_write(reg, val);

	reg = FCH_MISC_A_CGPLLCFG1_MMIO(fch_misc_a);
	val = mmio_reg_read(reg);
	val = FCH_MISC_A_CGPLLCFG1_SET_SSC_EN(val, ssc ? 1 : 0);
	mmio_reg_write(reg, val);

	/*
	 * Nothing happens until we set this bit to poke the CG.
	 */
	reg = FCH_MISC_A_CLKCTL0_MMIO(fch_misc_a);
	val = mmio_reg_read(reg);
	val = FCH_MISC_A_CLKCTL0_SET_UPDATE_REQ(val, 1);
	mmio_reg_write(reg, val);

	mmio_reg_block_unmap(&fch_misc_a);

	return (B_TRUE);
}

/*
 * Unfortunately, the conditions that result XXX
 */
void
milan_shutdown_detect_init()
{
	mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
        mmio_reg_t reg;
        uint64_t val;

        reg = FCH_PMIO_ACPICONFIG_MMIO(fch_pmio);
        val = mmio_reg_read(reg);
        val = FCH_PMIO_ACPICONFIG_SET_EN_SHUTDOWN_MSG(val, 1);
        mmio_reg_write(reg, val);

        reg = FCH_PMIO_PCICONTROL_MMIO(fch_pmio);
        val = mmio_reg_read(reg);
        val = FCH_PMIO_PCICONTROL_SET_SHUTDOWNOPTION(val, 1);
        mmio_reg_write(reg, val);

        reg = FCH_PMIO_RESETCONTROL1_MMIO(fch_pmio);
        val = mmio_reg_read(reg);
        val = FCH_PMIO_RESETCONTROL1_SET_RSTTOCPUPWRGDEN(val, 1);
        mmio_reg_write(reg, val);

        mmio_reg_block_unmap(&fch_pmio);
}
