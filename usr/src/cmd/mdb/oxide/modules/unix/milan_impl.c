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
 * Shared Milan DF and UMC data.
 */

#include <sys/sysmacros.h>

#include "zen_kmdb_impl.h"


static const char *milan_chan_ileaves[16] = {
	[0] = "1",
	[1] = "2",
	[3] = "4",
	[5] = "8",
	[6] = "6",
	[12] = "COD4 2CH",
	[13] = "COD2 4CH",
	[14] = "COD1 8CH",
};

const char *milan_chan_map[] = {
	[0] = "A",
	[1] = "B",
	[2] = "D",
	[3] = "C",
	[4] = "H",
	[5] = "G",
	[6] = "E",
	[7] = "F",
};

static const uint8_t milan_chan_umc_order[] = { 0, 1, 3, 2, 6, 7, 5, 4 };

#define	MILAN_COMP_ENTRY(inst, name, ndram, invalid_dest)	\
	{\
		.dc_inst = inst,\
		.dc_name = name,\
		.dc_ndram = ndram,\
		.dc_invalid_dest = invalid_dest\
	}

static const df_comp_t milan_comps[] = {
	[0] = MILAN_COMP_ENTRY(0, "UMC0", 2, B_FALSE),
	[1] = MILAN_COMP_ENTRY(1, "UMC1", 2, B_FALSE),
	[2] = MILAN_COMP_ENTRY(2, "UMC2", 2, B_FALSE),
	[3] = MILAN_COMP_ENTRY(3, "UMC3", 2, B_FALSE),
	[4] = MILAN_COMP_ENTRY(4, "UMC4", 2, B_FALSE),
	[5] = MILAN_COMP_ENTRY(5, "UMC5", 2, B_FALSE),
	[6] = MILAN_COMP_ENTRY(6, "UMC6", 2, B_FALSE),
	[7] = MILAN_COMP_ENTRY(7, "UMC7", 2, B_FALSE),
	[8] = MILAN_COMP_ENTRY(8, "CCIX0", 2, B_FALSE),
	[9] = MILAN_COMP_ENTRY(9, "CCIX1", 2, B_FALSE),
	[10] = MILAN_COMP_ENTRY(10, "CCIX2", 2, B_FALSE),
	[11] = MILAN_COMP_ENTRY(11, "CCIX3", 2, B_FALSE),
	[12] = MILAN_COMP_ENTRY(16, "CCM0", 16, B_FALSE),
	[13] = MILAN_COMP_ENTRY(17, "CCM1", 16, B_FALSE),
	[14] = MILAN_COMP_ENTRY(18, "CCM2", 16, B_FALSE),
	[15] = MILAN_COMP_ENTRY(19, "CCM3", 16, B_FALSE),
	[16] = MILAN_COMP_ENTRY(20, "CCM4", 16, B_FALSE),
	[17] = MILAN_COMP_ENTRY(21, "CCM5", 16, B_FALSE),
	[18] = MILAN_COMP_ENTRY(22, "CCM6", 16, B_FALSE),
	[19] = MILAN_COMP_ENTRY(23, "CCM7", 16, B_FALSE),
	[20] = MILAN_COMP_ENTRY(24, "IOMS0", 16, B_FALSE),
	[21] = MILAN_COMP_ENTRY(25, "IOMS1", 16, B_FALSE),
	[22] = MILAN_COMP_ENTRY(26, "IOMS2", 16, B_FALSE),
	[23] = MILAN_COMP_ENTRY(27, "IOMS3", 16, B_FALSE),
	[24] = MILAN_COMP_ENTRY(30, "PIE0", 8, B_FALSE),
	[25] = MILAN_COMP_ENTRY(31, "CAKE0", 0, B_TRUE),
	[26] = MILAN_COMP_ENTRY(32, "CAKE1", 0, B_TRUE),
	[27] = MILAN_COMP_ENTRY(33, "CAKE2", 0, B_TRUE),
	[28] = MILAN_COMP_ENTRY(34, "CAKE3", 0, B_TRUE),
	[29] = MILAN_COMP_ENTRY(35, "CAKE4", 0, B_TRUE),
	[30] = MILAN_COMP_ENTRY(36, "CAKE5", 0, B_TRUE),
	[31] = MILAN_COMP_ENTRY(37, "TCDX0", 0, B_TRUE),
	[32] = MILAN_COMP_ENTRY(38, "TCDX1", 0, B_TRUE),
	[33] = MILAN_COMP_ENTRY(39, "TCDX2", 0, B_TRUE),
	[34] = MILAN_COMP_ENTRY(40, "TCDX3", 0, B_TRUE),
	[35] = MILAN_COMP_ENTRY(41, "TCDX4", 0, B_TRUE),
	[36] = MILAN_COMP_ENTRY(42, "TCDX5", 0, B_TRUE),
	[37] = MILAN_COMP_ENTRY(43, "TCDX6", 0, B_TRUE),
	[38] = MILAN_COMP_ENTRY(44, "TCDX7", 0, B_TRUE),
	[39] = MILAN_COMP_ENTRY(45, "TCDX8", 0, B_TRUE),
	[40] = MILAN_COMP_ENTRY(46, "TCDX9", 0, B_TRUE),
	[41] = MILAN_COMP_ENTRY(47, "TCDX10", 0, B_TRUE),
	[42] = MILAN_COMP_ENTRY(48, "TCDX11", 0, B_TRUE)
};

df_props_t df_props_milan = {
	.dfp_rev = DF_REV_3,
	.dfp_reg_mask = 0x3fc,
	.dfp_max_cfgmap = DF_MAX_CFGMAP,
	/*
	 * For DRAM, default to CCM0 (we don't use a UMC because it has very few
	 * rules). For I/O ports, use CCM0 as well as the IOMS entries don't
	 * really have rules here. For MMIO and PCI buses, use IOMS0.
	 */
	.dfp_dram_io_inst = 16,
	.dfp_mmio_pci_inst = 24,
	.dfp_comps = milan_comps,
	.dfp_comps_count = ARRAY_SIZE(milan_comps),
	.dfp_chan_ileaves = milan_chan_ileaves,
	.dfp_chan_ileaves_count = ARRAY_SIZE(milan_chan_ileaves),
	.dfp_umc_count = ARRAY_SIZE(milan_chan_map),
	.dfp_umc_chan_map = milan_chan_map,
	.dfp_umc_order = milan_chan_umc_order,
};
