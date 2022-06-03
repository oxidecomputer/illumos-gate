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
 * Copyright 2022 Oxide Computer Company
 */

#ifndef _LIBDLTFPORT_H
#define	_LIBDLTFPORT_H

#include <sys/mac.h>
#include <libdladm_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dladm_tfport_attr {
	datalink_id_t		tfa_link_id;
	uint32_t		tfa_port_id;
	uchar_t			tfa_mac_addr[MAXMACADDRLEN];
	uint_t			tfa_mac_len;
} dladm_tfport_attr_t;

dladm_status_t dladm_tfport_create(dladm_handle_t, const char *,
    uint_t, uchar_t *, size_t);
dladm_status_t dladm_tfport_delete(dladm_handle_t, datalink_id_t);
dladm_status_t dladm_tfport_info(dladm_handle_t, datalink_id_t,
    dladm_tfport_attr_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBDLTFPORT_H */
