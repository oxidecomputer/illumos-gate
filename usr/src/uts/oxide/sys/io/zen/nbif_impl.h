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

#ifndef	_SYS_IO_ZEN_NBIF_IMPL_H
#define	_SYS_IO_ZEN_NBIF_IMPL_H

/*
 * Type definitions, structures, constants, and similar used in the
 * implementation of the NBIF across Zen microarchitectures.
 */

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>

#define	ZEN_NBIF_MAX_FUNCS	10

typedef struct zen_nbif zen_nbif_t;
typedef struct zen_nbif_func zen_nbif_func_t;

typedef int (*zen_nbif_cb_f)(zen_nbif_t *, void *);

typedef enum {
	/*
	 * This NBIF function should be enabled.
	 */
	ZEN_NBIF_F_ENABLED	= 1 << 0,

	/*
	 * This NBIF does not need any configuration or manipulation. This
	 * generally is the case because we have a dummy function.
	 */
	ZEN_NBIF_F_NO_CONFIG	= 1 << 1,

	/*
	 * Enable FLR support.
	 */
	ZEN_NBIF_F_FLR_EN	= 1 << 2,

	/*
	 * Enable the ACS capability.
	 */
	ZEN_NBIF_F_ACS_EN	= 1 << 3,

	/*
	 * Enable the AER capability.
	 */
	ZEN_NBIF_F_AER_EN	= 1 << 4,

	/*
	 * Enable communication of Dx state change.
	 */
	ZEN_NBIF_F_PMSTATUS_EN	= 1 << 5,

	/*
	 * Expose TPH Requester Capability.
	 */
	ZEN_NBIF_F_TPH_CPLR_EN	= 1 << 6,

	/*
	 * Enable Poisoned Error log as Advisory NonFatal Error.
	 */
	ZEN_NBIF_F_PANF_EN	= 1 << 7
} zen_nbif_func_flag_t;

typedef enum {
	ZEN_NBIF_T_ABSENT = 0,
	ZEN_NBIF_T_DUMMY,
	ZEN_NBIF_T_ACP,
	ZEN_NBIF_T_AZ,
	ZEN_NBIF_T_MPDMATF,
	ZEN_NBIF_T_NTB,
	ZEN_NBIF_T_NVME,
	ZEN_NBIF_T_PSPCCP,
	ZEN_NBIF_T_PTDMA,
	ZEN_NBIF_T_PVNTB,
	ZEN_NBIF_T_SATA,
	ZEN_NBIF_T_SVNTB,
	ZEN_NBIF_T_USB,
} zen_nbif_func_type_t;

typedef struct zen_nbif_info {
	zen_nbif_func_type_t	zni_type;
	bool			zni_enabled;
	uint8_t			zni_dev;
	uint8_t			zni_func;
} zen_nbif_info_t;

struct zen_nbif_func {
	uint8_t			znf_num;

	/*
	 * The type of this function.
	 */
	zen_nbif_func_type_t	znf_type;

	/*
	 * Software-defined flags for this function.
	 */
	zen_nbif_func_flag_t	znf_flags;

	/*
	 * The PCIe device and function numbers for this NBIF func.
	 */
	uint8_t			znf_dev;
	uint8_t			znf_func;

	zen_nbif_t		*znf_nbif;
};

struct zen_nbif {
	uint8_t			zn_num;
	uint8_t			zn_nfuncs;
	zen_nbif_func_t		zn_funcs[ZEN_NBIF_MAX_FUNCS];
	zen_ioms_t		*zn_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_NBIF_IMPL_H */
