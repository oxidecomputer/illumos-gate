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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2026 Oxide Computer Company
 */

#ifndef	_SYS_AMD_IOMMU_H
#define	_SYS_AMD_IOMMU_H

/*
 * The Oxide architecture supports the AMD IOMMU for interrupt remapping.
 * This file contains types, function prototypes, and so on, supporting
 * this functionality.
 *
 * The AMD IOMMU driver itself is in uts/oxide/io/amd_iommu.
 */

#include <sys/io/zen/fabric.h>
#include <sys/bitmap.h>
#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/sunddi.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct amd_iommu_segment amd_iommu_segment_t;
typedef struct amd_iommu amd_iommu_t;
typedef struct amd_iommu_dev_tbl amd_iommu_dev_tbl_t;
typedef struct amd_iommu_cmd_buf amd_iommu_cmd_buf_t;
typedef struct amd_iommu_event_log amd_iommu_event_log_t;
typedef struct amd_iommu_intr_remap_tbl amd_iommu_intr_remap_tbl_t;
typedef struct amd_iommu_intr_cookie amd_iommu_intr_cookie_t;

/* Architecturally defined structures. */
typedef struct amd_iommu_dte amd_iommu_dte_t;
typedef struct amd_iommu_irte amd_iommu_irte_t;
typedef struct amd_iommu_cbe amd_iommu_cbe_t;
typedef struct amd_iommu_evle amd_iommu_evle_t;

/*
 * This value is used for DDI DMA attributes: it is the most restrictive
 * physical alignment requirement imposed by the IOMMU specification (unless
 * using the vIOMMU, which we do not currently do).
 */
#define	AMD_IOMMU_PHYSMEM_ALIGN	4096

/*
 * PCI segment-wide resources.
 *
 * Each IOHC is associated with an IOMMU, and that IOMMU is responsible for the
 * devices attached to its associated IOHC(s).  Also, the DeviceID space seen by
 * an IOMMU is only unique within a PCI segment, since DeviceIDs are either
 * actual BDFs or things fabricated to look like them.  Since the set of buses
 * attached to an IOHC is fixed and does not overlap with those of other IOHCs,
 * the set of devices that can be seen by any given IOMMU similarly does not
 * overlap with any other IOMMU.  Thus, we can share the device table for all
 * devices within a PCI segment between all IOMMUs associated with IOHCs
 * assigned to that segment.
 */
struct amd_iommu_segment {
	uint8_t ais_segment;	/* segment number */
	list_t ais_iommus;	/* list of this segment's IOMMUs */
	size_t ais_niommus;	/* Number of IOMMUs for segment */
	kmutex_t ais_lock;	/* Synchronizes access to segment data */
	bool ais_running;	/* True if the IOMMUs have been started */

	/*
	 * The Device Table is shared, segment-wide.  Each IOMMU has a
	 * pointer to its segment, which points to the DT.
	 */
	amd_iommu_dev_tbl_t *ais_dev_tbl;

	/*
	 * While conceptually each interrupt remapping table is associated with
	 * a DeviceID representing a device associated with some specific IOMMU,
	 * remapping tables are referred to from the device table, so we track
	 * them at the segment level.
	 */
	amd_iommu_intr_remap_tbl_t *ais_intr_remap_tbls;
	size_t ais_intr_remap_ntbls;
};

/*
 * The Device Table.
 *
 * The device table covers a PCI segment's entire 16-bit BDF space.  Each entry
 * is 256 bits long.  Thus, the space required is 2^16*2^8/2^3 bytes, or
 * 2^(16+8-3) = 2^21 = 2MiB.  The device table must be 4KiB aligned.
 */
#define	AMD_IOMMU_DEV_TBL_ESIZE	32	/* Size of an entry, in bytes. */
#define	AMD_IOMMU_DEV_TBL_NENTS	65536	/* Number of entries in the table */
#define	AMD_IOMMU_DEV_TBL_SIZE	\
	(AMD_IOMMU_DEV_TBL_NENTS * AMD_IOMMU_DEV_TBL_ESIZE)
#define	AMD_IOMMU_DEV_TBL_ALIGN	4096

CTASSERT((AMD_IOMMU_PHYSMEM_ALIGN % AMD_IOMMU_DEV_TBL_ALIGN) == 0);

struct amd_iommu_dev_tbl {
	uint64_t aidt_phys_addr;
	amd_iommu_dte_t *aidt_dev_tbl;
};

/*
 * Represents a single IOMMU in a segment.  It contains a pointers to,
 * - the IOHC (the `zen_ioms_t`),
 * - the segment (PCIe segment number),
 * - per-IOMMU command buffer,
 * - per-IOMMU event log,
 * - per-IOMMU list of interrupt remapping tables allocated to the devices
 *   covered by the IOMMU,
 * - a count of how many interrupt remapping tables are associated with this
 *   IOMMU.
 */
struct amd_iommu {
	list_node_t ai_next;
	uint_t ai_instance;
	zen_ioms_t *ai_zen_ioms;
	amd_iommu_segment_t *ai_segment;
	amd_iommu_cmd_buf_t *ai_cmd_buf;
	amd_iommu_event_log_t *ai_evt_log;
	kmutex_t ai_ctl_lock;
};

/*
 * For each interrupt that is allocated, we provide apix with an opaque "cookie"
 * we can use to track state about that interrupt.  In our case, we track the
 * interrupt's index in the interrupt remapping table.  But we can also "rebind"
 * interrupts, which means one might move from one IRTE to another IRTE in the
 * same IRT.  There is a bit of a timing shuffle involved in doing so, and in
 * particular there is a window where one interrupt might logically "own" two
 * slots: the old slot and a new slot.  The comments in `amd_iommu_intrmap.c` go
 * into more detail about this, but the implication is that cookies associated
 * with an interrupt as seen from apix's perspective are not necessarily 1:1
 * with IRTEs, and it is insufficient to simply use the IRTE index itself as the
 * cookie.  Instead, for cookies we use pointers to instances of this structure,
 * which tracks both old and new indices.
 */
struct amd_iommu_intr_cookie {
	list_node_t aiic_next_free;
	amd_iommu_intr_remap_tbl_t *aiic_intrmap_tbl;
	uint16_t aiic_irte_index;
	uint16_t aiic_irte_oldindex;
};

/*
 * An Interrupt Remapping Table.
 *
 * We allocate 8KiB for the IRT, and IRTs are specific to a DeviceID.  Each IRTE
 * is 128 bits (16 bytes), so the maximum number of interrupts per DeviceID is
 * capped at 8192/16 = 2^13/2^4 = 2^(13-4) = 2^9 = 512, which is excessive given
 * that it is per PCIe-function or IOAPIC, but is chosen with consideration to
 * the maximum number of supported MSIs per function: The IOMMU's control
 * register has a field for specifying one of two choices for number of MSIs
 * supported per DeviceID to 512 entries (or 2048 on Turin).  The maximum is
 * further limited to 512 for physical destination mode interrupts (see table 19
 * of AMD IOMMU ver 3.11).
 *
 * IRTs must be 128-byte aligned, and can have fewer than 512 entries, but there
 * is no option to specify a smaller number of MSIs and as a practical matter,
 * any allocation we use will be at least 4KiB anyway.  As there are not that
 * many of them, we simply allocate 512 entry tables.
 *
 * We also have an array of cookies, one for each IRTE.  These are allocated to
 * interrupts on demand, but are otherwise interchangeable, so we simply track
 * available cookies on a list.  Incidentally, while a cookie may be associated
 * with two IRTEs for a brief window, and is thus not 1:1 with IRTEs, the
 * inverse is not true: an IRTE can never be associated with more than one
 * cookie at a time.  Since we size the cookie array to be the same length as
 * the IRT, if an IRTE is allocated for an interrupt, then a cookie will
 * necessarily be available.
 */
#define	AMD_IOMMU_INTR_REMAP_TBL_SIZE		8192
#define	AMD_IOMMU_INTR_REMAP_TBL_ALIGN		128
#define	AMD_IOMMU_MAX_INTR_LOG2			9
#define	AMD_IOMMU_MAX_INTR			(1 << AMD_IOMMU_MAX_INTR_LOG2)

CTASSERT((AMD_IOMMU_PHYSMEM_ALIGN % AMD_IOMMU_INTR_REMAP_TBL_ALIGN) == 0);

struct amd_iommu_intr_remap_tbl {
	amd_iommu_intr_remap_tbl_t *aiirt_next;
	amd_iommu_segment_t *aiirt_segment;

	uint64_t aiirt_phys_addr;
	amd_iommu_irte_t *aiirt_intr_tbl;

	uint16_t aiirt_device_id;

	/*
	 * We track available IRTEs in a bitmap, as they are subject to
	 * alignment constraints (see the comments in `amd_iommu_intrmap.c` for
	 * details).  When we allocate an interrupt, we also allocate a cookie
	 * for it that we give to apix.  Cookies are not subject to the same
	 * alignment constraints as IRTEs, but are not necessarily 1:1 with
	 * IRTEs, so free cookies are tracked separately, in a list.
	 */
	kmutex_t aiirt_alloc_lock;
	ulong_t aiirt_alloc_map[BT_BITOUL(AMD_IOMMU_MAX_INTR)];

	amd_iommu_intr_cookie_t aiirt_cookies[AMD_IOMMU_MAX_INTR];
	list_t aiirt_free_cookies;
};

extern bool amd_iommu_intrmap_disabled(void);
extern void amd_iommu_intrmap_os_startup(void);
extern amd_iommu_intr_remap_tbl_t *amd_iommu_intr_remap_tbl_find(
    amd_iommu_segment_t *, uint16_t);

/*
 * A Command Buffer.  Command buffers are unique per-IOMMU, not shared.
 *
 * The buffer must be 4KiB aligned, and each entry is 128 bits long (16 bytes).
 * There are a maximum of 32768 (2^15) entries, so the total size is
 * 2^15*2^7/2^3 = 2^(15+7-3) = 2^19 = 512KiB.
 */
#define	AMD_IOMMU_CMD_BUF_NENTS		32768
#define	AMD_IOMMU_CMD_BUF_SIZE		(512 * 1024)
#define	AMD_IOMMU_CMD_BUF_ALIGN		4096

CTASSERT((AMD_IOMMU_PHYSMEM_ALIGN % AMD_IOMMU_CMD_BUF_ALIGN) == 0);

struct amd_iommu_cmd_buf {
	amd_iommu_t *aicb_iommu;
	uint64_t aicb_phys_addr;
	amd_iommu_cbe_t *aicb_cmd_buf;
	kmutex_t aicb_lock;
	size_t aicb_tail;
};

extern size_t amd_iommu_read_cmd_buf_head(amd_iommu_cmd_buf_t *);
extern void amd_iommu_write_cmd_buf_tail(amd_iommu_cmd_buf_t *, size_t);

extern void amd_iommu_invalidate_intr_tbl(amd_iommu_t *, uint16_t);
extern void amd_iommu_invalidate_dte(amd_iommu_t *, uint16_t);
extern void amd_iommu_invalidate_all(amd_iommu_t *);
extern void amd_iommu_invalidate_intr_tbl_segment(amd_iommu_segment_t *,
    uint16_t);
extern void amd_iommu_invalidate_all_segment(amd_iommu_segment_t *);
extern void amd_iommu_invalidate_dte_segment(amd_iommu_segment_t *, uint16_t);

/*
 * An event log.  Event logs are per-IOMMU, not per-segment.  Event log entries
 * are the same size as command buffer entries, so the same math deterimes the
 * maximum number of allowable entries (2^15).
 */
#define	AMD_IOMMU_EVENT_LOG_NENTS	32768
#define	AMD_IOMMU_EVENT_LOG_SIZE	(512 * 1024)
#define	AMD_IOMMU_EVENT_LOG_ALIGN	4096

CTASSERT((AMD_IOMMU_PHYSMEM_ALIGN % AMD_IOMMU_EVENT_LOG_ALIGN) == 0);

struct amd_iommu_event_log {
	amd_iommu_t *aiel_iommu;
	uint64_t aiel_phys_addr;
	amd_iommu_evle_t *aiel_evt_log;
	size_t aiel_head;
	kmutex_t aiel_lock;
};

extern size_t amd_iommu_read_evtlog_tail(amd_iommu_event_log_t *);
extern void amd_iommu_write_evtlog_head(amd_iommu_event_log_t *, size_t);
extern int amd_iommu_process_event_log(amd_iommu_t *);
extern int amd_iommu_process_segment_event_logs(amd_iommu_segment_t *);

/*
 * The maximum number of log entries we will process at one time.
 */
static const size_t AMD_IOMMU_EVENT_LOG_MAX_READ = 128;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_AMD_IOMMU_H */
