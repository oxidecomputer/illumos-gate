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

/*
 * Defines Turin-specific SMN register addresses for SMU RPCs.  These are stored
 * in the microarchitecture-specific platform constants, and consumed in by the
 * Zen-generic SMU SMN register generator defined in sys/io/zen/smu_impl.h and
 * called from the SMU RPC code zen_smu.c.
 */

#ifndef _SYS_IO_TURIN_SMU_H
#define	_SYS_IO_TURIN_SMU_H

/*
 * The implementation of these types is exposed to implementers but not to
 * consumers; therefore we forward-declare them here and provide the actual
 * definitions only in the corresponding *_impl.h.  Consumers are allowed to use
 * pointers to these types only as opaque handles.
 */
typedef struct zen_iodie zen_iodie_t;

#include <sys/stdbool.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_REQ	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x930,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_RESP	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x97c,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_ARG0	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9c4,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_ARG1	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9c8,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_ARG2	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9cc,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_ARG3	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9d0,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_ARG4	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9d4,		\
}

/*CSTYLED*/
#define	D_TURIN_SMU_RPC_ARG5	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_SMU_RPC,	\
	.srd_reg = 0x9d8,		\
}

/*
 * Entry points for setting SMU features on Turin.
 */
extern bool turin_smu_early_features_init(zen_iodie_t *);
extern bool turin_smu_features_init(zen_iodie_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_TURIN_SMU_H */
