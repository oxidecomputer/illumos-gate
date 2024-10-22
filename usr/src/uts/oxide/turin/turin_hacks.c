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
 * Various and sundry Genoa-specific hacks.
 */

#include <sys/types.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/turin/hacks.h>
#include <sys/io/turin/iomux.h>

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
turin_hack_gpio(zen_hack_gpio_op_t op, uint16_t gpio)
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
			mux_val = TURIN_FCH_IOMUX_26_AGPIO26;
			break;
		case 27:
			mux_val = TURIN_FCH_IOMUX_27_EGPIO26_3;
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

			mux_val = TURIN_FCH_IOMUX_129_AGPIO129;
		}
			break;
		case 266:
			mux_val = TURIN_FCH_RMTMUX_10_EGPIO26_1;
			break;
		case 267:
			mux_val = TURIN_FCH_RMTMUX_11_EGPIO26_2;
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
		break;
	}
	default:
		cmn_err(CE_PANIC, "invalid turin GPIO hack op %d", op);
	}

	mmio_reg_block_unmap(&gpio_block);
}
