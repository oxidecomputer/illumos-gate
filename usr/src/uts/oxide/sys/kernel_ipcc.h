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

/*
 * The sizes of various static buffers in the panic data structures. The sizes
 * of these are a balance between having enough space to record panic
 * information and being able to fit the final packed structure into a
 * message to the SP.
 */
#define	IPCC_PANIC_STACKS		0x10
#define	IPCC_PANIC_DATALEN		0x100
#define	IPCC_PANIC_SYMLEN		0x20
#define	IPCC_PANIC_MSGLEN		0x80

/*
 * Panic reasons used to populate ipd_cause in ipcc_panic_data.
 * Some of these values are combined with additional data in the lower byte,
 * for example a page fault trap will be encoded as 0xa90e.
 */
#define	IPCC_PANIC_CALL			0xca11
#define	IPCC_PANIC_TRAP			0xa900
#define	IPCC_PANIC_USERTRAP		0x5e00
#define	IPCC_PANIC_EARLYBOOT		0xeb00
#define	IPCC_PANIC_EARLYBOOT_PROM	0xeb97
#define	IPCC_PANIC_EARLYBOOT_TRAP	0xeba9

/*
 * The ipcc_panic_data structure is sent to the SP over the IPCC as a raw data
 * stream. It is __packed to allow deserialisation with hubpack downstream,
 * and to save space.
 */
typedef struct ipcc_panic_stack {
	char		ips_symbol[IPCC_PANIC_SYMLEN];
	uintptr_t	ips_addr;
	off_t		ips_offset;
} __packed ipcc_panic_stack_t;

#define	IPCC_PANIC_VERSION	1

typedef struct ipcc_panic_data {
	uint8_t			ipd_version;
	uint16_t		ipd_cause;
	uint32_t		ipd_error;

	uint32_t		ipd_cpuid;
	uint64_t		ipd_thread;
	uint64_t		ipd_addr;
	uint64_t		ipd_pc;
	uint64_t		ipd_fp;
	uint64_t		ipd_rp;

	char			ipd_message[IPCC_PANIC_MSGLEN];

	uint8_t			ipd_stackidx;
	ipcc_panic_stack_t	ipd_stack[IPCC_PANIC_STACKS];

	uint_t			ipd_dataidx;
	char			ipd_data[IPCC_PANIC_DATALEN];
} __packed ipcc_panic_data_t;

typedef enum ipcc_panic_field {
	IPF_CAUSE,
	IPF_ERROR,
	IPF_CPUID,
	IPF_THREAD,
	IPF_ADDR,
	IPF_PC,
	IPF_FP,
	IPF_RP,
} ipcc_panic_field_t;

void kernel_ipcc_init(ipcc_init_t);
extern int kernel_ipcc_acquire(void);
extern void kernel_ipcc_release(void);
extern void kernel_ipcc_reboot(void);
extern void kernel_ipcc_poweroff(void);
extern void kernel_ipcc_panic(void);
extern int kernel_ipcc_bsu(uint8_t *);
extern int kernel_ipcc_ident(ipcc_ident_t *);
extern int kernel_ipcc_status(uint64_t *, uint64_t *);
extern int kernel_ipcc_ackstart(void);
extern int kernel_ipcc_bootfailv(ipcc_host_boot_failure_t, const char *,
    va_list);
extern int kernel_ipcc_bootfail(ipcc_host_boot_failure_t, const char *, ...);
extern int kernel_ipcc_keylookup(uint8_t, uint8_t *, size_t *);
extern int kernel_ipcc_imageblock(uint8_t *, uint64_t, uint8_t **, size_t *);

extern void kipcc_panic_field(ipcc_panic_field_t, uint64_t);
extern void kipcc_panic_vmessage(const char *, va_list);
extern void kipcc_panic_message(const char *, ...);
extern void kipcc_panic_stack_item(uintptr_t, const char *, off_t);
extern void kipcc_panic_vdata(const char *, va_list);
extern void kipcc_panic_data(const char *, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_KERNEL_IPCC_H */
