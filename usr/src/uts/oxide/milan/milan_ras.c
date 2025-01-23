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

#include <sys/io/zen/ras_impl.h>
#include <sys/io/milan/ras_impl.h>

/*
 * Maps from the hardware ID and MCA "type" fields in the IP ID register to a
 * generic bank type.
 */
static const zen_ras_bank_type_map_t milan_ras_bank_type_map[] = {
	/*
	 * These constants are taken from the PPR and seem mostly arbitrary.
	 * Note that the ordering here mirrors the table in the PPR for easy
	 * cross-reference.
	 */
	{ 0xb0, 0x10, ZEN_RBT_LS },
	{ 0xb0, 0x01, ZEN_RBT_IF },
	{ 0xb0, 0x02, ZEN_RBT_L2 },
	{ 0xb0, 0x07, ZEN_RBT_L3 },
	{ 0x01, 0x02, ZEN_RBT_MP5 },
	{ 0x05, 0x00, ZEN_RBT_PB },
	{ 0x96, 0x00, ZEN_RBT_UMC },
	{ 0x18, 0x00, ZEN_RBT_NBIO },
	{ 0x46, 0x00, ZEN_RBT_PCIE },
	{ 0x01, 0x01, ZEN_RBT_SMU },
	{ 0xff, 0x01, ZEN_RBT_PSP },
	{ 0x2e, 0x01, ZEN_RBT_PIE },
	{ 0x2e, 0x02, ZEN_RBT_CS },
	{ 0xb0, 0x05, ZEN_RBT_EX },
	{ 0xb0, 0x06, ZEN_RBT_FP },
	{ 0xb0, 0x03, ZEN_RBT_DE },
};

/*
 * Bits we set in MCA_CTL_MASK_LS.
 */
static const uint_t milan_ras_ls_mask_bits[] = {
	MILAN_RAS_MASK_LS_SYS_RD_DATA_LD,
	MILAN_RAS_MASK_LS_SYS_RD_DATA_SCB,
	MILAN_RAS_MASK_LS_SYS_RD_DATA_WCB
};

/*
 * Bits we set in MCA_CTL_MASK_IF.
 */
static const uint_t milan_ras_if_mask_bits[] = {
	MILAN_RAS_MASK_IF_L2_BTB_MULTI,
	MILAN_RAS_MASK_IF_L2_TLB_MULTI,
};

/*
 * Bits we set in MCA_CTL_MASK_L2.
 */
static const uint_t milan_ras_l2_mask_bits[] = {
	MILAN_RAS_MASK_L2_HWA,
};

/*
 * Bits we set in MCA_CTL_MASK_FP.
 */
static const uint_t milan_ras_fp_mask_bits[] = {
	MILAN_RAS_MASK_FP_HWA,
};

/*
 * Bits we set in MCA_CTL_MASK_CS.
 */
static const uint_t milan_ras_cs_mask_bits[] = {
	MILAN_RAS_MASK_CS_FTI_ADDR_VIOL,
};

/*
 * Bits we set in MCA_CTL_MASK_L3.
 */
static const uint_t milan_ras_l3_mask_bits[] = {
	MILAN_RAS_MASK_L3_HWA,
};

/*
 * Bits we set in MCA_CTL_MASK_NBIO.
 */
static const uint_t milan_ras_nbio_mask_bits[] = {
	MILAN_RAS_MASK_NBIO_PCIE_SB,
	MILAN_RAS_MASK_NBIO_PCIE_ERR_EVT,
};

/*
 * The map of bank types to bits we have to initialize in a bank of that type's
 * mask control register.
 */
static const zen_ras_bank_mask_bits_t milan_ras_bank_mask_map[] = {
	{
		.zrbmb_bank_type = ZEN_RBT_LS,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_ls_mask_bits),
		.zrbmb_bits = milan_ras_ls_mask_bits,
	},
	/* These appear to be set by HW/FW; take no chances. */
	{
		.zrbmb_bank_type = ZEN_RBT_IF,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_if_mask_bits),
		.zrbmb_bits = milan_ras_if_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_L2,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_l2_mask_bits),
		.zrbmb_bits = milan_ras_l2_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_FP,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_fp_mask_bits),
		.zrbmb_bits = milan_ras_fp_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_CS,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_cs_mask_bits),
		.zrbmb_bits = milan_ras_cs_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_L3,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_l3_mask_bits),
		.zrbmb_bits = milan_ras_l3_mask_bits,
	},
	{
		.zrbmb_bank_type = ZEN_RBT_NBIO,
		.zrbmb_nbits = ARRAY_SIZE(milan_ras_nbio_mask_bits),
		.zrbmb_bits = milan_ras_nbio_mask_bits,
	},
};

/*
 * RAS initialization data for Milan.
 */
const zen_ras_init_data_t milan_ras_init_data = {
	.zrid_bank_type_map = milan_ras_bank_type_map,
	.zrid_bank_type_nmap = ARRAY_SIZE(milan_ras_bank_type_map),
	.zrid_bank_mask_map = milan_ras_bank_mask_map,
	.zrid_bank_mask_nmap = ARRAY_SIZE(milan_ras_bank_mask_map),
};
