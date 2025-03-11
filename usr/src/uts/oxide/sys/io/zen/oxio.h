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

#ifndef _SYS_IO_OXIO_H
#define	_SYS_IO_OXIO_H

/*
 * oxio -- Oxide DXIO Configuration
 *
 * To utilize PCIe, SATA, and other I/O interfaces, the CPU needs to be told how
 * to set up the number of lanes it has. For example, SP3 and SP5 support 128
 * lanes, some of which may be used for PCIe, SATA, XGMI (for interconnecting
 * sockets), or other protocols. This is managed by the DXIO and KPX hardware
 * blocks; however, those are not manipulated directly by us and instead those
 * are managed by different firmware images and hidden cores within the SoC. In
 * the SP3 generation this is often referred to as 'DXIO' firmware and in SP5,
 * 'MPIO'.
 *
 * We define our own types for declaring this I/O which the corresponding
 * platform code transforms into the appropriate type. We do this for a few
 * reasons:
 *
 *  1) Not all of the fields which the structures require are things that we
 *     need to set on a per-device basis.
 *  2) This allows us to refer to fields in a way that is more straightforward
 *     when looking at a schematic and then translate them as required into the
 *     underlying hardware settings. For example, SP3 needs to tell firmware
 *     lane numbers in a way that is rather different from the physical lane
 *     numbering. But what's easiest is to actually just refer to this using a
 *     group ala G2 and a lane offset in the group.
 *  3) Some fields reference information from an earlier part. For example,
 *     firmware ends up setting up traditional PCIe hotplug after it's set up
 *     the normal hotplug. The hotplug information sometimes wants information
 *     such as what bridge we trained something on.
 *  4) There are settings that we want to set on a physical slot that aren't
 *     specific to the underlying firmware and it's useful to have a uniform way
 *     of setting this up.
 *
 * The general idea here is that each board will define a series of
 * oxio_engine_t structures which represent all of the I/O that should be
 * configured in this way. That should be linked into the platform detection
 * logic so that it is always set and known on the board. That does imply a
 * static mapping of board to slot information, which is fine for the time
 * being, but may not be valid in the limit. That level of dynamicism is
 * deferred until needed.
 *
 * Finally, not everything that the underlying firmware supports is in here.
 * What we have is mostly what's used by Oxide products and the various
 * development boards that we are interested in using.
 *
 * Finally, some terminology. Some of this is adopted from AMD.
 *
 * ENGINE
 *
 *	An engine refers to a logical MAC/PHY combination. For example, an x4
 *	slot that may have an NVMe device (or really anything via a K.2) is
 *	considered a single engine.
 *
 * TILE
 *
 *	A tile refers to one of several instances of the PCIe IP. The 128 lanes
 *	are generally grouped into 8 groups of 16 lanes each. The tile names use
 *	the nomenclature from the AMD motherboard guide and pin outs.
 */

#include <sys/stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This indicates the type of engine that we should be configuring. Today we
 * only support PCIe and a bit of UBM. A UBM device basically means that we can
 * discover at run-time what kinds of bifurcation and devices are supported. UBM
 * is supported to enable some development systems.
 */
typedef enum {
	OXIO_ENGINE_T_PCIE	= 0,
	OXIO_ENGINE_T_UBM
} oxio_engine_type_t;

/*
 * This indicates the type of hotplug that an engine is using. Note, setting the
 * overall engine type to UBM implies hotplug suppot.
 */
typedef enum {
	OXIO_HOTPLUG_T_NONE	= 0,
	OXIO_HOTPLUG_T_EXP_A,
	OXIO_HOTPLUG_T_EXP_B,
	OXIO_HOTPLUG_T_ENTSSD
} oxio_hotplug_type_t;

/*
 * This next two enumerations indicate the kind of i2c devices that might be
 * downstream. The first is for I2C GPIO expanders and the second is for I2C
 * switches.
 */
typedef enum {
	OXIO_I2C_GPIO_EXP_T_PCA9539,
	OXIO_I2C_GPIO_EXP_T_PCA9535,
	OXIO_I2C_GPIO_EXP_T_PCA9506
} oxio_i2c_gpio_expander_type_t;

typedef enum {
	OXIO_I2C_SWITCH_T_NONE,
	OXIO_I2C_SWITCH_T_9545,
	OXIO_I2C_SWITCH_T_9546_48,
} oxio_i2c_switch_type_t;

typedef enum {
	/*
	 * This flag indicates that the lanes for an engine should be reversed.
	 * This generally only applies to PCIe. Consider a x16 device. Normally
	 * lane 0 here should connect to lane 0 on the device, 1->1, 2->2, etc..
	 * If this flag is set, then instead lane 0->15, 1->14, etc. This is
	 * generally done to ease layout.
	 *
	 * This flag should be set purely based on ones understanding of the
	 * schematic. This should not concern itself with whether or not the
	 * core is reversed, that will be determined by the platform.
	 */
	OXIO_ENGINE_F_REVERSE	= 1 << 0,
} oxio_engine_flags_t;

typedef enum {
	/*
	 * This flag indicates that the reset descriptor is valid. This does not
	 * apply to UBM based hotplug.
	 */
	OXIO_HP_F_RESET_VALID	= 1 << 0,
} oxio_hp_flags_t;

/*
 * This enumerate is used to describe a given PHY's speed generation. This
 * corresponds to the PCIe speed, e.g. GEN 4 is 16.0 GT/s or the SATA
 * generation. Setting a value beyond the maximum that a given platform supports
 * will be treated as though one specified that the maximum is allowed.
 */
typedef enum {
	OXIO_SPEED_GEN_MAX = 0,
	OXIO_SPEED_GEN_1,
	OXIO_SPEED_GEN_2,
	OXIO_SPEED_GEN_3,
	OXIO_SPEED_GEN_4,
	OXIO_SPEED_GEN_5
} oxio_speed_t;

/*
 * This contains a list of applicable tiles that can be specified and translated
 * into lanes and hotplug information. Note, not all platforms support all
 * tiles. P4/P5 do not exist in SP3 and it will be an error to specify that.
 */
typedef enum oxio_tile {
	OXIO_TILE_G0,
	OXIO_TILE_P0,
	OXIO_TILE_G1,
	OXIO_TILE_P1,
	OXIO_TILE_G2,
	OXIO_TILE_P2,
	OXIO_TILE_G3,
	OXIO_TILE_P3,
	OXIO_TILE_P4,
	OXIO_TILE_P5
} oxio_tile_t;

/*
 * This describes an I2C switch that is being used for UBM. These describe what
 * must be traversed to reach the UBM EEPROM. The type describes what kind of
 * device is in use. The Address is the 7-bit address of the device. The select
 * indicates which segment of the mux needs to be used (0-based).
 */
typedef struct {
	oxio_i2c_switch_type_t ois_type;
	uint8_t ois_addr;
	uint8_t ois_select;
} oxio_i2c_switch_t;

#define	OXIO_UBM_I2C_SWITCH_MAX_DEPTH	2

/*
 * This represents the way to reach the GPIOs that are important for UBM: the
 * U.3 interface detection (IfDet#) and the UBM reset signal. The address is the
 * 7-bit address of the expander itself. The reset and ifdet members should be
 * the corresponding bit and byte that is found on the GPIO expander. The bit is
 * the relative bit in the byte, not the absolute byte. So if you have a PCA9506
 * and were on 'IO4_1', this would be byte 4, bit 1. These are always zero
 * indexed.
 */
typedef struct {
	oxio_i2c_gpio_expander_type_t oug_type;
	uint8_t oug_addr;
	uint8_t oug_ifdet_byte;
	uint8_t oug_ifdet_bit;
	uint8_t oug_reset_byte;
	uint8_t oug_reset_bit;
} oxio_ubm_gpio_t;

/*
 * This encompasses all the information that we need for UBM-specific hotplug.
 */
typedef struct {
	oxio_i2c_switch_t ohu_switch[OXIO_UBM_I2C_SWITCH_MAX_DEPTH];
	oxio_ubm_gpio_t ohu_gpio;
} oxio_hp_ubm_t;

/*
 * This enumeration indicates which hotplug related features the slot supports.
 * This should be the list of features that are connected to the GPIO expander.
 * Each bit present here will be translated into the corresponding settings in
 * the PCIe Slot Capabilities Register. Note, this is the opposite of how the
 * SMU functions. It wants to know which features should be masked off.
 */
typedef enum {
	OXIO_PCIE_CAP_OOB_PRSNT	= 1 << 0,
	OXIO_PCIE_CAP_PWREN	= 1 << 1,
	OXIO_PCIE_CAP_PWRFLT	= 1 << 2,
	OXIO_PCIE_CAP_ATTNLED	= 1 << 3,
	OXIO_PCIE_CAP_PWRLED	= 1 << 4,
	OXIO_PCIE_CAP_EMIL	= 1 << 5,
	OXIO_PCIE_CAP_EMILS	= 1 << 6,
	OXIO_PCIE_CAP_ATTNSW	= 1 << 7
} oxio_pcie_slot_cap_t;

/*
 * This structure represents a downstream GPIO for traditional hotplug features.
 */
typedef struct {
	/*
	 * This represents the path to the expander. If this is left zeroed,
	 * then we assume it is directly connected. Otherwise this will be
	 * transformed into the appropriate SMU definition. It is left in this
	 * form to make it easier to map to a schematic.
	 */
	oxio_i2c_switch_t otg_switch;
	oxio_i2c_gpio_expander_type_t otg_exp_type;
	/*
	 * This is the full 7-bit i2c address of the gpio expander. Note,
	 * traditional hotplug generally only allows for a few of the address
	 * select pins to be varied.
	 */
	uint8_t otg_addr;
	/*
	 * These represent the byte and bit offsets for a given GPIO group. When
	 * used for a reset, then this refers to the single GPIO that is used.
	 * Otherwise, this refers to the first GPIO. For ExpressModule, this is
	 * always an entire byte. For EnterpriseSSD, this refers to the starting
	 * nibble.
	 */
	uint8_t otg_byte;
	uint8_t otg_bit;
} oxio_trad_gpio_t;

/*
 * This is everything that we need for traditional hotplug.
 */
typedef struct {
	oxio_trad_gpio_t ohp_dev;
	oxio_trad_gpio_t ohp_reset;
	oxio_pcie_slot_cap_t ohp_cap;
} oxio_hp_trad_t;

/*
 * This structure represents various tuning that one might apply to a device. A
 * value of zero for any field will leave it at its default, allowing one to
 * leave it out for the most part.
 */
typedef struct oxio_tuning {
	/*
	 * The hardware limit (ot_hw_limit) represents the maximum that we tell
	 * the hardware it should operate at. The target similarly is another
	 * optional item that allows firmware to change how it operates.
	 */
	oxio_speed_t	ot_hw_limit;
	oxio_speed_t	ot_hw_target;
	/*
	 * This is a logical limit that we would like to apply to a device in a
	 * way that the OS can see. For PCIe devices, this will set a value in
	 * the PCIe Link Control 2 register. There are a few gotchas on the
	 * timing of this being applied. Please see the consumers of this for
	 * more information.
	 */
	oxio_speed_t	ot_log_limit;
} oxio_tuning_t;

typedef struct {
	/*
	 * The name is populated for debugging and humans. It serves no purpose
	 * for firmware. It is recommended that this match the external / topo
	 * slot name that we use on Oxide products.
	 */
	const char *oe_name;
	/*
	 * This identifies the engine type and what kind of hotplug is in use.
	 * If hotplug is not used, the hp type can be left out. Setting the
	 * hotplug type implies that the corresponding hotplug structure is
	 * valid. It is not necessary to set the hotplug type for UBM based
	 * entries. That is implicit in the type.
	 */
	oxio_engine_type_t oe_type;
	oxio_hotplug_type_t oe_hp_type;
	/*
	 * These three items uniquely identify an entry in the SoC. They consist
	 * of a tile, a starting lane relative to the tile, and a number of
	 * lanes. For G/P0-3 there are only 16 lanes, so the value of oe_lane
	 * can only ever be 0-15. The number of lanes can only ever be 1, 2, 4,
	 * 8, or 16.
	 */
	oxio_tile_t oe_tile;
	uint8_t oe_lane;
	uint8_t oe_nlanes;
	/*
	 * This is the slot number that should be programmed into the PCIe slot.
	 * For UBM based devices, this is the starting slot number that should
	 * be used.
	 */
	uint16_t oe_slot;
	/*
	 * These are flags that control the engine and the hotplug
	 * configuration.
	 */
	oxio_engine_flags_t oe_flags;
	oxio_hp_flags_t oe_hp_flags;
	/*
	 * The corresponding hotplug structure should be filled in based upon
	 * the hotplug type described above in oe_hp_type.
	 */
	oxio_hp_trad_t oe_hp_trad;
	oxio_hp_ubm_t oe_hp_ubm;
	/*
	 * This is a series of optional tuning information that may want to be
	 * applied.
	 */
	oxio_tuning_t oe_tuning;
} oxio_engine_t;

/*
 * The following are specific oxio engines that exist in the system.
 */
extern const oxio_engine_t oxio_gimlet[];
extern const size_t oxio_gimlet_nengines;

extern const oxio_engine_t oxio_ethanolx_s0[];
extern const oxio_engine_t oxio_ethanolx_s1[];
extern const size_t oxio_ethanolx_s0_nengines;
extern const size_t oxio_ethanolx_s1_nengines;

extern const oxio_engine_t oxio_ruby[];
extern const size_t oxio_ruby_nengines;

extern const oxio_engine_t oxio_cosmo[];
extern const size_t oxio_cosmo_nengines;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_OXIO_H */
