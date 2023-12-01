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
 * Copyright 2024 Oxide Computer Company
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

#define	APOB_MIN_LEN	16

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
