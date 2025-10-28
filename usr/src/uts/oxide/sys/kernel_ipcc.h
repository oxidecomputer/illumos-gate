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

#ifndef _SYS_KERNEL_IPCC_H
#define	_SYS_KERNEL_IPCC_H

#include <sys/ipcc_proto.h>
#include <sys/types.h>
#include <sys/privregs.h>
#include <sys/varargs.h>
#include <sys/debug.h>
#include <sys/apob.h>

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
extern int kernel_ipcc_acquire(ipcc_channel_flag_t);
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
extern apob_hdl_t *kernel_ipcc_apobread(void);
extern void kernel_ipcc_apobfree(apob_hdl_t *);
extern int kernel_ipcc_apobwrite(const apob_hdl_t *);
extern int kernel_ipcc_imageblock(uint8_t *, uint64_t, uint8_t **, size_t *);

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

#define	IPCC_PANIC_VERSION	2

typedef enum {
	/*
	 * An empty item that should be disregarded. This is chosen as 0 so
	 * that any trailing NUL bytes in the data are considered to be this
	 * type.
	 */
	IPI_NOP = 0,
	/*
	 * A message associated with the panic. Typically the "panic string".
	 * A sequence of printable characters.
	 */
	IPI_MESSAGE,
	/*
	 * An element of the stack trace for this panic. The data is a
	 * ipcc_panic_stackentry_t. If the symbol name cannot be resolved, it
	 * will be zero length.
	 */
	IPI_STACKENTRY,
	/*
	 * Additional ancillary data associated with the panic. A sequence of
	 * bytes, not necessarily printable.
	 */
	IPI_ANCIL,
} ipcc_panic_item_t;

typedef struct ipcc_panic_tlvhdr {
	uint8_t		ipth_type;
	uint16_t	ipth_len;
	uint8_t		ipth_data[];
} __packed ipcc_panic_tlvhdr_t;

typedef struct ipcc_panic_stackentry {
	uint64_t	ipse_addr;
	uint64_t	ipse_offset;
	uint8_t		ipse_symbol[];
} __packed ipcc_panic_stackentry_t;

typedef struct ipcc_panic_data {
	uint8_t			ipd_version;
	uint16_t		ipd_cause;
	uint32_t		ipd_error;

	hrtime_t		ipd_hrtime;
	timespec_t		ipd_hrestime;

	uint32_t		ipd_cpuid;
	uint64_t		ipd_thread;
	uint64_t		ipd_addr;
	uint64_t		ipd_pc;
	uint64_t		ipd_fp;
	uint64_t		ipd_rp;

	struct regs		ipd_regs;

	/*
	 * The remaining panic data in ipd_data is a sequence of TLV-encoded
	 * records. Each item is an ipcc_panic_tlvhdr_t followed by
	 * type-specific data; see the definition of ipcc_panic_item_t for more
	 * details.
	 */
	uint16_t		ipd_nitems;
	uint16_t		ipd_items_len;
	uint8_t			ipd_items[IPCC_MAX_DATA_SIZE - 0x150];
} __packed ipcc_panic_data_t;

CTASSERT(sizeof (ipcc_panic_data_t) <= IPCC_MAX_DATA_SIZE);

typedef enum {
	IPF_CAUSE,
	IPF_ERROR,
	IPF_CPUID,
	IPF_THREAD,
	IPF_ADDR,
	IPF_PC,
	IPF_FP,
	IPF_RP,
} ipcc_panic_field_t;

extern void kipcc_panic_field(ipcc_panic_field_t, uint64_t);
extern void kipcc_panic_regs(struct regs *);
extern void kipcc_panic_vmessage(const char *, va_list);
extern void kipcc_panic_message(const char *, ...);
extern void kipcc_panic_stack_item(uintptr_t, const char *, off_t);
extern void kipcc_panic_vdata(const char *, va_list);
extern void kipcc_panic_data(const char *, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_KERNEL_IPCC_H */
