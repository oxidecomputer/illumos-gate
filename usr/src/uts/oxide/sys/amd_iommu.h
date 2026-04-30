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
#include <sys/mutex.h>
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

/* Structures defined in the implementation. */
typedef struct amd_iommu_dte amd_iommu_dte_t;
typedef struct amd_iommu_irte amd_iommu_irte_t;
typedef struct amd_iommu_cbe amd_iommu_cbe_t;
typedef struct amd_iommu_evle amd_iommu_evle_t;

/*
 * PCI segment-wide resources.
 *
 * Each Zen IOHC has an associated IOMMU, so there can be several in a PCI
 * segment.  These share a DeviceID addres space, so they can share a device
 * table, as well.
 */
struct amd_iommu_segment {
	uint_t ais_segment;		/* segment number */
	amd_iommu_t *ais_iommus;	/* list of this segment's IOMMUs */
	size_t ais_niommus;		/* Number of IOMMUs for segment */

	/*
	 * The Device Table is shared, segment-wide.  Each individiual IOMMU
	 * has a back-reference to its segment, which points to the DT.
	 */
	amd_iommu_dev_tbl_t *ais_dev_tbl;
	/*
	 * While conceptually each interrupt remapping table is tied associated
	 * with a DeviceID representing a device associated with some specific
	 * IOMMU, remapping tables are referred to from the device table, so we
	 * track them at the segment level.
	 */
	kmutex_t ais_intr_remap_tbls_lock;
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
static const size_t AMD_IOMMU_DEV_TBL_SIZE = 2 * 1024 * 1024;
static const size_t AMD_IOMMU_DEV_TBL_ALIGN = 4096;

struct amd_iommu_dev_tbl {
	uint64_t aidt_phys_addr;
	amd_iommu_dte_t *aidt_dev_tbl;
	size_t aidt_size;
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
	amd_iommu_t *ai_next;
	uint_t ai_instance;
	zen_ioms_t *ai_zen_ioms;
	amd_iommu_segment_t *ai_segment;
	amd_iommu_cmd_buf_t *ai_cmd_buf;
	amd_iommu_event_log_t *ai_evt_log;
};

/*
 * An Interrupt Remapping Table.
 *
 * We allocate a 8KiB page to the IRT for each DeviceID.  Each ITRE is 128 bits
 * (16 bytes), so the maximum number of interrupts per DeviceID is capped at
 * 8192/16 = 2^13/2^4 = 2^(13-4) = 2^9 = 512.  This is excessive, but basically
 * architeturally mandated; the IOMMU's control register has a field that lets
 * one specify one of two sizes for table: either 512 entries, or 2048, and
 * there is no option for anything smaller.
 */
#define	AMD_IOMMU_INTR_REMAP_TBL_SIZE		8192
#define	AMD_IOMMU_INTR_REMAP_TBL_ALIGN		4096
#define	AMD_IOMMU_MAX_INTR			512
#define	AMD_IOMMU_MAX_INTR_LOG2			9

struct amd_iommu_intr_remap_tbl {
	amd_iommu_intr_remap_tbl_t *aiirt_next;
	amd_iommu_segment_t *aiirt_segment;
	void *aiirt_private;

	uint64_t aiirt_phys_addr;
	amd_iommu_irte_t *aiirt_intr_tbl;
	size_t aiirt_size;

	int aiirt_device_id;

	kmutex_t aiirt_alloc_lock;
	uint32_t aiirt_alloc_map[AMD_IOMMU_MAX_INTR / 32];
};

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

static const size_t AMD_IOMMU_CMD_BUF_SIZE = 512 * 1024;
static const size_t AMD_IOMMU_CMD_BUF_ALIGN = 4096;

struct amd_iommu_cmd_buf {
	amd_iommu_t *aicb_iommu;
	uint64_t aicb_phys_addr;
	amd_iommu_cbe_t *aicb_cmd_buf;
	size_t aicb_size;
	kmutex_t aicb_cmd_lock;
	size_t aicb_tail;
};

extern size_t amd_iommu_read_cmd_buf_head(amd_iommu_cmd_buf_t *);
extern void amd_iommu_write_cmd_buf_tail(amd_iommu_cmd_buf_t *, size_t);

/*
 * An event log.  Event logs are per-IOMMU, not per-segment.
 */
static const size_t AMD_IOMMU_EVENT_LOG_SIZE = 512 * 1024;
static const size_t AMD_IOMMU_EVENT_LOG_ALIGN = 4096;

struct amd_iommu_event_log {
	amd_iommu_t *aiel_iommu;
	uint64_t aiel_phys_addr;
	amd_iommu_evle_t *aiel_evt_log;
	size_t aiel_size;
	size_t aiel_head;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_AMD_IOMMU_H */
