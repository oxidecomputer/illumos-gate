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

#ifndef _SYS_IO_MILAN_PCIE_RSMU_H
#define	_SYS_IO_MILAN_PCIE_RSMU_H

/*
 * Contains a series of strap registers for different parts of the SoC.
 *
 * The PCIe straps are identical for both Rome and Milan. These come in two
 * groups. Those that cover the entire 'milan_pcie_port_t' and then those
 * specific to a given bridge. For PCIe straps, we note the size of these in
 * bits if it's more than one. We have guessed that these are in bits because
 * the PCIe vendor id strap is 16 units wide.
 */

#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/io/milan/fabric.h>
#include <sys/io/milan/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The location of registers in the PCIE_RSMU unit is, like some other parts of
 * the PCIe SMN space, highly nonstandard.  As a result, we ignore the values of
 * srd_nents and srd_stride (more pointedly: they must not be set); each
 * register in this block has a number of instances that depend upon the unit (1
 * per IOMS) and the aperture base is effectively formed by both the unit and
 * register instance numbers.  Registers are 32 bits wide, however.
 */

static inline smn_reg_t
milan_pcie_rsmu_smn_reg(const uint8_t iomsno, const smn_reg_def_t def,
    const uint16_t reginst)
{
	const uint32_t PCIE_RSMU_SMN_REG_MASK = 0xfff;
	const uint32_t ioms32 = (const uint32_t)iomsno;
	const uint32_t reginst32 = (const uint32_t)reginst;

	ASSERT0(def.srd_size);
	ASSERT3S(def.srd_unit, ==, SMN_UNIT_PCIE_RSMU);
	ASSERT0(def.srd_nents);
	ASSERT0(def.srd_stride);
	ASSERT3U(ioms32, <, 4);
	ASSERT0(def.srd_reg & ~PCIE_RSMU_SMN_REG_MASK);

#ifdef	DEBUG
	const uint32_t nents = (const uint32_t)milan_nbio_n_pcie_ports(iomsno);
	ASSERT3U(nents, >, reginst32);
#endif	/* DEBUG */

	const uint32_t aperture_base = 0x9046000;

	const uint32_t aperture_off = (ioms32 << 12) + (reginst32 << 14);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_RSMU_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg));
}

/*
 * Strap settings are accessed via the RSMU via this indirect index/data pair;
 * the registers are undocumented and their names are not known.
 */
/*CSTYLED*/
#define	D_PCIE_RSMU_STRAP_ADDR	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_RSMU,	\
	.srd_reg = 0x7ac	\
}
/*CSTYLED*/
#define	D_PCIE_RSMU_STRAP_DATA	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PCIE_RSMU,	\
	.srd_reg = 0x7b0	\
}
#define	PCIE_RSMU_STRAP_ADDR(h, p)	\
    milan_pcie_rsmu_smn_reg(h, D_PCIE_RSMU_STRAP_ADDR, p)
#define	PCIE_RSMU_STRAP_DATA(h, p)	\
    milan_pcie_rsmu_smn_reg(h, D_PCIE_RSMU_STRAP_DATA, p)

/*
 * The upper bits of the address register for any given strap
 */
#define	MILAN_STRAP_PCIE_ADDR_UPPER	0xfffe0000

/*
 * The transmitter preset encodings, which can be found in the PCIe base
 * specification. Values for each preset can be found at
 * Table 8-1 Tx Preset Ratios and Corresponding Coefficient Values
 */
#define	MILAN_STRAP_PCIE_TX_PRESET_0	0
#define	MILAN_STRAP_PCIE_TX_PRESET_1	1
#define	MILAN_STRAP_PCIE_TX_PRESET_2	2
#define	MILAN_STRAP_PCIE_TX_PRESET_3	3
#define	MILAN_STRAP_PCIE_TX_PRESET_4	4
#define	MILAN_STRAP_PCIE_TX_PRESET_5	5
#define	MILAN_STRAP_PCIE_TX_PRESET_6	6
#define	MILAN_STRAP_PCIE_TX_PRESET_7	7
#define	MILAN_STRAP_PCIE_TX_PRESET_8	8
#define	MILAN_STRAP_PCIE_TX_PRESET_9	9
#define	MILAN_STRAP_PCIE_TX_PRESET_10	10

/*
 * The reciever preset encodings. Note that these are optional and only valid
 * for Gen 3.
 */
#define	MILAN_STRAP_PCIE_RX_PRESET_6DB	0
#define	MILAN_STRAP_PCIE_RX_PRESET_7DB	1
#define	MILAN_STRAP_PCIE_RX_PRESET_8DB	2
#define	MILAN_STRAP_PCIE_RX_PRESET_9DB	3
#define	MILAN_STRAP_PCIE_RX_PRESET_10DB	4
#define	MILAN_STRAP_PCIE_RX_PRESET_11DB	5
#define	MILAN_STRAP_PCIE_RX_PRESET_12DB	6
#define	MILAN_STRAP_PCIE_RX_PRESET_RSVD	7

/*
 * A control that disallows further writes to the various straps. The default is
 * zero. Write 0x1 to disable.
 */
#define	MILAN_STRAP_PCIE_WRITE_DISABLE		0x00

/*
 * Controls whether MSI's are supported. Default value is 0x1, aka MSIs are
 * enabled.
 */
#define	MILAN_STRAP_PCIE_MSI_EN			0x01

/*
 * Controls whether AERs are enabled. The default is 0x0, aka no AERs.
 */
#define	MILAN_STRAP_PCIE_AER_EN			0x02

/*
 * 0x3 is reserved
 */

/*
 * Suggestive of whether or not PCIe Gen2 compliance mode is supported. The
 * default is 0x01.
 */
#define	MILAN_STRAP_PCIE_GEN2_COMP		0x04

/*
 * This controls the PCIe Link Capability bit 18 Clock Power Management feature.
 * Default is 0x0.
 */
#define	MILAN_STRAP_PCIE_CLK_PM_EN		0x05

/*
 * Unclear feature. Defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_DECODE_TO_HIDDEN_REG	0x06

/*
 * This is used to describe 'legacy devices'. This defaults to 0x0 and we
 * believe that the AMD instantiation does not end up with them.
 */
#define	MILAN_STRAP_PCIE_LEGACY_DEVICE_EN	0x07

/*
 * We believe that this controls the generation of initiator (master) completion
 * timeouts. This defaults to 0x1, that is, enabled.
 */
#define	MILAN_STRAP_PCIE_CPL_TO_EN		0x08

/*
 * This appears to be a means to force some timeout. Its relationship to the
 * strap above is unclear. Defaults to 0.
 */
#define	MILAN_STRAP_PCIE_FORCE_TO_EN		0x09

/*
 * The PCIe hardware apparently has an i2c debug interface that this allows one
 * to manipulate. That's, well, spicy. Defaults to 0.
 */
#define	MILAN_STRAP_PCIE_I2C_DBG_EN		0x0a

/*
 * This controls whether or not the Device Capabilities 2 TPH Completer
 * Supported bit is enabled. The default is 0x0, no.
 */
#define	MILAN_STRAP_PCIE_TPH_EN			0x0b

/*
 * This seems to control whether some internal soft reset can happen or not.
 * What kinds of resets this controls is not clear. The default is 0x0, so the
 * 'normal' flow happens.
 */
#define	MILAN_STRAP_PCIE_NO_SOFT_RST		0x0c

/*
 * This controls whether or not the device advertises itself as a multi-function
 * device and presumably a bunch more of side effects. This defaults to 0x1,
 * enabled.
 */
#define	MILAN_STRAP_PCIE_MULTI_FUNC_EN		0x0d

/*
 * This controls behavior related to an extended tag ECN. It's not clear what
 * ECN this is referring to. It is unlikely to be the PCIe 2.0 Extended Tag
 * Enable Default, though stranger things have happened. This defaults to 0x0.
 * We expect that the 10-bit tag support is more likely to be what we need to
 * deal with and manipulate (e.g. strap 0x110).
 */
#define	MILAN_STRAP_PCIE_TAG_EXT_ECN_EN		0x0e

/*
 * This controls whether or not the device advertises downstream port
 * containment features or not. Default 0x0.
 */
#define	MILAN_STRAP_PCIE_DPC_EN			0x0f

/*
 * This controls whether or not the Link Feature Extended Capability (0x25) is
 * enabled. The default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_DLF_EN			0x10

/*
 * This controls whether or not the Physical Layer 16.0 GT/s Extended Capability
 * (0x26) is enabled. The default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_PL_16G_EN		0x11

/*
 * This controls whether or not the Lane Margining at the Receiver Extended
 * Capability (0x27). The default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_LANE_MARGIN_EN		0x12

/*
 * 0x13 is reserved
 */

/*
 * This likely controls the Virtual Channel Capability, though it's not entirely
 * clear. This does seem to be used with CCIX. The same is true for the
 * subsequent definition. What exactly each of these controls is a mystery.
 */
#define	MILAN_STRAP_PCIE_VC_EN			0x14
#define	MILAN_STRAP_PCIE_2VC_EN			0x15

/*
 * We don't know what DSN here actually defines, unfortunately. It may relate to
 * something downstream. This defaults to zero and doesn't seem to be generally
 * manipulated.
 */
#define	MILAN_STRAP_PCIE_DSN_EN			0x16

/*
 * This controls the ARI Extended Capability and whether those features are
 * advertised or enabled. The default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_ARI_EN			0x17

/*
 * Based on what we have of the name of this strap, it may control whether or
 * not it advertises a function zero, which is a bit weird. However, it is
 * enabled by default.
 */
#define	MILAN_STRAP_PCIE_F0_EN			0x18

/*
 * The next two control whether we advertise support for D1 and D2 power states.
 * This most likely manifests itself in the power management capability. The
 * default is that these are 0 -- the device does not support them.
 */
#define	MILAN_STRAP_PCIE_POWER_D1_SUP		0x19
#define	MILAN_STRAP_PCIE_POWER_D2_SUP		0x1a

/*
 * This possibly controls whether or not 64-bit addressing is enabled on the
 * device in various ways. This defaults to being enabled.
 */
#define	MILAN_STRAP_PCIE_64BIT_ADDRS		0x1b

/*
 * This seems to control some alternate buffer implementation. It's disabled by
 * default, but what all it does, who knows.
 */
#define	MILAN_STRAP_PCIE_ALT_BUF_EN		0x1c

/*
 * Enables support for the Latency Tolerance Reporting (LTR) Extended
 * Capability. This changes the values in the device capabilities 2 register.
 * Defaults to 0x0, disabled.
 */
#define	MILAN_STRAP_PCIE_LTR_SUP		0x1d

/*
 * Controls whether optimized buffer flush/fill is advertised as supported in
 * the device capabilities 2 register. The default is 0x0, disabled.
 */
#define	MILAN_STRAP_PCIE_OBFF_SUP		0x1e

/*
 * Seems to control something related to symbol alignment in the device.
 * Specifics unknown. Defaults to 0x0, unknown effect.
 */
#define	MILAN_STRAP_PCIE_SYMALIGN_MODE		0x1f

/*
 * A debug mechanism for the above feature. Defaults to 0x0, which is presumably
 * disabled.
 */
#define	MILAN_STRAP_PCIE_SYMALIGN_DBG		0x20

/*
 * This supposedly controls whether we skip the internal scrambler in the data
 * path. Unclear exactly and set to 0x0, probably disabled, by default.
 */
#define	MILAN_STRAP_PCIE_BYPASS_SCRAMBLER	0x21

/*
 * This seems to control some internal rx error limit on deskewed data. It seems
 * to relate to some internal drop metric as well, but again, specifics unclear.
 * The default is 0x0 though this is a 3-bit wide field.
 */
#define	MILAN_STRAP_PCIE_DESKEW_RXERR_LIMIT	0x22

/*
 * This controls whether a deskew on 'empty mode' is supported. The default is
 * 0x1, suggesting it is by default.
 */
#define	MILAN_STRAP_PCIE_DESKEW_EMPTY		0x23

/*
 * Suggests that we only perform a deskew when a TS2 ordered set is received.
 * Default is 0x0, suggesting we don't.
 */
#define	MILAN_STRAP_PCIE_DESKEW_TS2_ONLY	0x24

/*
 * This one is mostly a guess from the name on deskewing when there's a bulk
 * unlikely repeating packet perhaps? Default is 0x0.
 */
#define	MILAN_STRAP_PCIE_DESKEW_RPT		0x25

/*
 * This seems to perform deskew logic on all SKP (skip) special physical layer
 * symbols. The default is 0x1, suggesting enabled.
 */
#define	MILAN_STRAP_PCIE_DESKEW_ALL_SKP		0x26

/*
 * This seems to control whether or not a transition in the link training and
 * status state machine (LTSSM) will cause a reset to the deskew logic.
 */
#define	MILAN_STRAP_PCIE_LTSSM_DESKEW_RESET	0x27

/*
 * This seems to control whether or not SKP symbols are removed on the data
 * path. The default is 0x1, suggesting this is enabled.
 */
#define	MILAN_STRAP_PCIE_DESKEW_RM_SKP		0x28

/*
 * This next one seems to be related to 'EI' or 'IE', which we're guessing
 * relates to electrical idle. This is notably a 6 bit value that appears to
 * control how many clock cycles are used to avoid some behavior happening.
 * Probably ignoring garbage. The default appears to be 0x20 cycles.
 */
#define	MILAN_STRAP_PCIE_DESKEW_EI_GAP		0x29

/*
 * This is related to the above and indicates when dealing with electrical idle
 * ordered sets whether or not the symbol data after the logical idle (IDL)
 * framing data is removed. The default is 0x1, indicating that this is done.
 */
#define	MILAN_STRAP_PCIE_DESKEW_EI_RM		0x2a

/*
 * This controls whether or not the hardware performs deskew logic on TS ordered
 * sets when it receives both a TS and SKP. The default appears to be 0x0,
 * indicating this is not performed.
 */
#define	MILAN_STRAP_PCIE_DESKEW_TS_SKP		0x2b

/*
 * This is a mysterious entry that appears to manipulate some aspect of the
 * deskew behavior, perhaps shrinking it. The default is 0x0, we probably
 * shouldn't toggle this.
 */
#define	MILAN_STRAP_PCIE_DESKEW_SHRINK		0x2c

/*
 * This appears to control specific behavior in PCIe Gen 3 related to the LSFR
 * (part of the scrambling behavior it appears) when SKP ordered sets are
 * received. The default is 0x0.
 */
#define	MILAN_STRAP_PCIE_DESKEW_GEN3_SKP	0x2d

/*
 * This appears to control whether or not the read pointer is reset in hardware
 * after a deskew attempt fails. The default is 0x1, this is enabled.
 */
#define	MILAN_STRAP_PCIE_DESKEW_READ_RST	0x2e

/*
 * This appears to control some amount of phase shift manipulation after a
 * deskew event has occurred. The default is 0x1, that this occurs.
 */
#define	MILAN_STRAP_PCIE_DESKEW_PHASE		0x2f

/*
 * This is a bit vague, but appears to control whether or not we have do a
 * synchronization or synchronous (not sure which) header check for a block.
 * This seems to be related to deskewing. The default is 0x1.
 */
#define	MILAN_STRAP_PCIE_DESKEW_BLOCK_HDR	0x30

/*
 * This appears to be a means to ignore part of the SKP ordered set related to
 * DC balancing. The default is 0x1, that presumably this is enabled.
 */
#define	MILAN_STRAP_PCIE_SKP_IGNORE_DC_BAL	0x31

/*
 * This is an unknown debug interface, seemingly reserved. This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_DEBUG_RXP		0x32

/*
 * We don't know much more than what this says on the tin. Basically that it
 * deals with some data rate checks. The default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_DATA_RATE_CHECK	0x33

/*
 * This controls whether or not the 'fast' transmit clock is always used or not.
 * This defaults to 0x0, suggesting we don't.
 */
#define	MILAN_STRAP_PCIE_FAST_TXCLK_EN		0x34

/*
 * This impacts the PLL somehow indicating the mode that it operates in or is
 * comparing against. This is a 2 bit field and the value defaults to 0x2.
 */
#define	MILAN_STRAP_PCIE_PLL_FREQ_MODE		0x35

/*
 * This seems to exist to force the link into Gen 2 mode. It defaults to 0,
 * disabled.
 */
#define	MILAN_STRAP_PCIE_FORCE_GEN2		0x36

/*
 * 0x37 and 0x38 are reserved
 */

/*
 * This one isn't very clear, but it seems to relate to loopback behavior,
 * received data, and perhaps an equalization evaluation? It defaults to 0x1.
 */
#define	MILAN_STRAP_PCIE_LO_RXEQEVAL_EN		0x39

/*
 * This seems to control whether the device advertises whether or not it
 * supports the LTSSM 'upconfigure' ability which allows a link to train up to a
 * higher speed later. The default is 0x0, this is not enabled.
 */
#define	MILAN_STRAP_PCIE_UPCONF_SUP		0x3a

/*
 * This seems to control whether or not the upconfigure discussed above is
 * enabled or not. This defaults to 0x0, suggesting it's not disabled by default
 * (however, it's also not advertised by default as noted above).
 */
#define	MILAN_STRAP_PCIE_UPCONF_DIS		0x3b

/*
 * This is a weird one where the most we get is from a name which is saying that
 * the the device should not deassert the receive enable while in some kind of
 * test. This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST	0x3c

/*
 * 0x3d is reserved.
 */

/*
 * This seems to control a selectable de-emphasis index. This defaults to index
 * 0. It is unclear what that means.
 */
#define	MILAN_STRAP_PCIE_SELECT_DEEMPH		0x3e

/*
 * 0x3f is reserved.
 */

/*
 * This controls whether or not the Link Bandwidth Management capability in the
 * Link Capabilities register is advertised, probably. This defaults to 0x1,
 * suggesting it is advertised.
 */
#define	MILAN_STRAP_PCIE_LINK_BW_NOTIF_SUP	0x40

/*
 * This field is a little weird and all we know is that this is to reverse
 * 'all'. What is all? Lanes, something else? It's hard to say, but this
 * defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_REVERSE_ALL		0x41

/*
 * This seems to exist to force the link into Gen 3 mode. It defaults to 0x0,
 * disabled.
 */
#define	MILAN_STRAP_PCIE_FORCE_GEN3		0x42

/*
 * This is used to control whether the device enables PCIe 3.1 compliant
 * features, not the LTSSM compliance mode. It defaults to 0x1, which says these
 * features are enabled (which is reasonable).
 */
#define	MILAN_STRAP_PCIE_GEN3_1_FEAT_EN		0x43

/*
 * This is used to control whether the device enables PCIe 4.0 compliant
 * features, not the LTSSM compliance mode. It defaults to 0x1, which says these
 * features are enabled (which is reasonable).
 */
#define	MILAN_STRAP_PCIE_GEN4_FEAT_EN		0x44

/*
 * This controls whether or not the ECRC generation is enabled. Our guess is
 * that this controls the 'ECRC Generation Enable' bit in the AER capability.
 * The default is 0x0, saying that this is not enabled.
 */
#define	MILAN_STRAP_PCIE_ECRC_GEN_EN		0x45

/*
 * This pairs with the strap above and indicates whether or not the device
 * defaults to checking a generated TLP CRC. This is the 'ECRC Check Enable' bit
 * in the AER capability. The default is 0x0, saying that this is not enabled.
 */
#define	MILAN_STRAP_PCIE_ECRC_CHECK_EN		0x46

/*
 * This seems to control the number of times that training can fail before
 * disabling a given speed failure. The default here is 0x2, suggesting perhaps
 * 2 times?
 */
#define	MILAN_STRAP_PCIE_TRAIN_FAIL_SPEED_DIS	0x47

/*
 * 0x48 is reserved.
 */

/*
 * This seems to control a 'port order' feature. Not clear what is being ordered
 * here. The default is 0x0, suggesting disabled.
 */
#define	MILAN_STRAP_PCIE_PORT_ORDER_EN		0x49

/*
 * The next several entries are all about ignoring certain kinds of errors that
 * can be detected on the receive side. These all default to 0x0, indicating
 * that we do *not* ignore the error, which is what we want.
 */
#define	MILAN_STRAP_PCIE_IGN_RX_IO_ERR		0x4a
#define	MILAN_STRAP_PCIE_IGN_RX_BE_ERR		0x4b
#define	MILAN_STRAP_PCIE_IGN_RX_MSG_ERR		0x4c

/*
 * 0x4d is reserved
 */

#define	MILAN_STRAP_PCIE_IGN_RX_CFG_ERR		0x4e
#define	MILAN_STRAP_PCIE_IGN_RX_CPL_ERR		0x4f
#define	MILAN_STRAP_PCIE_IGN_RX_EP_ERR		0x50
#define	MILAN_STRAP_PCIE_IGN_RX_BAD_LEN_ERR	0x51
#define	MILAN_STRAP_PCIE_IGN_RX_MAX_PAYLOAD_ERR	0x52
#define	MILAN_STRAP_PCIE_IGN_RX_TC_ERR		0x53

/*
 * 0x54 is reserved
 */

#define	MILAN_SRAP_PCIE_IGN_RX_AT_ERR		0x55

/*
 * 0x56 is reserved
 */

/*
 * Unlike the others, this seems to be a massive error reporting disable switch.
 * We want this to be zero at all costs, which is thankfully the default.
 */
#define	MILAN_STRAP_PCIE_ERR_REPORT_DIS		0x57

/*
 * This controls whether or not a CPL abort error is enabled in hardware. The
 * default for this is 0x1, that the error is presumably enabled.
 */
#define	MILAN_STRAP_PCIE_CPL_ABORT_ERR_EN	0x58

/*
 * This controls whether or not an internal error is enabled in hardware. The
 * default for this is 0x1, that the error is presumably enabled.
 */
#define	MILAN_STRAP_PCIE_INT_ERR_EN		0x59

/*
 * This strap is mysterious, all we get is a name. However, this appears to be
 * related to a PCD on rx margining persistence. This controls disabling
 * something. The default is 0x0.
 */
#define	MILAN_STRAP_PCIE_RXP_ACC_FULL_DIS	0x5a

/*
 * 0x5b is reserved.
 */

/*
 * This seems to control what the link's CDR filter (probably on the
 * clock) test offset is. It's not clear what the units of this are; however, it
 * defaults to 0x60. This is a 12-bit field.
 */
#define	MILAN_STRAP_PCIE_CDR_TEST_OFF		0x5c

/*
 * This relates to the same bit as above and instead seems to control something
 * related to 'sets. The default here is 0x18 and is again a 12 bit field.
 */
#define	MILAN_STRAP_PCIE_CDR_TEST_SETS		0x5d

/*
 * This is the third sibling of the above two and relates to the 'type' of the
 * CDR set. It defaults to a value of 0x1. This is a 2-bit field.
 */
#define	MILAN_STRAP_PCIE_CDR_TYPE		0x5e

/*
 * This is a force bit. Presumably when set it forces some of the other settings
 * to be honored. The default ix 0x0.
 */
#define	MILAN_STRAP_PCIE_CDR_MODE_FORCE		0x5f

/*
 * 0x60 is reserved.
 */

/*
 * This seems to control whether or not a test mode is enabled through a toggle.
 * This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_TEST_TOGGLE		0x61

/*
 * This seems to control an index for the toggle pattern.
 */
#define	MILAN_STRAP_PCIE_TEST_PATTERN		0x62

/*
 * This one is just a generic transmit test bit. It is 2 bits wide and defaults
 * to 0x0. Not sure what this controls exactly.
 */
#define	MILAN_STRAP_PCIE_TX_TEST_ALL		0x63

/*
 * Overwrite the advertised vendor id! The default is unsurprisingly 0x1022.
 * This is 16 bits wide.
 */
#define	MILAN_STRAP_PCIE_VENDOR_ID		0x64

/*
 * Set the base and sub class code. This is 0x6 and 0x4 as expected. Each of
 * these is 8 bits wide. These are what are advertised in configuration space.
 */
#define	MILAN_STRAP_PCIE_BASE_CLASS		0x65
#define	MILAN_STRAP_PCIE_SUB_CLASS		0x66

/*
 * These two bits control the upper and lower nibble of the configuration space
 * revision ID. This defaults to 0x0. Each of these is 4 bits wide.
 */
#define	MILAN_STRAP_PCIE_REV_ID_UPPER		0x67
#define	MILAN_STRAP_PCIE_REV_ID_LOWER		0x68

/*
 * 0x69 is reserved.
 */

/*
 * This seems to control what an i2c target address is. The base spec doesn't
 * really define this (while the CEM has i2c lanes and VPD messages exist). This
 * may be related to the debug interface. Either way this is a 7-bit field that
 * defaults to 0x8. We presume this is the device's 7-bit address.
 */
#define	MILAN_STRAP_PCIE_I2C_TARG_ADDR		0x6a

/*
 * 0x6b is a reserved control related to i2c.
 */

/*
 * The name for this one suggests that it has to deal with the link autonomous
 * bandwidth interrupt. However, there are pieces here that suggest this is
 * something internal to the device as opposed to the bits in the PCIe
 * capability. Either way this defaults to 0x1, suggesting it's probably
 * enabled.
 */
#define	MILAN_STRAP_PCIE_LINK_AUTO_BW_INT	0x6c

/*
 * 0x6d is reserved.
 */

/*
 * This next set of straps all control whether PCIe access control services is
 * turned on and various aspects of it. These all default to being disabled.
 */
#define	MILAN_STRAP_PCIE_ACS_EN			0x6e
#define	MILAN_STRAP_PCIE_ACS_SRC_VALID		0x6f
#define	MILAN_STRAP_PCIE_ACS_TRANS_BLOCK	0x70
#define	MILAN_STRAP_PCIE_ACS_DIRECT_TRANS_P2P	0x71
#define	MILAN_STRAP_PCIE_ACS_P2P_CPL_REDIR	0x72
#define	MILAN_STRAP_PCIE_ACS_P2P_REQ_RDIR	0x73
#define	MILAN_STRAP_PCIE_ACS_UPSTREAM_FWD	0x74

/*
 * This seemingly is used to indicate an SDP unit id. It is unlikely that this
 * is referring to the PCIe data link layer Start DLLP special symbol and is
 * instead probably related to the scalable data fabric and indicates its port.
 * Either way, this is a 4-bit field that defaults to 0x2.
 */
#define	MILAN_STRAP_PCIE_SDP_UNIT_ID		0x75

/*
 * Strap 0x76 is reserved for ACS.
 * Strap 0x77 is reserved for PM (power management).
 */

/*
 * This seems to be used to indicate some amount of power management event
 * support. This is a 5-bit field that defaults to 0x19.
 */
#define	MILAN_STRAP_PCIE_PME_SUP		0x78

/*
 * Strap 0x79 is reserved for PM again.
 */

/*
 * This strap is used to seemingly disable all Gen 3 features. This is used when
 * trying to use the special BMC lanes that we thankfully are ignoring. The
 * default is 0x0, Gen 3 is enabled.
 */
#define	MILAN_STRAP_PCIE_GEN3_DIS		0x7a

/*
 * This is used to control whether or not multicast is supported on the device.
 */
#define	MILAN_STRAP_PCIE_MCAST_EN		0x7b

/*
 * These next two control whether or not we have atomics enabled and whether or
 * not they can be routed. The default in both cases is 0x0. These both seem to
 * be related to f0, which we believe is part of the root complex.
 */
#define	MILAN_STRAP_PCIE_F0_ATOMIC_EN		0x7c
#define	MILAN_STRAP_PCIE_F0_ATOMIC_ROUTE_EN	0x7d

/*
 * This controls the number of MSIs requested by the bridge in the MSI
 * capability. This is a 3-bit field and defaults to 0x0, indicating 1 MSI, a
 * common default.
 */
#define	MILAN_STRAP_PCIE_MSI_MULTI_MSG_CAP	0x7e

/*
 * This appears to control whether or not the primary root complex advertises
 * the 'No RO-enabled PR-PR Passing' bit of the Device Capabilities 2 register,
 * which is related to relaxed ordering. The default appears to be 0x0,
 * suggesting it does not advertise this bit.
 */
#define	MILAN_STRAP_PCIE_F0_NO_RO_PR_PR_PASS	0x7f

/*
 * This is a stranger strap. It appears to relate to the MSI capability
 * 'Multiple Message Enable' field. Perhaps it controls whether or not it does
 * things to honor when a write occurs. The default of this field ix 0x1.
 */
#define	MILAN_STRAP_PCIE_MSI_MULTI_MSG_EN	0x80

/*
 * This seems to control something related to the reset of the phy's
 * calibration. It defaults to 0x0. It's unclear when this would be set.
 */
#define	MILAN_STRAP_PCIE_PHY_CALIB_RESET	0x81

/*
 * This appears to be some kind of reset control that restricts that happens on
 * a reset, though it's very hard to say. The default here is 0x0.
 */
#define	MILAN_STRAP_PCIE_CFG_REG_RST_ONLY	0x82

/*
 * This probably controls some kind of reset happening when the link goes down.
 * Unclear. It defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_LINK_DOWN_RST_EN	0x83

/*
 * This strap is used to seemingly disable all Gen 4 features. This is used when
 * trying to use the special BMC lanes that we thankfully are ignoring. The
 * default is 0x0, Gen 4 is enabled.
 */
#define	MILAN_STRAP_PCIE_GEN4_DIS		0x84

/*
 * This one is a mystery. It defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_STATIC_PG_EN		0x85

/*
 * This one is related to the above, but again a real mystery. This is a 2-bit
 * field that defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_FW_PG_EXIT_CTL		0x86

/*
 * This presumably relates to the previous two in some way, but we don't know
 * what livmin is. This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_LIVMIN_EXIT_CTL	0x87

/*
 * 0x88 is reserved.
 */

/*
 * The next large set of straps all relate to the use of CCIX and AMD's CCIX
 * Enhanced Speed Mode, their CCIX/PCIe extension. The first here controls
 * whether support is advertised.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_SUP		0x89

/*
 * This controls presumably some part of what the phy's internal reach is. This
 * defaults to 0x0 and is 2 bits wide.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_PHY_REACH_CAP	0x8a

/*
 * This controls whether or not a recalibrate is needed and defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_RECALIBRATE	0x8b

/*
 * These next several all relate to calibration time and timeouts. Each field is
 * 3 bits wide, though the units aren't clear. The default in all cases is zero.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_CALIB_TIME	0x8c
#define	MILAN_STRAP_PCIE_CCIX_ESM_QUICK_EQ_TO	0x8d
#define	MILAN_STRAP_PCIE_CCIX_ESM_EQ_PHASE2_TO	0x8e
#define	MILAN_STRAP_PCIE_CCIX_ESM_EQ_PHASE3_TO	0x8f

/*
 * These control the upstream and downstream tx equalization presets. These are
 * both 3 bit fields and default to 0x0.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_DSP_20GT_EQ_TX	0x90
#define	MILAN_STRAP_PCIE_CCIX_ESM_USP_20GT_EQ_TX	0x91

/*
 * This presumably controls a CCIX optional tlp formation. It defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_CCIX_OPT_TLP_FMT_SUP	0x92

/*
 * 0x93 is reserved.
 */

/*
 * This controls the CCIX vendor ID level value. This defaults to 0x1002.
 */
#define	MILAN_STRAP_PCIE_CCIX_VENDOR_ID		0x94

/*
 * 0x95 is reserved.
 */

/*
 * This seems to be yet another hardware debug strap related to the 'PI' part of
 * the device. It is 32 bits wide.
 */
#define	MILAN_STRAP_PCIE_PI_HW_DEBUG		0x96

/*
 * These next two are used to advertise the device serial number. There are two
 * 32-bit words, one of which is considered the 'LSB' and the other the 'MSB'.
 * This presumably contribute to a device serial number capability. The LSB
 * supposedly defaults to '0x1' and the MSB to '0xc8700'
 */
#define	MILAN_STRAP_PCIE_SN_LSB			0x97
#define	MILAN_STRAP_PCIE_SN_MSB			0x98

/*
 * These next two straps control our favorite subsystem IDs. These are 32-bit
 * ids and it defaults to 0x1022,1234.
 */
#define	MILAN_STRAP_PCIE_SUBVID			0x99
#define	MILAN_STRAP_PCIE_SUBDID			0x9a

#define	PCI_VENDOR_ID_OXIDE			0x1de
#define	MILAN_STRAP_PCIE_SUBDID_BRIDGE		0xfff9
/*
 * This is a link config control with no information on it other than it is 6
 * bits wide and defaults to 0xd.
 */
#define	MILAN_STRAP_PCIE_LINK_CONFIG		0x9b

/*
 * This seems to control some configuration permeation index. It is 4 bits wide
 * and defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_LINK_CONFIG_PERMUTE	0x9c

/*
 * This controls some chip mode. It is 8 bits wide and defaults to 0xff. A guess
 * would be if set to something else that would set us to some other specific
 * mode.
 */
#define	MILAN_STRAP_PCIE_CHIP_MODE		0x9d

/*
 * 0x9e is reserved.
 */

/*
 * Downstream and upstream lane equalization control preset hint. While this is
 * a lane setting, it is used for all lanes in the device. This seems to be for
 * Gen 3. Defaults to 0x3 for downstream, 0x0 for upstream, 3 bits wide.
 */
#define	MILAN_STRAP_PCIE_EQ_DS_RX_PRESET_HINT	0x9f
#define	MILAN_STRAP_PCIE_EQ_US_RX_PRESET_HINT	0xa0

/*
 * TX variants of above. These are 4 bits wide. Defaults to 0x3 for upstream and
 * 0x4 for downstream.
 */
#define	MILAN_STRAP_PCIE_EQ_DS_TX_PRESET	0xa1
#define	MILAN_STRAP_PCIE_EQ_US_TX_PRESET	0xa2

/*
 * 16.0 GT/s TX lane TX presets. 4 bits wide and defaults to 0x3 for downstream
 * and 0x1 for upstream.
 */
#define	MILAN_STRAP_PCIE_16GT_EQ_DS_TX_PRESET	0xa3
#define	MILAN_STRAP_PCIE_16GT_EQ_US_TX_PRESET	0xa4

/*
 * 25.0 GT/s ESM tx presets. 4 bits wide, defaults to 0xf in both cases.
 */
#define	MILAN_STRAP_PCIE_25GT_EQ_DS_TX_PRESET	0xa5
#define	MILAN_STRAP_PCIE_25GT_EQ_US_TX_PRESET	0xa6

/*
 * 0xa7 is reserved.
 */

/*
 * This seems to control something called 'quicksim', mysterious. Default is
 * 0x0.
 * This seems to be meant for validation.
 * Some clues can be found here;
 * https://www.amd.com/system/files/TechDocs/48692.pdf
 */
#define	MILAN_STRAP_PCIE_QUICKSIM_START		0xa8

/*
 * 0xa9 is reserved.
 */

/*
 * This next set all control various ESM speeds it seems. These all default to
 * 0x1 and are 1 bit wide with the exception of the minimum time in electrical
 * idle which is a 9 bit field and defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_ESM_12P6_12P8		0xaa
#define	MILAN_STRAP_PCIE_ESM_12P1_12P5		0xab
#define	MILAN_STRAP_PCIE_ESM_11P1_12P0		0xac
#define	MILAN_STRAP_PCIE_ESM_9P6_11P0		0xad
#define	MILAN_STRAP_PCIE_ESM_MIN_EI_TIME	0xae
#define	MILAN_STRAP_PCIE_ESM_16P0		0xaf
#define	MILAN_STRAP_PCIE_ESM_17P0		0xb0
#define	MILAN_STRAP_PCIE_ESM_18P0		0xb1
#define	MILAN_STRAP_PCIE_ESM_19P0		0xb2
#define	MILAN_STRAP_PCIE_ESM_20P0		0xb3
#define	MILAN_STRAP_PCIE_ESM_21P0		0xb4
#define	MILAN_STRAP_PCIE_ESM_22P0		0xb5
#define	MILAN_STRAP_PCIE_ESM_23P0		0xb6
#define	MILAN_STRAP_PCIE_ESM_24P0		0xb7
#define	MILAN_STRAP_PCIE_ESM_25P0		0xb8

/*
 * 0xb9 is reserved.
 */

/*
 * These next set of straps all involve a 'SWUS', which is an upstream switch.
 * They are duplicates of a number of other straps that we've already seen. In
 * general, these don't seem to be needed as there isn't much on that upstream
 * switch on our platforms. These all seem to have the same sizes and defaults
 * values as the non-SWUS variant.
 */
#define	MILAN_STRAP_PCIE_SWUS_MSI_EN		0xba
#define	MILAN_STRAP_PCIE_SWUS_VC_EN		0xbb
#define	MILAN_STRAP_PCIE_SWUS_DSN_EN		0xbc
#define	MILAN_STRAP_PCIE_SWUS_AER_EN		0xbd
#define	MILAN_STRAP_PCIE_SWUS_ECRC_CHECK_EN	0xbe
#define	MILAN_STRAP_PCIE_SWUS_ECRC_GEN_EN	0xbf
#define	MILAN_STRAP_PCIE_SWUS_CPL_ABORT_ERR_EN	0xc0
#define	MILAN_STRAP_PCIE_SWUS_F0_ATOMIC_EN	0xc1
#define	MILAN_STRAP_PCIE_SWUS_F0_ATOMIC_ROUTE_EN	0xc2
#define	MILAN_STRAP_PCIE_SWUS_F0_NO_RO_PR_PR_PASS	0xc3
#define	MILAN_STRAP_PCIE_SWUS_ERR_REPORT_DIS	0xc4
#define	MILAN_STRAP_PCIE_SWUS_NO_SOFT_RST	0xc5
#define	MILAN_STRAP_PCIE_SWUS_POWER_D1_SUP	0xc6
#define	MILAN_STRAP_PCIE_SWUS_POWER_D2_SUP	0xc7
#define	MILAN_STRAP_PCIE_SWUS_LTR_SUP		0xc8
#define	MILAN_STRAP_PCIE_SWUS_ARI_EN		0xc9
#define	MILAN_STRAP_PCIE_SWUS_SUBVID		0xca
#define	MILAN_STRAP_PCIE_SWUS_SUB_CLASS		0xcb
#define	MILAN_STRAP_PCIE_SWUS_BASE_CLASS	0xcc
#define	MILAN_STRAP_PCIE_SWUS_REV_ID_UPPER	0xcd
#define	MILAN_STRAP_PCIE_SWUS_REV_ID_LOWER	0xce
#define	MILAN_STRAP_PCIE_SWUS_PME_SUP		0xcf
#define	MILAN_STRAP_PCIE_SWUS_OBFF_SUP		0xd0

/*
 * At this point all of our PCIe straps are now changed to be per-logical
 * bridge. Each of the 8 bridges all have the same set of straps; however, each
 * one is 0x61 off from one another (or put differently there are 0x61 straps
 * per bridge).
 */
#define	MILAN_STRAP_PCIE_NUM_PER_BRIDGE		0x61

/*
 * 0xd1 is reserved
 */

/*
 * One guess for this setting is that it is used to pause the act of training
 * wherever it is currently. Unclear whether this is technically a 'strap' the
 * same way that others are; however, it has to be written indirectly via the
 * SMU in the same way. The default is 0x0, that this isn't held.
 */
#define	MILAN_STRAP_PCIE_P_HOLD_TRAINING	0xd2

/*
 * This seems to be a way to control the links mode in some way when the above
 * strap is asserted, probably. This is only a single bit wide, which makes it
 * an interesting choice for a mode control bit. The default is 0x0.
 */
#define	MILAN_STRAP_PCIE_P_LC_HOLD_TRAINING_MODE	0xd3

/*
 * 0xd4 and 0xd5 are reserved
 */

/*
 * This strap appears to be a means of disabling the root complexes ability to
 * perform an automatic speed negotiation. It's not clear if this is just
 * general link training or also the autonomous settings as well. This defaults
 * to 0x0, meaning this is enabled. There is a second variant of this for 16
 * GT/s operation (e.g. gen 4).
 */
#define	MILAN_STRAP_PCIE_P_RC_SPEED_NEG_DIS	0xd6
#define	MILAN_STRAP_PCIE_P_RC_SPEED_NEG_16GT_DIS	0xd7

/*
 * The next two straps appear to control whether or not link speed negotiation
 * can begin in either the L0s or the L1 link states. These default to 0x1,
 * suggesting that they are enabled.
 */
#define	MILAN_STRAP_PCIE_P_L0s_SPEED_NEG_EN	0xd8
#define	MILAN_STRAP_PCIE_P_L1_SPEED_NEG_EN	0xd9

/*
 * This strap seems to provide a means of saying that a target speed override
 * should be enabled. This presumably pairs with the target link speed strap
 * below which would contain the actual setting. This defaults to 0x0, disabled.
 */
#define	MILAN_STRAP_PCIE_P_TARG_LINK_SPEED_EN	0xda

/*
 * These next two straps provide a way to bypass different parts of PCIe Gen 3
 * equalization. The first seems to skip it in general while the second is for
 * the request phase. Both default to 0x0, suggesting that equalization always
 * happens.
 */
#define	MILAN_STRAP_PCIE_P_8GT_BYPASS_EQ	0xdb
#define	MILAN_STRAP_PCIE_P_8GT_BYPASS_EQ_REQ	0xdc

/*
 * This straps appears to provide a means for setting the equalization search
 * mode. It is a two bit field that defaults to 0x3.
 */
#define	MILAN_STRAP_PCIE_P_8GT_EQ_SEARCH_MODE	0xdd

/*
 * These next three are the Gen 4 variants of the Gen 3 bits above. They have
 * the same sizes and default values.
 */
#define	MILAN_STRAP_PCIE_P_16GT_BYPASS_EQ	0xde
#define	MILAN_STRAP_PCIE_P_16GT_BYPASS_EQ_REQ	0xdf
#define	MILAN_STRAP_PCIE_P_16GT_EQ_SEARCH_MODE	0xe0

/*
 * This strap works in tandem with the MILAN_STRAP_PCIE_P_TARG_LINK_SPEED_EN
 * strap above, probably. This controls what the target link speed of the port
 * would be. It is a two bit field that defaults to 0x3, which suggests that is
 * PCIe Gen 4 operation, which in turn would suggest that the values here are:
 * bit value = gen - 1.
 */
#define	MILAN_STRAP_PCIE_P_TARG_LINK_SPEED	0xe1

/*
 * 0xe2 and 0xe3 are reserved
 */

/*
 * These next two straps are related to the L0s and L1s inactivity value. It's
 * likely that these refer to PCIEPORT::PCIE_LC_CNTL. These are 4-bit fields
 * that default to 0x0, with 0 meaning disabled.
 */
#define	MILAN_STRAP_PCIE_P_L0s_INACTIVITY	0xe4
#define	MILAN_STRAP_PCIE_P_L1_INACTIVITY	0xe5

/*
 * 0xe6 is reserved.
 */

/*
 * This strap controls what the default electrical idle mode is supposed to be
 * for the link. Unfortunately, we do not know what the values for this are.
 * This is a 2-bit field that defaults to a setting of 0x1.
 */
#define	MILAN_STRAP_PCIE_P_ELEC_IDLE_MODE	0xe7

/*
 * This seems to control the default ASPM support field in the Link capabilities
 * register. It is a two-bit field that seems to mimic the ASPM control field.
 * The default is 0x3, which is L0s and L1 are supported.
 */
#define	MILAN_STRAP_PCIE_P_ASPM_SUP		0xe8

/*
 * These next two straps control the defined exit latency values for L0s and L1.
 * These seem to mimic the values in the PCIe link configuration register and
 * the L0s Exit Latency and L1 Exit Latency values. These are 3-bit fields. The
 * default for L1 is 0x6 and for L0s, 0x3. Note, it appears that it is expected
 * that software tunes the L0s exit latency to 7 so as to discourage its use.
 */
#define	MILAN_STRAP_PCIE_P_L1_EXIT_LAT		0xe9
#define	MILAN_STRAP_PCIE_P_L0s_EXIT_LAT		0xea

/*
 * It isn't exactly clear what this does. Based on the fact that it's in the
 * ASPM group and that it's a 1-bit field that defaults to 0x1, we can wager
 * that this is used to control some amount of signalling when the link exits an
 * L1 state.
 */
#define	MILAN_STRAP_PCIE_P_L1_EXIT_SIGNAL	0xeb

/*
 * 0xec is reserved.
 */

/*
 * This strap is a bit unclear. It seems related to link bandwidth notifications
 * and controls the detection mode for those perhaps? This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_LINK_BW_NOTIF_DETECT_MODE	0xed

/*
 * This strap supposedly actually controls some amount of the equalization
 * behavior when a timeout occurs. It is said to discard the (equalization?)
 * coefficients that were received at that point in time. This defaults to 0x1
 * suggesting that it does indeed discard this data.
 */
#define	MILAN_STRAP_PCIE_P_LINK_EQ_DISCARD_AFTER_TIMEOUT	0xee

/*
 * This appears to control the behavior of the equalization algorithm when it is
 * doing an exhaustive search of some kind. Nominally this bit is selecting
 * which path it takes, though what those paths are are unknown. This defaults
 * to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_LINK_EQ_SEARCH_MODE	0xef

/*
 * 0xf0 is reserved.
 */

/*
 * This seems to control whether a selectable de-emphasis is enabled, seemingly
 * for Gen 2 devices. The default value of this strap is 0x1.
 */
#define	MILAN_STRAP_PCIE_P_DEEMPH_SEL		0xf1

/*
 * These next two straps are used to control the indication of a retimer
 * presence detection supported. These show up in the Link Capabilities 2
 * register, most likely. The default for both of these is 0x0, suggesting it is
 * not supported. Presumably it would be up to the platform to provide this.
 */
#define	MILAN_STRAP_PCIE_P_RETIMER1_DET_SUP	0xf2
#define	MILAN_STRAP_PCIE_P_RETIMER2_DET_SUP	0xf3

/*
 * This register isn't entirely clear but seems to relate to whether a test
 * related timer is used or not. It is a 2-bit field and defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_TEST_TIMER_SEL	0xf4

/*
 * This bit most likely controls the behavior of the margining port capabilities
 * which is required for Gen 4 and higher operation and is used to indicate
 * whether external software is required. Apparently the answer is no.
 */
#define	MILAN_STRAP_PCIE_P_MARGIN_NEEDS_SW	0xf5

/*
 * This strap isn't very clear, but seems to allow some amount of control over
 * the automatic disabling of supported speeds. This could be something that
 * allows the device to say we're giving up on a given transfer rate or
 * something else entirely. Unfortunately it's not very clear. This defaults to
 * 0x0, suggesting this feature isn't enabled.
 */
#define	MILAN_STRAP_PCIE_P_AUTO_DIS_SPEED_SUP_EN	0xf6

/*
 * These next three straps are all related to 'SPC', which seems to stand for
 * Symbol Per Clock and seem to target individual generation speeds. They are
 * all 2 bits wide. 2.5GT and 5.0GT default to 0, while 8 GT defaults to 1, and
 * 16 GT to 2. It could be possible these are related to SRIS.
 */
#define	MILAN_STRAP_PCIE_P_SPC_MODE_2P5GT	0xf7
#define	MILAN_STRAP_PCIE_P_SPC_MODE_5GT		0xf8
#define	MILAN_STRAP_PCIE_P_SPC_MODE_8GT		0xf9
#define	MILAN_STRAP_PCIE_P_SPC_MODE_16GT	0xfa

/*
 * The next two straps are part of SRIS (separate reference clocks with
 * independent spread spectrum) support on the device. The first seems to
 * control whether it is enabled and the second allows an autodetection to be
 * enabled. In general it seems both of these shouldn't be enabled though they
 * can both be disabled. They both default to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_SRIS_EN		0xfb
#define	MILAN_STRAP_PCIE_P_AUTO_SRIS_EN		0xfc

/*
 * This strap is unclear and seems to control something related to transmitter
 * behavior. The swing here probably is something related to the transmitter's
 * voltage swing, but this is a 1-bit field with a default value of 0x0, making
 * it a bit less clear.
 */
#define	MILAN_STRAP_PCIE_P_TX_SWING		0xfd

/*
 * These next two seem to relate to whether various preset parameters. In
 * particular, this probably is related to the voltage swing, equalization, etc.
 * Both of these seem to suggest they impact 'all' of the presets being
 * accepted. If this is disabled, perhaps only some are taken? The second of
 * these refers to a test situation. These both default to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_ACCEPT_PRESETS	0xfe
#define	MILAN_STRAP_PCIE_P_ACCEPT_PRESETS_TEST	0xff

/*
 * This controls how long the PHY has for Figure of Merit (FOM). This is a 2-bit
 * field and defaults to 0x0.
 * 0: 300us
 * 1: 200us
 * 2: 100us
 * 3: < 100us.
 */
#define	MILAN_STRAP_PCIE_P_FOM_TIME		0x100
#define	MILAN_STRAP_PCIE_P_FOM_300US		0
#define	MILAN_STRAP_PCIE_P_FOM_200US		1
#define	MILAN_STRAP_PCIE_P_FOM_100US		2
#define	MILAN_STRAP_PCIE_P_FOM_SUB_100US	3

/*
 * This is used to specify some kind of safe equalization search setting. It
 * defaults to 0x0, probably suggesting this isn't used.
 */
#define	MILAN_STRAP_PCIE_P_EQ_SAFE_SEARCH	0x101

/*
 * These next two seem to relate to an 'ENH' variant of the 8GT and 16GT preset
 * search selections. In other contexts we see 'ENH' to refer to enhanced
 * configuration space (e.g. PCIe configuration starting at 0x100); however,
 * that doesn't seem quite appropriate here. These are both 2-bit fields that
 * default to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_8GT_PRESET_SEARCH_SEL	0x102
#define	MILAN_STRAP_PCIE_P_16GT_PRESET_SEARCH_SEL	0x103

/*
 * The next two fields relate to some mask that is applied to the preset values
 * for 8 and 16GT operation. These are 10-bit fields that default to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_8GT_PRESET_MASK	0x104
#define	MILAN_STRAP_PCIE_P_16GT_PRESET_MASK	0x105

/*
 * 0x106 is reserved.
 */

/*
 * This strap likely relates to how poison is treated and whether it is an
 * advisory non-fatal error perhaps? It's not entirely clear from just this.
 * Either way, this  defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_POISON_ADV_NF	0x107

/*
 * This strap seems to set a default maximum payload. This is a 3-bit field and
 * probably relates to the maximum payload size supported field of the device
 * capabilities register. This is a 3-bit field and the default value is 0x2. If
 * this is correct then that suggests that the default maximum payload is 512
 * bytes.
 */
#define	MILAN_STRAP_PCIE_P_MAX_PAYLOAD_SUP	0x108

/*
 * This strap is a bit unclear, but may relate to the behavior of logging errors
 * in a root port's header log register. This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_LOG_FIRST_RX_ERR	0x109

/*
 * This strap probably controls whether or not the 'Extended Tag Field
 * supported' bit is set in the device control register. This is set to 0x0 by
 * default; however, it is important that software change this (and the 10-bit
 * settings) to allow for a larger number of PCIe transactions.
 */
#define	MILAN_STRAP_PCIE_P_EXT_TAG_SUP		0x10a

/*
 * This probably determines whether or not the various end-to-end TLP prefix
 * types are supported such as PASID and TPH. This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_E2E_TLP_PREFIX_EN	0x10b

/*
 * This controls an unknown ECC related setting for the 'BCH'. This is perhaps
 * something internal the device, but hard to say. This defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_BCH_ECC_EN		0x10c

/*
 * This presumably controls whether the port supports the ECRC being regenerated
 * when dealing with multicast transactions.  The default is 0x0, suggesting
 * that this is not advertised as supported by default.
 */
#define	MILAN_STRAP_PCIE_P_MC_ECRC_REGEN_SUP	0x10d

/*
 * Each of these fields is a mask with one bit per link speed, 2.5 GT/s in bit 0
 * up to 16 GT/s in bit 3.  GEN controls whether we support generating SKP
 * ordered sets at the lower rate used with common clocking or SRNS (600 ppm
 * clock skew), while RCV controls whether we support receiving SKP OSs at that
 * lower rate.  It's not clear whether this has any effect without either SRIS
 * or SRIS autodetection being enabled.  The manual tells us that if we want to
 * use SRIS, at least the receive register needs to be zero, meaning that we
 * expect to receive SKP OSs at the higher rate specified for SRIS (5600 ppm),
 * which makes sense: we need the extra SKPs to prevent the receiver's elastic
 * buffers from over- or underflowing.  The default values are 0.
 */
#define	MILAN_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP	0x10e
#define	MILAN_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP	0x10f

/*
 * These next two relate to the device capabilities 2 register and whether or
 * not they support the 10-bit tag completer or requester respectively. This are
 * related to MILAN_STRAP_PCIE_P_EXT_TAG_SUP and control whether or not the
 * device advertises 10-bit support. The default for these is 0x0, suggesting
 * that they are not supported.
 */
#define	MILAN_STRAP_PCIE_P_10B_TAG_CMPL_SUP	0x110
#define	MILAN_STRAP_PCIE_P_10B_TAG_REQ_SUP	0x111

/*
 * This controls whether or not the CCIX vendor specific cap is advertised or
 * not, it seems. The default is 0x0, suggesting this is not supported.
 */
#define	MILAN_STRAP_PCIE_P_CCIX_EN		0x112

/*
 * 0x113 is reserved
 */

/*
 * This strap seems to control a bunch of settings with respect to how the
 * device's training behavior operates. It is a 3-bit field with a bunch of
 * hardware-specific meanings. This generally seems to be left at 0x0 (the
 * default), which is to negotiate in a compliant mode trying to get as wide a
 * link as plausible.  Most likely this corresponds to
 * PCIEPORT::PCIEP_STRAP_LC[STRAP_LANE_NEGOTIATION].
 */
#define	MILAN_STRAP_PCIE_P_LANE_NEG_MODE	0x114

/*
 * 0x115 is reserved
 */

/*
 * This strap isn't clear, but may suggest that hardware actually skip doing
 * receiver detection logic discussed in the PCIe spec. The default is 0x0,
 * suggesting that this is not bypassed.  Most likely corresponds to
 * PCIEPORT::PCIEP_STRAP_LC[STRAP_RCVR_BYPASS_DET].
 */
#define	MILAN_STRAP_PCIE_P_BYPASS_RX_DET	0x116

/*
 * This strap presumably allows for a link to be forced into compliance mode.
 * This defaults to 0x0, allowing the LTTSM to transition normally.  Most likely
 * corresponds to PCIEPORT::PCIEP_STRAP_LC[STRAP_FORCE_COMPLIANCE].
 */
#define	MILAN_STRAP_PCIE_P_COMPLIANCE_FORCE	0x117

/*
 * This is the opposite of the one above and seems to allow for compliance mode
 * to be disabled entirely. This defaults to 0x0, allowing the LTTSM to
 * transition to compliance mode.  Most likely corresponds to
 * PCIEPORT::PCIEP_STRAP_LC[STRAP_COMPLIANCE_DIS].
 */
#define	MILAN_STRAP_PCIE_P_COMPLIANCE_DIS	0x118

/*
 * Our guess for this strap is that it allows a device to actually train to
 * 0x12. This strap is phrased as a disable, disallowing this width (probably
 * due to it being uncommon) and its corresponding default is 0x1.
 */
#define	MILAN_STRAP_PCIE_P_NEG_X12_DIS		0x119

/*
 * This strap seems to relate to lane reversal. What this is or isn't reversing
 * isn't clear; however, it seems this shouldn't be used and instead we should
 * use the features built into the DXIO subsystem for doing this. The default is
 * 0x0, suggesting no reversals.  This most likely corresponds to
 * PCIEPORT::PCIEP_STRAP_MISC[STRAP_REVERSE_LANES].
 */
#define	MILAN_STRAP_PCIE_P_REVERSE_LANES	0x11a

/*
 * 0x11b is reserved.
 */

/*
 * This strap is for a mysterious form of 'enhanced hotplug'. It is unclear what
 * this actually refers to on the device. The default here is 0x0, suggesting
 * that it is disabled.
 */
#define	MILAN_STRAP_PCIE_P_ENHANCED_HP_EN	0x11c

/*
 * 0x11d is reserved.
 */

/*
 * The next two straps aren't very clear. FTS could be the fast training sets
 * and that this is describing the number of ones we expect. The first one seems
 * to relate to an expected count that's received hile the second seems to
 * relate to an initial number of ones to send. The first one is a 2-bit field
 * and defaults to 0x0 while the second is an 8-bit field that defaults to 0x18.
 */
#define	MILAN_STRAP_PCIE_P_FTS_TS_COUNT		0x11e
#define	MILAN_STRAP_PCIE_P_FTS_INIT_NUM		0x11f

/*
 * This strap seems to be the device ID. This is a 16-bit field that defaults to
 * 148d.
 */
#define	MILAN_STRAP_PCIE_P_DEVID		0x120

/*
 * This strap is a bit mysterious. It basically is used to indicate if it is a
 * 'SB'. In normal AMD parlance this might be something related to controlling
 * or indicating whether this is a southbridge; however, here, that's less
 * obvious. All we know is that this is a 1-bit field that defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_IS_SB		0x121

/*
 * 0x122 is reserved
 */

/*
 * These next few fields all relate to the L1 PM substates capability and
 * various features there. In particular, we have bits for PCI and ASPM L1.2 and
 * L1.1 supported. The default is that only PCI PM L1.1 is supported along with
 * L1 PM substates.
 */
#define	MILAN_STRAP_PCIE_P_PCIPM_L1P2_SUP	0x123
#define	MILAN_STRAP_PCIE_P_PCIPM_L1P1_SUP	0x124
#define	MILAN_STRAP_PCIE_P_ASPM_L1P2_SUP	0x125
#define	MILAN_STRAP_PCIE_P_ASPM_L1P1_SUP	0x126
#define	MILAN_STRAP_PCIE_P_PM_SUB_SUP		0x127

/*
 * 0x128 is reserved
 */

/*
 * This controls the port's value of Tcommonmode in us. This controls the
 * restoration of common clocking and is part of the L1.0 exit process and
 * controls a minimum time that the TS1 training sequence is sent for. The
 * default for this 8-bit field is 0x0. It appears that software must overwrite
 * this to 0xa.
 */
#define	MILAN_STRAP_PCIE_P_TCOMMONMODE_TIME	0x129

/*
 * This presumably sets the default Tpower_on scale value in the L1 PM Substates
 * Control 2 register. This is a 2-bit field that defaults to 0x0, indicating
 * that the scale of Tpower_on is 2us. It appears software is expected to
 * overwrite this to 0x1, indicating 10us.
 */
#define	MILAN_STRAP_PCIE_P_TPON_SCALE		0x12a

/*
 * 0x12b is reserved
 */

/*
 * This goes along with MILAN_STRAP_PCIE_P_TPON_SCALE and sets the value that
 * should be there. A companion to our friend above. This is a 5-bit register
 * and the default value is 0x5. It seems software may be expected to set this
 * to 0xf.
 */
#define	MILAN_STRAP_PCIE_P_TPON_VALUE		0x12c

/*
 * 0x12d is reserved
 */

/*
 * The next two straps are related to the PCIe Gen 4 data link feature
 * capability. The first controls whether this is supported or not while the
 * latter allows for feature exchange to occur. These both default to 0x0,
 * indicating that they are not supported and enabled respectively.
 */
#define	MILAN_STRAP_PCIE_P_DLF_SUP		0x12e
#define	MILAN_STRAP_PCIE_P_DLF_EXCHANGE_EN	0x12f

/*
 * This strap seems to control what the header scaling factor, which seems to be
 * used as part of scaled control flow features. This seems to control the scale
 * itself. This is a 2-bit field that defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_P_DLF_HDR_SCALE_MODE	0x130

/*
 * 0x131 is reserved
 */

/*
 * This last strap is a confusing one. It seems to control a port offset, but
 * this 4-bit field always defaults to 0x0. What it offsets or is used for is a
 * a bit of a mystery.
 */
#define	MILAN_STRAP_PCIE_P_PORT_OFF		0x132

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_PCIE_RSMU_H */
