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

#ifndef _LIBHSMP_H
#define	_LIBHSMP_H

/*
 * Library routines providing access to a curated set of the functions that
 * the AMD Host System Management Port (HSMP) offers, via the uhsmp(4D)
 * driver. Each function operates on a single HSMP target, nominated by a
 * zero-based identifier. AMD describe the HSMP as a resource of an IO die,
 * and every processor that the library currently supports has one IO die
 * per socket. libhsmp_target_info() maps a target to the socket and IO die
 * that it serves.
 *
 * The set of available HSMP functions depends on the interface version
 * implemented by the SMU firmware. Requests for a function that is not
 * available on the running system fail with LIBHSMP_ERR_UNSUPPORTED.
 *
 * The interfaces herein are MT-Safe only if each thread within a
 * multi-threaded caller uses its own library handle.
 */

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libhsmp_handle libhsmp_handle_t;

/*
 * This is the maximum size of an error message from the library.
 * It is the maximum length of the NUL-terminated string returned by
 * libhsmp_errmsg(), and the size of the buffer that should be passed to
 * libhsmp_init() to receive any initialisation error.
 */
#define	LIBHSMP_ERR_LEN	1024

typedef enum {
	LIBHSMP_ERR_OK = 0,
	/*
	 * Indicates that there was a memory allocation error. The system
	 * error contains the specific errno.
	 */
	LIBHSMP_ERR_NO_MEM,
	/*
	 * No uhsmp device was found. HSMP is not available on this system.
	 */
	LIBHSMP_ERR_NO_DEVICE,
	/*
	 * The caller has insufficient privilege, or is not running in the
	 * global zone.
	 */
	LIBHSMP_ERR_PRIVILEGE,
	/*
	 * One of the function parameters does not pass validation. There will
	 * be more detail available via libhsmp_errmsg().
	 */
	LIBHSMP_ERR_INVALID_PARAM,
	/*
	 * The requested target identifier is out of range for this system.
	 */
	LIBHSMP_ERR_BAD_TARGET,
	/*
	 * The requested function is not supported by the HSMP interface
	 * version that the SMU firmware implements.
	 */
	LIBHSMP_ERR_UNSUPPORTED,
	/*
	 * The SMU is busy and the operation may be retried.
	 */
	LIBHSMP_ERR_BUSY,
	/*
	 * The firmware rejected the command because a prerequisite was not
	 * met.
	 */
	LIBHSMP_ERR_PREREQ,
	/*
	 * The firmware rejected the command's arguments.
	 */
	LIBHSMP_ERR_INVALID_ARGS,
	/*
	 * The SMU did not respond to the command in time.
	 */
	LIBHSMP_ERR_TIMEOUT,
	/*
	 * An internal error occurred. There will be more detail available
	 * via libhsmp_errmsg() and libhsmp_syserr().
	 */
	LIBHSMP_ERR_INTERNAL
} libhsmp_err_t;

extern libhsmp_err_t libhsmp_err(const libhsmp_handle_t *);
extern const char *libhsmp_strerror(libhsmp_err_t);
extern int32_t libhsmp_syserr(const libhsmp_handle_t *);
extern const char *libhsmp_errmsg(const libhsmp_handle_t *);

extern bool libhsmp_init(libhsmp_handle_t **, libhsmp_err_t *, int32_t *,
    char *const, size_t);
extern void libhsmp_fini(libhsmp_handle_t *);

/*
 * An identifier for one of the HSMP targets found on the system. Valid
 * identifiers are 0 .. libhsmp_ntargets() - 1. libhsmp_target_info()
 * reports the zero-based socket and IO die indices to which a target's
 * commands are despatched. Either output pointer may be NULL if the
 * corresponding value is not required.
 */
typedef uint32_t libhsmp_target_t;

extern uint32_t libhsmp_ntargets(const libhsmp_handle_t *);
extern bool libhsmp_target_info(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *, uint32_t *);

/*
 * The version of the SMU firmware that services a target. The HSMP
 * interface version is a plain integer and is retrieved separately with
 * libhsmp_interface_version().
 */
typedef struct libhsmp_smu_version {
	uint8_t lsv_major;
	uint8_t lsv_minor;
	uint8_t lsv_patch;
} libhsmp_smu_version_t;

extern bool libhsmp_smu_version(libhsmp_handle_t *, libhsmp_target_t,
    libhsmp_smu_version_t *);
extern bool libhsmp_interface_version(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *);

/*
 * Socket power telemetry and control. All values are in milliwatts.
 * The value passed to libhsmp_power_limit_set() is clamped by the firmware
 * to the limit reported by libhsmp_power_limit_max().
 */
extern bool libhsmp_power(libhsmp_handle_t *, libhsmp_target_t, uint32_t *);
extern bool libhsmp_power_limit(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *);
extern bool libhsmp_power_limit_max(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *);
extern bool libhsmp_power_limit_set(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t);

/*
 * Core boost limits. Individual cores are identified by their APIC ID and
 * limits are expressed in MHz. libhsmp_boost_limit_set_all() applies a limit
 * to every core in the socket.
 */
extern bool libhsmp_boost_limit(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t, uint32_t *);
extern bool libhsmp_boost_limit_set(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t, uint32_t);
extern bool libhsmp_boost_limit_set_all(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t);

/*
 * Socket status and clocks. Frequencies are in MHz and the C0 residency is
 * a percentage in the range 0-100.
 */
extern bool libhsmp_prochot(libhsmp_handle_t *, libhsmp_target_t, bool *);
extern bool libhsmp_fclk_memclk(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *, uint32_t *);
extern bool libhsmp_cclk_limit(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *);
extern bool libhsmp_c0_residency(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *);

/*
 * The current active frequency limit of the socket, and the sources of that
 * limit. This requires HSMP interface version 5 or later. The source is
 * reported as a bitwise-OR of the following values. Newer firmware may
 * report source bits beyond those defined here, and they are passed through
 * unchanged.
 */
typedef enum {
	LIBHSMP_FREQ_SRC_CHTC_ACTIVE	= 1 << 0,
	LIBHSMP_FREQ_SRC_PROCHOT	= 1 << 1,
	LIBHSMP_FREQ_SRC_TDC		= 1 << 2,
	LIBHSMP_FREQ_SRC_PPT		= 1 << 3,
	LIBHSMP_FREQ_SRC_OPN_MAX	= 1 << 4,
	LIBHSMP_FREQ_SRC_RELIABILITY	= 1 << 5,
	LIBHSMP_FREQ_SRC_APML_AGENT	= 1 << 6,
	LIBHSMP_FREQ_SRC_HSMP_AGENT	= 1 << 7
} libhsmp_freq_src_t;

extern bool libhsmp_freq_limit(libhsmp_handle_t *, libhsmp_target_t,
    uint32_t *, libhsmp_freq_src_t *);

/*
 * Returns a constant string naming the given frequency limit source, or
 * NULL if the value is not a single source known to the library.
 */
extern const char *libhsmp_freq_src_str(libhsmp_freq_src_t);

/*
 * DDR bandwidth telemetry. This requires HSMP interface version 3 or later.
 */
typedef struct libhsmp_ddr_bw {
	uint32_t ldb_max_gbps;
	uint32_t ldb_util_gbps;
	uint32_t ldb_util_pct;
} libhsmp_ddr_bw_t;

extern bool libhsmp_ddr_bandwidth(libhsmp_handle_t *, libhsmp_target_t,
    libhsmp_ddr_bw_t *);

#ifdef __cplusplus
}
#endif

#endif /* _LIBHSMP_H */
