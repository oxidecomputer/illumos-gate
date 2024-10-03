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
 * We'd like to open the GPIO driver and do this properly, but we need to
 * manipulate GPIOs before the DDI is fully set up.  So we have this handy
 * function to do it for us directly.  This is used to release PERST during the
 * LISM on Ethanol-X (but not Gimlet, which uses the GPIO expanders for PERST)
 * and to signal register capture for PCIe debugging via a logic analyser.
 * The CONFIGURE op claims the GPIO via the IOMUX and configures it as an output
 * with internal pulls disabled.  We allow setup of only those pins we know
 * can/should be used by this code; others will panic.  The other operations are
 * all straightforward and will work on any GPIO that has been configured,
 * whether by us, by firmware, or at power-on reset.  If the mux has not been
 * configured, this will still work but there will be no visible effect outside
 * the processor.
 *
 * We use MMIO here to accommodate broken firmware that blocks SMN access to
 * these blocks.
 */
void
milan_hack_gpio(zen_hack_gpio_op_t op, uint16_t gpio)
{
	mmio_reg_block_t gpio_block;
	mmio_reg_t gpio_reg;
	uint32_t val;

	if (gpio < 256) {
		gpio_block = fch_gpio_mmio_block();
		gpio_reg = FCH_GPIO_GPIO_MMIO(gpio_block, gpio);
	} else {
		gpio_block = fch_rmtgpio_mmio_block();
		gpio_reg = FCH_GPIO_GPIO_MMIO(gpio_block, gpio - 256);
	}

	switch (op) {
	case ZHGOP_CONFIGURE: {
		mmio_reg_block_t iomux_block;
		mmio_reg_t iomux_reg;
		uint8_t mux_val = 0;

		if (gpio < 256) {
			iomux_block = fch_iomux_mmio_block();
			iomux_reg = FCH_IOMUX_IOMUX_MMIO(iomux_block, gpio);
		} else {
			iomux_block = fch_rmtmux_mmio_block();
			iomux_reg = FCH_RMTMUX_IOMUX_MMIO(iomux_block,
			    gpio - 256);
		}

		switch (gpio) {
		case 26:
			mux_val = MILAN_FCH_IOMUX_26_EGPIO26;
			break;
		case 27:
			mux_val = MILAN_FCH_IOMUX_27_EGPIO26_3;
			break;
		case 129: {
			/*
			 * For reasons no one will ever understand, changing the
			 * state of this GPIO -- even leaving it as an input --
			 * while FCH::PM::RESETCONTROL1[kbrsten] is set will
			 * cause the machine to reset.  This is true even if we
			 * first set the GPIO to an input, then set the IOMUX to
			 * the GPIO, then set the GPIO to an output.  There is
			 * no really sensible explanation for this other than
			 * that the GPIO's internal state is somehow connected
			 * directly to the KBRST logic's input regardless of the
			 * IOMUX.  Words fail.  Work around this here.
			 */
			mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
			mmio_reg_t rstctl_reg;
			uint64_t rstctl_val;

			rstctl_reg = FCH_PMIO_RESETCONTROL1_MMIO(fch_pmio);
			rstctl_val = mmio_reg_read(rstctl_reg);
			rstctl_val = FCH_PMIO_RESETCONTROL1_SET_KBRSTEN(
			    rstctl_val, 0);
			mmio_reg_write(rstctl_reg, rstctl_val);
			mmio_reg_block_unmap(&fch_pmio);

			mux_val = MILAN_FCH_IOMUX_129_AGPIO129;
		}
			break;
		case 266:
			mux_val = MILAN_FCH_RMTMUX_10_EGPIO26_1;
			break;
		case 267:
			mux_val = MILAN_FCH_RMTMUX_11_EGPIO26_2;
			break;
		default:
			cmn_err(CE_PANIC, "attempt to hack unexpected GPIO %d",
			    gpio);
		}

		/*
		 * Before muxing in the GPIO, we want to set it up in a known
		 * initial state.
		 */
		val = mmio_reg_read(gpio_reg);
		val = FCH_GPIO_GPIO_SET_OUT_EN(val, 1);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, 0);
		val = FCH_GPIO_GPIO_SET_PD_EN(val, 0);
		val = FCH_GPIO_GPIO_SET_PU_EN(val, 0);
		val = FCH_GPIO_GPIO_SET_WAKE_S5(val, 0);
		val = FCH_GPIO_GPIO_SET_WAKE_S3(val, 0);
		val = FCH_GPIO_GPIO_SET_WAKE_S0I3(val, 0);
		val = FCH_GPIO_GPIO_SET_INT_EN(val, 0);
		val = FCH_GPIO_GPIO_SET_INT_STS_EN(val, 0);

		mmio_reg_write(gpio_reg, val);
		mmio_reg_write(iomux_reg, mux_val);

		mmio_reg_block_unmap(&iomux_block);
	}
		break;
	case ZHGOP_RESET:
		val = mmio_reg_read(gpio_reg);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, 0);
		mmio_reg_write(gpio_reg, val);
		break;
	case ZHGOP_SET:
		val = mmio_reg_read(gpio_reg);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, 1);
		mmio_reg_write(gpio_reg, val);
		break;
	case ZHGOP_TOGGLE: {
		uint32_t output;

		val = mmio_reg_read(gpio_reg);
		output = FCH_GPIO_GPIO_GET_OUTPUT(val);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, !output);
		mmio_reg_write(gpio_reg, val);
	}
		break;
	default:
		cmn_err(CE_PANIC, "invalid milan GPIO hack op %d", op);
	}

	mmio_reg_block_unmap(&gpio_block);
}
