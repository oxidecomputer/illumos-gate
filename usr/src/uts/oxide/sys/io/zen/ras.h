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

#ifndef	_SYS_IO_ZEN_RAS_H
#define	_SYS_IO_ZEN_RAS_H

/*
 * Prototypes and so forth for manipulating RAS from the common parts of the
 * Oxide architecture code.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Initialize the current CPU's MCA banks.
 */
extern void zen_ras_init(void);

extern void zen_null_ras_init(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_RAS_H */
