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

#ifndef	_SYS_IO_ZEN_PCIE_H
#define	_SYS_IO_ZEN_PCIE_H

/*
 * Structures, prototypes, enumerations, and constants common across our
 * supported microarchitectures.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following definitions are all in normal PCI configuration space, and
 * represent fixed offsets to capabilities that one would normally have to walk
 * and find in a device. We define these for when we only care about one
 * specific kind of device: a bridge.
 *
 * Note that the actual bit definitions are not included here as they are
 * already present in sys/pcie.h.
 */

/*
 * PCIERCCFG::PCIE_CAP. This is the core PCIe capability register offset. This
 * is related to the PCIE_PCIECAP, but already adjusted for the fixed capability
 * offset.
 */
#define	ZEN_BRIDGE_R_PCI_PCIE_CAP	0x5a

/*
 * These are PCIe slot capability, control, status and link control registers.
 *
 * These are the same as the illumos PCIE_SLOTCAP, PCIE_SLOTCTL, PCIE_SLOTSTS,
 * PCIE_LINKCTL2, and PCIE_SLOTCTL2, but adjusted for the capability offset.
 */
#define	ZEN_BRIDGE_R_PCI_SLOT_CAP	0x6c	/* PCIERCCFG::SLOT_CAP */
#define	ZEN_BRIDGE_R_PCI_SLOT_CTL	0x70	/* PCIERCCFG::SLOT_CNTL */
#define	ZEN_BRIDGE_R_PCI_SLOT_STS	0x72	/* PCIERCCFG::SLOT_STATUS */
#define	ZEN_BRIDGE_R_PCI_LINK_CTL2	0x88	/* PCIERCCFG::LINK_CNTL2 */
#define	ZEN_BRIDGE_R_PCI_SLOT_CAP2	0x8c	/* PCIERCCFG::SLOT_CAP2 */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_PCIE_H */
