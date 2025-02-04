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

#ifndef	_SYS_IPCC_H
#define	_SYS_IPCC_H

#include <sys/ethernet.h>
#include <sys/types.h>
#include <sys/debug.h>

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
#define	IPCC_IMAGEBLOCK		(IPCC_IOC|6)
#define	IPCC_INVENTORY		(IPCC_IOC|7)
#define	IPCC_KEYSET		(IPCC_IOC|8)
#define	IPCC_APOB		(IPCC_IOC|9)

/*
 * The minimum message size is a protocol detail that should be in
 * sys/ipcc_proto.h, but it is here in order that the max data size can be
 * calculated for use in messages which use opaque data.
 * IPCC_MIN_MESSAGE_SIZE is the size of the protocol header fields and the
 * checksum - i.e. the size of a message with no associated data.
 * IPCC_MAX_MESSAGE_SIZE is chosen to allow a message to contain a full 4KiB of
 * data with an additional 64-bits in the data portion of the message.
 */
#define	IPCC_MIN_MESSAGE_SIZE	19
#define	IPCC_MAX_MESSAGE_SIZE	4123
#define	IPCC_MAX_DATA_SIZE	(IPCC_MAX_MESSAGE_SIZE - IPCC_MIN_MESSAGE_SIZE)

/* Keep synchronised with the header definition in boot_image/oxide_boot_sp.h */
#define	IPCC_IMAGE_HASHLEN		32

/*
 * Both model and serial numbers are currently 11 bytes on Gimlet, but the
 * buffers are sized to allow 50 bytes and a string terminator to provide some
 * level of future proofing. If this is ever exceeded, it will be necessary to
 * grow the message and increase the protocol version. It is still a short
 * message from the SP.
 */
#define	MODEL_STRING_SIZE	51
#define	IDENT_STRING_SIZE	51
typedef struct ipcc_ident {
	uint8_t		ii_model[MODEL_STRING_SIZE];	/* 913-nnnnnnn */
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

/*
 * A keyset message is prefixed by a uint8_t that selects the slot being
 * written.
 */
#define	IPCC_KEYSET_MAX_PAYLOAD (IPCC_MAX_DATA_SIZE - sizeof (uint8_t))

typedef struct ipcc_keyset {
	uint8_t		iks_result;
	uint8_t		iks_key;
	uint16_t	iks_datalen;
	uint8_t		iks_data[IPCC_KEYSET_MAX_PAYLOAD];
} ipcc_keyset_t;

/*
 * A keylookup response is prefixed by a uint8_t response code.
 */
#define	IPCC_KEYLOOKUP_MAX_PAYLOAD (IPCC_MAX_DATA_SIZE - sizeof (uint8_t))

#define	IPCC_KEY_PING			0
#define	IPCC_KEY_INSTALLINATOR_IMAGE_ID	1
#define	IPCC_KEY_INVENTORY		2
#define	IPCC_KEY_ETC_SYSTEM		3
#define	IPCC_KEY_DTRACE_CONF		4

/*
 * We wish to send APOB data in 4KiB chunks. An APOB message is prefixed by a
 * uint64_t that specifies the offset of the data in the payload and we assert
 * that there is room for this.
 */
#define	IPCC_APOB_MAX_PAYLOAD 0x1000
CTASSERT(IPCC_APOB_MAX_PAYLOAD <= IPCC_MAX_DATA_SIZE - sizeof (uint64_t));

typedef struct ipcc_apob {
	uint8_t		ia_result;
	uint64_t	ia_offset;
	uint16_t	ia_datalen;
	uint8_t		ia_data[IPCC_APOB_MAX_PAYLOAD];
} ipcc_apob_t;

typedef struct ipcc_imageblock {
	uint8_t		ii_hash[IPCC_IMAGE_HASHLEN];
	uint64_t	ii_offset;
	uint16_t	ii_buflen;
	uint16_t	ii_datalen;
	uint8_t		*ii_buf;
} ipcc_imageblock_t;

#if defined(_SYSCALL32)
typedef struct ipcc_keylookup32 {
	uint8_t		ik_key;
	uint16_t	ik_buflen;
	uint8_t		ik_result;
	uint16_t	ik_datalen;
	caddr32_t	ik_buf;
} ipcc_keylookup32_t;

typedef struct ipcc_imageblock32 {
	uint8_t		ii_hash[IPCC_IMAGE_HASHLEN];
	uint64_t	ii_offset;
	uint16_t	ii_buflen;
	uint16_t	ii_datalen;
	caddr32_t	ii_buf;
} ipcc_imageblock32_t;
#endif /* _SYSCALL32 */

#define	IPCC_KEYLOOKUP_SUCCESS		0
#define	IPCC_KEYLOOKUP_UNKNOWN_KEY	1
#define	IPCC_KEYLOOKUP_NO_VALUE		2
#define	IPCC_KEYLOOKUP_BUFFER_TOO_SMALL	3

#define	IPCC_KEYSET_SUCCESS		0
#define	IPCC_KEYSET_UNKNOWN_KEY		1
#define	IPCC_KEYSET_READONLY		2
#define	IPCC_KEYSET_TOO_LONG		3

#define	IPCC_APOB_SUCCESS		0
#define	IPCC_APOB_BAD_OFFSET		1

typedef struct ipcc_rot {
	uint64_t	ir_len;
	uint8_t		ir_data[IPCC_MAX_DATA_SIZE];
} ipcc_rot_t;

#define	IPCC_INVENTORY_NAMELEN	32
#define	IPCC_INVENTORY_DATALEN	\
	(IPCC_MAX_DATA_SIZE - IPCC_INVENTORY_NAMELEN - sizeof (uint8_t) * 2)

#define	IPCC_INVENTORY_SUCCESS		0
#define	IPCC_INVENTORY_INVALID_INDEX	1
#define	IPCC_INVENTORY_IO_DEV_MISSING	2
#define	IPCC_INVENTORY_IO_ERROR		3

typedef struct ipcc_inventory {
	uint32_t	iinv_idx;
	uint8_t		iinv_res;
	uint8_t		iinv_name[IPCC_INVENTORY_NAMELEN];
	uint8_t		iinv_type;
	uint16_t	iinv_data_len;
	uint8_t		iinv_data[IPCC_INVENTORY_DATALEN];
} ipcc_inventory_t;

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_IPCC_H */
