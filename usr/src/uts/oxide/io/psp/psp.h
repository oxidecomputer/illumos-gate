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
 * Copyright 2025 Oxide Computer Company
 */

#ifndef _IO_PSP_PSP_H
#define	_IO_PSP_PSP_H

/*
 * This file contains definitions and private ioctls for interfacing with the
 * PSP driver.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minor node for issuing commands via the PSP driver.
 */
#define	PSP_MINOR_NAME	"psp"
#define	PSP_MINOR_NUM	0

/*
 * The PSP driver is a nexus driver that provides mediated access with specific
 * functionality implemented by separate child drivers. This enum represents the
 * set of such child drivers.
 */
typedef enum psp_child {
	PSP_C_INVAL,
	PSP_C_EINJ = 1,
	/*
	 * This limit is artifical so that we may have a shared IOCTL space for
	 * all PSP drivers.
	 */
	PSP_C_MAX = 0xF,
} psp_child_t;

#define	PSP_IOCTL_BASE	(('p' << 24) | ('s' << 16) | ('p' << 8))
#define	PSP_IOCTL(X)	(PSP_IOCTL_BASE | (PSP_C_##X << 4))

#define	PSP_IOC			PSP_IOCTL_BASE
#define	PSP_IOC_GET_VERS	(PSP_IOC | 0x01)

/*
 * PSP firmware versions returned via PSP_IOC_GET_VERS.
 */
typedef struct psp_versions {
	/*
	 * The PSP's own firmware version.
	 */
	uint8_t		pv_psp[4];
	/*
	 * The AGESA Boot Loader (ABL) version.
	 */
	uint8_t		pv_agesa[4];
	/*
	 * The System Management Unit (SMU) firmware version.
	 */
	uint8_t		pv_smu[4];
} psp_versions_t;

#ifdef __cplusplus
}
#endif

#endif /* _IO_PSP_PSP_H */
