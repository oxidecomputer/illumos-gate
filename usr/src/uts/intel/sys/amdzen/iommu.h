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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _SYS_AMDZEN_IOMMU_H
#define	_SYS_AMDZEN_IOMMU_H

/*
 * This file contains definitions specific to AMD IOMMUs as implemented on Zen
 * architectures.  Primarily, we care about the alignment and
 * and common MSR definitions, see x86_archext.h and controlregs.h.  There is
 * presently no analogue for Intel-specific MSRs because there are no consumers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The size and required alignment of the IOMMU MMIO register space. Currently
 * unused on platforms specifically targetting Zen, as we use SMN access to
 * manipulate those registers, instead.
 */
#define	AMD_IOMMU_MMIO_ALIGN	(512 * 1024U)
#define	AMD_IOMMU_MMIO_SIZE	(512 * 1024U)

/*
 * The registers in the IOMMU's MMIO space come from the IOMMU specification,
 * and are not microarchitecture specific.  Examining the PPRs for the various
 * microarchitectures shows that these registers are also accessible via SMN,
 * and that the offsets of the registers relative to their SMN base address, as
 * well as the bit contents of the registers themselves, mirror the definitions
 * in the microarchitecture-independent IOMMMU documentation.  The only real
 * caveat is that the IOMMU documentation refers to bit offsets relative to the
 * overall register size (e.g., 64-bits for the control registers), whereas the
 * SMN-based definitions in the PPRs are uniformly 32 bits, dividing the larger
 * registers into multiple 32-bit registers as required; e.g., the 64-bit
 * control register is divided into a hi- and lo- pair of 32-bit registers.
 */

/*
 * IOMMUMMIO::IOMMU_MMIO_DEVTBL_BASE_0.  The low 32 bits of the IOMMU device
 * table base register.  Note that the low 9 bits of this register contains the
 * length of the table, in 4KiB increments, minus 1.  Thus, a length of 0x1FF
 * corresponds to a 2MiB table.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_DEVTBL_BASE_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0000	\
}
#define	IOMMU_MMIO_DEVTBL_BASE_0_SET_BASE_LO(r, v)	bitset32(r, 31, 12, v)
#define	IOMMU_MMIO_DEVTBL_BASE_0_SET_DEV_TBL_SIZE(r, v)	bitset32(r, 8, 0, v)

#define	IOMMU_MMIO_DEVTBL_SIZE_2MIB			0x1FF

/*
 * IOMMUMMIO::IOMMU_MMIO_DEVTBL_BASE_1.  The high 32 bits of the IOMMU device
 * table base register.  Note that only the first 20 bits of the register are
 * used; the rest are reserved.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_DEVTBL_BASE_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0004	\
}
#define	IOMMU_MMIO_DEVTBL_BASE_1_SET_BASE_HI(r, v)	bitset32(r, 19, 0, v)

/*
 * IOMMUMMIO::IOMMU_MMIO_CMD_BASE_0.  The low 32 bits of the IOMMU command base
 * register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CMD_BASE_0		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0008	\
}
#define	IOMMU_MMIO_CMD_BASE_0_SET_BASE_LO(r, v)		bitset32(r, 31, 12, v)

/*
 * IOMMUMMIO::IOMMU_MMIO_CMD_BASE_1.  The high 32 bits of the IOMMU command base
 * register.  The nibble at 27:24 specifies the length of the command buffer, in
 * number of 128-bit entries, encoded as a power of two; 0000b through 0111b are
 * reserved, so the minimum length is 256 entries, requiring size 4096 bytes.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CMD_BASE_1		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x000c	\
}
#define	IOMMU_MMIO_CMD_BASE_1_SET_COM_LEN(r, v)		bitset32(r, 27, 24, v)
#define	IOMMU_MMIO_CMD_BASE_1_SET_BASE_HI(r, v)		bitset32(r, 19, 0, v)

#define	IOMMU_MMIO_CMD_SIZE_512KIB			0x0F

/*
 * IOMMUMMIO::IOMMU_MMIO_EVENT_BASE_0.  The low 32 bits of the IOMMU event log
 * base register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EVENT_BASE_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0010	\
}
#define	IOMMU_MMIO_EVENT_BASE_0_SET_BASE_LO(r, v)	bitset32(r, 31, 12, v)

/*
 * IOMMUMMIO::IOMMU_MMIO_EVENT_BASE_1.  The high 32 bits of the IOMMU event log
 * base register.  The nibble at 27:24 specifies the length of the event log, in
 * number of 128-bit entries, encoded as a power of two; 0000b through 0111b are
 * reserved, so the minimum length is 256 entries, requiring size 4096 bytes.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EVENT_BASE_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0014	\
}
#define	IOMMU_MMIO_EVENT_BASE_1_SET_EVENT_LEN(r, v)	bitset32(r, 27, 24, v)
#define	IOMMU_MMIO_EVENT_BASE_1_SET_BASE_HI(r, v)	bitset32(r, 19, 0, v)

#define	IOMMU_MMIO_EVENT_SIZE_512KIB			0x0F

/*
 * IOMMUMMIO::IOMMU_MMIO_CNTRL_0.  The low 32 bits of the IOMMU control
 * register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CTL_0		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0018	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_CNTRL_1.  The high 32 bits of the IOMMU control
 * register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CTL_1		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x001c	\
}

/*
 * The IOMMU exclusion range is used to list addresses for which accesses are
 * passed through and not translated by the IOMMU.  The bounds of the exclusion
 * range are inclusive, and given by a <base,limit> pair.
 *
 * As Oxide does not currently implement address translation, it is not
 * presently used by the Oxide architecture.  We include these definitions for
 * completeness.
 */

/*
 * IOMMUMMIO::IOMMU_MMIO_EXCL_BASE_0.  The low 32 bits of the IOMMU exclusion
 * range base register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EXCL_BASE_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0020	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EXCL_BASE_1.  The high 32 bits of the IOMMU exclusion
 * range base register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EXCL_BASE_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0024	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EXCL_LIM_0.  The low 32 bits of the IOMMU exclusion
 * range limit register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EXCL_LIM_0		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0028	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EXCL_LIM_1.  The high 32 bits of the IOMMU exclusion
 * range limit register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EXCL_LIM_1		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x002c	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EFR_0.  The low 32 bits of the IOMMU extended features
 * register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EFR_0		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0030	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EFR_1.  The high 32 bits of the IOMMU extended features
 * regiter.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EFR_1		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x0034	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_CMD_BUF_HDPTR_0.  The low 32 bits of the command buffer
 * head pointer.  The sole datum in this register is a 128-bit aligned offset
 * from the command buffer base address, addressing the next entry to be read.
 *
 * Note that hardware writes this in ring fashion, looping back around to the
 * beginning once it hits the end; softwre generally does not write here, unless
 * resetting IOMMU state.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CMD_BUF_HDPTR_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2000	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_CMD_BUF_HDPTR_1.  The high 32 bits of the command
 * buffer head pointer.
 *
 * Note that the entire 32 bits are reserved, and this register is read-only
 * with fixed, all-zero, contents.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CMD_BUF_HDPTR_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2004	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_CMD_BUF_TAILPTR_0.  The low 32 bits of the command
 * buffer tail pointer.  The sole datum in this register is a 128 bit (16 byte)
 * aligned offset to the next command to be consumed by the IOMMU.
 *
 * Software writes this when a new command is added to the buffer, taking care
 * to calculate the offset modulo the buffer size.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CMD_BUF_TAILPTR_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2008	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_CMD_BUF_TAILPTR_1.  The high 32 bits of the command
 * buffer tail pointer.
 *
 * Note that the entire 32 bits are reserved, and this register is read-only
 * with fixed, all-zero, contents.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_CMD_BUF_TAILPTR_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x200c	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EVENT_BUF_HDPTR_0.  The low 32 bits of the event log
 * buffer head pointer.  The only datum in this register is an 128 bit (16 byte)
 * aligned offset to the next event log entry to be read.
 *
 * Software writes this when a new log entry is consumed from the buffer, taking
 * care to calculate the offset modulo the buffer size.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EVENT_BUF_HDPTR_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2010	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EVENT_BUF_HDPTR_1.  The high 32 bits of the event log
 * buffer head pointer register.
 *
 * Note that the entire 32 bits are reserved, and this register is read-only
 * with fixed, all-zero, contents.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EVENT_BUF_HDPTR_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2014	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EVENT_BUF_TAILPTR_0.  The low 32 bits of the event log
 * buffer tail pointer register.  The only datum in this register is a 128 bit
 * (16 byte) aligned offset of the next log entry to be written.
 *
 * Note that hardware writes this in ring fashion, looping back around to the
 * beginning once it hits the end; softwre generally does not write here, unless
 * resetting IOMMU state.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EVENT_BUF_TAILPTR_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2018	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_EVENT_BUF_TAILPTR_1.  The high 32 bits of the event log
 * buffer tail pointer register.
 *
 * Note that the entire 32 bits are reserved, and this register is read-only
 * with fixed, all-zero, contents.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_EVENT_BUF_TAILPTR_1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x201c	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_STATUS_0.  The low 32 bits of the IOMMU status
 * register.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_STATUS_0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2020	\
}

/*
 * IOMMUMMIO::IOMMU_MMIO_STATUS_1.  The high 32 bits of the IOMMU status
 * register.
 *
 * Note that the entire 32 bits are reserved, and this register is read-only
 * with fixed, all-bits zero, contents.
 */
/*CSTYLED*/
#define	D_IOMMU_MMIO_STATUS_1		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_IOMMUMMIO,	\
	.srd_reg = 0x2024	\
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_IOMMU_H */
