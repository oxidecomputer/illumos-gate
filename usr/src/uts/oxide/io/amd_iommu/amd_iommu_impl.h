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
 * Copyright 2026 Oxide Computer Company
 */

#ifndef _SYS_AMD_IOMMU_IMPL_H
#define	_SYS_AMD_IOMMU_IMPL_H

/*
 * Defines internal types, function prototypes, and so on, for working
 * with the AMD IOMMU.  See AMD document 48882 for details.
 *
 * Presently, the Oxide architecture only makes use of the Interrupt Remapping
 * functionality of the IOMMU, so only that part is implemented.
 *
 * A word on notation: AMD defines these structures in terms of their absolute
 * bit ranges, and annotates individual members using an inclusive range of bit
 * positions, most-significant bit first (as in "msb:lsb"; for example, 15:0
 * describes a 16-bit wide field occupying the low 16-bits of a structure).
 * Our definitions define these types as fixed-sized types bearing bit-fields,
 * or discrete fixed-sized types where applicable.  Hopefully striking a balance
 * between ease of reading and reference, we note the start of each group of 32
 * or 64 bits in a pseudo-C notation, and if we describe a bit range, we use
 * AMD's (inclusive, msb-first) notation.
 */

#include <sys/stddef.h>
#include <sys/stdint.h>
#include <sys/types.h>

/*
 * The Interrupt Remapping Table Entry
 *
 * This 128-bit aligned, 16-byte structure defines a single entry in the
 * IOMMU's Interrupt Remapping Table.
 *
 * There are two versions of this defined by AMD: one is for use with a guest
 * virtual APIC in "Guest Mode"; the other is for the guest vAPIC in "non-guest
 * mode."  Though both are tied to virtual APICs and SVM, when the local APIC
 * is in x2APIC mode, the table can be used to expand the size of the APIC ID
 * space to direct interrupts to high-numbered CPUs, without virtualization.
 * Since this second sense is the way that we currently use the IOMMU, we only
 * provide that definition.
 *
 * Note that this struct makes several assumptions about both the endianness of
 * the platform, and alignment.  Given that this code is intimately tied to the
 * platform, however, we will not address these further.
 */
typedef struct amd_iommu_irte {
	/* uint32_t irte[0] - bits 31:0 */
	uint32_t aiie_remap_en:1;	/* Remap enable */
	uint32_t aiie_sup_io_pf:1;	/* Suppress IO page fault */
	uint32_t aiie_int_type:3;	/* Interrupt type  */
	uint32_t aiie_rq_eoi:1;		/* Request EOI */
	uint32_t aiie_dm:1;		/* Destination Mode  */
	uint32_t aiie_guest_mode:1;	/* Guest mode (must be zero) */
	uint32_t aiie_dest_lo:24;	/* Destination x2APIC ID bits 23:0 */
	/* uint32_t irte[1] - bits 63:32 */
	uint32_t aiie_resv32;		/* (Reserved) */
	/* uint32_t irte[2] - bits 95:64 */
	uint32_t aiie_vector:8;		/* Interrupt vector */
	uint32_t aiie_resv72:24;	/* (Reserved) */
	/* uint32_t irte[3] - bits 127:96 */
	uint32_t aiie_resv96:24;	/* (Reserved) */
	uint32_t aiie_dest_hi:8;	/* Destination x2APIC ID bits 31:24 */
} amd_iommu_irte_t;
CTASSERT(sizeof (amd_iommu_irte_t) == 16);

/*
 * Device Table Entry
 */
typedef struct amd_iommu_dte {
	/* uint64_t dte[0] - bits 63:0 */
	uint64_t aide_v:1;		/* Valid */
	uint64_t aide_tv:1;		/* Translation valid  */
	uint64_t aide_resv2:2;		/* (Reserved) */
	uint64_t aide_cxl_mem_attr:3;	/* CXL Memory Attributes */
	uint64_t aide_had:2;		/* Update host Access/Dirty PTE bits */
	uint64_t aide_paging_mode:3;	/* Paging mode */
	uint64_t aide_page_tbl_root:40;	/* Page table root address */
	uint64_t aide_ppr_en:1;		/* PPR enable */
	uint64_t aide_gprp_en:1;	/* Guest PPR Response; PASID enabled */
	uint64_t aide_giov:1;		/* Guest IO protection valid */
	uint64_t aide_gv:1;		/* Guest translation valid */
	uint64_t aide_glx:2;		/* Guest levels translated */
	uint64_t aide_gcr3_trp_lo:3;	/* Guest CR3 bits 14:12 */
	uint64_t aide_ir:1;		/* Non-page table defined read perm */
	uint64_t aide_iw:1;		/* Non-page table defined write perm */
	uint64_t aide_resv63:1;		/* (Reserved) */
	/* uint64_t dte[1] - bits 127:64 */
	uint16_t aide_domain_id;	/* DomainID (cache tag) */
	uint16_t aide_gcr3_trp_mid;	/* Guest CR3 bits 30:15 */
	uint32_t aide_i:1;		/* IOTLB enable  */
	uint32_t aide_se:1;		/* Suppress IO page fault events */
	uint32_t aide_sa:1;		/* Suppress ALL IO page fault events */
	uint32_t aide_io_ctl:2;		/* Port IO control */
	uint32_t aide_cache:1;		/* IOTLB cache hint */
	uint32_t aide_sd:1;		/* Page table walk snoop disable */
	uint32_t aide_ex:1;		/* Allow IOMMU exclusion range access */
	uint32_t aide_sysmgt:2;		/* System management message enable */
	uint32_t aide_sats:1;		/* Secure ATS */
	uint32_t aide_gcr3_trp_hi:21;	/* Guest CR3 bits 51:31 */
	/* uint64_t dte[2] - bits 191:128 */
	uint64_t aide_iv:1;		/* Interrupt map valid */
	uint64_t aide_int_tab_len:4;	/* Interrupt table length (2^val) */
	uint64_t aide_ig:1;		/* Ignore unmapped interrupts */
	uint64_t aide_intr_tbl_root:46;	/* Interrupt table root pointer */
	uint64_t aide_resv180:2;	/* (Reserved) */
	uint64_t aide_gpaging_mode:2;	/* Guest paging mode */
	uint64_t aide_init_pass:1;	/* Pass INIT intr messages unmapped */
	uint64_t aide_eint_pass:1;	/* Pass ExtInt intr messages unmapped */
	uint64_t aide_nmi_pass:1;	/* Pass NMI intr messages unmapped */
	uint64_t aide_hpt_mode:1;	/* Host page table mode hint */
	uint64_t aide_int_ctl:2;	/* Interrupt control */
	uint64_t aide_lint0_pass:1;	/* LINT0 interrupts passed unmapped */
	uint64_t aide_lint1_pass:1;	/* LINT1 interrupts passed unmapped */
	/* uint64_t dte[3] - bits 255:192 */
	uint16_t aide_resv192:15;	/* (Reserved) */
	uint16_t aide_vimu_en:1;	/* Virtualize IOMMU enabled */
	uint16_t aide_gdevice_id;	/* Guest DeviceID */
	uint16_t aide_guest_id;		/* Guest ID */
	uint8_t aide_resv240:5;		/* (Reserved) */
	uint8_t aide_resv245_mbz:1;	/* (Reserved; explicitly must be 0) */
	uint8_t aide_attr_v:1;		/* Attribute override valid */
	uint8_t aide_mode0_fc:1;	/* Value of PTE.FC when DTE.Mode = 0 */
	uint8_t aide_snoop_attr:8;	/* Snoop attributes */
} amd_iommu_dte_t;

CTASSERT(sizeof (amd_iommu_dte_t) == 32);

/*
 * According to AMD pub 48882, "the presence of an IOMMU capability block in a
 * PCIe Function indicates the presence of an IOMMU."
 */
static const uint8_t AMD_IOMMU_BASE_PCI_CAP_ID = 0x0F;
static const uint32_t AMD_IOMMU_BASE_PCI_CAP_BLOCK_TYPE = 0x3;
static const uint32_t AMD_IOMMU_BASE_PCI_CAP_REV = 0x1;

typedef struct amd_iommu_pci_cap_header {
	uint32_t aipch_cap_id:8;
	uint32_t aipch_cap_ptr:8;
	uint32_t aipch_cap_type:3;
	uint32_t aipch_cap_rev:5;
	uint32_t aipch_iotbl_supt:1;
	uint32_t aipch_ht_tunnel_sup:1;
	uint32_t aipch_np_ents_cached:1;
	uint32_t aipch_ext_feat_reg_sup:1;
	uint32_t aipch_cap_ext:1;
	uint32_t aipch_resv:3;
} amd_iommu_pci_cap_header_t;
CTASSERT(sizeof (amd_iommu_pci_cap_header_t) == 4);

/*
 * The basic structure of the IOMMU PCI capability in config space.
 *
 * The first 32-bit "DWORD" in the capability is the header; subsequent dwords
 * are defined in the IOMMU reference, AMD publication 48882 vers 3.11 sec 3.2).
 * Each dword is in the capability structure is preceded by a comment giving its
 * hex offset and description, in AMD's notation, for cross-reference.
 */
typedef struct amd_iommu_pci_cap {
	/* Capability Offset 00h IOMMU Capability Header */
	amd_iommu_pci_cap_header_t aipc_cap;
	/* Capability Offset 04h IOMMU Base Address Low Register */
	uint32_t aipc_en:1;
	uint32_t aipc_base_addr_resv:13;
	uint32_t aipc_base_addr_mbz:5;
	uint32_t aipc_base_addr_lo:13;
	/* Capability Offset 08h IOMMU Base Address High Register */
	uint32_t aipc_base_addr_hi;
	/* Capability Offset 0Ch IOMMU Range Register */
	uint8_t aipc_range_unit_id:5;
	uint8_t aipc_range_resv:2;
	uint8_t aipc_range_valid:1;
	uint8_t aipc_range_bus;
	uint8_t aipc_range_first_dev;
	uint8_t aipc_range_last_dev;
	/* Capability Offset 10h IOMMU Miscellaneous Information Register 0 */
	uint32_t aipc_misc0_msi_num:5;
	uint32_t aipc_misc0_gva_size:3;
	uint32_t aipc_misc0_pa_size:7;
	uint32_t aipc_misc0_va_size:7;
	uint32_t aipc_misc0_ht_ats_range_rsvd:1;
	uint32_t aipc_misc0_resv:4;
	uint32_t aipc_misc0_ppr_msi_num:5;
	/* Capability Offset 14h IOMMU Miscellaneous Information Register 1 */
	uint32_t aipc_misc1_msi_num_ga:5;
	uint32_t aipc_misc1_resv:27;
} amd_iommu_pci_cap_t;

/*
 * The vendor capability registers (48882 vers 3.11 sec 3.3) provide information
 * for using SR-IOV style virtual functions with the IOMMU.  We have no need for
 * this currently, and do not support it.
 */

/*
 * MMIO Offset 0018h IOMMU Control Register
 *
 * The IOMMU's control registers are mapped into the system physical address
 * space using the high and low base address registers in the capability.  This
 * structure describes those registers; refer to 48882 vers 3.11 sec 3.4 for
 * details.
 *
 * Unlike PCI-specific structures, AMD defines these data in terms of 64-bit
 * quadwords.
 *
 * In the Oxide architecture, we access this via two 32-bit accesses via SMN
 * space; in i86pc, we use MMIO into a region set up for us by system firmware.
 * Ordinarily, Oxide SMN registers are manipulated by a set of macros that
 * define bitwise operations on an integer type, but to promote commonality
 * between architectures, we use these structs, instead.
 */
typedef struct amd_iommu_mmio_reg_ctl {
	uint64_t aimr_ctl_en:1;
	uint64_t aimr_ctl_ht_tun_en:1;
	uint64_t aimr_ctl_evt_log_en:1;
	uint64_t aimr_ctl_evt_int_en:1;
	uint64_t aimr_ctl_com_wait_int_en:1;
	uint64_t aimr_ctl_inv_timeout:3;

	uint64_t aimr_ctl_pass_pw:1;
	uint64_t aimr_ctl_res_pass_pw:1;
	uint64_t aimr_ctl_coherent:1;
	uint64_t aimr_ctl_isoc:1;
	uint64_t aimr_ctl_cmd_buf_en:1;
	uint64_t aimr_ctl_ppr_log_en:1;
	uint64_t aimr_ctl_ppr_int_en:1;
	uint64_t aimr_ctl_ppr_en:1;

	uint64_t aimr_ctl_gt_en:1;
	uint64_t aimr_ctl_ga_en:1;
	uint64_t aimr_ctl_crw:4;
	uint64_t aimr_ctl_smi_filter_en:1;
	uint64_t aimr_ctl_self_wb_dis:1;

	uint64_t aimr_ctl_smi_filter_log_en:1;
	uint64_t aimr_ctl_gvapic_mode_en:3;
	uint64_t aimr_ctl_gvapic_ga_log_en:1;
	uint64_t aimr_ctl_vgapic_ga_int_en:1;
	uint64_t aimr_ctl_dual_ppr_log_en:2;

	uint64_t aimr_ctl_dual_evt_log_en:2;
	uint64_t aimr_ctl_devtbl_seg_en:3;
	uint64_t aimr_ctl_priv_abort_en:2;
	uint64_t aimr_ctl_ppr_auto_resp_en:1;

	uint64_t aimr_ctl_marc_en:1;
	uint64_t aimr_ctl_block_stopmarc_en:1;
	uint64_t aimr_ctl_ppr_auto_resp_always_on:1;
	uint64_t aimr_ctl_num_int_remap_mode:2;
	uint64_t aimr_ctl_eperiph_page_req_handling_en:1;
	uint64_t aimr_ctl_update_host_pte_ad_bits_en:2;

	uint64_t aimr_ctl_update_guest_pte_d_bit_dis:1;
	uint64_t aimr_ctl_resv:1;
	uint64_t aimr_ctl_x2apic_en:1;
	uint64_t aimr_ctl_x2apic_int_en:1;
	uint64_t aimr_ctl_vcmdbuf_en:1;
	uint64_t aimr_ctl_viommu_en:1;
	uint64_t aimr_ctl_update_guest_pte_a_bit_dis:1;
	uint64_t aimr_ctl_gapic_cpu_physint_en:1;

	uint64_t aimr_ctl_tiered_mem_page_migrate_sup_en:1;
	uint64_t aimr_ctl_cxl_mem_attr_en:1;
	uint64_t aimr_ctl_gcr3_tbl_root_ptr_mode:1;
	uint64_t aimr_ctl_irte_cache_dis:1;
	uint64_t aimr_ctl_guest_buffer_trp_mode:1;
	uint64_t aimr_ctl_snp_vapic_en:3;
} amd_iommu_mmio_reg_ctl_t;
CTASSERT(sizeof (amd_iommu_mmio_reg_ctl_t) == sizeof (uint64_t));

/*
 * MMIO Offset 0030h IOMMU Extended Feature Register
 */
typedef struct amd_iommu_mmio_reg_extfeat {
	uint64_t aimr_extfeat_prefetch_sup:1;
	uint64_t aimr_extfeat_ppr_sup:1;
	uint64_t aimr_extfeat_x2apic_sup:1;
	uint64_t aimr_extfeat_nx_sup:1;
	uint64_t aimr_extfeat_gt_sup:1;
	uint64_t aimr_extfeat_gapic_cpu_physint_sup:1;
	uint64_t aimr_extfeat_inv_all_sup:1;
	uint64_t aimr_extfeat_gvapic_sup:1;

	uint64_t aimr_extfeat_hw_err_reg_sup:1;
	uint64_t aimr_extfeat_perf_cntr_sup:1;
	uint64_t aimr_extfeat_host_addr_trans_levels:2;
	uint64_t aimr_extfeat_guest_addr_trans_levels:2;
	uint64_t aimr_extfeat_guest_cr3_root_tbl_level_sup:2;

	uint64_t aimr_extfeat_smi_filter_sup:2;
	uint64_t aimr_extfeat_smi_filter_nregs:3;
	uint64_t aimr_extfeat_gvapic_mode_sup:3;

	uint64_t aimr_extfeat_dual_ppr_log_sup:2;
	uint64_t aimr_extfeat_resv26:2;
	uint64_t aimr_extfeat_dual_evt_log_sup:2;
	uint64_t aimr_extfeat_resv30:1;
	uint64_t aimr_extfeat_secure_ats_sup:1;

	uint64_t aimr_extfeat_max_pasid_sup:5;
	uint64_t aimr_extfeat_us_page_prot_sup:1;
	uint64_t aimr_extfeat_dev_tbl_seg_sup:2;

	uint64_t aimr_extfeat_ppr_ovrflow_early_sup:1;
	uint64_t aimr_extfeat_ppr_auto_rsp_sup:1;
	uint64_t aimr_extfeat_marc_sup:2;
	uint64_t aimr_extfeat_block_stopmarc_sup:1;
	uint64_t aimr_extfeat_perf_opt_sup:1;
	uint64_t aimr_extfeat_msi_cap_mmio_access_sup:1;
	uint64_t aimr_extfeat_snoop_attr_sup:1;

	uint64_t aimr_extfeat_guest_io_prot_sup:1;
	uint64_t aimr_extfeat_host_access_sup:1;
	uint64_t aimr_extfeat_ppr_ehandling_sup:1;
	uint64_t aimr_extfeat_attr_fwd_sup:1;
	uint64_t aimr_extfeat_host_pte_d_bit_sup:1;
	uint64_t aimr_extfeat_v2_pte_d_bit_dis_hw_update_sup:1;
	uint64_t aimr_extfeat_inv_iotlb_type_sup:1;
	uint64_t aimr_extfeat_viommu_sup:1;

	uint64_t aimr_extfeat_hw_sev_tio_sup:1;
	uint64_t aimr_extfeat_resv57:4;
	uint64_t aimr_extfeat_v2_pte_a_bit_dis_hw_update_sup:1;
	uint64_t aimr_extfeat_force_remapped_int_phys_dest_sup:1;
	uint64_t aimr_extfeat_secure_nested_paging_sup:1;
} amd_iommu_mmio_reg_extfeat_t;
CTASSERT(sizeof (amd_iommu_mmio_reg_extfeat_t) == sizeof (uint64_t));

/*
 * MMIO Offset 0000h Device Table Base Address Register
 */
typedef struct amd_iommu_mmio_reg_devtbl_base {
	uint64_t aimr_devtbl_base_size:9;
	uint64_t aimr_devtbl_base_resv9:3;
	uint64_t aimr_devtbl_base_addr:40;
	uint64_t aimr_devtbl_base_resv52:12;
} amd_iommu_mmio_reg_devtbl_base_t;

/*
 * MMIO Offset 0008h Command Buffer Base Address Register
 */
typedef struct amd_iommu_mmio_reg_cmdbuf_base {
	uint64_t aimr_cmdbuf_base_resv0:12;
	uint64_t aimr_cmdbuf_base_addr:40;
	uint64_t aimr_cmdbuf_base_resv52:4;
	uint64_t aimr_cmdbuf_base_len:4;
	uint64_t aimr_cmdbuf_base_resv60:4;
} amd_iommu_mmio_reg_cmdbuf_base_t;

/*
 * MMIO Offset 0010h Event Log Base Address Register
 */
typedef struct amd_iommu_mmio_reg_evtlog_base {
	uint64_t aimr_evtlog_base_resv0:12;
	uint64_t aimr_evtlog_base_addr:40;
	uint64_t aimr_evtlog_base_resv52:4;
	uint64_t aimr_evtlog_base_len:4;
	uint64_t aimr_evtlog_base_resv60:4;
} amd_iommu_mmio_reg_evtlog_base_t;

/*
 * MMIO Offset 0020h IOMMU Exclusion Base / Completion Store Register
 */
typedef struct amd_iommu_mmio_reg_excl_base {
	uint64_t aimr_excbase_excl_range_en:1;
	uint64_t aimr_excbase_allow_all_fwd_untrans:1;
	uint64_t aimr_excbase_resv2:10;
	uint64_t aimr_excbase_base_addr:40;
	uint64_t aimr_excbase_resv52:12;
} amd_iommu_mmio_reg_excl_base_t;

/*
 * MMIO Offset 0028h IOMMU Exclusion Range / Completion Store Limit
 */
typedef struct amd_iommu_mmio_reg_excl_lim {
	uint64_t aimr_exclim_resv0:12;
	uint64_t aimr_exclim_limit:40;
	uint64_t aimr_exclim_resv52:12;
} amd_iommu_mmio_reg_excl_lim_t;

/*
 * MMIO Offset 2020h IOMMU Status Register
 */
typedef struct amd_iommu_mmio_reg_status {
	/* Bits 0..=7 */
	uint64_t aimr_status_evt_overflow:1;
	uint64_t aimr_status_evt_log_int:1;
	uint64_t aimr_status_cmd_wait_int:1;
	uint64_t aimr_status_evt_log_run:1;
	uint64_t aimr_status_cmd_buf_run:1;
	uint64_t aimr_status_ppr_overflow:1;
	uint64_t aimr_status_ppr_int:1;
	uint64_t aimr_status_ppr_log_run:1;
	/* Bits 8..=15 */
	uint64_t aimr_status_ga_log_run:1;
	uint64_t aimr_status_ga_log_overflow:1;
	uint64_t aimr_status_ga_int:1;
	uint64_t aimr_status_ppr_b_overflow:1;
	uint64_t aimr_status_ppr_log_active:1;
	uint64_t aimr_status_resv13:2;
	uint64_t aimr_status_evt_log_b_overflow:1;
	/* Bits 16..=23 */
	uint64_t aimr_status_evt_log_active:1;
	uint64_t aimr_status_ppr_b_early_overflow:1;
	uint64_t aimr_status_ppr_early_overflow:1;
	uint64_t aimr_status_resv19:5;
	/* Bits 24..=31: All reserved */
	uint64_t aimr_status_resv24:8;
	/* Bits 32..=63: All reserved */
	uint64_t aimr_status_resv32:32;
} amd_iommu_mmio_reg_status_t;
CTASSERT(sizeof (amd_iommu_mmio_reg_status_t) == sizeof (uint64_t));

/*
 * The "Generic" form of a command buffer entry.
 */
typedef struct amd_iommu_cbe {
	uint64_t aicbe_op1:60;
	uint64_t aicbe_opcode:4;
	uint64_t aicbe_op2;
} amd_iommu_cbe_t;

typedef enum amd_iommu_fixarb_intr_ctl {
	AIIC_TGT_ABORT = 0,
	AIIC_FWD_NOREMAP = 1,
	AIIC_FWD_REMAP = 2,
} amd_iommu_fixarb_intr_ctl_t;

typedef enum amd_iommu_cbe_opcodes {
	AICO_COMPLETION_WAIT = 0x01,
	AICO_INVALIDATE_DTE = 0x02,
	AICO_INVALIDATE_PAGES = 0x03,
	AICO_INVALIDATE_IOTLB_PAGES = 0x04,
	AICO_INVALIDATE_INTR_TBL = 0x05,
	AICO_PREFETCH_PAGES = 0x06,
	AICO_COMPLETE_PPR_REQ = 0x07,
	AICO_INVALIDATE_ALL = 0x08,
	AICO_INSERT_GUEST_EVT = 0x09,
	AICO_RST_VMMIO = 0x0a,
} amd_iommu_cbe_opcodes_t;

/*
 * The different event types that the IOMMU will log.
 */
typedef enum amd_iommu_event {
	AMD_IOMMU_EVT_RESERVED = 0,
	AMD_IOMMU_EVT_ILLEGAL_DEV_TABLE_ENTRY = 1,
	AMD_IOMMU_EVT_IO_PAGE_FAULT = 2,
	AMD_IOMMU_EVT_DEV_TAB_HARDWARE_ERROR = 3,
	AMD_IOMMU_EVT_PAGE_TAB_HARDWARE_ERROR = 4,
	AMD_IOMMU_EVT_ILLEGAL_COMMAND_ERROR = 5,
	AMD_IOMMU_EVT_COMMAND_HARDWARE_ERROR = 6,
	AMD_IOMMU_EVT_IOTLB_INVAL_TIMEOUT = 7,
	AMD_IOMMU_EVT_INVALID_DEVICE_REQUEST = 8,
	AMD_IOMMU_EVT_INVALID_PPR_REQUEST = 9,
	AMD_IOMMU_EVT_EVENT_COUNTER_ZERO = 10,
} amd_iommu_event_t;

/*
 * The "Generic" form of an event log entry.
 */
typedef struct amd_iommu_evle {
	uint64_t aievle_op1:60;
	uint64_t aievle_evt_code:4;
	uint64_t aievle_op2;
} amd_iommu_evle_t;

CTASSERT(sizeof (amd_iommu_evle_t) == 16);

/*
 * In the types that follow, AMD document 48882 describes these structures in
 * terms of 32-bit words, so we lay them out that way as well.  Note that the
 * descriptions in the PPR are odd, in that they are described in a table in
 * big-endian _bit_ order within a word, but little-endian _word_ order.  Caveat
 * emptor.
 */

/*
 * The Illegal Device Table Entry event log entry.
 */
typedef struct amd_iommu_evle_idte {
	/* uint32_t illegal_dev_table_entry_event[0] */
	uint32_t aie_idte_device_id:16;
	uint32_t aie_idte_pasid_hi:4;
	uint32_t aie_idte_resv20:7;
	uint32_t aie_idte_vnr:1;
	uint32_t aie_idte_resv28:1;
	uint32_t aie_idte_vevent:1;
	uint32_t aie_idte_vppr:1;
	uint32_t aie_idte_vcmd:1;
	/* uint32_t illegal_dev_table_entry_event[1] */
	uint32_t aie_idte_pasid_lo:16;
	uint32_t aie_idte_gn:1;		/* Guest/nested */
	uint32_t aie_idte_resv49:2;
	uint32_t aie_idte_i:1;		/* Interrupt */
	uint32_t aie_idte_resv52:1;
	uint32_t aie_idte_rw:1;		/* Read/write (1 == write) */
	uint32_t aie_idte_resv54:1;
	uint32_t aie_idte_rz:1;		/* Reserved bit not zero or inv level */
	uint32_t aie_idte_tr:1;		/* Translation request */
	uint32_t aie_idte_resv57:3;
	uint32_t aie_idte_evt_code:4;
	/* uint32_t illegal_dev_table_entry_event[2] */
	uint32_t aie_idte_resv64:2;
	uint32_t aie_idte_address_lo:30;
	/* uint32_t illegal_dev_table_entry_event[3] */
	uint32_t aie_idte_address_hi;
} amd_iommu_evle_idte_t;

CTASSERT(sizeof (amd_iommu_evle_idte_t) == 16);

/*
 * The IO Page Fault event log entry.
 */
typedef struct amd_iommu_evle_iopf {
	/* uint32_t io_page_fault_event[0] */
	uint32_t aie_iopf_device_id:16;
	uint32_t aie_iopf_pasid_hi:4;
	uint32_t aie_iopf_resv20:7;
	uint32_t aie_iopf_vnr:1;
	uint32_t aie_iopf_resv28:1;
	uint32_t aie_iopf_vevent:1;
	uint32_t aie_iopf_vppr:1;
	uint32_t aie_iopf_vcmd:1;
	/* uint32_t io_page_fault_event[1] */
	uint32_t aie_iopf_domid_pasid_lo:16;
	uint32_t aie_iopf_gn:1;		/* Guest/nested */
	uint32_t aie_iopf_nx:1;		/* No-execute */
	uint32_t aie_iopf_us:1;		/* User/Supervisor */
	uint32_t aie_iopf_i:1;		/* Interrupt */
	uint32_t aie_iopf_pr:1;		/* Present (valid) */
	uint32_t aie_iopf_rw:1;		/* Read/write (1 == write) */
	uint32_t aie_iopf_pe:1;		/* Permission Indicator */
	uint32_t aie_iopf_rz:1;		/* Reserved bit not zero or inv level */
	uint32_t aie_iopf_tr:1;		/* Translation request */
	uint32_t aie_iopf_resv57:3;
	uint32_t aie_iopf_evt_code:4;
	/* uint32_t io_page_fault_event[2] */
	uint32_t aie_iopf_address_lo;
	/* uint32_t io_page_fault_event[3] */
	uint32_t aie_iopf_address_hi;
} amd_iommu_evle_iopf_t;

CTASSERT(sizeof (amd_iommu_evle_iopf_t) == 16);

/*
 * The Device Table Hardware Error event log entry.
 */
typedef struct amd_iommu_evle_dthe {
	/* uint32_t dev_tab_hardware_error_event[0] */
	uint32_t aie_dthe_device_id:16;
	uint32_t aie_dthe_resv16:11;
	uint32_t aie_dthe_vnr:1;
	uint32_t aie_dthe_resv28:1;
	uint32_t aie_dthe_vevent:1;
	uint32_t aie_dthe_vppr:1;
	uint32_t aie_dthe_vcmd:1;
	/* uint32_t dev_tab_hardware_error_event[1] */
	uint32_t aie_dthe_resv32:19;
	uint32_t aie_dthe_i:1;		/* Interrupt */
	uint32_t aie_dthe_s:1;		/* Fault with secure data structure */
	uint32_t aie_dthe_rw:1;		/* Read/write (1 == write) */
	uint32_t aie_dthe_resv54:2;
	uint32_t aie_dthe_tr:1;		/* Translation request */
	uint32_t aie_dthe_type:2;	/* Error type. */
	uint32_t aie_dthe_resv59:1;
	uint32_t aie_dthe_evt_code:4;
	/* uint32_t dev_tab_hardware_error_event[2] */
	uint32_t aie_dthe_resv64:4;
	uint32_t aie_dthe_address_lo:28;
	/* uint32_t dev_tab_hardware_error_event[3] */
	uint32_t aie_dthe_address_hi;
} amd_iommu_evle_dthe_t;

CTASSERT(sizeof (amd_iommu_evle_dthe_t) == 16);

/*
 * The Illegal Command Error event log entry.
 */
typedef struct amd_iommu_evle_ice {
	/* uint32_t illegal_command_error_event[0] */
	uint32_t aie_ice_resv0;
	/* uint32_t illegal_command_error_event[1] */
	uint32_t aie_ice_resv32:28;
	uint32_t aie_ice_evt_code:4;
	/* uint32_t illegal_command_error_event[2] */
	uint32_t aie_ice_resv64:4;
	uint32_t aie_ice_address_lo:28;
	/* uint32_t illegal_command_error_event[3] */
	uint32_t aie_ice_address_hi;
} amd_iommu_evle_ice_t;

CTASSERT(sizeof (amd_iommu_evle_ice_t) == 16);

/*
 * The Command Hardware Error event log entry.
 */
typedef struct amd_iommu_evle_che {
	/* uint32_t command_hardware_error_event[0] */
	uint32_t aie_che_resv0;
	/* uint32_t command_hardware_error_event[1] */
	uint32_t aie_che_resv32:25;
	uint32_t aie_che_type:2;
	uint32_t aie_che_resv59:1;
	uint32_t aie_che_evt_code:4;
	/* uint32_t command_hardware_error_event[2] */
	uint32_t aie_che_resv64:4;
	uint32_t aie_che_address_lo:28;
	/* uint32_t command_hardware_error_event[3] */
	uint32_t aie_che_address_hi;
} amd_iommu_evle_che_t;

CTASSERT(sizeof (amd_iommu_evle_che_t) == 16);

/*
 * The Invalid Device Request event log entry.
 */
typedef struct amd_iommu_evle_idr {
	/* uint32_t invalid_device_request_event[0] */
	uint32_t aie_idr_device_id:16;
	uint32_t aie_idr_pasid_hi:4;
	uint32_t aie_idr_resv20:12;
	/* uint32_t invalid_device_request_event[1] */
	uint32_t aie_idr_pasid_lo:16;
	uint32_t aie_idr_gn:1;		/* Guest/Nested */
	uint32_t aie_idr_us:1;		/* User/Supervisor */
	uint32_t aie_idr_resv50:6;
	uint32_t aie_idr_tr:1;		/* Translation request */
	uint32_t aie_idr_type:3;	/* Event type. */
	uint32_t aie_idr_evt_code:4;	/* Event code */
	/* uint32_t invalid_device_request_event[2] */
	uint32_t aie_idr_address_lo;
	/* uint32_t invalid_device_request_event[3] */
	uint32_t aie_idr_address_hi;
} amd_iommu_evle_idr_t;

CTASSERT(sizeof (amd_iommu_evle_idr_t) == 16);

extern amd_iommu_mmio_reg_status_t amd_iommu_read_status(amd_iommu_t *);
extern void amd_iommu_write_status(amd_iommu_t *, amd_iommu_mmio_reg_status_t);
extern amd_iommu_mmio_reg_ctl_t amd_iommu_read_ctl(amd_iommu_t *);
extern void amd_iommu_write_ctl(amd_iommu_t *, amd_iommu_mmio_reg_ctl_t);
extern amd_iommu_mmio_reg_extfeat_t amd_iommu_read_extfeat(amd_iommu_t *);

#endif	/* !_SYS_AMD_IOMMU_IMPL_H */
