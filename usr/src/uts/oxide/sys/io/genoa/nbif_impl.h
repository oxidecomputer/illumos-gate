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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_IO_GENOA_NBIF_IMPL_H
#define	_SYS_IO_GENOA_NBIF_IMPL_H

/*
 * Genoa-specific register and bookkeeping definitions for north bridge
 * interconnect (?) functions (?) (nBIF or NBIF).  This subsystem provides a
 * PCIe-ish interface to a variety of components like USB and SATA that are not
 * supported by this machine architecture.
 */

#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/nbif.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The maximum number of functions is based on the hardware design here. Each
 * NBIF has potentially one or more root complexes and endpoints.
 */
#define	GENOA_NBIF0_NFUNCS	3
#define	GENOA_NBIF1_NFUNCS	7
#define	GENOA_NBIF2_NFUNCS	3

/*
 * These types aren't exposed but we need forward declarations to break the type
 * dependency cycle so we can have both the child types and a pointer to the
 * parent.
 */
struct genoa_nbif_func;
typedef struct genoa_nbif_func genoa_nbif_func_t;

typedef enum genoa_nbif_func_type {
	GENOA_NBIF_T_DUMMY,
	GENOA_NBIF_T_NTB,
	GENOA_NBIF_T_NVME,
	GENOA_NBIF_T_PTDMA,
	GENOA_NBIF_T_PSPCCP,
	GENOA_NBIF_T_USB,
	GENOA_NBIF_T_AZ,
	GENOA_NBIF_T_SATA
} genoa_nbif_func_type_t;

typedef enum genoa_nbif_func_flag {
	/*
	 * This NBIF function should be enabled.
	 */
	GENOA_NBIF_F_ENABLED	= 1 << 0,
	/*
	 * This NBIF does not need any configuration or manipulation. This
	 * generally is the case because we have a dummy function.
	 */
	GENOA_NBIF_F_NO_CONFIG	= 1 << 1
} genoa_nbif_func_flag_t;

struct genoa_nbif_func {
	genoa_nbif_func_type_t	mne_type;
	genoa_nbif_func_flag_t	mne_flags;
	uint8_t			mne_dev;
	uint8_t			mne_func;
	genoa_nbif_t		*mne_nbif;
};

struct genoa_nbif {
	uint8_t			mn_nbifno;
	uint8_t			mn_nfuncs;
	genoa_nbif_func_t	mn_funcs[GENOA_NBIF_MAX_FUNCS];
	genoa_ioms_t		*mn_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_NBIF_IMPL_H */
