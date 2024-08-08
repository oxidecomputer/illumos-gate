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

#include <sys/amdzen/ccd.h>
#include <sys/amdzen/fch/iomux.h>
#include <sys/amdzen/fch/gpio.h>
#include <sys/io/fch/i2c.h>
#include <sys/io/fch/misc.h>
#include <sys/io/fch/pmio.h>
#include <sys/io/fch/smi.h>

#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/platform.h>
#include <sys/io/zen/smn.h>

#include <sys/io/milan/fabric_impl.h>
#include <sys/io/milan/ioapic.h>
#include <sys/io/milan/iohc.h>
#include <sys/io/milan/iommu.h>
#include <sys/io/milan/smu_impl.h>
#include <sys/io/milan/ras.h>
#include <sys/io/milan/hacks.h>
#include <milan/milan_apob.h>


/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	MILAN_IOMS_PER_IODIE	4

/*
 * Per the PPR, the following defines the InstanceID of the first IOMS on Milan.
 */
#define	MILAN_DF_FIRST_IOMS_ID	24


static const zen_apob_ops_t milan_apob_ops = {
	.zao_reserve_phys = milan_apob_reserve_phys,
};

static const zen_ccx_ops_t milan_ccx_ops = {
	.zco_init = milan_ccx_init,
	.zco_physmem_init = zen_ccx_physmem_init,
	.zco_mmio_init = zen_ccx_mmio_init,
	.zco_start_thread = milan_ccx_start_thread,
};

static const zen_fabric_ops_t milan_fabric_ops = {
	.zfo_fabric_init = milan_fabric_init,
	.zfo_enable_nmi = milan_fabric_enable_nmi,
	.zfo_nmi_eoi = milan_fabric_nmi_eoi,

	.zfo_topo_init = milan_fabric_topo_init,
	.zfo_soc_init = milan_fabric_soc_init,
	.zfo_thread_apicid = milan_fabric_thread_apicid,
	.zfo_ioms_init = milan_fabric_ioms_init,
};

static const zen_hack_ops_t milan_hack_ops = {
	.zho_check_furtive_reset = milan_check_furtive_reset,
	.zho_cgpll_set_ssc = milan_cgpll_set_ssc,
};

static const zen_ras_ops_t milan_ras_ops = {
	.zro_ras_init = milan_ras_init,
};

static const zen_smn_ops_t milan_smn_ops = {
	.zso_core_reg_fn = {
		[SMN_UNIT_SCFCTP]	= amdzen_scfctp_smn_reg,
	},
	.zso_ccd_reg_fn = {
		[SMN_UNIT_SMUPWR]	= amdzen_smupwr_smn_reg,
	},
	/*
	 * We consider the IOAGR to be part of the NBIO/IOHC/IOMS, so the
	 * IOMMUL1's IOAGR block falls under the IOMS; the IOAPIC, SDPMUX, and
	 * IOMMUL2 are similar as they do not (currently) have independent
	 * representation in the fabric.
	 */
	.zso_ioms_reg_fn = {
		[SMN_UNIT_IOAPIC]	= milan_ioapic_smn_reg,
		[SMN_UNIT_IOHC]		= milan_iohc_smn_reg,
		[SMN_UNIT_IOAGR]	= milan_ioagr_smn_reg,
		[SMN_UNIT_SDPMUX]	= milan_sdpmux_smn_reg,
		[SMN_UNIT_IOMMUL1]	= milan_iommul1_smn_reg,
		[SMN_UNIT_IOMMUL2]	= milan_iommul2_smn_reg,
	},
	.zso_iodie_reg_fn = {
		[SMN_UNIT_SMU_RPC]		= milan_smu_smn_reg,
		[SMN_UNIT_FCH_SMI]		= fch_smi_smn_reg,
		[SMN_UNIT_FCH_PMIO]		= fch_pmio_smn_reg,
		[SMN_UNIT_FCH_MISC_A]		= fch_misc_a_smn_reg,
		[SMN_UNIT_FCH_I2CPAD]		= fch_i2cpad_smn_reg,
		[SMN_UNIT_FCH_MISC_B]		= fch_misc_b_smn_reg,
		[SMN_UNIT_FCH_I2C]		= huashan_i2c_smn_reg,
		[SMN_UNIT_FCH_IOMUX]		= fch_iomux_smn_reg,
		[SMN_UNIT_FCH_GPIO]		= fch_gpio_smn_reg,
		[SMN_UNIT_FCH_RMTGPIO]		= fch_rmtgpio_smn_reg,
		[SMN_UNIT_FCH_RMTMUX]		= fch_rmtmux_smn_reg,
		[SMN_UNIT_FCH_RMTGPIO_AGG]	= fch_rmtgpio_agg_smn_reg,
	},
};

zen_platform_t milan_platform = {
	.zp_consts = {
		.zpc_df_rev = DF_REV_3,
		.zpc_ioms_per_iodie = MILAN_IOMS_PER_IODIE,
		.zpc_ccds_per_iodie = MILAN_MAX_CCDS_PER_IODIE,
		.zpc_cores_per_ccx = MILAN_MAX_CORES_PER_CCX,
		/*
		 * Milan has a single IOMS component hence these are the same.
		 */
		.zpc_df_first_iom_id = MILAN_DF_FIRST_IOMS_ID,
		.zpc_df_first_ios_id = MILAN_DF_FIRST_IOMS_ID,
	},
	.zp_apob_ops = &milan_apob_ops,
	.zp_ccx_ops = &milan_ccx_ops,
	.zp_fabric_ops = &milan_fabric_ops,
	.zp_hack_ops = &milan_hack_ops,
	.zp_ras_ops = &milan_ras_ops,
	.zp_smn_ops = &milan_smn_ops,
};
