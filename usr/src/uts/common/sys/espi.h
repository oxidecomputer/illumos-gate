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

#ifndef _SYS_ESPI_H
#define	_SYS_ESPI_H

/*
 * Definitions relating to the eSPI specification, v1.5
 */

#include <sys/bitext.h>
#include <sys/stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cycle types.
 */

/* Peripheral Channel */
#define	ESPI_CYCLE_MEMREAD32			0x0
#define	ESPI_CYCLE_MEMWRITE32			0x1
#define	ESPI_CYCLE_MEMREAD64			0x2
#define	ESPI_CYCLE_MEMWRITE64			0x3
#define	ESPI_CYCLE_COMPLETION			0x6
#define	ESPI_CYCLE_UNSUCCESSFUL_COMPLETION	0x8
#define	ESPI_CYCLE_COMPLETION_WITH_DATA		0x9
#define	ESPI_CYCLE_MESSAGE			0x10
#define	ESPI_CYCLE_MESSAGE_WITH_DATA		0x11
/* OOB Message Channel */
#define	ESPI_CYCLE_OOB_TUNNELED_SMBUS		0x21
/*
 * Flash Access Channel. The completion cycle types defined for the
 * peripheral channel above are also used for flash access completions. A
 * completion without data corresponds to a flash write or erase and one with
 * data to a flash read.
 */
#define	ESPI_CYCLE_FLASH_READ			0x0
#define	ESPI_CYCLE_FLASH_WRITE			0x1
#define	ESPI_CYCLE_FLASH_ERASE			0x2

/*
 * Completion cycle types, other than a successful completion without data,
 * encode the position of the completion within a split completion sequence in
 * bits 2:1 of the cycle type. The values above are the encodings with those
 * bits clear.
 */
#define	ESPI_CYCLE_CPL_GET_SPLIT(c)		bitx8(c, 2, 1)
#define	ESPI_CYCLE_CPL_SPLIT_MIDDLE		0
#define	ESPI_CYCLE_CPL_SPLIT_FIRST		1
#define	ESPI_CYCLE_CPL_SPLIT_LAST		2
#define	ESPI_CYCLE_CPL_SPLIT_ONLY		3
#define	ESPI_CYCLE_CPL_BASE(c)			bitset8(c, 2, 1, 0)

/*
 * Virtual Wires.
 *
 * A virtual wire packet contains pairs of an index byte and a data byte. For
 * the system event indices below, the data byte holds four wires in bits 3:0
 * and a valid bit for each of them in bits 7:4.
 */
#define	ESPI_VW_DATA_GET_VALID(d)		bitx8(d, 7, 4)
#define	ESPI_VW_DATA_SET_VALID(d, v)		bitset8(d, 7, 4, v)
#define	ESPI_VW_DATA_GET_LEVEL(d)		bitx8(d, 3, 0)
#define	ESPI_VW_DATA_SET_LEVEL(d, v)		bitset8(d, 3, 0, v)

/*
 * System event virtual wire indices and the wire assignments within each. The
 * wire numbers are bit positions within the level nibble of the data byte.
 */
/* Controller to target. SLP_S3#, SLP_S4#, SLP_S5# are active low. */
#define	ESPI_VW_IDX_SYSEV_SLP			2
#define	ESPI_VW_SYSEV_SLP_S3			0
#define	ESPI_VW_SYSEV_SLP_S4			1
#define	ESPI_VW_SYSEV_SLP_S5			2
/* Controller to target. SUS_STAT# and PLTRST# are active low. */
#define	ESPI_VW_IDX_SYSEV_PLTRST		3
#define	ESPI_VW_SYSEV_SUS_STAT			0
#define	ESPI_VW_SYSEV_PLTRST			1
#define	ESPI_VW_SYSEV_OOB_RST_WARN		2
/* Target to controller. WAKE# and PME# are active low. */
#define	ESPI_VW_IDX_SYSEV_WAKE			4
#define	ESPI_VW_SYSEV_OOB_RST_ACK		0
#define	ESPI_VW_SYSEV_WAKE			2
#define	ESPI_VW_SYSEV_PME			3
/* Target to controller. */
#define	ESPI_VW_IDX_SYSEV_BOOT			5
#define	ESPI_VW_SYSEV_TARGET_BOOT_LOAD_DONE	0
#define	ESPI_VW_SYSEV_ERROR_FATAL		1
#define	ESPI_VW_SYSEV_ERROR_NONFATAL		2
#define	ESPI_VW_SYSEV_TARGET_BOOT_LOAD_STATUS	3
/* Target to controller. SCI#, SMI# and RCIN# are active low. */
#define	ESPI_VW_IDX_SYSEV_SCI			6
#define	ESPI_VW_SYSEV_SCI			0
#define	ESPI_VW_SYSEV_SMI			1
#define	ESPI_VW_SYSEV_RCIN			2
#define	ESPI_VW_SYSEV_HOST_RST_ACK		3
/* Controller to target. SMIOUT# and NMIOUT# are active low. */
#define	ESPI_VW_IDX_SYSEV_HOST_RST		7
#define	ESPI_VW_SYSEV_HOST_RST_WARN		0
#define	ESPI_VW_SYSEV_SMIOUT			1
#define	ESPI_VW_SYSEV_NMIOUT			2

/*
 * eSPI target Capabilities and Configuration Registers.
 */

/* Device Identification */
#define	ESPI_REG_IDENT		0x4
#define	ESPI_REG_IDENT_GET_VERSION(r)		bitx32(r, 7, 0)
/*
 * Note that this value encompasses revisions 1.0 and 1.5 of the specification.
 */
#define	ESPI_REG_IDENT_VERSION_1X		1

/* General Capabilities and Configurations */
#define	ESPI_REG_GEN_CAP	0x8
#define	ESPI_REG_GEN_CAP_GET_CRC_EN(r)		bitx32(r, 31, 31)
#define	ESPI_REG_GEN_CAP_SET_CRC_EN(r, v)	bitset32(r, 31, 31, v)
#define	ESPI_REG_GEN_CAP_GET_RSPMOD_EN(r)	bitx32(r, 30, 30)
#define	ESPI_REG_GEN_CAP_SET_RSPMOD_EN(r, v)	bitset32(r, 30, 30, v)
#define	ESPI_REG_GEN_CAP_GET_RTCBMC_EN(r)	bitx32(r, 29, 29)
#define	ESPI_REG_GEN_CAP_GET_ALERTMODE(r)	bitx32(r, 28, 28)
#define	ESPI_REG_GEN_CAP_SET_ALERTMODE(r, v)	bitset32(r, 28, 28, v)
#define	ESPI_REG_GEN_CAP_ALERTMODE_IO		0
#define	ESPI_REG_GEN_CAP_ALERTMODE_PIN		1
#define	ESPI_REG_GEN_CAP_GET_IOMODE(r)		bitx32(r, 27, 26)
#define	ESPI_REG_GEN_CAP_SET_IOMODE(r, v)	bitset32(r, 27, 26, v)
#define	ESPI_REG_GEN_CAP_IOMODE_SINGLE		0
#define	ESPI_REG_GEN_CAP_IOMODE_DUAL		1
#define	ESPI_REG_GEN_CAP_IOMODE_QUAD		2
#define	ESPI_REG_GEN_CAP_GET_IOMODE_SUP(r)	bitx32(r, 25, 24)
#define	ESPI_REG_GEN_CAP_GET_OD_ALERT(r)	bitx32(r, 23, 23)
#define	ESPI_REG_GEN_CAP_SET_OD_ALERT(r, v)	bitset32(r, 23, 23, v)
#define	ESPI_REG_GEN_CAP_GET_FREQ(r)		bitx32(r, 22, 20)
#define	ESPI_REG_GEN_CAP_SET_FREQ(r, v)		bitset32(r, 22, 20, v)
#define	ESPI_REG_GEN_CAP_FREQ_20MHZ		0
#define	ESPI_REG_GEN_CAP_FREQ_25MHZ		1
#define	ESPI_REG_GEN_CAP_FREQ_33MHZ		2
#define	ESPI_REG_GEN_CAP_FREQ_50MHZ		3
#define	ESPI_REG_GEN_CAP_FREQ_66MHZ		4
#define	ESPI_REG_GEN_CAP_GET_OD_ALERT_SUP(r)	bitx32(r, 19, 19)
#define	ESPI_REG_GEN_CAP_GET_FREQ_SUP(r)	bitx32(r, 18, 16)
#define	ESPI_REG_GEN_CAP_GET_MAX_WAITST(r)	bitx32(r, 15, 12)
#define	ESPI_REG_GEN_CAP_SET_MAX_WAITST(r, v)	bitset32(r, 15, 12, v)
#define	ESPI_REG_GEN_CAP_GET_FLASH(r)		bitx32(r, 3, 3)
#define	ESPI_REG_GEN_CAP_GET_OOB(r)		bitx32(r, 2, 2)
#define	ESPI_REG_GEN_CAP_GET_VWIRE(r)		bitx32(r, 1, 1)
#define	ESPI_REG_GEN_CAP_GET_PERIPH(r)		bitx32(r, 0, 0)

/* Channel 0 - Peripheral channel - Capabilities and Configurations */
#define	ESPI_REG_CHAN0_CAP	0x10
#define	ESPI_REG_CHAN0_GET_CAP_MAXREAD(r)	bitx32(r, 14, 12)
#define	ESPI_REG_CHAN0_CAP_SET_MAXREAD(r, v)	bitset32(r, 14, 12, v)
#define	ESPI_REG_CHAN0_CAP_READ_64		1
#define	ESPI_REG_CHAN0_CAP_READ_128		2
#define	ESPI_REG_CHAN0_CAP_READ_256		3
#define	ESPI_REG_CHAN0_CAP_READ_512		4
#define	ESPI_REG_CHAN0_CAP_READ_1024		5
#define	ESPI_REG_CHAN0_CAP_READ_2048		6
#define	ESPI_REG_CHAN0_CAP_READ_4096		7
#define	ESPI_REG_CHAN0_GET_CAP_SELPAYLOAD(r)	bitx32(r, 10, 8)
#define	ESPI_REG_CHAN0_CAP_SET_SELPAYLOAD(r, v)	bitset32(r, 10, 8, v)
#define	ESPI_REG_CHAN0_GET_CAP_MAXPAYLOAD(r)	bitx32(r, 6, 4)
#define	ESPI_REG_CHAN0_CAP_PAYLOAD_64		1
#define	ESPI_REG_CHAN0_CAP_PAYLOAD_128		2
#define	ESPI_REG_CHAN0_CAP_PAYLOAD_256		3
#define	ESPI_REG_CHAN0_GET_CAP_BUSMASTER_EN(r)	bitx32(r, 2, 2)
#define	ESPI_REG_CHAN0_CAP_SET_BM_EN(r, v)	bitset32(r, 2, 2, v)
#define	ESPI_REG_CHAN0_GET_CAP_READY(r)		bitx32(r, 1, 1)
#define	ESPI_REG_CHAN0_GET_CAP_EN(r)		bitx32(r, 0, 0)
#define	ESPI_REG_CHAN0_CAP_SET_EN(r, v)		bitset32(r, 0, 0, v)

/* Channel 1 - Virtual wire - Capabilities and Configurations */
#define	ESPI_REG_CHAN1_CAP	0x20
#define	ESPI_REG_CHAN1_CAP_GET_OPMAX_VW(r)	bitx32(r, 21, 16)
#define	ESPI_REG_CHAN1_CAP_SET_OPMAX_VW(r, v)	bitset32(r, 21, 16, v)
#define	ESPI_REG_CHAN1_CAP_GET_MAX_VW(r)	bitx32(r, 13, 8)
#define	ESPI_REG_CHAN1_CAP_GET_READY(r)		bitx32(r, 1, 1)
#define	ESPI_REG_CHAN1_CAP_GET_EN(r)		bitx32(r, 0, 0)
#define	ESPI_REG_CHAN1_CAP_SET_EN(r, v)		bitset32(r, 0, 0, v)

/* Channel 2 - OOB - Capabilities and Configurations */
#define	ESPI_REG_CHAN2_CAP	0x30
#define	ESPI_REG_CHAN2_CAP_GET_SELPAYLOAD(r)	bitx32(r, 10, 8)
#define	ESPI_REG_CHAN2_CAP_SET_SELPAYLOAD(r, v)	bitset32(r, 10, 8, v)
#define	ESPI_REG_CHAN2_CAP_GET_MAXPAYLOAD(r)	bitx32(r, 6, 4)
#define	ESPI_REG_CHAN2_CAP_PAYLOAD_64		1
#define	ESPI_REG_CHAN2_CAP_PAYLOAD_128		2
#define	ESPI_REG_CHAN2_CAP_PAYLOAD_256		3
#define	ESPI_REG_CHAN2_CAP_GET_READY(r)		bitx32(r, 1, 1)
#define	ESPI_REG_CHAN2_CAP_GET_EN(r)		bitx32(r, 0, 0)
#define	ESPI_REG_CHAN2_CAP_SET_EN(r, v)		bitset32(r, 0, 0, v)

/* Channel 3 - Flash - Capabilities and Configurations */
#define	ESPI_REG_CHAN3_CAP	0x40
#define	ESPI_REG_CHAN3_CAP_GET_RPMC_OP1(r)	bitx32(r, 31, 24)
#define	ESPI_REG_CHAN3_CAP_GET_RPMC_CNT(r)	bitx32(r, 23, 20)
#define	ESPI_REG_CHAN3_CAP_GET_SHARE_SUP(r)	bitx32(r, 17, 16)
/*
 * MAFS is master attached flash sharing and SAFS slave attached, the names
 * that eSPI 1.0 and AMD use. eSPI 1.5 calls these controller attached and
 * target attached flash sharing. This field was reserved and read as 0 in
 * eSPI 1.0, which only defined MAFS, so eSPI 1.5 gives 0 and 1 the same
 * meaning.
 */
#define	ESPI_REG_CHAN3_CAP_SHARE_SUP_MAFS0	0
#define	ESPI_REG_CHAN3_CAP_SHARE_SUP_MAFS1	1
#define	ESPI_REG_CHAN3_CAP_SHARE_SUP_SAFS	2
#define	ESPI_REG_CHAN3_CAP_SHARE_SUP_BOTH	3
#define	ESPI_REG_CHAN3_CAP_GET_MAXREAD(r)	bitx32(r, 14, 12)
#define	ESPI_REG_CHAN3_CAP_SET_MAXREAD(r, v)	bitset32(r, 14, 12, v)
#define	ESPI_REG_CHAN3_CAP_READ_64		1
#define	ESPI_REG_CHAN3_CAP_READ_128		2
#define	ESPI_REG_CHAN3_CAP_READ_256		3
#define	ESPI_REG_CHAN3_CAP_READ_512		4
#define	ESPI_REG_CHAN3_CAP_READ_1024		5
#define	ESPI_REG_CHAN3_CAP_READ_2048		6
#define	ESPI_REG_CHAN3_CAP_READ_4096		7
#define	ESPI_REG_CHAN3_CAP_GET_SHARE(r)		bitx32(r, 11, 11)
#define	ESPI_REG_CHAN3_CAP_SET_SHARE(r, v)	bitset32(r, 11, 11, v)
#define	ESPI_REG_CHAN3_CAP_GET_SELPAYLOAD(r)	bitx32(r, 10, 8)
#define	ESPI_REG_CHAN3_CAP_SET_SELPAYLOAD(r, v)	bitset32(r, 10, 8, v)
#define	ESPI_REG_CHAN3_CAP_GET_MAXPAYLOAD(r)	bitx32(r, 7, 5)
#define	ESPI_REG_CHAN3_CAP_PAYLOAD_64		1
#define	ESPI_REG_CHAN3_CAP_PAYLOAD_128		2
#define	ESPI_REG_CHAN3_CAP_PAYLOAD_256		3
#define	ESPI_REG_CHAN3_CAP_GET_ERASE_SZ(r)	bitx32(r, 4, 2)
#define	ESPI_REG_CHAN3_CAP_SET_ERASE_SZ(r, v)	bitset32(r, 4, 2, v)
#define	ESPI_REG_CHAN3_CAP_ERASE_SZ_4K		1
#define	ESPI_REG_CHAN3_CAP_ERASE_SZ_64K		2
#define	ESPI_REG_CHAN3_CAP_ERASE_SZ_4K_64K	3
#define	ESPI_REG_CHAN3_CAP_ERASE_SZ_128K	4
#define	ESPI_REG_CHAN3_CAP_ERASE_SZ_256K	5
#define	ESPI_REG_CHAN3_CAP_GET_READY(r)		bitx32(r, 1, 1)
#define	ESPI_REG_CHAN3_CAP_GET_EN(r)		bitx32(r, 0, 0)
#define	ESPI_REG_CHAN3_CAP_SET_EN(r, v)		bitset32(r, 0, 0, v)

#define	ESPI_REG_CHAN3_CAP2	0x44
#define	ESPI_REG_CHAN3_CAP_GET2_RPMC_MAXDEV(r)	bitx32(r, 23, 22)
#define	ESPI_REG_CHAN3_CAP2_RPMC_1DEV		0
#define	ESPI_REG_CHAN3_CAP2_RPMC_2DEV		1
#define	ESPI_REG_CHAN3_CAP2_RPMC_3DEV		2
#define	ESPI_REG_CHAN3_CAP2_RPMC_4DEV		3
#define	ESPI_REG_CHAN3_CAP_GET2_RPMC_SUP(r)	bitx32(r, 21, 16)
#define	ESPI_REG_CHAN3_CAP_GET2_ERASE_SZ(r)	bitx32(r, 15, 8)
#define	ESPI_REG_CHAN3_CAP2_ERASE_SZ_4K		4
#define	ESPI_REG_CHAN3_CAP2_ERASE_SZ_32K	32
#define	ESPI_REG_CHAN3_CAP2_ERASE_SZ_64K	64
#define	ESPI_REG_CHAN3_CAP2_ERASE_SZ_128K	128
#define	ESPI_REG_CHAN3_CAP_GET2_MAXREAD(r)	bitx32(r, 2, 0)
#define	ESPI_REG_CHAN3_CAP_READ_64		1 /* also 0 */
#define	ESPI_REG_CHAN3_CAP_READ_128		2
#define	ESPI_REG_CHAN3_CAP_READ_256		3
#define	ESPI_REG_CHAN3_CAP_READ_512		4
#define	ESPI_REG_CHAN3_CAP_READ_1024		5
#define	ESPI_REG_CHAN3_CAP_READ_2048		6
#define	ESPI_REG_CHAN3_CAP_READ_4096		7

#define	ESPI_REG_CHAN3_CAP3	0x48
#define	ESPI_REG_CHAN3_CAP_GET3_RPMCD2_OP1(r)	bitx32(r, 31, 24)
#define	ESPI_REG_CHAN3_CAP_GET3_RPMCD2_CNT(r)	bitx32(r, 23, 20)

#define	ESPI_REG_CHAN3_CAP4	0x4c
#define	ESPI_REG_CHAN3_CAP_GET4_RPMCD4_OP1(r)	bitx32(r, 31, 24)
#define	ESPI_REG_CHAN3_CAP_GET4_RPMCD4_CNT(r)	bitx32(r, 23, 20)
#define	ESPI_REG_CHAN3_CAP_GET4_RPMCD3_OP1(r)	bitx32(r, 15, 8)
#define	ESPI_REG_CHAN3_CAP_GET4_RPMCD3_CNT(r)	bitx32(r, 7, 4)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ESPI_H */
