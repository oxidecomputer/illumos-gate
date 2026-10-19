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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _SYS_IO_FCH_ESPI_H
#define	_SYS_IO_FCH_ESPI_H

/*
 * FCH::ITF::ESPI contains eSPI controllers. All supported platforms have at
 * least one of these; Songshan and later have two.
 */

#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The SPI region is controlled by FCH::LPCPCICFG::SPI_BASE_ADDR, a
 * non-standard BAR in the LPC controller's PCI config space. The reset value
 * of this register is FEC1_0000 and ESPI is always SPI + 0x10_000, with
 * eSPI1 another 0x10_000 beyond that on Songshan and beyond. Note that the
 * terminology in the PPRs is 'ESPI' and 'ESPI1'.
 */

#define	FCH_MAX_ESPI			2

#define	FCH_ESPI_PHYS_BASE		0xfec20000
#define	FCH_ESPI_PHYS_STEP		0x10000

#define	FCH_ESPI_SMN_BASE		0x02dc5000
#define	FCH_ESPI_SMN_STEP		0x5000

#define	FCH_ESPI_SIZE			0x170

/*
 * Not all registers are included here; there are far more in the PPRs. These
 * are the ones we use or have used in the past. More can be added as
 * required.
 */

static inline mmio_reg_block_t
fch_espi_mmio_block(const uint8_t unit)
{
	ASSERT3U(unit, <, FCH_MAX_ESPI);

	const mmio_reg_block_phys_t phys = {
		.mrbp_base = FCH_ESPI_PHYS_BASE + unit * FCH_ESPI_PHYS_STEP,
		.mrbp_len = FCH_ESPI_SIZE
	};

	return (mmio_reg_block_map(SMN_UNIT_FCH_ESPI, phys));
}

static inline smn_reg_t
fch_espi_smn_reg(const uint8_t unit, const smn_reg_def_t def,
    const uint8_t count)
{
	ASSERT3U(unit, <, FCH_MAX_ESPI);
	const uint32_t aperture = FCH_ESPI_SMN_BASE + unit * FCH_ESPI_SMN_STEP;

	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_FCH_ESPI);

	return (SMN_MAKE_REG(aperture + def.srd_reg, SMN_UNIT_FCH_ESPI));
}

MAKE_MMIO_FCH_REG_FN(ESPI, espi, 4);

/*
 * FCH::ITF::ESPI::DN_TXHDR_0th -- this register is the first of three that are
 * programmed with bits of information that should be transmitted with a
 * downstream message. The lower 8-bits of this register are common across
 * commands and are where the command type is configured, and the other 24 bits
 * are broken into three 8-bit values, the meaning of which depends on the
 * selected command. Across this register and FCH::ITF::ESPI::DN_TXHDR_[2:1],
 * there are eight such command-specific 8-bit values, HDATA[7:0].
 */
#define	FCH_ESPI_DN_TXHDR0		0x0
#define	D_FCH_ESPI_DN_TXHDR0					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR0			\
	}
#define	FCH_ESPI_DN_TXHDR0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR0, 0)

#define	FCH_ESPI_DN_TXHDR0_GET_HDATA2(r)		bitx32(r, 31, 24)
#define	FCH_ESPI_DN_TXHDR0_SET_HDATA2(r, v)		bitset32(r, 31, 24, v)
/*
 * The peripheral, OOB and Flash channels use HDATA2 for the lower 8 bits of
 * the data length.
 */
#define	FCH_ESPI_DN_TXHDR0_SET_LENL(r, v) FCH_ESPI_DN_TXHDR0_SET_HDATA2(r, v)

#define	FCH_ESPI_DN_TXHDR0_GET_HDATA1(r)		bitx32(r, 23, 16)
#define	FCH_ESPI_DN_TXHDR0_SET_HDATA1(r, v)		bitset32(r, 23, 16, v)
/*
 * The peripheral, OOB and Flash channels subdivide HDATA1 in the same way,
 * into a tag and the high bits of data length.
 */
#define	FCH_ESPI_DN_TXHDR0_SET_TAG(r, v)		bitset32(r, 23, 20, v)
#define	FCH_ESPI_DN_TXHDR0_SET_LENH(r, v)		bitset32(r, 19, 16, v)

#define	FCH_ESPI_DN_TXHDR0_GET_HDATA0(r)		bitx32(r, 15, 8)
#define	FCH_ESPI_DN_TXHDR0_SET_HDATA0(r, v)		bitset32(r, 15, 8, v)

#define	FCH_ESPI_DN_TXHDR0_GET_DNCMD_STATUS(r)		bitx32(r, 3, 3)
#define	FCH_ESPI_DN_TXHDR0_SET_DNCMD_STATUS(r, v)	bitset32(r, 3, 3, v)
#define	FCH_ESPI_DN_TXHDR0_GET_DNCMD_TYPE(r)		bitx32(r, 2, 0)
#define	FCH_ESPI_DN_TXHDR0_SET_DNCMD_TYPE(r, v)		bitset32(r, 2, 0, v)
#define	FCH_ESPI_DN_TXHDR0_TYPE_SETCONF			0
#define	FCH_ESPI_DN_TXHDR0_TYPE_GETCONF			1
#define	FCH_ESPI_DN_TXHDR0_TYPE_RESET			2
#define	FCH_ESPI_DN_TXHDR0_TYPE_PERIPH			4
#define	FCH_ESPI_DN_TXHDR0_TYPE_VW			5
#define	FCH_ESPI_DN_TXHDR0_TYPE_OOB			6
#define	FCH_ESPI_DN_TXHDR0_TYPE_FLASH			7

/*
 * FCH::ITF::ESPI::DN_TXHDR_0th -- as above, but modeled as four separate 8-bit
 * registers to allow portions to be updated with byte writes.
 */
#define	FCH_ESPI_DN_TXHDR0_TYPE		0x0
#define	D_FCH_ESPI_DN_TXHDR0_TYPE				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR0_TYPE,		\
		.srd_size = 1					\
	}
#define	FCH_ESPI_DN_TXHDR0_TYPE_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR0_TYPE, 0)

#define	FCH_ESPI_DN_TXHDR0_HDATA0	0x1
#define	D_FCH_ESPI_DN_TXHDR0_HDATA0				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR0_HDATA0,		\
		.srd_size = 1					\
	}
#define	FCH_ESPI_DN_TXHDR0_HDATA0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR0_HDATA0, 0)

#define	FCH_ESPI_DN_TXHDR0_HDATA1	0x2
#define	D_FCH_ESPI_DN_TXHDR0_HDATA1				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR0_HDATA1,		\
		.srd_size = 1					\
	}
#define	FCH_ESPI_DN_TXHDR0_HDATA1_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR0_HDATA1, 0)

#define	FCH_ESPI_DN_TXHDR0_HDATA2	0x3
#define	D_FCH_ESPI_DN_TXHDR0_HDATA2				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR0_HDATA2,		\
		.srd_size = 1					\
	}
#define	FCH_ESPI_DN_TXHDR0_HDATA2_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR0_HDATA2, 0)

/*
 * FCH::ITF::ESPI::DN_TXHDR_1 -- the second register containing
 * command-specific 8-bit values.
 */
#define	FCH_ESPI_DN_TXHDR1		0x4
#define	D_FCH_ESPI_DN_TXHDR1					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR1			\
	}
#define	FCH_ESPI_DN_TXHDR1_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR1, 0)

#define	FCH_ESPI_DN_TXHDR1_GET_HDATA6(r)		bitx32(r, 31, 24)
#define	FCH_ESPI_DN_TXHDR1_SET_HDATA6(r, v)		bitset32(r, 31, 24, v)
#define	FCH_ESPI_DN_TXHDR1_GET_HDATA5(r)		bitx32(r, 23, 16)
#define	FCH_ESPI_DN_TXHDR1_SET_HDATA5(r, v)		bitset32(r, 23, 16, v)
#define	FCH_ESPI_DN_TXHDR1_GET_HDATA4(r)		bitx32(r, 15, 8)
#define	FCH_ESPI_DN_TXHDR1_SET_HDATA4(r, v)		bitset32(r, 15, 8, v)
#define	FCH_ESPI_DN_TXHDR1_GET_HDATA3(r)		bitx32(r, 7, 0)
#define	FCH_ESPI_DN_TXHDR1_SET_HDATA3(r, v)		bitset32(r, 7, 0, v)

/*
 * FCH::ITF::ESPI::DN_TXHDR_2 -- the third register containing
 * command-specific 8-bit values. In this case just the one.
 */
#define	FCH_ESPI_DN_TXHDR2		0x8
#define	D_FCH_ESPI_DN_TXHDR2					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXHDR2			\
	}
#define	FCH_ESPI_DN_TXHDR2_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXHDR2, 0)

#define	FCH_ESPI_DN_TXHDR2_GET_HDATA7(r)		bitx32(r, 7, 0)
#define	FCH_ESPI_DN_TXHDR2_SET_HDATA7(r, v)		bitset32(r, 7, 0, v)

/*
 * FCH::ITF::ESPI::DN_TXDATA_PORT -- this register is used to load data into
 * the FIFO ready to be sent as part of an eSPI message.
 */
#define	FCH_ESPI_DN_TXDATA_PORT		0xc
#define	D_FCH_ESPI_DN_TXDATA_PORT					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_DN_TXDATA_PORT			\
	}
#define	FCH_ESPI_DN_TXDATA_PORT_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_DN_TXDATA_PORT, 0)

/*
 * FCH::ITF::ESPI::UP_RXHDR_0 -- upstream message receipt register, the first
 * of two. Like the TXHDR registers above, these are broken into
 * command-specific 8-bit values.
 */
#define	FCH_ESPI_UP_RXHDR0		0x10
#define	D_FCH_ESPI_UP_RXHDR0				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_UP_RXHDR0		\
	}
#define	FCH_ESPI_UP_RXHDR0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_UP_RXHDR0, 0)
#define	FCH_ESPI_UP_RXHDR0_GET_HDATA2(v)		bitx32(v, 31, 24)
/*
 * The OOB and Flash channels use HDATA2 for the lower 8 bits of the data
 * length.
 */
#define	FCH_ESPI_UP_RXHDR0_GET_LENL(v) FCH_ESPI_UP_RXHDR0_GET_HDATA2(v)

#define	FCH_ESPI_UP_RXHDR0_GET_HDATA1(v)		bitx32(v, 23, 16)
/*
 * The OOB and Flash channels subdivide HDATA1 in the same way, into a tag and
 * the high bits of data length.
 */
#define	FCH_ESPI_UP_RXHDR0_GET_TAG(v)			bitx32(v, 23, 20)
#define	FCH_ESPI_UP_RXHDR0_GET_LENH(v)			bitx32(v, 19, 16)

#define	FCH_ESPI_UP_RXHDR0_GET_HDATA0(v)		bitx32(v, 15, 8)

#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_STAT(v)		bitx32(v, 3, 3)
#define	FCH_ESPI_UP_RXHDR0_CLEAR_UPCMD_STAT(v)		bitset32(v, 3, 3, 1)
#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_TYPE(v)		bitx32(v, 2, 0)
#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_TYPE_FLASH_NP	0
#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_TYPE_OOB		1
#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_TYPE_FLASH_C	2
#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_TYPE_PUT_FLASH_NP	3
#define	FCH_ESPI_UP_RXHDR0_GET_UPCMD_TYPE_GET_STATUS	4

/*
 * FCH::ITF::ESPI::UP_RXHDR_1 -- the second register containing
 * command-specific 8-bit values.
 */
#define	FCH_ESPI_UP_RXHDR1		0x14
#define	D_FCH_ESPI_UP_RXHDR1				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_UP_RXHDR1		\
	}
#define	FCH_ESPI_UP_RXHDR1_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_UP_RXHDR1, 0)
#define	FCH_ESPI_UP_RXHDR1_GET_HDATA6(v)		bitx32(v, 31, 24)
#define	FCH_ESPI_UP_RXHDR1_GET_HDATA5(v)		bitx32(v, 23, 16)
#define	FCH_ESPI_UP_RXHDR1_GET_HDATA4(v)		bitx32(v, 15, 8)
#define	FCH_ESPI_UP_RXHDR1_GET_HDATA3(v)		bitx32(v, 7, 0)

/*
 * FCH::ITF::ESPI::UP_RXDATA_PORT -- reading this register retrieves data from
 * the FIFO.
 */
#define	FCH_ESPI_UP_RXDATA_PORT		0x18
#define	D_FCH_ESPI_UP_RXDATA_PORT				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_UP_RXDATA_PORT		\
	}
#define	FCH_ESPI_UP_RXDATA_PORT_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_UP_RXDATA_PORT, 0)

/*
 * FCH::ITF::ESPI::RESERVED_REG0 -- miscellaneous status and enable/disable
 * bits for the behaviour of the eSPI controller. These are probably best left
 * at the reset defaults unless there's a good reason to tweak them.
 */
#define	FCH_ESPI_RESERVED_REG0		0x1c
#define	D_FCH_ESPI_RESERVED_REG0				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_RESERVED_REG0		\
	}
#define	FCH_ESPI_RESERVED_REG0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_RESERVED_REG0, 0)

#define	FCH_ESPI_RESERVED_REG0_GET_SAFS_ARM_SM_STAT(r)	bitx32(r, 27, 24)
#define	FCH_ESPI_RESERVED_REG0_GET__ONENP_SM_STAT(r)	bitx32(r, 23, 20)
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT(r)		bitx32(r, 19, 16)
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_IDLE		0
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_SETCONF_VW	1
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_GETCONF_VW	2
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_ACPI_RSTB	3
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_SETCONF_SAFS	4
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_GETCONF_FLASH	5
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_GETCONF_PERIPH	6
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_SUCCESS	7
#define	FCH_ESPI_RESERVED_REG0_INIT_STAT_RESETTING	8
#define	FCH_ESPI_RESERVED_REG0_GET_CYCLE_MM_EN(r)	bitx32(r, 4, 4)
#define	FCH_ESPI_RESERVED_REG0_GET_WDG_RETRY_EN(r)	bitx32(r, 3, 3)
#define	FCH_ESPI_RESERVED_REG0_GET_NP_WDG_CLR_DIS(r)	bitx32(r, 2, 2)
#define	FCH_ESPI_RESERVED_REG0_GET_ROMR_ATT_EN(r)	bitx32(r, 1, 1)
#define	FCH_ESPI_RESERVED_REG0_GET_LERR_EUP_EN(r)	bitx32(r, 0, 0)

/*
 * FCH::ITF::ESPI::ESPI_MISC_CONTROL_REG0 -- a mostly read-only register that
 * reflects the live status of various bits in the eSPI target's status
 * register as seen in the last target response, and a few more behaviour
 * enable/disable bits. These are also probably best left at the reset defaults
 * unless there's a good reason to tweak them.
 */
#define	FCH_ESPI_MISC_CTL0		0x20
#define	D_FCH_ESPI_MISC_CTL0				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_MISC_CTL0		\
	}
#define	FCH_ESPI_MISC_CTL0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_MISC_CTL0, 0)

#define	FCH_ESPI_MISC_CTL0_GET_FLASH_NP_AVAIL(r)	bitx32(r, 29, 29)
#define	FCH_ESPI_MISC_CTL0_GET_FLASH_C_AVAIL(r)		bitx32(r, 28, 28)
#define	FCH_ESPI_MISC_CTL0_GET_FLASH_NP_FREE(r)		bitx32(r, 25, 25)
#define	FCH_ESPI_MISC_CTL0_GET_FLASH_C_FREE(r)		bitx32(r, 24, 24)
#define	FCH_ESPI_MISC_CTL0_GET_OOB_AVAIL(r)		bitx32(r, 23, 23)
#define	FCH_ESPI_MISC_CTL0_GET_VW_AVAIL(r)		bitx32(r, 22, 22)
#define	FCH_ESPI_MISC_CTL0_GET_NP_AVAIL(r)		bitx32(r, 21, 21)
#define	FCH_ESPI_MISC_CTL0_GET_PC_AVAIL(r)		bitx32(r, 20, 20)
#define	FCH_ESPI_MISC_CTL0_GET_OOB_FREE(r)		bitx32(r, 19, 19)
#define	FCH_ESPI_MISC_CTL0_GET_VW_FREE(r)		bitx32(r, 18, 18)
#define	FCH_ESPI_MISC_CTL0_GET_NP_FREE(r)		bitx32(r, 17, 17)
#define	FCH_ESPI_MISC_CTL0_GET_PC_FREE(r)		bitx32(r, 16, 16)

#define	FCH_ESPI_MISC_CTL0_GET_LPCRST_1US_DIS(r)	bitx32(r, 15, 15)
#define	FCH_ESPI_MISC_CTL0_GET_NPWDGTOENH_DIS(r)	bitx32(r, 12, 12)
#define	FCH_ESPI_MISC_CTL0_GET_SMIBEB_DIS(r)		bitx32(r, 11, 11)
#define	FCH_ESPI_MISC_CTL0_GET_NF_V1P0_DIS(r)		bitx32(r, 10, 10)
#define	FCH_ESPI_MISC_CTL0_GET_DEFER_GETPC_FATAL_DIS(r)	bitx32(r, 9, 9)
#define	FCH_ESPI_MISC_CTL0_GET_NONROM_PREF_DIS(r)	bitx32(r, 8, 8)
#define	FCH_ESPI_MISC_CTL0_GET_IO80_NP_FREE_EN(r)	bitx32(r, 7, 7)
#define	FCH_ESPI_MISC_CTL0_GET_ROMREAD_FREE_DIS(r)	bitx32(r, 6, 6)
#define	FCH_ESPI_MISC_CTL0_GET_MEMWR_LEN_DIS(r)		bitx32(r, 3, 3)
#define	FCH_ESPI_MISC_CTL0_GET_TARRM_DOUTEN_DIS(r)	bitx32(r, 2, 2)
#define	FCH_ESPI_MISC_CTL0_GET_PREFETCH_RETRY_DIS(r)	bitx32(r, 1, 1)
#define	FCH_ESPI_MISC_CTL0_GET_OOB_LEN_LIM_EN(r)	bitx32(r, 0, 0)
#define	FCH_ESPI_MISC_CTL0_SET_OOB_LEN_LIM_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::ITF::ESPI::MASTER_CAP -- eSPI controller capability bits. These are all
 * read-only and fixed.
 */
#define	FCH_ESPI_MASTER_CAP		0x2c
#define	D_FCH_ESPI_MASTER_CAP				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_MASTER_CAP		\
	}
#define	FCH_ESPI_MASTER_CAP_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_MASTER_CAP, 0)

#define	FCH_ESPI_MASTER_CAP_GET_CRC(r)			bitx32(r, 31, 31)
#define	FCH_ESPI_MASTER_CAP_GET_ALERT(r)		bitx32(r, 30, 30)
#define	FCH_ESPI_MASTER_CAP_GET_IOMODE(r)		bitx32(r, 29, 28)
#define	FCH_ESPI_MASTER_CAP_GET_CLKFREQ(r)		bitx32(r, 27, 25)
#define	FCH_ESPI_MASTER_CAP_GET_SLV_NUM(r)		bitx32(r, 24, 22)
#define	FCH_ESPI_MASTER_CAP_GET_PR_MAXSZ(r)		bitx32(r, 21, 19)
#define	FCH_ESPI_MASTER_CAP_GET_VW_MAXSZ(r)		bitx32(r, 18, 13)
#define	FCH_ESPI_MASTER_CAP_GET_OOB_MAXSZ(r)		bitx32(r, 12, 10)
#define	FCH_ESPI_MASTER_CAP_GET_FLASH_MAXSZ(r)		bitx32(r, 9, 7)
#define	FCH_ESPI_MASTER_CAP_GET_VER(r)			bitx32(r, 6, 4)
#define	FCH_ESPI_MASTER_CAP_VER_0_7			0x0
#define	FCH_ESPI_MASTER_CAP_VER_0_75			0x1
#define	FCH_ESPI_MASTER_CAP_VER_1_0			0x2
#define	FCH_ESPI_MASTER_CAP_GET_PR(r)			bitx32(r, 3, 3)
#define	FCH_ESPI_MASTER_CAP_GET_VW(r)			bitx32(r, 2, 2)
#define	FCH_ESPI_MASTER_CAP_GET_OOB(r)			bitx32(r, 1, 1)
#define	FCH_ESPI_MASTER_CAP_GET_FLASH(r)		bitx32(r, 0, 0)

/*
 * FCH::ITF::ESPI::GLOBAL_CONTROL_0 -- watchdog, wait state and clock gating
 * controls for the controller as a whole.
 */
#define	FCH_ESPI_GLOBAL_CTL0		0x30
#define	D_FCH_ESPI_GLOBAL_CTL0					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_GLOBAL_CTL0			\
	}
#define	FCH_ESPI_GLOBAL_CTL0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_GLOBAL_CTL0, 0)

#define	FCH_ESPI_GLOBAL_CTL0_GET_SAFS_CLK_GATE_EN(r)	bitx32(r, 31, 31)
#define	FCH_ESPI_GLOBAL_CTL0_SET_SAFS_CLK_GATE_EN(r, v)	bitset32(r, 31, 31, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_PR_RST_EN_PLTRST(r)	bitx32(r, 30, 30)
#define	FCH_ESPI_GLOBAL_CTL0_SET_PR_RST_EN_PLTRST(r, v)	bitset32(r, 30, 30, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_WAIT_CNT(r)		bitx32(r, 29, 24)
#define	FCH_ESPI_GLOBAL_CTL0_SET_WAIT_CNT(r, v)		bitset32(r, 29, 24, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_WDG_CNT(r)		bitx32(r, 23, 8)
#define	FCH_ESPI_GLOBAL_CTL0_SET_WDG_CNT(r, v)		bitset32(r, 23, 8, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_DBGCLK_GATING_EN(r)	bitx32(r, 7, 7)
#define	FCH_ESPI_GLOBAL_CTL0_SET_DBGCLK_GATING_EN(r, v)	bitset32(r, 7, 7, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_AL_IDLE_TIMER(r)	bitx32(r, 6, 4)
#define	FCH_ESPI_GLOBAL_CTL0_SET_AL_IDLE_TIMER(r, v)	bitset32(r, 6, 4, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_AL_STOP_EN(r)		bitx32(r, 3, 3)
#define	FCH_ESPI_GLOBAL_CTL0_SET_AL_STOP_EN(r, v)	bitset32(r, 3, 3, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_PR_CLKGAT_EN(r)	bitx32(r, 2, 2)
#define	FCH_ESPI_GLOBAL_CTL0_SET_PR_CLKGAT_EN(r, v)	bitset32(r, 2, 2, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_WAIT_CHKEN(r)		bitx32(r, 1, 1)
#define	FCH_ESPI_GLOBAL_CTL0_SET_WAIT_CHKEN(r, v)	bitset32(r, 1, 1, v)
#define	FCH_ESPI_GLOBAL_CTL0_GET_WDG_EN(r)		bitx32(r, 0, 0)
#define	FCH_ESPI_GLOBAL_CTL0_SET_WDG_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::ITF::ESPI::GLOBAL_CONTROL_1 -- ROM address translation, alert
 * observation, interrupt mapping and decode controls. The two interrupt
 * mapping fields select which of the eSPI IRQ0-23 lines, or SMI, the
 * controller raises for register command completion and for errors.
 */
#define	FCH_ESPI_GLOBAL_CTL1		0x34
#define	D_FCH_ESPI_GLOBAL_CTL1					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_GLOBAL_CTL1			\
	}
#define	FCH_ESPI_GLOBAL_CTL1_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_GLOBAL_CTL1, 0)

#define	FCH_ESPI_GLOBAL_CTL1_GET_ROM_ADDR_XOR_SET3(r)	bitx32(r, 31, 30)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ROM_ADDR_XOR_SET3(r, v) bitset32(r, 31, 30, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_ROM_ADDR_XOR_SET2(r)	bitx32(r, 29, 28)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ROM_ADDR_XOR_SET2(r, v) bitset32(r, 29, 28, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_ROM_ADDR_XOR_SET1(r)	bitx32(r, 27, 26)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ROM_ADDR_XOR_SET1(r, v) bitset32(r, 27, 26, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_ROM_ADDR_XOR_SET0(r)	bitx32(r, 25, 24)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ROM_ADDR_XOR_SET0(r, v) bitset32(r, 25, 24, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_REQ_NOTWITH_VW_REQ(r)	bitx32(r, 21, 21)
#define	FCH_ESPI_GLOBAL_CTL1_SET_REQ_NOTWITH_VW_REQ(r, v) \
	bitset32(r, 21, 21, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_ALERT_ENABLE(r)	bitx32(r, 20, 20)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ALERT_ENABLE(r, v)	bitset32(r, 20, 20, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_FL_REQ_VALID_DIS(r)	bitx32(r, 19, 19)
#define	FCH_ESPI_GLOBAL_CTL1_SET_FL_REQ_VALID_DIS(r, v)	bitset32(r, 19, 19, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_ROM_SHARING_DIS(r)	bitx32(r, 18, 18)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ROM_SHARING_DIS(r, v)	bitset32(r, 18, 18, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_RGCMD_INT_MAP(r)	bitx32(r, 17, 13)
#define	FCH_ESPI_GLOBAL_CTL1_SET_RGCMD_INT_MAP(r, v)	bitset32(r, 17, 13, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_ERR_INT_MAP(r)		bitx32(r, 12, 8)
#define	FCH_ESPI_GLOBAL_CTL1_SET_ERR_INT_MAP(r, v)	bitset32(r, 12, 8, v)
#define	FCH_ESPI_GLOBAL_CTL1_INT_MAP_IRQ(n)		(n)
#define	FCH_ESPI_GLOBAL_CTL1_INT_MAP_SMI		0x1f
#define	FCH_ESPI_GLOBAL_CTL1_GET_SUB_DECODE_EN(r)	bitx32(r, 2, 2)
#define	FCH_ESPI_GLOBAL_CTL1_SET_SUB_DECODE_EN(r, v)	bitset32(r, 2, 2, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_BUS_MASTER_EN(r)	bitx32(r, 1, 1)
#define	FCH_ESPI_GLOBAL_CTL1_SET_BUS_MASTER_EN(r, v)	bitset32(r, 1, 1, v)
#define	FCH_ESPI_GLOBAL_CTL1_GET_SW_RST(r)		bitx32(r, 0, 0)
#define	FCH_ESPI_GLOBAL_CTL1_SET_SW_RST(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::ITF::ESPI::SEMAPHORE_MISC_CONTROL_REG0 -- semaphore register used to
 * co-ordinate access authority.
 * There are a number of well defined semaphore owners, and a more general
 * 8-bit identifier that can be used for any additional requirements.
 *
 *	SW0	- Reserved for the ASP
 *	SW1	- Reserved for MP1
 *	SW2	- For x86 to use
 *	SW3	- For x86 to use
 *	SW4	- additional
 */
#define	FCH_ESPI_SEM_MISC_CTL_REG0	0x38
#define	D_FCH_ESPI_SEM_MISC_CTL_REG0				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_SEM_MISC_CTL_REG0		\
	}
#define	FCH_ESPI_SEM_MISC_CTL_REG0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_SEM_MISC_CTL_REG0, 0)

#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW3_OWN_CLR(r)	 bitx32(r, 30, 30)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW3_OWN_CLR(r, v) bitset32(r, 30, 30, v)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW3_OWN_SET(r)	 bitx32(r, 29, 29)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW3_OWN_SET(r, v) bitset32(r, 29, 29, v)

#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_CLR(r)	 bitx32(r, 26, 26)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_CLR(r, v) bitset32(r, 26, 26, v)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_SET(r)	 bitx32(r, 25, 25)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_SET_SW2_OWN_SET(r, v) bitset32(r, 25, 25, v)

#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW3_OWN_STAT(r)	 bitx32(r, 28, 28)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW2_OWN_STAT(r)	 bitx32(r, 24, 24)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW1_OWN_STAT(r)	 bitx32(r, 20, 20)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW0_OWN_STAT(r)	 bitx32(r, 16, 16)
#define	FCH_ESPI_SEM_MISC_CTL_REG0_GET_SW4_USER_ID(r)	 bitx32(r, 15, 8)

/*
 * FCH::ITF::ESPI::SLAVE0_DECODE_EN -- enables for the peripheral channel
 * address decode ranges below and for the fixed legacy I/O ports. I/O ranges
 * 0-3 are enabled by bits 8-11 and ranges 4-15 by bits 16-27. MMIO ranges 0-3
 * are enabled by bits 12-15 and range 4 by bit 28.
 */
#define	FCH_ESPI_S0_DECODE_EN		0x40
#define	D_FCH_ESPI_S0_DECODE_EN					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_DECODE_EN		\
	}
#define	FCH_ESPI_S0_DECODE_EN_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_DECODE_EN, 0)

#define	FCH_ESPI_S0_DECODE_EN_IO_RANGE_BIT(n)	((n) < 4 ? 8 + (n) : 12 + (n))
#define	FCH_ESPI_S0_DECODE_EN_GET_IO_RANGE_EN(r, n)		\
	bitx32(r, FCH_ESPI_S0_DECODE_EN_IO_RANGE_BIT(n),	\
	FCH_ESPI_S0_DECODE_EN_IO_RANGE_BIT(n))
#define	FCH_ESPI_S0_DECODE_EN_SET_IO_RANGE_EN(r, n, v)		\
	bitset32(r, FCH_ESPI_S0_DECODE_EN_IO_RANGE_BIT(n),	\
	FCH_ESPI_S0_DECODE_EN_IO_RANGE_BIT(n), v)
#define	FCH_ESPI_S0_DECODE_EN_MMIO_RANGE_BIT(n)	((n) < 4 ? 12 + (n) : 24 + (n))
#define	FCH_ESPI_S0_DECODE_EN_GET_MMIO_RANGE_EN(r, n)		\
	bitx32(r, FCH_ESPI_S0_DECODE_EN_MMIO_RANGE_BIT(n),	\
	FCH_ESPI_S0_DECODE_EN_MMIO_RANGE_BIT(n))
#define	FCH_ESPI_S0_DECODE_EN_SET_MMIO_RANGE_EN(r, n, v)	\
	bitset32(r, FCH_ESPI_S0_DECODE_EN_MMIO_RANGE_BIT(n),	\
	FCH_ESPI_S0_DECODE_EN_MMIO_RANGE_BIT(n), v)
#define	FCH_ESPI_S0_DECODE_EN_GET_IO_80_EN(r)		bitx32(r, 2, 2)
#define	FCH_ESPI_S0_DECODE_EN_SET_IO_80_EN(r, v)	bitset32(r, 2, 2, v)
#define	FCH_ESPI_S0_DECODE_EN_GET_IO_60_AND_64_EN(r)	bitx32(r, 1, 1)
#define	FCH_ESPI_S0_DECODE_EN_SET_IO_60_AND_64_EN(r, v)	bitset32(r, 1, 1, v)
#define	FCH_ESPI_S0_DECODE_EN_GET_IO_2E_TO_2F_EN(r)	bitx32(r, 0, 0)
#define	FCH_ESPI_S0_DECODE_EN_SET_IO_2E_TO_2F_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * FCH::ITF::ESPI::SLAVE0_IO_BASE_REG[7:0] -- base addresses of the sixteen
 * peripheral channel I/O decode ranges, two 16-bit addresses per register.
 * The registers are not contiguous. For these and the other numbered
 * registers below, the register index is pasted into the register name and so
 * must be an integer constant.
 */
#define	FCH_ESPI_S0_IO_BASE0		0x44
#define	FCH_ESPI_S0_IO_BASE1		0x48
#define	FCH_ESPI_S0_IO_BASE2		0x80
#define	FCH_ESPI_S0_IO_BASE3		0x84
#define	FCH_ESPI_S0_IO_BASE4		0x8c
#define	FCH_ESPI_S0_IO_BASE5		0x90
#define	FCH_ESPI_S0_IO_BASE6		0xb0
#define	FCH_ESPI_S0_IO_BASE7		0xb4
#define	D_FCH_ESPI_S0_IO_BASE(n)				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_IO_BASE ## n		\
	}
#define	FCH_ESPI_S0_IO_BASE_MMIO(b, n)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_IO_BASE(n), 0)

#define	FCH_ESPI_S0_IO_BASE_GET_HI(r)		bitx32(r, 31, 16)
#define	FCH_ESPI_S0_IO_BASE_SET_HI(r, v)	bitset32(r, 31, 16, v)
#define	FCH_ESPI_S0_IO_BASE_GET_LO(r)		bitx32(r, 15, 0)
#define	FCH_ESPI_S0_IO_BASE_SET_LO(r, v)	bitset32(r, 15, 0, v)

/*
 * FCH::ITF::ESPI::SLAVE0_IO_SIZE[3:0] -- sizes of the sixteen I/O decode
 * ranges, four 8-bit sizes per register. The PPR names the first of these
 * SLAVE0_IO_SIZE with no suffix.
 */
#define	FCH_ESPI_S0_IO_SIZE0		0x4c
#define	FCH_ESPI_S0_IO_SIZE1		0x88
#define	FCH_ESPI_S0_IO_SIZE2		0x94
#define	FCH_ESPI_S0_IO_SIZE3		0xb8
#define	D_FCH_ESPI_S0_IO_SIZE(n)				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_IO_SIZE ## n		\
	}
#define	FCH_ESPI_S0_IO_SIZE_MMIO(b, n)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_IO_SIZE(n), 0)

#define	FCH_ESPI_S0_IO_SIZE_GET_RANGE(r, i)	\
	bitx32(r, 8 * (i) + 7, 8 * (i))
#define	FCH_ESPI_S0_IO_SIZE_SET_RANGE(r, i, v)	\
	bitset32(r, 8 * (i) + 7, 8 * (i), v)

/*
 * FCH::ITF::ESPI::SLAVE0_MMIO_BASE_REG[4:0] -- base addresses of the five
 * peripheral channel MMIO decode ranges, one per register.
 */
#define	FCH_ESPI_S0_MMIO_BASE0		0x50
#define	FCH_ESPI_S0_MMIO_BASE1		0x54
#define	FCH_ESPI_S0_MMIO_BASE2		0x58
#define	FCH_ESPI_S0_MMIO_BASE3		0x5c
#define	FCH_ESPI_S0_MMIO_BASE4		0xbc
#define	D_FCH_ESPI_S0_MMIO_BASE(n)				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_MMIO_BASE ## n		\
	}
#define	FCH_ESPI_S0_MMIO_BASE_MMIO(b, n)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_MMIO_BASE(n), 0)

/*
 * FCH::ITF::ESPI::SLAVE0_MMIO_SIZE_REG[2:0] -- sizes of the MMIO decode
 * ranges. Registers 0 and 1 hold two 16-bit sizes each for ranges 0-3.
 * Register 2 holds a single 25-bit size for range 4.
 */
#define	FCH_ESPI_S0_MMIO_SIZE0		0x60
#define	FCH_ESPI_S0_MMIO_SIZE1		0x64
#define	FCH_ESPI_S0_MMIO_SIZE2		0xc0
#define	D_FCH_ESPI_S0_MMIO_SIZE(n)				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_MMIO_SIZE ## n		\
	}
#define	FCH_ESPI_S0_MMIO_SIZE_MMIO(b, n)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_MMIO_SIZE(n), 0)

#define	FCH_ESPI_S0_MMIO_SIZE_GET_HI(r)		bitx32(r, 31, 16)
#define	FCH_ESPI_S0_MMIO_SIZE_SET_HI(r, v)	bitset32(r, 31, 16, v)
#define	FCH_ESPI_S0_MMIO_SIZE_GET_LO(r)		bitx32(r, 15, 0)
#define	FCH_ESPI_S0_MMIO_SIZE_SET_LO(r, v)	bitset32(r, 15, 0, v)
#define	FCH_ESPI_S0_MMIO_SIZE2_GET_RANGE4(r)	bitx32(r, 24, 0)
#define	FCH_ESPI_S0_MMIO_SIZE2_SET_RANGE4(r, v)	bitset32(r, 24, 0, v)

/*
 * FCH::ITF::ESPI::SLAVE0_CONFIG -- the controller's view of the target's
 * configuration. Software programs this to match what it has configured in
 * the target with SET_CONFIGURATION, which is why the fields mirror the
 * target's general and channel capability registers.
 */
#define	FCH_ESPI_S0_CONFIG		0x68
#define	D_FCH_ESPI_S0_CONFIG					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_CONFIG			\
	}
#define	FCH_ESPI_S0_CONFIG_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_CONFIG, 0)

#define	FCH_ESPI_S0_CONFIG_GET_CRC_CHECK_EN(r)		bitx32(r, 31, 31)
#define	FCH_ESPI_S0_CONFIG_SET_CRC_CHECK_EN(r, v)	bitset32(r, 31, 31, v)
#define	FCH_ESPI_S0_CONFIG_GET_ALERT_MODE_SEL(r)	bitx32(r, 30, 30)
#define	FCH_ESPI_S0_CONFIG_SET_ALERT_MODE_SEL(r, v)	bitset32(r, 30, 30, v)
#define	FCH_ESPI_S0_CONFIG_ALERT_MODE_IO1		0
#define	FCH_ESPI_S0_CONFIG_ALERT_MODE_PIN		1
#define	FCH_ESPI_S0_CONFIG_GET_IO_MODE_SEL(r)		bitx32(r, 29, 28)
#define	FCH_ESPI_S0_CONFIG_SET_IO_MODE_SEL(r, v)	bitset32(r, 29, 28, v)
#define	FCH_ESPI_S0_CONFIG_IO_MODE_SINGLE		0
#define	FCH_ESPI_S0_CONFIG_IO_MODE_DUAL			1
#define	FCH_ESPI_S0_CONFIG_IO_MODE_QUAD			2
#define	FCH_ESPI_S0_CONFIG_GET_CLK_FREQ_SEL(r)		bitx32(r, 27, 25)
#define	FCH_ESPI_S0_CONFIG_SET_CLK_FREQ_SEL(r, v)	bitset32(r, 27, 25, v)
#define	FCH_ESPI_S0_CONFIG_CLK_FREQ_20MHZ		0
#define	FCH_ESPI_S0_CONFIG_CLK_FREQ_25MHZ		1
#define	FCH_ESPI_S0_CONFIG_CLK_FREQ_33MHZ		2
#define	FCH_ESPI_S0_CONFIG_CLK_FREQ_50MHZ		3
#define	FCH_ESPI_S0_CONFIG_CLK_FREQ_66MHZ		4
#define	FCH_ESPI_S0_CONFIG_CLK_FREQ_16MHZ		6
#define	FCH_ESPI_S0_CONFIG_GET_FLASH_MODIFIER_EN(r)	bitx32(r, 11, 11)
#define	FCH_ESPI_S0_CONFIG_SET_FLASH_MODIFIER_EN(r, v)	bitset32(r, 11, 11, v)
#define	FCH_ESPI_S0_CONFIG_GET_SAFS_DEFER_VALID_EN(r)	bitx32(r, 10, 10)
#define	FCH_ESPI_S0_CONFIG_SET_SAFS_DEFER_VALID_EN(r, v) bitset32(r, 10, 10, v)
#define	FCH_ESPI_S0_CONFIG_GET_PUT_FLASH_NP_HDR_EN(r)	bitx32(r, 9, 9)
#define	FCH_ESPI_S0_CONFIG_SET_PUT_FLASH_NP_HDR_EN(r, v) bitset32(r, 9, 9, v)
#define	FCH_ESPI_S0_CONFIG_GET_PUT_FLASH_NP_HDR_DATA_EN(r)	bitx32(r, 8, 8)
#define	FCH_ESPI_S0_CONFIG_SET_PUT_FLASH_NP_HDR_DATA_EN(r, v)	\
	bitset32(r, 8, 8, v)
#define	FCH_ESPI_S0_CONFIG_GET_FLASH_MPS(r)		bitx32(r, 7, 5)
#define	FCH_ESPI_S0_CONFIG_SET_FLASH_MPS(r, v)		bitset32(r, 7, 5, v)
#define	FCH_ESPI_S0_CONFIG_FLASH_MPS_64			1
#define	FCH_ESPI_S0_CONFIG_FLASH_MPS_128		2
#define	FCH_ESPI_S0_CONFIG_FLASH_MPS_256		3
#define	FCH_ESPI_S0_CONFIG_GET_FLASH_SHARING_MODE(r)	bitx32(r, 4, 4)
#define	FCH_ESPI_S0_CONFIG_SET_FLASH_SHARING_MODE(r, v)	bitset32(r, 4, 4, v)
#define	FCH_ESPI_S0_CONFIG_FLASH_SHARING_MAFS		0
#define	FCH_ESPI_S0_CONFIG_FLASH_SHARING_SAFS		1
#define	FCH_ESPI_S0_CONFIG_GET_PR_EN(r)			bitx32(r, 3, 3)
#define	FCH_ESPI_S0_CONFIG_SET_PR_EN(r, v)		bitset32(r, 3, 3, v)
#define	FCH_ESPI_S0_CONFIG_GET_VW_EN(r)			bitx32(r, 2, 2)
#define	FCH_ESPI_S0_CONFIG_SET_VW_EN(r, v)		bitset32(r, 2, 2, v)
#define	FCH_ESPI_S0_CONFIG_GET_OOB_EN(r)		bitx32(r, 1, 1)
#define	FCH_ESPI_S0_CONFIG_SET_OOB_EN(r, v)		bitset32(r, 1, 1, v)
#define	FCH_ESPI_S0_CONFIG_GET_FLASH_EN(r)		bitx32(r, 0, 0)
#define	FCH_ESPI_S0_CONFIG_SET_FLASH_EN(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::ITF::ESPI::SLAVE0_INT_EN -- interrupt enable register; each
 * non-reserved bit corresponding to a different interrupt.
 */
#define	FCH_ESPI_S0_INT_EN		0x6c
#define	D_FCH_ESPI_S0_INT_EN				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_INT_EN		\
	}
#define	FCH_ESPI_S0_INT_EN_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_INT_EN, 0)

#define	FCH_ESPI_S0_INT_EN_GET_FLASHREQ(r)		bitx32(r, 31, 31)
#define	FCH_ESPI_S0_INT_EN_SET_FLASHREQ(r, v)		bitset32(r, 31, 31, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXOOB(r)			bitx32(r, 30, 30)
#define	FCH_ESPI_S0_INT_EN_SET_RXOOB(r, v)		bitset32(r, 30, 30, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXMSG(r)			bitx32(r, 29, 29)
#define	FCH_ESPI_S0_INT_EN_SET_RXMSG(r, v)		bitset32(r, 29, 29, v)
#define	FCH_ESPI_S0_INT_EN_GET_DNCMD(r)			bitx32(r, 28, 28)
#define	FCH_ESPI_S0_INT_EN_SET_DNCMD(r, v)		bitset32(r, 28, 28, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXVW_G3(r)		bitx32(r, 27, 27)
#define	FCH_ESPI_S0_INT_EN_SET_RXVW_G3(r, v)		bitset32(r, 27, 27, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXVW_G2(r)		bitx32(r, 26, 26)
#define	FCH_ESPI_S0_INT_EN_SET_RXVW_G2(r, v)		bitset32(r, 26, 26, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXVW_G1(r)		bitx32(r, 25, 25)
#define	FCH_ESPI_S0_INT_EN_SET_RXVW_G1(r, v)		bitset32(r, 25, 25, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXVW_G0(r)		bitx32(r, 24, 24)
#define	FCH_ESPI_S0_INT_EN_SET_RXVW_G0(r, v)		bitset32(r, 24, 24, v)
#define	FCH_ESPI_S0_INT_EN_GET_RSMU(r)			bitx32(r, 23, 23)
#define	FCH_ESPI_S0_INT_EN_SET_RSMU(r, v)		bitset32(r, 23, 23, v)
/* 22:20 reserved */
#define	FCH_ESPI_S0_INT_EN_GET_WDG_TO(r)		bitx32(r, 19, 19)
#define	FCH_ESPI_S0_INT_EN_SET_WDG_TO(r, v)		bitset32(r, 19, 19, v)
#define	FCH_ESPI_S0_INT_EN_GET_MST_ABORT(r)		bitx32(r, 18, 18)
#define	FCH_ESPI_S0_INT_EN_SET_MST_ABORT(r, v)		bitset32(r, 18, 18, v)
#define	FCH_ESPI_S0_INT_EN_GET_UPFIFO_WDG_TO(r)		bitx32(r, 17, 17)
#define	FCH_ESPI_S0_INT_EN_SET_UPFIFO_WDG_TO(r, v)	bitset32(r, 17, 17, v)
/* 16 reserved */
#define	FCH_ESPI_S0_INT_EN_GET_PROTOERR(r)		bitx32(r, 15, 15)
#define	FCH_ESPI_S0_INT_EN_SET_PROTOERR(r, v)		bitset32(r, 15, 15, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXFLASH_OFLOW(r)		bitx32(r, 14, 14)
#define	FCH_ESPI_S0_INT_EN_SET_RXFLASH_OFLOW(r, v)	bitset32(r, 14, 14, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXMSG_OFLOW(r)		bitx32(r, 13, 13)
#define	FCH_ESPI_S0_INT_EN_SET_RXMSG_OFLOW(r, v)	bitset32(r, 13, 13, v)
#define	FCH_ESPI_S0_INT_EN_GET_RXOOB_OFLOW(r)		bitx32(r, 12, 12)
#define	FCH_ESPI_S0_INT_EN_SET_RXOOB_OFLOW(r, v)	bitset32(r, 12, 12, v)
#define	FCH_ESPI_S0_INT_EN_GET_ILL_LEN(r)		bitx32(r, 11, 11)
#define	FCH_ESPI_S0_INT_EN_SET_ILL_LEN(r, v)		bitset32(r, 11, 11, v)
#define	FCH_ESPI_S0_INT_EN_GET_ILL_TAG(r)		bitx32(r, 10, 10)
#define	FCH_ESPI_S0_INT_EN_SET_ILL_TAG(r, v)		bitset32(r, 10, 10, v)
#define	FCH_ESPI_S0_INT_EN_GET_USF_CPL(r)		bitx32(r, 9, 9)
#define	FCH_ESPI_S0_INT_EN_SET_USF_CPL(r, v)		bitset32(r, 9, 9, v)
#define	FCH_ESPI_S0_INT_EN_GET_UNK_CYC(r)		bitx32(r, 8, 8)
#define	FCH_ESPI_S0_INT_EN_SET_UNK_CYC(r, v)		bitset32(r, 8, 8, v)
#define	FCH_ESPI_S0_INT_EN_GET_UNK_RSP(r)		bitx32(r, 7, 7)
#define	FCH_ESPI_S0_INT_EN_SET_UNK_RSP(r, v)		bitset32(r, 7, 7, v)
#define	FCH_ESPI_S0_INT_EN_GET_NFATAL_ERR(r)		bitx32(r, 6, 6)
#define	FCH_ESPI_S0_INT_EN_SET_NFATAL_ERR(r, v)		bitset32(r, 6, 6, v)
#define	FCH_ESPI_S0_INT_EN_GET_FATAL_ERR(r)		bitx32(r, 5, 5)
#define	FCH_ESPI_S0_INT_EN_SET_FATAL_ERR(r, v)		bitset32(r, 5, 5, v)
#define	FCH_ESPI_S0_INT_EN_GET_NO_RSP(r)		bitx32(r, 4, 4)
#define	FCH_ESPI_S0_INT_EN_SET_NO_RSP(r, v)		bitset32(r, 4, 4, v)
#define	FCH_ESPI_S0_INT_EN_GET_CRC_ERR(r)		bitx32(r, 2, 2)
#define	FCH_ESPI_S0_INT_EN_SET_CRC_ERR(r, v)		bitset32(r, 2, 2, v)
#define	FCH_ESPI_S0_INT_EN_GET_WAIT_TMT(r)		bitx32(r, 1, 1)
#define	FCH_ESPI_S0_INT_EN_SET_WAIT_TMT(r, v)		bitset32(r, 1, 1, v)
#define	FCH_ESPI_S0_INT_EN_GET_BUS_ERR(r)		bitx32(r, 0, 0)
#define	FCH_ESPI_S0_INT_EN_SET_BUS_ERR(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::ITF::ESPI::SLAVE0_INT_STS -- the target interrupt status; each
 * non-reserved bit corresponding to a different interrupt.
 */
#define	FCH_ESPI_S0_INT_STS		0x70
#define	D_FCH_ESPI_S0_INT_STS				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_INT_STS		\
	}
#define	FCH_ESPI_S0_INT_STS_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_INT_STS, 0)

#define	FCH_ESPI_S0_INT_STS_GET_FLASHREQ(r)		bitx32(r, 31, 31)
#define	FCH_ESPI_S0_INT_STS_CLEAR_FLASHREQ(r)		bitset32(r, 31, 31, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXOOB(r)		bitx32(r, 30, 30)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXOOB(r)		bitset32(r, 30, 30, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXMSG(r)		bitx32(r, 29, 29)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXMSG(r)		bitset32(r, 29, 29, 1)
#define	FCH_ESPI_S0_INT_STS_GET_DNCMD(r)		bitx32(r, 28, 28)
#define	FCH_ESPI_S0_INT_STS_CLEAR_DNCMD(r)		bitset32(r, 28, 28, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXVW_G3(r)		bitx32(r, 27, 27)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXVW_G3(r)		bitset32(r, 27, 27, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXVW_G2(r)		bitx32(r, 26, 26)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXVW_G2(r)		bitset32(r, 26, 26, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXVW_G1(r)		bitx32(r, 25, 25)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXVW_G1(r)		bitset32(r, 25, 25, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXVW_G0(r)		bitx32(r, 24, 24)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXVW_G0(r)		bitset32(r, 24, 24, 1)
/* 23:20 reserved */
#define	FCH_ESPI_S0_INT_STS_GET_WDG_TO(r)		bitx32(r, 19, 19)
#define	FCH_ESPI_S0_INT_STS_CLEAR_WDG_TO(r)		bitset32(r, 19, 19, 1)
#define	FCH_ESPI_S0_INT_STS_GET_MST_ABORT(r)		bitx32(r, 18, 18)
#define	FCH_ESPI_S0_INT_STS_CLEAR_MST_ABORT(r)		bitset32(r, 18, 18, 1)
#define	FCH_ESPI_S0_INT_STS_GET_UPFIFO_WDG_TO(r)	bitx32(r, 17, 17)
#define	FCH_ESPI_S0_INT_STS_CLEAR_UPFIFO_WDG_TO(r)	bitset32(r, 17, 17, 1)
/* 16 reserved */
#define	FCH_ESPI_S0_INT_STS_GET_PROTOERR(r)		bitx32(r, 15, 15)
#define	FCH_ESPI_S0_INT_STS_CLEAR_PROTOERR(r)		bitset32(r, 15, 15, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXFLASH_OFLOW(r)	bitx32(r, 14, 14)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXFLASH_OFLOW(r)	bitset32(r, 14, 14, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXMSG_OFLOW(r)		bitx32(r, 13, 13)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXMSG_OFLOW(r)	bitset32(r, 13, 13, 1)
#define	FCH_ESPI_S0_INT_STS_GET_RXOOB_OFLOW(r)		bitx32(r, 12, 12)
#define	FCH_ESPI_S0_INT_STS_CLEAR_RXOOB_OFLOW(r)	bitset32(r, 12, 12, 1)
#define	FCH_ESPI_S0_INT_STS_GET_ILL_LEN(r)		bitx32(r, 11, 11)
#define	FCH_ESPI_S0_INT_STS_CLEAR_ILL_LEN(r)		bitset32(r, 11, 11, 1)
#define	FCH_ESPI_S0_INT_STS_GET_ILL_TAG(r)		bitx32(r, 10, 10)
#define	FCH_ESPI_S0_INT_STS_CLEAR_ILL_TAG(r)		bitset32(r, 10, 10, 1)
#define	FCH_ESPI_S0_INT_STS_GET_USF_CPL(r)		bitx32(r, 9, 9)
#define	FCH_ESPI_S0_INT_STS_CLEAR_USF_CPL(r)		bitset32(r, 9, 9, 1)
#define	FCH_ESPI_S0_INT_STS_GET_UNK_CYC(r)		bitx32(r, 8, 8)
#define	FCH_ESPI_S0_INT_STS_CLEAR_UNK_CYC(r)		bitset32(r, 8, 8, 1)
#define	FCH_ESPI_S0_INT_STS_GET_UNK_RSP(r)		bitx32(r, 7, 7)
#define	FCH_ESPI_S0_INT_STS_CLEAR_UNK_RSP(r)		bitset32(r, 7, 7, 1)
#define	FCH_ESPI_S0_INT_STS_GET_NFATAL_ERR(r)		bitx32(r, 6, 6)
#define	FCH_ESPI_S0_INT_STS_CLEAR_NFATAL_ERR(r)		bitset32(r, 6, 6, 1)
#define	FCH_ESPI_S0_INT_STS_GET_FATAL_ERR(r)		bitx32(r, 5, 5)
#define	FCH_ESPI_S0_INT_STS_CLEAR_FATAL_ERR(r)		bitset32(r, 5, 5, 1)
#define	FCH_ESPI_S0_INT_STS_GET_NO_RSP(r)		bitx32(r, 4, 4)
#define	FCH_ESPI_S0_INT_STS_CLEAR_NO_RSP(r)		bitset32(r, 4, 4, 1)
#define	FCH_ESPI_S0_INT_STS_GET_CRC_ERR(r)		bitx32(r, 2, 2)
#define	FCH_ESPI_S0_INT_STS_CLEAR_CRC_ERR(r)		bitset32(r, 2, 2, 1)
#define	FCH_ESPI_S0_INT_STS_GET_WAIT_TMT(r)		bitx32(r, 1, 1)
#define	FCH_ESPI_S0_INT_STS_CLEAR_WAIT_TMT(r)		bitset32(r, 1, 1, 1)
#define	FCH_ESPI_S0_INT_STS_GET_BUS_ERR(r)		bitx32(r, 0, 0)
#define	FCH_ESPI_S0_INT_STS_CLEAR_BUS_ERR(r)		bitset32(r, 0, 0, 1)

/*
 * FCH::ITF::ESPI::SLAVE0_RXMSG_HDR0 -- header of a received peripheral channel
 * message (MESSAGE or MESSAGE_WITH_DATA cycle). The three bytes following the
 * cycle type are laid out as in the eSPI packet, so the tag and length are
 * split across them in the same way as for the UP_RXHDR registers.
 */
#define	FCH_ESPI_S0_RXMSG_HDR0		0x74
#define	D_FCH_ESPI_S0_RXMSG_HDR0				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXMSG_HDR0		\
	}
#define	FCH_ESPI_S0_RXMSG_HDR0_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXMSG_HDR0, 0)

#define	FCH_ESPI_S0_RXMSG_HDR0_GET_MSG_CODE(r)		bitx32(r, 31, 24)
#define	FCH_ESPI_S0_RXMSG_HDR0_GET_LENL(r)		bitx32(r, 23, 16)
#define	FCH_ESPI_S0_RXMSG_HDR0_GET_TAG(r)		bitx32(r, 15, 12)
#define	FCH_ESPI_S0_RXMSG_HDR0_GET_LENH(r)		bitx32(r, 11, 8)
#define	FCH_ESPI_S0_RXMSG_HDR0_GET_CYCLETYPE(r)		bitx32(r, 7, 0)

/*
 * FCH::ITF::ESPI::SLAVE0_RXMSG_HDR1 -- the four message specific bytes of a
 * received peripheral channel message.
 */
#define	FCH_ESPI_S0_RXMSG_HDR1		0x78
#define	D_FCH_ESPI_S0_RXMSG_HDR1				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXMSG_HDR1		\
	}
#define	FCH_ESPI_S0_RXMSG_HDR1_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXMSG_HDR1, 0)

#define	FCH_ESPI_S0_RXMSG_HDR1_GET_BYTE3(r)		bitx32(r, 31, 24)
#define	FCH_ESPI_S0_RXMSG_HDR1_GET_BYTE2(r)		bitx32(r, 23, 16)
#define	FCH_ESPI_S0_RXMSG_HDR1_GET_BYTE1(r)		bitx32(r, 15, 8)
#define	FCH_ESPI_S0_RXMSG_HDR1_GET_BYTE0(r)		bitx32(r, 7, 0)

/*
 * FCH::ITF::ESPI::SLAVE0_RXMSG_DATA_PORT -- reading this register retrieves
 * the data of a received peripheral channel message from the FIFO.
 */
#define	FCH_ESPI_S0_RXMSG_DATA_PORT	0x7c
#define	D_FCH_ESPI_S0_RXMSG_DATA_PORT				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXMSG_DATA_PORT		\
	}
#define	FCH_ESPI_S0_RXMSG_DATA_PORT_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXMSG_DATA_PORT, 0)

/*
 * FCH::ITF::ESPI::SLAVE0_RXVW -- the state of the system event virtual wires
 * received from the target, plus the status of one selected IRQ wire. The
 * event bits are write-1-to-clear. The level wires (RCIN#, SMI#, SCI#, PME#,
 * WAKE#) are written with 1 to return them to their inactive state.
 */
#define	FCH_ESPI_S0_RXVW		0x9c
#define	D_FCH_ESPI_S0_RXVW					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXVW			\
	}
#define	FCH_ESPI_S0_RXVW_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXVW, 0)

#define	FCH_ESPI_S0_RXVW_GET_CPUTEMP_REQ(r)		bitx32(r, 31, 31)
#define	FCH_ESPI_S0_RXVW_CLEAR_CPUTEMP_REQ(r)		bitset32(r, 31, 31, 1)
#define	FCH_ESPI_S0_RXVW_GET_RTCTIME_REQ(r)		bitx32(r, 30, 30)
#define	FCH_ESPI_S0_RXVW_CLEAR_RTCTIME_REQ(r)		bitset32(r, 30, 30, 1)
#define	FCH_ESPI_S0_RXVW_GET_HOST_RST_ACK(r)		bitx32(r, 19, 19)
#define	FCH_ESPI_S0_RXVW_CLEAR_HOST_RST_ACK(r)		bitset32(r, 19, 19, 1)
#define	FCH_ESPI_S0_RXVW_GET_RCIN_B(r)			bitx32(r, 18, 18)
#define	FCH_ESPI_S0_RXVW_SET_RCIN_B(r, v)		bitset32(r, 18, 18, v)
#define	FCH_ESPI_S0_RXVW_GET_SMI_B(r)			bitx32(r, 17, 17)
#define	FCH_ESPI_S0_RXVW_SET_SMI_B(r, v)		bitset32(r, 17, 17, v)
#define	FCH_ESPI_S0_RXVW_GET_SCI_B(r)			bitx32(r, 16, 16)
#define	FCH_ESPI_S0_RXVW_SET_SCI_B(r, v)		bitset32(r, 16, 16, v)
#define	FCH_ESPI_S0_RXVW_GET_SLAVE_BOOT_LOAD_STS(r)	bitx32(r, 15, 15)
#define	FCH_ESPI_S0_RXVW_CLEAR_SLAVE_BOOT_LOAD_STS(r)	bitset32(r, 15, 15, 1)
#define	FCH_ESPI_S0_RXVW_GET_ERROR_NONFATAL(r)		bitx32(r, 14, 14)
#define	FCH_ESPI_S0_RXVW_CLEAR_ERROR_NONFATAL(r)	bitset32(r, 14, 14, 1)
#define	FCH_ESPI_S0_RXVW_GET_ERROR_FATAL(r)		bitx32(r, 13, 13)
#define	FCH_ESPI_S0_RXVW_CLEAR_ERROR_FATAL(r)		bitset32(r, 13, 13, 1)
#define	FCH_ESPI_S0_RXVW_GET_SLAVE_BOOT_LOAD_DONE(r)	bitx32(r, 12, 12)
#define	FCH_ESPI_S0_RXVW_CLEAR_SLAVE_BOOT_LOAD_DONE(r)	bitset32(r, 12, 12, 1)
#define	FCH_ESPI_S0_RXVW_GET_PME_B(r)			bitx32(r, 11, 11)
#define	FCH_ESPI_S0_RXVW_SET_PME_B(r, v)		bitset32(r, 11, 11, v)
#define	FCH_ESPI_S0_RXVW_GET_WAKE_B(r)			bitx32(r, 10, 10)
#define	FCH_ESPI_S0_RXVW_SET_WAKE_B(r, v)		bitset32(r, 10, 10, v)
#define	FCH_ESPI_S0_RXVW_GET_OOB_RST_ACK(r)		bitx32(r, 8, 8)
#define	FCH_ESPI_S0_RXVW_CLEAR_OOB_RST_ACK(r)		bitset32(r, 8, 8, 1)
#define	FCH_ESPI_S0_RXVW_GET_IRQ_STS(r)			bitx32(r, 7, 5)
#define	FCH_ESPI_S0_RXVW_GET_IRQ_SEL(r)			bitx32(r, 4, 0)
#define	FCH_ESPI_S0_RXVW_SET_IRQ_SEL(r, v)		bitset32(r, 4, 0, v)

/*
 * FCH::ITF::ESPI::SLAVE0_RXVW_DATA -- the data byte most recently received for
 * each of the four virtual wire groups selected by SLAVE0_RXVW_INDEX and
 * enabled in SLAVE0_RXVW_MISC_CNTL.
 */
#define	FCH_ESPI_S0_RXVW_DATA		0xa0
#define	D_FCH_ESPI_S0_RXVW_DATA					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXVW_DATA		\
	}
#define	FCH_ESPI_S0_RXVW_DATA_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXVW_DATA, 0)

#define	FCH_ESPI_S0_RXVW_DATA_GET_GRP(r, n)	bitx32(r, 8 * (n) + 7, 8 * (n))

/*
 * FCH::ITF::ESPI::SLAVE0_RXVW_INDEX -- the virtual wire index that each of the
 * four groups captures.
 */
#define	FCH_ESPI_S0_RXVW_INDEX		0xa4
#define	D_FCH_ESPI_S0_RXVW_INDEX				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXVW_INDEX		\
	}
#define	FCH_ESPI_S0_RXVW_INDEX_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXVW_INDEX, 0)

#define	FCH_ESPI_S0_RXVW_INDEX_GET_GRP(r, n)	bitx32(r, 8 * (n) + 7, 8 * (n))
#define	FCH_ESPI_S0_RXVW_INDEX_SET_GRP(r, n, v)	\
	bitset32(r, 8 * (n) + 7, 8 * (n), v)

/*
 * FCH::ITF::ESPI::SLAVE0_RXVW_MISC_CNTL -- masks for the IRQ0-23 virtual
 * wires, enables for the four capture groups and for a few wires the
 * controller generates itself.
 */
#define	FCH_ESPI_S0_RXVW_MISC_CNTL	0xa8
#define	D_FCH_ESPI_S0_RXVW_MISC_CNTL				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXVW_MISC_CNTL		\
	}
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXVW_MISC_CNTL, 0)

#define	FCH_ESPI_S0_RXVW_MISC_CNTL_GET_IRQ_MASK(r, n)	\
	bitx32(r, 8 + (n), 8 + (n))
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_SET_IRQ_MASK(r, n, v)	\
	bitset32(r, 8 + (n), 8 + (n), v)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_GET_CPUTEMP_RTCTIME_VW_INDEX_SEL(r)	\
	bitx32(r, 7, 7)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_SET_CPUTEMP_RTCTIME_VW_INDEX_SEL(r, v) \
	bitset32(r, 7, 7, v)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_GET_CPUTEMP_RTCTIME_VW_EN(r)	\
	bitx32(r, 6, 6)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_SET_CPUTEMP_RTCTIME_VW_EN(r, v)	\
	bitset32(r, 6, 6, v)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_GET_SUS_STAT_VWEN(r)	bitx32(r, 4, 4)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_SET_SUS_STAT_VWEN(r, v) bitset32(r, 4, 4, v)
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_GET_GRP_EN(r, n)	bitx32(r, (n), (n))
#define	FCH_ESPI_S0_RXVW_MISC_CNTL_SET_GRP_EN(r, n, v)	\
	bitset32(r, (n), (n), v)

/*
 * FCH::ITF::ESPI::SLAVE0_RXVW_POLARITY -- polarity of the IRQ0-23 virtual
 * wires and enables for the controller-generated system event wires.
 */
#define	FCH_ESPI_S0_RXVW_POLARITY	0xac
#define	D_FCH_ESPI_S0_RXVW_POLARITY				\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_S0_RXVW_POLARITY		\
	}
#define	FCH_ESPI_S0_RXVW_POLARITY_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_S0_RXVW_POLARITY, 0)

#define	FCH_ESPI_S0_RXVW_POLARITY_GET_OOB_RST_WARN_EN(r)	\
	bitx32(r, 28, 28)
#define	FCH_ESPI_S0_RXVW_POLARITY_SET_OOB_RST_WARN_EN(r, v)	\
	bitset32(r, 28, 28, v)
#define	FCH_ESPI_S0_RXVW_POLARITY_GET_NMIOUT_EN(r)	bitx32(r, 27, 27)
#define	FCH_ESPI_S0_RXVW_POLARITY_SET_NMIOUT_EN(r, v)	bitset32(r, 27, 27, v)
#define	FCH_ESPI_S0_RXVW_POLARITY_GET_SMIOUT_EN(r)	bitx32(r, 26, 26)
#define	FCH_ESPI_S0_RXVW_POLARITY_SET_SMIOUT_EN(r, v)	bitset32(r, 26, 26, v)
#define	FCH_ESPI_S0_RXVW_POLARITY_GET_HOST_RST_WARN_DIS(r)	\
	bitx32(r, 25, 25)
#define	FCH_ESPI_S0_RXVW_POLARITY_SET_HOST_RST_WARN_DIS(r, v)	\
	bitset32(r, 25, 25, v)
#define	FCH_ESPI_S0_RXVW_POLARITY_GET_PLTRSTB_DIS(r)	bitx32(r, 24, 24)
#define	FCH_ESPI_S0_RXVW_POLARITY_SET_PLTRSTB_DIS(r, v)	bitset32(r, 24, 24, v)
#define	FCH_ESPI_S0_RXVW_POLARITY_GET_IRQ(r, n)		bitx32(r, (n), (n))
#define	FCH_ESPI_S0_RXVW_POLARITY_SET_IRQ(r, n, v)	bitset32(r, (n), (n), v)

/*
 * FCH::ITF::ESPI::ESPI_MiscCtrl1 -- the watchdog that clears UP_RXHDR_0
 * [UPCMD_STATUS] if software is too slow to do so itself. The counter
 * advances once every 16 A-Link clocks.
 */
#define	FCH_ESPI_MISC_CTL1		0xcc
#define	D_FCH_ESPI_MISC_CTL1					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_MISC_CTL1			\
	}
#define	FCH_ESPI_MISC_CTL1_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_MISC_CTL1, 0)

#define	FCH_ESPI_MISC_CTL1_GET_UPFIFO_WDG_TIMER_EN(r)	bitx32(r, 16, 16)
#define	FCH_ESPI_MISC_CTL1_SET_UPFIFO_WDG_TIMER_EN(r, v) bitset32(r, 16, 16, v)
#define	FCH_ESPI_MISC_CTL1_GET_UPFIFO_WDG_TIMER(r)	bitx32(r, 15, 0)
#define	FCH_ESPI_MISC_CTL1_SET_UPFIFO_WDG_TIMER(r, v)	bitset32(r, 15, 0, v)

/*
 * FCH::ITF::ESPI::ESPI_TRAN_CONTROL_REG -- controls over how SAFS flash
 * transactions are issued, including the tags used for them and whether the
 * register interface may issue them at all.
 */
#define	FCH_ESPI_TRAN_CTL		0x124
#define	D_FCH_ESPI_TRAN_CTL					\
	(const smn_reg_def_t) {					\
		.srd_unit = SMN_UNIT_FCH_ESPI,			\
		.srd_reg = FCH_ESPI_TRAN_CTL			\
	}
#define	FCH_ESPI_TRAN_CTL_MMIO(b)		\
    fch_espi_mmio_reg((b), D_FCH_ESPI_TRAN_CTL, 0)

#define	FCH_ESPI_TRAN_CTL_GET_DIS_MASCTL_REG_WR(r)	bitx32(r, 24, 24)
#define	FCH_ESPI_TRAN_CTL_SET_DIS_MASCTL_REG_WR(r, v)	bitset32(r, 24, 24, v)
#define	FCH_ESPI_TRAN_CTL_GET_ROM_INIT_ACCESS_EN(r)	bitx32(r, 20, 20)
#define	FCH_ESPI_TRAN_CTL_SET_ROM_INIT_ACCESS_EN(r, v)	bitset32(r, 20, 20, v)
#define	FCH_ESPI_TRAN_CTL_GET_ONE_NP_CMD_EN(r)		bitx32(r, 19, 19)
#define	FCH_ESPI_TRAN_CTL_SET_ONE_NP_CMD_EN(r, v)	bitset32(r, 19, 19, v)
#define	FCH_ESPI_TRAN_CTL_GET_ROM_PREFETCH_EN(r)	bitx32(r, 18, 18)
#define	FCH_ESPI_TRAN_CTL_SET_ROM_PREFETCH_EN(r, v)	bitset32(r, 18, 18, v)
#define	FCH_ESPI_TRAN_CTL_GET_BLOCK_FIFO2SAFS(r)	bitx32(r, 16, 16)
#define	FCH_ESPI_TRAN_CTL_SET_BLOCK_FIFO2SAFS(r, v)	bitset32(r, 16, 16, v)
#define	FCH_ESPI_TRAN_CTL_GET_LOW_SECURED_TAG_EN(r)	bitx32(r, 14, 14)
#define	FCH_ESPI_TRAN_CTL_SET_LOW_SECURED_TAG_EN(r, v)	bitset32(r, 14, 14, v)
#define	FCH_ESPI_TRAN_CTL_GET_MID_SECURED_TAG_EN(r)	bitx32(r, 13, 13)
#define	FCH_ESPI_TRAN_CTL_SET_MID_SECURED_TAG_EN(r, v)	bitset32(r, 13, 13, v)
#define	FCH_ESPI_TRAN_CTL_GET_HIGH_SECURED_TAG_EN(r)	bitx32(r, 12, 12)
#define	FCH_ESPI_TRAN_CTL_SET_HIGH_SECURED_TAG_EN(r, v)	bitset32(r, 12, 12, v)
#define	FCH_ESPI_TRAN_CTL_GET_LOW_SECURED_TAG(r)	bitx32(r, 11, 8)
#define	FCH_ESPI_TRAN_CTL_SET_LOW_SECURED_TAG(r, v)	bitset32(r, 11, 8, v)
#define	FCH_ESPI_TRAN_CTL_GET_MID_SECURED_TAG(r)	bitx32(r, 7, 4)
#define	FCH_ESPI_TRAN_CTL_SET_MID_SECURED_TAG(r, v)	bitset32(r, 7, 4, v)
#define	FCH_ESPI_TRAN_CTL_GET_HIGH_SECURED_TAG(r)	bitx32(r, 3, 0)
#define	FCH_ESPI_TRAN_CTL_SET_HIGH_SECURED_TAG(r, v)	bitset32(r, 3, 0, v)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_ESPI_H */
