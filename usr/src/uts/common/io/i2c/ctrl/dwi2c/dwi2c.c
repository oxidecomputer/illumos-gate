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
 * Device driver for the Designware I2C controller. Currently this only works on
 * the oxide platform for AMD devices.
 *
 * ---------
 * Discovery
 * ---------
 *
 * The designware I2C block is a common I2C controller that is found on a wide
 * number of platforms. The means by which it is discovered varies on the
 * platform. While it is most often an MMIO device that is discovered by some
 * platform-specific means (e.g. ACPI, device tree, etc.) it can also sometimes
 * be found as a PCI(e) device. The driver is intended to work with any parent
 * that can set up a reg[] entry.
 *
 * ------------------
 * I/O State Machines
 * ------------------
 *
 * The hardware functions by having a variable sized FIFO that may be smaller
 * than the overall size of the request. Commands are pushed into this FIFO
 * which result in either a read or write of a byte on the bus occurring. After
 * a request is started, we basically have the following rough state machine:
 *
 *  Framework Request
 *       |
 *       * . Fixed request start up: programming address
 *       |   registers and resetting interrupt related bits.
 *       |
 *       v
 *     +----+
 *     | TX |---------------------------+------------------+
 *     +----+                           |                  |
 *       |                              |                  |
 *       * . Once all bytes are         |                  |
 *       |   transmitted.               |                  |
 *       v                              |                  |
 *     +----+                           |                  |
 *     | RX |---------------------------+------------------+
 *     +----+                           |                  |
 *       |                              |                  |
 *       * . Once all bytes are         |                  |
 *       |   received.                  |                  |
 *       v                              |                  |
 *     +------+                         |                  |
 *     | STOP |-------------------------|------------------+
 *     +------+                         |                  |
 *       |                              |                  |
 *       * . Once the controller        |                  |
 *       |   indicates it has written   |                  |
 *       |   a stop.                    |                  |
 *       v                              |                  v
 *     +------+                         |              +-------+
 *     | DONE |<------------------------+--------------| ABORT |
 *     +------+                                        +-------+
 *       |
 *       |
 *       v
 * Framework Reply
 *
 * The broader I2C framework guarantees that only one request can be outstanding
 * at a time due to the fact that the bus can only have one I/O ongoing at any
 * time. We first start the transaction by claiming the bus, setting up the
 * address information, and beginning to fill bytes in the command buffer. First
 * we fill any bytes to transmit. If there are no bytes to transmit or we fill
 * all the transmit commands and still have space, then we'll move onto filling
 * the command buffer with receive commands. Once those are done (or if there
 * are none), we'll finish by writing a stop command into the command FIFO. Only
 * once the command FIFO is finished, then we will proceed to the DONE state and
 * reply with a successful command to the framework.
 *
 * Of course, it wouldn't be I2C if everything just worked. There are two
 * different classes of errors that can occur. The first is when the controller
 * tells us that there was a transmit abort. We get this as part of polling for
 * status. This includes everything from an address NAK to arbitration lost, or
 * that we got a NAK while reading from the target. In those cases, the
 * controller will have stopped everything and the queues end up flushed. When
 * this kind of abort occurs, we translate the hardware specific cause
 * information into the appropriate I2C error. The behavior that we do is
 * captured in dwi2c_aborts[]. In general, we don't log most errors other than
 * things that we think are related to weird behavior as a result of the driver
 * and panic on error that could only happen due to gross programmer error at
 * our end (e.g. we receive an error related to target mode that we don't
 * enable).
 *
 * The second class of error is our internal state machines ABORT, aka
 * DW_I2C_IO_ABORT. This occurs when we have no response from the hardware after
 * any period of time. The most common case for this is when we're on a bus
 * without the proper pull up resistors to function. When this occurs, we'll
 * instead turn off the device and issue a hardware abort. The controller will
 * reply relatively promptly to the abort.
 *
 * Now, inside of the original state machine is a second state machine related
 * to the FIFO depth. The FIFO has both a transmit and receive maximum depth.
 * These tell us how many commands we can issue before we need to take action.
 * Notably when we issue any command (whether for a read or a write) that goes
 * into the transmit FIFO. Any data that comes back goes into the read FIFO.
 * Related to this, there are thresholds around where the hardware will notify
 * us about the FIFO's state. There are separate RX and TX thresholds.
 *
 * We always set the RX threshold to trigger the moment there's a single byte.
 * There's no reason to do anything else as the moment data is available we want
 * to read it, even if there's more data available. The second bit is the TX
 * threshold. We basically set that to half the size of the TX FIFO. The idea
 * here is that since we'll fill up the TX FIFO with commands, there's no need
 * to come back to that until there's enough space to issue another chunk of
 * commands.
 *
 * Let's expand the TX state that we had above for a moment:
 *
 *     +-----------+                    +----------+
 *     | Data to   |---*--------------->| TX Queue |
 *     | transmit? |   . Yes            | Full?    |
 *     +-----------+                    +----------+
 *       |    ^                           |      |
 *  No . *    |                      No . *      * . Yes
 *       |    |           +--------+      |      |
 *       |    +-----------| Insert |------+      |
 *       |    |           +--------+             |
 *       |    |                                  v
 *       |    |                        +--------------+
 *       |    +------------------------| TX Threshold |
 *       |                             |   Crossed    |
 *       v                             |  Status Set  |
 *  Proceed to RX                      +--------------+
 *
 * To determine if the TX FIFO is full, we look at the depth of the FIFO and
 * compare that to the limit that we set. Because of the nature of how the
 * interrupts work, even if the FIFO transitions while we're processing it,
 * because of the write-to-clear nature, we will find that fact when we next
 * look for status.
 *
 * One important thing here is that this doesn't actually indicate whether the
 * data was successfully sent. That's ultimately why we end up waiting for the
 * stopped status, which is our sign that everything is done on the transaction.
 * Next, let's discuss how we deal with RX commands.
 *
 * RX commands come in two parts, we have to put data into the transmit FIFO and
 * then we have to read it out of the receive FIFO. This puts an important bound
 * on the amount of receive commands that we want to issue. Instead of us just
 * asking if the TX FIFO is full, we need to ask is the TX FIFO full and how
 * much outstanding I/O is there to be read in the RX FIFO. Imagine a case where
 * the TX FIFO had 32 entries, but the RX FIFO only had 16. If we put more than
 * 16 read commands into the TX FIFO, then then the RX FIFO would lose data.
 *
 * This is also why we'll always attempt to process the RX FIFO once we're in
 * the RX state first, before we send commands. Once we enter the RX state, we
 * also change our interrupt mask, removing asking for an interrupt on an empty
 * TX FIFO as what matters isn't that the TX FIFO is empty, but that we have
 * data in the RX FIFO. This is roughly summarized in the following flow
 * diagram:
 *
 *    +----------+        . No   +----------+        +----------------+
 *    | All data |--------*----->| Drain RX |------->| All read bytes |<----+
 *    |   read?  |               |   FIFO   |        |   requested?   |     |
 *    +----------+               +----------+        +----------------+     |
 *       |    ^                                         |          |        |
 *       |    |           . Yes                         |     No . *        |
 *       |    +-----------*-----------------------------+          |        |
 *       |    |                                                    |        |
 *       |    |                                                    |        |
 *       |    |                                                    v        |
 *       |    |           . No                          +-------------+     |
 *       |    +-----------*-----------------------------|  Space in   |     |
 *       |                                              | TX/RX FIFO? |     |
 *       |                                              +-------------+     |
 *       |                                                      |           |
 *       v                                                      v           |
 * Proceed to STOP                                         +--------+       |
 *                                                         | Insert |->-----+
 *                                                         +--------+
 *
 * After this point, we simply insert a stop command to be inserted into the
 * transmit FIFO, which always has space for this by definition of our state
 * machine (if we've read everything there are no commands) and we can proceed
 * to wait for the controller to acknowledge it.
 */

#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/stdbool.h>
#include <sys/stdbit.h>

#include <sys/i2c/controller.h>
#include "dw_apb_i2c.h"

/*
 * These should be relegated to a platform-specific module / the DDI. Until we
 * solve this with the generalized regset, we need this for now.
 */
#include <sys/amdzen/mmioreg.h>
#include <sys/io/fch/i2c.h>

/*
 * The device programming guide recommends delaying for up to 10 times the clock
 * frequency when checking. For a 100 kHz bus at standard speed, this would be
 * 100 us.
 */
static uint32_t dwi2c_en_count = 100;
static uint32_t dwi2c_en_delay_us = 100;

typedef enum {
	/*
	 * Indicates that the hardware supports the data hold related registers.
	 */
	DWI2C_F_SUP_SDA_HOLD		= 1 << 0,
	/*
	 * This is used to indicate that our properties have changed such that
	 * we need an update to the current timing properties.
	 */
	DWI2C_F_NEED_TIME_UPDATE	= 1 << 1
} dwi2c_flags_t;

typedef enum {
	DWI2C_IO_TX,
	DWI2C_IO_RX,
	DWI2C_IO_STOP,
	DWI2C_IO_ABORT,
	DWI2C_IO_DONE
} dwi2c_io_state_t;

typedef struct dwi2c {
	dev_info_t *dwi_dip;
	mmio_reg_block_t dwi_rb;
	i2c_ctrl_hdl_t *dwi_hdl;
	kmutex_t dwi_mutex;
	kcondvar_t dwi_cv;
	/*
	 * Version and features of the hardware.
	 */
	uint32_t dwi_vers;
	uint32_t dwi_params;
	dwi2c_flags_t dwi_flags;
	/*
	 * What are possible speeds and what have we been asked to set. As well
	 * as maximum FIFO values.
	 */
	i2c_speed_t dwi_speed_pos;
	i2c_speed_t dwi_speed_cur;
	i2c_speed_t dwi_speed_def;
	uint16_t dwi_fifo_rx_max;
	uint16_t dwi_fifo_tx_max;
	/*
	 * Values for various device timing registers.
	 */
	uint16_t dwi_ss_hcnt;
	uint16_t dwi_ss_lcnt;
	uint16_t dwi_fs_hcnt;
	uint16_t dwi_fs_lcnt;
	uint16_t dwi_hs_hcnt;
	uint16_t dwi_hs_lcnt;
	uint8_t dwi_sda_rx_hold;
	uint16_t dwi_sda_tx_hold;
	/*
	 * Current interrupt mask that we should be using.
	 */
	dw_i2c_intr_t dwi_mask;
	/*
	 * The current request and our state processing it. For transmit we
	 * track our offset in the buffer (tx_off). For receive we have to track
	 * several different things: out offset in the receive buffer that we
	 * should fill (rx_off), the number of requests we have actually issued
	 * to the transmit FIFO (rx_req), and the number of currently
	 * outstanding requests to avoid an rx FIFO overrun (rx_nout).
	 */
	i2c_req_t *dwi_req;
	dwi2c_io_state_t dwi_req_state;
	uint32_t dwi_req_tx_off;
	uint32_t dwi_req_rx_off;
	uint32_t dwi_req_rx_req;
	uint32_t dwi_req_rx_nout;
	uint32_t dwi_last_abort;
	dw_i2c_intr_t dwi_last_intr;
} dwi2c_t;

/*
 * The following is the interrupt mask that we enable by default for the
 * controller.
 */
static const dw_i2c_intr_t dwi2c_intr_mask = DW_I2C_INTR_RX_FULL |
    DW_I2C_INTR_TX_EMPTY | DW_I2C_INTR_TX_ABORT | DW_I2C_INTR_STOP_DET;

/*
 * We can isolate all of the amdzen/mmioreg.h stuff by moving register read /
 * regs setup to specific files. This would make it pretty easy to build ACPI
 * and oxide specific versions or other mmio bits. In the future, this should be
 * something we could figure out how to unify in the driver / DDI.
 */
static uint32_t
dwi2c_read32(dwi2c_t *dwi, uint32_t reg)
{
	const smn_reg_def_t r = {
		.srd_unit = SMN_UNIT_FCH_I2C,
		.srd_reg = reg
	};

	const mmio_reg_t mmio = fch_i2c_mmio_reg(dwi->dwi_rb, r, 0);
	return (x_ddi_reg_get(mmio));
}

static void
dwi2c_write32(dwi2c_t *dwi, uint32_t reg, uint32_t val)
{
	const smn_reg_def_t r = {
		.srd_unit = SMN_UNIT_FCH_I2C,
		.srd_reg = reg
	};

	const mmio_reg_t mmio = fch_i2c_mmio_reg(dwi->dwi_rb, r, 0);
	x_ddi_reg_put(mmio, val);
}

static bool
dwi2c_ctrl_en_dis(dwi2c_t *dwi, bool en)
{
	for (uint32_t i = 0; i < dwi2c_en_count; i++) {
		uint32_t reg = dwi2c_read32(dwi, DW_I2C_EN);
		if (DW_I2C_EN_GET_EN(reg) == en) {
			return (true);
		}

		reg = DW_I2C_EN_SET_EN(reg, en);
		dwi2c_write32(dwi, DW_I2C_EN, reg);
		delay(drv_usectohz(dwi2c_en_delay_us));
	}

	dev_err(dwi->dwi_dip, CE_WARN, "timed out trying to %s controller",
	    en ? "enable" : "disable");
	return (false);
}

/*
 * Go through and determine initial values for timings that we should use. In
 * the future, this should provide a hook to source this information via ACPI,
 * device tree, or similar and then we use the hardware defaults otherwise.
 *
 * An alternative approach to consider for the future is rather than saying the
 * hardware probably has a reasonable configuration, would be to go through and
 * determine what makes sense for the device based on the initial frequency that
 * we want to run this at.
 */
static void
dwi2c_timing_init(dwi2c_t *dwi)
{
	if (dwi->dwi_ss_hcnt == 0) {
		uint32_t v = dwi2c_read32(dwi, DW_I2C_SS_SCL_HCNT);
		dwi->dwi_ss_hcnt = DW_I2C_SCL_CNT_GET_CNT(v);
	}

	if (dwi->dwi_ss_lcnt == 0) {
		uint32_t v = dwi2c_read32(dwi, DW_I2C_SS_SCL_LCNT);
		dwi->dwi_ss_lcnt = DW_I2C_SCL_CNT_GET_CNT(v);
	}

	if (dwi->dwi_fs_hcnt == 0) {
		uint32_t v = dwi2c_read32(dwi, DW_I2C_FS_SCL_HCNT);
		dwi->dwi_fs_hcnt = DW_I2C_SCL_CNT_GET_CNT(v);
	}

	if (dwi->dwi_fs_lcnt == 0) {
		uint32_t v = dwi2c_read32(dwi, DW_I2C_FS_SCL_LCNT);
		dwi->dwi_fs_lcnt = DW_I2C_SCL_CNT_GET_CNT(v);
	}

	if (dwi->dwi_hs_hcnt == 0) {
		uint32_t v = dwi2c_read32(dwi, DW_I2C_HS_SCL_HCNT);
		dwi->dwi_hs_hcnt = DW_I2C_SCL_CNT_GET_CNT(v);
	}

	if (dwi->dwi_hs_lcnt == 0) {
		uint32_t v = dwi2c_read32(dwi, DW_I2C_HS_SCL_LCNT);
		dwi->dwi_hs_lcnt = DW_I2C_SCL_CNT_GET_CNT(v);
	}

	if ((dwi->dwi_flags & DWI2C_F_SUP_SDA_HOLD) == 0)
		return;

	uint32_t v = dwi2c_read32(dwi, DW_I2C_SDA_HOLD);
	if (dwi->dwi_sda_rx_hold == 0) {
		dwi->dwi_sda_rx_hold = DW_I2C_SDA_HOLD_GET_RX(v);
		dwi->dwi_sda_tx_hold = DW_I2C_SDA_HOLD_GET_TX(v);
	}
}

static uint32_t
dwi2c_speed_to_reg(dwi2c_t *dwi)
{
	switch (dwi->dwi_speed_cur) {
	case I2C_SPEED_STD:
		return (DW_I2C_CON_SPEED_STD);
	case I2C_SPEED_FAST:
	case I2C_SPEED_FPLUS:
		return (DW_I2C_CON_SPEED_FAST);
	case I2C_SPEED_HIGH:
		return (DW_I2C_CON_SPEED_HIGH);
	default:
		panic("programmer error: invalid/unsupported I2C speed");
	}
}

/*
 * Update all of the timing values in the controller based on values that we
 * have in the controller. This can only be called while the controller is
 * disabled. Callers are responsible for making sure that is the case.
 */
static void
dwi2c_timing_update(dwi2c_t *dwi)
{
	uint32_t v;

	v = dwi2c_read32(dwi, DW_I2C_CON);
	v = DW_I2C_CON_SET_SPEED(v, dwi2c_speed_to_reg(dwi));
	dwi2c_write32(dwi, DW_I2C_CON, v);

	v = dwi2c_read32(dwi, DW_I2C_SS_SCL_HCNT);
	v = DW_I2C_SCL_CNT_SET_CNT(v, dwi->dwi_ss_hcnt);
	dwi2c_write32(dwi, DW_I2C_SS_SCL_HCNT, v);

	v = dwi2c_read32(dwi, DW_I2C_SS_SCL_LCNT);
	v = DW_I2C_SCL_CNT_SET_CNT(v, dwi->dwi_ss_lcnt);
	dwi2c_write32(dwi, DW_I2C_SS_SCL_LCNT, v);

	v = dwi2c_read32(dwi, DW_I2C_FS_SCL_HCNT);
	v = DW_I2C_SCL_CNT_SET_CNT(v, dwi->dwi_fs_hcnt);
	dwi2c_write32(dwi, DW_I2C_FS_SCL_HCNT, v);

	v = dwi2c_read32(dwi, DW_I2C_FS_SCL_LCNT);
	v = DW_I2C_SCL_CNT_SET_CNT(v, dwi->dwi_fs_lcnt);
	dwi2c_write32(dwi, DW_I2C_FS_SCL_LCNT, v);

	v = dwi2c_read32(dwi, DW_I2C_HS_SCL_HCNT);
	v = DW_I2C_SCL_CNT_SET_CNT(v, dwi->dwi_hs_hcnt);
	dwi2c_write32(dwi, DW_I2C_HS_SCL_HCNT, v);

	v = dwi2c_read32(dwi, DW_I2C_HS_SCL_LCNT);
	v = DW_I2C_SCL_CNT_SET_CNT(v, dwi->dwi_hs_lcnt);
	dwi2c_write32(dwi, DW_I2C_HS_SCL_LCNT, v);

	if ((dwi->dwi_flags & DWI2C_F_SUP_SDA_HOLD) == 0)
		return;

	v = dwi2c_read32(dwi, DW_I2C_SDA_HOLD);
	v = DW_I2C_SDA_HOLD_SET_RX(v, dwi->dwi_sda_rx_hold);
	v = DW_I2C_SDA_HOLD_SET_TX(v, dwi->dwi_sda_tx_hold);
	dwi2c_write32(dwi, DW_I2C_SDA_HOLD, v);
}

/*
 * Prepare the controller for use. Here we need to go through and do a few
 * different things:
 *
 *  - Determine the controller's version and features.
 *  - Determine initial timing values.
 *  - Initialize the FIFO depths.
 *  - Determine the set of supported speeds
 *  - Disable the target mode of operation and configure everything to run the
 *    controller.
 */
static bool
dwi2c_ctrl_init(dwi2c_t *dwi)
{
	dwi->dwi_vers = dwi2c_read32(dwi, DW_I2C_COMP_VERS);
	dwi->dwi_params = dwi2c_read32(dwi, DW_I2C_COMP_PARAM_1);

	if (dwi->dwi_vers >= DW_I2C_COMP_VERS_MIN_SDA_HOLD) {
		dwi->dwi_flags |= DWI2C_F_SUP_SDA_HOLD;
	}

	dwi2c_timing_init(dwi);

	/*
	 * It is important that the controller is disabled before we attempt to
	 * program any registers that impact its operation. We purposefully
	 * leave this with the controller disabled. It will remain disabled
	 * until we perform I/O.
	 */
	if (!dwi2c_ctrl_en_dis(dwi, false)) {
		return (false);
	}

	/*
	 * Determine the set of speeds this controller supports and set the
	 * default speed as the lowest that it supports.
	 */
	uint8_t max_speed = DW_I2C_COMP_PARAM_1_GET_MAX_SPEED(dwi->dwi_params);
	if (max_speed >= DW_I2C_COMP_PARAM_1_MAX_SPEED_STD) {
		if (dwi->dwi_speed_cur == 0) {
			dwi->dwi_speed_cur = I2C_SPEED_STD;
		}
		dwi->dwi_speed_pos |= I2C_SPEED_STD;
	}

	if (max_speed >= DW_I2C_COMP_PARAM_1_MAX_SPEED_FAST) {
		if (dwi->dwi_speed_cur == 0) {
			dwi->dwi_speed_cur = I2C_SPEED_FAST;
		}
		dwi->dwi_speed_pos |= I2C_SPEED_FAST | I2C_SPEED_FPLUS;
	}

	if (max_speed >= DW_I2C_COMP_PARAM_1_MAX_SPEED_HIGH) {
		if (dwi->dwi_speed_cur == 0) {
			dwi->dwi_speed_cur = I2C_SPEED_HIGH;
		}
		dwi->dwi_speed_pos |= I2C_SPEED_HIGH;
	}

	if (max_speed == 0) {
		dev_err(dwi->dwi_dip, CE_WARN, "controller has invalid "
		    "maximum speed, limiting device to standard 100 kHz");
		dwi->dwi_speed_pos = I2C_SPEED_STD;
		dwi->dwi_speed_cur = I2C_SPEED_STD;
	}
	dwi->dwi_speed_def = dwi->dwi_speed_cur;

	/*
	 * We need to set thresholds for when to trigger interrupts on the FIFO.
	 * Hardware basically only has a way to notify is when the receive FIFO
	 * is "full". WE basically need to set the threshold to zero so we are
	 * notified whenever there is data. Otherwise, we would not be able to
	 * rely on simply polling / waiting for interrupt bits to be set.
	 *
	 * On the TX side, we want to have a chance to start adding more data
	 * before the hardware has finished and emptied the FIFO. As such, we
	 * set the transmit empty threshold to half so we can keep putting data
	 * in there before the FIFO empties.
	 */
	dwi->dwi_fifo_rx_max = DW_I2C_COMP_PARAM_1_GET_RX_BUF(dwi->dwi_params) +
	    1;
	dwi->dwi_fifo_tx_max = DW_I2C_COMP_PARAM_1_GET_TX_BUF(dwi->dwi_params) +
	    1;
	dwi2c_write32(dwi, DW_I2C_RX_THRESH, 0);
	dwi2c_write32(dwi, DW_I2C_TX_THRESH, dwi->dwi_fifo_tx_max / 2);

	/*
	 * Go and program the controller with the default timing values that we
	 * have stored.
	 */
	dwi2c_timing_update(dwi);

	/*
	 * Finally actually program the controller register itself. We set this
	 * up so the controller is operational, there is no target mode. We use
	 * the speed that we determined above. We default to 7-bit addressing,
	 * but this will be changed by any I/O that we perform. We also default
	 * to enabling the restart mode of the controller as this is part of the
	 * standard documentation flow.
	 */
	uint32_t con = 0;
	con = DW_I2C_CON_SET_CTRL(con, DW_I2C_CON_CTRL_EN);
	con = DW_I2C_CON_SET_SPEED(con, dwi2c_speed_to_reg(dwi));
	con = DW_I2C_CON_SET_10BIT_TGT(con, 0);
	con = DW_I2C_CON_SET_10BIT_CTRL(con, 0);
	con = DW_I2C_CON_SET_RST(con, DW_I2C_CON_RST_EN);
	con = DW_I2C_CON_SET_TGT_DIS(con, DW_I2C_CON_TGT_DIS);
	dwi2c_write32(dwi, DW_I2C_CON, con);

	return (true);
}

static bool
dwi2c_regs_setup(dwi2c_t *dwi)
{
	int nregs, ret;
	ddi_device_acc_attr_t attr;
	uint32_t type;

	if (ddi_dev_nregs(dwi->dwi_dip, &nregs) != DDI_SUCCESS) {
		dev_err(dwi->dwi_dip, CE_WARN, "failed to get number of "
		    "device registers");
		return (false);
	}

	if (nregs != 1) {
		dev_err(dwi->dwi_dip, CE_WARN, "encountered unexpected "
		    "number of device registers %u, expected 1", nregs);
		return (false);
	}

	attr.devacc_attr_version = DDI_DEVICE_ATTR_V1;
	attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	attr.devacc_attr_access = DDI_DEFAULT_ACC;

	ret = x_ddi_reg_block_setup(dwi->dwi_dip, 0, &attr, &dwi->dwi_rb);
	if (ret != DDI_SUCCESS) {
		dev_err(dwi->dwi_dip, CE_WARN, "failed to map i2c register "
		    "block: %d", ret);
		return (false);
	}

	/*
	 * Currently we assume that the register layout is in the current
	 * endianness. This check sees whether or not this is true or we have
	 * the device that we expect. If we find a part where this is reversed,
	 * then we need to set up a new device attributes where we have swapping
	 * in place. If the ID doesn't match an endian-swapped form, then we
	 * should hard fail.
	 */
	type = dwi2c_read32(dwi, DW_I2C_COMP_TYPE);
	if (type != DW_I2C_COMP_TYPE_I2C) {
		dev_err(dwi->dwi_dip, CE_WARN, "found unexpected device "
		    "type 0x%x\n", type);
		return (false);
	}

	return (true);
}

static i2c_errno_t
dwi2c_prop_info(void *arg, i2c_prop_t prop, i2c_prop_info_t *info)
{
	dwi2c_t *dwi = arg;
	i2c_errno_t ret = I2C_CORE_E_OK;

	mutex_enter(&dwi->dwi_mutex);

	switch (prop) {
	case I2C_PROP_BUS_SPEED:
		i2c_prop_info_set_perm(info, I2C_PROP_PERM_RW);
		i2c_prop_info_set_def_u32(info, dwi->dwi_speed_def);
		i2c_prop_info_set_pos_bit32(info, dwi->dwi_speed_pos);
		break;
	case I2C_PROP_MAX_READ:
	case I2C_PROP_MAX_WRITE:
		i2c_prop_info_set_perm(info, I2C_PROP_PERM_RO);
		i2c_prop_info_set_def_u32(info, I2C_REQ_MAX);
		break;
	case I2C_PROP_STD_SCL_HIGH:
	case I2C_PROP_FAST_SCL_HIGH:
	case I2C_PROP_HIGH_SCL_HIGH:
		i2c_prop_info_set_perm(info, I2C_PROP_PERM_RW);
		i2c_prop_info_set_range_u32(info, DW_IC_SCL_HCNT_MIN,
		    DW_IC_SCL_HCNT_MAX);
		break;
	case I2C_PROP_STD_SCL_LOW:
	case I2C_PROP_FAST_SCL_LOW:
	case I2C_PROP_HIGH_SCL_LOW:
		i2c_prop_info_set_perm(info, I2C_PROP_PERM_RW);
		i2c_prop_info_set_range_u32(info, DW_IC_SCL_LCNT_MIN,
		    DW_IC_SCL_LCNT_MAX);
		break;
	default:
		ret = I2C_PROP_E_UNSUP;
	}

	mutex_exit(&dwi->dwi_mutex);
	return (ret);
}

static i2c_errno_t
dwi2c_prop_get(void *arg, i2c_prop_t prop, void *buf, size_t buflen)
{
	dwi2c_t *dwi = arg;
	i2c_errno_t ret = I2C_CORE_E_OK;
	uint32_t val = 0;

	mutex_enter(&dwi->dwi_mutex);
	switch (prop) {
	case I2C_PROP_BUS_SPEED:
		val = dwi->dwi_speed_cur;
		break;
	case I2C_PROP_MAX_READ:
	case I2C_PROP_MAX_WRITE:
		val = I2C_REQ_MAX;
		break;
	case I2C_PROP_STD_SCL_HIGH:
		val = dwi->dwi_ss_hcnt;
		break;
	case I2C_PROP_STD_SCL_LOW:
		val = dwi->dwi_ss_lcnt;
		break;
	case I2C_PROP_FAST_SCL_HIGH:
		val = dwi->dwi_fs_hcnt;
		break;
	case I2C_PROP_FAST_SCL_LOW:
		val = dwi->dwi_fs_lcnt;
		break;
	case I2C_PROP_HIGH_SCL_HIGH:
		val = dwi->dwi_hs_hcnt;
		break;
	case I2C_PROP_HIGH_SCL_LOW:
		val = dwi->dwi_hs_hcnt;
		break;
	default:
		ret = I2C_PROP_E_UNSUP;
	}
	mutex_exit(&dwi->dwi_mutex);

	if (ret == I2C_CORE_E_OK) {
		VERIFY3U(buflen, >=, sizeof (val));
		bcopy(&val, buf, sizeof (val));
	}

	return (ret);
}

static i2c_errno_t
dwi2c_prop_set(void *arg, i2c_prop_t prop, const void *buf, size_t buflen)
{
	dwi2c_t *dwi = arg;
	i2c_errno_t ret = I2C_CORE_E_OK;
	uint32_t val;

	mutex_enter(&dwi->dwi_mutex);

	switch (prop) {
	case I2C_PROP_BUS_SPEED:
		bcopy(buf, &val, sizeof (val));
		if ((val & dwi->dwi_speed_pos) == 0) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		if (stdc_count_ones_ui(val) != 1) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_speed_cur = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	case I2C_PROP_MAX_READ:
	case I2C_PROP_MAX_WRITE:
		ret = I2C_PROP_E_READ_ONLY;
		break;
	case I2C_PROP_STD_SCL_HIGH:
		bcopy(buf, &val, sizeof (val));

		if (val < DW_IC_SCL_HCNT_MIN || val > DW_IC_SCL_HCNT_MAX) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_ss_hcnt = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	case I2C_PROP_STD_SCL_LOW:
		bcopy(buf, &val, sizeof (val));

		if (val < DW_IC_SCL_LCNT_MIN || val > DW_IC_SCL_LCNT_MAX) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_ss_lcnt = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	case I2C_PROP_FAST_SCL_HIGH:
		bcopy(buf, &val, sizeof (val));

		if (val < DW_IC_SCL_HCNT_MIN || val > DW_IC_SCL_HCNT_MAX) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_fs_hcnt = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	case I2C_PROP_FAST_SCL_LOW:
		bcopy(buf, &val, sizeof (val));

		if (val < DW_IC_SCL_LCNT_MIN || val > DW_IC_SCL_LCNT_MAX) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_fs_lcnt = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	case I2C_PROP_HIGH_SCL_HIGH:
		bcopy(buf, &val, sizeof (val));

		if (val < DW_IC_SCL_HCNT_MIN || val > DW_IC_SCL_HCNT_MAX) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_hs_hcnt = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	case I2C_PROP_HIGH_SCL_LOW:
		bcopy(buf, &val, sizeof (val));

		if (val < DW_IC_SCL_LCNT_MIN || val > DW_IC_SCL_LCNT_MAX) {
			ret = I2C_PROP_E_BAD_VAL;
			break;
		}

		dwi->dwi_hs_lcnt = val;
		dwi->dwi_flags |= DWI2C_F_NEED_TIME_UPDATE;
		break;
	default:
		ret = I2C_PROP_E_UNSUP;
		break;
	}

	mutex_exit(&dwi->dwi_mutex);

	return (ret);
}

/*
 * Determine whether or not the bus is available.
 */
static bool
dwi2c_bus_avail(dwi2c_t *dwi)
{
	const uint32_t count = i2c_ctrl_timeout_count(dwi->dwi_hdl,
	    I2C_CTRL_TO_BUS_ACT);
	const uint32_t wait = i2c_ctrl_timeout_delay_us(dwi->dwi_hdl,
	    I2C_CTRL_TO_BUS_ACT);

	for (uint32_t i = 0; i < count; i++) {
		uint32_t r = dwi2c_read32(dwi, DW_I2C_STS);
		if ((r & DW_I2C_STS_ACTIVITY) == 0) {
			return (true);
		}

		delay(drv_usectohz(wait));
	}

	dev_err(dwi->dwi_dip, CE_WARN, "controller timed out waiting for "
	    "bus activity to cease");
	return (false);
}

static void
dwi2c_io_set_addr(dwi2c_t *dwi, const i2c_addr_t *addr)
{
	uint32_t tar, con;
	bool ten_bit;

	ten_bit = addr->ia_type == I2C_ADDR_10BIT;
	con = dwi2c_read32(dwi, DW_I2C_CON);
	con = DW_I2C_CON_SET_10BIT_TGT(con, ten_bit);
	dwi2c_write32(dwi, DW_I2C_CON, con);

	tar = DW_I2C_TAR_SET_ADDR(0, addr->ia_addr);
	tar = DW_I2C_TAR_SET_10BIT_CTRL(tar, ten_bit);
	dwi2c_write32(dwi, DW_I2C_TAR, tar);
}

/*
 * This is called to determine the current set of interrupt bits that have been
 * set. Interrupt bits can be set in two different registers: DW_I2C_INTR_STS
 * and DW_I2C_INTR_RAW. The primary difference here is that the raw register has
 * anything that has occurred, while the status register takes into account the
 * mask.
 *
 * There are a few different ways interrupts can be cleared. There is a general
 * interrupt clear register and there is a source-specific register that can be
 * read. The general interrupt clear register is not synchronized with the
 * interrupt status register. That is, if we read the interrupt status register
 * and then read the clear register, anything that occurred between those two
 * events will be lost.
 *
 * When we read the status register, we proceed to clear all interrupts that are
 * software clearable. A few, such as DW_I2C_INTR_RX_FULL or
 * DW_I2C_INTR_TX_EMPTY, are not. We end up clearing several things beyond what
 * we actually enable in interrupts as a bit of future proofing and general
 * cleanliness. One gotcha for us is that clearing the abort interrupt also has
 * a side effect of clearing the abort source. So we'll end up special casing
 * that to pull that out.
 */
static dw_i2c_intr_t
dwi2c_get_intr(dwi2c_t *dwi)
{
	uint32_t val;
	dw_i2c_intr_t sts;

	struct reg_map {
		dw_i2c_intr_t rm_intr;
		uint32_t rm_reg;
	} map[] = {
		{ DW_I2C_INTR_RX_UNDERRUN, DW_I2C_CLEAR_RX_UNDERRUN },
		{ DW_I2C_INTR_RX_OVERRUN, DW_I2C_CLEAR_RX_OVERRUN },
		{ DW_I2C_INTR_TX_OVERRUN, DW_I2C_CLEAR_TX_OVERRUN },
		{ DW_I2C_INTR_READ_REQ, DW_I2C_CLEAR_READ_REQ },
		{ DW_I2C_INTR_TX_ABORT, DW_I2C_CLEAR_TX_ABORT },
		{ DW_I2C_INTR_ACTIVITY, DW_I2C_CLEAR_ACTIVITY },
		{ DW_I2C_INTR_STOP_DET, DW_I2C_CLEAR_STOP_DET },
		{ DW_I2C_INTR_START_DET, DW_I2C_CLEAR_START_DET },
		{ DW_I2C_INTR_GEN_CALL, DW_I2C_CLEAR_GEN_CALL }
	};

	ASSERT(MUTEX_HELD(&dwi->dwi_mutex));

	val = dwi2c_read32(dwi, DW_I2C_INTR_STS);
	sts = DW_I2C_INTR_GET_INTR(val);
	if ((sts & DW_I2C_INTR_TX_ABORT) != 0) {
		dwi->dwi_last_abort = dwi2c_read32(dwi, DW_I2C_TX_ABORT);
	}

	for (size_t i = 0; i < ARRAY_SIZE(map); i++) {
		if ((sts & map[i].rm_intr) != 0) {
			(void) dwi2c_read32(dwi, map[i].rm_reg);
		}
	}

	dwi->dwi_last_intr = sts;
	return (sts);
}

/*
 * Loop through our current message and write any bytes that need to into the
 * transmit FIFO. See the theory statement for more information on how this
 * work. Both read requests and write requests go into here. A few notes on
 * things we have to do:
 *
 *  - We never end up setting that a restart is needed while processing this. We
 *    only have a single upstream message at a time right now in the API and
 *    therefore we will always end up doing a full start/stop and don't need a
 *    restart when transmitting.
 *  - The hardware has a configurable parameter that is invisible to software.
 *    This is called 'IC_EMPTYFIFO_HOLD_MASTER_EN'. When this parameter is true,
 *    we are responsible for indicating the stop bit manually. Because this is
 *    unknowable, we must always do it. This only applies here if there is
 *    nothing to read after we are done writing.
 *  - We will write as much data as we can into the transmit FIFO. If there is
 *    more data to write than fits in one go, then we'll wait until we are given
 *    another interrupt.
 *  - Once we have written everything, we are responsible for indicating the
 *    next state transition that we are waiting for.
 *  - When inserting read requests we need to both consider the tx and rx queue
 *    depths. See the receive I/O State Machine in theory statement for more
 *    information.
 */
static void
dwi2c_tx(dwi2c_t *dwi)
{
	uint32_t tx_depth, tx_limit, rem, towrite;
	i2c_req_t *req = dwi->dwi_req;

	ASSERT(MUTEX_HELD(&dwi->dwi_mutex));

	tx_depth = dwi2c_read32(dwi, DW_I2C_TX_DEPTH);
	tx_depth = DW_I2C_DEPTH_GET_DEPTH(tx_depth);
	tx_limit = dwi->dwi_fifo_tx_max - tx_depth;

	/*
	 * Determine how much data we have to transmit. If there was never any
	 * data to transmit, then immediately advance to the receive state.
	 * However, if there is data to transmit, we will instead advance as
	 * part of determining what we need to do with the STOP.
	 */
	if (dwi->dwi_req_state == DWI2C_IO_TX) {
		if (req->ir_wlen == 0) {
			ASSERT0(dwi->dwi_req_tx_off);
			dwi->dwi_req_state = DWI2C_IO_RX;
		}
		rem = req->ir_wlen - dwi->dwi_req_tx_off;
		towrite = MIN(rem, tx_limit);
	} else {
		rem = 0;
		towrite = 0;
	}

	for (uint32_t i = 0; i < towrite; i++, rem--) {
		uint32_t data;

		data = DW_I2C_DATA_SET_CMD(0, DW_I2C_DATA_CMD_WRITE);
		data = DW_I2C_DATA_SET_DATA(data,
		    req->ir_wdata[dwi->dwi_req_tx_off]);
		dwi->dwi_req_tx_off++;


		/*
		 * This is the last byte to transmit. We must figure out if we
		 * need to set the stop bit and whether we continue on to
		 * writing or not.
		 */
		ASSERT3U(rem, !=, 0);
		if (rem == 1) {
			ASSERT3U(i + 1, ==, towrite);
			if (req->ir_rlen == 0) {
				dwi->dwi_req_state = DWI2C_IO_STOP;
				data = DW_I2C_DATA_SET_STOP(data, 1);
			} else {
				dwi->dwi_req_state = DWI2C_IO_RX;
				data = DW_I2C_DATA_SET_STOP(data, 0);
				ASSERT0(dwi->dwi_req_rx_off);
				ASSERT0(dwi->dwi_req_rx_req);
				ASSERT0(dwi->dwi_req_rx_nout);
			}
		}

		dwi2c_write32(dwi, DW_I2C_DATA, data);
	}

	/*
	 * Update the tx depth and limit now that we have written all of our
	 * data. If we are potentially inserting read commands then this changes
	 * things.
	 */
	tx_depth += towrite;
	tx_limit -= towrite;

	/*
	 * If we still need to transmit more data, then there's no reason to
	 * check if we should put read requests into the FIFO.
	 */
	if (dwi->dwi_req_state != DWI2C_IO_RX) {
		return;
	}

	/*
	 * Put in a number of read requests into the transmit FIFO. This is
	 * limited by:
	 *
	 *  - The transmit FIFO's remaining depth
	 *  - The receive FIFO's depth
	 *  - The amount of receive data outstanding (to avoid RX FIFO overrun)
	 *  - The actual number of bytes the request wishes to receive that we
	 *    haven't set requests for (which is different from the amount we've
	 *    actually received to date)
	 */
	uint32_t rx_depth, rx_limit, toread;
	rx_depth = dwi2c_read32(dwi, DW_I2C_RX_THRESH);
	rx_depth = DW_I2C_THRESH_GET_THRESH(rx_depth);
	rx_limit = dwi->dwi_fifo_tx_max - MAX(rx_depth, dwi->dwi_req_rx_nout);
	rem = req->ir_rlen - dwi->dwi_req_rx_req;
	toread = MIN(rem, rx_limit);

	for (uint32_t i = 0; i < toread; i++, rem--) {
		uint32_t data;

		data = DW_I2C_DATA_SET_CMD(0, DW_I2C_DATA_CMD_READ);

		/*
		 * If this is the last bit that we are going to receive, then we
		 * need to set the stop bit as well.
		 */
		ASSERT3U(rem, !=, 0);
		if (rem == 1) {
			ASSERT3U(i + 1, ==, toread);
			data = DW_I2C_DATA_SET_STOP(data, 1);
		}

		dwi->dwi_req_rx_nout++;
		dwi->dwi_req_rx_req++;
		dwi2c_write32(dwi, DW_I2C_DATA, data);
	}

	/*
	 * If we have sent all the data that we care about, then we should turn
	 * off this class of interrupt. It will be turned on again when we get
	 * to the next request.
	 */
	if (req->ir_rlen == dwi->dwi_req_rx_req) {
		dwi->dwi_mask &= ~DW_I2C_INTR_TX_EMPTY;
		dwi2c_write32(dwi, DW_I2C_INTR_MASK, dwi->dwi_mask);
	}
}

/*
 * We've been told that there's data for us to read from the receive FIFO. Go
 * through and read that out. Requests for read data were made in dwi2c_tx()
 * above.
 */
static void
dwi2c_rx(dwi2c_t *dwi)
{
	uint32_t to_read;
	i2c_req_t *req = dwi->dwi_req;

	ASSERT(MUTEX_HELD(&dwi->dwi_mutex));

	to_read = dwi2c_read32(dwi, DW_I2C_RX_DEPTH);
	to_read = DW_I2C_DEPTH_GET_DEPTH(to_read);

	for (uint32_t i = 0; i < to_read; i++) {
		uint32_t data = dwi2c_read32(dwi, DW_I2C_DATA);

		ASSERT3U(dwi->dwi_req_rx_nout, >, 0);
		ASSERT3U(dwi->dwi_req_rx_req, >, 0);
		ASSERT3U(dwi->dwi_req_rx_off, <, req->ir_rlen);

		req->ir_rdata[dwi->dwi_req_rx_off] = DW_I2C_DATA_GET_DATA(data);
		dwi->dwi_req_rx_off++;
		dwi->dwi_req_rx_nout--;
	}

	/*
	 * If we've received all of the data we intend, then proceed to waiting
	 * for a STOP to be seen. We don't remove the RX FIFO full interrupt
	 * here because nothing should generate it, unlike the TX FIFO empty.
	 */
	if (req->ir_rlen == dwi->dwi_req_rx_off) {
		ASSERT0(dwi->dwi_req_rx_nout);
		ASSERT3U(dwi->dwi_req_rx_off, ==, dwi->dwi_req_rx_req);
		dwi->dwi_req_state = DWI2C_IO_STOP;
	}
}

/*
 * Our goal is to translate the saved abort source into a useful error for
 * userland to process. There are multiple possible bits that can be set. The
 * type of error and its significance varies. For example, a case where there is
 * no ack is quite reasonable, especially during a device scan. Conversely,
 * losing arbitration is much more notable.
 *
 * We divide the errors here into different classes depending on whether or not
 * they can be generated while acting as the primary controller vs. target,
 * whether they require features we don't leverage, etc. In general, errors that
 * relate to the target or the currently unused user abort will generate a
 * panic. If none of the errors match, then we'll generate an internal error and
 * log that. If there is more than one error, then we use the first one we find.
 */
typedef struct {
	dw_i2c_abort_t am_abort;
	i2c_ctrl_error_t am_error;
	bool am_log;
	bool am_panic;
} dwi2c_abort_map_t;

static const dwi2c_abort_map_t dwi2c_aborts[] = {
	{
		.am_abort = DW_I2C_ABORT_7B_ADDR_NOACK,
		.am_error = I2C_CTRL_E_ADDR_NACK,
	}, {
		.am_abort = DW_I2C_ABORT_10B_ADDR1_NOACK,
		.am_error = I2C_CTRL_E_ADDR_NACK,
	}, {
		.am_abort = DW_I2C_ABORT_10B_ADDR2_NOACK,
		.am_error = I2C_CTRL_E_ADDR_NACK,
	}, {
		.am_abort = DW_I2C_ABORT_TX_NOACK,
		.am_error = I2C_CTRL_E_DATA_NACK,
	}, {
		.am_abort = DW_I2C_ABORT_GEN_CALL_NOACK,
		.am_error = I2C_CTRL_E_ADDR_NACK,
	}, {
		.am_abort = DW_I2C_ABORT_GEN_CALL_READ,
		.am_error = I2C_CTRL_E_DRIVER,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_HIGH_CODE_ACK,
		.am_error = I2C_CTRL_E_BAD_ACK,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_START_ACK,
		.am_error = I2C_CTRL_E_BAD_ACK,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_HS_NORESTART,
		.am_error = I2C_CTRL_E_DRIVER,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_START_RESTART,
		.am_error = I2C_CTRL_E_DRIVER,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_10B_RESTART_DIS,
		.am_error = I2C_CTRL_E_DRIVER,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_CTRL_DIS,
		.am_error = I2C_CTRL_E_DRIVER,
		.am_log = true
	}, {
		.am_abort = DW_I2C_ABORT_ARB_LOST,
		.am_error = I2C_CTRL_E_ARB_LOST,
	}, {
		.am_abort = DW_I2C_ABORT_TGT_FLUSH_TX,
		.am_panic = true
	}, {
		.am_abort = DW_I2C_ABORT_TGT_ARB,
		.am_panic = true
	}, {
		.am_abort = DW_I2C_ABORT_TGT_READ,
		.am_panic = true
	}, {
		.am_abort = DW_I2C_ABORT_USER,
		.am_error = I2C_CTRL_E_REQ_TO
	}
};

static void
dwi2c_abort_to_error(dwi2c_t *dwi)
{
	uint32_t status = DW_I2C_TX_ABORT_GET_STS(dwi->dwi_last_abort);
	ASSERT3U(status, !=, 0);

	for (size_t i = 0; i < ARRAY_SIZE(dwi2c_aborts); i++) {
		if ((status & dwi2c_aborts[i].am_abort) == 0)
			continue;

		if (dwi2c_aborts[i].am_panic) {
			panic("unexpected dwi2c programmer error: abort 0x%x",
			    status);
		}

		if (dwi2c_aborts[i].am_log) {
			dev_err(dwi->dwi_dip, CE_WARN, "!aborting i2c "
			    "transaction with code 0x%x", status);
		}

		i2c_ctrl_io_error(&dwi->dwi_req->ir_error,
		    I2C_CORE_E_CONTROLLER, dwi2c_aborts[i].am_error);
		return;
	}

	/*
	 * This is an error that we don't know how to map. Log about this and
	 * return this as a generic internal/unknown error.
	 */
	dev_err(dwi->dwi_dip, CE_WARN, "!aborting i2c transaction with "
	    "unmapped abort source 0x%x", status);
	i2c_ctrl_io_error(&dwi->dwi_req->ir_error, I2C_CORE_E_CONTROLLER,
	    I2C_CTRL_E_INTERNAL);
}

static void
dwi2c_io(dwi2c_t *dwi, dw_i2c_intr_t intr)
{
	/*
	 * If we have encountered an abort, mark that this I/O is done. All of
	 * the FIFOs will have been flushed and we have the abort source. We
	 * cannot do anything else here.
	 */
	if ((intr & DW_I2C_INTR_TX_ABORT) != 0) {
		dwi->dwi_req_state = DWI2C_IO_DONE;
		dwi->dwi_mask = 0;
		dwi2c_write32(dwi, DW_I2C_INTR_MASK, dwi->dwi_mask);
		dwi2c_abort_to_error(dwi);
		return;
	}

	if ((intr & DW_I2C_INTR_RX_FULL) != 0 &&
	    dwi->dwi_req_state == DWI2C_IO_RX) {
		dwi2c_rx(dwi);
	}

	if ((intr & DW_I2C_INTR_TX_EMPTY) != 0 &&
	    dwi->dwi_req_state <= DWI2C_IO_RX) {
		dwi2c_tx(dwi);
	}

	if (dwi->dwi_req_state == DWI2C_IO_STOP &&
	    (intr & DW_I2C_INTR_STOP_DET) != 0) {
		dwi->dwi_req_state = DWI2C_IO_DONE;
		dwi->dwi_req_tx_off = 0;
		dwi->dwi_req_rx_off = 0;
		dwi->dwi_req_rx_req = 0;
		dwi->dwi_req_rx_nout = 0;
		i2c_ctrl_io_success(&dwi->dwi_req->ir_error);
	}

	/*
	 * Some AMD implementations require a workaround to trigger pending
	 * interrupts. For now, we just do this with everything.
	 */
	dwi2c_write32(dwi, DW_I2C_INTR_MASK, 0);
	dwi2c_write32(dwi, DW_I2C_INTR_MASK, dwi->dwi_mask);
}

/*
 * We have hit our internal timeout waiting for a transaction to complete. Go
 * through and transition the request state, interrupt mask, and actually issue
 * the abort to the controller.
 */
static void
dwi2c_abort(dwi2c_t *dwi)
{
	uint32_t en;

	VERIFY(MUTEX_HELD(&dwi->dwi_mutex));
	VERIFY3P(dwi->dwi_req, !=, NULL);
	VERIFY3U(dwi->dwi_req_state, !=, DWI2C_IO_DONE);

	/*
	 * Now that we're aborting, we should only bother with waiting for an
	 * abort.
	 */
	dwi->dwi_req_state = DWI2C_IO_ABORT;
	dwi->dwi_mask = DW_I2C_INTR_TX_ABORT;
	dwi2c_write32(dwi, DW_I2C_INTR_MASK, dwi->dwi_mask);

	/*
	 * Actually issue the abort.
	 */
	en = dwi2c_read32(dwi, DW_I2C_EN);
	ASSERT3U(DW_I2C_EN_GET_EN(en), !=, 0);
	en = DW_I2C_EN_SET_ABORT(en, 1);
	dwi2c_write32(dwi, DW_I2C_EN, en);
}

static void
dwi2c_wait(dwi2c_t *dwi, bool poll)
{
	uint32_t to, spin;

	VERIFY(MUTEX_HELD(&dwi->dwi_mutex));
	VERIFY3P(dwi->dwi_req, !=, NULL);

	to = i2c_ctrl_timeout_delay_us(dwi->dwi_hdl, I2C_CTRL_TO_IO);
	spin = i2c_ctrl_timeout_delay_us(dwi->dwi_hdl, I2C_CTRL_TO_POLL_CTRL);

restart:
	if (!poll) {
		clock_t abs = ddi_get_lbolt() + drv_usectohz(to);
		while (dwi->dwi_req_state != DWI2C_IO_DONE) {
			clock_t ret = cv_timedwait(&dwi->dwi_cv,
			    &dwi->dwi_mutex, abs);
			if (ret == -1)
				break;
		}
	} else {
		/*
		 * We're in charge of polling and advancing the state machine
		 * here.
		 */
		hrtime_t abs = gethrtime() + USEC2NSEC(to);
		while (dwi->dwi_req_state != DWI2C_IO_DONE &&
		    gethrtime() < abs) {
			drv_usecwait(spin);
			dw_i2c_intr_t intr = dwi2c_get_intr(dwi);
			dwi2c_io(dwi, intr);
		}
	}

	if (dwi->dwi_req_state != DWI2C_IO_DONE) {
		/*
		 * This is the case where we've failed to abort the abort.
		 * That's not good, but there's also not a whole lot that we can
		 * do at this point. There is no standardized device reset.
		 * Complain, fail the request, and hopefully some day we'll do
		 * better.
		 */
		if (dwi->dwi_req_state == DWI2C_IO_ABORT) {
			return;
		}

		/*
		 * Otherwise this is the first time we've hit our timeout.
		 * Update our timeout and wait for the abort to complete.
		 */
		to = i2c_ctrl_timeout_delay_us(dwi->dwi_hdl, I2C_CTRL_TO_ABORT);
		dwi2c_abort(dwi);
		goto restart;
	}
}

/*
 * We've been asked to perform an I/O request. The framework has guaranteed that
 * we only have one I/O request pending at any given time.
 *
 * To perform I/O we must do the following in order:
 *
 *  - Check for any pending I/O
 *  - Disable the controller so we can make updates to it
 *  - Set the target address
 *  - Ensure interrupts are clear and disabled. The interrupt disable is due to
 *    certain classes of hardware having issues here (according to other
 *    drivers)
 *  - Enable the controller
 *  - Perform a dummy read of the Enable Status register to work around issues
 *    in certain hardware (supposedly Bay Trail)
 *  - Clear and enable interrupts
 *  - Poll / wait for interrupt status bits to occur and use that to begin the
 *    transfer. We do not return from this function until this is completed.
 */
static void
dwi2c_io_i2c(void *arg, uint32_t port, i2c_req_t *req)
{
	dwi2c_t *dwi = arg;
	ASSERT3U(port, ==, 0);

	mutex_enter(&dwi->dwi_mutex);

	if (!dwi2c_bus_avail(dwi)) {
		mutex_exit(&dwi->dwi_mutex);
		i2c_ctrl_io_error(&req->ir_error, I2C_CORE_E_CONTROLLER,
		    I2C_CTRL_E_BUS_BUSY);
		return;
	}

	if (!dwi2c_ctrl_en_dis(dwi, false)) {
		mutex_exit(&dwi->dwi_mutex);
		i2c_ctrl_io_error(&req->ir_error, I2C_CORE_E_CONTROLLER,
		    I2C_CTRL_E_INTERNAL);
		return;
	}

	/*
	 * Set the address.
	 */
	dwi2c_io_set_addr(dwi, &req->ir_addr);

	/*
	 * Update any timings that are required.
	 */
	if ((dwi->dwi_flags & DWI2C_F_NEED_TIME_UPDATE) != 0) {
		dwi2c_timing_update(dwi);
		dwi->dwi_flags &= ~DWI2C_F_NEED_TIME_UPDATE;
	}

	/*
	 * Disable and clear interrupts.
	 */
	dwi2c_write32(dwi, DW_I2C_INTR_MASK, 0);
	(void) dwi2c_read32(dwi, DW_I2C_CLEAR_INTR);

	if (!dwi2c_ctrl_en_dis(dwi, true)) {
		mutex_exit(&dwi->dwi_mutex);
		i2c_ctrl_io_error(&req->ir_error, I2C_CORE_E_CONTROLLER,
		    I2C_CTRL_E_INTERNAL);
		return;
	}

	/*
	 * Set this request as the one that we care about before any interrupts
	 * can be generated. Ensure all of our state tracking is back at the
	 * default.
	 */
	dwi->dwi_req = req;
	dwi->dwi_req_state = DWI2C_IO_TX;
	dwi->dwi_req_tx_off = 0;
	dwi->dwi_req_rx_off = 0;
	dwi->dwi_req_rx_req = 0;
	dwi->dwi_req_rx_nout = 0;

	/*
	 * Enable the interrupts we care about.
	 */
	(void) dwi2c_read32(dwi, DW_I2C_CLEAR_INTR);
	dwi->dwi_mask = dwi2c_intr_mask;
	dwi2c_write32(dwi, DW_I2C_INTR_MASK, dwi->dwi_mask);

	/*
	 * Right now we don't have interrupt support in the driver. When we do,
	 * we should check the poll flags.
	 */
	dwi2c_wait(dwi, true);

	/*
	 * Disable the controller again. We don't really care too much if this
	 * fails at this time.
	 */
	(void) dwi2c_ctrl_en_dis(dwi, false);

	dwi->dwi_req = NULL;
	mutex_exit(&dwi->dwi_mutex);
}

static const i2c_ctrl_ops_t dwi_ctrl_ops = {
	.i2c_port_name_f = i2c_ctrl_port_name_portno,
	.i2c_io_i2c_f = dwi2c_io_i2c,
	.i2c_prop_info_f = dwi2c_prop_info,
	.i2c_prop_get_f = dwi2c_prop_get,
	.i2c_prop_set_f = dwi2c_prop_set
};

static bool
dwi2c_register(dwi2c_t *dwi)
{
	i2c_ctrl_reg_error_t ret;
	i2c_ctrl_register_t *reg;

	ret = i2c_ctrl_register_alloc(I2C_CTRL_PROVIDER, &reg);
	if (ret != 0) {
		dev_err(dwi->dwi_dip, CE_WARN, "failed to allocate i2c "
		    "controller registration structure: 0x%x", ret);
		return (false);
	}

	reg->ic_type = I2C_CTRL_TYPE_I2C;
	reg->ic_nports = 1;
	reg->ic_dip = dwi->dwi_dip;
	reg->ic_drv = dwi;
	reg->ic_ops = &dwi_ctrl_ops;

	ret = i2c_ctrl_register(reg, &dwi->dwi_hdl);
	i2c_ctrl_register_free(reg);
	if (ret != 0) {
		dev_err(dwi->dwi_dip, CE_WARN, "failed to register with i2c "
		    "framework: 0x%x", ret);
		return (false);
	}

	return (true);
}

static void
dwi2c_cleanup(dwi2c_t *dwi)
{
	VERIFY3P(dwi->dwi_req, ==, NULL);
	VERIFY3P(dwi->dwi_hdl, ==, NULL);

	x_ddi_reg_block_free(&dwi->dwi_rb);

	cv_destroy(&dwi->dwi_cv);
	mutex_destroy(&dwi->dwi_mutex);
	ddi_set_driver_private(dwi->dwi_dip, NULL);
	dwi->dwi_dip = NULL;
	kmem_free(dwi, sizeof (dwi2c_t));
}

static int
dwi2c_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	dwi2c_t *dwi;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	dwi = kmem_zalloc(sizeof (dwi2c_t), KM_SLEEP);
	dwi->dwi_dip = dip;
	ddi_set_driver_private(dip, dwi);

	mutex_init(&dwi->dwi_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&dwi->dwi_cv, NULL, CV_DRIVER, NULL);

	if (!dwi2c_regs_setup(dwi))
		goto err;

	if (!dwi2c_ctrl_init(dwi))
		goto err;

	if (!dwi2c_register(dwi))
		goto err;

	return (DDI_SUCCESS);

err:
	dwi2c_cleanup(dwi);
	return (DDI_FAILURE);
}

static int
dwi2c_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	i2c_ctrl_reg_error_t ret;
	dwi2c_t *dwi;

	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	dwi = ddi_get_driver_private(dip);
	if (dwi == NULL) {
		dev_err(dip, CE_WARN, "asked to detach instance with no state");
		return (DDI_FAILURE);
	}

	VERIFY3P(dip, ==, dwi->dwi_dip);

	ret = i2c_ctrl_unregister(dwi->dwi_hdl);
	if (ret != 0) {
		dev_err(dip, CE_WARN, "failed to unregister from i2c "
		    "framework: 0x%x", ret);
		return (DDI_FAILURE);
	}
	dwi->dwi_hdl = NULL;

	dwi2c_cleanup(dwi);
	return (DDI_SUCCESS);
}

static struct dev_ops dwi2c_dev_ops = {
	.devo_rev = DEVO_REV,
	.devo_refcnt = 0,
	.devo_identify = nulldev,
	.devo_probe = nulldev,
	.devo_attach = dwi2c_attach,
	.devo_detach = dwi2c_detach,
	.devo_reset = nodev,
	.devo_quiesce = ddi_quiesce_not_supported,
};

static struct modldrv dwi2c_modldrv = {
	.drv_modops = &mod_driverops,
	.drv_linkinfo = "Designware I2C Controller",
	.drv_dev_ops = &dwi2c_dev_ops
};

static struct modlinkage dwi2c_modlinkage = {
	.ml_rev = MODREV_1,
	.ml_linkage = { &dwi2c_modldrv, NULL }
};

int
_init(void)
{
	int ret;

	i2c_ctrl_mod_init(&dwi2c_dev_ops);
	if ((ret = mod_install(&dwi2c_modlinkage)) != 0) {
		i2c_ctrl_mod_fini(&dwi2c_dev_ops);
	}

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&dwi2c_modlinkage, modinfop));
}

int
_fini(void)
{
	int ret;

	if ((ret = mod_remove(&dwi2c_modlinkage)) == 0) {
		i2c_ctrl_mod_fini(&dwi2c_dev_ops);
	}

	return (ret);
}
