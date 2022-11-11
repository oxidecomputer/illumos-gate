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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef	_SYS_IPCC_PROTO_H
#define	_SYS_IPCC_PROTO_H

/*
 * This file defines the types and prototypes for consumers of os/ipcc_proto.c
 */

#include <sys/stdbool.h>
#include <sys/ipcc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	IPCC_VERSION		1
#define	IPCC_MAGIC		0x1DE19CC

#define	IPCC_COBS_SIZE(x)	(1 + (x) + (x) / 0xfe)
#define	IPCC_MIN_PACKET_SIZE	IPCC_COBS_SIZE(IPCC_MIN_MESSAGE_SIZE)
#define	IPCC_MAX_PACKET_SIZE	IPCC_COBS_SIZE(IPCC_MAX_MESSAGE_SIZE)

#define	IPCC_SEQ_MASK		0x7fffffffffffffffull
#define	IPCC_SEQ_REPLY		0x8000000000000000ull

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
} ipcc_sp_cmd_t;

typedef enum ipcc_sp_decode_failure {
	IPCC_DECODEFAIL_COBS = 1,
	IPCC_DECODEFAIL_CRC,
	IPCC_DECODEFAIL_DESERIALIZE,
	IPCC_DECODEFAIL_MAGIC,
	IPCC_DECODEFAIL_VERSION,
	IPCC_DECODEFAIL_SEQUENCE,
	IPCC_DECODEFAIL_DATALEN,
} ipcc_sp_decode_failure_t;

typedef enum ipcc_sp_status {
	IPCC_STATUS_STARTED		= 1 << 0,
	IPCC_STATUS_ALERT		= 1 << 1,
	IPCC_STATUS_RESET		= 1 << 2,
} ipcc_sp_status_t;

typedef enum ipcc_sp_startup {
	IPCC_STARTUP_RECOVERY		= 1 << 0, /* phase2 recovery */
	IPCC_STARTUP_KBM		= 1 << 1, /* set kbm_debug */
	IPCC_STARTUP_BOOTRD		= 1 << 2, /* set bootrd_debug */
	IPCC_STARTUP_PROM		= 1 << 3, /* set prom_debug */
	IPCC_STARTUP_KMDB		= 1 << 4, /* boot with -k */
	IPCC_STARTUP_KMDB_BOOT		= 1 << 5, /* boot with -kd */
} ipcc_sp_startup_t;

#define	IPCC_IDENT_DATALEN	106
#define	IPCC_BSU_DATALEN	1
#define	IPCC_MAC_DATALEN	9
#define	IPCC_STATUS_DATALEN	16

typedef enum ipcc_log_type {
	IPCC_LOG_DEBUG,
	IPCC_LOG_HEX,
} ipcc_log_type_t;

typedef enum ipcc_pollevent {
	IPCC_INTR	= 1 << 0,
	IPCC_POLLIN	= 1 << 1,
	IPCC_POLLOUT	= 1 << 2,
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
extern int ipcc_reboot(const ipcc_ops_t *, void *);
extern int ipcc_poweroff(const ipcc_ops_t *, void *);
extern int ipcc_bsu(const ipcc_ops_t *, void *, uint8_t *);
extern int ipcc_ident(const ipcc_ops_t *, void *, ipcc_ident_t *);
extern int ipcc_macs(const ipcc_ops_t *, void *, ipcc_mac_t *);
extern int ipcc_rot(const ipcc_ops_t *, void *, ipcc_rot_t *);
extern int ipcc_bootfail(const ipcc_ops_t *, void *, uint8_t);
extern int ipcc_status(const ipcc_ops_t *, void *, uint64_t *, uint64_t *);
extern int ipcc_ackstart(const ipcc_ops_t *, void *);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IPCC_PROTO_H */
