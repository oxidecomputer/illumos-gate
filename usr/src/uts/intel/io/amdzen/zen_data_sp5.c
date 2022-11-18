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
 * Copyright 2022 Oxide Computer Company
 */

/*
 * This contains SP5 specific data related to the processor's pins, GPIOs, and
 * I/O Mux.
 */

#include <sys/sysmacros.h>
#include "amdzen_data.h"

#define	SP5_MUX_ENTRY(m0, m1, m2, m3) \
	.zg_iomux = { SP5_SIGNAL_ ## m0, SP5_SIGNAL_ ## m1, \
	SP5_SIGNAL_ ## m2, SP5_SIGNAL_ ## m3 }

/*
 * This currently covers Genoa / Bergamo CPUs.
 */
const zen_gpio_pindata_t zen_gpio_sp5_data[] = { {
	.zg_name = "AGPIO0",
	.zg_signal = "BP_PWR_BTN_L",
	.zg_pin = "DH7",
	.zg_id = 0,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(PWR_BTN_L, AGPIO0, AGPIO0, AGPIO0)
}, {
	.zg_name = "AGPIO1",
	.zg_signal = "BP_SYS_RESET_L",
	.zg_pin = "DH6",
	.zg_id = 1,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(SYS_RESET_L, AGPIO1, AGPIO1, AGPIO1)
}, {
	.zg_name = "AGPIO2",
	.zg_signal = "BP_WAKE_L",
	.zg_pin = "DJ10",
	.zg_id = 2,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(WAKE_L, AGPIO2, AGPIO2, AGPIO2)
}, {
	/*
	 * This GPIO is also SPD_HOST_CTRL_L. All of the mux entries are the
	 * same and suggest that sometime platform firmware can take control of
	 * this. However, this mostly appears to be used by the PSP at which
	 * point AGESA ultimately opts to use it and its subsequent usability is
	 * determined based upon the hardware design. However, at all times this
	 * is still a GPIO.
	 */
	.zg_name = "AGPIO3",
	.zg_signal = "BP_AGPIO3",
	.zg_pin = "DE36",
	.zg_id = 3,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO3, AGPIO3, AGPIO3, AGPIO3)
}, {
	.zg_name = "AGPIO4",
	.zg_signal = "BP_AGPIO4",
	.zg_pin = "DF36",
	.zg_id = 4,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO4, SATA_ACT_L, AGPIO4, AGPIO4)
}, {
	.zg_name = "AGPIO5",
	.zg_signal = "BP_AGPIO5",
	.zg_pin = "DF40",
	.zg_id = 5,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO5, DEVSLP0, AGPIO5, AGPIO5)
}, {
	.zg_name = "AGPIO6",
	.zg_signal = "BP_AGPIO6",
	.zg_pin = "DE40",
	.zg_id = 6,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO6, DEVSLP1, AGPIO6, AGPIO6)
}, {
	.zg_name = "AGPIO7",
	.zg_signal = "BP_AGPIO7",
	.zg_pin = "DH4",
	.zg_id = 7,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO7, AGPIO7, AGPIO7, AGPIO7)
}, {
	.zg_name = "AGPIO12",
	.zg_signal = "BP_PWRGD_OUT",
	.zg_pin = "DH36",
	.zg_id = 12,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(PWRGD_OUT, AGPIO12, AGPIO12, AGPIO12)
}, {
	/*
	 * The second entry here is a clock output that it is hard to find docs
	 * for and isn't listed in the FDS or mobo guide, but is in the ppr. For
	 * now we assume it is invalid.
	 */
	.zg_name = "AGPIO13",
	.zg_signal = "BP_I2C4_SCL_HP",
	.zg_pin = "DG12",
	.zg_id = 13,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I2C4_SCL, INVALID, AGPIO13, AGPIO13)
}, {
	/*
	 * Entry two here is the return of our favorite, unknown function the
	 * S0A3 block. It is unclear how we actually leverage and utilize this.
	 * Thus we mark this entry as invalid for the time being until
	 * additional clarity in terms of what it actually maps to and if it
	 * actually exists becomes clear.
	 */
	.zg_name = "AGPIO14",
	.zg_signal = "BP_I2C4_SDA_HP",
	.zg_pin = "DH12",
	.zg_id = 14,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I2C4_SDA, INVALID, AGPIO14, AGPIO14)
}, {
	.zg_name = "AGPIO16",
	.zg_signal = "BP_USB10_OC_L",
	.zg_pin = "DG40",
	.zg_id = 16,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(USB10_OC_L, AGPIO16, AGPIO16, AGPIO16)
}, {
	.zg_name = "AGPIO17",
	.zg_signal = "BP_USB11_OC_L",
	.zg_pin = "DH40",
	.zg_id = 17,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(USB11_OC_L, AGPIO17, AGPIO17, AGPIO17)
}, {
	.zg_name = "AGPIO19",
	.zg_signal = "BP_I2C5_BMC_SCL",
	.zg_pin = "DJ21",
	.zg_id = 19,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I2C5_SCL, SMBUS1_SCL, AGPIO19, AGPIO19)
}, {
	.zg_name = "AGPIO20",
	.zg_signal = "BP_I2C5_BMC_SDA",
	.zg_pin = "DJ20",
	.zg_id = 20,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I2C5_SDA, SMBUS1_SDA, AGPIO20, AGPIO20)
}, {
	.zg_name = "AGPIO21",
	.zg_signal = "BP_AGPIO21",
	.zg_pin = "DF33",
	.zg_id = 21,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO21, AGPIO21, AGPIO21, SD0_CMD)
}, {
	.zg_name = "AGPIO22",
	.zg_signal = "BP_AGPIO22",
	.zg_pin = "DE32",
	.zg_id = 22,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO22, INVALID, AGPIO22, AGPIO22)
}, {
	.zg_name = "AGPIO23",
	.zg_signal = "BP_ESPI_RSTOUT_L",
	.zg_pin = "DE27",
	.zg_id = 23,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_RSTOUT_L, AGPIO23, INVALID, AGPIO23)
}, {
	.zg_name = "AGPIO24",
	.zg_signal = "BP_SMERR_L",
	.zg_pin = "DJ11",
	.zg_id = 24,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	SP5_MUX_ENTRY(SMERR_L, AGPIO24, AGPIO24, AGPIO24)
}, {
	.zg_name = "AGPIO26",
	.zg_signal = "BP_PCIE_RST1_L",
	.zg_pin = "DG36",
	.zg_id = 26,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(PCIE_RST1_L, AGPIO26, AGPIO26, AGPIO26)
}, {
	.zg_name = "AGPIO74",
	.zg_signal = "BP_ESPI_CLK2",
	.zg_pin = "DF27",
	.zg_id = 74,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_CLK2, AGPIO74, AGPIO74, AGPIO74)
}, {
	.zg_name = "AGPIO75",
	.zg_signal = "BP_ESPI_CLK1",
	.zg_pin = "DG23",
	.zg_id = 75,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_CLK1, AGPIO75, AGPIO75, INVALID)
}, {
	.zg_name = "AGPIO76",
	.zg_signal = "BP_AGPIO76",
	.zg_pin = "DJ25",
	.zg_id = 76,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO76, SPI_TPM_CS_L, AGPIO76, AGPIO76)
}, {
	.zg_name = "AGPIO87",
	.zg_signal = "BP_AGPIO87",
	.zg_pin = "DG11",
	.zg_id = 87,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO87, INVALID, AGPIO87, AGPIO87)
}, {
	.zg_name = "AGPIO88",
	.zg_signal = "BP_AGPIO88",
	.zg_pin = "DJ29",
	.zg_id = 88,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO88, INVALID, AGPIO88, AGPIO88)
}, {
	.zg_name = "AGPIO89",
	.zg_signal = "BP_GENINT_L",
	.zg_pin = "DJ35",
	.zg_id = 89,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(GENINT_L, PM_INTR_L, AGPIO89, AGPIO89)
}, {
	.zg_name = "AGPIO104",
	.zg_signal = "BP_AGPIO104",
	.zg_pin = "DH30",
	.zg_id = 104,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO104, AGPIO104, INVALID, AGPIO104)
}, {
	.zg_name = "AGPIO105",
	.zg_signal = "BP_AGPIO105",
	.zg_pin = "DE30",
	.zg_id = 105,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO105, AGPIO105, INVALID, AGPIO105)
}, {
	.zg_name = "AGPIO106",
	.zg_signal = "BP_AGPIO106",
	.zg_pin = "DF31",
	.zg_id = 106,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO106, AGPIO106, INVALID, AGPIO106)
}, {
	.zg_name = "AGPIO107",
	.zg_signal = "BP_AGPIO107",
	.zg_pin = "DG31",
	.zg_id = 107,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO107, AGPIO107, INVALID, AGPIO107)
}, {
	/*
	 * The PPR claims that this and pin 110 are ESPI[01]_ALERT_L via an
	 * override; however, that it is otherwise ESPI[01]_ALERT_D1. The latter
	 * does not exist according to the FDS. For the moment, we honor the
	 * PPR's phrasing of this, but we do not note an override.
	 */
	.zg_name = "AGPIO108",
	.zg_signal = "BP_ESPI0_ALERT_L",
	.zg_pin = "DJ22",
	.zg_id = 108,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI0_ALERT_D1, AGPIO108, INVALID, AGPIO108)
}, {
	.zg_name = "AGPIO109",
	.zg_signal = "BP_AGPIO109",
	.zg_pin = "DG32",
	.zg_id = 109,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO109, AGPIO109, SD0_CLK, AGPIO109)
}, {
	/*
	 * See BP_ESPI0_ALERT_L for more information on this pin and its
	 * oddities.
	 */
	.zg_name = "AGPIO110",
	.zg_signal = "BP_ESPI1_ALERT_L",
	.zg_pin = "DF24",
	.zg_id = 110,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI1_ALERT_D1, AGPIO110, AGPIO110, AGPIO110)
}, {
	/*
	 * While the PPR suggests that the I/O Mux supports a CLKREQ11_L, the
	 * FDS explicitly says that this isn't supported and the motherboard
	 * guide confirms. This leads to what we call an invalid IOMUX entry.
	 */
	.zg_name = "AGPIO115",
	.zg_signal = "BP_AGPIO115",
	.zg_pin = "DH16",
	.zg_id = 115,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO115, INVALID, AGPIO115, AGPIO115)
}, {
	/*
	 * This pin has the same constraints as the one above with respect to
	 * CLK_REQ12_l not actually existing.
	 */
	.zg_name = "AGPIO116",
	.zg_signal = "BP_AGPIO116",
	.zg_pin = "DH15",
	.zg_id = 116,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO116, INVALID, AGPIO116, AGPIO116)
}, {
	.zg_name = "AGPIO117",
	.zg_signal = "BP_ESPI_CLK0",
	.zg_pin = "DJ27",
	.zg_id = 117,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_CLK0, AGPIO117, AGPIO117, AGPIO117)
}, {
	.zg_name = "AGPIO118",
	.zg_signal = "BP_SPI_CS0_L",
	.zg_pin = "DF30",
	.zg_id = 118,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(SPI_CS0_L, AGPIO118, AGPIO118, AGPIO118)
}, {
	.zg_name = "AGPIO119",
	.zg_signal = "BP_SPI_CS1_L",
	.zg_pin = "DG26",
	.zg_id = 119,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(SPI_CS1_L, AGPIO119, AGPIO119, AGPIO119)
}, {
	.zg_name = "AGPIO120",
	.zg_signal = "BP_ESPI0_DAT0",
	.zg_pin = "DF28",
	.zg_id = 120,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI0_DATA0, AGPIO120, AGPIO120, AGPIO120)
}, {
	.zg_name = "AGPIO121",
	.zg_signal = "BP_ESPI0_DAT1",
	.zg_pin = "DG22",
	.zg_id = 121,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI0_DATA1, AGPIO121, AGPIO121, AGPIO121)
}, {
	.zg_name = "AGPIO122",
	.zg_signal = "BP_ESPI0_DAT2",
	.zg_pin = "DJ28",
	.zg_id = 122,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI0_DATA2, AGPIO122, AGPIO122, AGPIO122)
}, {
	.zg_name = "AGPIO123",
	.zg_signal = "BP_ESPI0_DAT3",
	.zg_pin = "DG29",
	.zg_id = 123,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI0_DATA3, AGPIO123, AGPIO123, AGPIO123)
}, {
	.zg_name = "AGPIO124",
	.zg_signal = "BP_ESPI_CS0_L",
	.zg_pin = "DG28",
	.zg_id = 124,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_CS0_L, AGPIO124, AGPIO124, AGPIO124)
}, {
	.zg_name = "AGPIO125",
	.zg_signal = "BP_ESPI_CS1_L",
	.zg_pin = "DJ23",
	.zg_id = 125,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_CS1_L, AGPIO125, AGPIO125, AGPIO125)
}, {
	.zg_name = "AGPIO126",
	.zg_signal = "BP_SPI_CS2_L",
	.zg_pin = "DH27",
	.zg_id = 126,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(SPI_CS2_L, AGPIO126, AGPIO126, AGPIO126)
}, {
	.zg_name = "AGPIO129",
	.zg_signal = "BP_ESPI_RSTIN_L",
	.zg_pin = "DJ26",
	.zg_id = 129,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI_RSTIN_L, KBRST_L, AGPIO129, AGPIO129)
}, {
	.zg_name = "AGPIO131",
	.zg_signal = "BP_ESPI1_DAT0",
	.zg_pin = "DH24",
	.zg_id = 131,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI1_DATA0, AGPIO131, AGPIO131, AGPIO131)
}, {
	.zg_name = "AGPIO132",
	.zg_signal = "BP_ESPI1_DAT1",
	.zg_pin = "DE24",
	.zg_id = 132,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI1_DATA1, AGPIO132, AGPIO132, AGPIO132)
}, {
	.zg_name = "AGPIO133",
	.zg_signal = "BP_ESPI1_DAT2",
	.zg_pin = "DF25",
	.zg_id = 133,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI1_DATA2, AGPIO133, AGPIO133, AGPIO133)
}, {
	.zg_name = "AGPIO134",
	.zg_signal = "BP_ESPI1_DAT3",
	.zg_pin = "DG25",
	.zg_id = 134,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(ESPI1_DATA3, AGPIO134, AGPIO134, AGPIO134)
}, {
	.zg_name = "AGPIO135",
	.zg_signal = "BP_UART0_CTS_L",
	.zg_pin = "DJ33",
	.zg_id = 135,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART0_CTS_L, UART2_TXD, AGPIO135, AGPIO135)
}, {
	.zg_name = "AGPIO136",
	.zg_signal = "BP_UART0_RXD",
	.zg_pin = "DG35",
	.zg_id = 136,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART0_RXD_L, AGPIO136, AGPIO136, AGPIO136)
}, {
	.zg_name = "AGPIO137",
	.zg_signal = "BP_UART0_RTS_L",
	.zg_pin = "DF34",
	.zg_id = 137,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART0_RTS_L, UART2_RXD, AGPIO137, AGPIO137)
}, {
	.zg_name = "AGPIO138",
	.zg_signal = "BP_UART0_TXD",
	.zg_pin = "DJ34",
	.zg_id = 138,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART0_TXD, AGPIO138, AGPIO138, AGPIO138)
}, {
	.zg_name = "AGPIO139",
	.zg_signal = "BP_UART0_INTR",
	.zg_pin = "DG34",
	.zg_id = 139,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART0_INTR, AGPIO139, AGPIO139, AGPIO139)
}, {
	.zg_name = "AGPIO141",
	.zg_signal = "BP_UART1_RXD",
	.zg_pin = "DH33",
	.zg_id = 141,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART1_RXD, AGPIO141, AGPIO141, AGPIO141)
}, {
	.zg_name = "AGPIO142",
	.zg_signal = "BP_UART1_TXD",
	.zg_pin = "DJ32",
	.zg_id = 142,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(UART1_TXD, AGPIO142, AGPIO142, AGPIO142)
}, {
	.zg_name = "AGPIO145",
	.zg_signal = "BP_I3C0_SCL",
	.zg_pin = "A71",
	.zg_id = 145,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C0_SCL, I2C0_SCL, SMBUS0_SCL, AGPIO145)
}, {
	.zg_name = "AGPIO146",
	.zg_signal = "BP_I3C0_SDA",
	.zg_pin = "B71",
	.zg_id = 146,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C0_SDA, I2C0_SDA, SMBUS0_SDA, AGPIO146)
}, {
	.zg_name = "AGPIO147",
	.zg_signal = "BP_I3C1_SCL",
	.zg_pin = "C7",
	.zg_id = 147,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C1_SCL, I2C1_SCL, AGPIO147, AGPIO147)
}, {
	.zg_name = "AGPIO148",
	.zg_signal = "BP_I3C1_SDA",
	.zg_pin = "A5",
	.zg_id = 148,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C1_SDA, I2C1_SDA, AGPIO148, AGPIO148)
}, {
	.zg_name = "AGPIO149",
	.zg_signal = "BP_I3C2_SCL",
	.zg_pin = "C72",
	.zg_id = 149,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C2_SCL, I2C2_SCL, AGPIO149, AGPIO149)
}, {
	.zg_name = "AGPIO150",
	.zg_signal = "BP_I3C2_SDA",
	.zg_pin = "B72",
	.zg_id = 150,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C2_SDA, I2C2_SDA, AGPIO150, AGPIO150)
}, {
	.zg_name = "AGPIO151",
	.zg_signal = "BP_I3C3_SCL",
	.zg_pin = "A4",
	.zg_id = 151,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C3_SCL, I2C3_SCL, AGPIO151, AGPIO151)
}, {
	.zg_name = "AGPIO152",
	.zg_signal = "BP_I3C3_SDA",
	.zg_pin = "B4",
	.zg_id = 152,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(I3C3_SDA, I2C3_SDA, AGPIO152, AGPIO152)
}, {
	.zg_name = "AGPIO256",
	.zg_signal = "BP_AGPIO256",
	.zg_pin = "A14",
	.zg_id = 256,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO256, SGPIO0_CLK, AGPIO256, AGPIO256)
}, {
	/*
	 * The invalid pin mux entry is the unsupported CLK_REQ function.
	 */
	.zg_name = "AGPIO257",
	.zg_signal = "BP_AGPIO257",
	.zg_pin = "C14",
	.zg_id = 257,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO257, SGPIO1_CLK, INVALID, AGPIO257)
}, {
	/*
	 * The invalid pin mux entry is the unsupported CLK_REQ function.
	 */
	.zg_name = "AGPIO258",
	.zg_signal = "BP_AGPIO258",
	.zg_pin = "A13",
	.zg_id = 258,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO258, SGPIO2_CLK, INVALID, AGPIO258)
}, {
	.zg_name = "AGPIO259",
	.zg_signal = "BP_AGPIO259",
	.zg_pin = "B16",
	.zg_id = 259,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(AGPIO259, SGPIO3_CLK, AGPIO259, AGPIO259)
}, {
	.zg_name = "AGPIO260",
	.zg_signal = "BP_SGPIO_DATAOUT",
	.zg_pin = "D15",
	.zg_id = 260,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(SGPIO_DATAOUT, AGPIO260, AGPIO260, AGPIO260)
}, {
	.zg_name = "AGPIO261",
	.zg_signal = "BP_SGPIO_LOAD",
	.zg_pin = "B15",
	.zg_id = 261,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(SGPIO_LOAD, AGPIO261, AGPIO261, AGPIO261)
}, {
	.zg_name = "AGPIO264",
	.zg_signal = "BP_USB00_OC_L",
	.zg_pin = "E23",
	.zg_id = 264,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(USB00_OC_L, AGPIO264, AGPIO264, AGPIO264)
}, {
	.zg_name = "AGPIO265",
	.zg_signal = "BP_USB01_OC_L",
	.zg_pin = "E24",
	.zg_id = 265,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(USB01_OC_L, AGPIO265, AGPIO265, AGPIO265)
}, {
	.zg_name = "AGPIO266",
	.zg_signal = "BP_PCIE_RST0_L",
	.zg_pin = "D18",
	.zg_id = 266,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5,
	SP5_MUX_ENTRY(PCIE_RST0_L, AGPIO266, AGPIO266, AGPIO266)
} };

const size_t zen_gpio_sp5_nents = ARRAY_SIZE(zen_gpio_sp5_data);
