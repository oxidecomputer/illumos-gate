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

#ifndef	_SYS_IO_ZEN_DXIO_IMPL_H
#define	_SYS_IO_ZEN_DXIO_IMPL_H

/*
 * Type, structure, and function definitions for interacting with DXIO via the
 * SMU for things like driving the DXIO crossbar to train PCIe lanes, etc.
 */

#include <sys/types.h>

#include <sys/io/zen/dxio.h>
#include <sys/io/zen/fabric.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 * This macro should be a value like 0xff because this reset group is defined to
 * be an opaque token that is passed back to us. However, if we actually want to
 * do something with reset and get a chance to do something before the DXIO
 * engine begins training, that value will not work and experimentally the value
 * 0x1 (which is what Ethanol and others use, likely every other board too),
 * then it does. For the time being, use this for our internal things which
 * should go through GPIO expanders so we have a chance of being a fool of a
 * Took.
 */
#define	ZEN_DXIO_FW_GROUP_UNUSED	0x01
#define	ZEN_DXIO_FW_PLATFORM_EPYC	0x00

typedef struct zen_dxio_config {
	zen_dxio_fw_platform_t	*zdc_conf;
	zen_dxio_fw_anc_data_t	*zdc_anc;
	uint64_t		zdc_pa;
	uint64_t		zdc_anc_pa;
	uint32_t		zdc_alloc_len;
	uint32_t		zdc_conf_len;
	uint32_t		zdc_anc_len;
} zen_dxio_config_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_DXIO_IMPL_H */
