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

#ifndef _TOFINO_REGS_H
#define	_TOFINO_REGS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
/*
 * Register offsets
 *
 * Tofino and Tofino2 have largely the same register set for managing the tbus,
 * but they are found at different offsets.
 */

/* tbus status */
#define	TF_REG_TBUS_CTRL	0x180010
#define	TF_REG_TBUS_DMA_FLUSH	0x180014
#define	TF_REG_TBUS_LINK_DOWN	0x180018
/* tbus interrupt management */
#define	TF_REG_TBUS_INT_STAT0	0x18001C
#define	TF_REG_TBUS_INT_STAT1	0x180020
#define	TF_REG_TBUS_INT_STAT2	0x180024
#define	TF_REG_TBUS_INT_EN0_0	0x180028
#define	TF_REG_TBUS_INT_EN0_1	0x18002C
#define	TF_REG_TBUS_INT_EN1_0	0x180030
#define	TF_REG_TBUS_INT_EN1_1	0x180034
#define	TF_REG_TBUS_INT_EN2_0	0x180038
#define	TF_REG_TBUS_INT_EN2_1	0x18003C
/* DR config registers */
#define	TF_REG_TBUS_TX_BASE	0x180100
#define	TF_REG_TBUS_CMP_BASE	0x180200
#define	TF_REG_TBUS_FM_BASE	0x180400
#define	TF_REG_TBUS_RX_BASE	0x180600

/* tbus status */
#define	TF2_REG_TBUS_CTRL	0x300010
#define	TF2_REG_TBUS_DMA_FLUSH	0x300014
#define	TF2_REG_TBUS_LINK_DOWN	0x300018
/* tbus interrupt management */
#define	TF2_REG_TBUS_INT_STAT0	0x300020
#define	TF2_REG_TBUS_INT_STAT1	0x300024
#define	TF2_REG_TBUS_INT_STAT2	0x300028
#define	TF2_REG_TBUS_INT_EN0_0	0x30002C
#define	TF2_REG_TBUS_INT_EN0_1	0x300030
#define	TF2_REG_TBUS_INT_EN1_0	0x300034
#define	TF2_REG_TBUS_INT_EN1_1	0x300038
#define	TF2_REG_TBUS_INT_EN2_0	0x30003C
#define	TF2_REG_TBUS_INT_EN2_1	0x300040
/* DR config registers */
#define	TF2_REG_TBUS_TX_BASE	0x300100
#define	TF2_REG_TBUS_CMP_BASE	0x300200
#define	TF2_REG_TBUS_FM_BASE	0x300400
#define	TF2_REG_TBUS_RX_BASE	0x300600

/*
 * Contents of the TBUS_CRTL register
 */
typedef struct tf_tbus_ctrl {
	uint32_t tftc_pfc_fm : 8;
	uint32_t tftc_pfc_rx : 8;
	uint32_t tftc_ecc_dec_dis : 1;
	uint32_t tftc_crcchk_dis : 1;
	uint32_t tftc_crcrmv_dis : 1;
	uint32_t tftc_crcgen_dis : 1;
	uint32_t tftc_rx_en : 1;
	uint32_t tftc_port_alive : 1;
	uint32_t tftc_rx_channel_offset : 4; /* tf2 only */
	uint32_t tftc_crcerr_keep : 1; /* tf2 only */
	uint32_t tftc_reserved : 5;
} tf_tbus_ctrl_t;

/*
 * Bitfields for TBUS_INT_EN0_x and TBUS_INT_STAT0_x
 */
#define	TBUS_INT0_HOST_OVERFLOW		(1 << 0)
#define	TBUS_INT0_TX_DR_0_EMPTY		(1 << 1)
#define	TBUS_INT0_TX_DR_1_EMPTY		(1 << 2)
#define	TBUS_INT0_TX_DR_2_EMPTY		(1 << 3)
#define	TBUS_INT0_TX_DR_3_EMPTY		(1 << 4)
#define	TBUS_INT0_TX_DR_0_FULL		(1 << 5)
#define	TBUS_INT0_TX_DR_1_FULL		(1 << 6)
#define	TBUS_INT0_TX_DR_2_FULL		(1 << 7)
#define	TBUS_INT0_TX_DR_3_FULL		(1 << 8)
#define	TBUS_INT0_CPL_DR_0_EMPTY	(1 << 9)
#define	TBUS_INT0_CPL_DR_1_EMPTY	(1 << 10)
#define	TBUS_INT0_CPL_DR_2_EMPTY	(1 << 11)
#define	TBUS_INT0_CPL_DR_3_EMPTY	(1 << 12)
#define	TBUS_INT0_CPL_DR_0_FULL		(1 << 13)
#define	TBUS_INT0_CPL_DR_1_FULL		(1 << 14)
#define	TBUS_INT0_CPL_DR_2_FULL		(1 << 15)
#define	TBUS_INT0_CPL_DR_3_FULL		(1 << 16)
#define	TBUS_INT0_TX_DR_0_RD_ERR	(1 << 17)
#define	TBUS_INT0_TX_DR_1_RD_ERR	(1 << 18)
#define	TBUS_INT0_TX_DR_2_RD_ERR	(1 << 19)
#define	TBUS_INT0_TX_DR_3_RD_ERR	(1 << 20)
#define	TBUS_INT0_FM_DR_0_RD_ERR	(1 << 21)
#define	TBUS_INT0_FM_DR_1_RD_ERR	(1 << 22)
#define	TBUS_INT0_FM_DR_2_RD_ERR	(1 << 23)
#define	TBUS_INT0_FM_DR_3_RD_ERR	(1 << 24)
#define	TBUS_INT0_FM_DR_4_RD_ERR	(1 << 25)
#define	TBUS_INT0_FM_DR_5_RD_ERR	(1 << 26)
#define	TBUS_INT0_FM_DR_6_RD_ERR	(1 << 27)
#define	TBUS_INT0_FM_DR_7_RD_ERR	(1 << 28)
#define	TBUS_INT0_TBUS_FLUSH_DONE	(1 << 29)

/*
 * Interrupt bits that signal a change in a completion DR
 */
#define	TBUS_INT0_CPL_EVENT	( \
	TBUS_INT0_CPL_DR_0_EMPTY | TBUS_INT0_CPL_DR_1_EMPTY | \
	TBUS_INT0_CPL_DR_2_EMPTY | TBUS_INT0_CPL_DR_3_EMPTY | \
	TBUS_INT0_CPL_DR_0_FULL | TBUS_INT0_CPL_DR_1_FULL | \
	TBUS_INT0_CPL_DR_2_FULL | TBUS_INT0_CPL_DR_3_FULL)

/*
 * Bitfields for TBUS_INT_EN1_x and TBUS_INT_STAT1_x
 */
#define	TBUS_INT1_FM_DR_0_EMPTY	(1 << 0)
#define	TBUS_INT1_FM_DR_1_EMPTY	(1 << 1)
#define	TBUS_INT1_FM_DR_2_EMPTY	(1 << 2)
#define	TBUS_INT1_FM_DR_3_EMPTY	(1 << 3)
#define	TBUS_INT1_FM_DR_4_EMPTY	(1 << 4)
#define	TBUS_INT1_FM_DR_5_EMPTY	(1 << 5)
#define	TBUS_INT1_FM_DR_6_EMPTY	(1 << 6)
#define	TBUS_INT1_FM_DR_7_EMPTY	(1 << 7)
#define	TBUS_INT1_FM_DR_0_FULL	(1 << 8)
#define	TBUS_INT1_FM_DR_1_FULL	(1 << 9)
#define	TBUS_INT1_FM_DR_2_FULL	(1 << 10)
#define	TBUS_INT1_FM_DR_3_FULL	(1 << 11)
#define	TBUS_INT1_FM_DR_4_FULL	(1 << 12)
#define	TBUS_INT1_FM_DR_5_FULL	(1 << 13)
#define	TBUS_INT1_FM_DR_6_FULL	(1 << 14)
#define	TBUS_INT1_FM_DR_7_FULL	(1 << 15)
#define	TBUS_INT1_RX_DR_0_EMPTY	(1 << 16)
#define	TBUS_INT1_RX_DR_1_EMPTY	(1 << 17)
#define	TBUS_INT1_RX_DR_2_EMPTY	(1 << 18)
#define	TBUS_INT1_RX_DR_3_EMPTY	(1 << 19)
#define	TBUS_INT1_RX_DR_4_EMPTY	(1 << 20)
#define	TBUS_INT1_RX_DR_5_EMPTY	(1 << 21)
#define	TBUS_INT1_RX_DR_6_EMPTY	(1 << 22)
#define	TBUS_INT1_RX_DR_7_EMPTY	(1 << 23)
#define	TBUS_INT1_RX_DR_0_FULL	(1 << 24)
#define	TBUS_INT1_RX_DR_1_FULL	(1 << 25)
#define	TBUS_INT1_RX_DR_2_FULL	(1 << 26)
#define	TBUS_INT1_RX_DR_3_FULL	(1 << 27)
#define	TBUS_INT1_RX_DR_4_FULL	(1 << 28)
#define	TBUS_INT1_RX_DR_5_FULL	(1 << 29)
#define	TBUS_INT1_RX_DR_6_FULL	(1 << 30)
#define	TBUS_INT1_RX_DR_7_FULL	(1 << 31)

/*
 * Interrupt bits that signal a change in an rx packet DR
 */
#define	TBUS_INT1_RX_EVENT	( \
	TBUS_INT1_RX_DR_0_EMPTY | TBUS_INT1_RX_DR_1_EMPTY | \
	TBUS_INT1_RX_DR_2_EMPTY | TBUS_INT1_RX_DR_3_EMPTY | \
	TBUS_INT1_RX_DR_4_EMPTY | TBUS_INT1_RX_DR_5_EMPTY | \
	TBUS_INT1_RX_DR_6_EMPTY | TBUS_INT1_RX_DR_7_EMPTY | \
	TBUS_INT1_RX_DR_0_FULL | TBUS_INT1_RX_DR_1_FULL | \
	TBUS_INT1_RX_DR_2_FULL | TBUS_INT1_RX_DR_3_FULL | \
	TBUS_INT1_RX_DR_4_FULL | TBUS_INT1_RX_DR_5_FULL | \
	TBUS_INT1_RX_DR_6_FULL | TBUS_INT1_RX_DR_7_FULL)

/*
 * Bitfields for TBUS_INT_EN2_x and TBUS_INT_STAT2_x
 */
#define	TBUS_INT2_IQUEUE_MBE	(1 << 0)
#define	TBUS_INT2_OQUEUE_MBE	(1 << 1)
#define	TBUS_INT2_IQUEUE_SBE	(1 << 2)
#define	TBUS_INT2_OQUEUE_SBE	(1 << 3)
#define	TBUS_INT2_CRC_ERR	(1 << 4)

/*
 * Each DR has multiple registers defining the addresses and characteristics of
 * the DR.  Tofino has 11 such registers per DR.  Tofino2 has the same 11, as
 * well as 2 more.  The offsets of each register are defined below.
 */
#define	TBUS_DR_OFF_CTRL		0x00
#define	TBUS_DR_OFF_BASE_ADDR_LOW	0x04
#define	TBUS_DR_OFF_BASE_ADDR_HIGH	0x08
#define	TBUS_DR_OFF_LIMIT_ADDR_LOW	0x0c
#define	TBUS_DR_OFF_LIMIT_ADDR_HIGH	0x10
#define	TBUS_DR_OFF_SIZE		0x14
#define	TBUS_DR_OFF_HEAD_PTR		0x18
#define	TBUS_DR_OFF_TAIL_PTR		0x1c
#define	TBUS_DR_OFF_RING_TIMEOUT	0x20
#define	TBUS_DR_OFF_DATA_TIMEOUT	0x24
#define	TBUS_DR_OFF_DATA_STATUS		0x2c
/* The following two are specific to Tofino2 */
#define	TBUS_DR_OFF_EMPTY_INT_TIME	0x30
#define	TBUS_DR_OFF_EMPTY_INT_CNT	0x34

/* Size of the register space needed to describe each ring */
#define	TF_DR_SIZE	(11 * sizeof (uint32_t))
#define	TF2_DR_SIZE	(13 * sizeof (uint32_t))

/* Fields in the DR control register */
#define	TBUS_DR_CTRL_ENABLE		0x01
#define	TBUS_DR_CTRL_WRITE_TIME_MODE	0x02
#define	TBUS_DR_CTRL_HEAD_PTR_MODE	0x04
#define	TBUS_DR_CTRL_TAIL_PTR_MODE	0x08

/* Fields in the DR status register */
#define	TBUS_DR_STATUS_DR_EMPTY		0x01
#define	TBUS_DR_STATUS_DR_FULL		0x02
#define	TBUS_DR_STATUS_MQ_EMPTY		0x04
#define	TBUS_DR_STATUS_MQ_FULL		0x08

/* Size of each type of descriptor, given in 64-byte words */
#define	TBUS_DR_DESC_SZ_FM	1
#define	TBUS_DR_DESC_SZ_RX	2
#define	TBUS_DR_DESC_SZ_TX	4
#define	TBUS_DR_DESC_SZ_CMP	2

#ifdef	__cplusplus
}
#endif

#endif /* _TOFINO_REGS_H */
