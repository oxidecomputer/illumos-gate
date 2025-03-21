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
 * Copyright 2024 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_SMU_IMPL_H
#define	_SYS_IO_MILAN_SMU_IMPL_H

/*
 * Definitions for the System Management Unit (SMU), which is probably the same
 * thing as the hidden core called MP1 in some documentation.  Its
 * responsibilities are mainly power and thermal management, but it also manages
 * the DXIO subsystem and PCIe hotplug.  The SMN regions used by the SMU are not
 * well documented and we make some conservative guesses about how its address
 * space is used.  We do know for certain that some of the individual
 * register/mailbox addresses are specific to processor families so we're also
 * conservative with the namespace.
 */

#include <sys/bitext.h>
#include <sys/types.h>
#include <sys/amdzen/smn.h>
#include <sys/io/milan/smu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMU RPC Operation Codes. Note, these are tied to firmware and therefore may
 * not be portable between Rome, Milan, or other processors.
 */
#define	MILAN_SMU_OP_TEST		0x01
#define	MILAN_SMU_OP_ENABLE_FEATURE	0x03
#define	MILAN_SMU_OP_DISABLE_FEATURE	0x04
#define	MILAN_SMU_OP_HAVE_AN_ADDRESS	0x05
#define	MILAN_SMU_OP_TOOLS_ADDRESS	0x06
#define	MILAN_SMU_OP_DEBUG_ADDRESS	0x07
#define	MILAN_SMU_OP_DXIO		0x08
#define	MILAN_SMU_OP_READ_DPM_WEIGHT	0x09
#define	MILAN_SMU_OP_DC_BOOT_CALIB	0x0c
#define	MILAN_SMU_OP_GET_BRAND_STRING	0x0d
#define	MILAN_SMU_OP_TX_PP_TABLE	0x10
#define	MILAN_SMU_OP_TX_PCIE_HP_TABLE	0x12
#define	MILAN_SMU_OP_START_HOTPLUG	0x18
#define	MILAN_SMU_OP_START_HOTPLUG_POLL		0x10
#define	MILAN_SMU_OP_START_HOTPLUG_FWFIRST	0x20
#define	MILAN_SMU_OP_START_HOTPLUG_RESET	0x40
#define	MILAN_SMU_OP_I2C_SWITCH_ADDR	0x1a
#define	MILAN_SMU_OP_SET_HOTPLUG_FLAGS	0x1d
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
#define	MILAN_SMU_OP_HAVE_A_HP_ADDRESS	0x47

/*
 * SMU features, as enabled via MILAN_SMU_OP_ENABLE_FEATURE. Note that not
 * all combinations of features will result in correct system behavior!
 */
#define	MILAN_SMU_FEATURE_DATA_CALCULATION			(1 << 0)
#define	MILAN_SMU_FEATURE_PPT					(1 << 1)
#define	MILAN_SMU_FEATURE_THERMAL_DESIGN_CURRENT		(1 << 2)
#define	MILAN_SMU_FEATURE_THERMAL				(1 << 3)
#define	MILAN_SMU_FEATURE_PRECISION_BOOST_OVERDRIVE		(1 << 4)
#define	MILAN_SMU_FEATURE_ELECTRICAL_DESIGN_CURRENT		(1 << 5)
#define	MILAN_SMU_FEATURE_CSTATE_BOOST				(1 << 6)
#define	MILAN_SMU_FEATURE_PROCESSOR_THROTTLING_TEMPERATURE	(1 << 7)
#define	MILAN_SMU_FEATURE_CORE_CLOCK_DPM			(1 << 8)
#define	MILAN_SMU_FEATURE_FABRIC_CLOCK_DPM			(1 << 9)
#define	MILAN_SMU_FEATURE_LCLK_DPM				(1 << 10)
#define	MILAN_SMU_FEATURE_XGMI_DYNAMIC_LINK_WIDTH_MANAGEMENT	(1 << 11)
#define	MILAN_SMU_FEATURE_DIGITAL_LDO				(1 << 12)
#define	MILAN_SMU_FEATURE_SOCCLK_DEEP_SLEEP			(1 << 13)
#define	MILAN_SMU_FEATURE_LCLK_DEEP_SLEEP			(1 << 14)
#define	MILAN_SMU_FEATURE_SYSHUBCLK_DEEP_SLEEP			(1 << 15)
#define	MILAN_SMU_FEATURE_CORE_C6				(1 << 17)
#define	MILAN_SMU_FEATURE_DF_CSTATES				(1 << 19)
#define	MILAN_SMU_FEATURE_CLOCK_GATING				(1 << 20)
#define	MILAN_SMU_FEATURE_FAN_CONTROLLER			(1 << 21)
#define	MILAN_SMU_FEATURE_CPPC					(1 << 22)
#define	MILAN_SMU_FEATURE_DYNAMIC_LDO_DROPOUT_LIMITER		(1 << 23)
#define	MILAN_SMU_FEATURE_CPPC_PREFERRED_CORES			(1 << 24)
#define	MILAN_SMU_FEATURE_DYNAMIC_VID_OPTIMIZER			(1 << 25)
#define	MILAN_SMU_FEATURE_AGE					(1 << 26)
#define	MILAN_SMU_FEATURE_DIAGNOSTIC_MODE			(1 << 27)

/*
 * For unknown reasons we have multiple ways to give the SMU an address, and
 * they're apparently operation-specific.  Distinguish them with this.
 */
typedef enum milan_smu_addr_kind {
	MSAK_GENERIC,
	MSAK_HOTPLUG
} milan_smu_addr_kind_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_SMU_IMPL_H */
