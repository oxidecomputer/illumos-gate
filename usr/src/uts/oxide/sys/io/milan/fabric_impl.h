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
 * Copyright 2022 Oxide Computer Co.
 */

#ifndef _SYS_IO_MILAN_FABRIC_IMPL_H
#define	_SYS_IO_MILAN_FABRIC_IMPL_H

/*
 * Private I/O fabric types.  This file should not be included outside the
 * implementation.
 */

#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/types.h>
#include <sys/x86_archext.h>
#include <sys/io/milan/fabric.h>
#include <sys/io/milan/ccx_impl.h>
#include <sys/io/milan/dxio_impl.h>
#include <sys/io/milan/nbif_impl.h>
#include <sys/io/milan/pcie_impl.h>
#include <sys/amdzen/smn.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This defines what the maximum number of SoCs that are supported in Milan (and
 * Rome).
 */
#define	MILAN_FABRIC_MAX_SOCS		2

/*
 * This is the maximum number of I/O dies that can exist in a given SoC. Since
 * Rome this has been 1. Previously on Naples this was 4. Because we do not work
 * on Naples based platforms, this is kept low (unlike the more general amdzen
 * nexus driver).
 */
#define	MILAN_FABRIC_MAX_DIES_PER_SOC	1

#define	MILAN_DF_FIRST_CCM_ID	16

/*
 * This is the number of IOMS instances that we know are supposed to exist per
 * die.
 */
#define	MILAN_IOMS_PER_IODIE	4

/*
 * The maximum number of NBIFs and PCIe ports off of an IOMS. The IOMS has up to
 * three ports (though only one has three with the WAFL link). There are always
 * three primary NBIFs (nbif_impl.h), but only two of the SYSHUB NBIFs in
 * alternate space. Each PCIe PORT has a maximum of 8 bridges for devices
 * (pcie_impl.h).
 */
#define	MILAN_IOMS_MAX_PCIE_PORTS	3
#define	MILAN_IOMS_WAFL_PCIE_PORT	2

/*
 * Per the PPR, the following defines the first enry for the Milan IOMS.
 */
#define	MILAN_DF_FIRST_IOMS_ID	24

/*
 * This indicates the ID number of the IOMS instance that happens to have the
 * FCH present.
 */
#define	MILAN_IOMS_HAS_FCH	3

/*
 * Similarly, the IOMS instance with the WAFL port.
 */
#define	MILAN_IOMS_HAS_WAFL	0

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

struct milan_ioms {
	milan_ioms_flag_t	mio_flags;
	uint16_t		mio_pci_busno;
	uint8_t			mio_num;
	uint8_t			mio_fabric_id;
	uint8_t			mio_comp_id;
	uint8_t			mio_npcie_ports;
	uint8_t			mio_nnbifs;
	milan_pcie_port_t	mio_pcie_ports[MILAN_IOMS_MAX_PCIE_PORTS];
	milan_nbif_t		mio_nbifs[MILAN_IOMS_MAX_NBIF];
	ioms_memlists_t		mio_memlists;
	milan_iodie_t		*mio_iodie;
};

struct milan_iodie {
	kmutex_t		mi_df_ficaa_lock;
	kmutex_t		mi_smn_lock;
	kmutex_t		mi_smu_lock;
	uint8_t			mi_node_id;
	uint8_t			mi_dfno;
	uint8_t			mi_smn_busno;
	uint8_t			mi_nioms;
	uint8_t			mi_nccds;
	uint8_t			mi_smu_fw[3];
	uint32_t		mi_dxio_fw[2];
	milan_iodie_flag_t	mi_flags;
	milan_dxio_sm_state_t	mi_state;
	milan_dxio_config_t	mi_dxio_conf;
	milan_ioms_t		mi_ioms[MILAN_IOMS_PER_IODIE];
	milan_ccd_t		mi_ccds[MILAN_MAX_CCDS_PER_IODIE];
	milan_soc_t		*mi_soc;
};

struct milan_soc {
	uint8_t			ms_socno;
	uint8_t			ms_ndies;
	char			ms_brandstr[CPUID_BRANDSTR_STRLEN + 1];
	milan_iodie_t		ms_iodies[MILAN_FABRIC_MAX_DIES_PER_SOC];
	milan_fabric_t		*ms_fabric;
};

struct milan_fabric {
	uint8_t		mf_nsocs;
	/*
	 * This represents a cache of everything that we've found in the fabric.
	 */
	uint_t		mf_total_ioms;
	/*
	 * These are masks and shifts that describe how to take apart an ID into
	 * its node ID and corresponding component ID.
	 */
	uint8_t		mf_node_shift;
	uint32_t	mf_node_mask;
	uint32_t	mf_comp_mask;
	/*
	 * While TOM and TOM2 are nominally set per-core and per-IOHC, these
	 * values are fabric-wide.
	 */
	uint64_t	mf_tom;
	uint64_t	mf_tom2;
	uint64_t	mf_ecam_base;
	uint64_t	mf_mmio64_base;
	milan_hotplug_t	mf_hotplug;
	milan_soc_t	mf_socs[MILAN_FABRIC_MAX_SOCS];
};

extern uint32_t milan_smn_read(struct milan_iodie *, const smn_reg_t);
extern void milan_smn_write(struct milan_iodie *, const smn_reg_t,
    const uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_MILAN_FABRIC_IMPL_H */
