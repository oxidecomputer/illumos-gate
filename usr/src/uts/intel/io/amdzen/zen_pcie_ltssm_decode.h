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

#ifndef _IO_AMDZEN_ZEN_PCIE_LTSSM_DECODE_H
#define	_IO_AMDZEN_ZEN_PCIE_LTSSM_DECODE_H

/*
 * Decode of the AMD/Zen 6-bit LC_STATE encoding to a name and a common LTSSM
 * (state, sub-state). The implementation lives in
 * common/amdzen/zen_pcie_ltssm_decode.c
 */

#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/pcie.h>
#include <sys/x86_archext.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Look up the name, common state, and sub-state for a raw LC_STATE value on the
 * given AMD processor family. Returns false if the family has no validated
 * decode table or the value does not correspond to a known state. Any of the
 * output pointers may be NULL.
 */
extern bool zen_ltssm_lookup(x86_processor_family_t, uint8_t, const char **,
    pcie_ltssm_state_t *, pcie_ltssm_substate_t *);

#ifdef __cplusplus
}
#endif

#endif /* _IO_AMDZEN_ZEN_PCIE_LTSSM_DECODE_H */
