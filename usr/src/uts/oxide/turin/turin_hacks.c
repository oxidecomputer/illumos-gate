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
 * Copyright 2025 Oxide Computer Company
 */

/*
 * Various regrettable hacks that are unfortunate but necessary -- and don't
 * seem to fit anywhere else.  This file could also be called turin_misc.c or
 * turin_subr.c, but it seems that being slightly pejorative with respect to its
 * name may make it a little less likely to grow appendages that in fact belong
 * elsewhere...
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/turin/hacks.h>
#include <sys/io/turin/iomux.h>
#include <sys/io/zen/hacks.h>

/*
 * Enables or disables PCIe spread spectrum via the Kunlun FCH's clock
 * generator. We only ever enable this but this function can also turn it off.
 * The PPR says this should be done only if the FCH is in "internal clock
 * mode"; what that means is not clear but the way to check for it is. If the
 * caller tries to enable SSC in external clock mode, we fail. Disabling SSC
 * is always allowed. At present this works only for socket 0 as the fch
 * driver hasn't set up the remote FCH aperture yet! However, the PPR also
 * says we're supposed to enable SSC only on socket 0 anyway, presumably
 * because the clock from socket 0 ends up being passed along to socket 1.
 */
bool
turin_cgpll_set_ssc(bool ssc)
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
