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

#ifndef _SYS_IO_ZEN_FABRIC_LIMITS_H
#define	_SYS_IO_ZEN_FABRIC_LIMITS_H

/*
 * Limits as to what the Oxide platform can support with respect to fabric
 * structures. These are generally sized to accomodate the largest number used
 * by any supported microarchitecture.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is the maximum number of I/O dies that can exist in a given SoC. Since
 * Rome this has been 1. Previously on Naples this was 4. Because we do not work
 * on Naples based platforms, this is kept low (unlike the more general amdzen
 * nexus driver).
 */
#define	ZEN_FABRIC_MAX_DIES_PER_SOC	1

/*
 * The Oxide platform supports a maximum of 2 SOCs.
 */
#define	ZEN_FABRIC_MAX_SOCS		2

/*
 * The resulting maximum number of I/O dies that can be found in the fabric.
 */
#define	ZEN_FABRIC_MAX_IO_DIES		(ZEN_FABRIC_MAX_SOCS * \
					    ZEN_FABRIC_MAX_DIES_PER_SOC)

/*
 * The exact number of NBIO and IOM/S is platform-specific and determined
 * determined dynamically during fabric topology initialization. These are the
 * maximums supported on the Oxide platform.
 */
#define	ZEN_IODIE_MAX_NBIO		4
#define	ZEN_IODIE_MAX_IOMS		8
#define	ZEN_NBIO_MAX_IOMS		4

/*
 * The maximum number of PCIe cores per IOMS.
 */
#define	ZEN_IOHC_MAX_PCIE_CORES		3

/*
 * The maximum number of NBIFs (Northbridge Interfaces, though possible
 * Northbridge interconnect functions; definitions vary) per IOMS.
 */
#define	ZEN_IOMS_MAX_NBIF		3

/*
 * The maximum number of PCI bus, I/O and MMIO routing rules supported on the
 * Oxide platform. Of the supported processors, Turin supports the most rules.
 */
#define	ZEN_MAX_CFGMAP			DF_MAX_CFGMAP_TURIN
#define	ZEN_MAX_IO_RULES		DF_MAX_IO_RULES_TURIN
#define	ZEN_MAX_MMIO_RULES		DF_MAX_MMIO_RULES_TURIN

/*
 * This is the limit of the number of UBM DFC and HFC devices that we will
 * currently track. Note, the architectural limit is currently 32 DFCs per HFC.
 * We limit these to lower values based on the system we support in practice.
 * Given that a given HFC is mapped to a group of 16 PHYs, we set this to 16 for
 * SATA mapping. The HFC threshold is currently based on one per primary PCIe
 * core for each socket (8 *2).
 */
#define	ZEN_MAX_UBM_DFC_PER_HFC		16
#define	ZEN_MAX_UBM_HFC			16

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_LIMITS_H */
