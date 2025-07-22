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
 * Set the host to indicate command target should execute.
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

#ifdef __cplusplus
}
#endif

#endif /* _SYS_AMDZEN_PSP_H */
