/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver.
 *
 * Copyright (C) 2009-2019 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 */

#ifndef __T4_HW_H
#define __T4_HW_H

#include "osdep.h"

enum {
	NCHAN           = 4,     /* # of HW channels */
	MAX_MTU         = 9600,  /* max MAC MTU, excluding header + FCS */
	EEPROMSIZE      = 17408, /* Serial EEPROM physical size */
	EEPROMVSIZE     = 32768, /* Serial EEPROM virtual address space size */
	EEPROMPFSIZE    = 1024,  /* EEPROM writable area size for PFn, n>0 */
	RSS_NENTRIES    = 2048,  /* # of entries in RSS mapping table */
	T6_RSS_NENTRIES = 4096,  /* # of entries in RSS mapping table */
	TCB_SIZE        = 128,   /* TCB size */
	NMTUS           = 16,    /* size of MTU table */
	NCCTRL_WIN      = 32,    /* # of congestion control windows */
	NTX_SCHED       = 8,     /* # of HW Tx scheduling queues */
	PM_NSTATS       = 5,     /* # of PM stats */
	T6_PM_NSTATS    = 7,     /* # of PM stats in T6 */
	T7_PM_RX_CACHE_NSTATS = 27, /* # of PM Rx Cache stats in T7 */
	MBOX_LEN        = 64,    /* mailbox size in bytes */
	TRACE_LEN       = 112,   /* length of trace data and mask */
	FILTER_OPT_LEN  = 36,    /* filter tuple width of optional components */
	UDBS_SEG_SIZE   = 128,   /* segment size for BAR2 user doorbells */
	MAX_UP_CORES = 8, /* Max # of uP cores that can be enabled */
};

enum {
	CIM_NUM_IBQ    = 6,     /* # of CIM IBQs */
	CIM_NUM_IBQ_T7 = 16,    /* # of CIM IBQs for T7 */
	CIM_NUM_OBQ    = 6,     /* # of CIM OBQs */
	CIM_NUM_OBQ_T5 = 8,     /* # of CIM OBQs for T5 adapter */
	CIM_NUM_OBQ_T7 = 16,    /* # of CIM OBQs for T7 adapter */
	CIMLA_SIZE     = (256 * 8),  /* 256 rows * ceil(235/32) 32-bit words */
	CIMLA_SIZE_T6  = (256 * 10), /* 256 rows * ceil(311/32) 32-bit words */
	CIM_PIFLA_SIZE = 64,    /* # of 192-bit words in CIM PIF LA */
	CIM_MALA_SIZE  = 64,    /* # of 160-bit words in CIM MA LA */
	CIM_IBQ_SIZE   = 128,   /* # of 128-bit words in a CIM IBQ */
	CIM_OBQ_SIZE   = 128,   /* # of 128-bit words in a CIM OBQ */
	TPLA_SIZE      = 128,   /* # of 64-bit words in TP LA */
	ULPRX_LA_SIZE  = 512,   /* # of 256-bit words in ULP_RX LA */
};

enum {
	SF_PAGE_SIZE = 256,           /* serial flash page size */
	SF_SEC_SIZE = 64 * 1024,      /* serial flash sector size */
};

/* SGE context types */
enum ctxt_type { CTXT_EGRESS, CTXT_INGRESS, CTXT_FLM, CTXT_CNM };

enum { RSP_TYPE_FLBUF, RSP_TYPE_CPL, RSP_TYPE_INTR }; /* response entry types */

enum { MBOX_OWNER_NONE, MBOX_OWNER_FW, MBOX_OWNER_DRV };    /* mailbox owners */

enum {
	SGE_MAX_WR_LEN = 512,     /* max WR size in bytes */
	SGE_CTXT_SIZE = 24,       /* size of SGE context */
	SGE_NTIMERS = 6,          /* # of interrupt holdoff timer values */
	SGE_NCOUNTERS = 4,        /* # of interrupt packet counter values */
	SGE_NDBQTIMERS = 8,       /* # of Doorbell Queue Timer values */
	SGE_MAX_IQ_SIZE = 65520,
};

/* PCI-e memory window access */
enum pcie_memwin {
	MEMWIN_NIC      = 0,
	MEMWIN_RSVD1    = 1,
	MEMWIN_RSVD2    = 2,
	MEMWIN_RDMA     = 3,
	MEMWIN_RSVD4    = 4,
	MEMWIN_FOISCSI  = 5,
	MEMWIN_CSIOSTOR = 6,
	MEMWIN_RSVD7    = 7,
};

struct sge_qstat {                /* data written to SGE queue status entries */
	__be32 qid;
	__be16 cidx;
	__be16 pidx;
};

#define S_QSTAT_PIDX    0
#define M_QSTAT_PIDX    0xffff
#define G_QSTAT_PIDX(x) (((x) >> S_QSTAT_PIDX) & M_QSTAT_PIDX)

#define S_QSTAT_CIDX    16
#define M_QSTAT_CIDX    0xffff
#define G_QSTAT_CIDX(x) (((x) >> S_QSTAT_CIDX) & M_QSTAT_CIDX)

/*
 * Structure for last 128 bits of response descriptors
 */
struct rsp_ctrl {
	__be32 hdrbuflen_pidx;
	__be32 pldbuflen_qid;
	union {
		u8 type_gen;
		__be64 last_flit;
	} u;
};

#define S_RSPD_NEWBUF    31
#define V_RSPD_NEWBUF(x) ((x) << S_RSPD_NEWBUF)
#define F_RSPD_NEWBUF    V_RSPD_NEWBUF(1U)

#define S_RSPD_LEN    0
#define M_RSPD_LEN    0x7fffffff
#define V_RSPD_LEN(x) ((x) << S_RSPD_LEN)
#define G_RSPD_LEN(x) (((x) >> S_RSPD_LEN) & M_RSPD_LEN)

#define S_RSPD_QID    S_RSPD_LEN
#define M_RSPD_QID    M_RSPD_LEN
#define V_RSPD_QID(x) V_RSPD_LEN(x)
#define G_RSPD_QID(x) G_RSPD_LEN(x)

#define S_RSPD_GEN    7
#define V_RSPD_GEN(x) ((x) << S_RSPD_GEN)
#define F_RSPD_GEN    V_RSPD_GEN(1U)

#define S_RSPD_QOVFL    6
#define V_RSPD_QOVFL(x) ((x) << S_RSPD_QOVFL)
#define F_RSPD_QOVFL    V_RSPD_QOVFL(1U)

#define S_RSPD_TYPE    4
#define M_RSPD_TYPE    0x3
#define V_RSPD_TYPE(x) ((x) << S_RSPD_TYPE)
#define G_RSPD_TYPE(x) (((x) >> S_RSPD_TYPE) & M_RSPD_TYPE)

/* Rx queue interrupt deferral fields: counter enable and timer index */
#define S_QINTR_CNT_EN    0
#define V_QINTR_CNT_EN(x) ((x) << S_QINTR_CNT_EN)
#define F_QINTR_CNT_EN    V_QINTR_CNT_EN(1U)

#define S_QINTR_TIMER_IDX    1
#define M_QINTR_TIMER_IDX    0x7
#define V_QINTR_TIMER_IDX(x) ((x) << S_QINTR_TIMER_IDX)
#define G_QINTR_TIMER_IDX(x) (((x) >> S_QINTR_TIMER_IDX) & M_QINTR_TIMER_IDX)

/* # of pages a pagepod can hold without needing another pagepod */
#define PPOD_PAGES 4U

struct pagepod {
	__be64 vld_tid_pgsz_tag_color;
	__be64 len_offset;
	__be64 rsvd;
	__be64 addr[PPOD_PAGES + 1];
};

#define S_PPOD_COLOR    0
#define M_PPOD_COLOR    0x3F
#define V_PPOD_COLOR(x) ((x) << S_PPOD_COLOR)

#define S_PPOD_TAG    6
#define M_PPOD_TAG    0xFFFFFF
#define V_PPOD_TAG(x) ((x) << S_PPOD_TAG)

#define S_PPOD_PGSZ    30
#define M_PPOD_PGSZ    0x3
#define V_PPOD_PGSZ(x) ((x) << S_PPOD_PGSZ)

#define S_PPOD_TID    32
#define M_PPOD_TID    0xFFFFFF
#define V_PPOD_TID(x) ((__u64)(x) << S_PPOD_TID)

#define S_PPOD_VALID    56
#define V_PPOD_VALID(x) ((__u64)(x) << S_PPOD_VALID)
#define F_PPOD_VALID    V_PPOD_VALID(1ULL)

#define S_PPOD_LEN    32
#define M_PPOD_LEN    0xFFFFFFFF
#define V_PPOD_LEN(x) ((__u64)(x) << S_PPOD_LEN)

#define S_PPOD_OFST    0
#define M_PPOD_OFST    0xFFFFFFFF
#define V_PPOD_OFST(x) ((x) << S_PPOD_OFST)

/*
 * Flash layout.
 */
#define FLASH_START(start)	((start) * SF_SEC_SIZE)
#define FLASH_MAX_SIZE(nsecs)	((nsecs) * SF_SEC_SIZE)

enum t4_flash_loc {
	/*
	 * Various Expansion-ROM boot images, etc.
	 */
	FLASH_LOC_EXP_ROM = 0,

	/*
	 * iSCSI Boot Firmware Table (iBFT) and other driver-related
	 * parameters ...
	 */
	FLASH_LOC_IBFT,

	/*
	 * Boot configuration data.
	 */
	FLASH_LOC_BOOTCFG,

	/*
	 * Location of firmware image in FLASH.
	 */
	FLASH_LOC_FW,

	/*
	 * Location of bootstrap firmware image in FLASH.
	 */
	FLASH_LOC_FWBOOTSTRAP,

	/*
	 * iSCSI persistent/crash information.
	 */
	FLASH_LOC_ISCSI_CRASH,

	/*
	 * FCoE persistent/crash information.
	 */
	FLASH_LOC_FCOE_CRASH,

	/*
	 * Location of Firmware Configuration File in FLASH.
	 */
	FLASH_LOC_CFG,

	/*
	 * CUDBG chip dump.
	 */
	FLASH_LOC_CUDBG,

	/*
	 * FW chip dump.
	 */
	FLASH_LOC_CHIP_DUMP,

	/*
	 * DPU boot information store.
	 */
	FLASH_LOC_DPU_BOOT,

	/*
	 * DPU peristent information store.
	 */
	FLASH_LOC_DPU_AREA,

	/*
	 * VPD location.
	 */
	FLASH_LOC_VPD,

	/*
	 * Helper to retrieve info that spans the entire Boot related area.
	 */
	FLASH_LOC_BOOT_AREA,

	/*
	 * Helper to determine minimum standard set of sections needed for
	 * normal operations.
	 */
	FLASH_LOC_MIN_SIZE,

	/*
	 * End of FLASH regions.
	 */
	FLASH_LOC_END
};

struct t4_flash_loc_entry {
	u16 start_sec;
	u16 nsecs;
};

#define S_SGE_TIMESTAMP 0
#define M_SGE_TIMESTAMP 0xfffffffffffffffULL
#define V_SGE_TIMESTAMP(x) ((__u64)(x) << S_SGE_TIMESTAMP)
#define G_SGE_TIMESTAMP(x) (((__u64)(x) >> S_SGE_TIMESTAMP) & M_SGE_TIMESTAMP)

#define I2C_DEV_ADDR_A0		0xa0
#define I2C_DEV_ADDR_A2		0xa2
#define I2C_PAGE_SIZE		0x100
#define SFP_DIAG_TYPE_ADDR	0x5c
#define SFP_DIAG_TYPE_LEN	0x1
#define SFF_8472_COMP_ADDR	0x5e
#define SFF_8472_COMP_LEN	0x1
#define SFF_REV_ADDR		0x1
#define SFF_REV_LEN		0x1

#endif /* __T4_HW_H */
