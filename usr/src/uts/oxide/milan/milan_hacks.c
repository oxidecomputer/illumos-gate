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
