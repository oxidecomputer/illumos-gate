/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/* Copyright 2026 Oxide Computer Company */

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
 * Our definitions define these types as `uint32_t`s bearing bit-fields.
 * Hopefully striking a balance between ease of reading and reference, we note
 * the start of each group of 32 in a pseudo-C notation, and if we describe a
 * bit range, we use AMD's (inclusive, msb-first) notation.
 */

#ifndef _SYS_AMD_IOMMU_IMPL_H
#define	_SYS_AMD_IOMMU_IMPL_H

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
 * space to direct external interrupts to high-numbered CPUs, without
 * virtualization.  Since this seconde sense is the way that we currently use
 * the IOMMU, we only provide that definition.
 *
 * Note that this struct makes several assumptions about both the endianness of
 * the platform, and alignment.  Given that this code is intimately tied to the
 * platform, however, we will not address these further.
 */
typedef struct amd_iommu_irte {
	/* uint32_t irte[0] - bits 31:0 */
	uint32_t aiie_remap_en: 1;	/* Remap enable */
	uint32_t aiie_sup_io_pf: 1;	/* Suppress IO page fault */
	uint32_t aiie_int_type: 3;	/* Interrupt type  */
	uint32_t aiie_rq_eoi: 1;	/* Request EOI */
	uint32_t aiie_dm: 1;		/* Destination Mode  */
	uint32_t aiie_guest_mode: 1;	/* Guest mode (must be zero) */
	uint32_t aiie_dest_0_23: 24;	/* Destination x2APIC ID bits 23:0 */
	/* uint32_t irte[1] - bits 63:32 */
	uint32_t aiie_res0: 32;		/* (Reserved) */
	/* uint32_t irte[2] - bits 95:64 */
	uint32_t aiie_vector: 8;	/* Interrupt vector */
	uint32_t aiie_res1: 24;		/* (Reserved) */
	/* uint32_t irte[3] - bits 127:96 */
	uint32_t aiie_res2: 24;		/* (Reserved) */
	uint32_t aiie_dest_24_31: 8;	/* Destination x2APIC ID bits 31:24 */
} amd_iommu_irte_t;
CTASSERT(sizeof (amd_iommu_irte_t) == 16);

/*
 * Device Table Entry
 */
typedef struct amd_iommu_dte {
	/* uint32_t dte[0] - bits 31:0 */
	uint32_t aide_valid:1;		/* Valid */
	uint32_t aide_xlate_valid:1;	/* Translation valid  */
	uint32_t aide_resv0:2;		/* (Reserved) */
	uint32_t aide_cxl_mem_attr:3;	/* CXL Memory Attributes */
	uint32_t aide_host_ad:2;	/* Update host Access/Dirty PTE bits */
	uint32_t aide_paging_mode:3;	/* Paging mode */
	uint32_t aide_pt_root_lo:20;	/* Low 20 bits of PT root address */
	/* uint32_t dte[1] - bits 63:32 */
	uint32_t aide_pt_root_hi:20;	/* High 20 bits of PT root address */
	uint32_t aide_ppr_en:1;		/* PPR enable */
	uint32_t aide_gprp_en:1;	/* Guest PPR Response; PASID enabled */
	uint32_t aide_gio_prot_valid:1;	/* Guest IO protection valid */
	uint32_t aide_gxlate_valid:1;	/* Guest translation valid */
	uint32_t aide_glevels_xlate:2;	/* Guest levels translated */
	uint32_t aide_gcr3_trp0:3;	/* Guest CR3 bits 14:12 */
	uint32_t aide_io_r:1;		/* Non-page table defined read perm */
	uint32_t aide_io_w:1;		/* Non-page table defined write perm */
	uint32_t aide_resv1:1;		/* (Reserved) */
	/* uint32_t dte[2] - bits 95:64 */
	uint32_t aide_domain_id:16;	/* DomainID (cache tag) */
	uint32_t aide_gcr3_trp1:16;	/* Guest CR3 bits 30:15 */
	/* uint32_t dte[3] - bits 127:96 */
	uint32_t aide_iotlb_en:1;	/* IOTLB enable  */
	uint32_t aide_supp_io_pf:1;	/* Suppress IO page fault events */
	uint32_t aide_supp_all_io_pf:1;	/* Suppress ALL IO page fault events */
	uint32_t aide_io_port_ctl:2;	/* Port IO control */
	uint32_t aide_cache_hint:1;	/* IOTLB cache hint */
	uint32_t aide_snoop_dis:1;	/* Page table walk snoop disable */
	uint32_t aide_allow_excl:1;	/* Allow IOMMU exclusion range access */
	uint32_t aide_sysmgt_msg_en:2;	/* System management message enable */
	uint32_t aide_secure_ats:1;	/* Secure ATS */
	uint32_t aide_gcr3_trp2:21;	/* Guest CR3 bits 51:31 */
	/* uint32_t dte[4] - bits 159:128 */
	uint32_t aide_intr_map_valid:1;	/* Interrupt map valid */
	uint32_t aide_intr_tbl_len:4;	/* Interrupt table length (2^val) */
	uint32_t aide_unmap_intr_ign:1;	/* Ignore unmapped interrupts */
	uint32_t aide_intr_tbl_lo:26;	/* Interrupt table root pointer 31:6 */
	/* uint32_t dte[5] - bits 191:160 */
	uint32_t aide_intr_tbl_hi:20;	/* Interrupt table root pointer 51:32 */
	uint32_t aide_resv2:2;		/* (Reserved) */
	uint32_t aide_gpaging_mode:2;	/* Guest paging mode */
	uint32_t aide_init_nomap:1;	/* Pass INIT intr messages unmapped */
	uint32_t aide_ext_int_nomap:1;	/* Pass ExtInt intr messages unmapped */
	uint32_t aide_nmi_nomap:1;	/* Pass NMI intr messages unmapped */
	uint32_t aide_host_pt_mode:1;	/* Host page table mode hint */
	uint32_t aide_int_ctl:2;	/* Interrupt control */
	uint32_t aide_lint0_nomap:1;	/* LINT0 interrupts passed unmapped */
	uint32_t aide_lint1_nomap:1;	/* LINT1 interrupts passed unmapped */
	/* uint32_t dte[6] - bits 223:192 */
	uint32_t aide_resv3:15;		/* (Reserved) */
	uint32_t aide_viommu_en:1;	/* Virtualize IOMMU enabled */
	uint32_t aide_gdevice_id:16;	/* Guest DeviceID */
	/* uint32_t dte[7] - bits 255:224 */
	uint32_t aide_guest_id:16;	/* Guest ID */
	uint32_t aide_resv4:5;		/* (Reserved) */
	uint32_t aide_resv5_mbz:1;	/* (Reserved; explicitly must be 0) */
	uint32_t aide_attr_ovr_valid:1;	/* Attribute override valid */
	uint32_t aide_mode0_fc:1;	/* Value of PTE.FC when DTE.Mode = 0 */
	uint32_t aide_snoop_attr:8;	/* Snoop attributes */
} amd_iommu_dte_t;
CTASSERT(sizeof (amd_iommu_dte_t) == 32);

/*
 * According to AMD pub 48882, "the presence of an IOMMU capability block in a
 * PCIe Function indicates the presence of an IOMMU."
 *
 * XXX: This is not currently used in the Oxide architecture, but we retain
 * these definitions for the i86pc port.
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
	uint32_t aipc_base_addr_19_31:13;
	/* Capability Offset 08h IOMMU Base Address High Register */
	uint32_t aipc_base_addr_32_63;
	/* Capability Offset 0Ch IOMMU Range Register */
	uint32_t aipc_range_unit_id:5;
	uint32_t aipc_range_resv:2;
	uint32_t aipc_range_valid:1;
	uint32_t aipc_range_bus:8;
	uint32_t aipc_range_first_dev:8;
	uint32_t aipc_range_last_dev:8;
	/* Capabiity Offset 10h IOMMU Miscellaneous Information Register 0 */
	uint32_t aipc_misc0_msi_num:5;
	uint32_t aipc_misc0_gva_size:3;
	uint32_t aipc_misc0_pa_size:7;
	uint32_t aipc_misc0_va_size:7;
	uint32_t aipc_misc0_ht_ats_range_rsvd:1;
	uint32_t aipc_misc0_resv:4;
	uint32_t aipc_misc0_ppr_msi_num:5;
	/* Capabilit Offset 14h IOMMU Miscellaneous Informatin Register 1 */
	uint32_t aipc_misc1_msi_num_ga:5;
	uint32_t aipc_misc1_resv:27;
} amd_iommu_pci_cap_t;

/*
 * The vendor capability registers (48882 vers 3.11 sec 3.3) provide information
 * for using SR-IOV style virtual functions with the IOMMU.  We have no need for
 * this currently, and do not support it.
 */

/*
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
 * between architectures, we use this these structs, instead.
 */
typedef struct amd_iommu_mmio_reg_ctl {
	uint64_t aimr_ctl_en:1;
	uint64_t aimr_ctl_ht_tun_en:1;
	uint64_t aimr_ctl_evt_log_en:1;
	uint64_t aimr_ctl_evt_int_en:1;
	uint64_t aimr_ctl_cmd_wait_int_en:1;
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

/* MMIO Offset 0030h IOMMU Extended Feature Regster */
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
	uint64_t aimr_extfeat_resv0:2;
	uint64_t aimr_extfeat_dual_evt_log_sup:2;
	uint64_t aimr_extfeat_resv1:1;
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
	uint64_t aimr_extfeat_resv2:4;
	uint64_t aimr_extfeat_v2_pte_a_bit_dis_hw_update_sup:1;
	uint64_t aimr_extfeat_force_remapped_int_phys_dest_sup:1;
	uint64_t aimr_extfeat_secure_nested_paging_sup:1;
} amd_iommu_mmio_reg_extfeat_t;
CTASSERT(sizeof (amd_iommu_mmio_reg_extfeat_t) == sizeof (uint64_t));

/*
 * XXX: This is not used used in the Oxide architecture.  It is for a future
 * port to i86pc.
 */
typedef struct amd_iommu_mmio_reg_devtbl_base {
	uint64_t aimr_devtbl_base_size:9;
	uint64_t aimr_devtbl_base_resv0:3;
	uint64_t aimr_devtbl_base_addr:40;
	uint64_t aimr_devtbl_base_resv1:12;
} amd_iommu_mmio_reg_devtbl_base_t;

/*
 * XXX: This is not used used in the Oxide architecture.  It is for a future
 * port to i86pc.
 */
typedef struct amd_iommu_mmio_reg_cmdbuf_base {
	uint64_t aimr_cmdbuf_base_resv0:12;
	uint64_t aimr_cmdbuf_base_addr:40;
	uint64_t aimr_cmdbuf_base_resv1:4;
	uint64_t aimr_cmdbuf_base_len:4;
	uint64_t aimr_cmdbuf_base_resv2:4;
} amd_iommu_mmio_reg_cmdbuf_base_t;

/*
 * XXX: This is not used used in the Oxide architecture.  It is for a future
 * port to i86pc.
 */
/* MMIO Offset 0010h Event Log Base Address Register */
typedef struct amd_iommu_mmio_reg_evtlog_base {
	uint64_t aimr_evtlog_base_resv0:12;
	uint64_t aimr_evtlog_base_addr:40;
	uint64_t aimr_evtlog_base_resv1:4;
	uint64_t aimr_evtlog_base_len:4;
	uint64_t aimr_evtlog_base_resv2:4;
} amd_iommu_mmio_reg_evtlog_base_t;

/*
 * XXX: This is not used used in the Oxide architecture.  It is for a future
 * port to i86pc.
 */
/* MMIO Offset 0020h IOMMU Exclusion Base / Completion Store Register */
typedef struct amd_iommu_mmio_reg_excl_base {
	uint64_t aimr_excbase_excl_range_en:1;
	uint64_t aimr_excbase_allow_all_fwd_untrans:1;
	uint64_t aimr_excbase_resv0:10;
	uint64_t aimr_excbase_base_addr:40;
	uint64_t aimr_excbase_resv1:12;
} amd_iommu_mmio_reg_excl_base_t;

/*
 * XXX: This is not used used in the Oxide architecture.  It is for a future
 * port to i86pc.
 */
/* MMIO Offset 0028h IOMMU Exclusion Range / Completion Store Limit */
typedef struct amd_iommu_mmio_reg_excl_lim {
	uint64_t aimr_exclim_resv0:12;
	uint64_t aimr_exclim_limit:40;
	uint64_t aimr_exclim_resv1:12;
} amd_iommu_mmio_reg_excl_lim_t;

/*
 * The IOMMU Status Register
 *
 * This 64-bit register defines status indicators for various IOMMU functions;
 * for example whether a command is pending, an interrupt has been generated
 * from the IOMMU itself, or the event log has overflowed.
 */
/* MMIO Offset 2020h IOMMU Status Register */
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
	uint64_t aimr_status_resv0:2;
	uint64_t aimr_status_evt_log_b_overflow:1;
	/* Bits 16..=23 */
	uint64_t aimr_status_evt_log_active:1;
	uint64_t aimr_status_ppr_b_early_overflow:1;
	uint64_t aimr_status_ppr_early_overflow:1;
	uint64_t aimr_status_resv1:5;
	/* Bits 24..=31: All reserved */
	uint64_t aimr_status_resv2:8;
	/* Bits 32..=63: All reserved */
	uint64_t aimr_status_resv3:32;
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
 * The "Generic" form of an event log entry.
 */
typedef struct amd_iommu_evle {
	uint64_t aievle_op1:60;
	uint64_t aievle_evt_code:4;
	uint64_t aievle_op2;
} amd_iommu_evle_t;

extern amd_iommu_mmio_reg_status_t amd_iommu_read_status(amd_iommu_t *);
extern void amd_iommu_write_status(amd_iommu_t *, amd_iommu_mmio_reg_status_t);
extern amd_iommu_mmio_reg_ctl_t amd_iommu_read_ctl(amd_iommu_t *);
extern void amd_iommu_write_ctl(amd_iommu_t *, amd_iommu_mmio_reg_ctl_t);
extern amd_iommu_mmio_reg_extfeat_t amd_iommu_read_extfeat(amd_iommu_t *);

#endif	/* !_SYS_AMD_IOMMU_IMPL_H */
