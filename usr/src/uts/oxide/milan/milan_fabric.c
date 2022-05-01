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
 * Various routines and things to access, initialize, understand, and manage
 * Milan's I/O fabric. This consists of both the data fabric and the
 * northbridges.
 *
 * ---------------------
 * Physical Organization
 * ---------------------
 *
 * In AMD's Zen 2 and 3 designs, the CPU socket is organized as a series of
 * chiplets with a series of compute complexes and then a central I/O die.
 * cpuid.c has an example of what this looks like. Critically, this I/O die is
 * the major device that we are concerned with here as it bridges the cores to
 * basically the outside world through a combination of different devices and
 * I/O paths.
 *
 * XXX More on physical organization, terms, and related. ASCII art.
 */

#include <sys/types.h>
#include <sys/ksynch.h>
#include <sys/pci.h>
#include <sys/pci_cfgspace.h>
#include <sys/pcie.h>
#include <sys/spl.h>
#include <sys/debug.h>
#include <sys/prom_debug.h>
#include <sys/x86_archext.h>
#include <sys/bitext.h>
#include <sys/sysmacros.h>
#include <sys/memlist_impl.h>
#include <sys/machsystm.h>
#include <sys/plat/pci_prd.h>
#include <sys/apic.h>
#include <sys/cpuvar.h>

#include <asm/bitmap.h>

#include <milan/milan_apob.h>
#include <milan/milan_ccx.h>
#include <milan/milan_dxio_data.h>
#include <milan/milan_fabric.h>
#include <milan/milan_physaddrs.h>
#include <milan/milan_straps.h>

/*
 * XXX This header contains a lot of the definitions that the broader system is
 * currently using for register definitions. For the moment we're trying to keep
 * this consolidated, hence this wacky include path.
 */
#include <io/amdzen/amdzen.h>

/*
 * XXX MOVE ME
 */
#define	AMDZEN_DF_F1_PHYSICAL_CORE_ENABLE0	0x300
#define	AMDZEN_DF_F1_PHYSICAL_CORE_ENABLE1	0x304

/*
 * This defines what the maximum number of SoCs that are supported in Milan (and
 * Rome).
 */
#define	MILAN_FABRIC_MAX_SOCS		2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC. Since
 * Rome this has been 1. Previously on Naples this was 4. Because we do not work
 * on Naples based platforms, this is kept low (unlike the more general amdzen
 * nexus driver).
 */
#define	MILAN_FABRIC_MAX_DIES_PER_SOC	1

#define	MILAN_DF_FIRST_CCM_ID	16

/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	MILAN_IOMS_PER_IODIE	4

/*
 * The maximum number of NBIFs and PCIe ports off of an IOMS. The IOMS has up to
 * three ports (though only one has three with the WAFL link). There are always
 * three primary NBIFs. Each PCIe PORT has a maximum of 8 bridges for devices.
 */
#define	MILAN_IOMS_MAX_PCIE_PORTS	3
#define	MILAN_IOMS_MAX_NBIF		3
#define	MILAN_IOMS_MAX_PCIE_BRIDGES	8
#define	MILAN_IOMS_WAFL_PCIE_NBRIDGES	2
#define	MILAN_IOMS_WAFL_PCIE_PORT	2

/*
 * The maximum number of functions is based on the hardware design here. Each
 * NBIF has potentially one or more root complexes and endpoints.
 */
#define	MILAN_NBIF0_NFUNCS	3
#define	MILAN_NBIF1_NFUNCS	7
#define	MILAN_NBIF2_NFUNCS	3
#define	MILAN_NBIF_MAX_FUNCS	7
#define	MILAN_NBIF_MAX_DEVS	3

/*
 * Per the PPR, the following defines the first enry for the Milan IOMS.
 */
#define	MILAN_DF_FIRST_IOMS_ID	24

/*
 * This indicates the ID number of the IOMS instance that happens to have the
 * FCH present.
 */
#define	MILAN_IOMS_HAS_FCH	3

/*
 * IOMS SMN bases and various shifts based on instance ID to indicate the right
 * device. Sometimes we need to select the correct SMN aperture ID and other
 * times we just need to select an offset into that aperture.
 */
#define	MILAN_SMN_IOHC_BASE	0x13b00000
#define	MILAN_SMN_IOHC_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_IOHC_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_IOHC_BASE_BITS, _r)
#define	MILAN_SMN_IOAGR_BASE	0x15b00000
#define	MILAN_SMN_IOAGR_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_IOAGR_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_IOAGR_BASE_BITS, _r)
#define	MILAN_SMN_IOMS_SHIFT(x)	((x) << 20)

/*
 * The SDPMUX SMN addresses are a bit weird. There is one per IOMS instance;
 * however, the SMN addresses are very different. While they increment based on
 * the IOMS shift above, they actually add one to the IOMS id, unlike others. In
 * addition, everything beyond the first also adds (1 << 23). It is unclear why
 * exactly this is, but just comes to how the different aperture IDs seem to
 * have shaken out.
 */
#define	MILAN_SMN_SDPMUX_BASE	0x04400000
#define	MILAN_SMN_SDPMUX_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_SDPMUX_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_SDPMUX_BASE_BITS, _r)
#define	MILAN_SMN_SDPMUX_IOMS_SHIFT(x)	((1 << 23) + ((x + 1) << 20))

/*
 * IOHC Registers of Interest. The SMN based addresses are all relative to the
 * IOHC base address.
 */

/*
 * IOHC::NB_TOP_OF_DRAM_SLOT1. This indicates where the top of DRAM below (or
 * at) 4 GiB is. Note, bit 32 for getting to 4 GiB is actually in bit 0.
 * Otherwise it's all bits 31:23.
 */
#define	MILAN_IOHC_R_PCI_NB_TOP_OF_DRAM		0x90
#define	MILAN_IOHC_R_SET_NB_TOP_OF_DRAM(r, v)		bitset32(r, 31, 23, v)
#define	MILAN_IOHC_R_SET_NB_TOP_OF_DRAM_BIT32(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::IOHC_REFCLK_MODE. Seemingly controls the speed of the reference clock
 * that is presumably used by PCIe.
 */
#define	MILAN_IOHC_R_SMN_REFCLK_MODE	0x10020
#define	MILAN_IOHC_R_REFCLK_MODE_SET_MODE_27MHZ(r, v)	bitset32(r, 2, 2, v)
#define	MILAN_IOHC_R_REFCLK_MODE_SET_MODE_25MHZ(r, v)	bitset32(r, 1, 1, v)
#define	MILAN_IOHC_R_REFCLK_MODE_SET_MODE_100MHZ(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::IOHC_PCIE_CRS_Count. Controls configuration space retries. The limit
 * indicates the length of time that retries can be issued for. Apparently in
 * 1.6ms units. The delay is the amount of time that is used between retries,
 * which are in units of 1.6us.
 */
#define	MILAN_IOHC_R_SMN_PCIE_CRS_COUNT		0x10028
#define	MILAN_IOHC_R_SET_PCIE_CRS_COUNT_LIMIT(r, v)	bitset32(r, 27, 16, v)
#define	MILAN_IOHC_R_SET_PCIE_CRS_COUNT_DELAY(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::NB_BUS_NUM_CNTL
 */
#define	MILAN_IOHC_R_SMN_BUS_NUM_CNTL		0x10044
#define	MILAN_IOHC_R_SET_BUS_NUM_CNTL_EN(r, v)		bitset32(r, 8, 8, v)
#define	MILAN_IOHC_R_SET_BUS_NUM_CNTL_BUS(r, v)		bitset32(r, 7, 0, v)

/*
 * IOHC::NB_LOWER_TOP_OF_DRAM2.  Indicates to the NB where DRAM above 4 GiB goes
 * up to. Note, that due to the wholes where there are system reserved ranges of
 * memory near 1 TiB, this may be split into two values.
 */
#define	MILAN_IOHC_R_SMN_DRAM_TOM2_LOW		0x10064
#define	MILAN_IOHC_R_SET_DRAM_TOM2_LOW_EN(r, v)		bitset32(r, 0, 0, v)
#define	MILAN_IOHC_R_SET_DRAM_TOM2_LOW_TOM2(r, v)	bitset32(r, 31, 23, v)

/*
 * IOHC::NB_UPPER_TOP_OF_DRAM2.
 */
#define	MILAN_IOHC_R_SMN_DRAM_TOM2_HI		0x10068
#define	MILAN_IOHC_R_SET_DRAM_TOM2_HI_TOM2(r, v)	bitset32(r, 8, 0, v)

/*
 * IOHC::NB_LOWER_DRAM2_BASE. This indicates the starting address of DRAM at 4
 * GiB. This register resets to all zeros indicating that it starts at 4 GiB,
 * hence why it is not set. This contains the lower 32 bits (of which 31:23) are
 * valid.
 */
#define	MILAN_IOHC_R_SMN_DRAM_BASE2_LOW		0x1006c
#define	MILAN_IOHC_R_SET_DRAM_BASE2_LOW_BASE(x)		bitset32(r, 31, 23, v)

/*
 * IOHC::NB_UPPER_DRAM2_BASE. This indicates the starting address of DRAM at 4
 * GiB. This register resets to 001h indicating that it starts at 4 GiB, hence
 * why it is not set. This contains the upper 8 bits (47:32) of the starting
 * address.
 */
#define	MILAN_IOHC_R_SMN_DRAM_BASE2_HI		0x10070
#define	MILAN_IOHC_R_SET_DRAM_BASE2_HI_BASE(r, v)	bitset32(r, 8, 0, v)

/*
 * IOHC::SB_LOCATION. Indicates where the FCH aka the old south bridge is
 * located.
 */
#define	MILAN_IOHC_R_SMN_SB_LOCATION		0x1007c
#define	MILAN_IOHC_R_SET_SMN_SB_LOCATION_CORE(r, v)	bitset32(r, 31, 16, v)
#define	MILAN_IOHC_R_SET_SMN_SB_LOCATION_PORT(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::IOHC_FEATURE_CNTL. As it says on the tin, controls some various feature
 * bits here.
 */
#define	MILAH_IOHC_R_SMN_FEATURE_CNTL		0x10118
#define	MILAN_IOCH_R_FEATURE_CNTL_GET_DGPU(r)	bitx32(r, 28, 28)
#define	MILAN_IOHC_R_FEATURE_CNTL_SET_ARI(r, v)	bitset32(r, 22, 22, v)
#define	MILAN_IOHC_R_FEATURE_CNTL_GET_ARCH(r)	bitx32(r, 3, 3)
#define	MILAN_IOHC_R_FEATURE_CNTL_SET_P2P(r, v)	bitset32(r, 2, 1, v)
#define	MILAN_IOHC_R_FEATURE_CNTL_P2P_DROP_NMATCH	0
#define	MILAN_IOHC_R_FEATURE_CNTL_P2P_FWD_NMATCH	1
#define	MILAN_IOHC_R_FEATURE_CNTL_P2P_FWD_ALL		2
#define	MILAN_IOHC_R_FEATURE_CNTL_P2P_DISABLE		3
#define	MILAN_IOHC_R_FEATURE_CNTL_GET_HP_DEVID_EN(x)	bitx32(r, 0, 0)

/*
 * IOHC::NB_TOP_OF_DRAM3. This is another use of defining memory. It starts at
 * bit 40 of PA. This register is a bit different from the others in that it is
 * an inclusive register. The register containts bits 51:22, mapped to the
 * register's 29:0.
 */
#define	MILAN_IOHC_R_SMN_DRAM_TOM3		0x10138
#define	MILAN_IOHC_R_SET_DRAM_TOM3_EN(r, v)	bitset32(r, 31, 31, v)
#define	MILAN_IOHC_R_SET_DRAM_TOM3_LIMIT(r, v)	bitset32(r, 29, 0, v)

/*
 * IOHC::PSP_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * PSP.
 */
#define	MILAN_IOHC_R_SMN_PSP_ADDR_LO		0x102e0
#define	MILAN_IOHC_R_SET_PSP_ADDR_LO_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	MILAN_IOHC_R_SET_PSP_ADDR_LO_LOCK(r, v)	bitset32(r, 7, 8, v)
#define	MILAN_IOHC_R_SET_PSP_ADDR_LO_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::PSP_BASE_ADDR_HI. This contains the upper bits of the PSP base
 * address.
 */
#define	MILAN_IOHC_R_SMN_PSP_ADDR_HI		0x102e4
#define	MILAN_IOHC_R_SET_PSP_ADDR_HI_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::SMU_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * SMU.
 */
#define	MILAN_IOHC_R_SMN_SMU_ADDR_LO		0x102e8
#define	MILAN_IOHC_R_SET_SMU_ADDR_LO_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	MILAN_IOHC_R_SET_SMU_ADDR_LO_LOCK(r, v)	bitset32(r, 7, 8, v)
#define	MILAN_IOHC_R_SET_SMU_ADDR_LO_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::SMU_BASE_ADDR_HI. This contains the upper bits of the SMU base
 * address.
 */
#define	MILAN_IOHC_R_SMN_SMU_ADDR_HI		0x102ec
#define	MILAN_IOHC_R_SET_SMU_ADDR_HI_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::IOAPIC_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * IOAPIC.
 */
#define	MILAN_IOHC_R_SMN_IOAPIC_ADDR_LO		0x102f0
#define	MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_ADDR(r, v)	bitset32(r, 31, 8, v)
#define	MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::IOAPIC_BASE_ADDR_HI. This contains the upper bits of the IOAPIC base
 * address.
 */
#define	MILAN_IOHC_R_SMN_IOAPIC_ADDR_HI		0x102f4
#define	MILAN_IOHC_R_SET_IOAPIC_ADDR_HI_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::DBG_BASE_ADDR_LO. This contains the MMIO address that is used by the
 * DBG registers. What this debugs, is unfortunately unclear.
 */
#define	MILAN_IOHC_R_SMN_DBG_ADDR_LO		0x102f8
#define	MILAN_IOHC_R_SET_DBG_ADDR_LO_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	MILAN_IOHC_R_SET_DBG_ADDR_LO_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	MILAN_IOHC_R_SET_DBG_ADDR_LO_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::DBG_BASE_ADDR_HI. This contains the upper bits of the DBG base
 * address.
 */
#define	MILAN_IOHC_R_SMN_DBG_ADDR_HI		0x102fc
#define	MILAN_IOHC_R_SET_DBG_ADDR_HI_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::FASTREG_BASE_ADDR_LO. This contains the MMIO address that is used by
 * the 'FastRegs' which provides access to an SMN aperture.
 */
#define	MILAN_IOHC_R_SMN_FASTREG_ADDR_LO		0x10300
#define	MILAN_IOHC_R_SET_FASTREG_ADDR_LO_ADDR(r, v)	bitset32(r, 31, 20, v)
#define	MILAN_IOHC_R_SET_FASTREG_ADDR_LO_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	MILAN_IOHC_R_SET_FASTREG_ADDR_LO_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::FASTREG_BASE_ADDR_HI. This contains the upper bits of the FASTREG base
 * address.
 */
#define	MILAN_IOHC_R_SMN_FASTREG_ADDR_HI		0x10304
#define	MILAN_IOHC_R_SET_FASTREG_ADDR_HI_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::FASTREGCNTL_BASE_ADDR_LO. This contains the MMIO address that is used
 * by the FASTREGCNTL.
 */
#define	MILAN_IOHC_R_SMN_FASTREGCNTL_ADDR_LO		0x10308
#define	MILAN_IOHC_R_SET_FASTREGCNTL_ADDR_LO_ADDR(r, v)	bitset32(r, 31, 12, v)
#define	MILAN_IOHC_R_SET_FASTREGCNTL_ADDR_LO_LOCK(r, v)	bitset32(r, 1, 1, v)
#define	MILAN_IOHC_R_SET_FASTREGCNTL_ADDR_LO_EN(r, v)	bitset32(r, 0, 0, v)

/*
 * IOHC::FASTREGCNTL_BASE_ADDR_HI. This contains the upper bits of the
 * FASTREGCNTL base address.
 */
#define	MILAN_IOHC_R_SMN_FASTREGCNTL_ADDR_HI		0x1030c
#define	MILAN_IOHC_R_SET_FASTREGCNTL_ADDR_HI_ADDR(r, v)	bitset32(r, 15, 0, v)

/*
 * IOHC::IOHC_SDP_PORT_CONTROL. This is used to control how the port disconnect
 * behavior operates for the connection to the data fabric.
 */
#define	MILAN_IOHC_R_SMN_SDP_PORT_CONTROL	0x10344
#define	MILAN_IOHC_R_SET_SDP_PORT_CONTROL_SDF_RT_HYSTERESIS(r, v)	\
    bitset32(r, 15, 8, v)
#define	MILAN_IOHC_R_SET_SDP_PORT_CONTROL_PORT_HYSTERESIS(r, v)	\
    bitset32(r, 7, 0, v)

/*
 * IOHC::IOHC_EARLY_WAKE_UP_EN. This is seemingly used to control how the SDP
 * port and DMA work with clock requests.
 */
#define	MILAN_IOHC_R_SMN_SDP_EARLY_WAKE_UP	0x10348
#define	MILAN_IOHC_R_SET_SDP_EARLY_WAKE_UP_HOST_ENABLE(r, v)	\
    bitset32(r, 31, 16, v)
#define	MILAN_IOHC_R_SET_SDP_EARLY_WAKE_UP_DMA_ENABLE(r, v)	\
    bitset32(r, 0, 0, v)

/*
 * IOHC::USB_QoS_CNTL. This controls the USB data fabric priority.
 */
#define	MILAN_IOHC_R_SMN_USB_QOS_CNTL		0x14044
#define	MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID1_EN(r, v)	bitset32(r, 28, 28, v)
#define	MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID1_PRI(r, v)	bitset32(r, 27, 24, v)
#define	MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID1_ID(r, v)	bitset32(r, 22, 16, v)
#define	MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID0_EN(r, v)	bitset32(r, 12, 12, v)
#define	MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID0_PRI(r, v)	bitset32(r, 11, 8, v)
#define	MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID0_ID(r, v)	bitset32(r, 6, 0, v)

/*
 * IOHC::IOHC_SION_S0_CLIENT_REQ_BURSTTARGET_LOWER and friends. There are a
 * bunch of these and a varying number of them. These registers all seem to
 * adjust arbitration targets, what should be preferred, and related. There are
 * a varying number of instances of this in each IOHC MISC. There are also
 * definitions for values to go in these. Not all of the registers in the PPR
 * are set. Not all instances of these are always set with values. I'm sorry, I
 * can only speculate as to why.
 */
#define	MILAN_IOHC_R_SMN_SION_S0_CLIREQ_BURST_LOW	0x14400
#define	MILAN_IOHC_R_SMN_SION_S0_CLIREQ_BURST_HI	0x14404
#define	MILAN_IOHC_R_SMN_SION_S0_CLIREQ_TIME_LOW	0x14408
#define	MILAN_IOHC_R_SMN_SION_S0_CLIREQ_TIME_HI		0x1440c

#define	MILAN_IOHC_R_SMN_SION_S0_RDRSP_BURST_LOW	0x14410
#define	MILAN_IOHC_R_SMN_SION_S0_RDRSP_BURST_HI		0x14414
#define	MILAN_IOHC_R_SMN_SION_S0_RDRSP_TIME_LOW		0x14418
#define	MILAN_IOHC_R_SMN_SION_S0_RDRSP_TIME_HI		0x1441c

#define	MILAN_IOHC_R_SMN_SION_S0_WRRSP_BURST_LOW	0x14420
#define	MILAN_IOHC_R_SMN_SION_S0_WRRSP_BURST_HI		0x14424
#define	MILAN_IOHC_R_SMN_SION_S0_WRRSP_TIME_LOW		0x14428
#define	MILAN_IOHC_R_SMN_SION_S0_WRRSP_TIME_HI		0x1442c

#define	MILAN_IOHC_R_SMN_SION_S1_CLIREQ_BURST_LOW	0x14430
#define	MILAN_IOHC_R_SMN_SION_S1_CLIREQ_BURST_HI	0x14434
#define	MILAN_IOHC_R_SMN_SION_S1_CLIREQ_TIME_LOW	0x14438
#define	MILAN_IOHC_R_SMN_SION_S1_CLIREQ_TIME_HI		0x1443c

#define	MILAN_IOHC_R_SMN_SION_S1_RDRSP_BURST_LOW	0x14440
#define	MILAN_IOHC_R_SMN_SION_S1_RDRSP_BURST_HI		0x14444
#define	MILAN_IOHC_R_SMN_SION_S1_RDRSP_TIME_LOW		0x14448
#define	MILAN_IOHC_R_SMN_SION_S1_RDRSP_TIME_HI		0x1444c

#define	MILAN_IOHC_R_SMN_SION_S1_WRRSP_BURST_LOW	0x14450
#define	MILAN_IOHC_R_SMN_SION_S1_WRRSP_BURST_HI		0x14454
#define	MILAN_IOHC_R_SMN_SION_S1_WRRSP_TIME_LOW		0x14458
#define	MILAN_IOHC_R_SMN_SION_S1_WRRSP_TIME_HI		0x1445c

#define	MILAN_IOHC_R_SION_MAX_ENTS	7
#define	MILAN_IOHC_R_SION_SHIFT(x)	((x) * 404)

#define	MILAN_IOHC_R_SION_CLIREQ_BURST_VAL	0x08080808
#define	MILAN_IOHC_R_SION_CLIREQ_TIME_0_2_VAL	0x21212121
#define	MILAN_IOHC_R_SION_CLIREQ_TIME_3_4_VAL	0x84218421
#define	MILAN_IOHC_R_SION_CLIREQ_TIME_5_VAL	0x85218521
#define	MILAN_IOHC_R_SION_RDRSP_BURST_VAL	0x02020202

/*
 * IOHC::IOHC_SION_S1_CLIENT_NP_ReqDeficitThreshold only has a single instance
 * and IOHC::IOHC_SION_S0_CLIENT_NP_ReqDeficitThreshold actually starts at
 * instance 1, there is no instance 0.
 */
#define	MILAN_IOHC_R_SMN_SION_S1_CLI_NP_DEFECIT	0x14480
#define	MILAN_IOHC_R_SMN_SION_S0_CLI_NP_DEFICIT	0x14484
#define	MILAN_IOHC_R_SET_SION_CLI_NP_DEFICIT(r, v)	bitset32(r, 7, 0, v)
#define	MILAN_IOHC_R_SION_CLI_NP_DEFICIT_VAL	0x40
#define	MILAN_IOCH_R_SION_NP_DEFECIT_SHIFT(x)	((x - 1) * 404)

/*
 * IOHC::IOHC_SION_LiveLock_WatchDog_Threshold. This is used to set an
 * arbitration threshold for the overall bus.
 */
#define	MILAN_IOHC_R_SMN_SION_LLWD_THRESH	0x15c9c
#define	MILAN_IOHC_R_SET_SION_LLWD_THRESH_THRESH(r, v)	bitset32(r, 7, 0, v)
#define	MILAN_IOHC_R_SION_LLWD_THRESH_VAL	0x11

/*
 * IOHC Device specific addresses. There are a region of IOHC addresses that are
 * devoted to each PCIe bridge, NBIF, and the southbridge.
 */
#define	MILAN_IOHC_R_SMN_PCIE_BASE	0x31000
#define	MILAN_SMN_IOHC_PCIE_BASE_BITS	(MILAN_SMN_ADDR_BLOCK_BITS + 10)
#define	MILAN_SMN_IOHC_PCIE_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_IOHC_PCIE_BASE_BITS, _r)

/*
 * IOHC::IOHC_Bridge_CNTL. This register controls several internal properties of
 * the various bridges.  The address of this register is confusing because it
 * shows up in different locations with a large number of instances at different
 * bases. There is an instance for each PCIe root port in the system and then
 * one for each NBIF integrated root complex (note NBIF2 does not have one of
 * these). There is also one for the southbridge/fch.
 */
#define	MILAN_IOHC_R_SMN_BRIDGE_CNTL_PCIE	0x4
#define	MILAN_IOHC_R_SMN_BRIDGE_CNTL_BRIDGE_SHIFT(x)	((x) << 10)
#define	MILAN_IOHC_R_SMN_BRIDGE_CNTL_NBIF	0x38004
#define	MILAN_IOHC_R_SMN_BRIDGE_CNTL_NBIF_SHIFT(x)	((x) << 12)
#define	MILAN_IOHC_R_SMN_BRIDGE_CNTL_SB		0x3c004
#define	MILAN_IOHC_R_BRIDGE_CNTL_GET_APIC_RANGE(r)	bitx32(r, 31, 24)
#define	MILAN_IOHC_R_BRIDGE_CNTL_GET_APIC_ENABLE(r)	bitx32(r, 23, 23)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_CRS_ENABLE(r, v)	bitset32(r, 18, 18, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_IDO_MODE(r, v)	bitset32(r, 11, 10, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_IDO_MODE_NO_MOD		0
#define	MILAN_IOHC_R_BRIDGE_CNTL_IDO_MODE_DIS			1
#define	MILAN_IOHC_R_BRIDGE_CNTL_IDO_MODE_FORCE_ON		2
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_FORCE_RSP_PASS(r, v)	\
    bitset32(r, 9, 9, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_NO_SNOOP(r, v)	\
    bitset32(r, 8, 8, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_RELAX_POW(r, v)	\
    bitset32(r, 7, 7, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_MASK_UR(r, v)		\
    bitset32(r, 6, 6, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_CFG(r, v)		\
    bitset32(r, 2, 2, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_BUS_MASTER(r, v)	\
    bitset32(r, 1, 1, v)
#define	MILAN_IOHC_R_BRIDGE_CNTL_SET_BRIDGE_DISABLE(r, v)	\
    bitset32(r, 0, 0, v)

/*
 * IOAGR Registers. The SMN based addresses are all relative to the IOAGR base
 * address.
 */

/*
 * IOAGR::IOAGR_EARLY_WAKE_UP_EN. This register controls the ability to interact
 * with the clocks and DMA. Specifics unclear. Companion to the IOHC variant.
 */
#define	MILAN_IOAGR_R_SMN_EARLY_WAKE_UP		0x00090
#define	MILAN_IOAGR_R_SET_EARLY_WAKE_UP_HOST_ENABLE(r, v)	\
    bitset32(r, 31, 16, v)
#define	MILAN_IOAGR_R_SET_EARLY_WAKE_UP_DMA_ENABLE(r, v)	\
    bitset32(r, 0, 0, v)

/*
 * IOAGR::IOAGR_SION_S0_Client_Req_BurstTarget_Lower. While the case has
 * changed and the number of entries from our friends in the IOHC, everything
 * said above is still true.
 */
#define	MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_BURST_LOW	0x00400
#define	MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_BURST_HI	0x00404
#define	MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_TIME_LOW	0x00408
#define	MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_TIME_HI	0x0040c

#define	MILAN_IOAGR_R_SMN_SION_S0_RDRSP_BURST_LOW	0x00410
#define	MILAN_IOAGR_R_SMN_SION_S0_RDRSP_BURST_HI	0x00414
#define	MILAN_IOAGR_R_SMN_SION_S0_RDRSP_TIME_LOW	0x00418
#define	MILAN_IOAGR_R_SMN_SION_S0_RDRSP_TIME_HI		0x0041c

#define	MILAN_IOAGR_R_SMN_SION_S0_WRRSP_BURST_LOW	0x00420
#define	MILAN_IOAGR_R_SMN_SION_S0_WRRSP_BURST_HI	0x00424
#define	MILAN_IOAGR_R_SMN_SION_S0_WRRSP_TIME_LOW	0x00428
#define	MILAN_IOAGR_R_SMN_SION_S0_WRRSP_TIME_HI		0x0042c

#define	MILAN_IOAGR_R_SMN_SION_S1_CLIREQ_BURST_LOW	0x00430
#define	MILAN_IOAGR_R_SMN_SION_S1_CLIREQ_BURST_HI	0x00434
#define	MILAN_IOAGR_R_SMN_SION_S1_CLIREQ_TIME_LOW	0x00438
#define	MILAN_IOAGR_R_SMN_SION_S1_CLIREQ_TIME_HI	0x0043c

#define	MILAN_IOAGR_R_SMN_SION_S1_RDRSP_BURST_LOW	0x00440
#define	MILAN_IOAGR_R_SMN_SION_S1_RDRSP_BURST_HI	0x00444
#define	MILAN_IOAGR_R_SMN_SION_S1_RDRSP_TIME_LOW	0x00448
#define	MILAN_IOAGR_R_SMN_SION_S1_RDRSP_TIME_HI		0x0044c

#define	MILAN_IOAGR_R_SMN_SION_S1_WRRSP_BURST_LOW	0x00450
#define	MILAN_IOAGR_R_SMN_SION_S1_WRRSP_BURST_HI	0x00454
#define	MILAN_IOAGR_R_SMN_SION_S1_WRRSP_TIME_LOW	0x00458
#define	MILAN_IOAGR_R_SMN_SION_S1_WRRSP_TIME_HI		0x0045c

#define	MILAN_IOAGR_R_SION_MAX_ENTS	5
#define	MILAN_IOAGR_R_SION_SHIFT(x)	((x) * 400)

#define	MILAN_IOAGR_R_SION_CLIREQ_BURST_VAL	0x08080808
#define	MILAN_IOAGR_R_SION_CLIREQ_TIME_0_2_VAL	0x21212121
#define	MILAN_IOAGR_R_SION_CLIREQ_TIME_3_VAL	0x84218421
#define	MILAN_IOAGR_R_SION_RDRSP_BURST_VAL	0x02020202

/*
 * IOAGR::IOAGR_SION_LiveLock_WatchDog_Threshold. This is used to set an
 * arbitration threshold for the IOAGR. Companion to the IOHC variant.
 */
#define	MILAN_IOAGR_R_SMN_SION_LLWD_THRESH		0x01498
#define	MILAN_IOAGR_R_SET_SION_LLWD_THRESH_THRESH(r, v)	bitset32(r, 7, 0, v)
#define	MILAN_IOAGR_R_SION_LLWD_THRESH_VAL	0x11

/*
 * SDPMUX registers of interest.
 */

/*
 * SDPMUX::SDPMUX_SDP_PORT_CONTROL. More Clock request bits in the spirit of
 * other blocks.
 */
#define	MILAN_SDPMUX_R_SMN_SDP_PORT_CONTROL		0x00008
#define	MILAN_SDPMUX_R_SET_SDP_PORT_CONTROL_HOST_ENABLE(r, v)		\
    bitset32(r, 31, 16, v)
#define	MILAN_SDPMUX_R_SET_SDP_PORT_CONTROL_DMA_ENABLE(r, v)		\
    bitset32(r, 15, 15, v)
#define	MILAN_SDPMUX_R_SET_SDP_PORT_CONTROL_PORT_HYSTERESIS(r, v)	\
    bitset32(r, 7, 0, v)

/*
 * SDPMUX::SDPMUX_SION_S0_Client_Req_BurstTarget_Lower. While the case has
 * changed and the number of entries from our friends in the IOHC, everything
 * said above is still true.
 */
#define	MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_BURST_LOW	0x00400
#define	MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_BURST_HI	0x00404
#define	MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_TIME_LOW	0x00408
#define	MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_TIME_HI	0x0040c

#define	MILAN_SDPMUX_R_SMN_SION_S0_RDRSP_BURST_LOW	0x00410
#define	MILAN_SDPMUX_R_SMN_SION_S0_RDRSP_BURST_HI	0x00414
#define	MILAN_SDPMUX_R_SMN_SION_S0_RDRSP_TIME_LOW	0x00418
#define	MILAN_SDPMUX_R_SMN_SION_S0_RDRSP_TIME_HI	0x0041c

#define	MILAN_SDPMUX_R_SMN_SION_S0_WRRSP_BURST_LOW	0x00420
#define	MILAN_SDPMUX_R_SMN_SION_S0_WRRSP_BURST_HI	0x00424
#define	MILAN_SDPMUX_R_SMN_SION_S0_WRRSP_TIME_LOW	0x00428
#define	MILAN_SDPMUX_R_SMN_SION_S0_WRRSP_TIME_HI	0x0042c

#define	MILAN_SDPMUX_R_SMN_SION_S1_CLIREQ_BURST_LOW	0x00430
#define	MILAN_SDPMUX_R_SMN_SION_S1_CLIREQ_BURST_HI	0x00434
#define	MILAN_SDPMUX_R_SMN_SION_S1_CLIREQ_TIME_LOW	0x00438
#define	MILAN_SDPMUX_R_SMN_SION_S1_CLIREQ_TIME_HI	0x0043c

#define	MILAN_SDPMUX_R_SMN_SION_S1_RDRSP_BURST_LOW	0x00440
#define	MILAN_SDPMUX_R_SMN_SION_S1_RDRSP_BURST_HI	0x00444
#define	MILAN_SDPMUX_R_SMN_SION_S1_RDRSP_TIME_LOW	0x00448
#define	MILAN_SDPMUX_R_SMN_SION_S1_RDRSP_TIME_HI	0x0044c

#define	MILAN_SDPMUX_R_SMN_SION_S1_WRRSP_BURST_LOW	0x00450
#define	MILAN_SDPMUX_R_SMN_SION_S1_WRRSP_BURST_HI	0x00454
#define	MILAN_SDPMUX_R_SMN_SION_S1_WRRSP_TIME_LOW	0x00458
#define	MILAN_SDPMUX_R_SMN_SION_S1_WRRSP_TIME_HI	0x0045c

#define	MILAN_SDPMUX_R_SION_MAX_ENTS	5
#define	MILAN_SDPMUX_R_SION_SHIFT(x)	((x) * 400)

#define	MILAN_SDPMUX_R_SION_CLIREQ_BURST_VAL	0x08080808
#define	MILAN_SDPMUX_R_SION_CLIREQ_TIME_VAL	0x21212121
#define	MILAN_SDPMUX_R_SION_RDRSP_BURST_VAL	0x02020202

/*
 * IOAPIC registers. These exist on a per-IOMS basis. These are not the
 * traditional software IOAPIC registers that exist in the Northbridge.
 */
#define	MILAN_SMN_IOAPIC_BASE	0x14300000
#define	MILAN_SMN_IOAPIC_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_IOAPIC_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_IOAPIC_BASE_BITS, _r)

/*
 * IOAPIC::FEATURES_ENABLE. This controls various features of the IOAPIC.
 */
#define	MILAN_IOAPIC_R_SMN_FEATURES		0x00
#define	MILAN_IOAPIC_R_SET_FEATURES_LEVEL_ONLY(r, v)	bitset32(r, 9, 9, v)
#define	MILAN_IOAPIC_R_SET_FEATURES_PROC_MODE(r, v)	bitset32(r, 8, 8, v)
#define	MILAN_IOAPIC_R_SET_FEATURES_SECONDARY(r, v)	bitset32(r, 5, 5, v)
#define	MILAN_IOAPIC_R_SET_FEATURES_FCH(r, v)		bitset32(r, 4, 4, v)
#define	MILAN_IOAPIC_R_SET_FEATURES_ID_EXT(r, v)	bitset32(r, 2, 2, v)
#define	MILAN_IOAPIC_R_FEATURES_ID_EXT_4BIT	0
#define	MILAN_IOAPIC_R_FEATURES_ID_EXT_8BIT	1

/*
 * IOAPIC::IOAPIC_BR_INTERRUPT_ROUTING. There are several instances of this
 * register and they determine how a given logical bridge on the IOMS maps to
 * the IOAPIC pins. Hence why there are 22 routes.
 */
#define	MILAN_IOAPIC_R_NROUTES			22
#define	MILAN_IOAPIC_R_SMN_ROUTE		0x40
#define	MILAN_IOAPIC_R_SET_ROUTE_BRIDGE_MAP(r, v)	bitset32(r, 20, 16, v)
#define	MILAN_IOAPIC_R_SET_ROUTE_INTX_SWIZZLE(r, v)	bitset32(r, 5, 4, v)
#define	MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD		0
#define	MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_BCDA		1
#define	MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB		2
#define	MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC		3
#define	MILAN_IOAPIC_R_SET_ROUTE_INTX_GROUP(r, v)	bitset32(r, 2, 0, v)

/*
 * SDPMUX::SDPMUX_SION_LiveLock_WatchDog_Threshold. This is used to set an
 * arbitration threshold for the SDPMUX. Companion to the IOHC variant.
 */
#define	MILAN_SDPMUX_R_SMN_SION_LLWD_THRESH		0x01498
#define	MILAN_SDPMUX_R_SET_SION_LLWD_THRESH_THRESH(r, v)	\
    bitset32(r, 7, 0, v)
#define	MILAN_SDPMUX_R_SION_LLWD_THRESH_VAL	0x11

/*
 * IOMMU Registers. The IOMMU is broken into an L1 and L2. The L1 exists for
 * multiple different bases, that is for the IOAGR, NBIF0, and the two PCI
 * ports (even on IOMS 0). XXX We only really include the IOAGR variant here for
 * right now. The L2 register set only exists on a per-IOMS basis.
 */
#define	MILAN_SMN_IOMMUL1_BASE	0x14700000
#define	MILAN_SMN_IOMMUL1_DEV_SHIFT(x)	((x) << 22)
#define	MILAN_SMN_IOMMUL1_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_IOMMUL1_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_IOMMUL1_BASE_BITS, _r)
#define	MILAN_SMN_IOMMUL2_BASE	0x13f00000
#define	MILAN_SMN_IOMMUL2_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_IOMMUL2_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_IOMMUL2_BASE_BITS, _r)

/*
 * IOMMU types, note that the PCI port ID is designed to correspond to the first
 * two entries.
 */
typedef enum milan_iommul1_type {
	IOMMU_L1_PCIE0,
	IOMMU_L1_PCIE1,
	IOMMU_L1_NBIF,
	IOMMU_L1_IOAGR
} milan_iommul1_type_t;

/*
 * IOMMU1::L1_MISC_CNTRL_1.  This register contains a smorgasbord of settings.
 * Some of which are used in the hotplug path.
 */
#define	MILAN_IOMMUL1_R_SMN_L1_CTL1	0x1c
#define	MILAN_IOMMUL1_R_SET_L1_CTL1_ORDERING(r, v)	bitset32(r, 0, 0, v)

/*
 * IOMMUL1::L1_SB_LOCATION.  Programs where the FCH is into a given L1 IOMMU.
 */
#define	MILAN_IOMMUL1_R_SMN_SB_LOCATION		0x24

/*
 * IOMMUL2::L2_SB_LOCATION. Yet another place we program the FCH information.
 */
#define	MILAN_IOMMUL2_R_SMN_SB_LOCATION		0x112c

/*
 * PCIe related SMN addresses. This is determined based on a combination of
 * which IOMS we're on, which PCIe port we're on on the IOMS, and then finally
 * which PCIe port it is itself. There are two SMN bases. One for internal
 * configuration and one where the common configuration space exists.
 *
 * Registers in this space are sometimes specific to an overall port (e.g. the
 * thing that encompasses a given group of root bridges and an x16 port) or to a
 * bridge within the port.
 *
 * The use of bits [19:18] to represent the sub-block and [15:12] to represent
 * the bridge offset means that the effective base SMN address for per-port core
 * registers occupies 14 bits, and for the per-bridge port and config registers
 * occupies 20 bits.
 */
#define	MILAN_SMN_PCIE_CFG_BASE		0x11100000
#define	MILAN_SMN_PCIE_PORT_BASE	0x11140000
#define	MILAN_SMN_PCIE_CORE_BASE	0x11180000
#define	MILAN_SMN_PCIE_BRIDGE_SHIFT(x)	((x) << 12)
#define	MILAN_SMN_PCIE_PORT_SHIFT(x)	((x) << 22)
#define	MILAN_SMN_PCIE_IOMS_SHIFT(x)	((x) << 20)
#define	MILAN_SMN_PCIE_CORE_BASE_BITS	(MILAN_SMN_ADDR_BLOCK_BITS + 2)
#define	MILAN_SMN_PCIE_PORT_BASE_BITS	(MILAN_SMN_ADDR_BLOCK_BITS + 8)
#define	MILAN_SMN_PCIE_CORE_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_PCIE_CORE_BASE_BITS, _r)
#define	MILAN_SMN_PCIE_PORT_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_PCIE_PORT_BASE_BITS, _r)

/*
 * General PCIe port controls. This is a register that exists in 'Port Space'
 * and is specific to a bridge.
 */
#define	MILAN_PCIE_PORT_R_SMN_PORT_CNTL	0x40
#define	MILAN_PCIE_PORT_R_SET_PORT_CNTL_PWRFLT_EN(r, v)		\
    bitset32(r, 4, 4, v)

/*
 * PCIe TX Control. This is a register that exists in 'Port Space' and is
 * specific to a bridge. XXX figure out what other bits we need.
 */
#define	MILAN_PCIE_PORT_R_SMN_TX_CNTL	0x80
#define	MILAN_PCIE_PORT_R_SET_TX_CNTL_TLP_FLUSH_DOWN_DIS(r, v)	\
    bitset32(r, 15, 15, v)

/*
 * Port Link Training Control. This register seems to control some amount of the
 * general aspects of link training.
 */
#define	MILAN_PCIE_PORT_R_SMN_TRAIN_CNTL	0x284
#define	MILAN_PCIE_PORT_R_SET_TRAIN_CNTL_TRAIN_DIS(r, v)	\
    bitset32(r, 13, 13, v)

/*
 * Port Link Control Register 5. There are several others, but this one seems to
 * be require for hotplug.
 */
#define	MILAN_PCIE_PORT_R_SMN_LC_CNTL5	0x2dc
#define	MILAN_PCIE_PORT_R_SET_LC_CNTL5_WAIT_DETECT(r, v)	\
    bitset32(r, 28, 28, v)

/*
 * Port Hotplug Descriptor control. This is a register that exists in 'Port
 * Space' and is specific to a bridge. This seems to relate something in the
 * port to the SMU's hotplug engine.
 */
#define	MILAN_PCIE_PORT_R_SMN_HP_CNTL	0x36c
#define	MILAN_PCIE_PORT_R_SET_HP_CNTL_SLOT(r, v)	bitset32(r, 5, 0, v)
#define	MILAN_PCIE_PORT_R_SET_HP_CNTL_ACTIVE(r, v)	bitset32(r, 31, 31, v)

/*
 * PCIe Port level TX controls. Note, this register is in 'core' space and is
 * specific to the overall milan_pcie_port_t, as opposed to the bridge. XXX Add
 * in other bits.
 */
#define	MILAN_PCIE_CORE_R_SMN_CI_CNTL	0x80
#define	MILAN_PCIE_CORE_R_SET_CI_CNTL_LINK_DOWN_CTO_EN(r, v)		\
    bitset32(r, 29, 29, v)
#define	MILAN_PCIE_CORE_R_SET_CI_CNTL_IGN_LINK_DOWN_CTO_ERR(r, v)	\
    bitset32(r, 31, 31, v)

/*
 * PCIe port SDP Control. This register seems to be used to tell the system how
 * to map a given port to the data fabric and related.
 */
#define	MILAN_PICE_CORE_R_SMN_SDP_CTRL	0x18c
#define	MILAN_PCIE_CORE_R_SET_SDP_CTRL_PORT_ID(r, v)	bitset32(r, 28, 26, v)
#define	MILAN_PCIE_CORE_R_SET_SDP_CTRL_UNIT_ID(r, v)	bitset32(r, 3, 0, v)

/*
 * PCIe Software Reset Control #6. This is in 'Core Space' and controls whether
 * or not all of a given set of ports are stopped from training.
 */
#define	MILAN_PCIE_CORE_R_SMN_SWRST_CNTL6	0x428
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_A(r, v)	bitset32(r, 0, 0, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_B(r, v)	bitset32(r, 1, 1, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_C(r, v)	bitset32(r, 2, 2, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_D(r, v)	bitset32(r, 3, 3, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_E(r, v)	bitset32(r, 4, 4, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_F(r, v)	bitset32(r, 5, 5, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_G(r, v)	bitset32(r, 6, 6, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_H(r, v)	bitset32(r, 7, 7, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_I(r, v)	bitset32(r, 8, 8, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_J(r, v)	bitset32(r, 9, 9, v)
#define	MILAN_PCIE_CORE_R_SET_SWRST_CNTL6_HOLD_K(r, v)	bitset32(r, 10, 10, v)

/*
 * PCIe Presence Detect Control. This is 'Core Space', so it exists once
 * per port. This is used to determine whether we should consider something
 * present based on the link up OR the side-band signals, or instead require
 * both (e.g. AND).
 */
#define	MILAN_PCIE_CORE_R_SMN_PRES	0x4e0
#define	MILAN_PCIE_CORE_R_SET_PRES_MODE(r, v)	bitset32(r, 24, 24, v)
#define	MILAN_PCIE_CORE_R_PRES_MODE_OR	0
#define	MILAN_PCIE_CORE_R_PRES_MODE_AND	1

/*
 * nBIF SMN Addresses. These have multiple different shifts that we need to
 * account for. There are different bases based on which IOMS, which NBIF, and
 * which downstream device and function as well. There is a second SMN aperture
 * ID that seems to be used tha deals with the nBIF's clock gating, DMA
 * enhancements with the syshub, and related.
 */
#define	MILAN_SMN_NBIF_BASE		0x10100000
#define	MILAN_SMN_NBIF_FUNC_OFF		0x34000
#define	MILAN_SMN_NBIF_ALT_BASE		0x01400000
#define	MILAN_SMN_NBIF_FUNC_SHIFT(x)	((x) << 9)
#define	MILAN_SMN_NBIF_DEV_SHIFT(x)	((x) << 12)
#define	MILAN_SMN_NBIF_NBIF_SHIFT(x)	((x) << 22)
#define	MILAN_SMN_NBIF_IOMS_SHIFT(x)	((x) << 20)
#define	MILAN_SMN_NBIF_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_NBIF_ALT_BASE_BITS	MILAN_SMN_ADDR_BLOCK_BITS
#define	MILAN_SMN_NBIF_FUNC_BASE_BITS	(MILAN_SMN_ADDR_BLOCK_BITS + 11)
#define	MILAN_SMN_NBIF_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_NBIF_BASE_BITS, _r)
#define	MILAN_SMN_NBIF_ALT_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_NBIF_ALT_BASE_BITS, _r)
#define	MILAN_SMN_NBIF_FUNC_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_NBIF_FUNC_BASE_BITS, _r)

/*
 * The NBIF device straps for the port use a different shift style than those
 * above which are for the function space.
 */
#define	MILAN_SMN_NBIF_DEV_PORT_SHIFT(x)	((x) << 9)

/*
 * nBIF related registers.
 */

/*
 * NBIF Function strap 0. This SMN address is relative to the actual function
 * space.
 */
#define	MILAN_NBIF_R_SMN_FUNC_STRAP0	0x00
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_SUP_D2(r, v)	bitset32(r, 31, 31, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_SUP_D1(r, v)	bitset32(r, 30, 30, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_BE_PCIE(r, v)	bitset32(r, 29, 29, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_EXIST(r, v)	bitset32(r, 28, 28, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_GFX_REV(r, v)	bitset32(r, 27, 24, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_MIN_REV(r, v)	bitset32(r, 23, 20, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_MAJ_REV(r, v)	bitset32(r, 19, 16, v)
#define	MILAN_NBIF_R_SET_FUNC_STRAP0_DEV_ID(r, v)	bitset32(r, 0, 15, v)

/*
 * This register is arranged with one byte per device. Each bit corresponds to
 * an endpoint.
 */
#define	MILAN_NBIF_R_SMN_INTR_LINE	0x3a008
#define	MILAN_NBIF_R_INTR_LINE_SET_INTR(reg, dev, func, val)	\
    bitset32(reg, ((dev) * 8) + (func), ((dev) * 8) + (func), val)

/*
 * Straps for the NBIF port. These are relative to the main NBIF base register.
 */
#define	MILAN_NBIF_R_SMN_PORT_STRAP3	0x3100c
#define	MILAN_NBIF_R_SET_PORT_STRAP3_COMP_TO(r, v)	bitset32(r, 7, 7, v)


/*
 * This register seems to control various bits of control for a given NBIF. XXX
 * other bits.
 */
#define	MILAN_NBIF_R_SMN_BIFC_MISC_CTRL0	0x3a010
#define	MILAN_NBIF_R_SET_BIFC_MISC_CTRL0_PME_TURNOFF(r, v)	\
    bitset32(r, 28, 28, v)
#define	MILAN_NBIF_R_BIFC_MISC_CTRL0_PME_TURNOFF_BYPASS		0
#define	MILAN_NBIF_R_BIFC_MISC_CTRL0_PME_TURNOFF_FW		1

/*
 * The following two registers are not found in the PPR. These are used for some
 * amount of arbitration in the same vein as the SION values. The base register
 * seemingly just has a bit which defaults to saying use these values.
 */
#define	MILAN_NBIF_R_SMN_GMI_WRR_WEIGHT2	0x3a124
#define	MILAN_NBIF_R_SMN_GMI_WRR_WEIGHT3	0x3a128
#define	MILAN_NBIF_R_GMI_WRR_WEIGHT_VAL		0x04040404

/*
 * This undocumented register is a weird SYSHUB and NBIF crossover that is in
 * the alternate space.
 */
#define	MILAN_NBIF_R_SMN_SYSHUB_BGEN_BYPASS	0x10008
#define	MILAN_NBIF_R_SET_SYSHUB_BGEN_BYPASS_DMA_SW0(r, v)	\
    bitset32(r, 16, 16, v)
#define	MILAN_NBIF_R_SET_SYSHUB_BGEN_BYPASS_DMA_SW1(r, v)	\
    bitset32(r, 17, 17, v)

#define	MILAN_SMN_SCFCTP_BASE	0x20000000
#define	MILAN_SMN_SCFCTP_BASE_BITS	(MILAN_SMN_ADDR_BLOCK_BITS + 3)
#define	MILAN_SMN_SCFCTP_MAKE_ADDR(_b, _r)	\
	MILAN_SMN_MAKE_ADDR(_b, MILAN_SMN_SCFCTP_BASE_BITS, _r)
#define	MILAN_SMN_SCFCTP_CCD_SHIFT(_d)	((_d) << 23)
#define	MILAN_SMN_SCFCTP_CORE_SHIFT(_c)	((_c) << 17)

#define	MILAN_SCFCTP_R_SMN_PMREG_INITPKG0	0x2FD0
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_LOGICALDIEID(_r)	\
	bitx32(_r, 22, 19)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_LOGICALCOMPLEXID(_r)	\
	bitx32(_r, 18, 18)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_LOGICALCOREID(_r)	\
	bitx32(_r, 17, 14)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_SOCKETID(_r)	bitx32(_r, 13, 12)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_PHYSICALDIEID(_r)	\
	bitx32(_r, 11, 8)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_PHYSICALCOMPLEXID(_r)	\
	bitx32(_r, 7, 7)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_PHYSICALCOREID(_r)	\
	bitx32(_r, 6, 3)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG0_SMTEN(_r)	bitx32(_r, 2, 0)

#define	MILAN_SCFCTP_R_SMN_PMREG_INITPKG7	0x2FEC
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_NUMOFSOCKETS(_r)	\
	bitx32(_r, 26, 25)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_NUMOFLOGICALDIE(_r)	\
	bitx32(_r, 24, 21)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_NUMOFLOGICALCOMPLEXES(_r)	\
	bitx32(_r, 20, 20)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_NUMOFLOGICALCORES(_r)	\
	bitx32(_r, 19, 16)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_CHIDXHASHEN(_r)	\
	bitx32(_r, 10, 10)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_S3(_r)	bitx32(_r, 9, 9)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_S0I3(_r)	bitx32(_r, 8, 8)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_CORETYPEISARM(_r)	\
	bitx32(_r, 7, 7)
#define	MILAN_SCFCTP_R_GET_PMREG_INITPKG7_SOCID(_r)	bitx32(_r, 6, 3)

/*
 * The following definitions are all in normal PCI configuration space. These
 * represent the fixed offsets into capabilities that normally would be
 * something that one has to walk and find in the device. We opt to use the
 * fixed offsets here because we only care about one specific device, the
 * bridges here. Note, the actual bit definitions are not included here as they
 * are already present in sys/pcie.h.
 */

/*
 * PCIERCCFG::SLOT_CAP. This is the PCIe capability's slot capability register.
 * This is the illumos PCIE_SLOTCAP.
 */
#define	MILAN_BRIDGE_R_PCI_SLOT_CAP	0x6c

/*
 * SMN addresses to reach the SMU for RPCs.
 */
#define	MILAN_SMU_SMN_RPC_REQ	0x3b10530
#define	MILAN_SMU_SMN_RPC_RESP	0x3b1057c
#define	MILAN_SMU_SMN_RPC_ARG0	0x3b109c4
#define	MILAN_SMU_SMN_RPC_ARG1	0x3b109c8
#define	MILAN_SMU_SMN_RPC_ARG2	0x3b109cc
#define	MILAN_SMU_SMN_RPC_ARG3	0x3b109d0
#define	MILAN_SMU_SMN_RPC_ARG4	0x3b109d4
#define	MILAN_SMU_SMN_RPC_ARG5	0x3b109d8

/*
 * SMU RPC Response codes
 */
#define	MILAN_SMU_RPC_NOTDONE	0x00
#define	MILAN_SMU_RPC_OK	0x01
#define	MILAN_SMU_RPC_EBUSY	0xfc
#define	MILAN_SMU_RPC_EPREREQ	0xfd
#define	MILAN_SMU_RPC_EUNKNOWN	0xfe
#define	MILAN_SMU_RPC_ERROR	0xff

/*
 * SMU RPC Operation Codes. Note, these are tied to firmware and therefore may
 * not be portable between Rome, Milan, or other processors.
 */
#define	MILAN_SMU_OP_TEST		0x01
#define	MILAN_SMU_OP_GET_VERSION	0x02
#define	MILAN_SMU_OP_GET_VERSION_MAJOR(x)	bitx32(x, 23, 16)
#define	MILAN_SMU_OP_GET_VERSION_MINOR(x)	bitx32(x, 15, 8)
#define	MILAN_SMU_OP_GET_VERSION_PATCH(x)	bitx32(x, 7, 0)
#define	MILAN_SMU_OP_ENABLE_FEATURE	0x03
#define	MILAN_SMU_OP_DISABLE_FEATURE	0x04
#define	MILAN_SMU_OP_HAVE_AN_ADDRESS	0x05
#define	MILAN_SMU_OP_TOOLS_ADDRESS	0x06
#define	MILAN_SMU_OP_DEBUG_ADDRESS	0x07
#define	MILAN_SMU_OP_DXIO		0x08
#define	MILAN_SMU_OP_DC_BOOT_CALIB	0x0c
#define	MILAN_SMU_OP_GET_BRAND_STRING	0x0d
#define	MILAN_SMU_OP_TX_PP_TABLE	0x10
#define	MILAN_SMU_OP_TX_PCIE_HP_TABLE	0x12
#define	MILAN_SMU_OP_START_HOTPLUG	0x18
#define	MILAN_SMU_OP_START_HOTPLUG_POLL		0x10
#define	MILAN_SMU_OP_START_HOTPLUG_FWFIRST	0x20
#define	MILAN_SMU_OP_START_HOTPLUG_RESET	0x40
#define	MILAN_SMU_OP_I2C_SWITCH_ADDR	0x1a
#define	MILAN_SMU_OP_SET_HOPTLUG_FLAGS	0x1d
#define	MILAN_SMU_OP_SET_POWER_GATE	0x2a
#define	MILAN_SMU_OP_MAX_ALL_CORES_FREQ	0x2b
#define	MILAN_SMU_OP_SET_NBIO_LCLK	0x34
#define	MILAN_SMU_OP_SET_L3_CREDIT_MODE	0x35
#define	MILAN_SMU_OP_FLL_BOOT_CALIB	0x37
#define	MILAN_SMU_OP_DC_SOC_BOOT_CALIB	0x38
#define	MILAN_SMU_OP_HSMP_PAY_ATTN	0x41
#define	MILAN_SMU_OP_SET_APML_FLOOD	0x42
#define	MILAN_SMU_OP_FDD_BOOT_CALIB	0x43
#define	MILAN_SMU_OP_VDDCR_CPU_LIMIT	0x44
#define	MILAN_SMU_OP_SET_EDC_TRACK	0x45
#define	MILAN_SMU_OP_SET_DF_IRRITATOR	0x46

/*
 * DXIO message codes. These are also specific to firmware.
 */
#define	MILAN_DXIO_OP_INIT		0x00
#define	MILAN_DXIO_OP_GET_SM_STATE	0x09
#define	MILAN_DXIO_OP_SET_LINK_SPEED	0x10
#define	MILAN_DXIO_OP_GET_VERSION	0x13
#define	MILAN_DXIO_OP_GET_ENGINE_CFG	0x14
#define	MILAN_DXIO_OP_SET_VARIABLE	0x22
#define	MILAN_DXIO_OP_LOAD_DATA		0x23
#define	MILAN_DXIO_OP_LOAD_CAPS		0x24
#define	MILAN_DXIO_OP_RELOAD_SM		0x2d
#define	MILAN_DXIO_OP_GET_ERROR_LOG	0x2b
#define	MILAN_DXIO_OP_SET_RUNTIME_PROP	0x3a
#define	MILAN_DXIO_OP_XGMI_BER_ADAPT	0x40
#define	MILAN_DXIO_OP_INIT_ESM		0x53

/*
 * The 0x300 in these are used to indicate deferred returns.
 */
#define	MILAN_DXIO_OP_START_SM		0x307
#define	MILAN_DXIO_OP_RESUME_SM		0x308

/*
 * Various DXIO Reply codes. Most of these codes are undocumented. In general,
 * most RPCs will return MILAN_DXIO_RPC_OK to indicate success. However, we have
 * seen MILAN_DXIO_OP_SET_VARIABLE actually return MILAN_DXIO_RPC_MBOX_IDLE as
 * it seems to actually be using the mailboxes under the hood.
 */
#define	MILAN_DXIO_RPC_NULL		0
#define	MILAN_DXIO_RPC_TIMEOUT		1
#define	MILAN_DXIO_RPC_ERROR		2
#define	MILAN_DXIO_RPC_OK		3
#define	MILAN_DXIO_RPC_UNKNOWN_LOCK	4
#define	MILAN_DXIO_RPC_EAGAIN		5
#define	MILAN_DXIO_RPC_MBOX_IDLE	6
#define	MILAN_DXIO_RPC_MBOX_BUSY	7
#define	MILAN_DXIO_RPC_MBOX_DONE	8

/*
 * Different data heaps that can be loaded.
 */
#define	MILAN_DXIO_HEAP_EMPTY		0x00
#define	MILAN_DXIO_HEAP_FABRIC_INIT	0x01
#define	MILAN_DXIO_HEAP_MACPCS		0x02
#define	MILAN_DXIO_HEAP_ENGINE_CONFIG	0x03
#define	MILAN_DXIO_HEAP_CAPABILITIES	0x04
#define	MILAN_DXIO_HEAP_GPIO		0x05
#define	MILAN_DXIO_HEAP_ANCILLARY	0x06

/*
 * Some commands refer to an explicit engine in their request.
 */
#define	MILAN_DXIO_ENGINE_NONE		0x00
#define	MILAN_DXIO_ENGINE_PCIE		0x01
#define	MILAN_DXIO_ENGINE_USB		0x02
#define	MILAN_DXIO_ENGINE_SATA		0x03

/*
 * The various variable codes that one can theoretically use with
 * MILAN_DXIO_OP_SET_VARIABLE.
 */
#define	MILAN_DXIO_VAR_SKIP_PSP			0x0d
#define	MLIAN_DXIO_VAR_RET_AFTER_MAP		0x0e
#define	MILAN_DXIO_VAR_RET_AFTER_CONF		0x0f
#define	MILAN_DXIO_VAR_ANCILLARY_V1		0x10
#define	MILAN_DXIO_VAR_NTB_HP_EN		0x11
#define	MILAN_DXIO_VAR_MAP_EXACT_MATCH		0x12
#define	MILAN_DXIO_VAR_S3_MODE			0x13
#define	MILAN_DXIO_VAR_PHY_PROG			0x14
#define	MILAN_DXIO_VAR_PCIE_COMPL		0x23
#define	MILAN_DXIO_VAR_SLIP_INTERVAL		0x24
#define	MILAN_DXIO_VAR_PCIE_POWER_OFF_DELAY	0x25

/*
 * The following are all values that can be used with
 * MILAN_DXIO_OP_SET_RUNTIME_PROP. It consists of various codes. Some of which
 * have their own codes.
 */
#define	MILAN_DXIO_RT_SET_CONF		0x00
#define	MILAN_DXIO_RT_SET_CONF_DXIO_WA		0x03
#define	MILAN_DXIO_RT_SET_CONF_SPC_WA		0x04
#define	MILAN_DXIO_RT_SET_CONF_FC_CRED_WA_DIS	0x05
#define	MILAN_DXIO_RT_SET_CONF_TX_CLOCK		0x06
#define	MILAN_DXIO_RT_SET_CONF_SRNS		0x08
#define	MILAN_DXIO_RT_SET_CONF_TX_FIFO_MODE	0x09
#define	MILAN_DXIO_RT_SET_CONF_DLF_WA_DIS	0x0a
#define	MILAN_DXIO_RT_SET_CONF_CE_SRAM_ECC	0x0b

#define	MILAN_DXIO_RT_CONF_PCIE_TRAIN	0x02
#define	MILAN_DXIO_RT_CONF_CLOCK_GATE	0x03
#define	MILAN_DXIO_RT_PLEASE_LEAVE	0x05
#define	MILAN_DXIO_RT_FORGET_BER	0x22

/*
 * DXIO Link training state machine states
 */
typedef enum milan_dxio_sm_state {
	MILAN_DXIO_SM_INIT =		0x00,
	MILAN_DXIO_SM_DISABLED =	0x01,
	MILAN_DXIO_SM_SCANNED =		0x02,
	MILAN_DXIO_SM_CANNED =		0x03,
	MILAN_DXIO_SM_LOADED =		0x04,
	MILAN_DXIO_SM_CONFIGURED =	0x05,
	MILAN_DXIO_SM_IN_EARLY_TRAIN =	0x06,
	MILAN_DXIO_SM_EARLY_TRAINED =	0x07,
	MILAN_DXIO_SM_VETTING =		0x08,
	MILAN_DXIO_SM_GET_VET =		0x09,
	MILAN_DXIO_SM_NO_VET =		0x0a,
	MILAN_DXIO_SM_GPIO_INIT =	0x0b,
	MILAN_DXIO_SM_NHP_TRAIN =	0x0c,
	MILAN_DXIO_SM_DONE =		0x0d,
	MILAN_DXIO_SM_ERROR =		0x0e,
	MILAN_DXIO_SM_MAPPED =		0x0f
} milan_dxio_sm_state_t;

/*
 * PCIe Link Training States
 */
typedef enum milan_dxio_pcie_state {
	MILAN_DXIO_PCIE_ASSERT_RESET_GPIO	= 0x00,
	MILAN_DXIO_PCIE_ASSERT_RESET_DURATION	= 0x01,
	MILAN_DXIO_PCIE_DEASSERT_RESET_GPIO	= 0x02,
	MILAN_DXIO_PCIE_ASSERT_RESET_ENTRY	= 0x03,
	MILAN_DXIO_PCIE_GPIO_RESET_TIMEOUT	= 0x04,
	MILAN_DXIO_PCIE_RELEASE_LINK_TRAIN	= 0x05,
	MILAN_DXIO_PCIE_DETECT_PRESENCE		= 0x06,
	MILAN_DXIO_PCIE_DETECTING		= 0x07,
	MILAN_DXIO_PCIE_BAD_LANE		= 0x08,
	MILAN_DXIO_PCIE_GEN2_FAILURE		= 0x09,
	MILAN_DXIO_PCIE_REACHED_L0		= 0x0a,
	MILAN_DXIO_PCIE_VCO_NEGOTIATED		= 0x0b,
	MILAN_DXIO_PCIE_FORCE_RETRAIN		= 0x0c,
	MILAN_DXIO_PCIE_FAILED			= 0x0d,
	MILAN_DXIO_PCIE_SUCCESS			= 0x0e,
	MILAN_DXIO_PCIE_GRAPHICS_WORKAROUND	= 0x0f,
	MILAN_DXIO_PCIE_COMPLIANCE_MODE		= 0x10,
	MILAN_DXIO_PCIE_NO_DEVICE		= 0x11,
	MILAN_DXIO_PCIE_COMPLETED		= 0x12
} milan_dxio_pcie_state_t;

/*
 * When using MILAN_DXIO_OP_GET_SM_STATE, the following structure is actually
 * filled in via the RPC argument. This structure is more generally used amongst
 * different RPCs; however, since the state machine can often get different
 * types of requests this ends up mattering a bit more.
 */
typedef enum milan_dxio_data_type {
	MILAN_DXIO_DATA_TYPE_NONE	 = 0,
	MILAN_DXIO_DATA_TYPE_GENERIC,
	MILAN_DXIO_DATA_TYPE_SM,
	MILAN_DXIO_DATA_TYPE_HPSM,
	MILAN_DXIO_DATA_TYPE_RESET
} milan_dxio_data_type_t;

typedef struct milan_dxio_reply {
	milan_dxio_data_type_t	mds_type;
	uint8_t			mds_nargs;
	uint32_t		mds_arg0;
	uint32_t		mds_arg1;
	uint32_t		mds_arg2;
	uint32_t		mds_arg3;
} milan_dxio_reply_t;

/*
 * Types of DXIO Link speed updates. These must be ORed in with the base code.
 */
#define	MILAN_DXIO_LINK_SPEED_SINGLE	0x800

/*
 * A structure that can be used to pass around a SMU RPC request.
 */
typedef struct milan_smu_rpc {
	uint32_t	msr_req;
	uint32_t	msr_resp;
	uint32_t	msr_arg0;
	uint32_t	msr_arg1;
	uint32_t	msr_arg2;
	uint32_t	msr_arg3;
	uint32_t	msr_arg4;
	uint32_t	msr_arg5;
} milan_smu_rpc_t;

/*
 * This is a structure that we can use internally to pass around a DXIO RPC
 * request.
 */
typedef struct milan_dxio_rpc {
	uint32_t	mdr_req;
	uint32_t	mdr_dxio_resp;
	uint32_t	mdr_smu_resp;
	uint32_t	mdr_engine;
	uint32_t	mdr_arg0;
	uint32_t	mdr_arg1;
	uint32_t	mdr_arg2;
	uint32_t	mdr_arg3;
} milan_dxio_rpc_t;

typedef struct milan_bridge_info {
	uint8_t	mpbi_dev;
	uint8_t	mpbi_func;
} milan_bridge_info_t;

/*
 * These three tables encode knowledge about how the SoC assigns devices and
 * functions to root ports.
 */
static const milan_bridge_info_t milan_pcie0[MILAN_IOMS_MAX_PCIE_BRIDGES] = {
	{ 0x1, 0x1 },
	{ 0x1, 0x2 },
	{ 0x1, 0x3 },
	{ 0x1, 0x4 },
	{ 0x1, 0x5 },
	{ 0x1, 0x6 },
	{ 0x1, 0x7 },
	{ 0x2, 0x1 }
};

static const milan_bridge_info_t milan_pcie1[MILAN_IOMS_MAX_PCIE_BRIDGES] = {
	{ 0x3, 0x1 },
	{ 0x3, 0x2 },
	{ 0x3, 0x3 },
	{ 0x3, 0x4 },
	{ 0x3, 0x5 },
	{ 0x3, 0x6 },
	{ 0x3, 0x7 },
	{ 0x4, 0x1 }
};

static const milan_bridge_info_t milan_pcie2[MILAN_IOMS_WAFL_PCIE_NBRIDGES] = {
	{ 0x5, 0x1 },
	{ 0x5, 0x2 }
};

/*
 * These are internal bridges tat correspond to NBIFs.
 */
static const milan_bridge_info_t milan_int_bridges[4] = {
	{ 0x7, 0x1 },
	{ 0x8, 0x1 },
	{ 0x8, 0x2 },
	{ 0x8, 0x3 }
};

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently following the recommendation of the PPR.
 */
typedef struct milan_ioapic_info {
	uint8_t mii_group;
	uint8_t mii_swiz;
	uint8_t mii_map;
} milan_ioapic_info_t;

static const milan_ioapic_info_t milan_ioapic_routes[MILAN_IOAPIC_R_NROUTES] = {
	{ .mii_group = 0x0, .mii_map = 0x10,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x1, .mii_map = 0x11,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x2, .mii_map = 0x12,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x3, .mii_map = 0x13,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x4, .mii_map = 0x10,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x5, .mii_map = 0x11,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x6, .mii_map = 0x12,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x7, .mii_map = 0x13,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_ABCD },
	{ .mii_group = 0x7, .mii_map = 0x0c,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x6, .mii_map = 0x0d,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x5, .mii_map = 0x0e,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x4, .mii_map = 0x0f,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x3, .mii_map = 0x0c,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x2, .mii_map = 0x0d,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x1, .mii_map = 0x0e,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x0, .mii_map = 0x0f,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_CDAB },
	{ .mii_group = 0x0, .mii_map = 0x08,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x1, .mii_map = 0x09,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x2, .mii_map = 0x0a,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x3, .mii_map = 0x0b,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x4, .mii_map = 0x08,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC },
	{ .mii_group = 0x5, .mii_map = 0x09,
	    .mii_swiz = MILAN_IOAPIC_R_ROUTE_INTX_SWIZZLE_DABC }
};

typedef enum milan_nbif_func_type {
	MILAN_NBIF_T_DUMMY,
	MILAN_NBIF_T_NTB,
	MILAN_NBIF_T_NVME,
	MILAN_NBIF_T_PTDMA,
	MILAN_NBIF_T_PSPCCP,
	MILAN_NBIF_T_USB,
	MILAN_NBIF_T_AZ,
	MILAN_NBIF_T_SATA
} milan_nbif_func_type_t;

/* XXX Track platform default presence */
typedef struct milan_nbif_info {
	milan_nbif_func_type_t	mni_type;
	uint8_t			mni_dev;
	uint8_t			mni_func;
} milan_nbif_info_t;

static const milan_nbif_info_t milan_nbif0[MILAN_NBIF0_NFUNCS] = {
	{ .mni_type = MILAN_NBIF_T_DUMMY, .mni_dev = 0, .mni_func = 0 },
	{ .mni_type = MILAN_NBIF_T_NTB, .mni_dev = 0, .mni_func = 1 },
	{ .mni_type = MILAN_NBIF_T_PTDMA, .mni_dev = 0, .mni_func = 2 }
};

static const milan_nbif_info_t milan_nbif1[MILAN_NBIF1_NFUNCS] = {
	{ .mni_type = MILAN_NBIF_T_DUMMY, .mni_dev = 0, .mni_func = 0 },
	{ .mni_type = MILAN_NBIF_T_PSPCCP, .mni_dev = 0, .mni_func = 1 },
	{ .mni_type = MILAN_NBIF_T_PTDMA, .mni_dev = 0, .mni_func = 2 },
	{ .mni_type = MILAN_NBIF_T_USB, .mni_dev = 0, .mni_func = 3 },
	{ .mni_type = MILAN_NBIF_T_AZ, .mni_dev = 0, .mni_func = 4 },
	{ .mni_type = MILAN_NBIF_T_SATA, .mni_dev = 1, .mni_func = 0 },
	{ .mni_type = MILAN_NBIF_T_SATA, .mni_dev = 2, .mni_func = 0 }
};

static const milan_nbif_info_t milan_nbif2[MILAN_NBIF2_NFUNCS] = {
	{ .mni_type = MILAN_NBIF_T_DUMMY, .mni_dev = 0, .mni_func = 0 },
	{ .mni_type = MILAN_NBIF_T_NTB, .mni_dev = 0, .mni_func = 1 },
	{ .mni_type = MILAN_NBIF_T_NVME, .mni_dev = 0, .mni_func = 2 }
};

typedef enum milan_nbif_func_flag {
	/*
	 * This NBIF function should be enabled.
	 */
	MILAN_NBIF_F_ENABLED	= 1 << 0,
	/*
	 * This NBIF does not need any configuration or manipulation. This
	 * generally is the case because we have a dummy function.
	 */
	MILAN_NBIF_F_NO_CONFIG	= 1 << 1
} milan_nbif_func_flag_t;

typedef struct milan_nbif_func {
	milan_nbif_func_type_t	mne_type;
	milan_nbif_func_flag_t	mne_flags;
	uint8_t			mne_dev;
	uint8_t			mne_func;
	uint32_t		mne_func_smn_base;
} milan_nbif_func_t;

typedef struct milan_nbif {
	uint32_t		mn_nbif_smn_base;
	uint32_t		mn_nbif_alt_smn_base;
	uint8_t			mn_nbifno;
	uint8_t			mn_nfuncs;
	milan_nbif_func_t	mn_funcs[MILAN_NBIF_MAX_FUNCS];
} milan_nbif_t;

typedef enum milan_pcie_bridge_flags {
	/*
	 * Indicates that there is a corresponding zen_dxio_engine_t associated
	 * with this bridge.
	 */
	MILAN_PCIE_BRIDGE_F_MAPPED	= 1 << 0,
	/*
	 * Indicates that this bridge has been hidden from visibility. When a
	 * port is not used, the brige is hidden.
	 */
	MILAN_PCIE_BRIDGE_F_HIDDEN	= 1 << 1,
	/*
	 * This bridge is being used for hotplug shenanigans. This means that is
	 * actually meaningful.
	 */
	MILAN_PCIE_BRIDGE_F_HOTPLUG	= 1 << 2
} milan_pcie_bridge_flags_t;

typedef struct milan_pcie_bridge {
	milan_pcie_bridge_flags_t	mpb_flags;
	uint8_t				mpb_device;
	uint8_t				mpb_func;
	uint32_t			mpb_iohc_smn_base;
	uint32_t			mpb_port_smn_base;
	uint32_t			mpb_cfg_smn_base;
	zen_dxio_engine_t		*mpb_engine;
	smu_hotplug_type_t		mpb_hp_type;
	uint16_t			mpb_hp_slotno;
	uint32_t			mpb_hp_smu_mask;
} milan_pcie_bridge_t;

/*
 * This structure and the following table encodes the mapping of the set of dxio
 * lanes to a give PCIe port on an IOMS. This is ordered such that all of the
 * normal engines are present; however, the wafl port, being special is not
 * here. The dxio engine uses different lane numbers than the phys. Note, that
 * all lanes here are inclusive. e.g. [start, end].
 */
typedef struct milan_pcie_port_info {
	const char	*mppi_name;
	uint16_t	mppi_dxio_start;
	uint16_t	mppi_dxio_end;
	uint16_t	mppi_phy_start;
	uint16_t	mppi_phy_end;
} milan_pcie_port_info_t;

static const milan_pcie_port_info_t milan_lane_maps[8] = {
	{ "G0", 0x10, 0x1f, 0x10, 0x1f },
	{ "P0", 0x2a, 0x39, 0x00, 0x0f },
	{ "P1", 0x3a, 0x49, 0x20, 0x2f },
	{ "G1", 0x00, 0x0f, 0x30, 0x3f },
	{ "G3", 0x72, 0x81, 0x60, 0x6f },
	{ "P3", 0x5a, 0x69, 0x70, 0x7f },
	{ "P2", 0x4a, 0x59, 0x50, 0x5f },
	{ "G2", 0x82, 0x91, 0x40, 0x4f }
};

static const milan_pcie_port_info_t milan_wafl_map = {
	"WAFL", 0x24, 0x25, 0x80, 0x81
};

typedef enum milan_pcie_port_flags {
	/*
	 * This is used to indicate that a single engine exists on the port that
	 * is in use.
	 */
	MILAN_PCIE_PORT_F_USED		= 1 << 0,
	/*
	 * This indicates that at least one engine mapped to this port is
	 * considered hotpluggable. This is importnat for making sure that we
	 * deal with the visibility of PCIe devices correctl.
	 */
	MILAN_PCIE_PORT_F_HAS_HOTPLUG	= 1 << 1,
} milan_pcie_port_flags_t;

typedef struct milan_pcie_port {
	milan_pcie_port_flags_t	mpp_flags;
	uint8_t			mpp_portno;
	uint8_t			mpp_sdp_unit;
	uint8_t			mpp_sdp_port;
	uint8_t			mpp_nbridges;
	uint16_t		mpp_dxio_lane_start;
	uint16_t		mpp_dxio_lane_end;
	uint16_t		mpp_phys_lane_start;
	uint16_t		mpp_phys_lane_end;
	uint32_t		mpp_core_smn_addr;
	uint32_t		mpp_strap_smn_addr;
	milan_pcie_bridge_t	mpp_bridges[MILAN_IOMS_MAX_PCIE_BRIDGES];
} milan_pcie_port_t;

typedef enum milan_ioms_flag {
	MILAN_IOMS_F_HAS_FCH	= 1 << 0,
	MILAN_IOMS_F_HAS_WAFL	= 1 << 1
} milan_ioms_flag_t;

/*
 * Warning: These memlists cannot be given directly to PCI. They expect to be
 * kmem_alloc'd which we are not doing here at all.
 */
typedef struct ioms_memlists {
	kmutex_t		im_lock;
	struct memlist_pool	im_pool;
	struct memlist		*im_io_avail;
	struct memlist		*im_io_used;
	struct memlist		*im_mmio_avail;
	struct memlist		*im_mmio_used;
	struct memlist		*im_bus_avail;
	struct memlist		*im_bus_used;
} ioms_memlists_t;

typedef struct milan_ioms {
	milan_ioms_flag_t	mio_flags;
	uint32_t		mio_iohc_smn_base;
	uint32_t		mio_ioagr_smn_base;
	uint32_t		mio_sdpmux_smn_base;
	uint32_t		mio_ioapic_smn_base;
	uint32_t		mio_iommul1_smn_base;
	uint32_t		mio_iommul2_smn_base;
	uint16_t		mio_pci_busno;
	uint8_t			mio_num;
	uint8_t			mio_fabric_id;
	uint8_t			mio_comp_id;
	uint8_t			mio_npcie_ports;
	uint8_t			mio_nnbifs;
	milan_pcie_port_t	mio_pcie_ports[MILAN_IOMS_MAX_PCIE_PORTS];
	milan_nbif_t		mio_nbifs[MILAN_IOMS_MAX_NBIF];
	ioms_memlists_t		mio_memlists;
} milan_ioms_t;

typedef struct milan_dxio_config {
	zen_dxio_platform_t	*mdc_conf;
	zen_dxio_anc_data_t	*mdc_anc;
	uint64_t		mdc_pa;
	uint64_t		mdc_anc_pa;
	uint32_t		mdc_alloc_len;
	uint32_t		mdc_conf_len;
	uint32_t		mdc_anc_len;
} milan_dxio_config_t;

typedef struct milan_iodie {
	kmutex_t		mi_df_ficaa_lock;
	kmutex_t		mi_smn_lock;
	kmutex_t		mi_smu_lock;
	kmutex_t		mi_pcie_strap_lock;
	uint8_t			mi_node_id;
	uint8_t			mi_dfno;
	uint8_t			mi_smn_busno;
	uint8_t			mi_nioms;
	uint8_t			mi_nccds;
	uint8_t			mi_smu_fw[3];
	uint32_t		mi_dxio_fw[2];
	milan_dxio_sm_state_t	mi_state;
	milan_dxio_config_t	mi_dxio_conf;
	milan_ioms_t		mi_ioms[MILAN_IOMS_PER_IODIE];
	milan_ccd_t		mi_ccds[MILAN_MAX_CCDS_PER_IODIE];
	struct milan_soc	*mi_soc;
} milan_iodie_t;

typedef struct milan_soc {
	uint8_t			ms_socno;
	uint8_t			ms_ndies;
	char			ms_brandstr[CPUID_BRANDSTR_STRLEN + 1];
	milan_iodie_t		ms_iodies[MILAN_FABRIC_MAX_DIES_PER_SOC];
	struct milan_fabric	*ms_fabric;
} milan_soc_t;

typedef struct milan_hotplug {
	smu_hotplug_table_t	*mh_table;
	uint64_t		mh_pa;
	uint32_t		mh_alloc_len;
} milan_hotplug_t;

CTASSERT(sizeof (smu_hotplug_table_t) <= MMU_PAGESIZE);

typedef struct milan_fabric {
	uint8_t		mf_nsocs;
	/*
	 * This represents a cache of everything that we've found in the fabric.
	 */
	uint_t		mf_total_ioms;
	/*
	 * These are masks and shifts that describe how to take apart an ID into
	 * its node ID and corresponding component ID.
	 */
	uint8_t		mf_node_shift;
	uint32_t	mf_node_mask;
	uint32_t	mf_comp_mask;
	/*
	 * While TOM and TOM2 are nominally set per-core and per-IOHC, these
	 * values are fabric-wide.
	 */
	uint64_t	mf_tom;
	uint64_t	mf_tom2;
	uint64_t	mf_mmio64_base;
	milan_hotplug_t	mf_hotplug;
	milan_soc_t	mf_socs[MILAN_FABRIC_MAX_SOCS];
} milan_fabric_t;

/*
 * Function callback signatures for making operating on a given unit simpler.
 */
typedef int (*milan_iodie_cb_f)(milan_fabric_t *, milan_soc_t *,
    milan_iodie_t *, void *);
typedef int (*milan_ioms_cb_f)(milan_fabric_t *, milan_soc_t *,
    milan_iodie_t *, milan_ioms_t *, void *);
typedef int (*milan_nbif_cb_f)(milan_fabric_t *, milan_soc_t *,
    milan_iodie_t *, milan_ioms_t *, milan_nbif_t *, void *);
typedef int (*milan_pcie_port_cb_f)(milan_fabric_t *, milan_soc_t *,
    milan_iodie_t *, milan_ioms_t *, milan_pcie_port_t *port, void *);
typedef int (*milan_bridge_cb_f)(milan_fabric_t *, milan_soc_t *,
    milan_iodie_t *, milan_ioms_t *, milan_pcie_port_t *port,
    milan_pcie_bridge_t *, void *);

typedef int (*milan_ccd_cb_f)(milan_ccd_t *, void *);
typedef int (*milan_ccx_cb_f)(milan_ccx_t *, void *);
typedef int (*milan_core_cb_f)(milan_core_t *, void *);

/*
 * XXX Belongs in a header.
 */
extern void *contig_alloc(size_t, ddi_dma_attr_t *, uintptr_t, int);
extern void contig_free(void *, size_t);

static boolean_t milan_smu_rpc_read_brand_string(milan_iodie_t *,
    char *, size_t);

/*
 * Our primary global data. This is the reason that we exist.
 */
static milan_fabric_t milan_fabric;
static uint_t nthreads;

/*
 * Variable to let us dump all SMN traffic while still developing.
 */
int milan_smn_log = 0;

static int
milan_fabric_walk_iodie(milan_fabric_t *fabric, milan_iodie_cb_f func,
    void *arg)
{
	for (uint_t socno = 0; socno < fabric->mf_nsocs; socno++) {
		milan_soc_t *soc = &fabric->mf_socs[socno];
		for (uint_t iono = 0; iono < soc->ms_ndies; iono++) {
			int ret;
			milan_iodie_t *iodie = &soc->ms_iodies[iono];

			ret = func(fabric, soc, iodie, arg);
			if (ret != 0) {
				return (ret);
			}
		}
	}

	return (0);
}

typedef struct milan_fabric_ioms_cb {
	milan_ioms_cb_f	mfic_func;
	void		*mfic_arg;
} milan_fabric_ioms_cb_t;

static int
milan_fabric_walk_ioms_iodie_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	milan_fabric_ioms_cb_t *cb = arg;

	for (uint_t iomsno = 0; iomsno < iodie->mi_nioms; iomsno++) {
		int ret;
		milan_ioms_t *ioms = &iodie->mi_ioms[iomsno];

		ret = cb->mfic_func(fabric, soc, iodie, ioms, cb->mfic_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
milan_fabric_walk_ioms(milan_fabric_t *fabric, milan_ioms_cb_f func, void *arg)
{
	milan_fabric_ioms_cb_t cb;

	cb.mfic_func = func;
	cb.mfic_arg = arg;
	return (milan_fabric_walk_iodie(fabric, milan_fabric_walk_ioms_iodie_cb,
	    &cb));
}

typedef struct milan_fabric_nbif_cb {
	milan_nbif_cb_f	mfnc_func;
	void		*mfnc_arg;
} milan_fabric_nbif_cb_t;

static int
milan_fabric_walk_nbif_ioms_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	milan_fabric_nbif_cb_t *cb = arg;

	for (uint_t nbifno = 0; nbifno < ioms->mio_nnbifs; nbifno++) {
		int ret;
		milan_nbif_t *nbif = &ioms->mio_nbifs[nbifno];
		ret = cb->mfnc_func(fabric, soc, iodie, ioms, nbif,
		    cb->mfnc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
milan_fabric_walk_nbif(milan_fabric_t *fabric, milan_nbif_cb_f func, void *arg)
{
	milan_fabric_nbif_cb_t cb;

	cb.mfnc_func = func;
	cb.mfnc_arg = arg;
	return (milan_fabric_walk_ioms(fabric, milan_fabric_walk_nbif_ioms_cb,
	    &cb));
}

typedef struct milan_fabric_pcie_port_cb {
	milan_pcie_port_cb_f	mfppc_func;
	void			*mfppc_arg;
} milan_fabric_pcie_port_cb_t;

static int
milan_fabric_walk_pcie_port_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	milan_fabric_pcie_port_cb_t *cb = arg;

	for (uint_t portno = 0; portno < ioms->mio_npcie_ports; portno++) {
		int ret;
		milan_pcie_port_t *port = &ioms->mio_pcie_ports[portno];

		ret = cb->mfppc_func(fabric, soc, iodie, ioms, port,
		    cb->mfppc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
milan_fabric_walk_pcie_port(milan_fabric_t *fabric, milan_pcie_port_cb_f func,
    void *arg)
{
	milan_fabric_pcie_port_cb_t cb;

	cb.mfppc_func = func;
	cb.mfppc_arg = arg;
	return (milan_fabric_walk_ioms(fabric, milan_fabric_walk_pcie_port_cb,
	    &cb));
}

typedef struct milan_fabric_bridge_cb {
	milan_bridge_cb_f	mfbc_func;
	void			*mfbc_arg;
} milan_fabric_bridge_cb_t;

static int
milan_fabric_walk_bridge_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    void *arg)
{
	milan_fabric_bridge_cb_t *cb = arg;

	for (uint_t bridgeno = 0; bridgeno < port->mpp_nbridges;
	    bridgeno++) {
		int ret;
		milan_pcie_bridge_t *bridge = &port->mpp_bridges[bridgeno];

		ret = cb->mfbc_func(fabric, soc, iodie, ioms, port, bridge,
		    cb->mfbc_arg);
		if (ret != 0) {
			return (ret);
		}
	}

	return (0);
}

static int
milan_fabric_walk_bridge(milan_fabric_t *fabric, milan_bridge_cb_f func,
    void *arg)
{
	milan_fabric_bridge_cb_t cb;

	cb.mfbc_func = func;
	cb.mfbc_arg = arg;
	return (milan_fabric_walk_pcie_port(fabric, milan_fabric_walk_bridge_cb,
	    &cb));
}

typedef struct milan_fabric_ccd_cb {
	milan_ccd_cb_f	mfcc_func;
	void		*mfcc_arg;
} milan_fabric_ccd_cb_t;

static int
milan_fabric_walk_ccd_iodie_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	milan_fabric_ccd_cb_t *cb = arg;

	for (uint8_t ccdno = 0; ccdno < iodie->mi_nccds; ccdno++) {
		int ret;
		milan_ccd_t *ccd = &iodie->mi_ccds[ccdno];

		if ((ret = cb->mfcc_func(ccd, cb->mfcc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
milan_fabric_walk_ccd(milan_ccd_cb_f func, void *arg)
{
	milan_fabric_ccd_cb_t cb;

	cb.mfcc_func = func;
	cb.mfcc_arg = arg;
	return (milan_fabric_walk_iodie(&milan_fabric,
	    milan_fabric_walk_ccd_iodie_cb, &cb));
}

typedef struct milan_fabric_ccx_cb {
	milan_ccx_cb_f	mfcc_func;
	void		*mfcc_arg;
} milan_fabric_ccx_cb_t;

static int
milan_fabric_walk_ccx_ccd_cb(milan_ccd_t *ccd, void *arg)
{
	milan_fabric_ccx_cb_t *cb = arg;

	for (uint8_t ccxno = 0; ccxno < ccd->mcd_nccxs; ccxno++) {
		int ret;
		milan_ccx_t *ccx = &ccd->mcd_ccxs[ccxno];

		if ((ret = cb->mfcc_func(ccx, cb->mfcc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
milan_fabric_walk_ccx(milan_ccx_cb_f func, void *arg)
{
	milan_fabric_ccx_cb_t cb;

	cb.mfcc_func = func;
	cb.mfcc_arg = arg;
	return (milan_fabric_walk_ccd(milan_fabric_walk_ccx_ccd_cb, &cb));
}

typedef struct milan_fabric_core_cb {
	milan_core_cb_f	mfcc_func;
	void		*mfcc_arg;
} milan_fabric_core_cb_t;

static int
milan_fabric_walk_core_ccx_cb(milan_ccx_t *ccx, void *arg)
{
	milan_fabric_core_cb_t *cb = arg;

	for (uint8_t coreno = 0; coreno < ccx->mcx_ncores; coreno++) {
		int ret;
		milan_core_t *core = &ccx->mcx_cores[coreno];

		if ((ret = cb->mfcc_func(core, cb->mfcc_arg)) != 0)
			return (ret);
	}

	return (0);
}

static int
milan_fabric_walk_core(milan_core_cb_f func, void *arg)
{
	milan_fabric_core_cb_t cb;

	cb.mfcc_func = func;
	cb.mfcc_arg = arg;
	return (milan_fabric_walk_ccx(milan_fabric_walk_core_ccx_cb, &cb));
}

typedef struct milan_fabric_thread_cb {
	milan_thread_cb_f	mftc_func;
	void			*mftc_arg;
} milan_fabric_thread_cb_t;

static int
milan_fabric_walk_thread_core_cb(milan_core_t *core, void *arg)
{
	milan_fabric_thread_cb_t *cb = arg;

	for (uint8_t threadno = 0; threadno < core->mc_nthreads; threadno++) {
		int ret;
		milan_thread_t *thread = &core->mc_threads[threadno];

		if ((ret = cb->mftc_func(thread, cb->mftc_arg)) != 0)
			return (ret);
	}

	return (0);
}

int
milan_fabric_walk_thread(milan_thread_cb_f func, void *arg)
{
	milan_fabric_thread_cb_t cb;

	cb.mftc_func = func;
	cb.mftc_arg = arg;
	return (milan_fabric_walk_core(milan_fabric_walk_thread_core_cb, &cb));
}

typedef struct {
	uint32_t	mffi_dest;
	milan_ioms_t	*mffi_ioms;
} milan_fabric_find_ioms_t;

static int
milan_fabric_find_ioms_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	milan_fabric_find_ioms_t *mffi = arg;

	if (mffi->mffi_dest == ioms->mio_fabric_id) {
		mffi->mffi_ioms = ioms;
	}

	return (0);
}

static int
milan_fabric_find_ioms_by_bus_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	milan_fabric_find_ioms_t *mffi = arg;

	if (mffi->mffi_dest == ioms->mio_pci_busno) {
		mffi->mffi_ioms = ioms;
	}

	return (0);
}

static milan_ioms_t *
milan_fabric_find_ioms(milan_fabric_t *fabric, uint32_t destid)
{
	milan_fabric_find_ioms_t mffi;

	mffi.mffi_dest = destid;
	mffi.mffi_ioms = NULL;

	milan_fabric_walk_ioms(fabric, milan_fabric_find_ioms_cb, &mffi);

	return (mffi.mffi_ioms);
}

static milan_ioms_t *
milan_fabric_find_ioms_by_bus(milan_fabric_t *fabric, uint32_t pci_bus)
{
	milan_fabric_find_ioms_t mffi;

	mffi.mffi_dest = pci_bus;
	mffi.mffi_ioms = NULL;

	milan_fabric_walk_ioms(fabric, milan_fabric_find_ioms_by_bus_cb, &mffi);

	return (mffi.mffi_ioms);
}

typedef struct {
	const milan_iodie_t *mffp_iodie;
	uint16_t mffp_start;
	uint16_t mffp_end;
	milan_pcie_port_t *mffp_port;
} milan_fabric_find_port_t;

static int
milan_fabric_find_port_by_lanes_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    void *arg)
{
	milan_fabric_find_port_t *mffp = arg;

	if (mffp->mffp_iodie != iodie) {
		return (0);
	}

	if (mffp->mffp_start >= port->mpp_dxio_lane_start &&
	    mffp->mffp_start <= port->mpp_dxio_lane_end &&
	    mffp->mffp_end >= port->mpp_dxio_lane_start &&
	    mffp->mffp_end <= port->mpp_dxio_lane_end) {
		mffp->mffp_port = port;
		return (1);
	}

	return (0);
}


static milan_pcie_port_t *
milan_fabric_find_port_by_lanes(milan_fabric_t *fabric, milan_iodie_t *iodie,
    uint16_t start, uint16_t end)
{
	milan_fabric_find_port_t mffp;

	mffp.mffp_iodie = iodie;
	mffp.mffp_start = start;
	mffp.mffp_end = end;
	mffp.mffp_port = NULL;
	ASSERT3U(start, <=, end);

	(void) milan_fabric_walk_pcie_port(fabric,
	    milan_fabric_find_port_by_lanes_cb, &mffp);

	return (mffp.mffp_port);
}

/*
 * XXX optimise me so this isn't N^2 for the caller, ugh.  Requires documenting
 * and maintaining an invariant ordering in these traversals.  As it is, this
 * is just about the worst implementation imaginable.  Consider instead putting
 * a pointer to the fabric thread into struct machcpu or perhaps hashing this
 * somewhere; it's static on our architecture.
 */
typedef struct milan_fabric_find_thread {
	uint32_t	mfft_search;
	uint32_t	mfft_count;
	milan_thread_t	*mfft_found;
} milan_fabric_find_thread_t;

static int
milan_fabric_find_thread_by_cpuid_cb(milan_thread_t *thread, void *arg)
{
	milan_fabric_find_thread_t *mfft = arg;
	if (mfft->mfft_count == mfft->mfft_search) {
		mfft->mfft_found = thread;
		return (1);
	}
	++mfft->mfft_count;
	return (0);
}

milan_thread_t *
milan_fabric_find_thread_by_cpuid(uint32_t cpuid)
{
	milan_fabric_find_thread_t mfft;

	mfft.mfft_search = cpuid;
	mfft.mfft_count = 0;
	mfft.mfft_found = NULL;
	(void) milan_fabric_walk_thread(milan_fabric_find_thread_by_cpuid_cb,
	    &mfft);

	return (mfft.mfft_found);
}

/*
 * buf, len, and return value semantics match those of snprintf(9f).
 */
size_t
milan_fabric_thread_get_brandstr(const milan_thread_t *thread,
    char *buf, size_t len)
{
	milan_soc_t *soc = thread->mt_core->mc_ccx->mcx_ccd->mcd_iodie->mi_soc;
	return (snprintf(buf, len, "%s", soc->ms_brandstr));
}

static uint32_t
milan_df_read32(milan_iodie_t *iodie, uint8_t inst, uint8_t func, uint16_t reg)
{
	uint32_t val;

	mutex_enter(&iodie->mi_df_ficaa_lock);
	val = AMDZEN_DF_F4_FICAA_TARG_INST | AMDZEN_DF_F4_FICAA_SET_REG(reg) |
	    AMDZEN_DF_F4_FICAA_SET_FUNC(func) |
	    AMDZEN_DF_F4_FICAA_SET_INST(inst);
	pci_putl_func(0, iodie->mi_dfno, 4, AMDZEN_DF_F4_FICAA, val);
	val = pci_getl_func(0, iodie->mi_dfno, 4, AMDZEN_DF_F4_FICAD_LO);
	mutex_exit(&iodie->mi_df_ficaa_lock);

	return (val);
}

/*
 * A broadcast read is allowed to use PCIe configuration space directly to read
 * the register. Because we are not using the indirect registers, there is no
 * locking being used as the purpose of mi_df_ficaa_lock is just to ensure
 * there's only one use of it at any given time.
 */
static uint32_t
milan_df_bcast_read32(milan_iodie_t *iodie, uint8_t func, uint16_t reg)
{
	return (pci_getl_func(0, iodie->mi_dfno, func, reg));
}

static void
milan_df_bcast_write32(milan_iodie_t *iodie, uint8_t func, uint16_t reg,
    uint32_t val)
{
	pci_putl_func(0, iodie->mi_dfno, func, reg, val);
}

static uint32_t
milan_smn_read32(milan_iodie_t *iodie, uint32_t reg)
{
	uint32_t val;

	mutex_enter(&iodie->mi_smn_lock);
	pci_putl_func(iodie->mi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, reg);
	val = pci_getl_func(iodie->mi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA);
	if (milan_smn_log != 0) {
		cmn_err(CE_NOTE, "SMN R reg 0x%x: 0x%x", reg, val);
	}
	mutex_exit(&iodie->mi_smn_lock);

	return (val);
}

static void
milan_smn_write32(milan_iodie_t *iodie, uint32_t reg, uint32_t val)
{
	mutex_enter(&iodie->mi_smn_lock);
	if (milan_smn_log != 0) {
		cmn_err(CE_NOTE, "SMN W reg 0x%x: 0x%x", reg, val);
	}
	pci_putl_func(iodie->mi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_ADDR, reg);
	pci_putl_func(iodie->mi_smn_busno, AMDZEN_NB_SMN_DEVNO,
	    AMDZEN_NB_SMN_FUNCNO, AMDZEN_NB_SMN_DATA, val);
	mutex_exit(&iodie->mi_smn_lock);
}

static uint32_t
milan_iohc_read32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_IOHC_MAKE_ADDR(ioms->mio_iohc_smn_base, reg)));
}

static void
milan_iohc_write32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_IOHC_MAKE_ADDR(ioms->mio_iohc_smn_base, reg), val);
}

static uint32_t
milan_ioagr_read32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_IOAGR_MAKE_ADDR(ioms->mio_ioagr_smn_base, reg)));
}

static void
milan_ioagr_write32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_IOAGR_MAKE_ADDR(ioms->mio_ioagr_smn_base, reg), val);
}

static uint32_t
milan_sdpmux_read32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_SDPMUX_MAKE_ADDR(ioms->mio_sdpmux_smn_base, reg)));
}

static void
milan_sdpmux_write32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_SDPMUX_MAKE_ADDR(ioms->mio_sdpmux_smn_base, reg), val);
}

static uint32_t
milan_ioapic_read32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_IOAPIC_MAKE_ADDR(ioms->mio_ioapic_smn_base, reg)));
}

static void
milan_ioapic_write32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_IOAPIC_MAKE_ADDR(ioms->mio_ioapic_smn_base, reg), val);
}

static inline uint32_t
milan_iommul1_addr(const milan_ioms_t *ioms,
    milan_iommul1_type_t l1t, uint32_t reg)
{
	uint32_t base = ioms->mio_iommul1_smn_base;

	switch (l1t) {
	case IOMMU_L1_PCIE0:
	case IOMMU_L1_PCIE1:
	case IOMMU_L1_NBIF:
	case IOMMU_L1_IOAGR:
		base += MILAN_SMN_IOMMUL1_DEV_SHIFT(l1t);
		break;
	default:
		panic("unknown IOMMU l1 type: %x", l1t);
	}

	return (MILAN_SMN_IOMMUL1_MAKE_ADDR(base, reg));
}

static uint32_t
milan_iommul1_read32(milan_iodie_t *iodie, milan_ioms_t *ioms,
    milan_iommul1_type_t l1t, uint32_t reg)
{
	return (milan_smn_read32(iodie, milan_iommul1_addr(ioms, l1t, reg)));
}

static void
milan_iommul1_write32(milan_iodie_t *iodie, milan_ioms_t *ioms,
    milan_iommul1_type_t l1t, uint32_t reg, uint32_t val)
{
	milan_smn_write32(iodie, milan_iommul1_addr(ioms, l1t, reg), val);
}

static uint32_t
milan_iommul2_read32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_IOMMUL2_MAKE_ADDR(ioms->mio_iommul2_smn_base, reg)));
}

static void
milan_iommul2_write32(milan_iodie_t *iodie, milan_ioms_t *ioms, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_IOMMUL2_MAKE_ADDR(ioms->mio_iommul2_smn_base, reg), val);
}

static uint32_t
milan_nbif_read32(milan_iodie_t *iodie, milan_nbif_t *nbif, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_NBIF_MAKE_ADDR(nbif->mn_nbif_smn_base, reg)));
}

static void
milan_nbif_write32(milan_iodie_t *iodie, milan_nbif_t *nbif, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_NBIF_MAKE_ADDR(nbif->mn_nbif_smn_base, reg), val);
}

static uint32_t
milan_nbif_func_read32(milan_iodie_t *iodie, milan_nbif_func_t *func,
    uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_NBIF_FUNC_MAKE_ADDR(func->mne_func_smn_base, reg)));
}

static void
milan_nbif_func_write32(milan_iodie_t *iodie, milan_nbif_func_t *func,
    uint32_t reg, uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_NBIF_FUNC_MAKE_ADDR(func->mne_func_smn_base, reg), val);
}

static uint32_t
milan_nbif_alt_read32(milan_iodie_t *iodie, milan_nbif_t *nbif, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_NBIF_ALT_MAKE_ADDR(nbif->mn_nbif_alt_smn_base, reg)));
}

static void
milan_nbif_alt_write32(milan_iodie_t *iodie, milan_nbif_t *nbif, uint32_t reg,
    uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_NBIF_ALT_MAKE_ADDR(nbif->mn_nbif_alt_smn_base, reg), val);
}

static uint32_t
milan_iohc_pcie_read32(milan_iodie_t *iodie, milan_pcie_bridge_t *bridge,
    uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_IOHC_PCIE_MAKE_ADDR(bridge->mpb_iohc_smn_base, reg)));
}

static void
milan_iohc_pcie_write32(milan_iodie_t *iodie, milan_pcie_bridge_t *bridge,
    uint32_t reg, uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_IOHC_PCIE_MAKE_ADDR(bridge->mpb_iohc_smn_base, reg), val);
}

static uint32_t
milan_bridge_port_read32(milan_iodie_t *iodie, milan_pcie_bridge_t *bridge,
    uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_PCIE_PORT_MAKE_ADDR(bridge->mpb_port_smn_base, reg)));
}

static void
milan_bridge_port_write32(milan_iodie_t *iodie, milan_pcie_bridge_t *bridge,
    uint32_t reg, uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_PCIE_PORT_MAKE_ADDR(bridge->mpb_port_smn_base, reg), val);
}

static uint32_t
milan_pcie_core_read32(milan_iodie_t *iodie, milan_pcie_port_t *port,
    uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_PCIE_CORE_MAKE_ADDR(port->mpp_core_smn_addr, reg)));
}

static void
milan_pcie_core_write32(milan_iodie_t *iodie, milan_pcie_port_t *port,
    uint32_t reg, uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_PCIE_CORE_MAKE_ADDR(port->mpp_core_smn_addr, reg), val);
}

uint32_t
milan_smupwr_read32(milan_ccd_t *ccd, uint32_t reg)
{
	milan_iodie_t *iodie = ccd->mcd_iodie;

	return (milan_smn_read32(iodie,
	    MILAN_SMN_SMUPWR_MAKE_ADDR(ccd->mcd_smupwr_smn_base, reg)));
}

void
milan_smupwr_write32(milan_ccd_t *ccd, uint32_t reg, uint32_t val)
{
	milan_iodie_t *iodie = ccd->mcd_iodie;

	milan_smn_write32(iodie,
	    MILAN_SMN_SMUPWR_MAKE_ADDR(ccd->mcd_smupwr_smn_base, reg), val);
}

static uint32_t
milan_scfctp_read32(milan_iodie_t *iodie, milan_core_t *core, uint32_t reg)
{
	return (milan_smn_read32(iodie,
	    MILAN_SMN_SCFCTP_MAKE_ADDR(core->mc_scfctp_smn_base, reg)));
}

static void
milan_scfctp_write32(milan_iodie_t *iodie, milan_core_t *core,
    uint32_t reg, uint32_t val)
{
	milan_smn_write32(iodie,
	    MILAN_SMN_SCFCTP_MAKE_ADDR(core->mc_scfctp_smn_base, reg), val);
}

static void
milan_fabric_ioms_pcie_init(milan_ioms_t *ioms)
{
	for (uint_t pcino = 0; pcino < ioms->mio_npcie_ports; pcino++) {
		milan_pcie_port_t *port = &ioms->mio_pcie_ports[pcino];
		const milan_bridge_info_t *binfop = NULL;
		const milan_pcie_port_info_t *info;

		port->mpp_portno = pcino;
		if (pcino == MILAN_IOMS_WAFL_PCIE_PORT) {
			port->mpp_nbridges = MILAN_IOMS_WAFL_PCIE_NBRIDGES;
		} else {
			port->mpp_nbridges = MILAN_IOMS_MAX_PCIE_BRIDGES;
		}

		VERIFY3U(pcino, <=, MILAN_IOMS_WAFL_PCIE_PORT);
		switch (pcino) {
		case 0:
			/* XXX Macros */
			port->mpp_sdp_unit = 2;
			port->mpp_sdp_port = 0;
			binfop = milan_pcie0;
			break;
		case 1:
			port->mpp_sdp_unit = 3;
			port->mpp_sdp_port = 0;
			binfop = milan_pcie1;
			break;
		case MILAN_IOMS_WAFL_PCIE_PORT:
			port->mpp_sdp_unit = 4;
			port->mpp_sdp_port = 5;
			binfop = milan_pcie2;
			break;
		}

		if (pcino == MILAN_IOMS_WAFL_PCIE_PORT) {
			info = &milan_wafl_map;
		} else {
			info = &milan_lane_maps[ioms->mio_num * 2 + pcino];
		}

		port->mpp_dxio_lane_start = info->mppi_dxio_start;
		port->mpp_dxio_lane_end = info->mppi_dxio_end;
		port->mpp_phys_lane_start = info->mppi_phy_start;
		port->mpp_phys_lane_end = info->mppi_phy_end;

		port->mpp_core_smn_addr = MILAN_SMN_PCIE_CORE_BASE +
		    MILAN_SMN_PCIE_IOMS_SHIFT(ioms->mio_num) +
		    MILAN_SMN_PCIE_PORT_SHIFT(pcino);
		MILAN_SMN_VERIFY_BASE_ADDR(port->mpp_core_smn_addr,
		    MILAN_SMN_PCIE_CORE_BASE_BITS);

		port->mpp_strap_smn_addr = MILAN_SMN_PCIE_STRAP_BASE +
		    MILAN_SMN_PCIE_STRAP_IOMS_SHIFT(ioms->mio_num) +
		    MILAN_SMN_PCIE_STRAP_PORT_SHIFT(pcino);
		MILAN_SMN_VERIFY_BASE_ADDR(port->mpp_strap_smn_addr,
		    MILAN_SMN_PCIE_STRAP_BASE_BITS);

		for (uint_t bridgeno = 0; bridgeno < port->mpp_nbridges;
		    bridgeno++) {
			milan_pcie_bridge_t *bridge =
			    &port->mpp_bridges[bridgeno];
			uint32_t shift;

			bridge->mpb_device = binfop[bridgeno].mpbi_dev;
			bridge->mpb_func = binfop[bridgeno].mpbi_func;
			bridge->mpb_hp_type = SMU_HP_INVALID;

			shift = MILAN_SMN_PCIE_BRIDGE_SHIFT(bridgeno) +
			    MILAN_SMN_PCIE_PORT_SHIFT(pcino) +
			    MILAN_SMN_PCIE_IOMS_SHIFT(ioms->mio_num);
			bridge->mpb_port_smn_base = MILAN_SMN_PCIE_PORT_BASE +
			    shift;
			MILAN_SMN_VERIFY_BASE_ADDR(bridge->mpb_port_smn_base,
			    MILAN_SMN_PCIE_PORT_BASE_BITS);
			bridge->mpb_cfg_smn_base = MILAN_SMN_PCIE_CFG_BASE +
			    shift;
			MILAN_SMN_VERIFY_BASE_ADDR(bridge->mpb_cfg_smn_base,
			    MILAN_SMN_PCIE_PORT_BASE_BITS);

			/*
			 * Each bridge has a range of control addresses that are
			 * hidden in the IOHC. The bridge offset is multiplied
			 * by the port number to get the absolute address in
			 * this space.
			 */
			bridge->mpb_iohc_smn_base = ioms->mio_iohc_smn_base +
			    MILAN_IOHC_R_SMN_PCIE_BASE +
			    MILAN_IOHC_R_SMN_BRIDGE_CNTL_BRIDGE_SHIFT(bridgeno +
			    pcino * 8);
			MILAN_SMN_VERIFY_BASE_ADDR(bridge->mpb_iohc_smn_base,
			    MILAN_SMN_IOHC_PCIE_BASE_BITS);
		}
	}
}

static void
milan_fabric_ioms_nbif_init(milan_ioms_t *ioms)
{
	for (uint_t nbifno = 0; nbifno < ioms->mio_nnbifs; nbifno++) {
		const milan_nbif_info_t *ninfo = NULL;
		milan_nbif_t *nbif = &ioms->mio_nbifs[nbifno];

		nbif->mn_nbifno = nbifno;
		VERIFY3U(nbifno, <, MILAN_IOMS_MAX_NBIF);
		switch (nbifno) {
		case 0:
			nbif->mn_nfuncs = MILAN_NBIF0_NFUNCS;
			ninfo = milan_nbif0;
			break;
		case 1:
			nbif->mn_nfuncs = MILAN_NBIF1_NFUNCS;
			ninfo = milan_nbif1;
			break;
		case 2:
			nbif->mn_nfuncs = MILAN_NBIF2_NFUNCS;
			ninfo = milan_nbif2;
			break;
		}

		nbif->mn_nbif_smn_base = MILAN_SMN_NBIF_BASE +
		    MILAN_SMN_NBIF_NBIF_SHIFT(nbif->mn_nbifno) +
		    MILAN_SMN_NBIF_IOMS_SHIFT(ioms->mio_num);
		MILAN_SMN_VERIFY_BASE_ADDR(nbif->mn_nbif_smn_base,
		    MILAN_SMN_NBIF_BASE_BITS);

		nbif->mn_nbif_alt_smn_base = MILAN_SMN_NBIF_ALT_BASE +
		    MILAN_SMN_NBIF_NBIF_SHIFT(nbif->mn_nbifno) +
		    MILAN_SMN_NBIF_IOMS_SHIFT(ioms->mio_num);
		MILAN_SMN_VERIFY_BASE_ADDR(nbif->mn_nbif_alt_smn_base,
		    MILAN_SMN_NBIF_ALT_BASE_BITS);

		for (uint_t funcno = 0; funcno < nbif->mn_nfuncs; funcno++) {
			milan_nbif_func_t *func = &nbif->mn_funcs[funcno];

			func->mne_type = ninfo[funcno].mni_type;
			func->mne_dev = ninfo[funcno].mni_dev;
			func->mne_func = ninfo[funcno].mni_func;
			func->mne_func_smn_base = nbif->mn_nbif_smn_base +
			    MILAN_SMN_NBIF_FUNC_OFF +
			    MILAN_SMN_NBIF_FUNC_SHIFT(func->mne_func) +
			    MILAN_SMN_NBIF_DEV_SHIFT(func->mne_dev);
			MILAN_SMN_VERIFY_BASE_ADDR(func->mne_func_smn_base,
			    MILAN_SMN_NBIF_FUNC_BASE_BITS);

			/*
			 * As there is a dummy device on each of these, this in
			 * theory doesn't need any explicit configuration.
			 */
			if (func->mne_type == MILAN_NBIF_T_DUMMY) {
				func->mne_flags |= MILAN_NBIF_F_NO_CONFIG;
			}
		}
	}
}

static void
milan_ccx_init_core(milan_ccx_t *ccx, uint8_t lidx, uint8_t pidx)
{
	uint32_t val;
	milan_core_t *core = &ccx->mcx_cores[lidx];
	milan_ccd_t *ccd = ccx->mcx_ccd;
	milan_iodie_t *iodie = ccd->mcd_iodie;

	core->mc_ccx = ccx;
	core->mc_scfctp_smn_base = ccx->mcx_scfctp_smn_base +
	    MILAN_SMN_SCFCTP_CORE_SHIFT(pidx);

	MILAN_SMN_VERIFY_BASE_ADDR(core->mc_scfctp_smn_base,
	    MILAN_SMN_SCFCTP_BASE_BITS);

	core->mc_physical_coreno = pidx;

	val = milan_scfctp_read32(iodie, core,
	    MILAN_SCFCTP_R_SMN_PMREG_INITPKG0);
	VERIFY3U(val, !=, 0xffffffffU);

	core->mc_logical_coreno =
	    MILAN_SCFCTP_R_GET_PMREG_INITPKG0_LOGICALCOREID(val);

	VERIFY3U(MILAN_SCFCTP_R_GET_PMREG_INITPKG0_PHYSICALCOREID(val), ==,
	    pidx);
	VERIFY3U(MILAN_SCFCTP_R_GET_PMREG_INITPKG0_PHYSICALCOMPLEXID(val), ==,
	    ccx->mcx_physical_cxno);
	VERIFY3U(MILAN_SCFCTP_R_GET_PMREG_INITPKG0_PHYSICALDIEID(val), ==,
	    ccx->mcx_ccd->mcd_physical_dieno);

	core->mc_nthreads = MILAN_SCFCTP_R_GET_PMREG_INITPKG0_SMTEN(val) + 1;
	VERIFY3U(core->mc_nthreads, <=, MILAN_MAX_THREADS_PER_CORE);

	for (uint8_t thr = 0; thr < core->mc_nthreads; thr++) {
		uint32_t apicid = 0;
		milan_thread_t *thread = &core->mc_threads[thr];

		thread->mt_threadno = thr;
		thread->mt_core = core;
		nthreads++;

		/*
		 * You may be wondering why we don't use the contents of
		 * DF::CcdUnitIdMask here to determine the number of bits at
		 * each level.  There are two reasons, one simple and one not:
		 *
		 * - First, it's not correct.  The UnitId masks describe (*)
		 *   the physical ID spaces, which are distinct from how APIC
		 *   IDs are computed.  APIC IDs depend on the number of each
		 *   component that are *actually present*, rounded up to the
		 *   next power of 2 at each component.  For example, if there
		 *   are 4 CCDs, there will be 2 bits in the APIC ID for the
		 *   logical CCD number, even though representing the UnitId
		 *   on Milan requires 3 bits for the CCD.  No, we don't know
		 *   why this is so; it would certainly have been simpler to
		 *   always use the physical ID to compute the initial APIC ID.
		 * - Second, not only are APIC IDs not UnitIds, there is nothing
		 *   documented that does consume UnitIds.  We are given a nice
		 *   discussion of what they are and this lovingly detailed way
		 *   to discover how to compute them, but so far as I have been
		 *   able to tell, neither UnitIds nor the closely related
		 *   CpuIds are ever used.  If we later find that we do need
		 *   these identifiers, additional code to construct them based
		 *   on this discovery mechanism should be added.
		 */
		apicid = iodie->mi_soc->ms_socno;
		apicid <<= highbit(iodie->mi_soc->ms_ndies - 1);
		apicid |= 0;	/* XXX multi-die SOCs not supported here */
		apicid <<= highbit(iodie->mi_nccds - 1);
		apicid |= ccd->mcd_logical_dieno;
		apicid <<= highbit(ccd->mcd_nccxs - 1);
		apicid |= ccx->mcx_logical_cxno;
		apicid <<= highbit(ccx->mcx_ncores - 1);
		apicid |= core->mc_logical_coreno;
		apicid <<= highbit(core->mc_nthreads - 1);
		apicid |= thr;

		thread->mt_apicid = (apicid_t)apicid;
	}
}

static void
milan_ccx_init_soc(milan_soc_t *soc)
{
	const milan_fabric_t *fabric = soc->ms_fabric;
	milan_iodie_t *iodie = &soc->ms_iodies[0];

	/*
	 * We iterate over the physical CCD space; population of that
	 * space may be sparse.  Keep track of the logical CCD index in
	 * lccd; ccdpno is the physical CCD index we're considering.
	 */
	for (uint8_t ccdpno = 0, lccd = 0;
	    ccdpno < MILAN_MAX_CCDS_PER_IODIE; ccdpno++) {
		uint8_t core_shift, pcore, lcore;
		uint32_t val;
		uint32_t cores_enabled;
		milan_ccd_t *ccd = &iodie->mi_ccds[lccd];
		milan_ccx_t *ccx = &ccd->mcd_ccxs[0];

		/*
		 * The CCM is part of the IO die, not the CCD itself.
		 * If it is disabled, we skip this CCD index as even if
		 * it exists nothing can reach it.
		 */
		val = milan_df_read32(iodie,
		    MILAN_DF_FIRST_CCM_ID + ccdpno, 0, AMDZEN_DF_F0_FBIINFO0);

		VERIFY3U(AMDZEN_DF_F0_FBIINFO0_TYPE(val), ==,
		    AMDZEN_DF_TYPE_CCM);
		if (AMDZEN_DF_F0_FBIINFO0_ENABLED(val) == 0)
			continue;

		/*
		 * At leaast some of the time, a CCM will be enabled
		 * even if there is no corresponding CCD.  To avoid
		 * a possibly invalid read (see milan_fabric_topo_init()
		 * comments), we also check whether any core is enabled
		 * on this CCD.
		 *
		 * XXX reduce magic
		 */
		val = milan_df_bcast_read32(iodie, 1,
		    (ccdpno < 4) ? AMDZEN_DF_F1_PHYSICAL_CORE_ENABLE0 :
		    AMDZEN_DF_F1_PHYSICAL_CORE_ENABLE1);
		core_shift = (ccdpno & 3) * MILAN_MAX_CORES_PER_CCX *
		    MILAN_MAX_CCXS_PER_CCD;
		cores_enabled = bitx32(val, core_shift + 7, core_shift);

		if (cores_enabled == 0)
			continue;

		VERIFY3U(lccd, <, MILAN_MAX_CCDS_PER_IODIE);
		ccd->mcd_iodie = iodie;
		ccd->mcd_logical_dieno = lccd++;
		ccd->mcd_physical_dieno = ccdpno;
		ccd->mcd_ccm_comp_id = MILAN_DF_FIRST_CCM_ID + ccdpno;
		/*
		 * XXX Non-Milan may require nonzero component ID shift.
		 */
		ccd->mcd_ccm_fabric_id = ccd->mcd_ccm_comp_id |
		    (iodie->mi_node_id << fabric->mf_node_shift);
		ccd->mcd_smupwr_smn_base = MILAN_SMN_SMUPWR_BASE +
		    MILAN_SMN_SMUPWR_CCD_SHIFT(ccdpno);

		MILAN_SMN_VERIFY_BASE_ADDR(ccd->mcd_smupwr_smn_base,
		    MILAN_SMN_SMUPWR_BASE_BITS);

		/* XXX avoid panicking on bad data from firmware */
		val = milan_smupwr_read32(ccd, MILAN_SMUPWR_R_SMN_CCD_DIE_ID);
		VERIFY3U(val, ==, ccdpno);

		val = milan_smupwr_read32(ccd,
		    MILAN_SMUPWR_R_SMN_THREAD_CONFIGURATION);
		ccd->mcd_nccxs =
		    MILAN_SMUPWR_R_GET_THREAD_CONFIGURATION_COMPLEX_COUNT(val) +
		    1;
		VERIFY3U(ccd->mcd_nccxs, <=, MILAN_MAX_CCXS_PER_CCD);

		if (ccd->mcd_nccxs == 0) {
			cmn_err(CE_NOTE, "CCD 0x%x: no CCXs reported",
			    ccd->mcd_physical_dieno);
			continue;
		}

		/*
		 * Make sure that the CCD's local understanding of
		 * enabled cores matches what we found earlier through
		 * the DF.  A mismatch here is a firmware bug; XXX and
		 * if that happens?
		 */
		val = milan_smupwr_read32(ccd, MILAN_SMUPWR_R_SMN_CORE_ENABLE);
		VERIFY3U(MILAN_SMUPWR_R_GET_CORE_ENABLE_COREEN(val), ==,
		    cores_enabled);

		/*
		 * XXX While we know there is only ever 1 CCX per Milan CCD,
		 * DF::CCXEnable allows for 2 because the DFv3 implementation
		 * is shared with Rome, which has up to 2 CCXs per CCD.
		 * Although we know we only ever have 1 CCX, we don't,
		 * strictly, know that the CCX is always physical index 0.
		 * Here we assume it, but we probably want to change the
		 * MILAN_MAX_xxx_PER_yyy so that they reflect the size of the
		 * physical ID spaces rather than the maximum logical entity
		 * counts.  Doing so would accommodate a part that has a single
		 * CCX per CCD, but at index 1.
		 */
		ccx->mcx_ccd = ccd;
		ccx->mcx_logical_cxno = 0;
		ccx->mcx_physical_cxno = 0;
		ccx->mcx_scfctp_smn_base = MILAN_SMN_SCFCTP_BASE +
		    MILAN_SMN_SCFCTP_CCD_SHIFT(ccdpno);

		MILAN_SMN_VERIFY_BASE_ADDR(ccx->mcx_scfctp_smn_base,
		    MILAN_SMN_SCFCTP_BASE_BITS);

		/*
		 * All the cores on the CCD will (should) return the
		 * same values in PMREG_INITPKG0 and PMREG_INITPKG7.
		 * The catch is that we have to read them from a core
		 * that exists or we get all-1s.  Use the mask of
		 * cores enabled on this die that we already computed
		 * to find one to read from, then bootstrap into the
		 * core enumeration.  XXX At some point we probably
		 * should do away with all this cross-checking and
		 * choose something to trust.
		 */
		for (pcore = 0;
		    (cores_enabled & (1 << pcore)) == 0 &&
		    pcore < MILAN_MAX_CORES_PER_CCX; pcore++)
			;
		VERIFY3U(pcore, <, MILAN_MAX_CORES_PER_CCX);
		val = milan_smn_read32(iodie,
		    MILAN_SMN_SCFCTP_MAKE_ADDR(ccx->mcx_scfctp_smn_base +
		    MILAN_SMN_SCFCTP_CORE_SHIFT(pcore),
		    MILAN_SCFCTP_R_SMN_PMREG_INITPKG7));

		VERIFY3U(val, !=, 0xffffffffU);
		ccx->mcx_ncores =
		    MILAN_SCFCTP_R_GET_PMREG_INITPKG7_NUMOFLOGICALCORES(val) +
		    1;

		iodie->mi_nccds =
		    MILAN_SCFCTP_R_GET_PMREG_INITPKG7_NUMOFLOGICALDIE(val) + 1;

		for (pcore = 0, lcore = 0;
		    pcore < MILAN_MAX_CORES_PER_CCX; pcore++) {
			if ((cores_enabled & (1 << pcore)) == 0)
				continue;
			milan_ccx_init_core(ccx, lcore, pcore);
			++lcore;
		}

		VERIFY3U(lcore, ==, ccx->mcx_ncores);
	}
}

/*
 * Right now we're running on the boot CPU. We know that a single socket has to
 * be populated. Our job is to go through and determine what the rest of the
 * topology of this system looks like in terms of the data fabric, north
 * bridges, and related. We can rely on the DF instance 0/18/0 to exist;
 * however, that's it.
 *
 * An important rule of discovery here is that we should not rely on invalid PCI
 * reads. We should be able to bootstrap from known good data and what the
 * actual SoC has discovered here rather than trying to fill that in ourselves.
 */
void
milan_fabric_topo_init(void)
{
	uint8_t nsocs;
	uint32_t syscfg, syscomp, fidmask;
	milan_fabric_t *fabric = &milan_fabric;

	PRM_POINT("milan_fabric_topo_init() starting...");

	syscfg = pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE, 1,
	    AMDZEN_DF_F1_SYSCFG);
	syscomp = pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE, 1,
	    AMDZEN_DF_F1_SYSCOMP);
	nsocs = AMDZEN_DF_F1_SYSCFG_OTHERSOCK(syscfg) + 1;

	/*
	 * These are used to ensure that we're on a platform that matches our
	 * expectations. These are generally constraints of Rome and Milan.
	 */
	VERIFY3U(nsocs, ==, AMDZEN_DF_F1_SYSCOMP_PIE(syscomp));
	VERIFY3U(nsocs * MILAN_IOMS_PER_IODIE, ==,
	    AMDZEN_DF_F1_SYSCOMP_IOMS(syscomp));

	fabric->mf_tom = MSR_AMD_TOM_MASK(rdmsr(MSR_AMD_TOM));
	fabric->mf_tom2 = MSR_AMD_TOM_MASK(rdmsr(MSR_AMD_TOM2));

	/*
	 * Set up the base of 64-bit MMIO. The actual starting point depends on
	 * the combination of where DRAM ends and where the mysterious hole
	 * ends. As a result, we always start things at the higher of the two.
	 */
	fabric->mf_mmio64_base = MAX(fabric->mf_tom2,
	    MILAN_PHYSADDR_MYSTERY_HOLE_END);

	/*
	 * Gather the register masks for decoding global fabric IDs into local
	 * instance IDs.
	 */
	fidmask = pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE, 1,
	    AMDZEN_DF_F1_FIDMASK0);

	fabric->mf_node_mask = AMDZEN_DF_F1_FIDMASK0_NODE_MASK(fidmask);
	fabric->mf_comp_mask = AMDZEN_DF_F1_FIDMASK0_COMP_MASK(fidmask);

	fidmask = pci_getl_func(AMDZEN_DF_BUSNO, AMDZEN_DF_FIRST_DEVICE, 1,
	    AMDZEN_DF_F1_FIDMASK1);
	fabric->mf_node_shift = AMDZEN_DF_F1_FIDMASK1_NODE_SHIFT(fidmask);

	fabric->mf_nsocs = nsocs;
	for (uint8_t socno = 0; socno < nsocs; socno++) {
		uint32_t busno, nodeid;
		milan_soc_t *soc = &fabric->mf_socs[socno];
		milan_iodie_t *iodie = &soc->ms_iodies[0];

		soc->ms_socno = socno;
		soc->ms_ndies = MILAN_FABRIC_MAX_DIES_PER_SOC;
		soc->ms_fabric = fabric;
		iodie->mi_dfno = AMDZEN_DF_FIRST_DEVICE + socno;

		nodeid = pci_getl_func(AMDZEN_DF_BUSNO, iodie->mi_dfno, 1,
		    AMDZEN_DF_F1_SYSCFG);
		iodie->mi_node_id = AMDZEN_DF_F1_SYSCFG_NODEID(nodeid);
		iodie->mi_soc = soc;

		/*
		 * XXX Because we do not know the circumstances all these locks
		 * will be used during early initialization, set these to be
		 * spin locks for the moment.
		 */
		mutex_init(&iodie->mi_df_ficaa_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->mi_smn_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->mi_smu_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));
		mutex_init(&iodie->mi_pcie_strap_lock, NULL, MUTEX_SPIN,
		    (ddi_iblock_cookie_t)ipltospl(15));

		busno = milan_df_bcast_read32(iodie, 0,
		    AMDZEN_DF_F0_CFG_ADDR_CTL);
		iodie->mi_smn_busno = AMDZEN_DF_F0_CFG_ADDR_CTL_BUS_NUM(busno);

		iodie->mi_nioms = MILAN_IOMS_PER_IODIE;
		fabric->mf_total_ioms += iodie->mi_nioms;
		for (uint8_t iomsno = 0; iomsno < iodie->mi_nioms; iomsno++) {
			uint32_t val;

			milan_ioms_t *ioms = &iodie->mi_ioms[iomsno];

			ioms->mio_num = iomsno;
			ioms->mio_comp_id = MILAN_DF_FIRST_IOMS_ID + iomsno;
			ioms->mio_fabric_id = ioms->mio_comp_id |
			    (iodie->mi_node_id << fabric->mf_node_shift);

			val = milan_df_read32(iodie, ioms->mio_comp_id, 0,
			    AMDZEN_DF_F0_CFG_ADDR_CTL);
			ioms->mio_pci_busno =
			    AMDZEN_DF_F0_CFG_ADDR_CTL_BUS_NUM(val);


			/*
			 * Only IOMS 0 has a WAFL port.
			 */
			if (iomsno == 0) {
				ioms->mio_npcie_ports =
				    MILAN_IOMS_MAX_PCIE_PORTS;
				ioms->mio_flags |= MILAN_IOMS_F_HAS_WAFL;
			} else {
				ioms->mio_npcie_ports =
				    MILAN_IOMS_MAX_PCIE_PORTS - 1;
			}
			ioms->mio_nnbifs = MILAN_IOMS_MAX_NBIF;

			if (iomsno == MILAN_IOMS_HAS_FCH) {
				ioms->mio_flags |= MILAN_IOMS_F_HAS_FCH;
			}

			ioms->mio_iohc_smn_base = MILAN_SMN_IOHC_BASE +
			    MILAN_SMN_IOMS_SHIFT(iomsno);
			MILAN_SMN_VERIFY_BASE_ADDR(ioms->mio_iohc_smn_base,
			    MILAN_SMN_IOHC_BASE_BITS);

			ioms->mio_ioagr_smn_base = MILAN_SMN_IOAGR_BASE +
			    MILAN_SMN_IOMS_SHIFT(iomsno);
			MILAN_SMN_VERIFY_BASE_ADDR(ioms->mio_ioagr_smn_base,
			    MILAN_SMN_IOAGR_BASE_BITS);

			ioms->mio_ioapic_smn_base = MILAN_SMN_IOAPIC_BASE +
			    MILAN_SMN_IOMS_SHIFT(iomsno);
			MILAN_SMN_VERIFY_BASE_ADDR(ioms->mio_ioapic_smn_base,
			    MILAN_SMN_IOAPIC_BASE_BITS);

			ioms->mio_iommul1_smn_base = MILAN_SMN_IOMMUL1_BASE +
			    MILAN_SMN_IOMS_SHIFT(iomsno);
			MILAN_SMN_VERIFY_BASE_ADDR(ioms->mio_iommul1_smn_base,
			    MILAN_SMN_IOMMUL1_BASE_BITS);

			ioms->mio_iommul2_smn_base = MILAN_SMN_IOMMUL2_BASE +
			    MILAN_SMN_IOMS_SHIFT(iomsno);
			MILAN_SMN_VERIFY_BASE_ADDR(ioms->mio_iommul2_smn_base,
			    MILAN_SMN_IOMMUL2_BASE_BITS);

			/*
			 * SDPMUX SMN base addresses are confusingly different
			 * and inconsistent. IOMS0 uses a different scheme for
			 * the others.
			 */
			ioms->mio_sdpmux_smn_base = MILAN_SMN_SDPMUX_BASE;
			if (iomsno > 0) {
				ioms->mio_sdpmux_smn_base +=
				    MILAN_SMN_SDPMUX_IOMS_SHIFT(iomsno);
			}
			MILAN_SMN_VERIFY_BASE_ADDR(ioms->mio_sdpmux_smn_base,
			    MILAN_SMN_SDPMUX_BASE_BITS);

			milan_fabric_ioms_pcie_init(ioms);
			milan_fabric_ioms_nbif_init(ioms);
		}

		milan_ccx_init_soc(soc);
		if (!milan_smu_rpc_read_brand_string(iodie, soc->ms_brandstr,
		    sizeof (soc->ms_brandstr))) {
			soc->ms_brandstr[0] = '\0';
		}
	}

	if (nthreads > NCPU) {
		cmn_err(CE_WARN, "%d CPUs found but only %d supported",
		    nthreads, NCPU);
		nthreads = NCPU;
	}
	boot_max_ncpus = max_ncpus = boot_ncpus = nthreads;
}

/*
 * Create DMA attributes that are appropriate for the SMU. In particular, we
 * know experimentally that there is usually a 32-bit length register for DMA
 * and generally a 64-bit address register. There aren't many other bits that we
 * actually know here, as such, we generally end up making some assumptions out
 * of paranoia in an attempt at safety. In particular, we assume and ask for
 * page alignment here.
 *
 * XXX Remove 32-bit addr_hi constraint.
 */
static void
milan_smu_dma_attr(ddi_dma_attr_t *attr)
{
	bzero(attr, sizeof (attr));
	attr->dma_attr_version = DMA_ATTR_V0;
	attr->dma_attr_addr_lo = 0;
	attr->dma_attr_addr_hi = UINT32_MAX;
	attr->dma_attr_count_max = UINT32_MAX;
	attr->dma_attr_align = MMU_PAGESIZE;
	attr->dma_attr_minxfer = 1;
	attr->dma_attr_maxxfer = UINT32_MAX;
	attr->dma_attr_seg = UINT32_MAX;
	attr->dma_attr_sgllen = 1;
	attr->dma_attr_granular = 1;
	attr->dma_attr_flags = 0;
}

static void
milan_smu_rpc(milan_iodie_t *iodie, milan_smu_rpc_t *rpc)
{
	uint32_t resp;

	mutex_enter(&iodie->mi_smu_lock);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_RESP, MILAN_SMU_RPC_NOTDONE);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_ARG0, rpc->msr_arg0);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_ARG1, rpc->msr_arg1);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_ARG2, rpc->msr_arg2);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_ARG3, rpc->msr_arg3);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_ARG4, rpc->msr_arg4);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_ARG5, rpc->msr_arg5);
	milan_smn_write32(iodie, MILAN_SMU_SMN_RPC_REQ, rpc->msr_req);

	/*
	 * XXX Infinite spins are bad, but we don't even have drv_usecwait yet.
	 * When we add a timeout this should then return an int.
	 */
	for (;;) {
		resp = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_RESP);
		if (resp != MILAN_SMU_RPC_NOTDONE) {
			break;
		}
	}

	rpc->msr_resp = resp;
	if (rpc->msr_resp == MILAN_SMU_RPC_OK) {
		rpc->msr_arg0 = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_ARG0);
		rpc->msr_arg1 = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_ARG1);
		rpc->msr_arg2 = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_ARG2);
		rpc->msr_arg3 = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_ARG3);
		rpc->msr_arg4 = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_ARG4);
		rpc->msr_arg5 = milan_smn_read32(iodie, MILAN_SMU_SMN_RPC_ARG5);
	}
	mutex_exit(&iodie->mi_smu_lock);
}

static boolean_t
milan_smu_rpc_get_version(milan_iodie_t *iodie, uint8_t *major, uint8_t *minor,
    uint8_t *patch)
{
	milan_smu_rpc_t rpc = { 0 };

	rpc.msr_req = MILAN_SMU_OP_GET_VERSION;
	milan_smu_rpc(iodie, &rpc);
	if (rpc.msr_resp != MILAN_SMU_RPC_OK) {
		return (B_FALSE);
	}

	*major = MILAN_SMU_OP_GET_VERSION_MAJOR(rpc.msr_arg0);
	*minor = MILAN_SMU_OP_GET_VERSION_MINOR(rpc.msr_arg0);
	*patch = MILAN_SMU_OP_GET_VERSION_PATCH(rpc.msr_arg0);

	return (B_TRUE);
}

static boolean_t
milan_smu_rpc_i2c_switch(milan_iodie_t *iodie, uint32_t addr)
{
	milan_smu_rpc_t rpc = { 0 };

	rpc.msr_req = MILAN_SMU_OP_I2C_SWITCH_ADDR;
	rpc.msr_arg0 = addr;
	milan_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != MILAN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Set i2c address RPC Failed: addr: 0x%x, "
		    "SMU 0x%x", addr, rpc.msr_resp);
	}

	return (rpc.msr_resp == MILAN_SMU_RPC_OK);
}

static boolean_t
milan_smu_rpc_give_address(milan_iodie_t *iodie, uint64_t addr)
{
	milan_smu_rpc_t rpc = { 0 };

	rpc.msr_req = MILAN_SMU_OP_HAVE_AN_ADDRESS;
	rpc.msr_arg0 = bitx64(addr, 31, 0);
	rpc.msr_arg1 = bitx64(addr, 63, 32);
	milan_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != MILAN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Have an Address RPC Failed: addr: 0x%lx, "
		    "SMU 0x%x", addr, rpc.msr_resp);
	}

	return (rpc.msr_resp == MILAN_SMU_RPC_OK);

}

static boolean_t
milan_smu_rpc_send_hotplug_table(milan_iodie_t *iodie)
{
	milan_smu_rpc_t rpc = { 0 };

	rpc.msr_req = MILAN_SMU_OP_TX_PCIE_HP_TABLE;
	milan_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != MILAN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU TX Hotplug Table Failed: SMU 0x%x",
		    rpc.msr_resp);
	}

	return (rpc.msr_resp == MILAN_SMU_RPC_OK);
}

static boolean_t
milan_smu_rpc_hotplug_flags(milan_iodie_t *iodie, uint32_t flags)
{
	milan_smu_rpc_t rpc = { 0 };

	rpc.msr_req = MILAN_SMU_OP_SET_HOPTLUG_FLAGS;
	rpc.msr_arg0 = flags;
	milan_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != MILAN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Set Hotplug Flags failed: SMU 0x%x",
		    rpc.msr_resp);
	}

	return (rpc.msr_resp == MILAN_SMU_RPC_OK);
}
static boolean_t
milan_smu_rpc_start_hotplug(milan_iodie_t *iodie, boolean_t one_based,
    uint8_t flags)
{
	milan_smu_rpc_t rpc = { 0 };

	rpc.msr_req = MILAN_SMU_OP_START_HOTPLUG;
	if (one_based) {
		rpc.msr_arg0 = 1;
	}
	rpc.msr_arg0 |= flags;
	milan_smu_rpc(iodie, &rpc);

	if (rpc.msr_resp != MILAN_SMU_RPC_OK) {
		cmn_err(CE_WARN, "SMU Start Yer Hotplug Failed: SMU 0x%x",
		    rpc.msr_resp);
	}

	return (rpc.msr_resp == MILAN_SMU_RPC_OK);
}

/*
 * buf and len semantics here match those of snprintf
 */
static boolean_t
milan_smu_rpc_read_brand_string(milan_iodie_t *iodie, char *buf, size_t len)
{
	milan_smu_rpc_t rpc = { 0 };
	uint_t off;

	len = MIN(len, CPUID_BRANDSTR_STRLEN + 1);
	buf[len - 1] = '\0';
	rpc.msr_req = MILAN_SMU_OP_GET_BRAND_STRING;

	for (off = 0; off * 4 < len - 1; off++) {
		rpc.msr_arg0 = off;
		milan_smu_rpc(iodie, &rpc);

		if (rpc.msr_resp != MILAN_SMU_RPC_OK)
			return (B_FALSE);

		bcopy(&rpc.msr_arg0, buf + off * 4, len - off * 4);
	}

	return (B_TRUE);
}

static void
milan_dxio_rpc(milan_iodie_t *iodie, milan_dxio_rpc_t *dxio_rpc)
{
	milan_smu_rpc_t smu_rpc = { 0 };

	smu_rpc.msr_req = MILAN_SMU_OP_DXIO;
	smu_rpc.msr_arg0 = dxio_rpc->mdr_req;
	smu_rpc.msr_arg1 = dxio_rpc->mdr_engine;
	smu_rpc.msr_arg2 = dxio_rpc->mdr_arg0;
	smu_rpc.msr_arg3 = dxio_rpc->mdr_arg1;
	smu_rpc.msr_arg4 = dxio_rpc->mdr_arg2;
	smu_rpc.msr_arg5 = dxio_rpc->mdr_arg3;

	milan_smu_rpc(iodie, &smu_rpc);

	dxio_rpc->mdr_smu_resp = smu_rpc.msr_resp;
	if (smu_rpc.msr_resp == MILAN_SMU_RPC_OK) {
		dxio_rpc->mdr_dxio_resp = smu_rpc.msr_arg0;
		dxio_rpc->mdr_engine = smu_rpc.msr_arg1;
		dxio_rpc->mdr_arg0 = smu_rpc.msr_arg2;
		dxio_rpc->mdr_arg1 = smu_rpc.msr_arg3;
		dxio_rpc->mdr_arg2 = smu_rpc.msr_arg4;
		dxio_rpc->mdr_arg3 = smu_rpc.msr_arg5;
	}
}

static boolean_t
milan_dxio_rpc_get_version(milan_iodie_t *iodie, uint32_t *major,
    uint32_t *minor)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_GET_VERSION;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Get Version RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x", rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	*major = rpc.mdr_arg0;
	*minor = rpc.mdr_arg1;

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_init(milan_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_INIT;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Init RPC Failed: SMU 0x%x, DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_set_var(milan_iodie_t *iodie, uint32_t var, uint32_t val)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_VARIABLE;
	rpc.mdr_engine = var;
	rpc.mdr_arg0 = val;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == MILAN_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp == MILAN_DXIO_RPC_MBOX_IDLE)) {
		cmn_err(CE_WARN, "DXIO Set Variable Failed: Var: 0x%x, "
		    "Val: 0x%x, SMU 0x%x, DXIO: 0x%x", var, val,
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_pcie_poweroff_config(milan_iodie_t *iodie, uint8_t delay,
    boolean_t disable_prep)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_VARIABLE;
	rpc.mdr_engine = MILAN_DXIO_VAR_PCIE_POWER_OFF_DELAY;
	rpc.mdr_arg0 = delay;
	rpc.mdr_arg1 = disable_prep ? 1 : 0;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == MILAN_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp == MILAN_DXIO_RPC_MBOX_IDLE)) {
		cmn_err(CE_WARN, "DXIO Set PCIe Power Off Config Failed: "
		    "Delay: 0x%x, Disable Prep: 0x%x, SMU 0x%x, DXIO: 0x%x",
		    delay, disable_prep, rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_clock_gating(milan_iodie_t *iodie, uint8_t mask, uint8_t val)
{
	milan_dxio_rpc_t rpc = { 0 };

	/*
	 * The mask and val are only allowed to be 7-bit values.
	 */
	VERIFY0(mask & 0x80);
	VERIFY0(val & 0x80);
	rpc.mdr_req = MILAN_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = MILAN_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = MILAN_DXIO_RT_CONF_CLOCK_GATE;
	rpc.mdr_arg1 = mask;
	rpc.mdr_arg2 = val;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Clock Gating Failed: SMU 0x%x, "
		    "DXIO: 0x%x", rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Currently there are no capabilities defined, which makes it hard for us to
 * know the exact command layout here. The only thing we know is safe is that
 * it's all zeros, though it probably otherwise will look like
 * MILAN_DXIO_OP_LOAD_DATA.
 */
static boolean_t
milan_dxio_rpc_load_caps(milan_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_LOAD_CAPS;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Load Caps Failed: SMU 0x%x, DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_load_data(milan_iodie_t *iodie, uint32_t type,
    uint64_t phys_addr, uint32_t len, uint32_t mystery)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_LOAD_DATA;
	rpc.mdr_engine = (uint32_t)(phys_addr >> 32);
	rpc.mdr_arg0 = phys_addr & 0xffffffff;
	rpc.mdr_arg1 = len / 4;
	rpc.mdr_arg2 = mystery;
	rpc.mdr_arg3 = type;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Load Data Failed: Heap: 0x%x, PA: "
		    "0x%lx, Len: 0x%x, SMU 0x%x, DXIO: 0x%x", type, phys_addr,
		    len, rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_conf_training(milan_iodie_t *iodie, uint32_t reset_time,
    uint32_t rx_poll, uint32_t l0_poll)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = MILAN_DXIO_ENGINE_PCIE;
	rpc.mdr_arg0 = MILAN_DXIO_RT_CONF_PCIE_TRAIN;
	rpc.mdr_arg1 = reset_time;
	rpc.mdr_arg2 = rx_poll;
	rpc.mdr_arg3 = l0_poll;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == MILAN_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK)) {
		cmn_err(CE_WARN, "DXIO Conf. PCIe Training RPC Failed: "
		    "SMU 0x%x, DXIO: 0x%x", rpc.mdr_smu_resp,
		    rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * This is a hodgepodge RPC that is used to set various rt configuration
 * properties.
 */
static boolean_t
milan_dxio_rpc_misc_rt_conf(milan_iodie_t *iodie, uint32_t code,
    boolean_t state)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_SET_RUNTIME_PROP;
	rpc.mdr_engine = MILAN_DXIO_ENGINE_NONE;
	rpc.mdr_arg0 = MILAN_DXIO_RT_SET_CONF;
	rpc.mdr_arg1 = code;
	rpc.mdr_arg2 = state ? 1 : 0;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    !(rpc.mdr_dxio_resp == MILAN_DXIO_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK)) {
		cmn_err(CE_WARN, "DXIO Set Misc. rt conf failed: Code: 0x%x, "
		    "Val: 0x%x, SMU 0x%x, DXIO: 0x%x", code, state,
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_sm_start(milan_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_START_SM;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_sm_resume(milan_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_RESUME_SM;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
milan_dxio_rpc_sm_reload(milan_iodie_t *iodie)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_RELOAD_SM;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Reload RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}


static boolean_t
milan_dxio_rpc_sm_getstate(milan_iodie_t *iodie, milan_dxio_reply_t *smp)
{
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_GET_SM_STATE;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO SM Start RPC Failed: SMU 0x%x, "
		    "DXIO: 0x%x",
		    rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	smp->mds_type = bitx64(rpc.mdr_engine, 7, 0);
	smp->mds_nargs = bitx64(rpc.mdr_engine, 16, 8);
	smp->mds_arg0 = rpc.mdr_arg0;
	smp->mds_arg1 = rpc.mdr_arg1;
	smp->mds_arg2 = rpc.mdr_arg2;
	smp->mds_arg3 = rpc.mdr_arg3;

	return (B_TRUE);
}

/*
 * Retrieve the current engine data from DXIO.
 */
static boolean_t
milan_dxio_rpc_retrieve_engine(milan_iodie_t *iodie)
{
	milan_dxio_config_t *conf = &iodie->mi_dxio_conf;
	milan_dxio_rpc_t rpc = { 0 };

	rpc.mdr_req = MILAN_DXIO_OP_GET_ENGINE_CFG;
	rpc.mdr_engine = (uint32_t)(conf->mdc_pa >> 32);
	rpc.mdr_arg0 = conf->mdc_pa & 0xffffffff;
	rpc.mdr_arg1 = conf->mdc_alloc_len / 4;

	milan_dxio_rpc(iodie, &rpc);
	if (rpc.mdr_smu_resp != MILAN_SMU_RPC_OK ||
	    rpc.mdr_dxio_resp != MILAN_DXIO_RPC_OK) {
		cmn_err(CE_WARN, "DXIO Retrieve Engine Failed: SMU 0x%x, "
		    "DXIO: 0x%x", rpc.mdr_smu_resp, rpc.mdr_dxio_resp);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static int
milan_dump_versions(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	uint8_t maj, min, patch;
	uint32_t dxmaj, dxmin;

	if (milan_smu_rpc_get_version(iodie, &maj, &min, &patch)) {
		cmn_err(CE_NOTE, "Socket %u SMU Version: %u.%u.%u",
		    soc->ms_socno, maj, min, patch);
		iodie->mi_smu_fw[0] = maj;
		iodie->mi_smu_fw[1] = min;
		iodie->mi_smu_fw[2] = patch;
	} else {
		cmn_err(CE_NOTE, "Socket %u: failed to read SMU version",
		    soc->ms_socno);
	}

	if (milan_dxio_rpc_get_version(iodie, &dxmaj, &dxmin)) {
		cmn_err(CE_NOTE, "Socket %u DXIO Version: %u.%u",
		    soc->ms_socno, dxmaj, dxmin);
		iodie->mi_dxio_fw[0] = dxmaj;
		iodie->mi_dxio_fw[1] = dxmin;
	} else {
		cmn_err(CE_NOTE, "Socket %u: failed to read DXIO version",
		    soc->ms_socno);
	}

	return (0);
}

/*
 * The IOHC needs our help to know where the top of memory is. This is
 * complicated for a few reasons. Right now we're relying on where TOM and TOM2
 * have been programmed by the PSP to determine that. The biggest gotcha here is
 * the secondary MMIO hole that leads to us needing to actually have a 3rd
 * register in the IOHC for indicating DRAM/MMIO splits.
 */
static int
milan_fabric_init_tom(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;
	uint64_t tom2, tom3;

	/*
	 * This register is a little funky. Bit 32 of the address has to be
	 * specified in bit 0. Otherwise, bits 31:23 are the limit.
	 */
	val = pci_getl_func(ioms->mio_pci_busno, 0, 0,
	    MILAN_IOHC_R_PCI_NB_TOP_OF_DRAM);
	if (bitx64(fabric->mf_tom, 32, 32) != 0) {
		val = MILAN_IOHC_R_SET_NB_TOP_OF_DRAM_BIT32(val, 1);
	}

	val = MILAN_IOHC_R_SET_NB_TOP_OF_DRAM(val,
	    bitx64(fabric->mf_tom, 31, 23));
	pci_putl_func(ioms->mio_pci_busno, 0, 0,
	    MILAN_IOHC_R_PCI_NB_TOP_OF_DRAM, val);

	if (fabric->mf_tom2 == 0) {
		return (0);
	}

	if (fabric->mf_tom2 > MILAN_PHYSADDR_MYSTERY_HOLE_END) {
		tom2 = MILAN_PHYSADDR_MYSTERY_HOLE;
		tom3 = fabric->mf_tom2 - 1;
	} else {
		tom2 = fabric->mf_tom2;
		tom3 = 0;
	}

	/*
	 * Write the upper register before the lower so we don't accidentally
	 * enable it in an incomplete fashion.
	 */
	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_DRAM_TOM2_HI);
	val = MILAN_IOHC_R_SET_DRAM_TOM2_HI_TOM2(val, bitx64(tom2, 40, 32));
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_DRAM_TOM2_HI, val);

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_DRAM_TOM2_LOW);
	val = MILAN_IOHC_R_SET_DRAM_TOM2_LOW_EN(val, 1);
	val = MILAN_IOHC_R_SET_DRAM_TOM2_LOW_TOM2(val, bitx64(tom2, 31, 23));
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_DRAM_TOM2_LOW, val);

	if (tom3 == 0) {
		return (0);
	}

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_DRAM_TOM3);
	val = MILAN_IOHC_R_SET_DRAM_TOM3_EN(val, 1);
	val = MILAN_IOHC_R_SET_DRAM_TOM3_LIMIT(val, bitx64(tom3, 51, 22));
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_DRAM_TOM3, val);

	return (0);
}

/*
 * Different parts of the IOMS need to be programmed such that they can figure
 * out if they have a corresponding FCH present on them. The FCH is only present
 * on IOMS 3. Therefore if we're on IOMS 3 we need to update various other bis
 * of the IOAGR and related; however, if we're not on IOMS 3 then we just need
 * to zero out some of this.
 */
static int
milan_fabric_init_iohc_fch_link(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	if ((ioms->mio_flags & MILAN_IOMS_F_HAS_FCH) != 0) {
		uint32_t val;

		val = milan_iohc_read32(iodie, ioms,
		    MILAN_IOHC_R_SMN_SB_LOCATION);
		milan_iommul1_write32(iodie, ioms, IOMMU_L1_IOAGR,
		    MILAN_IOMMUL1_R_SMN_SB_LOCATION, val);
		milan_iommul2_write32(iodie, ioms,
		    MILAN_IOMMUL2_R_SMN_SB_LOCATION, val);
	} else {
		milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_SB_LOCATION,
		    0);
	}

	return (0);
}

/*
 * For some reason the PCIe reference clock does not default to 100 MHz. We need
 * to do this ourselves. If we don't do this, PCIe will not be very happy.
 */
static int
milan_fabric_init_pcie_refclk(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_REFCLK_MODE);
	val = MILAN_IOHC_R_REFCLK_MODE_SET_MODE_27MHZ(val, 0);
	val = MILAN_IOHC_R_REFCLK_MODE_SET_MODE_25MHZ(val, 0);
	val = MILAN_IOHC_R_REFCLK_MODE_SET_MODE_100MHZ(val, 1);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_REFCLK_MODE, val);

	return (0);
}

/*
 * While the value for the delay comes from the PPR, the value for the limit
 * comes from other AMD sources.
 */
static int
milan_fabric_init_pci_to(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_PCIE_CRS_COUNT);
	val = MILAN_IOHC_R_SET_PCIE_CRS_COUNT_LIMIT(val, 0x262);
	val = MILAN_IOHC_R_SET_PCIE_CRS_COUNT_DELAY(val, 0x6);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_PCIE_CRS_COUNT, val);

	return (0);
}

/*
 * Here we initialize several of the IOHC features and related vendor-specific
 * messages are all set up correctly. XXX We're using lazy defaults of what the
 * system default has historically been here for some of these. We should test
 * and forcibly disable in hardware. Probably want to manipulate
 * IOHC::PCIE_VDM_CNTL2 at some point to beter figure out the VDM story. XXX
 * Also, ARI entablement is being done earlier than otherwise because we want to
 * only touch this reg in one place if we can.
 */
static int
milan_fabric_init_iohc_features(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = milan_iohc_read32(iodie, ioms, MILAH_IOHC_R_SMN_FEATURE_CNTL);
	val = MILAN_IOHC_R_FEATURE_CNTL_SET_ARI(val, 1);
	/* XXX Wants to be MILAN_IOHC_R_FEATURE_CNTL_P2P_DISABLE? */
	val = MILAN_IOHC_R_FEATURE_CNTL_SET_P2P(val,
	    MILAN_IOHC_R_FEATURE_CNTL_P2P_DROP_NMATCH);
	milan_iohc_write32(iodie, ioms, MILAH_IOHC_R_SMN_FEATURE_CNTL, val);

	return (0);
}

static int
milan_fabric_init_arbitration_ioms(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	/*
	 * Start with IOHC burst related entries. These are always the same
	 * across every entity. The value used for the actual time entries just
	 * varies.
	 */
	for (uint_t i = 0; i < MILAN_IOHC_R_SION_MAX_ENTS; i++) {
		uint32_t tsval;
		uint32_t regoff = MILAN_IOHC_R_SION_SHIFT(i);

		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S0_CLIREQ_BURST_LOW,
		    MILAN_IOHC_R_SION_CLIREQ_BURST_VAL);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S0_CLIREQ_BURST_HI,
		    MILAN_IOHC_R_SION_CLIREQ_BURST_VAL);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S1_CLIREQ_BURST_LOW,
		    MILAN_IOHC_R_SION_CLIREQ_BURST_VAL);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S1_CLIREQ_BURST_HI,
		    MILAN_IOHC_R_SION_CLIREQ_BURST_VAL);

		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S0_RDRSP_BURST_LOW,
		    MILAN_IOHC_R_SION_RDRSP_BURST_VAL);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S0_RDRSP_BURST_HI,
		    MILAN_IOHC_R_SION_RDRSP_BURST_VAL);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S1_RDRSP_BURST_LOW,
		    MILAN_IOHC_R_SION_RDRSP_BURST_VAL);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S1_RDRSP_BURST_HI,
		    MILAN_IOHC_R_SION_RDRSP_BURST_VAL);

		switch (i) {
		case 0:
		case 1:
		case 2:
			tsval = MILAN_IOHC_R_SION_CLIREQ_TIME_0_2_VAL;
			break;
		case 3:
		case 4:
			tsval = MILAN_IOHC_R_SION_CLIREQ_TIME_3_4_VAL;
			break;
		case 5:
			tsval = MILAN_IOHC_R_SION_CLIREQ_TIME_5_VAL;
			break;
		default:
			continue;
		}

		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S0_CLIREQ_TIME_LOW, tsval);
		milan_iohc_write32(iodie, ioms, regoff +
		    MILAN_IOHC_R_SMN_SION_S0_CLIREQ_TIME_HI, tsval);
	}

	/*
	 * Yes, we only set [4:1] here. I know it's odd. There is no 0, it's
	 * used by the S1 Client.
	 */
	for (uint_t i = 1; i < 4; i++) {
		uint32_t regoff = MILAN_IOHC_R_SION_SHIFT(i);

		val = milan_iohc_read32(iodie, ioms,
		    regoff + MILAN_IOHC_R_SMN_SION_S0_CLI_NP_DEFICIT);
		val = MILAN_IOHC_R_SET_SION_CLI_NP_DEFICIT(val,
		    MILAN_IOHC_R_SION_CLI_NP_DEFICIT_VAL);
		milan_iohc_write32(iodie, ioms,
		    regoff + MILAN_IOHC_R_SMN_SION_S0_CLI_NP_DEFICIT, val);
	}

	/*
	 * Go back and finally set the S1 threshold and live lock watchdog to
	 * finish off the IOHC.
	 */
	val = milan_iohc_read32(iodie, ioms,
	    MILAN_IOHC_R_SMN_SION_S1_CLI_NP_DEFECIT);
	val = MILAN_IOHC_R_SET_SION_CLI_NP_DEFICIT(val,
	    MILAN_IOHC_R_SION_CLI_NP_DEFICIT_VAL);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_SION_S1_CLI_NP_DEFECIT,
	    val);

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_SION_LLWD_THRESH);
	val = MILAN_IOHC_R_SET_SION_LLWD_THRESH_THRESH(val,
	    MILAN_IOHC_R_SION_LLWD_THRESH_VAL);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_SION_LLWD_THRESH, val);

	/*
	 * Next on our list is the IOAGR. While there are 5 entries, only 4 are
	 * ever set it seems.
	 */
	for (uint_t i = 0; i < 4; i++) {
		uint32_t tsval;
		uint32_t regoff = MILAN_IOAGR_R_SION_SHIFT(i);

		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_BURST_LOW,
		    MILAN_IOAGR_R_SION_CLIREQ_BURST_VAL);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_BURST_HI,
		    MILAN_IOAGR_R_SION_CLIREQ_BURST_VAL);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S1_CLIREQ_BURST_LOW,
		    MILAN_IOAGR_R_SION_CLIREQ_BURST_VAL);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S1_CLIREQ_BURST_HI,
		    MILAN_IOAGR_R_SION_CLIREQ_BURST_VAL);

		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S0_RDRSP_BURST_LOW,
		    MILAN_IOAGR_R_SION_RDRSP_BURST_VAL);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S0_RDRSP_BURST_HI,
		    MILAN_IOAGR_R_SION_RDRSP_BURST_VAL);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S1_RDRSP_BURST_LOW,
		    MILAN_IOAGR_R_SION_RDRSP_BURST_VAL);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S1_RDRSP_BURST_HI,
		    MILAN_IOAGR_R_SION_RDRSP_BURST_VAL);

		switch (i) {
		case 0:
		case 1:
		case 2:
			tsval = MILAN_IOAGR_R_SION_CLIREQ_TIME_0_2_VAL;
			break;
		case 3:
			tsval = MILAN_IOAGR_R_SION_CLIREQ_TIME_3_VAL;
			break;
		default:
			continue;
		}

		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_TIME_LOW, tsval);
		milan_ioagr_write32(iodie, ioms, regoff +
		    MILAN_IOAGR_R_SMN_SION_S0_CLIREQ_TIME_HI, tsval);
	}

	/*
	 * The IOAGR only has the watchdog.
	 */
	val = milan_ioagr_read32(iodie, ioms,
	    MILAN_IOAGR_R_SMN_SION_LLWD_THRESH);
	val = MILAN_IOAGR_R_SET_SION_LLWD_THRESH_THRESH(val,
	    MILAN_IOAGR_R_SION_LLWD_THRESH_VAL);
	milan_ioagr_write32(iodie, ioms, MILAN_IOAGR_R_SMN_SION_LLWD_THRESH,
	    val);

	/*
	 * Finally, the SDPMUX variant, which is surprisingly consistent
	 * compared to everything else to date.
	 */
	for (uint_t i = 0; i < MILAN_SDPMUX_R_SION_MAX_ENTS; i++) {
		uint32_t regoff = MILAN_SDPMUX_R_SION_SHIFT(i);

		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_BURST_LOW,
		    MILAN_SDPMUX_R_SION_CLIREQ_BURST_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_BURST_HI,
		    MILAN_SDPMUX_R_SION_CLIREQ_BURST_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S1_CLIREQ_BURST_LOW,
		    MILAN_SDPMUX_R_SION_CLIREQ_BURST_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S1_CLIREQ_BURST_HI,
		    MILAN_SDPMUX_R_SION_CLIREQ_BURST_VAL);

		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S0_RDRSP_BURST_LOW,
		    MILAN_SDPMUX_R_SION_RDRSP_BURST_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S0_RDRSP_BURST_HI,
		    MILAN_SDPMUX_R_SION_RDRSP_BURST_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S1_RDRSP_BURST_LOW,
		    MILAN_SDPMUX_R_SION_RDRSP_BURST_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S1_RDRSP_BURST_HI,
		    MILAN_SDPMUX_R_SION_RDRSP_BURST_VAL);

		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_TIME_LOW,
		    MILAN_SDPMUX_R_SION_CLIREQ_TIME_VAL);
		milan_sdpmux_write32(iodie, ioms, regoff +
		    MILAN_SDPMUX_R_SMN_SION_S0_CLIREQ_TIME_HI,
		    MILAN_SDPMUX_R_SION_CLIREQ_TIME_VAL);
	}

	val = milan_sdpmux_read32(iodie, ioms,
	    MILAN_SDPMUX_R_SMN_SION_LLWD_THRESH);
	val = MILAN_SDPMUX_R_SET_SION_LLWD_THRESH_THRESH(val,
	    MILAN_SDPMUX_R_SION_LLWD_THRESH_VAL);
	milan_sdpmux_write32(iodie, ioms, MILAN_SDPMUX_R_SMN_SION_LLWD_THRESH,
	    val);

	/*
	 * XXX We probably don't need this since we don't have USB. But until we
	 * have things working and can experiment, hard to say. If someone were
	 * to use the us, probably something we need to consider.
	 */
	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_USB_QOS_CNTL);
	val = MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID1_EN(val, 0x1);
	val = MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID1_PRI(val, 0x0);
	val = MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID1_ID(val, 0x30);
	val = MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID0_EN(val, 0x1);
	val = MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID0_PRI(val, 0x0);
	val = MILAN_IOHC_R_SET_USB_QOS_CNTL_UNID0_ID(val, 0x2f);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_USB_QOS_CNTL, val);

	return (0);
}

static int
milan_fabric_init_arbitration_nbif(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_nbif_t *nbif, void *arg)
{
	uint32_t val;

	milan_nbif_write32(iodie, nbif, MILAN_NBIF_R_SMN_GMI_WRR_WEIGHT2,
	    MILAN_NBIF_R_GMI_WRR_WEIGHT_VAL);
	milan_nbif_write32(iodie, nbif, MILAN_NBIF_R_SMN_GMI_WRR_WEIGHT3,
	    MILAN_NBIF_R_GMI_WRR_WEIGHT_VAL);

	val = milan_nbif_read32(iodie, nbif, MILAN_NBIF_R_SMN_BIFC_MISC_CTRL0);
	val = MILAN_NBIF_R_SET_BIFC_MISC_CTRL0_PME_TURNOFF(val,
	    MILAN_NBIF_R_BIFC_MISC_CTRL0_PME_TURNOFF_FW);
	milan_nbif_write32(iodie, nbif, MILAN_NBIF_R_SMN_BIFC_MISC_CTRL0, val);

	return (0);
}

/*
 * This sets up a bunch of hysteresis and port controls around the SDP, DMA
 * actions, and ClkReq. In general, these values are what we're told to set them
 * to in the PPR. Note, there is no need to change
 * IOAGR::IOAGR_SDP_PORT_CONTROL, which is why it is missing. The SDPMUX does
 * not have an early wake up register.
 */
static int
milan_fabric_init_sdp_control(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_SDP_PORT_CONTROL);
	val = MILAN_IOHC_R_SET_SDP_PORT_CONTROL_PORT_HYSTERESIS(val, 0xff);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_SDP_PORT_CONTROL, val);

	val = milan_iohc_read32(iodie, ioms,
	    MILAN_IOHC_R_SMN_SDP_EARLY_WAKE_UP);
	val = MILAN_IOHC_R_SET_SDP_EARLY_WAKE_UP_HOST_ENABLE(val, 0xffff);
	val = MILAN_IOHC_R_SET_SDP_EARLY_WAKE_UP_DMA_ENABLE(val, 0x1);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_SDP_EARLY_WAKE_UP,
	    val);

	val = milan_ioagr_read32(iodie, ioms, MILAN_IOAGR_R_SMN_EARLY_WAKE_UP);
	val = MILAN_IOAGR_R_SET_EARLY_WAKE_UP_DMA_ENABLE(val, 0x1);
	milan_ioagr_write32(iodie, ioms, MILAN_IOAGR_R_SMN_EARLY_WAKE_UP, val);

	val = milan_sdpmux_read32(iodie, ioms,
	    MILAN_SDPMUX_R_SMN_SDP_PORT_CONTROL);
	val = MILAN_SDPMUX_R_SET_SDP_PORT_CONTROL_HOST_ENABLE(val, 0xffff);
	val = MILAN_SDPMUX_R_SET_SDP_PORT_CONTROL_DMA_ENABLE(val, 0x1);
	val = MILAN_SDPMUX_R_SET_SDP_PORT_CONTROL_PORT_HYSTERESIS(val, 0xff);
	milan_sdpmux_write32(iodie, ioms, MILAN_SDPMUX_R_SMN_SDP_PORT_CONTROL,
	    val);

	return (0);
}

/*
 * XXX This bit of initialization is both strange and not very well documented.
 * This is a bit weird where by we always set this on nbif0 across all IOMS
 * instances; however, we only do it on NBIF1 for IOMS 0/1. Not clear why that
 * is. There are a bunch of things that don't quite make sense about being
 * specific to the syshub when generally we expect the one we care about to
 * actually be on IOMS 3.
 */
static int
milan_fabric_init_nbif_syshub_dma(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_nbif_t *nbif, void *arg)
{
	uint32_t val;

	if (nbif->mn_nbifno > 0 && ioms->mio_num > 1) {
		return (0);
	}
	val = milan_nbif_alt_read32(iodie, nbif,
	    MILAN_NBIF_R_SMN_SYSHUB_BGEN_BYPASS);
	val = MILAN_NBIF_R_SET_SYSHUB_BGEN_BYPASS_DMA_SW0(val, 1);
	milan_nbif_alt_write32(iodie, nbif, MILAN_NBIF_R_SMN_SYSHUB_BGEN_BYPASS,
	    val);
	return (0);
}

/*
 * We need to initialize each IOAPIC as there is one per IOMS. First we
 * initialize the interrupt routing table. This is used to mux the various
 * legacy INTx interrupts and the bridge's interrupt to a given location. This
 * follow from the PPR.
 *
 * After that we need to go through and program the feature register for the
 * IOAPIC and its address. Because there is one IOAPIC per IOMS, one has to be
 * elected the primary and the rest, secondary. This is done based on which IOMS
 * has the FCH.
 */
static int
milan_fabric_init_ioapic(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	ASSERT3U(ARRAY_SIZE(milan_ioapic_routes), ==, MILAN_IOAPIC_R_NROUTES);

	for (uint_t i = 0; i < ARRAY_SIZE(milan_ioapic_routes); i++) {
		uint32_t reg = MILAN_IOAPIC_R_SMN_ROUTE + i * 4;
		uint32_t route = milan_ioapic_read32(iodie, ioms, reg);

		route = MILAN_IOAPIC_R_SET_ROUTE_BRIDGE_MAP(route,
		    milan_ioapic_routes[i].mii_map);
		route = MILAN_IOAPIC_R_SET_ROUTE_INTX_SWIZZLE(route,
		    milan_ioapic_routes[i].mii_swiz);
		route = MILAN_IOAPIC_R_SET_ROUTE_INTX_GROUP(route,
		    milan_ioapic_routes[i].mii_group);

		milan_ioapic_write32(iodie, ioms, reg, route);
	}

	/*
	 * The address registers are in the IOHC while the feature registers are
	 * in the IOAPIC SMN space. To ensure that the other IOAPICs can't be
	 * enabled with reset addresses, we instead lock them. XXX Should we
	 * lock primary?
	 */
	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_IOAPIC_ADDR_HI);
	if ((ioms->mio_flags & MILAN_IOMS_F_HAS_FCH) != 0) {
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_HI_ADDR(val,
		    bitx64(MILAN_PHYSADDR_IOHC_IOAPIC, 47, 32));
	} else {
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_HI_ADDR(val, 0);
	}
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_IOAPIC_ADDR_HI, val);

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_IOAPIC_ADDR_LO);
	if ((ioms->mio_flags & MILAN_IOMS_F_HAS_FCH) != 0) {
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_ADDR(val,
		    bitx64(MILAN_PHYSADDR_IOHC_IOAPIC, 31, 8));
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_LOCK(val, 0);
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_EN(val, 1);
	} else {
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_ADDR(val, 0);
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_LOCK(val, 1);
		val = MILAN_IOHC_R_SET_IOAPIC_ADDR_LO_EN(val, 0);
	}
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_IOAPIC_ADDR_LO, val);

	/*
	 * Every IOAPIC requires that we enable 8-bit addressing and that it be
	 * able to generate interrupts to the FCH. The most important bit here
	 * is the secondary bit which determines whether or not this IOAPIC is
	 * subordinate to another.
	 */
	val = milan_ioapic_read32(iodie, ioms,
	    MILAN_IOAPIC_R_SMN_FEATURES);
	if ((ioms->mio_flags & MILAN_IOMS_F_HAS_FCH) != 0) {
		val = MILAN_IOAPIC_R_SET_FEATURES_SECONDARY(val, 0);
	} else {
		val = MILAN_IOAPIC_R_SET_FEATURES_SECONDARY(val, 1);
	}
	val = MILAN_IOAPIC_R_SET_FEATURES_FCH(val, 1);
	val = MILAN_IOAPIC_R_SET_FEATURES_ID_EXT(val, 1);
	milan_ioapic_write32(iodie, ioms, MILAN_IOAPIC_R_SMN_FEATURES, val);

	return (0);
}

/*
 * Each IOHC has registers that can further constraion what type of PCI bus
 * numbers the IOHC itself is expecting to reply to. As such, we program each
 * IOHC with its primary bus number and enable this.
 */
static int
milan_fabric_init_bus_num(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;

	val = milan_iohc_read32(iodie, ioms, MILAN_IOHC_R_SMN_BUS_NUM_CNTL);
	val = MILAN_IOHC_R_SET_BUS_NUM_CNTL_EN(val, 1);
	val = MILAN_IOHC_R_SET_BUS_NUM_CNTL_BUS(val, ioms->mio_pci_busno);
	milan_iohc_write32(iodie, ioms, MILAN_IOHC_R_SMN_BUS_NUM_CNTL, val);

	return (0);
}

/*
 * Go through and configure and set up devices and functions. In particular we
 * need to go through and set up the following:
 *
 *  o Strap bits that determine whether or not the function is enabled
 *  o Enabling the interrupts of corresponding functions
 *  o Setting up specific PCI device straps around multi-function, FLR, poison
 *    control, TPH settings, etc.
 *
 * XXX For getting to PCIe faster and since we're not going to use these, and
 * they're all disabled, for the moment we just ignore the straps that aren't
 * related to interrupts, enables, and cfg comps.
 */
static int
milan_fabric_init_nbif_dev_straps(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_nbif_t *nbif, void *arg)
{
	uint32_t intr;

	intr = milan_nbif_read32(iodie, nbif, MILAN_NBIF_R_SMN_INTR_LINE);
	for (uint_t funcno = 0; funcno < nbif->mn_nfuncs; funcno++) {
		uint32_t strap;
		milan_nbif_func_t *func = &nbif->mn_funcs[funcno];

		/*
		 * This indicates that we have a dummy function or similar. In
		 * which case there's not much to do here, the system defaults
		 * are generally what we want. XXX Kind of sort of. Not true
		 * over time.
		 */
		if ((func->mne_flags & MILAN_NBIF_F_NO_CONFIG) != 0) {
			continue;
		}

		strap = milan_nbif_func_read32(iodie, func,
		    MILAN_NBIF_R_SMN_FUNC_STRAP0);

		if ((func->mne_flags & MILAN_NBIF_F_ENABLED) != 0) {
			strap = MILAN_NBIF_R_SET_FUNC_STRAP0_EXIST(strap, 1);
			intr = MILAN_NBIF_R_INTR_LINE_SET_INTR(intr,
			    func->mne_dev, func->mne_func, 1);

			/*
			 * Strap enabled SATA devices to what AMD asks for.
			 */
			if (func->mne_type == MILAN_NBIF_T_SATA) {
				strap = MILAN_NBIF_R_SET_FUNC_STRAP0_MAJ_REV(
				    strap, 7);
				strap = MILAN_NBIF_R_SET_FUNC_STRAP0_MIN_REV(
				    strap, 1);
			}
		} else {
			strap = MILAN_NBIF_R_SET_FUNC_STRAP0_EXIST(strap, 0);
			intr = MILAN_NBIF_R_INTR_LINE_SET_INTR(intr,
			    func->mne_dev, func->mne_func, 0);
		}

		milan_nbif_func_write32(iodie, func,
		    MILAN_NBIF_R_SMN_FUNC_STRAP0, strap);
	}

	milan_nbif_write32(iodie, nbif, MILAN_NBIF_R_SMN_INTR_LINE, intr);

	/*
	 * Each nBIF has up to three devices on them, though not all of them
	 * seem to be used. However, it's suggested that we enable completion
	 * timeouts on all three device straps.
	 */
	for (uint_t devno = 0; devno < MILAN_NBIF_MAX_DEVS; devno++) {
		uint32_t val, smn_addr;

		smn_addr = MILAN_SMN_NBIF_DEV_PORT_SHIFT(devno) +
		    MILAN_NBIF_R_SMN_PORT_STRAP3;

		val = milan_nbif_read32(iodie, nbif, smn_addr);
		val = MILAN_NBIF_R_SET_PORT_STRAP3_COMP_TO(val, 1);
		milan_nbif_write32(iodie, nbif, smn_addr, val);
	}

	return (0);
}

/*
 * There are five bridges that are associated with the NBIFs. One on NBIF0,
 * three on NBIF1, and the last on the SB. There is nothing on NBIF 2 which is
 * why we don't use the nbif iterator, though this is somewhat uglier. The
 * default expectation of the system is that the CRS bit is set. XXX these have
 * all been left enabled for now.
 */
static int
milan_fabric_init_nbif_bridge(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	uint32_t val;
	uint32_t nbif1_base = MILAN_IOHC_R_SMN_BRIDGE_CNTL_NBIF +
	    MILAN_IOHC_R_SMN_BRIDGE_CNTL_NBIF_SHIFT(1);
	uint32_t smn_addrs[5] = { MILAN_IOHC_R_SMN_BRIDGE_CNTL_NBIF,
	    nbif1_base,
	    nbif1_base + MILAN_IOHC_R_SMN_BRIDGE_CNTL_BRIDGE_SHIFT(1),
	    nbif1_base + MILAN_IOHC_R_SMN_BRIDGE_CNTL_BRIDGE_SHIFT(2),
	    MILAN_IOHC_R_SMN_BRIDGE_CNTL_SB };

	for (uint_t i = 0; i < ARRAY_SIZE(smn_addrs); i++) {
		val = milan_iohc_read32(iodie, ioms, smn_addrs[i]);
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_CRS_ENABLE(val, 1);
		milan_iohc_write32(iodie, ioms, smn_addrs[i], val);
	}
	return (0);
}

static int
milan_dxio_init(milan_fabric_t *fabric, milan_soc_t *soc, milan_iodie_t *iodie,
    void *arg)
{
	/*
	 * XXX There's a BMC in Ethanol. As a result when we're on that die we
	 * need to issue the SM reload command. What that is reloading is hard
	 * to say. This only exists on Socket 0 so don't do it on the other
	 * socket.
	 */
	if (soc->ms_socno == 0 && !milan_dxio_rpc_sm_reload(iodie)) {
		return (1);
	}


	if (!milan_dxio_rpc_init(iodie)) {
		return (1);
	}

	/*
	 * XXX These 0x4f values were kind of given to us. Do better than a
	 * magic constant, rm.
	 */
	if (!milan_dxio_rpc_clock_gating(iodie, 0x4f, 0x4f)) {
		return (1);
	}

	/*
	 * Set up a few different variables in firmware. Best guesses is that we
	 * need MILAN_DXIO_VAR_PCIE_COMPL so we can get PCIe completions to
	 * actually happen, MILAN_DXIO_VAR_SLIP_INTERVAL is disabled, but I
	 * can't say why. XXX We should probably disable NTB hotplug because we
	 * don't have them just in case something changes here.
	 */
	if (!milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_PCIE_COMPL, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_SLIP_INTERVAL, 0)) {
		return (1);
	}

	/*
	 * This seems to configure behavior when the link is going down and
	 * power off. We explicitly ask for no delay. The latter argument is
	 * about disabling another command (which we don't use), but to keep
	 * firmware in its expected path we don't set that.
	 *
	 * XXX Not in 1.0.0.1
	 */
#if 0
	if (!milan_dxio_rpc_pcie_poweroff_config(iodie, 0, B_FALSE)) {
		return (1);
	}
#endif

	/*
	 * Next we set a couple of variables that are required for us to
	 * cause the state machine to pause after a couple of different stages
	 * and then also to indicate that we want to use the v1 ancillary data
	 * format.
	 */
	if (!milan_dxio_rpc_set_var(iodie, MLIAN_DXIO_VAR_RET_AFTER_MAP, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_RET_AFTER_CONF, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_ANCILLARY_V1, 1)) {
		return (1);
	}

	/*
	 * Here, it's worth calling out what we're not setting. One of which is
	 * MILAN_DXIO_VAR_MAP_EXACT_MATCH which ends up being used to cause
	 * the mapping phase to only work if there are exact matches. I believe
	 * this means that if a device has more lanes then the configured port,
	 * it wouldn't link up, which generally speaking isn't something we want
	 * to do. Similarly, since there is no S3 support here, no need to
	 * change the save and restore mode with MILAN_DXIO_VAR_S3_MODE.
	 *
	 * From here, we do want to set MILAN_DXIO_VAR_SKIP_PSP, because the PSP
	 * really doesn't need to do anything with us. We do want to enable
	 * MILAN_DXIO_VAR_PHY_PROG so the dxio engine can properly configure
	 * things.
	 *
	 * XXX Should we gamble and set things that aren't unconditionally set
	 * so we don't rely on hw defaults?
	 */
	if (!milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_PHY_PROG, 1) ||
	    !milan_dxio_rpc_set_var(iodie, MILAN_DXIO_VAR_SKIP_PSP, 1)) {
		return (0);
	}

	return (0);
}

typedef enum {
	GIMLET,
	ETHANOL
} milan_board_type_t;

/*
 * Here is a temporary rough heuristic for determining what board we're on.
 */
static milan_board_type_t
milan_board_type(const milan_fabric_t *fabric)
{
	if (fabric->mf_nsocs == 2) {
		return (ETHANOL);
	} else {
		return (GIMLET);
	}
}

/*
 * Here we need to assemble data for the system we're actually on. XXX Right now
 * we're just assuming we're Ethanol-X and only leveraging ancillary data from
 * the PSP.
 */
static int
milan_dxio_plat_data(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	ddi_dma_attr_t attr;
	size_t engn_size;
	pfn_t pfn;
	milan_dxio_config_t *conf = &iodie->mi_dxio_conf;
	const zen_dxio_platform_t *source_data;
	zen_dxio_anc_data_t *anc;
	const void *phy_override;
	size_t phy_len;
	int err;

	/*
	 * XXX Figure out how to best not hardcode Ethanol. Realistically
	 * probably an SP boot property.
	 */
	if (milan_board_type(fabric) == ETHANOL) {
		if (soc->ms_socno == 0) {
			source_data = &ethanolx_engine_s0;
		} else {
			source_data = &ethanolx_engine_s1;
		}
	} else {
		VERIFY3U(soc->ms_socno, ==, 0);
		source_data = &gimlet_engine;
	}

	engn_size = sizeof (zen_dxio_platform_t) +
	    source_data->zdp_nengines * sizeof (zen_dxio_engine_t);
	VERIFY3U(engn_size, <=, MMU_PAGESIZE);
	conf->mdc_conf_len = engn_size;

	milan_smu_dma_attr(&attr);
	conf->mdc_alloc_len = MMU_PAGESIZE;
	conf->mdc_conf = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->mdc_conf, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->mdc_conf);
	conf->mdc_pa = mmu_ptob((uint64_t)pfn);

	bcopy(source_data, conf->mdc_conf, engn_size);

	/*
	 * We need to account for an extra 8 bytes, surprisingly. It's a good
	 * thing we have a page. Note, dxio wants this in uint32_t units. We do
	 * that when we make the RPC call. Finally, we want to make sure that if
	 * we're in an incomplete word, that we account for that in the length.
	 */
	conf->mdc_conf_len += 8;
	conf->mdc_conf_len = P2ROUNDUP(conf->mdc_conf_len, 4);

	phy_override = milan_apob_find(MILAN_APOB_GROUP_FABRIC,
	    MILAN_APOB_FABRIC_PHY_OVERRIDE, 0, &phy_len, &err);
	if (phy_override == NULL) {
		if (err == ENOENT) {
			return (0);
		}

		cmn_err(CE_WARN, "failed to find phy override table in APOB: "
		    "0x%x", err);
		return (1);
	}

	conf->mdc_anc = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(conf->mdc_anc, MMU_PAGESIZE);

	pfn = hat_getpfnum(kas.a_hat, (caddr_t)conf->mdc_anc);
	conf->mdc_anc_pa = mmu_ptob((uint64_t)pfn);

	/*
	 * First we need to program the initial descriptor. Its type is one of
	 * the Heap types. Yes, this is different from the sub data payloads
	 * that we use. Yes, this is different from the way that the engine
	 * config data is laid out. Each entry has the amount of space they take
	 * up. Confusingly, it seems that the top entry does not include the
	 * space its header takes up. However, the subsequent payloads do.
	 */
	anc = conf->mdc_anc;
	anc->zdad_type = MILAN_DXIO_HEAP_ANCILLARY;
	anc->zdad_vers = DXIO_ANCILLARY_VERSION;
	anc->zdad_nu32s = (sizeof (zen_dxio_anc_data_t) + phy_len) >> 2;
	anc++;
	anc->zdad_type = ZEN_DXIO_ANCILLARY_T_PHY;
	anc->zdad_vers = DXIO_ANCILLARY_PAYLOAD_VERSION;
	anc->zdad_nu32s = (sizeof (zen_dxio_anc_data_t) + phy_len) >> 2;
	anc++;
	bcopy(phy_override, anc, phy_len);
	conf->mdc_anc_len = phy_len + 2 * sizeof (zen_dxio_anc_data_t);

	return (0);
}

static int
milan_dxio_load_data(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	milan_dxio_config_t *conf = &iodie->mi_dxio_conf;

	/*
	 * Begin by loading the NULL capabilities before we load any data heaps.
	 */
	if (!milan_dxio_rpc_load_caps(iodie)) {
		return (1);
	}

	if (conf->mdc_anc != NULL && !milan_dxio_rpc_load_data(iodie,
	    MILAN_DXIO_HEAP_ANCILLARY, conf->mdc_anc_pa, conf->mdc_anc_len,
	    0)) {
		return (1);
	}

	/*
	 * It seems that we're required to load both of these heaps with the
	 * mystery bit set to one. It's called that because we don't know what
	 * it does; however, these heaps are always loaded with no data, even
	 * though ancillary is skipped if there is none.
	 */
	if (!milan_dxio_rpc_load_data(iodie, MILAN_DXIO_HEAP_MACPCS,
	    0, 0, 1) ||
	    !milan_dxio_rpc_load_data(iodie, MILAN_DXIO_HEAP_GPIO, 0, 0, 1)) {
		return (1);
	}

	/*
	 * Load our real data!
	 */
	if (!milan_dxio_rpc_load_data(iodie, MILAN_DXIO_HEAP_ENGINE_CONFIG,
	    conf->mdc_pa, conf->mdc_conf_len, 0)) {
		return (1);
	}

	return (0);
}

static int
milan_dxio_more_conf(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	/*
	 * Note, here we might use milan_dxio_rpc_conf_training() if we want to
	 * override any of the properties there. But the defaults in DXIO
	 * firmware seem to be used by default. We also might apply various
	 * workarounds that we don't seem to need to
	 * (MILAN_DXIO_RT_SET_CONF_DXIO_WA, MILAN_DXIO_RT_SET_CONF_SPC_WA,
	 * MILAN_DXIO_RT_SET_CONF_FC_CRED_WA_DIS).
	 */

	/*
	 * XXX Do we care about any of the following:
	 *    o MILAN_DXIO_RT_SET_CONF_TX_CLOCK
	 *    o MILAN_DXIO_RT_SET_CONF_SRNS
	 *    o MILAN_DXIO_RT_SET_CONF_DLF_WA_DIS
	 *
	 * I wonder why we don't enable MILAN_DXIO_RT_SET_CONF_CE_SRAM_ECC in
	 * the old world.
	 */

	/*
	 * This is set to 1 by default because we want 'latency behaviour' not
	 * 'improved latency'.
	 */
	if (!milan_dxio_rpc_misc_rt_conf(iodie,
	    MILAN_DXIO_RT_SET_CONF_TX_FIFO_MODE, 1)) {
		return (1);
	}

	return (0);
}


/*
 * Given all of the engines on an I/O die, try and map each one to a
 * corresponding IOMS and bridge. We only care about an engine if it is a PCIe
 * engine. Note, because each I/O die is processed independently, this only
 * operates on a single I/O die.
 */
static boolean_t
milan_dxio_map_engines(milan_fabric_t *fabric, milan_iodie_t *iodie)
{
	boolean_t ret = B_TRUE;
	zen_dxio_platform_t *plat = iodie->mi_dxio_conf.mdc_conf;

	for (uint_t i = 0; i < plat->zdp_nengines; i++) {
		zen_dxio_engine_t *en = &plat->zdp_engines[i];
		milan_pcie_port_t *port;
		milan_pcie_bridge_t *bridge;
		uint8_t bridgeno;

		if (en->zde_type != DXIO_ENGINE_PCIE)
			continue;


		port = milan_fabric_find_port_by_lanes(fabric, iodie,
		    en->zde_start_lane, en->zde_end_lane);
		if (port == NULL) {
			cmn_err(CE_WARN, "failed to map engine %u [%u, %u] to "
			    "a PCIe port", i, en->zde_start_lane,
			    en->zde_end_lane);
			ret = B_FALSE;
			continue;
		}

		bridgeno = en->zde_config.zdc_pcie.zdcp_mac_port_id;
		if (bridgeno >= port->mpp_nbridges) {
			cmn_err(CE_WARN, "failed to map engine %u [%u, %u] to "
			    "a PCIe bridge: found nbridges %u, but mapped to "
			    "bridge %u",  i, en->zde_start_lane,
			    en->zde_end_lane, port->mpp_nbridges, bridgeno);
			ret = B_FALSE;
			continue;
		}

		bridge = &port->mpp_bridges[bridgeno];
		if (bridge->mpb_engine != NULL) {
			cmn_err(CE_WARN, "engine %u [%u, %u] mapped to "
			    "bridge %u, which already has an engine [%u, %u]",
			    i, en->zde_start_lane, en->zde_end_lane,
			    port->mpp_nbridges,
			    bridge->mpb_engine->zde_start_lane,
			    bridge->mpb_engine->zde_end_lane);
			ret = B_FALSE;
			continue;
		}

		bridge->mpb_flags |= MILAN_PCIE_BRIDGE_F_MAPPED;
		bridge->mpb_engine = en;
		port->mpp_flags |= MILAN_PCIE_PORT_F_USED;
		if (en->zde_config.zdc_pcie.zdcp_caps.zdlc_hp !=
		    DXIO_HOTPLUG_T_DISABLED) {
			port->mpp_flags |= MILAN_PCIE_PORT_F_HAS_HOTPLUG;
		}
	}

	return (ret);
}

/*
 * These PCIe straps need to be set after mapping is done, but before link
 * training has started. While we do not understand in detail what all of these
 * registers do, we've split this broadly into 2 categories:
 * 1) Straps where:
 *     a) the defaults in hardware seem to be reasonable given our (sometimes
 *     limited) understanding of their function
 *     b) are not features/parameters that we currently care specifically about
 *     one way or the other
 *     c) and we are currently ok with the defaults changing out from underneath
 *     us on different hardware revisions unless proven otherwise.
 * or 2) where:
 *     a) We care specifically about a feature enough to ensure that it is set
 *     (e.g. AERs) or purposefully disabled (e.g. I2C_DBG_EN)
 *     b) We are not ok with these changing based on potentially different
 *     defaults set in different hardware revisions
 * For 1), we've chosen to leave them based on whatever the hardware has chosen
 * as the default, while all the straps detailed underneath fall into category
 * 2. Note that this list is by no means definitive, and will almost certainly
 * change as our understanding of what we require from the hardware evolves.
 */
typedef struct milan_pcie_strap_setting {
	uint32_t		strap_reg;
	uint32_t		strap_data;
} milan_pcie_strap_setting_t;

/*
 * PCIe Straps that we unconditionally set to 1
 */
static const uint32_t milan_pcie_strap_enable[] = {
	MILAN_STRAP_PCIE_MSI_EN,
	MILAN_STRAP_PCIE_AER_EN,
	MILAN_STRAP_PCIE_GEN2_COMP,
	/* We want completion timeouts */
	MILAN_STRAP_PCIE_CPL_TO_EN,
	MILAN_STRAP_PCIE_TPH_EN,
	MILAN_STRAP_PCIE_MULTI_FUNC_EN,
	MILAN_STRAP_PCIE_DPC_EN,
	MILAN_STRAP_PCIE_ARI_EN,
	MILAN_STRAP_PCIE_PL_16G_EN,
	MILAN_STRAP_PCIE_LANE_MARGIN_EN,
	MILAN_STRAP_PCIE_LTR_SUP,
	MILAN_STRAP_PCIE_LINK_BW_NOTIF_SUP,
	MILAN_STRAP_PCIE_GEN3_1_FEAT_EN,
	MILAN_STRAP_PCIE_GEN4_FEAT_EN,
	MILAN_STRAP_PCIE_ECRC_GEN_EN,
	MILAN_STRAP_PCIE_ECRC_CHECK_EN,
	MILAN_STRAP_PCIE_CPL_ABORT_ERR_EN,
	MILAN_STRAP_PCIE_INT_ERR_EN,
	MILAN_STRAP_PCIE_RXP_ACC_FULL_DIS,

	/* ACS straps */
	MILAN_STRAP_PCIE_ACS_EN,
	MILAN_STRAP_PCIE_ACS_SRC_VALID,
	MILAN_STRAP_PCIE_ACS_TRANS_BLOCK,
	MILAN_STRAP_PCIE_ACS_DIRECT_TRANS_P2P,
	MILAN_STRAP_PCIE_ACS_P2P_CPL_REDIR,
	MILAN_STRAP_PCIE_ACS_P2P_REQ_RDIR,
	MILAN_STRAP_PCIE_ACS_UPSTREAM_FWD,
};

/*
 * PCIe Straps that we unconditionally set to 0
 * These are generally debug and test settings that are usually not a good idea
 * in my experience to allow accidental enablement.
 */
static const uint32_t milan_pcie_strap_disable[] = {
	MILAN_STRAP_PCIE_I2C_DBG_EN,
	MILAN_STRAP_PCIE_DEBUG_RXP,
	MILAN_STRAP_PCIE_NO_DEASSERT_RX_EN_TEST,
	MILAN_STRAP_PCIE_ERR_REPORT_DIS,
	MILAN_STRAP_PCIE_TX_TEST_ALL,
	MILAN_STRAP_PCIE_MCAST_EN,
};

/*
 * PCIe Straps that have other values.
 */
static const milan_pcie_strap_setting_t milan_pcie_strap_settings[] = {
	{
	    .strap_reg = MILAN_STRAP_PCIE_EQ_DS_RX_PRESET_HINT,
	    .strap_data = MILAN_STRAP_PCIE_RX_PRESET_9DB,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_EQ_US_RX_PRESET_HINT,
	    .strap_data = MILAN_STRAP_PCIE_RX_PRESET_9DB,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_EQ_DS_TX_PRESET,
	    .strap_data = MILAN_STRAP_PCIE_TX_PRESET_7,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_EQ_US_TX_PRESET,
	    .strap_data = MILAN_STRAP_PCIE_TX_PRESET_7,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_16GT_EQ_DS_TX_PRESET,
	    .strap_data = MILAN_STRAP_PCIE_TX_PRESET_7,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_16GT_EQ_US_TX_PRESET,
	    .strap_data = MILAN_STRAP_PCIE_TX_PRESET_5,
	},
};

/*
 * Strap settings that only apply to Ethanol
 */
static const milan_pcie_strap_setting_t milan_pcie_strap_ethanol_settings[] = {
};

/*
 * Strap settings that only apply to Gimlet
 */
static const milan_pcie_strap_setting_t milan_pcie_strap_gimlet_settings[] = {
	{
	    .strap_reg = MILAN_STRAP_PCIE_SUBVID,
	    .strap_data = PCI_VENDOR_ID_OXIDE,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_SUBDID,
	    .strap_data = MILAN_STRAP_PCIE_SUBDID_BRIDGE,
	},
};

/*
 * PCIe Straps that exist on a per-bridge level.
 */
static const milan_pcie_strap_setting_t milan_pcie_bridge_settings[] = {
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_EXT_TAG_SUP,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_E2E_TLP_PREFIX_EN,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_10B_TAG_CMPL_SUP,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_10B_TAG_REQ_SUP,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_TCOMMONMODE_TIME,
	    .strap_data = 0xa,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_TPON_SCALE,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_TPON_VALUE,
	    .strap_data = 0xf,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_DLF_SUP,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_DLF_EXCHANGE_EN,
	    .strap_data = 0x1,
	},
	{
	    .strap_reg = MILAN_STRAP_PCIE_P_FOM_TIME,
	    .strap_data = MILAN_STRAP_PCIE_P_FOM_300US,
	},
};

static void
milan_fabric_write_pcie_strap(milan_iodie_t *iodie, milan_ioms_t *ioms,
    milan_pcie_port_t *port, uint32_t reg, uint32_t data)
{
	mutex_enter(&iodie->mi_pcie_strap_lock);
	milan_smn_write32(iodie,
	    MILAN_SMN_MAKE_ADDR(port->mpp_strap_smn_addr,
	    MILAN_SMN_PCIE_STRAP_BASE_BITS,
	    MILAN_SMN_PCIE_STRAP_R_ADDR),
	    MILAN_STRAP_PCIE_ADDR_UPPER + reg);
	milan_smn_write32(iodie,
	    MILAN_SMN_MAKE_ADDR(port->mpp_strap_smn_addr,
	    MILAN_SMN_PCIE_STRAP_BASE_BITS,
	    MILAN_SMN_PCIE_STRAP_R_DATA),
	    data);
	mutex_exit(&iodie->mi_pcie_strap_lock);
}

/*
 * Here we set up all the straps for PCIe features that we care about and want
 * advertised as capabilities. Note that we do not enforce any order between the
 * straps. It is our understanding that the straps themselves do not kick off
 * any change, but instead another stage (presumably before link training)
 * initializes the read of all these straps in one go.
 * Currently, we set these straps on all ports and all bridges regardless of
 * whether they are used, though this may be changed if it proves problematic.
 */
static int
milan_fabric_init_pcie_straps(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    void *arg)
{
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_strap_enable); i++) {
		milan_fabric_write_pcie_strap(iodie, ioms, port,
		    milan_pcie_strap_enable[i], 0x1);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_strap_disable); i++) {
		milan_fabric_write_pcie_strap(iodie, ioms, port,
		    milan_pcie_strap_disable[i], 0x0);
	}
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_strap_settings); i++) {
		const milan_pcie_strap_setting_t *strap =
		    &milan_pcie_strap_settings[i];

		milan_fabric_write_pcie_strap(iodie, ioms, port,
		    strap->strap_reg, strap->strap_data);
	}

	/* Handle Special case for DLF which needs to be set on non WAFL */
	if (port->mpp_portno != MILAN_IOMS_WAFL_PCIE_PORT) {
		milan_fabric_write_pcie_strap(iodie, ioms, port,
		    MILAN_STRAP_PCIE_DLF_EN, 1);
	}

	/* Handle board specific straps */
	const milan_pcie_strap_setting_t *board_list;
	int array_size;
	if (milan_board_type(fabric) == ETHANOL) {
		board_list = milan_pcie_strap_ethanol_settings;
		array_size = ARRAY_SIZE(milan_pcie_strap_ethanol_settings);
	} else {
		board_list = milan_pcie_strap_gimlet_settings;
		array_size = ARRAY_SIZE(milan_pcie_strap_gimlet_settings);
	}
	for (uint_t i = 0; i < array_size; i++) {
		const milan_pcie_strap_setting_t *strap =
		    &board_list[i];

		milan_fabric_write_pcie_strap(iodie, ioms, port,
		    strap->strap_reg, strap->strap_data);
	}

	/* Handle per bridge initialization */
	for (uint_t i = 0; i < ARRAY_SIZE(milan_pcie_bridge_settings); i++) {
		const milan_pcie_strap_setting_t *strap =
		    &milan_pcie_bridge_settings[i];
		for (uint_t j = 0; j < port->mpp_nbridges; j++) {
			milan_fabric_write_pcie_strap(iodie, ioms, port,
			    strap->strap_reg +
			    (j * MILAN_STRAP_PCIE_NUM_PER_BRIDGE),
			    strap->strap_data);
		}
	}

	return (0);
}

/*
 * Here we are, it's time to actually kick off the state machine that we've
 * wanted to do.
 */
static int
milan_dxio_state_machine(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	if (!milan_dxio_rpc_sm_start(iodie)) {
		return (1);
	}

	for (;;) {
		milan_dxio_reply_t reply = { 0 };

		if (!milan_dxio_rpc_sm_getstate(iodie, &reply)) {
			return (1);
		}

		switch (reply.mds_type) {
		case MILAN_DXIO_DATA_TYPE_SM:
			cmn_err(CE_WARN, "Socket %u SM 0x%x->0x%x",
			    soc->ms_socno, iodie->mi_state, reply.mds_arg0);
			iodie->mi_state = reply.mds_arg0;
			switch (iodie->mi_state) {
			/*
			 * The mapped state indicates that the engines and lanes
			 * that we have provided in our DXIO configuration have
			 * been mapped back to the actual set of PCIe ports on
			 * the IOMS (e.g. G0, P0) and specific bridge indexes
			 * within that port group. The very first thing we need
			 * to do here is to figure out what actually has been
			 * mapped to what and update what ports are actually
			 * being used by devices or not.
			 */
			case MILAN_DXIO_SM_MAPPED:
				if (!milan_dxio_rpc_retrieve_engine(iodie)) {
					return (1);
				}

				if (!milan_dxio_map_engines(fabric, iodie)) {
					cmn_err(CE_WARN, "failed to map all "
					    "DXIO engines to devices in the "
					    "milan_fabric_t");
					return (1);
				}
				cmn_err(CE_WARN, "XXX skipping a ton of mapped "
				    "stuff");
				/*
				 * Now that we have the mapping done, we set up
				 * the straps for PCIe.
				 */
				(void) milan_fabric_walk_pcie_port(fabric,
				    milan_fabric_init_pcie_straps, NULL);

				cmn_err(CE_NOTE, "Finished writing PCIe "
				    "straps.");
				break;
			case MILAN_DXIO_SM_CONFIGURED:
				cmn_err(CE_WARN, "XXX skipping a ton of "
				    "configured stuff");
				break;
			case MILAN_DXIO_SM_DONE:
				/*
				 * We made it. Somehow we're done!
				 */
				cmn_err(CE_WARN, "we're out of here");
				goto done;
			default:
				/*
				 * For most states there doesn't seem to be much
				 * to do. So for now we just leave the default
				 * case to continue and proceed to the next
				 * state machine state.
				 */
				break;
			}
			break;
		case MILAN_DXIO_DATA_TYPE_RESET:
			cmn_err(CE_WARN, "let's go deasserting: %x, %x",
			    reply.mds_arg0, reply.mds_arg1);
			if (reply.mds_arg0 == 0) {
				cmn_err(CE_WARN, "Asked to set GPIO to zero, "
				    "which  would PERST. Nope. Continuing?");
				break;
			}

			/*
			 * XXX We're doing this the max power way. This is
			 * definitely probably not the right way. These are in
			 * order:
			 *
			 * FCH::GPIO::GPIO_26
			 * FCH::GPIO::GPIO_27
			 * FCH::RMTGPIO::GPIO_266
			 * FCH::RMTGPIO::GPIO_267
			 */
			milan_smn_write32(iodie, 0x2d02568, 0xc40000);
			milan_smn_write32(iodie, 0x2d0256c, 0xc40000);
			milan_smn_write32(iodie, 0x2d02228, 0xc40000);
			milan_smn_write32(iodie, 0x2d0222c, 0xc40000);
			break;
		case MILAN_DXIO_DATA_TYPE_NONE:
			cmn_err(CE_WARN, "Got the none data type... are we "
			    "actually done?");
			goto done;
		default:
			cmn_err(CE_WARN, "Got unexpected DXIO return type: "
			    "0x%x. Sorry, no PCIe for us on socket %u.",
			    reply.mds_type, soc->ms_socno);
			return (1);
		}

		if (!milan_dxio_rpc_sm_resume(iodie)) {
			return (1);
		}
	}

done:
	if (!milan_dxio_rpc_retrieve_engine(iodie)) {
		return (1);
	}

	return (0);
}

/*
 * Our purpose here is to set up memlist structures for use in tracking. Right
 * now we use the xmemlist feature, though having something that is backed by
 * kmem would make life easier; however, that will wait for the great memlist
 * merge that is likely not to happen anytime soon.
 */
static int
milan_fabric_init_memlists(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	ioms_memlists_t *imp = &ioms->mio_memlists;
	void *page = kmem_zalloc(MMU_PAGESIZE, KM_SLEEP);

	mutex_init(&imp->im_lock, NULL, MUTEX_DRIVER, NULL);
	xmemlist_free_block(&imp->im_pool, page, MMU_PAGESIZE);
	return (0);
}

/*
 * We want to walk the DF and record information about how PCI buses are routed.
 * We make an assumption here, which is that each DF instance has been
 * programmed the same way by the PSP/SMU (which if was not done would lead to
 * some chaos). As such, we end up using the first socket's df and its first
 * IOMS to figure this out.
 */
static void
milan_route_pci_bus(milan_fabric_t *fabric)
{
	milan_iodie_t *iodie = &fabric->mf_socs[0].ms_iodies[0];
	uint_t inst = iodie->mi_ioms[0].mio_comp_id;

	for (uint_t i = 0; i < AMDZEN_DF_F0_MAX_CFGMAP; i++) {
		int ret;
		milan_ioms_t *ioms;
		ioms_memlists_t *imp;
		uint32_t base, limit, dest;
		uint32_t val = milan_df_read32(iodie, inst, 0,
		    AMDZEN_DF_F0_CFGMAP(i));

		/*
		 * If a configuration map entry doesn't have both read and write
		 * enabled, then we treat that as something that we should skip.
		 * There is no validity bit here, so this is the closest that we
		 * can come to.
		 */
		if (AMDZEN_DF_F0_GET_CFGMAP_RE(val) == 0 ||
		    AMDZEN_DF_F0_GET_CFGMAP_WE(val) == 0) {
			continue;
		}

		base = AMDZEN_DF_F0_GET_CFGMAP_BUS_BASE(val);
		limit = AMDZEN_DF_F0_GET_CFGMAP_BUS_LIMIT(val);
		dest = AMDZEN_DF_F0_GET_CFGMAP_DEST_ID(val);

		ioms = milan_fabric_find_ioms(fabric, dest);
		if (ioms == NULL) {
			cmn_err(CE_WARN, "PCI Bus fabric rule %u [0x%x, 0x%x] "
			    "maps to unknown fabric id: 0x%x", i, base, limit,
			    dest);
			continue;
		}
		imp = &ioms->mio_memlists;

		if (base != ioms->mio_pci_busno) {
			cmn_err(CE_PANIC, "unexpected bus routing rule, rule "
			    "base 0x%x does not match destination base: 0x%x",
			    base, ioms->mio_pci_busno);
		}

		/*
		 * We assign the IOMS's PCI bus as used and all the remainin as
		 * available.
		 */
		ret = xmemlist_add_span(&imp->im_pool, base, 1,
		    &imp->im_bus_used, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);

		if (base == limit)
			continue;
		ret = xmemlist_add_span(&imp->im_pool, base + 1, limit - base,
		    &imp->im_bus_avail, 0);
		VERIFY3S(ret, ==, MEML_SPANOP_OK);
	}
}

typedef struct milan_route_io {
	uint32_t	mri_per_ioms;
	uint32_t	mri_next_base;
	uint32_t	mri_cur;
	uint32_t	mri_last_ioms;
	uint32_t	mri_bases[AMDZEN_DF_F0_MAX_IO_RULES];
	uint32_t	mri_limits[AMDZEN_DF_F0_MAX_IO_RULES];
	uint32_t	mri_dests[AMDZEN_DF_F0_MAX_IO_RULES];
} milan_route_io_t;

static int
milan_io_ports_allocate(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	int ret;
	milan_route_io_t *mri = arg;
	ioms_memlists_t *imp = &ioms->mio_memlists;

	/*
	 * The primary FCH (e.g. the IOMS that has the FCH on iodie 0) always
	 * has a base of zero so we can cover the legacy I/O ports.
	 */
	if ((ioms->mio_flags & MILAN_IOMS_F_HAS_FCH) != 0 &&
	    iodie->mi_node_id == 0) {
		mri->mri_bases[mri->mri_cur] = 0;
	} else {
		mri->mri_bases[mri->mri_cur] = mri->mri_next_base;
		mri->mri_next_base += mri->mri_per_ioms;

		mri->mri_last_ioms = mri->mri_cur;
	}

	mri->mri_limits[mri->mri_cur] = mri->mri_bases[mri->mri_cur] +
	    mri->mri_per_ioms - 1;
	mri->mri_dests[mri->mri_cur] = ioms->mio_fabric_id;

	/*
	 * We purposefully assign all of the I/O ports here and not later on as
	 * we want to make sure that we don't end up recording the fact that
	 * someone has the rest of the ports that aren't available on x86. XXX
	 * Where do we want to filter out the fact that we don't want to assign
	 * the first set of ports. There is some logic for this in pci_boot.c.
	 */
	ret = xmemlist_add_span(&imp->im_pool, mri->mri_bases[mri->mri_cur],
	    mri->mri_limits[mri->mri_cur] - mri->mri_bases[mri->mri_cur] + 1,
	    &imp->im_io_avail, 0);
	VERIFY3S(ret, ==, MEML_SPANOP_OK);

	mri->mri_cur++;
	return (0);
}

/*
 * The I/O ports effectively use the RE and WE bits as enable bits. Therefore we
 * need to make sure to set the limit register before setting the base register
 * for a given entry.
 */
static int
milan_io_ports_assign(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	milan_route_io_t *mri = arg;

	for (uint32_t i = 0; i < mri->mri_cur; i++) {
		uint32_t base = 0, limit = 0;

		base = AMDZEN_DF_F0_SET_IO_BASE_RE(base, 1);
		base = AMDZEN_DF_F0_SET_IO_BASE_WE(base, 1);
		base = AMDZEN_DF_F0_SET_IO_BASE_BASE(base,
		    mri->mri_bases[i] >> AMDZEN_DF_F0_IO_BASE_SHIFT);

		limit = AMDZEN_DF_F0_SET_IO_LIMIT_DEST_ID(limit,
		    mri->mri_dests[i]);
		limit = AMDZEN_DF_F0_SET_IO_LIMIT_LIMIT(limit,
		    mri->mri_limits[i] >> AMDZEN_DF_F0_IO_LIMIT_SHIFT);

		milan_df_bcast_write32(iodie, 0, AMDZEN_DF_F0_IO_LIMIT(i),
		    limit);
		milan_df_bcast_write32(iodie, 0, AMDZEN_DF_F0_IO_BASE(i), base);
	}

	return (0);
}

/*
 * We need to set up the I/O port mappings to all IOMS instances. Like with
 * other things, for the moment we do the simple thing and make them shared
 * equally across all units. However, there are a few gotchas:
 *
 *  o The first 4 KiB of I/O ports are considered 'legacy'/'compatibility' I/O.
 *    This means that they need to go to the IOMS with the FCH.
 *  o The I/O space base and limit registers all have a 12-bit granularity.
 *  o The DF actually supports 24-bits of I/O space
 *  o x86 cores only support 16-bits of I/O space
 *  o There are only 8 routing rules here, so 1/IOMS in a 2P system
 *
 * So with all this in mind, we're going to do the following:
 *
 *  o Each IOMS will be assigned a single route (whether there are 4 or 8)
 *  o We're basically going to assign the 16-bits of ports evenly between all
 *    found IOMS instances.
 *  o Yes, this means the FCH is going to lose some I/O ports relative to
 *    everything else, but that's fine. If we're constrained on I/O ports, we're
 *    in trouble.
 *  o Because we have a limited number of entries, the FCH on node 0 (e.g. the
 *    primary one) has the region starting at 0.
 *  o Whoever is last gets all the extra I/O ports filling up the 1 MiB.
 */
static void
milan_route_io_ports(milan_fabric_t *fabric)
{
	milan_route_io_t mri;
	uint32_t total_size = UINT16_MAX + 1;

	bzero(&mri, sizeof (mri));
	mri.mri_per_ioms = total_size / fabric->mf_total_ioms;
	VERIFY3U(mri.mri_per_ioms, >=, 1 << AMDZEN_DF_F0_IO_BASE_SHIFT);
	mri.mri_next_base = mri.mri_per_ioms;

	/*
	 * First walk each IOMS to assign things evenly. We'll come back and
	 * then find the last non-primary one and that'll be the one that gets a
	 * larger limit.
	 */
	(void) milan_fabric_walk_ioms(fabric, milan_io_ports_allocate, &mri);
	mri.mri_limits[mri.mri_last_ioms] = AMDZEN_MAX_IO_LIMIT;
	(void) milan_fabric_walk_iodie(fabric, milan_io_ports_assign, &mri);
}

typedef struct milan_route_mmio {
	uint32_t	mrm_cur;
	uint32_t	mrm_mmio32_base;
	uint32_t	mrm_mmio32_chunks;
	uint32_t	mrm_fch_base;
	uint32_t	mrm_fch_chunks;
	uint64_t	mrm_mmio64_base;
	uint64_t	mrm_mmio64_chunks;
	uint64_t	mrm_bases[AMDZEN_DF_F0_MAX_MMIO_RULES];
	uint64_t	mrm_limits[AMDZEN_DF_F0_MAX_MMIO_RULES];
	uint32_t	mrm_dests[AMDZEN_DF_F0_MAX_MMIO_RULES];
} milan_route_mmio_t;

/*
 * We allocate two rules per device. The first is a 32-bit rule. The second is
 * then its corresponding 64-bit.
 */
static int
milan_mmio_allocate(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, void *arg)
{
	int ret;
	milan_route_mmio_t *mrm = arg;
	const uint32_t mmio_gran = 1 << AMDZEN_DF_F0_MMIO_SHIFT;
	ioms_memlists_t *imp = &ioms->mio_memlists;

	/*
	 * The primary FCH is treated as a special case so that its 32-bit MMIO
	 * region is as close to the subtractive compat region as possible.
	 */
	if ((ioms->mio_flags & MILAN_IOMS_F_HAS_FCH) != 0 &&
	    iodie->mi_node_id == 0) {
		mrm->mrm_bases[mrm->mrm_cur] = mrm->mrm_fch_base;
		mrm->mrm_limits[mrm->mrm_cur] = mrm->mrm_fch_base;
		mrm->mrm_limits[mrm->mrm_cur] += mrm->mrm_fch_chunks *
		    mmio_gran - 1;
	} else {
		mrm->mrm_bases[mrm->mrm_cur] = mrm->mrm_mmio32_base;
		mrm->mrm_limits[mrm->mrm_cur] = mrm->mrm_mmio32_base;
		mrm->mrm_limits[mrm->mrm_cur] += mrm->mrm_mmio32_chunks *
		    mmio_gran - 1;
		mrm->mrm_mmio32_base += mrm->mrm_mmio32_chunks *
		    mmio_gran;
	}

	mrm->mrm_dests[mrm->mrm_cur] = ioms->mio_fabric_id;
	ret = xmemlist_add_span(&imp->im_pool, mrm->mrm_bases[mrm->mrm_cur],
	    mrm->mrm_limits[mrm->mrm_cur] - mrm->mrm_bases[mrm->mrm_cur] + 1,
	    &imp->im_mmio_avail, 0);
	VERIFY3S(ret, ==, MEML_SPANOP_OK);

	mrm->mrm_cur++;

	/*
	 * Now onto the 64-bit register, which is thankfully uniform for all
	 * IOMS entries.
	 */
	mrm->mrm_bases[mrm->mrm_cur] = mrm->mrm_mmio64_base;
	mrm->mrm_limits[mrm->mrm_cur] = mrm->mrm_mmio64_base +
	    mrm->mrm_mmio64_chunks * mmio_gran - 1;
	mrm->mrm_mmio64_base += mrm->mrm_mmio64_chunks * mmio_gran;
	mrm->mrm_dests[mrm->mrm_cur] = ioms->mio_fabric_id;

	ret = xmemlist_add_span(&imp->im_pool, mrm->mrm_bases[mrm->mrm_cur],
	    mrm->mrm_limits[mrm->mrm_cur] - mrm->mrm_bases[mrm->mrm_cur] + 1,
	    &imp->im_mmio_avail, 0);
	VERIFY3S(ret, ==, MEML_SPANOP_OK);

	mrm->mrm_cur++;

	return (0);
}

/*
 * We need to set the three registers that make up an MMIO rule. Importantly we
 * set the control register last as that's what contains the effective enable
 * bits.
 */
static int
milan_mmio_assign(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, void *arg)
{
	milan_route_mmio_t *mrm = arg;

	for (uint32_t i = 0; i < mrm->mrm_cur; i++) {
		uint32_t base, limit;
		uint32_t ctrl = 0;

		base = mrm->mrm_bases[i] >> AMDZEN_DF_F0_MMIO_SHIFT;
		limit = mrm->mrm_limits[i] >> AMDZEN_DF_F0_MMIO_SHIFT;
		ctrl = AMDZEN_Z2_3_DF_F0_SET_MMIO_CTRL_RE(ctrl, 1);
		ctrl = AMDZEN_Z2_3_DF_F0_SET_MMIO_CTRL_WE(ctrl, 1);
		ctrl = AMDZEN_Z2_3_DF_F0_SET_MMIO_CTRL_DEST_ID(ctrl,
		    mrm->mrm_dests[i]);

		milan_df_bcast_write32(iodie, 0, AMDZEN_DF_F0_MMIO_BASE(i),
		    base);
		milan_df_bcast_write32(iodie, 0, AMDZEN_DF_F0_MMIO_LIMIT(i),
		    limit);
		milan_df_bcast_write32(iodie, 0, AMDZEN_Z2_3_DF_F0_MMIO_CTRL(i),
		    ctrl);
	}

	return (0);
}

/*
 * Routing MMIO is both important and a little complicated mostly due to the how
 * x86 actually has historically split MMIO between the below 4 GiB region and
 * the above 4 GiB region. In addition, there are only 16 routing rules that we
 * can write, which means we get a maximum of 2 routing rules per IOMS (mostly
 * because we're being lazy).
 *
 * The below 4 GiB space is split due to the compat region
 * (MILAN_PHYSADDR_COMPAT_MMIO) and the presence of the PCIe configuration space
 * at (MILAN_PHYSADDR_PCIECFG). The way we divide up the lower region is simple:
 *
 *   o The region between TOM and PCIe is split evenly among our non-primary
 *     entry. In a 1P system this results in 256 MiB per IOMS. This is divided
 *     more awkwardly when we have a 2P system, but it's fine.
 *
 *   o The 32-bit region between PCIe and compat is just always just given to
 *     the primary root bridge because there are nebulous suggestions that some
 *     of its other devices want to be mapped in that region.
 *
 * 64-bit space is simple. We find which is higher: TOM2 or the top of the
 * second hole (MILAN_PHYSADDR_MYSTERY_HOLE_END). From there, we just divide all
 * the remaining space between that and MILAN_PHYSADDR_MMIO_END. This is the
 * milan_fabric_t's mf_mmio64_base member.
 *
 * Our general assumption with this strategy is that 64-bit MMIO is plentiful
 * and that's what we'd rather assign and use.  This ties into the last bit
 * which is important: the hardware requires us to allocate in 16-bit chunks. So
 * we actually really treat all of our allocations as units of 64 KiB,
 * accepting that we're going to waste some 32-bit space.
 */
static void
milan_route_mmio(milan_fabric_t *fabric)
{
	uint32_t mmio32_size, fch_size;
	uint64_t mmio64_size;
	uint_t nioms32;
	milan_route_mmio_t mrm;
	const uint32_t mmio_gran = 1 << AMDZEN_DF_F0_MMIO_SHIFT;

	VERIFY(IS_P2ALIGNED(fabric->mf_tom, mmio_gran));
	VERIFY3U(MILAN_PHYSADDR_PCIECFG, >, fabric->mf_tom);
	mmio32_size = MILAN_PHYSADDR_PCIECFG - fabric->mf_tom;
	nioms32 = fabric->mf_total_ioms - 1;
	VERIFY3U(mmio32_size, >, nioms32 * mmio_gran);

	VERIFY(IS_P2ALIGNED(fabric->mf_mmio64_base, mmio_gran));
	VERIFY3U(MILAN_PHYSADDR_MMIO_END, >, fabric->mf_mmio64_base);
	mmio64_size = MILAN_PHYSADDR_MMIO_END - fabric->mf_mmio64_base;
	VERIFY3U(mmio64_size, >,  fabric->mf_total_ioms * mmio_gran);

	VERIFY(IS_P2ALIGNED(MILAN_PHYSADDR_PCIECFG_END, mmio_gran));
	VERIFY3U(MILAN_PHYSADDR_COMPAT_MMIO, >, MILAN_PHYSADDR_PCIECFG_END);
	fch_size = MILAN_PHYSADDR_COMPAT_MMIO - MILAN_PHYSADDR_PCIECFG_END;

	bzero(&mrm, sizeof (mrm));
	mrm.mrm_mmio32_base = fabric->mf_tom;
	mrm.mrm_mmio32_chunks = mmio32_size / mmio_gran / nioms32;
	mrm.mrm_fch_base = MILAN_PHYSADDR_PCIECFG_END;
	mrm.mrm_fch_chunks = fch_size / mmio_gran;
	mrm.mrm_mmio64_base = fabric->mf_mmio64_base;
	mrm.mrm_mmio64_chunks = mmio64_size / mmio_gran / fabric->mf_total_ioms;

	(void) milan_fabric_walk_ioms(fabric, milan_mmio_allocate, &mrm);
	(void) milan_fabric_walk_iodie(fabric, milan_mmio_assign, &mrm);
}

/*
 * This is a request that we take resources from a given IOMS root port and
 * basically give what remains and hasn't been allocated to PCI. This is a bit
 * of a tricky process as we want to both:
 *
 *  1. Give everything that's currently available to PCI; however, it needs
 *     memlists that are allocated with kmem due to how PCI memlists work.
 *  2. We need to move everything that we're giving to PCI into our used list
 *     just for our own tracking purposes.
 */
struct memlist *
milan_fabric_pci_subsume(uint32_t bus, pci_prd_rsrc_t rsrc)
{
	milan_ioms_t *ioms;
	ioms_memlists_t *imp;
	milan_fabric_t *fabric = &milan_fabric;
	struct memlist **avail, **used, *ret;

	ioms = milan_fabric_find_ioms_by_bus(fabric, bus);
	if (ioms == NULL) {
		return (NULL);
	}

	imp = &ioms->mio_memlists;
	mutex_enter(&imp->im_lock);
	switch (rsrc) {
	case PCI_PRD_R_IO:
		avail = &imp->im_io_avail;
		used = &imp->im_io_used;
		break;
	case PCI_PRD_R_MMIO:
		avail = &imp->im_mmio_avail;
		used = &imp->im_mmio_used;
		break;
	case PCI_PRD_R_BUS:
		avail = &imp->im_bus_avail;
		used = &imp->im_bus_used;
		break;
	default:
		mutex_exit(&imp->im_lock);
		return (NULL);
	}

	/*
	 * If there are no resources, that may be because there never were any
	 * or they had already been handed out.
	 */
	if (*avail == NULL) {
		mutex_exit(&imp->im_lock);
		return (NULL);
	}

	/*
	 * We have some resources available for this PCI root complex. In this
	 * particular case, we need to first duplicate these using kmem and then
	 * we can go ahead and move all of these to the used list.
	 */
	ret = memlist_kmem_dup(*avail, KM_SLEEP);

	/*
	 * XXX This ends up not really coalescing ranges, but maybe that's fine.
	 */
	while (*avail != NULL) {
		struct memlist *to_move = *avail;
		memlist_del(to_move, avail);
		memlist_insert(to_move, used);
	}

	mutex_exit(&imp->im_lock);
	return (ret);
}

/*
 * Here we are going through bridges and need to start setting them up with the
 * various features that we care about. Most of these are an attempt to have
 * things set up so PCIe enumeration can meaningfully actually use these. The
 * exact set of things required is ill-defined. Right now this includes:
 *
 *   o Enabling the bridges such that they can actually allow software to use
 *     them. XXX Though really we should disable DMA until such a time as we're
 *     OK with that.
 *
 *   o Changing settings that will allow the links to actually flush TLPs when
 *     the link goes down.
 */
static int
milan_fabric_init_bridges(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    milan_pcie_bridge_t *bridge, void *arg)
{
	uint32_t val;
	boolean_t hide;

	/*
	 * XXX We need to determine whether or not this bridge should be
	 * considered visible. This is messy. Ideally, we'd just have every
	 * bridge be visible; however, life isn't that simple because convincing
	 * the PCIe engine that it should actually allow for completion timeouts
	 * to function as expected. The current masking rules are based on what
	 * we have learned from trial and error works.
	 *
	 * More specifically, if we can get a bridge that the SMU thinks is
	 * hotpluggable or there's a device present then we're in good shape.
	 * What's interesting is that the hotpluggable bit seems to function at
	 * the PCIe port level. Otherwise, if there is no device present, then
	 * there's not much we can do.
	 */
	hide = ((port->mpp_flags & MILAN_PCIE_PORT_F_HAS_HOTPLUG) == 0 &&
	    ((bridge->mpb_flags & MILAN_PCIE_BRIDGE_F_MAPPED) == 0 ||
	    bridge->mpb_engine->zde_config.zdc_pcie.zdcp_link_train !=
	    MILAN_DXIO_PCIE_SUCCESS));
	if (hide) {
		bridge->mpb_flags |= MILAN_PCIE_BRIDGE_F_HIDDEN;
	}

	val = milan_iohc_pcie_read32(iodie, bridge,
	    MILAN_IOHC_R_SMN_BRIDGE_CNTL_PCIE);
	val = MILAN_IOHC_R_BRIDGE_CNTL_SET_CRS_ENABLE(val, 1);
	if (hide) {
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_BRIDGE_DISABLE(val, 1);
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_BUS_MASTER(val, 1);
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_CFG(val, 1);
	} else {
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_BRIDGE_DISABLE(val, 0);
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_BUS_MASTER(val, 0);
		val = MILAN_IOHC_R_BRIDGE_CNTL_SET_DISABLE_CFG(val, 0);
	}
	milan_iohc_pcie_write32(iodie, bridge,
	    MILAN_IOHC_R_SMN_BRIDGE_CNTL_PCIE, val);

	val = milan_bridge_port_read32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_TX_CNTL);
	val = MILAN_PCIE_PORT_R_SET_TX_CNTL_TLP_FLUSH_DOWN_DIS(val, 0);
	milan_bridge_port_write32(iodie, bridge, MILAN_PCIE_PORT_R_SMN_TX_CNTL,
	    val);

	return (0);
}

/*
 * This is a companion to milan_fabric_init_bidges, that operates on the PCIe
 * port level before we get to the individual bridge. This initialization
 * generally is required to ensure that each port (regardless of whether it's
 * hidden or not) is able to properly generate an all 1s response.
 */
static int
milan_fabric_init_pcie_ports(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    void *arg)
{
	uint32_t val;

	val = milan_pcie_core_read32(iodie, port,
	    MILAN_PCIE_CORE_R_SMN_CI_CNTL);
	val = MILAN_PCIE_CORE_R_SET_CI_CNTL_LINK_DOWN_CTO_EN(val, 1);
	val = MILAN_PCIE_CORE_R_SET_CI_CNTL_IGN_LINK_DOWN_CTO_ERR(val, 1);
	milan_pcie_core_write32(iodie, port, MILAN_PCIE_CORE_R_SMN_CI_CNTL,
	    val);

	/*
	 * Program the unit ID for this device's SDP port.
	 */
	val = milan_pcie_core_read32(iodie, port,
	    MILAN_PICE_CORE_R_SMN_SDP_CTRL);
	val = MILAN_PCIE_CORE_R_SET_SDP_CTRL_PORT_ID(val, port->mpp_sdp_port);
	val = MILAN_PCIE_CORE_R_SET_SDP_CTRL_UNIT_ID(val, port->mpp_sdp_unit);
	milan_pcie_core_write32(iodie, port, MILAN_PICE_CORE_R_SMN_SDP_CTRL,
	    val);

	/*
	 * The IOMMUL1 does not have an instance for the on-the side WAFL lanes.
	 * So if our bridge number has reached the maximum number of bridges,
	 * we're on that port, the rest of these should not be touched.
	 */
	if (port->mpp_portno >= MILAN_IOMS_MAX_PCIE_BRIDGES)
		return (0);

	val = milan_iommul1_read32(iodie, ioms, port->mpp_portno,
	    MILAN_IOMMUL1_R_SMN_L1_CTL1);
	val = MILAN_IOMMUL1_R_SET_L1_CTL1_ORDERING(val, 1);
	milan_iommul1_write32(iodie, ioms, port->mpp_portno,
	    MILAN_IOMMUL1_R_SMN_L1_CTL1, val);

	return (0);
}

typedef struct {
	milan_ioms_t *pbc_ioms;
	uint8_t pbc_busoff;
} pci_bus_counter_t;

static int
milan_fabric_hack_bridges_cb(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    milan_pcie_bridge_t *bridge, void *arg)
{
	uint8_t bus, secbus;
	pci_bus_counter_t *pbc = arg;

	bus = ioms->mio_pci_busno;
	if (pbc->pbc_ioms != ioms) {
		pbc->pbc_ioms = ioms;
		pbc->pbc_busoff = 1 + ARRAY_SIZE(milan_int_bridges);
		for (uint_t i = 0; i < ARRAY_SIZE(milan_int_bridges); i++) {
			const milan_bridge_info_t *info = &milan_int_bridges[i];
			pci_putb_func(bus, info->mpbi_dev, info->mpbi_func,
			    PCI_BCNF_PRIBUS, bus);
			pci_putb_func(bus, info->mpbi_dev, info->mpbi_func,
			    PCI_BCNF_SECBUS, bus + 1 + i);
			pci_putb_func(bus, info->mpbi_dev, info->mpbi_func,
			    PCI_BCNF_SUBBUS, bus + 1 + i);

		}
	}

	if ((bridge->mpb_flags & MILAN_PCIE_BRIDGE_F_HIDDEN) != 0) {
		return (0);
	}

	secbus = bus + pbc->pbc_busoff;

	pci_putb_func(bus, bridge->mpb_device, bridge->mpb_func,
	    PCI_BCNF_PRIBUS, bus);
	pci_putb_func(bus, bridge->mpb_device, bridge->mpb_func,
	    PCI_BCNF_SECBUS, secbus);
	pci_putb_func(bus, bridge->mpb_device, bridge->mpb_func,
	    PCI_BCNF_SUBBUS, secbus);

	pbc->pbc_busoff++;
	return (0);
}

/*
 * XXX This whole function exists to workaround deficiencies in software and
 * basically try to ape parts of the PCI firmware spec. The OS should natively
 * handle this. In particular, we currently do the following:
 *
 *   o Program a single downstream bus onto each root port. We can only get away
 *     with this because we know there are no other bridges right now. This
 *     cannot be a long term solution, though I know we will be temped to make
 *     it one. I'm sorry future us.
 */
static void
milan_fabric_hack_bridges(milan_fabric_t *fabric)
{
	pci_bus_counter_t c;
	bzero(&c, sizeof (c));

	milan_fabric_walk_bridge(fabric, milan_fabric_hack_bridges_cb, &c);
}

/*
 * Allocate and initialize the hotplug table. The return value here is used to
 * indicate whether or not the platform has hotplug and thus should continue or
 * not with actual set up.
 */
static boolean_t
milan_smu_hotplug_data_init(milan_fabric_t *fabric)
{
	ddi_dma_attr_t attr;
	milan_hotplug_t *hp = &fabric->mf_hotplug;
	const smu_hotplug_entry_t *entry;
	pfn_t pfn;
	boolean_t cont;

	milan_smu_dma_attr(&attr);
	hp->mh_alloc_len = MMU_PAGESIZE;
	hp->mh_table = contig_alloc(MMU_PAGESIZE, &attr, MMU_PAGESIZE, 1);
	bzero(hp->mh_table, MMU_PAGESIZE);
	pfn = hat_getpfnum(kas.a_hat, (caddr_t)hp->mh_table);
	hp->mh_pa = mmu_ptob((uint64_t)pfn);

	if (milan_board_type(fabric) == ETHANOL) {
		entry = ethanolx_hotplug_ents;
	} else {
		entry = gimlet_hotplug_ents;
	}

	cont = entry[0].se_slotno != SMU_HOTPLUG_ENT_LAST;

	/*
	 * The way the SMU takes this data table is that entries are indexed by
	 * physical slot number. We basically use an interim structure that's
	 * different so we can have a sparse table. In addition, if we find a
	 * device, update that info on its bridge.
	 */
	for (uint_t i = 0; entry[i].se_slotno != SMU_HOTPLUG_ENT_LAST; i++) {
		uint_t slot = entry[i].se_slotno;
		const smu_hotplug_map_t *map;
		milan_iodie_t *iodie;
		milan_ioms_t *ioms;
		milan_pcie_port_t *port;
		milan_pcie_bridge_t *bridge;

		hp->mh_table->smt_map[slot] = entry[i].se_map;
		hp->mh_table->smt_func[slot] = entry[i].se_func;
		hp->mh_table->smt_reset[slot] = entry[i].se_reset;

		/*
		 * Attempt to find the bridge this corresponds to. It should
		 * already have been mapped.
		 */
		map = &entry[i].se_map;
		iodie = &fabric->mf_socs[map->shm_die_id].ms_iodies[0];
		ioms = &iodie->mi_ioms[map->shm_tile_id % 4];
		port = &ioms->mio_pcie_ports[map->shm_tile_id / 4];
		bridge = &port->mpp_bridges[map->shm_port_id];

		cmn_err(CE_NOTE, "mapped entry %u to bridge %p", i, bridge);
		VERIFY((bridge->mpb_flags & MILAN_PCIE_BRIDGE_F_MAPPED) != 0);
		VERIFY((bridge->mpb_flags & MILAN_PCIE_BRIDGE_F_HIDDEN) == 0);
		bridge->mpb_flags |= MILAN_PCIE_BRIDGE_F_HOTPLUG;
		bridge->mpb_hp_type = map->shm_format;
		bridge->mpb_hp_slotno = slot;
		bridge->mpb_hp_smu_mask = entry[i].se_func.shf_mask;
	}

	return (cont);
}

/*
 * Determine the set of feature bits that should be enabled. If this is Ethanol,
 * use our hacky static versions for a moment.
 */
static uint32_t
milan_hotplug_bridge_features(milan_fabric_t *fabric,
    milan_pcie_bridge_t *bridge)
{
	uint32_t feats;

	if (milan_board_type(fabric) == ETHANOL) {
		if (bridge->mpb_hp_type == SMU_HP_ENTERPRISE_SSD) {
			return (ethanolx_pcie_slot_cap_entssd);
		} else {
			return (ethanolx_pcie_slot_cap_express);
		}
	}

	feats = PCIE_SLOTCAP_HP_SURPRISE | PCIE_SLOTCAP_HP_CAPABLE;

	/*
	 * The set of features we enable changes based on the type of hotplug
	 * mode. While Enterprise SSD uses a static set of features, the various
	 * ExpressModule modes have a mask register that is used to tell the SMU
	 * that it doesn't support a given feature. As such, we check for these
	 * masks to determine what to enable. Because these bits are used to
	 * turn off features in the SMU, we check for the absence of it (e.g. ==
	 * 0) to indicate that we should enable the feature.
	 */
	switch (bridge->mpb_hp_type) {
	case SMU_HP_ENTERPRISE_SSD:
		/*
		 * For Enterprise SSD the set of features that are supported are
		 * considered a constant and this doesn't really vary based on
		 * the board. There is no power control, just surprise hotplug
		 * capabilities. Apparently in this mode there is no SMU command
		 * completion.
		 */
		return (feats | PCIE_SLOTCAP_NO_CMD_COMP_SUPP);
	case SMU_HP_EXPRESS_MODULE_A:
		if ((bridge->mpb_hp_smu_mask & SMU_ENTA_ATTNSW) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_BUTTON;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTA_EMILS) == 0 ||
		    (bridge->mpb_hp_smu_mask & SMU_ENTA_EMIL) == 0) {
			feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTA_PWREN) == 0) {
			feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTA_ATTNLED) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTA_PWRLED) == 0) {
			feats |= PCIE_SLOTCAP_PWR_INDICATOR;
		}
		break;
	case SMU_HP_EXPRESS_MODULE_B:
		if ((bridge->mpb_hp_smu_mask & SMU_ENTB_ATTNSW) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_BUTTON;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTB_EMILS) == 0 ||
		    (bridge->mpb_hp_smu_mask & SMU_ENTB_EMIL) == 0) {
			feats |= PCIE_SLOTCAP_EMI_LOCK_PRESENT;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTB_PWREN) == 0) {
			feats |= PCIE_SLOTCAP_POWER_CONTROLLER;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTB_ATTNLED) == 0) {
			feats |= PCIE_SLOTCAP_ATTN_INDICATOR;
		}

		if ((bridge->mpb_hp_smu_mask & SMU_ENTB_PWRLED) == 0) {
			feats |= PCIE_SLOTCAP_PWR_INDICATOR;
		}
		break;
	default:
		return (0);
	}

	return (feats);
}


/*
 * At this point we need to go through and prep all hotplug-capable bridges.
 * This means setting up the following:
 *
 *   o Setting the appropriate slot capabilities.
 *   o Setting the slot's actual number in PCIe and in a secondary SMN location.
 *   o Setting control bits in the PCIe IP to ensure we don't enter loopback
 *     mode and some amount of other state machine control.
 *   o Making sure that power faults work.
 */
static int
milan_hotplug_bridge_init(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    milan_pcie_bridge_t *bridge, void *arg)
{
	uint32_t val;
	uint32_t slot_mask;

	/*
	 * Skip over all non-hotplug slots and the simple presence mode. Though
	 * one has to ask oneself, why have hotplug if you're going to use the
	 * simple presence mode.
	 */
	if ((bridge->mpb_flags & MILAN_PCIE_BRIDGE_F_HOTPLUG) == 0 ||
	    bridge->mpb_hp_type == SMU_HP_PRESENCE_DETECT) {
		return (0);
	}

	/*
	 * Set the hotplug slot information in the PCIe IP, presumably so that
	 * it'll do something useful for the SMU.
	 */
	val = milan_bridge_port_read32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_HP_CNTL);
	val = MILAN_PCIE_PORT_R_SET_HP_CNTL_SLOT(val, bridge->mpb_hp_slotno);
	val = MILAN_PCIE_PORT_R_SET_HP_CNTL_ACTIVE(val, 1);
	milan_bridge_port_write32(iodie, bridge, MILAN_PCIE_PORT_R_SMN_HP_CNTL,
	    val);

	/*
	 * This register is apparently set to ensure that we don't remain in the
	 * detect state machine state.
	 */
	val = milan_bridge_port_read32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_LC_CNTL5);
	val = MILAN_PCIE_PORT_R_SET_LC_CNTL5_WAIT_DETECT(val, 0);
	milan_bridge_port_write32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_LC_CNTL5, val);

	/*
	 * This ensures the port can't enter loopback mode.
	 */
	val = milan_bridge_port_read32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_TRAIN_CNTL);
	val = MILAN_PCIE_PORT_R_SET_TRAIN_CNTL_TRAIN_DIS(val, 1);
	milan_bridge_port_write32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_TRAIN_CNTL, val);

	/*
	 * Make sure that power faults can actually work (in theory).
	 */
	val = milan_bridge_port_read32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_PORT_CNTL);
	val = MILAN_PCIE_PORT_R_SET_PORT_CNTL_PWRFLT_EN(val, 1);
	milan_bridge_port_write32(iodie, bridge,
	    MILAN_PCIE_PORT_R_SMN_PORT_CNTL, val);

	/*
	 * Go through and set up the slot capabilities register. In our case
	 * we've already filtered out the non-hotplug capable bridges. To
	 * determine the set of hotplug features that should be set here we
	 * derive that from the acutal hoptlug entities. Because one is required
	 * to give the SMU a list of functions to mask, the umasked bits tells
	 * us what to enable as features here.
	 */
	slot_mask = PCIE_SLOTCAP_ATTN_BUTTON | PCIE_SLOTCAP_POWER_CONTROLLER |
	    PCIE_SLOTCAP_MRL_SENSOR | PCIE_SLOTCAP_ATTN_INDICATOR |
	    PCIE_SLOTCAP_PWR_INDICATOR | PCIE_SLOTCAP_HP_SURPRISE |
	    PCIE_SLOTCAP_HP_CAPABLE | PCIE_SLOTCAP_EMI_LOCK_PRESENT |
	    PCIE_SLOTCAP_NO_CMD_COMP_SUPP;

	val = pci_getl_func(ioms->mio_pci_busno, bridge->mpb_device,
	    bridge->mpb_func, MILAN_BRIDGE_R_PCI_SLOT_CAP);
	val &= ~(PCIE_SLOTCAP_PHY_SLOT_NUM_MASK <<
	    PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT);
	val |= bridge->mpb_hp_slotno << PCIE_SLOTCAP_PHY_SLOT_NUM_SHIFT;
	val &= ~slot_mask;
	val |= milan_hotplug_bridge_features(fabric, bridge);
	pci_putl_func(ioms->mio_pci_busno, bridge->mpb_device,
	    bridge->mpb_func, MILAN_BRIDGE_R_PCI_SLOT_CAP, val);

	return (0);
}

/*
 * This is an analogue to the above functions; however, it operates on the PCIe
 * port basis rather than the individual bridge. This mostly includes:
 *   o Making sure that there are no holds on link training on any port.
 *   o Ensuring that presence detection is based on an 'OR'
 *
 * XXX SMN_NBIO0PCIE0_SWRST_CONTROL_6_A
 */
static int
milan_hotplug_port_init(milan_fabric_t *fabric, milan_soc_t *soc,
    milan_iodie_t *iodie, milan_ioms_t *ioms, milan_pcie_port_t *port,
    void *arg)
{
	uint32_t val;

	/*
	 * Nothing to do if there's no hotplug.
	 */
	if ((port->mpp_flags & MILAN_PCIE_PORT_F_HAS_HOTPLUG) == 0) {
		return (0);
	}

	/*
	 * While there are reserved bits in this register, it appears that
	 * reserved bits are ignored and always set to zero.
	 */
	milan_pcie_core_write32(iodie, port, MILAN_PCIE_CORE_R_SMN_SWRST_CNTL6,
	    0);

	val = milan_pcie_core_read32(iodie, port, MILAN_PCIE_CORE_R_SMN_PRES);
	val = MILAN_PCIE_CORE_R_SET_PRES_MODE(val,
	    MILAN_PCIE_CORE_R_PRES_MODE_OR);
	milan_pcie_core_write32(iodie, port, MILAN_PCIE_CORE_R_SMN_PRES, val);

	return (0);
}

/*
 * XXX This is a total hack. Unfortunately the SMU relies on x86 software to
 * actually set the i2c clock up to something expected for it. Temporarily do
 * this the max power way.
 */
static boolean_t
xxx_fixup_i2c_clock(void)
{
	void *va = device_arena_alloc(MMU_PAGESIZE, VM_SLEEP);
	pfn_t pfn = mmu_btop(0xfedc2000);
	hat_devload(kas.a_hat, va, MMU_PAGESIZE, pfn,
	    PROT_READ | PROT_WRITE | HAT_STRICTORDER,
	    HAT_LOAD_LOCK | HAT_LOAD_NOCONSIST);
	*(uint32_t *)va = 0x63;
	hat_unload(kas.a_hat, va, MMU_PAGESIZE, HAT_UNLOAD_UNLOCK);
	device_arena_free(va, MMU_PAGESIZE);

	return (B_TRUE);
}

/*
 * Begin the process of initializing the hotplug subsystem with the SMU. In
 * particular we need to do the following steps:
 *
 *  o Send a series of commands to set up the i2c switches in general. These
 *    correspond to the various bit patterns that we program in the function
 *    payload.
 *
 *  o Set up and send across our hotplug table.
 *
 *  o Finish setting up the bridges to be ready for hotplug.
 *
 *  o Actually tell it to start.
 *
 * Unlike with DXIO initialization, it appears that hotplug initialization only
 * takes place on the primary SMU. In some ways, this makes some sense because
 * the hotplug table has information about which dies and sockets are used for
 * what and further, only the first socket ever is connected to the hotplug i2c
 * bus; however, it is still also a bit mysterious.
 */
static boolean_t
milan_hotplug_init(milan_fabric_t *fabric)
{
	milan_hotplug_t *hp = &fabric->mf_hotplug;
	milan_iodie_t *iodie = &fabric->mf_socs[0].ms_iodies[0];

	/*
	 * These represent the addresses that we need to program in the SMU.
	 * Strictly speaking, the lower 8-bits represents the addresses that the
	 * SMU seems to expect. The upper byte is a bit more of a mystery;
	 * however, it does correspond to the expected values that AMD roughly
	 * documents for 5-bit bus segment value which is the shf_i2c_bus member
	 * of the smu_hotplug_function_t.
	 */
	const uint32_t i2c_addrs[4] = { 0x70, 0x171, 0x272, 0x373 };

	if (!milan_smu_hotplug_data_init(fabric)) {
		/*
		 * This case is used to indicate that there was nothing in
		 * particular that needed hotplug. Therefore, we don't bother
		 * trying to tell the SMU about it.
		 */
		return (B_TRUE);
	}

	for (uint_t i = 0; i < ARRAY_SIZE(i2c_addrs); i++) {
		if (!milan_smu_rpc_i2c_switch(iodie, i2c_addrs[i])) {
			return (B_FALSE);
		}
	}

	if (!milan_smu_rpc_give_address(iodie, hp->mh_pa)) {
		return (B_FALSE);
	}

	if (!milan_smu_rpc_send_hotplug_table(iodie)) {
		return (B_FALSE);
	}

	/*
	 * Go through now and set up bridges for hotplug data. Honor the spirit
	 * of the old world by doing this after we send the hotplug table, but
	 * before we enable things. It's unclear if the order is load bearing or
	 * not.
	 */
	(void) milan_fabric_walk_pcie_port(fabric, milan_hotplug_port_init,
	    NULL);
	(void) milan_fabric_walk_bridge(fabric, milan_hotplug_bridge_init,
	    NULL);

	if (!milan_smu_rpc_hotplug_flags(iodie, 0)) {
		return (B_FALSE);
	}

	/*
	 * XXX This is an unfortunate bit. The SMU relies on someone else to
	 * have set the actual state of the i2c clock.
	 */
	if (!xxx_fixup_i2c_clock()) {
		return (B_FALSE);
	}

	if (!milan_smu_rpc_start_hotplug(iodie, B_FALSE, 0)) {
		return (B_FALSE);
	}

	/*
	 * XXX We should probably reset the slot a little bit before we end up
	 * handing things over to others.
	 */

	return (B_TRUE);
}

/*
 * This is the main place where we basically do everything that we need to do to
 * get the PCIe engine up and running.
 */
void
milan_fabric_init(void)
{
	milan_fabric_t *fabric = &milan_fabric;

	/*
	 * XXX We're missing initialization of some different pieces of the data
	 * fabric here. While some of it like scrubbing should be done as part
	 * of the memory controller driver and broader policy rather than all
	 * here right now.
	 */

	/*
	 * When we come out of reset, the PSP and/or SMU have set up our DRAM
	 * routing rules and the PCI bus routing rules. We need to go through
	 * and save this information as well as set up I/O ports and MMIO. This
	 * process will also save our own allocations of these resources,
	 * allowing us to use them for our own purposes or for PCI.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_memlists, NULL);
	milan_route_pci_bus(fabric);
	milan_route_io_ports(fabric);
	milan_route_mmio(fabric);

	/*
	 * While DRAM training seems to have programmed the initial memory
	 * settings our boot CPU and the DF, it is not done on the various IOMS
	 * instances. It is up to us to program that across them all.
	 *
	 * XXX We still need to go back and figure out how to assign MMIO to
	 * IOMS instances and program the DF.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_tom, NULL);

	/*
	 * Let's set up PCIe. To lead off, let's make sure the system uses the
	 * right clock and let's start the process of dealing with the how
	 * configuration space retries should work, though this isn't sufficient
	 * for them to work.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_pcie_refclk, NULL);
	milan_fabric_walk_ioms(fabric, milan_fabric_init_pci_to, NULL);
	milan_fabric_walk_ioms(fabric, milan_fabric_init_iohc_features, NULL);

	/*
	 * There is a lot of different things that we have to do here. But first
	 * let me apologize in advance. The what here is weird and the why is
	 * non-existent. Effectively this is being done because either we were
	 * explicitly told to in the PPR or through other means. This is going
	 * to be weird and you have every right to complain.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_iohc_fch_link, NULL);
	milan_fabric_walk_ioms(fabric, milan_fabric_init_arbitration_ioms,
	    NULL);
	milan_fabric_walk_nbif(fabric, milan_fabric_init_arbitration_nbif,
	    NULL);
	milan_fabric_walk_ioms(fabric, milan_fabric_init_sdp_control, NULL);
	milan_fabric_walk_nbif(fabric, milan_fabric_init_nbif_syshub_dma,
	    NULL);

	/*
	 * XXX IOHC and friends clock gating.
	 */

	/*
	 * With that done, proceed to initialize the IOAPIC in each IOMS. While
	 * the FCH contains what the OS generally thinks of as the IOAPIC, we
	 * need to go through and deal with interrupt routing and how that
	 * interface with each of the northbridges here.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_ioapic, NULL);

	/*
	 * XXX For some reason programming IOHC::NB_BUS_NUM_CNTL is lopped in
	 * with the IOAPIC initialization. We may want to do this, but it can at
	 * least be its own function.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_bus_num, NULL);

	/*
	 * Go through and configure all of the straps for NBIF devices before
	 * they end up starting up.
	 *
	 * XXX There's a bunch we're punting on here and we'll want to make sure
	 * that we actually have the platform's config for this. But this
	 * includes doing things like:
	 *
	 *  o Enabling and Disabling devices visibility through straps and their
	 *    interrupt lines.
	 *  o Device multi-function enable, related PCI config space straps.
	 *  o Lots of clock gating
	 *  o Subsystem IDs
	 *  o GMI round robin
	 *  o BIFC stuff
	 */

	/* XXX Need a way to know which devs to enable on the board */
	milan_fabric_walk_nbif(fabric, milan_fabric_init_nbif_dev_straps, NULL);

	/*
	 * To wrap up the nBIF devices, go through and update the bridges here.
	 * We do two passes, one to get the NBIF instances and another to deal
	 * with the special instance that we believe is for the southbridge.
	 */
	milan_fabric_walk_ioms(fabric, milan_fabric_init_nbif_bridge, NULL);

	/*
	 * Go ahead and begin everything with DXIO and the SMU. In particular,
	 * we go through now and capture versions before we go through and do
	 * DXIO initialisation so we can use these. Currently we do all of our
	 * initial DXIO training for PCIe before we enable features that have to
	 * do with the SMU. XXX Cargo Culting.
	 */
	(void) milan_fabric_walk_iodie(fabric, milan_dump_versions, NULL);

	/*
	 * It's time to begin the dxio initialization process. We do this in a
	 * few different steps:
	 *
	 *   1. Program all of the misc. settings and variables that it wants
	 *	before we begin to load data anywhere.
	 *   2. Construct the per-die payloads that we require and assemble
	 *	them.
	 *   3. Actually program all of the different payloads we need.
	 *   4. Go back and set a bunch more things that probably can all be
	 *	done in (1) when we're done aping.
	 *   5. Make the appropriate sacrifice to the link training gods.
	 *   6. Kick off and process the state machines, one I/O die at a time.
	 *
	 * XXX htf do we want to handle errors
	 */
	if (milan_fabric_walk_iodie(fabric, milan_dxio_init, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: lasciate ogni "
		    "speranza voi che pcie");
		return;
	}

	if (milan_fabric_walk_iodie(fabric, milan_dxio_plat_data, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: no platform "
		    "data");
		return;
	}

	if (milan_fabric_walk_iodie(fabric, milan_dxio_load_data, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to load "
		    "data into dxio");
		return;
	}

	if (milan_fabric_walk_iodie(fabric, milan_dxio_more_conf, NULL) != 0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to do yet "
		    "more configuration");
		return;
	}

	if (milan_fabric_walk_iodie(fabric, milan_dxio_state_machine, NULL) !=
	    0) {
		cmn_err(CE_WARN, "DXIO Initialization failed: failed to walk "
		    "through the state machine");
		return;
	}

	cmn_err(CE_NOTE, "DXIO devices successfully trained?");

	/*
	 * Now that we have successfully trained devices, it's time to go
	 * through and set up the bridges so that way we can actual handle them
	 * aborting transactions and related.
	 */
	milan_fabric_walk_pcie_port(fabric, milan_fabric_init_pcie_ports, NULL);
	milan_fabric_walk_bridge(fabric, milan_fabric_init_bridges, NULL);

	/*
	 * XXX This is a terrible hack. We should really fix pci_boot.c and we
	 * better before we go to market.
	 */
	milan_fabric_hack_bridges(fabric);

	/*
	 * At this point, go talk to the SMU to actually initialize our hotplug
	 * support.
	 */
	if (!milan_hotplug_init(fabric)) {
		cmn_err(CE_WARN, "Eh, just don't unplug anything. I'm sure it "
		    "will be fine. Not like someone's going to come and steal "
		    "your silmarils");
	}

	/*
	 * XXX At some point, maybe not here, but before we really go too much
	 * futher we should lock all the various MMIO assignment registers,
	 * especially ones we don't intend to use.
	 */
}
