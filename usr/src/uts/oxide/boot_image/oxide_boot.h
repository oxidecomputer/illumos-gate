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

#include <sys/crypto/api.h>

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

typedef struct oxide_boot {
	kmutex_t		oxb_mutex;

	ldi_ident_t		oxb_li;
	ldi_handle_t		oxb_rd_ctl;
	ldi_handle_t		oxb_rd_disk;

	uint64_t		oxb_ramdisk_data_size;
	uint64_t		oxb_ramdisk_size;
	char			*oxb_ramdisk_path;
	char			*oxb_ramdisk_dataset;

	crypto_context_t	oxb_crypto;
	crypto_mechanism_t	oxb_mechanism;

	uint8_t			oxb_csum_want[OXBOOT_CSUMLEN_SHA256];
	uint8_t			oxb_csum_have[OXBOOT_CSUMLEN_SHA256];
} oxide_boot_t;

extern bool oxide_boot_ramdisk_create(oxide_boot_t *, uint64_t);
extern bool oxide_boot_ramdisk_write(oxide_boot_t *, iovec_t *, uint_t,
    uint64_t);
extern bool oxide_boot_ramdisk_set_len(oxide_boot_t *, uint64_t);
extern bool oxide_boot_ramdisk_set_csum(oxide_boot_t *, uint8_t *, size_t);
extern bool oxide_boot_ramdisk_set_dataset(oxide_boot_t *, const char *);

extern bool oxide_boot_disk_read(ldi_handle_t, uint64_t, char *, size_t);

extern bool oxide_boot_net(oxide_boot_t *);
extern bool oxide_boot_disk(oxide_boot_t *);

#ifdef __cplusplus
}
#endif

#endif /* _OXIDE_BOOT_H */
