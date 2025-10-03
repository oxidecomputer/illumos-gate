/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2025 Oxide Computer Company
 */

#ifndef	_SYS_IPCC_PROTO_H
#define	_SYS_IPCC_PROTO_H

/*
 * This file defines the types and prototypes for consumers of os/ipcc_proto.c
 */

#include <sys/stdbool.h>
#include <sys/types.h>
#include <sys/varargs.h>
#include <sys/ipcc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	IPCC_PROTOCOL_VERSION	1
#define	IPCC_MAGIC		0x1DE19CC

#define	IPCC_COBS_SIZE(x)	(1 + (x) + (x) / 0xfe)
#define	IPCC_MIN_PACKET_SIZE	IPCC_COBS_SIZE(IPCC_MIN_MESSAGE_SIZE)
/* Add one more to allow for the frame terminator */
#define	IPCC_MAX_PACKET_SIZE	(IPCC_COBS_SIZE(IPCC_MAX_MESSAGE_SIZE) + 1)

#define	IPCC_SEQ_MASK		0x7fffffffffffffffull
#define	IPCC_SEQ_REPLY		0x8000000000000000ull

typedef enum ipcc_channel_flag {
	/*
	 * Suppresses general information and progress messages from being
	 * logged.
	 */
	IPCC_CHAN_QUIET = 1 << 0
} ipcc_channel_flag_t;

typedef enum ipcc_hss_cmd {
	IPCC_HSS_REBOOT = 1,
	IPCC_HSS_POWEROFF,
	IPCC_HSS_BSU,
	IPCC_HSS_IDENT,
	IPCC_HSS_MACS,
	IPCC_HSS_BOOTFAIL,
	IPCC_HSS_PANIC,
	IPCC_HSS_STATUS,
	IPCC_HSS_ACKSTART,
	IPCC_HSS_ALERT,
	IPCC_HSS_ROT,
	IPCC_HSS_ADD_MEASUREMENTS,
	IPCC_HSS_IMAGEBLOCK,
	IPCC_HSS_KEYLOOKUP,
	IPCC_HSS_INVENTORY,
	IPCC_HSS_KEYSET,
	IPCC_HSS_APOBBEGIN,
	IPCC_HSS_APOBCOMMIT,
	IPCC_HSS_APOBDATA,
	IPCC_HSS_APOBREAD
} ipcc_hss_cmd_t;

typedef enum ipcc_sp_cmd {
	IPCC_SP_NONE = 0,
	IPCC_SP_ACK,
	IPCC_SP_DECODEFAIL,
	IPCC_SP_BSU,
	IPCC_SP_IDENT,
	IPCC_SP_MACS,
	IPCC_SP_STATUS,
	IPCC_SP_ALERT,
	IPCC_SP_ROT,
	IPCC_SP_IMAGEBLOCK,
	IPCC_SP_KEYLOOKUP,
	IPCC_SP_INVENTORY,
	IPCC_SP_KEYSET,
	IPCC_SP_APOBBEGIN,
	IPCC_SP_APOBCOMMIT,
	IPCC_SP_APOBDATA,
	IPCC_SP_APOBREAD
} ipcc_sp_cmd_t;

typedef enum ipcc_sp_decode_failure {
	IPCC_DECODEFAIL_COBS = 1,
	IPCC_DECODEFAIL_CRC,
	IPCC_DECODEFAIL_DESERIALIZE,
	IPCC_DECODEFAIL_MAGIC,
	IPCC_DECODEFAIL_VERSION,
	IPCC_DECODEFAIL_SEQUENCE,
	IPCC_DECODEFAIL_DATALEN
} ipcc_sp_decode_failure_t;

typedef enum ipcc_host_boot_failure {
	IPCC_BOOTFAIL_GENERAL = 1,
	IPCC_BOOTFAIL_NOPHASE2,
	IPCC_BOOTFAIL_HEADER,
	IPCC_BOOTFAIL_INTEGRITY,
	IPCC_BOOTFAIL_RAMDISK
} ipcc_host_boot_failure_t;

typedef enum ipcc_sp_status {
	IPCC_STATUS_STARTED		= 1 << 0,
	IPCC_STATUS_ALERT		= 1 << 1,
	IPCC_STATUS_RESET		= 1 << 2
} ipcc_sp_status_t;

typedef enum ipcc_sp_startup {
	IPCC_STARTUP_RECOVERY		= 1 << 0, /* phase2 recovery */
	IPCC_STARTUP_KBM		= 1 << 1, /* set kbm_debug */
	IPCC_STARTUP_BOOTRD		= 1 << 2, /* set bootrd_debug */
	IPCC_STARTUP_PROM		= 1 << 3, /* set prom_debug */
	IPCC_STARTUP_KMDB		= 1 << 4, /* boot with -k */
	IPCC_STARTUP_KMDB_BOOT		= 1 << 5, /* boot with -kd */
	IPCC_STARTUP_BOOT_RAMDISK	= 1 << 6, /* no phase 2, use ramdisk */
	IPCC_STARTUP_BOOT_NET		= 1 << 7, /* boot from network */
	IPCC_STARTUP_VERBOSE		= 1 << 8  /* boot with -v */
} ipcc_sp_startup_t;

typedef enum ipcc_apob_begin {
	IPCC_APOB_BEGIN_SUCCESS = 0,
	IPCC_APOB_BEGIN_NOTSUP,
	IPCC_APOB_BEGIN_INVALID_STATE,
	IPCC_APOB_BEGIN_INVALID_ALG,
	IPCC_APOB_BEGIN_INVALID_HASHLEN,
	IPCC_APOB_BEGIN_INVALID_LEN
} ipcc_apob_begin_t;

typedef enum ipcc_apob_alg {
	IPCC_APOB_ALG_SHA256 = 0
} ipcc_apob_alg_t;

typedef enum ipcc_apob_commit {
	IPCC_APOB_COMMIT_SUCCESS = 0,
	IPCC_APOB_COMMIT_NOTSUP,
	IPCC_APOB_COMMIT_INVALID_STATE,
	IPCC_APOB_COMMIT_INVALID_DATA,
	IPCC_APOB_COMMIT_FAILED
} ipcc_apob_commit_t;

typedef enum ipcc_apob_data {
	IPCC_APOB_DATA_SUCCESS = 0,
	IPCC_APOB_DATA_NOTSUP,
	IPCC_APOB_DATA_INVALID_STATE,
	IPCC_APOB_DATA_INVALID_OFFSET,
	IPCC_APOB_DATA_INVALID_SIZE,
	IPCC_APOB_DATA_FAILED,
	IPCC_APOB_DATA_NOT_ERASED
} ipcc_apob_data_t;

typedef enum ipcc_apob_read {
	IPCC_APOB_READ_SUCCESS = 0,
	IPCC_APOB_READ_NOTSUP,
	IPCC_APOB_READ_INVALID_STATE,
	IPCC_APOB_READ_NODATA,
	IPCC_APOB_READ_INVALID_OFFSET,
	IPCC_APOB_READ_INVALID_SIZE,
	IPCC_APOB_READ_FAILED
} ipcc_apob_read_t;

#define	IPCC_IDENT_DATALEN		106
#define	IPCC_BSU_DATALEN		1
#define	IPCC_MAC_DATALEN		9
#define	IPCC_STATUS_DATALEN		16
#define	IPCC_KEYSET_DATALEN		1
#define	IPCC_APOB_MAX_PAYLOAD		\
	(IPCC_MAX_DATA_SIZE - sizeof (uint64_t))
#define	IPCC_BOOTFAIL_MAX_PAYLOAD	\
	(IPCC_MAX_DATA_SIZE - sizeof (uint8_t))

/*
 * The Oxide SP reserves 2MiB to store the APOB. Any APOB larger than this is
 * unsupported. Note that AMD currently only reserves 850KiB in BIOS images so
 * there is some headroom.
 */
#define IPCC_APOB_MAX_SIZE		(2 * 1024 * 1024)

typedef enum ipcc_log_type {
	IPCC_LOG_DEBUG,
	IPCC_LOG_HEX,
	IPCC_LOG_WARNING
} ipcc_log_type_t;

typedef enum ipcc_pollevent {
	IPCC_INTR	= 1 << 0,
	IPCC_POLLIN	= 1 << 1,
	IPCC_POLLOUT	= 1 << 2
} ipcc_pollevent_t;

typedef struct ipcc_ops {
	int (*io_open)(void *);
	void (*io_close)(void *);
	void (*io_flush)(void *);
	int (*io_poll)(void *, ipcc_pollevent_t, ipcc_pollevent_t *, uint64_t);
	bool (*io_readintr)(void *);
	int (*io_read)(void *, uint8_t *, size_t *);
	int (*io_write)(void *, uint8_t *, size_t *);
	void (*io_log)(void *, ipcc_log_type_t, const char *, ...);
} ipcc_ops_t;

extern void ipcc_begin_multithreaded(void);
extern bool ipcc_channel_held(void);
extern int ipcc_acquire_channel(const ipcc_ops_t *, void *);
extern void ipcc_channel_setflags(ipcc_channel_flag_t);
extern void ipcc_release_channel(const ipcc_ops_t *, void *, bool);
extern int ipcc_reboot(const ipcc_ops_t *, void *);
extern int ipcc_poweroff(const ipcc_ops_t *, void *);
extern int ipcc_panic(const ipcc_ops_t *, void *, uint8_t *, size_t);
extern int ipcc_bsu(const ipcc_ops_t *, void *, uint8_t *);
extern int ipcc_ident(const ipcc_ops_t *, void *, ipcc_ident_t *);
extern int ipcc_macs(const ipcc_ops_t *, void *, ipcc_mac_t *);
extern int ipcc_keylookup(const ipcc_ops_t *, void *, ipcc_keylookup_t *,
    uint8_t *);
extern int ipcc_keyset(const ipcc_ops_t *, void *, ipcc_keyset_t *);
extern int ipcc_rot(const ipcc_ops_t *, void *, ipcc_rot_t *);
extern int ipcc_bootfail(const ipcc_ops_t *, void *, ipcc_host_boot_failure_t,
    const uint8_t *, size_t);
extern int ipcc_status(const ipcc_ops_t *, void *, uint64_t *, uint64_t *);
extern int ipcc_ackstart(const ipcc_ops_t *, void *);
extern int ipcc_imageblock(const ipcc_ops_t *, void *, uint8_t *, uint64_t,
    uint8_t **, size_t *);
extern int ipcc_inventory(const ipcc_ops_t *, void *, ipcc_inventory_t *);

extern const char *ipcc_apob_begin_errstr(ipcc_apob_begin_t);
extern const char *ipcc_apob_commit_errstr(ipcc_apob_commit_t);
extern const char *ipcc_apob_data_errstr(ipcc_apob_data_t);
extern const char *ipcc_apob_read_errstr(ipcc_apob_read_t);
extern int ipcc_apob_begin(const ipcc_ops_t *, void *, size_t, ipcc_apob_alg_t,
    uint8_t *, size_t, ipcc_apob_begin_t *);
extern int ipcc_apob_commit(const ipcc_ops_t *, void *, ipcc_apob_commit_t *);
extern int ipcc_apob_data(const ipcc_ops_t *, void *, uint64_t, const uint8_t *,
    size_t, ipcc_apob_data_t *);
extern int ipcc_apob_read(const ipcc_ops_t *, void *, uint64_t, uint8_t *,
    size_t *, ipcc_apob_read_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IPCC_PROTO_H */
