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
 * Copyright 2024 Oxide Computer Co.
 */

#ifndef _SYS_IO_ZEN_NBIF_IMPL_H
#define	_SYS_IO_ZEN_NBIF_IMPL_H

/*
 * Zen-specific register and bookkeeping definitions for north bridge
 * interconnect (?) functions (?) (nBIF or NBIF).  This subsystem provides a
 * PCIe-ish interface to a variety of components like USB and SATA that are not
 * supported by this machine architecture.
 */

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/nbif.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The maximum number of functions supported by the hardware. Each
 * NBIF has potentially one or more root complexes and endpoints.
 */
#define	ZEN_NBIF0_NFUNCS	3
#define	ZEN_NBIF1_NFUNCS	7
#define	ZEN_NBIF2_NFUNCS	3

/*
 * These types aren't exposed but we need forward declarations to break the type
 * dependency cycle so we can have both the child types and a pointer to the
 * parent.
 */
struct zen_nbif_func;
typedef struct zen_nbif_func zen_nbif_func_t;

typedef enum zen_nbif_func_type {
	ZEN_NBIF_T_DUMMY,
	ZEN_NBIF_T_NTB,
	ZEN_NBIF_T_NVME,
	ZEN_NBIF_T_PTDMA,
	ZEN_NBIF_T_PSPCCP,
	ZEN_NBIF_T_USB,
	ZEN_NBIF_T_AZ,
	ZEN_NBIF_T_SATA
} zen_nbif_func_type_t;

typedef enum zen_nbif_func_flag {
	/*
	 * This NBIF function should be enabled.
	 */
	ZEN_NBIF_F_ENABLED	= 1 << 0,
	/*
	 * This NBIF does not need any configuration or manipulation. This
	 * generally is the case because we have a dummy function.
	 */
	ZEN_NBIF_F_NO_CONFIG	= 1 << 1
} zen_nbif_func_flag_t;

struct zen_nbif_func {
	zen_nbif_func_type_t	zne_type;
	zen_nbif_func_flag_t	zne_flags;
	uint8_t			zne_dev;
	uint8_t			zne_func;
	zen_nbif_t		*zne_nbif;
};

struct zen_nbif {
	uint8_t			zn_nbifno;
	uint8_t			zn_nfuncs;
	zen_nbif_func_t		zn_funcs[ZEN_NBIF_MAX_FUNCS];
	zen_ioms_t		*zn_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_NBIF_IMPL_H */
