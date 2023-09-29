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
 * Copyright 2023 Oxide Computer Company
 */

#ifndef _LIBIPCC_H
#define	_LIBIPCC_H

/*
 * Library routines that interface with the Oxide Inter-Processor
 * Communications Channel (IPCC) driver in order to send commands to,
 * and retrieve data from, the service processor in Oxide hardware.
 *
 * The interfaces herein are MT-Safe only if each thread within a
 * multi-threaded caller uses its own library handle.
 */

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/ethernet.h>
#include <sys/ipcc.h>

typedef struct libipcc_handle libipcc_handle_t;

typedef enum {
	LIBIPCC_ERR_OK = 0,
	/*
	 * Indicates that there was a memory allocation error. The system
	 * error contains the specific errno.
	 */
	LIBIPCC_ERR_NO_MEM,
	/*
	 * One of the function parameters does not pass validation. There will
	 * be more detail available via libipcc_errmsg().
	 */
	LIBIPCC_ERR_INVALID_PARAM,
	/*
	 * An internal error occurred. There will be more detail available via
	 * libipcc_errmsg() and libipcc_syserr().
	 */
	LIBIPCC_ERR_INTERNAL,
	/*
	 * The requested lookup key was not known to the SP.
	 */
	LIBIPCC_ERR_KEY_UNKNOWN,
	/*
	 * The value for the requested lookup key was too large for the
	 * supplied buffer.
	 */
	LIBIPCC_ERR_KEY_BUFTOOSMALL,
	/*
	 * An attempt to write to a key failed because the key is read-only.
	 */
	LIBIPCC_ERR_KEY_READONLY,
	/*
	 * An attempt to write to a key failed because the passed value is too
	 * long.
	 */
	LIBIPCC_ERR_KEY_VALTOOLONG,
	/*
	 * Compression or decompression failed. If appropriate,
	 * libipcc_syserr() will return the Z_ error from zlib.
	 */
	LIBIPCC_ERR_KEY_ZERR,
} libipcc_err_t;

extern libipcc_err_t libipcc_err(libipcc_handle_t *);
extern const char *libipcc_strerror(libipcc_err_t);
extern int32_t libipcc_syserr(libipcc_handle_t *);
extern const char *libipcc_errmsg(libipcc_handle_t *);

extern bool libipcc_init(libipcc_handle_t **, libipcc_err_t *, int32_t *,
    char * const, size_t);
extern void libipcc_fini(libipcc_handle_t *);

/*
 * SP status/startup registers.
 */
extern bool libipcc_status(libipcc_handle_t *, uint64_t *);
extern bool libipcc_startup_options(libipcc_handle_t *, uint64_t *);

/*
 * VPD identity information.
 */
typedef struct libipcc_ident libipcc_ident_t;

extern bool libipcc_ident(libipcc_handle_t *, libipcc_ident_t **);
extern const uint8_t *libipcc_ident_serial(libipcc_ident_t *);
extern const uint8_t *libipcc_ident_model(libipcc_ident_t *);
extern uint32_t libipcc_ident_rev(libipcc_ident_t *);
extern void libipcc_ident_free(libipcc_ident_t *);

/*
 * Attempt to retrieve a block of an image served by MGS.
 */
extern bool libipcc_imageblock(libipcc_handle_t *, uint8_t *, size_t,
    uint64_t, uint8_t *, size_t *);

/*
 * These calls allow retrieval of different sets of MAC addresses derived from
 * the addresses that the SP advertises to the host for its own use.
 * Obtain a mac address handle via one of:
 *	libipcc_mac_all		all host MAC addresses
 *	libipcc_mac_nic		the subset allocated to the NICs
 *	libipcc_mac_bootstrap	the subset allocated for bootstrap addresses
 * Interrogate it with libipcc_mac_{addr,count,stride} and free it with
 * libipcc_mac_free once done.
 */
typedef struct libipcc_mac libipcc_mac_t;

extern bool libipcc_mac_all(libipcc_handle_t *, libipcc_mac_t **);
extern bool libipcc_mac_nic(libipcc_handle_t *, libipcc_mac_t **);
extern bool libipcc_mac_bootstrap(libipcc_handle_t *, libipcc_mac_t **);
extern const struct ether_addr *libipcc_mac_addr(libipcc_mac_t *);
extern uint16_t libipcc_mac_count(libipcc_mac_t *);
extern uint8_t libipcc_mac_stride(libipcc_mac_t *);
extern void libipcc_mac_free(libipcc_mac_t *);

/*
 * Retrieve a value from the key/value store in the SP. If the buffer parameter
 * is NULL then a buffer will be allocated by the library and the caller is
 * responsible for freeing it explicitly by calling libipcc_keylookup_free().
 */
#define	LIBIPCC_KEY_PING			IPCC_KEY_PING
#define	LIBIPCC_KEY_INSTALLINATOR_IMAGE_ID	IPCC_KEY_INSTALLINATOR_IMAGE_ID
#define	LIBIPCC_KEY_INVENTORY			IPCC_KEY_INVENTORY
#define	LIBIPCC_KEY_ETC_SYSTEM			IPCC_KEY_ETC_SYSTEM
#define	LIBIPCC_KEY_DTRACE_CONF			IPCC_KEY_DTRACE_CONF

typedef enum {
	/*
	 * Specifies that the key data stored in the SP is compressed. When
	 * passed to libipcc_keylookup(), the library will attempt to
	 * decompress the data and return the result -- in this case
	 * the caller must allow the library to allocate the memory required
	 * for the buffer by passing in a NULL buffer parameter.
	 */
	LIBIPCC_KEYF_COMPRESSED	= 1 << 0
} libipcc_key_flag_t;

extern bool libipcc_keylookup(libipcc_handle_t *, uint8_t, uint8_t **,
    size_t *, libipcc_key_flag_t);
extern void libipcc_keylookup_free(uint8_t *, size_t);

/*
 * Setting key values in the SP's key/value store.
 */

extern bool libipcc_keyset(libipcc_handle_t *, uint8_t, uint8_t *, size_t,
    libipcc_key_flag_t);

/*
 * Retrieval of system inventory data from the SP. Retrieve an inventory item
 * with libipcc_inventory, interrogate it with
 * libipcc_inventory_{status,type,name,data} and then free it with
 * libipcc_inventory_free once done.
 */
typedef struct libipcc_inventory libipcc_inventory_t;

typedef enum {
	LIBIPCC_INVENTORY_STATUS_SUCCESS = 0,
	LIBIPCC_INVENTORY_STATUS_INVALID_INDEX,
	LIBIPCC_INVENTORY_STATUS_IO_DEV_MISSING,
	LIBIPCC_INVENTORY_STATUS_IO_ERROR
} libipcc_inventory_status_t;

extern bool libipcc_inventory_metadata(libipcc_handle_t *, uint32_t *,
    uint32_t *);
extern bool libipcc_inventory(libipcc_handle_t *, uint32_t,
    libipcc_inventory_t **);
extern const char *libipcc_inventory_status_str(libipcc_inventory_status_t);
extern libipcc_inventory_status_t libipcc_inventory_status(
    libipcc_inventory_t *);
extern uint8_t libipcc_inventory_type(libipcc_inventory_t *);
extern const uint8_t *libipcc_inventory_name(libipcc_inventory_t *, size_t *);
extern const uint8_t *libipcc_inventory_data(libipcc_inventory_t *, size_t *);
extern void libipcc_inventory_free(libipcc_inventory_t *);

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

#endif /* _LIBIPCC_H */
