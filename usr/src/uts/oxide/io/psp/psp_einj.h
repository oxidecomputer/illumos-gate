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

#ifndef _IO_PSP_PSP_EINJ_H
#define	_IO_PSP_PSP_EINJ_H

/*
 * This file contains definitions and private ioctls for interfacing with the
 * PSP Error Injection (EINJ) driver.
 */

#include <sys/stdint.h>
#include <psp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minor node for issuing commands via the PSP Error Injection driver.
 */
#define	PSP_EINJ_MINOR_NAME	"einj"
#define	PSP_EINJ_MINOR_NUM	0

#define	PSP_EINJ_IOC		PSP_IOCTL(EINJ)
#define	PSP_EINJ_IOC_INJECT	(PSP_EINJ_IOC | 0x01)

/*
 * Supported types of error for injection via this driver.
 */
typedef enum psp_einj_type {
	PSP_EINJ_TYPE_MEM_CORRECTABLE,
	PSP_EINJ_TYPE_MEM_UNCORRECTABLE,
	PSP_EINJ_TYPE_MEM_FATAL,
	PSP_EINJ_TYPE_PCIE_CORRECTABLE,
	PSP_EINJ_TYPE_PCIE_UNCORRECTABLE,
	PSP_EINJ_TYPE_PCIE_FATAL,
} psp_einj_type_t;

/*
 * Error injection type and details submitted via PSP_EINJ_IOC_EINJ.
 */
typedef struct psp_einj_req {
	psp_einj_type_t			per_type;
	uint32_t			per_no_trigger;
	union {
		struct {
			uint64_t	per_mem_addr;
			uint64_t	per_mem_addr_mask;
		};
		struct {
			uint8_t		per_pcie_reserved:8;
			uint8_t		per_pcie_func:3;
			uint8_t		per_pcie_dev:5;
			uint8_t		per_pcie_bus;
			uint8_t		per_pcie_seg;
		};
		uint32_t		per_pcie_sbdf;
	};
} psp_einj_req_t;

#ifdef __cplusplus
}
#endif

#endif /* _IO_PSP_PSP_EINJ_H */
