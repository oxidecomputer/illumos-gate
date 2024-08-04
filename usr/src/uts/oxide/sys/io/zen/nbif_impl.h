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

#ifndef	_SYS_IO_ZEN_NBIF_IMPL_H
#define	_SYS_IO_ZEN_NBIF_IMPL_H

#include <sys/bitext.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>

#define	ZEN_NBIF_MAX_FUNCS	7

typedef struct zen_nbif zen_nbif_t;
typedef struct zen_nbif_func zen_nbif_func_t;

/*
 * Function callback signature.
 */
typedef int (*zen_nbif_cb_f)(zen_nbif_t *, void *);

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
	/*
	 * Software-defined flags for this function.
	 */
	zen_nbif_func_flag_t	znf_flags;

	/*
	 * PCIe device number associated with this function.
	 */
	uint8_t			znf_dev;

	/*
	 * This function's PCIe function number.
	 */
	uint8_t			znf_func;

	/*
	 * A pointer to the NBIF this function is associated with.
	 */
	zen_nbif_t		*znf_nbif;

	/*
	 * A pointer to microarchitecturally-specific data for this
	 * function.
	 */
	void			*znf_uarch_nbif_func;
};

struct zen_nbif {
	/*
	 * This NBIF's index.
	 */
	uint8_t			zn_nbifno;

	/*
	 * The actual number of functions on this NBIF.
	 */
	uint8_t			zn_nfuncs;

	/*
	 * The functions on this NBIF.
	 */
	zen_nbif_func_t		zn_funcs[ZEN_NBIF_MAX_FUNCS];

	/*
	 * A pointer to the IOMS this NBIF is attached to.
	 */
	zen_ioms_t		*zn_ioms;
};

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_NBIF_IMPL_H */
