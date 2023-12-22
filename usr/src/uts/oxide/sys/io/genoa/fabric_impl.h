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
 * Copyright 2023 Oxide Computer Co.
 */

#ifndef _SYS_IO_GENOA_FABRIC_IMPL_H
#define	_SYS_IO_GENOA_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/io/genoa/fabric.h>
#include <sys/io/genoa/ccx_impl.h>
#include <sys/io/genoa/dxio_impl.h>
#include <sys/io/genoa/nbif_impl.h>
#include <sys/io/genoa/pcie_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This defines what the maximum number of SoCs that are supported in Genoa (
 * and Milan and Rome).
 */
#define	GENOA_FABRIC_MAX_SOCS		2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC. Since
 * Rome this has been 1. Previously on Naples this was 4. Because we do not work
 * on Naples based platforms, this is kept low (unlike the more general amdzen
 * nexus driver).
 */
#define	GENOA_FABRIC_MAX_DIES_PER_SOC	1

/*
 * The Instance & Component IDs of the first CCM in the SoC.
 */
#define	GENOA_DF_FIRST_CCM_INST_ID	0x10
#define	GENOA_DF_FIRST_CCM_COMP_ID	0x60

/*
 * This is the number of IO[MS] (IOHUB[MS]) instances that we know are supposed
 * to exist per NBIO.
 */
#define	GENOA_IOMS_PER_NBIO	2

/*
 * This is the number of NBIO instances that we know are supposed to exist per
 * die.
 */
#define	GENOA_NBIO_PER_IODIE	2

/*
 * This is the number of IO[MS] instances that we know are supposed to exist per
 * die.
 */
#define	GENOA_IOMS_PER_IODIE	(GENOA_IOMS_PER_NBIO*GENOA_NBIO_PER_IODIE)

/*
 * The maximum number of PCIe cores in an NBIO IOMS. The IOMS has up to four
 * cores, but only the one with the WAFL link has core number 2.
 */
#define	GENOA_IOMS_MAX_PCIE_CORES	4
#define	GENOA_IOMS_WAFL_PCIE_CORENO	2

/*
 * Per the PPR, the following defines the first Instance & Component IDs for the
 * Genoa IOMs (IOMx_IOHUBMx) & IOS (IOHUBSx) instances.
 */
#define	GENOA_DF_FIRST_IOM_INST_ID	0x20
#define	GENOA_DF_FIRST_IOM_COMP_ID	0x78

#define	GENOA_DF_FIRST_IOS_INST_ID	0x24
#define	GENOA_DF_FIRST_IOS_COMP_ID	0x20

/*
 * This indicates the ID number of the IOS instance that happens to have the
 * FCH present.
 */
#define	GENOA_IOMS_HAS_FCH	3

/*
 * Similarly, the IOMS instance with the WAFL port.
 */
#define	GENOA_IOMS_HAS_WAFL	0

/*
 * There are supposed to be 23 digital power management (DPM) weights provided
 * by each Genoa SMU.  Note that older processor families may have fewer, and
 * Naples also has more SMUs.
 */
#define	GENOA_MAX_DPM_WEIGHTS	23

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

struct genoa_ioms {
	genoa_ioms_flag_t	gio_flags;
	uint16_t		gio_pci_busno;
	uint8_t			gio_num;
	uint8_t			gio_iom_fabric_id;
	uint8_t			gio_iom_comp_id;
	uint8_t			gio_iom_inst_id;
	uint8_t			gio_ios_fabric_id;
	uint8_t			gio_ios_comp_id;
	uint8_t			gio_ios_inst_id;
	uint8_t			gio_npcie_cores;
	uint8_t			gio_nnbifs;
	genoa_pcie_core_t	gio_pcie_cores[GENOA_IOMS_MAX_PCIE_CORES];
	genoa_nbif_t		gio_nbifs[GENOA_IOMS_MAX_NBIF];
	ioms_memlists_t		gio_memlists;
	genoa_iodie_t		*gio_iodie;
};

struct genoa_iodie {
	kmutex_t		gi_df_ficaa_lock;
	kmutex_t		gi_smn_lock;
	kmutex_t		gi_smu_lock;
	uint8_t			gi_node_id;
	uint8_t			gi_dfno;
	uint8_t			gi_smn_busno;
	uint8_t			gi_nioms;
	uint8_t			gi_nccds;
	uint8_t			gi_smu_fw[3];
	uint32_t		gi_dxio_fw[2];
	genoa_iodie_flag_t	gi_flags;
	genoa_dxio_sm_state_t	gi_state;
	genoa_dxio_config_t	gi_dxio_conf;
	uint64_t		gi_dpm_weights[GENOA_MAX_DPM_WEIGHTS];
	genoa_ioms_t		gi_ioms[GENOA_IOMS_PER_IODIE];
	genoa_ccd_t		gi_ccds[GENOA_MAX_CCDS_PER_IODIE];
	genoa_soc_t		*gi_soc;
};

struct genoa_soc {
	uint8_t			gs_socno;
	uint8_t			gs_ndies;
	char			gs_brandstr[CPUID_BRANDSTR_STRLEN + 1];
	genoa_iodie_t		gs_iodies[GENOA_FABRIC_MAX_DIES_PER_SOC];
	genoa_fabric_t		*gs_fabric;
};

struct genoa_fabric {
	uint8_t		gf_nsocs;
	/*
	 * This represents a cache of everything that we've found in the fabric.
	 */
	uint_t		gf_total_ioms;
	/*
	 * These are masks and shifts that describe how to take apart an ID into
	 * its node ID and corresponding component ID.
	 */
	uint8_t		gf_node_shift;
	uint32_t	gf_node_mask;
	uint32_t	gf_comp_mask;
	/*
	 * While TOM and TOM2 are nominally set per-core and per-IOHC, these
	 * values are fabric-wide.
	 */
	uint64_t	gf_tom;
	uint64_t	gf_tom2;
	uint64_t	gf_ecam_base;
	uint64_t	gf_mmio64_base;
	genoa_hotplug_t	gf_hotplug;
	genoa_soc_t	gf_socs[GENOA_FABRIC_MAX_SOCS];
};

extern uint32_t genoa_smn_read(struct genoa_iodie *, const smn_reg_t);
extern void genoa_smn_write(struct genoa_iodie *, const smn_reg_t,
    const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_GENOA_FABRIC_IMPL_H */
