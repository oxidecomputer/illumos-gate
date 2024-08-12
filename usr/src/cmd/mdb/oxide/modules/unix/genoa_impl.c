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
 * Shared Genoa DF and UMC data.
 */

#include <sys/sysmacros.h>

#include "zen_kmdb_impl.h"


static const char *genoa_chan_ileaves[32] = {
	[0] = "1",
	[1] = "2",
	[3] = "4",
	[5] = "8",
	[7] = "16",
	[8] = "32",
	[16] = "NPS4 2CH",
	[17] = "NPS2 4CH",
	[18] = "NPS1 8CH",
	[19] = "NPS4 3CH",
	[20] = "NPS2 6CH",
	[21] = "NPS1 12CH",
	[22] = "NPS2 5CH",
	[23] = "NPS1 10CH",
};

static const char *genoa_chan_map[] = {
	[0] = "C",
	[1] = "E",
	[2] = "F",
	[3] = "A",
	[4] = "B",
	[5] = "D",
	[6] = "I",
	[7] = "K",
	[8] = "L",
	[9] = "G",
	[10] = "H",
	[11] = "J",
};

static const uint8_t genoa_chan_umc_order[] = {
    3, 4, 0, 5, 1, 2, 9, 10, 6, 11, 7, 8
};

#define	GENOA_COMP_ENTRY(inst, name, ndram, invalid_dest)	\
	[inst] = {\
		.dc_inst = inst,\
		.dc_name = name,\
		.dc_ndram = ndram,\
		.dc_invalid_dest = invalid_dest\
	}

static const df_comp_t genoa_comps[] = {
	GENOA_COMP_ENTRY(0, "UMC0", 4, B_FALSE),
	GENOA_COMP_ENTRY(1, "UMC1", 4, B_FALSE),
	GENOA_COMP_ENTRY(2, "UMC2", 4, B_FALSE),
	GENOA_COMP_ENTRY(3, "UMC3", 4, B_FALSE),
	GENOA_COMP_ENTRY(4, "UMC4", 4, B_FALSE),
	GENOA_COMP_ENTRY(5, "UMC5", 4, B_FALSE),
	GENOA_COMP_ENTRY(6, "UMC6", 4, B_FALSE),
	GENOA_COMP_ENTRY(7, "UMC7", 4, B_FALSE),
	GENOA_COMP_ENTRY(8, "UMC8", 4, B_FALSE),
	GENOA_COMP_ENTRY(9, "UMC9", 4, B_FALSE),
	GENOA_COMP_ENTRY(10, "UMC10", 4, B_FALSE),
	GENOA_COMP_ENTRY(11, "UMC11", 4, B_FALSE),
	GENOA_COMP_ENTRY(12, "CMP0", 4, B_FALSE),
	GENOA_COMP_ENTRY(13, "CMP1", 4, B_FALSE),
	GENOA_COMP_ENTRY(14, "CMP2", 4, B_FALSE),
	GENOA_COMP_ENTRY(15, "CMP3", 4, B_FALSE),
	GENOA_COMP_ENTRY(16, "CCM0", 20, B_FALSE),
	GENOA_COMP_ENTRY(17, "CCM1", 20, B_FALSE),
	GENOA_COMP_ENTRY(18, "CCM2", 20, B_FALSE),
	GENOA_COMP_ENTRY(19, "CCM3", 20, B_FALSE),
	GENOA_COMP_ENTRY(20, "CCM4", 20, B_FALSE),
	GENOA_COMP_ENTRY(21, "CCM5", 20, B_FALSE),
	GENOA_COMP_ENTRY(22, "CCM6", 20, B_FALSE),
	GENOA_COMP_ENTRY(23, "CCM7", 20, B_FALSE),
	GENOA_COMP_ENTRY(24, "ACM0", 20, B_FALSE),
	GENOA_COMP_ENTRY(25, "ACM1", 20, B_FALSE),
	GENOA_COMP_ENTRY(26, "ACM2", 20, B_FALSE),
	GENOA_COMP_ENTRY(27, "ACM3", 20, B_FALSE),
	GENOA_COMP_ENTRY(28, "NCM0_IOMMU0", 20, B_FALSE),
	GENOA_COMP_ENTRY(29, "NCM1_IOMMU1", 20, B_FALSE),
	GENOA_COMP_ENTRY(30, "NCM2_IOMMU2", 20, B_FALSE),
	GENOA_COMP_ENTRY(31, "NCM3_IOMMU3", 20, B_FALSE),
	GENOA_COMP_ENTRY(32, "IOM0_IOHUBM0", 20, B_FALSE),
	GENOA_COMP_ENTRY(33, "IOM1_IOHUBM1", 20, B_FALSE),
	GENOA_COMP_ENTRY(34, "IOM2_IOHUBM2", 20, B_FALSE),
	GENOA_COMP_ENTRY(35, "IOM3_IOHUBM3", 20, B_FALSE),
	GENOA_COMP_ENTRY(36, "IOHUBS0", 1, B_FALSE),
	GENOA_COMP_ENTRY(37, "IOHUBS1", 1, B_FALSE),
	GENOA_COMP_ENTRY(38, "IOHUBS2", 1, B_FALSE),
	GENOA_COMP_ENTRY(39, "IOHUBS3", 1, B_FALSE),
	GENOA_COMP_ENTRY(40, "ICNG0", 0, B_FALSE),
	GENOA_COMP_ENTRY(41, "ICNG1", 0, B_FALSE),
	GENOA_COMP_ENTRY(42, "ICNG2", 0, B_FALSE),
	GENOA_COMP_ENTRY(43, "ICNG3", 0, B_FALSE),
	GENOA_COMP_ENTRY(44, "PIE0", 20, B_FALSE),
	GENOA_COMP_ENTRY(45, "CAKE0", 0, B_TRUE),
	GENOA_COMP_ENTRY(46, "CAKE1", 0, B_TRUE),
	GENOA_COMP_ENTRY(47, "CAKE2", 0, B_TRUE),
	GENOA_COMP_ENTRY(48, "CAKE3", 0, B_TRUE),
	GENOA_COMP_ENTRY(49, "CAKE4", 0, B_TRUE),
	GENOA_COMP_ENTRY(50, "CAKE5", 0, B_TRUE),
	GENOA_COMP_ENTRY(51, "CAKE6", 0, B_TRUE),
	GENOA_COMP_ENTRY(52, "CAKE7", 0, B_TRUE),
	GENOA_COMP_ENTRY(53, "CNLI0", 0, B_TRUE),
	GENOA_COMP_ENTRY(54, "CNLI1", 0, B_TRUE),
	GENOA_COMP_ENTRY(55, "CNLI2", 0, B_TRUE),
	GENOA_COMP_ENTRY(56, "CNLI3", 0, B_TRUE),
	GENOA_COMP_ENTRY(57, "PFX0", 0, B_TRUE),
	GENOA_COMP_ENTRY(58, "PFX1", 0, B_TRUE),
	GENOA_COMP_ENTRY(59, "PFX2", 0, B_TRUE),
	GENOA_COMP_ENTRY(60, "PFX3", 0, B_TRUE),
	GENOA_COMP_ENTRY(61, "PFX4", 0, B_TRUE),
	GENOA_COMP_ENTRY(62, "PFX5", 0, B_TRUE),
	GENOA_COMP_ENTRY(63, "PFX6", 0, B_TRUE),
	GENOA_COMP_ENTRY(64, "PFX7", 0, B_TRUE),
	GENOA_COMP_ENTRY(65, "SPF0", 8, B_TRUE),
	GENOA_COMP_ENTRY(66, "SPF1", 8, B_TRUE),
	GENOA_COMP_ENTRY(67, "SPF2", 8, B_TRUE),
	GENOA_COMP_ENTRY(68, "SPF3", 8, B_TRUE),
	GENOA_COMP_ENTRY(69, "SPF4", 8, B_TRUE),
	GENOA_COMP_ENTRY(70, "SPF5", 8, B_TRUE),
	GENOA_COMP_ENTRY(71, "SPF6", 8, B_TRUE),
	GENOA_COMP_ENTRY(72, "SPF7", 8, B_TRUE),
	GENOA_COMP_ENTRY(73, "SPF8", 8, B_TRUE),
	GENOA_COMP_ENTRY(74, "SPF9", 8, B_TRUE),
	GENOA_COMP_ENTRY(75, "SPF10", 8, B_TRUE),
	GENOA_COMP_ENTRY(76, "SPF11", 8, B_TRUE),
	GENOA_COMP_ENTRY(77, "SPF12", 8, B_TRUE),
	GENOA_COMP_ENTRY(78, "SPF13", 8, B_TRUE),
	GENOA_COMP_ENTRY(79, "SPF14", 8, B_TRUE),
	GENOA_COMP_ENTRY(80, "SPF15", 8, B_TRUE),
	GENOA_COMP_ENTRY(81, "TCDX0", 0, B_TRUE),
	GENOA_COMP_ENTRY(82, "TCDX1", 0, B_TRUE),
	GENOA_COMP_ENTRY(83, "TCDX2", 0, B_TRUE),
	GENOA_COMP_ENTRY(84, "TCDX3", 0, B_TRUE),
	GENOA_COMP_ENTRY(85, "TCDX4", 0, B_TRUE),
	GENOA_COMP_ENTRY(86, "TCDX5", 0, B_TRUE),
	GENOA_COMP_ENTRY(87, "TCDX6", 0, B_TRUE),
	GENOA_COMP_ENTRY(88, "TCDX7", 0, B_TRUE),
	GENOA_COMP_ENTRY(89, "TCDX8", 0, B_TRUE),
	GENOA_COMP_ENTRY(90, "TCDX9", 0, B_TRUE),
	GENOA_COMP_ENTRY(91, "TCDX10", 0, B_TRUE),
	GENOA_COMP_ENTRY(92, "TCDX11", 0, B_TRUE),
	GENOA_COMP_ENTRY(93, "TCDX12", 0, B_TRUE),
	GENOA_COMP_ENTRY(94, "TCDX13", 0, B_TRUE),
	GENOA_COMP_ENTRY(95, "TCDX14", 0, B_TRUE),
	GENOA_COMP_ENTRY(96, "TCDX15", 0, B_TRUE)
};

df_props_t df_props_genoa = {
	.dfp_rev = DF_REV_4,
	.dfp_max_cfgmap = DF_MAX_CFGMAP,
	/*
	 * For DRAM, default to CCM0 (we don't use a UMC because it has very few
	 * rules). For I/O ports, use CCM0 as well as the IOMS entries don't
	 * really have rules here. For MMIO and PCI buses, use IOM0_IOHUBM0.
	 */
	.dfp_dram_io_inst = 16,
	.dfp_mmio_pci_inst = 32,
	.dfp_comps = genoa_comps,
	.dfp_comps_count = ARRAY_SIZE(genoa_comps),
	.dfp_chan_ileaves = genoa_chan_ileaves,
	.dfp_chan_ileaves_count = ARRAY_SIZE(genoa_chan_ileaves),
	.dfp_umc_count = ARRAY_SIZE(genoa_chan_map),
	.dfp_umc_chan_map = genoa_chan_map,
	.dfp_umc_order = genoa_chan_umc_order,
};
