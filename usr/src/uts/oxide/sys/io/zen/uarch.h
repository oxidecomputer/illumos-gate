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
#include <sys/amdzen/smn.h>
#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/nbif_impl.h>

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
	 * Program the IOHC registers relating to where the top of memory is.
	 */
	void		(*zfo_init_tom)(zen_ioms_t *, uint64_t, uint64_t,
	    uint64_t);

	/*
	 * Disable the VGA MMIO hole.
	 */
	void		(*zfo_disable_vga)(zen_ioms_t *);

	/*
	 * Configure the IOHC PCI device's subsystem identifiers.
	 */
	void		(*zfo_iohc_pci_ids)(zen_ioms_t *);

	/*
	 * Configure the PCIe reference clock.
	 */
	void		(*zfo_pcie_refclk)(zen_ioms_t *);

	/*
	 * Configure PCI configuration space timeouts.
	 */
	void		(*zfo_pci_crs_to)(zen_ioms_t *, uint16_t, uint16_t);

	/*
	 * Initialize IOHC features
	 */
	void		(*zfo_iohc_features)(zen_ioms_t *);

	/*
	 * Program each IOHC with its primary bus number
	 */
	void		(*zfo_iohc_bus_num)(zen_ioms_t *, uint8_t);

	/*
	 * Program each IOMS' knowledge of whether they have an FCH.
	 */
	void		(*zfo_iohc_fch_link)(zen_ioms_t *, bool);

	/*
	 * IOHC arbitration control.
	 */
	void		(*zfo_iohc_arbitration)(zen_ioms_t *);

	/*
	 * nBIF DMA arbitration control.
	 */
	void		(*zfo_nbif_arbitration)(zen_nbif_t *);

	/*
	 * SDP port control register setup.
	 */
	void		(*zfo_sdp_control)(zen_ioms_t *);

	/*
	 * SYSHUB DMA tweaks.
	 */
	void		(*zfo_nbif_syshub_dma)(zen_nbif_t *);

	/*
	 * IOAPIC initialization.
	 */
	void		(*zfo_ioapic)(zen_ioms_t *);

	/*
	 * Finalize setting up the PCIe fabric.
	 */
	void		(*zfo_pcie)(zen_fabric_t *);

	/*
	 * Retrieve register handles for PCIe core and port registers.
	 */
	smn_reg_t	(*zfo_pcie_port_reg)(const zen_pcie_port_t *const,
	    const smn_reg_def_t);
	smn_reg_t	(*zfo_pcie_core_reg)(const zen_pcie_core_t *const,
	    const smn_reg_def_t);

	/*
	 * Signal that we're collecting register data.
	 */
	void		(*zfo_pcie_dbg_signal)(void);

	/*
	 * Enables and EOIs NMIs generated through the IO fabric, for instance
	 * via an external pin.
	 */
	void		(*zfo_enable_nmi)(void);
	void		(*zfo_nmi_eoi)(void);

	/*
	 * The following (optional) functions provide callbacks for any
	 * uarch-specific logic during fabric topology initialization.
	 */
	void		(*zfo_topo_init)(zen_fabric_t *);
	void		(*zfo_soc_init)(zen_soc_t *);
	void		(*zfo_iodie_init)(zen_iodie_t *);
	void		(*zfo_smu_misc_init)(zen_iodie_t *);
	void		(*zfo_ioms_init)(zen_ioms_t *);

	/*
	 * Retrieves and reports the version of the firmware for the component
	 * responsible for interfacing with the DXIO crossbar (either the SMU or
	 * MPIO).
	 */
	bool		(*zfo_get_dxio_fw_version)(zen_iodie_t *);
	void		(*zfo_report_dxio_fw_version)(const zen_iodie_t *);
} zen_fabric_ops_t;

typedef struct zen_hack_ops {
	void	(*zho_check_furtive_reset)(void);
	bool	(*zho_cgpll_set_ssc)(bool);
} zen_hack_ops_t;

/*
 * These null operations are no-ops, for operations that are unnecessary on a
 * given microarchitecture.
 */
extern void zen_null_check_furtive_reset(void);
extern bool zen_null_cgpll_set_ssc(bool);
extern void zen_null_fabric_iohc_pci_ids(zen_ioms_t *);
extern void zen_null_fabric_nbif_arbitration(zen_nbif_t *);
extern void zen_null_fabric_sdp_control(zen_ioms_t *);
extern void zen_null_fabric_nbif_syshub_dma(zen_nbif_t *);

typedef struct zen_ras_ops {
	void	(*zro_ras_init)(void);
} zen_ras_ops_t;

/*
 * These are register constants for accessing SMU RPC registers via SMN.
 */
typedef struct zen_smu_smn_addrs {
	/*
	 * The RPC request register holds the RPC request operation number.
	 */
	const smn_reg_def_t	zssa_req;

	/*
	 * The response register holds the SMU response to an RPC, as well as
	 * the specific request type.
	 */
	const smn_reg_def_t	zssa_resp;

	/*
	 * There are six argument registers that are dual purposed for both
	 * input to and output from the RPC.
	 */
	const smn_reg_def_t	zssa_arg0;
	const smn_reg_def_t	zssa_arg1;
	const smn_reg_def_t	zssa_arg2;
	const smn_reg_def_t	zssa_arg3;
	const smn_reg_def_t	zssa_arg4;
	const smn_reg_def_t	zssa_arg5;
} zen_smu_smn_addrs_t;

/*
 * These are register constants for accessing MPIO RPC registers via SMN.
 */
typedef struct zen_mpio_smn_addrs {
	/*
	 * There are six argument registers that are dual purposed for both
	 * input to and output from the RPC.
	 */
	const smn_reg_def_t	zmsa_arg0;
	const smn_reg_def_t	zmsa_arg1;
	const smn_reg_def_t	zmsa_arg2;
	const smn_reg_def_t	zmsa_arg3;
	const smn_reg_def_t	zmsa_arg4;
	const smn_reg_def_t	zmsa_arg5;

	/*
	 * The response register.  The RPC mechanism writes the requested
	 * operation to this register, then rings the doorbell by strobing the
	 * doorbell register.  The response will be in this register.
	 *
	 * To recover the response, a caller polls this register until the high
	 * bit (ZEN_MPIO_RESP_READY) is set.  Finally, the response is extracted
	 * from the low bits.
	 */
	const smn_reg_def_t	zmsa_resp;

	/*
	 * The RPC mechanism strobes the doorbell register to initiate the RPC
	 * after filling in the arguments and request type.
	 */
	const smn_reg_def_t	zmsa_doorbell;
} zen_mpio_smn_addrs_t;

/*
 * These are constants specific to a given platform.  These are as distinct from
 * the maximum architectural constants across all platforms implemented in the
 * Oxide architecture.
 */
typedef struct zen_platform_consts {
	/*
	 * The specific DF revision supported by this platform.
	 * Note this is only used very early on before the fabric topology
	 * is initialized and compared against the version discovered dynmically
	 * on each I/O die.
	 */
	const df_rev_t			zpc_df_rev;

	/*
	 * The maximum number of PCI Bus configuration address maps.
	 */
	const size_t			zpc_max_cfgmap;

	/*
	 * The maximum number of I/O routing rules.
	 */
	const size_t			zpc_max_iorr;

	/*
	 * The maximum number of MMIO routing rules.
	 */
	const size_t			zpc_max_mmiorr;

	/*
	 * These represent the microarchitecture-specific max counts of various
	 * components on a Zen SoC.
	 */
	const uint8_t			zpc_ccds_per_iodie;
	const uint8_t			zpc_cores_per_ccx;

	/*
	 * The platform-specific SMN addresses of the SMU RPC registers.
	 */
	const zen_smu_smn_addrs_t	zpc_smu_smn_addrs;

	/*
	 * The platform-specific SMN addresses of the MPIO RPC registers.
	 */
	const zen_mpio_smn_addrs_t	zpc_mpio_smn_addrs;

	/*
	 * Platform-specific data for configuring the nBIF devices and
	 * functions.
	 */
	const uint8_t			zpc_nnbif;
	const uint8_t			*zpc_nbif_nfunc;
	const zen_nbif_info_t		(*zpc_nbif_data)[ZEN_NBIF_MAX_FUNCS];

	/*
	 * These are pointers to tables of PCIe core and port registers which
	 * should be sampled at various points during initialization. This is
	 * only done in DEBUG kernels.
	 */
	const zen_pcie_reg_dbg_t	*zpc_pcie_core_dbg_regs;
	const size_t			zpc_pcie_core_dbg_nregs;
	const zen_pcie_reg_dbg_t	*zpc_pcie_port_dbg_regs;
	const size_t			zpc_pcie_port_dbg_nregs;
} zen_platform_consts_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_UARCH_H */
