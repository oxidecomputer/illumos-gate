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

#ifndef _SYS_IO_MILAN_NBIF_IMPL_H
#define	_SYS_IO_MILAN_NBIF_IMPL_H

/*
 * Milan-specific register and bookkeeping definitions for north bridge
 * interconnect (?) functions (?) (nBIF or NBIF).  This subsystem provides a
 * PCIe-ish interface to a variety of components like USB and SATA that are not
 * supported by this machine architecture.
 */

#include <sys/io/milan/fabric_impl.h>
#include <sys/io/milan/nbif.h>
#include <sys/io/zen/nbif_impl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The maximum number of functions is based on the hardware design here. Each
 * NBIF has potentially one or more root complexes and endpoints.
 */
#define	MILAN_NBIF0_NFUNCS	3
#define	MILAN_NBIF1_NFUNCS	7
#define	MILAN_NBIF2_NFUNCS	3

/*
 * These types aren't exposed but we need forward declarations to break the type
 * dependency cycle so we can have both the child types and a pointer to the
 * parent.
 */

typedef enum milan_nbif_func_type {
	MILAN_NBIF_T_DUMMY,
	MILAN_NBIF_T_NTB,
	MILAN_NBIF_T_NVME,
	MILAN_NBIF_T_PTDMA,
	MILAN_NBIF_T_PSPCCP,
	MILAN_NBIF_T_USB,
	MILAN_NBIF_T_AZ,
	MILAN_NBIF_T_SATA
} milan_nbif_func_type_t;

typedef struct milan_nbif_func {
	milan_nbif_func_type_t	mne_type;
} milan_nbif_func_t;

typedef struct milan_nbif {
	milan_nbif_func_t	mn_funcs[MILAN_NBIF_MAX_FUNCS];
} milan_nbif_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_NBIF_IMPL_H */
