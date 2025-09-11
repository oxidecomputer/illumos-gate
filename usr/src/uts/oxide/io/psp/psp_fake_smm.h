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

#ifndef _IO_PSP_PSP_FAKE_SMM_H
#define	_IO_PSP_PSP_FAKE_SMM_H

/*
 * This file contains definitions for issuing CPU-to-PSP commands requiring
 * SMM mode.
 */

#include <sys/amdzen/psp.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int psp_c_c2pmbox_smm_cmd(cpu2psp_mbox_cmd_t, c2p_mbox_buffer_hdr_t *);

#ifdef __cplusplus
}
#endif

#endif /* _IO_PSP_PSP_FAKE_SMM_H */
