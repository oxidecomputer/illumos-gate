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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _UPMT_H
#define	_UPMT_H

#include <sys/types.h>

/*
 * Private ioctls for interfacing with the upmt driver. The minor number
 * selects the DF (IO die) whose PM table is exposed.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	UPMT_IOCTL	(('u' << 24) | ('p' << 16) | ('t' << 8))

#define	UPMT_INFO		(UPMT_IOCTL | 0x00)
#define	UPMT_REFRESH		(UPMT_IOCTL | 0x01)
#define	UPMT_READ		(UPMT_IOCTL | 0x02)

typedef struct upmt_info {
	uint32_t	ui_version;	/* out: table layout version */
	uint32_t	ui_pad;
	uint64_t	ui_len;		/* out: table size in bytes */
} upmt_info_t;

/*
 * The user buffer address is carried as a uint64_t so that the structure has
 * the same layout in all data models.
 *
 * If the supplied buffer is smaller than the table, nothing is copied;
 * ur_len is updated to the required size and the ioctl fails with EOVERFLOW,
 * so a caller can size a buffer by passing ur_len of 0. The table contents
 * are defined only when the ioctl succeeds, and a successful read is always
 * of the full table.
 */
typedef struct upmt_read {
	uint32_t	ur_version;	/* out: table layout version */
	uint32_t	ur_pad;
	uint64_t	ur_buf;		/* in: user buffer address */
	uint64_t	ur_len;		/* in: buffer size; out: table size */
} upmt_read_t;

#ifdef __cplusplus
}
#endif

#endif /* _UPMT_H */
