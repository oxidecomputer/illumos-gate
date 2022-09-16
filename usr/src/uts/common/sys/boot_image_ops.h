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

#ifndef _BOOT_IMAGE_OPS_H
#define	_BOOT_IMAGE_OPS_H

/*
 * boot_image_ops: a mechanism for interposing on boot right before
 * vfs_mountroot(), to locate or fetch an appropriate root file system.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	_BOOT_IMAGE_OPS_MAGIC		0xB0075000
#define	_BOOT_IMAGE_OPS_VERSION(n)	(_BOOT_IMAGE_OPS_MAGIC | (n))

#define	BOOT_IMAGE_OPS_VERSION		_BOOT_IMAGE_OPS_VERSION(1)

typedef struct boot_image_ops {
	uint32_t bimo_version;
	void (*bimo_locate)(void);
} boot_image_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* _BOOT_IMAGE_OPS_H */
