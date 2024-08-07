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

#ifndef	_SYS_IO_ZEN_SMN_H
#define	_SYS_IO_ZEN_SMN_H

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * SMN access routines -- can only be used after early fabric init.
 */

extern uint32_t zen_smn_read(zen_iodie_t *, const smn_reg_t);
extern void zen_smn_write(zen_iodie_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for Core registers.
 */
extern smn_reg_t zen_core_reg(const zen_core_t *const, const smn_reg_def_t);
extern uint32_t zen_core_read(zen_core_t *, const smn_reg_t);
extern void zen_core_write(zen_core_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for CCD registers.
 */
extern smn_reg_t zen_ccd_reg(const zen_ccd_t *const, const smn_reg_def_t);
extern uint32_t zen_ccd_read(zen_ccd_t *, const smn_reg_t);
extern void zen_ccd_write(zen_ccd_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for IOMS registers.
 */
extern smn_reg_t zen_ioms_reg(const zen_ioms_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t zen_ioms_read(zen_ioms_t *, const smn_reg_t);
extern void zen_ioms_write(zen_ioms_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for IO die registers.
 */
extern smn_reg_t zen_iodie_reg(const zen_iodie_t *const, const smn_reg_def_t,
    const uint16_t);
extern uint32_t zen_iodie_read(zen_iodie_t *, const smn_reg_t);
extern void zen_iodie_write(zen_iodie_t *, const smn_reg_t, const uint32_t);

/*
 * Function pointer types for retrieving different types of SMN registers.
 */
typedef smn_reg_t (*zen_smn_core_reg_f)(uint8_t, uint8_t, const smn_reg_def_t,
    const uint16_t);
typedef smn_reg_t (*zen_smn_ccd_reg_f)(uint8_t, const smn_reg_def_t,
    const uint16_t);
typedef smn_reg_t (*zen_smn_ioms_reg_f)(const uint8_t, const smn_reg_def_t,
    const uint16_t);
typedef smn_reg_t (*zen_smn_iodie_reg_f)(const smn_reg_def_t, const uint16_t);

typedef struct zen_smn_ops {
	/*
	 * Unit-specific Core register accessor functions.
	 */
	const zen_smn_core_reg_f	zso_core_reg_fn[SMN_UNIT_MAX];

	/*
	 * Unit-specific CCD register accessor functions.
	 */
	const zen_smn_ccd_reg_f		zso_ccd_reg_fn[SMN_UNIT_MAX];

	/*
	 * Unit-specific IOMS register accessor functions.
	 */
	const zen_smn_ioms_reg_f	zso_ioms_reg_fn[SMN_UNIT_MAX];

	/*
	 * Unit-specific IO die register accessor functions.
	 */
	const zen_smn_iodie_reg_f	zso_iodie_reg_fn[SMN_UNIT_MAX];
} zen_smn_ops_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_SMN_H */
