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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef	_OXIDE_BOOT_H
#define	_OXIDE_BOOT_H

#include <sys/stdbool.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/crypto/api.h>
#include <sys/zmod.h>

/*
 * Oxide Boot: mechanisms to obtain boot ramdisk image, from either local
 * storage or over ethernet.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	OXBOOT_RAMDISK_NAME	"rpool"

#define	OXBOOT_DATASET_LEN	128
#define	OXBOOT_CSUMLEN_SHA256	32
#define	OXBOOT_CSUMBUF_SHA256	(OXBOOT_CSUMLEN_SHA256 * 2 + 1)
/*
 * Images should be less than 4GiB, because that would be too large!
 * serves as another validity check on the header.
 */
#define	OXBOOT_MAX_IMAGE_SIZE		(4UL * 1024 * 1024 * 1024)

typedef struct oxide_boot {
	/*
	 * Data marked I is initialised once when the object is created.  Other
	 * data, marked M, is protected by oxb_mutex.
	 */
	kmutex_t	oxb_mutex;

	ldi_ident_t	oxb_li;					/* I */
	ldi_handle_t	oxb_rd_disk;				/* M */

	uint64_t	oxb_ramdisk_data_size;			/* M */
	uint64_t	oxb_ramdisk_size;			/* M */
	char		*oxb_ramdisk_path;			/* M */
	char		*oxb_ramdisk_dataset;			/* M */

	uint8_t		oxb_csum_want[OXBOOT_CSUMLEN_SHA256];	/* I */
	uint8_t		oxb_csum_have[OXBOOT_CSUMLEN_SHA256];	/* M */

	/*
	 * Although the ramdisk device accepts writes to arbitrary offsets, it
	 * does not appear to put the data where one might expect if the
	 * offsets are not aligned to DEV_BSIZE. This appears to be a bug in
	 * the ramdisk driver in that it should either deal with unaligned
	 * writes properly or reject them; TBD. To work around this for now,
	 * data (after inflation if a compressed image is being read) are
	 * accumulated in oxb_block and written to the ramdisk in chunks
	 * aligned to DEV_BSIZE.
	 */
	uint8_t		oxb_block[DEV_BSIZE];			/* M */
	size_t		oxb_acc;				/* M */
	size_t		oxb_opos;				/* M */

	bool		oxb_compressed;				/* M */
	zmod_stream_t	*oxb_zstream;				/* M */
} oxide_boot_t;

extern char *oxide_format_sum(char *, size_t, const uint8_t *);

extern bool oxide_boot_ramdisk_create(oxide_boot_t *, uint64_t);
extern bool oxide_boot_ramdisk_write_append(oxide_boot_t *, uint8_t *, size_t);
extern bool oxide_boot_ramdisk_write_iov_offset(oxide_boot_t *, iovec_t *,
    uint_t, uint64_t);
extern bool oxide_boot_ramdisk_write_flush(oxide_boot_t *);
extern bool oxide_boot_ramdisk_set_len(oxide_boot_t *, uint64_t);
extern bool oxide_boot_ramdisk_set_csum(oxide_boot_t *, uint8_t *, size_t);
extern bool oxide_boot_ramdisk_set_dataset(oxide_boot_t *, const char *);
extern bool oxide_boot_set_compressed(oxide_boot_t *);

extern bool oxide_boot_disk_read(ldi_handle_t, uint64_t, uint8_t *, size_t);

extern bool oxide_boot_net(oxide_boot_t *);
extern bool oxide_boot_disk(oxide_boot_t *, int);
extern bool oxide_boot_sp(oxide_boot_t *);

#ifdef __cplusplus
}
#endif

#endif /* _OXIDE_BOOT_H */
