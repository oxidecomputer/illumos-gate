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
#include <sys/stdbool.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/milan/hacks.h>
#include <sys/io/milan/iomux.h>
#include <sys/io/zen/hacks.h>

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
bool
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

	return (true);
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
bool
milan_cgpll_set_ssc(bool ssc)
{
	mmio_reg_block_t fch_misc_a = fch_misc_a_mmio_block();
	mmio_reg_t reg;
	uint32_t val;

	if (ssc) {
		reg = FCH_MISC_A_STRAPSTATUS_MMIO(fch_misc_a);
		val = mmio_reg_read(reg);
		if (FCH_MISC_A_STRAPSTATUS_GET_CLKGEN(val) !=
		    FCH_MISC_A_STRAPSTATUS_CLKGEN_INT) {
			mmio_reg_block_unmap(&fch_misc_a);
			return (false);
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

	return (true);
}

/*
 * Check the non-reserved bits in FCH::PM::S5_RESET_STATUS.  If there exists
 * some condition or window whereby the chip can reset without toggling
 * RESET_L/PWROK or otherwise being detected by the SP -- which we dub a
 * "furtive reset" -- we will pick it up here.  It's important to panic with
 * the specific reason that we discover:  this should point us to the
 * condition that is somehow resulting in the reset.  That we panic is
 * essential: if the system has been reset without transitioning to A2, we
 * absolutely do not want to continue -- and panicking now will (or should,
 * assuming a properly functioning SP) result in a trip back to A2 to get our
 * state cleared (that is, we are unlikely to panic loop).  This should be
 * called far enough into boot to be able to get a clean panic, but not so far
 * that we increase the likelihood of encountering cascading failure from
 * stale, hidden state.
 */
void
milan_check_furtive_reset()
{
	mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
	mmio_reg_t reg = FCH_PMIO_S5_RESET_STATUS_MMIO(fch_pmio);
	uint64_t val = mmio_reg_read(reg);

	if (FCH_PMIO_S5_RESET_STATUS_GET_MP1_WDTOUT(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SYNC_FLOOD(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_REMOTERESETFROMASF(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_WATCHDOGISSUERESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_FAILBOOTRST(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SHUTDOWN_MSG(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_KB_RESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SLEEPRESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_RESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_INIT(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SOFT_PCIRST(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_USERRST(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_INTTHERMALTRIP(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_REMOTEPOWERDOWNFROMASF(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SHUTDOWN(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_PWRBTN4SECOND(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_THERMALTRIP(val) != 0) {
		panic("FCH::PM::S5_RESET_STATUS 0x%08lx "
		    "implies furtive reset", val);
	}

	mmio_reg_block_unmap(&fch_pmio);
}

/*
 * Provide an interface to enable or disable KBRST_L.
 *
 * On Milan, configuring any GPIO on that manipulates KBRST_L requires special
 * handling.  Currently, the only pin that manipulates KBRST_L is 129.  For
 * reasons no one will ever understand, changing the state of this GPIO, or even
 * leaving it as an input, while FCH::PM::RESETCONTROL1[kbrsten] is set will
 * cause the machine to reset.  This is true even if we first set the GPIO to an
 * input, then set the IOMUX to the GPIO, then set the GPIO to an output.  There
 * is no really sensible explanation for this other than that the GPIO's
 * internal state is somehow connected directly to the KBRST logic's input
 * regardless of the IOMUX.  Words fail.  We can work around this by disabling
 * KBRST_L before GPIO configuration.
 *
 * Note that testing on Genoa and Turin leads us to believe that this only
 * applies to Milan, so we only do this on gimlet; hence why we haven't
 * generalized this function.
 */
void
milan_hack_set_kbrst_en(bool state)
{
	mmio_reg_block_t block = fch_pmio_mmio_block();
	mmio_reg_t reg = FCH_PMIO_RESETCONTROL1_MMIO(block);
	uint64_t val = mmio_reg_read(reg);

	val = FCH_PMIO_RESETCONTROL1_SET_KBRSTEN(val, state ? 1 : 0);
	mmio_reg_write(reg, val);
	mmio_reg_block_unmap(&block);
}
