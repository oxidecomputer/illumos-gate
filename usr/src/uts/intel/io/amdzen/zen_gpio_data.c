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
 * Copyright 2022 Oxide Comptuer Company
 */

/*
 * This file contains the various pin table data files that we have for each
 * type of processor / socket.
 */

#include <sys/sysmacros.h>
#include "zen_gpio_impl.h"

/*
 * This currently covers Rome and Milan based SP3 CPUs.
 */
const zen_gpio_pindata_t zen_gpio_sp3_data[] = { {
	.zg_name = "AGPIO0",
	.zg_signal = "BP_PWR_BTN_L",
	.zg_pin = "A29",
	.zg_id = 0,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5,
}, {
	.zg_name = "AGPIO1",
	.zg_signal = "BP_SYS_RESET_L",
	.zg_id = 1,
	.zg_pin = "A35",
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO2",
	.zg_signal = "BP_WAKE_L",
	.zg_pin = "B36",
	.zg_id = 2,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO3",
	.zg_signal = "BP_AGPIO3",
	.zg_pin = "CY24",
	.zg_id = 3,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO4",
	.zg_signal = "BP_AGPIO4",
	.zg_pin = "E17",
	.zg_id = 4,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO5",
	.zg_signal = "BP_AGPIO5",
	.zg_pin = "DB24",
	.zg_id = 5,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO6",
	.zg_signal = "BP_AGPIO6",
	.zg_pin = "DA24",
	.zg_id = 6,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO9_0",
	.zg_signal = "BP_AGPIO9_0",
	.zg_pin = "DB29",
	.zg_id = 9,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO10_0",
	.zg_signal = "BP_S0A3_GPIO_0",
	.zg_pin = "DA28",
	.zg_id = 10,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO12",
	.zg_signal = "BP_PWRGD_OUT",
	.zg_pin = "B30",
	.zg_id = 12,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO16_0",
	.zg_signal = "BP_USB_OC0_L",
	.zg_pin = "CY15",
	.zg_id = 16,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO17_0",
	.zg_signal = "BP_USB_OC1_L",
	.zg_pin = "CY14",
	.zg_id = 17,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO19",
	.zg_signal = "BP_SCL1",
	.zg_pin = "CW15",
	.zg_id = 19,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5 | ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO20",
	.zg_signal = "BP_SDA1",
	.zg_pin = "CV15",
	.zg_id = 20,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5 | ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO21",
	.zg_signal = "BP_LPC_PD_L",
	.zg_pin = "CV27",
	.zg_id = 21,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO22",
	.zg_signal = "BP_LPC_PME_L",
	.zg_pin = "CV26",
	.zg_id = 22,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO23_0",
	.zg_signal = "BP_AGPIO23_0",
	.zg_pin = "CY27",
	.zg_id = 23,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO26",
	.zg_signal = "BP_PCIE_RST0_L",
	.zg_pin = "DB27",
	.zg_id = 26,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO26_3",
	.zg_signal = "BP_PCIE_RST3_L",
	.zg_pin = "DB21",
	.zg_id = 27,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO9_3",
	.zg_signal = "BP_AGPIO9_3",
	.zg_pin = "CY41",
	.zg_id = 29,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO23_3",
	.zg_signal = "BP_AGPIO23_3",
	.zg_pin = "CY42",
	.zg_id = 30,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO10_3",
	.zg_signal = "BP_S0A3_GPIO_3",
	.zg_pin = "DA43",
	.zg_id = 31,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO40_3",
	.zg_signal = "BP_AGPIO40_3",
	.zg_pin = "DB44",
	.zg_id = 32,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "AGPIO40_0",
	.zg_signal = "BP_AGPIO40_0",
	.zg_pin = "DA27",
	.zg_id = 40,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO42",
	.zg_signal = "BP_EGPIO42",
	.zg_pin = "CV21",
	.zg_id = 42,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_UNKNOWN
}, {
	.zg_name = "EGPIO70",
	.zg_signal = "BP_EGPIO70",
	.zg_pin = "CW25",
	.zg_id = 70,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_UNKNOWN
}, {
	.zg_name = "EGPIO74",
	.zg_signal = "BP_LPCCLK0",
	.zg_pin = "CV30",
	.zg_id = 74,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO75",
	.zg_signal = "BP_LPCCLK1",
	.zg_pin = "CV33",
	.zg_id = 75,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "AGPIO76",
	.zg_signal = "BP_AGPIO76",
	.zg_id = 76,
	.zg_pin = "CY30",
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "AGPIO86",
	.zg_signal = "BP_LPC_SMI_L",
	.zg_pin = "CW34",
	.zg_id = 86,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "AGPIO87",
	.zg_signal = "BP_SERIRQ",
	.zg_pin = "CW33",
	.zg_id = 87,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "AGPIO88",
	.zg_signal = "BP_LPC_CLKRUN_L",
	.zg_pin = "CW31",
	.zg_id = 88,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "AGPIO89",
	.zg_signal = "BP_GENINT1_L",
	.zg_pin = "CY44",
	.zg_id = 89,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO104",
	.zg_signal = "BP_LAD0",
	.zg_pin = "CV29",
	.zg_id = 104,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO105",
	.zg_signal = "BP_LAD1",
	.zg_pin = "CW30",
	.zg_id = 105,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO106",
	.zg_signal = "BP_LAD2",
	.zg_pin = "CV32",
	.zg_id = 106,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO107",
	.zg_signal = "BP_LAD3",
	.zg_pin = "CS28",
	.zg_id = 107,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO108",
	.zg_signal = "BP_ESPI_ALERT_L",
	.zg_pin = "CW24",
	.zg_id = 108,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_UNKNOWN
}, {
	.zg_name = "EGPIO109",
	.zg_signal = "BP_LFRAME_L",
	.zg_pin = "CW27",
	.zg_id = 109,
	.zg_pad = ZEN_GPIO_PAD_TYPE_SD,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO113",
	.zg_signal = "BP_SCL0",
	.zg_pin = "DA42",
	.zg_id = 113,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO114",
	.zg_signal = "BP_SDA0",
	.zg_pin = "DB42",
	.zg_id = 114,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO117",
	.zg_signal = "BP_SPI_CLK",
	.zg_pin = "DB30",
	.zg_id = 117,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "EGPIO118",
	.zg_signal = "BP_SPI_CS1_L",
	.zg_pin = "DB32",
	.zg_id = 118,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "EGPIO119",
	.zg_signal = "BP_SPI_CS2_L",
	.zg_pin = "DA30",
	.zg_id = 119,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "EGPIO120",
	.zg_signal = "BP_SPI_DI",
	.zg_pin = "DA30",
	.zg_id = 120,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "EGPIO121",
	.zg_signal = "BP_SPI_DO",
	.zg_pin = "CY32",
	.zg_id = 121,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "EGPIO122",
	.zg_signal = "BP_SPI_WP_L",
	.zg_pin = "DA31",
	.zg_id = 122,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "AGPIO129",
	.zg_signal = "BP_ESPI_RESET_L",
	.zg_pin = "CY29",
	.zg_id = 129,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO133",
	.zg_signal = "BP_SPI_HOLD_L",
	.zg_pin = "CY33",
	.zg_id = 133,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S0
}, {
	.zg_name = "EGPIO135",
	.zg_signal = "BP_UART0_CTS_L",
	.zg_pin = "CV41",
	.zg_id = 135,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO136",
	.zg_signal = "BP_UART0_RXD",
	.zg_pin = "CV39",
	.zg_id = 136,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO137",
	.zg_signal = "BP_UART0_RTS_L",
	.zg_pin = "CW40",
	.zg_id = 137,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO138",
	.zg_signal = "BP_UART0_TXD",
	.zg_pin = "CW39",
	.zg_id = 138,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "AGPIO139",
	.zg_signal = "BP_UART0_INTR",
	.zg_pin = "CV38",
	.zg_id = 139,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO140",
	.zg_signal = "BP_UART1_CTS_L",
	.zg_pin = "CY38",
	.zg_id = 140,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO141",
	.zg_signal = "BP_UART1_RXD",
	.zg_pin = "DB36",
	.zg_id = 141,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO142",
	.zg_signal = "BP_UART1_RTS_L",
	.zg_pin = "DB38",
	.zg_id = 142,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO143",
	.zg_signal = "BP_UART1_TXD",
	.zg_pin = "DA37",
	.zg_id = 143,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "AGPIO144",
	.zg_signal = "BP_UART1_INTR",
	.zg_pin = "DA36",
	.zg_id = 144,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO145",
	.zg_signal = "BP_I2C0_SCL",
	.zg_pin = "DA40",
	.zg_id = 145,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO146",
	.zg_signal = "BP_I2C0_SDA",
	.zg_pin = "DB41",
	.zg_id = 146,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO147",
	.zg_signal = "BP_I2C1_SCL",
	.zg_pin = "E22",
	.zg_id = 147,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO148",
	.zg_signal = "BP_I2C1_SDA",
	.zg_pin = "D22",
	.zg_id = 148,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO149",
	.zg_signal = "BP_I2C4_SCL",
	.zg_pin = "CW43",
	.zg_id = 149,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO150",
	.zg_signal = "BP_I2C4_SDA",
	.zg_pin = "CV44",
	.zg_id = 150,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO151",
	.zg_signal = "BP_I2C5_SCL",
	.zg_pin = "CV42",
	.zg_id = 151,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO152",
	.zg_signal = "BP_I2C5_SDA",
	.zg_pin = "CW42",
	.zg_id = 152,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S0 | ZEN_GPIO_V_3P3_S0
}, {
	.zg_name = "EGPIO9_2",
	.zg_signal = "BP_AGPIO9_2",
	.zg_pin = "D28",
	.zg_id = 256,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO23_2",
	.zg_signal = "BP_AGPIO23_2",
	.zg_pin = "E28",
	.zg_id = 257,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO10_2",
	.zg_signal = "BP_S0A3_GPIO_2",
	.zg_pin = "E26",
	.zg_id = 258,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO40_2",
	.zg_signal = "BP_AGPIO40_2",
	.zg_pin = "D27",
	.zg_id = 259,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO9_1",
	.zg_signal = "BP_AGPIO9_1",
	.zg_pin = "E25",
	.zg_id = 260,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO23_1",
	.zg_signal = "BP_AGPIO23_1",
	.zg_pin = "E23",
	.zg_id = 261,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO10_1",
	.zg_signal = "BP_S0A3_GPIO_1",
	.zg_pin = "E23",
	.zg_id = 262,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO40_1",
	.zg_signal = "BP_AGPIO40_1",
	.zg_pin = "D24",
	.zg_id = 263,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO16_1",
	.zg_signal = "BP_USB_OC2_L",
	.zg_pin = "C13",
	.zg_id = 264,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO17_1",
	.zg_signal = "BP_USB_OC3_L",
	.zg_pin = "C14",
	.zg_id = 265,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO26_1",
	.zg_signal = "BP_PCIE_RST1_L",
	.zg_pin = "A23",
	.zg_id = 266,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
}, {
	.zg_name = "EGPIO26_2",
	.zg_signal = "BP_PCIE_RST2_L",
	.zg_pin = "B28",
	.zg_id = 267,
	.zg_cap = ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_3P3_S5
} };

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
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO1",
	.zg_signal = "BP_SYS_RESET_L",
	.zg_pin = "DH6",
	.zg_id = 1,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO2",
	.zg_signal = "BP_WAKE_L",
	.zg_pin = "DJ10",
	.zg_id = 2,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO3",
	.zg_signal = "BP_AGPIO3",
	.zg_pin = "DE36",
	.zg_id = 3,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO4",
	.zg_signal = "BP_AGPIO4",
	.zg_pin = "DF36",
	.zg_id = 4,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO5",
	.zg_signal = "BP_AGPIO5",
	.zg_pin = "DF40",
	.zg_id = 5,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO6",
	.zg_signal = "BP_AGPIO6",
	.zg_pin = "DE40",
	.zg_id = 6,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO7",
	.zg_signal = "BP_AGPIO7",
	.zg_pin = "DH4",
	.zg_id = 7,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO12",
	.zg_signal = "BP_PWRGD_OUT",
	.zg_pin = "DH36",
	.zg_id = 12,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO13",
	.zg_signal = "BP_I2C4_SCL_HP",
	.zg_pin = "DG12",
	.zg_id = 14,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO14",
	.zg_signal = "BP_I2C4_SDA_HP",
	.zg_pin = "DH12",
	.zg_id = 14,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO16",
	.zg_signal = "BP_USB10_OC_L",
	.zg_pin = "DG40",
	.zg_id = 16,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO17",
	.zg_signal = "BP_USB11_OC_L",
	.zg_pin = "DH40",
	.zg_id = 17,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO19",
	.zg_signal = "BP_I2C5_BMC_SCL",
	.zg_pin = "DJ21",
	.zg_id = 19,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO20",
	.zg_signal = "BP_I2C5_BMC_SDA",
	.zg_pin = "DJ20",
	.zg_id = 20,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I2C,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO21",
	.zg_signal = "BP_AGPIO21",
	.zg_pin = "DF33",
	.zg_id = 21,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO22",
	.zg_signal = "BP_AGPIO22",
	.zg_pin = "DE32",
	.zg_id = 22,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO23",
	.zg_signal = "BP_ESPI_RSTOUT_L",
	.zg_pin = "DE27",
	.zg_id = 23,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO24",
	.zg_signal = "BP_SMERR_L",
	.zg_pin = "DJ11",
	.zg_id = 24,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO26",
	.zg_signal = "BP_PCIE_RST1_L",
	.zg_pin = "DG36",
	.zg_id = 26,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO74",
	.zg_signal = "BP_ESPI_CLK2",
	.zg_pin = "DF27",
	.zg_id = 74,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO75",
	.zg_signal = "BP_ESPI_CLK1",
	.zg_pin = "DG23",
	.zg_id = 75,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO76",
	.zg_signal = "BP_AGPIO76",
	.zg_pin = "DJ25",
	.zg_id = 76,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO87",
	.zg_signal = "BP_AGPIO87",
	.zg_pin = "DG11",
	.zg_id = 87,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO88",
	.zg_signal = "BP_AGPIO88",
	.zg_pin = "DJ29",
	.zg_id = 88,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO89",
	.zg_signal = "BP_GENINT_L",
	.zg_pin = "DJ35",
	.zg_id = 89,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO104",
	.zg_signal = "BP_AGPIO104",
	.zg_pin = "DH30",
	.zg_id = 104,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO105",
	.zg_signal = "BP_AGPIO105",
	.zg_pin = "DE30",
	.zg_id = 105,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO106",
	.zg_signal = "BP_AGPIO106",
	.zg_pin = "DF31",
	.zg_id = 106,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO107",
	.zg_signal = "BP_AGPIO107",
	.zg_pin = "DG31",
	.zg_id = 107,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO108",
	.zg_signal = "BP_ESPI0_ALERT_L",
	.zg_pin = "DJ22",
	.zg_id = 108,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO109",
	.zg_signal = "BP_AGPIO109",
	.zg_pin = "DG32",
	.zg_id = 109,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO110",
	.zg_signal = "BP_ESPI1_ALERT_L",
	.zg_pin = "DF24",
	.zg_id = 110,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO115",
	.zg_signal = "BP_AGPIO115",
	.zg_pin = "DH16",
	.zg_id = 115,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO116",
	.zg_signal = "BP_AGPIO116",
	.zg_pin = "DH15",
	.zg_id = 116,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO117",
	.zg_signal = "BP_ESPI_CLK0",
	.zg_pin = "DJ27",
	.zg_id = 117,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO118",
	.zg_signal = "BP_SPI_CS0_L",
	.zg_pin = "DF30",
	.zg_id = 118,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO119",
	.zg_signal = "BP_SPI_CS1_L",
	.zg_pin = "DG26",
	.zg_id = 119,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO120",
	.zg_signal = "BP_ESPI0_DAT0",
	.zg_pin = "DF28",
	.zg_id = 120,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO121",
	.zg_signal = "BP_ESPI0_DAT1",
	.zg_pin = "DG22",
	.zg_id = 121,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO122",
	.zg_signal = "BP_ESPI0_DAT2",
	.zg_pin = "DJ28",
	.zg_id = 122,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO123",
	.zg_signal = "BP_ESPI0_DAT3",
	.zg_pin = "DG29",
	.zg_id = 123,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO124",
	.zg_signal = "BP_ESPI_CS0_L",
	.zg_pin = "DG28",
	.zg_id = 124,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO125",
	.zg_signal = "BP_ESPI_CS1_L",
	.zg_pin = "DJ23",
	.zg_id = 125,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO126",
	.zg_signal = "BP_SPI_CS2_L",
	.zg_pin = "DH27",
	.zg_id = 126,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO129",
	.zg_signal = "BP_ESPI_RSTIN_L",
	.zg_pin = "DJ26",
	.zg_id = 129,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO131",
	.zg_signal = "BP_ESPI1_DAT0",
	.zg_pin = "DH24",
	.zg_id = 131,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO132",
	.zg_signal = "BP_ESPI1_DAT1",
	.zg_pin = "DE24",
	.zg_id = 132,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO133",
	.zg_signal = "BP_ESPI1_DAT2",
	.zg_pin = "DF25",
	.zg_id = 133,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO134",
	.zg_signal = "BP_ESPI1_DAT3",
	.zg_pin = "DG25",
	.zg_id = 134,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO135",
	.zg_signal = "BP_UART0_CTS_L",
	.zg_pin = "DJ33",
	.zg_id = 135,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO136",
	.zg_signal = "BP_UART0_RXD",
	.zg_pin = "DG35",
	.zg_id = 136,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO137",
	.zg_signal = "BP_UART0_RTS_L",
	.zg_pin = "DF34",
	.zg_id = 137,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO138",
	.zg_signal = "BP_UART0_TXD",
	.zg_pin = "DJ34",
	.zg_id = 138,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO139",
	.zg_signal = "BP_UART0_INTR",
	.zg_pin = "DG34",
	.zg_id = 139,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO141",
	.zg_signal = "BP_UART1_RXD",
	.zg_pin = "DH33",
	.zg_id = 141,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO142",
	.zg_signal = "BP_UART1_TXD",
	.zg_pin = "DJ32",
	.zg_id = 142,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO145",
	.zg_signal = "BP_I3C0_SCL",
	.zg_pin = "A71",
	.zg_id = 145,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO146",
	.zg_signal = "BP_I3C0_SDA",
	.zg_pin = "B71",
	.zg_id = 146,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO147",
	.zg_signal = "BP_I3C1_SCL",
	.zg_pin = "C7",
	.zg_id = 147,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO148",
	.zg_signal = "BP_I3C1_SDA",
	.zg_pin = "A5",
	.zg_id = 148,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO149",
	.zg_signal = "BP_I3C2_SCL",
	.zg_pin = "C72",
	.zg_id = 149,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO150",
	.zg_signal = "BP_I3C2_SDA",
	.zg_pin = "B72",
	.zg_id = 150,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO151",
	.zg_signal = "BP_I3C3_SCL",
	.zg_pin = "A4",
	.zg_id = 151,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO152",
	.zg_signal = "BP_I3C3_SDA",
	.zg_pin = "B4",
	.zg_id = 152,
	.zg_cap = ZEN_GPIO_C_AGPIO,
	.zg_pad = ZEN_GPIO_PAD_TYPE_I3C,
	.zg_voltage = ZEN_GPIO_V_1P1_S3 | ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO256",
	.zg_signal = "BP_AGPIO256",
	.zg_pin = "A14",
	.zg_id = 256,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO257",
	.zg_signal = "BP_AGPIO257",
	.zg_pin = "C14",
	.zg_id = 257,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO258",
	.zg_signal = "BP_AGPIO258",
	.zg_pin = "A13",
	.zg_id = 258,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO259",
	.zg_signal = "BP_AGPIO259",
	.zg_pin = "B16",
	.zg_id = 259,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO260",
	.zg_signal = "BP_SGPIO_DATAOUT",
	.zg_pin = "D15",
	.zg_id = 260,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO261",
	.zg_signal = "BP_SGPIO_LOAD",
	.zg_pin = "B15",
	.zg_id = 261,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO264",
	.zg_signal = "BP_USB00_OC_L",
	.zg_pin = "E23",
	.zg_id = 265,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO265",
	.zg_signal = "BP_USB01_OC_L",
	.zg_pin = "E24",
	.zg_id = 265,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
}, {
	.zg_name = "AGPIO266",
	.zg_signal = "BP_PCIE_RST0_L",
	.zg_pin = "D18",
	.zg_id = 266,
	.zg_cap = ZEN_GPIO_C_AGPIO | ZEN_GPIO_C_REMOTE,
	.zg_pad = ZEN_GPIO_PAD_TYPE_GPIO,
	.zg_voltage = ZEN_GPIO_V_1P8_S5
} };

const size_t zen_gpio_sp3_nents = ARRAY_SIZE(zen_gpio_sp3_data);
const size_t zen_gpio_sp5_nents = ARRAY_SIZE(zen_gpio_sp5_data);
