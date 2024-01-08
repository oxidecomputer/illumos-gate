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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_IO_FCH_PMIO_H
#define	_SYS_IO_FCH_PMIO_H

/*
 * FCH::PM is notionally power management, but in fact it's one of several
 * dumping grounds in the FCH covering everything from the hardware
 * implementation of ACPI to decoding control to clock gating to voltage
 * thresholds and much more.  If you're looking for something that in any
 * way controls low-speed functionality, power states, or legacy PC-ish
 * functions that actually appear elsewhere, this is probably a good place to
 * start looking.  We've defined the PMIO functional unit to be the *first*
 * chunk of FCH::PM, choosing to refer to the *second* chunk as a separate
 * FCH_ACPI functional unit, as the two regions are discontiguous and the ACPI
 * registers are more or less defined by that standard.
 */

#ifndef	_ASM
#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/amdzen/mmioreg.h>
#endif	/* !_ASM */

#include <sys/amdzen/fch.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	FCH_PMIO_OFF		0x0300
#define	FCH_PMIO_SMN_BASE	(FCH_RELOCATABLE_SMN_BASE + FCH_PMIO_OFF)
#define	FCH_PMIO_PHYS_BASE	(FCH_RELOCATABLE_PHYS_BASE + FCH_PMIO_OFF)
#define	FCH_PMIO_SIZE		0x100

/*
 * Not all registers are included here; there are far more in the PPRs.  These
 * are the ones we use or have used in the past.  More can be added as
 * required.
 */

#define	FCH_PMIO_REGOFF_DECODEEN		0x00
#define	FCH_PMIO_REGOFF_PCICONTROL		0x08
#define	FCH_PMIO_REGOFF_ACPICONFIG		0x74
#define	FCH_PMIO_REGOFF_RESETCONTROL1		0xbe
#define	FCH_PMIO_REGOFF_S5_RESET_STATUS		0xc0
#define	FCH_PMIO_REGOFF_ALTMMIOEN		0xd5
#define	FCH_PMIO_REGOFF_ALTMMIOBASE		0xd6

#ifndef	_ASM

MAKE_SMN_FCH_REG_FN(PMIO, pmio, FCH_PMIO_SMN_BASE, FCH_PMIO_SIZE, 4);
MAKE_MMIO_FCH_RELOC_REG_BLOCK_FNS(PMIO, pmio, FCH_PMIO_OFF, FCH_PMIO_SIZE);
MAKE_MMIO_FCH_REG_FN(PMIO, pmio, 4);

/*
 * FCH::PM::DECODEEN.  Controls not only whether the FCH decodes various
 * additional MMIO and legacy IO ranges but also has a few configuration bits
 * for other functional units.
 */
/*CSTYLED*/
#define	D_FCH_PMIO_DECODEEN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,	\
	.srd_reg = FCH_PMIO_REGOFF_DECODEEN	\
}
#define	FCH_PMIO_DECODEEN()	fch_pmio_smn_reg(D_FCH_PMIO_DECODEEN, 0)
#define	FCH_PMIO_DECODEEN_MMIO(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_DECODEEN, 0)

#define	FCH_PMIO_DECODEEN_GET_IOAPICCFG(r)		bitx32(r, 31, 30)
#define	FCH_PMIO_DECODEEN_SET_IOAPICCFG(r, v)		bitset32(r, 31, 30, v)
#define	FCH_PMIO_DECODEEN_IOAPICCFG_LOW_LAT	3
#define	FCH_PMIO_DECODEEN_GET_HPET_MSI_EN(r)		bitx32(r, 29, 29)
#define	FCH_PMIO_DECODEEN_SET_HPET_MSI_EN(r)		bitset32(r, 29, 29, v)
#define	FCH_PMIO_DECODEEN_GET_HPET_WIDTH_SEL(r)		bitx32(r, 28, 28)
#define	FCH_PMIO_DECODEEN_SET_HPET_WIDTH_SEL(r, v)	bitset32(r, 28, 28, v)
#define	FCH_PMIO_DECODEEN_HPET_WIDTH_32	0
#define	FCH_PMIO_DECODEEN_HPET_WIDTH_64	1
#define	FCH_PMIO_DECODEEN_GET_WDTOPTS(r)		bitx32(r, 27, 26)
#define	FCH_PMIO_DECODEEN_SET_WDTOPTS(r, v)		bitset32(r, 27, 26, v)
#define	FCH_PMIO_DECODEEN_WDTOPTS_NORMAL	0
#define	FCH_PMIO_DECODEEN_GET_WDTPER(r)			bitx32(r, 25, 24)
#define	FCH_PMIO_DECODEEN_SET_WDTPER(r, v)		bitset32(r, 25, 24, v)
#define	FCH_PMIO_DECODEEN_WDTPER_32US	0
#define	FCH_PMIO_DECODEEN_WDTPER_10MS	1
#define	FCH_PMIO_DECODEEN_WDTPER_100MS	2
#define	FCH_PMIO_DECODEEN_WDTPER_1S	3
#define	FCH_PMIO_DECODEEN_GET_ASFCLKSEL(r)		bitx32(r, 23, 21)
#define	FCH_PMIO_DECODEEN_SET_ASFCLKSEL(r, v)		bitset32(r, 23, 21, v)
#define	FCH_PMIO_DECODEEN_ASFCLK_100K	0
#define	FCH_PMIO_DECODEEN_ASFCLK_200K	1
#define	FCH_PMIO_DECODEEN_ASFCLK_300K	2
#define	FCH_PMIO_DECODEEN_ASFCLK_400K	3
#define	FCH_PMIO_DECODEEN_ASFCLK_600K	4
#define	FCH_PMIO_DECODEEN_ASFCLK_800K	5
#define	FCH_PMIO_DECODEEN_ASFCLK_900K	6
#define	FCH_PMIO_DECODEEN_ASFCLK_1M	7
#define	FCH_PMIO_DECODEEN_GET_SMBUS0SEL(r)		bitx32(r, 20, 19)
#define	FCH_PMIO_DECODEEN_SET_SMBUS0SEL(r, v)		bitset32(r, 20, 19, v)
#define	FCH_PMIO_DECODEEN_SMBUS_ONBOARD	0
#define	FCH_PMIO_DECODEEN_SMBUS_TSI	1
#define	FCH_PMIO_DECODEEN_GET_ASFCLKOVR(r)		bitx32(r, 18, 18)
#define	FCH_PMIO_DECODEEN_SET_ASFCLKOVR(r, v)		bitset32(r, 18, 18, v)
#define	FCH_PMIO_DECODEEN_GET_ASFCLKSTRETCHEN(r)	bitx32(r, 17, 17)
#define	FCH_PMIO_DECODEEN_SET_ASFCLKSTRETCHEN(r, v)	bitset32(r, 17, 17, v)
#define	FCH_PMIO_DECODEEN_GET_ASFSMMASTEREN(r)		bitx32(r, 16, 16)
#define	FCH_PMIO_DECODEEN_SET_ASFSMMASTEREN(r, v)	bitset32(r, 16, 16, v)
#define	FCH_PMIO_DECODEEN_GET_SMBUSASFIOBASE(r)		bitx32(r, 15, 8)
#define	FCH_PMIO_DECODEEN_SET_SMBUSASFIOBASE(r, v)	bitset32(r, 15, 8, v)
#define	FCH_PMIO_DECODEEN_SMBUSASFIOBASE_SHIFT	8
#define	FCH_PMIO_DECODEEN_GET_WDTEN(r)			bitx32(r, 7, 7)
#define	FCH_PMIO_DECODEEN_SET_WDTEN(r)			bitset32(r, 7, 7, v)
#define	FCH_PMIO_DECODEEN_GET_HPETEN(r)			bitx32(r, 6, 6)
#define	FCH_PMIO_DECODEEN_SET_HPETEN(r, v)		bitset32(r, 6, 6, v)
#define	FCH_PMIO_DECODEEN_GET_IOAPICEN(r)		bitx32(r, 5, 5)
#define	FCH_PMIO_DECODEEN_SET_IOAPICEN(r, v)		bitset32(r, 5, 5, v)
#define	FCH_PMIO_DECODEEN_GET_SMBUSASFIOEN(r)		bitx32(r, 4, 4)
#define	FCH_PMIO_DECODEEN_SET_SMBUSASFIOEN(r, v)	bitset32(r, 4, 4, v)
#define	FCH_PMIO_DECODEEN_GET_DMAPORT80(r)		bitx32(r, 3, 3)
#define	FCH_PMIO_DECODEEN_SET_DMAPORT80(r, v)		bitset32(r, 3, 3, v)
#define	FCH_PMIO_DECODEEN_GET_LEGACYDMAIOEN(r)		bitx32(r, 2, 2)
#define	FCH_PMIO_DECODEEN_SET_LEGACYDMAIOEN(r, v)	bitset32(r, 2, 2, v)
#define	FCH_PMIO_DECODEEN_GET_CF9IOEN(r)		bitx32(r, 1, 1)
#define	FCH_PMIO_DECODEEN_SET_CF9IOEN(r, v)		bitset32(r, 1, 1, v)
#define	FCH_PMIO_DECODEEN_GET_LEGACYIOEN(r)		bitx32(r, 0, 0)
#define	FCH_PMIO_DECODEEN_SET_LEGACYIOEN(r, v)		bitset32(r, 0, 0, v)

/*
 * FCH::PM::PCICONTROL.  Another garbage barge of random configuration;
 * we only care about shutdownoption (bit 20).
 */
/*CSTYLED*/
#define	D_FCH_PMIO_PCICONTROL	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,	\
	.srd_reg = FCH_PMIO_REGOFF_PCICONTROL	\
}
#define	FCH_PMIO_PCICONTROL_SMN()	\
    fch_pmio_smn_reg(D_FCH_PMIO_PCICONTROL, 0)
#define	FCH_PMIO_PCICONTROL_MMIO(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_PCICONTROL, 0)

#define	FCH_PMIO_PCICONTROL_SET_SHUTDOWNOPTION(r, v)	bitset32(r, 20, 20, v)

/*
 * FCH::PM::ACPICONFIG.
 */
/*CSTYLED*/
#define	D_FCH_PMIO_ACPICONFIG	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,	\
	.srd_reg = FCH_PMIO_REGOFF_ACPICONFIG \
}
#define	FCH_PMIO_ACPICONFIG_SMN()	\
    fch_pmio_smn_reg(D_FCH_PMIO_ACPICONFIG, 0)
#define	FCH_PMIO_ACPICONFIG_MMIO(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_ACPICONFIG, 0)

#define	FCH_PMIO_ACPICONFIG_SET_EN_SHUTDOWN_MSG(r, v) bitset32(r, 17, 17, v)

/*
 * FCH::PM::RESETCONTROL1.
 */
/*CSTYLED*/
#define	D_FCH_PMIO_RESETCONTROL1 (const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,		\
	.srd_reg = FCH_PMIO_REGOFF_RESETCONTROL1, \
	.srd_size = 1				\
}
#define	FCH_PMIO_RESETCONTROL1_SMN()	\
    fch_pmio_smn_reg(D_FCH_PMIO_RESETCONTROL1, 0)
#define	FCH_PMIO_RESETCONTROL1_MMIO(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_RESETCONTROL1, 0)

#define	FCH_PMIO_RESETCONTROL1_SET_RSTTOCPUPWRGDEN(r, v) bitset8(r, 7, 7, v)
#define	FCH_PMIO_RESETCONTROL1_SET_KBRSTEN(r, v)	bitset8(r, 4, 4, v)

/*
 * FCH::PM::S5_RESET_STATUS.  Used to detect a furtive reset.
 */
/*CSTYLED*/
#define	D_FCH_PMIO_S5_RESET_STATUS (const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,		\
	.srd_reg = FCH_PMIO_REGOFF_S5_RESET_STATUS \
}
#define	FCH_PMIO_S5_RESET_STATUS_SMN()	\
    fch_pmio_smn_reg(D_FCH_PMIO_S5_RESET_STATUS, 0)
#define	FCH_PMIO_S5_RESET_STATUS_MMIO(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_S5_RESET_STATUS, 0)

#define FCH_PMIO_S5_RESET_STATUS_GET_SW_SYNC_FLOOD_FLAG(r)	\
    bitx32(r, 31, 31)
#define FCH_PMIO_S5_RESET_STATUS_GET_SDP_PARITY_ERR(r)	bitx32(r, 30, 30)
#define	FCH_PMIO_S5_RESET_STATUS_GET_MP1_WDTOUT(r)	bitx32(r, 29, 29)
#define	FCH_PMIO_S5_RESET_STATUS_GET_SYNC_FLOOD(r)	bitx32(r, 27, 27)
#define	FCH_PMIO_S5_RESET_STATUS_GET_REMOTERESETFROMASF(r) bitx32(r, 26, 26)
#define	FCH_PMIO_S5_RESET_STATUS_GET_WATCHDOGISSUERESET(r) bitx32(r, 25, 25)
#define	FCH_PMIO_S5_RESET_STATUS_GET_FAILBOOTRST(r)	bitx32(r, 24, 24)
#define	FCH_PMIO_S5_RESET_STATUS_GET_SHUTDOWN_MSG(r)	bitx32(r, 23, 23)
#define	FCH_PMIO_S5_RESET_STATUS_GET_KB_RESET(r)	bitx32(r, 22, 22)
#define	FCH_PMIO_S5_RESET_STATUS_GET_SLEEPRESET(r)	bitx32(r, 21, 21)
#define FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_FULL_RESET(r)	\
    bitx32(r, 20, 20)
#define	FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_RESET(r)	bitx32(r, 19, 19)
#define	FCH_PMIO_S5_RESET_STATUS_GET_DO_K8_INIT(r)	bitx32(r, 18, 18)
#define	FCH_PMIO_S5_RESET_STATUS_GET_SOFT_PCIRST(r)	bitx32(r, 17, 17)
#define	FCH_PMIO_S5_RESET_STATUS_GET_USERRST(r)		bitx32(r, 16, 16)
#define	FCH_PMIO_S5_RESET_STATUS_GET_INTTHERMALTRIP(r)	bitx32(r, 9, 9)
#define	FCH_PMIO_S5_RESET_STATUS_GET_REMOTEPOWERDOWNFROMASF(r)	bitx32(r, 4, 4)
#define	FCH_PMIO_S5_RESET_STATUS_GET_SHUTDOWN(r)	bitx32(r, 2, 2)
#define	FCH_PMIO_S5_RESET_STATUS_GET_PWRBTN4SECOND(r)	bitx32(r, 1, 1)
#define	FCH_PMIO_S5_RESET_STATUS_GET_THERMALTRIP(r)	bitx32(r, 0, 0)

/*
 * FCH::PM::AltMmioEn.  Flags for the alternate MMIO space BAR.  Meaningful only
 * on secondary FCHs.
 */
/*CSTYLED*/
#define	D_FCH_PMIO_ALTMMIOEN	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,		\
	.srd_reg = FCH_PMIO_REGOFF_ALTMMIOEN,	\
	.srd_size = 1				\
}
#define	FCH_PMIO_ALTMMIOEN_SMN()	\
    fch_pmio_smn_reg(D_FCH_PMIO_ALTMMIOEN, 0)
#define	FCH_PMIO_ALTMMIOEN(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_ALTMMIOEN, 0)
#define	FCH_PMIO_ALTMMIOEN_GET_EN(r)		bitx8(r, 0, 0)
#define	FCH_PMIO_ALTMMIOEN_SET_EN(r, v)		bitset8(r, 0, 0, v)
#define	FCH_PMIO_ALTMMIOEN_GET_WIDTH(r)		bitx8(r, 1, 1)
#define	FCH_PMIO_ALTMMIOEN_SET_WIDTH(r, v)	bitset8(r, 1, 1, v)
#define	FCH_PMIO_ALTMMIOEN_WIDTH_32	0
#define	FCH_PMIO_ALTMMIOEN_WIDTH_64	1

/*
 * FCH::PM::AltMmioBase.  Alternate MMIO space for most of the small functional
 * units in this FCH.  Meaningful only on secondary FCHs.
 */
/*CSTYLED*/
#define	D_FCH_PMIO_ALTMMIOBASE	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_FCH_PMIO,			\
	.srd_reg = FCH_PMIO_REGOFF_ALTMMIOBASE,		\
	.srd_size = 2					\
}
#define	FCH_PMIO_ALTMMIOBASE_SMN()	\
    fch_pmio_smn_reg(D_FCH_PMIO_ALTMMIOBASE, 0)
#define	FCH_PMIO_ALTMMIOBASE(b)	\
    fch_pmio_mmio_reg((b), D_FCH_PMIO_ALTMMIOBASE, 0)
#define	FCH_PMIO_ALTMMIOBASE_GET(r)	bitx16(r, 15, 0)
#define	FCH_PMIO_ALTMMIOBASE_SET(r, v)	bitset16(r, 15, 0, v)
#define	FCH_PMIO_ALTMMIOBASE_SHIFT	16
#define	FCH_PMIO_ALTMMIOBASE_SIZE	0x2000UL

#endif	/* !_ASM */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_FCH_PMIO_H */
