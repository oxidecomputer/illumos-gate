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

#ifndef	_SYS_IO_ZEN_UARCH_H
#define	_SYS_IO_ZEN_UARCH_H

/*
 * This header file declares all the ops vectors that must be implemented by
 * every supported Zen microarchitecture.
 */

#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/x86_archext.h>

#include <sys/amdzen/df.h>
#include <sys/amdzen/smn.h>
#include <sys/io/zen/ccx.h>
#include <sys/io/zen/fabric_impl.h>
#include <sys/io/zen/nbif_impl.h>
#include <sys/io/zen/oxio.h>
#include <sys/io/zen/ras_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zen_ccx_ops {
	/*
	 * Optional hook for any further microachitecture-specific physical
	 * memory initialization.
	 */
	void	(*zco_physmem_init)(void);

	/*
	 * Platform-specific hook to provide the digital power management (DPM)
	 * weights to set at ccx initialization.  An implementation may return
	 * a length of 0 to indicate that DPM weights should not be explicitly
	 * set from the host.  zen_fabric_thread_get_dpm_weights_noop does this
	 * and has the correct signature for use here.
	 */
	void	(*zco_get_dpm_weights)(const zen_thread_t *, const uint64_t **,
	    uint32_t *);

	/*
	 * The microarchitecture specific hooks called during CCX initialization
	 * to setup various functional units within a thread/core/core complex.
	 *
	 * LS: load-store, the gateway to the thread
	 * IC: (L1) instruction cache
	 * DC: (L1) data cache
	 * TW: table walker (part of the MMU)
	 * DE: instruction decode(/execute?)
	 * FP: floating point
	 * L2, L3: caches
	 * UC: microcode -- this is not microcode patch/upgrade
	 *
	 * Feature initialization refers to setting up the internal registers
	 * that are reflected into cpuid leaf values.
	 *
	 * All of these routines must be infallible; avoid using on_trap() or
	 * similar as we want to panic if any of the necessary registers do not
	 * exist or cannot be accessed. Implementations should use
	 * wrmsr_and_test() which, when building with DEBUG enabled, will panic
	 * if writing the bits we intend to change is ineffective. None of these
	 * outcomes should ever be possible on a supported processor; indeed,
	 * understanding what to do here is a critical element of adding support
	 * for a new processor family or revision.
	 */
	void	(*zco_thread_feature_init)(void);
	void	(*zco_thread_uc_init)(void);
	void	(*zco_core_ls_init)(void);
	void	(*zco_core_ic_init)(void);
	void	(*zco_core_dc_init)(void);
	void	(*zco_core_tw_init)(void);
	void	(*zco_core_de_init)(void);
	void	(*zco_core_fp_init)(void);
	void	(*zco_core_l2_init)(void);
	void	(*zco_ccx_l3_init)(void);
	void	(*zco_core_undoc_init)(void);

} zen_ccx_ops_t;

typedef struct zen_fabric_ops {
	/*
	 * Sets microarchitecture-specific MPIO global configuration options.
	 */
	void		(*zfo_set_mpio_global_config)(
	    zen_mpio_global_config_t *);

	/*
	 * Determine the physical address width (in bits). This is used early
	 * in boot, pre CPUID_PASS_BASIC and therefore before we can use
	 * cpuid_get_addrsize().
	 */
	uint8_t		(*zfo_physaddr_size)(void);

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
	void		(*zfo_nbio_features)(zen_nbio_t *);

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
	void		(*zfo_nbio_arbitration)(zen_nbio_t *);

	/*
	 * nBIF DMA arbitration control.
	 */
	void		(*zfo_nbif_arbitration)(zen_nbif_t *);

	/*
	 * SDP port control register setup.
	 */
	void		(*zfo_sdp_control)(zen_ioms_t *);
	void		(*zfo_nbio_sdp_control)(zen_nbio_t *);

	/*
	 * SYSHUB DMA tweaks.
	 */
	void		(*zfo_nbif_syshub_dma)(zen_nbif_t *);

	/*
	 * IOHC and friends clock gating.
	 */
	void		(*zfo_iohc_clock_gating)(zen_ioms_t *);
	void		(*zfo_nbio_clock_gating)(zen_nbio_t *);

	/*
	 * nBIF clock gating.
	 */
	void		(*zfo_nbif_clock_gating)(zen_nbif_t *);

	/*
	 * IOAPIC clock gating.
	 */
	void		(*zfo_ioapic_clock_gating)(zen_ioms_t *);

	/*
	 * IOAPIC initialization.
	 */
	void		(*zfo_ioapic)(zen_ioms_t *);

	/*
	 * Configure nBIF straps.
	 */
	void		(*zfo_nbif_dev_straps)(zen_nbif_t *);

	/*
	 * Update nBIF bridges.
	 */
	void		(*zfo_nbif_bridges)(zen_ioms_t *);

	/*
	 * Finalize setting up the PCIe fabric.
	 */
	void		(*zfo_pcie)(zen_fabric_t *);

	/*
	 * Determine the NBIO that contains a given IOMS.
	 */
	uint8_t		(*zfo_ioms_nbio_num)(const uint8_t);

	/*
	 * Determine the number of PCIe cores on a given IOHC, and the number
	 * of ports on a given core.
	 */
	uint8_t		(*zfo_iohc_n_pcie_cores)(const uint8_t);
	uint8_t		(*zfo_pcie_core_n_ports)(const uint8_t);

	/*
	 * Retrieve information about the configuration of a PCIe core or port.
	 */
	const zen_pcie_core_info_t *(*zfo_pcie_core_info)(const uint8_t,
	    const uint8_t);
	const zen_pcie_port_info_t *(*zfo_pcie_port_info)(const uint8_t,
	    const uint8_t);

	/*
	 * Retrieve register handles for PCIe core and port registers.
	 */
	smn_reg_t	(*zfo_pcie_port_reg)(const zen_pcie_port_t *const,
	    const smn_reg_def_t);
	smn_reg_t	(*zfo_pcie_core_reg)(const zen_pcie_core_t *const,
	    const smn_reg_def_t);

	/*
	 * Optional microarchitecture-specific overrides for PCIe core and port
	 * register access if the default SMN accessors are not appropriate.
	 */
	uint32_t	(*zfo_pcie_core_read)(zen_pcie_core_t *,
	    const smn_reg_t);
	void		(*zfo_pcie_core_write)(zen_pcie_core_t *,
	    const smn_reg_t, const uint32_t);
	uint32_t	(*zfo_pcie_port_read)(zen_pcie_port_t *,
	    const smn_reg_t);
	void		(*zfo_pcie_port_write)(zen_pcie_port_t *,
	    const smn_reg_t, const uint32_t);

	/*
	 * Signal that we're collecting register data.
	 */
	void		(*zfo_pcie_dbg_signal)(void);

	/*
	 * Enables and EOIs NMIs generated through the IO fabric, for instance
	 * via an external pin.
	 */
	void		(*zfo_iohc_enable_nmi)(zen_ioms_t *);
	void		(*zfo_iohc_nmi_eoi)(zen_ioms_t *);

	/*
	 * The following (optional) functions provide callbacks for any
	 * uarch-specific logic during fabric topology initialization.
	 */
	void		(*zfo_smu_misc_init)(zen_iodie_t *);
	/*
	 * This hook for late SMU init is called after all CPUs are online and
	 * should only be used to work around specific platform problems. In
	 * general prefer zfo_smu_misc_init.
	 */
	void		(*zfo_smu_misc_late_init)(zen_iodie_t *);
	bool		(*zfo_smu_pptable_init)(zen_fabric_t *, void *,
	    size_t *);
	void		(*zfo_smu_pptable_post)(zen_iodie_t *);
	void		(*zfo_nbio_init)(zen_nbio_t *);
	void		(*zfo_ioms_init)(zen_ioms_t *);
	void		(*zfo_nbif_init)(zen_nbif_t *);

	/*
	 * Returns true iff a port is trained.
	 */
	bool		(*zfo_pcie_port_is_trained)(const zen_pcie_port_t *);

	/*
	 * Sets PCIe bridges so that they are hidden or not hidden in the IOHC.
	 */
	void		(*zfo_pcie_port_unhide_bridge)(zen_pcie_port_t *);
	void		(*zfo_pcie_port_hide_bridge)(zen_pcie_port_t *);

	/*
	 * Initializes the given PCIe core.
	 */
	void		(*zfo_init_pcie_core)(zen_pcie_core_t *);

	/*
	 * Initializes the bridges attached to the given PCIe port.
	 */
	void		(*zfo_init_bridge)(zen_pcie_port_t *);

	/*
	 * Walks the given IOMS and disables unused PCIe bridges on each IOHC.
	 */
	void		(*zfo_iohc_disable_unused_pcie_bridges)(zen_ioms_t *);

	/*
	 * Initializes SMN register state for the given PCIe port.
	 */
	void		(*zfo_init_pcie_port)(zen_pcie_port_t *);
	void		(*zfo_init_pcie_port_after_reconfig)(zen_pcie_port_t *);

	/*
	 * Microarchitecture specific entries for hotplug initialization and
	 * start.
	 */
	void		(*zfo_pcie_hotplug_port_data_init)(zen_pcie_port_t *,
	    zen_hotplug_table_t *);
	bool		(*zfo_pcie_hotplug_fw_init)(zen_iodie_t *);
	void		(*zfo_pcie_hotplug_core_init)(zen_pcie_core_t *);
	void		(*zfo_pcie_hotplug_port_init)(zen_pcie_port_t *);
	void		(*zfo_pcie_hotplug_port_unblock_training)(
	    zen_pcie_port_t *);
	bool		(*zfo_pcie_hotplug_set_flags)(zen_iodie_t *);
	bool		(*zfo_pcie_hotplug_start)(zen_iodie_t *);

	/*
	 * Initializes PCIe straps on the given PCIe core and its ports.
	 */
	void		(*zfo_init_pcie_straps)(zen_pcie_core_t *);

	/*
	 * Retrieves and reports the version of the firmware for the component
	 * responsible for interfacing with the DXIO crossbar (either the SMU or
	 * MPIO).
	 */
	bool		(*zfo_get_dxio_fw_version)(zen_iodie_t *);
	void		(*zfo_report_dxio_fw_version)(const zen_iodie_t *);

	/*
	 * Maps the OXIO tile ID identifying this tile to the value used by
	 * firmware for traditional hotplug.
	 */
	uint8_t		(*zfo_tile_fw_hp_id)(const oxio_engine_t *);
} zen_fabric_ops_t;

typedef struct zen_hack_ops {
	void	(*zho_check_furtive_reset)(void);
	bool	(*zho_cgpll_set_ssc)(bool);
} zen_hack_ops_t;

/*
 * These null operations are no-ops for operations that are unnecessary on a
 * given microarchitecture.
 */
extern void zen_null_check_furtive_reset(void);
extern bool zen_null_cgpll_set_ssc(bool);
extern void zen_null_fabric_iohc_pci_ids(zen_ioms_t *);
extern void zen_null_fabric_sdp_control(zen_ioms_t *);
extern void zen_null_fabric_nbio_sdp_control(zen_nbio_t *);
extern void zen_null_fabric_nbif_bridges(zen_ioms_t *);

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
	const uint32_t zmsa_reg_base;
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
	 * The specific chip revisions supported by this platform -- used to
	 * guard against, e.g., running on later revisions of a chip which
	 * require different CCX initialization.
	 */
	const x86_chiprev_t		zpc_chiprev;

	/*
	 * The maximum number of hole entries expected in the APOB memory map.
	 */
	const uint32_t			zpc_max_apob_mem_map_holes;

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
	 * These correspond to the MCA(X) per-bank mask registers that we must
	 * initialize when setting up RAS.
	 */
	const zen_ras_init_data_t	*zpc_ras_init_data;

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
	 * The base SDP unit ID for the first PCIe core in each IOMS.
	 */
	const uint8_t			zpc_pcie_core0_unitid;

	/*
	 * These are pointers to tables of PCIe core and port registers which
	 * should be sampled at various points during initialization. This is
	 * only done in DEBUG kernels.
	 */
	const zen_pcie_reg_dbg_t	*zpc_pcie_core_dbg_regs;
	const size_t *const		zpc_pcie_core_dbg_nregs;
	const zen_pcie_reg_dbg_t	*zpc_pcie_port_dbg_regs;
	const size_t *const		zpc_pcie_port_dbg_nregs;

	/*
	 * The microarchitectural maximum number of ports per PCIe core.
	 */
	const uint8_t			zpc_pcie_core_max_ports;

	/*
	 * Internal ports that are modeled as PCIe devices, though not exposed
	 * outside of the package.  These are indexed by IOHC number.
	 */
	const zen_iohc_nbif_ports_t	*zpc_pcie_int_ports;

	/*
	 * This is the maximum PCIe Generation Supported.
	 */
	const oxio_speed_t		zpc_pcie_max_speed;
} zen_platform_consts_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_UARCH_H */
