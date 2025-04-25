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

/*
 * SMN access routines.  These are used to read and write to/from the network,
 * and access registers on specific parts.
 *
 * Caveat: these can only be used after early fabric init.
 */

#include <sys/types.h>
#include <sys/stdint.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_ioms zen_ioms_t;
typedef struct zen_nbif zen_nbif_t;
typedef struct zen_nbif_func zen_nbif_func_t;
typedef struct zen_iodie zen_iodie_t;

/*
 * Issue reads and writes on the network associated with the given IO die.
 */
extern uint32_t zen_smn_read(zen_iodie_t *, const smn_reg_t);
extern void zen_smn_write(zen_iodie_t *, const smn_reg_t, const uint32_t);
extern void zen_hsmp_test(zen_iodie_t *);

/*
 * Accessors for Core registers.
 */
extern uint32_t zen_core_read(zen_core_t *, const smn_reg_t);
extern void zen_core_write(zen_core_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for CCD registers.
 */
extern uint32_t zen_ccd_read(zen_ccd_t *, const smn_reg_t);
extern void zen_ccd_write(zen_ccd_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for IOMS registers.
 */
extern uint32_t zen_ioms_read(zen_ioms_t *, const smn_reg_t);
extern void zen_ioms_write(zen_ioms_t *, const smn_reg_t, const uint32_t);

/*
 * Accessors for nBIF registers.
 */
extern uint32_t zen_nbif_read(zen_nbif_t *, const smn_reg_t);
extern void zen_nbif_write(zen_nbif_t *, const smn_reg_t, const uint32_t);
extern uint32_t zen_nbif_func_read(zen_nbif_func_t *, const smn_reg_t);
extern void zen_nbif_func_write(zen_nbif_func_t *, const smn_reg_t,
    const uint32_t);

/*
 * Accessors for IO die registers.
 */
extern uint32_t zen_iodie_read(zen_iodie_t *, const smn_reg_t);
extern void zen_iodie_write(zen_iodie_t *, const smn_reg_t, const uint32_t);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_SMN_H */
