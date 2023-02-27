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

#ifndef	_OXIDE_BOOT_SP_H
#define	_OXIDE_BOOT_SP_H

#include <sys/types.h>
#include <sys/ipcc.h>

/*
 * The protocol used between this module and MGS, via the SP, consists of a
 * header block and then data which comprises the image itself. For now, MGS
 * just sends the raw header from the phase 2 disk image, and so the
 * definitions here match those in oxide_boot_disk.c - this will change in the
 * future to incorporate more data that the system needs (such as the identity
 * of the real phase 2 hash which should be subsequently fetched and
 * installed).
 */
#define	OXBOOT_SP_VERSION		2

#define	OXBOOT_SP_MAGIC			0x1DEB0075
#define	OXBOOT_SP_HEADER_SIZE		0x1000

#define	OBSH_FLAG_COMPRESSED		0x1

#define	OXBOOT_SP_DATASET_LEN	128
#define	OXBOOT_SP_IMAGENAME_LEN	128

typedef struct oxide_boot_sp_header {
	uint32_t obsh_magic;
	uint32_t obsh_version;

	uint64_t obsh_flags;
	uint64_t obsh_data_size;
	uint64_t obsh_image_size;
	uint64_t obsh_target_size;

	uint8_t obsh_sha256[IPCC_IMAGE_HASHLEN];

	char obsh_dataset[OXBOOT_SP_DATASET_LEN];
	char obsh_imagename[OXBOOT_SP_IMAGENAME_LEN];
} oxide_boot_sp_header_t;

#endif /* _OXIDE_BOOT_SP_H */
