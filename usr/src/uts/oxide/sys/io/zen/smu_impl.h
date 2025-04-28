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

/*
 * Type, structure, and function definitions for interacting with the System
 * Management Unit (SMU).
 */

#ifndef	_ZEN_SMU_IMPL_H
#define	_ZEN_SMU_IMPL_H

#include <sys/types.h>
#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/smu.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 * The arguments, request, and response for an RPC sent to the SMU.  Note that
 * the response field holds the raw response from firmware and is kept for
 * debugging and error reporting, and not generally used by callers, which
 * instead examine a zen_smu_rpc_res_t.
 */
typedef struct zen_smu_rpc {
	uint32_t			zsr_req;
	uint32_t			zsr_resp;
	uint32_t			zsr_args[6];
} zen_smu_rpc_t;

/*
 * Synchronously invoke an RPC on the SMU.  Returns the RPC status.
 * Overwrites rpc->zsr_args with data returned by the RPC on success; zsr_args
 * is unmodified if the RPC fails.
 */
extern zen_smu_rpc_res_t zen_smu_rpc(zen_iodie_t *, zen_smu_rpc_t *);

/*
 * Returns the string representation of a SMU RPC response.
 */
extern const char *zen_smu_rpc_res_str(const zen_smu_rpc_res_t);

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;

/*
 * Retrieves and reports the SMU firmware version.
 */
extern bool zen_smu_get_fw_version(zen_iodie_t *iodie);
extern void zen_smu_report_fw_version(const zen_iodie_t *iodie);

/*
 * Returns true if the firmware version running on the SMU for the given IO die
 * is greater than or equal to the given major, minor, and patch versions.
 */
extern bool zen_smu_version_at_least(const zen_iodie_t *, uint8_t, uint8_t,
    uint8_t);

/*
 * Reads the CPU "name" string from the SMU.
 */
extern bool zen_smu_get_brand_string(zen_iodie_t *, char *, size_t);

/*
 * Provides an address to the SMU pertaining to the subsequent operation.
 */
extern bool zen_smu_rpc_give_address(zen_iodie_t *, uint64_t);

/*
 * Transmits the Power and Performance table to the SMU.
 */
extern bool zen_smu_rpc_send_pptable(zen_iodie_t *, zen_pptable_t *);

/*
 * Sets SMU features.
 */
extern bool zen_smu_set_features(zen_iodie_t *, uint32_t, uint32_t);

/*
 * Enables HSMP interrupts.
 */
extern bool zen_smu_rpc_enable_hsmp_int(zen_iodie_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _ZEN_SMU_IMPL_H */
