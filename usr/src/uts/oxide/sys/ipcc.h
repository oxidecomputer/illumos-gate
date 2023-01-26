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
 * Copyright 2023 Oxide Computer Company
 */

#ifndef	_SYS_IPCC_H
#define	_SYS_IPCC_H

#include <sys/ethernet.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	IPCC_DEV		"/dev/ipcc"

/*
 * ioctl numbers
 */
#define	IPCC_IOC		(('i'<<24)|('c'<<16)|('c'<<8))

#define	IPCC_GET_VERSION	(IPCC_IOC|0)
#define	IPCC_STATUS		(IPCC_IOC|1)
#define	IPCC_IDENT		(IPCC_IOC|2)
#define	IPCC_MACS		(IPCC_IOC|3)
#define	IPCC_KEYLOOKUP		(IPCC_IOC|4)
#define	IPCC_ROT		(IPCC_IOC|5)

/*
 * The minimum message size is a protocol detail that should be in
 * sys/ipcc_proto.h, but it is here in order that the max data size can be
 * calculated for use in messages which use opaque data.
 * IPCC_MIN_MESSAGE_SIZE is the size of the protocol header fields and the
 * checksum - i.e. the size of a message with no associated data.
 * IPCC_MAX_MESSAGE_SIZE is chosen to allow a message to contain a full 4KiB of
 * data with an additional 64-bits in the data portion of the message.
 * XXX - this may be revised once the details of the phase2 image transfer and
 * RoT messages are further along.
 */
#define	IPCC_MIN_MESSAGE_SIZE	19
#define	IPCC_MAX_MESSAGE_SIZE	4123
#define	IPCC_MAX_DATA_SIZE	(IPCC_MAX_MESSAGE_SIZE - IPCC_MIN_MESSAGE_SIZE)

/*
 * Both model and serial numbers are currently 11 bytes on Gimlet, but the
 * buffers are sized to allow 50 bytes and a string terminator to provide some
 * level of future proofing. If this is ever exceeded, it will be necessary to
 * grow the message and increase the protocol version. It is still a short
 * message from the SP.
 */
#define	IDENT_STRING_SIZE	51
typedef struct ipcc_ident {
	uint8_t		ii_model[IDENT_STRING_SIZE];	/* 913-nnnnnnn */
	uint8_t		ii_serial[IDENT_STRING_SIZE];	/* MMSWWYYnnnn */
	uint32_t	ii_rev;
} ipcc_ident_t;

typedef struct ipcc_mac {
	uint16_t	im_count;
	uint8_t		im_base[ETHERADDRL];
	uint8_t		im_stride;
} ipcc_mac_t;

typedef struct ipcc_status {
	uint64_t	is_status;
	uint64_t	is_startup;
} ipcc_status_t;

typedef struct ipcc_keylookup {
	uint8_t		ik_key;
	uint16_t	ik_buflen;
	uint8_t		ik_result;
	uint16_t	ik_datalen;
	uint8_t		*ik_buf;
} ipcc_keylookup_t;

#if defined(_SYSCALL32)
typedef struct ipcc_keylookup32 {
	uint8_t		ik_key;
	uint16_t	ik_buflen;
	uint8_t		ik_result;
	uint16_t	ik_datalen;
	caddr32_t	ik_buf;
} ipcc_keylookup32_t;
#endif

#define	IPCC_KEYLOOKUP_SUCCESS		0
#define	IPCC_KEYLOOKUP_UNKNOWN_KEY	1
#define	IPCC_KEYLOOKUP_NO_VALUE		2
#define	IPCC_KEYLOOKUP_BUFFER_TOO_SMALL	3

typedef struct ipcc_rot {
	uint64_t	ir_len;
	uint8_t		ir_data[IPCC_MAX_DATA_SIZE];
} ipcc_rot_t;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IPCC_H */
