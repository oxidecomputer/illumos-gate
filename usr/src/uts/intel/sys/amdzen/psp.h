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

#ifndef _SYS_AMDZEN_PSP_H
#define	_SYS_AMDZEN_PSP_H

#include <sys/bitext.h>
#include <sys/stdint.h>
#include <sys/x86_archext.h>

#include <sys/amdzen/smn.h>

/*
 * This header covers the SMN registers and associated data for interacting with
 * the AMD Platform Security Processor (PSP/MP0), also known as the AMD Secure
 * Processor (ASP/MPASP).
 */

#ifdef __cplusplus
extern "C" {
#endif

AMDZEN_MAKE_SMN_REG_FN(amdzen_smn_psp_reg, PSP, 0x3800000,
    SMN_APERTURE_MASK, 1, 0);

/*
 * MP::MP0CRU::MP0_C2PMSG_<N> / MP::MPASPPCRU::MPASP_C2PMSG_<N> -- CPU-to-PSP
 * (C2P) message registers. The location and actual number present varies across
 * processor families. Besides the few we use below, most of these are otherwise
 * undocumented. We currently only support a handful of CPUs for which we know
 * the correct location and count.
 */
static inline uint16_t
PSP_C2PMSG_MAX_UNITS(x86_processor_family_t fam)
{
	switch (fam) {
	case X86_PF_AMD_MILAN:
		return (104);
	case X86_PF_AMD_GENOA:
	case X86_PF_AMD_BERGAMO:
		return (128);
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		return (138);
	default:
		return (0);
	}
}

static inline smn_reg_t
PSP_C2PMSG(x86_processor_family_t fam, const uint16_t reginst)
{
	smn_reg_def_t regdef = { 0 };
	regdef.srd_unit = SMN_UNIT_PSP;
	regdef.srd_reg = 0x10900;
	regdef.srd_nents = PSP_C2PMSG_MAX_UNITS(fam);

	switch (fam) {
	case X86_PF_AMD_MILAN:
	case X86_PF_AMD_GENOA:
	case X86_PF_AMD_BERGAMO:
		/*
		 * Pre-Zen 5, the first 32 registers are at an earlier offset
		 * but the later ones otherwise match up.
		 */
		if (reginst < 32)
			regdef.srd_reg = 0x10500;
		break;
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
		break;
	default:
		panic("encountered unknown family 0x%x while constructing "
		    "C2PMSG_%u", fam, reginst);
	}

	ASSERT3U(regdef.srd_nents, !=, 0);

	return (amdzen_smn_psp_reg(0, regdef, reginst));
}

/*
 * AMD Platform Security Processor BIOS Implementation Guide for Server EPYC
 * Processors (Pub. 57299 Rev. 2.0 February 2025) describes a set of mailboxes
 * allowing for BIOS and Host software to interface with the PSP:
 *    1) BIOS-to-PSP
 *    2) Host-to-PSP/TEE
 *    3) PSP-to-BIOS
 *
 * The BIOS-to-PSP mailbox interface allows for the BIOS (or equivalent) to
 * issue commands to the PSP via C2PMSG_[28-30]. See definitions below.
 *
 * We don't currently make use of the Host-to-PSP/TEE or PSP-to-BIOS interfaces.
 */

/*
 * MP::MP0CRU::MP0_C2PMSG_28, MP::MPASPPCRU::MPASP_C2PMSG_28 -- (BIOS)CPU-to-PSP
 * mailbox command and status register.
 */
#define	PSP_C2PMBOX(pf)				PSP_C2PMSG(pf, 28)
/*
 * Mailbox state set by target (PSP):
 *    0 - Target not ready or executing previous command
 *    1 - Target ready for new command
 */
#define	PSP_C2PMBOX_GET_READY(r)		bitx32(r, 31, 31)
#define	PSP_C2PMBOX_CLEAR_READY(r)		bitset32(r, 31, 31, 0)
/*
 * Set by the target (PSP) to indicate the host must perform FW recovery
 * sequence.
 */
#define	PSP_C2PMBOX_GET_RECOVERY(r)		bitx32(r, 30, 30)
/*
 * Set by the target (PSP) to indicate the host must perform a warm reset if FW
 * corruption detected.
 */
#define	PSP_C2PMBOX_GET_RESET_REQUIRED(r)	bitx32(r, 29, 29)
/*
 * Set by the host to indicate command target should execute.
 */
#define	PSP_C2PMBOX_SET_CMD_ID(r, v)		bitset32(r, 23, 16, v)
/*
 * Set by the target (PSP) to indicate the status of the last executed command
 * with 0 denoting success.
 */
#define	PSP_C2PMBOX_GET_STATUS(r)		bitx32(r, 15, 0)

/*
 * MP::MP0CRU::MP0_C2PMSG_29, MP::MPASPPCRU::MPASP_C2PMSG_29 -- (BIOS)CPU-to-PSP
 * mailbox command buffer physical address (lower 32-bits).
 */
#define	PSP_C2PMBOX_BUF_ADDR_LO(pf)	PSP_C2PMSG(pf, 29)

/*
 * MP::MP0CRU::MP0_C2PMSG_30, MP::MPASPPCRU::MPASP_C2PMSG_30 -- (BIOS)CPU-to-PSP
 * mailbox command buffer physical address (upper 32-bits).
 */
#define	PSP_C2PMBOX_BUF_ADDR_HI(pf)	PSP_C2PMSG(pf, 30)

/*
 * The provided command buffer address must be 32 byte aligned.
 */
#define	PSP_C2PMBOX_BUF_ALIGN	32

/*
 * MP::MP0CRU::MP0_C2PMSG_31, MP::MPASPPCRU::MPASP_C2PMSG_31 -- CPU-to-PSP
 * RAS mailbox command and result register.
 */
#define	PSP_RAS_MBOX(pf)		PSP_C2PMSG(pf, 31)
/*
 * Mailbox state - cleared by host to process new command and set by target
 * when it completes processing command.
 */
#define	PSP_RAS_MBOX_GET_DONE(r)	bitx32(r, 31, 31)
#define	PSP_RAS_MBOX_CLEAR_DONE(r)	bitset32(r, 31, 31, 0)
/*
 * Set by the target to provide the result for the last command when the main
 * result field is otherwise repurposed to provide a payload.
 */
#define	PSP_RAS_MBOX_GET_ALT_STATUS(r)	bitx32(r, 27, 24)
/*
 * Set by the host to indicate command target should execute.
 */
#define	PSP_RAS_MBOX_SET_CMD_ID(r, v)	bitset32(r, 23, 16, v)
/*
 * Set by the target to provide the result for the last command in-line
 * (_GET_DATA) or to indicate the status of the last executed command
 * (_GET_STATUS) depending on the command last executed.
 */
#define	PSP_RAS_MBOX_GET_DATA(r)	bitx32(r, 15, 0)
#define	PSP_RAS_MBOX_GET_STATUS(r)	PSP_RAS_MBOX_GET_DATA(r)
/*
 * Set by the host to pass command specific data in-line (v.s. writing to the
 * command buffer).
 */
#define	PSP_RAS_MBOX_SET_DATA(r, v)	bitset32(r, 15, 0, v)

#pragma pack(1)

/*
 * Known CPU-to-PSP commands. The set of supported commands may vary between
 * processor families, i.e. Naples (ZP), Rome (SSP), Milan (GN), Genoa (RS),
 * Turin (BRH[D]), Venice (WH)
 */
typedef enum cpu2psp_mbox_cmd {
	/*
	 * Provide details on the SMM memory region reserved for communicating
	 * with the PSP.
	 *
	 * Supported: ZP, SSP, GN, RS, BRH[D], WH
	 */
	C2P_MBOX_CMD_SMM_INFO		= 0x2,
	/*
	 * Retrieve runtime firmware versions. The PSP will fill in
	 * the provided command buffer according to `c2p_mbox_get_ver_buffer_t`.
	 *
	 * Supported: ZP, SSP, GN, RS, BRH[D], WH
	 */
	C2P_MBOX_CMD_GET_VER		= 0x19,
	/*
	 * Enable/disable PSP side support for ACPI RAS Error Injection (EINJ).
	 *
	 * Supported: GN, RS, BRH[D], WH
	 */
	C2P_MBOX_CMD_ACPI_RAS_EINJ	= 0x41,
	/*
	 * Abort the last command.
	 */
	C2P_MBOX_CMD_ABORT		= 0xfe,
} cpu2psp_mbox_cmd_t;

/*
 * Common header for command buffers submitted to PSP.
 */
typedef struct c2p_mbox_buffer_hdr {
	/*
	 * Total size of buffer submitted with command: sizeof this structure
	 * along with the size of whatever command specific data follows.
	 */
	uint32_t	c2pmb_size;
	/*
	 * The status of the command as copied over from the status register.
	 */
	uint32_t	c2pmb_status;
} c2p_mbox_buffer_hdr_t;

/*
 * Runtime firmware version provided by the PSP.
 */
typedef struct psp_fw_versions {
	/*
	 * The PSP's own firmware version.
	 */
	uint8_t		pfv_psp[4];
	/*
	 * The AGESA Boot Loader (ABL) version.
	 */
	uint8_t		pfv_agesa[4];
	/*
	 * The APPB, APCB & APOB versions. Note on Turin at least these don't
	 * seem to be populated with the versions as listed in the AGESA PI
	 * release notes. The BIOS Implementation Guide (Pub. 57299 Rev. 2.0
	 * February 2025) also only mentions the PSP, AGESA & SMU versions,
	 * which do match the values in the release notes.
	 *
	 * Glossary:
	 * APPB - AMD/AGESA PSP PMU Block/Blob
	 * APCB - AMD/AGESA PSP Configuration/Customization Block/Blob
	 * APOB - AMD/AGESA PSP Output Block/Blob
	 */
	uint8_t		pfv_appb[4];
	uint8_t		pfv_apcb[4];
	uint8_t		pfv_apob[4];
	/*
	 * The System Management Unit (SMU) firmware version.
	 */
	uint8_t		pfv_smu[4];
} psp_fw_versions_t;

/*
 * Command buffer provided for `C2P_MBOX_CMD_GET_VER`.
 */
typedef struct c2p_mbox_get_ver_buffer {
	c2p_mbox_buffer_hdr_t	c2pmgvb_hdr;
	psp_fw_versions_t	c2pmgvb_vers;
} c2p_mbox_get_ver_buffer_t;

typedef enum psp_acpi_ras_einj_action {
	PSP_ACPI_RAS_EINJ_ENABLE	= 1,
	PSP_ACPI_RAS_EINJ_DISABLE	= 2,
} psp_acpi_ras_einj_action_t;

/*
 * Command buffer provided for `C2P_MBOX_CMD_ACPI_RAS_EINJ`.
 */
typedef struct c2p_mbox_ras_einj_buffer {
	c2p_mbox_buffer_hdr_t	c2pmreb_hdr;
	uint32_t		c2pmreb_action;
} c2p_mbox_ras_einj_buffer_t;

/*
 * SMM register accessed via MMIO.
 */
#define	PSP_SMM_ADDR_TYPE_MEM		1
/*
 * SMM register is 32-bits wide.
 */
#define	PSP_SMM_ADDR_WIDTH_DWORD	2

typedef struct psp_smm_trigger_info {
	uint64_t	psti_addr;
	uint32_t	psti_addr_type;
	uint32_t	psti_width;
	uint32_t	psti_and_mask;
	uint32_t	psti_or_mask;
} psp_smm_trigger_info_t;

typedef struct psp_smm_register {
	uint64_t	psr_addr;
	uint32_t	psr_addr_type;
	uint32_t	psr_width;
	uint32_t	psr_and_mask;
	uint32_t	psr_or_mask;
} psp_smm_register_t;

typedef struct psp_smm_register_info {
	psp_smm_register_t	psri_smi_enb;
	psp_smm_register_t	psri_eos;
	psp_smm_register_t	psri_fakesmien;
	psp_smm_register_t	psri_reserved[5];
} psp_smm_register_info_t;

typedef struct c2p_mbox_smm_info {
	uint64_t		c2pmsi_smm_base;
	uint64_t		c2pmsi_smm_mask;
	uint64_t		c2pmsi_psp_data_base;
	uint64_t		c2pmsi_psp_data_len;
	psp_smm_trigger_info_t	c2pmsi_trig_info;
	psp_smm_register_info_t	c2pmsi_reg_info;
	uint64_t		c2pmsi_mbox_buf_addr;
	uint64_t		c2pmsi_smm_flag_addr;
} c2p_mbox_smm_info_t;

typedef struct c2p_mbox_smm_info_buffer {
	c2p_mbox_buffer_hdr_t	c2pmsib_hdr;
	c2p_mbox_smm_info_t	c2pmsib_info;
} c2p_mbox_smm_info_buffer_t;


typedef union {
	c2p_mbox_buffer_hdr_t		c2pmb_hdr;
	c2p_mbox_get_ver_buffer_t	c2pmb_get_ver;
	c2p_mbox_ras_einj_buffer_t	c2pmb_ras_einj;
	c2p_mbox_smm_info_buffer_t	c2pmb_smm_info;
} c2p_mbox_buffer_t;

/*
 * Supported PSP RAS EINJ commands.
 */
typedef enum psp_ras_mbox_cmd {
	/*
	 * Buffer address bits 15:0
	 */
	PSP_RAS_MBOX_CMD_BUF_ADDR0	= 0,
	/*
	 * Buffer address bits 31:16
	 */
	PSP_RAS_MBOX_CMD_BUF_ADDR1	= 1,
	/*
	 * Buffer address bits 47:32
	 */
	PSP_RAS_MBOX_CMD_BUF_ADDR2	= 2,
	/*
	 * Buffer address bits 63:48
	 */
	PSP_RAS_MBOX_CMD_BUF_ADDR3	= 3,
	/*
	 * Execute the command identified by the value specified in bits 15:0
	 * of the RAS mailbox.
	 */
	PSP_RAS_MBOX_CMD_DIRECT		= 4,
	/*
	 * Semantics of this command are currently unknown; presumambly relies
	 * on parameters provided via the RAS command buffer (the address of
	 * which is retreived via the above commands).
	 */
	PSP_RAS_MBOX_CMD_BUFFER		= 5,
} psp_ras_mbox_cmd_t;

/*
 * Begin execution of the error injection operation specified in the RAS command
 * buffer. This is a "Direct Command" submitted via the RAS mailbox
 * (see `PSP_RAS_MBOX_CMD_DIRECT`).
 */
#define	PSP_RAS_EINJ_EXECUTE_OPERATION  0x83

/*
 * This structure describes the set of supported error injection types as
 * provided by the PSP. Also used by the host to indicate what type of error
 * to inject.
 *
 * See ACPI Specification, Version 6.3, Table 18-409 Error Type Definition.
 */
typedef union psp_ras_error_types {
	struct {
		uint32_t	pret_processor_correctable:1;
		uint32_t	pret_processor_uncorrectable:1;
		uint32_t	pret_processor_fatal:1;
		uint32_t	pret_memory_correctable:1;
		uint32_t	pret_memory_uncorrectable:1;
		uint32_t	pret_memory_fatal:1;
		uint32_t	pret_pcie_correctable:1;
		uint32_t	pret_pcie_uncorrectable:1;
		uint32_t	pret_pcie_fatal:1;
		uint32_t	pret_platform_correctable:1;
		uint32_t	pret_platform_uncorrectable:1;
		uint32_t	pret_platform_fatal:1;
		uint32_t	pret_reserved:19;
		uint32_t	pret_vendor:1;
	};
	uint32_t		pret_val;
} psp_ras_error_types_t;
CTASSERT(sizeof (psp_ras_error_types_t) == sizeof (uint32_t));

/*
 * This structure allows the host to specify both a type of error to inject
 * along with type-specific details (e.g., target memory address or PCIe BDF).
 *
 * See ACPI Specification, Version 6.3, Table 18-410 SET_ERROR_TYPE_WITH_ADDRESS
 * Data Structure.
 */
typedef struct psp_ras_error_types_ext {
	/*
	 * The specific error type to inject.
	 */
	psp_ras_error_types_t		prete_error_type;
	/*
	 * AGESA populates this with the offset to the appropriate vendor error
	 * extension struct (rev 0 vs 1) for ACPI clients. Given we're skipping
	 * ACPI we don't bother filling this in.
	 */
	uint32_t			prete_vendor_ext_off;
	/*
	 * Flags indicating validity of subsequent fields.
	 */
	union {
		uint32_t		prete_flags;
		struct {
			uint32_t	prete_apic_id_valid:1;
			uint32_t	prete_mem_addr_valid:1;
			uint32_t	prete_pcie_sbdf_valid:1;
			uint32_t	prete_reserved:29;
		};
	};
	/*
	 * For processor errors, if valid, this specifies the physical APIC ID
	 * or the x2APIC ID of the error injection target processor.
	 */
	uint32_t			prete_apic_id;
	/*
	 * For memory errors, if valid, this provides the base physical address
	 * for error injection.
	 */
	uint64_t			prete_mem_addr;
	/*
	 * For memory errors, in addition to the above base, this may optionally
	 * be specified to provide a mask for the desired target address range.
	 * A mask of 0 is equivalent to a mask of all-1s.
	 */
	uint64_t			prete_mem_addr_mask;
	/*
	 * For PCIe errors, if valid, this provides the Segment, Bus, Device, &
	 * Function (SBDF) to target.
	 */
	union {
		uint32_t		prete_pcie_sbdf;
		struct {
			uint8_t		prete_pcie_reserved;
			uint8_t		prete_pcie_func:3;
			uint8_t		prete_pcie_dev:5;
			uint8_t		prete_pcie_bus;
			uint8_t		prete_pcie_seg;
		};
	};
} psp_ras_error_types_ext_t;

typedef struct amd_vendor_errors {
	uint64_t	ave_supported_errs;
	uint64_t	ave_err_to_inj;
	uint8_t		ave_severity;
	uint8_t		ave_reserved[3];
	uint32_t	ave_inj_ctrl;
	uint8_t		ave_location[4];
} amd_vendor_errors_t;

typedef struct psp_ras_vendor_error_type {
	uint32_t		prvet_len;
	uint32_t		prvet_sbdf;
	uint16_t		prvet_vendor_id;
	uint16_t		prvet_device_id;
	uint8_t			prvet_rev_id;
	uint8_t			prvet_reserved[3];
	amd_vendor_errors_t	prvet_oem;
} psp_ras_vendor_error_type_t;

typedef struct psp_ras_vendor_error_type_rev1 {
	psp_ras_vendor_error_type_t	prvet1_base;
	uint8_t				prvet1_reserved[84];
	uint8_t				prvet1_ext_err_ctrl_buf[512];
	uint8_t				prvet1_ext_err_log[1024];
} psp_ras_vendor_error_type_rev1_t;

/*
 * Status codes returned for an error injection status.
 */
typedef enum psp_ras_einj_status {
	EINJ_STATUS_SUCCESS	= 0,
	EINJ_STATUS_FAIL	= 1,
	EINJ_STATUS_INVALID	= 2,
} psp_ras_einj_status_t;

#define	PSP_EINJ_FW_REV0	0
#define	PSP_EINJ_FW_REV1	1	/* Incremented with Turin. */

/*
 * This structure provides the definition for the region of memory provided by
 * the PSP for error injection. On a system with AGESA-based firmware, one would
 * make use of the ACPI-based Error Injection (EINJ) table for error injection
 * from the OS. The routines exposed via ACPI would essentially then correspond
 * to reads & writes of the fields described here. ACPI (v6.3) defintions are
 * included below as a reference.
 */
typedef struct psp_ras_command_buffer {
	/*
	 * GET_ERROR_TYPE - Populated by the PSP to indicate the supported error
	 * injection types.
	 */
	psp_ras_error_types_t		prcb_error_types;
	/*
	 * The EINJ FW revision.
	 */
	uint8_t				prcb_einj_fw_rev;
	uint8_t				prcb_reserved[3];
	/*
	 * SET_ERROR_TYPE - Set by the host to indicate the type of error to
	 * inject. Only one kind of error may be injected at a time. Injecting
	 * errors via `prcb_set_error_type_with_addr` should be preferred
	 * and attempting to inject an error via this may not work.
	 */
	psp_ras_error_types_t		prcb_set_error_type;
	uint32_t			prcb_reserved2;
	union {
		struct {
			/*
			 * Indicates the status of an error injection operation.
			 * Host sets it to begin a new operation and polls on it
			 * until cleared by FW.
			 */
			uint32_t	prcb_busy:1;
			/*
			 * FW provided error status for the last error injection
			 * operation (see `psp_ras_einj_status_t`).
			 */
			uint32_t	prcb_command_status:8;
			uint32_t	prcb_reserved3:23;
		};
		uint32_t		prcb_command_busy_status;
	};
	uint32_t			prcb_reserved4[3];
	/*
	 * ACPI Error Injection (ACPI EINJ) is a 2-step process: the desired
	 * error type and information is provided, followed by a trigger action
	 * described as a sequence of instructions. This field usually provides
	 * the physical address to said trigger action table. The table itself
	 * then simply contains an address to the memory location the host must
	 * write to trigger the error. That address is simply the
	 * `prcb_trigger_error_start` field below so for our purposes we can
	 * skip the indirection.
	 */
	uint64_t			prcb_trig_act_tbl_addr;
	/*
	 * Flag polled by the FW and set by the host to trigger the last
	 * injected error.
	 */
	uint32_t			prcb_trigger_error_start;
	/*
	 * Flag polled by the FW and set by the host to stop triggering the last
	 * injected error.
	 */
	uint32_t			prcb_trigger_error_stop;
	/*
	 * SET_ERROR_TYPE_WITH_ADDRESS - Like SET_ERROR_TYPE but allows the host
	 * to provide specific details like what memory address or PCIe BDF to
	 * inject an error for. If set, this takes precedence to SET_ERROR_TYPE.
	 */
	psp_ras_error_types_ext_t	prcb_set_error_type_with_addr;
	/*
	 * Rev 0 Vendor Error Type Extension Structure.
	 */
	psp_ras_vendor_error_type_t	prcb_vendor_error_type;
	/*
	 * See comments on `prcb_trig_act_tbl_addr`.
	 */
	uint8_t				prcb_trig_act_tbl[48];
	uint8_t				prcb_reserved5[80];
	/*
	 * GET_EXECUTE_OPERATION_TIMINGS
	 */
	uint64_t			prcb_execute_operations_time;
	uint8_t				prcb_reserved6[120];
	/*
	 * Rev 1 Vendor Error Type Extension Structure.
	 */
	psp_ras_vendor_error_type_rev1_t	prcb_vendor_error_type_rev1;
} psp_ras_command_buffer_t;
CTASSERT(sizeof (psp_ras_command_buffer_t) == 0x800);

#pragma pack()	/* pack(1) */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_PSP_H */
