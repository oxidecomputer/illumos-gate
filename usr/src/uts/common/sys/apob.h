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

#ifndef _SYS_APOB_H
#define	_SYS_APOB_H

#include <sys/types.h>
#include <sys/bitext.h>
#include <sys/stdbool.h>
#include <sys/sunddi.h>

/*
 * Definitions that relate to parsing and understanding the processor family
 * independent attributes of the APOB.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	APOB_MIN_LEN	16

struct apob_hdl;
typedef struct apob_hdl apob_hdl_t;

typedef enum apob_group {
	APOB_GROUP_MEMORY = 1,
	APOB_GROUP_DF,
	APOB_GROUP_CCX,
	APOB_GROUP_NBIO,
	APOB_GROUP_FCH,
	APOB_GROUP_PSP,
	APOB_GROUP_GENERAL,
	APOB_GROUP_SMBIOS,
	APOB_GROUP_FABRIC,
	APOB_GROUP_APCB
} apob_group_t;

typedef enum apob_fabric_type {
	APOB_FABRIC_TYPE_SYS_MEM_MAP = 9,
} apob_fabric_type_t;

typedef enum apob_mem_hole_type {
	APOB_MEM_HOLE_TYPE_BDAT = 18,
} apob_mem_hole_type_t;

#pragma pack(1)

/*
 * Describes a region of physical address space which may not be used as RAM.
 */
typedef struct apob_sys_mem_map_hole {
	/*
	 * Base physical address of this hole.
	 */
	uint64_t		asmmh_base;
	/*
	 * The size in bytes of this hole.
	 */
	uint64_t		asmmh_size;
	/*
	 * A tag indicating the purpose of this hole -- the specific values
	 * may vary between different microarchitectures and/or firmware.
	 */
	uint32_t		asmmh_type;
	uint32_t		asmmh_padding;
} apob_sys_mem_map_hole_t;

/*
 * The structure returned for (group, type, instance) = (APOB_GROUP_FABRIC,
 * APOB_FABRIC_TYPE_SYS_MEM_MAP, 0) which describes the upper bound of available
 * memory and what ranges to explicitly avoid.
 */
typedef struct apob_sys_mem_map {
	/*
	 * The physical address representing the upper limit (exclusive) of
	 * available RAM.
	 */
	uint64_t		asmm_high_phys;
	/*
	 * The number of apob_sys_mem_map_hole entries laid out after the end of
	 * this structure in the APOB. There should always be at least one entry
	 * but the maximum possible number of entries is variable.
	 */
	uint32_t		asmm_hole_count;
	uint32_t		asmm_padding;
	/*
	 * The collection of asmm_hole_count address ranges that should be
	 * reserved and otherwise not treated as RAM.
	 */
	apob_sys_mem_map_hole_t	asmm_holes[];
} apob_sys_mem_map_t;

#pragma pack()	/* pack(1) */

/*
 * These functions are implemented in code that is common to the kernel and
 * possible user consumers.
 */
extern size_t apob_handle_size(void);
extern size_t apob_init_handle(apob_hdl_t *, const uint8_t *, const size_t);
extern size_t apob_get_len(const apob_hdl_t *);
extern const uint8_t *apob_get_raw(const apob_hdl_t *);
extern const void *apob_find(apob_hdl_t *, const apob_group_t, const uint32_t,
    const uint32_t, size_t *);
extern int apob_errno(const apob_hdl_t *);
extern const char *apob_errmsg(const apob_hdl_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_APOB_H */
