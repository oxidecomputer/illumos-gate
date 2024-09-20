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

#ifndef _SYS_IO_GENOA_PCIE_IMPL_H
#define	_SYS_IO_GENOA_PCIE_IMPL_H

#include <sys/types.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/genoa/pcie.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each of the normal PCIe cores is an RC9x16: up to 9 ports across 16 lanes.
 * Each bonus PCIe core is an RC4x4.
 */
#define	GENOA_PCIE_CORE_MAX_PORTS	9
#define	GENOA_PCIE_CORE_BONUS_PORTS	4

/*
 * These stages of configuration are referred to in the per-port and per-RC
 * register storage structures, which provide a debugging facility to help
 * understand what both firmware and software have done to these registers over
 * time.  They do not control any software behaviour other than in mdb.  See the
 * theory statement in genoa_fabric.c for the definitions of these stages.
 */
typedef enum genoa_pcie_config_stage {
	GPCS_PRE_INIT,
	GPCS_NUM_STAGES
} genoa_pcie_config_stage_t;

CTASSERT(GPCS_NUM_STAGES <= ZPCS_MAX_STAGES);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_PCIE_IMPL_H */
