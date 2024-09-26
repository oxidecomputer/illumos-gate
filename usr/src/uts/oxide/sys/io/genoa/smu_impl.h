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

#ifndef _SYS_IO_GENOA_SMU_IMPL_H
#define	_SYS_IO_GENOA_SMU_IMPL_H

#include <sys/io/genoa/smu.h>

/*
 * Genoa-specific definitions related to the implementation of SMU functionality
 * on the Oxide architecture.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SMU features, as enabled via ZEN_SMU_OP_ENABLE_FEATURE. Note that not
 * all combinations of features will result in correct system behavior!
 */
#define	GENOA_SMU_FEATURE_DATA_CALCULATION			(1U << 0)
#define	GENOA_SMU_FEATURE_PPT					(1U << 1)
#define	GENOA_SMU_FEATURE_THERMAL_DESIGN_CURRENT		(1U << 2)
#define	GENOA_SMU_FEATURE_THERMAL				(1U << 3)
#define	GENOA_SMU_FEATURE_FIT					(1U << 4)
#define	GENOA_SMU_FEATURE_ELECTRICAL_DESIGN_CURRENT		(1U << 5)
#define	GENOA_SMU_FEATURE_CSTATE_BOOST				(1U << 6)
#define	GENOA_SMU_FEATURE_PROCESSOR_THROTTLING_TEMPERATURE	(1U << 7)
#define	GENOA_SMU_FEATURE_CORE_CLOCK_DPM			(1U << 8)
#define	GENOA_SMU_FEATURE_FABRIC_CLOCK_DPM			(1U << 9)
#define	GENOA_SMU_FEATURE_LCLK_DPM				(1U << 10)
#define	GENOA_SMU_FEATURE_PSI7					(1U << 11)
#define	GENOA_SMU_FEATURE_DIGITAL_LDO				(1U << 12)
#define	GENOA_SMU_FEATURE_SOCCLK_DEEP_SLEEP			(1U << 13)
#define	GENOA_SMU_FEATURE_LCLK_DEEP_SLEEP			(1U << 14)
#define	GENOA_SMU_FEATURE_SYSHUBCLK_DEEP_SLEEP			(1U << 15)
#define	GENOA_SMU_FEATURE_DYNAMIC_VID_OPTIMIZER			(1U << 16)
#define	GENOA_SMU_FEATURE_CORE_C6				(1U << 17)
#define	GENOA_SMU_FEATURE_PC6					(1U << 18)
#define	GENOA_SMU_FEATURE_DF_CSTATES				(1U << 19)
#define	GENOA_SMU_FEATURE_CLOCK_GATING				(1U << 20)
#define	GENOA_SMU_FEATURE_FAN_CONTROLLER			(1U << 21)
#define	GENOA_SMU_FEATURE_CPPC					(1U << 22)
#define	GENOA_SMU_FEATURE_DYNAMIC_LDO_DROPOUT_LIMITER		(1U << 23)
#define	GENOA_SMU_FEATURE_CPPC_PREFERRED_CORES			(1U << 24)
#define	GENOA_SMU_FEATURE_GMI_FOLDING				(1U << 25)
#define	GENOA_SMU_FEATURE_GMI_DLWM				(1U << 26)
#define	GENOA_SMU_FEATURE_XGMI_DLWM				(1U << 27)
#define	GENOA_SMU_FEATURE_DF_LIGHT_CSTATE			(1U << 28)
#define	GENOA_SMU_FEATURE_SMNCLK_DEEP_SLEEP			(1U << 29)
#define	GENOA_SMU_FEATURE_PCI_SPEED_CONTROLLER			(1U << 30)
#define	GENOA_SMU_FEATURE_GFX_DPM				(1U << 31)

#define	GENOA_SMU_EXT_FEATURE_DS_GFXCLK			(1U << (32 - 32))
#define	GENOA_SMU_EXT_FEATURE_PCC			(1U << (33 - 32))
#define	GENOA_SMU_EXT_FEATURE_AGE			(1U << (34 - 32))
#define	GENOA_SMU_EXT_FEATURE_S0I3			(1U << (35 - 32))
#define	GENOA_SMU_EXT_FEATURE_VCN_DPM			(1U << (36 - 32))
#define	GENOA_SMU_EXT_FEATURE_DS_VCN			(1U << (37 - 32))
#define	GENOA_SMU_EXT_FEATURE_MPDMA_TF_CLK_DEEP_SLEEP	(1U << (38 - 32))
#define	GENOA_SMU_EXT_FEATURE_MPDMA_PM_CLK_DEEP_SLEEP	(1U << (39 - 32))
#define	GENOA_SMU_EXT_FEATURE_VDDOFF			(1U << (40 - 32))
#define	GENOA_SMU_EXT_FEATURE_DCFCLK_DPM		(1U << (41 - 32))
#define	GENOA_SMU_EXT_FEATURE_DCFCLK_DEEP_SLEEP		(1U << (42 - 32))

#define	GENOA_SMU_EXT_FEATURE_DIAGNOSTIC_MODE		(1U << (50 - 32))
#define	GENOA_SMU_EXT_FEATURE_CXL_QOS			(1U << (51 - 32))

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_SMU_IMPL_H */
