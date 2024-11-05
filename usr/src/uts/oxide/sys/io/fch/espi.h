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

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

MAKE_MMIO_FCH_REG_FN(ESPI, espi, 4);

/*
 * FCH::ITF::ESPI::DN_TXHDR_0th -- this register is the first of three that are
 * programmed with bits of information that should be transmitted with a
 * downstream message. The lower 8-bits of this register are common across
 * commands and are where the command type is configured, and the other 24 bits
 * are broken into three 8-bit values, the meaning of which depends on the
 * selected command. Across this register and FCH::ITF::ESPI::DN_TXHDR_[1:2],
 * there are eight such command-specific 8-bit values, HDATA[0:7].
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

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_ESPI_H */
