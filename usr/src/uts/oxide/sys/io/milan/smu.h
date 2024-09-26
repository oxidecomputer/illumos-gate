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

#ifndef _SYS_IO_MILAN_SMU_H
#define	_SYS_IO_MILAN_SMU_H

/*
 * Defines Milan-specific SMN register addresses for SMU RPCs.  These are stored
 * in the microarchitecture-specific platform constants, and consumed in by the
 * Zen-generic SMU SMN register generator function defined in
 * sys/io/zen/smu_impl.h and called from the SMU RPC code zen_smu.c.
 */

#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_REQ	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x530,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_RESP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x57c,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_ARG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9c4,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_ARG1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9c8,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_ARG2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9cc,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_ARG3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9d0,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_ARG4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9d4,		\
}

/*CSTYLED*/
#define	D_MILAN_SMU_RPC_ARG5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9d8,		\
}

/*
 * Entry point for setting SMU features on Milan.
 */
extern bool milan_smu_features_init(zen_iodie_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_SMU_H */
