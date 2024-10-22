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
 * Copyright 2024 Oxide Computer Company
 */

#ifndef _SYS_IO_TURIN_PCIE_RSMU_H
#define	_SYS_IO_TURIN_PCIE_RSMU_H

/*
 * Turin and Dense Turin PCIe straps. These straps are generally additive to
 * what exists in Genoa; however, they are not at the same addresses. Like in
 * Genoa, straps are written via MPIO and nominally readable via SMN space.
 *
 * See uts/oxide/sys/io/milan/pcie_rsmu.h for an overview of these,
 * abbreviations, etc.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * See PPR 13.7.4.3.1.  This defines the port bifurcation (set of ports to be
 * created and the number of lanes in each) from the 16 lanes available to this
 * core.  This is normally set up by DXIO firmware from the engine
 * configuration.
 */
#define	TURIN_STRAP_PCIE_LINK_CONFIG		0x00

/*
 * We have no idea what this controls, if anything.  It is 4 bits wide,
 * supposedly defaults to 0, and AMD never uses it.
 */
#define	TURIN_STRAP_PCIE_LINK_CONFIG_PERMUTE	0x01

/*
 * We have no idea what this controls, if anything. It is 9 bits wide,
 * supposedly defaults to 0x1ff, and AMD never uses it. The size appears to
 * correspond to the number of ports that exist in the core given the increase
 * from 8 to 9 bits wide from SP3 to SP5.
 */
#define	TURIN_STRAP_PCIE_CHIP_MODE		0x02

/*
 * This is a 9 bit wide item that defaults to 0x00. Based on guesses from the
 * name, this likely controls what endpoints are expected to automatically exist
 * on the other side of a downstream side of a PCIe switch. AMD does not
 * explicitly touch this on the x86 side.
 */
#define	TURIN_STRAP_PCIE_SWDS_EP_EN	0x03

/*
 * 0x04 is reserved for some Link Configuration related feature.
 */

/*
 * A control that disallows further writes to some, but possibly not all, the
 * various RSMU straps. The default is zero. Write 0x1 to disable writes; AMD
 * documentation suggests that this strap is itself governed by the setting,
 * meaning that a reset would be required to modify the affected straps again.
 * This is probably less valuable than it would seem: these straps don't have
 * any effect unless the PCIe core is reset, which is done only by DXIO firmware
 * during boot.  See also PCIECORE::SWRST_COMMAND_1 and related registers
 * providing access to the PCIe core reset functionality.
 */
#define	TURIN_STRAP_PCIE_WRITE_DISABLE		0x05

/*
 * Controls whether MSI's are supported. Default value is 0x1, aka MSIs are
 * enabled.  This is possibly related to
 * PCIECORE::PCIE_STRAP_F0[STRAP_F0_MSI_EN].  Regardless, it needs to be left
 * enabled because MSI support is required by PCIe.  See PCIe5 7.7.1.
 */
#define	TURIN_STRAP_PCIE_MSI_EN			0x06

/*
 * Controls whether the AER capability structure exists for the host bridges in
 * this core.  See PCIe5 6.2 and 7.8.4.  This single-bit strap defaults to 0;
 * enabling it does not enable error detection or reporting by itself, but
 * allows generic code to do so.
 */
#define	TURIN_STRAP_PCIE_AER_EN			0x07

/*
 * See PCIECORE::PCIE_STRAP_MISC2[STRAP_GEN2_COMPLIANCE] and the gen3/4/5
 * companions below at TURIN_STRAP_PCIE_GEN3_1_FEAT_EN.  This single-bit strap
 * defaults to 1.
 */
#define	TURIN_STRAP_PCIE_GEN2_FEAT_EN		0x08

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_CLK_PM_EN] and PCIe5 7.5.3.6.  This
 * controls the PCIe Link Capability bit 18 Clock Power Management feature.
 * Default is 0x0.
 */
#define	TURIN_STRAP_PCIE_CLK_PM_EN		0x09

/*
 * See PCIECORE::PCIE_CFG_CNTL[CFG_EN_DEC_TO_HIDDEN_REG].  This single-bit strap
 * is 0 by default.  It's not known which registers this, or its two companions,
 * exposes.
 */
#define	TURIN_STRAP_PCIE_DECODE_TO_HIDDEN_REG	0x0a

/*
 * See PCIECORE::PCIE_STRAP_F0[STRAP_F0_LEGACY_DEVICE_TYPE_EN] and PCIe5 1.3.2.
 * The AMD instantiation of PCIe does not end up with any of these, and the host
 * bridges are all PCI Express endpoints.  This single-bit strap defaults to 0
 * and needs to be left there.
 */
#define	TURIN_STRAP_PCIE_LEGACY_DEVICE_EN	0x0b

/*
 * We believe that this controls the generation of initiator (master) completion
 * timeouts; it doesn't correspond directly to anything documented, but may be
 * PCIECORE::PCIE_STRAP_MISC2[STRAP_MSTCPL_TIMEOUT_EN]. This defaults to 0x1,
 * that is, enabled.
 */
#define	TURIN_STRAP_PCIE_CPL_TO_EN		0x0c

/*
 * This appears to be a means to force some master timeout. Its relationship to
 * the strap above is unclear and this doesn't correspond to anything
 * documented.  Single bit, defaults to 0.
 */
#define	TURIN_STRAP_PCIE_FORCE_TO_EN		0x0d

/*
 * The PCIe hardware apparently has an i2c debug interface that this allows one
 * to manipulate. That's, well, spicy. Single bit, defaults to 0.  See
 * PCIECORE::PCIE_STRAP_I2C_BD and the companion strap
 * TURIN_STRAP_PCIE_I2C_TARG_ADDR below.
 */
#define	TURIN_STRAP_PCIE_I2C_DBG_EN		0x0e

/*
 * This controls whether or not the Device Capabilities 2 TPH Completer
 * Supported bit is enabled.  See
 * PCIECORE::PCIE_STRAP_MISC2[STRAP_TPH_SUPPORTED] and PCIe5 6.17 and 7.5.3.15.
 * Note that while the field in devcap2 is 2 bits in size, this field, like the
 * core register, is only 1, implying that the HW does not support the extended
 * variant of the feature.  The default value is 0.
 */
#define	TURIN_STRAP_PCIE_TPH_EN			0x0f

/*
 * See PCIe5 7.5.2.2; this controls PCIERCCFG::PMI_STATUS_CNTL[NO_SOFT_RESET].
 * It's a single-bit strap that defaults to 0.
 */
#define	TURIN_STRAP_PCIE_NO_SOFT_RST		0x10

/*
 * This controls whether or not the device advertises itself as a multi-function
 * device and presumably a bunch more of side effects. This defaults to 0x1,
 * enabled.
 */
#define	TURIN_STRAP_PCIE_MULTI_FUNC_EN		0x11

/*
 * See the PPR discussion of extended tag support in 13.8.1.5.4.3; this is
 * mainly geared toward the NBIF/SWUS application, and the documented effect (it
 * sets the default value of Extended Tag Field Enable in the Device Control
 * register) is not correct, and we don't know what if anything it does.
 * Whether set to 0 or 1, this doesn't change the devcap or devctl extended tag
 * bits.  See also PCIe5 7.5.3.4, and note that we normally enable 10-bit tags
 * regardless.  The default value of this single-bit strap is 0.
 */
#define	TURIN_STRAP_PCIE_TAG_EXT_ECN_EN		0x12

/*
 * This controls whether or not the device advertises downstream port
 * containment features or not; the exact effect on the system isn't documented.
 * See PCIe5 6.2.10.  This single-bit field defaults to 0.
 */
#define	TURIN_STRAP_PCIE_DPC_EN			0x13

/*
 * This controls whether or not the Data Link Feature Extended Capability (0x25)
 * is advertised.  See PCIECORE::PCIE_STRAP_MISC[STRAP_DLF_EN] and PCIe5 7.7.4.
 * The single-bit default is 0x1, enabled.  See the per-port settings at
 * TURIN_STRAP_PCIE_P_DLF_SUP below, which control the values of bits in this
 * capability if this strap enables its existence.
 */
#define	TURIN_STRAP_PCIE_DLF_EN			0x14

/*
 * This controls whether or not the Physical Layer 16.0 GT/s Extended Capability
 * (0x26) is advertised. See PCIECORE::PCIE_STRAP_MISC[STRAP_16GT_EN] and PCIe5
 * 7.7.5.  The single-bit default is 0x1, enabled.
 */
#define	TURIN_STRAP_PCIE_PL_16G_EN		0x15

/*
 * This controls whether or not the Lane Margining at the Receiver Extended
 * Capability (0x27) exists. See PCIECORE::PCIE_STRAP_MISC[STRAP_MARGINING_EN]
 * and PCIe5 7.7.7.  The single-bit default is 0x1, enabled.
 */
#define	TURIN_STRAP_PCIE_LANE_MARGIN_EN		0x16

/*
 * This controls whether or not the Physical Layer 32.0 GT/s Extended Capability
 * (0x2A) is advertised. See PCIECORE::PCIE_STRAP_MISC[STRAP_32GT_EN] and PCIe5
 * 7.7.6. The single-bit default is 0x1, enabled.
 */
#define	TURIN_STRAP_PCIE_PL_32G_EN		0x17

/*
 * This controls whether or not the Native PCIe Enclosure Management Extended
 * Capability (0x29) is advertised. See PCIECORE::PCIE_STRAP_MISC[STRAP_NPEM_EN]
 * and PCIe5 7.9.20. The single-bit default is 0x0, disabled.
 */
#define	TURIN_STRAP_PCIE_NPEM_EN		0x18

/*
 * Virtual channel capability.  See PCIECORE::PCIE_STRAP_F0[STRAP_F0_VC_EN],
 * which controls whether this capability exists (PCIe5 7.9.1).  The second of
 * these isn't documented.  While in SP3 they were enabled for CCIX related
 * reasons, they do not appear to be any more via x86.  These are single-bit
 * straps that default to 0.
 */
#define	TURIN_STRAP_PCIE_VC_EN			0x19
#define	TURIN_STRAP_PCIE_2VC_EN			0x1a

/*
 * See PCIECORE::PCIE_STRAP_F0[STRAP_F0_DSN_EN].  This enables the device serial
 * number capability for the host bridges in this core.  The actual serial
 * number to present to software is set by TURIN_STRAP_PCIE_SN_{L,M}SB straps
 * below.  This single-bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_DSN_EN			0x1b

/*
 * This controls the ARI Extended Capability and whether those features are
 * advertised or enabled. See PCIe5 7.8.7 and some additional controls in
 * IOHC::IOHC_FEATURE_CNTL; a similar bit exists in PCIECORE::PCIE_STRAP_F0 that
 * claims to override this strap for SWUS ports only, which we don't have. This
 * single-bit strap defaults to 1.
 */
#define	TURIN_STRAP_PCIE_ARI_EN			0x1c

/*
 * Controls whether to expose function 0 on each of the devices that can contain
 * a root bridge function associated with this core (see milan_pcie_port_info in
 * milan_fabric.c).  If enabled, each device's F0 presents a type 0
 * configuration space header with no capabilities; however, F0 does identify
 * itself as being part of a multi-function device.  In other contexts, it also
 * appears that some core-level registers refer to F0 as a sort of template for
 * all downstream bridges associated with the core, without regard for that
 * bridge's device number; each of the two primary cores creates a set of
 * bridges with two different device numbers.  This strap is a single bit in
 * size and is enabled by default; it sets the default value of
 * PCIECORE::PCIE_STRAP_F0[STRAP_F0_EN].
 */
#define	TURIN_STRAP_PCIE_F0_EN			0x1d

/*
 * The next two control whether we advertise support for D1 and D2 power states
 * in the otherwise read-only PMI_CAP[D{2,1}_SUPPORT] fields.  Each single-bit
 * strap defaults to 0.  See PCIe5 7.5.2.1.
 */
#define	TURIN_STRAP_PCIE_POWER_D1_SUP		0x1e
#define	TURIN_STRAP_PCIE_POWER_D2_SUP		0x1f

/*
 * See PCIECORE::PCIE_MISC_STRAP[STRAP_MST_ADR64_EN], basically the main switch
 * for 64-bit addressing.  This single-bit strap defaults to 1, which everyone
 * wants.
 */
#define	TURIN_STRAP_PCIE_64BIT_ADDRS		0x20

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_TL_ALT_BUF_EN], not that it will help you
 * any.  This single-bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_ALT_BUF_EN		0x21

/*
 * Enables support for the Latency Tolerance Reporting (LTR) Extended
 * Capability. This changes the values in the device capabilities 2 register.
 * See PCIe5 7.8.2.  This single-bit field defaults to 0.
 */
#define	TURIN_STRAP_PCIE_LTR_SUP		0x22

/*
 * Controls whether optimized buffer flush/fill is advertised as supported in
 * the device capabilities 2 register.  See PCIe5 6.19 and 7.5.3.15 and
 * PCIEPORT::PCIEP_STRAP_MISC[STRAP_OBFF_SUPPORTED].  This is a 2-bit field that
 * defaults to 0.
 */
#define	TURIN_STRAP_PCIE_OBFF_SUP		0x23

/*
 * 0x24 is reserved.
 */

/*
 * See PCIECORE::PCIE_P_CNTL[P_SYMALIGN_{MODE,HW_DEBUG}].  This and its
 * subsequent companion are both single-bit fields that default to 0.
 */
#define	TURIN_STRAP_PCIE_SYMALIGN_MODE		0x25
#define	TURIN_STRAP_PCIE_SYMALIGN_DBG		0x26

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_BYPASS_SCRAMBLER] and PCIe5 4.2.1.3.
 * This single-bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_BYPASS_SCRAMBLER	0x27

/*
 * This seems to control some internal rx error limit on deskewed data. It seems
 * to relate to some internal drop metric as well, but again, specifics unclear.
 * The default is 0x0 though this is a 3-bit wide field.
 *
 * The next several straps all involve deskew logic, and all are undocumented.
 */
#define	TURIN_STRAP_PCIE_DESKEW_RXERR_LIMIT	0x28

/*
 * This controls whether a deskew on 'empty mode' is supported. The default is
 * 0x1, suggesting it is by default.
 */
#define	TURIN_STRAP_PCIE_DESKEW_EMPTY		0x29

/*
 * Suggests that we only perform a deskew when a TS2 ordered set is received.
 * Default is 0x0, suggesting we don't.
 */
#define	TURIN_STRAP_PCIE_DESKEW_TS2_ONLY	0x2a

/*
 * This one is mostly a guess from the name on deskewing when there's a bulk
 * unlikely repeating packet perhaps? Default is 0x0.
 */
#define	TURIN_STRAP_PCIE_DESKEW_RPT		0x2b

/*
 * This seems to control deskewing on all SKP OSs.  The default is 0x0,
 * suggesting disabled.
 */
#define	TURIN_STRAP_PCIE_DESKEW_ALL_SKP		0x2c

/*
 * This seems to control whether or not a transition in the link training and
 * status state machine (LTSSM) will cause a reset to the deskew logic. The
 * default is 0x1, suggesting enabled.
 */
#define	TURIN_STRAP_PCIE_LTSSM_DESKEW_RESET	0x2d

/*
 * This seems to control whether or not SKP symbols are removed on the data
 * path. The default is 0x1, suggesting this is enabled.
 */
#define	TURIN_STRAP_PCIE_DESKEW_RM_SKP		0x2e

/*
 * This next one seems to be related to 'EI' or 'IE', which we're guessing
 * relates to electrical idle. This is notably a 6 bit value that appears to
 * control how many clock cycles are used to avoid some behavior happening.
 * Probably ignoring garbage. The default appears to be 0x20 cycles.
 */
#define	TURIN_STRAP_PCIE_DESKEW_EI_GAP		0x2f

/*
 * This is related to the above and indicates when dealing with electrical idle
 * ordered sets whether or not the symbol data after the logical idle (IDL)
 * framing data is removed. The default is 0x1, indicating that this is done.
 */
#define	TURIN_STRAP_PCIE_DESKEW_EI_RM		0x30

/*
 * This controls whether or not the hardware performs deskew logic on TS ordered
 * sets when it receives both a TS and SKP. The default appears to be 0x0,
 * indicating this is not performed.
 */
#define	TURIN_STRAP_PCIE_DESKEW_TS_SKP		0x31

/*
 * This is a mysterious entry that appears to manipulate some aspect of the
 * deskew behavior, perhaps shrinking it. The default is 0x0, we probably
 * shouldn't toggle this.
 */
#define	TURIN_STRAP_PCIE_DESKEW_SHRINK		0x32

/*
 * This appears to control specific behavior in PCIe Gen 3 related to the LSFR
 * (part of the scrambling behavior it appears) when SKP ordered sets are
 * received. The default is 0x0.
 */
#define	TURIN_STRAP_PCIE_DESKEW_GEN3_SKP	0x33

/*
 * This appears to control whether or not the read pointer is reset in hardware
 * after a deskew attempt fails. The default is 0x1, this is enabled.
 */
#define	TURIN_STRAP_PCIE_DESKEW_READ_RST	0x34

/*
 * This appears to control some amount of phase shift manipulation after a
 * deskew event has occurred. The default is 0x1, that this occurs.
 */
#define	TURIN_STRAP_PCIE_DESKEW_PHASE		0x35

/*
 * This is a bit vague, but appears to control whether or not we report block
 * sync header errors from the deskew logic.  This doesn't correspond to
 * anything documented.  The default for this single-bit strap is 0x1.
 */
#define	TURIN_STRAP_PCIE_DESKEW_BLOCK_HDR	0x36

/*
 * This appears to be a means to ignore part of the SKP ordered set related to
 * DC balancing, possibly for interoperability reasons. The default is 0x1, that
 * presumably this is enabled.
 */
#define	TURIN_STRAP_PCIE_SKP_IGNORE_DC_BAL	0x37

/*
 * This is an unknown debug interface, seemingly reserved and 4 bits wide. This
 * defaults to 0x0.  It doesn't correspond to anything documented.
 */
#define	TURIN_STRAP_PCIE_DEBUG_RXP		0x38

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_CHECK_DATA_RATE] and note the
 * relationship with enabling 5 GT/s speed support in the engine configuration.
 * This single-bit strap defaults to 1 and is not port-specific even though the
 * registers it sets up are.
 */
#define	TURIN_STRAP_PCIE_DATA_RATE_CHECK	0x39

/*
 * See PCIECORE::PCIE_P_CNTL[P_ALWAYS_USE_FAST_TXCLK].  This single-bit strap
 * defaults to 0, and changing it looks like a very bad idea.
 */
#define	TURIN_STRAP_PCIE_FAST_TXCLK_EN		0x3a

/*
 * This impacts the PLL somehow indicating the mode that it operates in or is
 * comparing against. This is a 2 bit field and the value defaults to 0x3 and
 * doesn't correspond to any documented register or standard feature.
 */
#define	TURIN_STRAP_PCIE_PLL_FREQ_MODE		0x3b

/*
 * This seems to exist to force the link into Gen 2 mode. It defaults to 0,
 * disabled.  Note that, like all per-core straps, this presumably affects all
 * ports formed from this core.
 */
#define	TURIN_STRAP_PCIE_FORCE_GEN2		0x3c

/*
 * This strap relates how different parts of the PCIe tile deal with clocking.
 * In particular, the RX and TX portions will determine a transmit clock that is
 * related to the 'P-clock' that is used by the LC and PI subsystems. This is a
 * 1-bit value that defaults to 0x1. Likely controls
 * PCIECORE::PCIE_P_CNTL[LC_PCLK_2GHZ_MAPPING].
 */
#define	TURIN_STRAP_PCIE_2GHZ_MAP		0x3d

/*
 * 0x3e is reserved.
 */

/*
 * See PCIE_LC_RXRECOVER_RXSTANDBY_CNTL[LC_RXEQEVAL_AFTER_BYPASSED_EQ_EN] and
 * PCIEPORT::PCIE_LC_RXRECOVER_RXSTANDBY_CNTL[LC_LOOPBACK_RXEQEVAL_EN]. These
 * are single-bit straps that default to 1.
 */
#define	TURIN_STRAP_PCIE_LO_RXEQEVAL_EN		0x3f
#define	TURIN_STRAP_PCIE_LO_RXEQEVAL_BYEQ_EN	0x40

/*
 * This seems to control whether the device advertises whether or not it
 * supports the LTSSM 'upconfigure' ability which allows a link to train up to a
 * higher speed later. The default is 0x0, this is not enabled.  See
 * PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL[LC_UPCONFIGURE_SUPPORT].  Also see the next
 * strap.  Presumably these affect all ports in this core.
 */
#define	TURIN_STRAP_PCIE_UPCONF_SUP		0x41

/*
 * See PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL[LC_UPCONFIGURE_DIS].  This single-bit
 * strap is 0 by default.
 */
#define	TURIN_STRAP_PCIE_UPCONF_DIS		0x42

/*
 * See PCIEPORT::PCIE_LC_TRAINING_CNTL[LC_DONT_DEASSERT_RX_EN_IN_TEST] and mind
 * the triple-negatives.  Single bit, defaults to 0.
 */
#define	TURIN_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST	0x43

/*
 * 0x44 is reserved.
 */

/*
 * See discussion below on TURIN_STRAP_PCIE_P_DEEMPH_SEL.  This single-bit strap
 * defaults to 0.
 */
#define	TURIN_STRAP_PCIE_SELECT_DEEMPH		0x45

/*
 * 0x46 is reserved.
 */

/*
 * This controls whether or not the Link Bandwidth Management capability in the
 * Link Capabilities register is advertised. The single-bit strap defaults to
 * 0x1.
 */
#define	TURIN_STRAP_PCIE_LINK_BW_NOTIF_SUP	0x47

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_REVERSE_ALL].  Note that this applies to
 * the entire core's set of 16 lanes; see P_REVERSE_LANES below for the
 * port-specific variant.  Like that one, leave this to the DXIO firmware to
 * configure based on our engine configuration.
 */
#define	TURIN_STRAP_PCIE_REVERSE_ALL		0x48

/*
 * This seems to exist to force the link into Gen 3 mode. It's a single bit and
 * defaults to 0x0, disabled.  This doesn't correspond to any documented
 * register or standard feature.
 */
#define	TURIN_STRAP_PCIE_FORCE_GEN3		0x49

/*
 * The next three single-bit fields both default to 1.  They control whether the
 * root ports in this core enable PCIe 3.1, 4.0, and 5.0 compliant features
 * respectively, not the LTSSM compliance mode.  See PCIECORE::PCIE_STRAP_MISC2.
 */
#define	TURIN_STRAP_PCIE_GEN3_1_FEAT_EN		0x4a
#define	TURIN_STRAP_PCIE_GEN4_FEAT_EN		0x4b
#define	TURIN_STRAP_PCIE_GEN5_FEAT_EN		0x4c

/*
 * This controls the otherwise read-only 'ECRC Generation Capable' bit in the
 * root port's AER capability.  The default is 0x0, saying that this is not
 * advertised.  See PCIe5 7.8.4.7.  Note that this applies to all ports in this
 * core.
 */
#define	TURIN_STRAP_PCIE_ECRC_GEN_EN		0x4d

/*
 * This pairs with the strap above and indicates whether or not the root port
 * advertises support for checking a generated TLP CRC. This is the 'ECRC Check
 * Capable' bit in the AER capability. The default is 0x0, saying that this is
 * not advertised.
 */
#define	TURIN_STRAP_PCIE_ECRC_CHECK_EN		0x4e

/*
 * See PCIEPORT::LC_CNTL3[LC_AUTO_DISABLE_SPEED_SUPPORT_MAX_FAIL_SEL].  This
 * 2-bit strap defaults to 2 and maps directly to that field.  See additional
 * comments on TURIN_STRAP_PCIE_P_AUTO_DIS_SPEED_SUP_EN below.
 */
#define	TURIN_STRAP_PCIE_TRAIN_FAIL_SPEED_DIS	0x4f

/*
 * This pair of straps controls whether or not the LTTSM state waits in detect
 * after an explicit link disable or hot reset respectively. These correspond to
 * PCIEPORT::PCIE_LC_CNTL13[LC_HR_WAIT_DETECT_EN] and
 * PCIEPORT::PCIE_LC_CNTL13[LC_LD_WAIT_DETECT_EN]. These are 1 bit values that
 * default to 0. In general we want to leave these at zero as they basically
 * will hold the link in detect until another bit in port space is cleared as
 * opposed to allowing the normal LTTSM to proceed.
 */
#define	TURIN_STRAP_PCIE_HOLD_DETECT_HOT_RESET	0x50
#define	TURIN_STRAP_PCIE_HOLD_DETECT_LINK_DIS	0x51

/*
 * 0x52 is reserved.
 */

/*
 * This strap is actually PCIECORE::LC_CPM_CONTROL_1[RCVR_DET_EN_HANDSHAKE_DIS].
 * Note, this was the same value that was previously
 * TURIN_STRAP_PCIE_PORT_ORDER_EN and goes by the corresponding AGESA name still
 * even in Turin. We have renamed it to reflect what it is actually doing. It is
 * still a 1 bit value that defaults to 0.
 */
#define	TURIN_STRAP_PCIE_RCVRDET_HANDSHAKE	0x53

/*
 * See PCIEPORT::PCIE_RX_CNTL[RX_IGNORE_AT_ERR].  The next several entries are
 * all about ignoring certain kinds of errors that can be detected on the
 * receive side. These all default to 0x0, indicating that we do *not* ignore
 * the error, which is what we want. There are three straps in this group that
 * follow it which don't seem to relate as obviously.
 */
#define	TURIN_STRAP_PCIE_IGN_RX_IO_ERR		0x54
#define	TURIN_STRAP_PCIE_IGN_RX_BE_ERR		0x55
#define	TURIN_STRAP_PCIE_IGN_RX_MSG_ERR		0x56
#define	TURIN_STRAP_PCIE_IGN_RX_CFG_ERR		0x58
#define	TURIN_STRAP_PCIE_IGN_RX_CPL_ERR		0x59
#define	TURIN_STRAP_PCIE_IGN_RX_EP_ERR		0x5a
#define	TURIN_STRAP_PCIE_IGN_RX_BAD_LEN_ERR	0x5b
#define	TURIN_STRAP_PCIE_IGN_RX_MAX_PAYLOAD_ERR	0x5c
#define	TURIN_STRAP_PCIE_IGN_RX_TC_ERR		0x5d
#define	TURIN_STRAP_PCIE_IGN_RX_AT_ERR		0x60

/*
 * These three straps are interleaved in the group above and are mysterious.
 * They don't immediately relate to PCIE_RX_CNTL or any other similar registers;
 * however, there are some similarities in spirit. Each of these is a 1 bit
 * value and defaults to zero.
 */
#define	TURIN_STRAP_PCIE_TXPL_COMP_DROP		0x57
#define	TURIN_STRAP_PCIE_IGN_CTRL_PARITY	0x5e
#define	TURIN_STRAP_PCIE_IGN_SKP_MARGIN		0x5f

/*
 * 0x61 is reserved
 */

/*
 * Unlike the others, this seems to be a massive error reporting disable switch.
 * We want this to be zero at all costs, which is thankfully the default.  This
 * doesn't correspond to any documented register or feature and is distinct from
 * AER capability enabling via TURIN_STRAP_PCIE_AER_EN.
 */
#define	TURIN_STRAP_PCIE_ERR_REPORT_DIS		0x62

/*
 * This controls whether or not completer abort error reporting is enabled in
 * hardware.  The default for this single-bit strap is 0x1.
 */
#define	TURIN_STRAP_PCIE_CPL_ABORT_ERR_EN	0x63

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_INTERNAL_ERR_EN].  The default for this
 * single-bit strap is 0x1.
 */
#define	TURIN_STRAP_PCIE_INT_ERR_EN		0x64

/*
 * It's not entirely clear what this strap is for; however, if C SKP is really
 * control SKP, then perhaps it's related to that aspect of lane margining
 * somehow (PCIe5 4.2.13), but at the end of the day this is a mystery. It is 1
 * bit and defaults to zero; however, it is set by default by AGESA and is in
 * the same logical position as TURIN_STRAP_PCIE_RXP_ACC_FULL_DIS. Whether they
 * are related or not is hard to say, though they are used in the same manner it
 * appears.
 */
#define	TURIN_STRAP_PCIE_MARGIN_IGN_C_SKP	0x65

/*
 * This is a mysterious 1-bit strap that defaults to 1. Our current guess is
 * that this has to do with a credits pool that relates to SDP ports.
 */
#define	TURIN_STRAP_SDP_OPT_POOL_CREDITS_EN	0x66

/*
 * This strap likely relates to the PCIe Link Capabilities register bit 19:
 * 'Surprise Down Error Reporting Capable'. It's unclear if this merely changes
 * the advertising there or changes parts of the LSSTM ala PCIe5 3.2.1. Likely
 * now a strap due to the use of triggering this as an AER as part of DPC based
 * removals.
 */
#define	TURIN_STRAP_SURPRISE_DOWN_ERR_EN		0x67

/*
 * 0x68 is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_CDR_CNTL.  The single-bit strap CDR_MODE_FORCE defaults
 * to 0; if set, the contents of that register as set by the other straps take
 * effect.  This is used for testing and validation of the CDR subsystem and
 * should not be set by normal software.  The first three fields are of the same
 * widths as the corresponding fields in the documented register and their
 * defaults are those found in the PPR.
 */
#define	TURIN_STRAP_PCIE_CDR_TEST_OFF		0x69
#define	TURIN_STRAP_PCIE_CDR_TEST_SETS		0x6a
#define	TURIN_STRAP_PCIE_CDR_TYPE		0x6b
#define	TURIN_STRAP_PCIE_CDR_MODE_FORCE		0x6c

/*
 * 0x6d is reserved.
 */

/*
 * See PCIECORE::PCIE_STRAP_PI; this is used for validation and shouldn't be
 * done by normal software.  Both single-bit fields default to 0.
 */
#define	TURIN_STRAP_PCIE_TEST_TOGGLE		0x6e
#define	TURIN_STRAP_PCIE_TEST_PATTERN		0x6f

/*
 * This one is just a generic transmit test bit. It is 2 bits wide and defaults
 * to 0x0. Not sure what this controls exactly; it corresponds to no documented
 * register.
 */
#define	TURIN_STRAP_PCIE_TX_TEST_ALL		0x70

/*
 * Overwrite the advertised vendor id for the host bridges in this core! The
 * default is unsurprisingly 0x1022.  This is 16 bits wide.  For this and
 * following straps, see PCIe5 7.5.1.
 */
#define	TURIN_STRAP_PCIE_VENDOR_ID		0x71

/*
 * Set the base and sub class code. This is 0x6 and 0x4 as expected. Each of
 * these is 8 bits wide. These are what are advertised in configuration space
 * for host bridges in this core.
 */
#define	TURIN_STRAP_PCIE_BASE_CLASS		0x72
#define	TURIN_STRAP_PCIE_SUB_CLASS		0x73

/*
 * These two bits control the upper and lower nibble of the configuration space
 * revision ID for each host bridge. This defaults to 0x0. Each of these is 4
 * bits wide.
 */
#define	TURIN_STRAP_PCIE_REV_ID_UPPER		0x74
#define	TURIN_STRAP_PCIE_REV_ID_LOWER		0x75

/*
 * 0x76 is reserved.
 */

/*
 * See PCIECORE::PCIE_STRAP_I2C_BD.  This 7-bit strap defaults to 0x8 and is
 * apparently used to set the core's I2C slave address (but this strap does not
 * enable the port; see TURIN_STRAP_PCIE_I2C_DBG_EN above).  This is presumably
 * used for debugging; we don't use it.
 */
#define	TURIN_STRAP_PCIE_I2C_TARG_ADDR		0x77

/*
 * This is a 10-bit strap that defaults to zero. This is now what enables
 * margining as a whole, that is TURIN_STRAP_PCIE_LANE_MARGIN_EN above. 10 bits
 * does not really correspond to any of the PCIE Core Margining related
 * registers either.
 */
#define	TURIN_STRAP_PCIE_MARGIN_CTL		0x78

/*
 * 0x79 is a reserved control related to i2c.
 */

/*
 * One might think this controls either whether link bandwidth notification is
 * advertised in the otherwise read-only Link Capabilities register, or perhaps
 * the default value of the Link Autonomous Bandwidth Interrupt Enable field in
 * the Link Control register (see PCIe5 7.5.3.6).  Empirically, it does neither,
 * and if it does anything we know not what.
 */
#define	TURIN_STRAP_PCIE_LINK_AUTO_BW_INT	0x7a

/*
 * 0x7b is reserved.
 */

/*
 * This next set of straps all control whether PCIe access control services is
 * turned on and various aspects of it. These all default to being disabled.
 * See PCIe5 7.7.8; these control whether this capability exists and the
 * otherwise read-only fields in it.
 */
#define	TURIN_STRAP_PCIE_ACS_EN			0x7c
#define	TURIN_STRAP_PCIE_ACS_SRC_VALID		0x7d
#define	TURIN_STRAP_PCIE_ACS_TRANS_BLOCK	0x7e
#define	TURIN_STRAP_PCIE_ACS_DIRECT_TRANS_P2P	0x7f
#define	TURIN_STRAP_PCIE_ACS_P2P_CPL_REDIR	0x80
#define	TURIN_STRAP_PCIE_ACS_P2P_REQ_RDIR	0x81
#define	TURIN_STRAP_PCIE_ACS_UPSTREAM_FWD	0x82

/*
 * See PCIECORE::PCIE_SDP_CTRL[SDP_UNIT_ID] and
 * PCIECORE::PCIE_SDP_CTRL[SDP_UNIT_ID_LOWER].  This is a 7-bit field that
 * defaults to 0x10.  Note that we program these fields directly from SW.
 */
#define	TURIN_STRAP_PCIE_SDP_UNIT_ID		0x83

/*
 * See PCIECORE::PCIE_TX_CTRL_4[TX_PORT_ACCESS_TIMER_SKEW]. This is a 4 bit
 * field and defaults to 1.
 */
#define	TURIN_STRAP_PCIE_TX_PAT_SKEW		0x84

/*
 * Strap 0x85 is reserved for ACS.
 * Strap 0x86 is reserved for PM (power management).
 */

/*
 * This strap sets the otherwise read-only PCIERCCFG::PMI_CAP[PME_SUPPORT] field
 * corresponding to the PME_Support field of the Power Management Capabilities
 * register.  See PCIe5 7.5.2.1.  This is a 5-bit field that defaults to 0x19.
 */
#define	TURIN_STRAP_PCIE_PME_SUP		0x87

/*
 * This is a 16-bit value. We're not sure what it does, but the name suggests
 * that it changes the number of lanes that are available. It defaults to zero.
 */
#define	TURIN_STRAP_PCIE_REDUCE_LANES		0x88

/*
 * An unknown 1 bit strap that defaults to 0. Nominally controls items related
 * to resets. Unused by x86.
 */
#define	TURIN_STRAP_PCIE_ISO_RST		0x89

/*
 * This strap appears to relate to the way the RSMU leverages the LTR interrupt
 * for some uses. This appears to be related to 'PD' purposes. Unclear. 1 bit
 * strap that defaults to zero.
 */
#define	TURIN_STRAP_PCIE_RSMU_LTR		0x8a

/*
 * This is another mysterious strap. It's supposedly for allowing a slow clock
 * to see certain reset behavior before allowing traffic, perhaps in a power
 * gating scenario. This 1 bit strap defaults to zero.
 */
#define	TURIN_STRAP_PCIE_SLOW_CLOCK_RST		0x8b

/*
 * Strap 0x8c is reserved for PM again.
 */

/*
 * This strap is used to seemingly disable all Gen 3 features. This was used in
 * Milan when trying to use the special BMC lanes. It seems that with the
 * formalization of the bonus lanes for more than the BMC this is no longer
 * used. The default is 0x0, Gen 3 is enabled. See additional notes on Milan's
 * PCIE_GEN4_DIS in <sys/io/milan/pcie_rsmu.h>.
 */
#define	TURIN_STRAP_PCIE_GEN3_DIS		0x84

/*
 * This is used to control whether or not multicast is supported on the core's
 * host bridges.  If this single-bit field, which defaults to 0, is set, the
 * multicast capability will be advertised.  See PCIe5 7.9.11.
 */
#define	TURIN_STRAP_PCIE_MCAST_EN		0x8e

/*
 * These next two control whether or not we have AtomicOp completion and
 * AtomicOp routing support in the RC; this is propagated to the Device
 * Capabilities 2 registers in all host bridges in this RC.  See
 * PCIECORE::PCIE_STRAP_F0[STRAP_F0_ATOMIC_ROUTING_EN] and [STRAP_F0_ATOMIC_EN]
 * along with PCIe5 7.5.3.15.  Note that 64-bit AtomicOp Completer is not
 * supported by hardware and the corresponding bit is fixed at 0, so there is no
 * corresponding strap.  The default for each is 0x0.
 */
#define	TURIN_STRAP_PCIE_F0_ATOMIC_EN		0x8f
#define	TURIN_STRAP_PCIE_F0_ATOMIC_ROUTE_EN	0x90

/*
 * This controls the number of MSIs requested by the bridge in the otherwise
 * read-only MSI capability field. This is a 3-bit field and defaults to 0x0,
 * indicating 1 interrupt; see PCIe5 7.7.1.2.  While it's possible to get more
 * interrupts, the PCIe capability and AER RE status register fields that
 * indicate which interrupt is used for reporting various conditions are all
 * fixed at 0, so there's no use for the additional vectors.
 */
#define	TURIN_STRAP_PCIE_MSI_MULTI_MSG_CAP	0x91

/*
 * This controls whether or not the primary root complex advertises the 'No
 * RO-enabled PR-PR Passing' bit of the Device Capabilities 2 register, which is
 * related to relaxed ordering of peer-to-peer posted requests. See PCIe5
 * 7.5.3.15 and PCIECORE::PCIE_STRAP_F0[STRAP_F0_NO_RO_ENABLED_P2P_PASSING].
 * The default appears to be 0x0, suggesting it does not advertise this bit and,
 * therefore, relaxed-ordering of peer-to-peer posted requests is allowed.
 */
#define	TURIN_STRAP_PCIE_F0_NO_RO_PR_PR_PASS	0x92

/*
 * See PCIECORE::PCIE_STRAP_F0[STRAP_F0_MSI_MAP_EN].  This single-bit field
 * defaults to 1.
 */
#define	TURIN_STRAP_PCIE_MSI_MAP_EN		0x93

/*
 * This single-bit field defaults to 0.  Its semantics are unknown; phy
 * calibration reset detection is discussed in PCIECORE::SWRST_COMMAND_STATUS.
 * Note that while this register contains fields that are supposedly specific to
 * SWDS or SWUS applications, empirical evidence indicates that they are active
 * on root ports as well.  There is no documented place where this strap, as a
 * control value, would end up.
 */
#define	TURIN_STRAP_PCIE_PHY_CALIB_RESET	0x94

/*
 * Most likely PCIECORE::SWRST_EP_CONTROL_0[EP_CFG_RESET_ONLY_EN].  This
 * single-bit field defaults to 0, and the above register is applicable only to
 * endpoints which in our machines this logic never supports.  Actual semantics
 * are unknown.
 */
#define	TURIN_STRAP_PCIE_CFG_REG_RST_ONLY	0x95

/*
 * Most likely PCIECORE::SWRST_EP_CONTROL_0[EP_LNKDWN_RESET_EN].  This
 * single-bit field defaults to 0, and the above register is applicable only to
 * endpoints which in our machines this logic never supports.  Actual semantics
 * are unknown.
 */
#define	TURIN_STRAP_PCIE_LINK_DOWN_RST_EN	0x96

/*
 * This strap is used to seemingly disable all Gen 4 features. See discussion of
 * GEN3_DIS above.
 */
#define	TURIN_STRAP_PCIE_GEN4_DIS		0x97

/*
 * This is a power-gating mechanism related to the next two straps, but exactly
 * what it controls isn't known (consider PCIECORE::PCIE_PGMST_CNTL[CFG_PG_EN]).
 * It's a single bit and defaults to 0x0.
 */
#define	TURIN_STRAP_PCIE_STATIC_PG_EN		0x98

/*
 * See PCIECORE::PCIE_PGMST_CNTL[CFG_FW_PG_EXIT_CNTL].  This 2-bit field
 * defaults to 0x0.
 */
#define	TURIN_STRAP_PCIE_FW_PG_EXIT_CTL		0x99

/*
 * This presumably relates to the previous two clock gating settings in some
 * way, but we don't know what livmin is (see
 * PCIECORE::CPM_CONTROL[PCIE_CORE_IDLE]). This single-bit strap defaults to
 * 0x0.
 */
#define	TURIN_STRAP_PCIE_LIVMIN_EXIT_CTL	0x9a

/*
 * This strap is used to seemingly disable all Gen 5 features. See discussion of
 * GEN3_DIS above.
 */
#define	TURIN_STRAP_PCIE_GEN5_DIS		0x9b

/*
 * Likely controls the AER Receiver Overflow related features.
 */
#define	TURIN_STRAP_PCIE_AER_RCVR_OVFLW		0x9c

/*
 * These control the Device Readiness State and Function Readiness State
 * messages. See PCIe5 2.2.8.6.3 and 2.2.8.6.4 respectively. These have
 * corresponding bits and overrides in PCIEPORT::PCIE_STRAP_RX_TILE1. FRS
 * queueing likely controls both the hardware support (PCIe5 6.23.3) and the
 * corresponding FRS Queueing Extended configuration space capability (0x21).
 * The above are all 1 bit values that default to 0.
 *
 * In the same PCIECORE register the FRS queue depth controls the reported FRS
 * queue max depth in the capability register. This is a 2 bit value which
 * defaults to 0x3 meaning that the maximum depth is reported.
 */
#define	TURIN_STRAP_PCIE_DRS_SUP_EN		0x9d
#define	TURIN_STRAP_PCIE_FRS_SUP_EN		0x9e
#define	TURIN_STRAP_PCIE_FRS_QUEUE_EN		0x9f
#define	TURIN_STRAP_PCIE_FRS_DEPTH		0xa0

/*
 * Controls features related to the Readiness Time Reporting Extended Capability
 * (0x22), PCIe5 7.9.17. These strap values are also reflected and controlled in
 * PCIEPORT::PCIE_STRAP_RX_TILE1. The first two straps are 1 bit and default to
 * 0. The latter is 2 bits and defaults to 0x3.
 */
#define	TURIN_STRAP_PCIE_RTR_EN			0xa1
#define	TURIN_STRAP_PCIE_RTR_IR_EN		0xa2
#define	TURIN_STRAP_PCIE_RTR_RST_TIME		0xa3

/*
 * See PCIECORE::CPM_CONTROL_EXT2[PCIE_LCLK_DEEPSLEEP]. Appears to control how
 * the LCLK (PCIe Link Frequency Clock) controls some sleep settings.
 */
#define	TURIN_STRAP_PCIE_LCLK_SLEEP		0xa4

/*
 * Relates to whether the Extended Message Data Register for MSIs is supported.
 * 1 bit strap that defaults to zero.
 */
#define	TURIN_STRAP_PCIE_MSI_EXT_DATA_EN	0xa5

/*
 * Controls whether or not Completion Timeout logging is advertised in the AER
 * capabilities register. This defaults to 0, 1 bit. See
 * PCIECORE::PCIE_STRAP_MISC2[STRAP_F0_CTO_LOG_CAPABLE].
 */
#define	TURIN_STRAP_PCIE_AER_CTO_LOG_EN		0xa6

/*
 * 0xa7 is reserved.
 */

/*
 * Enables support for the system firmware intermediary. See PCIe5 6.7.4 and
 * PCIe5 7.9.23 for the SFI Extended Capability (0x2c). 1 bit value defaults to
 * 0. Note, there is also a per-port control for this,
 * TURIN_STRAP_PCIE_P_SFI_EN.
 */
#define	TURIN_STRAP_PCIE_SFI_EN			0xa8

/*
 * Controls the valid bit in the Readiness Time Reporting 1 register in the RTR
 * Capability (PCIe5 7.9.17.2). See also
 * PCIEPORT::PCIE_STRAP_RX_TILE1[STRAP_RTR_VALID]. Defaults to 0x1, 1 bit. This
 * also requires that RTR_EN be set otherwise it will not do anything in theory.
 *
 * The next bit controls whether immediate readiness bit is set in presumably
 * the device 0 status register It defaults to 0.
 */
#define	TURIN_STRAP_PCIE_RTR_VALID_EN		0xa9
#define	TURIN_STRAP_PCIE_RTR_IR_D0_EN		0xaa

/*
 * The following straps are related to the Integrity and Device Encryption (IDE)
 * and Data Object Exchange (DOE). These are formally part of PCIe6; however,
 * there are ECNs for them in PCIe 5. These are all 1 bit straps that default to
 * 0. See also PCIECORE::PRIV_PCIE_IDE_CAP and
 * PCIECORE::PCIE_STRAP_MISC[STRAP_DOE_EN].
 *
 * The latter two straps control bits in the IDE capability register,
 * specifically bits 5 and 7 respectively (PCIe6 7.9.26.2).
 */
#define	TURIN_STRAP_PCIE_DOE_EN			0xab
/*
 * 0xac is reserved.
 */
#define	TURIN_STRAP_PCIE_IDE_EN			0xad
#define	TURIN_STRAP_PCIE_IDE_PCRC_SUP_DIS	0xae
#define	TURIN_STRAP_PCIE_IDE_SEL_CFG_SUP	0xaf

/*
 * 0xb1 is reserved
 */

/*
 * The next large set of straps all relate to the use of CCIX and AMD's CCIX
 * Enhanced Speed Mode, their CCIX/PCIe extension. The first here controls
 * whether support is advertised at all; it's a single bit that defaults to 0
 * which is where we'll leave it because we don't support this ever.
 */
#define	TURIN_STRAP_PCIE_CCIX_ESM_SUP		0xb2

/*
 * See PCIEPORT::PCIEP_STRAP_LC2[STRAP_ESM_PHY_REACH_LEN_CAP]. This defaults to
 * 0x0 and is 2 bits wide.
 */
#define	TURIN_STRAP_PCIE_CCIX_ESM_PHY_REACH_CAP	0xb3

/*
 * This controls whether or not a recalibrate is needed and defaults to 0x0.
 * See PCIEPORT::PCIEP_STRAP_LC2[STRAP_ESM_RECAL_NEEDED].
 */
#define	TURIN_STRAP_PCIE_CCIX_ESM_RECALIBRATE	0xb4

/*
 * These next several all relate to calibration time and timeouts. Each field is
 * 3 bits wide and defaults to 0.  See PCIEPORT::PCIEP_STRAP_LC2.
 */
#define	TURIN_STRAP_PCIE_CCIX_ESM_CALIB_TIME	0xb5
#define	TURIN_STRAP_PCIE_CCIX_ESM_QUICK_EQ_TO	0xb6
#define	TURIN_STRAP_PCIE_CCIX_ESM_EQ_PHASE2_TO	0xb7
#define	TURIN_STRAP_PCIE_CCIX_ESM_EQ_PHASE3_TO	0xb8

/*
 * These control the upstream and downstream tx equalization presets. These are
 * both 4 bit fields and default to 0xf.  See
 * PCIERCCFG::ESM_LANE_EQUALIZATION_CNTL_20GT.
 */
#define	TURIN_STRAP_PCIE_CCIX_ESM_DSP_20GT_EQ_TX	0xb9
#define	TURIN_STRAP_PCIE_CCIX_ESM_USP_20GT_EQ_TX	0xba

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_CCIX_OPT_TLP_FMT_SUPPORT].  Likely
 * applies to all ports in the core we're strapping, but if CCIX were in use
 * that presumably means there's only one.
 */
#define	TURIN_STRAP_PCIE_CCIX_OPT_TLP_FMT_SUP	0xbb

/*
 * 0xbc is reserved.
 */

/*
 * This controls the CCIX vendor ID level value. This 16-bit field defaults to
 * 0x1E2C.
 */
#define	TURIN_STRAP_PCIE_CCIX_VENDOR_ID		0xbd

/*
 * 25 GT/s variant of the 20GT above. These are 4 bit fields that default to
 * 0xf.
 */
#define	TURIN_STRAP_PCIE_CCIX_ESM_DSP_25GT_EQ_TX	0xbe
#define	TURIN_STRAP_PCIE_CCIX_ESM_USP_25GT_EQ_TX	0xbf

/*
 * 0xc0 is reserved.
 */

/*
 * CXL and PCIe SMN Aperture base registers. These are 12-bit values and default
 * to 0x800 and 0x1a3 respectively.
 */
#define	TURIN_STRAP_PCIE_CXL_SMN_BASE		0xc1
#define	TURIN_STRAP_PCIE_PCIE_SMN_BASE		0xc2

/*
 * This seems to to change something about where the above SMN Apertures come
 * from. In particular, the default value (0x1) is to use something other than
 * the straps.
 */
#define	TURIN_STRAP_PCIE_SMN_BASE_SRC		0xc3

/*
 * The following straps control CXL related capabilities. The first two enable
 * the designated vendor-specific (DVSEC) capabilities for CXL 1.0 and CXL 2.0.
 * These are 1 bit straps that default to 0. The next controls the CXL mode. The
 * meaning of this is unknown; however, the 4 bit value is written as 0xF by
 * software, but it defaults to 0. The CXL VID controls the vendor ID that is
 * advertised in the DVSEC. The default is 0x1e98 which is the vendor ID used by
 * the CXL consortium.
 */
#define	TURIN_STRAP_PCIE_CXL1_EN		0xc4
#define	TURIN_STRAP_PCIE_CXL2_EN		0xc5
#define	TURIN_STRAP_PCIE_CXL_MODE		0xc6
/*
 * 0xc7 is reserved for CXL.
 */
#define	TURIN_STRAP_PCIE_CXL_VID		0xc8
/*
 * 0xc9 is reserved for CXL.
 */

/*
 * It's not known whether this 32-bit strap that defaults to 0 corresponds to
 * PCIECORE::PCIE_HW_DEBUG, some other documented register, or something else
 * entirely.  This strap isn't used, but we do set some of these registers from
 * software.
 */
#define	TURIN_STRAP_PCIE_PI_HW_DEBUG		0xca

/*
 * These next two are used to advertise values in the standard device serial
 * number capability for the host bridges in this core.  See PCIe5 7.9.3.  The
 * default values of these two 32-bit fields are 0xc8700 (MSB) and 1 (LSB).
 * Note that the use of non-unique values violates the standard.
 */
#define	TURIN_STRAP_PCIE_SN_LSB			0xcb
#define	TURIN_STRAP_PCIE_SN_MSB			0xcc

/*
 * These next two straps control our the subsystem vendor and device IDs for the
 * host bridge functions that will be constructed within this core. These are
 * each 16 bits wide and default to 1022,1234, though various PCDs generally
 * overwrite this.
 */
#define	TURIN_STRAP_PCIE_SUBVID			0xcd
#define	TURIN_STRAP_PCIE_SUBDID			0xce

/*
 * Downstream and upstream RX lane equalization control preset hint. While this
 * is a lane setting, the same preset is used for all lanes in the core. This
 * applies to Gen 3 and defaults to 0x3 for downstream, 0x0 for upstream, 3 bits
 * wide.  See PCIe5 7.7.3.4, and note that these apply only to the 8 GT/s EQ
 * procedure; hints don't apply to the 16 GT/s EQ scheme.
 */
#define	TURIN_STRAP_PCIE_EQ_DS_RX_PRESET_HINT	0xcf
#define	TURIN_STRAP_PCIE_EQ_US_RX_PRESET_HINT	0xd0

/*
 * Gen3 (8 GT/s) transmitter EQ settings for ports used as DS and US
 * respectively. These are 4 bits wide and use the PCIE_TX_PRESET_* encodings in
 * <sys/pcie.h> that are shared with Gen 4 and 5.  Defaults to 0x1 for US and
 * 0x3 for DS.  These establish the values in the Lane Equalization Control
 * registers for all lanes in this core; see PCIe5 7.7.3.4.
 */
#define	TURIN_STRAP_PCIE_EQ_DS_TX_PRESET	0xd1
#define	TURIN_STRAP_PCIE_EQ_US_TX_PRESET	0xd2

/*
 * 16.0 GT/s TX EQ presets. 4 bits wide and defaults to 0x3 for downstream and
 * 0x1 for upstream, establishing the values in the 16 GT/s Lane Equalization
 * Control registers for all lanes in this core.  See PCIe5 7.7.5.9.
 */
#define	TURIN_STRAP_PCIE_16GT_EQ_DS_TX_PRESET	0xd3
#define	TURIN_STRAP_PCIE_16GT_EQ_US_TX_PRESET	0xd4

/*
 * 32.0 GT/s TX EQ presets. 4 bits wide and defaults to 0x3 for downstream and
 * 0x1 for upstream, establishing the values in the 32 GT/s Lane Equalization
 * Control registers for all lanes in this core.  See PCIe5 7.7.6.9.
 */
#define	TURIN_STRAP_PCIE_32GT_EQ_DS_TX_PRESET	0xd5
#define	TURIN_STRAP_PCIE_32GT_EQ_US_TX_PRESET	0xd6

/*
 * 0xd7 is reserved.
 */

/*
 * This seems to control something called 'quicksim', mysterious. Default is
 * 0x0.  This seems to be meant for validation.  Some clues can be found here;
 * https://www.amd.com/system/files/TechDocs/48692.pdf.  See also
 * PCIECORE::PCIE_STRAP_PI[STRAP_QUICKSIM_START].
 */
#define	TURIN_STRAP_PCIE_QUICKSIM_START		0xd8

/*
 * This is documented as a 31-bit field with a default value of 0; the
 * individual fields it contains are not documented and the only other value we
 * know about is 0x200 which we surmise is to enable the subsystem capability to
 * show up in configuration space.
 */
#define	TURIN_STRAP_PCIE_WRP_MISC		0xd9
#define	TURIN_STRAP_PCIE_WRP_MISC_SSID_EN	0x200

/*
 * 0xda-0xdd are reserved.
 */

/*
 * This next set all control various ESM speeds it seems. These all default to
 * 0x1 and are 1 bit wide with the exception of the minimum time in electrical
 * idle which is a 9 bit field and defaults to 0x0 and sets the value of
 * PCIERCCFG::PCIE_ESM_STATUS[MIN_TIME_IN_EI_VAL].  The other bits correspond to
 * fields in the CCIX ESM capability registers; the PPR documents these but
 * presumably the real documentation is found only in the CCIX specs.
 */
#define	TURIN_STRAP_PCIE_ESM_12P6_12P8		0xde
#define	TURIN_STRAP_PCIE_ESM_12P1_12P5		0xdf
#define	TURIN_STRAP_PCIE_ESM_11P1_12P0		0xe0
#define	TURIN_STRAP_PCIE_ESM_9P6_11P0		0xe1
#define	TURIN_STRAP_PCIE_ESM_MIN_EI_TIME	0xe2
#define	TURIN_STRAP_PCIE_ESM_16P0		0xe3
#define	TURIN_STRAP_PCIE_ESM_17P0		0xe4
#define	TURIN_STRAP_PCIE_ESM_18P0		0xe5
#define	TURIN_STRAP_PCIE_ESM_19P0		0xe6
#define	TURIN_STRAP_PCIE_ESM_20P0		0xe7
#define	TURIN_STRAP_PCIE_ESM_21P0		0xe8
#define	TURIN_STRAP_PCIE_ESM_22P0		0xe9
#define	TURIN_STRAP_PCIE_ESM_23P0		0xea
#define	TURIN_STRAP_PCIE_ESM_24P0		0xeb
#define	TURIN_STRAP_PCIE_ESM_25P0		0xec

/*
 * 0xed is reserved.
 */

/*
 * These are duplicates of a number of other straps that we've already seen, but
 * applicable to the SWUS functionality which is not found on any machine
 * supported by this kernel.  These all seem to have the same sizes and defaults
 * values as the non-SWUS variant.
 */
#define	TURIN_STRAP_PCIE_SWUS_MSI_EN		0xee
#define	TURIN_STRAP_PCIE_SWUS_VC_EN		0xef
#define	TURIN_STRAP_PCIE_SWUS_DSN_EN		0xf0
#define	TURIN_STRAP_PCIE_SWUS_AER_EN		0xf1
#define	TURIN_STRAP_PCIE_SWUS_ECRC_CHECK_EN	0xf2
#define	TURIN_STRAP_PCIE_SWUS_ECRC_GEN_EN	0xf3
#define	TURIN_STRAP_PCIE_SWUS_CPL_ABORT_ERR_EN	0xf4
#define	TURIN_STRAP_PCIE_SWUS_F0_ATOMIC_EN	0xf5
#define	TURIN_STRAP_PCIE_SWUS_F0_ATOMIC_ROUTE_EN	0xf6
#define	TURIN_STRAP_PCIE_SWUS_F0_NO_RO_PR_PR_PASS	0xf7
#define	TURIN_STRAP_PCIE_SWUS_ERR_REPORT_DIS	0xf8
#define	TURIN_STRAP_PCIE_SWUS_NO_SOFT_RST	0xf9
#define	TURIN_STRAP_PCIE_SWUS_POWER_D2_SUP	0xfa
#define	TURIN_STRAP_PCIE_SWUS_POWER_D1_SUP	0xfb
#define	TURIN_STRAP_PCIE_SWUS_LTR_SUP		0xfc
#define	TURIN_STRAP_PCIE_SWUS_ARI_EN		0xfd
#define	TURIN_STRAP_PCIE_SWUS_SUBVID		0xfe
#define	TURIN_STRAP_PCIE_SWUS_SUB_CLASS		0xff
#define	TURIN_STRAP_PCIE_SWUS_BASE_CLASS	0x100
#define	TURIN_STRAP_PCIE_SWUS_REV_ID_UPPER	0x101
#define	TURIN_STRAP_PCIE_SWUS_REV_ID_LOWER	0x102
#define	TURIN_STRAP_PCIE_SWUS_PME_SUP		0x103
#define	TURIN_STRAP_PCIE_SWUS_OBFF_SUP		0x104
/* 0x105 is below as it isn't part of the SWUS sets */
#define	TURIN_STRAP_PCIE_SWUS_SSID_EN		0x106

/*
 * This strap seems to control whether or not flow control is checked prior to
 * going to the L1 PCIe state is disabled. See
 * PCIECORE::PCIE_TX_CTRL_3[TX_CHK_FC_FOR_L1_DIS]. Defaults to 0, meaning the
 * check is enabled.
 */
#define	TURIN_STRAP_PCIE_FC_L1_DIS		0x105

/*
 * 0x107 is reserved.
 */

/*
 * At this point all of our PCIe straps are now changed to be per port.
 * Each of the 9 possible ports all have the same set of straps; however, each
 * one is 0xc2 off from one another (or put differently there are 0xc2 straps
 * per port).
 */
#define	TURIN_STRAP_PCIE_NUM_PER_PORT		0xc2

/*
 * The relationship between this strap and
 * PCIECORE::SWRST_CONTROL_6[HOLD_TRAINING_x] is not entirely clear.  While the
 * POR value of those bits is 1, the default value of this strap is ostensibly
 * 0.  It's not necessary to set this strap in order for training to be held by
 * default, and it's not known whether setting it causes firmware not to release
 * the HOLD_TRAINING bit automatically during LISM execution.
 */
#define	TURIN_STRAP_PCIE_P_HOLD_TRAINING	0x108

/*
 * We believe that this strap corresponds to
 * PCIEPORT::PCIE_LC_CNTL5[LC_HOLD_TRAINING_MODE]. Unlike our SP3 counterpart
 * this value is 3 bits wide which matches the actual field. The default value
 * here is 0x2 which would correspond to powering down devices.
 */
#define	TURIN_STRAP_PCIE_P_LC_HOLD_TRAINING_MODE	0x109

/*
 * This strap seems to suggest that the port will automatically release hold
 * training at some point. When is not entirely clear and it's not obvious what
 * the corresponding port register is for this. This is 1 bit and defaults to 0,
 * likely meaning that it will not automatically release.
 */
#define	TURIN_STRAP_PCIE_P_HOLD_TRAINING_AUTO_REL	0x10a

/*
 * 0x10b is reserved.
 */

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_AUTO_RC_SPEED_NEGOTIATION_DIS].  This
 * 1-bit strap defaults to 0x0, meaning automatic speed negotiation is enabled
 * on this root port. There are two variants of this for 16.0 (Gen 4) and 32.0
 * (Gen 5) operation which have similar bits in the same register.
 */
#define	TURIN_STRAP_PCIE_P_RC_SPEED_NEG_DIS	0x10c
#define	TURIN_STRAP_PCIE_P_RC_SPEED_NEG_16GT_DIS	0x10d
#define	TURIN_STRAP_PCIE_P_RC_SPEED_NEG_32GT_DIS	0x10e

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_INIT_SPEED_NEG_IN_L{1,0s}_EN].
 * Both these single-bit straps default to 0x1, allowing speed negotiation in L1
 * (and in principle the unsupported L0s).
 */
#define	TURIN_STRAP_PCIE_P_L0s_SPEED_NEG_EN	0x10f
#define	TURIN_STRAP_PCIE_P_L1_SPEED_NEG_EN	0x110

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_TARGET_LINK_SPEED_OVERRIDE_EN].  This
 * strap seems to provide a means of saying that a target speed override should
 * be enabled. This presumably pairs with the target link speed strap below
 * which would contain the actual setting. This single-bit strap defaults to
 * 0x0, disabled.
 */
#define	TURIN_STRAP_PCIE_P_TARG_LINK_SPEED_EN	0x111

/*
 * See PCIEPORT::PCIE_LC_EQ_CNTL_8GT[LC_BYPASS_EQ_{,REQ_PHASE_}8GT].  The
 * default values of these single-bit fields is 0, which leaves the normal gen3
 * EQ process intact.
 */
#define	TURIN_STRAP_PCIE_P_8GT_BYPASS_EQ	0x112
#define	TURIN_STRAP_PCIE_P_8GT_BYPASS_EQ_REQ	0x113

/*
 * See PCIEPORT::PCIE_LC_EQ_CNTL_8GT[LC_EQ_SEARCH_MODE_8GT].  This is a two bit
 * field that defaults to 0x3 (preset search); see PPR for encodings.
 */
#define	TURIN_STRAP_PCIE_P_8GT_EQ_SEARCH_MODE	0x114

/*
 * These next three are the Gen 4 variants of the Gen 3 bits above. They have
 * the same sizes and default values; see corresponding fields in
 * PCIEPORT::PCIE_LC_EQ_CNTL_16GT.
 */
#define	TURIN_STRAP_PCIE_P_16GT_BYPASS_EQ	0x115
#define	TURIN_STRAP_PCIE_P_16GT_BYPASS_EQ_REQ	0x116
#define	TURIN_STRAP_PCIE_P_16GT_EQ_SEARCH_MODE	0x117

/*
 * This strap works in tandem with the TURIN_STRAP_PCIE_P_TARG_LINK_SPEED_EN
 * strap above; see PCIEPORT::PCIE_LC_SPEED_CNTL[LC_TARGET_LINK_SPEED_OVERRIDE]. This
 * controls what the target link speed of the port would be. It is a three bit
 * field that defaults to 0x4 (32.0 GT/s).  See PPR for encodings.
 */
#define	TURIN_STRAP_PCIE_P_TARG_LINK_SPEED	0x118

/*
 * These next three are the Gen 4 variants of the Gen 3 bits above. They have
 * the same sizes and default values; see corresponding fields in
 * PCIEPORT::PCIE_LC_EQ_CNTL_32GT.
 */
#define	TURIN_STRAP_PCIE_P_32GT_BYPASS_EQ	0x119
#define	TURIN_STRAP_PCIE_P_32GT_BYPASS_EQ_REQ	0x11a
#define	TURIN_STRAP_PCIE_P_32GT_EQ_SEARCH_MODE	0x11b

/*
 * These two straps are all settings in PCIEPORT::PCIE_LC_CNTL11 that relate to
 * the equalization bypass to highest rate features. These are both 1 bit values
 * that are enabled by default and correspond to whether it's supported and
 * advertised. See LC_BYPASS_EQ_TO_HIGH_RATE_SUPPORT and
 * LC_ADVERTISE_EQ_TO_HIGH_RATE_SUPPORT.
 */
#define	TURIN_STRAP_PCIE_P_EQ_BYPASS_TO_HR_SUP	0x11c
#define	TURIN_STRAP_PCIE_P_EQ_BYPASS_TO_HR_ADV	0x11d

/*
 * These are two similar straps in the same register as the last two, but they
 * relate to the No Equalization feature needed aka LC_NO_EQ_NEEDED_SUPPORT and
 * LC_ADVERTISE_NO_EQ_NEEDED_SUPPORT. These are 1 bit straps that default to 0.
 */
#define	TURIN_STRAP_PCIE_P_NO_EQ_ADV	0x11e
#define	TURIN_STRAP_PCIE_P_NO_EQ_SUP	0x11f

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_COMP_PATTERN_MAX_SPEED].
 */
#define	TURIN_STRAP_PCIE_P_COMP_SPEED	0x120

/*
 * 0x121 is reserved.
 */

/*
 * See PCIEPORT::LC_CNTL[LC_L{1,0S}_INACTIVITY].  Both 4-bit straps default to
 * 0, which disables entering the corresponding state.  Note that the L1 time is
 * applicable only to upstream ports, which ours never are.
 */
#define	TURIN_STRAP_PCIE_P_L0s_INACTIVITY	0x122
#define	TURIN_STRAP_PCIE_P_L1_INACTIVITY	0x123

/*
 * Controls PCIEPORT::PCIE_LC_CNTL2[LC_RCV_L0_TO_RCV_L0S_DIS]. If set to 0, the
 * current default, then a link is allowed to transition from L0 to L0s on the
 * receive path. Software is expected to change this to 1.
 */
#define	TURIN_STRAP_PCIE_P_L0_TO_L0s_DIS	0x124

/*
 * Controls PCIEPORT::PCIE_LC_CNTL4[LC_GO_TO_RECOVERY_ANY_UNEXPECTED_EIOS]. If
 * an unexpected Electrical Idle Ordered Set (EIOS) comes in while in L0
 * controls whether to go to recovery or not, dependent on other settings. 1 bit
 * value that defaults to 0x1.
 */
#define	TURIN_STRAP_PCIE_P_L0_EIOS_RCVRY	0x125

/*
 * 0x126 is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_CNTL2[LC_ELEC_IDLE_MODE]; the 2-bit field shares
 * encodings with PCIE_PORT_LC_CTL2_ELEC_IDLE_xx and defaults to 1.  Note that
 * we set this from software instead of strapping it.
 */
#define	TURIN_STRAP_PCIE_P_ELEC_IDLE_MODE	0x127

/*
 * See PCIERCCFG::LINK_CAP[PM_SUPPORT], especially the contradictory notes in
 * the PPR, and PCIe5 7.5.3.6.  This is a 2-bit strap, which defaults to 0x3.
 * This strap did not seem to do anything in Milan, we have not yet empirically
 * tested its effects.  See also PCIEPORT::PCIE_LC_CNTL[LC_ASPM_TO_L1_DIS].
 */
#define	TURIN_STRAP_PCIE_P_ASPM_SUP		0x128

/*
 * These next two straps control the defined exit latency values for L0s and L1
 * in the otherwise read-only Link Capabilities register; see PCIe5 7.5.3.6.
 * These 3-bit fields default to 6 for L1 and 3 for L0s.  Note that L0s isn't
 * supported (see previous strap), so the implementation note in PCIe5 5.4.1.1
 * applies and the recommended value is 7 (>4 microseconds); however, our system
 * never has the kind of legacy software that note describes so this is of
 * little importance.
 */
#define	TURIN_STRAP_PCIE_P_L1_EXIT_LAT		0x129
#define	TURIN_STRAP_PCIE_P_L0s_EXIT_LAT		0x12a

/*
 * It isn't exactly clear what this does. Based on the fact that it's in the
 * ASPM group and that it's a 1-bit field that defaults to 0x1, we can wager
 * that this is used to control some amount of signalling when the link exits an
 * L1 state.  It doesn't correspond to any documented register or field, and has
 * no observable effect on any PCIe port registers.
 */
#define	TURIN_STRAP_PCIE_P_L1_EXIT_SIGNAL	0x12b

/*
 * 0x12c is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_BW_CHANGE_CNTL[LC_LINK_BW_NOTIFICATION_DETECT_MODE].
 * This single-bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_LINK_BW_NOTIF_DETECT_MODE	0x12d

/*
 * See PCIEPORT::PCIE_LC_CNTL7[LC_AUTO_REJECT_AFTER_TIMEOUT].  This single-bit
 * strap is documented to default to 1, but whether one leaves it alone or
 * explicitly straps it to 1, firmware clears the bit anyway before the LISM
 * reaches the CONFIGURED state, making the strap effectively useless. While we
 * haven't gotten far enough in Turin to prove this yet, it does happen in
 * Genoa (and Milan).
 */
#define	TURIN_STRAP_PCIE_P_LINK_EQ_DISCARD_AFTER_TIMEOUT	0x12e

/*
 * See PCIEPORT::PCIE_LC_CNTL9[LC_EX_SEARCH_TRAVERSAL_MODE].  This single-bit
 * strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_LINK_EQ_SEARCH_MODE	0x12f

/*
 * This is supposed to control an internal 'refClkReq' which may be related to
 * the optional CEM CLKREQ# pin or, because there are no related pins on the
 * package, related to an internal signal in the port itself. While this refers
 * to PCIEPORT::PCIE_LC_CNTL9[LC_REFCLK_OFF_NO_RCVR_LANES], we cannot find
 * anything that seems to resemble this. Specifically this describes behavior
 * that occurs when lanes are explicitly turned off due to not detecting a
 * receiver. This is a 1 bit strap that defaults to 0x1.
 */
#define	TURIN_STRAP_PCIE_P_DIS_REFCLK_NO_RCVR	0x130

/*
 * See PCIEPORT::PCIE_LC_CNTL9[LC_USE_LONG_SERIAL_QUICKSIM_TIMEOUTS]. This is
 * another simulation special. 1 bit value that defaults to zero.
 */
#define	TURIN_STRAP_PCIE_P_SIM_TO		0x131

/*
 * See the Milan version of this strap for a long history of things this 1 bit
 * strap was thought to have possibly been related to, but never could be
 * proven. Comments still suggest that it is related to the Link Control 2
 * register; however, what this is still a mystery.
 */
#define	TURIN_STRAP_PCIE_P_DEEMPH_SEL		0x132

/*
 * These next two straps are used to control the indication of retimer presence
 * detection support. These show up in the Link Capabilities 2 register; see
 * PCIe5 7.5.3.18 and PCIEPORT::PCIEP_STRAP_LC[STRAP_RTM{1,2}_PRESENCE_DET_SUP],
 * which are controlled by these straps.  The default for both of these
 * single-bit straps is 0, meaning that the capability of detecting retimers is
 * not advertised.  Curiously, however, even with these bits clear, both are set
 * in the standard capability register for the root port despite the PPR's claim
 * that the read-only capability bits are always 0.  It's unclear whether the
 * PCIe core provides this support or external logic would be needed for it to
 * work (i.e., whether this does anything other than simply advertise the
 * capability).  Software in the Turin generation straps the first, but not the
 * second.
 */
#define	TURIN_STRAP_PCIE_P_RETIMER1_DET_SUP	0x133
#define	TURIN_STRAP_PCIE_P_RETIMER2_DET_SUP	0x134

/*
 * Allows changing the timeout values used by the LTSSM, primarily for
 * simulation or validation.  See PCIEPORT::PCIE_LC_CNTL2[LC_TEST_TIMER_SEL].
 * This 2-bit field defaults to 0, which codes as PCIe-compliant values.  Don't
 * touch.
 */
#define	TURIN_STRAP_PCIE_P_TEST_TIMER_SEL	0x135

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_MARGINING_USES_SOFTWARE].  This single-bit
 * strap defaults to 0, meaning that software (whatever it would do!) is
 * apparently not needed for RX margining.  We don't know what happens if one
 * sets this, so leave it alone.
 */
#define	TURIN_STRAP_PCIE_P_MARGIN_NEEDS_SW	0x136

/*
 * This strap controls hardware autonomous disabling of higher-speed support,
 * specifically via PCIEPORT::PCIE_LC_CNTL3[LC_AUTO_DISABLE_SPEED_SUPPORT_EN].
 * The default for this single-bit strap is 0, which means that hardware does
 * not disable higher speeds when training failures occur.  However, firmware
 * still does so and there's no documented way to turn off that undesirable
 * behaviour.  The result is that under some conditions, a root port will
 * support only 2.5 GT/s even though it was configured to support other speeds
 * in the DXIO engine definition.  Changing this strap won't help.
 */
#define	TURIN_STRAP_PCIE_P_AUTO_DIS_SPEED_SUP_EN	0x137

/*
 * These per-link-speed straps are each 2 bits wide. 2.5GT and 5.0GT default to
 * 0, while 8 GT defaults to 1, and 16 GT and 32 GT to 2.  These correspond to
 * fields in PCIEPORT::PCIE_LC_CNTL6, which almost certainly should not be
 * changed from the recommended values.
 */
#define	TURIN_STRAP_PCIE_P_SPC_MODE_2P5GT	0x138
#define	TURIN_STRAP_PCIE_P_SPC_MODE_5GT		0x139
#define	TURIN_STRAP_PCIE_P_SPC_MODE_8GT		0x13a
#define	TURIN_STRAP_PCIE_P_SPC_MODE_16GT	0x13b
#define	TURIN_STRAP_PCIE_P_SPC_MODE_32GT	0x13c

/*
 * The next two straps are part of SRIS (separate reference clocks with
 * independent spread spectrum) support on the device. The first controls
 * whether SRIS is force-enabled and the second enables one of several
 * autodetection modes.  Enabling these is mutually exclusive, and both are off
 * by default.  See PCIEPORT::PCIE_LC_CNTL6[LC_SRIS_AUTODETECT_EN, LC_SRIS_EN]
 * and related fields that seemingly need to be set directly by software.  It's
 * possible that this is strappable so that links requiring SRIS can avoid
 * triggering fatal errors when first configured, but otherwise the reason for
 * these straps is unknown.
 */
#define	TURIN_STRAP_PCIE_P_SRIS_EN		0x13d
#define	TURIN_STRAP_PCIE_P_AUTO_SRIS_EN		0x13e

/*
 * Single-bit field controlling PCIEPORT::PCIE_LC_CNTL4[LC_TX_SWING].  The
 * default is 0, full-swing mode for the transmitters on this port.  See PCIe5
 * 8.3.3.10, 4.2.3.1, and ch. 8 generally.
 */
#define	TURIN_STRAP_PCIE_P_TX_SWING		0x13f

/*
 * See PCIEPORT::PCIE_LC_CNTL5[LC_ACCEPT_ALL_PRESETS{,_TEST}].  These default to
 * 0 and are relevant only in reduced swing mode which is not currently used.
 */
#define	TURIN_STRAP_PCIE_P_ACCEPT_PRESETS	0x140
#define	TURIN_STRAP_PCIE_P_ACCEPT_PRESETS_TEST	0x141

/*
 * This controls how long the PHY has for Figure of Merit (FOM). This is a 2-bit
 * field and defaults to 0x0.  See PCIEPORT::PCIE_LC_CNTL8[LC_FOM_TIME].  The
 * FOM is defined by PCIe5 8.5.1.4.2 for 8 and 16 GT/s only as the open eye
 * area; however the minimum eye opening is only 0.1 UI and the total eye width
 * is never going to be even close to 0.5 UI; the UI for 8 GT/s is 125 ps, so
 * the most reasonable interpretation of these values is an interval of time
 * over which the phy is allowed to assess the FOM in evaluating EQ parameters.
 * The actual semantics are undocumented, but see also
 * PCIEPORT::PCIE_LC_TRAINING_CNTL[LC_WAIT_FOR_FOM_VALID_AFTER_TRACK].
 */
#define	TURIN_STRAP_PCIE_P_FOM_TIME		0x142
#define	TURIN_STRAP_PCIE_P_FOM_300US		0
#define	TURIN_STRAP_PCIE_P_FOM_200US		1
#define	TURIN_STRAP_PCIE_P_FOM_100US		2
#define	TURIN_STRAP_PCIE_P_FOM_SUB_100US	3

/*
 * Controls the LC_SAFE_EQ_SEARCH_<speed>GT values in each of the corresponding
 * LC_EQ_CNTL registers, such as
 * PCIEPORT::PCIE_LC_EQ_CNTL_8GT[LC_SAFE_EQ_SEARCH_8GT] and the corresponding
 * 16GT and 32GT variants. This 1 bit strap defaults to 0 and presumably
 * influences all three when set to 1. See also
 * TURIN_STRAP_PCIE_P_{16,32}GT_EQ_SAFE_SEARCH below. The exact interaction
 * between all three sets of straps it not clear right now. It is possible that
 * this may only close 8 GT/s.
 */
#define	TURIN_STRAP_PCIE_P_EQ_SAFE_SEARCH	0x143

/*
 * See PCIEPORT::PCIE_LC_CNTL11[LC_SET_TRANSMITTER_PRECODE_REQUEST]. Controls
 * whether transmit precoding is requested during the 32.0 GT/s link
 * equalization phase or not. 1 bit strap that defaults to 0; however, software
 * is expected to enable this for Gen 5 operation.
 */
#define	TURIN_STRAP_PCIE_P_32GT_PRECODE_REQ	0x144

/*
 * See PCIEPORT::PCIE_LC_CNTL9[LC_REPEAT_RXEQEVAL_AFTER_TIMEOUT]. 1 bit value
 * that defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_EQ_ADAPT_AFTER_TO	0x145

/*
 * This 2 bit strap which defaults to 0x2 indicates the number of consecutive
 * Training Control bits that must match in the TS1 ordered sets (see PCIe5
 * 4.2.4.1) for the device to enter the Training state. See also
 * PCIEPORT::PCIE_LC_CNTL9[LC_TRAINING_BITS_REQUIRED].
 */
#define	TURIN_STRAP_PCIE_P_TRAIN_TS1		0x146

/*
 * See PCIEPORT::PCIE_LC_SAVE_RESTORE_1[LC_SAVE_RESTORE_EN]. Controls where link
 * coefficients come from. 1 bit value that defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_REST_COEFF		0x147

/*
 * These next three straps are all 1 bit straps that default to 0 and correspond
 * to fields in PCIEPORT::PCIE_LC_RXRECOVER_RXSTANDBY_CNTL. These control how
 * the PHY's receiver recovery operates. The first is a general control and the
 * latter control what happens when no TS1 ordered sets are seen in polling and
 * configuration.
 */
#define	TURIN_STRAP_PCIE_P_RXRECOVER_EN		0x148
#define	TURIN_STRAP_PCIE_P_RXRECOVER_POLLACT	0x149
#define	TURIN_STRAP_PCIE_P_RXRECOVER_CFG	0x14a

/*
 * See PCIEPORT::PCIE_LC_CNTL4[LC_DSC_CHECK_COEFFS_IN_RLOCK]. 1 bit strap that
 * defaults to 0x1, meaning that final coefficients are checked after link
 * equalization.
 */
#define	TURIN_STRAP_PCIE_P_CHECK_COEFFS		0x14b

/*
 * See PCIEPORT::PCIE_LC_CNTL4[LC_EXTEND_EIEOS_MODE]. 1 bit strap that defaults
 * to 0. Controls behavior of when TURIN_STRAP_PCIE_P_TS1_EXT_EIEOS takes effec
 * takes effect
 */
#define	TURIN_STRAP_PCIE_P_TS1_EXT_EIEOS_MODE	0x14c

/*
 * See PCIEPORT::PCIE_LC_CNTL9[LC_ALT_RX_EQ_IN_PROGRESS_EN]. 1 bit strap that
 * defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_RXEQ_PROG_EN		0x14d

/*
 * See PCIEPORT::PCIE_LC_CNTL4[LC_EXTEND_EIEOS]. 1 bit strap that defaults
 * to 0. Controls TS1 bit 2.
 */
#define	TURIN_STRAP_PCIE_P_TS1_EXT_EIEOS	0x14e

/*
 * These three straps control the 8 GT/s, 16 GT/s, and 32 GT/s preset conversion
 * of coefficients is performed. There is one register per speed, e.g.
 * PCIEPORT::PCIE_LC_EQ_CNTL_8GT[LC_ALWAYS_PERFORM_GEN3_PRESET_CONVERSION].
 * These are 1 bit values that default to 0.
 */
#define	TURIN_STRAP_PCIE_P_8GT_PRESET_CONV	0x14f
#define	TURIN_STRAP_PCIE_P_16GT_PRESET_CONV	0x150
#define	TURIN_STRAP_PCIE_P_32GT_PRESET_CONV	0x151

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_LOOPBACK_TEST_MODE_RCVRDET]. This likely is
 * just used for test situations (based on the name) as it allows a link to
 * proceed to configuration even if it uses a somewhat invalid lane
 * configuration. 1 bit strap that defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_LBTEST_RCVRDET	0x152

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_LOOPBACK_EQ_LOCK_REVERSAL]. 1 bit strap that
 * defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_LB_EQ_REV		0x153

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_LIVE_DESKEW_MASK_EN]. 1 bit strap that
 * defaults to 1. Causes a deskew mask to be passed along through different
 * phases.
 */
#define	TURIN_STRAP_PCIE_P_DESKEW_MASK_EN	0x154

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_EQ_REQ_PHASE_WAIT_FOR_FINAL_TS1]. This strap
 * likely corresponds to the above register. 1 bit value that defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_EQ_WAIT_FOR_FINAL_TS1	0x155

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_RESET_TSX_CNT_ON_RXEQEVAL]. 1 bit strap that
 * defaults to 0x1.
 */
#define	TURIN_STRAP_PCIE_P_RST_TS_CNT_RXEQ	0x156

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_RESET_TSX_CNT_ON_SAFERECOVER]. 1 bit strap
 * that defaults to 0x1.
 */
#define	TURIN_STRAP_PCIE_P_RST_TS_CNT_SAFERCVR	0x157

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_TRACK_RX_WAIT_FOR_TS1]. 1 bit strap that
 * defaults to 0x1.
 */
#define	TURIN_STRAP_PCIE_P_TRACK_EQ_WAIT_FOR_TS1	0x158

/*
 * See PCIEPORT::PCIE_LC_EQ_CNTL_8GT[LC_ENH_PRESET_SEARCH_SEL_8GT] and the
 * corresponding 16 and 32 GT/s options. 2 bits each, default to 0. This control
 * isn't used unless the TURIN_STRAP_PCIE_P_{8,16,32}GT_EQ_SEARCH_MODE is set to
 * 3.
 */
#define	TURIN_STRAP_PCIE_P_8GT_PRESET_SEARCH_SEL	0x159
#define	TURIN_STRAP_PCIE_P_16GT_PRESET_SEARCH_SEL	0x15a
#define	TURIN_STRAP_PCIE_P_32GT_PRESET_SEARCH_SEL	0x15b

/*
 * 0x15c is reserved.
 */

/*
 * These 10-bit fields default to 0 and correspond to
 * PCIEPORT::PCIE_LC_PRESET_MASK_CNTL[LC_PRESET_MASK_{8,16,32}GT].
 */
#define	TURIN_STRAP_PCIE_P_8GT_PRESET_MASK	0x15d
#define	TURIN_STRAP_PCIE_P_16GT_PRESET_MASK	0x15e
#define	TURIN_STRAP_PCIE_P_32GT_PRESET_MASK	0x15f

/*
 * 0x160 is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_ALTERNATE_PROTOCOL_IGNORE_USAGEMODE_ABORT]. 1 bit strap defaults to 0x1.
 */
#define	TURIN_STRAP_PCIE_P_ALTPROT_IGN_USAGE	0x161

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_MODTS2_NEG_FAILURE_OVERRIDE_EN]. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_BAD_MODTS2_DIS_CXL	0x162

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CNLI_FREQ_ADJ_ACK_TIMEOUT]. 2 bit strap defaults to 0, meaning no timeout.
 * The other values control whether an ack is issued after 2us (1), 10 us (2),
 * or 1 ms (3).
 */
#define	TURIN_STRAP_PCIE_P_CNLI_FREQ_ADJ_ACK_TO	0x163

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_LC_TRANSMIT_MUX_PAD_SMALL_SKID_ENTRIES]
 * and [STRAP_LC_TRANSMIT_MUX_PAD_SMALL_SKID_ENTRIES_PCLK_CHANGE]. These control
 * behaviors about how transmit skid entries behave while in PCIe mode.
 */
#define	TURIN_STRAP_PCIE_P_TX_PAD_SKID		0x164
#define	TURIN_STRAP_PCIE_P_TX_PAD_SKID_PCLK	0x165

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CNLI_FREQ_ADJ_ACK_RLOCK_TIMEOUT]. 3 bit strap defaults to 0, meaning no
 * timeout.
 */
#define	TURIN_STRAP_PCIE_P_CNLI_FREQ_ADJ_ACK_RLOCK_TO	0x166

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_RECOVERY_WAIT_FOR_STATE_SYNC_MODE]. 2 bit strap that defaults to 1
 * indicating that a subset of state transitions do not wait for a CXL State
 * Sync before entering recovery.
 */
#define	TURIN_STRAP_PCIE_P_CXL_SYNC_RCVERY	0x167

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_L1_L2_ENTRY_WAIT_FOR_STATE_SYNC]. 1 bit strap that defaults to 0.
 * Controls if CXL PM state transitions wait for outstanding state
 * synchronization.
 */
#define	TURIN_STRAP_PCIE_P_CXL_L12_SYNC		0x168

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_OVERRIDE_PM_ENABLES]. 1 bit strap that defaults to 1. Indicates
 * certain PCIe power states should be disabled in CXL 1.1
 */
#define	TURIN_STRAP_PCIE_P_CXL_DIS_NONCXL_PM	0x169

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_FREQ_ADJ_ACK_RLOCK_TIMEOUT_DEASSERT_RXWAIT]. 1 bit strap defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_CNLI_TO_RM_RXWAIT	0x16a

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_FREQ_ADJ_WAIT_FOR_INWAKE_RLOCK]. 1 bit strap defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_CNLI_INWAKE_RCVRY_LOCK	0x16b

/*
 * These next two straps are 1 bit and default to 1. They correspond to
 * PCIEPORT::PCIE_LC_TRANMIT_FIFO_CDC_CNTL[LC_TFIFO_CDC_HIDE_EN] and
 * [LC_TFIFO_CDC_HIDE_EARLY_RELEASE].
 */
#define	TURIN_STRAP_PCIE_P_TX_FIFO_HIDE_CDC	0x16c
#define	TURIN_STRAP_PCIE_P_TX_FIFO_HIDE_CD_EARLY_REL	0x16d

/*
 * See PCIEPORT::PCIE_LC_CNTL13[LC_SPEED_CHANGE_COUNT_MODE]. 1 bit strap that
 * defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_SPEED_CHANGE_CNT_MODE	0x16e

/*
 * See PCIEPORT::PCIE_LC_CNTL13[LC_ASSERT_RXSTANDBY_EARLIER_RECOVERY_SPEED]. 1
 * bit strap that defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_RX_STANDBY_ASSERT_SPEED	0x16f

/*
 * See PCIEPORT::PCIE_LC_LTSSM_CXL_CNTL_EXTRA[
 * LC_TEMP_GEN5_ADVERTISEMENT_FOR_ALTPTCL]. 1 bit strap defaults to 1, meaning
 * that if modified TS support is enabled for alternate protocols then 32 GT/s
 * will be initially advertised.
 */
#define	TURIN_STRAP_PCIE_P_ALWAYS_ADV_32GT_FOR_MODTS	0x170

/*
 * 0x171 is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_TRANMIT_FIFO_CDC_CNTL[LC_TFIFO_CDC_HIDE_DELAY]. 6 bit
 * strap that defaults to 3. Units for this delay are unknown, perhaps in
 * certain TX or CDC related clock cycles.
 */
#define	TURIN_STRAP_PCIE_P_TX_FIFO_HIDE_DELAY	0x172

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_LC_TRANSMIT_MUX_CXL_FORCE_IO_MODE]. 2 bit
 * strap defaults to 2 which inserts an extra flit if there isn't a complete I/O
 * data flit already in the skid buffer.
 */
#define	TURIN_STRAP_PCIE_P_CXL_IO_FLIT_MODE	0x173

/*
 * See PCIEPORT::PCIE_LC_CNTL11[LC_CHECK_TS1_EC_ON_EQ_EXIT]. 1 bit strap
 * defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_EQ_WAIT_FOR_TS1_EQ_EXIT_EC	0x174

/*
 * See PCIEPORT::PCIE_LC_CNTL10[LC_USE_PENDING_FOM_SKIP_SECOND_RXEQEVAL]. 1 bit
 * strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_SKIP_RXEQ2		0x175

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_VIRTUAL_PM_STATE_RECOVERY_EXIT_MODE]. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_CXL_VIRT_RCVERY_MODE	0x176

/*
 * See PCIEPORT::PCIE_LC_CNTL12[LC_DELAY_PHASE1]. This 3 bit strap defaults to
 * 0, indicating no delay during the first phase of Recover.Equalization (PCIe5
 * 4.2.5.4.2) state. Other values indicate time in approximately 1.57ms
 * increments.
 */
#define	TURIN_STRAP_PCIE_P_EQ_PHASE1_DELAY	0x177

/*
 * These two straps are complements to TURIN_STRAP_PCIE_P_EQ_SAFE_SEARCH and are
 * explicitly for 16 GT/s and 32 GT/s operation and set the corresponding bit in
 * PCIEPORT::PCIE_LC_EQ_CNTL_{16,32}GT[LC_SAFE_EQ_SEARCH_{16,32}]. Both are 1
 * bit and default to 0.
 */
#define	TURIN_STRAP_PCIE_P_16GT_EQ_SAFE_SEARCH	0x178
#define	TURIN_STRAP_PCIE_P_32GT_EQ_SAFE_SEARCH	0x179

/*
 * See PCIEPORT::PCIE_LC_CNTL10[LC_NO_SKIP_P0]. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_PWR_DOWN_INC_P0	0x17a

/*
 * See PCIEPORT::PCIE_LC_CNTL13[LC_EXTRA_WAIT_IN_DETECT]. 3 bit value that
 * defaults to 0. Controls a minimum amount of time in the LTSSM Detect state.
 * After this, it will exit if it noticed it's no longer in electrical idle.
 */
#define	TURIN_STRAP_PCIE_P_EXTRA_DETECT		0x17b

/*
 * See PCIEPORT::PCIE_LC_CNTL13[LC_INFERRED_EI_FAILED_SPEED_MODE]. 1 bit strap
 * defaults to 0. When set, indicates that a failed speed change should cause
 * one to enter electrical idle.
 */
#define	TURIN_STRAP_PCIE_P_EI_AFTER_BAD_SPEED_CHANGE	0x17c

/*
 * See PCIEPORT::PCIE_LC_RXRECOVER_RXSTANDBY_CNTL[
 * LC_DEFER_RXSTANDBY_POLL_ACTIVE]. 3 bit strap defaults to 6. Similar to
 * TURIN_STRAP_PCIE_P_EXTRA_DETECT, but instead controls behavior in
 * Polling.Active with receiver standby asserted.
 */
#define	TURIN_STRAP_PCIE_P_EXTRA_RX_STANDBY	0x17d

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[LC_CXL_RETIMER_MISMATCH_EN].
 * This is a 1 bit field that defaults to 0 and when enabled means that software
 * needs to clear certain error cases in the future.
 */
#define	TURIN_STRAP_PCIE_P_CXL_RETIMER_MISMATCH_EN	0x17e

/*
 * 0x17f is reserved
 */

/*
 * See PCIEPORT::PCIE_ERR_CNTL[STRAP_POISONED_ADVISORY_NONFATAL].  Single-bit
 * strap defaults to 0; presumably this exists as a strap to allow progress to
 * be made if a device below the port sends poisoned TLPs before software has an
 * opportunity to set this bit directly.
 */
#define	TURIN_STRAP_PCIE_P_POISON_ADV_NF	0x180

/*
 * Empirical results indicate that this strap sets the otherwise read-only MPS
 * field in the bridge's Device Capabilities register.  The 3-byte field
 * defaults to 2 and the encodings are those in the capability.  See PCIe5
 * 7.5.3.3.  Note that this appears to set the capability field *directly*, not
 * by changing the private override values in PCIEPORT::PCIEP_PORT_CNTL or
 * PCIEPORT::PCIE_CONFIG_CNTL; those registers aren't affected by this strap,
 * and firmware always sets the mode bits such that the devctl value is used.
 */
#define	TURIN_STRAP_PCIE_P_MAX_PAYLOAD_SUP	0x181

/*
 * See PCIEPORT::PCIE_ERR_CNTL[STRAP_FIRST_RCVD_ERR_LOG].  This single-bit field
 * defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_LOG_FIRST_RX_ERR	0x182

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_EXTENDED_FMT_SUPPORTED] and PCIe5
 * 7.5.3.15.  This value ends up in the Device Capabilities 2 register.  It's a
 * single bit and defaults to 0; note that PCIe5 strongly recommends enabling
 * this capability so that the 3-bit TLP fmt field is available to support
 * end-to-end TLP prefixes.
 */
#define	TURIN_STRAP_PCIE_P_EXT_FMT_SUP		0x183

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_E2E_PREFIX_EN].  Almost certainly needs
 * the previous strap to be set in order to work; this single-bit field defaults
 * to 0.
 */
#define	TURIN_STRAP_PCIE_P_E2E_TLP_PREFIX_EN	0x184

/*
 * We know what this sets, but not what it means.  The single-bit field defaults
 * to 0 and controls PCIEPORT::PCIEP_BCH_ECC_CNTL[STRAP_BCH_ECC_EN], which is
 * supposedly reserved.  BCH is defined nowhere in PCIe5 or the PPR, so what
 * does it mean?  Well, setting this bit means your machine will hang hard when
 * PCIe traffic happens later on.
 */
#define	TURIN_STRAP_PCIE_P_BCH_ECC_EN		0x185

/*
 * This controls whether the port supports the ECRC being regenerated when
 * dealing with multicast transactions, and specifically the corresponding bit
 * in the Multicast Capability register; see PCIe5 7.9.11.2.  This single-bit
 * field defaults to 0 and sets the otherwise read-only capability bit directly,
 * which will matter only if multicast is also enabled
 * (TURIN_STRAP_PCIE_MCAST_EN).
 */
#define	TURIN_STRAP_PCIE_P_MC_ECRC_REGEN_SUP	0x186

/*
 * Each of these fields is a mask with one bit per link speed, 2.5 GT/s in bit 0
 * up to 32 GT/s in bit 4.  GEN controls whether we support generating SKP
 * ordered sets at the lower rate used with common clocking or SRNS (600 ppm
 * clock skew), while RCV controls whether we support receiving SKP OSs at that
 * lower rate.  It's not clear whether this has any effect without either SRIS
 * or SRIS autodetection being enabled.  The manual tells us that if we want to
 * use SRIS, at least the receive register needs to be zero, meaning that we
 * expect to receive SKP OSs at the higher rate specified for SRIS (5600 ppm),
 * which makes sense: we need the extra SKPs to prevent the receiver's elastic
 * buffers from over- or underflowing.  The default values are 0.  See PCIe5
 * 7.5.3.18; these settings end up in the Link Capabilities 2 register.
 */
#define	TURIN_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP	0x187
#define	TURIN_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP	0x188

/*
 * These next two also relate to the Device Capabilities 2 register and whether
 * or not the port supports the 10-bit tag completer or requester respectively.
 * These single-bit fields default to 0 and set the otherwise read-only
 * capabilities directly, not via the overrides in PCIEPORT::PCIE_CONFIG_CNTL.
 */
#define	TURIN_STRAP_PCIE_P_10B_TAG_CMPL_SUP	0x189
#define	TURIN_STRAP_PCIE_P_10B_TAG_REQ_SUP	0x19a

/*
 * This controls whether or not the CCIX vendor specific cap is advertised or
 * not.  This single-bit field defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_CCIX_EN		0x18b

/*
 * This strap controls the size of the CXL MEMBAR0 region. It's a 6 bit strap
 * that defaults to zero and software initializes to 0x10. It's unclear what the
 * exact units of this, are but if this is sized like a BAR and it's a number of
 * bits that would be 64 KiB, which would cover most of the CXL component
 * registers today.
 */
#define	TURIN_STRAP_PCIE_P_CXL_MEMBAR_SIZE	0x18c

/*
 * Controls whether or not the Alternate Protocol Extended Capability (0x2b) is
 * advertised or not. See PCIe5 7.9.21. Note, this is required for CXL. 1 bit
 * strap defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_EN		0x18d

/*
 * 0x18e is reserved.
 */

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_LANE_NEGOTIATION].  This 3-bit field's
 * encodings are the same, and it defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_LANE_NEG_MODE	0x18f

/*
 * 0x190 is reserved
 */

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_BYPASS_RCVR_DET].  The single-bit field
 * defaults to 0, meaning we get the behaviour described in PCIe5 ch. 4.  Don't
 * touch.
 */
#define	TURIN_STRAP_PCIE_P_BYPASS_RX_DET	0x191

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_FORCE_COMPLIANCE].  This single-bit field
 * defaults to 0, allowing the LTSSM to transition normally.
 */
#define	TURIN_STRAP_PCIE_P_COMPLIANCE_FORCE	0x192

/*
 * This is the opposite of the one above and seems to allow for compliance mode
 * to be disabled entirely.  Note that doing this is a gross violation of PCIe5
 * 4.2.5.2.  The single-bit field defaults to 0, meaning compliance mode is
 * supported.  See PCIEPORT::PCIEP_STRAP_LC[STRAP_COMPLIANCE_DIS].
 */
#define	TURIN_STRAP_PCIE_P_COMPLIANCE_DIS	0x193

/*
 * See PCIEPORT::PCIE_LC_CNTL2[LC_X12_NEGOTIATION_DIS].  This single-bit field
 * defaults to 1, which causes this bit to be set and x12 negotiation to be
 * prohibited.
 */
#define	TURIN_STRAP_PCIE_P_NEG_X12_DIS		0x194

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_REVERSE_LANES].  Setting this single-bit
 * strap forces lane reversal; the default of 0 allows autonegotiation.  This
 * shouldn't normally be used and instead we should use the features built into
 * the DXIO subsystem for communicating reversal.
 */
#define	TURIN_STRAP_PCIE_P_REVERSE_LANES	0x195

/*
 * 0x196 is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_CNTL3[LC_ENHANCED_HOT_PLUG_EN] and AMD's AGESA
 * specification for a description of enhanced hotplug mode.  This mode is not
 * supported by this kernel and this single-bit strap should be left at its
 * default value of 0.
 */
#define	TURIN_STRAP_PCIE_P_ENHANCED_HP_EN	0x197

/*
 * 0x198 is reserved.
 */

/*
 * The next two straps set PCIEPORT::PCIEP_STRAP_LC[STRAP_FTS_yTSx_COUNT] (2
 * bits, default is 0) and a value that does not seem to correspond with any
 * visible register.  The initial N_FTS value, which is an 8-bit field that
 * defaults to 0x18, is used instead of
 * PCIEPORT::PCIE_LC_N_FTS_CNTL[LC_XMIT_N_FTS] because the
 * LC_XMIT_N_FTS_OVERRIDE_EN bit is clear by default.  If it's necessary to
 * override the value of the N_FTS field in transmitted TSx OSs, one could
 * either set this strap or set those fields directly; note that setting the
 * strap may allow this to take effect in a context where SW hasn't had the
 * chance to do that before the field's value has an effect.  It shouldn't be
 * necessary to change any of this.
 */
#define	TURIN_STRAP_PCIE_P_FTS_TS_COUNT		0x199
#define	TURIN_STRAP_PCIE_P_FTS_INIT_NUM		0x19a

/*
 * Sets the device ID for presumably a SWUS, defaulting to 0 now. In SP3 this
 * was clearly for a SWUS. It is less clear here.
 */
#define	TURIN_STRAP_PCIE_P_DEVID		0x19b

/*
 * This strap is a bit mysterious. It basically is used to indicate if it is a
 * 'SB'. In normal AMD parlance this might be something related to controlling
 * or indicating whether this is a southbridge; however, here, that's less
 * obvious. All we know is that this is a 1-bit field that defaults to 0x0.  AMD
 * doesn't document nor use this strap, and we shouldn't either.
 */
#define	TURIN_STRAP_PCIE_P_IS_SB		0x19c

/*
 * 0x19d is reserved
 */

/*
 * These next few fields all relate to the L1 PM substates capability and
 * various features there. Each of these straps is a single bit; PCIPM_L1P1 and
 * PM_SUB_SUP default to 1 and the others to 0, according to AMD's
 * documentation.  However, in practice, ASPM_L1P1_SUP is also set by default.
 * See PCIe5 7.8.3.2; these bits control the corresponding values in the L1 PM
 * Substates Capabilities register.  Note that the PPR claims these bits are
 * read/write there, which somewhat surprisingly is true.
 */
#define	TURIN_STRAP_PCIE_P_PCIPM_L1P2_SUP	0x19e
#define	TURIN_STRAP_PCIE_P_PCIPM_L1P1_SUP	0x19f
#define	TURIN_STRAP_PCIE_P_ASPM_L1P2_SUP	0x1a0
#define	TURIN_STRAP_PCIE_P_ASPM_L1P1_SUP	0x1a1
#define	TURIN_STRAP_PCIE_P_PM_SUB_SUP		0x1a2

/*
 * 0x1a3 is reserved
 */

/*
 * This controls the port's value of Tcommonmode in us. This controls the
 * restoration of common clocking and is part of the L1.0 exit process and
 * controls a minimum time that the TS1 training sequence is sent for. The
 * default for this 8-bit field is 0x0. It appears that software must overwrite
 * this to 0xa.  See PCIEPORT::PCIE_LC_L1_PM_SUBSTATE2[LC_CM_RESTORE_TIME] and
 * PCIe5 7.8.3.3 which describes the field in the L1 PM substates control
 * register that this value overrides.
 */
#define	TURIN_STRAP_PCIE_P_TCOMMONMODE_TIME	0x1a4

/*
 * This presumably sets the default Tpower_on scale value in the L1 PM Substates
 * Control 2 register. This is a 2-bit field that defaults to 0x0, indicating
 * that the scale of Tpower_on is 2us. It appears software is expected to
 * overwrite this to 0x1, indicating 10us.  See PCIe5 7.8.3.4.  This appears to
 * set the standard register directly rather than putting the value in the
 * override field PCIEPORT::PCIE_LC_L1_PM_SUBSTATE[LC_T_POWER_ON_SCALE].
 */
#define	TURIN_STRAP_PCIE_P_TPON_SCALE		0x1a5

/*
 * 0x1a6 is reserved
 */

/*
 * This goes along with TURIN_STRAP_PCIE_P_TPON_SCALE and sets the value that
 * should be there. A companion to our friend above. This is a 5-bit register
 * and the default value is 0x5. It seems software may be expected to set this
 * to 0xf, meaning (with the preceding) 150 microseconds.
 */
#define	TURIN_STRAP_PCIE_P_TPON_VALUE		0x1a7

/*
 * 0x1a8 is reserved
 */

/*
 * The next two straps are related to the PCIe Gen 4 data link feature
 * capability. The first controls whether this is supported or not while the
 * latter allows for feature exchange to occur. These both default to 0x0,
 * indicating that they are not supported and enabled respectively.  See PCIe5
 * 3.3 and 7.7.4.  The first causes bit 0 to be set in the Local Data Link
 * Feature Supported field of the capability register; the second causes bit 31
 * to be set.  Note that the corresponding capability bits are otherwise
 * read-only.
 */
#define	TURIN_STRAP_PCIE_P_DLF_SUP		0x1a9
#define	TURIN_STRAP_PCIE_P_DLF_EXCHANGE_EN	0x1aa

/*
 * This strap controls the header scaling factor used in scaled flow control.
 * Specifically, it is the HdrScale value to be transmitted and is a 2-bit field
 * that defaults to 0x0.  See PCIe5 3.4.2.  This doesn't appear to correspond to
 * any documented register.
 */
#define	TURIN_STRAP_PCIE_P_DLF_HDR_SCALE_MODE	0x1ab

/*
 * 0x1ac is reserved
 */

/*
 * These next straps control the behavior of SFI, see also
 * TURIN_STRAP_PCIE_SFI_EN. The first likely controls the capability on a
 * per-port basis. See PCIEPORT::PCIE_STRAP_RX_TILE1[STRAP_SFI_EN]. The next
 * strap controls the capability register bit 0: SFI OOB PD supported.
 */
#define	TURIN_STRAP_PCIE_P_SFI_EN		0x1ad
#define	TURIN_STRAP_PCIE_P_SFI_OOB_PD_SUP	0x1ae

/*
 * This likely correlates to the default value of bit 16, 'ERR_COR Subclass
 * Capable', in the Device Capabilities Register. This is a 1 bit strap that
 * defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_ERR_COR_EN		0x1af

/*
 * A per-port variant of TURIN_STRAP_PCIE_DPC_EN. How it interacts with the
 * former core strap is not quite clear.
 */
#define	TURIN_STRAP_PCIE_P_DPC_EN		0x1b0

/*
 * 0x1b1 is reserved.
 */

/*
 * See discussion in milan/pcie_rsmu.h.
 */
#define	TURIN_STRAP_PCIE_P_PORT_OFF	0x1b2

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ADVERTISE_MODIFIED_TS_OS_SUPPORT]. 1 bit strap that controls whether
 * modified TS support is advertised. May impact the 32.0 GT/s capabilities
 * register (PCIe5 7.7.6.2). 1 bit strap that defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_MODTS_SUP	0x1b3

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_SPEED_FAILURE_OVERRIDE_EN]. 1 bit strap that defaults to 1. Controls
 * the behavior of whether or not CXL support remains active after transitioning
 * to 2.5GT/s or 5GT/s operation (PCIe 1/2).
 */
#define	TURIN_STRAP_PCIE_P_CXL_DIS_GEN12	0x1b4

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_SPEED_FAILURE_AUTO_HOTRESET] and
 * LC_CXL_SPEED_FAILURE_AUTO_HOTRESET_MODE. These two 1 bit straps which default
 * to 1 controls behavior around CXL that causes it to do automatically reset
 * the link (disabling and re-enabling) if the device speed dips below Gen 3.
 */
#define	TURIN_STRAP_PCIE_P_CXL_SPEED_AUTO_RESET		0x1b5
#define	TURIN_STRAP_PCIE_P_CXL_SPEED_AUTO_RESET_MODE	0x1b6

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[LC_CXL_BYPASS_ARBMUX_IO_ONLY].
 * 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_CXL_IO_BYPASS_MUX	0x1b7

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[
 * LC_CXL_SPEED_FAILURE_WAIT_DETECT_EN]. 1 bit strap defaults to 0. Controls
 * whether or not CXL devices will wait in Detect on link speed reductions below
 * 8 GT/s.
 */
#define	TURIN_STRAP_PCIE_P_CXL_SPEED_WAIT_DETECT	0x1b8

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ALTERNATE_PROTOCOL_RESPONSE_TIME_LIMIT]. 2 bit strap that defaults to 2,
 * aka 100us. Other values are 10us (0), 20us (1), and 1 ms (3). Controls how
 * long the LTSSM waits in Configuration.Lanenum.Wait (PCIe5 4.2.6.3.4).
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_LTTSM_LANE_WAIT	0x1b9

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL6[LC_CXL_SKIP_NEGOTIATION]. 1
 * bit strap defaults to 0. Controls whether CXL is enabled automatically. We
 * should not use this as it would bypass feature negotiation and local features
 * are entirely assumed.
 */
#define	TURIN_STRAP_PCIE_P_CXL_SKIP_AUTONEG	0x1ba

/*
 * 0x1bb is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[LC_ALTERNATE_PROTOCOL_COUNT].
 * Likely corresponds to the Alternate Protocol Count in the Alternate Protocol
 * Capabilities Register (PCIe5 7.9.21.2). 8 bit strap that defaults to 0x2
 * (likely PCIe + CXL).
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_CNT		0x1bc

/*
 * These two straps control the Modified TS Usage Mode 1 and 2 bits in the 32.0
 * GT/s capabilities register (PCIe5 7.7.6.2). The straps set the corresponding
 * fields in PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1. See
 * LC_MODIFIED_TS_USAGE_MODE_{1,2}_SUPPORTED. Both are 1 bit straps. Usage Mode
 * 1 defaults to 0, Usage Mode 2 defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_MODTS_1_SUP		0x1bd
#define	TURIN_STRAP_PCIE_P_MODTS_2_SUP		0x1be

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ALTERNATE_PROTOCOL_SELECTIVE_ENABLE_SUPPORTED]. Controls the corresponding
 * bit in the Alternate Protocol Capabilities Register (PCIe5 7.9.21.2). 1 bit
 * strap defaults to 1.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_SEL_EN	0x1bf

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ALTERNATE_PROTOCOL_CXL_PCIE_ONLY_NEG_MODE]. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_CXL_IO_PCIE_NEG	0x1c0

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ALTERNATE_PROTOCOL_CHECK_COMMON_CLOCK]. Enables checking of common clock
 * portions of the modified Training Sets. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_CHECK_CLK	0x1c1

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ALTERNATE_PROTOCOL_CHECK_RTM_CXL_AWARE]. Enables checking of retimers
 * being CXL aware in the Modified TS2 data. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_CHECK_CXL_RETIMER	0x1c2

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_ALTERNATE_PROTOCOL_ABORT_RCVD_USAGE_MODE_000]. 1 bit strap defaults to 0.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_ABORT_UM_000	0x1c3

/*
 * See PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL1[
 * LC_CXL_COMMON_CLOCK_IN_MODTS2]. 1 bit strap defaults to 0. Controls whether
 * CXL transmits the common clock field in modified TS2 or just TS1.
 */
#define	TURIN_STRAP_PCIE_P_ALT_PROT_TX_COM_CLK	0x1c4

/*
 * 0x1c5 is reserved.
 */

/*
 * The next three straps control the behavior of what is transmitted in the
 * modified TS1 data. These are split across two different registers:
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL2 and
 * PCIEPORT::PCIE_LC_ALTERNATE_PROTOCOL_CNTL3. These control the information 1,
 * information 2, and vendor ID fields. The Information 1 strap is only 11 bits
 * large; however, the field is 16 bits. It defaults to 0x8. The vendor field is
 * 16 bits and defaults to 0x1e98. The Protocol 2 details field is 24 bits and
 * defaults to 0x7. For CXL the latter controls the capability bits advertised
 * and the bit values likely are buried in the CXL spec.
 */
#define	TURIN_STRAP_P_ALT_PROT_DETAILS1		0x1c6
#define	TURIN_STRAP_P_ALT_PROT_VID		0x1c7
#define	TURIN_STRAP_P_ALT_PROT_DETAILS2		0x1c7

/*
 * 0x1c9 is reserved.
 */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_PCIE_RSMU_H */
