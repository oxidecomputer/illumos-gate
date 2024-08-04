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

typedef struct zen_smn_ops {
	/*
	 * Retrieves an IOMS register.
	 */
	smn_reg_t	(*zso_smn_ioms_reg)(const zen_ioms_t *const,
			    const smn_reg_def_t, const uint16_t);

	/*
	 * Retrieves an IO die register.
	 */
	smn_reg_t	(*zso_smn_iodie_reg)(const zen_iodie_t *const,
			    const smn_reg_def_t, const uint16_t);

	/*
	 * Reads the given SMN register from the given die.
	 */
	uint32_t	(*zso_smn_read)(zen_iodie_t *, const smn_reg_t);

	/*
	 * Writes the given SMN register from the given die.
	 */
	void		(*zso_smn_write)(zen_iodie_t *,
			    const smn_reg_t, const uint32_t);
} zen_smn_ops_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_SMN_H */
