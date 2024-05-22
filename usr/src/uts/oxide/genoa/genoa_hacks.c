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

#include <sys/types.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/hacks.h>
#include <sys/io/genoa/iomux.h>
#include <sys/platform_detect.h>

/* True while we're hacking. */
bool xxxhackymchackface = true;

/*
 * Various regrettable hacks that are unfortunate but necessary -- and don't
 * seem to fit anywhere else.  This file could also be called genoa_misc.c or
 * genoa_subr.c, but it seems that being slightly pejorative with respect to its
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
genoa_fixup_i2c_clock(void)
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
bool
genoa_cgpll_set_ssc(bool ssc)
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
 * It is an unfortunate reality that the reset and shutdown conditions of an
 * x86 microprocessor are ill-defined and dependent upon implicit interactions
 * between many different components:  the core inducing the shutdown or
 * reset, the other cores on the die, the hidden computer that is effectively
 * contained within that die (euphemistically called a system-on-a-chip), the
 * lowest level software running on each those components, and the surrounding
 * machine itself (replete with its own historical artifacts).  Each of these
 * is poorly documented and strictly proprietary; it is no surprise that their
 * confluence works by accident such as it works at all.  In short, it is a
 * midden pit of computing:  interesting, perhaps, to future anthropoligists
 * -- but consisting only of refuse, it was never designed at all, let alone
 *  to serve as foundation.
 *
 * The problem in front of us -- ludicrous as it may seem -- is to make sure
 * that a core shutdown properly induces a machine reset (that is, we wish
 * to transition the machine from A0 to A2).
 *
 * The first issue is even more basic:  assuring that a single core shutdown
 * in fact shuts down all cores.  (Amazingly, this is not the default
 * disposition, and a single core shutdown will just result in a chunk of the
 * system silently disappearing, with the rest of the system left to discover
 * its absence only through the prescribed work that it is apparently no
 * longer doing.)
 *
 * Experimentation has revealed that this issue can be resolved by setting
 * en_shutdown_msg in FCH::PM::ACPICONFIG: when this bit is set, a shutdown on
 * a single core results in a SHUTDOWN message being sent in such a way that
 * all cores shutdown.  This is important, but it is insufficent: the shutdown
 * message will result in all cores entering the shutdown state, but there
 * isn't further activity (that is, there is no reset, externally visible or
 * otherwise).
 *
 * Fortunately, there is an additional register, FCH::PM::PCICONTROL that has
 * a shutdownoption field; this is defined to "Generate Pci (sic) reset when
 * receiving shutdown message." The type of reset is itself not defined, but
 * it has been empirically determined that setting this bit does result in a
 * shutdown message inducing behavior consistent with a Warm Reset.
 * (Specifically: we see RESET_L become de-asserted for ~60 milliseconds while
 * PWROK remains asserted.) Note that the CPU itself appears to go back to ABL
 * under this condition, and retrains DIMMs, etc.
 *
 * Importantly, the SoC resets under this condition, but the FCH is not reset.
 * Specifically, FCH::PM::S5_RESET_STATUS does correctly reflect the reset
 * reason (namely, shutdown_msg is set). On the one hand, this is helpful in
 * that it gives us a potential backstop, but on the other hand it is chilling:
 * if there were any lingering doubts that the state of the system is too
 * ill-defined after a reset to depend on, this should eliminate them!
 *
 * Finally: setting rsttocpupwrgden in FCH::PM::RESETCONTROL1 results in what
 * appears to be closer to a cold reset, in that in addition to RESET_L being
 * asserted, PWROK is also de-asserted (for ~6 milliseconds).
 *
 * The below code takes these three actions, and together with modifications
 * to the boarder system to detect any change in RESET_L/PWROK, assures that
 * a single core shutdown (e.g., due to a triple fault) results in our
 * desired semantics:  a machine reset through A2.
 */
void
genoa_shutdown_detect_init()
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
genoa_check_furtive_reset()
{
	mmio_reg_block_t fch_pmio = fch_pmio_mmio_block();
	mmio_reg_t reg = FCH_PMIO_S5_RESET_STATUS_MMIO(fch_pmio);
	uint64_t val = mmio_reg_read(reg);

	if (FCH_PMIO_S5_RESET_STATUS_GET_SW_SYNC_FLOOD_FLAG(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SDP_PARITY_ERR(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_MP1_WDTOUT(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SYNC_FLOOD(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_REMOTERESETFROMASF(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_WATCHDOGISSUERESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_FAILBOOTRST(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SHUTDOWN_MSG(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_KB_RESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SLEEPRESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_FULL_RESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_RESET(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_INIT(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SOFT_PCIRST(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_USERRST(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_INTTHERMALTRIP(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_REMOTEPOWERDOWNFROMASF(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_SHUTDOWN(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_PWRBTN4SECOND(val) != 0 ||
	    FCH_PMIO_S5_RESET_STATUS_GET_THERMALTRIP(val) != 0) {
		// Don't if there's no SP (e.g. we're running on a Ruby)
		if (oxide_board_data->obd_board == OXIDE_BOARD_COSMO) {
			panic("FCH::PM::S5_RESET_STATUS 0x%08lx "
			    "implies furtive reset", val);
		} else {
			cmn_err(CE_WARN, "FCH::PM::S5_RESET_STATUS 0x%08lx "
			    "implies furtive reset", val);
		}
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
genoa_hack_gpio(genoa_hack_gpio_op_t op, uint16_t gpio)
{
	mmio_reg_block_t gpio_block;
	mmio_reg_t gpio_reg;
	uint32_t val;

	if (gpio < 256) {
		gpio_block = fch_gpio_mmio_block();
		gpio_reg = FCH_GPIO_GPIO_MMIO(gpio_block, gpio);
	} else {
		gpio_block = fch_rmtgpio_mmio_block();
		gpio_reg = FCH_RMTGPIO_GPIO_MMIO(gpio_block, gpio - 256);
	}

	switch (op) {
	case GHGOP_CONFIGURE: {
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
			mux_val = GENOA_FCH_IOMUX_26_AGPIO26;
			break;
		case 27:
			mux_val = GENOA_FCH_IOMUX_27_EGPIO26_3;
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

			mux_val = GENOA_FCH_IOMUX_129_AGPIO129;
		}
			break;
		case 266:
			mux_val = GENOA_FCH_RMTMUX_10_EGPIO26_1;
			break;
		case 267:
			mux_val = GENOA_FCH_RMTMUX_11_EGPIO26_2;
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
		break;
	}
	case GHGOP_RESET:
		val = mmio_reg_read(gpio_reg);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, 0);
		mmio_reg_write(gpio_reg, val);
		break;
	case GHGOP_SET:
		val = mmio_reg_read(gpio_reg);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, 1);
		mmio_reg_write(gpio_reg, val);
		break;
	case GHGOP_TOGGLE: {
		uint32_t output;

		val = mmio_reg_read(gpio_reg);
		output = FCH_GPIO_GPIO_GET_OUTPUT(val);
		val = FCH_GPIO_GPIO_SET_OUTPUT(val, !output);
		mmio_reg_write(gpio_reg, val);
		break;
	}
	default:
		cmn_err(CE_PANIC, "invalid genoa GPIO hack op %d", op);
	}

	mmio_reg_block_unmap(&gpio_block);
}

