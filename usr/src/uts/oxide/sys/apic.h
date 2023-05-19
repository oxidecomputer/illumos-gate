/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1993, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2018 Joyent, Inc.
 * Copyright (c) 2017 by Delphix. All rights reserved.
 * Copyright 2022 Oxide Computer Co.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#ifndef _SYS_APIC_H
#define	_SYS_APIC_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

#include <sys/psm_types.h>
#include <sys/avintr.h>
#include <sys/pci.h>
#include <sys/psm_common.h>
#include <sys/acpi/acpi.h>	/* XXX needed by following */
#include <sys/acpica.h>		/* XXX iflag_t */

#endif	/* !_ASM */

#define	APIC_APIX_NAME		"apix"

#define	APIC_LOCAL_ADDR	0xfee00000
#define	APIC_IO_MEMLEN	0xf
#define	APIC_LOCAL_MEMLEN	0xfffff

/* Local Unit ID register */
#define	APIC_LID_REG		0x8

/* I/o Unit Version Register */
#define	APIC_VERS_REG		0xc

/* Task Priority register */
#define	APIC_TASK_REG		0x20

/* Arbitration Priority register */
#define	APIC_ARB_PRI_REG	0x24

/* Processor Priority register */
#define	APIC_PROC_PRI_REG	0x28

/* EOI register */
#define	APIC_EOI_REG		0x2c

/* Remote Read register		*/
#define	APIC_REMOTE_READ	0x30

/* Logical Destination register */
#define	APIC_DEST_REG		0x34

/* Destination Format register */
#define	APIC_FORMAT_REG		0x38

/* Spurious Interrupt Vector register */
#define	APIC_SPUR_INT_REG	0x3c

#define	APIC_IN_SVC_BASE_REG	0x40
#define	APIC_TM_BASE_REG	0x60
#define	APIC_REQUEST_BASE_REG	0x80

/* Error Status Register */
#define	APIC_ERROR_STATUS	0xa0

/* Interrupt Command registers */
#define	APIC_INT_CMD1		0xc0
#define	APIC_INT_CMD2		0xc4

/* Local Interrupt Vector registers */
#define	APIC_CMCI_VECT		0xbc
#define	APIC_THERM_VECT		0xcc
#define	APIC_PCINT_VECT		0xd0
#define	APIC_INT_VECT0		0xd4
#define	APIC_INT_VECT1		0xd8
#define	APIC_ERR_VECT		0xdc

/* IPL for performance counter interrupts */
#define	APIC_PCINT_IPL		0xe
#define	APIC_LVT_MASK		0x10000		/* Mask bit (16) in LVT */

#ifndef	_ASM

/* Initial Count register */
#define	APIC_INIT_COUNT		0xe0

/* Current Count Register */
#define	APIC_CURR_COUNT		0xe4
#define	APIC_CURR_ADD		0x39	/* used for remote read command */
#define	CURR_COUNT_OFFSET	(sizeof (int32_t) * APIC_CURR_COUNT)

/* Divider Configuration Register */
#define	APIC_DIVIDE_REG		0xf8

#define	APIC_EXTD_FEATURE_REG	0x100
#define	APIC_EXTD_CTRL_REG	0x104
#define	APIC_EXTD_SEOI_REG	0x108
#define	APIC_EXTD_IER_BASE_REG	0x120
#define	APIC_EXTD_LVT_BASE_REG	0x140

#define	APIC_EXTF_IER		0x1
#define	APIC_EXTF_SEOI		0x2
#define	APIC_EXTF_8BIT_ID	0x4

/* Various mode for local APIC. Modes are mutually exclusive  */
typedef enum apic_mode {
	APIC_IS_DISABLED = 0,
	APIC_MODE_NOTSET,
	LOCAL_APIC,
	LOCAL_X2APIC
} apic_mode_t;

/* x2APIC SELF IPI Register */
#define	X2APIC_SELF_IPI		0xFC

/* General x2APIC constants used at various places */
#define	APIC_SVR_SUPPRESS_BROADCAST_EOI		0x1000
#define	APIC_DIRECTED_EOI_BIT			0x1000000
#define	APIC_EXTENDED_BIT			0x80000000UL

/* x2APIC enable bit in REG_APIC_BASE_MSR (Intel: Extd, AMD: x2ApicEn) */
#define	X2APIC_ENABLE_BIT	10
#define	X2APIC_ENABLE_MASK	(1UL << (X2APIC_ENABLE_BIT))

/* xAPIC (LAPIC) enable bit in REG_APIC_BASE_MSR (Intel: EN, AMD: ApicEn) */
#define	LAPIC_ENABLE_BIT	11
#define	LAPIC_ENABLE_MASK	(1UL << (LAPIC_ENABLE_BIT))

#define	LAPIC_MODE_MASK		(X2APIC_ENABLE_MASK | LAPIC_ENABLE_MASK)

/* IRR register	*/
#define	APIC_IRR_REG		0x80

/* ISR register	*/
#define	APIC_ISR_REG		0x40

#define	APIC_IO_REG		0x0
#define	APIC_IO_DATA		0x4
#define	APIC_IO_EOI		0x10

/* Bit offset of APIC ID in LID_REG, INT_CMD and in DEST_REG */
#define	APIC_ID_BIT_OFFSET	24
#define	APIC_ICR_ID_BIT_OFFSET	24
#define	APIC_LDR_ID_BIT_OFFSET	24

/*
 * Choose between flat and clustered models by writing the following to the
 * FORMAT_REG. 82489 DX documentation seemed to suggest that writing 0 will
 * disable logical destination mode.
 * Does not seem to be in the docs for local APICs on the processors.
 */
#define	APIC_FLAT_MODEL		0xFFFFFFFFUL
#define	APIC_CLUSTER_MODEL	0x0FFFFFFF

/*
 * The commands which follow are window selectors written to APIC_IO_REG
 * before data can be read/written from/to APIC_IO_DATA
 */

#define	APIC_ID_CMD		0x0
#define	APIC_VERS_CMD		0x1
#define	APIC_ARB_CMD		0x2
#define	APIC_RDT_CMD		0x10
#define	APIC_RDT_CMD2		0x11

#define	APIC_INTEGRATED_VERS	0x10	/* 0x10 & above indicates integrated */
#define	IOAPIC_VER_82489DX	0x01	/* Version ID: 82489DX External APIC */

#define	APIC_INT_SPURIOUS	-1

#define	VENID_AMD		0x1022

#define	IOAPICS_NODE_NAME	"ioapics"
#define	IOAPICS_CHILD_NAME	"ioapic"
#define	IOAPICS_DEV_TYPE	"ioapic"
#define	IOAPICS_PROP_VENID	"vendor-id"
#define	IOAPICS_PROP_DEVID	"device-id"

/*
 * These macros are used in frequently called routines like
 * apic_intr_enter().
 */
#define	X2APIC_WRITE(reg, v) \
	wrmsr((REG_X2APIC_BASE_MSR + (reg >> 2)), v)

#endif	/* !_ASM */

#define	MAX_IO_APIC		32	/* maximum # of IOAPICs supported */

/* local vector table							*/
#define	AV_MASK		0x10000

/* interrupt command register 32-63					*/
#define	AV_TOALL	0x7fffffff
#define	AV_HIGH_ORDER	0x40000000
#define	AV_IM_OFF	0x40000000

/* interrupt command register 0-31					*/
#define	AV_DELIV_MODE	0x700

#define	AV_FIXED	0x000
#define	AV_LOPRI	0x100
#define	AV_SMI		0x200
#define	AV_REMOTE	0x300
#define	AV_NMI		0x400
#define	AV_RESET	0x500
#define	AV_STARTUP	0x600
#define	AV_EXTINT	0x700

#define	AV_PDEST	0x000
#define	AV_LDEST	0x800

/* IO & Local APIC Bit Definitions */
#define	AV_PENDING	0x1000
#define	AV_ACTIVE_LOW	0x2000		/* only for integrated APIC */
#define	AV_REMOTE_IRR   0x4000		/* IOAPIC RDT-specific */
#define	AV_LEVEL	0x8000
#define	AV_DEASSERT	AV_LEVEL
#define	AV_ASSERT	0xc000

#define	AV_READ_PENDING	0x10000
#define	AV_REMOTE_STATUS	0x20000	/* 1 = valid, 0 = invalid */

#define	AV_SH_SELF		0x40000	/* Short hand for self */
#define	AV_SH_ALL_INCSELF	0x80000 /* All processors */
#define	AV_SH_ALL_EXCSELF	0xc0000 /* All excluding self */
/* spurious interrupt vector register					*/
#define	AV_UNIT_ENABLE	0x100
#define	AV_FOCUS_DISABLE	0x200

#ifndef	_ASM

#define	RDT_VECTOR(x)	((uchar_t)((x) & 0xFF))

#define	APIC_MAXVAL	0xffffffffUL
#define	APIC_TIME_MIN	0x5000
#define	APIC_TIME_COUNT	0x4000

/*
 * Range of the low byte value in apic_tick before starting calibration
 */
#define	APIC_LB_MIN	0x60
#define	APIC_LB_MAX	0xe0

#define	APIC_MAX_VECTOR		255
#define	APIC_RESV_IRQ		0xfe
#define	APIC_BASE_VECT		0x20	/* This will come in as interrupt 0 */
#define	APIC_IPL_SHIFT		4	/* >> to get ipl part of vector */
#define	APIC_FIRST_FREE_IRQ	0x10

#define	APIC_MASK_ALL		0xf0	/* Mask all interrupts */

/* spurious interrupt vector						*/
#define	APIC_SPUR_INTR		0xFF

/* special or reserve vectors */
#define	APIC_CHECK_RESERVE_VECTORS(v) \
	(((v) == T_FASTTRAP) || ((v) == APIC_SPUR_INTR) || \
	((v) == T_SYSCALLINT) || ((v) == T_DTRACE_RET))

/*
 * definitions for MSI Address
 */
#define	MSI_ADDR_HDR		APIC_LOCAL_ADDR
#define	MSI_ADDR_DEST_SHIFT	12	/* Destination CPU's apic id */
#define	MSI_ADDR_RH_FIXED	0x0	/* Redirection Hint Fixed */
#define	MSI_ADDR_RH_LOPRI	0x1	/* Redirection Hint Lowest priority */
#define	MSI_ADDR_RH_SHIFT	3
#define	MSI_ADDR_DM_PHYSICAL	0x0	/* Physical Destination Mode */
#define	MSI_ADDR_DM_LOGICAL	0x1	/* Logical Destination Mode */
#define	MSI_ADDR_DM_SHIFT	2

/*
 * TM is either edge or level.
 */
#define	TRIGGER_MODE_EDGE		0x0	/* edge sensitive */
#define	TRIGGER_MODE_LEVEL		0x1	/* level sensitive */

/*
 * definitions for MSI Data
 */
#define	MSI_DATA_DELIVERY_FIXED		0x0	/* Fixed delivery */
#define	MSI_DATA_DELIVERY_LOPRI		0x1	/* Lowest priority delivery */
#define	MSI_DATA_DELIVERY_SMI		0x2
#define	MSI_DATA_DELIVERY_NMI		0x4
#define	MSI_DATA_DELIVERY_INIT		0x5
#define	MSI_DATA_DELIVERY_EXTINT	0x7
#define	MSI_DATA_DELIVERY_SHIFT		8
#define	MSI_DATA_TM_EDGE		TRIGGER_MODE_EDGE
#define	MSI_DATA_TM_LEVEL		TRIGGER_MODE_LEVEL
#define	MSI_DATA_TM_SHIFT		15
#define	MSI_DATA_LEVEL_DEASSERT		0x0
#define	MSI_DATA_LEVEL_ASSERT		0x1	/* Edge always assert */
#define	MSI_DATA_LEVEL_SHIFT		14

typedef	uint32_t apicid_t;

/*
 * This corresponds roughly to i86pc's XXX_INDEX definitions.  Unlike i86pc,
 * we support neither the old MPS table into which the value , if >= 0, indexed,
 * nor ACPI.  Therefore we never have any table to index into; indeed, on a
 * modern PC this could be used as well.  Our FIXED is effectively equivalent to
 * ACPI on a PC, meaning that there is no entry in the MPS table because we got
 * the information from somewhere else.  The rest of these are essentially the
 * same except for NONE, which we use to indicate that the rest of the data
 * in this entry is invalid because the irq has never been allocated.
 */
typedef enum apic_irq_kind {
	AIRQK_NONE,
	AIRQK_FREE,
	AIRQK_FIXED,
	AIRQK_MSI,
	AIRQK_MSIX,
	AIRQK_RESERVED
} apic_irq_kind_t;

/*
 * use to define each irq setup by the apic
 */
typedef struct apic_irq {
	apic_irq_kind_t	airq_kind;
	uint16_t	airq_rdt_entry;	/* level, polarity & trig mode */
	uint8_t		airq_intin_no;
	uint8_t		airq_ioapicindex;

	/*
	 * IRQ could be shared (in H/W) in which case dip & major will be
	 * for the one that was last added at this level. We cannot keep a
	 * linked list as delspl does not tell us which device has just
	 * been unloaded. For most servers where we are worried about
	 * performance, interrupt should not be shared & should not be
	 * a problem. This does not cause any correctness issue - dip is
	 * used only as an optimisation to avoid going thru all the tables
	 * in translate IRQ (which is always called twice due to brokenness
	 * in the way IPLs are determined for devices). major is used only
	 * to bind interrupts corresponding to the same device on the same
	 * CPU. Not finding major will just cause it to be potentially bound
	 * to another CPU.
	 */
	dev_info_t	*airq_dip;	/* see above */
	major_t		airq_major;	/* see above */

	uint32_t	airq_cpu;	/* !RESERVED only, target CPU */
	uint32_t	airq_temp_cpu;	/* !RESERVED only, for disable_intr */
	uint8_t		airq_vector;	/* Vector chosen for this irq */
	uint8_t		airq_share;	/* number of interrupts at this irq */
	uint8_t		airq_share_id;	/* id to identify source from irqno */
	uint8_t		airq_ipl;	/* The ipl at which this is handled */
	uint32_t	airq_busy;	/* How frequently did clock find */
					/* us in this */
	iflag_t		airq_iflag;	/* interrupt flag */
	uint8_t		airq_origirq;	/* original irq passed in */
	struct apic_irq *airq_next;	/* chain of intpts sharing a vector */
	void		*airq_intrmap_private; /* intr remap private data */
} apic_irq_t;

#define	IRQ_USER_BOUND	0x80000000 /* user requested bind if set in airq_cpu */
#define	IRQ_UNBOUND	(uint32_t)-1	/* set in airq_cpu and airq_temp_cpu */
#define	IRQ_UNINIT	(uint32_t)-2 /* in airq_temp_cpu till addspl called */

/*
 * We align apic_cpus_info at 64-byte cache line boundary. Please make sure we
 * adjust APIC_PADSZ as we add/modify any member of apic_cpus_info. We also
 * don't want the compiler to optimize apic_cpus_info.
 */
#define	APIC_PADSZ	15

#pragma	pack(1)
typedef struct apic_cpus_info {
	uint32_t aci_local_id;
	uchar_t	aci_local_ver;
	uchar_t	aci_status;
	uchar_t	aci_redistribute;	/* Selected for redistribution */
	uchar_t	aci_curipl;		/* IPL of current ISR */
	uint_t	aci_busy;		/* Number of ticks we were in ISR */
	uint_t	aci_spur_cnt;		/* # of spurious intpts on this cpu */
	uint_t	aci_ISR_in_progress;	/* big enough to hold 1 << MAXIPL */
	uchar_t	aci_current[MAXIPL];	/* Current IRQ at each IPL */
	uint32_t aci_bound;		/* # of user requested binds ? */
	uint32_t aci_temp_bound;	/* # of non user IRQ binds */
	uint32_t aci_processor_id;	/* XXX needed? */
	uchar_t	aci_idle;		/* The CPU is idle */
	/*
	 * Fill to make sure each struct is in separate 64-byte cache line.
	 */
	uchar_t	aci_pad[APIC_PADSZ];	/* padding for 64-byte cache line */
} apic_cpus_info_t;
#pragma	pack()

CTASSERT(sizeof (apic_cpus_info_t) == 64);

#define	APIC_CPU_ONLINE		0x1
#define	APIC_CPU_INTR_ENABLE	0x2
#define	APIC_CPU_FREE		0x4	/* APIC CPU slot is free */
#define	APIC_CPU_DIRTY		0x8	/* Slot was once used */
#define	APIC_CPU_SUSPEND	0x10

/*
 * APIC ops to support various flavors of APIC like APIC and x2APIC.
 */
typedef	struct apic_regs_ops {
	uint64_t	(*apic_read)(uint32_t);
	void		(*apic_write)(uint32_t, uint64_t);
	int		(*apic_get_pri)(void);
	void		(*apic_write_task_reg)(uint64_t);
	void		(*apic_write_int_cmd)(uint32_t, uint32_t);
	void		(*apic_send_eoi)(uint32_t);
} apic_reg_ops_t;

/*
 * interrupt structure for ioapic and msi
 */
typedef struct ioapic_rdt {
	uint32_t	ir_lo;
	uint32_t	ir_hi;
} ioapic_rdt_t;

typedef struct msi_regs {
	uint32_t	mr_data;
	uint64_t	mr_addr;
} msi_regs_t;

/*
 * APIC ops to support intel interrupt remapping
 */
typedef struct apic_intrmap_ops {
	int	(*apic_intrmap_init)(int);
	void	(*apic_intrmap_enable)(int);
	void	(*apic_intrmap_alloc_entry)(void **, dev_info_t *, uint16_t,
		    int, uchar_t);
	void	(*apic_intrmap_map_entry)(void *, void *, uint16_t, int);
	void	(*apic_intrmap_free_entry)(void **);
	void	(*apic_intrmap_record_rdt)(void *, ioapic_rdt_t *);
	void	(*apic_intrmap_record_msi)(void *, msi_regs_t *);
} apic_intrmap_ops_t;

extern uint32_t ioapic_read(int ioapic_ix, uint32_t reg);
extern void ioapic_write(int ioapic_ix, uint32_t reg, uint32_t value);
extern void ioapic_write_eoi(int ioapic_ix, uint32_t value);

/* Macros for reading/writing the IOAPIC RDT entries */
#define	READ_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix, ipin) \
	ioapic_read(ioapic_ix, APIC_RDT_CMD + (2 * (ipin)))

#define	READ_IOAPIC_RDT_ENTRY_HIGH_DWORD(ioapic_ix, ipin) \
	ioapic_read(ioapic_ix, APIC_RDT_CMD2 + (2 * (ipin)))

#define	WRITE_IOAPIC_RDT_ENTRY_LOW_DWORD(ioapic_ix, ipin, value) \
	ioapic_write(ioapic_ix, APIC_RDT_CMD + (2 * (ipin)), value)

#define	WRITE_IOAPIC_RDT_ENTRY_HIGH_DWORD(ioapic_ix, ipin, value) \
	ioapic_write(ioapic_ix, APIC_RDT_CMD2 + (2 * (ipin)), value)

/* Used by PSM_INTR_OP_GET_INTR to return device information. */
typedef struct {
	uint16_t	avgi_req_flags;	/* request flags - to kernel */
	uint8_t		avgi_num_devs;	/* # devs on this ino - from kernel */
	uint8_t		avgi_vector;	/* vector */
	uint32_t	avgi_cpu_id;	/* cpu of interrupt - from kernel */
	dev_info_t	**avgi_dip_list; /* kmem_alloc'ed list of dev_infos. */
					/* Contains num_devs elements. */
} apic_get_intr_t;

/* Used by PSM_INTR_OP_GET_TYPE to return platform information. */
typedef struct {
	char		*avgi_type;	/*  platform type - from kernel */
	uint32_t	avgi_num_intr;	/*  max intr number - from kernel */
	uint32_t	avgi_num_cpu;	/*  max cpu number - from kernel */
} apic_get_type_t;

/* Masks for avgi_req_flags. */
#define	PSMGI_REQ_CPUID		0x1	/* Request CPU ID */
#define	PSMGI_REQ_NUM_DEVS	0x2	/* Request num of devices on vector */
#define	PSMGI_REQ_VECTOR	0x4
#define	PSMGI_REQ_GET_DEVS	0x8	/* Request device list */
#define	PSMGI_REQ_ALL		0xf	/* Request everything */

/* Other flags */
#define	PSMGI_INTRBY_VEC	0	/* Vec passed.  xlate to IRQ needed */
#define	PSMGI_INTRBY_IRQ	0x8000	/* IRQ passed.  no xlate needed */
#define	PSMGI_INTRBY_DEFAULT	0x4000	/* PSM specific default value */
#define	PSMGI_INTRBY_FLAGS	0xc000	/* Mask for this flag */

extern int	apic_verbose;

/* Flag definitions for apic_verbose */
#define	APIC_VERBOSE_IOAPIC_FLAG		0x00000001
#define	APIC_VERBOSE_IRQ_FLAG			0x00000002
#define	APIC_VERBOSE_POWEROFF_FLAG		0x00000004
#define	APIC_VERBOSE_POWEROFF_PAUSE_FLAG	0x00000008
#define	APIC_VERBOSE_INIT			0x00000010
#define	APIC_VERBOSE_REBIND			0x00000020
#define	APIC_VERBOSE_ALLOC			0x00000040
#define	APIC_VERBOSE_IPI			0x00000080
#define	APIC_VERBOSE_INTR			0x00000100

/* required test to wait until APIC command is sent on the bus */
#define	APIC_AV_PENDING_SET() \
	while (apic_reg_ops->apic_read(APIC_INT_CMD1) & AV_PENDING) \
		apic_ret();

#ifdef	DEBUG

#define	DENT		0x0001
extern int	apic_debug;
/*
 * set apic_restrict_vector to the # of vectors we want to allow per range
 * useful in testing shared interrupt logic by setting it to 2 or 3
 */
extern int	apic_restrict_vector;

#define	APIC_DEBUG_MSGBUFSIZE	2048
extern int	apic_debug_msgbuf[];
extern int	apic_debug_msgbufindex;

/*
 * Put "int" info into debug buffer. No MP consistency, but light weight.
 * Good enough for most debugging.
 */
#define	APIC_DEBUG_BUF_PUT(x) \
	apic_debug_msgbuf[apic_debug_msgbufindex++] = x; \
	if (apic_debug_msgbufindex >= (APIC_DEBUG_MSGBUFSIZE - NCPU)) \
		apic_debug_msgbufindex = 0;

#define	APIC_VERBOSE(flag, fmt)			     \
	if (apic_verbose & APIC_VERBOSE_##flag) \
		cmn_err fmt;

#define	APIC_VERBOSE_POWEROFF(fmt) \
	if (apic_verbose & APIC_VERBOSE_POWEROFF_FLAG) \
		prom_printf fmt;

#else	/* DEBUG */

#define	APIC_VERBOSE(flag, fmt)
#define	APIC_VERBOSE_POWEROFF(fmt)

#endif	/* DEBUG */

#define	APIC_VERBOSE_IOAPIC(fmt)	APIC_VERBOSE(IOAPIC_FLAG, fmt)
#define	APIC_VERBOSE_IRQ(fmt)		APIC_VERBOSE(IRQ_FLAG, fmt)

extern int	apic_error;
/* values which apic_error can take. Not catastrophic, but may help debug */
#define	APIC_ERR_BOOT_EOI		0x1
#define	APIC_ERR_GET_IPIVECT_FAIL	0x2
#define	APIC_ERR_INVALID_INDEX		0x4
#define	APIC_ERR_MARK_VECTOR_FAIL	0x8
#define	APIC_ERR_APIC_ERROR		0x40000000
#define	APIC_ERR_NMI			0x80000000

/* APIC error flags we care about */
#define	APIC_SEND_CS_ERROR	0x01
#define	APIC_RECV_CS_ERROR	0x02
#define	APIC_CS_ERRORS		(APIC_SEND_CS_ERROR|APIC_RECV_CS_ERROR)

/* Maximum number of times to retry reprogramming at apic_intr_exit time */
#define	APIC_REPROGRAM_MAX_TRIES 10000

/* Parameter to ioapic_init_intr(): Should ioapic ints be masked? */
#define	IOAPIC_MASK 1
#define	IOAPIC_NOMASK 0

#define	INTR_ROUND_ROBIN_WITH_AFFINITY	0
#define	INTR_ROUND_ROBIN		1
#define	INTR_LOWEST_PRIORITY		2

struct ioapic_reprogram_data {
	boolean_t			done;
	apic_irq_t			*irqp;
	/* The CPU to which the int will be bound */
	int				bindcpu;
	/* # times the reprogram timeout was called */
	unsigned			tries;
};

/* The irq # is implicit in the array index: */
extern struct ioapic_reprogram_data apic_reprogram_info[];

extern int apic_probe_common(char *);
extern void ioapic_disable_redirection(void);
extern int apic_allocate_irq(int);
extern int apic_state(psm_state_request_t *);
extern boolean_t apic_cpu_in_range(int);
extern int apic_check_msi_support(void);
extern uint32_t *mapin_apic(uint32_t, size_t, int);
extern uint32_t *mapin_ioapic(uint32_t, size_t, int);
extern void mapout_apic(caddr_t, size_t);
extern void mapout_ioapic(caddr_t, size_t);
extern void apic_pci_msi_unconfigure(dev_info_t *, int, int);
extern void apic_pci_msi_disable_mode(dev_info_t *, int);
extern uint16_t	apic_get_apic_version(void);
extern void x2apic_send_ipi(int, int);
extern void apic_ret(void);
extern int apic_detect_x2apic(void);
extern void apic_enable_x2apic(void);
extern int apic_local_mode(void);
extern void apic_send_EOI(uint32_t);
extern void apic_send_directed_EOI(uint32_t);
extern uint64_t apic_calibrate(void);
extern void x2apic_send_pir_ipi(processorid_t);

extern volatile uint32_t *apicadr;	/* virtual addr of local APIC   */
extern int apic_forceload;
extern apic_cpus_info_t *apic_cpus;
#ifdef _MACHDEP
extern cpuset_t apic_cpumask;
#endif
extern uint_t apic_picinit_called;
extern apic_irq_t *apic_irq_table[APIC_MAX_VECTOR+1];
extern volatile uint32_t *apicioadr[MAX_IO_APIC];
extern uchar_t apic_io_id[MAX_IO_APIC];
extern lock_t apic_ioapic_lock;
extern uint32_t apic_physaddr[MAX_IO_APIC];
extern kmutex_t airq_mutex;
extern int apic_first_avail_irq;
extern char apic_level_intr[APIC_MAX_VECTOR+1];
extern uchar_t apic_resv_vector[MAXIPL+1];
extern int apic_sample_factor_redistribution;
extern int apic_int_busy_mark;
extern int apic_int_free_mark;
extern int apic_diff_for_redistribution;
extern int apic_nproc;
extern int apic_max_nproc;
extern int apic_next_bind_cpu;
extern int apic_redistribute_sample_interval;
extern int apic_multi_msi_enable;
extern int apic_sci_vect;
extern int apic_hpet_vect;
extern apic_reg_ops_t *apic_reg_ops;
extern apic_reg_ops_t local_apic_regs_ops;
extern apic_mode_t apic_mode;
extern void x2apic_update_psm(void);
extern void apic_change_ops(void);
extern void apic_common_send_ipi(int, int);
extern void apic_set_directed_EOI_handler(void);
extern int apic_directed_EOI_supported(void);
extern void apic_common_send_pir_ipi(processorid_t);

extern apic_intrmap_ops_t *apic_vt_ops;

#endif	/* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_APIC_H */
