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

#ifndef	_SYS_IO_TURIN_RAS_H
#define	_SYS_IO_TURIN_RAS_H

/*
 * Turin-specific constants for RAS, primarily bits in MCA mask MSRs.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Only bits we refer to in the corresponding private setup code are defined
 * here; these should eventually end up in generic headers as they are not
 * specific to the oxide architecture and correspond to status bits that
 * machine-independent CPU modules will need.
 *
 * These are bit position numbers, not masks.
 */

/*
 * LS (load-store) mask bits.
 */
#define	TURIN_RAS_MASK_LS_SYS_RD_DATA_WCB	25
#define	TURIN_RAS_MASK_LS_SYS_RD_DATA_MAB	10
#define	TURIN_RAS_MASK_LS_SYS_RD_DATA_UCODE	9

/*
 * IF (instruction fetch) mask bits.
 */
#define	TURIN_RAS_MASK_IF_L2_SYS_DATA_RD_ERR	13

/*
 * L2 mask bits.
 */
#define	TURIN_RAS_MASK_L2_HWA			3

/*
 * FP (floating-point) mask bits.
 */
#define	TURIN_RAS_MASK_FP_HWA			6
/*
 * NBIO (northbridge I/O) mask bits.
 */
#define	TURIN_RAS_MASK_NBIO_EXT_SDP_ERR_EVT	2
#define	TURIN_RAS_MASK_NBIO_PCIE_SB		1

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IO_TURIN_RAS_H */
