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
 * This implements RAS bank identification and defines bits that must be set in
 * bank mask registers on Genoa.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/sysmacros.h>

#include <sys/io/turin/ras_impl.h>
#include <sys/io/zen/ras_impl.h>

/*
 * Maps from the hardware ID and MCA "type" fields in the IP ID register to a
 * generic bank type.
 */
static const zen_ras_bank_type_map_t turin_ras_bank_type_map[] = {
	/*
	 * These constants are taken from the PPR and seem mostly arbitrary.
	 * Note that the ordering here mirrors the table in the PPR for easy
	 * cross-reference.
	 */
	{ 0xb0, 0x00, ZEN_RBT_LS },
	{ 0xb0, 0x01, ZEN_RBT_IF },
	{ 0xb0, 0x02, ZEN_RBT_L2 },
	{ 0xb0, 0x07, ZEN_RBT_L3 },
	{ 0x01, 0x02, ZEN_RBT_MP5 },
	{ 0x241, 0x0, ZEN_RBT_PCS_GMI },
	{ 0x269, 0x0, ZEN_RBT_KPX_GMI },
	{ 0x01, 0x03, ZEN_RBT_MPDMA },
	{ 0x96, 0x00, ZEN_RBT_UMC },
	{ 0x46, 0x01, ZEN_RBT_PCIE },
	{ 0xa8, 0x00, ZEN_RBT_SATA },
	{ 0xaa, 0x00, ZEN_RBT_USB },
	{ 0x18, 0x00, ZEN_RBT_NBIO },
	{ 0x6c, 0x00, ZEN_RBT_NBIF },
	{ 0x01, 0x01, ZEN_RBT_SMU },
	{ 0x80, 0x00, ZEN_RBT_SHUB },
	{ 0x2e, 0x01, ZEN_RBT_PIE },
	{ 0xff, 0x01, ZEN_RBT_PSP },
	{ 0x50, 0x00, ZEN_RBT_PCS_XGMI },
	{ 0x259, 0x0, ZEN_RBT_KPX_SERDES },
	{ 0x2e, 0x02, ZEN_RBT_CS },
	{ 0xb0, 0x05, ZEN_RBT_EX },
	{ 0xb0, 0x06, ZEN_RBT_FP },
	{ 0xb0, 0x03, ZEN_RBT_DE },
};

/*
 * Bits we set in MCA_CTL_MASK_LS.
 */
static const uint_t turin_ras_ls_mask_bits[] = {
	TURIN_RAS_MASK_LS_SYS_RD_DATA_WCB,
	TURIN_RAS_MASK_LS_SYS_RD_DATA_MAB,
	TURIN_RAS_MASK_LS_SYS_RD_DATA_UCODE,
};

/*
 * Bits we set in MCA_CTL_MASK_IF.
 */
static const uint_t turin_ras_if_mask_bits[] = {
	TURIN_RAS_MASK_IF_L2_SYS_DATA_RD_ERR,
};

/*
 * Bits we set in MCA_CTL_MASK_L2.
 */
static const uint_t turin_ras_l2_mask_bits[] = {
	TURIN_RAS_MASK_L2_HWA,
};

/*
 * Bits we set in MCA_CTL_MASK_FP.
 */
static const uint_t turin_ras_fp_mask_bits[] = {
	TURIN_RAS_MASK_FP_HWA,
};

/*
 * Bits we set in MCA_CTL_MASK_NBIO.
 */
static const uint_t turin_ras_nbio_mask_bits[] = {
	TURIN_RAS_MASK_NBIO_EXT_SDP_ERR_EVT,
	TURIN_RAS_MASK_NBIO_PCIE_SB,
};

/*
 * The map of bank types to bits we have to initialize in a bank of that type's
 * mask control register.
 */
static const zen_ras_bank_mask_bits_t turin_ras_bank_mask_map[] = {
	{
		.zrbmb_bank_type = ZEN_RBT_LS,
		.zrbmb_nbits = ARRAY_SIZE(turin_ras_ls_mask_bits),
		.zrbmb_bits = turin_ras_ls_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_IF,
		.zrbmb_nbits = ARRAY_SIZE(turin_ras_if_mask_bits),
		.zrbmb_bits = turin_ras_if_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_L2,
		.zrbmb_nbits = ARRAY_SIZE(turin_ras_l2_mask_bits),
		.zrbmb_bits = turin_ras_l2_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_FP,
		.zrbmb_nbits = ARRAY_SIZE(turin_ras_fp_mask_bits),
		.zrbmb_bits = turin_ras_fp_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_NBIO,
		.zrbmb_nbits = ARRAY_SIZE(turin_ras_nbio_mask_bits),
		.zrbmb_bits = turin_ras_nbio_mask_bits,
	},
};

/*
 * RAS initialization data for Turin.
 */
const zen_ras_init_data_t turin_ras_init_data = {
	.zrid_bank_type_map = turin_ras_bank_type_map,
	.zrid_bank_type_nmap = ARRAY_SIZE(turin_ras_bank_type_map),
	.zrid_bank_mask_map = turin_ras_bank_mask_map,
	.zrid_bank_mask_nmap = ARRAY_SIZE(turin_ras_bank_mask_map),
};
