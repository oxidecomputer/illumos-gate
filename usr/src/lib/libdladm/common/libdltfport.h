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

#ifndef _LIBDLTFPORT_H
#define	_LIBDLTFPORT_H

#include <sys/mac.h>
#include <libdladm_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dladm_tfport_attr {
	datalink_id_t		tfa_link_id;
	datalink_id_t		tfa_pkt_id;
	uint32_t		tfa_port_id;
	uchar_t			tfa_mac_addr[ETHERADDRL];
	uint_t			tfa_mac_len;
} dladm_tfport_attr_t;

dladm_status_t dladm_tfport_create(dladm_handle_t, const char *,
    datalink_id_t, uint32_t, char *, size_t);
dladm_status_t dladm_tfport_delete(dladm_handle_t, datalink_id_t);
dladm_status_t dladm_tfport_info(dladm_handle_t, datalink_id_t,
    dladm_tfport_attr_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBDLTFPORT_H */
