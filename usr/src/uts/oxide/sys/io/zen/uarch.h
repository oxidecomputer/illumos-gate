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

#ifndef	_SYS_IO_ZEN_UARCH_H
#define	_SYS_IO_ZEN_UARCH_H

/*
 * This header file declares all the ops vectors that must be implemented by
 * every supported Zen microarchitecture.
 */

#include <sys/stdbool.h>
#include <sys/types.h>

#include <sys/amdzen/df.h>
#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_apob_ops {
	void	(*zao_reserve_phys)(void);
} zen_apob_ops_t;

typedef struct zen_ccx_ops {
	void	(*zco_init)(void);
	bool	(*zco_start_thread)(const zen_thread_t *);

	/*
	 * Optional hook for any further microachitecture-specific physical
	 * memory initialization.
	 */
	void	(*zco_physmem_init)(void);
} zen_ccx_ops_t;

typedef struct zen_fabric_ops {
	/*
	 * Initialize the SoC's I/O fabric, including the SMU, DXIO, NBIO, etc.
	 */
	void		(*zfo_fabric_init)(void);

	/*
	 * Enables and EOIs NMIs generated through the IO fabric, for instance
	 * via an external pin.
	 */
	void		(*zfo_enable_nmi)(void);
	void		(*zfo_nmi_eoi)(void);

	/*
	 * The following (optional) functions provide callbacks for any
	 * uarch-specific logic during fabric topology initialization.
	 * This provides an opportunity to initialize the uarch-specific
	 * void pointers in the given topology structures.
	 */
	void		(*zfo_topo_init)(zen_fabric_t *);
	void		(*zfo_soc_init)(zen_soc_t *, zen_iodie_t *);
	void		(*zfo_ioms_init)(zen_ioms_t *);
} zen_fabric_ops_t;

typedef struct zen_hack_ops {
	void	(*zho_check_furtive_reset)(void);
	bool	(*zho_cgpll_set_ssc)(bool);
} zen_hack_ops_t;

/*
 * These null operations are no-ops, for hacks that are unnecessary on a given
 * microarchitecture.
 */
extern void zen_null_check_furtive_reset(void);
extern bool zen_null_cgpll_set_ssc(bool);

typedef struct zen_ras_ops {
	void	(*zro_ras_init)(void);
} zen_ras_ops_t;

/*
 * These are constants specific to a given platform.  These are as distinct from
 * the maximum architectural constants across all platforms implemented in the
 * Oxide arhcitecture.
 */
typedef struct zen_platform_consts {
	/*
	 * The specific DF revision supported by this platform.
	 * Note this is only used very early on before the fabric topology
	 * is initialized and compared against the version discovered dynmically
	 * on each I/O die.
	 */
	const df_rev_t		zpc_df_rev;

	/*
	 * These represent the microarchitecture-specific max counts of various
	 * components on a Zen SoC.
	 */
	const uint8_t		zpc_ccds_per_iodie;
	const uint8_t		zpc_cores_per_ccx;
} zen_platform_consts_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_UARCH_H */
