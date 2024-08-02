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

#ifndef	_SYS_IO_ZEN_FABRIC_IMPL_H
#define	_SYS_IO_ZEN_FABRIC_IMPL_H

#include <sys/types.h>
#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/mutex.h>
#include <sys/x86_archext.h>

#include <io/amdzen/amdzen_client.h>
#include <sys/io/zen/fabric.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef int (*zen_iodie_cb_f)(zen_iodie_t *, void *);

extern int zen_fabric_walk_ioms(zen_fabric_t *, zen_ioms_cb_f, void *);
extern int zen_fabric_walk_iodie(zen_fabric_t *, zen_iodie_cb_f, void *);
extern zen_ioms_t *zen_fabric_find_ioms(zen_fabric_t *, uint32_t);
extern zen_ioms_t *zen_fabric_find_ioms_by_bus(zen_fabric_t *, uint32_t);

extern void zen_fabric_dma_attr(ddi_dma_attr_t *attr);

extern void zen_fabric_topo_init_common(void);

/*
 * These are platform maximums, and are sized to accommodate
 * the largest number used by any supported microarchitecture.
 */

/*
 * This is the maximum number of I/O dies that can exist in a given SoC. Since
 * Rome this has been 1. Previously on Naples this was 4. Because we do not work
 * on Naples based platforms, this is kept low (unlike the more general amdzen
 * nexus driver).
 */
#define	ZEN_FABRIC_MAX_DIES_PER_SOC	1


/*
 * The Oxide platform supports a maximum of 2 SOCs.
 */
#define	ZEN_FABRIC_MAX_SOCS		2

/*
 * The exact number of IOMS per IO Die is platform-specific and is determined
 * determined by the platform's `zpc_ioms_per_iodie`.
 */
#define	ZEN_IODIE_MAX_IOMS		8

/*
 * Warning: These memlists cannot be given directly to PCI. They expect to be
 * kmem_alloc'd which we are not doing here at all.
 */
typedef struct zen_ioms_memlists {
	kmutex_t		zim_lock;
	struct memlist_pool	zim_pool;
	struct memlist		*zim_io_avail_pci;
	struct memlist		*zim_io_avail_gen;
	struct memlist		*zim_io_used;
	struct memlist		*zim_mmio_avail_pci;
	struct memlist		*zim_mmio_avail_gen;
	struct memlist		*zim_mmio_used;
	struct memlist		*zim_pmem_avail;
	struct memlist		*zim_pmem_used;
	struct memlist		*zim_bus_avail;
	struct memlist		*zim_bus_used;
} zen_ioms_memlists_t;

/*
 * On Milan, the IOMS is a single component within the data fabric as far as
 * we were concerned (i.e., instances for register access). But with Genoa and
 * later, it has been split into 2 components: the IOM and the IOS. But since
 * it's still a 1:1 mapping, we treat them as a single entity.
 */
struct zen_ioms {
	/*
	 * The 0-based index of this IOMS.
	 */
	uint8_t			zio_num;

	/*
	 * The instance ID of the IOMS and IOS components used for accessing
	 * instance specific registers. On Milan, both values are the same.
	 */
	uint8_t			zio_iom_inst_id;
	uint8_t			zio_ios_inst_id;

	/*
	 * The FabricID of this IOMS used as a destination for address mapping
	 * and packet routing. For Genoa onwards, this is the IOS's FabricID.
	 */
	uint16_t		zio_dest_id;

	/*
	 * The bus number used for accessing per-instance IOMS registers via
	 * PCI config space.
	 */
	uint8_t			zio_pci_busno;

	/*
	 * Per-IOMS flags.
	 */
	zen_ioms_flag_t		zio_flags;

	/*
	 * The IO Die to which this IOMS belongs.
	 */
	zen_iodie_t		*zio_iodie;

	/*
	 * A pointer to the microarchitecturally specific state for this
	 * IOMS.
	 */
	void			*zio_uarch_ioms;
};

struct zen_iodie {
	/*
	 * A lock to serialize FICAA/FICAD indirect DF config register access.
	 */
	kmutex_t		zi_df_ficaa_lock;

	/*
	 * A lock to serialize SMN register operations.
	 */
	kmutex_t		zi_smn_lock;

	/*
	 * A lock to serialize SMU operations.
	 */
	kmutex_t		zi_smu_lock;

	/*
	 * The node ID of this die.
	 */
	uint16_t		zi_node_id;

	/*
	 * The device number of this IO Die used for accessing DF configuration
	 * registers via PCI config space.
	 */
	uint8_t			zi_devno;

	/*
	 * The bus used for accessing SMN registers via PCI config space.
	 */
	uint8_t			zi_smn_busno;

	/*
	 * The actual number of IOMS instances present on this IO Die.
	 */
	uint8_t			zi_nioms;

	/*
	 * Per-die flags.
	 */
	zen_iodie_flag_t	zi_flags;

	/*
	 * The IOMS instances present on this IO Die.
	 */
	zen_ioms_t		zi_ioms[ZEN_IODIE_MAX_IOMS];

	/*
	 * The SOC to which this IO Die belongs.
	 */
	zen_soc_t		*zi_soc;

	/*
	 * A pointer to microarchitecturally specific state for this die.
	 */
	void			*zi_uarch_iodie;
};

/*
 * The per-SOC details.
 */
struct zen_soc {
	/*
	 * The index of the SOC within the fabric.
	 */
	uint8_t			zs_socno;

	/*
	 * The cached brand string fetched from the SMU during early boot.
	 */
	char			zs_brandstr[CPUID_BRANDSTR_STRLEN + 1];

	/*
	 * While earlier generations of EPYC supported more (Naples had 4),
	 * since Rome there is only one IO Die per SOC.  Regardless, keep
	 * this as an array in order to accommodate future architectures
	 * that may expand this again.
	 */
	zen_iodie_t		zs_iodies[ZEN_FABRIC_MAX_DIES_PER_SOC];

	/*
	 * The number of IO dies in the sock; statically initialized to
	 * whatever the microarchitectural constant is (usually 1).
	 */
	uint8_t			zs_niodies;

	/*
	 * The fabric to which this SOC belongs.
	 */
	zen_fabric_t		*zs_fabric;

	/*
	 * A pointer to the microarchitecturally specific data for this SOC.
	 */
	void			*zs_uarch_soc;
};

/*
 * The top-level description of various components contained within the Zen
 * fabric.
 */
struct zen_fabric {
	/*
	 * The number of SOCs present.
	 */
	uint8_t			zf_nsocs;

	/*
	 * The information necessary to (de)compose Fabric/Node/Component IDs.
	 */
	df_fabric_decomp_t	zf_decomp;

	/*
	 * The total number of IOMS present across all the per-SOC, iodies.
	 * We cache this value here for convenience in, e.g., splitting up MMIO
	 * space evenly.
	 */
	uint_t			zf_total_ioms;

	/*
	 * While TOM and TOM2 are nominally set per-core and per-IOHC, these
	 * values are fabric-wide.
	 */

	/*
	 * The cached Core::X86::Msr::TOP_MEM value.
	 */
	uint64_t		zf_tom;
	/*
	 * The cached Core::X86::Msr::TOM2 value.
	 */
	uint64_t		zf_tom2;
	/*
	 * The portion of 64-bit MMIO space used for PCIe ECAM. This gets
	 * located above DRAM (TOM2) while taking into account the IOMMU hole.
	 */
	uint64_t		zf_ecam_base;
	/*
	 * The start of the remainder of the 64-bit MMIO space.
	 */
	uint64_t		zf_mmio64_base;

	/*
	 * The per-SOC details. Always at least one, up to `zf_nsocs`.
	 */
	zen_soc_t		zf_socs[ZEN_FABRIC_MAX_SOCS];

	/*
	 * A pointer to the microarchitecturally specific data for this
	 * fabric.
	 */
	void			*zf_uarch_fabric;
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_IMPL_H */
