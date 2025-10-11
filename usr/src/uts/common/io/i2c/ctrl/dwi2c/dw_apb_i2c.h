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

#ifndef _DW_APB_I2C_H
#define	_DW_APB_I2C_H

/*
 * Register definitions for the Designware APB I2C controller.
 */

#include <sys/bitext.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IC_CON -- This is the general control register. The valid bits vary depending
 * on how the hardware block was instantiated. There are more bits that are
 * possible to set here for additional blocks.
 */
#define	DW_I2C_CON		0x00
#define	DW_I2C_CON_GET_RX_FULL_HOLD(r)	bitx32(r, 9, 9)
#define	DW_I2C_CON_GET_RX_FULL_DIS	0
#define	DW_I2C_CON_GET_RX_FULL_EN	1
#define	DW_I2C_CON_GET_TX_EMPTY(r)	bitx32(r, 8, 8)
#define	DW_I2C_CON_TX_EMPTY_DIS		0
#define	DW_I2C_CON_TX_EMPTY_EN		1
#define	DW_I2C_CON_GET_STOP_DET(r)	bitx32(r, 7, 7)
#define	DW_I2C_CON_STOP_DET_ADDR	0
#define	DW_I2C_CON_STOP_DET_ANY		1
#define	DW_I2C_CON_GET_TGT_DIS(r)	bitx32(r, 6, 6)
#define	DW_I2C_CON_TGT_EN	0
#define	DW_I2C_CON_TGT_DIS	1
#define	DW_I2C_CON_GET_RST(r)		bitx32(r, 5, 5)
#define	DW_I2C_CON_RST_DIS	0
#define	DW_I2C_CON_RST_EN	1
#define	DW_I2C_CON_GET_10BIT_CTRL(r)	bitx32(r, 4, 4)
#define	DW_I2C_CON_GET_10BIT_TGT(r)	bitx32(r, 3, 3)
#define	DW_I2C_CON_GET_SPEED(r)		bitx32(r, 2, 1)
#define	DW_I2C_CON_SPEED_STD	1
#define	DW_I2C_CON_SPEED_FAST	2
#define	DW_I2C_CON_SPEED_HIGH	3
#define	DW_I2C_CON_GET_CTRL(r)		bitx32(r, 0, 0)
#define	DW_I2C_CON_CTRL_DIS	0
#define	DW_I2C_CON_CTRL_EN	1

#define	DW_I2C_CON_SET_TGT_DIS(r, v)	bitset32(r, 6, 6, v)
#define	DW_I2C_CON_SET_RST(r, v)	bitset32(r, 5, 5, v)
#define	DW_I2C_CON_SET_10BIT_CTRL(r, v)	bitset32(r, 4, 4, v)
#define	DW_I2C_CON_SET_10BIT_TGT(r, v)	bitset32(r, 3, 3, v)
#define	DW_I2C_CON_SET_SPEED(r, v)	bitset32(r, 2, 1, v)
#define	DW_I2C_CON_SET_CTRL(r, v)	bitset32(r, 0, 0, v)

/*
 * IC_TAR -- This controls the target address register. In general, the device
 * must be disabled before this can be changed (though an optional feature
 * allows dynamic updates).
 */
#define	DW_I2C_TAR	0x04
#define	DW_I2C_TAR_SET_10BIT_CTRL(r, v)	bitset32(r, 12, 12, v)
#define	DW_I2C_TAR_SET_SPECIAL(r, v)	bitx32(r, 11, 11, v)
#define	DW_I2C_TAR_SET_TYPE(r, v)	bitx32(r, 10, 10, v)
#define	DW_I2C_TAR_TYPE_GEN_CALL	0
#define	DW_I2C_TAR_TYPE_START_BYTE	1
#define	DW_I2C_TAR_GET_ADDR(r)		bitx32(r, 9, 0)
#define	DW_I2C_TAR_SET_ADDR(r, v)	bitset32(r, 9, 0, v)

/*
 * IC_SAR -- This has the address of the target when operating as one.
 */
#define	DW_I2C_SAR	0x08
#define	DW_I2C_SAR_GET_ADDR(r)		bitx32(r, 9, 0)
#define	DW_I2C_SAR_SET_ADDR(r, v)	bitset32(r, 9, 0, v)

/*
 * IC_HS_MADDR -- When you're using a high-speed configuration with multiple
 * initiators, one can program a 3-bit code to indicate what to send for
 * arbitration purposes. This can only be manipulated while disabled.
 */
#define	DW_I2C_HS_CODE	0x0C
#define	DW_I2C_GET_HS_CODE(r)		bitx32(r, 2, 0)
#define	DW_I2C_SET_HS_CODE(r, v)	bitset32(r, 2, 0, v)

/*
 * IC_DATA_CMD -- This register is a bit deceptive. Reading it causes you to
 * read from the underlying receive FIFO. Writing to it causes you to write to
 * the underlying transmit FIFO. That means you cannot read back what you wrote.
 * The number of bits vary based on features. Only the first 9 bits are
 * guaranteed for writes and 8 bits (that is the data) for reads.
 */
#define	DW_I2C_DATA	0x10
#define	DW_I2C_DATA_GET_FIRST_BYTE(r)	bitx32(r, 11, 11)
#define	DW_I2C_DATA_SET_RESTART(r, v)	bitset32(r, 10, 10, v)
#define	DW_I2C_DATA_SET_STOP(r, v)	bitset32(r, 9, 9, v)
#define	DW_I2C_DATA_SET_CMD(r, v)	bitset32(r, 8, 8, v)
#define	DW_I2C_DATA_CMD_WRITE	0
#define	DW_I2C_DATA_CMD_READ	1
#define	DW_I2C_DATA_GET_DATA(r)		bitx32(r, 7, 0)
#define	DW_I2C_DATA_SET_DATA(r, v)	bitset32(r, 7, 0, v)

/*
 * IC_SS_SCL_HCNT, IC_SS_SCL_LCNT -- These registers control the period of the
 * high and low part of the clock respectively. The high part of the clock must
 * be in the bounds [6, 65525], where as the low part is [8, 65535].
 */
#define	DW_I2C_SS_SCL_HCNT	0x14
#define	DW_I2C_SS_SCL_LCNT	0x18
#define	DW_I2C_SCL_CNT_GET_CNT(r)	bitx32(r, 15, 0)
#define	DW_I2C_SCL_CNT_SET_CNT(r, v)	bitset32(r, 15, 0, v)

/*
 * IC_FS_SCL_HCNT, IC_FS_SCL_LCNT -- This is the variant of the above for fast
 * speed.
 */
#define	DW_I2C_FS_SCL_HCNT	0x1c
#define	DW_I2C_FS_SCL_LCNT	0x20

/*
 * IC_HS_SCL_HCNT, IC_HS_SCL_LCNT -- Finally, the high-speed equivalents.
 */
#define	DW_I2C_HS_SCL_HCNT	0x24
#define	DW_I2C_HS_SCL_LCNT	0x28

/*
 * The lower and upper bounds for the various registers. These are generally the
 * same. The HCNT MAX is called out for standard speed and so we use it for all
 * of them, lacking a better value.
 */
#define	DW_IC_SCL_HCNT_MIN	6
#define	DW_IC_SCL_HCNT_MAX	65525
#define	DW_IC_SCL_LCNT_MIN	8
#define	DW_IC_SCL_LCNT_MAX	UINT16_MAX

/*
 * IC_INTR_STAT, IC_INTR_MASK, IC_RAW_INTR_STAT -- These registers provide an
 * interrupt status, a mask to control what interrupts are generated, and the
 * underlying unmasked value respectively. All the bits are the same between
 * these.
 */
#define	DW_I2C_INTR_STS		0x2c
#define	DW_I2C_INTR_MASK	0x30
#define	DW_I2C_INTR_RAW		0x34

typedef enum {
	/*
	 * This bit is set when the initiator is holding the bus because the TX
	 * FIFO is empty.  However, this requires specific hardware
	 * configuration to function in the IP block.
	 */
	DW_I2C_INTR_INIT_HOLD	= 1 << 13,
	/*
	 * This bit is set when acting as a target and a RESTART has occurred.
	 * However, this requires specific hardware configuration to function
	 * in the IP block.
	 */
	DW_I2C_INTR_RESTART_DET	= 1 << 12,
	/*
	 * This bit is set when a general call has been received and the
	 * hardware has acknowledged it.
	 */
	DW_I2C_INTR_GEN_CALL	= 1 << 11,
	/*
	 * This bit indicates whether a START or RESTART occurred on the bus.
	 * This happens whether the device is operating as an initiator or
	 * target.
	 */
	DW_I2C_INTR_START_DET	= 1 << 10,
	/*
	 * This bit indicates that a STOP occurred on the bus. This bit is set
	 * regardless of whether operating as an initiator or target.
	 */
	DW_I2C_INTR_STOP_DET	= 1 << 9,
	/*
	 * This bit indicates that activity has occurred and remains until it is
	 * explicitly cleared.
	 */
	DW_I2C_INTR_ACTIVITY	= 1 << 8,
	/*
	 * This bit is set when if acting as a target, the remote initiator does
	 * not acknowledge a byte.
	 */
	DW_I2C_INTR_RX_DONE	= 1 << 7,
	/*
	 * This is used to indicate that a transmit abort has occurred with more
	 * detailed info indicated in the register DW_I2C_TX_ABORT.
	 */
	DW_I2C_INTR_TX_ABORT	= 1 << 6,
	/*
	 * This bit is set when the device is configured as a target and a
	 * request for data has come in.
	 */
	DW_I2C_INTR_READ_REQ	= 1 << 5,
	/*
	 * This bit's behavior varies on the setting in the DW_I2C_CON register.
	 * In general it is used to notify us when the TX queue is empty or has
	 * reached the notification threshold that is set via the
	 * DW_I2C_TX_THRESH register. When DW_I2C_CON_TX_EMPTY_EN is set, then
	 * this bit is not set until after the transmission of the data is
	 * complete. Otherwise we would get the interrupt as soon as it left the
	 * FIFO.
	 */
	DW_I2C_INTR_TX_EMPTY	= 1 << 4,
	/*
	 * This bit is set when we (the OS) write too much data into the data
	 * register and would overflow the TX queue.
	 */
	DW_I2C_INTR_TX_OVERRUN	= 1 << 3,
	/*
	 * This bit is set when the entire RX queue is full.
	 */
	DW_I2C_INTR_RX_FULL	= 1 << 2,
	/*
	 * This bit is set if the hardware receives additional data, but the
	 * FIFO is full. Overrun data is lost.
	 */
	DW_I2C_INTR_RX_OVERRUN	= 1 << 1,
	/*
	 * This bit is set if we try to read from the FIFO and there is no data
	 * available in it.
	 */
	DW_I2C_INTR_RX_UNDERRUN	= 1 << 0
} dw_i2c_intr_t;

#define	DW_I2C_INTR_GET_INTR(r)		bitx32(r, 13, 0)
#define	DW_I2C_INTR_SET_INTR(r, v)	bitset32(r, 13, 0)

/*
 * IC_RX_TL, IC_TX_TL -- These registers control the receive and transmit FIFO
 * thresholds. When we cross these we will trigger RX_FULL and TX_EMPTY
 * respectively.
 */
#define	DW_I2C_RX_THRESH	0x38
#define	DW_I2C_TX_THRESH	0x3c
#define	DW_I2C_THRESH_GET_THRESH(r)	bitx32(r, 7, 0)
#define	DW_I2C_THRESH_SET_THRESH(r, v)	bitset32(r, 7, 0, v)

/*
 * IC_CLR_INTR -- Reading this register has side effects. It will cause all
 * software clearable interrupts and the TX abort source register to be cleared.
 */
#define	DW_I2C_CLEAR_INTR	0x40

/*
 * IC_CLR_RX_UNDER, IC_CLR_RX_OVER, IC_CLR_TX_OVER, IC_CLR_RD_REQ,
 * IC_CLR_TX_ABRT, IC_CLR_RX_DONE, IC_CLR_ACTIVITY, IC_CLR_STOP_DET,
 * IC_CLR_START_DET, IC_CLR_GEN_CALL -- These registers all clear their
 * corresponding bit from the interrupt set.
 */
#define	DW_I2C_CLEAR_RX_UNDERRUN	0x44
#define	DW_I2C_CLEAR_RX_OVERRUN		0x48
#define	DW_I2C_CLEAR_TX_OVERRUN		0x4c
#define	DW_I2C_CLEAR_READ_REQ		0x50
#define	DW_I2C_CLEAR_TX_ABORT		0x54
#define	DW_I2C_CLEAR_RX_DONE		0x58
#define	DW_I2C_CLEAR_ACTIVITY		0x5c
#define	DW_I2C_CLEAR_STOP_DET		0x60
#define	DW_I2C_CLEAR_START_DET		0x64
#define	DW_I2C_CLEAR_GEN_CALL		0x68

/*
 * IC_ENABLE -- This register controls if the device is enabled or not, aborts,
 * and blocking transmit. The device generally must be disabled when we are
 * changing settings; however, it must be enabled to write into the FIFO.
 */
#define	DW_I2C_EN	0x6c
#define	DW_I2C_EN_SET_TX(r, v)	bitset32(r, 2, 2, v)
#define	DW_I2C_EN_TX_AUTO	0
#define	DW_I2C_EN_TX_BLOCK	1
#define	DW_I2C_EN_SET_ABORT(r, v)	bitset32(r, 1, 1, v)
#define	DW_I2C_EN_SET_EN(r, v)		bitset32(r, 0, 0, v)
#define	DW_I2C_EN_GET_EN(r)		bitx32(r, 0, 0)

/*
 * IC_STATUS -- Contains various read-only bits about the state of the system.
 *
 * Note, we keep the individual bits here as an enumeration so we can more
 * easily interpret this with MDB.
 */
#define	DW_I2C_STS	0x70
#define	DW_I2C_STS_GET_STS(r)		bitx32(r, 10, 0)
typedef enum {
	/*
	 * This status bit is set whenever the device is acting as a target and
	 * it is holding the bus due to the fact that the RX FIFO is full, but
	 * it would like to receive another byte.
	 */
	DW_I2C_STS_TGT_HOLD_RX_FULL	 = 1 << 10,
	/*
	 * This bit is set whenever the device is acting as a target and it is
	 * holding the bus because the TX FIFO is empty. This happens probably
	 * because there was a read from a remote initiator and the controller
	 * is waiting for a response.
	 */
	DW_I2C_STS_TGT_HOLD_TX_EMPTY	= 1 << 9,
	/*
	 * This bit is set when the device is acting as an initiator when the RX
	 * FIFO is full, the controller would receive another byte, and
	 * therefore the bus is being held.
	 */
	DW_I2C_STS_INIT_HOLD_RX_FULL	= 1 << 8,
	/*
	 * This bit is set whenever the device is acting as an initiator and its
	 * opted to hold the bus because the TX FIFO is empty.
	 */
	DW_I2C_STS_INIT_HOLD_TX_EMPTY	= 1 << 7,
	/*
	 * This bit is set whenever the device's target state machines are
	 * ongoing. When it's zero the device is idle and it's safe to modify
	 * things.
	 */
	DW_I2C_STS_TGT_ACTIVITY		= 1 << 6,
	/*
	 * This bit is set whenever the device's initiator state machines are
	 * ongoing. When it's zero the device is idle and it's safe to modify
	 * things.
	 */
	DW_I2C_STS_INIT_ACTIVITY	= 1 << 5,
	/*
	 * This bit is set whenever the RX FIFO is full.
	 */
	DW_I2C_STS_RX_FULL		= 1 << 4,
	/*
	 * This bit is set whenever the RX FIFO has data. When zero it's empty.
	 */
	DW_I2C_STS_RX_NOT_EMPTY		= 1 << 3,
	/*
	 * This bit is set whenever the TX FIFO is empty. When zero there is
	 * data to transmit.
	 */
	DW_I2C_STS_TX_EMPTY		= 1 << 2,
	/*
	 * This bit is set whenever the TX FIFO still has space for more data.
	 */
	DW_I2C_STS_TX_NOT_FULL		= 1 << 1,
	/*
	 * This bit is set whenever there is ongoing activity on the device.
	 */
	DW_I2C_STS_ACTIVITY		= 1 << 0
} dw_i2c_sts_t;

/*
 * IC_TXFLR, IC_RXFLR -- These contain the number of valid FIFO entries in the
 * transmit and receive queues respectively.
 */
#define	DW_I2C_TX_DEPTH	0x74
#define	DW_I2C_RX_DEPTH	0x78
#define	DW_I2C_DEPTH_GET_DEPTH(r)	bitx32(r, 8, 0)

/*
 * IC_SDA_HOLD -- This controls the hold time that should occur for rx (when
 * receiving) and tx (when transmitting).
 */
#define	DW_I2C_SDA_HOLD	0x7c
#define	DW_I2C_SDA_HOLD_GET_RX(r)	bitx32(r, 23, 16)
#define	DW_I2C_SDA_HOLD_GET_TX(r)	bitx32(r, 15, 0)
#define	DW_I2C_SDA_HOLD_SET_RX(r, v)	bitset32(r, 23, 16, v)
#define	DW_I2C_SDA_HOLD_SET_TX(r, v)	bitset32(r, 15, 0, v)

/*
 * IC_TX_ABRT_SOURCE -- This register contains information about why an abort
 * occurred. We store the individual reasons as an enum so we can more easily
 * print and debug this with DTrace / mdb. Note, the amount of abort bits
 * supported varies on the hardware compilation status.
 */
#define	DW_I2C_TX_ABORT	0x80
#define	DW_I2C_TX_ABORT_GET_TX_CNT(r)	bitx32(r, 31, 23)
#define	DW_I2C_TX_ABORT_GET_STS(r)	bitx32(r, 22, 0)
typedef enum {
	/*
	 * This bit is set when software initiates an abort on the initiator via
	 * the DW_I2C_EN register.
	 */
	DW_I2C_ABORT_USER		= 1 << 16,
	/*
	 * This abort happens when acting as a target. After the device has
	 * received a read request that it must reply to, if it sets the write
	 * bit in a response in the TX FIFO via the DW_I2C_DATA register,
	 * that'll cause this abort.
	 */
	DW_I2C_ABORT_TGT_READ		= 1 << 15,
	/*
	 * This occurs when acting as a target and arbitration is lost on the
	 * bus. Though the datasheet notes that when acting as a target this is
	 * more of a something weird happened case.
	 */
	DW_I2C_ABORT_TGT_ARB		= 1 << 14,
	/*
	 * This abort occurs in target mode when a read request comes in but
	 * there is outstanding data in the TX FIFO. The target state machine in
	 * hardware wants to ensure that this is empty.
	 */
	DW_I2C_ABORT_TGT_FLUSH_TX	= 1 << 13,
	/*
	 * This indicates that an arbitration loss happened on the bus. If
	 * DW_I2C_ABORT_TGT_ARB is set, then this is a loss in target mode,
	 * otherwise initiator mode.
	 */
	DW_I2C_ABORT_ARB_LOST		= 1 << 12,
	/*
	 * This abort occurs if the initiator is disabled, but we tried to do
	 * something with it.
	 */
	DW_I2C_ABORT_CTRL_DIS		= 1 << 11,
	/*
	 * This occurs when restart's are not enabled in DW_I2C_CON and a 10-bit
	 * initiator needs to send a read command.
	 */
	DW_I2C_ABORT_10B_RESTART_DIS	= 1 << 10,
	/*
	 * This occurs because an initiator tried to send a start condition, but
	 * it was not able to due to the configuration of DW_I2C_CON.
	 */
	DW_I2C_ABORT_START_RESTART	= 1 << 9,
	/*
	 * The controller is in high speed mode and restart was disabled, but
	 * data the initiator was attempted to be used.
	 */
	DW_I2C_ABORT_HS_NORESTART	= 1 << 8,
	/*
	 * This occurs when a start byte is sent by the controller on the bus
	 * and it is acknowledged.
	 */
	DW_I2C_ABORT_START_ACK		= 1 << 7,
	/*
	 * This abort occurs when operating in high-speed mode and the initiator
	 * code was acknowledged.
	 */
	DW_I2C_ABORT_HIGH_CODE_ACK	= 1 << 6,
	/*
	 * This abort occurs when the device is set up to send a general call,
	 * but then a read byte is inserted into the transmit FIFO.
	 */
	DW_I2C_ABORT_GEN_CALL_READ	= 1 << 5,
	/*
	 * This abort occurs when a general call is issued by the controller but
	 * no device on the bus acknowledges i.
	 */
	DW_I2C_ABORT_GEN_CALL_NOACK	= 1 << 4,
	/*
	 * This occurs when there is no acknowledgement for data bytes after
	 * acknowledging the address.
	 */
	DW_I2C_ABORT_TX_NOACK		= 1 << 3,
	/*
	 * This abort occurs when operating with a 10-bit address and there is
	 * no acknowledgement received for the second address byte.
	 */
	DW_I2C_ABORT_10B_ADDR2_NOACK	= 1 << 2,
	/*
	 * This abort occurs when operating with a 10-bit address and there is
	 * no acknowledgement received for the first address byte.
	 */
	DW_I2C_ABORT_10B_ADDR1_NOACK	= 1 << 1,
	/*
	 * This abort occurs when operating with a 7-bit address and there is no
	 * acknowledgement received to the address byte.
	 */
	DW_I2C_ABORT_7B_ADDR_NOACK	= 1 << 0
} dw_i2c_abort_t;

/*
 * IC_SLV_DATA_NACK_ONLY -- This register controls whether a nack should be
 * generated in response to any data received while acting as a target. The
 * presence of this depends on whether certain aspects of the IP were compiled
 * in.
 */
#define	DW_I2C_TGT_NACK	0x84
#define	DW_I2C_TGT_NACK_GET_NACK(r)		bitset32(r, 0, 0)
#define	DW_I2C_TGT_NACK_SET_NACK(r, v)		bitset32(r, 0, 0, v)

/*
 * IC_SDA_SETUP -- This appears to control the amount of time that the clock is
 * held low during target operation related to replying to a read request. The
 * register is in units of the clock frequency. There is a required minimum
 * value of 2.
 */
#define	DW_I2C_SDA_SETUP	0x94
#define	DW_I2C_SDA_SETUP_SET_CYCLES(r, v)	bitst32(r, 7, 0, v)

/*
 * IC_ACK_GENERAL_CALL -- Controls whether the controller acks or nacks a
 * general call. This happens regardless of whether the device is a target or
 * initiator.
 */
#define	DW_I2C_GC_ACK		0x98
#define	DW_IC2_GC_ACK_GET_GC(r)		bitx32(r, 0, 0)
#define	DW_I2C_GC_ACK_GC_NACK		0
#define	DW_I2C_GC_ACK_GC_ACK		1
#define	DW_I2C_GC_ACK_SET_GC(r, v)	bitset32(r, 0, 0, v)

/*
 * IC_ENABLE_STATUS -- This register controls the general enable and disable
 * status.
 */
#define	DW_I2C_EN_STS		0x9c
#define	DW_I2C_EN_STS_GET_TGT_RX_LOST	bitx32(r, 2, 2, v)
#define	DW_I2C_EN_STS_GET_TGT_DIS_BUSY	bitx32(r, 1, 1, v)
#define	DW_I2C_EN_STS_GET_EN(r)		bitx32(r, 0, 0, v)

/*
 * IC_FS_SPKLEN, IC_HS_SPKLEN -- When operating at either full- or high-speed
 * the hardware has the ability to filter out certain spikes. This measures the
 * count of clock cycles that should be used to ignore spikes on either the
 * clock or data line.
 */
#define	DW_I2C_SPIKE_FS		0xa0
#define	DW_I2C_SPIKE_HS		0xa4
#define	DW_I2C_SPIKE_GET_CYCLES(r)	bitx32(r, 7, 0)
#define	D2_I2C_SPKE_SET_CYCLES(r, v)	bitset32(r, 7, 0, v)

/*
 * IC_COMP_PARAM_1 -- This register contains various features of the device as
 * built. However, there is a catch. If the valid bit is 0, then all of this
 * isn't trusted. In addition, this doesn't actually cover all of the features,
 * but you take what you can get.
 */
#define	DW_I2C_COMP_PARAM_1		0xf4
#define	DW_I2C_COMP_PARAM_1_GET_TX_BUF(r)	bitx32(r, 23, 16)
#define	DW_I2C_COMP_PARAM_1_GET_RX_BUF(r)	bitx32(r, 15, 8)
#define	DW_I2C_COMP_PARAM_1_GET_VALID(r)	bitx32(r, 7, 7)
#define	DW_I2C_COMP_PARAM_1_GET_DMA(r)		bitx32(r, 6, 6)
#define	DW_I2C_COMP_PARAM_1_GET_INTR_IO(r)	bitx32(r, 5, 5)
#define	DW_I2C_COMP_PARAM_1_GET_HC_COUNT(r)	bitx32(r, 4, 4)
#define	DW_I2C_COMP_PARAM_1_GET_MAX_SPEED(r)	bitx32(r, 3, 2)
#define	DW_I2C_COMP_PARAM_1_MAX_SPEED_STD	1
#define	DW_I2C_COMP_PARAM_1_MAX_SPEED_FAST	2
#define	DW_I2C_COMP_PARAM_1_MAX_SPEED_HIGH	3
#define	DW_I2C_COMP_PARAM_1_GET_APB_WIDTH(r)	bitx32(r, 1, 0)
#define	DW_I2C_COMP_PARAM_1_APB_WIDTH_8B	0
#define	DW_I2C_COMP_PARAM_1_APB_WIDTH_16B	1
#define	DW_I2C_COMP_PARAM_1_APB_WIDTH_32B	2

/*
 * DW_I2C_COMP_VERSION -- The component version.
 */
#define	DW_I2C_COMP_VERS		0xf8
#define	DW_I2C_COMP_VERS_MIN_SDA_HOLD	0x3131312a

/*
 * DW_I2C_COMP_TYPE -- This register can be used to theoretically determine the
 * type of device this is. While this type is in theory global across the
 * Designware space and could make this self-identifying, you'd have to know it
 * already existed.
 */
#define	DW_I2C_COMP_TYPE		0xfc
#define	DW_I2C_COMP_TYPE_I2C		0x44570140

#ifdef __cplusplus
}
#endif

#endif /* _DW_APB_I2C_H */
