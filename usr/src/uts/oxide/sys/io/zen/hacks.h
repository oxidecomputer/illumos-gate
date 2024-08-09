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

#ifndef	_SYS_IO_ZEN_HACKS_H
#define	_SYS_IO_ZEN_HACKS_H

#include <sys/io/zen/platform_impl.h>

/*
 * Support for various and sundry hacks that we have had to add for particular
 * quirks in Zen platforms.  Not all of these apply to every microarchitecture.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Setup the SoC so that a single core shutdown (e.g., due to a triple fault)
 * results in a machine reset through A2.
 */
extern void zen_shutdown_detect_init(void);

/*
 * Set up SSC clocking.  This clock hack enables or disables PCIe spread
 * spectrum clocking via the FCH clock generator.
 */
extern bool zen_cgpll_set_ssc(bool);

/*
 * Check for furtive reset and panic according.  A furtive reset is one that
 * cannot be detected by the SP for some reason, such as, on Milan, non-reserved
 * bits set in FCH::PM::S5_RESET_STATUS during a window where RESET_L/PWROK is
 * not toggled.
 */
extern void zen_check_furtive_reset(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_HACKS_H */
