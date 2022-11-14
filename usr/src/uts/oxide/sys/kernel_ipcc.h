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

#ifndef _SYS_KERNEL_IPCC_H
#define	_SYS_KERNEL_IPCC_H

#include <sys/ipcc_proto.h>
#include <sys/types.h>
#include <sys/varargs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	IPCC_INIT_UNSET = 0,
	IPCC_INIT_EARLYBOOT,
	IPCC_INIT_ENABLE_INTERRUPT,
	IPCC_INIT_KVMAVAIL,
	IPCC_INIT_DEVTREE,
} ipcc_init_t;

void kernel_ipcc_init(ipcc_init_t);
extern void kernel_ipcc_reboot(void);
extern void kernel_ipcc_poweroff(void);
extern int kernel_ipcc_bsu(uint8_t *);
extern int kernel_ipcc_ident(ipcc_ident_t *);
extern int kernel_ipcc_status(uint64_t *, uint64_t *);
extern int kernel_ipcc_ackstart(void);
extern int kernel_ipcc_bootfailv(ipcc_host_boot_failure_t, const char *,
    va_list);
extern int kernel_ipcc_bootfail(ipcc_host_boot_failure_t, const char *, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_KERNEL_IPCC_H */
