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

#ifndef _SYS_IO_MILAN_PCIE_RSMU_H
#define	_SYS_IO_MILAN_PCIE_RSMU_H

/*
 * Contains a series of strap registers for different parts of the SoC.
 *
 * The PCIe straps are identical for both Rome and Milan. These come in two
 * groups: those with the lowest set of addresses cover the entire
 * 'milan_pcie_core_t'; they are followed by 8 groups of straps, identical to
 * one another but different from the per-core groups, one for each possible
 * port/host bridge the core can provide.
 *
 * To set a strap's value, the index register is first written with the upper 15
 * bits set (MILAN_STRAP_PCIE_ADDR_UPPER) and the lower 17 set to the address of
 * the strap to be set (definitions below).  The data register is then written
 * with a value; the number of bits that are meaningful depends on the strap and
 * these are listed with each where we have information.  The first group of
 * straps applies to the entire PCIe core to which this RSMU is attached, and is
 * followed by a set of groups, identical to one another but different from the
 * per-core straps, one for each of the 8 possible ports the core can support.
 * AMD documentation suggests it is also possible to read the value of a strap,
 * but whether this is meaningful depends on what has previously been done to
 * set up the core.  For more on where and how these straps fit into PCIe
 * initialisation, see the theory statement in milan_fabric.c.
 *
 * Many abbreviations and acronyms are used here.  Many are defined in the PCIe4
 * Terms and Acronyms introductory section starting on page 43.  Others not
 * found there are listed here, but this list is not exhaustive.  See inline
 * notes, additional comments in milan_fabric.c and uts/intel/sys/amdzen/smn.h,
 * and industry-standard reference materials.
 *
 * ASPM - Active State Power Management.  See PCIe4 5.4.1.
 *
 * CCIX - Cache Coherent Interconnect for Accelerators, a PCIe extension not
 * supported by our SW.
 *
 * CPL - Completion (see PCIe4).  CPL TO is a completion timeout, also referred
 * to as CTO.
 *
 * CDR - Clock and Data Recovery.  See PCIe4 4.3 and 8.6, among others.
 *
 * DBG - Debug, interfaces for observability and testing.
 *
 * DLF - Data Link Feature (Exchange).  See PCIe4 3.3.
 *
 * DS - Downstream.
 *
 * ECRC - End-to-end CRC.  See PCIe4 2.7.1.
 *
 * EQ - Equalisation.  This feature set was mainly introduced with PCIe3 and
 * expanded significantly in PCIe4.  See PCIe4 4.2.3 and ch. 8.
 *
 * ESM - Extended Speed Mode, a CCIX extension to PCIe4 allowing speeds of 20 or
 * 25 GT/s.  ESM is not currently supported by our SW.
 *
 * FOM - Figure of Merit.
 *
 * FTS - Fast Training Sequence ordered set.  See PCIe4 ch. 4.
 *
 * LTSSM - Link Training and Status State Machine.  See PCIe4 4.2.5.
 *
 * OS - Here, Ordered Set.  See PCIe4 4.2.1.2.
 *
 * PME - Power Management Event(s).  See PCIe4 5.11.
 *
 * SPC - Symbols Per Clock.  See the related by not identical symbol time in
 * PCIe4.
 *
 * SRIS/SRNS - Separate Reference, {Independent, No} Spreading.  Alternate
 * reference clocking architectures; see PCIe4 4.2.7.  SKP ("Skip") ordered sets
 * are used to implement these at the physical encoding layer.
 *
 * SWUS - Switch Upstream (also SWDS, Switch Downstream).  The PCIe logic AMD
 * employs, like most such cores, can be employed in many different
 * applications: root complex/host bridge (including most SWDS), standalone
 * switches with an SWUS and one or more SWDS ports, or an end device.  While
 * the primary application found in the AMD processor, and the only one we
 * actually support, is the root port/host bridge, SWUS is also used in AMD
 * documentation to refer to part of the NBIF mechanism linking the IOHC with
 * several integrated PCIe end devices not supported by this kernel.
 *
 * US - Upstream.
 *
 * Broadly, these straps cause fields in certain PCIe core and port registers to
 * be altered; in some cases, they provide a sort of hidden default that the
 * registers can override later on, while in other instances they appear to
 * cause the register fields to be set directly.  Confusingly, some of these
 * registers are those with STRAP in their names, like PCIECORE::PCIE_STRAP_MISC
 * and PCIEPORT::PCIEP_STRAP_LC.  However, other straps that can be set here
 * correspond to other registers whose relationships to the strapping process
 * are not documented by AMD.  See the Milan PPR ch. 13 and the theory statement
 * in milan_fabric.c for an incomplete understanding of how this works.  Where a
 * known or expected relationship exists between a strap defined here and a
 * field in a PPR-documented register, that relationship is noted above the
 * strap address's definition.  Use this knowledge with caution, and verify
 * assumed relationships before relying on them: these are not documented.
 */

#include <sys/types.h>
#include <sys/amdzen/smn.h>
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
 * register instance numbers.  Registers are 32 bits wide, however.  There is
 * one instance per PCIe core.
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
	const uint32_t nents = (const uint32_t)milan_iohc_n_pcie_cores(iomsno);
	ASSERT3U(nents, >, reginst32);
#endif	/* DEBUG */

	const uint32_t aperture_base = 0x9046000;

	const uint32_t aperture_off = (ioms32 << 12) + (reginst32 << 14);
	ASSERT3U(aperture_off, <=, UINT32_MAX - aperture_base);

	const uint32_t aperture = aperture_base + aperture_off;
	ASSERT0(aperture & PCIE_RSMU_SMN_REG_MASK);

	return (SMN_MAKE_REG(aperture + def.srd_reg, SMN_UNIT_PCIE_RSMU));
}

/*
 * Strap settings are accessed via the RSMU via this indirect index/data pair;
 * their AMD names are
 * SMU::RSMU::RSMU::PCIE0::MMIOEXT::RSMU_SW_STRAPRX_ADDR_PCIE0 and
 * SMU::RSMU::RSMU::PCIE0::MMIOEXT::RSMU_SW_STRAPRX_DATA_PCIE0.  An associated
 * register SMU::RSMU::RSMU::PCIE0::MMIOEXT::RSMU_SW_STRAP_CONTROL_PCIE0 is used
 * to trigger reloading of straps but it not currently used by our code and its
 * relationship with the PCIe internal configuration state machine is
 * undocumented.
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
 * The upper bits of the address register for any given strap.  Consuming code
 * will always OR this into the value being written to the index register.
 */
#define	MILAN_STRAP_PCIE_ADDR_UPPER	0xfffe0000

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
#define	MILAN_STRAP_PCIE_WRITE_DISABLE		0x00

/*
 * Controls whether MSI's are supported. Default value is 0x1, aka MSIs are
 * enabled.  This is possibly related to
 * PCIECORE::PCIE_STRAP_F0[STRAP_F0_MSI_EN].  Regardless, it needs to be left
 * enabled because MSI support is required by PCIe.  See PCIe4 7.7.1.
 */
#define	MILAN_STRAP_PCIE_MSI_EN			0x01

/*
 * Controls whether the AER capability structure exists for the host bridges in
 * this core.  See PCIe4 6.2 and 7.8.4.  This single-bit strap defaults to 0;
 * enabling it does not enable error detection or reporting by itself, but
 * allows generic code to do so.
 */
#define	MILAN_STRAP_PCIE_AER_EN			0x02

/*
 * 0x3 is reserved
 */

/*
 * See PCIECORE::PCIE_STRAP_MISC2[STRAP_GEN2_COMPLIANCE] and the gen3/4
 * companions below at MILAN_STRAP_PCIE_GEN3_1_FEAT_EN.  This single-bit strap
 * defaults to 1.
 */
#define	MILAN_STRAP_PCIE_GEN2_FEAT_EN		0x04

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_CLK_PM_EN] and PCIe4 7.5.3.6.  This
 * controls the PCIe Link Capability bit 18 Clock Power Management feature.
 * Default is 0x0.
 */
#define	MILAN_STRAP_PCIE_CLK_PM_EN		0x05

/*
 * See PCIECORE::PCIE_CFG_CNTL[CFG_EN_DEC_TO_HIDDEN_REG].  This single-bit strap
 * is 0 by default.  It's not known which registers this, or its two companions,
 * exposes.
 */
#define	MILAN_STRAP_PCIE_DECODE_TO_HIDDEN_REG	0x06

/*
 * See PCIECORE::PCIE_STRAP_F0[STRAP_F0_LEGACY_DEVICE_TYPE_EN] and PCIe4 1.3.2.
 * The AMD instantiation of PCIe does not end up with any of these, and the host
 * bridges are all PCI Express endpoints.  This single-bit strap defaults to 0
 * and needs to be left there.
 */
#define	MILAN_STRAP_PCIE_LEGACY_DEVICE_EN	0x07

/*
 * We believe that this controls the generation of initiator (master) completion
 * timeouts; it doesn't correspond directly to anything documented, but may be
 * PCIECORE::PCIE_STRAP_MISC2[STRAP_MSTCPL_TIMEOUT_EN]. This defaults to 0x1,
 * that is, enabled.
 */
#define	MILAN_STRAP_PCIE_CPL_TO_EN		0x08

/*
 * This appears to be a means to force some master timeout. Its relationship to
 * the strap above is unclear and this doesn't correspond to anything
 * documented.  Single bit, defaults to 0.
 */
#define	MILAN_STRAP_PCIE_FORCE_TO_EN		0x09

/*
 * The PCIe hardware apparently has an i2c debug interface that this allows one
 * to manipulate. That's, well, spicy. Single bit, defaults to 0.  See
 * PCIECORE::PCIE_STRAP_I2C_BD and the companion strap
 * MILAN_STRAP_PCIE_I2C_TARG_ADDR below.
 */
#define	MILAN_STRAP_PCIE_I2C_DBG_EN		0x0a

/*
 * This controls whether or not the Device Capabilities 2 TPH Completer
 * Supported bit is enabled.  See
 * PCIECORE::PCIE_STRAP_MISC2[STRAP_TPH_SUPPORTED] and PCIe4 6.17 and 7.5.3.15.
 * Note that while the field in devcap2 is 2 bits in size, this field, like the
 * core register, is only 1, implying that the HW does not support the extended
 * variant of the feature.  The default value is 0.
 */
#define	MILAN_STRAP_PCIE_TPH_EN			0x0b

/*
 * See PCIe4 7.5.2.2; this controls PCIERCCFG::PMI_STATUS_CNTL[NO_SOFT_RESET].
 * It's a single-bit strap that defaults to 0.
 */
#define	MILAN_STRAP_PCIE_NO_SOFT_RST		0x0c

/*
 * This controls whether or not the device advertises itself as a multi-function
 * device and presumably a bunch more of side effects. This defaults to 0x1,
 * enabled.
 */
#define	MILAN_STRAP_PCIE_MULTI_FUNC_EN		0x0d

/*
 * See the PPR discussion of extended tag support in 13.6.5.4.4; this is mainly
 * geared toward the NBIF/SWUS application, and the documented effect (it sets
 * the default value of Extended Tag Field Enable in the Device Control
 * register) is not correct, and we don't know what if anything it does.
 * Whether set to 0 or 1, this doesn't change the devcap or devctl extended tag
 * bits.  See also PCIe4 7.5.3.4, and note that we normally enable 10-bit tags
 * regardless.  The default value of this single-bit strap is 0.
 */
#define	MILAN_STRAP_PCIE_TAG_EXT_ECN_EN		0x0e

/*
 * This controls whether or not the device advertises downstream port
 * containment features or not; the exact effect on the system isn't documented.
 * See PCIe4 6.2.10.  This single-bit field defaults to 0.
 */
#define	MILAN_STRAP_PCIE_DPC_EN			0x0f

/*
 * This controls whether or not the Data Link Feature Extended Capability (0x25)
 * is advertised.  See PCIECORE::PCIE_STRAP_MISC[STRAP_DLF_EN] and PCIe4 7.7.4.
 * The single-bit default is 0x1, enabled.  See the per-port settings at
 * MILAN_STRAP_PCIE_P_DLF_SUP below, which control the values of bits in this
 * capability if this strap enables its existence.
 */
#define	MILAN_STRAP_PCIE_DLF_EN			0x10

/*
 * This controls whether or not the Physical Layer 16.0 GT/s Extended Capability
 * (0x26) is advertised. See PCIECORE::PCIE_STRAP_MISC[STRAP_16GT_EN] and PCIe4
 * 7.7.5.  The single-bit default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_PL_16G_EN		0x11

/*
 * This controls whether or not the Lane Margining at the Receiver Extended
 * Capability (0x27) exists. See PCIECORE::PCIE_STRAP_MISC[STRAP_MARGINING_EN]
 * and PCIe4 7.7.6.  The single-bit default is 0x1, enabled.
 */
#define	MILAN_STRAP_PCIE_LANE_MARGIN_EN		0x12

/*
 * 0x13 is reserved
 */

/*
 * Virtual channel capability.  See PCIECORE::PCIE_STRAP_F0[STRAP_F0_VC_EN],
 * which controls whether this capability exists (PCIe4 7.9.1).  The second of
 * these isn't documented.  Both are enabled for cores that need to support
 * CCIX, which ours do not.  These are single-bit straps that default to 0.
 */
#define	MILAN_STRAP_PCIE_VC_EN			0x14
#define	MILAN_STRAP_PCIE_2VC_EN			0x15

/*
 * See PCIECORE::PCIE_STRAP_F0[STRAP_F0_DSN_EN].  This enables the device serial
 * number capability for the host bridges in this core.  The actual serial
 * number to present to software is set by MILAN_STRAP_PCIE_SN_{L,M}SB straps
 * below.  This single-bit strap defaults to 0.
 */
#define	MILAN_STRAP_PCIE_DSN_EN			0x16

/*
 * This controls the ARI Extended Capability and whether those features are
 * advertised or enabled. See PCIe4 7.8.7 and some additional controls in
 * IOHC::IOHC_FEATURE_CNTL; a similar bit exists in PCIECORE::PCIE_STRAP_F0 that
 * claims to override this strap for SWUS ports only, which we don't have. This
 * single-bit strap defaults to 1.
 */
#define	MILAN_STRAP_PCIE_ARI_EN			0x17

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
#define	MILAN_STRAP_PCIE_F0_EN			0x18

/*
 * The next two control whether we advertise support for D1 and D2 power states
 * in the otherwise read-only PMI_CAP[D{2,1}_SUPPORT] fields.  Each single-bit
 * strap defaults to 0.  See PCIe4 7.5.2.1.
 */
#define	MILAN_STRAP_PCIE_POWER_D1_SUP		0x19
#define	MILAN_STRAP_PCIE_POWER_D2_SUP		0x1a

/*
 * See PCIECORE::PCIE_MISC_STRAP[STRAP_MST_ADR64_EN], basically the main switch
 * for 64-bit addressing.  This single-bit strap defaults to 1, which everyone
 * wants.
 */
#define	MILAN_STRAP_PCIE_64BIT_ADDRS		0x1b

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_TL_ALT_BUF_EN], not that it will help you
 * any.  This single-bit strap defaults to 0.
 */
#define	MILAN_STRAP_PCIE_ALT_BUF_EN		0x1c

/*
 * Enables support for the Latency Tolerance Reporting (LTR) Extended
 * Capability. This changes the values in the device capabilities 2 register.
 * See PCIe4 7.8.2.  This single-bit field defaults to 0.
 */
#define	MILAN_STRAP_PCIE_LTR_SUP		0x1d

/*
 * Controls whether optimized buffer flush/fill is advertised as supported in
 * the device capabilities 2 register.  See PCIe4 6.19 and 7.5.3.15 and
 * PCIEPORT::PCIEP_STRAP_MISC[STRAP_OBFF_SUPPORTED].  This is a 2-bit field that
 * defaults to 0.
 */
#define	MILAN_STRAP_PCIE_OBFF_SUP		0x1e

/*
 * See PCIECORE::PCIE_P_CNTL[P_SYMALIGN_{MODE,HW_DEBUG}].  This and its
 * subsequent companion are both single-bit fields that default to 0.
 */
#define	MILAN_STRAP_PCIE_SYMALIGN_MODE		0x1f
#define	MILAN_STRAP_PCIE_SYMALIGN_DBG		0x20

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_BYPASS_SCRAMBLER] and PCIe4 4.2.1.3.
 * This single-bit strap defaults to 0.
 */
#define	MILAN_STRAP_PCIE_BYPASS_SCRAMBLER	0x21

/*
 * This seems to control some internal rx error limit on deskewed data. It seems
 * to relate to some internal drop metric as well, but again, specifics unclear.
 * The default is 0x0 though this is a 3-bit wide field.
 *
 * The next several straps all involve deskew logic, and all are undocumented.
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
 * This seems to control deskewing on all SKP OSs.  The default is 0x1,
 * suggesting enabled.
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
 * This is a bit vague, but appears to control whether or not we report block
 * sync header errors from the deskew logic.  This doesn't correspond to
 * anything documented.  The default for this single-bit strap is 0x1.
 */
#define	MILAN_STRAP_PCIE_DESKEW_BLOCK_HDR	0x30

/*
 * This appears to be a means to ignore part of the SKP ordered set related to
 * DC balancing, possibly for interoperability reasons. The default is 0x1, that
 * presumably this is enabled.
 */
#define	MILAN_STRAP_PCIE_SKP_IGNORE_DC_BAL	0x31

/*
 * This is an unknown debug interface, seemingly reserved and 4 bits wide. This
 * defaults to 0x0.  It doesn't correspond to anything documented.
 */
#define	MILAN_STRAP_PCIE_DEBUG_RXP		0x32

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_CHECK_DATA_RATE] and note the
 * relationship with enabling 5 GT/s speed support in the engine configuration.
 * This single-bit strap defaults to 1 and is not port-specific even though the
 * registers it sets up are.
 */
#define	MILAN_STRAP_PCIE_DATA_RATE_CHECK	0x33

/*
 * See PCIECORE::PCIE_P_CNTL[P_ALWAYS_USE_FAST_TXCLK].  This single-bit strap
 * defaults to 0, and changing it looks like a very bad idea.
 */
#define	MILAN_STRAP_PCIE_FAST_TXCLK_EN		0x34

/*
 * This impacts the PLL somehow indicating the mode that it operates in or is
 * comparing against. This is a 2 bit field and the value defaults to 0x2 and
 * doesn't correspond to any documented register or standard feature.
 */
#define	MILAN_STRAP_PCIE_PLL_FREQ_MODE		0x35

/*
 * This seems to exist to force the link into Gen 2 mode. It defaults to 0,
 * disabled.  Note that, like all per-core straps, this presumably affects all
 * ports formed from this core.
 */
#define	MILAN_STRAP_PCIE_FORCE_GEN2		0x36

/*
 * 0x37 and 0x38 are reserved
 */

/*
 * See PCIEPORT::PCIE_LC_CNTL9[LC_LOOPBACK_RXEQEVAL_EN].  This single-bit strap
 * defaults to 1.
 */
#define	MILAN_STRAP_PCIE_LO_RXEQEVAL_EN		0x39

/*
 * This seems to control whether the device advertises whether or not it
 * supports the LTSSM 'upconfigure' ability which allows a link to train up to a
 * higher speed later. The default is 0x0, this is not enabled.  See
 * PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL[LC_UPCONFIGURE_SUPPORT].  Also see the next
 * strap.  Presumably these affect all ports in this core.
 */
#define	MILAN_STRAP_PCIE_UPCONF_SUP		0x3a

/*
 * See PCIEPORT::PCIE_LC_LINK_WIDTH_CNTL[LC_UPCONFIGURE_DIS].  This single-bit
 * strap is 0 by default.
 */
#define	MILAN_STRAP_PCIE_UPCONF_DIS		0x3b

/*
 * See PCIEPORT::PCIE_LC_TRAINING_CNTL[LC_DONT_DEASSERT_RX_EN_IN_TEST] and mind
 * the triple-negatives.  Single bit, defaults to 0.
 */
#define	MILAN_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST	0x3c

/*
 * 0x3d is reserved.
 */

/*
 * See discussion below on MILAN_STRAP_PCIE_P_DEEMPH_SEL.  This single-bit strap
 * defaults to 0.
 */
#define	MILAN_STRAP_PCIE_SELECT_DEEMPH		0x3e

/*
 * 0x3f is reserved.
 */

/*
 * This controls whether or not the Link Bandwidth Management capability in the
 * Link Capabilities register is advertised. The single-bit strap defaults to
 * 0x1.
 */
#define	MILAN_STRAP_PCIE_LINK_BW_NOTIF_SUP	0x40

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_REVERSE_ALL].  Note that this applies to
 * the entire core's set of 16 lanes; see P_REVERSE_LANES below for the
 * port-specific variant.  Like that one, leave this to the DXIO firmware to
 * configure based on our engine configuration.
 */
#define	MILAN_STRAP_PCIE_REVERSE_ALL		0x41

/*
 * This seems to exist to force the link into Gen 3 mode. It's a single bit and
 * defaults to 0x0, disabled.  This doesn't correspond to any documented
 * register or standard feature.
 */
#define	MILAN_STRAP_PCIE_FORCE_GEN3		0x42

/*
 * The next two single-bit fields both default to 1.  They control whether the
 * root ports in this core enable PCIe 3.1 and PCIe 4.0 compliant features
 * respectively, not the LTSSM compliance mode.  See PCIECORE::PCIE_STRAP_MISC2.
 */
#define	MILAN_STRAP_PCIE_GEN3_1_FEAT_EN		0x43
#define	MILAN_STRAP_PCIE_GEN4_FEAT_EN		0x44

/*
 * This controls the otherwise read-only 'ECRC Generation Capable' bit in the
 * root port's AER capability.  The default is 0x0, saying that this is not
 * advertised.  See PCIe4 7.8.4.7.  Note that this applies to all ports in this
 * core.
 */
#define	MILAN_STRAP_PCIE_ECRC_GEN_EN		0x45

/*
 * This pairs with the strap above and indicates whether or not the root port
 * advertises support for checking a generated TLP CRC. This is the 'ECRC Check
 * Capable' bit in the AER capability. The default is 0x0, saying that this is
 * not advertised.
 */
#define	MILAN_STRAP_PCIE_ECRC_CHECK_EN		0x46

/*
 * See PCIEPORT::LC_CNTL3[LC_AUTO_DISABLE_SPEED_SUPPORT_MAX_FAIL_SEL].  This
 * 2-bit strap defaults to 2 and maps directly to that field.  See additional
 * comments on MILAN_STRAP_PCIE_P_AUTO_DIS_SPEED_SUP_EN below.
 */
#define	MILAN_STRAP_PCIE_TRAIN_FAIL_SPEED_DIS	0x47

/*
 * 0x48 is reserved.
 */

/*
 * See the discussion in PPR 13.5.4 on port ordering.  Several other straps
 * herein also affect how this works; this is the master enable switch that
 * corresponds to PCIECORE::PCIE_LC_PORT_ORDER_CNTL[LC_PORT_ORDER_EN].  It's a
 * single-bit strap that defaults to 0.  Our implementation doesn't need port
 * reordering and we leave this to the DXIO firmware to deal with.
 */
#define	MILAN_STRAP_PCIE_PORT_ORDER_EN		0x49


/*
 * See PCIEPORT::PCIE_RX_CNTL[RX_IGNORE_AT_ERR].  The next several entries are
 * all about ignoring certain kinds of errors that can be detected on the
 * receive side. These all default to 0x0, indicating that we do *not* ignore
 * the error, which is what we want.
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
#define	MILAN_STRAP_PCIE_IGN_RX_AT_ERR		0x55
/*
 * 0x56 is reserved
 */

/*
 * Unlike the others, this seems to be a massive error reporting disable switch.
 * We want this to be zero at all costs, which is thankfully the default.  This
 * doesn't correspond to any documented register or feature and is distinct from
 * AER capability enabling via MILAN_STRAP_PCIE_AER_EN.
 */
#define	MILAN_STRAP_PCIE_ERR_REPORT_DIS		0x57

/*
 * This controls whether or not completer abort error reporting is enabled in
 * hardware.  The default for this single-bit strap is 0x1.
 */
#define	MILAN_STRAP_PCIE_CPL_ABORT_ERR_EN	0x58

/*
 * See PCIECORE::PCIE_STRAP_MISC[STRAP_INTERNAL_ERR_EN].  The default for this
 * single-bit strap is 0x1.
 */
#define	MILAN_STRAP_PCIE_INT_ERR_EN		0x59

/*
 * This strap is mysterious, all we get is a name and this corresponds to no
 * documented register or standard feature.  However, we do know that despite
 * the name, this needs to be set in order to enable a feature AMD refers to as
 * RX margin persistence mode.  The single-bit default is 0x0 and we'd best
 * leave it there.
 */
#define	MILAN_STRAP_PCIE_RXP_ACC_FULL_DIS	0x5a

/*
 * 0x5b is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_CDR_CNTL.  The single-bit strap CDR_MODE_FORCE defaults
 * to 0; if set, the contents of that register as set by the other straps take
 * effect.  This is used for testing and validation of the CDR subsystem and
 * should not be set by normal software.  The first three fields are of the same
 * widths as the corresponding fields in the documented register and their
 * defaults are those found in the PPR.
 */
#define	MILAN_STRAP_PCIE_CDR_TEST_OFF		0x5c
#define	MILAN_STRAP_PCIE_CDR_TEST_SETS		0x5d
#define	MILAN_STRAP_PCIE_CDR_TYPE		0x5e
#define	MILAN_STRAP_PCIE_CDR_MODE_FORCE		0x5f

/*
 * 0x60 is reserved.
 */

/*
 * See PCIECORE::PCIE_STRAP_PI; this is used for validation and shouldn't be
 * done by normal software.  Both single-bit fields default to 0.
 */
#define	MILAN_STRAP_PCIE_TEST_TOGGLE		0x61
#define	MILAN_STRAP_PCIE_TEST_PATTERN		0x62

/*
 * This one is just a generic transmit test bit. It is 2 bits wide and defaults
 * to 0x0. Not sure what this controls exactly; it corresponds to no documented
 * register.
 */
#define	MILAN_STRAP_PCIE_TX_TEST_ALL		0x63

/*
 * Overwrite the advertised vendor id for the host bridges in this core! The
 * default is unsurprisingly 0x1022.  This is 16 bits wide.  For this and
 * following straps, see PCIe4 7.5.1.
 */
#define	MILAN_STRAP_PCIE_VENDOR_ID		0x64

/*
 * Set the base and sub class code. This is 0x6 and 0x4 as expected. Each of
 * these is 8 bits wide. These are what are advertised in configuration space
 * for host bridges in this core.
 */
#define	MILAN_STRAP_PCIE_BASE_CLASS		0x65
#define	MILAN_STRAP_PCIE_SUB_CLASS		0x66

/*
 * These two bits control the upper and lower nibble of the configuration space
 * revision ID for each host bridge. This defaults to 0x0. Each of these is 4
 * bits wide.
 */
#define	MILAN_STRAP_PCIE_REV_ID_UPPER		0x67
#define	MILAN_STRAP_PCIE_REV_ID_LOWER		0x68

/*
 * 0x69 is reserved.
 */

/*
 * See PCIECORE::PCIE_STRAP_I2C_BD.  This 7-bit strap defaults to 0x8 and is
 * apparently used to set the core's I2C slave address (but this strap does not
 * enable the port; see MILAN_STRAP_PCIE_I2C_DBG_EN above).  This is presumably
 * used for debugging; we don't use it.
 */
#define	MILAN_STRAP_PCIE_I2C_TARG_ADDR		0x6a

/*
 * 0x6b is a reserved control related to i2c.
 */

/*
 * One might think this controls either whether link bandwidth notification is
 * advertised in the otherwise read-only Link Capabilities register, or perhaps
 * the default value of the Link Autonomous Bandwidth Interrupt Enable field in
 * the Link Control register (see PCIe4 7.5.3.6).  Empirically, it does neither,
 * and if it does anything we know not what.
 */
#define	MILAN_STRAP_PCIE_LINK_AUTO_BW_INT	0x6c

/*
 * 0x6d is reserved.
 */

/*
 * This next set of straps all control whether PCIe access control services is
 * turned on and various aspects of it. These all default to being disabled.
 * See PCIe4 7.7.7; these control whether this capability exists and the
 * otherwise read-only fields in it.
 */
#define	MILAN_STRAP_PCIE_ACS_EN			0x6e
#define	MILAN_STRAP_PCIE_ACS_SRC_VALID		0x6f
#define	MILAN_STRAP_PCIE_ACS_TRANS_BLOCK	0x70
#define	MILAN_STRAP_PCIE_ACS_DIRECT_TRANS_P2P	0x71
#define	MILAN_STRAP_PCIE_ACS_P2P_CPL_REDIR	0x72
#define	MILAN_STRAP_PCIE_ACS_P2P_REQ_RDIR	0x73
#define	MILAN_STRAP_PCIE_ACS_UPSTREAM_FWD	0x74

/*
 * See PCIECORE::PCIE_SDP_CTRL[SDP_UNIT_ID].  Note that this strap doesn't
 * appear to set the lower 3 bits (if needed) nor is there any other strap
 * capable of doing so.  This is a 4-bit field that defaults to 0x2.  Note that
 * we program these fields directly from SW.
 */
#define	MILAN_STRAP_PCIE_SDP_UNIT_ID		0x75

/*
 * Strap 0x76 is reserved for ACS.
 * Strap 0x77 is reserved for PM (power management).
 */

/*
 * This strap sets the otherwise read-only PCIERCCFG::PMI_CAP[PME_SUPPORT] field
 * corresponding to the PME_Support field of the Power Management Capabilities
 * register.  See PCIe4 7.5.2.1.  This is a 5-bit field that defaults to 0x19.
 */
#define	MILAN_STRAP_PCIE_PME_SUP		0x78

/*
 * Strap 0x79 is reserved for PM again.
 */

/*
 * This strap is used to seemingly disable all Gen 3 features. This is used when
 * trying to use the special BMC lanes that we thankfully are ignoring. The
 * default is 0x0, Gen 3 is enabled.  See additional notes on PCIE_GEN4_DIS
 * below.
 */
#define	MILAN_STRAP_PCIE_GEN3_DIS		0x7a

/*
 * This is used to control whether or not multicast is supported on the core's
 * host bridges.  If this single-bit field, which defaults to 0, is set, the
 * multicast capability will be advertised.  See PCIe4 7.9.11.
 */
#define	MILAN_STRAP_PCIE_MCAST_EN		0x7b

/*
 * These next two control whether or not we have AtomicOp completion and
 * AtomicOp routing support in the RC; this is propagated to the Device
 * Capabilities 2 registers in all host bridges in this RC.  See
 * PCIECORE::PCIE_STRAP_F0[STRAP_F0_ATOMIC_ROUTING_EN] and [STRAP_F0_ATOMIC_EN]
 * along with PCIe4 7.5.3.15.  Note that 64-bit AtomicOp Completer is not
 * supported by hardware and the corresponding bit is fixed at 0, so there is no
 * corresponding strap.  The default for each is 0x0.
 */
#define	MILAN_STRAP_PCIE_F0_ATOMIC_EN		0x7c
#define	MILAN_STRAP_PCIE_F0_ATOMIC_ROUTE_EN	0x7d

/*
 * This controls the number of MSIs requested by the bridge in the otherwise
 * read-only MSI capability field. This is a 3-bit field and defaults to 0x0,
 * indicating 1 interrupt; see PCIe4 7.7.1.2.  While it's possible to get more
 * interrupts, the PCIe capability and AER RE status register fields that
 * indicate which interrupt is used for reporting various conditions are all
 * fixed at 0, so there's no use for the additional vectors.
 */
#define	MILAN_STRAP_PCIE_MSI_MULTI_MSG_CAP	0x7e

/*
 * This controls whether or not the primary root complex advertises the 'No
 * RO-enabled PR-PR Passing' bit of the Device Capabilities 2 register, which is
 * related to relaxed ordering of peer-to-peer posted requests. See PCIe4
 * 7.5.3.15 and PCIECORE::PCIE_STRAP_F0[STRAP_F0_NO_RO_ENABLED_P2P_PASSING].
 * The default appears to be 0x0, suggesting it does not advertise this bit and,
 * therefore, relaxed-ordering of peer-to-peer posted requests is allowed.
 */
#define	MILAN_STRAP_PCIE_F0_NO_RO_PR_PR_PASS	0x7f

/*
 * See PCIECORE::PCIE_STRAP_F0[STRAP_F0_MSI_MAP_EN].  This single-bit field
 * defaults to 1.
 */
#define	MILAN_STRAP_PCIE_MSI_MAP_EN		0x80

/*
 * This single-bit field defaults to 0.  Its semantics are unknown; phy
 * calibration reset detection is discussed in PCIECORE::SWRST_COMMAND_STATUS.
 * Note that while this register contains fields that are supposedly specific to
 * SWDS or SWUS applications, empirical evidence indicates that they are active
 * on root ports as well.  There is no documented place where this strap, as a
 * control value, would end up.
 */
#define	MILAN_STRAP_PCIE_PHY_CALIB_RESET	0x81

/*
 * Most likely PCIECORE::SWRST_EP_CONTROL_0[EP_CFG_RESET_ONLY_EN].  This
 * single-bit field defaults to 0, and the above register is applicable only to
 * endpoints which in our machines this logic never supports.  Actual semantics
 * are unknown.
 */
#define	MILAN_STRAP_PCIE_CFG_REG_RST_ONLY	0x82

/*
 * Most likely PCIECORE::SWRST_EP_CONTROL_0[EP_LNKDWN_RESET_EN].  This
 * single-bit field defaults to 0, and the above register is applicable only to
 * endpoints which in our machines this logic never supports.  Actual semantics
 * are unknown.
 */
#define	MILAN_STRAP_PCIE_LINK_DOWN_RST_EN	0x83

/*
 * This strap is used to seemingly disable all Gen 4 features. This is used when
 * trying to use the special BMC lanes that we thankfully are ignoring. The
 * default is 0x0, Gen 4 is enabled.  There's no documented register
 * corresponding to this for PCIe, but there's a similar NBIF strapping bit in
 * NBIFMM::RCC_BIF_STRAP0[STRAP_BIF_KILL_GEN4], which is a pretty good indicator
 * that these PCIe straps do have their own registers (the reserved strap
 * indices correspond to unused bits in 32-bit strap registers) that are either
 * inaccessible or just undocumented.
 */
#define	MILAN_STRAP_PCIE_GEN4_DIS		0x84

/*
 * This is a power-gating mechanism related to the next two straps, but exactly
 * what it controls isn't known (consider PCIECORE::PCIE_PGMST_CNTL[CFG_PG_EN]).
 * It's a single bit and defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_STATIC_PG_EN		0x85

/*
 * See PCIECORE::PCIE_PGMST_CNTL[CFG_FW_PG_EXIT_CNTL].  This 2-bit field
 * defaults to 0x0.
 */
#define	MILAN_STRAP_PCIE_FW_PG_EXIT_CTL		0x86

/*
 * This presumably relates to the previous two clock gating settings in some
 * way, but we don't know what livmin is (see
 * PCIECORE::CPM_CONTROL[PCIE_CORE_IDLE]). This single-bit strap defaults to
 * 0x0.
 */
#define	MILAN_STRAP_PCIE_LIVMIN_EXIT_CTL	0x87

/*
 * 0x88 is reserved.
 */

/*
 * The next large set of straps all relate to the use of CCIX and AMD's CCIX
 * Enhanced Speed Mode, their CCIX/PCIe extension. The first here controls
 * whether support is advertised at all; it's a single bit that defaults to 0
 * which is where we'll leave it because we don't support this ever.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_SUP		0x89

/*
 * See PCIEPORT::PCIEP_STRAP_LC2[STRAP_ESM_PHY_REACH_LEN_CAP]. This defaults to
 * 0x0 and is 2 bits wide.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_PHY_REACH_CAP	0x8a

/*
 * This controls whether or not a recalibrate is needed and defaults to 0x0.
 * See PCIEPORT::PCIEP_STRAP_LC2[STRAP_ESM_RECAL_NEEDED].
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_RECALIBRATE	0x8b

/*
 * These next several all relate to calibration time and timeouts. Each field is
 * 3 bits wide and defaults to 0.  See PCIEPORT::PCIEP_STRAP_LC2.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_CALIB_TIME	0x8c
#define	MILAN_STRAP_PCIE_CCIX_ESM_QUICK_EQ_TO	0x8d
#define	MILAN_STRAP_PCIE_CCIX_ESM_EQ_PHASE2_TO	0x8e
#define	MILAN_STRAP_PCIE_CCIX_ESM_EQ_PHASE3_TO	0x8f

/*
 * These control the upstream and downstream tx equalization presets. These are
 * both 3 bit fields and default to 0x0.  See
 * PCIERCCFG::ESM_LANE_EQUALIZATION_CNTL_20GT.
 */
#define	MILAN_STRAP_PCIE_CCIX_ESM_DSP_20GT_EQ_TX	0x90
#define	MILAN_STRAP_PCIE_CCIX_ESM_USP_20GT_EQ_TX	0x91

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_CCIX_OPT_TLP_FMT_SUPPORT].  Likely
 * applies to all ports in the core we're strapping, but if CCIX were in use
 * that presumably means there's only one.
 */
#define	MILAN_STRAP_PCIE_CCIX_OPT_TLP_FMT_SUP	0x92

/*
 * 0x93 is reserved.
 */

/*
 * This controls the CCIX vendor ID level value. This 16-bit field defaults to
 * 0x1002; see PCIECORE::PCIE_TX_CCIX_CNTL1.
 */
#define	MILAN_STRAP_PCIE_CCIX_VENDOR_ID		0x94

/*
 * 0x95 is reserved.
 */

/*
 * It's not known whether this 32-bit strap that defaults to 0 corresponds to
 * PCIECORE::PCIE_HW_DEBUG, some other documented register, or something else
 * entirely.  This strap isn't used, but we do set some of these registers from
 * software.
 */
#define	MILAN_STRAP_PCIE_PI_HW_DEBUG		0x96

/*
 * These next two are used to advertise values in the standard device serial
 * number capability for the host bridges in this core.  See PCIe4 7.9.3.  The
 * default values of these two 32-bit fields are 0xc8700 (MSB) and 1 (LSB).
 * Note that the use of non-unique values violates the standard.
 */
#define	MILAN_STRAP_PCIE_SN_LSB			0x97
#define	MILAN_STRAP_PCIE_SN_MSB			0x98

/*
 * These next two straps control our the subsystem vendor and device IDs for the
 * host bridge functions that will be constructed within this core. These are
 * each 16 bits wide and default to 1022,1234.  We set ours to 1de,fff9 which is
 * the subsystem device ID allocated for the Gimlet baseboard.  See notes in
 * milan_fabric.c: this needs to be board-specific and the definitions don't
 * belong here.
 */
#define	MILAN_STRAP_PCIE_SUBVID			0x99
#define	MILAN_STRAP_PCIE_SUBDID			0x9a

/*
 * See PPR 13.5.4.3.1.  This defines the port bifurcation (set of ports to be
 * created and the number of lanes in each) from the 16 lanes available to this
 * core.  This is normally set up by DXIO firmware from the engine
 * configuration.
 */
#define	MILAN_STRAP_PCIE_LINK_CONFIG		0x9b

/*
 * We have no idea what this controls, if anything.  It is 4 bits wide,
 * supposedly defaults to 0, and AMD never uses it.
 */
#define	MILAN_STRAP_PCIE_LINK_CONFIG_PERMUTE	0x9c

/*
 * We have no idea what this controls, if anything. It is 8 bits wide,
 * supposedly defaults to 0xff, and AMD never uses it. The size appears to
 * correspond to the number of ports that exist in the core given the increase
 * from 8 to 9 bits wide from SP3 to SP5.
 */
#define	MILAN_STRAP_PCIE_CHIP_MODE		0x9d

/*
 * 0x9e is reserved.
 */

/*
 * Downstream and upstream RX lane equalization control preset hint. While this
 * is a lane setting, the same preset is used for all lanes in the core. This
 * applies to gen 3 and defaults to 0x3 for downstream, 0x0 for upstream, 3 bits
 * wide.  See PCIe4 7.7.3.4, and note that these apply only to the 8 GT/s EQ
 * procedure; hints don't apply to the 16 GT/s EQ scheme. See the
 * PCIE_GEN3_RX_PRESET_* definitions in <sys/pcie.h> for actual values.
 */
#define	MILAN_STRAP_PCIE_EQ_DS_RX_PRESET_HINT	0x9f
#define	MILAN_STRAP_PCIE_EQ_US_RX_PRESET_HINT	0xa0

/*
 * Gen3 (8 GT/s) transmitter EQ settings for ports used as DS and US
 * respectively. These are 4 bits wide and use the PCIE_TX_PRESET_* encodings in
 * <sys/pcie.h> that are shared with gen4.  Defaults to 0x1 for US and 0x3 for
 * DS.  These establish the values in the Lane Equalization Control registers
 * for all lanes in this core; see PCIe4 7.7.3.4.
 */
#define	MILAN_STRAP_PCIE_EQ_DS_TX_PRESET	0xa1
#define	MILAN_STRAP_PCIE_EQ_US_TX_PRESET	0xa2

/*
 * 16.0 GT/s TX EQ presets. 4 bits wide and defaults to 0x3 for downstream and
 * 0x1 for upstream, establishing the values in the 16 GT/s Lane Equalization
 * Control registers for all lanes in this core.  See PCIe4 7.7.5.9.
 */
#define	MILAN_STRAP_PCIE_16GT_EQ_DS_TX_PRESET	0xa3
#define	MILAN_STRAP_PCIE_16GT_EQ_US_TX_PRESET	0xa4

/*
 * 25.0 GT/s ESM (enhanced speed mode, a CCIX extension we don't use) TX EQ
 * presets. 4 bits wide, defaults to 0xf in both cases.  Presumably the
 * semantics are similar to the foregoing.
 */
#define	MILAN_STRAP_PCIE_25GT_EQ_DS_TX_PRESET	0xa5
#define	MILAN_STRAP_PCIE_25GT_EQ_US_TX_PRESET	0xa6

/*
 * 0xa7 is reserved.
 */

/*
 * This seems to control something called 'quicksim', mysterious. Default is
 * 0x0.  This seems to be meant for validation.  Some clues can be found here;
 * https://www.amd.com/system/files/TechDocs/48692.pdf.  See also
 * PCIECORE::PCIE_STRAP_PI[STRAP_QUICKSIM_START].
 */
#define	MILAN_STRAP_PCIE_QUICKSIM_START		0xa8

/*
 * This is documented as a 31-bit field with a default value of 0; the
 * individual fields it contains are not documented and the only other value we
 * know about is 0x605 which means that the surprise down error reporting
 * capability is reported for the ports in this core in the Link Capabilities
 * register; see PCIe4 6.2 and 7.5.3.6, especially the implementation notes.
 * There are additional steps and sequencing requirements associated with
 * enabling this feature.
 */
#define	MILAN_STRAP_PCIE_WRP_MISC		0xa9
#define	MILAN_STRAP_PCIE_WRP_MISC_SURPDN_EN	0x605

/*
 * This next set all control various ESM speeds it seems. These all default to
 * 0x1 and are 1 bit wide with the exception of the minimum time in electrical
 * idle which is a 9 bit field and defaults to 0x0 and sets the value of
 * PCIERCCFG::PCIE_ESM_STATUS[MIN_TIME_IN_EI_VAL].  The other bits correspond to
 * fields in the CCIX ESM capability registers; the PPR documents these but
 * presumably the real documentation is found only in the CCIX specs.
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
 * These are duplicates of a number of other straps that we've already seen, but
 * applicable to the SWUS functionality which is not found on any machine
 * supported by this kernel.  These all seem to have the same sizes and defaults
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
 * 0xd1 is reserved
 */

/*
 * At this point all of our PCIe straps are now changed to be per port.
 * Each of the 8 possible ports all have the same set of straps; however, each
 * one is 0x61 off from one another (or put differently there are 0x61 straps
 * per port).
 */
#define	MILAN_STRAP_PCIE_NUM_PER_PORT		0x61

/*
 * The relationship between this strap and
 * PCIECORE::SWRST_CONTROL_6[HOLD_TRAINING_x] is not entirely clear.  While the
 * POR value of those bits is 1, the default value of this strap is ostensibly
 * 0.  It's not necessary to set this strap in order for training to be held by
 * default, and it's not known whether setting it causes firmware not to release
 * the HOLD_TRAINING bit automatically during LISM execution.  See the LISM
 * notes in milan_fabric.c.
 */
#define	MILAN_STRAP_PCIE_P_HOLD_TRAINING	0xd2

/*
 * This strap ostensibly is only a single bit wide, which makes it an
 * interesting choice for a mode control bit especially given that we'd expect
 * it to affect PCIEPORT::PCIE_LC_CNTL5[LC_HOLD_TRAINING_MODE].  The default is
 * supposedly 0, but the POR value of that field is 2 and it remains there
 * throughout LISM execution unless changed by software.  There is reason to
 * believe that setting this to 1 specifically means that mode 1 (electrical
 * idle/compliance mode) should be used when training is being held off.
 */
#define	MILAN_STRAP_PCIE_P_LC_HOLD_TRAINING_MODE	0xd3

/*
 * 0xd4 and 0xd5 are reserved
 */

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_AUTO_RC_SPEED_NEGOTIATION_DIS].  This
 * 1-bit strap defaults to 0x0, meaning automatic speed negotiation is enabled
 * on this root port. There is a second variant of this for 16 GT/s operation
 * (e.g. gen 4), but it doesn't correspond to any documented register field, has
 * no observable effect on private register state, and its semantics are
 * unknown.
 */
#define	MILAN_STRAP_PCIE_P_RC_SPEED_NEG_DIS	0xd6
#define	MILAN_STRAP_PCIE_P_RC_SPEED_NEG_16GT_DIS	0xd7

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_INIT_SPEED_NEG_IN_L{1,0s}_EN].
 * Both these single-bit straps default to 0x1, allowing speed negotiation in L1
 * (and in principle the unsupported L0s).
 */
#define	MILAN_STRAP_PCIE_P_L0s_SPEED_NEG_EN	0xd8
#define	MILAN_STRAP_PCIE_P_L1_SPEED_NEG_EN	0xd9

/*
 * See PCIEPORT::PCIE_LC_SPEED_CNTL[LC_TARGET_LINK_SPEED_OVERRIDE_EN].  This
 * strap seems to provide a means of saying that a target speed override should
 * be enabled. This presumably pairs with the target link speed strap below
 * which would contain the actual setting. This single-bit strap defaults to
 * 0x0, disabled.
 */
#define	MILAN_STRAP_PCIE_P_TARG_LINK_SPEED_EN	0xda

/*
 * See PCIEPORT::PCIE_LC_CNTL4[LC_BYPASS_EQ_{,REQ_PHASE_}8GT].  The default
 * values of these single-bit fields is 0, which leaves the normal gen3 EQ
 * process intact.
 */
#define	MILAN_STRAP_PCIE_P_8GT_BYPASS_EQ	0xdb
#define	MILAN_STRAP_PCIE_P_8GT_BYPASS_EQ_REQ	0xdc

/*
 * See PCIEPORT::PCIE_LC_CNTL4[LC_EQ_SEARCH_MODE_8GT].  This is a two bit field
 * that defaults to 0x3 (preset search); see PPR for encodings.
 */
#define	MILAN_STRAP_PCIE_P_8GT_EQ_SEARCH_MODE	0xdd

/*
 * These next three are the Gen 4 variants of the Gen 3 bits above. They have
 * the same sizes and default values; see corresponding fields in
 * PCIEPORT::PCIE_LC_CNTL8.
 */
#define	MILAN_STRAP_PCIE_P_16GT_BYPASS_EQ	0xde
#define	MILAN_STRAP_PCIE_P_16GT_BYPASS_EQ_REQ	0xdf
#define	MILAN_STRAP_PCIE_P_16GT_EQ_SEARCH_MODE	0xe0

/*
 * This strap works in tandem with the MILAN_STRAP_PCIE_P_TARG_LINK_SPEED_EN
 * strap above; see PCIEPORT::PCIE_LC_SPEED_CNTL[LC_TARGET_LINK_SPEED]. This
 * controls what the target link speed of the port would be. It is a two bit
 * field that defaults to 0x3.  See PPR for encodings.
 */
#define	MILAN_STRAP_PCIE_P_TARG_LINK_SPEED	0xe1

/*
 * 0xe2 and 0xe3 are reserved
 */

/*
 * See PCIEPORT::LC_CNTL[LC_L{1,0S}_INACTIVITY].  Both 4-bit straps default to
 * 0, which disables entering the corresponding state.  Note that the L1 time is
 * applicable only to upstream ports, which ours never are.
 */
#define	MILAN_STRAP_PCIE_P_L0s_INACTIVITY	0xe4
#define	MILAN_STRAP_PCIE_P_L1_INACTIVITY	0xe5

/*
 * 0xe6 is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_CNTL2[LC_ELEC_IDLE_MODE]; the 2-bit field shares
 * encodings with PCIE_PORT_LC_CTL2_ELEC_IDLE_xx and defaults to 1.  Note that
 * we set this from software instead of strapping it.
 */
#define	MILAN_STRAP_PCIE_P_ELEC_IDLE_MODE	0xe7

/*
 * See PCIERCCFG::LINK_CAP[PM_SUPPORT], especially the contradictory notes in
 * the PPR, and PCIe4 7.5.3.6.  One might imagine this 2-bit strap, which
 * defaults to 3, actually does something, but empirically it appears this
 * capability field really is hardwired to 2.  If the strap does anything, its
 * effects aren't visible.  See also PCIEPORT::PCIE_LC_CNTL[LC_ASPM_TO_L1_DIS].
 */
#define	MILAN_STRAP_PCIE_P_ASPM_SUP		0xe8

/*
 * These next two straps control the defined exit latency values for L0s and L1
 * in the otherwise read-only Link Capabilities register; see PCIe4 7.5.3.6.
 * These 3-bit fields default to 6 for L1 and 3 for L0s.  Note that L0s isn't
 * supported (see previous strap), so the implementation note in PCIe4 5.4.1.1
 * applies and the recommended value is 7 (>4 microseconds); however, our system
 * never has the kind of legacy software that note describes so this is of
 * little importance.
 */
#define	MILAN_STRAP_PCIE_P_L1_EXIT_LAT		0xe9
#define	MILAN_STRAP_PCIE_P_L0s_EXIT_LAT		0xea

/*
 * It isn't exactly clear what this does. Based on the fact that it's in the
 * ASPM group and that it's a 1-bit field that defaults to 0x1, we can wager
 * that this is used to control some amount of signalling when the link exits an
 * L1 state.  It doesn't correspond to any documented register or field, and has
 * no observable effect on any PCIe port registers.
 */
#define	MILAN_STRAP_PCIE_P_L1_EXIT_SIGNAL	0xeb

/*
 * 0xec is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_BW_CHANGE_CNTL[LC_LINK_BW_NOTIFICATION_DETECT_MODE].
 * This single-bit strap defaults to 0.
 */
#define	MILAN_STRAP_PCIE_P_LINK_BW_NOTIF_DETECT_MODE	0xed

/*
 * See PCIEPORT::PCIE_LC_CNTL7[LC_AUTO_REJECT_AFTER_TIMEOUT].  This single-bit
 * strap is documented to default to 1, but whether one leaves it alone or
 * explicitly straps it to 1, firmware clears the bit anyway before the LISM
 * reaches the CONFIGURED state, making the strap effectively useless.
 */
#define	MILAN_STRAP_PCIE_P_LINK_EQ_DISCARD_AFTER_TIMEOUT	0xee

/*
 * See PCIEPORT::PCIE_LC_CNTL9[LC_EX_SEARCH_TRAVERSAL_MODE].  This single-bit
 * strap defaults to 0.
 */
#define	MILAN_STRAP_PCIE_P_LINK_EQ_SEARCH_MODE	0xef

/*
 * 0xf0 is reserved.
 */

/*
 * One would be forgiven for supposing that this controls the initial value of
 * the Selectable De-emphasis bit in the Link Control 2 register, which is
 * relevant only when the link is operating at 5 GT/s (see PCIe4 7.5.3.19).  But
 * while this single-bit strap is supposed to default to 1, all the root ports
 * have that bit clear in the control register when this strap is not set.  In
 * fact, the PPR states that the control bit is read-only and always 0, which is
 * true and matches the PCIe4 definition.  One might also think that this strap
 * actually controls PCIEPORT::PCIE_LC_CNTL3[LC_SELECT_DEEMPHASIS_CNTL] to force
 * the deemphasis to -3.5 dB if strapped to 1, which would in turn show up in
 * the Link Status 2 standard register even though the Link Control 2 field
 * still reads 0.  But empirically, the control register bit is always 0, the
 * status register bit is always 1, LC_SELECT_DEEMPHASIS_CNTL is always 3, and
 * if this strap does anything at all, it's impossible to see.
 */
#define	MILAN_STRAP_PCIE_P_DEEMPH_SEL		0xf1

/*
 * These next two straps are used to control the indication of retimer presence
 * detection support. These show up in the Link Capabilities 2 register; see
 * PCIe4 7.5.3.18 and PCIEPORT::PCIEP_STRAP_LC[STRAP_RTM{1,2}_PRESENCE_DET_SUP],
 * which are controlled by these straps.  The default for both of these
 * single-bit straps is 0, meaning that the capability of detecting retimers is
 * not advertised.  Curiously, however, even with these bits clear, both are set
 * in the standard capability register for the root port despite the PPR's claim
 * that the read-only capability bits are always 0.  It's unclear whether the
 * PCIe core provides this support or external logic would be needed for it to
 * work (i.e., whether this does anything other than simply advertise the
 * capability).
 */
#define	MILAN_STRAP_PCIE_P_RETIMER1_DET_SUP	0xf2
#define	MILAN_STRAP_PCIE_P_RETIMER2_DET_SUP	0xf3

/*
 * Allows changing the timeout values used by the LTSSM, primarily for
 * simulation or validation.  See PCIEPORT::PCIE_LC_CNTL2[LC_TEST_TIMER_SEL].
 * This 2-bit field defaults to 0, which codes as PCIe-compliant values.  Don't
 * touch.
 */
#define	MILAN_STRAP_PCIE_P_TEST_TIMER_SEL	0xf4

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_MARGINING_USES_SOFTWARE].  This single-bit
 * strap defaults to 0, meaning that software (whatever it would do!) is
 * apparently not needed for RX margining.  We don't know what happens if one
 * sets this, so leave it alone.
 */
#define	MILAN_STRAP_PCIE_P_MARGIN_NEEDS_SW	0xf5

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
#define	MILAN_STRAP_PCIE_P_AUTO_DIS_SPEED_SUP_EN	0xf6

/*
 * These per-link-speed straps are each 2 bits wide. 2.5GT and 5.0GT default to
 * 0, while 8 GT defaults to 1, and 16 GT to 2.  These correspond to fields in
 * PCIEPORT::PCIE_LC_CNTL6, which almost certainly should not be changed from
 * the recommended values.
 */
#define	MILAN_STRAP_PCIE_P_SPC_MODE_2P5GT	0xf7
#define	MILAN_STRAP_PCIE_P_SPC_MODE_5GT		0xf8
#define	MILAN_STRAP_PCIE_P_SPC_MODE_8GT		0xf9
#define	MILAN_STRAP_PCIE_P_SPC_MODE_16GT	0xfa

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
#define	MILAN_STRAP_PCIE_P_SRIS_EN		0xfb
#define	MILAN_STRAP_PCIE_P_AUTO_SRIS_EN		0xfc

/*
 * Single-bit field controlling PCIEPORT::PCIE_LC_CNTL4[LC_TX_SWING].  The
 * default is 0, full-swing mode for the transmitters on this port.  See PCIe4
 * 8.3.3.10, 4.2.3.1, and ch. 8 generally.
 */
#define	MILAN_STRAP_PCIE_P_TX_SWING		0xfd

/*
 * See PCIEPORT::PCIE_LC_CNTL5[LC_ACCEPT_ALL_PRESETS{,_TEST}].  These default to
 * 0 and are relevant only in reduced swing mode which is not currently used.
 */
#define	MILAN_STRAP_PCIE_P_ACCEPT_PRESETS	0xfe
#define	MILAN_STRAP_PCIE_P_ACCEPT_PRESETS_TEST	0xff

/*
 * This controls how long the PHY has for Figure of Merit (FOM). This is a 2-bit
 * field and defaults to 0x0.  See PCIEPORT::PCIE_LC_CNTL8[LC_FOM_TIME].  The
 * FOM is defined by PCIe4 8.5.1.5 for 8 and 16 GT/s only as the open eye area;
 * however the minimum eye opening is only 0.1 UI and the total eye width is
 * never going to be even close to 0.5 UI; the UI for 8 GT/s is 125 ps, so the
 * most reasonable interpretation of these values is an interval of time over
 * which the phy is allowed to assess the FOM in evaluating EQ parameters.  The
 * actual semantics are undocumented, but see also
 * PCIEPORT::PCIE_LC_TRAINING_CNTL[LC_WAIT_FOR_FOM_VALID_AFTER_TRACK].
 */
#define	MILAN_STRAP_PCIE_P_FOM_TIME		0x100
#define	MILAN_STRAP_PCIE_P_FOM_300US		0
#define	MILAN_STRAP_PCIE_P_FOM_200US		1
#define	MILAN_STRAP_PCIE_P_FOM_100US		2
#define	MILAN_STRAP_PCIE_P_FOM_SUB_100US	3

/*
 * See PCIEPORT::LC_CNTL8[LC_SAFE_EQ_SEARCH].  Single bit, defaults to 0.
 */
#define	MILAN_STRAP_PCIE_P_EQ_SAFE_SEARCH	0x101

/*
 * See PCIEPORT::LC_CNTL10[LC_ENH_PRESET_SEARCH_SEL_{8,16}GT].  2 bits each,
 * default to 0.  It's unclear whether these straps have an effect if
 * PCIEPORT::PCIE_LC_CNTL{4,8}[LC_SEARCH_MODE_{8,16}GT] is set to preset mode
 * earlier/later by software or only if the corresponding strap also sets preset
 * search mode at during the strapping process.  See also
 * MILAN_STRAP_PCIE_P_{8,16}GT_EQ_SEARCH_MODE above.
 */
#define	MILAN_STRAP_PCIE_P_8GT_PRESET_SEARCH_SEL	0x102
#define	MILAN_STRAP_PCIE_P_16GT_PRESET_SEARCH_SEL	0x103

/*
 * These 10-bit fields default to 0 and correspond to
 * PCIEPORT::PCIE_LC_CNTL10[LC_PRESET_MASK_{8,16}GT].
 */
#define	MILAN_STRAP_PCIE_P_8GT_PRESET_MASK	0x104
#define	MILAN_STRAP_PCIE_P_16GT_PRESET_MASK	0x105

/*
 * 0x106 is reserved.
 */

/*
 * See PCIEPORT::PCIE_ERR_CNTL[STRAP_POISONED_ADVISORY_NONFATAL].  Single-bit
 * strap defaults to 0; presumably this exists as a strap to allow progress to
 * be made if a device below the port sends poisoned TLPs before software has an
 * opportunity to set this bit directly.
 */
#define	MILAN_STRAP_PCIE_P_POISON_ADV_NF	0x107

/*
 * Empirical results indicate that this strap sets the otherwise read-only MPS
 * field in the bridge's Device Capabilities register.  The 3-byte field
 * defaults to 2 and the encodings are those in the capability.  See PCIe4
 * 7.5.3.3.  Note that this appears to set the capability field *directly*, not
 * by changing the private override values in PCIEPORT::PCIEP_PORT_CNTL or
 * PCIEPORT::PCIE_CONFIG_CNTL; those registers aren't affected by this strap,
 * and firmware always sets the mode bits such that the devctl value is used.
 */
#define	MILAN_STRAP_PCIE_P_MAX_PAYLOAD_SUP	0x108

/*
 * See PCIEPORT::PCIE_ERR_CNTL[STRAP_FIRST_RCVD_ERR_LOG].  This single-bit field
 * defaults to 0.
 */
#define	MILAN_STRAP_PCIE_P_LOG_FIRST_RX_ERR	0x109

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_EXTENDED_FMT_SUPPORTED] and PCIe4
 * 7.5.3.15.  This value ends up in the Device Capabilities 2 register.  It's a
 * single bit and defaults to 0; note that PCIe4 strongly recommends enabling
 * this capability so that the 3-bit TLP fmt field is available to support
 * end-to-end TLP prefixes.
 */
#define	MILAN_STRAP_PCIE_P_EXT_FMT_SUP		0x10a

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_E2E_PREFIX_EN].  Almost certainly needs
 * the previous strap to be set in order to work; this single-bit field defaults
 * to 0.
 */
#define	MILAN_STRAP_PCIE_P_E2E_TLP_PREFIX_EN	0x10b

/*
 * We know what this sets, but not what it means.  The single-bit field defaults
 * to 0 and controls PCIEPORT::PCIEP_BCH_ECC_CNTL[STRAP_BCH_ECC_EN], which is
 * supposedly reserved.  BCH is defined nowhere in PCIe4 or the PPR, so what
 * does it mean?  Well, setting this bit means your machine will hang hard when
 * PCIe traffic happens later on.
 */
#define	MILAN_STRAP_PCIE_P_BCH_ECC_EN		0x10c

/*
 * This controls whether the port supports the ECRC being regenerated when
 * dealing with multicast transactions, and specifically the corresponding bit
 * in the Multicast Capability register; see PCIe4 7.9.11.2.  This single-bit
 * field defaults to 0 and sets the otherwise read-only capability bit directly,
 * which will matter only if multicast is also enabled
 * (MILAN_STRAP_PCIE_MCAST_EN).
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
 * buffers from over- or underflowing.  The default values are 0.  See PCIe4
 * 7.5.3.18; these settings end up in the Link Capabilities 2 register.
 */
#define	MILAN_STRAP_PCIE_P_LOW_SKP_OS_GEN_SUP	0x10e
#define	MILAN_STRAP_PCIE_P_LOW_SKP_OS_RCV_SUP	0x10f

/*
 * These next two also relate to the Device Capabilities 2 register and whether
 * or not the port supports the 10-bit tag completer or requester respectively.
 * These single-bit fields default to 0 and set the otherwise read-only
 * capabilities directly, not via the overrides in PCIEPORT::PCIE_CONFIG_CNTL.
 */
#define	MILAN_STRAP_PCIE_P_10B_TAG_CMPL_SUP	0x110
#define	MILAN_STRAP_PCIE_P_10B_TAG_REQ_SUP	0x111

/*
 * This controls whether or not the CCIX vendor specific cap is advertised or
 * not.  This single-bit field defaults to 0.
 */
#define	MILAN_STRAP_PCIE_P_CCIX_EN		0x112

/*
 * 0x113 is reserved
 */

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_LANE_NEGOTIATION].  This 3-bit field's
 * encodings are the same, and it defaults to 0.
 */
#define	MILAN_STRAP_PCIE_P_LANE_NEG_MODE	0x114

/*
 * 0x115 is reserved
 */

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_BYPASS_RCVR_DET].  The single-bit field
 * defaults to 0, meaning we get the behaviour described in PCIe4 ch. 4.  Don't
 * touch.
 */
#define	MILAN_STRAP_PCIE_P_BYPASS_RX_DET	0x116

/*
 * See PCIEPORT::PCIEP_STRAP_LC[STRAP_FORCE_COMPLIANCE].  This single-bit field
 * defaults to 0, allowing the LTSSM to transition normally.
 */
#define	MILAN_STRAP_PCIE_P_COMPLIANCE_FORCE	0x117

/*
 * This is the opposite of the one above and seems to allow for compliance mode
 * to be disabled entirely.  Note that doing this is a gross violation of PCIe4
 * 4.2.5.2.  The single-bit field defaults to 0, meaning compliance mode is
 * supported.  See PCIEPORT::PCIEP_STRAP_LC[STRAP_COMPLIANCE_DIS].
 */
#define	MILAN_STRAP_PCIE_P_COMPLIANCE_DIS	0x118

/*
 * See PCIEPORT::PCIE_LC_CNTL2[LC_X12_NEGOTIATION_DIS].  This single-bit field
 * defaults to 1, which causes this bit to be set and x12 negotiation to be
 * prohibited.
 */
#define	MILAN_STRAP_PCIE_P_NEG_X12_DIS		0x119

/*
 * See PCIEPORT::PCIEP_STRAP_MISC[STRAP_REVERSE_LANES].  Setting this single-bit
 * strap forces lane reversal; the default of 0 allows autonegotiation.  This
 * shouldn't normally be used and instead we should use the features built into
 * the DXIO subsystem for communicating reversal.
 */
#define	MILAN_STRAP_PCIE_P_REVERSE_LANES	0x11a

/*
 * 0x11b is reserved.
 */

/*
 * See PCIEPORT::PCIE_LC_CNTL3[LC_ENHANCED_HOT_PLUG_EN] and AMD's AGESA
 * specification for a description of enhanced hotplug mode.  This mode is not
 * supported by this kernel and this single-bit strap should be left at its
 * default value of 0.
 */
#define	MILAN_STRAP_PCIE_P_ENHANCED_HP_EN	0x11c

/*
 * 0x11d is reserved.
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
#define	MILAN_STRAP_PCIE_P_FTS_TS_COUNT		0x11e
#define	MILAN_STRAP_PCIE_P_FTS_INIT_NUM		0x11f

/*
 * Sets the device ID for SWUS, defaulting to 0x148d.  This doesn't exist in any
 * processor we support.
 */
#define	MILAN_STRAP_PCIE_P_DEVID		0x120

/*
 * This strap is a bit mysterious. It basically is used to indicate if it is a
 * 'SB'. In normal AMD parlance this might be something related to controlling
 * or indicating whether this is a southbridge; however, here, that's less
 * obvious. All we know is that this is a 1-bit field that defaults to 0x0.  AMD
 * doesn't document nor use this strap, and we shouldn't either.
 */
#define	MILAN_STRAP_PCIE_P_IS_SB		0x121

/*
 * 0x122 is reserved
 */

/*
 * These next few fields all relate to the L1 PM substates capability and
 * various features there. Each of these straps is a single bit; PCIPM_L1P1 and
 * PM_SUB_SUP default to 1 and the others to 0, according to AMD's
 * documentation.  However, in practice, ASPM_L1P1_SUP is also set by default.
 * See PCIe4 7.8.3.2; these bits control the corresponding values in the L1 PM
 * Substates Capabilities register.  Note that the PPR claims these bits are
 * read/write there, which somewhat surprisingly is true.
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
 * this to 0xa.  See PCIEPORT::PCIE_LC_L1_PM_SUBSTATE2[LC_CM_RESTORE_TIME] and
 * PCIe4 7.8.3.3 which describes the field in the L1 PM substates control
 * register that this value overrides.
 */
#define	MILAN_STRAP_PCIE_P_TCOMMONMODE_TIME	0x129

/*
 * This presumably sets the default Tpower_on scale value in the L1 PM Substates
 * Control 2 register. This is a 2-bit field that defaults to 0x0, indicating
 * that the scale of Tpower_on is 2us. It appears software is expected to
 * overwrite this to 0x1, indicating 10us.  See PCIe4 7.8.3.4.  This appears to
 * set the standard register directly rather than putting the value in the
 * override field PCIEPORT::PCIE_LC_L1_PM_SUBSTATE[LC_T_POWER_ON_SCALE].
 */
#define	MILAN_STRAP_PCIE_P_TPON_SCALE		0x12a

/*
 * 0x12b is reserved
 */

/*
 * This goes along with MILAN_STRAP_PCIE_P_TPON_SCALE and sets the value that
 * should be there. A companion to our friend above. This is a 5-bit register
 * and the default value is 0x5. It seems software may be expected to set this
 * to 0xf, meaning (with the preceding) 150 microseconds.
 */
#define	MILAN_STRAP_PCIE_P_TPON_VALUE		0x12c

/*
 * 0x12d is reserved
 */

/*
 * The next two straps are related to the PCIe Gen 4 data link feature
 * capability. The first controls whether this is supported or not while the
 * latter allows for feature exchange to occur. These both default to 0x0,
 * indicating that they are not supported and enabled respectively.  See PCIe4
 * 3.3 and 7.7.4.  The first causes bit 0 to be set in the Local Data Link
 * Feature Supported field of the capability register; the second causes bit 31
 * to be set.  Note that the corresponding capability bits are otherwise
 * read-only.
 */
#define	MILAN_STRAP_PCIE_P_DLF_SUP		0x12e
#define	MILAN_STRAP_PCIE_P_DLF_EXCHANGE_EN	0x12f

/*
 * This strap controls the header scaling factor used in scaled flow control.
 * Specifically, it is the HdrScale value to be transmitted and is a 2-bit field
 * that defaults to 0x0.  See PCIe4 3.4.2.  This doesn't appear to correspond to
 * any documented register.
 */
#define	MILAN_STRAP_PCIE_P_DLF_HDR_SCALE_MODE	0x130

/*
 * 0x131 is reserved
 */

/*
 * This strap controls PCIEPORT::PCIE_LC_PORT_ORDER and has a different default
 * value for each of the 8 possible ports (0, 8, 0xa, 0xc, 0xe, 0xf, 0xf, 0xf).
 * See the discussion on port reordering and bifurcation in the PPR 13.5.4.3.
 * There is normally no need to change this as firmware will configure a default
 * port ordering from the DXIO engine data; however, this would allow alternate
 * ordering if needed.  Our implementation is indifferent to port ordering and
 * doesn't set these straps.
 */
#define	MILAN_STRAP_PCIE_P_PORT_OFF		0x132

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_PCIE_RSMU_H */
