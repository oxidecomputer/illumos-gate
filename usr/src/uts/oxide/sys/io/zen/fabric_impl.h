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

/*
 * Type definitions, structs, function prototypes, and constants common across
 * Zen microarchitectures and used in the implementation of data and IO fabric
 * initialization.
 */

#include <sys/types.h>
#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/mutex.h>
#include <sys/x86_archext.h>
#include <io/amdzen/amdzen_client.h>

#include <sys/io/zen/fabric.h>
#include <sys/io/zen/ccx_impl.h>
#include <sys/io/zen/dxio_impl.h>
#include <sys/io/zen/mpio_impl.h>
#include <sys/io/zen/nbif_impl.h>
#include <sys/io/zen/pcie_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef int (*zen_iodie_cb_f)(zen_iodie_t *, void *);

extern int zen_fabric_walk_ioms(zen_fabric_t *, zen_ioms_cb_f, void *);
extern int zen_fabric_walk_iodie(zen_fabric_t *, zen_iodie_cb_f, void *);
extern int zen_fabric_walk_pcie_core(zen_fabric_t *, zen_pcie_core_cb_f,
    void *);
extern int zen_fabric_walk_pcie_port(zen_fabric_t *, zen_pcie_port_cb_f,
    void *);
extern int zen_fabric_walk_nbif(zen_fabric_t *, zen_nbif_cb_f, void *);

extern zen_ioms_t *zen_fabric_find_ioms(zen_fabric_t *, uint32_t);
extern zen_ioms_t *zen_fabric_find_ioms_by_bus(zen_fabric_t *, uint32_t);

extern void zen_fabric_dma_attr(ddi_dma_attr_t *attr);

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
 * The exact number of IOM/S per I/O Die is platform-specific and is determined
 * determined dynamically during fabric topology initialization. This is the
 * maximum supported on the Oxide platform.
 */
#define	ZEN_IODIE_MAX_IOMS		8

/*
 * The maximum number of PCIe cores per IOMS.
 */
#define	ZEN_IOMS_MAX_PCIE_CORES		3

/*
 * The maximum number of NBIFs (Northbridge Interfaces, though possible
 * Northbridge interconnect functions; definitions vary) per IOMS.
 */
#define	ZEN_IOMS_MAX_NBIF		3

/*
 * The maximum number of PCI bus, I/O and MMIO routing rules supported on the
 * Oxide platform. Of the supported processors, Turin supports the most rules.
 */
#define	ZEN_MAX_CFGMAP			DF_MAX_CFGMAP_TURIN
#define	ZEN_MAX_IO_RULES		DF_MAX_IO_RULES_TURIN
#define	ZEN_MAX_MMIO_RULES		DF_MAX_MMIO_RULES_TURIN

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
	zen_ioms_flag_t		zio_flags;

	/*
	 * The bus number used for accessing per-instance IOMS registers via
	 * PCI config space.
	 */
	uint16_t		zio_pci_busno;

	/*
	 * The FabricID of this IOMS used as a destination for address mapping
	 * and packet routing. For Genoa onwards, this is the IOS's FabricID.
	 */
	uint16_t		zio_dest_id;

	/*
	 * The index of this IOMS relative to its associated die.  Used when
	 * accessing SMN registers, straps, etc.
	 */
	uint8_t			zio_num;

	/*
	 * The NBIO number that contains this IOMS. Used when accessing SMN
	 * registers for NBIO components such as the nBIFs.
	 */
	uint8_t			zio_nbionum;

	/*
	 * The index of the IOHC associated with this IOMS. Used when accessing
	 * IOHC SMN registers.
	 */
	uint8_t			zio_iohcnum;

	/*
	 * The type of IOHC associated with this IOMS.
	 */
	zen_iohc_type_t		zio_iohctype;

	/*
	 * The instance ID of the IOMS and IOS components used for accessing
	 * instance specific registers. On Milan, both values are the same.
	 */
	uint8_t			zio_iom_inst_id;
	uint8_t			zio_ios_inst_id;

	uint8_t			zio_npcie_cores;
	zen_pcie_core_t		zio_pcie_cores[ZEN_IOMS_MAX_PCIE_CORES];

	uint8_t			zio_nnbifs;
	zen_nbif_t		zio_nbifs[ZEN_IOMS_MAX_NBIF];

	zen_ioms_memlists_t	zio_memlists;

	zen_iodie_t		*zio_iodie;
	void			*zio_uarch_ioms;
};

struct zen_iodie {
	/*
	 * The index of this die relative to its associated soc.
	 */
	uint8_t			zi_num;

	/*
	 * The DF version as implemented by this I/O die. In contrast to
	 * zen_platform_consts_t.zpc_df_rev, we determine this dynamically.
	 */
	df_rev_t		zi_df_rev;

	/*
	 * The major and minor version of the DF as implemented by this I/O die.
	 */
	uint8_t			zi_df_major;
	uint8_t			zi_df_minor;

	kmutex_t		zi_df_ficaa_lock;
	kmutex_t		zi_smn_lock;
	kmutex_t		zi_smu_lock;
	kmutex_t		zi_mpio_lock;

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

	zen_iodie_flag_t	zi_flags;

	uint8_t			zi_nents;

	/*
	 * The number of CCMs present on this I/O die and the base (lowest)
	 * Instance ID may vary between microarchitectures / products so we
	 * cache those values here once discovered during fabric topology
	 * initialization.
	 */
	uint8_t			zi_nccms;
	uint8_t			zi_base_ccm_id;

	/*
	 * Like the CCMs, the number and base Instance ID of the IOM/IOS
	 * components also varies and is similarly cached here.
	 */
	uint8_t			zi_base_iom_id;
	uint8_t			zi_base_ios_id;
	uint8_t			zi_nioms;
	zen_ioms_t		zi_ioms[ZEN_IODIE_MAX_IOMS];

	uint8_t			zi_nccds;
	zen_ccd_t		zi_ccds[ZEN_MAX_CCDS_PER_IODIE];

	/*
	 * The version of the SMU firmware.
	 */
	uint32_t		zi_smu_fw[3];
	/*
	 * The version of the firmware of the component responsible for
	 * interfacing with the DXIO crossbar.  That is, the version reported by
	 * either MPIO (on Genoa or later) or the SMU-DXIO interface.
	 * zi_ndxio_fw denotes the number of elements actually used for the
	 * firmware version.
	 */
	uint8_t			zi_ndxio_fw;
	uint32_t		zi_dxio_fw[4];

	/*
	 * If we're using MPIO, then the configuration lives here.
	 * XXX(cross): this feels kind of hacky. @rm please comment.
	 * Alternatives may be to make this be a `void *` pointer,
	 * and have it set up to point to either a DXIO config for
	 * Milan, or MPIO for Genoa/Turin, with the actual config
	 * living in the uarch-data (as is done presently for the
	 * `milan_dxio_config_t` on Milan), or have a dedicated uarch-specific
	 * accessor for the config.
	 */
	union {
		zen_mpio_config_t	zi_mpio_conf;
		zen_dxio_config_t	zi_dxio_conf;
	};

	/*
	 * The cached brand string fetched from the SMU during early boot.
	 */
	char			zi_brandstr[CPUID_BRANDSTR_STRLEN + 1];

	zen_soc_t		*zi_soc;
	void			*zi_uarch_iodie;
};

/*
 * The per-SOC details.
 */
struct zen_soc {
	/*
	 * The index of the SOC within the fabric.
	 */
	uint8_t			zs_num;

	/*
	 * While earlier generations of EPYC supported more (Naples had 4),
	 * since Rome there is only one IO Die per SOC.  Regardless, keep
	 * this as an array in order to accommodate future architectures
	 * that may expand this again.
	 */
	uint8_t			zs_niodies;
	zen_iodie_t		zs_iodies[ZEN_FABRIC_MAX_DIES_PER_SOC];

	zen_fabric_t		*zs_fabric;
	void			*zs_uarch_soc;
};

/*
 * The top-level description of various components contained within the Zen
 * fabric.
 */
struct zen_fabric {
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
	 * Unlike the compatibility (32-bit) MMIO space which is fixed at 4 GiB,
	 * the end of the 64-bit MMIO space depends on the physical address
	 * space size.
	 */
	uint64_t		zf_mmio64_size;

	uint8_t			zf_nsocs;
	zen_soc_t		zf_socs[ZEN_FABRIC_MAX_SOCS];

	void			*zf_uarch_fabric;
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IO_ZEN_FABRIC_IMPL_H */
