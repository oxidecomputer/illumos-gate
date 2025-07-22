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
 * MP::MP0CRU::MP0_C2PMSG_<N> -- The CPU-to-PSP (C2P) message registers at their
 * legacy (pre-Zen 5) location. Note there are actually more than just 32
 * defined but the subsequent registers are disjoint and omitted here for
 * simplicity (we only make use of a handful that are covered here).
 */
/*CSTYLED*/
#define	D_PSP_C2PMSG_LEGACY	(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PSP,	\
	.srd_reg = 0x10500,		\
	.srd_nents = 32,		\
}
/*
 * MP::MPASPPCRU::MPASP_C2PMSG_<N> -- The CPU-to-PSP (C2P) message registers
 * for Zen 5 onwards. Unlike the pre-Zen 5 layout, the registers are no longer
 * disjoint but we keep the 32 count here to match the legacy definition and
 * just for simplicity as the actual count varies by family.
 */
/*CSTYLED*/
#define	D_PSP_C2PMSG		(const smn_reg_def_t){	\
	.srd_unit = SMN_UNIT_PSP,	\
	.srd_reg = 0x10900,		\
	.srd_nents = 32,		\
}

/*
 * Helper to select the correct CPU-to-PSP (C2P) message registers based on
 * the given processor family.
 */
static inline smn_reg_t
PSP_C2PMSG(x86_processor_family_t pf, const uint16_t reginst)
{
	smn_reg_def_t reg;
	switch (pf) {
	case X86_PF_AMD_TURIN:
	case X86_PF_AMD_DENSE_TURIN:
	case X86_PF_AMD_STRIX:
	case X86_PF_AMD_GRANITE_RIDGE:
	case X86_PF_AMD_KRACKAN:
	case X86_PF_AMD_STRIX_HALO:
		reg = D_PSP_C2PMSG;
		break;
	default:
		reg = D_PSP_C2PMSG_LEGACY;
		break;
	}
	return (amdzen_smn_psp_reg(0, reg, reginst));
}

/*
 * MP::MP0CRU::MP0_C2PMSG_28, MP::MPASPPCRU::MPASP_C2PMSG_28 -- CPU-to-PSP
 * mailbox command and status register (see `cpu2psp_mbox_t`).
 */
#define	PSP_C2PMBOX_CMD(pf)		PSP_C2PMSG(pf, 28)

/*
 * MP::MP0CRU::MP0_C2PMSG_29, MP::MPASPPCRU::MPASP_C2PMSG_29 -- CPU-to-PSP
 * mailbox command buffer physical address (lower 32-bits).
 */
#define	PSP_C2PMBOX_BUF_ADDR_LO(pf)	PSP_C2PMSG(pf, 29)

/*
 * MP::MP0CRU::MP0_C2PMSG_30, MP::MPASPPCRU::MPASP_C2PMSG_30 -- CPU-to-PSP
 * mailbox command buffer physical address (upper 32-bits).
 */
#define	PSP_C2PMBOX_BUF_ADDR_HI(pf)	PSP_C2PMSG(pf, 30)

/*
 * The provided command buffer address must be 32 byte aligned.
 */
#define	PSP_C2PMBOX_BUF_ALIGN	32

/*
 * The structure describes the fields of the CPU-to-PSP mailbox command and
 * status register (C2PMSG_28).
 */
typedef union {
	struct {
		/*
		 * Set by the target (PSP) to indicate status of the last
		 * executed command (see `cpu2psp_mbox_status_t`).
		 */
		uint16_t	c2pm_status;
		/*
		 * Set the host to indicate command target should execute.
		 */
		uint8_t		c2pm_cmd_id;
		uint8_t		c2pm_reserved:4;
		/*
		 * Set by the target (PSP) to indicate the previous command
		 * is asynchronous and still pending.
		 */
		uint8_t		c2pm_async_cmd_pending:1;
		/*
		 * Set by the target (PSP) to indicate the host must perform a
		 * warm reset if FW corruption detected.
		 */
		uint8_t		c2pm_reset_required:1;
		/*
		 * Set by the target (PSP) to indicate the host must perform
		 * FW recovery sequence.
		 */
		uint8_t		c2pm_recovery:1;
		/*
		 * Mailbox state set by target:
		 *    0 - Target not ready or executing previous command
		 *    1 - Target ready for new command
		 */
		uint8_t		c2pm_ready:1;
	};
	uint32_t		c2pm_val;
} cpu2psp_mbox_t;
CTASSERT(sizeof (cpu2psp_mbox_t) == sizeof (uint32_t));

/*
 * Status codes returned by PSP in processing CPU-to-PSP commands.
 */
typedef enum cpu2psp_mbox_status {
	/* Success */
	C2P_MBOX_OK				= 0,
	/* Previous command still pending */
	C2P_MBOX_BUSY				= 1,
	/* Previous command aborted */
	C2P_MBOX_ABORTED			= 2,
	/* Generic error */
	C2P_MBOX_ERROR				= 3,
	/* Unsupported command */
	C2P_MBOX_BAD_CMD			= 4,
	/* Invalid arguments for command */
	C2P_MBOX_BAD_PARAMS			= 5,
	/* Target (PSP) not initialized */
	C2P_MBOX_NOT_INIT			= 6,
	/* Failed to map memory */
	C2P_MBOX_MEM_MAP			= 7,
	/* Trustlet error from resume */
	C2P_MBOX_TRUSTLETS_ERR			= 8,
	/* PSB fusing disabled */
	C2P_MBOX_FUSING_NOT_ALLOWED		= 9,
	/* PSB fusing failed */
	C2P_MBOX_FUSING_FAILED			= 10,
	/* Can't fuse after BOOT DONE */
	C2P_MBOX_FUSING_AFTER_POST		= 11,
	/* ROM armor command after BOOT DONE */
	C2P_MBOX_ROM_ARMOR_BAD_MODE		= 12,
	/* PSB disablement failed */
	C2P_MBOX_PSB_DISABLEMENT_FAILED		= 13,
	/* PSB already fused disabled */
	C2P_MBOX_PSB_ALREADY_DISABLED		= 14,
	/* PSB already fused enabled */
	C2P_MBOX_PSB_ALREADY_ENABLED		= 15,
	/* PSB request after previous failure */
	C2P_MBOX_PSB_REQUEST_AFTER_FAILURE	= 16,
	/* Shadow ROM command after BOOT DONE */
	C2P_MBOX_SHADOW_ROM_FAILURE		= 17,
	C2P_MBOX_RESERVED_18			= 18,
	C2P_MBOX_RESERVED_19			= 19,
	/* RPMC not available */
	C2P_MBOX_RPMC_NOT_AVAILABLE		= 20,
	/* GPIO lock requested beyond max GPIO */
	C2P_MBOX_MAX_GPIO_LOCKED		= 21,
	/* Target (PSP) in invalid state */
	C2P_MBOX_INVALID_STATE			= 22,
	/* Command already executed */
	C2P_MBOX_ALREADY_COMPLETE		= 23,
	/* Invalid permissions */
	C2P_MBOX_INVALID_PERMISSIONS		= 24,
	/* Out of resources */
	C2P_MBOX_OUT_OF_RESOURCES		= 25,
	/* Command timed out */
	C2P_MBOX_TIMEOUT			= 26,
} cpu2psp_mbox_status_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_PSP_H */
