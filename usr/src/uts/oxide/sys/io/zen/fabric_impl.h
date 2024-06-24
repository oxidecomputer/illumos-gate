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

#ifndef _SYS_IO_ZEN_FABRIC_IMPL_H
#define	_SYS_IO_ZEN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/amdzen/smn.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/pcie_impl.h>
#include <sys/io/zen/nbif_impl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These are platform maximums, and are sized to accommodate
 * the largest number used by any supported microarchitecture.
 */
#define	ZEN_FABRIC_MAX_SOCS		2
#define	ZEN_FABRIC_MAX_IODIES_PER_SOC	1
#define	ZEN_IOMS_MAX_PER_IODIE		4
#define	ZEN_IOMS_MAX_PCIE_CORES		3
#define	ZEN_DPM_MAX_WEIGHTS		23

/*
 * This defines the limits for individual microarchitectures.
 */
typedef struct zen_fabric_impl {
	/* The maximum number of SoCs supported. */
	const uint_t zfi_max_socs;

	/* The maximum number of I/O dies in a given SoC. */
	const uint_t zfi_max_dies_per_soc;

	/* The ID of the first CCM. */
	const uint_t zfi_df_first_ccm_id;

	/* The number of IOMS instances expected per IO die. */
	const uint_t zfi_ioms_per_iodie;

	/* The maximum number of PCIe cores in an NBIO IOMS. */
	const uint_t zfi_ioms_max_pcie_cores;

	/* The PCIe core number of the core with the WAFL port. */
	const uint_t zfi_ioms_walf_pcie_coreno;

	/* The ID of first IOMS entry. */
	const uint_t zfi_df_first_ioms_id;

	/* The ID of the IOMS instance with the FCH. */
	const uint_t zfi_ioms_has_fch;

	/* The ID of the IOMS instance with the WAFL port. */
	const uint_t zfi_ioms_has_wafl;

	/*
	 * The maximum number of DPM (digial power management) weights
	 * provided by each SMU.
	 */
	const uint_t zfi_max_dpm_weights;
} zen_fabric_impl_t;

extern zen_fabric_impl_t *zen_fabric_impl;

/*
 * Warning: These memlists cannot be given directly to PCI. They expect to be
 * kmem_alloc'd which we are not doing here at all.
 */
typedef struct ioms_memlists {
	kmutex_t		im_lock;
	struct memlist_pool	im_pool;
	struct memlist		*im_io_avail_pci;
	struct memlist		*im_io_avail_gen;
	struct memlist		*im_io_used;
	struct memlist		*im_mmio_avail_pci;
	struct memlist		*im_mmio_avail_gen;
	struct memlist		*im_mmio_used;
	struct memlist		*im_pmem_avail;
	struct memlist		*im_pmem_used;
	struct memlist		*im_bus_avail;
	struct memlist		*im_bus_used;
} ioms_memlists_t;

struct zen_ioms {
	zen_ioms_flag_t	zio_flags;
	uint16_t	zio_pci_busno;
	uint8_t		zio_num;
	uint8_t		zio_fabric_id;
	uint8_t		zio_comp_id;
	uint8_t		zio_npcie_cores;
	uint8_t		zio_nnbifs;
	zen_pcie_core_t	zio_pcie_cores[ZEN_IOMS_MAX_PCIE_CORES];
	zen_nbif_t	zio_nbifs[ZEN_IOMS_MAX_NBIF];
	ioms_memlists_t	zio_memlists;
	zen_iodie_t	*zio_iodie;
};

struct zen_iodie {
	kmutex_t		zi_df_ficaa_lock;
	kmutex_t		zi_smn_lock;
	uint8_t			zi_node_id;
	uint8_t			zi_dfno;
	uint8_t			zi_smn_busno;
	uint8_t			zi_dpm_nweights;
	uint8_t			zi_nioms;
	uint8_t			zi_nccds;
	zen_iodie_flag_t	zi_flags;
	void			*zi_iofw_impl;
	uint64_t		zi_dpm_weights[ZEN_DPM_MAX_WEIGHTS];
	zen_ioms_t		zi_ioms[ZEN_IOMS_MAX_PER_IODIE];
	zen_ccd_t		zi_ccds[ZEN_MAX_CCDS_PER_IODIE];
	zen_soc_t		*zi_soc;
};

struct zen_soc {
	uint8_t			zs_socno;
	uint8_t			zs_niodies;
	char			zs_brandstr[CPUID_BRANDSTR_STRLEN + 1];
	zen_iodie_t		zs_iodies[ZEN_FABRIC_MAX_IODIES_PER_SOC];
	zen_fabric_t		*zs_fabric;
};

struct zen_fabric {
	uint8_t		zf_nsocs;
	/*
	 * This represents a cache of everything that we've found in the fabric.
	 */
	uint_t		zf_total_ioms;
	/*
	 * These are masks and shifts that describe how to take apart an ID into
	 * its node ID and corresponding component ID.
	 */
	uint8_t		zf_node_shift;
	uint32_t	zf_node_mask;
	uint32_t	zf_comp_mask;
	/*
	 * While TOM and TOM2 are nominally set per-core and per-IOHC, these
	 * values are fabric-wide.
	 */
	uint64_t	zf_tom;
	uint64_t	zf_tom2;
	uint64_t	zf_ecam_base;
	uint64_t	zf_mmio64_base;
	zen_hotplug_t	*zf_hotplug;
	zen_soc_t	zf_socs[ZEN_FABRIC_MAX_SOCS];
};

/*
 * This structure is used to find the map of dxio lanes to a given PCIe core on
 * an IOMS. Note that all lanes here are inclusive. e.g. [start, end].
 */
typedef struct zen_pcie_core_info {
	const char	*zpci_name;
	uint16_t	zpci_dxio_start;
	uint16_t	zpci_dxio_end;
	uint16_t	zpci_phy_start;
	uint16_t	zpci_phy_end;
} zen_pcie_core_info_t;

typedef struct zen_pcie_port_info {
	uint8_t	zppi_dev;
	uint8_t	zppi_func;
} zen_pcie_port_info_t;

/*
 * The following table encodes the per-bridge IOAPIC initialization routing. We
 * currently following the recommendation of the PPR.
 */
typedef struct zen_ioapic_info {
	uint8_t zii_group;
	uint8_t zii_swiz;
	uint8_t zii_map;
} zen_ioapic_info_t;

/* XXX Track platform default presence */
typedef struct zen_nbif_info {
	zen_nbif_func_type_t	zni_type;
	uint8_t			zni_dev;
	uint8_t			zni_func;
} zen_nbif_info_t;

extern uint32_t zen_smn_read(struct zen_iodie *, const smn_reg_t);
extern void zen_smn_write(struct zen_iodie *, const smn_reg_t,
    const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_IMPL_H */
