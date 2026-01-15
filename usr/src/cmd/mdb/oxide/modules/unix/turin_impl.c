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
 * Shared Turin DF and UMC data.
 */

#include <sys/sysmacros.h>

#include "zen_kmdb_impl.h"


static const char *turin_chan_ileaves[64] = {
	[0] = "1",
	[12] = "NPS1 16S8CH 1K",
	[14] = "NPS0 24CH 1K",
	[16] = "NPS4 2CH 1K",
	[17] = "NPS2 4CH 1K",
	[18] = "NPS1 8S4CH 1K",
	[19] = "NPS4 3CH 1K",
	[20] = "NPS2 6CH 1K",
	[21] = "NPS1 12CH 1K",
	[22] = "NPS2 5CH 1K",
	[23] = "NPS1 10CH 1K",
	[32] = "NPS4 2CH 2K",
	[33] = "NPS2 4CH 2K",
	[34] = "NPS1 8S4CH 2K",
	[35] = "NPS1 16S8CH 2K",
	[36] = "NPS4 3CH 2K",
	[37] = "NPS2 6CH 2K",
	[38] = "NPS1 12CH 2K",
	[39] = "NPS0 24CH 2K",
	[40] = "NPS2 5CH 2K",
	[41] = "NPS2 10CH 2K",
};

const char *turin_chan_map[] = {
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

static const uint8_t turin_chan_umc_order[] = {
    3, 4, 0, 5, 1, 2, 9, 10, 6, 11, 7, 8
};

#define	TURIN_COMP_ENTRY(inst, name, ndram, invalid_dest)	\
	[inst] = {\
		.dc_inst = inst,\
		.dc_name = name,\
		.dc_ndram = ndram,\
		.dc_invalid_dest = invalid_dest\
	}

static const df_comp_t turin_comps[] = {
	TURIN_COMP_ENTRY(0, "UMC0", 4, B_FALSE),
	TURIN_COMP_ENTRY(1, "UMC1", 4, B_FALSE),
	TURIN_COMP_ENTRY(2, "UMC2", 4, B_FALSE),
	TURIN_COMP_ENTRY(3, "UMC3", 4, B_FALSE),
	TURIN_COMP_ENTRY(4, "UMC4", 4, B_FALSE),
	TURIN_COMP_ENTRY(5, "UMC5", 4, B_FALSE),
	TURIN_COMP_ENTRY(6, "UMC6", 4, B_FALSE),
	TURIN_COMP_ENTRY(7, "UMC7", 4, B_FALSE),
	TURIN_COMP_ENTRY(8, "UMC8", 4, B_FALSE),
	TURIN_COMP_ENTRY(9, "UMC9", 4, B_FALSE),
	TURIN_COMP_ENTRY(10, "UMC10", 4, B_FALSE),
	TURIN_COMP_ENTRY(11, "UMC11", 4, B_FALSE),
	TURIN_COMP_ENTRY(12, "CMP0", 4, B_FALSE),
	TURIN_COMP_ENTRY(13, "CMP1", 4, B_FALSE),
	TURIN_COMP_ENTRY(14, "CMP2", 4, B_FALSE),
	TURIN_COMP_ENTRY(15, "CMP3", 4, B_FALSE),
	TURIN_COMP_ENTRY(16, "CCM0", 20, B_FALSE),
	TURIN_COMP_ENTRY(17, "CCM1", 20, B_FALSE),
	TURIN_COMP_ENTRY(18, "CCM2", 20, B_FALSE),
	TURIN_COMP_ENTRY(19, "CCM3", 20, B_FALSE),
	TURIN_COMP_ENTRY(20, "CCM4", 20, B_FALSE),
	TURIN_COMP_ENTRY(21, "CCM5", 20, B_FALSE),
	TURIN_COMP_ENTRY(22, "CCM6", 20, B_FALSE),
	TURIN_COMP_ENTRY(23, "CCM7", 20, B_FALSE),
	TURIN_COMP_ENTRY(24, "ACM0", 20, B_FALSE),
	TURIN_COMP_ENTRY(25, "ACM1", 20, B_FALSE),
	TURIN_COMP_ENTRY(26, "ACM2", 20, B_FALSE),
	TURIN_COMP_ENTRY(27, "ACM3", 20, B_FALSE),
	TURIN_COMP_ENTRY(28, "NCM_IOMMU0", 20, B_FALSE),
	TURIN_COMP_ENTRY(29, "NCM_IOMMU1", 20, B_FALSE),
	TURIN_COMP_ENTRY(30, "NCM_IOMMU2", 20, B_FALSE),
	TURIN_COMP_ENTRY(31, "NCM_IOMMU3", 20, B_FALSE),
	TURIN_COMP_ENTRY(32, "IOM0_IOHUBM0", 20, B_FALSE),
	TURIN_COMP_ENTRY(33, "IOM1_IOHUBM1", 20, B_FALSE),
	TURIN_COMP_ENTRY(34, "IOM2_IOHUBM2", 20, B_FALSE),
	TURIN_COMP_ENTRY(35, "IOM3_IOHUBM3", 20, B_FALSE),
	TURIN_COMP_ENTRY(36, "IOM4_IOHUBM4", 20, B_FALSE),
	TURIN_COMP_ENTRY(37, "IOM5_IOHUBM5", 20, B_FALSE),
	TURIN_COMP_ENTRY(38, "IOM6_IOHUBM6", 20, B_FALSE),
	TURIN_COMP_ENTRY(39, "IOM7_IOHUBM7", 20, B_FALSE),
	TURIN_COMP_ENTRY(40, "IOHUBS0", 1, B_FALSE),
	TURIN_COMP_ENTRY(41, "IOHUBS1", 1, B_FALSE),
	TURIN_COMP_ENTRY(42, "IOHUBS2", 1, B_FALSE),
	TURIN_COMP_ENTRY(43, "IOHUBS3", 1, B_FALSE),
	TURIN_COMP_ENTRY(44, "IOHUBS4", 1, B_FALSE),
	TURIN_COMP_ENTRY(45, "IOHUBS5", 1, B_FALSE),
	TURIN_COMP_ENTRY(46, "IOHUBS6", 1, B_FALSE),
	TURIN_COMP_ENTRY(47, "IOHUBS7", 1, B_FALSE),
	TURIN_COMP_ENTRY(48, "ICNG0", 0, B_FALSE),
	TURIN_COMP_ENTRY(49, "ICNG1", 0, B_FALSE),
	TURIN_COMP_ENTRY(50, "ICNG2", 0, B_FALSE),
	TURIN_COMP_ENTRY(51, "ICNG3", 0, B_FALSE),
	TURIN_COMP_ENTRY(52, "PIE0", 20, B_FALSE),
	TURIN_COMP_ENTRY(53, "CAKE_XGMI0", 0, B_TRUE),
	TURIN_COMP_ENTRY(54, "CAKE_XGMI1", 0, B_TRUE),
	TURIN_COMP_ENTRY(55, "CAKE_XGMI2", 0, B_TRUE),
	TURIN_COMP_ENTRY(56, "CAKE_XGMI3", 0, B_TRUE),
	TURIN_COMP_ENTRY(57, "CAKE_XGMI4", 0, B_TRUE),
	TURIN_COMP_ENTRY(58, "CAKE_XGMI5", 0, B_TRUE),
	TURIN_COMP_ENTRY(59, "CNLI0", 0, B_TRUE),
	TURIN_COMP_ENTRY(60, "CNLI1", 0, B_TRUE),
	TURIN_COMP_ENTRY(61, "CNLI2", 0, B_TRUE),
	TURIN_COMP_ENTRY(62, "CNLI3", 0, B_TRUE),
	TURIN_COMP_ENTRY(63, "PFX0", 0, B_TRUE),
	TURIN_COMP_ENTRY(64, "PFX1", 0, B_TRUE),
	TURIN_COMP_ENTRY(65, "PFX2", 0, B_TRUE),
	TURIN_COMP_ENTRY(66, "PFX3", 0, B_TRUE),
	TURIN_COMP_ENTRY(67, "PFX4", 0, B_TRUE),
	TURIN_COMP_ENTRY(68, "PFX5", 0, B_TRUE),
	TURIN_COMP_ENTRY(69, "PFX6", 0, B_TRUE),
	TURIN_COMP_ENTRY(70, "PFX7", 0, B_TRUE),
	TURIN_COMP_ENTRY(71, "SPF0", 8, B_TRUE),
	TURIN_COMP_ENTRY(72, "SPF1", 8, B_TRUE),
	TURIN_COMP_ENTRY(73, "SPF2", 8, B_TRUE),
	TURIN_COMP_ENTRY(74, "SPF3", 8, B_TRUE),
	TURIN_COMP_ENTRY(75, "SPF4", 8, B_TRUE),
	TURIN_COMP_ENTRY(76, "SPF5", 8, B_TRUE),
	TURIN_COMP_ENTRY(77, "SPF6", 8, B_TRUE),
	TURIN_COMP_ENTRY(78, "SPF7", 8, B_TRUE),
	TURIN_COMP_ENTRY(79, "SPF8", 8, B_TRUE),
	TURIN_COMP_ENTRY(80, "SPF9", 8, B_TRUE),
	TURIN_COMP_ENTRY(81, "SPF10", 8, B_TRUE),
	TURIN_COMP_ENTRY(82, "SPF11", 8, B_TRUE),
	TURIN_COMP_ENTRY(83, "SPF12", 8, B_TRUE),
	TURIN_COMP_ENTRY(84, "SPF13", 8, B_TRUE),
	TURIN_COMP_ENTRY(85, "SPF14", 8, B_TRUE),
	TURIN_COMP_ENTRY(86, "SPF15", 8, B_TRUE),
	TURIN_COMP_ENTRY(87, "TCDX0", 0, B_TRUE),
	TURIN_COMP_ENTRY(88, "TCDX1", 0, B_TRUE),
	TURIN_COMP_ENTRY(89, "TCDX2", 0, B_TRUE),
	TURIN_COMP_ENTRY(90, "TCDX3", 0, B_TRUE),
	TURIN_COMP_ENTRY(91, "TCDX4", 0, B_TRUE),
	TURIN_COMP_ENTRY(92, "TCDX5", 0, B_TRUE),
	TURIN_COMP_ENTRY(93, "TCDX6", 0, B_TRUE),
	TURIN_COMP_ENTRY(94, "TCDX7", 0, B_TRUE),
	TURIN_COMP_ENTRY(95, "TCDX8", 0, B_TRUE),
	TURIN_COMP_ENTRY(96, "TCDX9", 0, B_TRUE),
	TURIN_COMP_ENTRY(97, "TCDX10", 0, B_TRUE),
	TURIN_COMP_ENTRY(98, "TCDX11", 0, B_TRUE),
	TURIN_COMP_ENTRY(99, "TCDX12", 0, B_TRUE),
	TURIN_COMP_ENTRY(100, "TCDX13", 0, B_TRUE),
	TURIN_COMP_ENTRY(101, "TCDX14", 0, B_TRUE),
	TURIN_COMP_ENTRY(102, "TCDX15", 0, B_TRUE),
	TURIN_COMP_ENTRY(103, "TCDX16", 0, B_TRUE),
	TURIN_COMP_ENTRY(104, "TCDX17", 0, B_TRUE),
	TURIN_COMP_ENTRY(105, "TCDX18", 0, B_TRUE),
	TURIN_COMP_ENTRY(106, "TCDX19", 0, B_TRUE)
};

df_props_t df_props_turin = {
	.dfp_rev = DF_REV_4D2,
	.dfp_flags = DFPROP_FLAG_PROXY_PCIERW,
	.dfp_max_cfgmap = DF_MAX_CFGMAP_TURIN,
	.dfp_max_iorr = DF_MAX_IO_RULES_TURIN,
	.dfp_max_mmiorr = DF_MAX_MMIO_RULES_TURIN,
	/*
	 * For DRAM, default to CCM0 (we don't use a UMC because it has very few
	 * rules). For I/O ports, use CCM0 as well as the IOMS entries don't
	 * really have rules here. For MMIO and PCI buses, use IOM0_IOHUBM0.
	 */
	.dfp_dram_io_inst = 16,
	.dfp_mmio_pci_inst = 32,
	.dfp_comps = turin_comps,
	.dfp_comps_count = ARRAY_SIZE(turin_comps),
	.dfp_chan_ileaves = turin_chan_ileaves,
	.dfp_chan_ileaves_count = ARRAY_SIZE(turin_chan_ileaves),
	.dfp_umc_count = ARRAY_SIZE(turin_chan_map),
	.dfp_umc_chan_map = turin_chan_map,
	.dfp_umc_order = turin_chan_umc_order,
};
