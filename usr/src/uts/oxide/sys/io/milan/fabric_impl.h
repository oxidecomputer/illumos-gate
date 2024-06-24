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
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_FABRIC_IMPL_H
#define	_SYS_IO_MILAN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/io/milan/fabric.h>
#include <sys/io/milan/ccx_impl.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * The maximum number of PCIe cores in an NBIO IOMS. The IOMS has up to three
 * cores, but only the one with the WAFL link has core number 2.
 */
#define	MILAN_IOMS_MAX_PCIE_CORES	3
#define	MILAN_IOMS_WAFL_PCIE_CORENO	2

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
 * Similarly, the IOMS instance with the WAFL port.
 */
#define	MILAN_IOMS_HAS_WAFL	0

/*
 * There are supposed to be 23 digital power management (DPM) weights provided
 * by each Milan SMU.  Note that older processor families may have fewer, and
 * Naples also has more SMUs.
 */
#define	MILAN_MAX_DPM_WEIGHTS	23

extern uint32_t milan_smn_read(struct zen_iodie *, const smn_reg_t);
extern void milan_smn_write(struct zen_iodie *, const smn_reg_t,
    const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_FABRIC_IMPL_H */
