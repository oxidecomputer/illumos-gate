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

#ifndef _SYS_IO_TURIN_FABRIC_IMPL_H
#define	_SYS_IO_TURIN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/x86_archext.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/turin/ccx_impl.h>
#include <sys/io/turin/pcie_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This defines what the maximum number of SoCs that are supported in Turin.
 */
#define	TURIN_MAX_SOCS			2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC.
 */
#define	TURIN_IODIE_PER_SOC		1

/*
 * This is the number of NBIO instances that we know are supposed to exist per
 * die.
 */
#define	TURIN_NBIO_PER_IODIE		2

/*
 * This is the number of IO[MS] (IOHUB[MS]) instances that we know are supposed
 * to exist per NBIO.
 */
#define	TURIN_IOMS_PER_NBIO		4

/*
 * This is the number of IO[MS] instances that we know are supposed to exist per
 * die.
 */
#define	TURIN_IOMS_PER_IODIE	(TURIN_IOMS_PER_NBIO * TURIN_NBIO_PER_IODIE)

/*
 * Each NBIO has 4 x16 PCIe Gen5 cores, one on each of four IOHUBs.
 * Additionally, NBIO0/IOHUB2 (IOMS2) has a bonus x8 PCIe Gen3 core.
 * This all means that the most IOHUBs across both NBIOs has one core, while
 * NBIO0/IOHUB2 has two.
 */
#define	TURIN_IOMS_MAX_PCIE_CORES	2
#define	TURIN_NBIO_BONUS_IOMS		2
#define	TURIN_IOMS_BONUS_PCIE_CORENO	1

/*
 * Convenience macro to convert an IOMS number to the corresponding IOHUB.
 */
#define	TURIN_IOMS_IOHUB_NUM(num)	((num) % TURIN_IOMS_PER_NBIO)

/*
 * This is the primary initialization point for the Turin Data Fabric,
 * Northbridges, PCIe, and related.
 */
extern void turin_fabric_init(zen_fabric_t *);

extern smn_reg_t turin_pcie_port_reg(const zen_pcie_port_t *const,
    const smn_reg_def_t);
extern smn_reg_t turin_pcie_core_reg(const zen_pcie_core_t *const,
    const smn_reg_def_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_FABRIC_IMPL_H */
